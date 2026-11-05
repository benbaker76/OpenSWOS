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
#include "mjson.h"
#include "mod_api.h"
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

static void nav_lr(int32_t ordinal, int32_t left, int32_t right)
{
    MenuEntry *e = entry_at(ordinal);
    e->leftEntry = (int8_t)left;
    e->rightEntry = (int8_t)right;
    e->directionLeft = e->directionRight = -1;
}

static void modsMenu_OnInit(void)
{
    int32_t i;
    s_count = Mods_Scan(s_mods, kRows);
    for (i = 0; i < s_count; i++) {
        const int32_t name = ModsMenu_name0 + 2 * i, state = name + 1;
        nav(state, i ? state - 2 : -1, i + 1 < s_count ? state + 2 : ModsMenu_exit);
        nav(name, i ? name - 2 : -1, i + 1 < s_count ? name + 2 : ModsMenu_exit);
        nav_lr(name, -1, state);
        nav_lr(state, name, -1);
    }
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
    note(on && !m->cosmetic ? "ON FROM THE NEXT START: ONLINE, SAME MODS ONLY"
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

/* ---- a mod's page: its settings ---- */

enum {
    ModPage_header,
    ModPage_subTitle,
    ModPage_label0,                     /* label<i> = label0 + 2 i, value<i> = label0 + 2 i + 1 */
    ModPage_note = ModPage_label0 + 2 * 8,
    ModPage_exit,
};

typedef struct {
    char key[32], label[40];
    char choices[8][24];
    int32_t choiceCount;
} PageSetting;

static ModInfo     s_page;
static PageSetting s_pageSettings[kRows];
static int32_t     s_pageCount;
static char        s_pageName[48], s_pageAbout[64], s_pageLabels[kRows][40], s_pageValues[kRows][24];

/* mod.json's "settings": [{ "key", "label", "choices": [..], "default" }] */
static void read_page_settings(const ModInfo *m)
{
    char path[1024], about[256] = "";
    s_pageCount = 0;
    s_pageAbout[0] = '\0';
    if (m->canonical)
        snprintf(path, sizeof path, "%s/%s/mod.json", MODS_CANONICAL_DIR, m->slug);
    else
        snprintf(path, sizeof path, "%s/%s/mod.json", Mods_Root(), m->slug);
    FILE *f = fopen(path, "rb");
    if (!f)
        return;
    static char json[16384];
    const int len = (int)fread(json, 1, sizeof json - 1, f);
    fclose(f);
    json[len > 0 ? len : 0] = '\0';
    if (mjson_get_string(json, len, "$.about", about, (int)sizeof about) >= 0) {
        /* one line of the screen: cut at a word, with "..." when there is more */
        enum { kLine = 46 };
        if (strlen(about) > kLine) {
            int32_t cut = kLine - 3;
            while (cut > 0 && about[cut] != ' ')
                cut--;
            snprintf(about + (cut > 0 ? cut : kLine - 3), 4, "...");
        }
        upper(s_pageAbout, sizeof s_pageAbout, about);
    }
    for (int32_t i = 0; i < kRows; i++) {
        char q[64], t[64];
        PageSetting *ps = &s_pageSettings[s_pageCount];
        snprintf(q, sizeof q, "$.settings[%d].key", (int)i);
        if (mjson_get_string(json, len, q, ps->key, (int)sizeof ps->key) < 0)
            break;
        snprintf(q, sizeof q, "$.settings[%d].label", (int)i);
        if (mjson_get_string(json, len, q, t, (int)sizeof t) < 0)
            snprintf(t, sizeof t, "%s", ps->key);
        upper(ps->label, sizeof ps->label, t);
        ps->choiceCount = 0;
        for (int32_t c = 0; c < 8; c++) {
            snprintf(q, sizeof q, "$.settings[%d].choices[%d]", (int)i, (int)c);
            if (mjson_get_string(json, len, q, ps->choices[c], (int)sizeof ps->choices[c]) < 0)
                break;
            ps->choiceCount++;
        }
        if (!ps->choiceCount) {
            snprintf(ps->choices[0], sizeof ps->choices[0], "off");
            snprintf(ps->choices[1], sizeof ps->choices[1], "on");
            ps->choiceCount = 2;
        }
        snprintf(q, sizeof q, "$.settings[%d].default", (int)i);
        if (mjson_get_string(json, len, q, t, (int)sizeof t) >= 0)     /* (the default goes first) */
            for (int32_t c = 1; c < ps->choiceCount; c++)
                if (SDL_strcasecmp(ps->choices[c], t) == 0) {
                    char first[24];
                    memcpy(first, ps->choices[0], sizeof first);
                    memcpy(ps->choices[0], ps->choices[c], sizeof first);
                    memcpy(ps->choices[c], first, sizeof first);
                }
        s_pageCount++;
    }
}

static void page_refresh(void)
{
    int32_t i;
    upper(s_pageName, sizeof s_pageName, s_page.name);
    entry_at(ModPage_header)->fg = (uintptr_t)s_pageName;
    entry_at(ModPage_subTitle)->fg = (uintptr_t)s_pageAbout;
    for (i = 0; i < kRows; i++) {
        MenuEntry *label = entry_at(ModPage_label0 + 2 * i), *value = entry_at(ModPage_label0 + 2 * i + 1);
        label->isInvisible = value->isInvisible = i >= s_pageCount;
        if (i >= s_pageCount)
            continue;
        snprintf(s_pageLabels[i], sizeof s_pageLabels[i], "%s", s_pageSettings[i].label);
        upper(s_pageValues[i], sizeof s_pageValues[i],
              ModApi_Setting(s_page.slug, s_pageSettings[i].key, s_pageSettings[i].choices[0]));
        label->fg = (uintptr_t)s_pageLabels[i];
        value->fg = (uintptr_t)s_pageValues[i];
    }
    entry_at(ModPage_note)->fg = (uintptr_t)s_note;
}

static void modPageMenu_OnInit(void)
{
    int32_t i;
    for (i = 0; i < s_pageCount; i++) {
        const int32_t value = ModPage_label0 + 2 * i + 1;
        nav(value, i ? value - 2 : -1, i + 1 < s_pageCount ? value + 2 : ModPage_exit);
    }
    nav(ModPage_exit, s_pageCount ? ModPage_label0 + 2 * s_pageCount - 1 : -1, -1);
    if (!s_pageCount)
        SetCurrentEntry(ModPage_exit);          /* (nothing else to stand on) */
    note(s_pageCount ? "FIRE CHANGES A SETTING" : "THIS MOD HAS NO SETTINGS");
    page_refresh();
}

static void modPageMenu_OnDraw(void) { page_refresh(); }

static void page_cycle(int32_t i)
{
    if (i >= s_pageCount)
        return;
    const PageSetting *ps = &s_pageSettings[i];
    const char *now = ModApi_Setting(s_page.slug, ps->key, ps->choices[0]);
    int32_t at = 0;
    for (int32_t c = 0; c < ps->choiceCount; c++)
        if (SDL_strcasecmp(ps->choices[c], now) == 0)
            at = c;
    const char *next = ps->choices[(at + 1) % ps->choiceCount];
    ModApi_SetSetting(s_page.slug, ps->key, next);
    fprintf(stderr, "[mods] %s: %s = %s\n", s_page.slug, ps->key, next);
    note("CHANGED: SWOS.INI SAYS SO");
}

static void modPageMenu_row0_OnSelect(void) { page_cycle(0); }
static void modPageMenu_row1_OnSelect(void) { page_cycle(1); }
static void modPageMenu_row2_OnSelect(void) { page_cycle(2); }
static void modPageMenu_row3_OnSelect(void) { page_cycle(3); }
static void modPageMenu_row4_OnSelect(void) { page_cycle(4); }
static void modPageMenu_row5_OnSelect(void) { page_cycle(5); }
static void modPageMenu_row6_OnSelect(void) { page_cycle(6); }
static void modPageMenu_row7_OnSelect(void) { page_cycle(7); }
static void modPageMenu_exit_OnSelect(void) { SetExitMenuFlag(); }

static void open_page(int32_t i)
{
    if (i >= s_count)
        return;
    s_page = s_mods[i];
    read_page_settings(&s_page);
    uint8_t *menu = MenuJson_Load(MOD_PAGE_MENU_NAME);
    if (!menu) {
        fprintf(stderr, "[mods-menu] cannot show %s\n", MOD_PAGE_MENU_NAME);
        return;
    }
    ShowMenu(menu);
    note("TURNED ON OR OFF FROM THE NEXT START");
}

static void modsMenu_page0_OnSelect(void) { open_page(0); }
static void modsMenu_page1_OnSelect(void) { open_page(1); }
static void modsMenu_page2_OnSelect(void) { open_page(2); }
static void modsMenu_page3_OnSelect(void) { open_page(3); }
static void modsMenu_page4_OnSelect(void) { open_page(4); }
static void modsMenu_page5_OnSelect(void) { open_page(5); }
static void modsMenu_page6_OnSelect(void) { open_page(6); }
static void modsMenu_page7_OnSelect(void) { open_page(7); }

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
    HANDLER(modsMenu_page0_OnSelect),
    HANDLER(modsMenu_page1_OnSelect),
    HANDLER(modsMenu_page2_OnSelect),
    HANDLER(modsMenu_page3_OnSelect),
    HANDLER(modsMenu_page4_OnSelect),
    HANDLER(modsMenu_page5_OnSelect),
    HANDLER(modsMenu_page6_OnSelect),
    HANDLER(modsMenu_page7_OnSelect),
    HANDLER(modPageMenu_OnInit),
    HANDLER(modPageMenu_OnDraw),
    HANDLER(modPageMenu_row0_OnSelect),
    HANDLER(modPageMenu_row1_OnSelect),
    HANDLER(modPageMenu_row2_OnSelect),
    HANDLER(modPageMenu_row3_OnSelect),
    HANDLER(modPageMenu_row4_OnSelect),
    HANDLER(modPageMenu_row5_OnSelect),
    HANDLER(modPageMenu_row6_OnSelect),
    HANDLER(modPageMenu_row7_OnSelect),
    HANDLER(modPageMenu_exit_OnSelect),
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
