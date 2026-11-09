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
#include "swos_io.h"     /* swos_user_dir: choices_from */
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
    ModsMenu_scrollUp,                  /* SWOS's scroll arrows (sprites 183/184, as DIY's), shown */
    ModsMenu_scrollDown,                /* only when there are more mods than rows */
};
enum { kRows = 8, kMaxMods = 64 };

extern void SetExitMenuFlag(void);

static ModInfo s_mods[kMaxMods];      /* every mod on disk ... */
static int32_t s_count;
static int32_t s_top;                  /* ... row 0 showing s_mods[s_top] */
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
        const ModInfo *m = &s_mods[s_top + i];
        name->isInvisible = state->isInvisible = s_top + i >= s_count;
        if (s_top + i >= s_count)
            continue;
        char text[64];
        /* (a downloaded mod says so; the game's own -- most of the list -- need not) */
        snprintf(text, sizeof text, "%s %s%s", m->name, m->version, m->canonical ? "" : " (DLC)");
        upper(s_nameText[i], sizeof s_nameText[i], text);
        name->fg = (uintptr_t)s_nameText[i];
        state->fg = (uintptr_t)(m->problem[0] ? "CANNOT"
                                : g_arcadeOpts && set_on(g_arcadeOpts, m) ? "ON" : "OFF");
    }
    entry_at(ModsMenu_scrollUp)->isInvisible = entry_at(ModsMenu_scrollDown)->isInvisible = s_count <= kRows;
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
    s_count = Mods_Scan(s_mods, kMaxMods);
    const int32_t rows = s_count < kRows ? s_count : kRows, scroll = s_count > kRows;
    if (s_top > s_count - rows)         /* (back from a mod's page: the same place) */
        s_top = s_count - rows;
    if (s_top < 0)
        s_top = 0;
    for (i = 0; i < rows; i++) {
        const int32_t name = ModsMenu_name0 + 2 * i, state = name + 1;
        nav(state, i ? state - 2 : -1, i + 1 < rows ? state + 2 : ModsMenu_exit);
        nav(name, i ? name - 2 : -1, i + 1 < rows ? name + 2 : ModsMenu_exit);
        nav_lr(name, -1, state);
        nav_lr(state, name, -1);
    }
    if (scroll) {   /* the arrows: right of the first and the last row's ON/OFF */
        nav_lr(ModsMenu_name0 + 1, ModsMenu_name0, ModsMenu_scrollUp);
        nav_lr(ModsMenu_name0 + 2 * (kRows - 1) + 1, ModsMenu_name0 + 2 * (kRows - 1), ModsMenu_scrollDown);
        nav(ModsMenu_scrollUp, -1, ModsMenu_scrollDown);
        nav_lr(ModsMenu_scrollUp, ModsMenu_name0 + 1, -1);
        nav(ModsMenu_scrollDown, ModsMenu_scrollUp, ModsMenu_exit);
        nav_lr(ModsMenu_scrollDown, ModsMenu_name0 + 2 * (kRows - 1) + 1, -1);
    }
    nav(ModsMenu_exit, rows ? ModsMenu_name0 + 2 * rows - 1 : -1, -1);
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
    i += s_top;
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

static void scroll(int32_t by)
{
    const int32_t top = s_top + by, last = s_count - kRows;
    s_top = top < 0 ? 0 : top > last ? (last > 0 ? last : 0) : top;
    refresh();
}

static void modsMenu_scrollUp_OnSelect(void) { scroll(-1); }
static void modsMenu_scrollDown_OnSelect(void) { scroll(1); }

/* ---- a mod's page: its settings ---- */

enum {
    ModPage_header,
    ModPage_subTitle,
    ModPage_label0,                     /* label<i> = label0 + 2 i, value<i> = label0 + 2 i + 1 */
    ModPage_note = ModPage_label0 + 2 * 8,
    ModPage_exit,
    ModPage_author,                     /* (appended: the entries before keep their numbers) */
};

/* A setting's choices: its own list ("choices"), or the files of a folder ("choices_from") -- a
 * gallery mod's pictures can be many, so up to kMaxChoices, named up to 63 characters; more than
 * a page's rows and the setting opens a scrolling list (openswos/choiceMenu) instead of stepping. */
enum { kMaxChoices = 256, kChoiceLen = 64 };
typedef struct {
    char key[32], label[40];
    char choices[kMaxChoices][kChoiceLen];
    int32_t choiceCount;
} PageSetting;

static ModInfo     s_page;
static PageSetting s_pageSettings[kRows];
static int32_t     s_pageCount;
static char        s_pageName[48], s_pageAbout[64], s_pageAuthor[64], s_pageLabels[kRows][40], s_pageValues[kRows][40];

