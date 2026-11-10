/*
 * mod_menu_tools.c -- what a mod's SWOS 2020 screens need beyond the menu API (include/mod_api.h):
 * typing into an entry (ModApi_MenuInputText), and SWOS 2020's small menu
 * pictures (ModApi_DrawMap):
 * a mod's pixmaps.txt (scripts/extract_swos2020_pixmaps.py: per map a line "name width height",
 * then its rows, one character a pixel -- '1'..'9' 'A'..'F' a palette index, anything else
 * clear), drawn into the 8bpp frame as SWOS 2020's drawMapEx / drawMapExHalf drew them. Both
 * targets. (The practice mod keeps its own copy of this, from before.)
 */
#include "mod_api.h"

#include "input_text.h"
#include "menu_system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { kMaxFiles = 4, kMaxMaps = 8 };

typedef struct { char name[48]; int32_t w, h; const char *rows; } Map;
typedef struct {
    char    slug[24];
    char   *text;                       /* (ModApi_ReadFile's: kept for the run) */
    Map     maps[kMaxMaps];
    int32_t count;
} MapFile;

static MapFile s_files[kMaxFiles];
static int32_t s_numFiles;

static MapFile *file_of(const char *slug)
{
    for (int32_t i = 0; i < s_numFiles; i++)
        if (!strcmp(s_files[i].slug, slug))
            return &s_files[i];
    if (s_numFiles >= kMaxFiles)
        return NULL;
    MapFile *f = &s_files[s_numFiles++];
    memset(f, 0, sizeof *f);
    snprintf(f->slug, sizeof f->slug, "%s", slug);
    const char *dir = ModApi_ModDir(slug);
    if (!dir)
        return f;
    char path[512];
    uint32_t size = 0;
    snprintf(path, sizeof path, "%s/pixmaps.txt", dir);
    f->text = (char *)ModApi_ReadFile(path, &size);
    if (!f->text)
        return f;
    char *p = f->text, *end = f->text + size;
    while (p < end && f->count < kMaxMaps) {
        char *eol = memchr(p, '\n', (size_t)(end - p));
        if (!eol)
            break;
        *eol = '\0';
        if (*p != '#') {
            Map *m = &f->maps[f->count];
            /* "name width height" (no sscanf: the Amiga's libc has none) */
            const char *q = p;
            int32_t n = 0;
            while (*q && *q != ' ' && n < (int32_t)sizeof m->name - 1)
                m->name[n++] = *q++;
            m->name[n] = 0;
            m->w = (int32_t)strtol(q, (char **)&q, 10);
            m->h = (int32_t)strtol(q, NULL, 10);
            if (!n || m->w <= 0 || m->h <= 0)
                break;
            m->rows = eol + 1;
            p = eol + 1 + (size_t)(m->w + 1) * (size_t)m->h;    /* (each row its width and a '\n') */
            if (p > end)
                break;
            f->count++;
            continue;
        }
        p = eol + 1;
    }
    return f;
}

int32_t ModApi_DrawMap(const char *slug, const char *name, int32_t x, int32_t y, int32_t ox, int32_t oy,
                       int32_t w, int32_t h, int32_t half)
{
    int32_t drawn = 0;
    MapFile *f = file_of(slug);
    const Map *m = NULL;
    for (int32_t i = 0; f && i < f->count && !m; i++)
        if (!strcmp(f->maps[i].name, name))
            m = &f->maps[i];
    int32_t stride, sw, sh;
    uint8_t *fb = ModApi_Frame(&stride);
    if (!m || !fb)
        return 0;
    ModApi_ScreenSize(&sw, &sh);
    for (int32_t j = 0; j < h && oy + j < m->h; j++) {
        const char *row = m->rows + (size_t)(oy + j) * (size_t)(m->w + 1);
        for (int32_t i = 0; i < w && ox + i < m->w; i++) {
            const char c = row[ox + i];
            const int32_t v = c >= '1' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0;
            const int32_t px = x + (half ? i / 2 : i), py = y + (half ? j / 2 : j);
            if (v && px >= 0 && py >= 0 && px < sw && py < sh) {
                fb[py * stride + px] = (uint8_t)v;
                drawn++;
            }
        }
    }
    return drawn;
}

int32_t ModApi_DrawMapAtEntry(const char *slug, const char *name, int32_t entry, int32_t dx, int32_t dy,
                              int32_t ox, int32_t oy, int32_t w, int32_t h, int32_t half)
{
    int32_t x = 0, y = 0;
    ModApi_MenuEntryRect(entry, &x, &y, NULL, NULL);
    return ModApi_DrawMap(slug, name, ModApi_MenuMargin() + x + dx, y + dy, ox, oy, w, h, half);
}

/* the game's InputText on the entry, its text `text` (cap > maxLen): 1 typed, 0 left */
int32_t ModApi_MenuInputText(int32_t entry, char *text, int32_t maxLen)
{
    MenuEntry *e = CalcMenuEntryAddress((uint16_t)entry);
    if (!e || maxLen < 1 || maxLen > 98)
        return 0;
    e->fg = (uintptr_t)text;
    return InputText((uint16_t)(maxLen + 1), (uint8_t *)e) == 0;
}
