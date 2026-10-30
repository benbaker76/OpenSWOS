/*
 * mm_client.c -- the matchmaking client. See include/mm_client.h for the shape
 * of the thing and docs/NETPLAY.md section 7 for where it sits.
 *
 * The protocol is text over ENET, one message per packet, "HEADER:content".
 * third_party/matchmaker/OPENSWOS.md lists it; the authority is
 * third_party/matchmaker/common/Message.cpp (the header table) and
 * ServerConnection.cpp (what the reference client does with each one).
 *
 * THE PARSERS ARE THE RISKY PART, so they are written to a rule: never trust a
 * length or a count from the wire. The list payload is
 *
 *     <count>:<name>:<dataLen>:<dataBytes><name>:<dataLen>:<dataBytes>...
 *
 * where dataBytes is RAW and has no terminator -- so a bad dataLen walks off the
 * end of the packet, and a bad count spins. Every step below is bounded by the
 * packet end and every destination is a fixed array with an explicit cap. A list
 * longer than the cap is truncated and the rest ignored; it is a lobby display,
 * and refusing to draw 64 users because a 65th appeared would be worse.
 */
#include "mm_client.h"
#include "netplay_compat.h"   /* NETPLAY_COMPAT: in the VERSION: tag */
#include "project_identity.h" /* PROJECT_NETPLAY_TAG: its name */
#include "world.h"            /* and the football world, if not 96/97 */

#ifdef SWOS_SDL_BUILD

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Which server everything here talks to unless told otherwise. One copy, above
 * the netplay split, because three entry points need it -- the menus, the
 * self-test and the pair test -- and a test that can only reach the default
 * port cannot be run beside another server. */
/* [netplay] matchmaker, split once. s_cfgHost stays empty when nothing is
 * configured, which is what makes the fallback order below readable. */
static char     s_cfgHost[72];
static uint16_t s_cfgPort;
static char     s_cfgShown[80];

void MmClient_SetConfiguredServer(const char *spec)
{
    const char *colon;
    size_t hostLen;

    s_cfgHost[0] = '\0';
    s_cfgPort = 0;
    s_cfgShown[0] = '\0';
    if (!spec || !spec[0])
        return;

    /* An IPv6 literal would need brackets; the split is deliberately the simple
     * one, and a name or a dotted quad is what a player is given. */
    colon = strrchr(spec, ':');
    hostLen = colon ? (size_t)(colon - spec) : strlen(spec);
    if (hostLen == 0 || hostLen >= sizeof s_cfgHost)
        return;
    memcpy(s_cfgHost, spec, hostLen);
    s_cfgHost[hostLen] = '\0';
    if (colon) {
        int v = atoi(colon + 1);
        if (v > 0 && v <= 65535)
            s_cfgPort = (uint16_t)v;
    }
    snprintf(s_cfgShown, sizeof s_cfgShown, "%s:%u", s_cfgHost,
             (unsigned)(s_cfgPort ? s_cfgPort : MM_DEFAULT_PORT));
}

const char *MmClient_ConfiguredServer(void)
{
    return s_cfgShown;
}

const char *MmClient_DefaultHost(void)
{
    const char *h = getenv("SWOS_MM_HOST");
    if (h && h[0])
        return h;
    return s_cfgHost[0] ? s_cfgHost : MM_DEFAULT_HOST;
}

uint16_t MmClient_DefaultPort(void)
{
    const char *p = getenv("SWOS_MM_PORT");
    int v = (p && p[0]) ? atoi(p) : 0;
    if (v > 0 && v <= 65535)
        return (uint16_t)v;
    return s_cfgPort ? s_cfgPort : MM_DEFAULT_PORT;
}

#ifdef SWOS_NETPLAY

#include <enet/enet.h>

/* Not in a browser: its socket layer has no interfaces to list, and a browser's
 * local addresses are no use to a peer -- it only ever talks to the server. */
#if (defined(__unix__) || defined(__APPLE__) || defined(__CYGWIN__)) && !defined(__EMSCRIPTEN__)
#  include <sys/types.h>
#  include <ifaddrs.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  define MM_HAVE_IFADDRS 1
#endif

/* The compatibility tag sent as VERSION:, <PROJECT_NETPLAY_TAG>/<NETPLAY_COMPAT> (OPENSWOS/5,
 * project_identity.h): the server lists
 * a game only to players whose number is the same (Connections.cpp, PlayKey), so a
 * player never joins one the handshake would refuse (include/netplay_compat.h). A world
 * other than 96/97 adds -<world> (MmClient_Connect). */
#define MM_STR_(x) #x
#define MM_STR(x)  MM_STR_(x)
#ifdef SWOS_WASM_BUILD
/* The browser build says so: it can only play through the server, so the server
 * relays any match it is in (Connections.cpp). Still OPENSWOS to everything that
 * compares the prefix. */
#define MM_GAME_ID_DEFAULT PROJECT_NETPLAY_TAG "-WEB/" MM_STR(NETPLAY_COMPAT)
#else
#define MM_GAME_ID_DEFAULT PROJECT_NETPLAY_TAG "/" MM_STR(NETPLAY_COMPAT)
#endif

/* ---- message headers, exactly as common/Message.cpp spells them ---- */
#define H_INFO          "INFO:"
#define H_LOGIN         "LOGIN:"
#define H_VERSION       "VERSION:"
#define H_CREATE        "CREATE:"
#define H_JOIN          "JOIN:"
#define H_LEAVE         "LEAVE:"
#define H_APPROVE       "APPROVE:"
#define H_EJECT         "EJECT:"
#define H_START         "START:"
#define H_PLAYERSACTIVE "PLAYERSACTIVE:"
#define H_GAMESOPEN     "GAMESOPEN:"
#define H_GAMEINFO      "GAMEINFO:"
#define H_USERMESSAGE   "USERMESSAGE:"
#define H_READY         "READY:"
#define H_RELAY         "RELAY:"        /* ours, not upstream's: include/relay_proto.h */
#define H_RELAYFB       "RELAYFB:"      /* ours: the relay if the direct punch fails */

