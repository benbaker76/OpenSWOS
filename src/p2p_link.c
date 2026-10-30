/*
 * p2p_link.c -- the connection two players make before a netplay match (SDL builds).
 * docs/NETPLAY.md, "The MULTIPLAYER menu". The interface, and what the link is for,
 * are in include/p2p_link.h.
 *
 * The protocol is six packets over UDP, all of them tiny and none of them retried in
 * order -- every one is either repeated until it is answered (HELLO), or harmless to
 * lose (PING, PONG, BYE):
 *
 *   HELLO   joiner -> host    "this is the game I would play"
 *   WELCOME host  -> joiner   "so would I"   (also resent when a HELLO repeats)
 *   REFUSE  host  -> joiner   "no, because"  (a reason byte)
 *   PING    either way        the sender's own clock, to be echoed
 *   PONG    the echo          so the sender alone measures the round trip
 *   BYE     either way        leaving the menu
 *
 * Every packet starts with the same 8 bytes -- "SWUN", the protocol version, the type
 * -- so a peer of a future version is recognised as one rather than ignored, and told
 * so. Multi-byte fields are big-endian through endian_helpers.h, the byte order the
 * rest of this port's wire formats use.
 *
 * The socket is non-blocking and polled from the menu's per-frame draw; nothing here
 * ever waits, and nothing here runs during a match.
 */
#include "p2p_link.h"

#ifdef SWOS_SDL_BUILD

#include <SDL.h>            /* SDL_GetTicks, SDL_Delay (the self-test) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "build_info.h"      /* Build_Id: the handshake logs builds */
#include "netplay_compat.h"  /* NETPLAY_COMPAT: and compares this */
#include "world.h"           /* and the football world */

#ifdef SWOS_NETPLAY

#include "endian_helpers.h"
#include "sim_state.h"
#include "display.h"        /* _screen_width, SCREEN_WIDTH, PITCH_VIEW_MAX_WIDTH */
#include "relay_proto.h"
#include "netplay_netem.h"     /* SWOS_NETEM_LINK: the emulated network in front of the link too */

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef SWOS_WASM_BUILD
#include <emscripten.h>         /* emscripten_random */
#endif

/* Raise this when a packet's meaning changes; peers then refuse each other by version
 * instead of misreading a field. 2: messages travel on numbered channels, with a
 * generation, so a peer can replace what it sent (the lobby's selections). 3: every
 * packet ends in a checksum (wire_send), and the match's GGPO packets carry a keyed one
 * whose key the lobby's START hands over. */
enum { kProtocol = 5 };      /* 4: the setup carries the build (2026-09-27); 5: and
                              * NETPLAY_COMPAT, compared in its place (2026-10-25) */

enum {
    PKT_HELLO = 1, PKT_WELCOME = 2, PKT_REFUSE = 3,
    PKT_PING  = 4, PKT_PONG    = 5, PKT_BYE    = 6,
    PKT_DELAY = 7,              /* my proposed input delay, in frames */
    PKT_BLOB  = 8,              /* one chunk of a message */
    PKT_BLOB_ACK = 9,           /* which chunks of it the receiver has */
};

enum {
    kHeaderLen  = 8,
    kSetupLen   = 32,   /* header + the setup fields + the build id + compat, flags */
    kRefuseLen  = 9,
    kPingLen    = 12,
    kDelayLen   = 9,
    /* The READ buffer, so it has to fit the LARGEST packet, not the typical
     * one. It was 64 -- ample for the control packets, and 16 bytes of header
     * plus 1024 of payload short of a setup chunk. recvfrom truncates silently,
     * so every chunk lost all but its first 48 bytes while the transfer still
     * completed and acked: right length, full bitmap, wrong contents. A message
     * chunk defines this now: header, kChunkHeader, then kBlobChunk of payload
     * (both declared below and used here). */
    kPacketMax  = 8 + 12 + 1024 + 4,   /* + the checksum trailer (wire_send) */
};

enum {
    kHelloIntervalMs = 500,     /* until the host answers */
    kPingIntervalMs  = 1000,
    kPingWarmupMs    = 100,     /* until kDelaySamples round trips are in */
    kDelaySamples    = 4,       /* round trips averaged before the delay is proposed */
    kJoinTimeoutMs   = 15000,   /* no answer at all: wrong address, port or firewall */
    kRelayFallbackMs = 5000,    /* no round trip direct by now: play through the server */
    kQuietTimeoutMs  = 6000,    /* connected, then nothing heard */
    kMaxReadsPerPoll = 32,      /* a frame's worth; the rest waits for the next poll */
    kJoinPortTries   = 10,      /* two peers on one machine need different local ports */
    kMaxCandidates   = 8,       /* what the matchmaker hands over (MM_MAX_PEER_ADDRS) */
    kDelayIntervalMs = 250,     /* re-send the proposal until the peer's is known (UDP) */
    kDelayMax        = 8,       /* more input lag than this is worse than the rollback */

    /* Messages: the lobby's selections, the match setup. Too big for one
     * datagram, so they go as numbered chunks with the receiver naming what it
     * still lacks.
     *
     * 1024 bytes of payload keeps a chunk under the smallest MTU anyone
     * realistically has once IP and UDP headers are on it; going to the
     * "normal" 1472 saves a third of the packets and loses the match on any
     * path that fragments. */
    kChunkHeader     = 12,      /* channel, pad, generation, total length, index, count */
    kBlobChunk       = 1024,
    kBlobMaxChunks   = 128,                        /* 128 KB, well over what we send */
    kBlobMax         = kBlobChunk * kBlobMaxChunks,
    kBlobBitmapBytes = kBlobMaxChunks / 8,
    kBlobResendMs    = 120,     /* re-send whatever is still unacked */
};

/* What both sides must agree on, or the very first frame of a match diverges. The
 * state hash is SimState_LayoutHash (docs/ROLLBACK.md): the shape of the registry a
 * rollback saves, which differs whenever the build's match state does. */
typedef struct {
    uint32_t stateHi, stateLo;
    uint16_t screenWidth;
    uint8_t  gameplay;          /* bit 0 DOS gameplay, 1 arcade, 2 the original fire-hold lockout */
    char     build[9];          /* Build_Id()'s first 8: the commit, for the log */
    uint16_t compat;            /* NETPLAY_COMPAT: what is compared */
    uint8_t  dirty;             /* built from uncommitted changes: plays only its commit */
    uint8_t  world;             /* the football world (include/world.h); 0 = 96/97, which
                                 * is what the byte always held before worlds */
} Setup;

/* The message being sent on one channel. Sending again replaces it: the new one
 * gets the next generation and whatever of the old one was still unacked is
 * dropped, because the receiver only wants the newest. */
typedef struct {
    uint8_t  *data;
    uint32_t  len;
    int32_t   chunks;
    uint16_t  generation;               /* 0 = nothing sent on this channel yet */
    uint8_t   acked[kBlobBitmapBytes];
    uint32_t  lastSendMs;
} TxChannel;

/* What has arrived on one channel: the newest COMPLETE message, and at most one
 * newer one still coming in. */
typedef struct {
    uint8_t  *data;                     /* the transfer in progress, NULL if none */
    uint32_t  len;
    int32_t   chunks;
    uint16_t  generation;
    uint8_t   have[kBlobBitmapBytes];

    uint8_t  *done;                     /* the newest complete message */
    uint32_t  doneLen;
    uint16_t  doneGeneration;           /* 0 = none yet */
} RxChannel;

typedef struct {
    int32_t            sock;            /* -1 when closed */
    int32_t            isHost;
    P2PLinkState       state;
    P2PLinkFailure     failure;
    P2PLinkFailure     notice;
    Setup              local;
    uint16_t           localPort;
    struct sockaddr_in peer;
    int32_t            hasPeer;
    uint32_t           openedMs, lastHeardMs, lastSentMs;
    int32_t            pingMs;
    int32_t            jitterMs;        /* mean deviation of a round trip from pingMs */
    int32_t            pingSamples;     /* round trips measured so far */
    int32_t            delayLocal;      /* what we propose, -1 until we have a ping */
    int32_t            delayPeer;       /* what they proposed, -1 until it arrives */
    uint32_t           lastDelaySentMs;
    int32_t            joinTimeoutMs;
    char               peerText[24];    /* "255.255.255.255:65535" */

    /* NAT traversal: every address the peer might be reachable at. Both sides
     * send to all of them at once -- see punch_send. */
    struct sockaddr_in candidates[kMaxCandidates];
    int32_t            numCandidates;
    int32_t            punchRounds;     /* SWOS_P2P_LOG bookkeeping */
    int32_t            heardAnything;   /* any datagram from anywhere, ever */
    struct { struct sockaddr_in from; int32_t ours, refused; } heard[6];
    int32_t            heardSources;
    struct in_addr     sendErr[8];      /* destinations sendto refused, logged once each */
    int32_t            sendErrCount;

    /* Messages, one of each kind per channel, in each direction (see "messages"
     * below). */
    TxChannel          tx[P2P_NUM_CHANNELS];
    RxChannel          rx[P2P_NUM_CHANNELS];

    uint16_t           peerScreenWidth; /* from the peer's handshake: P2PLink_AgreedSimWidth */

    /* A relayed match (P2PLink_SetRelay). NOT cleared by link_reset: it belongs to
     * the matchmaking session, which outlives any one link -- a rematch punches
     * again and has to register again. */
    uint64_t           relayToken;      /* 0 = direct */
    int32_t            relayPlayer;
    int32_t            relayAcked;      /* the server has answered RELAY_OK */
    /* The relay as a FALLBACK (P2PLink_SetRelayFallback): a session the server made in
     * case the direct punch fails. Disarmed (token 0) once a round trip works direct;
     * `fbUsed` once the link has moved to it. The session's, like the relay's. */
    uint64_t           fbToken;
    int32_t            fbPlayer;
    struct sockaddr_in fbServer;
    int32_t            fbUsed;
    /* 1 or 2 when punching to several candidates, 0 when unknown: which side settles
     * the path (see the CONNECTED case of handle_packet). */
    int32_t            punchPlayer;
    /* The matchmaking server (P2PLink_SetServer), and the last thing it answered an
     * AskServer with. Like the relay's fields, the session's and not one link's. */
    struct sockaddr_in server;
    int32_t            haveServer;
    uint8_t            serverAnswer[96];
    int32_t            serverAnswerLen;
} P2PLink;

static P2PLink s_link = { -1, 0, P2P_IDLE, P2P_FAIL_NONE, P2P_FAIL_NONE,
                          { 0, 0, 0, 0 }, 0, { 0 }, 0, 0, 0, 0, -1,
                          0, 0,     /* jitterMs, pingSamples */
                          -1, -1, 0, kJoinTimeoutMs, "" };

static uint32_t now_ms(void)
{
    return SDL_GetTicks();
}

/* The game this build would play. The three gameplay flags are set from the command
 * line and SWOS.ini in main.c. The screen width travels too, but no longer has to match:
 * the two agree one simulation draw width from both (P2PLink_AgreedSimWidth). */
/* The rules a match is played by, one bit each: what the player chose (the gameplay
 * style, arcade mode, the fire-hold lockout). Two peers that differ in any bit play
 * different matches from the same inputs.
 *
 * P2P_RULE_NO_BOOKINGS / P2P_RULE_NO_INJURIES are never set here: they said a build
 * was compiled without cards and injuries (SWOS_TEST_NO_INJURIES_CARDS, the tester
 * builds until 2026-10-11). Every build sends 0, which is what makes this build refuse
 * an old tester build, whose match really would be a different one. Online the rule is
 * now the host's CARDS/INJURIES switch, decided in the lobby after this handshake; a
 * match played without it says so in its RECORDING's rules (netplay_match.c), as the
 * old builds' did, so srp_resim.c replays both the same way. */
uint8_t P2PLink_GameplayFlags(void)
{
    extern int _gDosGameplay;               /* dos_mode.h */
    extern int _gArcadeMode;                /* arcade.c */
    extern int _gOriginalFireHoldLockout;   /* team_control.c */
    extern int _gDosView;                   /* dos_graphics.c */

    /* The VIEW too, though it reads like a display setting: the simulation camera is
     * placed for the screen the match is seen on -- MatchViewEnter pins it to 320x200
     * for the DOS view -- and the camera moves players (which one you control, who is
     * on screen). Two peers on different views went out of step at frame 0, and a
     * recording replayed only on a machine with the recorder's view (2026-09-29). */
    return (uint8_t)((_gDosGameplay ? P2P_RULE_DOS : 0) |
                     (_gDosView ? P2P_RULE_DOS_VIEW : 0) | P2P_RULE_VIEW_KNOWN | P2P_RULE_ONE_TICK |
                     (_gArcadeMode ? P2P_RULE_ARCADE : 0) |
                     (_gOriginalFireHoldLockout ? P2P_RULE_FIRE_LOCKOUT : 0));
}

