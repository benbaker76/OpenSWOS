/*
 * menu_extras.c -- what a JSON menu says beyond the packed menu format (docs/MENUS.md §13): a
 * line describing the selected entry, and images drawn in an entry's box. SDL builds.
 *
 *   "description_entry": N        (the menu) its text entry N shows the selected entry's line
 *   "ticker_entry": N             (the menu) the box a ticker mod scrolls its news in
 *   "description": "..."          (an entry) that line
 *   "image": "<path>"             (an entry) a .bmp under the data folder (a mod's overlay first),
 *                                 drawn in the entry's box, centred, colours to the nearest of the
 *                                 menu palette; its colour 0 / magenta (255, 0, 255) transparent
 *
 * The packed format has no room for them, so the loader (menu_load_json.c) gives them here as it
 * builds, filed under the menu's final packed bytes: the pointer ShowMenu is given and
 * _currentMenuPtr holds. Only the picture: nothing here is match or menu state.
 */
#ifdef SWOS_SDL_BUILD

#include "display.h"
#include "menu_system.h"
#include "platform_sdl.h"
#include "swos_io.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { kMaxMenus = 64, kMaxEntries = 128 };

typedef struct {
    char    *description;
    char    *imagePath;
    uint8_t *pixels;            /* the image as palette indices, 0xFF transparent; NULL until drawn */
    int32_t  w, h, failed;
} Extra;

typedef struct {
    const uint8_t *menu;        /* the packed bytes (its key) */
    int32_t descEntry;
    int32_t tickerEntry;        /* the box a ticker mod scrolls its news in (-1 none) */
    Extra   entry[kMaxEntries];
} MenuExtras;

static MenuExtras s_menus[kMaxMenus];
static int32_t    s_count;
static MenuExtras s_pending;    /* the one being built */
static int32_t    s_pendingAny;

/* ---- from the loader ---- */

void MenuExtras_Begin(void)
{
    for (int32_t i = 0; i < kMaxEntries; i++) {
        free(s_pending.entry[i].description);
        free(s_pending.entry[i].imagePath);
    }
    memset(&s_pending, 0, sizeof s_pending);
    s_pending.descEntry = -1;
    s_pending.tickerEntry = -1;
    s_pendingAny = 0;
}

void MenuExtras_TickerEntryIs(int32_t entry)
{
    s_pending.tickerEntry = entry;
    s_pendingAny = 1;
}

void MenuExtras_DescriptionEntry(int32_t entry)
{
    s_pending.descEntry = entry;
    s_pendingAny = 1;
}

void MenuExtras_Entry(int32_t ordinal, const char *description, const char *image)
{
    if (ordinal < 0 || ordinal >= kMaxEntries)
        return;
    if (description && description[0]) {
        s_pending.entry[ordinal].description = strdup(description);
        s_pendingAny = 1;
    }
    if (image && image[0]) {
        s_pending.entry[ordinal].imagePath = strdup(image);
        s_pendingAny = 1;
    }
}

/* The menu was built into `menu` (its final place): what was pending is its. */
void MenuExtras_Commit(const uint8_t *menu)
{
    MenuExtras *m = NULL;
    if (!s_pendingAny)
        return;
    for (int32_t i = 0; i < s_count; i++)
        if (s_menus[i].menu == menu)
            m = &s_menus[i];
    if (!m) {
        if (s_count == kMaxMenus) {
            fprintf(stderr, "[menu-extras] more than %d menus with descriptions or images\n", kMaxMenus);
            return;
        }
        m = &s_menus[s_count++];
    } else {
        for (int32_t i = 0; i < kMaxEntries; i++) {
            free(m->entry[i].description);
            free(m->entry[i].imagePath);
            free(m->entry[i].pixels);
        }
    }
    *m = s_pending;
    m->menu = menu;
    memset(&s_pending, 0, sizeof s_pending);    /* (its strings are m's now) */
    s_pending.descEntry = -1;
    s_pending.tickerEntry = -1;
    s_pendingAny = 0;
}

/* ---- at draw time ---- */

extern uintptr_t _currentMenuPtr;

