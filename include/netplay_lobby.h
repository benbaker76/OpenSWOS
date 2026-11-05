/*
 * netplay_lobby.h -- where two connected players set their match up (SDL builds).
 * docs/NETPLAY.md, "The lobby".
 *
 * SWOS 2020's model: EACH PLAYER PUBLISHES THEIR OWN SIDE -- a team, a tactic, and
 * whether they are ready -- and THE HOST OWNS THE MATCH RULES (length, pitch,
 * substitutes). Either may change their mind as often as they like; every change is
 * re-sent over p2p_link, which keeps only the newest. When both are ready the host
 * starts, the joiner confirms, and both kick off from the same setup.
 *
 * No UI: the SWOS 2020 lobby menu (src/online_menus.c) and the two-process live
 * test (src/netplay_live_test.c) drive the same state through this API, so the
 * test exercises exactly what a player does.
 *
 * Begin once the link is CONNECTED and the input delay agreed, then Poll every
 * frame. Player 1 is the host (top team), player 2 the joiner (bottom team).
 */
#ifndef NETPLAY_LOBBY_H
#define NETPLAY_LOBBY_H

#ifdef SWOS_SDL_BUILD

#include "swos_types.h"
#include "game_types.h"

#define LOBBY_NAME_MAX   16
#define LOBBY_TACTIC_MAX 17         /* TACTIC_USER_F: the built-in 12, then USER A-F */

typedef enum {
    LOBBY_ACTIVITY_NONE,
    LOBBY_ACTIVITY_CHOOSING_TEAM,   /* the other player sees "CHOOSING A TEAM" */
    LOBBY_ACTIVITY_ARRANGING_TEAM,  /* ... "ARRANGING THEIR TEAM": line-up and formation */
} LobbyActivity;

/* The matchmaking server's word on a player's team (include/team_proto.h). Only a match
 * the server arranged is asked about; the two REFUSED ones stop it starting. */
typedef enum {
    LOBBY_TEAM_UNASKED,             /* no server (a direct match), no team, or not answered yet */
    LOBBY_TEAM_CANONICAL,           /* on the server's list, with this data */
    LOBBY_TEAM_REFUSED_UNKNOWN,     /* the server has no team with that id */
    LOBBY_TEAM_REFUSED_CHANGED,     /* it has the team, and its data is not this */
    LOBBY_TEAM_NO_ANSWER,           /* the server did not answer: played unverified */
} LobbyVerified;

typedef struct {
    int32_t    known;               /* heard from (always true for ourselves) */
    int32_t    ready;
    int32_t    activity;            /* LobbyActivity */
    int32_t    hasTeam;
    int32_t    refused;             /* the other player's only: they sent a team that is not a
                                     * database team (CustomTeam_Allowed), so they have none here */
    int32_t    verified;            /* LobbyVerified: what the matchmaking server said of this team */
    int32_t    tactic;              /* TACTIC_*: 0..LOBBY_TACTIC_MAX */
    int32_t    extraDelay;          /* frames this player adds to the input delay (FRAME DELAY +) */
    TeamFileEx team;
    TeamTactics userTactic;         /* the tactic itself when it is a USER one */
    char       name[LOBBY_NAME_MAX + 1];
} LobbyPlayer;