static void setup_local(Setup *s)
{
    uint64_t hash = SimState_LayoutHash();
    s->stateHi     = (uint32_t)(hash >> 32);
    s->stateLo     = (uint32_t)hash;
    s->screenWidth = (uint16_t)_screen_width;
    s->gameplay    = P2PLink_GameplayFlags();
    memset(s->build, 0, sizeof s->build);
    strncpy(s->build, Build_Id(), 8);
    s->compat = NETPLAY_COMPAT;
    s->dirty  = strstr(Build_Id(), "-dirty") != NULL;
    s->world  = (uint8_t)World_Current();
}

static P2PLinkFailure setup_mismatch(const Setup *mine, const Setup *theirs)
{
    /* A COMPATIBLE BUILD, before anything finer (include/netplay_compat.h). The state
     * layout below catches a build whose match state has a different shape, but two
     * builds with the same shape and different behaviour played each other and went
     * out of step in the middle of a match (2026-09-27: c1f49c0 against 31f8aef). Until
     * 2026-10-25 the commit itself had to match; now the number does, raised by
     * whoever changes how a match plays. A build with uncommitted changes is not the
     * commit it says, so its number vouches for nothing: it plays only its own commit. */
    const int32_t sameCommit = memcmp(mine->build, theirs->build, 8) == 0;
    if (mine->compat != theirs->compat || ((mine->dirty || theirs->dirty) && !sameCommit)) {
        fprintf(stderr, "[p2p] the other player runs build %.8s (netplay %u%s), this is %.8s "
                "(netplay %u%s): refused\n", theirs->build, (unsigned)theirs->compat,
                theirs->dirty ? ", uncommitted changes" : "", mine->build, (unsigned)mine->compat,
                mine->dirty ? ", uncommitted changes" : "");
        return P2P_FAIL_VERSION;
    }
    if (!sameCommit)
        fprintf(stderr, "[p2p] the other player runs build %.8s, this is %.8s: both netplay %u\n",
                theirs->build, mine->build, (unsigned)mine->compat);
    /* The same football world: another world's clubs and competitions are another game. */
    if (mine->world != theirs->world) {
        fprintf(stderr, "[p2p] the other player plays world %u, this is %u: refused\n",
                (unsigned)theirs->world, (unsigned)mine->world);
        return P2P_FAIL_WORLD;
    }
    if (mine->stateHi != theirs->stateHi || mine->stateLo != theirs->stateLo)
        return P2P_FAIL_STATE;
    /* The build's rules first: nothing in OPTIONS can make those agree. */
    if ((mine->gameplay ^ theirs->gameplay) & P2P_RULES_BUILD)
        return P2P_FAIL_BUILD;
    /* NOT the gameplay style: an online game plays its host's, and the player who
     * joins takes it when the link connects (link_connected, GameplayStyle_Borrow) --
     * a style mismatch used to send the joiner back to OPTIONS. Everything else in
     * the rules still has to match. */
    if ((mine->gameplay ^ theirs->gameplay) &
        ~(P2P_RULE_DOS | P2P_RULE_DOS_VIEW | P2P_RULE_VIEW_KNOWN | P2P_RULE_ONE_TICK))
        return P2P_FAIL_GAMEPLAY;
    /* The view follows the style unless --view pinned it: a pinned view that differs
     * from the other side's, on the SAME style, cannot be followed. */
    if (!((mine->gameplay ^ theirs->gameplay) & P2P_RULE_DOS) &&
        ((mine->gameplay ^ theirs->gameplay) & (P2P_RULE_DOS_VIEW | P2P_RULE_VIEW_KNOWN)))
        return P2P_FAIL_VIEW;
    /* Not the screen width. It used to be refused, because the simulation read it; the
     * match now reads one width both peers agree instead (display.h, _sim_draw_width).
     * P2P_FAIL_WIDTH stays in the enum for a peer of an older protocol to report. */
    return P2P_FAIL_NONE;
}

/* ---- packets ---- */

static int32_t pkt_header(uint8_t *p, uint8_t type)
{
    p[0] = 'S'; p[1] = 'W'; p[2] = 'U'; p[3] = 'N';
    p[4] = (uint8_t)kProtocol;
    p[5] = type;
    p[6] = 0; p[7] = 0;
    return kHeaderLen;
}

static void pkt_write_setup(uint8_t *p, const Setup *s)
{
    BE_WRITE_U32(p + 8,  s->stateHi);
    BE_WRITE_U32(p + 12, s->stateLo);
    BE_WRITE_U16(p + 16, s->screenWidth);
    p[18] = s->gameplay;
    p[19] = 0;
    memcpy(p + 20, s->build, 8);
    BE_WRITE_U16(p + 28, s->compat);
    p[30] = s->dirty;
    p[31] = s->world;
}

static void pkt_read_setup(const uint8_t *p, Setup *s)
{
    s->stateHi     = BE_READ_U32(p + 8);
    s->stateLo     = BE_READ_U32(p + 12);
    s->screenWidth = BE_READ_U16(p + 16);
    s->gameplay    = p[18];
    memcpy(s->build, p + 20, 8);
    s->build[8] = '\0';
    s->compat      = BE_READ_U16(p + 28);
    s->dirty       = p[30];
    s->world       = p[31];
}

/* The self-test's lossy wire: drop every Nth datagram a link sends, and flip a bit in
 * every Mth; 0 for none. Deterministic rather than random, so a failure repeats. */
static int32_t s_testDropEvery, s_testTamperEvery, s_testSendCount;

/* A packet's checksum: FNV-1a over every byte before it.
 *
 * UDP's own checksum makes a damaged datagram rare, but it is 16 bits, optional on
 * IPv4, and recomputed by anything that rewrites the header on the way -- and one
 * damaged field of a lobby message is enough to start two different matches. This is
 * not a defence against somebody forging packets (the link has no shared secret yet);
 * the match's GGPO packets are keyed instead, with a key this link carries. */
static uint32_t wire_checksum(const uint8_t *buf, size_t len)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++)
        h = (h ^ buf[i]) * 16777619u;
    return h;
}

static int32_t link_log_on(void);
static const char *addr_text(const struct sockaddr_in *a, char *buf, size_t cap);

static void wire_send(P2PLink *l, const struct sockaddr_in *to, const uint8_t *buf, size_t len)
{
    uint8_t out[kPacketMax];

    if (len + 4 > sizeof out)
        return;
    memcpy(out, buf, len);
    BE_WRITE_U32(out + len, wire_checksum(buf, len));
    ++s_testSendCount;
    if (s_testDropEvery > 0 && s_testSendCount % s_testDropEvery == 0)
        return;
    if (s_testTamperEvery > 0 && s_testSendCount % s_testTamperEvery == 0)
        out[(s_testSendCount * 7u) % (len + 4)] ^= (uint8_t)(1u << (s_testSendCount % 8));
    {
        ssize_t sent = !Netem_LinkSend(l->sock, (const char *)out, (int)(len + 4), 0,
                                       (const struct sockaddr *)to, (int)sizeof *to)
                     ? (ssize_t)(len + 4)       /* the emulator has it */
                     : sendto(l->sock, (const char *)out, len + 4, 0,
                              (const struct sockaddr *)to, (socklen_t)sizeof *to);
        /* A send that never leaves is indistinguishable from a peer that never
         * answers, and both end as "CONNECTION FAILED". Say which. Once per
         * destination, since the punch repeats twice a second. */
        if (sent < 0 && link_log_on()) {
            char b[64];
            int32_t h;
            for (h = 0; h < l->sendErrCount; h++)
                if (l->sendErr[h].s_addr == to->sin_addr.s_addr)
                    break;
            if (h == l->sendErrCount &&
                h < (int32_t)(sizeof l->sendErr / sizeof l->sendErr[0])) {
                l->sendErr[l->sendErrCount++] = to->sin_addr;
                fprintf(stderr, "[p2p] send to %s failed: %s\n",
                        addr_text(to, b, sizeof b), strerror(errno));
            }
        }
    }
}

/* Strip and check the trailer; the length without it, or -1 to drop the packet.
 *
 * A packet of ANOTHER protocol version is let through unchecked: handle_packet refuses
 * it with the reason ("a different netplay protocol"), which is worth more to the
 * player than a join that silently times out. Only ours has to be intact. */
static int32_t wire_receive(const uint8_t *buf, int32_t len)
{
    if (len >= kHeaderLen && memcmp(buf, "SWUN", 4) == 0 && buf[4] != (uint8_t)kProtocol)
        return len;
    if (len < kHeaderLen + 4)
        return -1;
    if (BE_READ_U32(buf + len - 4) != wire_checksum(buf, (size_t)(len - 4)))
        return -1;
    return len - 4;
}

static void link_send(P2PLink *l, const struct sockaddr_in *to, const uint8_t *buf, int32_t len)
{
    if (l->sock < 0)
        return;
    /* UDP and unreliable by design: every packet here is repeated or expendable. */
    wire_send(l, to, buf, (size_t)len);
}

static void send_setup_packet(P2PLink *l, const struct sockaddr_in *to, uint8_t type)
{
    uint8_t buf[kSetupLen];
    pkt_header(buf, type);
    pkt_write_setup(buf, &l->local);
    link_send(l, to, buf, (int32_t)sizeof buf);
}

static void send_refuse(P2PLink *l, const struct sockaddr_in *to, P2PLinkFailure reason)
{
    uint8_t buf[kRefuseLen];
    pkt_header(buf, PKT_REFUSE);
    buf[8] = (uint8_t)reason;
    link_send(l, to, buf, (int32_t)sizeof buf);
}

static void send_stamped(P2PLink *l, const struct sockaddr_in *to, uint8_t type, uint32_t stamp)
{
    uint8_t buf[kPingLen];
    pkt_header(buf, type);
    BE_WRITE_U32(buf + 8, stamp);
    link_send(l, to, buf, (int32_t)sizeof buf);
}

static void send_bye(P2PLink *l)
{
    uint8_t buf[kHeaderLen];
    pkt_header(buf, PKT_BYE);
    link_send(l, &l->peer, buf, (int32_t)sizeof buf);
}

/* ---- the socket ---- */

static void drop_socket(P2PLink *l)
{
    if (l->sock >= 0)
        close(l->sock);
    l->sock = -1;
}

/* Bind `port`, or the next free one up to `tries` above it (a joiner sharing a machine
 * with the host). Port 0 asks the system for any free one -- the self-test does, so it
 * never collides with whatever else is running.
 *
 * A port can be refused without anything using it: Windows keeps blocks of excluded
 * ports (Hyper-V takes some, and with WSL2's mirrored networking WSL's whole Linux
 * ephemeral range, 44620-48715 on the owner's machine), and a bind there fails with
 * EACCES rather than EADDRINUSE. `netsh int ipv4 show excludedportrange` does not always
 * list them. Say which it was: "in use" sends a player looking for another program. */
static P2PLinkFailure open_socket(P2PLink *l, uint16_t port, int32_t tries)
{
    P2PLinkFailure why = P2P_FAIL_PORT_IN_USE;
    int32_t s = (int32_t)socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
        return P2P_FAIL_SOCKET;
#ifdef SWOS_WASM_BUILD
    /* In a browser a port is only a name: nothing is bound, and 0 stays 0. But it is
     * the name the server's WebSocket gate knows this player by -- the link's socket and
     * then GGPO's, on the same port, must reach the relay from one address -- so two
     * browsers behind one address must not both say 0. */
    if (port == 0)
        port = (uint16_t)(20000 + (uint32_t)(emscripten_random() * 40000.0f));
#endif

    int32_t flags = fcntl(s, F_GETFL, 0);
    if (flags < 0 || fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(s);
        return P2P_FAIL_SOCKET;
    }

    for (int32_t i = 0; i < tries; i++) {
        if (port != 0 && (int32_t)port + i > 65535)
            break;
        struct sockaddr_in sin;
        memset(&sin, 0, sizeof sin);
        sin.sin_family      = AF_INET;
        sin.sin_addr.s_addr = htonl(INADDR_ANY);
        sin.sin_port        = htons((uint16_t)(port == 0 ? 0 : port + i));
        if (bind(s, (struct sockaddr *)&sin, (socklen_t)sizeof sin) == 0) {
            socklen_t len = (socklen_t)sizeof sin;
            if (getsockname(s, (struct sockaddr *)&sin, &len) == 0)
                l->localPort = ntohs(sin.sin_port);
            else
                l->localPort = (uint16_t)(port + i);
            l->sock = s;
            return P2P_FAIL_NONE;
        }
        why = errno == EACCES ? P2P_FAIL_PORT_RESERVED : P2P_FAIL_PORT_IN_USE;
        if (port == 0)
            break;
    }
    close(s);
    return why;
}

static void set_peer(P2PLink *l, const struct sockaddr_in *from)
{
    char ip[INET_ADDRSTRLEN];
    l->peer    = *from;
    l->hasPeer = 1;
    if (!inet_ntop(AF_INET, &from->sin_addr, ip, (socklen_t)sizeof ip))
        snprintf(ip, sizeof ip, "?");
    snprintf(l->peerText, sizeof l->peerText, "%s:%u", ip, (unsigned)ntohs(from->sin_port));
}

