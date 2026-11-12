/*
 * online_menus.c -- SWOS 2020's online-play menus, running in OpenSWOS.
 *
 * The menus are imported data: scripts/import_swos2020_menus.py turns the .mnu
 * files in swos-2020/src/menus into JSON under bin/data_json/menus/swos2020 and
 * into include/swos2020_menus.h (docs/MENUS.md section 10). This file supplies their
 * handlers to the JSON loader, under SWOS 2020's own names, so a handler ported
 * from swos-2020 (game.cpp, mpgame.cpp) keeps its name and its entry ordinals
 * (LogInMenu::header -> LogInMenu_header).
 *
 * Handlers get ported as the online features they drive arrive: the login service,
 * the matchmaking server, rollback netplay (src/netplay.c). A handler that is not
 * ported yet is bound to a no-op by the loader and logged once; --menu-selftest
 * reports the count.
 *
 * With no server, what works now: every imported menu opens, lays out and
 * navigates, and every exit button leaves its menu; the login form edits a username
 * and password and toggles its options; LOGIN goes to matchmaking in SWOS 2020's
 * own "server not connected" state, from where SETTINGS and RANKINGS open.
 */
#include "test_rules.h"
#include "online_menus.h"
#include "dos_mode.h"           /* the gameplay style: OPEN asks it, the lobby shows it */
#include "project_identity.h"  /* PROJECT_HIDE_SWOS2020_EXTRAS */
#include "mm_client.h"         /* the matchmaking server (docs/NETPLAY.md section 7) */
#include "p2p_menu.h"
#include "controls_menu.h"          /* OpenSWOS's own JSON-only menus */
#include "mods_menu.h"
#include "credits_menu.h"
#include "swos2020_menus.h"
#include "menu_load_json.h"
#include "menu_symbols.h"
#include "menu_system.h"
#include "display.h"        /* FRONTBUFFER_ROW_STRIDE_BYTES: the rating stars */
#include "input_text.h"
#include "p2p_link.h"         /* the peer connection the handoff makes */
#include "netplay_lobby.h"    /* where the two players set the match up */
#include "netplay_match.h"    /* the live GGPO session */
#include "netplay_setup.h"    /* the cup rules a match takes at kick-off */
#include "game_types.h"       /* TeamFileEx, for the match the handoff starts */
#include "platform_sdl.h"     /* Platform_SetFrameHook: the link outlives a modal menu */
#include "swos_settings.h"    /* Settings_Save: the SERVER row keeps what was typed */
#include "teams.h"            /* LoadTeamFile, for the lobby's test knobs */
#include "game_loop.h"        /* _gFrameStepInput */
#include "match_record.h"     /* MatchRecord_SetConfigured: the RECORD MATCHES row */
#include "match_report.h"     /* the match report: HOW WAS THE MATCH?, SEND MATCH REPORTS */
#include "match_prediction.h" /* both players' PRED and SEEN cross after the match */
#include "uniform.h"          /* the UNIFORM screen, from the lobby's team screen */
#include "custom_team.h"      /* MY CUSTOM TEAM */
#include "online_account.h"   /* accounts: none here; a fork's (SWOS United's) */
#include "chat_style.h"       /* SWOS_CHAT_DYNAMIC_FONT: the chat font, or the game's */
#include "chat_font.h"        /* the lobby's chat, drawn at the window's own pixels */
#include "match_chat.h"       /* ChatLine_*: the line typed into the chat field */
#include "gfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>                /* SDL_GetTicks, for a test knob */

/* ---- A FORK'S OWN ONLINE SCREENS ----
 * A fork that draws these screens its own way (SWOS United: SWOS 2020's matchmaking table and
 * lobby) keeps that code in src/online_menus_fork.inc, which OpenSWOS does not have; it is
 * included at the END of this file, so its code sees this file's helpers, and its
 * online_menus_fork() returns these hooks. Each is optional; with none, or no file, every screen
 * is OpenSWOS's. So a merge of OpenSWOS never meets the fork's screens. */
typedef struct {
    void        (*init)(void);                      /* OnlineMenus_Init: register its handlers */
    int32_t     ownsMatchMaking;                    /* MATCH MAKING is the fork's (matchMakingDraw) */
    void        (*matchMakingDraw)(void);           /* each frame, after the join requests */
    void        (*joinRequested)(const char *who);  /* a player asks to join (and may) */
    void        (*autoJoining)(const char *owner);  /* SWOS_MM_AUTO=join is asking owner's game */
    int32_t     (*hostStyleLayout)(void);           /* CHOOSE GAMEPLAY laid out: 1 = the fork's */
    void        (*lobbyEntered)(int32_t player);    /* OnlineMenus_EnterLobby */
    void        (*lobbyLeft)(void);                 /* the lobby closed */
    void        (*lobbyLayout)(void);               /* each frame, after lobby_layout */
    const char *(*styleName)(int32_t style);        /* the lobby's GAMEPLAY text, NULL: OpenSWOS's */
    void        (*lobbyStatus)(int32_t lobbyState); /* after the connection status is set */
    void        (*lobbyRow)(int32_t row, const LobbyPlayer *p, int32_t canEdit);
    int32_t     (*chatColor)(const LobbyChatLine *l);   /* a line's colour (fixed font), -1 OpenSWOS's */
    void        (*readying)(void);                  /* GO pressed, about to be ready */
    void        (*liveMatchStart)(void);            /* a live match's kick-off (the lobby's rules in force) */
    void        (*liveMatchEnd)(int32_t finished, const char *why);
    void        (*settingsDraw)(void);              /* MULTIPLAYER SETTINGS, each frame */
} OnlineMenusFork;
static const OnlineMenusFork *online_menus_fork(void);
#define FORK_HOOK(h) (online_menus_fork() && online_menus_fork()->h)

#define SWOS2020_MENU(name) "swos2020/" #name

/* SWOS 2020 mnu2h colour constants used below (mnu2h/Constants.py). */
enum {
    kGray = 7, kLightBrownWithYellowFrame = 6, kRed = 10, kPurple = 11,
    kLightBrownWithRedFrame = 12, kLightBlue = 13, kGreen = 14,
};

/* ---- entry access, the spelling SWOS 2020's handlers use (swos.h MenuEntry) ---- */
static MenuEntry *entry_at(int32_t ordinal)
{
    return CalcMenuEntryAddress((uint16_t)ordinal);
}

static void set_text(int32_t ordinal, const char *text)
{
    entry_at(ordinal)->fg = (uintptr_t)text;
}

static void set_visible(int32_t ordinal, int32_t visible)
{
    entry_at(ordinal)->isInvisible = visible ? 0 : 1;
}

/* SWOS 2020 setEntry(): text, selectable or not, background colour. */
static void set_entry(int32_t ordinal, const char *text, int32_t enabled, uint16_t color)
{
    MenuEntry *e = entry_at(ordinal);
    e->fg = (uintptr_t)text;
    e->isDisabled = enabled ? 0 : 1;
    e->bgColor = color;
}

/* ---- this player's team, as picked ----
 *
 * A kit chosen on the UNIFORM screen goes to the other player INSIDE the team: both of
 * its kits become the chosen one (lineup_end), so the match on both machines dresses
 * the side from the same record and nothing else needs sending. What that loses is the
 * team's own two kits -- which the UNIFORM screen needs again for SWITCH and RESET the
 * next time it is opened -- so they are remembered here from the moment a team is
 * picked, with enough of the team to know it is still the same one. */
static struct {
    int32_t valid;
    uint8_t country, ordinal;
    KitLook kit[2];
} s_picked;

/* 0 = not taken: the other player's game would refuse it (CustomTeam_Allowed), and say
 * so only there, so this one says so here. */
static int32_t lobby_pick_team(const TeamFileEx *team)
{
    extern void ShowErrorMenu(uint8_t *errorTextStr);
    static char notDatabase[] = "ONLY TEAMS OF THE DATABASE CAN BE PLAYED ONLINE";

    if (!CustomTeam_Allowed(team)) {
        ShowErrorMenu((uint8_t *)notDatabase);
        return 0;
    }
    s_picked.valid = 1;
    s_picked.country = team->team.countryNumber;
    s_picked.ordinal = team->team.teamOrdinal;
    KitArt_FromTeam(&team->team, 0, &s_picked.kit[0]);
    KitArt_FromTeam(&team->team, 1, &s_picked.kit[1]);
    NetplayLobby_SetTeam(team);
    return 1;
}

/* The lobby's team with its own kits back, and the kit it was sent in (if that is not
 * its own first kit) as the choice the UNIFORM screen starts from. */
static void lobby_unbake_kit(TeamFileEx *team)
{
    KitLook sent;

    if (!s_picked.valid || s_picked.country != team->team.countryNumber ||
        s_picked.ordinal != team->team.teamOrdinal)
        return;
    KitArt_FromTeam(&team->team, 0, &sent);
    KitArt_ToTeam(&s_picked.kit[0], 0, &team->team);
    KitArt_ToTeam(&s_picked.kit[1], 1, &team->team);
    if (memcmp(&sent, &s_picked.kit[0], sizeof sent) != 0)
        Uniform_Choose(&team->team, &sent);
}

void OnlineMenus_Show(const char *name)
{
    uint8_t *menu = MenuJson_Load(name);
    if (!menu) {
        fprintf(stderr, "[online-menus] cannot show %s\n", name);
        return;
    }
    TestRules_Online(1);        /* test_rules.h: the testers' rule stops at this door */
    ShowMenu(menu);
}

/* ======================================================================
 * logInMenu (swos-2020/src/menus/logIn.mnu; handlers in game.cpp)
 * ====================================================================== */
/* SWOS_MM_AUTO_PEER=<name>: the one player an automated test may play. Without it
 * the auto-joiner takes the first game on offer and the auto-host approves anyone --
 * fine against a server of our own, and wrong against a public one, where that is a
 * real player's game or a real player asking to join ours. Unset = any. */
static int32_t mm_auto_is_peer(const char *name)
{
    const char *peer = getenv("SWOS_MM_AUTO_PEER");
    return !peer || !peer[0] || (name && strcmp(name, peer) == 0);
}

/* SWOS_MM_AUTO: a test drives both ends of the matchmaking menu. */
enum { MM_AUTO_OFF = 0, MM_AUTO_HOST, MM_AUTO_JOIN };
static int32_t s_mmAuto;
static int32_t s_mmAutoDone;

static char s_username[33];
/* The row SWOS 2020 used for a password is the SERVER address here: our
 * matchmaker has no accounts (docs/NETPLAY.md 7 -- LOGIN: is a name and a blob,
 * and VerifyVersion is an empty TODO), so a password field would ask a player
 * for something nothing reads. The address is the one thing they DO have to be
 * told, and typing it here beats an environment variable. */
static char s_server[64];
static int32_t s_rememberLogin = 1;   /* the .mnu's initial ON/OFF texts */
static int32_t s_liveCast = 1;

/* A project with accounts (include/online_account.h: SWOS United's) asks for a PASSWORD in
 * that row instead, and checks the two at LOGIN; the server is then its own. */
static char    s_password[33];
static char    s_passwordShown[33];         /* what the row draws: '*'s, or the capitals */
static int32_t s_showPassword;
static int32_t s_checking;                  /* LOGIN: the account is being checked */
static char    s_loginLine[64];             /* under the title: the prompt, or how the check went */

/* What the SERVER row shows: what the player typed, else what SWOS.ini holds, else
 * the server LOGIN will actually use -- the public default, or a test's SWOS_MM_HOST.
 * Upper-cased for the menu font (a host name is case-blind), in a buffer of its own
 * because set_text keeps the pointer. */
static const char *server_text(void)
{
    static char shown[80];
    const char *src;
    char *c;
    const char *cfg = MmClient_ConfiguredServer();

    if (s_server[0])
        src = s_server;
    else if (cfg && cfg[0])
        src = cfg;
    else
        src = MmClient_DefaultHost();
    snprintf(shown, sizeof shown, "%s", src);
    for (c = shown; *c; c++)
        if (*c >= 'a' && *c <= 'z')
            *c = (char)(*c - 'a' + 'A');
    return shown;
}

/* Keep it: the menu writes SWOS.ini the way the OPTIONS -> GAMEPLAY row does
 * (menus.c), through the live options and resolved path main.c publishes. */
static void server_save(void)
{
    extern PlatformInitOptions *g_arcadeOpts;
    extern const char *g_arcadeIniPath;
    MmClient_SetConfiguredServer(s_server);
    if (!g_arcadeOpts)
        return;
    snprintf(g_arcadeOpts->matchmaker, sizeof g_arcadeOpts->matchmaker, "%s", s_server);
    if (g_arcadeIniPath)
        Settings_Save(g_arcadeOpts, g_arcadeIniPath);
}

/* What this screen relabels and hides. Applied on EVERY draw, not once in onInit
 * -- the same trap lobby_layout() documents: coming back from a menu opened from
 * here (matchmaking, SETTINGS) restores this one from its template, so SWOS 2020's
 * USERNAME / PASSWORD / SHOW PASSWORD come back and the buttons lose their text,
 * and there is no onReturn to redo it in. Every assignment here is a pointer
 * store, so doing it each frame costs nothing. */