static MenuExtras *current(void)
{
    for (int32_t i = 0; i < s_count; i++)
        if ((uintptr_t)s_menus[i].menu == _currentMenuPtr)
            return &s_menus[i];
    return NULL;
}

/* The current menu's ticker box, -1 none (ModApi_MenuTickerEntry). */
int32_t MenuExtras_TickerEntry(void)
{
    const MenuExtras *m = current();
    return m ? m->tickerEntry : -1;
}

/* DrawMenu, before the entries: the description line takes the selected entry's text. */
void MenuExtras_BeforeDraw(void)
{
    extern uint8_t _currentMenu[];
    MenuExtras *m = current();
    if (!m || m->descEntry < 0)
        return;
    const Menu *menu = (const Menu *)(void *)_currentMenu;
    const MenuEntry *sel = menu ? menu->selectedEntry : NULL;
    MenuEntry *line = CalcMenuEntryAddress((uint16_t)m->descEntry);
    static char empty[1];
    if (!line)
        return;
    const char *text = sel && sel->ordinal < kMaxEntries ? m->entry[sel->ordinal].description : NULL;
    line->fg = (uintptr_t)(text ? text : empty);
}

static void load_image(Extra *e)
{
    char path[1280];
    SDL_Surface *raw, *rgb;
    e->failed = 1;
    raw = SDL_LoadBMP(swos_data_path(e->imagePath, path, sizeof path));
    if (!raw) {
        fprintf(stderr, "[menu-extras] %s: %s\n", e->imagePath, SDL_GetError());
        return;
    }
    rgb = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_RGB888, 0);
    SDL_FreeSurface(raw);
    if (!rgb)
        return;
    e->w = rgb->w;
    e->h = rgb->h;
    e->pixels = (uint8_t *)malloc((size_t)e->w * (size_t)e->h);
    if (e->pixels) {
        SDL_LockSurface(rgb);
        for (int32_t y = 0; y < e->h; y++) {
            const uint32_t *row = (const uint32_t *)((const uint8_t *)rgb->pixels + (size_t)y * (size_t)rgb->pitch);
            for (int32_t x = 0; x < e->w; x++) {
                const uint8_t r = (uint8_t)(row[x] >> 16), g = (uint8_t)(row[x] >> 8), b = (uint8_t)row[x];
                e->pixels[y * e->w + x] = (r == 255 && g == 0 && b == 255) || (r | g | b) == 0
                                              ? 0xFF : Platform_NearestColour(r, g, b);
            }
        }
        SDL_UnlockSurface(rgb);
        e->failed = 0;
    }
    SDL_FreeSurface(rgb);
}

/* DrawMenu, after the entries: each visible entry's image in its box. */
void MenuExtras_Drawn(void)
{
    extern volatile uintptr_t _frontbufferBasePtr;
    MenuExtras *m = current();
    if (!m)
        return;
    uint8_t *frame = (uint8_t *)(uintptr_t)_frontbufferBasePtr;
    const int32_t sw = _screen_width, sh = _screen_height;
    for (int32_t i = 0; i < kMaxEntries && frame; i++) {
        Extra *e = &m->entry[i];
        if (!e->imagePath)
            continue;
        const MenuEntry *ent = CalcMenuEntryAddress((uint16_t)i);
        if (!ent || ent->isInvisible)
            continue;
        if (!e->pixels && !e->failed)
            load_image(e);
        if (!e->pixels)
            continue;
        const int32_t x0 = ent->x + _menu_draw_x_offset + ((int32_t)ent->dwWidth - e->w) / 2;
        const int32_t y0 = ent->y + ((int32_t)ent->dwHeight - e->h) / 2;
        for (int32_t y = 0; y < e->h; y++) {
            const int32_t fy = y0 + y;
            if (fy < 0 || fy >= sh)
                continue;
            for (int32_t x = 0; x < e->w; x++) {
                const int32_t fx = x0 + x;
                const uint8_t c = e->pixels[y * e->w + x];
                if (c != 0xFF && fx >= 0 && fx < sw)
                    frame[(size_t)fy * FRONTBUFFER_ROW_STRIDE_BYTES + (size_t)fx] = c;
            }
        }
    }
}

#endif /* SWOS_SDL_BUILD */