/* How long to wait for the server to complete the ENET connection. The server is
 * usually on this machine; a human needs to see "no answer" rather than a menu
 * that sits there. */
#define MM_CONNECT_TIMEOUT_MS 5000

typedef struct {
    ENetHost   *host;
    ENetPeer   *server;
    MmState     state;
    MmFailure   failure;
    uint32_t    connectStartMs;

    char        userName[MM_NAME_MAX];
    char        gameId[MM_NAME_MAX];

    MmUser      users[MM_MAX_USERS];
    int32_t     numUsers;
    MmGame      games[MM_MAX_GAMES];
    int32_t     numGames;

    MmGameInfo  gameInfo;
    int32_t     inGame;

    char        joinRequest[MM_NAME_MAX];
    int32_t     haveJoinRequest;
    int32_t     removed;

    MmStartInfo start;
    int32_t     haveStart;

    /* RELAY:, which the server sends just before START when the match is to be
     * played through it. Held until START takes it. */
    uint64_t    relayToken;
    int32_t     relayPlayer;
    int32_t     haveRelay;
    /* RELAYFB:, the same for a session to fall back to if punching fails. */
    uint64_t    relayFbToken;
    int32_t     relayFbPlayer;
    int32_t     haveRelayFb;
} MmClient;

static MmClient s_mm;
static int32_t  s_enetUp;

/* ---- small helpers -------------------------------------------------------- */

static void copy_str(char *dst, int32_t cap, const char *src, int32_t len)
{
    if (len < 0)
        len = 0;
    if (len > cap - 1)
        len = cap - 1;
    memcpy(dst, src, (size_t)len);
    dst[len] = '\0';
}

static void send_text(const char *header, const char *content)
{
    char buf[512];
    int n;

    if (!s_mm.server)
        return;
    n = snprintf(buf, sizeof buf, "%s%s", header, content ? content : "");
    if (n < 0)
        return;
    if (n > (int)sizeof buf - 1)
        n = (int)sizeof buf - 1;
    {
        /* RELIABLE: every one of these is a command or an ack, and the server's
         * state machine does not tolerate a lost one. */
        ENetPacket *p = enet_packet_create(buf, (size_t)n, ENET_PACKET_FLAG_RELIABLE);
        if (p)
            enet_peer_send(s_mm.server, 0, p);
        enet_host_flush(s_mm.host);
    }
}

/* Does `data` (length len) start with this header? Returns the content length
 * via *outLen and a pointer to the content, or NULL. */
static const char *match_header(const char *data, size_t len,
                                const char *header, size_t *outLen)
{
    size_t hlen = strlen(header);
    if (len < hlen || memcmp(data, header, hlen) != 0)
        return NULL;
    *outLen = len - hlen;
    return data + hlen;
}

/* Find `ch` in [p, end), or NULL. */
static const char *find_ch(const char *p, const char *end, char ch)
{
    while (p < end) {
        if (*p == ch)
            return p;
        p++;
    }
    return NULL;
}

/* Bounded decimal parse of [p, end). Returns -1 on anything that is not a
 * non-negative number -- the callers treat that as a malformed packet. */
static int32_t parse_int(const char *p, const char *end)
{
    int32_t v = 0;
    if (p >= end)
        return -1;
    while (p < end) {
        if (*p < '0' || *p > '9')
            return -1;
        if (v > 100000000)          /* nothing legitimate is this big */
            return -1;
        v = v * 10 + (*p - '0');
        p++;
    }
    return v;
}

/* ---- the list payload ----------------------------------------------------- */
/* <count>:<name>:<dataLen>:<raw dataLen bytes><name>:... — see the file header.
 * Writes at most `cap` names and returns how many were written. */
static int32_t parse_name_list(const char *p, size_t len,
                               char (*out)[MM_NAME_MAX], int32_t stride, int32_t cap)
{
    const char *end = p + len;
    const char *colon;
    int32_t count, written = 0, i;

    colon = find_ch(p, end, ':');
    if (!colon)
        return 0;
    count = parse_int(p, colon);
    if (count < 0)
        return 0;
    p = colon + 1;

    for (i = 0; i < count; i++) {
        const char *nameEnd, *lenEnd;
        int32_t dataLen;

        nameEnd = find_ch(p, end, ':');
        if (!nameEnd)
            break;
        lenEnd = find_ch(nameEnd + 1, end, ':');
        if (!lenEnd)
            break;
        dataLen = parse_int(nameEnd + 1, lenEnd);
        /* A dataLen that does not fit the packet is a malformed message, not a
         * reason to read past it. */
        if (dataLen < 0 || lenEnd + 1 + dataLen > end)
            break;

        if (written < cap) {
            copy_str((char *)out + (size_t)written * (size_t)stride, MM_NAME_MAX,
                     p, (int32_t)(nameEnd - p));
            written++;
        }
        p = lenEnd + 1 + dataLen;
    }
    return written;
}

/* The open-game list is NOT encoded like the user list, which is easy to miss
 * because both arrive as "a list of names":
 *
 *   PLAYERSACTIVE:  <count>:<name>:<dataLen>:<data>...   (count-prefixed)
 *   GAMESOPEN:      <name>,<name>,                       (comma-separated,
 *                                                         trailing comma, NO count)
 *
 * Server/Connections.cpp builds them in BroadcastActiveUsers and
 * BroadcastOpenGames respectively. Feeding the comma list to the count-prefixed
 * parser yields zero games and looks exactly like "nobody is hosting".
 *
 * Upstream's ParseOpenGames accepts EITHER -- it treats "contains a colon" as
 * the newer count-prefixed form -- so this does too, and for the same reason: a
 * newer server may send the other one. */
