/*
 * p2p_link.h -- the connection two players make before a netplay match (SDL builds).
 * docs/NETPLAY.md, "The MULTIPLAYER menu".
 *
 * A direct UDP link, no server: one player hosts on a port, the other joins by the
 * host's IPv4 address. The joiner says hello, the host answers, and each side checks
 * the other would play the same match -- the same protocol, the same state registry
 * (SimState_LayoutHash), the same gameplay style, the same screen width -- because a
 * session that disagrees on any of them desyncs on the first frame. While connected
 * both sides ping once a second, so a player sees the round trip before choosing an
 * input delay.
 *
 * This is only the handshake: GGPO opens its own socket for the match itself
 * (src/netplay.c). The link closes before it does, on the same local port, which is
 * also what keeps a NAT mapping open for the session that follows.
 *
 * Without SWOS_NETPLAY (make NETPLAY=1) there is no socket and every call fails with
 * P2P_FAIL_UNAVAILABLE -- the same builds that have no GGPO to play through.
 */
#ifndef P2P_LINK_H
#define P2P_LINK_H

#ifdef SWOS_SDL_BUILD

#include "swos_types.h"

typedef enum {
    P2P_IDLE,        /* no socket */
    P2P_HOSTING,     /* waiting for someone to join */
    P2P_JOINING,     /* saying hello to the host */
    P2P_CONNECTED,
    P2P_FAILED,      /* P2PLink_Failure says why; the socket is closed */
} P2PLinkState;

typedef enum {
    P2P_FAIL_NONE,
    P2P_FAIL_UNAVAILABLE,   /* built without SWOS_NETPLAY */
    P2P_FAIL_BAD_ADDRESS,   /* not a dotted IPv4 address */
    P2P_FAIL_PORT_IN_USE,
    P2P_FAIL_SOCKET,        /* the socket itself could not be opened */
    P2P_FAIL_NO_ANSWER,     /* the host never answered */
    P2P_FAIL_LOST,          /* nothing heard from the other player for too long */
    P2P_FAIL_PEER_LEFT,     /* the other player left the menu */
    P2P_FAIL_HOST_BUSY,     /* the host already has a player */
    P2P_FAIL_PROTOCOL,      /* a different netplay protocol version */
    P2P_FAIL_STATE,         /* a different match-state registry (a different build) */
    P2P_FAIL_GAMEPLAY,      /* a different gameplay style */
    P2P_FAIL_WIDTH,         /* a different screen width */
    P2P_FAIL_PORT_RESERVED, /* the system will not let anyone bind that port (EACCES) */
    P2P_FAIL_RELAY,         /* the server's relay does not know our token (expired, or never) */
    P2P_FAIL_BUILD,         /* the builds play by different rules (a test build and a release) */
    P2P_FAIL_VERSION,       /* a different build: another commit, or uncommitted changes */
    P2P_FAIL_VIEW,          /* one plays the DOS view, the other the Amiga view */
    P2P_FAIL_WORLD,         /* another football world (include/world.h) */
    P2P_FAIL_MODS,          /* other mods that change the match (Mods_OnlineSignature) */
} P2PLinkFailure;

/* The rules a match is played by (P2PLink_GameplayFlags), one bit each. */
#define P2P_RULE_DOS            0x01    /* DOS gameplay */
#define P2P_RULE_ARCADE         0x02    /* arcade mode */
#define P2P_RULE_FIRE_LOCKOUT   0x04    /* the original's fire-hold lockout */
#define P2P_RULE_NO_BOOKINGS    0x08    /* played without cards: the old tester builds, or the host's */
#define P2P_RULE_NO_INJURIES    0x10    /* ... CARDS/INJURIES switch. Recordings only, never the handshake */
#define P2P_RULE_DOS_VIEW       0x20    /* the DOS view: the simulation camera sees 320x200 */
#define P2P_RULE_VIEW_KNOWN     0x40    /* this byte says the view (a .SRP from before it did not) */
#define P2P_RULE_ONE_TICK       0x80    /* one clock tick a frame (a .SRP without it: two -- NetplayMatch_OwnsClock) */
#define P2P_RULES_BUILD         (P2P_RULE_NO_BOOKINGS | P2P_RULE_NO_INJURIES)