static const char *password_text(void)
{
    uint32_t i;
    for (i = 0; s_password[i] && i < sizeof s_passwordShown - 1; i++) {
        char c = s_password[i];
        s_passwordShown[i] = !s_showPassword ? '*' : (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') :
                             (c >= ' ' && c <= 'Z') ? c : '?';
    }
    s_passwordShown[i] = 0;
    return s_passwordShown;
}

static void logInMenu_layout(void)
{
    const int32_t accounts = Account_Enabled();
    int32_t i;

    set_text(LogInMenu_header, accounts ? "MULTIPLAYER LOGIN" : "PLAY ONLINE");
    set_text(LogInMenu_subTitle, s_loginLine[0] ? s_loginLine : Account_Prompt());
    set_text(LogInMenu_username, accounts ? "USERNAME" : "NAME");
    set_text(LogInMenu_password, accounts ? "PASSWORD" : "SERVER");
    set_text(LogInMenu_usernameInput, s_username);
    set_text(LogInMenu_passwordInput, accounts ? password_text() : server_text());
    set_text(LogInMenu_logInContinue, "LOGIN");
    set_text(LogInMenu_OfflinelogOut, "ABOUT");
    /* The button is RANKINGS for a service with them, else SWOS 2020's NEWS (its "latest
     * runs" and a Discord link) -- hidden where the project has no news
     * (PROJECT_HIDE_SWOS2020_EXTRAS). */
    set_text(LogInMenu_news, Account_Has(ACCOUNT_HAS_RANKINGS) ? "RANKINGS" : "NEWS");
    set_visible(LogInMenu_news, Account_Has(ACCOUNT_HAS_RANKINGS) || !PROJECT_HIDE_SWOS2020_EXTRAS);
    /* Under ABOUT: SWOS 2020 has it 8 to the left (34, the buttons above 42). */
    entry_at(LogInMenu_news)->x = entry_at(LogInMenu_OfflinelogOut)->x;
    /* SHOW PASSWORD only where there is one; no live cast here. */
    entry_at(LogInMenu_showPasswordLabel)->isInvisible = accounts ? 0 : 1;
    entry_at(LogInMenu_showPassword)->isInvisible = accounts ? 0 : 1;
    entry_at(LogInMenu_liveCastLabel)->isInvisible = 1;
    entry_at(LogInMenu_liveCast)->isInvisible = 1;
    /* REGISTER, MY PASSWORD?, MY USERNAME?: the account's web pages, where it has them. */
    for (i = 0; i < 3; i++) {
        static const char *const label[3] = { "REGISTER", "MY PASSWORD?", "MY USERNAME?" };
        set_text(LogInMenu_registerUser + i, label[i]);
        entry_at(LogInMenu_registerUser + i)->isInvisible = Account_Url((AccountLink)i) ? 0 : 1;
    }
}

/* REMEMBER LOGIN INFO: the name is kept in SWOS.ini beside the server ([netplay] name,
 * remember_login) while the switch is on, and taken out of it when it is turned off
 * (ReportBot 0023: the switch was drawn and did nothing, so the name was typed again
 * at every start). */
static void login_load(void)
{
    extern PlatformInitOptions *g_arcadeOpts;
    static int32_t loaded;

    if (loaded || !g_arcadeOpts)
        return;
    loaded = 1;
    s_rememberLogin = g_arcadeOpts->netplay_remember != 0;
    if (s_rememberLogin && !s_username[0])
        snprintf(s_username, sizeof s_username, "%s", g_arcadeOpts->netplay_name);
}

static void login_save(void)
{
    extern PlatformInitOptions *g_arcadeOpts;
    extern const char *g_arcadeIniPath;
    const char *name = s_rememberLogin ? s_username : "";

    if (!g_arcadeOpts)
        return;
    if (g_arcadeOpts->netplay_remember == s_rememberLogin && strcmp(g_arcadeOpts->netplay_name, name) == 0)
        return;                         /* nothing to write */
    g_arcadeOpts->netplay_remember = s_rememberLogin;
    snprintf(g_arcadeOpts->netplay_name, sizeof g_arcadeOpts->netplay_name, "%s", name);
    if (g_arcadeIniPath)
        Settings_Save(g_arcadeOpts, g_arcadeIniPath);
}

static void logInMenu_OnInit(void)
{
    login_load();
    logInMenu_layout();
}

/* Runs every frame (the menu is flagged dos_redraw), so the toggles just follow state. */
static void login_connect(void);

static void logInMenu_OnDraw(void)
{
    if (s_checking) {                   /* LOGIN with an account: how the check is going */
        const int32_t r = Account_Poll();
        if (r > 0) {
            s_checking = 0;
            s_loginLine[0] = 0;
            login_connect();
            return;
        }
        if (r < 0) {
            s_checking = 0;
            snprintf(s_loginLine, sizeof s_loginLine, "%s", Account_Error());
        }
    }
    logInMenu_layout();
    set_text(LogInMenu_rememberLogInInfo, s_rememberLogin ? "ON" : "OFF");
    set_text(LogInMenu_showPassword, s_showPassword ? "ON" : "OFF");
    set_text(LogInMenu_liveCast, s_liveCast ? "ON" : "OFF");
}

static void usernameInput_OnSelect(void)
{
    InputText(sizeof s_username, (uint8_t *)entry_at(LogInMenu_usernameInput));
    login_save();
}

/* SERVER: type "host" or "host:port". Saved on the way out, so it is typed once
 * ever rather than once per session. */
static void serverInput_OnSelect(void)
{
    MenuEntry *e = entry_at(LogInMenu_passwordInput);
    const char *shown = server_text();

    /* Start from what is in force -- editing an address to change one digit is
     * the common case. */
    if (!s_server[0] && shown && shown[0])
        snprintf(s_server, sizeof s_server, "%s", shown);
    e->fg = (uintptr_t)s_server;
    InputText(sizeof s_server, (uint8_t *)e);
    e->fg = (uintptr_t)server_text();
    server_save();
}

/* PASSWORD (a project with accounts): typed as it is -- small letters, symbols -- and drawn
 * masked unless SHOW PASSWORD. */
static void passwordInput_OnSelect(void)
{
    MenuEntry *e = entry_at(LogInMenu_passwordInput);
    if (!Account_Enabled()) {
        serverInput_OnSelect();
        return;
    }
    e->fg = (uintptr_t)s_password;
    InputText_SetRaw(1, s_showPassword);
    InputText(sizeof s_password, (uint8_t *)e);
    InputText_SetRaw(0, 0);
    e->fg = (uintptr_t)password_text();
}

static void showPassword_OnSelect(void)      { s_showPassword = !s_showPassword; }
static void rememberLogInInfo_OnSelect(void) { s_rememberLogin = !s_rememberLogin; login_save(); }
static void liveCast_OnSelect(void)          { s_liveCast = !s_liveCast; }

/* Which server to talk to. There is no account service -- the matchmaker has no
 * notion of a password, it takes a name and hands out introductions -- so LOGIN
 * connects with the username typed above and the password is not sent anywhere.
 *
 * Default is a matchmaker on this machine (scripts/run_matchmaker.sh), which is
 * what the whole local setup exists for. SWOS_MM_HOST / SWOS_MM_PORT point it
 * somewhere else without a rebuild. */
static const char *mm_host(void) { return MmClient_DefaultHost(); }
static uint16_t    mm_port(void) { return MmClient_DefaultPort(); }

/* LOGIN: start connecting, then show matchmaking. The connection is not waited
 * for here -- matchMakingMenu's OnDraw polls it and shows CONNECTING / ONLINE /
 * the failure, so a server that is not there leaves a usable menu saying so
 * rather than a frozen one. */
static void login_connect(void)
{
    MmClient_Connect(mm_host(), mm_port(), s_username, NULL);
    OnlineMenus_Show(SWOS2020_MENU(matchMakingMenu));
}

static void logInMenuLogInContinue_OnSelect(void)
{
    if (s_checking)
        return;
    if (Account_Enabled()) {
        /* The account first: checked off this thread, LOGIN's progress under the title. */
        if (!s_username[0] || !s_password[0]) {
            snprintf(s_loginLine, sizeof s_loginLine, "TYPE YOUR USERNAME AND PASSWORD");
            return;
        }
        login_save();
        snprintf(s_loginLine, sizeof s_loginLine, "CHECKING USERNAME AND PASSWORD...");
        Account_Begin(s_username, s_password);
        s_checking = 1;
        return;
    }
    if (!s_username[0])
        snprintf(s_username, sizeof s_username, "PLAYER");
    login_save();
    login_connect();
}

/* ABOUT: the game's credits. */
static void logInMenuAbout_OnSelect(void)
{
    CreditsMenu_Show();
}

/* REGISTER, MY PASSWORD?, MY USERNAME?: the account's web pages, in the browser. */
static void open_account_page(AccountLink link)
{
    const char *url = Account_Url(link);
    if (url)
        Platform_OpenUrl(url);          /* (says so in the log when it cannot) */
}
static void registerUserMenuNews_OnSelect(void)  { open_account_page(ACCOUNT_LINK_REGISTER); }
static void registerUserMenuNews2_OnSelect(void) { open_account_page(ACCOUNT_LINK_PASSWORD); }
static void registerUserMenuNews3_OnSelect(void) { open_account_page(ACCOUNT_LINK_USERNAME); }

static void logInMenuNews_OnSelect(void)
{
    if (Account_Has(ACCOUNT_HAS_RANKINGS))      /* (the log-in's RANKINGS, SWOS United's screen) */
        OnlineMenus_Show(SWOS2020_MENU(rankingsMenu));
    else
        OnlineMenus_Show(SWOS2020_MENU(newsMenu));
}

/* ======================================================================
 * matchMakingMenu (swos-2020/src/menus/matchMaking.mnu; handlers in mpgame.cpp)
 * ====================================================================== */
/* The lobby is redrawn from MmClient's state every frame, so these buffers hold
 * the text the menu entries point at. A MenuEntry stores a POINTER, so the
 * storage has to outlive the call that sets it -- a local would leave the menu
 * drawing freed stack. */
static char s_hostRow[MatchMakingMenu_kGS_MaxRows][MM_NAME_MAX + 8];
static char s_modeRow[MatchMakingMenu_kGS_MaxRows][16];
static char s_userRow[MatchMakingMenu_kGS_MaxRows2][MM_NAME_MAX + 4];
static char s_statusText[48];
static char s_numUsersText[24];
static char s_descText[64];

/* Which open game each visible host row stands for, so a click can resolve back
 * to a name. -1 = the row is empty. */
static int32_t s_rowGame[MatchMakingMenu_kGS_MaxRows];

/* Who is waiting on our yes or no (hostGameMessageMenu). */
static char s_joinAsker[MM_NAME_MAX];

static void matchMakingMenu_OnInit(void)
{
    int32_t row;

    for (row = 0; row < MatchMakingMenu_kGS_MaxRows; row++) {
        set_visible(MatchMakingMenu_host_00 + row, 0);
        set_visible(MatchMakingMenu_mode_00 + row, 0);
        set_visible(MatchMakingMenu_action_00 + row, 0);
        s_rowGame[row] = -1;
    }
    for (row = 0; row < MatchMakingMenu_kGS_MaxRows2; row++)
        set_visible(MatchMakingMenu_users_00 + row, 0);

    set_text(MatchMakingMenu_userName, s_username);
    set_entry(MatchMakingMenu_menuExit, "EXIT", 1, kLightBrownWithRedFrame);
}

/* Runs every frame (the menu is flagged dos_redraw). This is also where the
 * client is pumped: there is no other per-frame hook while a menu is up, and
 * MmClient_Poll() is cheap when nothing has arrived. */
/* The open-games column's navigation. SWOS 2020 moves through this list in code, so
 * its .mnu gives the rows no neighbours at all, and every button below points UP at
 * the eighth row (action_07) -- hidden unless eight games are open, and with no links
 * of its own a dead end: a tester could see a game and not reach it (2026-09-24).
 * So the host rows are a column: each down to the next, the last down to the
 * buttons, and the buttons up into its bottom. A hidden row is passed over along its
 * own link (MenuProc), so UP from the buttons lands on the lowest game actually
 * listed, and with none listed it stays where it is. Every draw, because coming
 * back from SETTINGS restores the menu from its template. */
static void matchMaking_link_list(void)
{
    const int32_t last = MatchMakingMenu_host_00 + MatchMakingMenu_kGS_MaxRows - 1;
    int32_t row;
    for (row = 0; row < MatchMakingMenu_kGS_MaxRows; row++) {
        MenuEntry *e = entry_at(MatchMakingMenu_host_00 + row);
        e->leftEntry  = -1;
        e->rightEntry = -1;
        e->upEntry    = (int8_t)(row > 0 ? MatchMakingMenu_host_00 + row - 1 : -1);
        e->downEntry  = (int8_t)(row < MatchMakingMenu_kGS_MaxRows - 1
                                     ? MatchMakingMenu_host_00 + row + 1
                                     : MatchMakingMenu_openGame);
        e->directionLeft = e->directionRight = e->directionUp = e->directionDown = -1;
    }
    entry_at(MatchMakingMenu_menuSettings)->upEntry = (int8_t)last;
    entry_at(MatchMakingMenu_openGame)->upEntry     = (int8_t)last;
    entry_at(MatchMakingMenu_rankings)->upEntry     = (int8_t)last;
}

/* Where SWOS 2020's .mnu put things, corrected (ReportBot 0035). Absolute positions, set
 * on every draw: coming back from SETTINGS restores the menu from its template.
 *
 *  - The nine buttons stand in three columns from x 34 to 312 of the menu's 320, hard
 *    against the right edge. Centred under the header (whose middle is 152) they start
 *    at 13; the line of help above them moves with them.
 *  - "N ONLINE" is right-aligned in a box ending at 312, the last pixel of the 336-wide
 *    screen, where the font's trailing column fell off it and took the final letter
 *    with it. The box ends 8 short of that.
 *
 * An entry's x is the .mnu's + 24 (menu_load_json.c, as UnpackMenu). */
static void matchMaking_layout(void)
{
    enum { kLeft = 13 + 24, kStep = 97, kShift = 34 - 13 };
    static const int16_t column[3][3] = {
        { MatchMakingMenu_menuSettings, MatchMakingMenu_menuCustomTeam, -1 },
        { MatchMakingMenu_openGame, MatchMakingMenu_cancelGame, MatchMakingMenu_menuExit },
        { MatchMakingMenu_rankings, MatchMakingMenu_leagues, MatchMakingMenu_matchLogs },
    };
    int32_t c, r;

    for (c = 0; c < 3; c++)
        for (r = 0; r < 3; r++)
            if (column[c][r] >= 0)
                entry_at(column[c][r])->x = (uint16_t)(kLeft + c * kStep);
    /* RANKINGS, LEAGUES, MATCH LOGS: only a service with them (online_account.h). */
    entry_at(MatchMakingMenu_rankings)->isInvisible  = Account_Has(ACCOUNT_HAS_RANKINGS) ? 0 : 1;
    entry_at(MatchMakingMenu_leagues)->isInvisible   = Account_Has(ACCOUNT_HAS_LEAGUES) ? 0 : 1;
    entry_at(MatchMakingMenu_matchLogs)->isInvisible = Account_Has(ACCOUNT_HAS_MATCH_LOGS) ? 0 : 1;
    entry_at(MatchMakingMenu_desc)->x = (uint16_t)(20 + 24 - kShift);
    entry_at(MatchMakingMenu_connector)->x = (uint16_t)(203 + 24 - kShift);
    entry_at(MatchMakingMenu_numOfUsers)->dwWidth = 120 - 8;
}

static void matchMakingMenu_OnDraw(void)
{
    const MmUser *users;
    const MmGame *games;
    const MmGameInfo *gi;
    int32_t numUsers = 0, numGames = 0, row;
    MmState state;
    char who[MM_NAME_MAX];

    matchMaking_layout();

    /* Every row's text and visibility is rebuilt below each frame, but these two
     * were set only in onInit -- so coming back from SETTINGS, which restores this
     * menu from its template, left the player's name and the EXIT button blank.
     * Re-applied here, first, so the early return below cannot skip them. (A fork's own
     * MATCH MAKING lays them out itself.) */
    if (!FORK_HOOK(ownsMatchMaking)) {
        set_text(MatchMakingMenu_userName, s_username);
        set_entry(MatchMakingMenu_menuExit, "EXIT", 1, kLightBrownWithRedFrame);
        matchMaking_link_list();
    }

    MmClient_Poll();
    state = MmClient_State();

    /* Someone is asking to join OUR game: ask, the way SWOS 2020 does, rather
     * than accepting for the player. One-shot, so the prompt opens once per
     * request however many frames it takes to answer. SWOS_MM_AUTO=host answers
     * it for a test -- there is nobody to press the button on either machine. */
    if (MmClient_TakeJoinRequest(who, sizeof who)) {
        const int32_t allowed = Account_JoinAllowed(who);
        snprintf(s_joinAsker, sizeof s_joinAsker, "%s", who);
        if (allowed && FORK_HOOK(joinRequested))
            online_menus_fork()->joinRequested(who);
        if (!allowed) {
            /* not this game's player (a league or cup match's opponent alone), or banned from it: refused */
            fprintf(stderr, "[mm] refusing '%s': not allowed in this game\n", s_joinAsker);
            MmClient_Eject(s_joinAsker);
            s_joinAsker[0] = '\0';
        } else if (s_mmAuto == MM_AUTO_HOST) {
            if (mm_auto_is_peer(s_joinAsker))
                MmClient_Approve(s_joinAsker);
            else
                fprintf(stderr, "[mm-auto] not approving '%s': waiting for '%s'\n",
                        s_joinAsker, getenv("SWOS_MM_AUTO_PEER"));
            s_joinAsker[0] = '\0';
        } else if (FORK_HOOK(ownsMatchMaking)) {
            s_joinAsker[0] = '\0';     /* (a fork's MATCH MAKING shows the asker itself: SWOS 2020's table) */
        } else {
            OnlineMenus_Show(SWOS2020_MENU(hostGameMessageMenu));
            return;
        }
    }

    /* SWOS_MM_AUTO=host|join: press OPEN, or pick the first game on offer, as
     * soon as the server answers. Two machines, no hands -- this is how a match
     * gets played through real matchmaking without a player at each end
     * (docs/NETPLAY.md 7). It does nothing unless the variable is set. */
    if (state == MM_CONNECTED && s_mmAuto != MM_AUTO_OFF && !MmClient_GameInfo()) {
        int32_t n = 0;
        const MmGame *open = MmClient_OpenGames(&n);
        if (s_mmAuto == MM_AUTO_HOST) {
            if (!s_mmAutoDone) { MmClient_CreateGame(); s_mmAutoDone = 1; }
        } else {
            int32_t g;
            for (g = 0; g < n; g++)
                if (mm_auto_is_peer(open[g].owner)) {
                    if (FORK_HOOK(autoJoining))
                        online_menus_fork()->autoJoining(open[g].owner);
                    MmClient_JoinGame(open[g].owner);
                    break;
                }
        }
    }

    if (FORK_HOOK(ownsMatchMaking)) {            /* the rest of the menu is the fork's */
        if (FORK_HOOK(matchMakingDraw))
            online_menus_fork()->matchMakingDraw();
        if (MmClient_StartInfo())
            OnlineMenus_StartMatchFromMatchmaking();
        return;
    }
    if (MmClient_TakeRemoved())
        set_entry(MatchMakingMenu_desc, "THE HOST LEFT THE GAME", 0, 0);

    switch (state) {
    case MM_CONNECTING:
        snprintf(s_statusText, sizeof s_statusText, "CONNECTING");
        break;
    case MM_CONNECTED:
        snprintf(s_statusText, sizeof s_statusText, "ONLINE");
        break;
    case MM_FAILED:
        snprintf(s_statusText, sizeof s_statusText, "%s", MmClient_FailureText());
        break;
    default:
        snprintf(s_statusText, sizeof s_statusText, "OFFLINE");
        break;
    }
    set_text(MatchMakingMenu_connectionStatus, s_statusText);

    users = MmClient_Users(&numUsers);
    games = MmClient_OpenGames(&numGames);
    gi    = MmClient_GameInfo();

    snprintf(s_numUsersText, sizeof s_numUsersText, "%d ONLINE", numUsers);
    set_text(MatchMakingMenu_numOfUsers, state == MM_CONNECTED ? s_numUsersText : "");

    /* The open games, one per host row. */
    for (row = 0; row < MatchMakingMenu_kGS_MaxRows; row++) {
        int32_t hasGame = (state == MM_CONNECTED) && (row < numGames);
        s_rowGame[row] = hasGame ? row : -1;
        if (hasGame) {
            snprintf(s_hostRow[row], sizeof s_hostRow[row], "%s", games[row].owner);
            snprintf(s_modeRow[row], sizeof s_modeRow[row], "FRIENDLY");
            set_text(MatchMakingMenu_host_00 + row, s_hostRow[row]);
            set_text(MatchMakingMenu_mode_00 + row, s_modeRow[row]);
        }
        set_visible(MatchMakingMenu_host_00 + row, hasGame);
        set_visible(MatchMakingMenu_mode_00 + row, hasGame);
        set_visible(MatchMakingMenu_action_00 + row, 0);
    }

    /* Who else is on. */
    for (row = 0; row < MatchMakingMenu_kGS_MaxRows2; row++) {
        int32_t hasUser = (state == MM_CONNECTED) && (row < numUsers);
        if (hasUser) {
            snprintf(s_userRow[row], sizeof s_userRow[row], "%s", users[row].name);
            set_text(MatchMakingMenu_users_00 + row, s_userRow[row]);
        }
        set_visible(MatchMakingMenu_users_00 + row, hasUser);
    }

    /* The description line and what the two buttons can do, from where we are. */
    if (state != MM_CONNECTED) {
        set_entry(MatchMakingMenu_desc,
                  state == MM_CONNECTING ? "CONNECTING TO SERVER" : "SERVER NOT CONNECTED", 0, 0);
        set_entry(MatchMakingMenu_openGame, "OPEN", 0, kGray);
        set_entry(MatchMakingMenu_cancelGame, "LEAVE", 0, kGray);
    } else if (gi) {
        if (gi->numRequested > 0)
            snprintf(s_descText, sizeof s_descText, "%s WANTS TO JOIN", gi->requested[0].name);
        else if (gi->numJoined > 0)
            snprintf(s_descText, sizeof s_descText, "%s JOINED - STARTING", gi->joined[0].name);
        else
            snprintf(s_descText, sizeof s_descText, "WAITING FOR AN OPPONENT");
        set_entry(MatchMakingMenu_desc, s_descText, 0, 0);
        set_entry(MatchMakingMenu_openGame, "OPEN", 0, kGray);
        set_entry(MatchMakingMenu_cancelGame, "LEAVE", 1, kLightBrownWithRedFrame);
    } else {
        set_entry(MatchMakingMenu_desc,
                  numGames ? "PICK A GAME, OR OPEN YOUR OWN" : "OPEN A GAME AND WAIT", 0, 0);
        set_entry(MatchMakingMenu_openGame, "OPEN", 1, kLightBrownWithRedFrame);
        set_entry(MatchMakingMenu_cancelGame, "LEAVE", 0, kGray);
    }

    /* The server has paired us with somebody: hand over to the match. */
    if (MmClient_StartInfo())
        OnlineMenus_StartMatchFromMatchmaking();
}

/* ======================================================================
 * hostGameMessageMenu -- "X wants to play. Yes or no?"
 * ======================================================================
 * The host decides. Accepting sends APPROVE, which the server turns into the
 * START both peers need; refusing ejects them and leaves the game open for
 * somebody else. Either way the lobby is what we return to.
 * ====================================================================== */
static char s_joinPrompt[64];
/* The same two-button prompt asks the host's question too: which style the game it is
 * about to OPEN plays (matchMaking_openGame_onSelect). */
static int32_t s_askStyle;

static void lobby_nav(int32_t ordinal, int32_t left, int32_t right, int32_t up, int32_t down);

static void hostGameMessageMenu_OnInit(void)
{
    if (s_askStyle && FORK_HOOK(hostStyleLayout) && online_menus_fork()->hostStyleLayout()) {
        set_text(HostGameMessageMenu_exit,     "EXIT");
        return;                         /* (a fork's CHOOSE GAMEPLAY) */
    }
    if (s_askStyle) {
        set_text(HostGameMessageMenu_header,   "HOST A GAME");
        set_text(HostGameMessageMenu_message1, "WHICH GAMEPLAY?");
        set_text(HostGameMessageMenu_message2, "WHOEVER JOINS PLAYS IT TOO");
        set_text(HostGameMessageMenu_message3, "");
        set_entry(HostGameMessageMenu_button1, "AMIGA (50FPS)", 1, kLightBrownWithRedFrame);
        set_entry(HostGameMessageMenu_button2, "PC (70FPS)", 1, kLightBrownWithRedFrame);
        /* The third, OpenSWOS's own (scripts/import_swos2020_menus.py EXTRA_ENTRIES). */
        set_entry(HostGameMessageMenu_button3, "XBOX (60FPS)", 1, kLightBrownWithRedFrame);
        set_visible(HostGameMessageMenu_button3, 1);
        lobby_nav(HostGameMessageMenu_button2, -1, -1, HostGameMessageMenu_button1, HostGameMessageMenu_button3);
        lobby_nav(HostGameMessageMenu_button3, -1, -1, HostGameMessageMenu_button2, HostGameMessageMenu_exit);
        lobby_nav(HostGameMessageMenu_exit, -1, -1, HostGameMessageMenu_button3, -1);
        set_text(HostGameMessageMenu_exit,     "EXIT");
        return;
    }
    set_visible(HostGameMessageMenu_button3, 0);
    lobby_nav(HostGameMessageMenu_button2, -1, -1, HostGameMessageMenu_button1, HostGameMessageMenu_exit);
    lobby_nav(HostGameMessageMenu_exit, -1, -1, HostGameMessageMenu_button2, -1);
    snprintf(s_joinPrompt, sizeof s_joinPrompt, "%s WANTS TO PLAY", s_joinAsker);
    set_text(HostGameMessageMenu_header,   "A CHALLENGER");
    set_text(HostGameMessageMenu_message1, s_joinPrompt);
    set_text(HostGameMessageMenu_message2, "");
    set_text(HostGameMessageMenu_message3, "");
    set_entry(HostGameMessageMenu_button1, "ACCEPT", 1, kLightBrownWithRedFrame);
    set_entry(HostGameMessageMenu_button2, "REFUSE", 1, kLightBrownWithRedFrame);
    set_text(HostGameMessageMenu_exit,     "EXIT");
}

/* The lobby is not being drawn while this prompt is up, and its OnDraw is what
 * pumps the client -- so this one has to, or the connection would time out
 * exactly while the player is deciding. */
static void hostGameMessageMenu_OnDraw(void)
{
    MmClient_Poll();
    if (MmClient_State() != MM_CONNECTED)
        SetExitMenuFlag();          /* nothing left to accept */
}

/* OPEN's question answered: the style, as OPTIONS would set it (saved), then the game. */
static void host_in_style(int style)
{
    extern void GameplayStyle_Set(int style, int persist);
    s_askStyle = 0;
    GameplayStyle_Set(style, 1);
    fprintf(stderr, "[mm] hosting a %s game\n", Gameplay_StyleName(style));
    MmClient_CreateGame();
    SetExitMenuFlag();
}

static void hostGameMessageMenu_button1_OnSelect(void)   /* ACCEPT */
{
    if (s_askStyle) {
        host_in_style(GAMEPLAY_AMIGA);
        return;
    }
    if (s_joinAsker[0])
        MmClient_Approve(s_joinAsker);
    s_joinAsker[0] = '\0';
    SetExitMenuFlag();
}

static void hostGameMessageMenu_button2_OnSelect(void)   /* REFUSE */
{
    if (s_askStyle) {
        host_in_style(GAMEPLAY_DOS);
        return;
    }
    if (s_joinAsker[0])
        MmClient_Eject(s_joinAsker);
    s_joinAsker[0] = '\0';
    SetExitMenuFlag();
}

static void hostGameMessageMenu_button3_OnSelect(void)   /* only for the style question */
{
    if (s_askStyle)
        host_in_style(GAMEPLAY_XBOX);
}

/* ======================================================================
 * The handoff: matchmaking is over, connect to the peer
 * ======================================================================
 * The server has paired us and given us the other player's addresses. From here
 * the server is out of the loop and the two of us talk directly: both punch to all
 * of the other's addresses over p2p_link (docs/NETPLAY.md sections 6 and 7), then
 * the lobby (mpGameSetupMenu below) sets the match up and the match is played. */

static char s_matchStatus[64];
static char s_matchPeer[MM_NAME_MAX];   /* the opponent, for the status when the link falls back to the relay */
static int32_t s_matchPlayer;      /* 1 = we created the game, 2 = we joined */
static char    s_myName[MM_NAME_MAX];

void OnlineMenus_StartMatchFromMatchmaking(void)
{
    const MmStartInfo *st = MmClient_StartInfo();
    if (!st)
        return;

    /* Take it once: this runs from a per-frame draw, and without clearing it we
     * would try to connect again on every one of them. */
    MmClient_ClearStart();

    s_matchPlayer  = st->playerNumber;
    /* Our login name, for the lobby's player column: read it before the disconnect
     * below forgets it. */
    snprintf(s_myName, sizeof s_myName, "%s", MmClient_UserName());

    /* NAT traversal: BOTH peers punch to all of the other's candidates at once.
     * Neither hosts -- a peer that only listens is unreachable from behind a
     * router, because a NAT lets a datagram in only after one has gone out to
     * that address. The player number decides who is player 1 and nothing else.
     *
     * The local port is the one the matchmaker saw us on, so the mapping its own
     * traffic already opened is the one the peer's packets arrive through -- and
     * that is exactly why the matchmaker's OWN SOCKET HAS TO GO FIRST. It still
     * holds that port; binding it fails while it is open, and the link quietly
     * takes the next one instead (kJoinPortTries). The peer is then punching to
     * the port the server saw, one below the one we are listening on, and
     * NOTHING ever arrives -- on either side, since both do it. Measured across
     * two machines: Windows on :61166 punched at :51977 while the Mac sat on
     * :51978 punching at :61165.
     *
     * So: copy what is needed out of the client's state, close it, then punch. */
    /*
     * A RELAYED match (include/relay_proto.h) is the same thing with one candidate:
     * the server. Both players register there with the token and send it
     * everything; it forwards to the other. p2p_link and GGPO each see one peer at
     * one address, and never learn that the address is the server's. */
    {
        char  addrs[MM_MAX_PEER_ADDRS][MM_ADDR_TEXT_MAX];
        const char *list[MM_MAX_PEER_ADDRS];
        char  peerName[MM_NAME_MAX];
        uint16_t localPort = st->localPort;
        int32_t i, n = 0;
        int32_t relay = st->relay;

        if (relay) {
            snprintf(addrs[0], sizeof addrs[0], "%s", st->relayAddr);
            list[0] = addrs[0];
            n = 1;
            P2PLink_SetRelay(st->relayToken, st->playerNumber);
            P2PLink_SetRelayFallback(0, 0, NULL);
        } else {
#ifdef SWOS_WASM_BUILD
            /* A browser reaches nothing but the server (its datagrams are WebSocket
             * messages to it), so a match the server does not relay cannot be played:
             * n stays 0 and the waiting screen says why. */
            P2PLink_SetRelay(0, 0);
            snprintf(peerName, sizeof peerName, "%s", st->peerName);
            MmClient_Disconnect();
            st = NULL;
            snprintf(s_matchStatus, sizeof s_matchStatus, "THIS SERVER CANNOT RELAY A BROWSER GAME");
            OnlineMenus_Show(SWOS2020_MENU(mpWaitingMenu));
            return;
#endif
            for (i = 0; i < st->numAddrs && i < MM_MAX_PEER_ADDRS; i++) {
                snprintf(addrs[n], sizeof addrs[n], "%s", st->addrs[i]);
                list[n] = addrs[n];
                n++;
            }
            /* Not relayed -- but the link still needs to know which player it is: with
             * several candidate addresses the two sides settle on player 1's path. */
            P2PLink_SetRelay(0, st->playerNumber);
            /* And if the server made a session to fall back to (RELAYFB:), the link
             * moves to it when the direct punch gets no round trip (p2p_link.c). */
            P2PLink_SetRelayFallback(st->relayFallbackToken, st->playerNumber, st->serverAddr);
        }
        snprintf(peerName, sizeof peerName, "%s", st->peerName);
        snprintf(s_matchPeer, sizeof s_matchPeer, "%s", st->peerName);
        P2PLink_SetServer(st->serverAddr);  /* the lobby asks it about the two teams (team_proto.h) */

        /* The lobby is done with; from here the two of us talk directly. */
        MmClient_Disconnect();
        MatchRecord_SetArrangedByServer(1);     /* [netplay] record = server records it */
        st = NULL;      /* it pointed into what that just cleared */

        if (n == 0 || P2PLink_Punch(list, n, localPort) != 0)
            snprintf(s_matchStatus, sizeof s_matchStatus, "NO WAY TO REACH %s", peerName);
        else if (relay)
            snprintf(s_matchStatus, sizeof s_matchStatus, "CONNECTING TO %s VIA THE SERVER",
                     peerName);
        else
            snprintf(s_matchStatus, sizeof s_matchStatus, "CONNECTING TO %s (%d route%s)",
                     peerName, n, n == 1 ? "" : "s");
    }

    OnlineMenus_Show(SWOS2020_MENU(mpWaitingMenu));

    /* Back on MATCH MAKING: the lobby was left, or the link never came. The connection to the server
     * went to make room for the link (above): log in again, or the player is left with a list
     * that says DISCONNECTED. */
    if (MmClient_State() == MM_IDLE && s_myName[0])
        MmClient_Connect(mm_host(), mm_port(), s_myName, NULL);
}

const char *OnlineMenus_MatchStatus(void)
{
    return s_matchStatus;
}

/* ======================================================================
 * mpWaitingMenu -- until the two players are connected
 * ====================================================================== */
static char s_waitLine[80];

/* Connected and the input delay agreed: into the lobby. It returns when the match has
 * been played or somebody has left, and either way this menu is done too. */
static void mpWaitingMenu_OnDraw(void)
{
    P2PLinkState st;

    P2PLink_Poll();
    st = P2PLink_State();

    if (st == P2P_CONNECTED && P2PLink_AgreedDelay() >= 0) {
        OnlineMenus_EnterLobby(s_matchPlayer, s_myName);
        SetExitMenuFlag();
        return;
    }

    switch (st) {
    case P2P_CONNECTED: {
        int32_t ping = P2PLink_PingMs();
        if (ping >= 0)
            snprintf(s_waitLine, sizeof s_waitLine, "CONNECTED TO %s - %d MS",
                     P2PLink_PeerText(), ping);
        else
            snprintf(s_waitLine, sizeof s_waitLine, "CONNECTED TO %s", P2PLink_PeerText());
        break;
    }
    case P2P_HOSTING:
    case P2P_JOINING:
        /* The direct punch failed and the link moved to the server (p2p_link.c,
         * relay_fallback): say so, rather than the route count it gave up on. */
        if (P2PLink_FellBackToRelay() && !strstr(s_matchStatus, "VIA THE SERVER"))
            snprintf(s_matchStatus, sizeof s_matchStatus, "CONNECTING TO %s VIA THE SERVER",
                     s_matchPeer);
        snprintf(s_waitLine, sizeof s_waitLine, "%s", s_matchStatus);
        break;
    case P2P_FAILED: {
        /* Why, not just that: a refused handshake (two gameplay styles) looked
         * exactly like a firewall here, and cost a morning's test run. */
        const char *line1, *line2;
        P2PLink_FailureText(P2PLink_Failure(), &line1, &line2);
        snprintf(s_waitLine, sizeof s_waitLine, "%s", line1[0] ? line1 : "CONNECTION FAILED");
        break;
    }
    default:
        snprintf(s_waitLine, sizeof s_waitLine, "%s", s_matchStatus);
        break;
    }
    set_text(MpWaitingMenu_message1, s_waitLine);
}

static void mpWaiting_menuExit_OnSelect(void)
{
    P2PLink_Close();
    SetExitMenuFlag();
}

/* ======================================================================
 * mpGameSetupMenu -- the lobby (swos-2020/src/menus/mpGameSetup.mnu)
 * ======================================================================
 * The protocol is src/netplay_lobby.c; this is only its face. Row 01 is player 1
 * (the host, the top team), row 02 player 2. A player can change their own row --
 * TEAMS opens the game's team list, TACTICS steps through the formations and the
 * USER tactics, GO says ready -- and the host the rules on the right. START needs
 * both GO lights on.
 *
 * Kept from SWOS 2020's screen: the columns, the rules, the latency, START and
 * CANCEL. Hidden until they exist here: the location flags, the ranked and
 * tournament modes, the team database choice, cup legs, the chat and the live
 * cast. */

/* The team list is a MENU of its own, modal, and while it is up nothing calls this
 * screen's onDraw -- so nothing would poll the link, and the other player would
 * see this one go quiet and drop the connection after six seconds. The frame hook
 * keeps it breathing: Platform_VBlank runs it on every frame of every menu. */
static void lobby_frame_hook(void)
{
    P2PLink_Poll();
    MatchReport_Poll();
    MatchPrediction_Poll();         /* the last match's report crosses while the lobby is up */
}

/* Test knobs for looking at the lobby without a pad (docs/NETPLAY.md, "The lobby"):
 * SWOS_LOBBY_TEAM=<file>:<ordinal> in hex picks a team as the lobby opens,
 * SWOS_LOBBY_READY=1 says ready -- as it opens, and again each time it comes back
 * after a match -- SWOS_LOBBY_START=1 has the host press START the moment it can, and
 * SWOS_LOBBY_MATCHES=N has both leave the game after N matches, the host once the lobby
 * is back up and the joiner when the host has gone. SWOS_LOBBY_SWITCHES=<hex> sets the
 * host's switches (NETPLAY_SW_*, netplay_match.h). */
static int32_t s_lobbyAgain;        /* run_live_match: show the lobby again, for a rematch */
static int32_t s_lobbyMatches;      /* matches played from this lobby */
static int32_t s_testReady;         /* SWOS_LOBBY_READY: say ready once the lobby is open */
static int32_t s_matchEndedEarly;   /* the last match was cut short: its notice says why */

static void lineup_test_swap(void);    /* SWOS_LOBBY_LINEUP_SWAP, beside the TACTICS screen */
static void lineup_screen(void);       /* the team screen, from TEAMS and TACTICS */
static void custom_tactics_warning(void);   /* after the team screen, a ranked match (SWOS 2020) */

static void lobby_test_knobs(void)
{
    extern uint8_t _teamFileBuffer[];
    const char *team = getenv("SWOS_LOBBY_TEAM");
    const char *cup = getenv("SWOS_LOBBY_CUP");     /* host: <legs>:<1 ET+PEN | 2 PEN ONLY> */
    const char *sw = getenv("SWOS_LOBBY_SWITCHES"); /* host: NETPLAY_SW_* in hex */
    const char *say = getenv("SWOS_LOBBY_SAY");     /* a chat line, said as the lobby opens */
    const char *mode = getenv("SWOS_LOBBY_MODE");   /* host: what OPEN's choice would have been */
    unsigned file, ordinal, legs, tie, mask;

    if (say && say[0])
        (void)NetplayLobby_Say(say);
#if SWOS_CHAT_DYNAMIC_FONT
    if (getenv("SWOS_LOBBY_CHAT_OPEN")) {           /* the chat field open, for SWOS_KEY_SCRIPT to type */
        extern void OnlineMenus_LobbyChatOpen(void);
        OnlineMenus_LobbyChatOpen();
    }
#endif
    if (getenv("SWOS_LOBBY_EXTRA_DELAY"))           /* FRAME DELAY +: this player's extra frames */
        NetplayLobby_SetExtraDelay(atoi(getenv("SWOS_LOBBY_EXTRA_DELAY")));

    if (mode && NetplayLobby_IsHost()) {
        LobbyRules r = *NetplayLobby_Rules();
        r.mode = (uint16_t)(!strcmp(mode, "ranked") ? LOBBY_MODE_RANKED :
                            !strcmp(mode, "league") ? LOBBY_MODE_LEAGUE : LOBBY_MODE_FRIENDLY);
        NetplayLobby_SetRules(&r);
    }

    if (sw && NetplayLobby_IsHost() && sscanf(sw, "%x", &mask) == 1) {
        LobbyRules r = *NetplayLobby_Rules();
        r.switches = (uint16_t)mask;
        NetplayLobby_SetRules(&r);
    }

    if (cup && NetplayLobby_IsHost() && sscanf(cup, "%u:%u", &legs, &tie) == 2) {
        LobbyRules r = *NetplayLobby_Rules();
        r.legs = (uint16_t)legs;
        r.tieBreak = (uint16_t)tie;
        NetplayLobby_SetRules(&r);
    }

    if (team && sscanf(team, "%x:%x", &file, &ordinal) == 2 && LoadTeamFile((uint16_t)file) == 0) {
        uint16_t count = *(uint16_t *)_teamFileBuffer, i;     /* host-native count, then records */
        TeamFileEx *rec = (TeamFileEx *)(_teamFileBuffer + 2);
        for (i = 0; i < count; i++) {
            if (rec[i].team.countryNumber == file && rec[i].team.teamOrdinal == ordinal) {
                lobby_pick_team(&rec[i]);
                lineup_test_swap();
                break;
            }
        }
    }
    s_testReady = getenv("SWOS_LOBBY_READY") != NULL;
}

/* SWOS_LOBBY_READY and SWOS_LOBBY_MATCHES, from the lobby's draw. */
static void lobby_test_draw(LobbyState st)
{
    const char *matches = getenv("SWOS_LOBBY_MATCHES");

    if (s_testReady && st == LOBBY_OPEN) {
        NetplayLobby_SetReady(1);
        s_testReady = 0;
    }
    static uint32_t openSince;
    static int32_t openAfter = -1;      /* the matches played when openSince was taken */
    if (st != LOBBY_OPEN || openAfter != s_lobbyMatches) {
        openSince = 0;                  /* (a match is played without this draw: reset here) */
        openAfter = s_lobbyMatches;
    }
    if (st != LOBBY_OPEN)
        openSince = 0;
    else if (!openSince)
        openSince = SDL_GetTicks() | 1u;
    /* The host leaves once the lobby has been back up for three seconds -- long enough
     * for the joiner to be back too, and for a capture to see the screen -- and the last
     * match's report has crossed both ways. SWOS_LOBBY_LINGER_MS: longer (a service's
     * requests after the match: a fork's results to its site). */
    const char *linger = getenv("SWOS_LOBBY_LINGER_MS");
    const uint32_t stay = linger ? (uint32_t)atoi(linger) : 3000;
    if (matches && s_lobbyMatches >= atoi(matches) &&
        (st == LOBBY_FAILED ||
         (st == LOBBY_OPEN && NetplayLobby_IsHost() && SDL_GetTicks() - openSince > stay &&
          !MatchReport_Pending()))) {
        printf("[lobby] %d match%s played; leaving (%s)\n", s_lobbyMatches,
               s_lobbyMatches == 1 ? "" : "es", st == LOBBY_FAILED ? NetplayLobby_Notice() : "host");
        fflush(stdout);
        MatchReport_Flush(20000);
        Account_LobbyClosed();          /* (as leaving does: a service's last requests) */
        P2PLink_Close();
        exit(0);
    }
}

/* The lobby, for as long as the two players stay in it: after every match it comes
 * back, reconnecting to the same player (run_live_match), until one of them leaves. */
static int32_t s_peerTold;           /* Account_LobbyPeer has had the other player's name */
static int32_t s_openMode;          /* LOBBY_MODE_*: what OPEN's game counts for (online_account.h) */

void OnlineMenus_EnterLobby(int32_t player, const char *name)
{
    OnlineMenus_Init();
    if (FORK_HOOK(lobbyEntered))
        online_menus_fork()->lobbyEntered(player);
    NetplayLobby_Begin(player, name);
    if (player == 1 && s_openMode != LOBBY_MODE_FRIENDLY) {    /* what OPEN's game counts for */
        LobbyRules r = *NetplayLobby_Rules();
        r.mode = (uint16_t)s_openMode;
        NetplayLobby_SetRules(&r);
    }
    s_peerTold = 0;
    Account_LobbyOpened(player == 1);
    lobby_test_knobs();
    s_lobbyMatches = 0;
    do {
        s_lobbyAgain = 0;
        OnlineMenus_Show(SWOS2020_MENU(mpGameSetupMenu));
    } while (s_lobbyAgain);
    MatchReport_Poll();
    MatchPrediction_Poll();
    MatchReport_Finish();       /* the other player's half, if it came, before the link goes */
    Account_LobbyClosed();      /* (a service's last word on this lobby's matches) */
    if (FORK_HOOK(lobbyLeft))
        online_menus_fork()->lobbyLeft();
    NetplayLobby_End();
    P2PLink_Close();
    Platform_SetFrameHook(NULL);
    {
        /* Back to this player's own style, if the game was the other player's
         * (p2p_link.c link_connected). Only here, where the lobby is left: the link
         * also closes before every match, and switching back then would play the
         * match in a different style from the host's. */
        extern void GameplayStyle_GiveBack(void);
        GameplayStyle_GiveBack();
    }
}

static char s_lobbyHeader[40], s_lobbyStyle[24], s_lobbyConn[24], s_lobbyPing[16];
static char s_lobbyActivity[64];
static char s_rowName[2][LOBBY_NAME_MAX + 1];
static char s_rowTeam[2][18];
static char s_rowTactic[2][10];
static char s_ruleMin[8], s_ruleMax[8], s_ruleDelay[12];

/* The ordinal of this machine's entry in the column whose header row is `column00`. */
static int32_t my_row(int32_t column00)    { return column00 + NetplayLobby_LocalPlayer(); }

static void lobby_hide(int32_t ordinal)    { set_visible(ordinal, 0); }

static const char *tactic_name(const LobbyPlayer *p, char *buf, uint32_t cap)
{
    extern void *_tacticsStringTable[];
    extern uint8_t _teamTacticsUSERA[], _teamTacticsUSERB[], _teamTacticsUSERC[];
    extern uint8_t _teamTacticsUSERD[], _teamTacticsUSERE[], _teamTacticsUSERF[];
    static uint8_t *const slots[6] = {
        _teamTacticsUSERA, _teamTacticsUSERB, _teamTacticsUSERC,
        _teamTacticsUSERD, _teamTacticsUSERE, _teamTacticsUSERF,
    };
    const char *name;

    if (!p->hasTeam)
        return "-";
    if (p->tactic < TACTIC_USER_A)
        name = (const char *)_tacticsStringTable[p->tactic];
    else if (p == NetplayLobby_Player(NetplayLobby_LocalPlayer()))
        name = (const char *)slots[p->tactic - TACTIC_USER_A];     /* our own slot, as it is */
    else
        name = (const char *)p->userTactic.name;                   /* theirs, as sent */
    snprintf(buf, cap, "%.9s", name);
    return buf;
}

static const char *pitch_name(uint16_t pitch)
{
    extern const char _aRandom[], _aFrozen[], _aMuddy[], _aWet[], _aSoft[];
    extern const char _aNormal[], _aDry[], _aHard[];
    static const char *const names[7] = { _aFrozen, _aMuddy, _aWet, _aSoft, _aNormal, _aDry, _aHard };
    return pitch < 7 ? names[pitch] : _aRandom;
}

/* The CUP row: SWOS 2020's legOptStr / etPenStr, with the leg a two-leg tie is on. */
static const char *legs_name(const LobbyRules *r)
{
    if (r->legs == 1)
        return "1 LEG";
    if (r->legs == 2)
        return r->leg == 2 ? "2ND LEG" : "2 LEGS";
    return "OFF";
}

static const char *tie_name(const LobbyRules *r)
{
    if (r->legs == 0)
        return "-";
    return r->tieBreak == LOBBY_TIE_PEN_ONLY ? "PEN ONLY" : "ET + PEN";
}

static const char *length_name(uint16_t length)
{
    extern const char _a3Mins[], _a5Mins[], _a7Mins[], _a10Mins[];
    static const char *const names[4] = { _a3Mins, _a5Mins, _a7Mins, _a10Mins };
    return names[length < 4 ? length : 0];
}

/* Where the cursor goes from an entry. SWOS 2020 wires this screen in code too
 * (setEntry in mpGameSetupMenu_OnDraw): the .mnu gives most of these entries no
 * neighbours at all, and a direction with none goes nowhere. A disabled or hidden
 * neighbour is passed over along ITS link in the same direction (MenuProc), so one
 * layout serves both players: the joiner's DOWN runs through the host's rules to
 * CANCEL. */
/* The host's switches (netplay_match.h), one row each on the left of the lobby: the
 * entries scripts/import_swos2020_menus.py adds to SWOS 2020's screen. */
enum { LOBBY_SWITCH_ROWS = 3 };
static const struct { int32_t entry; uint16_t sw; } s_switchRows[LOBBY_SWITCH_ROWS] = {
    { MpGameSetupMenu_switchShortPresses,  NETPLAY_SW_SHORT_PRESSES },
    { MpGameSetupMenu_switchKickoffSync,   NETPLAY_SW_KICKOFF_SYNC },
    { MpGameSetupMenu_switchCardsInjuries, NETPLAY_SW_CARDS_INJURIES },
};

/* The left half under the players is the CHAT (SWOS 2020's lobby chat), or -- SWITCHES,
 * where SWOS 2020 has LIVE CAST URL (no live cast here) -- the host's switches; the same
 * button says CHAT then and brings the chat back. */
enum { LOBBY_PANE_CHAT, LOBBY_PANE_SWITCHES };
enum { CHAT_ROWS = 10 };                /* chatMsg00..09: a line is two rows, its name and its text */
static int32_t  s_lobbyPane;
static int32_t  s_chatScroll;           /* lines back from the newest */
static uint32_t s_chatSeen;             /* NetplayLobby_ChatSerial when last drawn */
#if !SWOS_CHAT_DYNAMIC_FONT
static char     s_chatRow[CHAT_ROWS][LOBBY_CHAT_TEXT + 1];     /* (the game's font: chat_style.h) */
static char     s_chatInput[LOBBY_CHAT_TEXT + 1];
#endif


static void lobby_nav(int32_t ordinal, int32_t left, int32_t right, int32_t up, int32_t down)
{
    MenuEntry *e = entry_at(ordinal);
    e->leftEntry  = (int8_t)left;
    e->rightEntry = (int8_t)right;
    e->upEntry    = (int8_t)up;
    e->downEntry  = (int8_t)down;
    e->directionLeft = e->directionRight = e->directionUp = e->directionDown = -1;
}

/* What this screen hides, labels and links. Applied on EVERY draw, not once in onInit:
 * coming back from the team list restores the menu from its template -- every entry
 * visible again, the column headers blank -- and there is no onReturn to redo it in. */
static void lobby_layout(void)
{
    int32_t i;
    int32_t row = NetplayLobby_LocalPlayer();       /* 1 or 2: our row */
    int32_t teams = MpGameSetupMenu_teams_00 + row;
    int32_t tactics = MpGameSetupMenu_tactics_00 + row;
    int32_t go = MpGameSetupMenu_go_00 + row;

    lobby_nav(teams,   -1,      tactics, -1, MpGameSetupMenu_legOpt1);
    lobby_nav(tactics, teams,   go,      -1, MpGameSetupMenu_etPen1);
    lobby_nav(go,      tactics, -1,      -1, MpGameSetupMenu_etPen1);
    lobby_nav(MpGameSetupMenu_legOpt1, -1, MpGameSetupMenu_etPen1, teams, MpGameSetupMenu_gameLength1);
    lobby_nav(MpGameSetupMenu_etPen1, MpGameSetupMenu_legOpt1, -1, tactics, MpGameSetupMenu_gameLength1);
    lobby_nav(MpGameSetupMenu_gameLength1, -1, -1, MpGameSetupMenu_etPen1, MpGameSetupMenu_pitchType1);
    lobby_nav(MpGameSetupMenu_pitchType1,  -1, -1, MpGameSetupMenu_gameLength1, MpGameSetupMenu_minSubs1);
    lobby_nav(MpGameSetupMenu_minSubs1, -1, MpGameSetupMenu_maxSubs1,
              MpGameSetupMenu_pitchType1, MpGameSetupMenu_ggpoFrameDelayDec);
    lobby_nav(MpGameSetupMenu_maxSubs1, MpGameSetupMenu_minSubs1, -1,
              MpGameSetupMenu_pitchType1, MpGameSetupMenu_ggpoFrameDelayInc);
    /* FRAME DELAY: each player's own - and + (the value between puts it back to AUTO). */
    lobby_nav(MpGameSetupMenu_ggpoFrameDelayDec, -1, MpGameSetupMenu_ggpoFrameDelay_,
              MpGameSetupMenu_minSubs1, MpGameSetupMenu_cancel);
    lobby_nav(MpGameSetupMenu_ggpoFrameDelay_, MpGameSetupMenu_ggpoFrameDelayDec,
              MpGameSetupMenu_ggpoFrameDelayInc, MpGameSetupMenu_minSubs1, MpGameSetupMenu_start);
    lobby_nav(MpGameSetupMenu_ggpoFrameDelayInc, MpGameSetupMenu_ggpoFrameDelay_, -1,
              MpGameSetupMenu_maxSubs1, MpGameSetupMenu_start);
    lobby_nav(MpGameSetupMenu_cancel, -1, MpGameSetupMenu_start, MpGameSetupMenu_ggpoFrameDelayDec, -1);
    lobby_nav(MpGameSetupMenu_start, MpGameSetupMenu_cancel, -1, MpGameSetupMenu_ggpoFrameDelayInc, -1);

    /* The pane's button, under our row; then the chat or the switches below it, and the
     * rules' LEFT comes back to them. */
    lobby_nav(MpGameSetupMenu_liveCastUrlBtn, -1, MpGameSetupMenu_legOpt1, teams,
              s_lobbyPane == LOBBY_PANE_CHAT ? MpGameSetupMenu_scrollUp : s_switchRows[0].entry);
    if (s_lobbyPane == LOBBY_PANE_SWITCHES) {
        /* The host's switches, a column: RIGHT goes to the rules beside them. The joiner's
         * are disabled, so the cursor passes through them as it does the joiner's rules. */
        static const int32_t right[LOBBY_SWITCH_ROWS] = {
            MpGameSetupMenu_legOpt1, MpGameSetupMenu_gameLength1, MpGameSetupMenu_pitchType1,
        };
        for (i = 0; i < LOBBY_SWITCH_ROWS; i++)
            lobby_nav(s_switchRows[i].entry, -1, right[i],
                      i ? s_switchRows[i - 1].entry : MpGameSetupMenu_liveCastUrlBtn,
                      i + 1 < LOBBY_SWITCH_ROWS ? s_switchRows[i + 1].entry : MpGameSetupMenu_cancel);
        entry_at(MpGameSetupMenu_legOpt1)->leftEntry = (int8_t)MpGameSetupMenu_liveCastUrlBtn;
        entry_at(MpGameSetupMenu_gameLength1)->leftEntry = (int8_t)MpGameSetupMenu_switchShortPresses;
        entry_at(MpGameSetupMenu_pitchType1)->leftEntry = (int8_t)MpGameSetupMenu_switchKickoffSync;
        entry_at(MpGameSetupMenu_minSubs1)->leftEntry = (int8_t)MpGameSetupMenu_switchCardsInjuries;
        entry_at(MpGameSetupMenu_cancel)->upEntry = (int8_t)MpGameSetupMenu_switchCardsInjuries;
        entry_at(MpGameSetupMenu_cancel)->leftEntry = (int8_t)MpGameSetupMenu_switchCardsInjuries;
        for (i = 0; i < LOBBY_SWITCH_ROWS; i++) {               /* (hiding stays: show both panes' own) */
            set_visible(s_switchRows[i].entry, 1);
            set_visible(s_switchRows[i].entry - 1, 1);
        }
        for (i = MpGameSetupMenu_chatMsg00; i <= MpGameSetupMenu_chatMsg09; i++)
            lobby_hide(i);
        lobby_hide(MpGameSetupMenu_scrollUp);
        lobby_hide(MpGameSetupMenu_scrollDown);
        lobby_hide(MpGameSetupMenu_inputChatMsg);
#ifndef SWOS_TEST_RULES
        /* A release has the old rule compiled out: nothing to switch. */
        lobby_hide(MpGameSetupMenu_switchCardsInjuriesLabel);
        lobby_hide(MpGameSetupMenu_switchCardsInjuries);
#endif
    } else {
        /* The chat: the two arrows down its right edge, then the line to type in. */
        lobby_nav(MpGameSetupMenu_scrollUp, -1, MpGameSetupMenu_gameLength1,
                  MpGameSetupMenu_liveCastUrlBtn, MpGameSetupMenu_scrollDown);
        lobby_nav(MpGameSetupMenu_scrollDown, -1, MpGameSetupMenu_minSubs1,
                  MpGameSetupMenu_scrollUp, MpGameSetupMenu_inputChatMsg);
        lobby_nav(MpGameSetupMenu_inputChatMsg, -1, MpGameSetupMenu_cancel,
                  MpGameSetupMenu_scrollDown, -1);
        entry_at(MpGameSetupMenu_legOpt1)->leftEntry = (int8_t)MpGameSetupMenu_liveCastUrlBtn;
        entry_at(MpGameSetupMenu_gameLength1)->leftEntry = (int8_t)MpGameSetupMenu_scrollUp;
        entry_at(MpGameSetupMenu_pitchType1)->leftEntry = (int8_t)MpGameSetupMenu_scrollUp;
        entry_at(MpGameSetupMenu_minSubs1)->leftEntry = (int8_t)MpGameSetupMenu_scrollDown;
        entry_at(MpGameSetupMenu_cancel)->leftEntry = (int8_t)MpGameSetupMenu_inputChatMsg;
        for (i = 0; i < LOBBY_SWITCH_ROWS; i++) {
            lobby_hide(s_switchRows[i].entry);
            lobby_hide(s_switchRows[i].entry - 1);      /* (its label, just before it) */
        }
        for (i = MpGameSetupMenu_chatMsg00; i <= MpGameSetupMenu_chatMsg09; i++)
            set_visible(i, 1);
        set_visible(MpGameSetupMenu_scrollUp, 1);
        set_visible(MpGameSetupMenu_scrollDown, 1);
        set_visible(MpGameSetupMenu_inputChatMsg, 1);
    }

    lobby_hide(MpGameSetupMenu_teamDB);
    lobby_hide(MpGameSetupMenu_latency);
    lobby_hide(MpGameSetupMenu_latencyPercent);
    for (i = 0; i < MpGameSetupMenu_kGS_MaxRows; i++)
        lobby_hide(MpGameSetupMenu_location_00 + i);
    lobby_hide(MpGameSetupMenu_rankedMode1Label);
    lobby_hide(MpGameSetupMenu_rankedModeOption1);
    lobby_hide(MpGameSetupMenu_teamDB1Label);
    lobby_hide(MpGameSetupMenu_teamDB1);
    lobby_hide(MpGameSetupMenu_matchInfo);
    set_entry(MpGameSetupMenu_chatLabel, s_lobbyPane == LOBBY_PANE_CHAT ? "CHAT" : "SWITCHES", 0, kGreen);
    entry_at(MpGameSetupMenu_chatLabel)->dwWidth = 69;      /* (the .mnu's runs under the button) */
    /* The arrows: SWOS 2020 draws its own; here, small boxes with the direction in them. */
    entry_at(MpGameSetupMenu_scrollUp)->background = ENTRY_FRAME_AND_BACK_COLOR;
    entry_at(MpGameSetupMenu_scrollDown)->background = ENTRY_FRAME_AND_BACK_COLOR;
    set_entry(MpGameSetupMenu_scrollUp, "+", 1, kRed);
    set_entry(MpGameSetupMenu_scrollDown, "-", 1, kRed);
    set_entry(MpGameSetupMenu_liveCastUrlBtn, s_lobbyPane == LOBBY_PANE_CHAT ? "SWITCHES" : "CHAT", 1,
              kLightBlue);
#if SWOS_CHAT_DYNAMIC_FONT
    set_text(MpGameSetupMenu_inputChatMsg, "");     /* (drawn in the chat font: OnlineMenus_DrawOverlay) */
#else
    set_text(MpGameSetupMenu_inputChatMsg, s_chatInput);
#endif

    set_entry(MpGameSetupMenu_cPlayers_00, "PLAYERS", 0, kGray);
    set_entry(MpGameSetupMenu_teams_00,    "TEAMS",   0, kGray);
    set_entry(MpGameSetupMenu_tactics_00,  "TACTICS", 0, kGray);
    set_entry(MpGameSetupMenu_go_00,       "GO",      0, kGray);
    set_text(MpGameSetupMenu_latencyLabel, "LATENCY:");
    set_entry(MpGameSetupMenu_gameStyle1, "", 0, kGray);        /* the link already made both agree */
}

/* ---- the chat ---- */

enum { kWhiteText2 = 2, kGreenText = 14, kYellowText = 15, kLeftAligned = 1 << 15 };

#if SWOS_CHAT_DYNAMIC_FONT
/* The chat's rows (chatMsg00..09) and its field (inputChatMsg) keep their boxes but draw no
 * text of the game's font: lobby_chat_overlay draws the lines over them in the chat font --
 * small letters, accents, sharp at the window's size (chat_font.h). */
static void chat_draw(void)
{
    int32_t i;
    if (s_lobbyPane != LOBBY_PANE_CHAT)
        return;
    if (NetplayLobby_ChatSerial() != s_chatSeen) {      /* a new line: back to the newest */
        s_chatSeen = NetplayLobby_ChatSerial();
        s_chatScroll = 0;
    }
    if (s_chatScroll > NetplayLobby_ChatCount() - 1)
        s_chatScroll = NetplayLobby_ChatCount() > 1 ? NetplayLobby_ChatCount() - 1 : 0;
    for (i = MpGameSetupMenu_chatMsg00; i <= MpGameSetupMenu_chatMsg09; i++)
        entry_at(i)->fg = (uintptr_t)"";
}

static int32_t lobby_on_screen(void);
static void mpGameSetupMenu_OnInit(void);

/* "<name>: " -- a player's name, or "Player N" while theirs has not arrived yet. */
static void lobby_chat_name(char *out, uint32_t cap, int32_t who)
{
    const int32_t player = who == LOBBY_CHAT_ME ? NetplayLobby_LocalPlayer() : 3 - NetplayLobby_LocalPlayer();
    const LobbyPlayer *p = (player == 1 || player == 2) ? NetplayLobby_Player(player) : NULL;
    if (p && p->name[0])
        snprintf(out, cap, "%s: ", p->name);
    else
        snprintf(out, cap, "Player %d: ", (int)player);
}

/* The chat over the lobby: the lines in the rows' box, newest at the bottom (UP goes back),
 * wrapped, each its sayer then what they said -- the bot's yellow when someone arrives, green
 * otherwise (SWOS 2020's colours); and the field: the line being typed, with its caret. */
void OnlineMenus_DrawOverlay(void)
{
    if (!Gfx_InFrame() || !lobby_on_screen() || s_lobbyPane != LOBBY_PANE_CHAT)
        return;
    const MenuEntry *top = entry_at(MpGameSetupMenu_chatMsg00), *bot = entry_at(MpGameSetupMenu_chatMsg09);
    const MenuEntry *field = entry_at(MpGameSetupMenu_inputChatMsg);
    float px, py, pw, ph;
    Gfx_PictureRect(&px, &py, &pw, &ph);
    if (pw <= 0.0f || ph <= 0.0f || _screen_width <= 0 || _screen_height <= 0)
        return;
    const float sx = pw / _screen_width, sy = ph / _screen_height;
    int32_t size = (int32_t)(sy * 7.0f + 0.5f);          /* the game's small text's size, sharp */
    if (size < 10) size = 10;
    if (size > 64) size = 64;
    const GfxSpace space = Gfx_Space();
    const GfxBlend blend = Gfx_Blend();
    Gfx_SetSpace(GFX_SPACE_OUTPUT);
    Gfx_SetBlend(GFX_BLEND_ALPHA);
    if (ChatFont_Prepare(size) != 0) {
        Gfx_SetBlend(blend);
        Gfx_SetSpace(space);
        return;
    }
    const float lh = (float)ChatFont_LineHeight(), pad = SDL_floorf(size * 0.3f);
    const float ax = px + (_menu_draw_x_offset + top->x) * sx + pad, ay = py + top->y * sy;
    const float aw = top->dwWidth * sx - 2 * pad, ah = (bot->y + bot->dwHeight - top->y) * sy;
    const SDL_Color white = { 255, 255, 255, 255 }, meCol = { 120, 200, 255, 255 }, themCol = { 255, 214, 90, 255 },
                    botName = { 190, 190, 190, 255 }, green = { 110, 230, 110, 255 }, yellow = { 255, 230, 80, 255 },
                    shadow = { 0, 0, 0, 200 };
    const float sh = size >= 24 ? 2.0f : 1.0f;

    /* Rows, from the newest line up, s_chatScroll lines back. */
    enum { MAX_ROWS = 24 };
    struct { const LobbyChatLine *l; int32_t first, from, to; } rows[MAX_ROWS];
    int32_t nrows = 0, maxRows = (int32_t)(ah / lh);
    if (maxRows > MAX_ROWS) maxRows = MAX_ROWS;
    for (int32_t li = NetplayLobby_ChatCount() - 1 - s_chatScroll; li >= 0 && nrows < maxRows; li--) {
        const LobbyChatLine *l = NetplayLobby_ChatLine(li);
        char who[LOBBY_NAME_MAX + 16];
        int32_t starts[8], ends[8], nw = 0, at = 0;
        const int32_t len = (int32_t)strlen(l->text);
        if (l->who == LOBBY_CHAT_BOT)
            snprintf(who, sizeof who, "Jon Hare (bot): ");
        else
            lobby_chat_name(who, sizeof who, l->who);
        const float nameW = ChatFont_Measure(who, -1);
        while (at < len && nw < 8) {                    /* wrapped at a space when it can be */
            const float room = aw - (nw == 0 ? nameW : 0.0f);
            int32_t i = at, lastSpace = -1;
            while (i < len) {
                int32_t used;
                ChatFont_NextChar(l->text + i, len - i, &used);
                if (ChatFont_Measure(l->text + at, i + used - at) > room)
                    break;
                if (l->text[i] == ' ')
                    lastSpace = i;
                i += used;
            }
            if (i < len && lastSpace > at)
                i = lastSpace;
            if (i == at) {
                int32_t used;
                ChatFont_NextChar(l->text + at, len - at, &used);
                i = at + used;
            }
            starts[nw] = at;
            ends[nw++] = i;
            at = i;
            while (at < len && l->text[at] == ' ')
                at++;
        }
        if (nw == 0) {                                   /* (an empty line still has its name) */
            starts[0] = ends[0] = 0;
            nw = 1;
        }
        for (int32_t w = nw - 1; w >= 0 && nrows < maxRows; w--) {
            rows[nrows].l = l;
            rows[nrows].first = w == 0;
            rows[nrows].from = starts[w];
            rows[nrows].to = ends[w];
            nrows++;
        }
    }
    float y = ay + ah - lh;
    for (int32_t r = 0; r < nrows; r++, y -= lh) {
        const LobbyChatLine *l = rows[r].l;
        float x = ax;
        if (rows[r].first) {
            char who[LOBBY_NAME_MAX + 16];
            if (l->who == LOBBY_CHAT_BOT)
                snprintf(who, sizeof who, "Jon Hare (bot): ");
            else
                lobby_chat_name(who, sizeof who, l->who);
            ChatFont_Draw(x + sh, y + sh, who, -1, shadow);
            ChatFont_Draw(x, y, who, -1, l->who == LOBBY_CHAT_BOT ? botName :
                                         l->who == LOBBY_CHAT_ME ? meCol : themCol);
            x += ChatFont_Measure(who, -1);
        }
        const SDL_Color col = l->who != LOBBY_CHAT_BOT ? white : strstr(l->text, "CONNECTED") ? yellow : green;
        ChatFont_Draw(x + sh, y + sh, l->text + rows[r].from, rows[r].to - rows[r].from, shadow);
        ChatFont_Draw(x, y, l->text + rows[r].from, rows[r].to - rows[r].from, col);
    }

    /* The field: what is being typed (its end, when longer than the box), and the caret. */
    if (ChatLine_IsOpen()) {
        const float fx = px + (_menu_draw_x_offset + field->x) * sx + pad, fw = field->dwWidth * sx - 2 * pad;
        const float fy = py + field->y * sy + (field->dwHeight * sy - lh) / 2;
        const char *t = ChatLine_Text();
        while (*t && ChatFont_Measure(t, -1) > fw - size * 0.5f) {
            int32_t used;
            ChatFont_NextChar(t, -1, &used);
            t += used;
        }
        ChatFont_Draw(fx + sh, fy + sh, t, -1, shadow);
        ChatFont_Draw(fx, fy, t, -1, white);
        if ((SDL_GetTicks() / 500) & 1) {
            const SDL_FRect caret = { fx + ChatFont_Measure(t, -1) + 1.0f, fy + lh * 0.1f, sh, lh * 0.8f };
            Gfx_FillRect(&caret, white);
        }
    }
    Gfx_SetBlend(blend);
    Gfx_SetSpace(space);
}
#else   /* the game's own font (chat_style.h): the rows hold the text, two to a line */

static void chat_row(int32_t row, const char *text, uint16_t color)
{
    MenuEntry *e = entry_at(MpGameSetupMenu_chatMsg00 + row);
    snprintf(s_chatRow[row], sizeof s_chatRow[row], "%s", text);
    e->fg = (uintptr_t)s_chatRow[row];
    e->stringFlags = (uint16_t)(kLeftAligned | color);
}

/* A line's colour: the bot's in yellow when someone arrives, green otherwise (SWOS 2020's colours);
 * a fork's own where it says (OnlineMenusFork.chatColor). */
static uint16_t chat_color(const LobbyChatLine *l)
{
    const int32_t fork = FORK_HOOK(chatColor) ? online_menus_fork()->chatColor(l) : -1;
    if (fork >= 0)
        return (uint16_t)fork;
    return l->who != LOBBY_CHAT_BOT ? kWhiteText2 : strstr(l->text, "CONNECTED") ? kYellowText : kGreenText;
}

/* The rows from the bottom up, newest first: each line its text and, over it, its sayer's name --
 * once for a bot line wrapped onto more (LobbyChatLine.cont). UP goes back a line at a time. */
static void chat_draw(void)
{
    const int32_t n = NetplayLobby_ChatCount();
    int32_t i, r = CHAT_ROWS;
    if (s_lobbyPane != LOBBY_PANE_CHAT)
        return;
    if (NetplayLobby_ChatSerial() != s_chatSeen) {      /* a new line: back to the newest */
        s_chatSeen = NetplayLobby_ChatSerial();
        s_chatScroll = 0;
    }
    if (s_chatScroll > n - 1)
        s_chatScroll = n - 1 > 0 ? n - 1 : 0;
    for (i = n - 1 - s_chatScroll; i >= 0 && r > 0; i--) {
        const LobbyChatLine *l = NetplayLobby_ChatLine(i);
        char name[LOBBY_NAME_MAX + 2];
        if (!l)
            break;
        chat_row(--r, l->text, chat_color(l));
        if (l->cont || r == 0)
            continue;
        if (l->who == LOBBY_CHAT_BOT)
            snprintf(name, sizeof name, "JON HARE (BOT):");
        else
            snprintf(name, sizeof name, "%s:",
                     NetplayLobby_Player(l->who == LOBBY_CHAT_ME ? NetplayLobby_LocalPlayer()
                                                                 : 3 - NetplayLobby_LocalPlayer())->name);
        chat_row(--r, name, kWhiteText2);
    }
    while (r > 0)
        chat_row(--r, "", kWhiteText2);
}

void OnlineMenus_DrawOverlay(void) {}
#endif

/* SELECT MATCH: the service's (online_account.h). */
static void selectRankedModeOption1(void)
{
    Account_SelectMatch();
}

/* FRAME DELAY: this player's extra frames, never below AUTO (netplay_lobby.h). */
static void selectGgpoFrameDelayDec(void)
{
    NetplayLobby_SetExtraDelay(NetplayLobby_Player(NetplayLobby_LocalPlayer())->extraDelay - 1);
}

static void selectGgpoFrameDelayInc(void)
{
    NetplayLobby_SetExtraDelay(NetplayLobby_Player(NetplayLobby_LocalPlayer())->extraDelay + 1);
}

static void selectGGPOFrameDelay(void)
{
    NetplayLobby_SetExtraDelay(0);
}

/* SWITCHES / CHAT (SWOS 2020's LIVE CAST URL button). */
static void selectLiveCastUrl(void)
{
    s_lobbyPane = s_lobbyPane == LOBBY_PANE_CHAT ? LOBBY_PANE_SWITCHES : LOBBY_PANE_CHAT;
}

static void matchMaking_chat_scrollUp_onSelect(void)
{
    s_chatScroll++;                     /* (chat_draw keeps it in range) */
}

static void matchMaking_chat_scrollDown_onSelect(void)
{
    if (s_chatScroll > 0)
        s_chatScroll--;
}

#if SWOS_CHAT_DYNAMIC_FONT
/* The line to type in (the chat's shared line, match_chat.h): ENTER says it, ESC drops it.
 * Typed in the chat font -- small letters, accents -- with the phone's keyboard on a phone. */
static void lobby_say(const char *utf8)
{
    (void)NetplayLobby_Say(utf8);
}

static void selectInputChatMsg(void)
{
    if (ChatLine_IsOpen())
        ChatLine_Close();
    else
        ChatLine_Open(lobby_say);
}

void OnlineMenus_LobbyChatOpen(void) { ChatLine_Open(lobby_say); }
#else
/* The line to type in: Enter says it, Esc leaves it as it was. */
static void selectInputChatMsg(void)
{
    if (InputText(sizeof s_chatInput, (uint8_t *)entry_at(MpGameSetupMenu_inputChatMsg)) == 0 &&
        NetplayLobby_Say(s_chatInput) == 0)
        s_chatInput[0] = 0;
}
#endif

static void mpGameSetupMenu_OnInit(void);

/* The lobby is the menu on screen (not one of its own screens, TEAMS, TACTICS...). */
static uint32_t s_lobbyDrawnAt;         /* SDL_GetTicks of the lobby's last own frame */

static __attribute__((unused)) int32_t lobby_on_screen(void)
{
    extern uint8_t _currentMenu[];
    const Menu *menu = (const Menu *)_currentMenu;
    /* ... and drawing itself: the kick-off's screens (the line-ups, the match) are shown with
     * the lobby still the menu, from inside its own onDraw (run_live_match). */
    return menu->onInit == (MenuVoidFn)mpGameSetupMenu_OnInit && s_lobbyDrawnAt &&
           SDL_GetTicks() - s_lobbyDrawnAt < 100;
}

static void mpGameSetupMenu_OnInit(void)
{
    Platform_SetFrameHook(lobby_frame_hook);
    s_lobbyPane  = LOBBY_PANE_CHAT;
    s_chatScroll = 0;
    lobby_layout();

    /* Start on this player's TEAMS button: the first thing to do. */
    {
        extern uint8_t _currentMenu[];
        ((Menu *)_currentMenu)->selectedEntry = entry_at(my_row(MpGameSetupMenu_teams_00));
    }
}

static void run_live_match(void);
static int32_t current_ordinal(void);

/* START: the lobby's two players ready on the same rules, and the service (if any) done. */
static int32_t lobby_can_start(void)
{
    return NetplayLobby_CanStart() && Account_LobbyCanStart();
}

static void mpGameSetupMenu_OnDraw(void)
{
    extern int _gDosGameplay;
    const LobbyPlayer *players[2];
    const LobbyRules *rules;
    LobbyState st;
    int32_t me, host, open, row;

    NetplayLobby_Poll();
    lobby_test_draw(NetplayLobby_State());
    if (getenv("SWOS_LOBBY_START") && lobby_can_start() &&
        !(getenv("SWOS_LOBBY_MATCHES") && s_lobbyMatches >= atoi(getenv("SWOS_LOBBY_MATCHES"))))
        (void)NetplayLobby_Start();
    st = NetplayLobby_State();
    if (st == LOBBY_KICKOFF) {
        run_live_match();
        return;
    }

    lobby_layout();
    if (FORK_HOOK(lobbyLayout))
        online_menus_fork()->lobbyLayout();
    chat_draw();
    s_lobbyDrawnAt = SDL_GetTicks();
    me    = NetplayLobby_LocalPlayer();
    host  = NetplayLobby_IsHost();
    open  = st == LOBBY_OPEN;
    rules = NetplayLobby_Rules();
    players[0] = NetplayLobby_Player(1);
    players[1] = NetplayLobby_Player(2);

    snprintf(s_lobbyHeader, sizeof s_lobbyHeader, "%s - LOBBY (%s)",
             rules->mode == LOBBY_MODE_RANKED ? "RANKED MATCH" : rules->mode == LOBBY_MODE_LEAGUE ? "LEAGUE / CUP"
                                                                                               : "FRIENDLY GAME",
             host ? "HOST" : "CLIENT");
    /* A service with ranked and league / cup matches (online_account.h): its lines, the other
     * player's arrival, and the MATCH row -- what this match is for, SELECT MATCH the host's. */
    Account_LobbyPoll();
    if (!s_peerTold && players[1 - (me - 1)]->known && players[1 - (me - 1)]->name[0]) {
        s_peerTold = 1;
        Account_LobbyPeer(players[1 - (me - 1)]->name);
    }
    {
        const char *matchText = Account_MatchText();
        set_visible(MpGameSetupMenu_matchInfo, matchText != NULL);
        set_visible(MpGameSetupMenu_rankedMode1Label, matchText != NULL);
        set_visible(MpGameSetupMenu_rankedModeOption1, matchText != NULL && rules->mode == LOBBY_MODE_LEAGUE);
        if (matchText) {
            set_entry(MpGameSetupMenu_matchInfo, matchText, 0, 0);
            set_entry(MpGameSetupMenu_rankedModeOption1, "SELECT MATCH", host && open,
                      host && open ? kLightBlue : kGray);
        }
    }
    set_text(MpGameSetupMenu_header, s_lobbyHeader);
    snprintf(s_lobbyStyle, sizeof s_lobbyStyle, "%s",
             FORK_HOOK(styleName) && online_menus_fork()->styleName(_gGameplayStyle)
                 ? online_menus_fork()->styleName(_gGameplayStyle)
             : _gGameplayStyle == GAMEPLAY_DOS ? "PC (70FPS)"
             : _gGameplayStyle == GAMEPLAY_XBOX ? "XBOX (60FPS)"
             : _gGameplayStyle == GAMEPLAY_AMIGA60 ? "AMIGA (60FPS)"     /* (SWOS 2020's names) */
             : _gGameplayStyle == GAMEPLAY_MOON ? "MOON SOCCER" : "AMIGA (50FPS)");
    set_text(MpGameSetupMenu_gameStyle, s_lobbyStyle);
    set_text(MpGameSetupMenu_gameStyle1, s_lobbyStyle);
    snprintf(s_lobbyConn, sizeof s_lobbyConn, "%s",
             st == LOBBY_FAILED ? "DISCONNECTED" : st == LOBBY_RECONNECTING ? "RECONNECTING" : "CONNECTED");
    set_text(MpGameSetupMenu_connectionStatus, s_lobbyConn);
    if (FORK_HOOK(lobbyStatus))
        online_menus_fork()->lobbyStatus((int32_t)st);
    if (P2PLink_PingMs() >= 0)
        snprintf(s_lobbyPing, sizeof s_lobbyPing, "%dMS", P2PLink_PingMs());
    else
        snprintf(s_lobbyPing, sizeof s_lobbyPing, "...");
    set_text(MpGameSetupMenu_latencyPing, s_lobbyPing);

    for (row = 0; row < 2; row++) {
        const LobbyPlayer *p = players[row];
        int32_t mine = (row + 1 == me);
        int32_t canEdit = mine && open;

        if (p->known)
            snprintf(s_rowName[row], sizeof s_rowName[row], "%s", p->name);
        else
            snprintf(s_rowName[row], sizeof s_rowName[row], "WAITING...");
        set_entry(MpGameSetupMenu_cPlayers_01 + row, s_rowName[row], 0, kPurple);

        if (p->hasTeam)
            snprintf(s_rowTeam[row], sizeof s_rowTeam[row], "%.17s", (char *)p->team.team.teamName);
        else
            snprintf(s_rowTeam[row], sizeof s_rowTeam[row], "%s", mine ? "SELECT TEAM" : "-");
        set_entry(MpGameSetupMenu_teams_01 + row, s_rowTeam[row], canEdit,
                  canEdit ? kLightBrownWithYellowFrame : kGray);

        set_entry(MpGameSetupMenu_tactics_01 + row,
                  tactic_name(p, s_rowTactic[row], sizeof s_rowTactic[row]),
                  canEdit && p->hasTeam, canEdit && p->hasTeam ? kLightBrownWithYellowFrame : kGray);

        set_entry(MpGameSetupMenu_go_01 + row, p->ready ? "READY" : "-",
                  canEdit && p->hasTeam, p->ready ? kGreen : (canEdit && p->hasTeam ? kRed : kGray));
        if (FORK_HOOK(lobbyRow))
            online_menus_fork()->lobbyRow(row, p, canEdit);
    }

    /* The rules: the host's to change, everyone's to read. */
    if (Account_CupLocked())                    /* (a group match: no cup, and no choosing one) */
        set_entry(MpGameSetupMenu_legOpt1, "OFF", 0, kGray);
    else
        set_entry(MpGameSetupMenu_legOpt1, legs_name(rules), host && open, host && open ? kLightBlue : kGray);
    set_entry(MpGameSetupMenu_etPen1, tie_name(rules), host && open && rules->legs > 0,
              host && open && rules->legs > 0 ? kLightBlue : kGray);
    set_entry(MpGameSetupMenu_gameLength1, length_name(rules->gameLength), host && open,
              host && open ? kLightBlue : kGray);
    set_entry(MpGameSetupMenu_pitchType1, pitch_name(rules->pitchType), host && open,
              host && open ? kLightBlue : kGray);
    snprintf(s_ruleMin, sizeof s_ruleMin, "%u", (unsigned)rules->minSubs);
    snprintf(s_ruleMax, sizeof s_ruleMax, "%u", (unsigned)rules->maxSubs);
    set_entry(MpGameSetupMenu_minSubs1, s_ruleMin, host && open && rules->maxSubs > 0,
              host && open && rules->maxSubs > 0 ? kLightBlue : kGray);
    set_entry(MpGameSetupMenu_maxSubs1, s_ruleMax, host && open, host && open ? kLightBlue : kGray);
    {
        int32_t delay;
        NetplayLobby_Peer(NULL, 0, NULL, NULL, &delay, NULL);
        if (delay > NetplayLobby_AutoDelay())
            snprintf(s_ruleDelay, sizeof s_ruleDelay, "%d+%d", NetplayLobby_AutoDelay(),
                     delay - NetplayLobby_AutoDelay());
        else
            snprintf(s_ruleDelay, sizeof s_ruleDelay, "AUTO (%d)", delay);
        set_entry(MpGameSetupMenu_ggpoFrameDelay_, s_ruleDelay, open && players[me - 1]->extraDelay > 0,
                  kLightBlue);
        set_entry(MpGameSetupMenu_ggpoFrameDelayDec, "-", open && players[me - 1]->extraDelay > 0,
                  open && players[me - 1]->extraDelay > 0 ? kLightBlue : kGray);
        set_entry(MpGameSetupMenu_ggpoFrameDelayInc, "+",
                  open && players[me - 1]->extraDelay < LOBBY_EXTRA_DELAY_MAX,
                  open && players[me - 1]->extraDelay < LOBBY_EXTRA_DELAY_MAX ? kLightBlue : kGray);
    }

    /* The host's switches: the host's to change, the joiner's to see -- both play by
     * the host's. */
    for (row = 0; row < LOBBY_SWITCH_ROWS; row++)
        set_entry(s_switchRows[row].entry, (rules->switches & s_switchRows[row].sw) ? "ON" : "OFF",
                  host && open, host && open ? kLightBlue : kGray);

    if (host) {
        int32_t can = lobby_can_start();
        set_entry(MpGameSetupMenu_start, "START", can, can ? kRed : kGray);
    } else {
        set_visible(MpGameSetupMenu_start, 0);
    }

    /* One line: what went wrong, what the other player is doing, or what to do next. */
    {
        const LobbyPlayer *other = players[2 - me];
        const LobbyPlayer *self  = players[me - 1];
        const char *line = NetplayLobby_Notice();
        if (st == LOBBY_RECONNECTING) {
            /* A match cut short says why (the header already says RECONNECTING); a
             * finished one says what is happening, or a slow reconnect looks like a
             * hang. The full-time notice shows once the lobby is open again. */
            if (!s_matchEndedEarly)
                line = "FINDING YOUR OPPONENT AGAIN...";
        } else if (!line[0]) {
            if (st == LOBBY_STARTING)
                line = "STARTING...";
            else if (other->known && other->activity == LOBBY_ACTIVITY_CHOOSING_TEAM)
                line = "YOUR OPPONENT IS CHOOSING A TEAM";
            else if (other->known && other->activity == LOBBY_ACTIVITY_ARRANGING_TEAM)
                line = "YOUR OPPONENT IS ARRANGING THEIR TEAM";
            else if (other->known && other->refused)
                line = "YOUR OPPONENT'S TEAM IS NOT A DATABASE TEAM";
            else if (self->verified == LOBBY_TEAM_REFUSED_UNKNOWN || self->verified == LOBBY_TEAM_REFUSED_CHANGED)
                line = "THE SERVER DOES NOT KNOW YOUR TEAM - CHOOSE ANOTHER";
            else if (other->verified == LOBBY_TEAM_REFUSED_UNKNOWN || other->verified == LOBBY_TEAM_REFUSED_CHANGED)
                line = "THE SERVER DOES NOT KNOW YOUR OPPONENT'S TEAM";
            else if (!self->hasTeam)
                line = "CHOOSE YOUR TEAM";
            else if (!self->ready)
                line = "PRESS GO WHEN YOU ARE READY";
            else if (!other->ready)
                line = "WAITING FOR YOUR OPPONENT";
            else
                line = host ? "BOTH READY - PRESS START" : "BOTH READY - THE HOST STARTS";
        }
        snprintf(s_lobbyActivity, sizeof s_lobbyActivity, "%s", line);
        set_text(MpGameSetupMenu_oppoActivity, s_lobbyActivity);
    }
}

/* Whether the entry just selected is on this player's row of `column00`'s column. */
static int32_t on_my_row(int32_t column00)
{
    return current_ordinal() == my_row(column00) && NetplayLobby_State() == LOBBY_OPEN;
}

/* TEAMS: the game's own team list -- clubs, national sides and custom teams -- the
 * way IMPORT TEAM uses it (edit_tactics.c). */
static void mpGameSetup_teams_onSelect(void)
{
    extern void ChooseTeamsDialog(uint16_t showClub, uint16_t showNational, uint16_t showOrigCustom,
                                  uint8_t *title, void *onSelect);
    extern uint16_t    _selectedTeamCountryNumber;
    extern TeamFileEx *_selectedTeamPtr;
    static char title[] = "CHOOSE YOUR TEAM";

    if (!on_my_row(MpGameSetupMenu_teams_00))
        return;
    /* The tie is one team's: another now would leave the first leg's injuries behind it. */
    if (NetplayLobby_SecondLeg()) {
        NetplayLobby_ChatNotice("YOUR TEAM STAYS FOR THE SECOND LEG");
        return;
    }

    /* Tell the other player first -- and take ready down, so a start cannot be
     * offered while this player is away in the list. */
    NetplayLobby_SetReady(0);
    NetplayLobby_SetActivity(LOBBY_ACTIVITY_CHOOSING_TEAM);
    NetplayLobby_Poll();

    /* A player with custom teams is offered those first (MY CUSTOM TEAM, src/
     * custom_team.c), with OTHER TEAMS for the game's own list; one without goes
     * straight to the list, as before. */
    {
        static TeamFileEx custom;
        if (CustomTeam_Pick(&custom) > 0) {
            if (lobby_pick_team(&custom))
                lineup_screen();
            NetplayLobby_SetActivity(LOBBY_ACTIVITY_NONE);
            return;
        }
    }
    ChooseTeamsDialog(1, 1, 1, (uint8_t *)title, (void *)0);

    if ((int16_t)_selectedTeamCountryNumber != -1 && _selectedTeamPtr && lobby_pick_team(_selectedTeamPtr))
        lineup_screen();                /* straight on to the line-up, then CONFIRM = ready */
    NetplayLobby_SetActivity(LOBBY_ACTIVITY_NONE);
}

/* TACTICS: the game's own team screen, the one a friendly shows before kick-off -- the
 * line-up (FIRE on two players swaps them, a starter for a substitute too) and the
 * formation, and VIEW OPPONENTS. Then the arranged team goes to the other player.
 *
 * It matters more than it looks (ReportBot 0014): SWOS scales each human player's skills
 * by how well he suits the slot he is in, down to 75% (AdjustPlayerSkills), so a team
 * played in its file's order with a misplaced defender plays him a quarter weaker at
 * everything. Offline a player fixes that on this screen; the lobby had no way to.
 *
 * The screen finds a team by its number among the cached teams (GetTeamPointer: the
 * selected teams among them), and a swap edits the copy it finds -- so the two sides go
 * where a friendly puts them, _selectedTeams[0] and [1], for as long as it is open. The
 * host's START fills those slots afresh anyway (netplay_lobby.c assemble_match). */
static struct {
    TeamFileEx keep[2];
    TeamFileEx before;                  /* this player's side as the screen opened */
    uint16_t   numSel, curNo, selNo, gameType, isCareer, training, twoPlayers;
    uint16_t   marked[2];
    uint8_t    controls;
} s_lineup;

enum { kLineupControls = 2 };           /* TEAM_PLAYER_COACH: what the lobby plays a side as */

/* The two sides where the team screen looks for them; the lobby's own state kept. */
static void lineup_begin(void)
{
    extern TeamFileEx _selectedTeams[SELECTED_TEAMS_MAX];
    extern uint16_t   _numSelectedTeams, _currentTeamNumber, _selTeamNumber;
    extern uint16_t   _gameType, _isCareer, _trainingGame, _twoPlayers;
    extern uint16_t   _team1SelectedPlayerIndex, _team2SelectedPlayerIndex;
    const LobbyPlayer *p = NetplayLobby_Player(NetplayLobby_LocalPlayer());
    const LobbyPlayer *other = NetplayLobby_Player(3 - NetplayLobby_LocalPlayer());

    /* No player is marked as the one to play as. A friendly sets these to "nobody"
     * before it opens this screen (game_init.c); here nothing had, and they start the
     * program as 0 -- the goalkeeper -- so his row carried the blinking marker
     * (ReportBot 0022). */
    s_lineup.marked[0] = _team1SelectedPlayerIndex;
    s_lineup.marked[1] = _team2SelectedPlayerIndex;
    _team1SelectedPlayerIndex = 0xFFFF;
    _team2SelectedPlayerIndex = 0xFFFF;

    /* ONE player sits at this screen, whatever the last match was. A live match sets
     * _twoPlayers (SetupPlayers) and nothing clears it before the lobby comes back; with
     * it set and one controller plugged in, input_sdl.c makes the controller PLAYER 2
     * and the keyboard player 1 -- and this screen is opened for player 1 alone
     * (controlFlags 1), so the controller did nothing on it and the keyboard did
     * (ReportBot 0020). */
    s_lineup.twoPlayers = _twoPlayers;
    _twoPlayers = 0;

    s_lineup.keep[0] = _selectedTeams[0];
    s_lineup.keep[1] = _selectedTeams[1];
    s_lineup.numSel = _numSelectedTeams; s_lineup.curNo = _currentTeamNumber;
    s_lineup.selNo = _selTeamNumber; s_lineup.gameType = _gameType;
    s_lineup.isCareer = _isCareer; s_lineup.training = _trainingGame;

    _selectedTeams[0] = p->team;
    _selectedTeams[1] = other && other->hasTeam ? other->team : p->team;
    Uniform_Clear();
    lobby_unbake_kit(&_selectedTeams[0]);   /* its own kits back; what it was sent in is a choice */
    UniformMenu_SetOnline(other && other->hasTeam, NetplayLobby_LocalPlayer() == 1);
    s_lineup.before = p->team;
    s_lineup.before.team.tactics = (uint8_t)p->tactic;
    s_lineup.controls = p->team.team.teamControls;
    _selectedTeams[0].team.teamControls = kLineupControls;   /* a CPU team cannot swap */
    _selectedTeams[1].team.teamControls = kLineupControls;
    _selectedTeams[0].team.tactics = (uint8_t)p->tactic;
    _numSelectedTeams = 2;
    _currentTeamNumber = 0xFFFF;        /* neither the edit team nor a career team */
    _selTeamNumber = 0xFFFF;
    _gameType = 0;                      /* a friendly's screen */
    _isCareer = 0;
    _trainingGame = 0;
}

/* What the team screen changed, in SWOS.log -- a match report then says whether a
 * player arranged their side before kick-off (docs/NETPLAY_GHOSTING.md: a misplaced
 * player plays at 75%). */
static void lineup_log(const TeamFileEx *before, const TeamFileEx *after)
{
    extern void *_tacticsStringTable[];
    int32_t slot, changed = 0;

    if (before->team.tactics != after->team.tactics) {
        const char *was = before->team.tactics < 12 ? (const char *)_tacticsStringTable[before->team.tactics] : "USER";
        const char *now = after->team.tactics < 12 ? (const char *)_tacticsStringTable[after->team.tactics] : "USER";
        fprintf(stderr, "[lobby] team screen: formation %s -> %s\n", was, now);
        changed = 1;
    }
    for (slot = 0; slot < 16; slot++) {
        uint8_t a = before->team.playerNumbers[slot], b = after->team.playerNumbers[slot];
        if (a == b || a >= 16 || b >= 16)
            continue;
        fprintf(stderr, "[lobby] team screen: %s %d: %.22s -> %.22s\n", slot < 11 ? "slot" : "sub",
                (int)slot + 1, (const char *)before->players[a].playerName,
                (const char *)after->players[b].playerName);
        changed = 1;
    }
    if (!changed)
        fprintf(stderr, "[lobby] team screen: opened, nothing changed\n");
}

/* Everything put back, and the arranged team -- formation with it (team.tactics) --
 * sent to the other player. */
static void lineup_end(void)
{
    extern TeamFileEx _selectedTeams[SELECTED_TEAMS_MAX];
    extern uint16_t   _numSelectedTeams, _currentTeamNumber, _selTeamNumber;
    extern uint16_t   _gameType, _isCareer, _trainingGame, _twoPlayers;
    extern uint16_t   _team1SelectedPlayerIndex, _team2SelectedPlayerIndex;
    TeamFileEx arranged = _selectedTeams[0];
    KitLook kit;

    _team1SelectedPlayerIndex = s_lineup.marked[0];
    _team2SelectedPlayerIndex = s_lineup.marked[1];

    /* A kit chosen on the UNIFORM screen: both of the team's kits become it, so SWOS's
     * own kit rule, run on each machine at kick-off, can only dress this side in it. */
    if (Uniform_Take(&_selectedTeams[0].team, &kit)) {
        KitArt_ToTeam(&kit, 0, &arranged.team);
        KitArt_ToTeam(&kit, 1, &arranged.team);
        fprintf(stderr, "[lobby] team screen: a kit was chosen (type %d, colours %d/%d/%d/%d)\n",
                kit.type, kit.stripes, kit.basic, kit.shorts, kit.socks);
    }
    UniformMenu_SetOnline(-1, 0);
    lineup_log(&s_lineup.before, &arranged);
    arranged.team.teamControls = s_lineup.controls;
    _selectedTeams[0] = s_lineup.keep[0];
    _selectedTeams[1] = s_lineup.keep[1];
    _numSelectedTeams = s_lineup.numSel; _currentTeamNumber = s_lineup.curNo;
    _selTeamNumber = s_lineup.selNo; _gameType = s_lineup.gameType;
    _isCareer = s_lineup.isCareer; _trainingGame = s_lineup.training;
    _twoPlayers = s_lineup.twoPlayers;
    NetplayLobby_SetTeam(&arranged);
}

/* The team screen, from the lobby: straight after TEAMS picks a team, and from TACTICS
 * whenever a player wants to change their mind. Its PLAY button reads CONFIRM there, and
 * CONFIRM means ready -- the player's row goes green -- as in SWOS 2020's lobby. EXIT
 * keeps what was changed but leaves them not ready. The confirmed side is the lobby's to
 * keep: a rematch starts from it (netplay_lobby.c keeps each side across a match). */
static void lineup_screen(void)
{
    extern int16_t ShowPlayMatchMenu(int16_t controlFlags, int16_t maxSubstitutes, int16_t excludeFlags,
                                     int16_t isFirstPlayer, int16_t squadChanges,
                                     TeamFileEx *team1, TeamFileEx *team2);
    extern const char *g_playMatchStartLabel;
    extern int32_t (*g_playMatchStartRefused)(const TeamFileEx *team);
    extern TeamFileEx _selectedTeams[SELECTED_TEAMS_MAX];
    extern uint16_t   _exitGameFlag;
    static char confirm[] = "CONFIRM";
    int32_t confirmed;

    NetplayLobby_SetReady(0);
    NetplayLobby_SetActivity(LOBBY_ACTIVITY_ARRANGING_TEAM);
    NetplayLobby_Poll();
    lineup_begin();
    _exitGameFlag = 0;
    g_playMatchStartLabel = confirm;
    g_playMatchStartRefused = NetplayLobby_LegBlocked;     /* (a second leg: no CONFIRM while the
                                                             * first's injured or sent off play) */
    ShowPlayMatchMenu(1, (int16_t)NetplayLobby_Rules()->maxSubs, 0, 1, 1,
                      &_selectedTeams[0], &_selectedTeams[1]);
    g_playMatchStartLabel = NULL;
    g_playMatchStartRefused = NULL;
    confirmed = _exitGameFlag == 0;     /* EXIT sets it; CONFIRM (the PLAY button) does not */
    _exitGameFlag = 0;
    lineup_end();
    NetplayLobby_SetActivity(LOBBY_ACTIVITY_NONE);
    if (confirmed)
        NetplayLobby_SetReady(1);
    fprintf(stderr, "[lobby] team screen: %s\n", confirmed ? "confirmed, ready" : "left with EXIT, not ready");
    if (confirmed)
        custom_tactics_warning();
}

/* ---- the custom-tactics warning (SWOS 2020 port, agent D) ----
 * SWOS 2020's customTacticsWarningMenu (game.cpp, after the team screen of startMultiplayTest):
 * in a RANKED match not played in the PC style, when the other player's team plays a USER
 * tactic (12 and up), the player is told so on leaving the team screen -- "only proceed if you
 * agree ... otherwise, cancel and leave lobby". Only a warning: nothing is refused. */
static void customTacticsWarningMenu_OnInit(void)
{
    set_text(CustomTacticsWarningMenu_header, _gGameplayStyle == GAMEPLAY_XBOX
             ? "WARNING FOR RANKED MATCH XBOX" : "WARNING FOR RANKED MATCH AMIGA");
}

/* The lobby is not drawn while this is up and its OnDraw is what keeps the link going. */
static void customTacticsWarningMenu_OnDraw(void)
{
    NetplayLobby_Poll();
}

static void custom_tactics_warning(void)
{
    const LobbyPlayer *other = NetplayLobby_Player(3 - NetplayLobby_LocalPlayer());
    const int32_t test = getenv("SWOS_LOBBY_TACTICS_WARNING") != NULL;   /* (a test: shown always) */
    if (!test && (NetplayLobby_Rules()->mode != LOBBY_MODE_RANKED || _gGameplayStyle == GAMEPLAY_DOS ||
                  !other || !other->hasTeam || other->tactic < TACTIC_USER_A))
        return;
    fprintf(stderr, "[lobby] the other player plays a custom tactic: the ranked match's warning\n");
    if (test && getenv("SWOS_LOBBY_TACTICS_WARNING")[0] == '1')
        return;     /* (=1: say it, show nothing -- a headless run) */
    OnlineMenus_Show(SWOS2020_MENU(customTacticsWarningMenu));
}

static void mpGameSetup_tactics_onSelect(void)
{
    const LobbyPlayer *p = NetplayLobby_Player(NetplayLobby_LocalPlayer());
    if (!on_my_row(MpGameSetupMenu_tactics_00) || !p->hasTeam)
        return;
    lineup_screen();
}

/* SWOS_LOBBY_LINEUP_SWAP=a:b -- for a test, the same arranging with one swap made by the
 * team screen's own swap (slots 0..15, as PlayMatchMenuSwapPlayers numbers them), and no
 * screen: that the arranged team reaches the match on both machines. */
static void lineup_test_swap(void)
{
    extern TeamFileEx _selectedTeams[SELECTED_TEAMS_MAX];
    extern void PlayMatchMenuSwapPlayers(TeamFileEx *team, int16_t d3, int16_t d4);
    const char *v = getenv("SWOS_LOBBY_LINEUP_SWAP");
    int a, b;
    if (!v || sscanf(v, "%d:%d", &a, &b) != 2 || !NetplayLobby_Player(NetplayLobby_LocalPlayer())->hasTeam)
        return;
    lineup_begin();
    PlayMatchMenuSwapPlayers(&_selectedTeams[0], (int16_t)a, (int16_t)b);
    lineup_end();
    fprintf(stderr, "[lobby] test: swapped line-up slots %d and %d\n", a, b);
}

/* GO: ready, or not. */
static void mpGameSetup_go_onSelect(void)
{
    const LobbyPlayer *p = NetplayLobby_Player(NetplayLobby_LocalPlayer());
    if (!on_my_row(MpGameSetupMenu_go_00))
        return;
    if (!p->ready && FORK_HOOK(readying))
        online_menus_fork()->readying();
    NetplayLobby_SetReady(!p->ready);
}

static void change_rules(void (*edit)(LobbyRules *r))
{
    LobbyRules r;
    if (!NetplayLobby_IsHost() || NetplayLobby_State() != LOBBY_OPEN)
        return;
    r = *NetplayLobby_Rules();
    edit(&r);
    NetplayLobby_SetRules(&r);
}

static void next_length(LobbyRules *r) { r->gameLength = (uint16_t)((r->gameLength + 1) % 4); }

/* RANDOM, FROZEN .. HARD, RANDOM: the friendly menu's order (FriendlyPitchTypeProc). */
static void next_pitch(LobbyRules *r)
{
    r->pitchType = r->pitchType == 0xFFFF ? 0 : r->pitchType >= 6 ? 0xFFFF : (uint16_t)(r->pitchType + 1);
}

/* The friendly menu's substitutes rules (MinSubstitutes / MaxSubstitutes, menus.c). */
static void next_min_subs(LobbyRules *r)
{
    if (r->maxSubs == 0)
        return;
    r->minSubs = (uint16_t)(r->minSubs >= r->maxSubs ? 1 : r->minSubs + 1);
}

static void next_max_subs(LobbyRules *r)
{
    r->maxSubs = (uint16_t)(r->maxSubs >= 5 ? 0 : r->maxSubs + 1);
    if (r->maxSubs == 0)
        r->minSubs = 0;
    else if (r->minSubs == 0 || r->minSubs > r->maxSubs)
        r->minSubs = 1;
}

/* OFF, 1 LEG, 2 LEGS; a cup starts with extra time and penalties. */
static void next_legs(LobbyRules *r)
{
    r->legs = (uint16_t)((r->legs + 1) % 3);
    if (r->legs && !r->tieBreak)
        r->tieBreak = LOBBY_TIE_ET_PEN;
}

static void next_tie_break(LobbyRules *r)
{
    if (r->legs)
        r->tieBreak = r->tieBreak == LOBBY_TIE_ET_PEN ? LOBBY_TIE_PEN_ONLY : LOBBY_TIE_ET_PEN;
}

static void selectLegOpt1(void)
{
    if (!Account_CupLocked())
        change_rules(next_legs);
}
static void selectEtPen1(void)      { change_rules(next_tie_break); }
static void selectGameLength1(void) { change_rules(next_length); }
static void selectPitchType1(void)  { change_rules(next_pitch); }
static void selectMinSubs1(void)    { change_rules(next_min_subs); }
static void selectMaxSubs1(void)    { change_rules(next_max_subs); }

/* The host's switches: each row flips its own. */
static void flip_short_presses(LobbyRules *r)  { r->switches ^= NETPLAY_SW_SHORT_PRESSES; }
static void flip_kickoff_sync(LobbyRules *r)   { r->switches ^= NETPLAY_SW_KICKOFF_SYNC; }
static void flip_cards_injuries(LobbyRules *r) { r->switches ^= NETPLAY_SW_CARDS_INJURIES; }

static void selectSwitchShortPresses(void)  { change_rules(flip_short_presses); }
static void selectSwitchKickoffSync(void)   { change_rules(flip_kickoff_sync); }
static void selectSwitchCardsInjuries(void) { change_rules(flip_cards_injuries); }

static void selectMPSettingStart(void)
{
    if (lobby_can_start())
        (void)NetplayLobby_Start();
}

static void selectMPSettingExit(void)
{
    Platform_SetFrameHook(NULL);
    SetExitMenuFlag();              /* OnlineMenus_EnterLobby closes the link behind us */
}

/* ======================================================================
 * HOW WAS THE MATCH? (p2p/rateMatchMenu.json) -- after every online match.
 *
 * Five stars in a row: LEFT and RIGHT move along them and every star up to the cursor
 * lights, FIRE chooses and moves on to SEND -- so a quick rating is two presses. Below
 * them an optional line about any problems, typed as the log-in screen's NAME is, then
 * SEND, and SKIP for neither. Away from the stars they show the rating chosen. The stars
 * are SWOS 2020's own (its starsMap: a 7x8 star in one colour with its lower right in a
 * second), drawn three times the size. The answer goes to SWOS.log, and with both
 * players' logs into the match report (src/match_report.c, docs/NETPLAY_REPORT.md).
 * ====================================================================== */
#define RATE_MATCH_MENU   "p2p/rateMatchMenu"
extern volatile uintptr_t _frontbufferBasePtr;
extern int      _menu_draw_x_offset;
enum {
    RateMatch_header = 0, RateMatch_against, RateMatch_result,
    RateMatch_star1, RateMatch_star2, RateMatch_star3, RateMatch_star4, RateMatch_star5,
    RateMatch_hint, RateMatch_issuesLabel, RateMatch_issues, RateMatch_send, RateMatch_skip,
    RateMatch_numEntries
};

/* SWOS 2020's full star (PixelmapAndStrings.h starsMap, rows 10..17): 'C' the star,
 * '8' its shaded lower right. */
static const char *const k_starShape[8] = {
    "...C...", "...C8..", "CCCCCCC", ".CCCCC8", "..CCC88", ".CCCCC.", ".C888C8", "..8...8",
};
enum { kStarScale = 3, kStarLit = 15, kStarLitShade = 5, kStarDim = 9, kStarDimShade = 8 };

static char    s_rateAgainst[64], s_rateResult[64];
static char    s_rateIssues[51];        /* the optional line: 50 characters */
static int32_t s_rating;                /* 1..5 chosen; 3 until one is */
static int32_t s_rateSent;              /* 1 SEND, 0 SKIP */

static void select_entry(int32_t ordinal)
{
    extern uint8_t _currentMenu[];
    ((Menu *)_currentMenu)->selectedEntry = entry_at(ordinal);
}

static void rateMatch_OnInit(void)
{
    set_text(RateMatch_against, s_rateAgainst);
    set_text(RateMatch_result, s_rateResult);
    set_text(RateMatch_hint, "LEFT AND RIGHT TO RATE, FIRE TO CHOOSE");
    set_text(RateMatch_issues, s_rateIssues);
    /* UP from the comment box goes back to the star chosen. */
    entry_at(RateMatch_issues)->upEntry = (int8_t)(RateMatch_star1 + s_rating - 1);
}

static void rateMatch_OnDraw(void)
{
    uint8_t *fb = (uint8_t *)_frontbufferBasePtr + _menu_draw_x_offset;
    int32_t on = current_ordinal() - RateMatch_star1 + 1;   /* 1..5 on a star */
    int32_t lit = (on >= 1 && on <= 5) ? on : s_rating;
    int32_t star, x, y, dy;

    rateMatch_OnInit();                 /* coming back restores the template */
    for (star = 0; star < 5; star++) {
        const MenuEntry *e = entry_at(RateMatch_star1 + star);
        /* Centred in its entry, whose frame is the cursor. */
        int32_t ox = e->x + (e->dwWidth - 7 * kStarScale) / 2;
        int32_t oy = e->y + (e->dwHeight - 8 * kStarScale) / 2;
        for (y = 0; y < 8; y++)
            for (x = 0; x < 7; x++) {
                char c = k_starShape[y][x];
                uint8_t col;
                if (c == '.')
                    continue;
                col = star < lit ? (c == 'C' ? kStarLit : kStarLitShade)
                                 : (c == 'C' ? kStarDim : kStarDimShade);
                for (dy = 0; dy < kStarScale; dy++)
                    memset(fb + (uint32_t)(oy + y * kStarScale + dy) * FRONTBUFFER_ROW_STRIDE_BYTES +
                           ox + x * kStarScale, col, kStarScale);
            }
    }
}

static void rateMatch_star_OnSelect(void)
{
    s_rating = current_ordinal() - RateMatch_star1 + 1;
    select_entry(RateMatch_send);
}

static void rateMatch_issues_OnSelect(void)
{
    InputText(sizeof s_rateIssues, (uint8_t *)entry_at(RateMatch_issues));
}

static void rateMatch_send_OnSelect(void)
{
    s_rateSent = 1;
    SetExitMenuFlag();
}

static void rateMatch_skip_OnSelect(void)
{
    s_rateSent = 0;
    SetExitMenuFlag();
}

/* After an online match (run_live_match). Not in the automated tests, which play
 * match after match with nobody at the keyboard -- unless SWOS_RATE_MATCH=1 asks for
 * it, to test the screen itself; SWOS_NO_RATING=1 skips it anywhere. Answered or
 * not, the match report goes on its way (src/match_report.c), without a rating. */
static void rate_match(const char *opponent, int32_t outcome, const char *why)
{
    extern uint16_t _statsTeam1Goals, _statsTeam2Goals;
    snprintf(s_rateAgainst, sizeof s_rateAgainst, "AGAINST %s", opponent && opponent[0] ? opponent : "YOUR OPPONENT");
    if (outcome == NETPLAY_LIVE_ENDED_EARLY)
        snprintf(s_rateResult, sizeof s_rateResult, "%s", why && why[0] ? why : "THE MATCH ENDED EARLY");
    else
        snprintf(s_rateResult, sizeof s_rateResult, "FULL TIME  %u-%u",
                 (unsigned)_statsTeam1Goals, (unsigned)_statsTeam2Goals);
    s_rating = 3;
    s_rateSent = 0;
    s_rateIssues[0] = '\0';
    if (getenv("SWOS_NO_RATING") ||
        (!getenv("SWOS_RATE_MATCH") &&
         (getenv("SWOS_LOBBY_READY") || getenv("SWOS_LIVE_FRAMES") || getenv("SWOS_MM_AUTO")))) {
        MatchReport_Submit(0, "", s_rateResult);
        return;
    }
    OnlineMenus_Show(RATE_MATCH_MENU);
    if (!s_rateSent)
        fprintf(stderr, "[netplay] rating: skipped, %s (%s)\n", s_rateAgainst, s_rateResult);
    else if (s_rateIssues[0])
        fprintf(stderr, "[netplay] rating: %d of 5 stars, %s (%s): \"%s\"\n", (int)s_rating,
                s_rateAgainst, s_rateResult, s_rateIssues);
    else
        fprintf(stderr, "[netplay] rating: %d of 5 stars, %s (%s)\n", (int)s_rating,
                s_rateAgainst, s_rateResult);
    MatchReport_Submit(s_rateSent ? s_rating : 0, s_rateSent ? s_rateIssues : "", s_rateResult);
}

/* Both sides agreed: play.
 *
 * The link is CLOSED before the session opens: GGPO wants its own socket, on the same
 * local port, which is also what keeps a NAT mapping open across the handover. The
 * lobby took the peer's address while the link was up.
 *
 * InitAndPlayGame runs the whole match and returns when it is over, exactly as it
 * does for the friendly menu and for arcade mode. */
/* Goals a side scored in extra time: the scorers list keeps each goal's minute (three digits
 * after a byte that is not one; goal_scored.c), and a shoot-out's are not in it. */
static int32_t extra_time_goals(const ScorerInfo *list)
{
    int32_t n = 0, s, g;
    for (s = 0; s < 8; s++)
        for (g = 0; g < list[s].numGoals && g < 10; g++) {
            const int8_t *t = list[s].goals[g].time;
            const int32_t minute = (t[1] - '0') * 100 + (t[2] - '0') * 10 + (t[3] - '0');
            if (minute > 90)
                n++;
        }
    return n;
}

/* A match is over: how it ended, from this player's side, to the service (online_account.h). */
static void tell_service(int32_t completed)
{
    extern uint16_t _statsTeam1Goals, _statsTeam2Goals, _team1PenaltyGoals, _team2PenaltyGoals;
    extern ScorerInfo _team1Scorers[], _team2Scorers[];
    extern int _gDosGameplay;
    const LobbyRules *rules = NetplayLobby_Rules();
    const int32_t me = NetplayLobby_LocalPlayer();
    const LobbyPlayer *other = NetplayLobby_Player(3 - me);
    int32_t goals[2][3];
    AccountResult r;

    goals[0][2] = _team1PenaltyGoals;
    goals[1][2] = _team2PenaltyGoals;
    goals[0][1] = extra_time_goals(_team1Scorers);
    goals[1][1] = extra_time_goals(_team2Scorers);
    goals[0][0] = (int32_t)_statsTeam1Goals - goals[0][2] - goals[0][1];
    goals[1][0] = (int32_t)_statsTeam2Goals - goals[1][2] - goals[1][1];
    memset(&r, 0, sizeof r);
    r.mode      = rules->mode;
    r.matchId   = rules->matchId;
    r.legId     = rules->legId;
    r.host      = me == 1;
    r.completed = completed;
    r.dosStyle  = _gDosGameplay != 0;
    memcpy(r.mine, goals[me - 1], sizeof r.mine);       /* (player 1 is the top team, team 1) */
    memcpy(r.theirs, goals[2 - me], sizeof r.theirs);
    snprintf(r.opponent, sizeof r.opponent, "%s", other ? other->name : "");
    Account_MatchPlayed(&r);
}

static void run_live_match(void)
{
    extern TeamFileEx  _selectedTeams[SELECTED_TEAMS_MAX];
    extern void        InitAndPlayGame(TeamFileEx *team1, TeamFileEx *team2);

    char ip[24];
    uint16_t peerPort, localPort;
    int32_t delay, simWidth;

    Platform_SetFrameHook(NULL);
    MatchReport_Poll();
    MatchPrediction_Poll();
    MatchReport_Finish();               /* the last match's, with what there is */
    NetplayLobby_Peer(ip, sizeof ip, &peerPort, &localPort, &delay, &simWidth);
    NetplayLobby_End();
    P2PLink_Close();

    if (!ip[0] || NetplayLobby_ApplyMatch() != 0 ||
        NetplayMatch_StartLive(NetplayLobby_LocalPlayer(), ip, peerPort, localPort, delay,
                               simWidth) != 0) {
        snprintf(s_matchStatus, sizeof s_matchStatus, "COULD NOT START THE MATCH");
        NetplaySetup_ArmTieRules(0);
        NetplayLobby_RestoreTactics();
        SetExitMenuFlag();
        return;
    }

    snprintf(s_matchStatus, sizeof s_matchStatus, "PLAYING");
    _gFrameStepInput = 1;               /* one frame of input at a time (game_loop.h) */
    if (FORK_HOOK(liveMatchStart))
        online_menus_fork()->liveMatchStart();
    InitAndPlayGame(&_selectedTeams[0], &_selectedTeams[1]);
    s_lobbyMatches++;

    /* Back to the lobby with the same player: a rematch is two GOs and a START, as it
     * is in SWOS 2020. The menu is shown afresh (OnlineMenus_EnterLobby) rather than
     * carried on with, since the match has had the screen. */
    {
        extern uint16_t _statsTeam1Goals, _statsTeam2Goals;
        const char *why;
        int32_t outcome = NetplayMatch_LiveOutcome(&why);
        const LobbyRules *rules = NetplayLobby_Rules();
        char notice[64];
        s_matchEndedEarly = outcome == NETPLAY_LIVE_ENDED_EARLY;
        if (s_matchEndedEarly && why[0])
            snprintf(notice, sizeof notice, "%s", why);
        else if (!s_matchEndedEarly && rules->legs == 2 && rules->leg == 1)
            snprintf(notice, sizeof notice, "FIRST LEG %u-%u - GO FOR THE SECOND LEG",
                     (unsigned)_statsTeam1Goals, (unsigned)_statsTeam2Goals);
        else
            snprintf(notice, sizeof notice, "FULL TIME - PRESS GO FOR A REMATCH");
        if (FORK_HOOK(liveMatchEnd))
            online_menus_fork()->liveMatchEnd(!s_matchEndedEarly, why);
        tell_service(!s_matchEndedEarly);       /* (before the rules move on: the leg it was) */
        /* A two-leg tie moves on (the host's rules; the joiner hears of it). */
        NetplayLobby_MatchPlayed(!s_matchEndedEarly, _statsTeam1Goals, _statsTeam2Goals);
        NetplayMatch_Finish();
        (void)NetplayLobby_Rematch(notice);     /* on failure the lobby says why */
        /* HOW WAS THE MATCH? -- with the link to the other player kept polled, so a
         * player can take their time without the rematch connection timing out. */
        Platform_SetFrameHook(lobby_frame_hook);
        {
            const LobbyPlayer *other = NetplayLobby_Player(3 - NetplayLobby_LocalPlayer());
            rate_match(other ? other->name : NULL, outcome, why);
        }
    }
    s_testReady = getenv("SWOS_LOBBY_READY") != NULL;
    s_lobbyAgain = 1;
    SetExitMenuFlag();
}

/* OPEN: which style (hostGameMessageMenu), then advertise a game and wait. The game is
 * played in the HOST's style: whoever joins takes it for that game (p2p_link.c). */
static void matchMaking_openGame_onSelect(void)
{
    if (entry_at(MatchMakingMenu_openGame)->bgColor == kGray)
        return;                         /* (grey is not now: SWOS 2020's) */
    /* A service with ranked and league / cup matches asks what this one is for first. */
    s_openMode = Account_Has(ACCOUNT_HAS_RANKED) ? Account_ChooseMode() : LOBBY_MODE_FRIENDLY;
    if (s_openMode < 0) {
        s_openMode = LOBBY_MODE_FRIENDLY;
        return;
    }
    /* A match picked with its own style (a league or cup match's ranking): no question. */
    if (Account_Has(ACCOUNT_HAS_RANKED) && Account_ChosenStyle() >= 0) {
        extern void GameplayStyle_Set(int dos, int persist);
        GameplayStyle_Set(Account_ChosenStyle(), 1);
        fprintf(stderr, "[mm] hosting a %s game (the match's)\n", Account_ChosenStyle() ? "DOS" : "AMIGA");
        MmClient_CreateGame();
        return;
    }
    s_askStyle = 1;
    OnlineMenus_Show(SWOS2020_MENU(hostGameMessageMenu));
    s_askStyle = 0;                 /* EXIT, or the answer (which cleared it already) */
}

/* LEAVE: stop advertising, or withdraw a join request. */
static void matchMaking_cancelGame_onSelect(void)
{
    if (entry_at(MatchMakingMenu_cancelGame)->bgColor == kGray)
        return;
    MmClient_LeaveGame();
}

/* A host row: ask to join that game. Which row was clicked is recovered from the
 * menu's selected entry -- CalcMenuEntryAddress is ordinal -> address, so this
 * is the inverse. */
static int32_t current_ordinal(void)
{
    /* The live menu's backing buffer. Declared as a byte array to match its real
     * storage in swos_globals.c (`currentMenu: ds.b $16` + the adjacent
     * menuEntries) -- the same spelling animated_patterns.c and
     * highlights_menu.c use. */
    extern uint8_t _currentMenu[];
    const Menu *menu = (const Menu *)_currentMenu;
    const uint8_t *base = _currentMenu + MENU_HEADER_SIZE;
    const uint8_t *sel  = (const uint8_t *)menu->selectedEntry;
    if (!sel || sel < base)
        return -1;
    return (int32_t)((size_t)(sel - base) / MENUENTRY_STRIDE);
}

static void matchMaking_host_onSelect(void)
{
    int32_t ord = current_ordinal();
    int32_t row = ord - MatchMakingMenu_host_00;
    int32_t numGames = 0;
    const MmGame *games = MmClient_OpenGames(&numGames);

    if (row < 0 || row >= MatchMakingMenu_kGS_MaxRows)
        return;
    if (s_rowGame[row] < 0 || s_rowGame[row] >= numGames)
        return;
    MmClient_JoinGame(games[s_rowGame[row]].owner);
}

/* A user row. Upstream opens playerInfoMenu here: the account service's to show
 * (Account_UserSelected; OpenSWOS's has nothing to show yet). */
static void matchMaking_users_onSelect(void)
{
    const int32_t row = current_ordinal() - MatchMakingMenu_users_00;
    if (row >= 0 && row < MatchMakingMenu_kGS_MaxRows2 && s_userRow[row][0])
        Account_UserSelected(s_userRow[row]);
}

/* EXIT: leave the lobby properly rather than abandoning a socket -- otherwise
 * the server keeps advertising a game nobody is behind until the ENET timeout. */
static void matchMaking_exit_onSelect(void)
{
    extern void GameplayStyle_GiveBack(void);
    GameplayStyle_GiveBack();       /* a game joined and left before its lobby */
    MmClient_Disconnect();
    SetExitMenuFlag();
}

/* MY CUSTOM TEAM: the player's three teams for online play (src/custom_team.c). */
static void matchMaking_menuCustomTeam_onSelect(void)
{
    CustomTeam_Manage();
}

static void matchMaking_menuSettings_onSelect(void)
{
    OnlineMenus_Show(SWOS2020_MENU(mmSettingsMenu));
}

/* ======================================================================
 * mmSettingsMenu -- MULTIPLAYER SETTINGS
 *
 * SWOS 2020's rows here (pitch set, sound, commentary, the chat options) set
 * things OpenSWOS does not have, and none of them was ever bound -- the
 * loader maps a handler nobody lists to a no-op. The first two pairs are taken
 * over for multiplayer settings a player would otherwise edit SWOS.ini for:
 * streaming each match to a recorder (docs/NETPLAY.md 7 "The recorder",
 * docs/NETPLAY_SETUP.md 7), and sending match reports (docs/NETPLAY_REPORT.md).
 * Same move as logInMenu_OnInit turning the password row into SERVER.
 * ====================================================================== */

/* What ON means. "server" -- the matchmaker the player logged in to -- is what
 * nearly everyone wants. But if SWOS.ini already names a recorder of its own
 * (host[:port]), THAT is what ON restores, so switching the row off and back on
 * never throws away an address someone typed by hand. */
static char s_recordOn[64];
static char s_recordShown[64];   /* set_text keeps the pointer, so it must outlive the call */

static const char *record_current(void)
{
    extern PlatformInitOptions *g_arcadeOpts;
    return g_arcadeOpts ? g_arcadeOpts->record : "";
}

/* The same test match_record.c's resolve_target() makes: "", "off" and
 * "false" are off, anything else names a recorder. */
static int32_t record_is_on(const char *spec)
{
    return spec && spec[0] &&
           SDL_strcasecmp(spec, "off") != 0 && SDL_strcasecmp(spec, "false") != 0;
}

/* ---- SWOS 2020's MULTIPLAYER SETTINGS chat and match rows (SWOS 2020 port, agent A) ----
 * COMMENTARY ([audio] commentary), AUTO CLOSE CHATBOX ([chat] close_after), ESC PRESSING TO QUIT
 * ([netplay] esc_presses), DISABLE CONTROLLER IN CHAT ([chat] controller_off), CHAT WINDOW SCALE
 * ([chat] scale): each this player's own -- the other game is told nothing. SWOS 2020's VIDEO
 * VSYNC row is commented out in its own menu (vsync is on SETTINGS -> VIDEO); DLC SETTING and
 * PITCH SET are its DLC's (those two rows are RECORD MATCHES and SEND MATCH REPORTS here). */
extern void    InputSDL_SetChatPadOff(int on);
extern int     InputSDL_ChatPadOff(void);
extern void    InputSDL_SetEscPresses(int n);
extern int     InputSDL_EscPresses(void);
extern void    DosSound_SetCommentary(int32_t on);
static char s_mmsChatbox[24], s_mmsEsc[16], s_mmsScale[16];

static void mms_save(void)
{
    extern PlatformInitOptions *g_arcadeOpts;
    extern const char *g_arcadeIniPath;
    if (g_arcadeOpts && g_arcadeIniPath)
        Settings_Save(g_arcadeOpts, g_arcadeIniPath);
}

static void mms_chat_rows_draw(void)
{
    extern PlatformInitOptions *g_arcadeOpts;
    static const char *const k_comm[3] = { "OFF", "ON", "AUTO" };
    const int32_t comm = g_arcadeOpts ? g_arcadeOpts->commentary : 2;
    const int32_t secs = MatchChat_AutoClose(), esc = InputSDL_EscPresses(), sc = MatchChat_Scale();
    set_text(MmSettingsMenu_commentary, k_comm[comm >= 0 && comm <= 2 ? comm : 2]);
    snprintf(s_mmsChatbox, sizeof s_mmsChatbox, "AFTER %d SECOND%s", (int)secs, secs > 1 ? "S" : "");
    set_text(MmSettingsMenu_chatboxTimeout, s_mmsChatbox);
    snprintf(s_mmsEsc, sizeof s_mmsEsc, "%d TIME%s", (int)esc, esc > 1 ? "S" : "");
    set_text(MmSettingsMenu_escPressingMax, s_mmsEsc);
    set_text(MmSettingsMenu_disableControllerInChatbox, InputSDL_ChatPadOff() ? "ON" : "OFF");
    snprintf(s_mmsScale, sizeof s_mmsScale, "%d%sX", (int)(1 + sc / 2), sc % 2 ? ".5" : "");   /* (0..8: 1x .. 5x) */
    set_text(MmSettingsMenu_chatWindowScale, s_mmsScale);
}

static void mmsSelectCommentary(void)
{
    extern PlatformInitOptions *g_arcadeOpts;
    if (!g_arcadeOpts)
        return;
    g_arcadeOpts->commentary = (g_arcadeOpts->commentary + 1) % 3;     /* OFF -> ON -> AUTO */
    DosSound_SetCommentary(g_arcadeOpts->commentary);
    mms_save();
}

static void mmsSelectChatboxTimeout(void)       /* (SWOS 2020 counts down: 5, 4 ... 1, 5) */
{
    extern PlatformInitOptions *g_arcadeOpts;
    int32_t secs = MatchChat_AutoClose() - 1;
    MatchChat_SetAutoClose(secs < 1 ? 5 : secs);
    if (g_arcadeOpts)
        g_arcadeOpts->chat_auto_close = MatchChat_AutoClose();
    mms_save();
}

static void mmsSelectEscPressingMax(void)
{
    extern PlatformInitOptions *g_arcadeOpts;
    InputSDL_SetEscPresses(InputSDL_EscPresses() % 3 + 1);
    if (g_arcadeOpts)
        g_arcadeOpts->esc_presses = InputSDL_EscPresses();
    mms_save();
}

static void mmsDisableControllerInChatbox(void)
{
    extern PlatformInitOptions *g_arcadeOpts;
    InputSDL_SetChatPadOff(!InputSDL_ChatPadOff());
    if (g_arcadeOpts)
        g_arcadeOpts->chat_controller_off = InputSDL_ChatPadOff();
    mms_save();
}

static void mmsChangeChatWindowScale(void)
{
    extern PlatformInitOptions *g_arcadeOpts;
    int32_t sc = MatchChat_Scale() + 1;
    MatchChat_SetScale(sc > MATCHCHAT_SCALE_MAX ? 0 : sc);
    if (g_arcadeOpts)
        g_arcadeOpts->chat_scale = MatchChat_Scale();
    mms_save();
}

static void mmsOnInit(void)
{
    const char *cur = record_current();
    if (!s_recordOn[0])
        snprintf(s_recordOn, sizeof s_recordOn, "%s", record_is_on(cur) ? cur : "server");
}

/* Every frame, so the row just follows the setting: ON for the matchmaker, the
 * address itself for a recorder of the player's own so it is not hidden. */
static void mmsOnDraw(void)
{
    const char *cur = record_current();
    /* The label on every draw, not once: a menu is restored from its template
     * when one opened from it closes (see logInMenu_layout). */
    set_text(MmSettingsMenu_pitchSetLabel, "RECORD MATCHES");
    if (!record_is_on(cur)) {
        set_text(MmSettingsMenu_pitchSet, "OFF");
    } else if (SDL_strcasecmp(cur, "server") == 0) {
        set_text(MmSettingsMenu_pitchSet, "ON");
    } else {
        char *c;
        snprintf(s_recordShown, sizeof s_recordShown, "%s", cur);
        for (c = s_recordShown; *c; c++)          /* menu text is upper case */
            if (*c >= 'a' && *c <= 'z')
                *c = (char)(*c - 'a' + 'A');
        set_text(MmSettingsMenu_pitchSet, s_recordShown);
    }
    /* After an online match, both players' logs and ratings, zipped, to the server
     * (include/match_report.h). */
    set_text(MmSettingsMenu_soundFxLabel, "SEND MATCH REPORTS");
    set_text(MmSettingsMenu_soundFx, MatchReport_Enabled() ? "ON" : "OFF");
    mms_chat_rows_draw();
    if (FORK_HOOK(settingsDraw))
        online_menus_fork()->settingsDraw();
    /* SWOS 2020's other rows -- ACCEPT SOUND, JOIN AMIGA SUPER LEAGUE, JOIN DISCORD, HELP DESK --
     * are a fork's (United's), not this project's: hidden (the cursor passes over a hidden entry
     * to the next one its way), CHAT WINDOW SCALE moved up into ACCEPT SOUND's place and MY CUSTOM
     * TEAM under it. A fork that builds them keeps them (PROJECT_HIDE_SWOS2020_EXTRAS). */
    if (PROJECT_HIDE_SWOS2020_EXTRAS) {
        set_visible(MmSettingsMenu_acceptSoundLabel, 0);
        set_visible(MmSettingsMenu_acceptSound, 0);
        entry_at(MmSettingsMenu_chatWindowScaleLabel)->y = entry_at(MmSettingsMenu_acceptSoundLabel)->y;
        entry_at(MmSettingsMenu_chatWindowScale)->y = entry_at(MmSettingsMenu_acceptSound)->y;
        set_visible(MmSettingsMenu_joinASL, 0);
        set_visible(MmSettingsMenu_joinDiscord, 0);
        set_visible(MmSettingsMenu_helpDesk, 0);
        entry_at(MmSettingsMenu_createOwnCustom)->y = (uint16_t)(entry_at(MmSettingsMenu_chatWindowScale)->y + 12);
    }
}

/* MY CUSTOM TEAM: the slots, as MATCH MAKING's button opens them. */
static void mmsSelectCreateOwnCustom(void)
{
    CustomTeam_Manage();
}

/* Toggle and keep it, both places it lives: SWOS.ini, the way the SERVER row
 * writes it, and the live value match_record.c reads as each match starts --
 * so it applies to the very next match with no restart. */
static void mmsSelectPitchSet(void)
{
    extern PlatformInitOptions *g_arcadeOpts;
    extern const char *g_arcadeIniPath;

    if (!g_arcadeOpts)
        return;
    if (!s_recordOn[0])
        snprintf(s_recordOn, sizeof s_recordOn, "server");

    /* "off", not "": an empty record= is the default, which records (main.c). */
    if (record_is_on(g_arcadeOpts->record))
        snprintf(g_arcadeOpts->record, sizeof g_arcadeOpts->record, "off");
    else
        snprintf(g_arcadeOpts->record, sizeof g_arcadeOpts->record, "%s", s_recordOn);

    MatchRecord_SetConfigured(g_arcadeOpts->record);
    if (g_arcadeIniPath)
        Settings_Save(g_arcadeOpts, g_arcadeIniPath);
}

/* SEND MATCH REPORTS, on the SOUND FX row: the same two places as RECORD MATCHES. */
static void mmsSelectSoundFx(void)
{
    extern PlatformInitOptions *g_arcadeOpts;
    extern const char *g_arcadeIniPath;

    if (!g_arcadeOpts)
        return;
    g_arcadeOpts->reports = !g_arcadeOpts->reports;
    MatchReport_SetEnabled(g_arcadeOpts->reports);
    if (g_arcadeIniPath)
        Settings_Save(g_arcadeOpts, g_arcadeIniPath);
}

static void matchMaking_rankings_onSelect(void)
{
    OnlineMenus_Show(SWOS2020_MENU(rankingsMenu));
}

/* ======================================================================
 * Handler table. Unlisted names are bound to a no-op by the loader.
 * ====================================================================== */
#define HANDLER(fn)            { #fn, (void *)fn, SYM_FUNC }
#define HANDLER_AS(name, fn)   { name, (void *)fn, SYM_FUNC }

static const MenuSymbol k_handlers[] = {
    /* ~SetExitMenuFlag in the .mnu files is the SWOS routine of the same name. */
    HANDLER(SetExitMenuFlag),

    HANDLER(rateMatch_OnInit),
    HANDLER(rateMatch_OnDraw),
    HANDLER(rateMatch_star_OnSelect),
    HANDLER(rateMatch_issues_OnSelect),
    HANDLER(rateMatch_send_OnSelect),
    HANDLER(rateMatch_skip_OnSelect),

    HANDLER(logInMenu_OnInit),
    HANDLER(logInMenu_OnDraw),
    HANDLER(usernameInput_OnSelect),
    /* The .mnu calls it the password input; it is the SERVER address here. */
    HANDLER(passwordInput_OnSelect),
    HANDLER(showPassword_OnSelect),
    HANDLER(registerUserMenuNews_OnSelect),
    HANDLER(registerUserMenuNews2_OnSelect),
    HANDLER(registerUserMenuNews3_OnSelect),
    HANDLER(rememberLogInInfo_OnSelect),
    /* SHOW PASSWORD has nothing left to show -- the row is hidden in OnInit. */
    HANDLER_AS("showPassword_OnSelect", SetExitMenuFlag),
    HANDLER(liveCast_OnSelect),
    HANDLER(logInMenuLogInContinue_OnSelect),
    HANDLER(logInMenuNews_OnSelect),

    HANDLER(matchMakingMenu_OnInit),
    HANDLER(matchMakingMenu_OnDraw),
    HANDLER(matchMaking_menuSettings_onSelect),
    HANDLER(matchMaking_menuCustomTeam_onSelect),
    HANDLER(mmsSelectCreateOwnCustom),
    /* MULTIPLAYER SETTINGS: its first row is RECORD MATCHES. */
    HANDLER(mmsOnInit),
    HANDLER(mmsOnDraw),
    HANDLER(mmsSelectPitchSet),
    HANDLER(mmsSelectSoundFx),
    HANDLER(mmsSelectCommentary),
    HANDLER(mmsSelectChatboxTimeout),
    HANDLER(mmsSelectEscPressingMax),
    HANDLER(mmsDisableControllerInChatbox),
    HANDLER(mmsChangeChatWindowScale),
    HANDLER(matchMaking_rankings_onSelect),
    HANDLER(matchMaking_openGame_onSelect),
    HANDLER(matchMaking_cancelGame_onSelect),
    HANDLER(matchMaking_host_onSelect),
    HANDLER(matchMaking_users_onSelect),
    HANDLER(matchMaking_exit_onSelect),

    HANDLER(hostGameMessageMenu_OnInit),
    HANDLER(hostGameMessageMenu_OnDraw),
    HANDLER(hostGameMessageMenu_button1_OnSelect),
    HANDLER(hostGameMessageMenu_button2_OnSelect),
    HANDLER(hostGameMessageMenu_button3_OnSelect),

    HANDLER(mpWaitingMenu_OnDraw),
    HANDLER(mpWaiting_menuExit_OnSelect),

    HANDLER(mpGameSetupMenu_OnInit),
    HANDLER(mpGameSetupMenu_OnDraw),
    HANDLER(mpGameSetup_teams_onSelect),
    HANDLER(mpGameSetup_tactics_onSelect),
    HANDLER(customTacticsWarningMenu_OnInit),
    HANDLER(customTacticsWarningMenu_OnDraw),
    HANDLER(mpGameSetup_go_onSelect),
    HANDLER(selectLegOpt1),
    HANDLER(selectEtPen1),
    HANDLER(selectGameLength1),
    HANDLER(selectPitchType1),
    HANDLER(selectMinSubs1),
    HANDLER(selectMaxSubs1),
    HANDLER(selectLiveCastUrl),
    HANDLER(selectRankedModeOption1),
    HANDLER(selectGgpoFrameDelayDec),
    HANDLER(selectGgpoFrameDelayInc),
    HANDLER(selectGGPOFrameDelay),
    HANDLER(matchMaking_chat_scrollUp_onSelect),
    HANDLER(matchMaking_chat_scrollDown_onSelect),
    HANDLER(selectInputChatMsg),
    HANDLER(selectSwitchShortPresses),
    HANDLER(selectSwitchKickoffSync),
    HANDLER(selectSwitchCardsInjuries),
    HANDLER(selectMPSettingStart),
    HANDLER(selectMPSettingExit),

    /* Exit and cancel buttons. Offline there is nothing to cancel or leave on a
     * server, so all of them just close their menu. */
    HANDLER_AS("cupMatchView_exit_onSelect",         SetExitMenuFlag),
    HANDLER_AS("exitNewsMenu_OnSelect",              SetExitMenuFlag),
    HANDLER_AS("exitRankingMenu_OnSelect",           SetExitMenuFlag),
    HANDLER_AS("hostGameMessageMenu_exit_OnSelect",  SetExitMenuFlag),
    HANDLER_AS("leagueInfo1_exit_onSelect",          SetExitMenuFlag),
    HANDLER_AS("leagueInfo2_exit_onSelect",          SetExitMenuFlag),
    HANDLER_AS("leagueInfoMain_exit_onSelect",       SetExitMenuFlag),
    HANDLER_AS("leagueInfoView_exit_onSelect",       SetExitMenuFlag),
    HANDLER_AS("logInMenuCancel_OnSelect",           SetExitMenuFlag),
    HANDLER_AS("logInMenuOfflinelogOut_OnSelect",    logInMenuAbout_OnSelect),
    HANDLER_AS("mLogReaderMenu_exit_onSelect",       SetExitMenuFlag),
    HANDLER_AS("mLogStatsMenu_exit_onSelect",        SetExitMenuFlag),
    HANDLER_AS("mLogTickerMenu_exit_onSelect",       SetExitMenuFlag),
    HANDLER_AS("matchView_exit_onSelect",            SetExitMenuFlag),
    HANDLER_AS("messageMenu_no_OnSelect",            SetExitMenuFlag),
    HANDLER_AS("messageMenu_yes_OnSelect",           SetExitMenuFlag),
    HANDLER_AS("myFriendlyStatsMenu_exit_OnSelect",  SetExitMenuFlag),
    HANDLER_AS("myRankedStatsMenu_exit_OnSelect",    SetExitMenuFlag),
    HANDLER_AS("selectExitOpenLobby",                SetExitMenuFlag),
};

#undef HANDLER
#undef HANDLER_AS

static const void *find_handler(const char *name)
{
    for (size_t i = 0; i < sizeof k_handlers / sizeof k_handlers[0]; i++)
        if (strcmp(k_handlers[i].name, name) == 0)
            return k_handlers[i].ptr;
    return NULL;
}

void OnlineMenus_Init(void)
{
    static int32_t registered;
    if (registered)
        return;
    registered = 1;
    MenuJson_RegisterSymbols(k_handlers, (int)(sizeof k_handlers / sizeof k_handlers[0]));
    if (FORK_HOOK(init))
        online_menus_fork()->init();
    Account_Init();             /* (a fork's own screens: online_account.h) */
}

/* ======================================================================
 * --menu=<name>, --matchmaker-menu[=name]
 * ====================================================================== */
static char s_startMenu[64];
static char s_autoLogin[MM_NAME_MAX];

void OnlineMenus_SetAutoLogin(const char *name)
{
    snprintf(s_autoLogin, sizeof s_autoLogin, "%s", (name && name[0]) ? name : "PLAYER");
    snprintf(s_username, sizeof s_username, "%s", s_autoLogin);
    OnlineMenus_SetStartMenu("swos2020/matchMakingMenu");
}

static int32_t s_quitAfterStart;

void OnlineMenus_SetStartMenu(const char *name)
{
    snprintf(s_startMenu, sizeof s_startMenu, "%s", name);
    s_quitAfterStart = 0;               /* (another start menu: the main menu after it) */
}

/* --online, and the online client (make ONLINE_CLIENT=1): the multiplayer log-in first --
 * with quitAfter, leaving it quits the game. */

void OnlineMenus_StartOnline(int32_t quitAfter)
{
    OnlineMenus_SetStartMenu("swos2020/logInMenu");
    s_quitAfterStart = quitAfter;
}

int32_t OnlineMenus_QuitAfterStartMenu(void)
{
    return s_quitAfterStart;
}

/* --netplay-lobby=host|join: two instances on one machine meet over loopback and open
 * the lobby, the way a matchmaking handoff would -- how the lobby menu gets tried
 * without a server or a second computer. */
static int32_t s_loopbackLobby;     /* 0 off, 1 host, 2 join */

int32_t OnlineMenus_SetLoopbackLobby(const char *role)
{
    if (role && strcmp(role, "host") == 0)      s_loopbackLobby = 1;
    else if (role && strcmp(role, "join") == 0) s_loopbackLobby = 2;
    else                                        return 1;
    return 0;
}

int32_t OnlineMenus_ShowStartMenu(void)
{
    if (s_loopbackLobby) {
        enum { kHostPort = 23921, kJoinPort = 23922 };
        char peer[24];
        const char *list[1] = { peer };
        int32_t player = s_loopbackLobby;

        s_loopbackLobby = 0;        /* once, like --menu */
        OnlineMenus_Init();
        snprintf(peer, sizeof peer, "127.0.0.1:%d", player == 1 ? kJoinPort : kHostPort);
        s_matchPlayer = player;
        snprintf(s_myName, sizeof s_myName, "%s", player == 1 ? "HOST" : "JOIN");
        snprintf(s_matchStatus, sizeof s_matchStatus, "CONNECTING OVER LOOPBACK");
        P2PLink_SetPlayer(player);      /* as the matchmaker's games are: whose style is played */
        if (P2PLink_Punch(list, 1, (uint16_t)(player == 1 ? kHostPort : kJoinPort)) != 0)
            return 0;
        OnlineMenus_Show(SWOS2020_MENU(mpWaitingMenu));
        return 1;
    }
    if (!s_startMenu[0])
        return 0;
    char name[sizeof s_startMenu];
    memcpy(name, s_startMenu, sizeof name);
    s_startMenu[0] = '\0';      /* once: the front end can be re-entered after a match */
    OnlineMenus_Init();
    /* --matchmaker-menu: log in before the menu opens, so it comes up live.
     * Driving the lobby under gdb does not work -- a process stopped at a
     * breakpoint sends no ENET keepalives and the server times it out -- so this
     * is how the real menu gets looked at. */
    if (s_autoLogin[0]) {
        const char *au = getenv("SWOS_MM_AUTO");
        s_mmAuto = (au && !strcmp(au, "host")) ? MM_AUTO_HOST :
                   (au && !strcmp(au, "join")) ? MM_AUTO_JOIN : MM_AUTO_OFF;
        MmClient_Connect(mm_host(), mm_port(), s_autoLogin, NULL);
        s_autoLogin[0] = '\0';
    }
    P2PMenu_Init();             /* --menu=p2p/p2pMenu wants its handlers too */
    ControlsMenu_Init();
    { extern void SettingsMenus_Init(void); SettingsMenus_Init(); }   /* (--menu=openswos/settingsMenu) */
    CreditsMenu_Init();              /* (--menu=openswos/creditsMenu, openswos/fullCreditsMenu) */
    ModsMenu_Init();            /* (--menu=openswos/modsMenu) */
    { extern void ModEntries_Init(void); ModEntries_Init(); }   /* (--menu=openswos/modEntriesMenu) */
    { extern void TeamPacksMenu_Init(void); TeamPacksMenu_Init(); }   /* (--menu=openswos/teamPacksMenu) */
    if (!strcmp(name, CREDITS_MENU_NAME))
        CreditsMenu_Show();         /* with its photos' palette, as the button opens it */
    else if (!strcmp(name, SWOS2020_MENU(selectMyCustomTeams)))
        CustomTeam_Manage();        /* the slots AND what choosing one does, as its button */
    else if (!strncmp(name, CHOICE_MENU_NAME ":", sizeof CHOICE_MENU_NAME)) {   /* (<slug>:<key>) */
        char slug[64];
        const char *rest = name + sizeof CHOICE_MENU_NAME, *colon = strchr(rest, ':');
        snprintf(slug, sizeof slug, "%.*s", colon ? (int)(colon - rest) : 0, rest);
        if (!colon || !ModsMenu_ShowChoices(slug, colon + 1))
            OnlineMenus_Show(MODS_MENU_NAME);
    }
    else
        OnlineMenus_Show(name);
    return 1;
}

/* ======================================================================
 * --menu-selftest
 * ====================================================================== */
int32_t OnlineMenus_SelfTest(void)
{
    OnlineMenus_Init();
    printf("[online-menus] self-test: the JSON-only menus\n");

    int32_t menus = 0, entries = 0, failed = 0;
#define VERIFY_MENU(menu, key, prefix, count)                                        \
    do {                                                                             \
        int bad = MenuJson_VerifyDynamic(key, count);                                \
        menus++;                                                                     \
        entries += count;                                                            \
        if (bad) {                                                                   \
            failed++;                                                                \
            printf("  FAIL %-26s %s\n", #menu, bad < 0 ? "did not load" : "mismatch (see log)"); \
        }                                                                            \
    } while (0);
    SWOS2020_MENUS(VERIFY_MENU)
#undef VERIFY_MENU

    /* OpenSWOS's own, checked the same way (src/p2p_menu.c, and HOW WAS THE MATCH?). */
    {
        int bad = MenuJson_VerifyDynamic(RATE_MATCH_MENU, RateMatch_numEntries);
        menus++;
        entries += RateMatch_numEntries;
        if (bad) {
            failed++;
            printf("  FAIL %-26s %s\n", "rateMatchMenu", bad < 0 ? "did not load" : "mismatch (see log)");
        }
    }
    P2PMenu_Init();
    {
        int bad = MenuJson_VerifyDynamic(P2P_MENU_NAME, P2P_MENU_ENTRIES);
        menus++;
        entries += P2P_MENU_ENTRIES;
        if (bad) {
            failed++;
            printf("  FAIL %-26s %s\n", "p2pMenu", bad < 0 ? "did not load" : "mismatch (see log)");
        }
    }
    ControlsMenu_Init();
    {
        int bad = MenuJson_VerifyDynamic(CONTROLS_MENU_NAME, CONTROLS_MENU_ENTRIES);
        menus++;
        entries += CONTROLS_MENU_ENTRIES;
        if (bad) {
            failed++;
            printf("  FAIL %-26s %s\n", "controlsMenu", bad < 0 ? "did not load" : "mismatch (see log)");
        }
    }
    {   /* SWOS 2020's OPTIONS screens and CONTROLLER SETUP (src/settings_menus.c) */
        static const struct { const char *name; int entries; } k_settings[] = {
            { "openswos/settingsMenu", 10 }, { "openswos/settingsPage", 27 }, { "openswos/joypadConfigMenu", 42 },
        };
        extern void SettingsMenus_Init(void);
        SettingsMenus_Init();
        for (size_t si = 0; si < sizeof k_settings / sizeof k_settings[0]; si++) {
            int bad = MenuJson_VerifyDynamic(k_settings[si].name, k_settings[si].entries);
            menus++;
            entries += k_settings[si].entries;
            if (bad) {
                failed++;
                printf("  FAIL %-26s %s\n", k_settings[si].name + 9, bad < 0 ? "did not load" : "mismatch (see log)");
            }
        }
    }
    ModsMenu_Init();
    {
        int bad = MenuJson_VerifyDynamic(MODS_MENU_NAME, MODS_MENU_ENTRIES);
        menus++;
        entries += MODS_MENU_ENTRIES;
        if (bad) {
            failed++;
            printf("  FAIL %-26s %s\n", "modsMenu", bad < 0 ? "did not load" : "mismatch (see log)");
        }
        { extern void ModEntries_Init(void); ModEntries_Init(); }
        bad = MenuJson_VerifyDynamic("openswos/modEntriesMenu", 12);
        menus++;
        entries += 12;
        if (bad) {
            failed++;
            printf("  FAIL %-26s %s\n", "modEntriesMenu", bad < 0 ? "did not load" : "mismatch (see log)");
        }
        bad = MenuJson_VerifyDynamic(MOD_PAGE_MENU_NAME, MOD_PAGE_MENU_ENTRIES);
        menus++;
        entries += MOD_PAGE_MENU_ENTRIES;
        if (bad) {
            failed++;
            printf("  FAIL %-26s %s\n", "modPageMenu", bad < 0 ? "did not load" : "mismatch (see log)");
        }
        bad = MenuJson_VerifyDynamic(CHOICE_MENU_NAME, CHOICE_MENU_ENTRIES);
        menus++;
        entries += CHOICE_MENU_ENTRIES;
        if (bad) {
            failed++;
            printf("  FAIL %-26s %s\n", "choiceMenu", bad < 0 ? "did not load" : "mismatch (see log)");
        }
    }
    CreditsMenu_Init();
    {
        int bad = MenuJson_VerifyDynamic(CREDITS_MENU_NAME, CREDITS_MENU_ENTRIES);
        menus++;
        entries += CREDITS_MENU_ENTRIES;
        if (bad) {
            failed++;
            printf("  FAIL %-26s %s\n", "creditsMenu", bad < 0 ? "did not load" : "mismatch (see log)");
        }
    }
    {
        int bad = MenuJson_VerifyDynamic(FULL_CREDITS_MENU_NAME, 1);
        menus++;
        entries += 1;
        if (bad || CreditsMenu_FullCreditsLines() <= 0) {
            failed++;
            printf("  FAIL %-26s %s\n", "fullCreditsMenu", bad < 0 ? "did not load" : bad ? "mismatch (see log)" :
                   "data/CREDITS.txt did not load");
        }
    }

#define NAME_STRING(fn) #fn,
    static const char *const handlers[] = { SWOS2020_MENU_CALLBACKS(NAME_STRING) };
#undef NAME_STRING
    int32_t numHandlers = (int32_t)(sizeof handlers / sizeof handlers[0]), ported = 0;
    for (int32_t i = 0; i < numHandlers; i++)
        if (find_handler(handlers[i]) || menu_resolve(handlers[i]))
            ported++;

    printf("  %d menus, %d entries unpacked and checked against their JSON: %d failed\n",
           menus, entries, failed);
    printf("  handlers: %d of %d ported, the rest run as no-ops\n", ported, numHandlers);
    printf("[online-menus] self-test %s\n", failed ? "FAILED" : "passed");
    fflush(stdout);
    return failed != 0;
}

/* ---- a fork's own online screens (the table at the top) ---- */
#if defined(__has_include)
#if __has_include("online_menus_fork.inc")
#define ONLINE_MENUS_FORK 1
#include "online_menus_fork.inc"        /* defines online_menus_fork() */
#endif
#endif
#ifndef ONLINE_MENUS_FORK
static const OnlineMenusFork *online_menus_fork(void) { return NULL; }
#endif