static int32_t parse_open_games(const char *p, size_t len,
                                char (*out)[MM_NAME_MAX], int32_t stride, int32_t cap)
{
    const char *end = p + len;
    int32_t written = 0;

    if (find_ch(p, end, ':') != NULL)
        return parse_name_list(p, len, out, stride, cap);

    while (p < end && written < cap) {
        const char *comma = find_ch(p, end, ',');
        const char *stop  = comma ? comma : end;
        if (stop > p) {
            copy_str((char *)out + (size_t)written * (size_t)stride, MM_NAME_MAX,
                     p, (int32_t)(stop - p));
            written++;
        }
        if (!comma)
            break;
        p = comma + 1;
    }
    return written;
}

/* GAMEINFO:<owner>:<joined,...>:<requested,...> */
static void parse_game_info(const char *p, size_t len)
{
    const char *end = p + len;
    const char *c1, *c2;
    MmGameInfo gi;

    memset(&gi, 0, sizeof gi);

    c1 = find_ch(p, end, ':');
    if (!c1) {
        /* No colon at all: not a game we can describe. Leave the old one. */
        return;
    }
    copy_str(gi.owner, sizeof gi.owner, p, (int32_t)(c1 - p));

    c2 = find_ch(c1 + 1, end, ':');
    {
        const char *jStart = c1 + 1;
        const char *jEnd   = c2 ? c2 : end;
        const char *q      = jStart;
        while (q < jEnd && gi.numJoined < MM_MAX_IN_GAME) {
            const char *comma = find_ch(q, jEnd, ',');
            const char *stop  = comma ? comma : jEnd;
            if (stop > q)
                copy_str(gi.joined[gi.numJoined++].name, MM_NAME_MAX, q, (int32_t)(stop - q));
            if (!comma)
                break;
            q = comma + 1;
        }
    }
    if (c2) {
        const char *q = c2 + 1;
        while (q < end && gi.numRequested < MM_MAX_IN_GAME) {
            const char *comma = find_ch(q, end, ',');
            const char *stop  = comma ? comma : end;
            if (stop > q)
                copy_str(gi.requested[gi.numRequested++].name, MM_NAME_MAX, q, (int32_t)(stop - q));
            if (!comma)
                break;
            q = comma + 1;
        }
    }

    s_mm.gameInfo = gi;
    s_mm.inGame = 1;
}

/* ---- addresses on the wire -------------------------------------------------
 * The server does NOT use dotted quads. common/Utils.cpp:
 *
 *     ToString(addr) = std::to_string(addr.host) + ":" + std::to_string(addr.port)
 *
 * i.e. the ENetAddress host field -- a uint32 holding the four octets in NETWORK
 * byte order -- printed as one decimal number. 127.0.0.1 is 16777343.
 *
 * Both directions matter. Sending dotted text in INFO does not fail: the server
 * runs it through atoi and stores 127, and the peer is later told to connect to
 * "127", which is a valid-looking address that goes nowhere. Measured -- the
 * first pair test reported peers at 127, 10, 192 and 169.
 *
 * The octets are extracted BY MEMORY ORDER, not by shifting. The uint32 already
 * holds them big-endian, so `host & 0xFF` is the first octet on a little-endian
 * machine and the LAST one on a big-endian machine. Shifting would work
 * everywhere we currently build and break on the first big-endian host. */

static void addr_u32_to_dotted(uint32_t hostBE, char *out, int32_t cap)
{
    uint8_t o[4];
    memcpy(o, &hostBE, sizeof o);
    snprintf(out, (size_t)cap, "%u.%u.%u.%u",
             (unsigned)o[0], (unsigned)o[1], (unsigned)o[2], (unsigned)o[3]);
}

/* "<uint32>:<port>" -> "a.b.c.d:port". Returns 0 on success. */
static int32_t addr_wire_to_text(const char *p, const char *end, char *out, int32_t cap)
{
    const char *colon = find_ch(p, end, ':');
    int32_t port;
    uint32_t hostBE;
    char dotted[16];
    int64_t hostVal = 0;

    if (!colon)
        return 1;
    /* The host does not fit int32_t (it is a full uint32), so it is parsed here
     * rather than with parse_int. */
    {
        const char *q = p;
        if (q >= colon)
            return 1;
        while (q < colon) {
            if (*q < '0' || *q > '9')
                return 1;
            hostVal = hostVal * 10 + (*q - '0');
            if (hostVal > 0xFFFFFFFFLL)
                return 1;
            q++;
        }
    }
    port = parse_int(colon + 1, end);
    if (port < 0 || port > 65535)
        return 1;

    hostBE = (uint32_t)hostVal;
    addr_u32_to_dotted(hostBE, dotted, (int32_t)sizeof dotted);
    snprintf(out, (size_t)cap, "%s:%d", dotted, port);
    return 0;
}