/* Open the link. Both return 0, or non-zero with the reason in P2PLink_Failure.
 * The joiner binds the same port number when it is free -- on one machine, or behind
 * one router, the next free port above it. */
/* NAT traversal. BOTH peers call this with the addresses the matchmaker gave
 * them for the other, and both send to all of them at once.
 *
 * There is no host and no joiner: a NAT lets a datagram in only after one has
 * gone out to that address, so a peer that merely listens cannot be reached.
 * Simultaneous punching is what connects two players behind different routers
 * without either of them forwarding a port.
 *
 * `localPort` should be the port the matchmaker saw us on -- the mapping its
 * own traffic opened is the one the peer's packets will arrive through.
 *
 * Each address is "a.b.c.d:port"; unparseable ones are skipped. The link ends up
 * on whichever address actually answered, which a NAT may have rewritten. */
int32_t        P2PLink_Punch(const char *const *addresses, int32_t count, uint16_t localPort);

/* A RELAYED match (include/relay_proto.h): the one address given to P2PLink_Punch
 * is the matchmaking server, and every punch round also registers us there as
 * `player` with the server's `token`. With token 0 (direct) `player` still matters
 * when punching to several candidates: the two sides may each find a different working
 * path, and they settle on player 1's. Kept across P2PLink_Punch calls, so a
 * rematch through the lobby registers again by itself. token 0 = direct, which
 * the direct-IP entry points (Host, Join) also set. */
void           P2PLink_SetRelay(uint64_t token, int32_t player);
/* Which player this side is (1 the host, 2 the joiner) on a link that PUNCHES, without
 * touching the relay: what decides who follows whose gameplay style (link_connected).
 * A punched link otherwise has both sides joining, and both borrowed the other's style
 * -- the direct-IP lobby's rematch, and the test harness's every link: AMIGA against DOS
 * or XBOX swapped and went out of step at frame 1 (2026-11-02). The matchmaker's games
 * get it from P2PLink_SetRelay. Kept across P2PLink_Punch calls, as that is. */
void           P2PLink_SetPlayer(int32_t player);
/* The token of the relayed match in progress, or 0 when it is played direct. */
uint64_t       P2PLink_RelayToken(void);
/* The relay as a FALLBACK: the server made a session (`token`, as `player`) in case the
 * direct punch fails, at `server` ("a.b.c.d:port"). Punching stays direct; a link with no
 * round trip after a few seconds (none at all, or one way only) starts again through the
 * server (p2p_link.c, relay_fallback). token 0 = none. Set before P2PLink_Punch. */
void           P2PLink_SetRelayFallback(uint64_t token, int32_t player, const char *server);
/* 1 once the link has moved to the fallback relay. */
int32_t        P2PLink_FellBackToRelay(void);
/* The matchmaking server that arranged this match ("a.b.c.d:port"; "" or NULL = none,
 * which the direct-IP entry points also set). The lobby asks it whether the two teams
 * are canonical (include/team_proto.h): AskServer sends one datagram to it from the
 * link's own socket (0 = sent), and ServerAnswer hands over the newest datagram that
 * came back carrying `magic` (its length, 0 = none; taken once). */
void           P2PLink_SetServer(const char *address);
int32_t        P2PLink_HasServer(void);
int32_t        P2PLink_AskServer(const uint8_t *data, int32_t len);
int32_t        P2PLink_ServerAnswer(const char *magic, uint8_t *out, int32_t max);
/* The rules this build and these settings play a match by (P2P_RULE_*): what the
 * handshake compares, and what a recording reports. */
uint8_t        P2PLink_GameplayFlags(void);

int32_t        P2PLink_Host(uint16_t port);
int32_t        P2PLink_Join(const char *address, uint16_t port);

/* Once a frame: read what arrived, send what is due, time out. Cheap when idle. */
void           P2PLink_Poll(void);

/* Leave: tell a connected player, close the socket, back to P2P_IDLE. Idempotent. */
void           P2PLink_Close(void);

P2PLinkState   P2PLink_State(void);
P2PLinkFailure P2PLink_Failure(void);

/* Two lines a menu can show for a failure, never NULL ("" for none). Every screen
 * that can end in a failed link says WHY with these -- "CONNECTION FAILED" on its
 * own sent a player (and a test run) looking in the wrong place. */
void           P2PLink_FailureText(P2PLinkFailure failure, const char **line1, const char **line2);

