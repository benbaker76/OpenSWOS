/*
 * team_packs_menu.c -- the list of team packs (openswos/teamPacksMenu): CHOOSE TEAMS' TEAM PACKS
 * row with more than one pack. Up to 8: each its name, its description under the list for the one
 * the cursor is on. SDL builds. include/team_packs.h.
 */
#include "team_packs.h"

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

static void refresh(void)
{
    const int32_t n = TeamPacks_Count();
    at(P_header)->fg = (uintptr_t)"TEAM PACKS";
    at(P_subTitle)->fg = (uintptr_t)"TEAMS THE MODS THAT ARE ON BRING";
    for (int32_t i = 0; i < kRows; i++) {
        const char *name = NULL;
        at(P_row0 + i)->isInvisible = TeamPacks_Info(i, NULL, &name, NULL, NULL) != 0;
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
                TeamPacks_Info(i, NULL, NULL, &d, NULL);
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
    const int32_t n = TeamPacks_Count() < kRows ? TeamPacks_Count() : kRows;
    for (int32_t i = 0; i < n; i++)
        nav(P_row0 + i, i ? P_row0 + i - 1 : -1, i + 1 < n ? P_row0 + i + 1 : P_exit);
    nav(P_exit, n ? P_row0 + n - 1 : -1, -1);
    refresh();
}

static void teamPacksMenu_OnDraw(void) { refresh(); }

static void pick(int32_t i)
{
    int32_t number = 0;
    if (TeamPacks_Info(i, &number, NULL, NULL, NULL) == 0) {
        s_picked = number;
        SetExitMenuFlag();
    }
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

/* The pack the player picks (its number), 0 when they left; one pack: that one, no list. */
int32_t TeamPacksMenu_Choose(void)
{
    int32_t number = 0;
    if (TeamPacks_Count() == 1 && TeamPacks_Info(0, &number, NULL, NULL, NULL) == 0)
        return number;
    TeamPacksMenu_Init();
    s_picked = 0;
    do
        GetKey();
    while (_currentKey != 0);
    uint8_t *menu = MenuJson_Load("openswos/teamPacksMenu");
    if (!menu)
        return 0;
    ShowMenu(menu);
    return s_picked;
}

#endif /* SWOS_SDL_BUILD */