/* START:<playerNumber>,<peerName>,<addr>,<addr>,... */
static void parse_start(const char *p, size_t len)
{
    const char *end = p + len;
    const char *c;
    MmStartInfo st;
    int32_t field = 0;

    memset(&st, 0, sizeof st);
    st.playerNumber = 0;

    while (p <= end) {
        c = find_ch(p, end, ',');
        {
            const char *stop = c ? c : end;
            int32_t n = (int32_t)(stop - p);
            if (field == 0) {
                st.playerNumber = parse_int(p, stop);
            } else if (field == 1) {
                copy_str(st.peerName, sizeof st.peerName, p, n);
            } else if (st.numAddrs < MM_MAX_PEER_ADDRS && n > 0) {
                /* Decoded here, once, so everything downstream (the menus, the
                 * log, p2p_link) sees a normal dotted address. A field we cannot
                 * decode is dropped rather than passed on as junk. */
                if (addr_wire_to_text(p, stop, st.addrs[st.numAddrs],
                                      MM_ADDR_TEXT_MAX) == 0)
                    st.numAddrs++;
            }
            field++;
            if (!c)
                break;
            p = c + 1;
        }
    }

    /* The port our own ENET socket is bound to. p2p_link binds its own socket for
     * the match, but the peer's NAT mapping was made against THIS one, so it is
     * worth knowing and is reported for the log. */
    {
        ENetAddress local;
        if (s_mm.host && enet_socket_get_address(s_mm.host->socket, &local) == 0)
            st.localPort = local.port;
    }

    if (s_mm.server) {
        char dotted[16];
        addr_u32_to_dotted(s_mm.server->address.host, dotted, (int32_t)sizeof dotted);
        snprintf(st.serverAddr, sizeof st.serverAddr, "%s:%u", dotted, (unsigned)s_mm.server->address.port);
    }
    /* Relayed: the peer is the server. Only if RELAY: named the player number
     * START did -- a token for one seat is no good in the other. */
    if (s_mm.haveRelay && s_mm.relayPlayer == st.playerNumber && s_mm.server) {
        char dotted[16];
        addr_u32_to_dotted(s_mm.server->address.host, dotted, (int32_t)sizeof dotted);
        snprintf(st.relayAddr, sizeof st.relayAddr, "%s:%u", dotted,
                 (unsigned)s_mm.server->address.port);
        st.relay      = 1;
        st.relayToken = s_mm.relayToken;
    }
    s_mm.haveRelay = 0;
    if (s_mm.haveRelayFb && s_mm.relayFbPlayer == st.playerNumber && !st.relay)
        st.relayFallbackToken = s_mm.relayFbToken;
    s_mm.haveRelayFb = 0;

    s_mm.start = st;
    s_mm.haveStart = 1;
}

/* RELAY:<token as 16 hex digits>,<player> -- and RELAYFB:, the same. 0 = parsed. */
static int32_t parse_token_player(const char *p, size_t len, uint64_t *tokenOut, int32_t *playerOut)
{
    const char *end = p + len;
    uint64_t token = 0;
    int32_t digits = 0;

    for (; p < end && *p != ','; p++, digits++) {
        char c = *p;
        int32_t v = (c >= '0' && c <= '9') ? c - '0'
                  : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                  : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (v < 0 || digits >= 16)
            return 1;
        token = (token << 4) | (uint64_t)v;
    }
    if (p >= end || digits == 0 || token == 0)
        return 1;
    *tokenOut  = token;
    *playerOut = parse_int(p + 1, end);
    return 0;
}

static void parse_relay(const char *p, size_t len)
{
    if (parse_token_player(p, len, &s_mm.relayToken, &s_mm.relayPlayer) == 0)
        s_mm.haveRelay = 1;
}

/* ---- the INFO message: our own candidate addresses ------------------------ */
/* The server already knows the address it SEES us on and puts that first in
 * START. What it cannot know is our address on a LAN, which is what lets two
 * players behind the same router reach each other directly. That is what this
 * reports. Loopback is included deliberately: two instances on one machine is
 * exactly the local-server case this was built for. */
static void send_local_addresses(void)
{
    char msg[512];
    size_t used = 0;
    uint16_t port = 0;
    ENetAddress local;

    if (s_mm.host && enet_socket_get_address(s_mm.host->socket, &local) == 0)
        port = local.port;

    msg[0] = '\0';

#ifdef MM_HAVE_IFADDRS
    {
        struct ifaddrs *addrs = NULL, *ifa;
        if (getifaddrs(&addrs) == 0) {
            for (ifa = addrs; ifa; ifa = ifa->ifa_next) {
                char text[32];
                struct sockaddr_in *in;
                uint32_t hostBE;
                int n;

                if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
                    continue;
                in = (struct sockaddr_in *)(void *)ifa->ifa_addr;
                /* s_addr is already the four octets in network byte order --
                 * exactly what ENetAddress.host holds and what the server's
                 * ToString prints. Send that number, NOT a dotted quad: the
                 * server atoi's this field (see addr_wire_to_text). */
                memcpy(&hostBE, &in->sin_addr, sizeof hostBE);
                n = snprintf(text, sizeof text, "%lu:%u",
                             (unsigned long)hostBE, (unsigned)port);
                if (n < 0 || n >= (int)sizeof text)
                    continue;
                if (used + strlen(text) + 2 >= sizeof msg)
                    break;
                if (used)
                    msg[used++] = ',';
                memcpy(msg + used, text, strlen(text));
                used += strlen(text);
                msg[used] = '\0';
            }
            freeifaddrs(addrs);
        }
    }
#endif

    if (!used) {
        /* Loopback in the same numeric form: 127.0.0.1 big-endian = 16777343. */
        uint32_t loopback;
        uint8_t o[4] = { 127, 0, 0, 1 };
        memcpy(&loopback, o, sizeof loopback);
        snprintf(msg, sizeof msg, "%lu:%u", (unsigned long)loopback, (unsigned)port);
    }

    send_text(H_INFO, msg);
}

/* ---- connection ----------------------------------------------------------- */

static void reset_lobby(void)
{
    s_mm.numUsers = 0;
    s_mm.numGames = 0;
    s_mm.inGame = 0;
    memset(&s_mm.gameInfo, 0, sizeof s_mm.gameInfo);
    s_mm.haveJoinRequest = 0;
    s_mm.removed = 0;
    s_mm.haveStart = 0;
    s_mm.haveRelay = 0;
    s_mm.haveRelayFb = 0;
}

static void close_socket(void)
{
    if (s_mm.host) {
        enet_host_destroy(s_mm.host);
        s_mm.host = NULL;
    }
    s_mm.server = NULL;
}

static void fail(MmFailure why)
{
    close_socket();
    s_mm.state = MM_FAILED;
    s_mm.failure = why;
}

