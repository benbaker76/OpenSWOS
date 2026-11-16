/*
 * p2p_menu.c -- the MULTIPLAYER menu: two players connect to each other directly, by
 * IP address, with no server in between (SDL builds; docs/NETPLAY.md, "The MULTIPLAYER
 * menu"). It is what the main menu's MULTIPLAYER button opens.
 *
 * The menu itself is JSON only -- bin/data_json/menus/p2p/p2pMenu.json, built on first
 * use by MenuJson_Load (docs/MENUS.md section 10) -- and the connection is
 * src/p2p_link.c. The menu is flagged dos_redraw, so its onDraw runs every frame: that
 * is where the link is polled and the status panel written.
 *
 * It connects and checks the two games match -- the part a tester can try over the
 * internet: the address, the port, the router -- and then hands both players to the
 * lobby (src/online_menus.c, mpGameSetupMenu), where the match is set up and played.
 *
 * SWOS 2020's own online menus are imported separately and are a different thing
 * entirely -- a login, a lobby and a matchmaking server (src/online_menus.c).
 */
#include "test_rules.h"
#include "world.h"            /* World_OnlineEnter / _Leave: online is 96/97 */
#include "p2p_menu.h"

#ifdef SWOS_SDL_BUILD

#include "p2p_link.h"
#include "online_menus.h"     /* OnlineMenus_EnterLobby: where a connected pair sets the match up */
#include "input_text.h"
#include "menu_load_json.h"
#include "menu_symbols.h"
#include "menu_system.h"
#include "match_record.h"   /* a direct match is not the matchmaker's: "server" does not record it */

#include <stdio.h>
#include <string.h>

/* The menu's entries, in the order p2pMenu.json lists them. */
enum {
    P2PMenu_header,
    P2PMenu_subTitle,
    P2PMenu_modeLabel,
    P2PMenu_mode,
    P2PMenu_addressLabel,
    P2PMenu_address,
    P2PMenu_portLabel,
    P2PMenu_port,
    P2PMenu_delayLabel,
    P2PMenu_delay,
    P2PMenu_panel,
    P2PMenu_status1,
    P2PMenu_status2,
    P2PMenu_start,
    P2PMenu_exit,
    P2PMenu_kNumEntries
};

/* SWOS 2020's colour constants (mnu2h/Constants.py), the ones this menu uses. */
enum { kGray = 7, kLightBlue = 13 };

enum { kMaxInputDelay = 6 };    /* frames; GGPO rolls back at most 8 (docs/NETPLAY.md) */

static int32_t s_hosting = 1;               /* host a game, or join one */
static char    s_address[16];               /* the host's IPv4, typed in */
/* Not SWOS 2020's 47885 (mpgame.h GGPO_PORT1), which sits in the block Windows reserves
 * for WSL2's mirrored networking (44620-48715) and so failed on the owner's machine before
 * anything was sent -- and nothing here talks to SWOS 2020's netplay anyway. Below 32768 is
 * outside every system's ephemeral range (Linux 32768+, Windows and macOS 49152+), where
 * both the reservations and the ports other programs are handed out live. */
static char    s_port[6] = "27885";
static int32_t s_delay = 2;

static char s_delayText[24];
static char s_status1[64];
static char s_status2[64];

/* What the last thing the player did had to say, until they do the next. */
static const char *s_notice1;
static const char *s_notice2;

/* ---- entries ---- */

static MenuEntry *entry_at(int32_t ordinal)
{
    return CalcMenuEntryAddress((uint16_t)ordinal);
}

static void set_text(int32_t ordinal, const char *text)
{
    entry_at(ordinal)->fg = (uintptr_t)text;
}

/* A settings row: its text, whether it can be chosen now, and the colour that says so. */
static void set_row(int32_t ordinal, const char *text, int32_t enabled)
{
    MenuEntry *e = entry_at(ordinal);
    e->fg         = (uintptr_t)text;
    e->isDisabled = enabled ? 0 : 1;
    e->bgColor    = (uint16_t)(enabled ? kLightBlue : kGray);
}

static void notice(const char *line1, const char *line2)
{
    s_notice1 = line1;
    s_notice2 = line2;
}

/* ---- the settings ---- */

/* The port as a number, or 0 if what was typed is not one this can bind. Ports below
 * 1024 need privileges on every platform we ship, so they are not offered. */
