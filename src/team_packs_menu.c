/*
 * team_packs_menu.c -- the list of team packs (openswos/teamPacksMenu): CHOOSE TEAMS' TEAM PACKS
 * row with more than one pack. Up to 8: each its name, its description under the list for the one
 * the cursor is on. SDL builds. include/team_packs.h.
 *
 * After the mods' packs, the other installed worlds (include/world_packs.h): a world opens the
 * list of its countries (openswos/worldCountriesMenu, 48 a page), and the country picked is a pack
 * of the session.
 */
#include "team_packs.h"
#include "world_packs.h"

#ifdef SWOS_SDL_BUILD

#include "menu_load_json.h"
#include "menu_symbols.h"
#include "menu_system.h"

#include <stdio.h>
#include <string.h>

extern void SetExitMenuFlag(void);
extern volatile uint16_t _currentKey;
extern uint8_t _currentMenu[];

enum { kRows = 8, P_header = 0, P_subTitle, P_row0, P_note = P_row0 + kRows, P_exit };

static int32_t s_picked;
static char    s_desc[64];

static MenuEntry *at(int32_t i) { return CalcMenuEntryAddress((uint16_t)i); }

/* The list's rows: the mods' packs, then the other worlds. */
static int32_t worlds(void)
{
#if SWOS_WORLDS
    return WorldPacks_WorldCount();
#else
    return 0;
#endif
}

static int32_t rows(void)
{
    const int32_t n = TeamPacks_Count() + worlds();
    return n < kRows ? n : kRows;
}

/* Row i's name and description (0), or -1. */
static int32_t row_info(int32_t i, const char **name, const char **desc)
{
    if (i < TeamPacks_Count())
        return TeamPacks_Info(i, NULL, name, desc, NULL);
#if SWOS_WORLDS
    if (WorldPacks_World(i - TeamPacks_Count(), NULL, name) == 0) {
        if (desc)
            *desc = "ITS CLUBS IN A FRIENDLY OR A DIY COMPETITION";
        return 0;
    }
#endif
    return -1;
}

static void refresh(void)
{
    const int32_t n = rows();
    at(P_header)->fg = (uintptr_t)(TeamPacks_Count() ? "TEAM PACKS" : "OTHER WORLDS");
    at(P_subTitle)->fg = (uintptr_t)(!worlds() ? "TEAMS THE MODS THAT ARE ON BRING"
                                     : TeamPacks_Count() ? "THE MODS' TEAMS AND THE OTHER WORLDS' CLUBS"
                                     : "THE CLUBS OF THE OTHER WORLDS INSTALLED");
    for (int32_t i = 0; i < kRows; i++) {
        const char *name = NULL;
        at(P_row0 + i)->isInvisible = row_info(i, &name, NULL) != 0;
        if (name)
            at(P_row0 + i)->fg = (uintptr_t)name;
    }
    /* the description of the one the cursor is on */
    s_desc[0] = '\0';
    {
        const Menu *menu = (const Menu *)_currentMenu;
        for (int32_t i = 0; i < kRows && i < n; i++)
            if (menu->selectedEntry == at(P_row0 + i)) {
                const char *d = NULL;
                row_info(i, NULL, &d);
                snprintf(s_desc, sizeof s_desc, "%s", d ? d : "");
            }
    }
    at(P_note)->fg = (uintptr_t)s_desc;
}

static void nav(int32_t i, int32_t up, int32_t down)
{
    MenuEntry *e = at(i);
    e->upEntry = (int8_t)up;
    e->downEntry = (int8_t)down;
    e->directionUp = e->directionDown = -1;
}

static void teamPacksMenu_OnInit(void)
{
    const int32_t n = rows();
    for (int32_t i = 0; i < n; i++)
        nav(P_row0 + i, i ? P_row0 + i - 1 : -1, i + 1 < n ? P_row0 + i + 1 : P_exit);
    nav(P_exit, n ? P_row0 + n - 1 : -1, -1);
    refresh();
}

static void teamPacksMenu_OnDraw(void) { refresh(); }

static int32_t s_world = -1;      /* a world picked: its countries next */

static void pick(int32_t i)
{
    int32_t number = 0;
    if (TeamPacks_Info(i, &number, NULL, NULL, NULL) == 0) {
        s_picked = number;
        SetExitMenuFlag();
    }
#if SWOS_WORLDS
    else if (WorldPacks_World(i - TeamPacks_Count(), &number, NULL) == 0) {
        s_world = number;
        SetExitMenuFlag();
    }
#endif
}