typedef struct {
    uint16_t gameLength;            /* _gameLength: 0..3 = 3, 5, 7, 10 minutes */
    uint16_t pitchType;             /* _friendlyPitchType: 0..6, or 0xFFFF for random */
    uint16_t minSubs, maxSubs;      /* 0..5, min <= max, min >= 1 when max > 0 */
    /* A cup tie, SWOS 2020's CUP row: */
    uint16_t legs;                  /* 0 a friendly (a draw stands), 1 or 2 legs */
    uint16_t tieBreak;              /* legs > 0: LOBBY_TIE_ET_PEN or LOBBY_TIE_PEN_ONLY */
    uint16_t leg;                   /* 2 legs: the one to play next, 1 or 2 */
    uint16_t firstLeg1, firstLeg2;  /* leg 2: how the first ended, top team first */
    /* The host's switches, NETPLAY_SW_* (netplay_match.h): how the match is played where
     * the builds since 7d9d6d9 changed it. Only the host's count; both play by them. */
    uint16_t switches;
    /* What the match counts for, on a service with rankings and leagues (online_account.h;
     * OpenSWOS's matches are all friendlies): the host's choice at OPEN, and for a league or
     * cup match, or a ranked one once the service has made it, the service's ids for it. */
    uint16_t mode;                  /* LOBBY_MODE_* */
    uint32_t matchId, legId;        /* 0: none (yet) */
} LobbyRules;

enum { LOBBY_TIE_ET_PEN = 1, LOBBY_TIE_PEN_ONLY = 2 };
enum { LOBBY_MODE_FRIENDLY = 0, LOBBY_MODE_LEAGUE = 1, LOBBY_MODE_RANKED = 2 };

typedef enum {
    LOBBY_CLOSED,                   /* not begun */
    LOBBY_OPEN,                     /* choosing */
    LOBBY_STARTING,                 /* the start is being agreed */
    LOBBY_KICKOFF,                  /* agreed: close the link and play (NetplayLobby_Peer) */
    LOBBY_FAILED,                   /* the link went; NetplayLobby_Notice says why */
    LOBBY_RECONNECTING,             /* after a match: finding the same player again */
} LobbyState;

void               NetplayLobby_Begin(int32_t localPlayer, const char *localName);
void               NetplayLobby_Poll(void);
void               NetplayLobby_End(void);
int32_t            NetplayLobby_IsOpen(void);    /* from Begin to End: an online session */

LobbyState         NetplayLobby_State(void);
int32_t            NetplayLobby_LocalPlayer(void);
const LobbyPlayer *NetplayLobby_Player(int32_t player);     /* 1 or 2 */
const LobbyRules  *NetplayLobby_Rules(void);
/* "" or one line for the player: why a start did not happen, why the lobby failed. */
const char        *NetplayLobby_Notice(void);

/* This machine's side. Each is sent to the other player; changing the team or the
 * tactic takes the ready flag down, so nobody starts a match against a team they
 * have not seen. Ignored unless the lobby is OPEN. */
void               NetplayLobby_SetTeam(const TeamFileEx *team);
void               NetplayLobby_SetTactic(int32_t tactic);
void               NetplayLobby_SetReady(int32_t ready);
void               NetplayLobby_SetActivity(int32_t activity);

/* The host's. Changing the number of legs or the tie-break starts a new tie. A change takes the joiner's ready flag down on the joiner's side, so
 * the joiner agrees to the rules they will play. */
void               NetplayLobby_SetRules(const LobbyRules *rules);
int32_t            NetplayLobby_IsHost(void);
/* FRAME DELAY (SWOS 2020's): never below AUTO -- the delay the link agreed from the ping
 * (P2PLink_AgreedDelay, Playa's table) -- but either player may add frames, against a stutter.
 * Both play the same: AUTO plus the larger of the two players' extras, which the host puts in
 * the START (NetplayLobby_Peer gives it). A change takes both ready flags down. */
#define LOBBY_EXTRA_DELAY_MAX 4
void               NetplayLobby_SetExtraDelay(int32_t frames);
int32_t            NetplayLobby_AutoDelay(void);

/* Both sides ready, on the same rules, and the host has seen the joiner's latest. */
int32_t            NetplayLobby_CanStart(void);
/* Host: build the match and offer it. 0 if offered. */
int32_t            NetplayLobby_Start(void);

/* Where the match is played, captured at Begin (and at a rematch's reconnect) while the
 * link is certainly up: the peer, our port, the agreed input delay and the agreed
 * simulation draw width (P2PLink_AgreedSimWidth). Any pointer may be NULL. */