static uint16_t parse_port(const char *text)
{
    int32_t value = 0;

    if (!text[0])
        return 0;
    for (const char *p = text; *p; p++) {
        if (*p < '0' || *p > '9')
            return 0;
        value = value * 10 + (*p - '0');
        if (value > 65535)
            return 0;
    }
    return value < 1024 ? 0 : (uint16_t)value;
}

static const char *delay_text(void)
{
    if (s_delay == 0)
        snprintf(s_delayText, sizeof s_delayText, "NONE");
    else if (s_delay == 1)
        snprintf(s_delayText, sizeof s_delayText, "1 FRAME (20 MS)");
    else
        snprintf(s_delayText, sizeof s_delayText, "%d FRAMES (%d MS)", s_delay, s_delay * 20);
    return s_delayText;
}

/* ---- the status panel ---- */

static void set_status(const char *line1, const char *line2)
{
    snprintf(s_status1, sizeof s_status1, "%s", line1);
    snprintf(s_status2, sizeof s_status2, "%s", line2);
}

static void update_status(P2PLinkState state)
{
    const char *line1, *line2;

    switch (state) {
    case P2P_IDLE:
        if (s_notice1)
            set_status(s_notice1, s_notice2 ? s_notice2 : "");
        else if (s_hosting)
            set_status("YOUR FRIEND NEEDS YOUR IP ADDRESS",
                       "AND THIS PORT, OPEN ON YOUR ROUTER");
        else
            set_status("TYPE THE HOST'S IP ADDRESS AND PORT", "THEN CHOOSE JOIN");
        break;

    case P2P_HOSTING:
        snprintf(s_status1, sizeof s_status1, "WAITING FOR A PLAYER ON PORT %u",
                 (unsigned)P2PLink_LocalPort());
        if (P2PLink_Notice() != P2P_FAIL_NONE) {
            P2PLink_FailureText(P2PLink_Notice(), &line1, &line2);
            snprintf(s_status2, sizeof s_status2, "%s", line1);
        } else {
            snprintf(s_status2, sizeof s_status2, "CHOOSE CANCEL TO STOP");
        }
        break;

    case P2P_JOINING:
        snprintf(s_status1, sizeof s_status1, "CONNECTING TO %s", P2PLink_PeerText());
        snprintf(s_status2, sizeof s_status2, "WAITING FOR THE HOST TO ANSWER");
        break;

    case P2P_CONNECTED:
        snprintf(s_status1, sizeof s_status1, "CONNECTED TO %s", P2PLink_PeerText());
        if (P2PLink_PingMs() >= 0)
            snprintf(s_status2, sizeof s_status2, "PING %d MS. PLAYING A MATCH COMES NEXT",
                     P2PLink_PingMs());
        else
            snprintf(s_status2, sizeof s_status2, "MEASURING THE CONNECTION");
        break;

    case P2P_FAILED:
        P2PLink_FailureText(P2PLink_Failure(), &line1, &line2);
        set_status(line1, line2);
        break;
    }
}

/* ---- handlers ---- */

static int32_t link_is_open(P2PLinkState state)
{
    return state == P2P_HOSTING || state == P2P_JOINING || state == P2P_CONNECTED;
}

static void p2pMenu_OnInit(void)
{
    /* A link left open by the last visit -- the menu was left while hosting, say. */
    P2PLink_Close();
    notice(NULL, NULL);
    set_status("", "");
}

/* Every frame (the menu is flagged dos_redraw), so the panel follows the link without
 * anything having to push it. */
static void p2pMenu_OnDraw(void)
{
    P2PLink_Poll();

    /* Connected and the delay agreed: the lobby, the same one matchmaking leads to. The
     * host is player 1. It returns with the link closed, the match played or abandoned. */
    if (P2PLink_State() == P2P_CONNECTED && P2PLink_AgreedDelay() >= 0) {
        OnlineMenus_EnterLobby(s_hosting ? 1 : 2, NULL);
        notice(NULL, NULL);
    }

    P2PLinkState state = P2PLink_State();
    int32_t      open  = link_is_open(state);

    set_row(P2PMenu_mode, s_hosting ? "HOST A GAME" : "JOIN A GAME", !open);
    set_row(P2PMenu_address,
            s_hosting ? "NOT NEEDED WHEN HOSTING" : (s_address[0] ? s_address : "TYPE IT HERE"),
            !open && !s_hosting);
    set_row(P2PMenu_port, s_port, !open);
    set_row(P2PMenu_delay, delay_text(), !open);

    set_text(P2PMenu_start, !open ? (s_hosting ? "HOST" : "JOIN")
                                  : (state == P2P_CONNECTED ? "DISCONNECT" : "CANCEL"));

    update_status(state);
    set_text(P2PMenu_status1, s_status1);
    set_text(P2PMenu_status2, s_status2);
}