static int32_t same_peer(const P2PLink *l, const struct sockaddr_in *from)
{
    return l->hasPeer &&
           l->peer.sin_addr.s_addr == from->sin_addr.s_addr &&
           l->peer.sin_port == from->sin_port;
}

static void blob_free(P2PLink *l);      /* defined with the rest of the blob code */

static void link_reset(P2PLink *l)
{
    drop_socket(l);
    blob_free(l);                       /* a half-sent setup does not outlive its link */
    l->isHost      = 0;
    l->state       = P2P_IDLE;
    l->failure     = P2P_FAIL_NONE;
    l->notice      = P2P_FAIL_NONE;
    l->hasPeer     = 0;
    l->pingMs      = -1;
    l->pingSamples = 0;
    l->jitterMs = 0;
    l->localPort   = 0;
    l->peerText[0] = '\0';
    memset(&l->peer, 0, sizeof l->peer);
}

static const char *failure_name(P2PLinkFailure f)
{
    static const char *const names[] = {
        "none", "unavailable", "bad address", "port in use", "socket", "no answer",
        "lost", "peer left", "host busy", "protocol", "state (a different build)",
        "gameplay style", "screen width", "port reserved", "relay refused us",
        "build rules (one is a test build)", "version (another build)",
        "view (one DOS, one Amiga)",
    };
    return (uint32_t)f < sizeof names / sizeof names[0] ? names[f] : "?";
}

static void link_fail(P2PLink *l, P2PLinkFailure reason)
{
    /* Every failure, not only the join timeout: a refusal (a different gameplay
     * style, say) used to close the link in silence, and both players just sat on
     * the waiting screen with nothing in the log to say why. */
    if (link_log_on())
        fprintf(stderr, "[p2p] link %s: %s\n",
                reason == P2P_FAIL_PEER_LEFT ? "closed" : "failed", failure_name(reason));
    drop_socket(l);
    l->hasPeer = 0;
    l->pingMs  = -1;
    l->pingSamples = 0;
    l->jitterMs = 0;
    l->state   = P2P_FAILED;
    l->failure = reason;
}

/* The other player is gone. A host keeps its socket and waits for the next one -- that
 * is what hosting is -- and says what happened to the last; a joiner has nothing left
 * to wait for. */
static void peer_gone(P2PLink *l, P2PLinkFailure reason)
{
    if (l->isHost) {
        l->state   = P2P_HOSTING;
        l->notice  = reason;
        l->hasPeer = 0;
        l->pingMs  = -1;
        l->pingSamples = 0;
        l->jitterMs = 0;
        l->peerText[0] = '\0';
        memset(&l->peer, 0, sizeof l->peer);
    } else {
        link_fail(l, reason);
    }
}

/* ---- the input delay, agreed rather than each side choosing its own ----------
 *
 * THE BUG THIS EXISTS TO NOT HAVE. SWOS 2020 computes a delay on each machine
 * from that machine's own measurements and applies it to its own player only
 * (`ggpo_set_frame_delay(ggpo, local_player_handle, frameDelay)`, mpgame.cpp) --
 * the inputs are ping, jitter, and `1000/getMPFps()`. Two machines on the same
 * link do not measure the same jitter, and if one runs slower they do not even
 * agree on the frame time, so the two sides can settle on different numbers: a
 * match that started "AUTO 3" can end up 1 against 5. That is the reported
 * "chasing" -- the player carrying 5 frames of lag is reacting a quarter of a
 * second after the other one, and cannot win a race or a duel. It is very
 * probably most of the reported "ghosting" too: in a shoulder-to-shoulder
 * contest, four extra frames decides it before the skill roll is ever reached.
 *
 * So the delay is NEGOTIATED, and the rule is chosen so that both sides compute
 * the SAME answer from the SAME two numbers: each proposes, both take the
 * maximum. Max, not min or mean: the worse connection is the one that needs the
 * buffer, and a mean would round differently on each side the moment the two
 * proposals differ by an odd number.
 *
 * It is also agreed ONCE and then left alone. Re-deriving it mid-match is the
 * other way the two sides drift apart, and a delay that changes under you is
 * itself indistinguishable from lag.
 *
 * Both peers still apply it to their own local player -- that is all GGPO
 * offers -- but because the number is identical, the two are symmetric. */
static int32_t s_proposedPing = -1, s_proposedJitter = -1;

static int32_t propose_delay(int32_t pingMs, int32_t jitterMs)
{
    extern int _gDosGameplay;               /* dos_mode.h */
    /* The frame rate comes from the GAMEPLAY MODE, not from anything this
     * machine measures. That matters: setup_mismatch already refuses a peer
     * whose gameplay flags differ, so both sides are certain to use the same
     * number here. Taking it from the actual pacing would let a machine that is
     * running slow propose a different delay from its opponent -- which is
     * precisely the asymmetry this routine exists to avoid. */
    const int32_t fps = _gDosGameplay ? 70 : 50;

    /* Playa's table (2026-10-30), from the ping the lobby shows: never 0 (no buffer
     * at all), and capped; each gameplay mode has its own (its frame time):
     *
     *      DOS (70 fps)              AMIGA (50 fps)
     *      1   up to 35 ms           1   up to 50 ms
     *      2   36 - 70 ms            2   51 - 99 ms
     *      3   71 - 140 ms           3   100 - 175 ms
     *      4   141 - 210 ms          4   over 175 ms (the cap)
     *      5   over 210 ms (the cap)
     *
     * It replaced SWOS 2020's AUTO (MPGame::getAutoFrameDelay: 33 ms off the ping, 0 frames
     * on a clean link, ceil(rtt / (3 * frame time)), capped at 3 / 4). Jitter no longer
     * counts: only the ping does. */
    static const int16_t kDos[]   = { 35, 70, 140, 210 };   /* each delay's top, ms */
    static const int16_t kAmiga[] = { 50, 99, 175 };
    const int16_t *top = fps > 50 ? kDos : kAmiga;
    const int32_t n = fps > 50 ? (int32_t)(sizeof kDos / sizeof kDos[0])
                               : (int32_t)(sizeof kAmiga / sizeof kAmiga[0]);
    int32_t d = 1;
    (void)jitterMs;
    while (d <= n && pingMs > top[d - 1])
        d++;
    return d;
}

static void send_delay(P2PLink *l, uint32_t now)
{
    uint8_t pkt[kDelayLen];
    if (!l->hasPeer || l->delayLocal < 0)
        return;
    memcpy(pkt, "SWUN", 4);
    pkt[4] = (uint8_t)kProtocol;
    pkt[5] = PKT_DELAY;
    pkt[6] = 0;
    pkt[7] = 0;
    pkt[8] = (uint8_t)l->delayLocal;
    wire_send(l, &l->peer, pkt, sizeof pkt);
    l->lastDelaySentMs = now;
}

/* ---- messages -----------------------------------------------------------------
 *
 * Anything too big for a datagram, or that has to arrive: the lobby's selections,
 * the match setup and the answer to it (src/netplay_lobby.c). Each kind has a
 * CHANNEL of its own, in each direction.
 *
 * Numbered chunks, receiver-driven: the receiver says which chunks it HAS (a
 * bitmap), the sender re-sends what is missing until the bitmap is full. No
 * ordering assumptions and no per-chunk ack storm -- one small ack packet
 * describes the whole transfer, so a lost ack costs one retransmit round rather
 * than a stall.
 *
 * GENERATIONS. Sending on a channel again REPLACES what was there: a player who
 * picks a second team does not want the first one delivered after it. Every
 * message carries its channel's generation, the receiver keeps only the newest
 * complete one and ignores anything older, and chunks of an older transfer that
 * are still in flight cannot be mixed into a newer one -- before generations, a
 * re-send of the same length was indistinguishable from the first, and would have
 * been assembled from both. */
static void bitmap_set(uint8_t *bm, int32_t i)   { bm[i >> 3] |= (uint8_t)(1u << (i & 7)); }
static int32_t bitmap_get(const uint8_t *bm, int32_t i) { return (bm[i >> 3] >> (i & 7)) & 1; }

static int32_t bitmap_full(const uint8_t *bm, int32_t n)
{
    int32_t i;
    for (i = 0; i < n; i++)
        if (!bitmap_get(bm, i))
            return 0;
    return 1;
}

static void blob_free(P2PLink *l)
{
    int32_t c;
    for (c = 0; c < P2P_NUM_CHANNELS; c++) {
        free(l->tx[c].data);
        free(l->rx[c].data);
        free(l->rx[c].done);
        memset(&l->tx[c], 0, sizeof l->tx[c]);
        memset(&l->rx[c], 0, sizeof l->rx[c]);
    }
}

/* One chunk: header, channel, pad, generation (u16), total length (u32), chunk
 * index (u16), chunk count (u16), then the bytes. */
static void send_blob_chunk(P2PLink *l, int32_t channel, int32_t idx)
{
    const TxChannel *t = &l->tx[channel];
    uint8_t pkt[kHeaderLen + kChunkHeader + kBlobChunk];
    uint32_t off = (uint32_t)idx * (uint32_t)kBlobChunk;
    uint32_t n   = t->len - off;

    if (n > (uint32_t)kBlobChunk)
        n = (uint32_t)kBlobChunk;
    pkt_header(pkt, PKT_BLOB);
    pkt[8] = (uint8_t)channel;
    pkt[9] = 0;
    BE_WRITE_U16(pkt + 10, t->generation);
    BE_WRITE_U32(pkt + 12, t->len);
    BE_WRITE_U16(pkt + 16, (uint16_t)idx);
    BE_WRITE_U16(pkt + 18, (uint16_t)t->chunks);
    memcpy(pkt + kHeaderLen + kChunkHeader, t->data + off, n);
    wire_send(l, &l->peer, pkt, kHeaderLen + kChunkHeader + n);
}

/* The ack names the channel and generation it is about, so a late ack for a
 * message since replaced cannot mark chunks of the new one as delivered. */
static void send_blob_ack(P2PLink *l, int32_t channel, uint16_t generation, const uint8_t *bitmap)
{
    uint8_t pkt[kHeaderLen + 4 + kBlobBitmapBytes];
    pkt_header(pkt, PKT_BLOB_ACK);
    pkt[8] = (uint8_t)channel;
    pkt[9] = 0;
    BE_WRITE_U16(pkt + 10, generation);
    memcpy(pkt + kHeaderLen + 4, bitmap, kBlobBitmapBytes);
    wire_send(l, &l->peer, pkt, sizeof pkt);
}

/* A chunk. Returns nothing: every outcome is either stored or ignored, and an ack
 * goes back for anything this peer has or has moved past. */
static void receive_chunk(P2PLink *l, const uint8_t *buf, int32_t len)
{
    int32_t  channel = buf[8];
    uint16_t gen     = BE_READ_U16(buf + 10);
    uint32_t total   = BE_READ_U32(buf + 12);
    int32_t  idx     = (int32_t)BE_READ_U16(buf + 16);
    int32_t  count   = (int32_t)BE_READ_U16(buf + 18);
    uint32_t n       = (uint32_t)len - (kHeaderLen + kChunkHeader);
    RxChannel *r;

    /* Refuse anything that does not describe a transfer we could hold, rather than
     * trusting a length off the wire. */
    if (channel < 0 || channel >= P2P_NUM_CHANNELS || gen == 0 ||
        total == 0 || total > (uint32_t)kBlobMax ||
        count <= 0 || count > kBlobMaxChunks ||
        count != (int32_t)((total + kBlobChunk - 1) / kBlobChunk) ||
        idx < 0 || idx >= count || n > (uint32_t)kBlobChunk)
        return;
    r = &l->rx[channel];

    if (gen <= r->doneGeneration) {
        /* We already have this one, or something newer. Say so in full, or the
         * sender keeps retransmitting a message we will never take again. */
        uint8_t all[kBlobBitmapBytes];
        int32_t i;
        memset(all, 0, sizeof all);
        for (i = 0; i < count; i++)
            bitmap_set(all, i);
        send_blob_ack(l, channel, gen, all);
        return;
    }
    if (r->data && gen < r->generation)
        return;                               /* an older transfer's straggler */
    if (r->data && (gen != r->generation || r->len != total || r->chunks != count)) {
        free(r->data);                        /* replaced by a newer message */
        r->data = NULL;
    }
    if (!r->data) {
        r->data = (uint8_t *)calloc(1, total);
        if (!r->data)
            return;
        r->len = total;
        r->chunks = count;
        r->generation = gen;
        memset(r->have, 0, sizeof r->have);
    }
    {
        uint32_t off = (uint32_t)idx * (uint32_t)kBlobChunk;
        if (off + n > r->len)
            return;
        memcpy(r->data + off, buf + kHeaderLen + kChunkHeader, n);
        bitmap_set(r->have, idx);
    }
    send_blob_ack(l, channel, gen, r->have);  /* every chunk: cheap, and self-clocking */

    if (bitmap_full(r->have, r->chunks)) {
        free(r->done);
        r->done           = r->data;
        r->doneLen        = r->len;
        r->doneGeneration = r->generation;
        r->data           = NULL;
    }
}

