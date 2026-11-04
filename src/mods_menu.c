/*
 * mods_menu.c -- the MODS screen (SDL builds): every mod on disk, canonical and DLC, each turned
 * on or off. Opened from OPTIONS (menus.c ShowModsMenu). docs/MODS.md §14-15.
 *
 * A mod's files go over the game's when the game starts (src/mods.c, the overlay), so a choice
 * here is played from the NEXT start, as OPTIONS -> WORLD's is; it is written to SWOS.ini at
 * once: [mods] enabled for a DLC (and a canonical mod that is off by default), [mods] disabled
 * for a canonical mod that is on by default.
 *
 * The menu is JSON only (bin/data_json/menus/openswos/modsMenu.json, docs/MENUS.md section 10)
 * and flagged dos_redraw, so its onDraw runs every frame.
 */
#include "mods.h"
#include "mods_menu.h"

#ifdef SWOS_SDL_BUILD

#include "menu_load_json.h"
#include "menu_symbols.h"
#include "menu_system.h"
#include "platform_sdl.h"
#include "swos_settings.h"

#include <stdio.h>
#include <string.h>

/* The menu's entries, in the order modsMenu.json lists them. */
enum {
    ModsMenu_header,
    ModsMenu_subTitle,
    ModsMenu_name0,                     /* name<i> = name0 + 2 i, state<i> = name0 + 2 i + 1 */
    ModsMenu_note = ModsMenu_name0 + 2 * 8,
    ModsMenu_exit,
};
enum { kRows = 8 };

extern void SetExitMenuFlag(void);

static ModInfo s_mods[kRows];
static int32_t s_count;
static char    s_nameText[kRows][40];
static char    s_note[64];

static MenuEntry *entry_at(int32_t ordinal)
{
    return CalcMenuEntryAddress((uint16_t)ordinal);
}

static void upper(char *dst, uint32_t cap, const char *src)
{
    uint32_t i;
    for (i = 0; src[i] && i + 1 < cap; i++)
        dst[i] = (char)(src[i] >= 'a' && src[i] <= 'z' ? src[i] - 32 : src[i]);
    dst[i] = '\0';
}

/* ---- [mods] enabled / disabled: comma lists of slugs ---- */

static int list_has(const char *list, const char *slug)
{
    const size_t n = strlen(slug);
    for (const char *p = list; (p = strstr(p, slug)) != NULL; p += n)
        if ((p == list || p[-1] == ',' || p[-1] == ' ') && (p[n] == '\0' || p[n] == ',' || p[n] == ' '))
            return 1;
    return 0;
}

static void list_set(char *list, uint32_t cap, const char *slug, int on)
{
    char out[256] = "", item[MODS_ID_MAX];
    uint32_t used = 0;
    const char *p = list;
    while (*p) {
        uint32_t k = 0;
        while (*p == ',' || *p == ' ') p++;
        while (*p && *p != ',' && *p != ' ' && k + 1 < sizeof item) item[k++] = *p++;
        item[k] = '\0';
        if (k && strcmp(item, slug) != 0)
            used += (uint32_t)snprintf(out + used, sizeof out - used, "%s%s", used ? ", " : "", item);
    }
    if (on)
        used += (uint32_t)snprintf(out + used, sizeof out - used, "%s%s", used ? ", " : "", slug);
    snprintf(list, cap, "%s", out);
}

/* Whether a mod is set to be on: what SWOS.ini says now (the next start's), not what this run
 * loaded. */
static int set_on(const PlatformInitOptions *o, const ModInfo *m)
{
    if (m->canonical && m->byDefault)
        return !list_has(o->mods_disabled, m->slug);
    return list_has(o->mods, m->slug);
}

static void refresh(void)
{
    extern PlatformInitOptions *g_arcadeOpts;
    int32_t i;
    for (i = 0; i < kRows; i++) {
        MenuEntry *name = entry_at(ModsMenu_name0 + 2 * i), *state = entry_at(ModsMenu_name0 + 2 * i + 1);
        name->isInvisible = state->isInvisible = i >= s_count;
        if (i >= s_count)
            continue;
        char text[64];
        snprintf(text, sizeof text, "%s %s (%s)", s_mods[i].name, s_mods[i].version,
                 s_mods[i].canonical ? "GAME'S" : "DLC");
        upper(s_nameText[i], sizeof s_nameText[i], text);
        name->fg = (uintptr_t)s_nameText[i];
        state->fg = (uintptr_t)(s_mods[i].problem[0] ? "CANNOT"
                                : g_arcadeOpts && set_on(g_arcadeOpts, &s_mods[i]) ? "ON" : "OFF");
    }
    entry_at(ModsMenu_note)->fg = (uintptr_t)s_note;
}