/* "choices_from": "<folder>": "off", then the image files (.png .bmp .jpg) in that folder of the
 * mod, of every enabled mod (a gallery DLC's images/: docs/MODS.md §25b) and of the player's game
 * folder (swos_user_dir(); "user_folder" when its name is another), each name once, up to
 * kMaxChoices. */
typedef struct { PageSetting *ps; const char *ext; } ChoicesCtx;
static int32_t choices_add(const char *name, void *ctx)
{
    PageSetting *ps = ((ChoicesCtx *)ctx)->ps;
    const char *dot = strrchr(name, '.');
    if (ps->choiceCount >= kMaxChoices)
        return 1;
    const char *ext = ((ChoicesCtx *)ctx)->ext;
    if (!dot || strlen(name) >= sizeof ps->choices[0])
        return 0;
    if (ext[0] ? SDL_strcasecmp(dot, ext) != 0 :
        !(SDL_strcasecmp(dot, ".png") == 0 || SDL_strcasecmp(dot, ".bmp") == 0 ||
          SDL_strcasecmp(dot, ".jpg") == 0 || SDL_strcasecmp(dot, ".jpeg") == 0))
        return 0;
    for (int32_t c = 0; c < ps->choiceCount; c++)
        if (SDL_strcasecmp(ps->choices[c], name) == 0)
            return 0;
    snprintf(ps->choices[ps->choiceCount++], sizeof ps->choices[0], "%s", name);
    return 0;
}

static int choice_cmp(const void *a, const void *b) { return SDL_strcasecmp((const char *)a, (const char *)b); }

static void choices_from_files(const ModInfo *m, const char *folder, const char *userFolder, const char *ext,
                               PageSetting *ps)
{
    char dir[1100];
    ChoicesCtx ctx = { ps, ext };
    snprintf(ps->choices[0], sizeof ps->choices[0], "off");
    ps->choiceCount = 1;
    snprintf(dir, sizeof dir, "%s/%s", ModApi_ModDir(m->slug), folder);
    ModApi_ListFiles(dir, choices_add, &ctx);
    for (int32_t i = 0; i < Mods_Count(); i++)          /* (the other enabled mods' same folder) */
        if (strcmp(Mods_Id(i), m->slug) != 0) {
            snprintf(dir, sizeof dir, "%s/%s", Mods_Dir(i), folder);
            ModApi_ListFiles(dir, choices_add, &ctx);
        }
    snprintf(dir, sizeof dir, "%s/%s", swos_user_dir(), userFolder[0] ? userFolder : folder);
    ModApi_ListFiles(dir, choices_add, &ctx);
    if (ps->choiceCount > 2)                            /* (by name, "off" first: a gallery's years in order) */
        SDL_qsort(ps->choices[1], (size_t)(ps->choiceCount - 1), sizeof ps->choices[0], choice_cmp);
}

/* mod.json's "settings": [{ "key", "label", "choices": [..] or "choices_from": "<folder>",
 * "default" }] */