static void receive_ack(P2PLink *l, const uint8_t *buf)
{
    int32_t  channel = buf[8];
    uint16_t gen     = BE_READ_U16(buf + 10);
    TxChannel *t;
    int32_t i;

    if (channel < 0 || channel >= P2P_NUM_CHANNELS)
        return;
    t = &l->tx[channel];
    if (!t->data || gen != t->generation)
        return;                               /* about a message since replaced */
    for (i = 0; i < kBlobBitmapBytes; i++)
        t->acked[i] |= buf[kHeaderLen + 4 + i];
}

/* Queue `data` on `channel`, replacing whatever was there. */
static int32_t message_send(P2PLink *l, int32_t channel, const uint8_t *data, uint32_t len)
{
    TxChannel *t;
    uint8_t *copy;

    if (l->state != P2P_CONNECTED || channel < 0 || channel >= P2P_NUM_CHANNELS ||
        !data || len == 0 || len > (uint32_t)kBlobMax)
        return 1;
    t = &l->tx[channel];
    if (t->generation == 0xFFFF)
        return 1;                             /* 65535 messages on one channel of one link */
    copy = (uint8_t *)malloc(len);
    if (!copy)
        return 1;
    memcpy(copy, data, len);
    free(t->data);
    t->data       = copy;
    t->len        = len;
    t->chunks     = (int32_t)((len + kBlobChunk - 1) / kBlobChunk);
    t->generation = (uint16_t)(t->generation + 1);
    memset(t->acked, 0, sizeof t->acked);
    t->lastSendMs = 0;                        /* the next poll starts sending */
    return 0;
}

static int32_t message_delivered(const P2PLink *l, int32_t channel)
{
    const TxChannel *t;
    if (channel < 0 || channel >= P2P_NUM_CHANNELS)
        return 0;
    t = &l->tx[channel];
    return t->data && bitmap_full(t->acked, t->chunks);
}

static const uint8_t *message_received(const P2PLink *l, int32_t channel, uint32_t *len, uint16_t *generation)
{
    const RxChannel *r;
    if (channel < 0 || channel >= P2P_NUM_CHANNELS)
        return NULL;
    r = &l->rx[channel];
    if (len)
        *len = r->done ? r->doneLen : 0;
    if (generation)
        *generation = r->doneGeneration;
    return r->done;
}

/* Is this packet from one of the addresses the peer might be at?
 *
 * HOST ONLY, deliberately: a NAT rewrites the SOURCE PORT, so the datagram that
 * finally arrives comes from a port nobody advertised. Comparing the port would
 * reject exactly the packets that prove a path works. The reference
 * implementation matches the same way (P2PConnection.cpp, matchAddressOnly). */
static int32_t from_candidate(const P2PLink *l, const struct sockaddr_in *from)
{
    int32_t i;
    for (i = 0; i < l->numCandidates; i++)
        if (l->candidates[i].sin_addr.s_addr == from->sin_addr.s_addr)
            return 1;
    return 0;
}

/* Say hello to every candidate at once.
 *
 * THIS IS THE HOLE PUNCH. Each datagram we send opens a mapping in our own NAT
 * for the address it went to, so the peer's reply has somewhere to arrive. Both
 * peers do it simultaneously -- neither is "the host" here -- which is what
 * gets two players behind different routers talking without either forwarding a
 * port. Most of these go nowhere (a LAN address on the other side of the
 * internet); that is the point, they cost a packet each and one of them works. */
/* SWOS_P2P_LOG=1: what the link is doing, for a failure nobody can see. A player
 * who cannot reach an opponent gets "CONNECTION FAILED" and nothing else, and so
 * did we -- five runs across two machines with no way to tell a blocked packet
 * from a wrong address. One line per event, never per packet. */
static int32_t link_log_on(void)
{
    static int32_t on = -1;
    if (on < 0)
        on = getenv("SWOS_P2P_LOG") != NULL;
    return on;
}

static const char *addr_text(const struct sockaddr_in *a, char *buf, size_t cap)
{
    char ip[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &a->sin_addr, ip, sizeof ip))
        snprintf(ip, sizeof ip, "?");
    snprintf(buf, cap, "%s:%u", ip, (unsigned)ntohs(a->sin_port));
    return buf;
}

/* RELAY_REGISTER to the server (include/relay_proto.h). Raw: the relay's control
 * packets are its own format, not ours, so no checksum trailer. */
static void relay_register(P2PLink *l)
{
    uint8_t buf[RELAY_CONTROL_LEN];
    RelayControl c;

    memset(&c, 0, sizeof c);
    c.type   = RELAY_REGISTER;
    c.player = (uint8_t)l->relayPlayer;
    c.token  = l->relayToken;
    Relay_WriteControl(buf, &c);
    /* candidates[0] is the server: a relayed match punches to nothing else. */
    if (Netem_LinkSend(l->sock, (const char *)buf, (int)sizeof buf, 0,
                       (const struct sockaddr *)&l->candidates[0], (int)sizeof l->candidates[0]))
        sendto(l->sock, (const char *)buf, sizeof buf, 0,
               (const struct sockaddr *)&l->candidates[0], (socklen_t)sizeof l->candidates[0]);
}

/* The server's answer. 1 if `buf` was one (and has been dealt with). */
static int32_t relay_answer(P2PLink *l, const uint8_t *buf, int32_t len)
{
    RelayControl c;
    if (!l->relayToken || Relay_ReadControl(buf, len, &c) != 0 || c.type != RELAY_REGISTERED ||
        c.token != l->relayToken)
        return 0;
    if (c.status != RELAY_OK) {
        if (l->state == P2P_JOINING)
            link_fail(l, P2P_FAIL_RELAY);
        return 1;
    }
    if (!l->relayAcked && link_log_on())
        fprintf(stderr, "[p2p] relay: registered as player %d, waiting for the other\n",
                (int)c.player);
    l->relayAcked = 1;
    return 1;
}

static void punch_send(P2PLink *l)
{
    int32_t i;
    /* First, so the server knows us before our hello reaches it: a hello from an
     * address it cannot place is not forwarded. */
    if (l->relayToken)
        relay_register(l);
    for (i = 0; i < l->numCandidates; i++)
        send_setup_packet(l, &l->candidates[i], PKT_HELLO);
    l->punchRounds++;
    if (link_log_on() && (l->punchRounds == 1 || (l->punchRounds % 10) == 0)) {
        char buf[64];
        fprintf(stderr, "[p2p] punch round %d from :%u to %d candidate(s)",
                (int)l->punchRounds, (unsigned)l->localPort, (int)l->numCandidates);
        for (i = 0; i < l->numCandidates; i++)
            fprintf(stderr, " %s", addr_text(&l->candidates[i], buf, sizeof buf));
        fprintf(stderr, "  (heard from peer: %s)\n", l->heardAnything ? "yes" : "NOTHING YET");
    }
    l->lastSentMs = now_ms();
}

static void link_connected(P2PLink *l, const struct sockaddr_in *from, const Setup *theirs,
                           uint32_t now)
{
    /* THE HOST'S STYLE. Player 2 -- the joiner: the punch says which it is, and in the
     * direct-IP menu it is the side that joined -- plays the host's gameplay style
     * for as long as this game lasts; the menus give their own back when they leave
     * it (GameplayStyle_GiveBack). Before any of the link's state changes, so what
     * is compared from here on is the style actually played. */
    {
        extern void GameplayStyle_Borrow(int dos);
        int follower = l->punchPlayer ? l->punchPlayer == 2 : l->state == P2P_JOINING;
        if (follower && ((theirs->gameplay ^ l->local.gameplay) & P2P_RULE_DOS)) {
            int dos = (theirs->gameplay & P2P_RULE_DOS) != 0;
            GameplayStyle_Borrow(dos);
            l->local.gameplay = P2PLink_GameplayFlags();
            fprintf(stderr, "[p2p] the host plays %s: this game is played that way\n",
                    dos ? "DOS" : "AMIGA");
        }
    }
    blob_free(l);   /* nothing carries over from a previous peer */
    l->peerScreenWidth = theirs->screenWidth;
    l->delayLocal = l->delayPeer = -1;
    l->lastDelaySentMs = 0;
    set_peer(l, from);
    l->state       = P2P_CONNECTED;
    l->notice      = P2P_FAIL_NONE;
    l->lastHeardMs = now;
    l->pingMs      = -1;
    l->pingSamples = 0;
    l->jitterMs = 0;
    /* Measure the round trip straight away rather than a ping interval later: it is
     * the number a player picks an input delay from. */
    send_stamped(l, &l->peer, PKT_PING, now);
    l->lastSentMs = now;
}

/* ---- receiving ---- */

static P2PLinkFailure refuse_reason(uint8_t byte)
{
    switch (byte) {
    case P2P_FAIL_HOST_BUSY:
    case P2P_FAIL_PROTOCOL:
    case P2P_FAIL_STATE:
    case P2P_FAIL_GAMEPLAY:
    case P2P_FAIL_WIDTH:
    case P2P_FAIL_BUILD:
    case P2P_FAIL_VERSION:
    case P2P_FAIL_VIEW:
    case P2P_FAIL_WORLD:
        return (P2PLinkFailure)byte;
    default:
        return P2P_FAIL_PROTOCOL;   /* a reason this version does not know */
    }
}

static void handle_packet(P2PLink *l, const uint8_t *buf, int32_t len,
                          const struct sockaddr_in *from, uint32_t now)
{
    if (len < kHeaderLen || memcmp(buf, "SWUN", 4) != 0)
        return;                                  /* not ours: some other traffic */

    uint8_t type = buf[5];

    if (buf[4] != (uint8_t)kProtocol) {
        /* An OpenSWOS of another version. Tell a joiner so, and take the same from a
         * host rather than waiting out the timeout. */
        if (l->state == P2P_HOSTING && type == PKT_HELLO)
            send_refuse(l, from, P2P_FAIL_PROTOCOL);
        else if (l->state == P2P_JOINING && same_peer(l, from))
            link_fail(l, P2P_FAIL_PROTOCOL);
        return;
    }

    switch (l->state) {
    case P2P_HOSTING:
        if (type == PKT_HELLO && len >= kSetupLen) {
            Setup theirs;
            pkt_read_setup(buf, &theirs);
            P2PLinkFailure bad = setup_mismatch(&l->local, &theirs);
            if (bad != P2P_FAIL_NONE) {
                send_refuse(l, from, bad);
                l->notice = bad;        /* so the host sees why someone bounced off */
                return;
            }
            send_setup_packet(l, from, PKT_WELCOME);
            link_connected(l, from, &theirs, now);
        }
        break;

    case P2P_JOINING:
        /* With a candidate list this is the PUNCHING state and any of them may
         * answer -- and from a port a NAT chose, so the match is on host only.
         * With a single peer (the direct-IP menu) it is the old behaviour. */
        if (l->numCandidates > 0 ? !from_candidate(l, from) : !same_peer(l, from))
            return;
        if (type == PKT_HELLO && len >= kSetupLen && l->numCandidates > 0) {
            /* The peer is punching too and its packet arrived first. Answer it
             * and take THIS address: it is the one demonstrably working, which
             * the advertised one may not be. */
            Setup theirs;
            pkt_read_setup(buf, &theirs);
            {
                P2PLinkFailure bad = setup_mismatch(&l->local, &theirs);
                if (bad != P2P_FAIL_NONE) {
                    send_refuse(l, from, bad);
                    link_fail(l, bad);
                    return;
                }
            }
            send_setup_packet(l, from, PKT_WELCOME);
            link_connected(l, from, &theirs, now);
            return;
        }
        if (type == PKT_WELCOME && len >= kSetupLen) {
            Setup theirs;
            pkt_read_setup(buf, &theirs);
            P2PLinkFailure bad = setup_mismatch(&l->local, &theirs);
            if (bad != P2P_FAIL_NONE) {
                link_fail(l, bad);
                return;
            }
            link_connected(l, from, &theirs, now);
        } else if (type == PKT_REFUSE && len >= kRefuseLen) {
            link_fail(l, refuse_reason(buf[8]));
        }
        break;

    case P2P_CONNECTED:
        if (!same_peer(l, from)) {
            /* ONE PEER, SEVERAL PATHS. Punching sends to every candidate at once, and each
             * side takes the first address that answers -- so on a jittery link the two can
             * settle on DIFFERENT paths (one on the other's loopback or LAN address, the
             * other on its public one), each then sending down its own path and dropping
             * everything arriving down the other: both went deaf and the link timed out
             * "lost" six seconds later (150 ms +/-70, 3% loss; the mobile profile). Any
             * two working paths do it -- two players behind one router, for a start.
             *
             * So a packet from another of the peer's candidate addresses is the peer, and
             * the two converge on one path: player 1 keeps its choice, player 2 follows
             * whatever player 1 sends from. Within a round trip both use player 1's path --
             * which the match that follows needs, since GGPO takes one address each side. */
            if (l->numCandidates > 0 && from_candidate(l, from)) {
                /* Followed on player 1's ordinary traffic only. Its punch HELLOs and
                 * WELCOMEs go to EVERY candidate for a moment after the link is up, and
                 * following those made player 2 hop between addresses packet by packet. */
                if (l->punchPlayer == 2 && type != PKT_HELLO && type != PKT_WELCOME) {
                    if (link_log_on()) {
                        char was[64], now_[64];
                        fprintf(stderr, "[p2p] player 1 is sending from %s, not %s: following it\n",
                                addr_text(from, now_, sizeof now_), addr_text(&l->peer, was, sizeof was));
                    }
                    set_peer(l, from);
                }
            } else {
                if (type == PKT_HELLO)
                    send_refuse(l, from, P2P_FAIL_HOST_BUSY);
                return;
            }
        }
        l->lastHeardMs = now;
        /* Replies go to OUR peer address, not back where the packet came from: the
         * peer may be reaching us down another of its paths (above), and answering down
         * that one had player 1 sending on two paths at once -- its pings on its own, its
         * pongs on player 2's -- so player 2 hopped between them for ever. One path each. */
        if (type == PKT_HELLO) {
            send_setup_packet(l, &l->peer, PKT_WELCOME);    /* our welcome was lost */
        } else if (type == PKT_PING && len >= kPingLen) {
            send_stamped(l, &l->peer, PKT_PONG, BE_READ_U32(buf + 8));
        } else if (type == PKT_PONG && len >= kPingLen) {
            int32_t rtt = (int32_t)(now - BE_READ_U32(buf + 8));
            if (rtt < 0)
                rtt = 0;
            if (l->pingMs < 0) {
                l->pingMs = rtt;
                l->jitterMs = 0;
            } else {
                int32_t dev = rtt > l->pingMs ? rtt - l->pingMs : l->pingMs - rtt;
                l->jitterMs = (l->jitterMs * 3 + dev) / 4;
                l->pingMs = (l->pingMs * 3 + rtt) / 4;
            }
            l->pingSamples++;
            if (l->fbToken && !l->fbUsed) {
                l->fbToken = 0;     /* both ways, direct: the server's session is not needed */
                if (link_log_on())
                    fprintf(stderr, "[p2p] direct both ways (%d ms): the relay fallback is not needed\n",
                            (int)rtt);
            }
        } else if (type == PKT_DELAY && len >= kDelayLen) {
            int32_t theirs = buf[8];
            if (theirs > kDelayMax)
                theirs = kDelayMax;      /* never take a number we would not send */
            l->delayPeer = theirs;
            /* ANSWER IT. Without this the exchange deadlocks: each side stopped
             * proposing once it had the other's, so whoever heard first went
             * quiet before the other had heard anything at all, and that side
             * waited for a proposal that would never be sent again. Measured --
             * the joiner had both numbers while the host timed out holding one.
             * A reply is 9 bytes and makes the exchange self-completing. */
            if (l->delayLocal >= 0)
                send_delay(l, now);
        } else if (type == PKT_BLOB && len > kHeaderLen + kChunkHeader) {
            receive_chunk(l, buf, len);
        } else if (type == PKT_BLOB_ACK && len >= kHeaderLen + 4 + kBlobBitmapBytes) {
            receive_ack(l, buf);
        } else if (type == PKT_BYE) {
            peer_gone(l, P2P_FAIL_PEER_LEFT);
        }
        break;

    default:
        break;
    }
}

