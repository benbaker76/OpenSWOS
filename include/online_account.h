/*
 * online_account.h -- accounts on a project's online service (SDL builds with netplay).
 *
 * OpenSWOS has none: a player types a name and plays, and the matchmaking server never asks
 * who they are (src/online_account.c). A fork with accounts -- SWOS United, whose players log
 * in to sensiblesoccer.de -- sets PROJECT_ACCOUNTS in include/project_identity.h and supplies
 * these functions from a file of its own; OpenSWOS's file compiles to nothing then. Everything
 * the multiplayer screens need to know about accounts goes through here (src/online_menus.c):
 * whether a password is asked for and checked, the web pages to send a player to, and the
 * check itself.
 *
 * Whatever a fork's accounts are, the CHAT is not one of their services: it runs over the
 * lobby's own link between the two players (netplay_lobby.h).
 */
#ifndef ONLINE_ACCOUNT_H
#define ONLINE_ACCOUNT_H

#include "swos_types.h"

typedef enum {
    ACCOUNT_LINK_REGISTER,          /* REGISTER */
    ACCOUNT_LINK_PASSWORD,          /* MY PASSWORD? */
    ACCOUNT_LINK_USERNAME,          /* MY USERNAME? */
    ACCOUNT_LINK_LEAGUES,           /* the online leagues (the LEAGUE / CUP screen) */
    ACCOUNT_LINK_DISCORD,           /* where to apply for one (the same screen) */
    ACCOUNT_LINKS
} AccountLink;

/* What the service has beyond the log-in. A screen OpenSWOS imported from SWOS 2020 for
 * one (RANKINGS, LEAGUES, MATCH LOGS, the ranked and league/cup matches of OPEN) shows its
 * button only when the service says it has it; the screens' handlers are the fork's own,
 * registered by Account_Init (MenuJson_RegisterSymbols). */
typedef enum {
    ACCOUNT_HAS_RANKINGS,
    ACCOUNT_HAS_LEAGUES,
    ACCOUNT_HAS_MATCH_LOGS,
    ACCOUNT_HAS_RANKED,         /* OPEN offers FRIENDLY, RANKED MATCH and LEAGUE / CUP */
    ACCOUNT_HAS_TEAM_IMPORT,    /* MY CUSTOM TEAMS offers IMPORT FROM SWOS 2020 */
    ACCOUNT_FEATURES
} AccountFeature;

void        Account_Init(void);                 /* once, with the online menus */
int32_t     Account_Has(AccountFeature feature);

/* A password is asked for and checked at LOGIN. 0: a name is all. */
int32_t     Account_Enabled(void);
/* The log-in screen's line under its title ("LOGIN TO YOUR SENSIBLESOCCER.DE ACCOUNT"). */
const char *Account_Prompt(void);
/* A web page, NULL for none (its button is hidden). */
const char *Account_Url(AccountLink link);

/* The check, off the menu's thread: Begin, then Poll each frame until it is not 0 --
 * 1 the account is good, -1 it is not (Account_Error says why, in the menus' capitals). */
void        Account_Begin(const char *user, const char *password);
int32_t     Account_Poll(void);
const char *Account_Error(void);

/* ---- ranked and league / cup matches (a service with ACCOUNT_HAS_RANKED) ----
 *
 * OPEN asks the service what the match counts for (its screen: FRIENDLY, RANKED MATCH,
 * LEAGUE / CUP): a LOBBY_MODE_* (netplay_lobby.h), or -1 to open nothing. The host's choice
 * goes into the lobby's rules, where the joiner sees it, with the service's match and leg ids
 * once there are any; the service follows the lobby through the calls below, on both
 * machines, and may change the host's rules itself (NetplayLobby_SetRules: a ranked match
 * the players' ratings do not allow played as a friendly, a league match selected). Its lines
 * go into the chat (NetplayLobby_ChatNotice). */