int32_t MmClient_Connect(const char *host, uint16_t port,
                         const char *userName, const char *gameId)
{
    ENetAddress addr;

    MmClient_Disconnect();
    memset(&s_mm, 0, sizeof s_mm);

    if (!s_enetUp) {
        if (enet_initialize() != 0) {
            s_mm.state = MM_FAILED;
            s_mm.failure = MM_FAIL_NO_SOCKET;
            return 1;
        }
        s_enetUp = 1;
    }

    copy_str(s_mm.userName, sizeof s_mm.userName,
             userName && userName[0] ? userName : "PLAYER", MM_NAME_MAX - 1);
    if (gameId && gameId[0]) {
        copy_str(s_mm.gameId, sizeof s_mm.gameId, gameId, MM_NAME_MAX - 1);
    } else if (World_Current() == WORLD_9697) {
        copy_str(s_mm.gameId, sizeof s_mm.gameId, MM_GAME_ID_DEFAULT, MM_NAME_MAX - 1);
    } else {
        /* Another football world plays only its own: OPENSWOS/<n>-<world>, which the
         * server's PlayKey (everything after the '/') keeps apart from 96/97's. */
        char id[MM_NAME_MAX];
        snprintf(id, sizeof id, "%s-%s", MM_GAME_ID_DEFAULT, World_Tag());
        copy_str(s_mm.gameId, sizeof s_mm.gameId, id, MM_NAME_MAX - 1);
    }

    if (enet_address_set_host(&addr, host && host[0] ? host : MM_DEFAULT_HOST) != 0) {
        s_mm.state = MM_FAILED;
        s_mm.failure = MM_FAIL_BAD_ADDRESS;
        return 1;
    }
    addr.port = port ? port : MM_DEFAULT_PORT;

    /* One outgoing connection, one channel -- everything is on channel 0. */
    s_mm.host = enet_host_create(NULL, 1, 1, 0, 0);
    if (!s_mm.host) {
        s_mm.state = MM_FAILED;
        s_mm.failure = MM_FAIL_NO_SOCKET;
        return 1;
    }

    s_mm.server = enet_host_connect(s_mm.host, &addr, 1, 0);
    if (!s_mm.server) {
        fail(MM_FAIL_NO_SOCKET);
        return 1;
    }

    s_mm.state = MM_CONNECTING;
    s_mm.failure = MM_FAIL_NONE;
    s_mm.connectStartMs = enet_time_get();
    return 0;
}

void MmClient_Disconnect(void)
{
    if (s_mm.server && s_mm.state != MM_FAILED) {
        /* Ask politely, then go. The server drops us on the ENET disconnect
         * anyway, so there is nothing to wait for. */
        enet_peer_disconnect(s_mm.server, 0);
        if (s_mm.host)
            enet_host_flush(s_mm.host);
    }
    close_socket();
    reset_lobby();
    s_mm.state = MM_IDLE;
    s_mm.failure = MM_FAIL_NONE;
}

static void handle_packet(const char *data, size_t len)
{
    const char *body;
    size_t blen;

    /* Order matters only in that PLAYERSACTIVE must be tested before any prefix
     * of it would match; the headers are distinct, so a simple chain is fine. */
    if ((body = match_header(data, len, H_PLAYERSACTIVE, &blen)) != NULL) {
        s_mm.numUsers = parse_name_list(body, blen, (char (*)[MM_NAME_MAX])s_mm.users,
                                        (int32_t)sizeof(MmUser), MM_MAX_USERS);
        return;
    }
    if ((body = match_header(data, len, H_GAMESOPEN, &blen)) != NULL) {
        s_mm.numGames = parse_open_games(body, blen, (char (*)[MM_NAME_MAX])s_mm.games,
                                         (int32_t)sizeof(MmGame), MM_MAX_GAMES);
        return;
    }
    if ((body = match_header(data, len, H_GAMEINFO, &blen)) != NULL) {
        parse_game_info(body, blen);
        return;
    }
    if ((body = match_header(data, len, H_RELAY, &blen)) != NULL) {
        parse_relay(body, blen);
        return;
    }
    if ((body = match_header(data, len, H_RELAYFB, &blen)) != NULL) {
        if (parse_token_player(body, blen, &s_mm.relayFbToken, &s_mm.relayFbPlayer) == 0)
            s_mm.haveRelayFb = 1;
        return;
    }
    if ((body = match_header(data, len, H_START, &blen)) != NULL) {
        parse_start(body, blen);
        return;
    }
    if ((body = match_header(data, len, H_LOGIN, &blen)) != NULL) {
        /* The server echoing LOGIN: is the "you are in" signal. */
        s_mm.state = MM_CONNECTED;
        return;
    }
    if ((body = match_header(data, len, H_CREATE, &blen)) != NULL) {
        s_mm.inGame = 1;
        return;
    }
    if ((body = match_header(data, len, H_JOIN, &blen)) != NULL) {
        /* Your own name back = your request was accepted by the server and is now
         * waiting on the host. Anyone else's = they want into YOUR game. */
        char who[MM_NAME_MAX];
        copy_str(who, sizeof who, body, (int32_t)blen);
        if (strcmp(who, s_mm.userName) != 0) {
            copy_str(s_mm.joinRequest, sizeof s_mm.joinRequest, body, (int32_t)blen);
            s_mm.haveJoinRequest = 1;
        }
        return;
    }
    if ((body = match_header(data, len, H_APPROVE, &blen)) != NULL) {
        /* Upstream's client calls StartGame() the moment anyone is approved,
         * because a game here is always two players. Same thing. */
        send_text(H_START, "");
        return;
    }
    if ((body = match_header(data, len, H_LEAVE, &blen)) != NULL) {
        s_mm.inGame = 0;
        memset(&s_mm.gameInfo, 0, sizeof s_mm.gameInfo);
        return;
    }
    if ((body = match_header(data, len, H_EJECT, &blen)) != NULL) {
        s_mm.inGame = 0;
        memset(&s_mm.gameInfo, 0, sizeof s_mm.gameInfo);
        s_mm.removed = 1;
        return;
    }
    /* INFO:, USERMESSAGE:, READY:, VERSION: and anything we do not know are
     * ignored rather than treated as an error -- the server is allowed to be
     * newer than us. */
}