/* THE RELAY AS A FALLBACK. Direct play needs packets to get through BOTH ways, and two
 * NATs do not always allow it: a VPN exit drops what it did not ask for, and a router
 * that gives every destination its own port (UMTYTM's, 2026-10-22: 55863 inside,
 * 40244 out) can let the other side's packets in while its own never arrive -- a link
 * one way only, where one game thinks it is connected and the other waits. So the test
 * is a ROUND TRIP: a link that has not measured one by kRelayFallbackMs, connected or
 * not, starts again through the server -- from the same socket, to the server alone,
 * registering with the session's token -- and from there it is a relayed match like
 * any other (include/relay_proto.h). Both sides of a failed or one-way link have no
 * round trip, so both move; a link that works both ways has one within a second. */
static void relay_fallback(P2PLink *l, uint32_t now)
{
    char buf[64];
    if (link_log_on())
        fprintf(stderr, "[p2p] no round trip direct after %u ms (%s): playing through the "
                        "server, %s\n", (unsigned)(now - l->openedMs),
                l->state == P2P_CONNECTED ? "packets get through one way only" : "nothing got through",
                addr_text(&l->fbServer, buf, sizeof buf));
    fprintf(stderr, "[p2p] direct connection failed: playing through the server\n");
    blob_free(l);
    l->fbUsed        = 1;
    l->relayToken    = l->fbToken;
    l->relayPlayer   = l->fbPlayer;
    l->relayAcked    = 0;
    l->candidates[0] = l->fbServer;
    l->numCandidates = 1;
    l->state         = P2P_JOINING;
    l->hasPeer       = 0;
    l->pingMs        = -1;
    l->pingSamples   = 0;
    l->jitterMs      = 0;
    l->delayLocal    = l->delayPeer = -1;
    l->heardAnything = 0;
    l->heardSources  = 0;
    l->peerText[0]   = '\0';
    memset(&l->peer, 0, sizeof l->peer);
    l->openedMs = l->lastHeardMs = now;       /* a fresh join timeout for the relay */
    punch_send(l);
}

static void link_poll(P2PLink *l, uint32_t now)
{
    if (l->sock < 0)
        return;
    Netem_LinkPump(l->sock);            /* what the emulated network is now due to deliver */

    for (int32_t i = 0; i < kMaxReadsPerPoll; i++) {
        uint8_t buf[kPacketMax];
        struct sockaddr_in from;
        socklen_t fromLen = (socklen_t)sizeof from;
        ssize_t n = recvfrom(l->sock, (char *)buf, sizeof buf, 0,
                             (struct sockaddr *)&from, &fromLen);
        if (n < 0) {
            /* Nothing left to read. Winsock (so Cygwin) also reports an ICMP "port
             * unreachable" from an earlier send here, on a socket that is not
             * connected -- a host that is not listening yet, which is not an error
             * for us: read on, and let the join time out if it never answers. */
            if (errno == ECONNRESET || errno == ECONNREFUSED)
                continue;
            break;
        }
        /* SWOS_P2P_TEST_BLOCK_DIRECT=1: a router that lets nothing in from the other
         * player -- only the server gets through. On both players, a pair that cannot
         * punch at all; on one, a link that works one way only. How the relay fallback
         * is tested (relay_fallback; RELAY=0 HOST_ENV=... scripts/relay_test.sh). */
        {
            static int32_t block = -1;
            if (block < 0)
                block = getenv("SWOS_P2P_TEST_BLOCK_DIRECT") != NULL;
            if (block && from.sin_family == AF_INET &&
                !(l->haveServer && from.sin_addr.s_addr == l->server.sin_addr.s_addr &&
                  from.sin_port == l->server.sin_port))
                continue;
        }
        /* The matchmaking server's answer to an AskServer: kept for whoever asked. Only
         * from the server's own address, and only a datagram that is not one of ours
         * (ours carry a checksum trailer and never start with a letter magic). */
        if (l->haveServer && from.sin_family == AF_INET && n >= 4 && n <= (int)sizeof l->serverAnswer &&
            from.sin_addr.s_addr == l->server.sin_addr.s_addr && from.sin_port == l->server.sin_port &&
            buf[0] == 'S' && buf[1] == 'W' && buf[2] == 'T') {
            memcpy(l->serverAnswer, buf, (size_t)n);
            l->serverAnswerLen = (int32_t)n;
            continue;
        }
        if (from.sin_family == AF_INET && relay_answer(l, buf, (int32_t)n)) {
            if (l->sock < 0)
                break;                  /* the relay refused us: the link is over */
            continue;
        }
        if (from.sin_family == AF_INET) {
            int32_t len = wire_receive(buf, (int32_t)n);
            /* Tally by SOURCE. One flag is not enough: the matchmaker's own last
             * packets arrive on this very port (the link reuses it deliberately)
             * and are refused, which made "we heard something" true while the
             * peer was still unreachable. */
            {
                int32_t h;
                for (h = 0; h < l->heardSources; h++)
                    if (l->heard[h].from.sin_addr.s_addr == from.sin_addr.s_addr &&
                        l->heard[h].from.sin_port == from.sin_port)
                        break;
                if (h == l->heardSources && h < (int32_t)(sizeof l->heard / sizeof l->heard[0])) {
                    l->heard[h].from = from;
                    l->heard[h].ours = l->heard[h].refused = 0;
                    l->heardSources++;
                }
                if (h < (int32_t)(sizeof l->heard / sizeof l->heard[0])) {
                    if (len >= 0) l->heard[h].ours++;
                    else          l->heard[h].refused++;
                }
            }
            l->heardAnything = 1;
            if (len >= 0)
                handle_packet(l, buf, len, &from, now);
        }
        if (l->sock < 0)
            break;                      /* the packet ended the link */
    }

    if (l->fbToken && !l->fbUsed && l->pingSamples == 0 &&
        (l->state == P2P_JOINING || l->state == P2P_CONNECTED) &&
        now - l->openedMs >= (uint32_t)kRelayFallbackMs)
        relay_fallback(l, now);

    if (l->state == P2P_JOINING) {
        if (now - l->openedMs >= (uint32_t)l->joinTimeoutMs) {
            if (link_log_on()) {
                char b[64];
                int32_t h;
                fprintf(stderr, "[p2p] giving up after %u ms: %d punch round(s) to %d "
                                "candidate(s); heard from %d source(s)%s\n",
                        (unsigned)(now - l->openedMs), (int)l->punchRounds,
                        (int)l->numCandidates, (int)l->heardSources,
                        l->heardSources ? ":" : " -- NOTHING arrived from anywhere");
                for (h = 0; h < l->heardSources; h++)
                    fprintf(stderr, "[p2p]   %s: %d ours, %d refused\n",
                            addr_text(&l->heard[h].from, b, sizeof b),
                            (int)l->heard[h].ours, (int)l->heard[h].refused);
            }
            link_fail(l, P2P_FAIL_NO_ANSWER);
        } else if (now - l->lastSentMs >= (uint32_t)kHelloIntervalMs) {
            if (l->numCandidates > 0) {
                punch_send(l);      /* keeps every mapping alive, not just one */
            } else {
                send_setup_packet(l, &l->peer, PKT_HELLO);
                l->lastSentMs = now;
            }
        }
    } else if (l->state == P2P_CONNECTED) {
        /* Keep pushing whatever the receiver has not acked yet, on every channel. */
        for (int32_t c = 0; c < P2P_NUM_CHANNELS; c++) {
            TxChannel *t = &l->tx[c];
            if (t->data && !bitmap_full(t->acked, t->chunks) &&
                now - t->lastSendMs >= (uint32_t)kBlobResendMs) {
                int32_t i, sent = 0;
                for (i = 0; i < t->chunks && sent < 16; i++) {
                    if (!bitmap_get(t->acked, i)) {
                        send_blob_chunk(l, c, i);
                        sent++;
                    }
                }
                t->lastSendMs = now;
            }
        }

        /* Propose as soon as there is a round trip to base it on, and keep
         * re-sending until the peer's proposal arrives: this is UDP and a lost
         * proposal would otherwise leave the match with no agreed delay. */
        if (l->delayLocal < 0 && l->pingSamples >= kDelaySamples) {
            l->delayLocal = propose_delay(l->pingMs, l->jitterMs);
            /* What the delay was proposed FROM, for the match's kick-off log line. Kept
             * across the link's resets (the match takes over the socket) until the next
             * proposal replaces it. */
            s_proposedPing   = l->pingMs;
            s_proposedJitter = l->jitterMs;
        }
        /* Keep proposing until BOTH halves are known here. Losing the reply
         * above is otherwise unrecoverable, and this is UDP. */
        if (l->delayLocal >= 0 && l->delayPeer < 0 &&
            now - l->lastDelaySentMs >= (uint32_t)kDelayIntervalMs)
            send_delay(l, now);

        if (now - l->lastHeardMs >= (uint32_t)kQuietTimeoutMs)
            peer_gone(l, P2P_FAIL_LOST);
        else if (now - l->lastSentMs >= (uint32_t)(l->pingSamples < kDelaySamples
                                                        ? kPingWarmupMs : kPingIntervalMs)) {
            send_stamped(l, &l->peer, PKT_PING, now);
            l->lastSentMs = now;
        }
    }
}

/* ---- opening and closing ---- */

static int32_t link_host(P2PLink *l, uint16_t port)
{
    link_reset(l);
    P2PLinkFailure bad = open_socket(l, port, 1);
    if (bad != P2P_FAIL_NONE) {
        l->state   = P2P_FAILED;
        l->failure = bad;
        return 1;
    }
    setup_local(&l->local);
    l->isHost   = 1;
    l->state    = P2P_HOSTING;
    l->openedMs = l->lastHeardMs = l->lastSentMs = now_ms();
    return 0;
}