static void teamPacksMenu_row0_OnSelect(void) { pick(0); }
static void teamPacksMenu_row1_OnSelect(void) { pick(1); }
static void teamPacksMenu_row2_OnSelect(void) { pick(2); }
static void teamPacksMenu_row3_OnSelect(void) { pick(3); }
static void teamPacksMenu_row4_OnSelect(void) { pick(4); }
static void teamPacksMenu_row5_OnSelect(void) { pick(5); }
static void teamPacksMenu_row6_OnSelect(void) { pick(6); }
static void teamPacksMenu_row7_OnSelect(void) { pick(7); }
static void teamPacksMenu_exit_OnSelect(void) { SetExitMenuFlag(); }

#define HANDLER(fn) { #fn, (void *)fn, SYM_FUNC }
static const MenuSymbol k_handlers[] = {
    HANDLER(teamPacksMenu_OnInit), HANDLER(teamPacksMenu_OnDraw),
    HANDLER(teamPacksMenu_row0_OnSelect), HANDLER(teamPacksMenu_row1_OnSelect),
    HANDLER(teamPacksMenu_row2_OnSelect), HANDLER(teamPacksMenu_row3_OnSelect),
    HANDLER(teamPacksMenu_row4_OnSelect), HANDLER(teamPacksMenu_row5_OnSelect),
    HANDLER(teamPacksMenu_row6_OnSelect), HANDLER(teamPacksMenu_row7_OnSelect),
    HANDLER(teamPacksMenu_exit_OnSelect),
};
#undef HANDLER

void TeamPacksMenu_Init(void)
{
    static int32_t registered;
    if (!registered) {
        registered = 1;
        MenuJson_RegisterSymbols(k_handlers, (int)(sizeof k_handlers / sizeof k_handlers[0]));
    }
}

#if SWOS_WORLDS
static int32_t WorldCountriesMenu_Choose(int32_t world);
#endif

/* The pack the player picks (its number), 0 when they left; one pack and no other world: that
 * one, no list. */
int32_t TeamPacksMenu_Choose(void)
{
    int32_t number = 0;
    if (TeamPacks_Count() == 1 && !worlds() && TeamPacks_Info(0, &number, NULL, NULL, NULL) == 0)
        return number;
    TeamPacksMenu_Init();
    for (;;) {
        s_picked = 0;
        s_world = -1;
        do
            GetKey();
        while (_currentKey != 0);
        uint8_t *menu = MenuJson_Load("openswos/teamPacksMenu");
        if (!menu)
            return 0;
        ShowMenu(menu);
#if SWOS_WORLDS
        if (s_world >= 0) {
            const int32_t pack = WorldCountriesMenu_Choose(s_world);
            if (pack > 0)
                return pack;
            continue;                       /* (EXIT on the countries: the list again) */
        }
#endif
        return s_picked;
    }
}

#if SWOS_WORLDS
/* ---- a world's countries (openswos/worldCountriesMenu) ---- */

enum { kCRows = 48, C_header = 0, C_subTitle, C_row0, C_prev = C_row0 + kCRows, C_exit, C_next };

static int32_t s_cWorld, s_cPage, s_cPicked;
static char    s_cHeader[40], s_cSub[64];

static int32_t c_count(void) { return WorldPacks_CountryCount(s_cWorld); }
static int32_t c_pages(void) { return (c_count() + kCRows - 1) / kCRows; }

static void c_nav(void)
{
    const int32_t first = s_cPage * kCRows;
    const int32_t n = c_count() - first < kCRows ? c_count() - first : kCRows;
    const int32_t bottom = c_pages() > 1 ? C_next : C_exit;
    for (int32_t i = 0; i < kCRows; i++) {
        MenuEntry *e = at(C_row0 + i);
        const int32_t col = i / 16, row = i % 16;
        e->isInvisible = i >= n;
        e->upEntry = (int8_t)(row ? C_row0 + i - 1 : -1);
        e->downEntry = (int8_t)(row < 15 && i + 1 < n ? C_row0 + i + 1 : bottom);
        e->leftEntry = (int8_t)(col ? C_row0 + i - 16 : -1);
        e->rightEntry = (int8_t)(col < 2 && i + 16 < n ? C_row0 + i + 16 : -1);
        e->directionUp = e->directionDown = e->directionLeft = e->directionRight = -1;
        const char *name = NULL;
        if (i < n && WorldPacks_Country(s_cWorld, first + i, &name, NULL) == 0)
            e->fg = (uintptr_t)name;
    }
    at(C_prev)->isInvisible = at(C_next)->isInvisible = c_pages() <= 1;
    nav(C_prev, n ? C_row0 : -1, -1);
    nav(C_exit, n ? C_row0 : -1, -1);
    nav(C_next, n ? C_row0 : -1, -1);
    at(C_prev)->rightEntry = C_exit;
    at(C_exit)->leftEntry = c_pages() > 1 ? C_prev : -1;
    at(C_exit)->rightEntry = c_pages() > 1 ? C_next : -1;
    at(C_next)->leftEntry = C_exit;
}