void               NetplayLobby_Peer(char *ip, uint32_t ipCap, uint16_t *peerPort,
                                     uint16_t *localPort, int32_t *delay, int32_t *simWidth);
/* (`delay`: the match's -- the START's once one is agreed, else AUTO plus the larger extra.) */

/* Kick-off: put the agreed setup -- teams, tactics, rules, random seeds -- back in
 * place on this machine, and initialise the teams as the friendly menu does. Call it
 * immediately before InitAndPlayGame, after the lobby has closed.
 *
 * Why again, when START already applied it: between START and kick-off each machine
 * keeps drawing the lobby for however many frames its half of the handshake takes,
 * and a different number of menu frames is a different number of random draws. The
 * kick-off state has to be the agreed one, not the agreed one plus local history.
 * 0 on success. */
int32_t            NetplayLobby_ApplyMatch(void);

/* After the match (or a refused start): put back the six USER tactics the match
 * setup overwrote, so a player's own library is never changed by playing online --
 * and take back the host's switches ApplyMatch put in force (netplay_match.h), giving
 * this player their own SHORT PRESSES again. */
void               NetplayLobby_RestoreTactics(void);

/* After a match, on both machines: back to this lobby with the same player. The link
 * closed for the match, so it is opened again -- both punching to the address the
 * match was played with, from the port it was played on, which is also the NAT
 * mapping the match kept open -- and the lobby is RECONNECTING until the two are
 * linked and have agreed an input delay again (it is measured afresh). Both sides,
 * their tactics and the host's rules are kept; both ready flags go down, so a rematch
 * is two GOs and a START. `notice` is the line to show meanwhile ("" for none).
 * 0 if reconnecting; otherwise the lobby is FAILED and says why. */
int32_t            NetplayLobby_Rematch(const char *notice);

/* After a match, before Rematch, on both machines: how it ended, top team first, and
 * whether it was played to the end. A two-leg tie moves on -- a first leg played out
 * makes the next match the second, with this score to make up; a second leg makes the
 * next the first of a new tie. The host's rules are still the ones that count; the
 * joiner's copy only means a GO pressed before they arrive is not taken down by them.
 * A match that ended early is played again. */
void               NetplayLobby_MatchPlayed(int32_t completed, int32_t goals1, int32_t goals2);

/* THE CHAT (SWOS 2020's lobby chat). A player's lines go to the other player only --
 * over this lobby's own link, never through a server -- and the lobby's bot ("JON HARE
 * (BOT)") adds lines of its own on each machine: who connected, how a match ended, why the
 * lobby closed. Kept for this lobby, across rematches; a new opponent starts it again. */
#define LOBBY_CHAT_TEXT  28         /* a line's characters: SWOS 2020's input field */
#define LOBBY_CHAT_KEEP  64         /* lines kept for scrolling back */

typedef enum { LOBBY_CHAT_ME, LOBBY_CHAT_THEM, LOBBY_CHAT_BOT } LobbyChatWho;

typedef struct {
    int32_t who;                    /* LobbyChatWho */
    char    text[LOBBY_CHAT_TEXT + 1];
} LobbyChatLine;

/* Say a line (trimmed, capitals, what the font draws). It is sent as soon as the link is up
 * -- after a rematch's reconnect too. 0 if there was something to say. */
int32_t              NetplayLobby_Say(const char *text);
/* A bot line, on this machine only. */
void                 NetplayLobby_ChatNotice(const char *text);
int32_t              NetplayLobby_ChatCount(void);          /* lines kept, oldest first */
const LobbyChatLine *NetplayLobby_ChatLine(int32_t i);
uint32_t             NetplayLobby_ChatSerial(void);         /* goes up with every new line */

#endif /* SWOS_SDL_BUILD */
#endif /* NETPLAY_LOBBY_H */
