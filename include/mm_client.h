/*
 * mm_client.h -- the matchmaking client (SDL builds).
 *
 * Online play in SWOS 2020 goes through a matchmaking server rather than players
 * swapping IP addresses: you log in, you see who else is on and what games are
 * open, you create or join one, and the server introduces the two of you. We
 * speak that same protocol, against the same server, vendored and run locally
 * (third_party/matchmaker, scripts/run_matchmaker.sh, docs/NETPLAY.md section 7).
 *
 * THE SERVER ONLY INTRODUCES YOU. Its START message hands each side the other's
 * candidate addresses and then it is out of the loop -- the match itself is
 * peer-to-peer over GGPO (src/netplay.c), reached through the same p2p_link
 * handshake the old direct-IP menu used (src/p2p_link.c). So the server going
 * down mid-match does not end the match; it only stops new ones being made.
 *
 * POLL AND READ, NOT CALLBACKS. The upstream C++ client delivers everything
 * through std::function callbacks. Menus here redraw from state every frame, so
 * this one keeps the lobby in plain arrays you can read whenever you like, and
 * reserves one-shot "take" calls for the things that are genuinely events (a
 * player asking to join; being thrown out). Call MmClient_Poll() once a frame.
 *
 * Without SWOS_NETPLAY (make NETPLAY=1) there is no ENET and every call fails
 * with MM_FAIL_UNAVAILABLE -- the same builds that have no GGPO to play through.
 */
#ifndef MM_CLIENT_H
#define MM_CLIENT_H

#include "project_identity.h"

#ifdef SWOS_SDL_BUILD

#include "swos_types.h"

/* The server's own limits are generous; these are what a menu can show. A list
 * longer than this is truncated, never overrun -- see the note in mm_client.c. */
#define MM_NAME_MAX        32
#define MM_MAX_USERS       64
#define MM_MAX_GAMES       32
#define MM_MAX_IN_GAME      8
#define MM_MAX_PEER_ADDRS   8
#define MM_ADDR_TEXT_MAX   24      /* "255.255.255.255:65535" + slack */

#define MM_DEFAULT_PORT    19602   /* third_party/matchmaker/OPENSWOS.md */
/* The public server, OpenSWOS's own since 2026-10-20 (was us.swos.eu, Redhair's;
 * docs/NETPLAY_SETUP.md, docs/INDEX.md). A default, not a lock:
 * the SERVER row and [netplay] matchmaker override it, and SWOS_MM_HOST overrides both --
 * which every local test sets, so none of them reaches this server by accident. */
#define MM_DEFAULT_HOST    PROJECT_MM_HOST   /* online.openswos.net (project_identity.h) */

typedef enum {
    MM_IDLE,          /* no socket */
    MM_CONNECTING,    /* ENET connecting, or logging in */
    MM_CONNECTED,     /* logged in: the lobby is live */
    MM_FAILED,        /* MmClient_Failure says why; the socket is closed */
} MmState;

typedef enum {
    MM_FAIL_NONE,
    MM_FAIL_UNAVAILABLE,   /* built without SWOS_NETPLAY */
    MM_FAIL_BAD_ADDRESS,   /* the server address did not resolve */
    MM_FAIL_NO_SOCKET,     /* could not create the local ENET host */
    MM_FAIL_NO_ANSWER,     /* the server never completed the connection */
    MM_FAIL_LOST,          /* connected, then the server went away */
} MmFailure;

typedef struct { char name[MM_NAME_MAX]; } MmUser;
typedef struct { char owner[MM_NAME_MAX]; } MmGame;

/* The game you are in, as host or joiner. `requested` are players waiting for
 * the host to approve them; as host they are yours to approve or eject. */
typedef struct {
    char    owner[MM_NAME_MAX];
    MmUser  joined[MM_MAX_IN_GAME];
    int32_t numJoined;
    MmUser  requested[MM_MAX_IN_GAME];
    int32_t numRequested;
} MmGameInfo;

/* What the server hands over when a game starts. `addrs[0]` is the address the
 * SERVER saw (i.e. through any NAT); the rest are what the peer reported about
 * itself. Try them in order -- on one machine the first already works. */