static void c_refresh(void)
{
    const char *label = "";
    for (int32_t i = 0; ; i++) {
        int32_t w;
        const char *l;
        if (WorldPacks_World(i, &w, &l) != 0)
            break;
        if (w == s_cWorld)
            label = l;
    }
    snprintf(s_cHeader, sizeof s_cHeader, "%s", label);
    if (c_pages() > 1)
        snprintf(s_cSub, sizeof s_cSub, "CHOOSE A COUNTRY  (PAGE %d OF %d)", (int)(s_cPage + 1), (int)c_pages());
    else
        snprintf(s_cSub, sizeof s_cSub, "CHOOSE A COUNTRY");
    at(C_header)->fg = (uintptr_t)s_cHeader;
    at(C_subTitle)->fg = (uintptr_t)s_cSub;
}

static void worldCountriesMenu_OnInit(void)
{
    c_nav();
    c_refresh();
}

static void worldCountriesMenu_OnDraw(void) { c_refresh(); }

static void c_pick(int32_t i)
{
    const char *file = NULL;
    if (WorldPacks_Country(s_cWorld, s_cPage * kCRows + i, NULL, &file) == 0) {
        s_cPicked = WorldPacks_Use(s_cWorld, file);
        if (s_cPicked > 0)
            SetExitMenuFlag();
    }
}

static void c_page(int32_t by)
{
    const int32_t pages = c_pages();
    if (pages <= 1)
        return;
    s_cPage = (s_cPage + by + pages) % pages;
    c_nav();
    c_refresh();
}

#define C_ROW(n) static void worldCountriesMenu_row##n##_OnSelect(void) { c_pick(n); }
C_ROW(0) C_ROW(1) C_ROW(2) C_ROW(3) C_ROW(4) C_ROW(5) C_ROW(6) C_ROW(7) C_ROW(8) C_ROW(9)
C_ROW(10) C_ROW(11) C_ROW(12) C_ROW(13) C_ROW(14) C_ROW(15) C_ROW(16) C_ROW(17) C_ROW(18) C_ROW(19)
C_ROW(20) C_ROW(21) C_ROW(22) C_ROW(23) C_ROW(24) C_ROW(25) C_ROW(26) C_ROW(27) C_ROW(28) C_ROW(29)
C_ROW(30) C_ROW(31) C_ROW(32) C_ROW(33) C_ROW(34) C_ROW(35) C_ROW(36) C_ROW(37) C_ROW(38) C_ROW(39)
C_ROW(40) C_ROW(41) C_ROW(42) C_ROW(43) C_ROW(44) C_ROW(45) C_ROW(46) C_ROW(47)
#undef C_ROW
static void worldCountriesMenu_prev_OnSelect(void) { c_page(-1); }
static void worldCountriesMenu_next_OnSelect(void) { c_page(1); }
static void worldCountriesMenu_exit_OnSelect(void) { SetExitMenuFlag(); }

#define HANDLER(fn) { #fn, (void *)fn, SYM_FUNC }
#define C_H(n) HANDLER(worldCountriesMenu_row##n##_OnSelect),
static const MenuSymbol k_cHandlers[] = {
    HANDLER(worldCountriesMenu_OnInit), HANDLER(worldCountriesMenu_OnDraw),
    C_H(0) C_H(1) C_H(2) C_H(3) C_H(4) C_H(5) C_H(6) C_H(7) C_H(8) C_H(9)
    C_H(10) C_H(11) C_H(12) C_H(13) C_H(14) C_H(15) C_H(16) C_H(17) C_H(18) C_H(19)
    C_H(20) C_H(21) C_H(22) C_H(23) C_H(24) C_H(25) C_H(26) C_H(27) C_H(28) C_H(29)
    C_H(30) C_H(31) C_H(32) C_H(33) C_H(34) C_H(35) C_H(36) C_H(37) C_H(38) C_H(39)
    C_H(40) C_H(41) C_H(42) C_H(43) C_H(44) C_H(45) C_H(46) C_H(47)
    HANDLER(worldCountriesMenu_prev_OnSelect), HANDLER(worldCountriesMenu_next_OnSelect),
    HANDLER(worldCountriesMenu_exit_OnSelect),
};
#undef C_H
#undef HANDLER

/* The pack number of the country picked from a world's, 0 when the player left. */
static int32_t WorldCountriesMenu_Choose(int32_t world)
{
    static int32_t registered;
    if (!registered) {
        registered = 1;
        MenuJson_RegisterSymbols(k_cHandlers, (int)(sizeof k_cHandlers / sizeof k_cHandlers[0]));
    }
    s_cWorld = world;
    s_cPage = 0;
    s_cPicked = 0;
    do
        GetKey();
    while (_currentKey != 0);
    uint8_t *menu = MenuJson_Load("openswos/worldCountriesMenu");
    if (!menu)
        return 0;
    ShowMenu(menu);
    return s_cPicked;
}
#endif

#endif /* SWOS_SDL_BUILD */
