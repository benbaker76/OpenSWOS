/*
 * mouse_cursors.c -- the mouse pointer over the window: [input] mouse_cursor = system | workbench2 |
 * workbench1 (SWOS 2020's --mouse-cursor, the SWOS United Launcher's "Windows", "Amiga Workbench
 * 2.04" and "Amiga Workbench 1.X"; CONTROLS' MOUSE POINTER). SDL builds.
 *
 * SWOS 2020 (render.cpp) loads cursor<n>.bmp, n the option, as an SDL colour cursor with its hot
 * spot at the top left, and 0 is the system's arrow. Its two pictures are here as pixel maps --
 * '.' clear, a letter one of the picture's colours -- converted from its released src/cursor1.bmp
 * and cursor2.bmp (22 x 22, each Amiga pixel drawn 2 x 2). Its cursor3.bmp the launcher never
 * offered. Shown when the menus show the pointer (menu_mouse.c, Platform_ShowMouseCursor).
 */
#ifdef SWOS_SDL_BUILD

#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* SWOS 2020's cursor1.bmp ("Amiga Workbench 2.04"): 22x22, hot spot 0,0 */
static const char *const k_workbench2[22] = {
    "aabb..................",
    "aabb..................",
    "ccaabbbb..............",
    "ccaabbbb..............",
    "..ccaaaabbbb..........",
    "..ccaaaabbbb..........",
    "..ccaaaaaaaabbbb......",
    "..ccaaaaaaaabbbb......",
    "....ccaaaaaaaaaabbbb..",
    "....ccaaaaaaaaaabbbb..",
    "....ccaaaaaaaaaaaaaa..",
    "....ccaaaaaaaaaaaaaa..",
    "......ccaaaaaabb......",
    "......ccaaaaaabb......",
    "......ccaaaaccaabb....",
    "......ccaaaaccaabb....",
    "........ccaa..ccaabb..",
    "........ccaa..ccaabb..",
    "........ccaa....ccaabb",
    "........ccaa....ccaabb",
    "..................ccaa",
    "..................ccaa",
};
static const uint8_t k_workbench2Colours[][3] = { { 224, 64, 64 }, { 224, 224, 192 }, { 0, 0, 0 } };   /* a, b, c */
/* SWOS 2020's cursor2.bmp ("Amiga Workbench 1.x"): 22x22, hot spot 0,0 */
static const char *const k_workbench1[22] = {
    "aaaaaaaaaaaa..........",
    "aaaaaaaaaaaa..........",
    "aabbbbbbbbbbaa........",
    "aabbbbbbbbbbaa........",
    "aaccccccccbbaa........",
    "aaccccccccbbaa........",
    "aaccccccbbaa..........",
    "aaccccccbbaa..........",
    "aaccccccccbbaa........",
    "aaccccccccbbaa........",
    "aaccccaaccccbbaa......",
    "aaccccaaccccbbaa......",
    "..aaaa..aaccccbbaa....",
    "..aaaa..aaccccbbaa....",
    "..........aaccccbbaa..",
    "..........aaccccbbaa..",
    "............aaccccbbaa",
    "............aaccccbbaa",
    "..............aaccaa..",
    "..............aaccaa..",
    "................aa....",
    "................aa....",
};
static const uint8_t k_workbench1Colours[][3] = { { 0, 0, 0 }, { 240, 192, 160 }, { 208, 32, 32 } };   /* a, b, c */

static SDL_Cursor *s_cursor;
static int32_t     s_style = -1;

static SDL_Cursor *make(const char *const *rows, int32_t h, const uint8_t (*colours)[3])
{
    const int32_t w = (int32_t)strlen(rows[0]);
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_Cursor *c;
    if (!s)
        return NULL;
    for (int32_t y = 0; y < h; y++) {
        uint8_t *p = (uint8_t *)s->pixels + (size_t)y * (size_t)s->pitch;
        for (int32_t x = 0; x < w; x++, p += 4) {
            const char k = rows[y][x];
            if (k < 'a' || k > 'c') {
                p[0] = p[1] = p[2] = p[3] = 0;
                continue;
            }
            p[0] = colours[k - 'a'][0];
            p[1] = colours[k - 'a'][1];
            p[2] = colours[k - 'a'][2];
            p[3] = 255;
        }
    }
    c = SDL_CreateColorCursor(s, 0, 0);
    SDL_FreeSurface(s);
    return c;
}

/* 0 the system's arrow, 1 Workbench 2.04's, 2 Workbench 1.x's. */
void MouseCursors_Apply(int32_t style)
{
    SDL_Cursor *old = s_cursor, *c = NULL;
    if (style < 0 || style > 2)
        style = 0;
    if (style == s_style)
        return;
    if (style == 1)
        c = make(k_workbench2, (int32_t)(sizeof k_workbench2 / sizeof k_workbench2[0]), k_workbench2Colours);
    else if (style == 2)
        c = make(k_workbench1, (int32_t)(sizeof k_workbench1 / sizeof k_workbench1[0]), k_workbench1Colours);
    if (style && !c) {
        fprintf(stderr, "[mouse] the pointer %d: %s -- the system's\n", (int)style, SDL_GetError());
        style = 0;
    }
    if (!c)
        c = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    if (c)
        SDL_SetCursor(c);
    s_cursor = c;
    s_style = style;
    if (old && old != c)
        SDL_FreeCursor(old);
}

int32_t MouseCursors_Style(void) { return s_style < 0 ? 0 : s_style; }

#endif /* SWOS_SDL_BUILD */