/* Service the host for up to `timeoutMs`, dispatching everything that arrives.
 *
 * The timeout is passed to ENET rather than slept around, because
 * enet_host_service(host, NULL, ms) DISCARDS events -- "if event == NULL then no
 * events will be delivered" (protocol.c). Waiting that way once cost a
 * self-test: login worked, then CREATE's GAMEINFO/GAMESOPEN replies were eaten
 * by the wait and it looked as though the server had ignored the message. There
 * is one service call, it always has a real event, and it is this one. */
static void mm_service(uint32_t timeoutMs)
{
    ENetEvent ev;

    if (!s_mm.host)
        return;

    if (s_mm.state == MM_CONNECTING &&
        enet_time_get() - s_mm.connectStartMs > MM_CONNECT_TIMEOUT_MS &&
        !s_mm.server->state) {
        fail(MM_FAIL_NO_ANSWER);
        return;
    }

    while (enet_host_service(s_mm.host, &ev, timeoutMs) > 0) {
        timeoutMs = 0;      /* wait at most once; then drain what is queued */
        switch (ev.type) {
        case ENET_EVENT_TYPE_CONNECT:
            /* The order here is the reference client's and the server's state
             * machine depends on it: the tag, then who we are reachable at, then
             * who we are. */
            send_text(H_VERSION, s_mm.gameId);
            send_local_addresses();
            {
                char login[MM_NAME_MAX * 2];
                /* LOGIN:<name>:<userData>. We send no user data; the colon still
                 * has to be there, because the server splits on it. */
                snprintf(login, sizeof login, "%s:", s_mm.userName);
                send_text(H_LOGIN, login);
            }
            break;

        case ENET_EVENT_TYPE_RECEIVE:
            handle_packet((const char *)ev.packet->data, ev.packet->dataLength);
            enet_packet_destroy(ev.packet);
            break;

        case ENET_EVENT_TYPE_DISCONNECT:
            fail(s_mm.state == MM_CONNECTED ? MM_FAIL_LOST : MM_FAIL_NO_ANSWER);
            return;

        default:
            break;
        }
    }

    /* A connect that never completes leaves the peer in a non-connected state
     * with no event to tell us; the timeout above is the only way out. */
    if (s_mm.state == MM_CONNECTING && s_mm.server &&
        enet_time_get() - s_mm.connectStartMs > MM_CONNECT_TIMEOUT_MS)
        fail(MM_FAIL_NO_ANSWER);
}

void MmClient_Poll(void)
{
    /* From the game loop: never block. */
    mm_service(0);
}

/* ---- reads ---------------------------------------------------------------- */

MmState   MmClient_State(void)   { return s_mm.state; }
MmFailure MmClient_Failure(void) { return s_mm.failure; }
const char *MmClient_UserName(void) { return s_mm.userName; }

const char *MmClient_FailureText(void)
{
    switch (s_mm.failure) {
    case MM_FAIL_NONE:        return "";
    case MM_FAIL_UNAVAILABLE: return "NETPLAY NOT BUILT IN";
    case MM_FAIL_BAD_ADDRESS: return "BAD SERVER ADDRESS";
    case MM_FAIL_NO_SOCKET:   return "COULD NOT OPEN SOCKET";
    case MM_FAIL_NO_ANSWER:   return "NO ANSWER FROM SERVER";
    case MM_FAIL_LOST:        return "LOST THE SERVER";
    }
    return "";
}

const MmUser *MmClient_Users(int32_t *count)
{
    if (count)
        *count = s_mm.numUsers;
    return s_mm.users;
}

const MmGame *MmClient_OpenGames(int32_t *count)
{
    if (count)
        *count = s_mm.numGames;
    return s_mm.games;
}

const MmGameInfo *MmClient_GameInfo(void)
{
    return s_mm.inGame ? &s_mm.gameInfo : NULL;
}

int32_t MmClient_TakeJoinRequest(char *name, int32_t len)
{
    if (!s_mm.haveJoinRequest)
        return 0;
    s_mm.haveJoinRequest = 0;
    if (name && len > 0)
        copy_str(name, len, s_mm.joinRequest, (int32_t)strlen(s_mm.joinRequest));
    return 1;
}

int32_t MmClient_TakeRemoved(void)
{
    int32_t was = s_mm.removed;
    s_mm.removed = 0;
    return was;
}

const MmStartInfo *MmClient_StartInfo(void)
{
    return s_mm.haveStart ? &s_mm.start : NULL;
}

void MmClient_ClearStart(void)
{
    s_mm.haveStart = 0;
}

/* ---- actions -------------------------------------------------------------- */

static int32_t require_connected(void)
{
    return s_mm.state == MM_CONNECTED && s_mm.server;
}

int32_t MmClient_CreateGame(void)
{
    if (!require_connected())
        return 1;
    /* CREATE's content is "<minPlayers>,<maxPlayers>" and nothing else.
     * Server/User.cpp: it splits on ',', requires EXACTLY two parts, and
     * std::stoi's both -- anything else throws and the game is silently not
     * created, with no reply at all. Sending a descriptive string here (the
     * obvious guess) looks exactly like the server ignoring you.
     *
     * SWOS is two players, so both are 2. */
    send_text(H_CREATE, "2,2");
    return 0;
}

int32_t MmClient_JoinGame(const char *owner)
{
    if (!require_connected() || !owner || !owner[0])
        return 1;
    send_text(H_JOIN, owner);
    return 0;
}

int32_t MmClient_Approve(const char *player)
{
    if (!require_connected() || !player || !player[0])
        return 1;
    send_text(H_APPROVE, player);
    return 0;
}

int32_t MmClient_Eject(const char *player)
{
    if (!require_connected() || !player || !player[0])
        return 1;
    send_text(H_EJECT, player);
    return 0;
}

int32_t MmClient_LeaveGame(void)
{
    if (!require_connected())
        return 1;
    send_text(H_LEAVE, "");
    return 0;
}