static void read_page_settings(const ModInfo *m)
{
    char path[1024], about[256] = "";
    s_pageCount = 0;
    s_pageAbout[0] = '\0';
    s_pageAuthor[0] = '\0';
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
    {   /* "author": BY ..., one line: its asides in brackets left out (the licence, a PR, where an
         * effect is from: in the mod's files), and ; / as the menu font has them */
        char author[256], line[256];
        if (mjson_get_string(json, len, "$.author", author, (int)sizeof author) >= 0) {
            size_t n = 0;
            int depth = 0;
            for (const char *p = author; *p && n + 1 < sizeof line; p++) {
                if (*p == '(') { depth++; continue; }
                if (*p == ')') { if (depth) depth--; continue; }
                if (depth)
                    continue;
                char c = *p == ';' ? ',' : *p == '/' ? '-' : *p;
                if (c == ' ' && (n == 0 || line[n - 1] == ' '))
                    continue;           /* (one space where an aside was) */
                if ((c == ',') && n && line[n - 1] == ' ')
                    n--;                /* ("x (y);" -> "x,") */
                line[n++] = c;
            }
            while (n && (line[n - 1] == ' ' || line[n - 1] == ','))
                n--;
            line[n] = '\0';
            char by[300];
            enum { kLine = 46 };
            snprintf(by, sizeof by, "BY %s", line);
            if (strlen(by) > kLine) {
                int cut = kLine - 3;
                while (cut > 0 && by[cut] != ' ')
                    cut--;
                snprintf(by + (cut > 0 ? cut : kLine - 3), 4, "...");
            }
            upper(s_pageAuthor, sizeof s_pageAuthor, by);
        }
    }
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
        for (int32_t c = 0; c < kMaxChoices; c++) {
            snprintf(q, sizeof q, "$.settings[%d].choices[%d]", (int)i, (int)c);
            if (mjson_get_string(json, len, q, ps->choices[c], (int)sizeof ps->choices[c]) < 0)
                break;
            ps->choiceCount++;
        }
        snprintf(q, sizeof q, "$.settings[%d].choices_from", (int)i);
        if (!ps->choiceCount && mjson_get_string(json, len, q, t, (int)sizeof t) > 0) {
            char user[64] = "", ext[16] = "";   /* (the player's folder's name, when not the same) */
            snprintf(q, sizeof q, "$.settings[%d].user_folder", (int)i);
            mjson_get_string(json, len, q, user, (int)sizeof user);
            snprintf(q, sizeof q, "$.settings[%d].choices_ext", (int)i);   /* (".slangp"; none: images) */
            mjson_get_string(json, len, q, ext, (int)sizeof ext);
            choices_from_files(m, t, user, ext, ps);    /* "off" and the files of a folder */
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
                    char first[kChoiceLen];
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
    entry_at(ModPage_author)->fg = (uintptr_t)s_pageAuthor;
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

/* ---- a setting with more choices than rows: a scrolling list (openswos/choiceMenu) ---- */

enum {
    Choice_header,
    Choice_row0,                        /* row<i> = row0 + i */
    Choice_note = Choice_row0 + kRows,
    Choice_exit,
    Choice_scrollUp,
    Choice_scrollDown,
};

static int32_t s_pick;                  /* the setting being chosen */
static int32_t s_pickTop;               /* row 0 showing its choice s_pickTop */
static char    s_pickRows[kRows][kChoiceLen], s_pickHeader[48];

static void pick_refresh(void)
{
    const PageSetting *ps = &s_pageSettings[s_pick];
    const char *now = ModApi_Setting(s_page.slug, ps->key, ps->choices[0]);
    entry_at(Choice_header)->fg = (uintptr_t)s_pickHeader;
    for (int32_t i = 0; i < kRows; i++) {
        MenuEntry *row = entry_at(Choice_row0 + i);
        const int32_t c = s_pickTop + i;
        row->isInvisible = c >= ps->choiceCount;
        if (c >= ps->choiceCount)
            continue;
        char text[kChoiceLen + 4];
        snprintf(text, sizeof text, "%s%s", SDL_strcasecmp(ps->choices[c], now) == 0 ? "> " : "", ps->choices[c]);
        upper(s_pickRows[i], sizeof s_pickRows[i], text);
        row->fg = (uintptr_t)s_pickRows[i];
    }
    entry_at(Choice_scrollUp)->isInvisible = entry_at(Choice_scrollDown)->isInvisible = ps->choiceCount <= kRows;
    entry_at(Choice_note)->fg = (uintptr_t)s_note;
}

static void choiceMenu_OnInit(void)
{
    const PageSetting *ps = &s_pageSettings[s_pick];
    const int32_t rows = ps->choiceCount < kRows ? ps->choiceCount : kRows;
    const char *now = ModApi_Setting(s_page.slug, ps->key, ps->choices[0]);
    snprintf(s_pickHeader, sizeof s_pickHeader, "%s", ps->label);
    s_pickTop = 0;                      /* (the current choice in view) */
    for (int32_t c = 0; c < ps->choiceCount; c++)
        if (SDL_strcasecmp(ps->choices[c], now) == 0)
            s_pickTop = c - c % kRows;
    if (s_pickTop > ps->choiceCount - rows)
        s_pickTop = ps->choiceCount - rows;
    for (int32_t i = 0; i < rows; i++) {
        nav(Choice_row0 + i, i ? Choice_row0 + i - 1 : -1, i + 1 < rows ? Choice_row0 + i + 1 : Choice_exit);
        nav_lr(Choice_row0 + i, -1, ps->choiceCount > kRows ? (i < kRows / 2 ? Choice_scrollUp : Choice_scrollDown) : -1);
    }
    if (ps->choiceCount > kRows) {
        nav(Choice_scrollUp, -1, Choice_scrollDown);
        nav_lr(Choice_scrollUp, Choice_row0, -1);
        nav(Choice_scrollDown, Choice_scrollUp, Choice_exit);
        nav_lr(Choice_scrollDown, Choice_row0 + kRows - 1, -1);
    }
    nav(Choice_exit, rows ? Choice_row0 + rows - 1 : -1, -1);
    note("FIRE CHOOSES; THE ARROWS SCROLL");
    pick_refresh();
}

static void choiceMenu_OnDraw(void) { pick_refresh(); }

static void pick_row(int32_t i)
{
    const PageSetting *ps = &s_pageSettings[s_pick];
    const int32_t c = s_pickTop + i;
    if (c >= ps->choiceCount)
        return;
    ModApi_SetSetting(s_page.slug, ps->key, ps->choices[c]);
    fprintf(stderr, "[mods] %s: %s = %s\n", s_page.slug, ps->key, ps->choices[c]);
    note("CHANGED: SWOS.INI SAYS SO");
    SetExitMenuFlag();
}

static void pick_scroll(int32_t by)
{
    const int32_t n = s_pageSettings[s_pick].choiceCount, last = n - kRows;
    const int32_t top = s_pickTop + by * kRows;  /* (a page at a time: a gallery can be long) */
    s_pickTop = top < 0 ? 0 : top > last ? (last > 0 ? last : 0) : top;
    pick_refresh();
}

static void choiceMenu_row0_OnSelect(void) { pick_row(0); }
static void choiceMenu_row1_OnSelect(void) { pick_row(1); }
static void choiceMenu_row2_OnSelect(void) { pick_row(2); }
static void choiceMenu_row3_OnSelect(void) { pick_row(3); }
static void choiceMenu_row4_OnSelect(void) { pick_row(4); }
static void choiceMenu_row5_OnSelect(void) { pick_row(5); }
static void choiceMenu_row6_OnSelect(void) { pick_row(6); }
static void choiceMenu_row7_OnSelect(void) { pick_row(7); }
static void choiceMenu_scrollUp_OnSelect(void) { pick_scroll(-1); }
static void choiceMenu_scrollDown_OnSelect(void) { pick_scroll(1); }
static void choiceMenu_exit_OnSelect(void) { SetExitMenuFlag(); }

static void page_cycle(int32_t i)
{
    if (i >= s_pageCount)
        return;
    const PageSetting *ps = &s_pageSettings[i];
    if (ps->choiceCount > kRows) {      /* (a long list: chosen from a scrolling one) */
        uint8_t *menu = MenuJson_Load(CHOICE_MENU_NAME);
        if (!menu) {
            fprintf(stderr, "[mods-menu] cannot show %s\n", CHOICE_MENU_NAME);
            return;
        }
        s_pick = i;
        ShowMenu(menu);
        return;
    }
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
    i += s_top;
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

int32_t ModsMenu_ShowChoices(const char *slug, const char *key)
{
    ModInfo all[kMaxMods];
    const int32_t n = Mods_Scan(all, kMaxMods);
    ModsMenu_Init();
    for (int32_t i = 0; i < n; i++) {
        if (strcmp(all[i].slug, slug) != 0)
            continue;
        s_page = all[i];
        read_page_settings(&s_page);
        for (int32_t k = 0; k < s_pageCount; k++)
            if (strcmp(s_pageSettings[k].key, key) == 0) {
                uint8_t *menu = MenuJson_Load(CHOICE_MENU_NAME);
                if (!menu)
                    return 0;
                s_pick = k;
                fprintf(stderr, "[mods-menu] %s: %s, %d choices\n", slug, key, (int)s_pageSettings[k].choiceCount);
                ShowMenu(menu);
                return 1;
            }
    }
    fprintf(stderr, "[mods-menu] --menu: no mod %s with a setting %s\n", slug, key);
    return 0;
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
    HANDLER(modsMenu_scrollUp_OnSelect),
    HANDLER(modsMenu_scrollDown_OnSelect),
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
    HANDLER(choiceMenu_OnInit),
    HANDLER(choiceMenu_OnDraw),
    HANDLER(choiceMenu_row0_OnSelect),
    HANDLER(choiceMenu_row1_OnSelect),
    HANDLER(choiceMenu_row2_OnSelect),
    HANDLER(choiceMenu_row3_OnSelect),
    HANDLER(choiceMenu_row4_OnSelect),
    HANDLER(choiceMenu_row5_OnSelect),
    HANDLER(choiceMenu_row6_OnSelect),
    HANDLER(choiceMenu_row7_OnSelect),
    HANDLER(choiceMenu_scrollUp_OnSelect),
    HANDLER(choiceMenu_scrollDown_OnSelect),
    HANDLER(choiceMenu_exit_OnSelect),
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
    s_top = 0;
    menu = MenuJson_Load(MODS_MENU_NAME);
    if (!menu) {
        fprintf(stderr, "[mods-menu] cannot show %s\n", MODS_MENU_NAME);
        return;
    }
    ShowMenu(menu);
}

#endif /* SWOS_SDL_BUILD */