/* `setupOverride` is the self-test's way to pose as a different build; NULL is this one. */
static int32_t link_join(P2PLink *l, const char *address, uint16_t port, const Setup *setupOverride)
{
    struct in_addr addr;

    link_reset(l);
    if (!address || inet_pton(AF_INET, address, &addr) != 1) {
        l->state   = P2P_FAILED;
        l->failure = P2P_FAIL_BAD_ADDRESS;
        return 1;
    }

    P2PLinkFailure bad = open_socket(l, port, kJoinPortTries);
    if (bad != P2P_FAIL_NONE) {
        l->state   = P2P_FAILED;
        l->failure = bad;
        return 1;
    }

    setup_local(&l->local);
    if (setupOverride)
        l->local = *setupOverride;

    struct sockaddr_in host;
    memset(&host, 0, sizeof host);
    host.sin_family = AF_INET;
    host.sin_addr   = addr;
    host.sin_port   = htons(port);
    set_peer(l, &host);

    l->isHost   = 0;
    l->state    = P2P_JOINING;
    l->openedMs = l->lastHeardMs = l->lastSentMs = now_ms();
    send_setup_packet(l, &l->peer, PKT_HELLO);
    return 0;
}

static void link_close(P2PLink *l)
{
    if (l->sock >= 0 && l->state == P2P_CONNECTED && l->hasPeer) {
        /* Twice: it is never retried, and it saves the other player a six-second wait. */
        send_bye(l);
        send_bye(l);
    }
    link_reset(l);
}

/* ---- the game's link ---- */

/* NAT traversal: both peers call this, with the addresses the matchmaker gave
 * them for the other one, and both send to all of them at once.
 *
 * There is no host and no joiner here. A NAT lets a datagram IN only after one
 * has gone OUT to that address, so a peer that merely listens is unreachable;
 * both have to be punching at the same time. The player number decides who is
 * player 1, and once connected which path the two settle on: several candidates
 * may all work, and each side may have found a different one first (the
 * CONNECTED case of handle_packet) -- player 2 follows player 1's.
 *
 * The local port matters: it is the one the matchmaker saw us on, so the
 * mapping the server's traffic already opened is the one the peer's packets
 * arrive through. Binding a different port throws that away. */
static int32_t link_punch(P2PLink *l, const char *const *addresses, int32_t count,
                          uint16_t localPort)
{
    int32_t i, n = 0;

    link_reset(l);
    if (!addresses || count <= 0) {
        l->state = P2P_FAILED;
        l->failure = P2P_FAIL_BAD_ADDRESS;
        return 1;
    }

    for (i = 0; i < count && n < kMaxCandidates; i++) {
        char text[24], *colon;
        struct in_addr host;
        uint16_t port = 0;

        if (!addresses[i] || !addresses[i][0])
            continue;
        snprintf(text, sizeof text, "%s", addresses[i]);
        colon = strchr(text, ':');
        if (colon) {
            *colon = '\0';
            port = (uint16_t)atoi(colon + 1);
        }
        if (!port || inet_pton(AF_INET, text, &host) != 1)
            continue;               /* not an address we can punch to; skip it */
        memset(&l->candidates[n], 0, sizeof l->candidates[n]);
        l->candidates[n].sin_family = AF_INET;
        l->candidates[n].sin_addr   = host;
        l->candidates[n].sin_port   = htons(port);
        n++;
    }
    if (n == 0) {
        l->state = P2P_FAILED;
        l->failure = P2P_FAIL_BAD_ADDRESS;
        return 1;
    }
    l->numCandidates = n;

    {
        P2PLinkFailure bad = open_socket(l, localPort, kJoinPortTries);
        if (bad != P2P_FAIL_NONE) {
            l->state = P2P_FAILED;
            l->failure = bad;
            return 1;
        }
    }
    setup_local(&l->local);
    l->isHost = 0;
    l->state  = P2P_JOINING;        /* "connecting", as the menus already read it */
    l->openedMs = l->lastHeardMs = l->lastSentMs = now_ms();
    punch_send(l);                  /* open our side's mappings immediately */
    return 0;
}

int32_t P2PLink_Punch(const char *const *addresses, int32_t count, uint16_t localPort)
{
    return link_punch(&s_link, addresses, count, localPort);
}

void P2PLink_SetRelay(uint64_t token, int32_t player)
{
    s_link.relayToken  = token;
    s_link.relayPlayer = token ? player : 0;
    s_link.relayAcked  = 0;
    s_link.punchPlayer = player;        /* direct too: which side settles the path */
}

uint64_t P2PLink_RelayToken(void) { return s_link.relayToken; }

void P2PLink_SetRelayFallback(uint64_t token, int32_t player, const char *server)
{
    char text[64], *colon;
    struct in_addr host;
    uint16_t port = 0;

    s_link.fbToken = 0;
    s_link.fbUsed  = 0;
    if (!token || !server || !server[0])
        return;
    snprintf(text, sizeof text, "%s", server);
    colon = strchr(text, ':');
    if (colon) {
        *colon = '\0';
        port = (uint16_t)atoi(colon + 1);
    }
    if (!port || inet_pton(AF_INET, text, &host) != 1)
        return;
    memset(&s_link.fbServer, 0, sizeof s_link.fbServer);
    s_link.fbServer.sin_family = AF_INET;
    s_link.fbServer.sin_addr   = host;
    s_link.fbServer.sin_port   = htons(port);
    s_link.fbToken  = token;
    s_link.fbPlayer = player;
}

int32_t P2PLink_FellBackToRelay(void) { return s_link.fbUsed; }

void P2PLink_SetServer(const char *address)
{
    char text[64];
    char *colon;
    struct in_addr host;
    int port = 0;

    s_link.haveServer = 0;
    s_link.serverAnswerLen = 0;
    if (!address || !address[0])
        return;
    snprintf(text, sizeof text, "%s", address);
    colon = strchr(text, ':');
    if (colon) {
        *colon = '\0';
        port = atoi(colon + 1);
    }
    if (port <= 0 || port > 65535 || inet_pton(AF_INET, text, &host) != 1)
        return;
    memset(&s_link.server, 0, sizeof s_link.server);
    s_link.server.sin_family = AF_INET;
    s_link.server.sin_addr   = host;
    s_link.server.sin_port   = htons((uint16_t)port);
    s_link.haveServer = 1;
}

int32_t P2PLink_HasServer(void) { return s_link.haveServer; }

/* Raw, as the relay's control packets are: the server's own formats, not the link's. */
int32_t P2PLink_AskServer(const uint8_t *data, int32_t len)
{
    P2PLink *l = &s_link;
    if (!l->haveServer || l->sock < 0 || len <= 0)
        return 1;
    if (Netem_LinkSend(l->sock, (const char *)data, (int)len, 0,
                       (const struct sockaddr *)&l->server, (int)sizeof l->server))
        sendto(l->sock, (const char *)data, (size_t)len, 0,
               (const struct sockaddr *)&l->server, (socklen_t)sizeof l->server);
    return 0;
}

int32_t P2PLink_ServerAnswer(const char *magic, uint8_t *out, int32_t max)
{
    int32_t n = s_link.serverAnswerLen;
    if (n <= 0 || n > max || memcmp(s_link.serverAnswer, magic, 4) != 0)
        return 0;
    memcpy(out, s_link.serverAnswer, (size_t)n);
    s_link.serverAnswerLen = 0;
    return n;
}

/* The direct-IP menu: never relayed, and no server arranged it, whatever the last
 * matchmaking session was. */
int32_t P2PLink_Host(uint16_t port)
{
    P2PLink_SetServer(NULL);
    P2PLink_SetRelay(0, 0);
    P2PLink_SetRelayFallback(0, 0, NULL);
    return link_host(&s_link, port);
}
int32_t P2PLink_Join(const char *address, uint16_t port)
{
    P2PLink_SetServer(NULL);
    P2PLink_SetRelay(0, 0);
    P2PLink_SetRelayFallback(0, 0, NULL);
    return link_join(&s_link, address, port, NULL);
}
void    P2PLink_Poll(void)                             { link_poll(&s_link, now_ms()); }
void    P2PLink_Close(void)                            { link_close(&s_link); }

P2PLinkState   P2PLink_State(void)     { return s_link.state; }
P2PLinkFailure P2PLink_Failure(void)   { return s_link.failure; }
P2PLinkFailure P2PLink_Notice(void)    { return s_link.notice; }
const char    *P2PLink_PeerText(void)  { return s_link.peerText; }
uint16_t       P2PLink_LocalPort(void) { return s_link.localPort; }

int32_t P2PLink_AgreedDelay(void)
{
    const P2PLink *l = &s_link;
    if (l->state != P2P_CONNECTED || l->delayLocal < 0 || l->delayPeer < 0)
        return -1;
    /* Max, computed from the same two numbers on both peers, so both get the
     * same answer. See the note above propose_delay. */
    return l->delayLocal > l->delayPeer ? l->delayLocal : l->delayPeer;
}

int32_t P2PLink_AgreedSimWidth(void)
{
    const P2PLink *l = &s_link;
    int32_t w;
    if (l->state != P2P_CONNECTED || l->peerScreenWidth == 0)
        return -1;
    /* The wider of the two: then neither player sees a margin that does not animate,
     * and both sides compute it from the same two numbers. */
    w = l->local.screenWidth > l->peerScreenWidth ? l->local.screenWidth : l->peerScreenWidth;
    if (w < SCREEN_WIDTH)
        w = SCREEN_WIDTH;
    if (w > PITCH_VIEW_MAX_WIDTH)
        w = PITCH_VIEW_MAX_WIDTH;
    return w;
}

/* ---- messages, from the outside ---- */

int32_t P2PLink_Send(int32_t channel, const uint8_t *data, uint32_t len)
{
    return message_send(&s_link, channel, data, len);
}

uint16_t P2PLink_SentGeneration(int32_t channel)
{
    if (channel < 0 || channel >= P2P_NUM_CHANNELS)
        return 0;
    return s_link.tx[channel].generation;
}

int32_t P2PLink_Delivered(int32_t channel)
{
    return message_delivered(&s_link, channel);
}

const uint8_t *P2PLink_Received(int32_t channel, uint32_t *len, uint16_t *generation)
{
    return message_received(&s_link, channel, len, generation);
}

int32_t P2PLink_TransferProgress(void)
{
    const P2PLink *l = &s_link;
    int32_t c, i, have = 0, n = 0;
    for (c = 0; c < P2P_NUM_CHANNELS; c++) {
        const TxChannel *t = &l->tx[c];
        const RxChannel *r = &l->rx[c];
        if (t->data && !bitmap_full(t->acked, t->chunks)) {
            n += t->chunks;
            for (i = 0; i < t->chunks; i++) have += bitmap_get(t->acked, i);
        }
        if (r->data) {
            n += r->chunks;
            for (i = 0; i < r->chunks; i++) have += bitmap_get(r->have, i);
        }
    }
    if (n <= 0)
        return -1;
    return have * 100 / n;
}

int32_t P2PLink_ProposedDelay(void) { return s_link.delayLocal; }
int32_t P2PLink_PeerProposedDelay(void) { return s_link.delayPeer; }
int32_t        P2PLink_PingMs(void)    { return s_link.pingMs; }
int32_t        P2PLink_JitterMs(void)  { return s_link.jitterMs; }
int32_t        P2PLink_ProposedPingMs(void)   { return s_proposedPing; }
int32_t        P2PLink_ProposedJitterMs(void) { return s_proposedJitter; }

int32_t P2PLink_IsAddress(const char *text)
{
    struct in_addr addr;
    return text && inet_pton(AF_INET, text, &addr) == 1;
}

/* ---- self-test ---- */

static const char *state_name(P2PLinkState s)
{
    switch (s) {
    case P2P_IDLE:      return "idle";
    case P2P_HOSTING:   return "hosting";
    case P2P_JOINING:   return "joining";
    case P2P_CONNECTED: return "connected";
    case P2P_FAILED:    return "failed";
    }
    return "?";
}

/* Poll every link for up to `ms`, stopping as soon as `done` is true. */
typedef int32_t (*DoneFn)(void *ctx);
static void pump(P2PLink **links, int32_t count, int32_t ms, DoneFn done, void *ctx)
{
    uint32_t start = now_ms();
    for (;;) {
        uint32_t now = now_ms();
        for (int32_t i = 0; i < count; i++)
            link_poll(links[i], now);
        if (done && done(ctx))
            return;
        if (now - start >= (uint32_t)ms)
            return;
        SDL_Delay(1);
    }
}

static int32_t s_failures;

