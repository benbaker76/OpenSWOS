/*
 * menu_extras.c -- what a JSON menu says beyond the packed menu format (docs/MENUS.md §13): a
 * line describing the selected entry, and images drawn in an entry's box. SDL builds.
 *
 *   "description_entry": N        (the menu) its text entry N shows the selected entry's line
 *   "ticker_entry": N             (the menu) the box a ticker mod scrolls its news in
 *   "description": "..."          (an entry) that line
 *   "image": "<path>"             (an entry) a .bmp under the data folder (a mod's overlay first),
 *                                 drawn in the entry's box -- centred, or at "image_x" / "image_y"
 *                                 from its top left -- each colour the nearest in the palette on
 *                                 screen (mapped again when it changes: a menu fades in); black /
 *                                 magenta (255, 0, 255) transparent. Drawn every menu frame.
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
    int32_t  imageX, imageY;    /* its place in the box (-1: centred) */
    uint32_t *rgb;              /* the image, 0xRRGGBB, 0xFF000000 transparent; NULL until drawn */
    uint8_t *pixels;            /* ... as palette indices (0xFF transparent), for `paletteHash` */
    uint32_t paletteHash;
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

void MenuExtras_Entry(int32_t ordinal, const char *description, const char *image, int32_t imageX,
                      int32_t imageY)
{
    if (ordinal < 0 || ordinal >= kMaxEntries)
        return;
    s_pending.entry[ordinal].imageX = imageX;
    s_pending.entry[ordinal].imageY = imageY;
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
            free(m->entry[i].rgb);
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

/* An entry's image, changed while its menu is up (ModApi_SetMenuImage): loaded at its next draw. */
void ModApi_SetMenuImage(int32_t entry, const char *path)
{
    MenuExtras *m;
    extern uintptr_t _currentMenuPtr;
    if (entry < 0 || entry >= kMaxEntries)
        return;
    m = current();
    if (!m) {
        if (s_count == kMaxMenus)
            return;
        m = &s_menus[s_count++];
        memset(m, 0, sizeof *m);
        m->menu = (const uint8_t *)_currentMenuPtr;
        m->descEntry = m->tickerEntry = -1;
        for (int32_t i = 0; i < kMaxEntries; i++)
            m->entry[i].imageX = m->entry[i].imageY = -1;
    }
    Extra *e = &m->entry[entry];
    if (e->imagePath && path && strcmp(e->imagePath, path) == 0)
        return;
    free(e->imagePath);
    free(e->rgb);
    free(e->pixels);
    e->imagePath = path ? strdup(path) : NULL;
    e->rgb = NULL;
    e->pixels = NULL;
    e->failed = 0;
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
    e->rgb = (uint32_t *)malloc((size_t)e->w * (size_t)e->h * sizeof *e->rgb);
    e->pixels = (uint8_t *)malloc((size_t)e->w * (size_t)e->h);
    if (e->rgb && e->pixels) {
        SDL_LockSurface(rgb);
        for (int32_t y = 0; y < e->h; y++) {
            const uint32_t *row = (const uint32_t *)((const uint8_t *)rgb->pixels + (size_t)y * (size_t)rgb->pitch);
            for (int32_t x = 0; x < e->w; x++) {
                const uint32_t c = row[x] & 0xFFFFFFu;
                e->rgb[y * e->w + x] = (c == 0xFF00FFu || c == 0) ? 0xFF000000u : c;
            }
        }
        SDL_UnlockSurface(rgb);
        e->paletteHash = 0;             /* (mapped at its first draw) */
        e->failed = 0;
    }
    SDL_FreeSurface(rgb);
}

/* The palette on screen, as a number: an image is mapped again when it changes (a menu fades
 * in from black, so its first frames' palette is not the menu's). */
static uint32_t palette_hash(void)
{
    uint32_t h = 2166136261u;
    for (int32_t i = 0; i < 256; i++) {
        uint8_t r, g, b;
        Platform_PaletteColour((uint8_t)i, &r, &g, &b);
        h = (h ^ r) * 16777619u;
        h = (h ^ g) * 16777619u;
        h = (h ^ b) * 16777619u;
    }
    return h | 1u;
}

static void map_image(Extra *e, uint32_t hash)
{
    for (int32_t i = 0; i < e->w * e->h; i++) {
        const uint32_t c = e->rgb[i];
        e->pixels[i] = c == 0xFF000000u ? 0xFF
                     : Platform_NearestColour((uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
    }
    e->paletteHash = hash;
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
    uint32_t hash = 0;
    for (int32_t i = 0; i < kMaxEntries && frame; i++) {
        Extra *e = &m->entry[i];
        if (!e->imagePath)
            continue;
        const MenuEntry *ent = CalcMenuEntryAddress((uint16_t)i);
        if (!ent || ent->isInvisible)
            continue;
        if (!e->rgb && !e->failed)
            load_image(e);
        if (!e->rgb || !e->pixels)
            continue;
        if (!hash)
            hash = palette_hash();
        if (e->paletteHash != hash)
            map_image(e, hash);
        const int32_t x0 = ent->x + _menu_draw_x_offset +
                           (e->imageX >= 0 ? e->imageX : ((int32_t)ent->dwWidth - e->w) / 2);
        const int32_t y0 = ent->y + (e->imageY >= 0 ? e->imageY : ((int32_t)ent->dwHeight - e->h) / 2);
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