typedef struct {
    int32_t  playerNumber;                 /* 1 = you created the game, 2 = you joined */
    char     peerName[MM_NAME_MAX];
    uint16_t localPort;                    /* the port our ENET socket is bound to */
    char     addrs[MM_MAX_PEER_ADDRS][MM_ADDR_TEXT_MAX];
    int32_t  numAddrs;
    /* A RELAYED match (include/relay_proto.h): the server said to play through it
     * rather than punch to the peer. `relayAddr` is the server itself, as we
     * reached it; `relayToken` is what registers us there. 0 = play direct. */
    int32_t  relay;
    uint64_t relayToken;
    char     relayAddr[MM_ADDR_TEXT_MAX];
    /* The relay as a FALLBACK (RELAYFB:): punch direct, and play through the server
     * (`serverAddr`, below) only if that fails -- P2PLink_SetRelayFallback. 0 = none. */
    uint64_t relayFallbackToken;
    /* The server itself, as we reached it ("a.b.c.d:port"), relayed or not: where the
     * lobby asks whether the two teams are canonical (include/team_proto.h). */
    char     serverAddr[MM_ADDR_TEXT_MAX];
} MmStartInfo;

/* Connect and log in. `gameId` is the server's compatibility tag: it refuses a
 * client whose tag differs, which is how a protocol change is kept from meeting
 * an old build. Returns 0, or non-zero with the reason in MmClient_Failure. */
int32_t     MmClient_Connect(const char *host, uint16_t port,
                             const char *userName, const char *gameId);

/* Once a frame: pump ENET, apply what arrived. Cheap when idle. */
void        MmClient_Poll(void);

/* Log out and close the socket. Idempotent. */
void        MmClient_Disconnect(void);

MmState     MmClient_State(void);
MmFailure   MmClient_Failure(void);
const char *MmClient_FailureText(void);     /* for a menu line; never NULL */
const char *MmClient_UserName(void);

/* The lobby. Both pointers stay valid until the next MmClient_Poll(). */
const MmUser *MmClient_Users(int32_t *count);
const MmGame *MmClient_OpenGames(int32_t *count);

/* The game you are in, or NULL when you are not in one. */
const MmGameInfo *MmClient_GameInfo(void);

/* Lobby actions. Each returns 0 when the request went out. */
int32_t MmClient_CreateGame(void);
int32_t MmClient_JoinGame(const char *owner);
int32_t MmClient_Approve(const char *player);
int32_t MmClient_Eject(const char *player);
int32_t MmClient_LeaveGame(void);

/* One-shot events. Each returns non-zero once, then goes quiet until it happens
 * again -- so a menu can poll them without seeing the same thing every frame.
 *   JoinRequest: someone wants into YOUR game; `name` gets who.
 *   Removed:     the host ejected you, or left, or the game went away. */
int32_t MmClient_TakeJoinRequest(char *name, int32_t len);
int32_t MmClient_TakeRemoved(void);

/* Non-NULL once the server has started a game. Stays until MmClient_ClearStart. */
const MmStartInfo *MmClient_StartInfo(void);
void               MmClient_ClearStart(void);

/* The server every entry point defaults to. Three sources, in order:
 *   1. SWOS_MM_HOST / SWOS_MM_PORT   -- a test pointing one process somewhere
 *   2. whatever the player typed on the login menu, kept in SWOS.ini
 *      ([netplay] matchmaker), published here by main.c
 *   3. MM_DEFAULT_HOST / MM_DEFAULT_PORT, the public server
 * so a tester types an address once and never sees an environment variable. */
const char *MmClient_DefaultHost(void);
uint16_t    MmClient_DefaultPort(void);

/* "host", "host:port" or "" (this machine). Parsed once, here, so the menu, the
 * ini and the self-tests cannot disagree about what "the server" means. */
void        MmClient_SetConfiguredServer(const char *spec);
/* What to show the player: the configured server as "host:port", or "" if none. */
const char *MmClient_ConfiguredServer(void);

/* Connect to a locally running server, log in, list the lobby, create and leave a
 * game, and log out. Prints a report; 0 on success. Needs
 * scripts/run_matchmaker.sh running. `bin/SWOS.x86_64 --matchmaker-selftest`. */
int32_t MmClient_SelfTest(const char *host, uint16_t port);

/* The two-client half: one process hosts, another joins, the host approves, and
 * both must receive START with the other's addresses. That handoff is the only
 * part of the protocol a single client cannot exercise.
 * `role` is "host" or "join". scripts/matchmaker_pair_test.sh runs the pair. */
int32_t MmClient_PairTest(const char *role, const char *host, uint16_t port);

#endif /* SWOS_SDL_BUILD */
#endif /* MM_CLIENT_H */