static void check(int32_t ok, const char *what)
{
    printf("  %-44s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok)
        s_failures++;
}

typedef struct { P2PLink *a, *b; } PairCtx;

static int32_t both_connected(void *ctx)
{
    PairCtx *p = (PairCtx *)ctx;
    return p->a->state == P2P_CONNECTED && p->b->state == P2P_CONNECTED && p->b->pingMs >= 0;
}

static int32_t link_settled(void *ctx)
{
    P2PLink *l = (P2PLink *)ctx;
    return l->state == P2P_FAILED;
}

static int32_t both_have_delay(void *ctx)
{
    PairCtx *p = (PairCtx *)ctx;
    return p->a->delayLocal >= 0 && p->a->delayPeer >= 0 &&
           p->b->delayLocal >= 0 && p->b->delayPeer >= 0;
}

/* Both ways: a's newest message on channel 0 is b's newest complete one, and the
 * other way round on channel 1, and both senders have seen them acked. */
static int32_t messages_across(void *ctx)
{
    PairCtx *p = (PairCtx *)ctx;
    return p->b->rx[0].doneGeneration == p->a->tx[0].generation && message_delivered(p->a, 0) &&
           p->a->rx[1].doneGeneration == p->b->tx[1].generation && message_delivered(p->b, 1);
}

static int32_t host_waiting_again(void *ctx)
{
    P2PLink *l = (P2PLink *)ctx;
    return l->state == P2P_HOSTING && l->notice != P2P_FAIL_NONE;
}

int32_t P2PLink_SelfTest(void)
{
    P2PLink host, joiner, second, mismatched, lonely;
    P2PLink *pair[2];

    printf("[p2p-link] self-test: two links over loopback\n");
    s_failures = 0;

    memset(&host, 0, sizeof host);       host.sock = -1;       host.pingMs = -1;       host.joinTimeoutMs = kJoinTimeoutMs;
    memset(&joiner, 0, sizeof joiner);   joiner.sock = -1;     joiner.pingMs = -1;     joiner.joinTimeoutMs = kJoinTimeoutMs;
    memset(&second, 0, sizeof second);   second.sock = -1;     second.pingMs = -1;     second.joinTimeoutMs = 1000;
    memset(&mismatched, 0, sizeof mismatched); mismatched.sock = -1; mismatched.pingMs = -1; mismatched.joinTimeoutMs = 1000;
    memset(&lonely, 0, sizeof lonely);   lonely.sock = -1;     lonely.pingMs = -1;     lonely.joinTimeoutMs = 400;

    /* 1. They connect, and each measures the round trip. */
    check(link_host(&host, 0) == 0, "host binds a port");
    if (host.state != P2P_HOSTING) {
        printf("  host could not start: %s\n", state_name(host.state));
        return 1;
    }
    check(link_join(&joiner, "127.0.0.1", host.localPort, NULL) == 0, "joiner opens and says hello");

    pair[0] = &host; pair[1] = &joiner;
    PairCtx both = { &host, &joiner };
    pump(pair, 2, 2000, both_connected, &both);
    check(host.state == P2P_CONNECTED && joiner.state == P2P_CONNECTED, "both sides connect");
    check(joiner.pingMs >= 0, "the joiner measures the round trip");
    check(host.peerText[0] != '\0' && joiner.peerText[0] != '\0', "each knows the other's address");

    /* 1b. The delay is AGREED, not chosen twice. Both sides must end up with the
     * same number -- an asymmetric delay is what makes one player feel they are
     * chasing the other (see propose_delay). */
    {
        PairCtx pairBoth = { &host, &joiner };
        pump(pair, 2, 3000, both_have_delay, &pairBoth);
        check(host.delayLocal >= 0 && joiner.delayLocal >= 0, "each side proposes a delay");
        check(host.delayPeer >= 0 && joiner.delayPeer >= 0, "each side hears the other's");
        {
            int32_t a = host.delayLocal > host.delayPeer ? host.delayLocal : host.delayPeer;
            int32_t b = joiner.delayLocal > joiner.delayPeer ? joiner.delayLocal : joiner.delayPeer;
            check(a == b, "both agree the SAME delay");
            if (a != b)
                printf("    host agreed %d, joiner agreed %d\n", a, b);
        }
    }

    /* 1c. Messages cross intact, both ways at once. ~40 KB one way, which is more
     * than the real kick-off state and well past one datagram, so it exercises the
     * chunking, the bitmap ack and the retransmit rather than a happy single packet.
     *
     * Then the thing generations are for: each side sends AGAIN before the first
     * has finished -- the second message the same length as the first, which is
     * exactly the case that used to be assembled from both -- and the receiver must
     * end up with the second, whole, and never report the first. */
    {
        enum { kTestBlob = 40000, kSmall = 3000 };
        uint8_t *big  = (uint8_t *)malloc(kTestBlob);
        uint8_t *v1   = (uint8_t *)malloc(kSmall);
        uint8_t *v2   = (uint8_t *)malloc(kSmall);
        uint8_t *back = (uint8_t *)malloc(kSmall);
        const uint8_t *got;
        uint32_t gotLen = 0, i;
        uint16_t gotGen = 0;
        PairCtx pairBoth = { &host, &joiner };

        if (big && v1 && v2 && back) {
            for (i = 0; i < kTestBlob; i++)
                big[i] = (uint8_t)(i * 31u + (i >> 8));       /* not compressible to nothing */
            for (i = 0; i < kSmall; i++) {
                v1[i]   = (uint8_t)(i * 7u);
                v2[i]   = (uint8_t)(i * 13u + 1);
                back[i] = (uint8_t)(i * 17u + 3);
            }
            check(message_send(&host, 0, big, kTestBlob) == 0, "the host sends a message");
            check(message_send(&joiner, 1, back, kSmall) == 0, "the joiner sends one back");
            pump(pair, 2, 8000, messages_across, &pairBoth);
            got = message_received(&joiner, 0, &gotLen, &gotGen);
            check(got != NULL, "the joiner receives all of it");
            check(got && gotLen == kTestBlob, "it is the right length");
            check(got && gotLen == kTestBlob && memcmp(got, big, kTestBlob) == 0,
                  "every byte matches");
            check(message_delivered(&host, 0), "the host sees it acknowledged");
            got = message_received(&host, 1, &gotLen, &gotGen);
            check(got && gotLen == kSmall && memcmp(got, back, kSmall) == 0,
                  "the host has the joiner's, at the same time");

            /* Replace: v1 then v2 on the same channel, no poll in between. */
            check(message_send(&host, 0, v1, kSmall) == 0 && message_send(&host, 0, v2, kSmall) == 0,
                  "the host replaces a message before it is sent");
            check(message_send(&joiner, 1, v1, kSmall) == 0, "the joiner starts one");
            pump(pair, 2, 30, NULL, NULL);                  /* let some of it cross */
            check(message_send(&joiner, 1, v2, kSmall) == 0, "and replaces it in flight");
            pump(pair, 2, 8000, messages_across, &pairBoth);
            got = message_received(&joiner, 0, &gotLen, &gotGen);
            check(got && gotLen == kSmall && gotGen == host.tx[0].generation &&
                  memcmp(got, v2, kSmall) == 0, "the joiner ends with the newest, whole");
            got = message_received(&host, 1, &gotLen, &gotGen);
            check(got && gotLen == kSmall && gotGen == joiner.tx[1].generation &&
                  memcmp(got, v2, kSmall) == 0, "so does the host");

            /* All of it again on a wire that loses every third datagram and damages
             * every fifth -- chunks, acks and replacements alike. Retransmission has to
             * cover every one, and a damaged one must never be taken for data. */
            s_testDropEvery = 3;
            s_testTamperEvery = 5;
            s_testSendCount = 0;
            check(message_send(&host, 0, big, kTestBlob) == 0 &&
                  message_send(&joiner, 1, v1, kSmall) == 0, "on a lossy wire, both send");
            pump(pair, 2, 60, NULL, NULL);
            check(message_send(&joiner, 1, back, kSmall) == 0, "the joiner replaces its own");
            pump(pair, 2, 15000, messages_across, &pairBoth);
            got = message_received(&joiner, 0, &gotLen, &gotGen);
            check(got && gotLen == kTestBlob && gotGen == host.tx[0].generation &&
                  memcmp(got, big, kTestBlob) == 0, "every byte still arrives");
            got = message_received(&host, 1, &gotLen, &gotGen);
            check(got && gotLen == kSmall && gotGen == joiner.tx[1].generation &&
                  memcmp(got, back, kSmall) == 0, "and the replacement, not the first");
            s_testDropEvery = 0;
            s_testTamperEvery = 0;
        }
        free(big); free(v1); free(v2); free(back);
    }

    /* 1d. NAT traversal: two peers punch to each other through a list that is
     * mostly WRONG. Only one address in each list can work; the rest are
     * unroutable, which is exactly the shape of a real candidate list (a LAN
     * address on the other side of the internet, an old NAT mapping). Both
     * punch at once, as they must -- neither is listening. */
    {
        P2PLink pa, pb;
        P2PLink *both2[2];
        char aAddr[32], bAddr[32];
        const char *toB[3], *toA[3];
        PairCtx pairPunch;

        memset(&pa, 0, sizeof pa); pa.sock = -1; pa.pingMs = -1; pa.joinTimeoutMs = 6000;
        memset(&pb, 0, sizeof pb); pb.sock = -1; pb.pingMs = -1; pb.joinTimeoutMs = 6000;

        /* Bind both first so each knows the port to advertise. */
        check(link_punch(&pa, (const char *const[]){ "192.0.2.1:9" }, 1, 0) == 0,
              "peer A opens a socket and starts punching");
        check(link_punch(&pb, (const char *const[]){ "192.0.2.2:9" }, 1, 0) == 0,
              "peer B opens a socket and starts punching");
        snprintf(aAddr, sizeof aAddr, "127.0.0.1:%u", (unsigned)pa.localPort);
        snprintf(bAddr, sizeof bAddr, "127.0.0.1:%u", (unsigned)pb.localPort);

        /* Now the real lists: two dead ends (TEST-NET-1, never routed) and the
         * one address that works -- deliberately last, so finding it means
         * every candidate was tried rather than the first one happening to be
         * right. */
        toB[0] = "192.0.2.10:9999"; toB[1] = "198.51.100.7:9999"; toB[2] = bAddr;
        toA[0] = "192.0.2.11:9999"; toA[1] = "198.51.100.8:9999"; toA[2] = aAddr;
        check(link_punch(&pa, toB, 3, pa.localPort) == 0, "A punches to B's three candidates");
        check(link_punch(&pb, toA, 3, pb.localPort) == 0, "B punches to A's three candidates");

        both2[0] = &pa; both2[1] = &pb;
        pairPunch.a = &pa; pairPunch.b = &pb;
        pump(both2, 2, 6000, both_connected, &pairPunch);
        check(pa.state == P2P_CONNECTED && pb.state == P2P_CONNECTED,
              "they find each other through the dead ends");
        check(pa.peerText[0] && pb.peerText[0], "each ends up on the address that worked");
        if (pa.state != P2P_CONNECTED || pb.state != P2P_CONNECTED)
            printf("    A %s, B %s\n", state_name(pa.state), state_name(pb.state));
        link_close(&pa);
        link_close(&pb);
    }

    /* 2. A second player is turned away while the host has one. */
    check(link_join(&second, "127.0.0.1", host.localPort, NULL) == 0, "a second player tries to join");
    {
        P2PLink *three[3] = { &host, &joiner, &second };
        pump(three, 3, 1500, link_settled, &second);
    }
    check(second.state == P2P_FAILED && second.failure == P2P_FAIL_HOST_BUSY, "the host is busy, and says so");
    link_close(&second);

    /* 3. The joiner leaves: the host hears it and waits for the next player. */
    link_close(&joiner);
    pump(pair, 1, 1500, host_waiting_again, &host);
    check(host.state == P2P_HOSTING && host.notice == P2P_FAIL_PEER_LEFT, "the host sees the player leave");

    /* 4. A build that would play a different game is refused, with the reason. */
    {
        Setup theirs;
        setup_local(&theirs);
        theirs.gameplay ^= P2P_RULE_ARCADE;     /* arcade mode: a different game */
        check(link_join(&mismatched, "127.0.0.1", host.localPort, &theirs) == 0, "a mismatched build joins");
        P2PLink *two[2] = { &host, &mismatched };
        pump(two, 2, 1500, link_settled, &mismatched);
        check(mismatched.state == P2P_FAILED && mismatched.failure == P2P_FAIL_GAMEPLAY,
              "it is refused: different rules");
        check(host.state == P2P_HOSTING && host.notice == P2P_FAIL_GAMEPLAY, "the host says who bounced off");
        link_close(&mismatched);
    }
    /* 4c. The other gameplay STYLE is not refused: the joiner plays the host's. */
    {
        extern void GameplayStyle_GiveBack(void);
        extern int _gDosGameplay;
        Setup theirs;
        int before = _gDosGameplay;
        setup_local(&theirs);
        theirs.gameplay ^= P2P_RULE_DOS | P2P_RULE_DOS_VIEW;   /* the other style, view with it */
        memset(&mismatched, 0, sizeof mismatched); mismatched.sock = -1; mismatched.pingMs = -1;
        mismatched.joinTimeoutMs = 1000;
        check(link_join(&mismatched, "127.0.0.1", host.localPort, &theirs) == 0, "a player on the other style joins");
        P2PLink *two[2] = { &host, &mismatched };
        pump(two, 2, 1500, link_settled, &mismatched);
        check(mismatched.state == P2P_CONNECTED, "it is let in");
        check(_gDosGameplay == before && (mismatched.local.gameplay & P2P_RULE_DOS) == (host.local.gameplay & P2P_RULE_DOS),
              "and plays the host's style");
        link_close(&mismatched);
        pump(two, 1, 1500, host_waiting_again, &host);
        GameplayStyle_GiveBack();
    }
    /* 4a. The other view: a different simulation camera, so a different match. */
    {
        Setup theirs;
        setup_local(&theirs);
        theirs.gameplay ^= P2P_RULE_DOS_VIEW;
        memset(&mismatched, 0, sizeof mismatched); mismatched.sock = -1; mismatched.pingMs = -1;
        mismatched.joinTimeoutMs = 1000;
        check(link_join(&mismatched, "127.0.0.1", host.localPort, &theirs) == 0, "a player on the other view joins");
        P2PLink *two[2] = { &host, &mismatched };
        pump(two, 2, 1500, link_settled, &mismatched);
        check(mismatched.state == P2P_FAILED && mismatched.failure == P2P_FAIL_VIEW,
              "it is refused: a different view");
        check(host.state == P2P_HOSTING && host.notice == P2P_FAIL_VIEW, "the host says which view");
        link_close(&mismatched);
    }
    /* 4b. Another netplay number is refused before anything else is compared. */
    {
        Setup theirs;
        setup_local(&theirs);
        theirs.compat++;
        memset(&mismatched, 0, sizeof mismatched); mismatched.sock = -1; mismatched.pingMs = -1;
        mismatched.joinTimeoutMs = 1000;
        check(link_join(&mismatched, "127.0.0.1", host.localPort, &theirs) == 0, "another build joins");
        P2PLink *two[2] = { &host, &mismatched };
        pump(two, 2, 1500, link_settled, &mismatched);
        check(mismatched.state == P2P_FAILED && mismatched.failure == P2P_FAIL_VERSION,
              "it is refused: a different build");
        check(host.state == P2P_HOSTING && host.notice == P2P_FAIL_VERSION, "the host says which build");
        link_close(&mismatched);
    }
    /* 4e. Another football world is refused. */
    {
        Setup theirs;
        setup_local(&theirs);
        theirs.world ^= 1;
        memset(&mismatched, 0, sizeof mismatched); mismatched.sock = -1; mismatched.pingMs = -1;
        mismatched.joinTimeoutMs = 1000;
        check(link_join(&mismatched, "127.0.0.1", host.localPort, &theirs) == 0, "a player of another world joins");
        P2PLink *two[2] = { &host, &mismatched };
        pump(two, 2, 1500, link_settled, &mismatched);
        check(mismatched.state == P2P_FAILED && mismatched.failure == P2P_FAIL_WORLD,
              "it is refused: another world");
        check(host.state == P2P_HOSTING && host.notice == P2P_FAIL_WORLD, "the host says which world");
        link_close(&mismatched);
    }
    /* 4d. Another commit with the same number plays -- unless either side has
     * uncommitted changes. */
    for (int32_t dirty = 0; dirty <= 1; dirty++) {
        Setup theirs;
        const uint8_t hostDirty = host.local.dirty;
        setup_local(&theirs);
        theirs.build[7] = theirs.build[7] == '0' ? '1' : '0';   /* one digit of the commit */
        theirs.dirty = (uint8_t)dirty;
        host.local.dirty = 0;           /* whatever tree this test was built from */
        memset(&mismatched, 0, sizeof mismatched); mismatched.sock = -1; mismatched.pingMs = -1;
        mismatched.joinTimeoutMs = 1000;
        check(link_join(&mismatched, "127.0.0.1", host.localPort, &theirs) == 0, "another commit joins");
        P2PLink *two[2] = { &host, &mismatched };
        pump(two, 2, 1500, link_settled, &mismatched);
        if (dirty)
            check(mismatched.state == P2P_FAILED && mismatched.failure == P2P_FAIL_VERSION,
                  "with uncommitted changes it is refused");
        else
            check(mismatched.state == P2P_CONNECTED, "with the same netplay number it is let in");
        link_close(&mismatched);
        if (!dirty)     /* the host takes its next player once it has heard this one go */
            pump(two, 1, 1500, host_waiting_again, &host);
        host.local.dirty = hostDirty;
    }

    /* 5. Nobody there: the join gives up rather than hanging.
     *
     * The empty port comes from the system, released again at once. It used to be the
     * host's port with a bit flipped, but a joiner binds the port it dials (or one of
     * the next kJoinPortTries), and on Windows a derived number can land in a block
     * reserved by Hyper-V or WSL2, where every one of those binds fails and the join
     * never starts. Which block depended on the host's ephemeral port, so the check
     * passed or failed from one run to the next. */
    {
        P2PLink probe;
        memset(&probe, 0, sizeof probe);
        probe.sock = -1;
        open_socket(&probe, 0, 1);
        uint16_t empty = probe.localPort;
        link_reset(&probe);
        P2PLink *one[1] = { &lonely };
        check(empty != 0, "the system has a free port to dial");
        check(link_join(&lonely, "127.0.0.1", empty, NULL) == 0, "a join with nobody listening");
        pump(one, 1, 2000, link_settled, &lonely);
        check(lonely.state == P2P_FAILED && lonely.failure == P2P_FAIL_NO_ANSWER, "it gives up after the timeout");
        link_close(&lonely);
    }

    link_close(&host);
    printf("[p2p-link] self-test %s\n", s_failures ? "FAILED" : "passed");
    fflush(stdout);
    return s_failures != 0;
}

#else /* !SWOS_NETPLAY -- the builds with no GGPO to play through either */

int32_t P2PLink_Host(uint16_t port)                      { (void)port; return 1; }
int32_t P2PLink_Join(const char *address, uint16_t port) { (void)address; (void)port; return 1; }
void    P2PLink_Poll(void)                               { }
void    P2PLink_Close(void)                              { }

P2PLinkState   P2PLink_State(void)     { return P2P_FAILED; }
P2PLinkFailure P2PLink_Failure(void)   { return P2P_FAIL_UNAVAILABLE; }
P2PLinkFailure P2PLink_Notice(void)    { return P2P_FAIL_NONE; }
const char    *P2PLink_PeerText(void)  { return ""; }
uint16_t       P2PLink_LocalPort(void) { return 0; }
int32_t        P2PLink_PingMs(void)    { return -1; }
int32_t        P2PLink_JitterMs(void)  { return -1; }
int32_t        P2PLink_ProposedPingMs(void)   { return -1; }
int32_t        P2PLink_ProposedJitterMs(void) { return -1; }
int32_t        P2PLink_IsAddress(const char *text) { (void)text; return 0; }

/* The matchmaking handoff and the agreed input delay. These exist here for the
 * same reason as everything above: online_menus.c and netplay_match.c call them
 * unconditionally, and a build without SWOS_NETPLAY has to link. Missing them is
 * what broke the macOS build (NETPLAY defaults OFF there) while Linux and Cygwin
 * stayed green — so add the stub WITH the function, not after a release. */
int32_t P2PLink_Punch(const char *const *addresses, int32_t count, uint16_t localPort)
{
    (void)addresses; (void)count; (void)localPort;
    return 1;
}
void           P2PLink_SetRelay(uint64_t token, int32_t player) { (void)token; (void)player; }
uint64_t       P2PLink_RelayToken(void) { return 0; }
void           P2PLink_SetRelayFallback(uint64_t token, int32_t player, const char *server)
               { (void)token; (void)player; (void)server; }
int32_t        P2PLink_FellBackToRelay(void) { return 0; }
void           P2PLink_SetServer(const char *address) { (void)address; }
int32_t        P2PLink_HasServer(void) { return 0; }
int32_t        P2PLink_AskServer(const uint8_t *data, int32_t len) { (void)data; (void)len; return 1; }
int32_t        P2PLink_ServerAnswer(const char *magic, uint8_t *out, int32_t max)
{
    (void)magic; (void)out; (void)max;
    return 0;
}
uint8_t        P2PLink_GameplayFlags(void) { return 0; }
int32_t        P2PLink_AgreedDelay(void)       { return -1; }
int32_t        P2PLink_AgreedSimWidth(void)    { return -1; }
int32_t        P2PLink_ProposedDelay(void)     { return -1; }
int32_t        P2PLink_PeerProposedDelay(void) { return -1; }
int32_t        P2PLink_Send(int32_t channel, const uint8_t *data, uint32_t len)
{
    (void)channel; (void)data; (void)len;
    return 1;
}
uint16_t       P2PLink_SentGeneration(int32_t channel) { (void)channel; return 0; }
int32_t        P2PLink_Delivered(int32_t channel)      { (void)channel; return 0; }
const uint8_t *P2PLink_Received(int32_t channel, uint32_t *len, uint16_t *generation)
{
    (void)channel;
    if (len) *len = 0;
    if (generation) *generation = 0;
    return NULL;
}
int32_t        P2PLink_TransferProgress(void) { return -1; }

int32_t P2PLink_SelfTest(void)
{
    printf("[p2p-link] self-test: built without netplay (make NETPLAY=1), nothing to test\n");
    return 0;
}

#endif /* SWOS_NETPLAY */

void P2PLink_FailureText(P2PLinkFailure failure, const char **line1, const char **line2)
{
    switch (failure) {
    case P2P_FAIL_UNAVAILABLE:
        *line1 = "NETWORK PLAY IS NOT IN THIS BUILD"; *line2 = ""; break;
    case P2P_FAIL_BAD_ADDRESS:
        *line1 = "THAT IS NOT AN IP ADDRESS"; *line2 = "TYPE IT LIKE 192.168.1.20"; break;
    case P2P_FAIL_PORT_IN_USE:
        *line1 = "THAT UDP PORT IS ALREADY IN USE"; *line2 = "CHOOSE ANOTHER ONE"; break;
    case P2P_FAIL_PORT_RESERVED:
        *line1 = "THE SYSTEM RESERVES THAT UDP PORT"; *line2 = "CHOOSE ANOTHER ONE"; break;
    case P2P_FAIL_SOCKET:
        *line1 = "THIS MACHINE WOULD NOT OPEN A SOCKET"; *line2 = ""; break;
    case P2P_FAIL_NO_ANSWER:
        *line1 = "THE HOST DID NOT ANSWER"; *line2 = "CHECK THE ADDRESS, PORT AND FIREWALL"; break;
    case P2P_FAIL_LOST:
        *line1 = "THE CONNECTION WENT QUIET"; *line2 = ""; break;
    case P2P_FAIL_PEER_LEFT:
        *line1 = "THE OTHER PLAYER LEFT"; *line2 = ""; break;
    case P2P_FAIL_HOST_BUSY:
        *line1 = "THE HOST ALREADY HAS A PLAYER"; *line2 = ""; break;
    case P2P_FAIL_PROTOCOL:
    case P2P_FAIL_STATE:
    case P2P_FAIL_VERSION:
        *line1 = "THE OTHER PLAYER HAS ANOTHER VERSION"; *line2 = "ONE OF YOU NEEDS TO UPDATE"; break;
    case P2P_FAIL_GAMEPLAY:
        /* Not the style any more -- the joiner plays the host's -- but arcade mode or
         * the original's fire-hold lockout ([gameplay] fire_hold_lockout). */
        *line1 = "YOU PLAY BY DIFFERENT RULES"; *line2 = "CHECK FIRE_HOLD_LOCKOUT IN SWOS.INI"; break;
    case P2P_FAIL_VIEW:
        /* Only a view pinned with --view: SWOS.ini has no view setting any more. */
        *line1 = "ONE OF YOU PINNED THE VIEW"; *line2 = "RUN WITHOUT --VIEW"; break;
    case P2P_FAIL_WIDTH:
        *line1 = "YOUR SCREEN WIDTHS ARE DIFFERENT"; *line2 = "BOTH OF YOU NEED THE SAME ONE"; break;
    case P2P_FAIL_RELAY:
        *line1 = "THE SERVER'S RELAY HAS CLOSED"; *line2 = "FIND EACH OTHER AGAIN"; break;
    case P2P_FAIL_BUILD:
        *line1 = "YOUR BUILDS PLAY BY DIFFERENT RULES"; *line2 = "ONE OF YOU HAS A TEST BUILD"; break;
    case P2P_FAIL_WORLD:
        *line1 = "YOU PLAY DIFFERENT WORLDS"; *line2 = "PICK THE SAME WORLD IN SWOS.INI"; break;
    default:
        *line1 = ""; *line2 = ""; break;
    }
}

#endif /* SWOS_SDL_BUILD */