int32_t     Account_ChooseMode(void);
/* After Account_ChooseMode: the gameplay style the chosen match is played in (0 the Amiga's, 1
 * the PC's -- a league or cup match's ranking), so OPEN does not ask; -1 OPEN asks the player. */
int32_t     Account_ChosenStyle(void);
/* May this player join the game this player is hosting? 0: the host's game refuses them (a
 * league or cup match picked before opening is for its opponent alone -- SWOS 2020's "YOU ARE
 * NOT ALLOWED TO JOIN"). */
int32_t     Account_JoinAllowed(const char *name);
/* What the service knows of a lobby player, for the lobby's LOCATIONS and stars: 1 known --
 * country (an ISO2 code such as "DE" or "GB-ENG", or a country's name) and their SRN in the game's
 * style (-5000 unranked); 0 not (yet). */
int32_t     Account_PlayerInfo(const char *name, char *country, uint32_t cap, int32_t *srn);
/* 1: the selected match is a league (group) match, so the lobby's CUP stays OFF and greyed. */
int32_t     Account_CupLocked(void);
void        Account_LobbyOpened(int32_t host);
void        Account_LobbyPeer(const char *name);    /* the other player's name has arrived */
void        Account_LobbyPoll(void);                /* each frame the lobby is up */
void        Account_LobbyClosed(void);              /* the player has left the lobby (blocks briefly) */
/* 0 holds the host's START (greyed): the service is still deciding what the match is -- a
 * ranked match's ratings not in yet, a league match not selected. */
int32_t     Account_LobbyCanStart(void);
/* The lobby's MATCH row: what this match is for ("(NO MATCH INFO)"), NULL hides the row and
 * its SELECT MATCH button. */
const char *Account_MatchText(void);
/* A league or cup match's tournament, its whole name (a fork's Discord posts: SWOS United's discord2020 mod); 0 when
 * the match is not one, or none is picked yet. */
const char *Account_MatchTournament(void);
void        Account_SelectMatch(void);              /* SELECT MATCH */
/* A player's row chosen in the matchmaking list (SWOS 2020 opens MATCH INFO / PLAYER INFO
 * there): the service shows what it has about them, or nothing. */
void        Account_UserSelected(const char *name);

/* MATCH INFO for a player, by name: a click on a name (the online list, the lobby, RANKINGS).
 * Returns when the player leaves it; nothing where the service has no such thing. */
void        Account_ShowPlayer(const char *name);

/* IMPORT FROM SWOS 2020 (MY CUSTOM TEAMS, src/custom_team.c). Start asks for the logged-in player's
 * teams; Poll, each draw, says how it stands, the two lines to show and whether the slots changed;
 * Answer is YES (1) or NO (0) to overwriting; Reset forgets what was last said. */
enum { TEAM_IMPORT_IDLE, TEAM_IMPORT_BUSY, TEAM_IMPORT_ASKING, TEAM_IMPORT_MAX = 3 };
void        Account_TeamImportStart(void);
int32_t     Account_TeamImportPoll(const char **line1, const char **line2, int32_t *changed);
void        Account_TeamImportAnswer(int32_t yes);
void        Account_TeamImportReset(void);

/* After every match, on both machines: how it ended, from THIS player's side -- goals in the
 * ninety minutes, in extra time, in the shoot-out -- and whether it was played to the end. */
typedef struct {
    int32_t  mode;                  /* LOBBY_MODE_*: the rules it was played by */
    uint32_t matchId, legId;
    int32_t  host;                  /* this player opened the game */
    int32_t  completed;
    int32_t  dosStyle;              /* played the PC game's way (else the Amiga's) */
    int32_t  mine[3], theirs[3];    /* goals: [0] the ninety minutes, [1] extra time, [2] penalties */
    char     opponent[24];
} AccountResult;
void        Account_MatchPlayed(const AccountResult *result);

#endif /* ONLINE_ACCOUNT_H */