/* ---- self-test ------------------------------------------------------------ */
/* Drives a real conversation with a real server, because the parsers are the
 * part worth testing and only the server produces the messages they read.
 * Needs scripts/run_matchmaker.sh running; that is the point, not a limitation. */

/* Pump until `done` or the deadline. Returns 1 if `done` came true. */
static int32_t pump_until(int32_t (*done)(void), uint32_t ms)
{
    uint32_t start = enet_time_get();
    while (enet_time_get() - start < ms) {
        mm_service(10);          /* blocks up to 10ms AND delivers -- see mm_service */
        if (done())
            return 1;
        if (s_mm.state == MM_FAILED)
            return 0;
    }
    return done();
}

static int32_t is_connected(void)  { return s_mm.state == MM_CONNECTED; }
static int32_t have_users(void)    { return s_mm.numUsers > 0; }
static int32_t in_a_game(void)     { return s_mm.inGame != 0; }
static int32_t out_of_game(void)   { return s_mm.inGame == 0; }

/* Our own game, listed by name in GAMESOPEN. */
static int32_t game_listed(void)
{
    int32_t i;
    for (i = 0; i < s_mm.numGames; i++)
        if (strcmp(s_mm.games[i].owner, s_mm.userName) == 0)
            return 1;
    return 0;
}

/* Gone from GAMESOPEN again, after we leave. */
static int32_t game_unlisted(void) { return !game_listed(); }

int32_t MmClient_SelfTest(const char *host, uint16_t port)
{
    int32_t failed = 0;
    int32_t i, n = 0;

    printf("[matchmaker] self-test against %s:%u\n",
           host && host[0] ? host : MM_DEFAULT_HOST,
           (unsigned)(port ? port : MM_DEFAULT_PORT));

    if (MmClient_Connect(host, port, "SELFTEST", NULL) != 0) {
        printf("  FAIL connect: %s\n", MmClient_FailureText());
        return 1;
    }
    if (!pump_until(is_connected, 6000)) {
        printf("  FAIL login: %s\n",
               s_mm.failure != MM_FAIL_NONE ? MmClient_FailureText() : "no LOGIN reply");
        printf("  is the server running?  scripts/run_matchmaker.sh\n");
        MmClient_Disconnect();
        return 1;
    }
    printf("  ok   connected and logged in as %s\n", MmClient_UserName());

    /* The server broadcasts the user list on every change, so our own arrival
     * must produce one containing us. */
    if (!pump_until(have_users, 3000)) {
        printf("  FAIL no PLAYERSACTIVE received\n");
        failed++;
    } else {
        const MmUser *u = MmClient_Users(&n);
        int32_t sawSelf = 0;
        for (i = 0; i < n; i++)
            if (strcmp(u[i].name, MmClient_UserName()) == 0)
                sawSelf = 1;
        printf("  %s user list: %d online%s\n", sawSelf ? "ok  " : "FAIL",
               n, sawSelf ? ", including us" : ", WITHOUT us");
        if (!sawSelf)
            failed++;
    }

    if (MmClient_CreateGame() != 0) {
        printf("  FAIL create was refused locally\n");
        failed++;
    } else if (!pump_until(in_a_game, 3000)) {
        printf("  FAIL no CREATE reply -- the server rejected it silently\n");
        failed++;
    } else {
        printf("  ok   created a game\n");
    }

    /* The game we just made must come back in the open list, BY NAME. Checking
     * only that the count is non-zero would have passed while the list was being
     * parsed with the wrong encoding and coming back empty -- which is exactly
     * what happened. Note GAMESOPEN uses a different encoding to the user list
     * (see parse_open_games), so this is a genuinely separate parser to cover.
     *
     * There is no GAMEINFO after a bare create: the server only sends that from
     * SendInfoToAll(), i.e. once somebody asks to join. So this does not wait
     * for one, and MmClient_GameInfo() being empty here is correct. */
    if (!pump_until(game_listed, 3000)) {
        MmClient_OpenGames(&n);
        printf("  FAIL our game is not in GAMESOPEN (%d listed)\n", n);
        failed++;
    } else {
        printf("  ok   open games: %d, ours among them\n", n);
    }

    if (MmClient_LeaveGame() != 0) {
        printf("  FAIL leave was refused locally\n");
        failed++;
    } else if (!pump_until(out_of_game, 3000)) {
        printf("  FAIL still in a game after LEAVE\n");
        failed++;
    } else if (!pump_until(game_unlisted, 3000)) {
        printf("  FAIL left, but the game is still advertised in GAMESOPEN\n");
        failed++;
    } else {
        printf("  ok   left the game, and it is no longer listed\n");
    }

    MmClient_Disconnect();
    if (s_mm.state != MM_IDLE) {
        printf("  FAIL not idle after disconnect\n");
        failed++;
    } else {
        printf("  ok   disconnected\n");
    }

    printf("[matchmaker] self-test %s\n", failed ? "FAILED" : "passed");
    fflush(stdout);
    return failed != 0;
}

/* ---- the two-client test: the part that actually matters ------------------ */
/* One process hosts, another joins; the host approves and BOTH must receive
 * START with the other's addresses. That is the handoff into a match, and it is
 * the only part of the protocol a single client cannot exercise at all.
 *
 *   term 1:  bin/SWOS.x86_64 --matchmaker-pair=host
 *   term 2:  bin/SWOS.x86_64 --matchmaker-pair=join
 *
 * scripts/matchmaker_pair_test.sh runs both and compares what they got. */

static int32_t have_start(void)       { return s_mm.haveStart; }
static int32_t have_join_request(void){ return s_mm.haveJoinRequest; }

static int32_t host_is_listed(void)
{
    int32_t i;
    for (i = 0; i < s_mm.numGames; i++)
        if (strcmp(s_mm.games[i].owner, "MMHOST") == 0)
            return 1;
    return 0;
}