/* What happened to the last player who tried, while hosting continues: one who was
 * turned away (a mismatch, or the host was busy), or the one who left or went quiet.
 * P2P_FAIL_NONE once hosting starts again. */
P2PLinkFailure P2PLink_Notice(void);

/* The other player as "a.b.c.d:port", or "" before there is one. */
const char    *P2PLink_PeerText(void);
/* The port actually bound, which is not the port asked for when that one was taken. */
uint16_t       P2PLink_LocalPort(void);
/* The round trip in milliseconds, or -1 until one has been measured. */
int32_t        P2PLink_PingMs(void);
int32_t        P2PLink_JitterMs(void);   /* mean deviation of a round trip, ms */
/* The ping and jitter the input delay was proposed from; kept after the link resets. */
int32_t        P2PLink_ProposedPingMs(void);
int32_t        P2PLink_ProposedJitterMs(void);

/* The input delay, in frames, that BOTH peers will use -- or -1 until the two
 * have exchanged proposals.
 *
 * It is agreed rather than measured independently on each side, because that is
 * the bug that makes one player feel like they are chasing the other: SWOS 2020
 * derives a delay from each machine's own ping, jitter and frame time and
 * applies it only to that machine's player, so a match that began "AUTO 3" can
 * end up 1 against 5 (src/p2p_link.c, propose_delay). Here each peer proposes
 * and both take the maximum -- the same two numbers on both sides, so the same
 * answer -- and it is then fixed for the match. */
int32_t        P2PLink_AgreedDelay(void);

/* The simulation draw width both peers will play the match at (display.h,
 * _sim_draw_width): the wider of their two screens, clamped to 336..672. -1 until
 * connected. Two players on a 4:3 and a 16:9 display used to be refused; this is what
 * lets them play. */
int32_t        P2PLink_AgreedSimWidth(void);

/* The two halves of that, for a menu line or a log: what we asked for, and what
 * they asked for. -1 until known. */
int32_t        P2PLink_ProposedDelay(void);
int32_t        P2PLink_PeerProposedDelay(void);

/* ---- messages --------------------------------------------------------------
 * Delivered, whole, and replaceable: what the lobby exchanges before a match
 * (src/netplay_lobby.c) -- each player's selection, the match setup, the answer
 * to it. Up to 128 KB each, sent as numbered chunks the receiver acks by bitmap.
 *
 * Each kind of message has a CHANNEL, 0..P2P_NUM_CHANNELS-1, in each direction.
 * Sending on a channel again REPLACES the message there: it gets the next
 * generation, and the receiver keeps only the newest complete one, so a player
 * who changes their mind twice is never seen with the first choice after the
 * second. Generations start at 1 with each connection.
 *
 * The payload is the caller's; encode it portably (endian_helpers.h).
 *
 * Buffers stay valid until the next poll that completes a newer message on the
 * same channel, or until the link closes -- copy out what you keep. */
#define P2P_NUM_CHANNELS 6     /* 0-2 the lobby, 3 the match report, 4 PRED (match_prediction.h), 5 the chat */

/* Queue a message, replacing the channel's last. 0 on success (connected only). */
int32_t        P2PLink_Send(int32_t channel, const uint8_t *data, uint32_t len);
/* The generation the last Send on `channel` got, 0 if none. */
uint16_t       P2PLink_SentGeneration(int32_t channel);
/* The last Send on `channel` has arrived whole at the other end. */
int32_t        P2PLink_Delivered(int32_t channel);
/* The newest complete message on `channel` from the other end, with its
 * generation, or NULL. */
const uint8_t *P2PLink_Received(int32_t channel, uint32_t *len, uint16_t *generation);
/* 0..100 over every message still crossing, either way; -1 when none is. */
int32_t        P2PLink_TransferProgress(void);

/* Whether `text` is a dotted IPv4 address -- what P2PLink_Join takes. */
int32_t        P2PLink_IsAddress(const char *text);

/* Two links over loopback: they connect, ping, refuse a mismatched game, notice a
 * player leaving, turn away a second one, and give up on a host that is not there.
 * Prints a report; 0 on success. Part of `--netplay-selftest`. */
int32_t        P2PLink_SelfTest(void);

#endif /* SWOS_SDL_BUILD */
#endif /* P2P_LINK_H */