static void p2pMenu_mode_OnSelect(void)
{
    if (link_is_open(P2PLink_State()))
        return;
    s_hosting = !s_hosting;
    notice(NULL, NULL);
}

static void p2pMenu_address_OnSelect(void)
{
    MenuEntry *e = entry_at(P2PMenu_address);

    if (s_hosting || link_is_open(P2PLink_State()))
        return;

    notice(NULL, NULL);
    e->fg = (uintptr_t)s_address;       /* edit the address itself, not the prompt */
    InputText(sizeof s_address, (uint8_t *)e);
    if (s_address[0] && !P2PLink_IsAddress(s_address))
        notice("THAT IS NOT AN IP ADDRESS", "TYPE IT LIKE 192.168.1.20");
}

static void p2pMenu_port_OnSelect(void)
{
    char previous[sizeof s_port];

    if (link_is_open(P2PLink_State()))
        return;

    notice(NULL, NULL);
    memcpy(previous, s_port, sizeof previous);
    InputText(sizeof s_port, (uint8_t *)entry_at(P2PMenu_port));
    if (!parse_port(s_port)) {
        memcpy(s_port, previous, sizeof s_port);
        notice("THE PORT IS A NUMBER FROM 1024 TO 65535", "");
    }
}

static void p2pMenu_delay_OnSelect(void)
{
    if (link_is_open(P2PLink_State()))
        return;
    s_delay = (s_delay + 1) % (kMaxInputDelay + 1);
}

static void p2pMenu_start_OnSelect(void)
{
    uint16_t port;

    if (link_is_open(P2PLink_State())) {
        P2PLink_Close();
        notice(NULL, NULL);
        return;
    }

    notice(NULL, NULL);
    port = parse_port(s_port);
    if (!port) {
        notice("THE PORT IS A NUMBER FROM 1024 TO 65535", "");
        return;
    }

    MatchRecord_SetArrangedByServer(0);
    if (s_hosting)
        P2PLink_Host(port);
    else
        P2PLink_Join(s_address, port);
    /* Either way the link now says what happened, and OnDraw shows it. */
}

static void p2pMenu_exit_OnSelect(void)
{
    extern void GameplayStyle_GiveBack(void);
    GameplayStyle_GiveBack();       /* joined a host of the other style, then left */
    P2PLink_Close();
    SetExitMenuFlag();
}

/* ---- the menu ---- */

#define HANDLER(fn) { #fn, (void *)fn, SYM_FUNC }

static const MenuSymbol k_handlers[] = {
    HANDLER(p2pMenu_OnInit),
    HANDLER(p2pMenu_OnDraw),
    HANDLER(p2pMenu_mode_OnSelect),
    HANDLER(p2pMenu_address_OnSelect),
    HANDLER(p2pMenu_port_OnSelect),
    HANDLER(p2pMenu_delay_OnSelect),
    HANDLER(p2pMenu_start_OnSelect),
    HANDLER(p2pMenu_exit_OnSelect),
};

#undef HANDLER

void P2PMenu_Init(void)
{
    static int32_t registered;

    if (registered)
        return;
    registered = 1;
    MenuJson_RegisterSymbols(k_handlers, (int)(sizeof k_handlers / sizeof k_handlers[0]));
}

void P2PMenu_Show(void)
{
    uint8_t *menu;

    P2PMenu_Init();
    menu = MenuJson_Load(P2P_MENU_NAME);
    if (!menu) {
        fprintf(stderr, "[p2p-menu] cannot show %s\n", P2P_MENU_NAME);
        return;
    }
    TestRules_Online(1);        /* test_rules.h: the testers' rule stops at this door */
    World_OnlineEnter();        /* (online is 96/97, whatever world is played offline: world_online.c) */
    ShowMenu(menu);
    World_OnlineLeave();
}

#endif /* SWOS_SDL_BUILD */
