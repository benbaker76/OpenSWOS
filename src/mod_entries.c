/*
 * mod_entries.c -- the mods' entries (include/mod_api.h ModApi_AddEntry: H1, H6), their menus'
 * handlers (ModApi_RegisterHandlers: H10), and the screen that lists a point's entries
 * (bin/data_json/menus/openswos/modEntriesMenu.json). SDL builds. docs/MODS.md §16.
 */
#include "mod_api.h"

#ifdef SWOS_SDL_BUILD

#include "menu_load_json.h"
#include "menu_symbols.h"
#include "menu_system.h"
#include "mods.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MOD_ENTRIES_MENU_NAME "openswos/modEntriesMenu"

extern void SetExitMenuFlag(void);
extern int32_t GameLoop_Active(void);
extern int32_t ModApi_InReplay(void);

enum { kRows = 8 };

typedef struct {
    char slug[MODS_ID_MAX];
    char label[40];
    void (*select)(void);
} Entry;

static Entry   s_entries[MODPOINT_COUNT][kRows];
static int32_t s_entryCount[MODPOINT_COUNT];

int32_t ModApi_AddEntry(ModPoint point, const char *slug, const char *label, void (*select)(void))
{
    if ((int32_t)point < 0 || point >= MODPOINT_COUNT || !label || !select || s_entryCount[point] >= kRows)
        return -1;
    Entry *e = &s_entries[point][s_entryCount[point]++];
    snprintf(e->slug, sizeof e->slug, "%s", slug ? slug : "");
    size_t i;
    for (i = 0; label[i] && i + 1 < sizeof e->label; i++)
        e->label[i] = (char)(label[i] >= 'a' && label[i] <= 'z' ? label[i] - 32 : label[i]);
    e->label[i] = '\0';
    e->select = select;
    return 0;
}

int32_t ModApi_EntryCount(ModPoint point)
{
    return (int32_t)point >= 0 && point < MODPOINT_COUNT ? s_entryCount[point] : 0;
}

int32_t ModApi_InMenus(void) { return !GameLoop_Active() && !ModApi_InReplay(); }

/* ---- handlers: "<slug>.<name>" in the menu loader's symbols ---- */

int32_t ModApi_RegisterHandlers(const char *slug, const ModHandler *handlers, int32_t count)
{
    if (!slug || !handlers || count <= 0)
        return -1;
    MenuSymbol *syms = (MenuSymbol *)calloc((size_t)count, sizeof *syms);   /* (kept: the loader keeps the table) */
    if (!syms)
        return -1;
    for (int32_t i = 0; i < count; i++) {
        const size_t n = strlen(slug) + 1 + strlen(handlers[i].name) + 1;
        char *name = (char *)malloc(n);
        if (!name)
            return -1;
        snprintf(name, n, "%s.%s", slug, handlers[i].name);
        syms[i].name = name;
        syms[i].ptr = (void *)handlers[i].fn;
        syms[i].kind = SYM_FUNC;
    }
    MenuJson_RegisterSymbols(syms, count);
    return 0;
}

int32_t ModApi_ShowMenu(const char *menuPath)
{
    uint8_t *menu = MenuJson_Load(menuPath);
    if (!menu) {
        fprintf(stderr, "[mods] cannot show %s\n", menuPath);
        return -1;
    }
    ShowMenu(menu);
    return 0;
}

/* ---- the screen ---- */

enum {
    Entries_header,
    Entries_subTitle,
    Entries_row0,
    Entries_note = Entries_row0 + kRows,
    Entries_exit,
};

static ModPoint s_shown;

static MenuEntry *entry_at(int32_t ordinal) { return CalcMenuEntryAddress((uint16_t)ordinal); }

static void refresh(void)
{
    static const char *const kTitle[MODPOINT_COUNT] = { "MODES", "AFTER THE MATCH" };
    static const char *const kSub[MODPOINT_COUNT] = { "WHAT THE MODS THAT ARE ON ADD", "FROM THE MODS THAT ARE ON" };
    entry_at(Entries_header)->fg = (uintptr_t)kTitle[s_shown];
    entry_at(Entries_subTitle)->fg = (uintptr_t)kSub[s_shown];
    for (int32_t i = 0; i < kRows; i++) {
        MenuEntry *row = entry_at(Entries_row0 + i);
        row->isInvisible = i >= s_entryCount[s_shown];
        if (i < s_entryCount[s_shown])
            row->fg = (uintptr_t)s_entries[s_shown][i].label;
    }
    entry_at(Entries_note)->fg = (uintptr_t)(s_entryCount[s_shown] ? "" : "NONE: NO MOD THAT IS ON HAS ONE");
}