static void print_start(const MmStartInfo *st)
{
    int32_t i;
    printf("  START: player %d, peer '%s', our port %u, %d address(es)\n",
           st->playerNumber, st->peerName, (unsigned)st->localPort, st->numAddrs);
    for (i = 0; i < st->numAddrs; i++)
        printf("    addr[%d] %s%s\n", i, st->addrs[i],
               i == 0 ? "   (as the SERVER saw the peer)" : "   (peer-reported)");
    /* Whether the server will play this match itself. The only way to tell from
     * outside that a server was started with --relay: a relaying one sends RELAY:
     * just before START (docs/NETPLAY_SETUP.md 6). */
    if (st->relayToken)
        printf("  RELAYED through %s, token %016llx\n",
               st->relayAddr, (unsigned long long)st->relayToken);
    else
        printf("  not relayed: the players connect to each other directly\n");
}

int32_t MmClient_PairTest(const char *role, const char *host, uint16_t port)
{
    int32_t isHost = (role && strcmp(role, "host") == 0);
    const char *me = isHost ? "MMHOST" : "MMJOIN";
    const MmStartInfo *st;
    int32_t failed = 0;

    printf("[matchmaker] pair test, role=%s, as %s\n", isHost ? "host" : "join", me);

    if (MmClient_Connect(host, port, me, NULL) != 0 || !pump_until(is_connected, 6000)) {
        printf("  FAIL could not log in: %s\n", MmClient_FailureText());
        printf("  is the server running?  scripts/run_matchmaker.sh\n");
        MmClient_Disconnect();
        return 1;
    }
    printf("  ok   logged in\n");

    if (isHost) {
        char who[MM_NAME_MAX];
        if (MmClient_CreateGame() != 0 || !pump_until(in_a_game, 3000)) {
            printf("  FAIL could not create a game\n");
            MmClient_Disconnect();
            return 1;
        }
        printf("  ok   hosting, waiting up to 60s for a joiner...\n");
        fflush(stdout);

        if (!pump_until(have_join_request, 60000)) {
            printf("  FAIL nobody asked to join\n");
            MmClient_Disconnect();
            return 1;
        }
        MmClient_TakeJoinRequest(who, sizeof who);
        printf("  ok   '%s' asked to join; approving\n", who);
        if (MmClient_Approve(who) != 0) {
            printf("  FAIL approve refused locally\n");
            MmClient_Disconnect();
            return 1;
        }
    } else {
        printf("  ok   waiting up to 60s for a host to appear...\n");
        fflush(stdout);
        if (!pump_until(host_is_listed, 60000)) {
            printf("  FAIL no MMHOST game in the open list\n");
            MmClient_Disconnect();
            return 1;
        }
        printf("  ok   found MMHOST's game; asking to join\n");
        if (MmClient_JoinGame("MMHOST") != 0) {
            printf("  FAIL join refused locally\n");
            MmClient_Disconnect();
            return 1;
        }
    }

    /* Both sides must get a START. The host's arrives because it approved; the
     * joiner's because the host did. */
    if (!pump_until(have_start, 30000)) {
        printf("  FAIL no START received\n");
        failed++;
    } else {
        st = MmClient_StartInfo();
        print_start(st);
        if (st->playerNumber != (isHost ? 1 : 2)) {
            printf("  FAIL expected to be player %d, got %d\n",
                   isHost ? 1 : 2, st->playerNumber);
            failed++;
        }
        if (strcmp(st->peerName, isHost ? "MMJOIN" : "MMHOST") != 0) {
            printf("  FAIL expected peer '%s', got '%s'\n",
                   isHost ? "MMJOIN" : "MMHOST", st->peerName);
            failed++;
        }
        if (st->numAddrs < 1) {
            printf("  FAIL no peer addresses -- nothing to connect to\n");
            failed++;
        }
    }

    MmClient_Disconnect();
    printf("[matchmaker] pair test (%s) %s\n", isHost ? "host" : "join",
           failed ? "FAILED" : "passed");
    fflush(stdout);
    return failed != 0;
}

#else  /* !SWOS_NETPLAY ------------------------------------------------------ */

int32_t MmClient_Connect(const char *host, uint16_t port,
                         const char *userName, const char *gameId)
{
    (void)host; (void)port; (void)userName; (void)gameId;
    return 1;
}
void MmClient_Poll(void) {}
void MmClient_Disconnect(void) {}
MmState MmClient_State(void) { return MM_FAILED; }
MmFailure MmClient_Failure(void) { return MM_FAIL_UNAVAILABLE; }
const char *MmClient_FailureText(void) { return "NETPLAY NOT BUILT IN"; }
const char *MmClient_UserName(void) { return ""; }
const MmUser *MmClient_Users(int32_t *count) { if (count) *count = 0; return NULL; }
const MmGame *MmClient_OpenGames(int32_t *count) { if (count) *count = 0; return NULL; }
const MmGameInfo *MmClient_GameInfo(void) { return NULL; }
int32_t MmClient_CreateGame(void) { return 1; }
int32_t MmClient_JoinGame(const char *owner) { (void)owner; return 1; }
int32_t MmClient_Approve(const char *player) { (void)player; return 1; }
int32_t MmClient_Eject(const char *player) { (void)player; return 1; }
int32_t MmClient_LeaveGame(void) { return 1; }
int32_t MmClient_TakeJoinRequest(char *name, int32_t len) { (void)name; (void)len; return 0; }
int32_t MmClient_TakeRemoved(void) { return 0; }
const MmStartInfo *MmClient_StartInfo(void) { return NULL; }
void MmClient_ClearStart(void) {}
int32_t MmClient_SelfTest(const char *host, uint16_t port)
{
    (void)host; (void)port;
    printf("[matchmaker] built without netplay (make NETPLAY=1)\n");
    return 1;
}
int32_t MmClient_PairTest(const char *role, const char *host, uint16_t port)
{
    (void)role; (void)host; (void)port;
    printf("[matchmaker] built without netplay (make NETPLAY=1)\n");
    return 1;
}

#endif /* SWOS_NETPLAY */

#endif /* SWOS_SDL_BUILD */