static void note(const char *text) { upper(s_note, sizeof s_note, text); }

/* ---- handlers ---- */

static void nav(int32_t ordinal, int32_t up, int32_t down)
{
    MenuEntry *e = entry_at(ordinal);
    e->upEntry = (int8_t)up;
    e->downEntry = (int8_t)down;
    e->directionUp = e->directionDown = -1;
}

static void modsMenu_OnInit(void)
{
    int32_t i;
    s_count = Mods_Scan(s_mods, kRows);
    for (i = 0; i < s_count; i++)
        nav(ModsMenu_name0 + 2 * i + 1, i ? ModsMenu_name0 + 2 * i - 1 : -1,
            i + 1 < s_count ? ModsMenu_name0 + 2 * i + 3 : ModsMenu_exit);
    nav(ModsMenu_exit, s_count ? ModsMenu_name0 + 2 * s_count - 1 : -1, -1);
    if (s_count)
        note("TURNED ON OR OFF FROM THE NEXT START");
    else
        note("NONE: A MOD IS A FOLDER IN THE MODS FOLDER");
    refresh();
}

static void modsMenu_OnDraw(void)
{
    refresh();
}

static void toggle(int32_t i)
{
    extern PlatformInitOptions *g_arcadeOpts;
    extern const char *g_arcadeIniPath;
    if (i >= s_count || !g_arcadeOpts)
        return;
    ModInfo *m = &s_mods[i];
    if (m->problem[0]) {
        note(m->problem);
        return;
    }
    const int on = !set_on(g_arcadeOpts, m);
    if (m->canonical && m->byDefault) {
        list_set(g_arcadeOpts->mods_disabled, sizeof g_arcadeOpts->mods_disabled, m->slug, !on);
        list_set(g_arcadeOpts->mods, sizeof g_arcadeOpts->mods, m->slug, 0);
    } else {
        list_set(g_arcadeOpts->mods, sizeof g_arcadeOpts->mods, m->slug, on);
    }
    if (g_arcadeIniPath)
        Settings_Save(g_arcadeOpts, g_arcadeIniPath);
    fprintf(stderr, "[mods] %s set %s, from the next start ([mods] enabled = %s; disabled = %s)\n", m->slug,
            on ? "on" : "off", g_arcadeOpts->mods, g_arcadeOpts->mods_disabled);
    note(on && !m->cosmetic ? "ON FROM THE NEXT START: KEEPS THE GAME OFF LINE"
         : on               ? "ON FROM THE NEXT START"
                            : "OFF FROM THE NEXT START");
}

static void modsMenu_row0_OnSelect(void) { toggle(0); }
static void modsMenu_row1_OnSelect(void) { toggle(1); }
static void modsMenu_row2_OnSelect(void) { toggle(2); }
static void modsMenu_row3_OnSelect(void) { toggle(3); }
static void modsMenu_row4_OnSelect(void) { toggle(4); }
static void modsMenu_row5_OnSelect(void) { toggle(5); }
static void modsMenu_row6_OnSelect(void) { toggle(6); }
static void modsMenu_row7_OnSelect(void) { toggle(7); }

static void modsMenu_exit_OnSelect(void)
{
    SetExitMenuFlag();
}

/* ---- the menu ---- */

#define HANDLER(fn) { #fn, (void *)fn, SYM_FUNC }

static const MenuSymbol k_handlers[] = {
    HANDLER(modsMenu_OnInit),
    HANDLER(modsMenu_OnDraw),
    HANDLER(modsMenu_row0_OnSelect),
    HANDLER(modsMenu_row1_OnSelect),
    HANDLER(modsMenu_row2_OnSelect),
    HANDLER(modsMenu_row3_OnSelect),
    HANDLER(modsMenu_row4_OnSelect),
    HANDLER(modsMenu_row5_OnSelect),
    HANDLER(modsMenu_row6_OnSelect),
    HANDLER(modsMenu_row7_OnSelect),
    HANDLER(modsMenu_exit_OnSelect),
};

#undef HANDLER

void ModsMenu_Init(void)
{
    static int32_t registered;

    if (registered)
        return;
    registered = 1;
    MenuJson_RegisterSymbols(k_handlers, (int)(sizeof k_handlers / sizeof k_handlers[0]));
}

void ModsMenu_Show(void)
{
    uint8_t *menu;

    ModsMenu_Init();
    menu = MenuJson_Load(MODS_MENU_NAME);
    if (!menu) {
        fprintf(stderr, "[mods-menu] cannot show %s\n", MODS_MENU_NAME);
        return;
    }
    ShowMenu(menu);
}

#endif /* SWOS_SDL_BUILD */