static void nav(int32_t ordinal, int32_t up, int32_t down)
{
    MenuEntry *e = entry_at(ordinal);
    e->upEntry = (int8_t)up;
    e->downEntry = (int8_t)down;
    e->directionUp = e->directionDown = -1;
}

static void modEntriesMenu_OnInit(void)
{
    const int32_t n = s_entryCount[s_shown];
    for (int32_t i = 0; i < n; i++)
        nav(Entries_row0 + i, i ? Entries_row0 + i - 1 : -1, i + 1 < n ? Entries_row0 + i + 1 : Entries_exit);
    nav(Entries_exit, n ? Entries_row0 + n - 1 : -1, -1);
    if (!n)
        SetCurrentEntry(Entries_exit);
    refresh();
}

static void modEntriesMenu_OnDraw(void) { refresh(); }

static void run(int32_t i)
{
    if (i < s_entryCount[s_shown] && s_entries[s_shown][i].select) {
        const ModPoint point = s_shown;
        fprintf(stderr, "[mods] %s: %s\n", s_entries[point][i].slug, s_entries[point][i].label);
        s_entries[point][i].select();
        s_shown = point;                    /* (an entry may show another point's list) */
    }
}

static void modEntriesMenu_row0_OnSelect(void) { run(0); }
static void modEntriesMenu_row1_OnSelect(void) { run(1); }
static void modEntriesMenu_row2_OnSelect(void) { run(2); }
static void modEntriesMenu_row3_OnSelect(void) { run(3); }
static void modEntriesMenu_row4_OnSelect(void) { run(4); }
static void modEntriesMenu_row5_OnSelect(void) { run(5); }
static void modEntriesMenu_row6_OnSelect(void) { run(6); }
static void modEntriesMenu_row7_OnSelect(void) { run(7); }
static void modEntriesMenu_exit_OnSelect(void) { SetExitMenuFlag(); }

#define HANDLER(fn) { #fn, (void *)fn, SYM_FUNC }
static const MenuSymbol k_handlers[] = {
    HANDLER(modEntriesMenu_OnInit),       HANDLER(modEntriesMenu_OnDraw),
    HANDLER(modEntriesMenu_row0_OnSelect), HANDLER(modEntriesMenu_row1_OnSelect),
    HANDLER(modEntriesMenu_row2_OnSelect), HANDLER(modEntriesMenu_row3_OnSelect),
    HANDLER(modEntriesMenu_row4_OnSelect), HANDLER(modEntriesMenu_row5_OnSelect),
    HANDLER(modEntriesMenu_row6_OnSelect), HANDLER(modEntriesMenu_row7_OnSelect),
    HANDLER(modEntriesMenu_exit_OnSelect),
};
#undef HANDLER

void ModEntries_Init(void)
{
    static int32_t registered;
    if (!registered) {
        registered = 1;
        MenuJson_RegisterSymbols(k_handlers, (int)(sizeof k_handlers / sizeof k_handlers[0]));
    }
}

void ModApi_ShowEntries(ModPoint point)
{
    if ((int32_t)point < 0 || point >= MODPOINT_COUNT)
        return;
    ModEntries_Init();
    s_shown = point;
    ModApi_ShowMenu(MOD_ENTRIES_MENU_NAME);
}

/* (for the game's C, int32_t: src/menus.c's MODES button) */
int32_t ModEntries_Count(int32_t point) { return ModApi_EntryCount((ModPoint)point); }
void ModEntries_Show(int32_t point) { ModApi_ShowEntries((ModPoint)point); }

/* After a friendly's final whistle (src/game_loop.c): its AFTER THE MATCH entries, when a mod
 * has one. */
void ModEntries_AfterMatch(void)
{
    if (s_entryCount[MODPOINT_AFTER_MATCH])
        ModApi_ShowEntries(MODPOINT_AFTER_MATCH);
}

#endif /* SWOS_SDL_BUILD */
