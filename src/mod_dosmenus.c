/*
 * mod_dosmenus.c -- the dosmenus mod (bin/mods/dosmenus; canonical, on by default since 2026-11-08): the menus'
 * buttons shaded as the PC game shades them. SDL only. Needs the PC game's data (grafs_dos/
 * FILL.256, which the setup installs from the player's own copy); without it, nothing changes.
 *
 * HOW THE PC GAME DOES IT (swos.asm DrawEmptyMenuItem): FILL.256 is a 320 x 200 map of shades
 * 0-31 -- a dithered gradient, dark at the bottom left to bright at the top right -- followed by
 * the menus' 256-colour palette, in which each of eight ramps is 32 shades of one colour. A box
 * of menu colour c is drawn pixel by pixel as ramp menuItemColorTable[c] plus the map's shade at
 * that pixel; a box one pixel wide or tall (a frame's line) is drawn flat. So a button's shading
 * depends on where it is on the screen, and the buttons' gradients line up across the menu.
 *
 * HERE: the engine says which boxes it fills (ModDef menuFill, menuDrawBegin when the whole menu
 * is drawn again); each frame the window shows, every pixel of those boxes that still has the box's
 * colour (the text and pictures on it have others) is painted over the picture in the PC game's
 * colour for that place -- the 336 x 256 menu mapped onto the 320 x 200 shade map. Drawing only.
 *
 * F5 (ModApi_AddHotkey; [mod.dosmenus] hotkey to change it) switches between the PC shading and
 * the Amiga's flat menus while the game runs -- Playa's ask, 2026-11-08. Not remembered: each
 * start shows the game's own look first, PROJECT_MENU_LOOK_AMIGA (include/project_identity.h: the
 * PC's in OpenSWOS, the Amiga's in SWOS United).
 */
#ifdef SWOS_SDL_BUILD

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mod_api.h"
#include "gfx.h"
#include "display.h"            /* _screen_width, _screen_height, FRONTBUFFER_ROW_STRIDE_BYTES */
#include "swos_io.h"            /* swos_data_path */
#include "project_identity.h"   /* PROJECT_MENU_LOOK_AMIGA */

#define SLUG "dosmenus"

extern const uint8_t *Platform_FrameShown(void);
extern uint8_t *Platform_HudLayerBuffer(void);
extern int EscConfirm_Showing(void);
extern int _menu_draw_x_offset;

/* swos.asm's menuItemColorTable: a menu colour's ramp in the PC menus' palette. */
static const uint8_t k_ramp[16] = { 96, 96, 96, 32, 64, 64, 64, 96, 32, 96, 128, 160, 64, 192, 224, 224 };

enum { kMapW = 320, kMapH = 200, kMaxBoxes = 160 };

static struct {
    int      tried, ok;
    uint8_t  shade[kMapW * kMapH];
    uint8_t  rgb[256][3];       /* the PC menus' palette, 8 bits */
} s_pc;

static struct { int32_t colour, x, y, w, h; } s_box[kMaxBoxes];
static int32_t s_boxes;

static uint32_t   *s_layer;     /* ARGB: the shaded pixels, clear elsewhere */
static uint8_t    *s_rgba;
static int32_t     s_lw, s_lh;
static GfxTexture *s_tex;

static void load_pc(void)
{
    char path[1024];
    FILE *f;
    uint8_t pal[768];
    s_pc.tried = 1;
    f = fopen(swos_data_path("grafs_dos/FILL.256", path, (uint32_t)sizeof path), "rb");
    if (!f) {
        fprintf(stderr, "[dosmenus] no grafs_dos/FILL.256 (the PC game's data): the menus stay flat\n");
        return;
    }
    s_pc.ok = fread(s_pc.shade, 1, sizeof s_pc.shade, f) == sizeof s_pc.shade &&
              fread(pal, 1, sizeof pal, f) == sizeof pal;
    fclose(f);
    for (int i = 0; s_pc.ok && i < 256; i++)
        for (int c = 0; c < 3; c++)
            s_pc.rgb[i][c] = (uint8_t)((pal[i * 3 + c] & 63) * 255 / 63);
    if (!s_pc.ok)
        fprintf(stderr, "[dosmenus] grafs_dos/FILL.256 is not the PC game's: the menus stay flat\n");
}

static int s_amigaLook = PROJECT_MENU_LOOK_AMIGA;  /* F5: the Amiga's flat menus for now */

static void toggle_look(void)
{
    s_amigaLook = !s_amigaLook;
    fprintf(stderr, "[dosmenus] menus: %s\n", s_amigaLook ? "Amiga (flat)" : "PC (shaded)");
}

static void init(void)
{
    ModApi_AddHotkey(SLUG, "F5", "switches the menus between the PC's shading and the Amiga's", toggle_look);
}

static void draw_begin(void) { s_boxes = 0; }

static void fill(int32_t colour, int32_t x, int32_t y, int32_t w, int32_t h)
{
    int32_t i;
    if (colour < 0 || colour > 15)
        return;
    for (i = 0; i < s_boxes; i++)       /* the same box again (a redrawn entry): its colour now */
        if (s_box[i].x == x && s_box[i].y == y && s_box[i].w == w && s_box[i].h == h)
            break;
    if (i == s_boxes) {
        if (s_boxes == kMaxBoxes)
            return;
        s_boxes++;
    }
    s_box[i].colour = colour;
    s_box[i].x = x;
    s_box[i].y = y;
    s_box[i].w = w;
    s_box[i].h = h;
}

static int ensure_layer(int32_t w, int32_t h)
{
    if (s_layer && s_lw == w && s_lh == h)
        return 1;
    free(s_layer);
    free(s_rgba);
    s_layer = (uint32_t *)calloc((size_t)w * h, 4);
    s_rgba = (uint8_t *)malloc((size_t)w * h * 4);
    if (s_tex)
        Gfx_DestroyTexture(s_tex);
    s_tex = s_layer && s_rgba ? Gfx_CreateTexture(w, h, 0) : NULL;
    s_lw = w;
    s_lh = h;
    return s_tex != NULL;
}

static void frame_drawn(void)
{
    const uint8_t *fb;
    int32_t any = 0, stride, xoff, hud = 0;
    if (s_amigaLook)
        return;
    if (!s_pc.tried)
        load_pc();
    if (!s_pc.ok || !s_boxes || ModApi_InReplay())
        return;
    if (ModApi_InMatch()) {
        /* In a match only the ESC screen's boxes (QUIT THE MATCH? RESUME / QUIT: esc_confirm.c),
         * which it draws on the HUD layer -- the screen's width, centred, colour 0 clear. */
        if (!EscConfirm_Showing() || !(fb = Platform_HudLayerBuffer()))
            return;
        stride = _screen_width;
        xoff = WIDE_MARGIN;
        hud = 1;
    } else {
        /* The menus, and the screens after a match (REPLAY / EXIT, AFTER THE MATCH). */
        if (!(fb = Platform_FrameShown()))
            return;
        stride = FRONTBUFFER_ROW_STRIDE_BYTES;
        xoff = _menu_draw_x_offset;
    }
    if (!ensure_layer(_screen_width, _screen_height))
        return;
    memset(s_layer, 0, (size_t)s_lw * s_lh * 4);
    for (int32_t b = 0; b < s_boxes; b++) {
        const int32_t c = s_box[b].colour;
        if (c == 0 && hud)
            continue;                   /* (the HUD layer's clear) */
        for (int32_t y = s_box[b].y; y < s_box[b].y + s_box[b].h; y++) {
            if (y < 0 || y >= s_lh)
                continue;
            /* the menu's 256 rows onto the map's 200 */
            const int32_t my = y * kMapH / 256 < kMapH ? y * kMapH / 256 : kMapH - 1;
            for (int32_t x = s_box[b].x; x < s_box[b].x + s_box[b].w; x++) {
                if (x < 0 || x >= s_lw || fb[(size_t)y * stride + x] != c)
                    continue;           /* (text, a picture, or the box is gone) */
                /* the menu's 336 columns onto the map's 320 */
                int32_t mx = (x - xoff) * kMapW / 336;
                mx = mx < 0 ? 0 : mx >= kMapW ? kMapW - 1 : mx;
                const uint8_t *rgb = s_pc.rgb[(k_ramp[c] + s_pc.shade[my * kMapW + mx]) & 255];
                s_layer[(size_t)y * s_lw + x] = 0xFF000000u | ((uint32_t)rgb[0] << 16) | ((uint32_t)rgb[1] << 8) | rgb[2];
                any = 1;
            }
        }
    }
    if (!any)
        return;
    for (int32_t i = 0; i < s_lw * s_lh; i++) {
        const uint32_t v = s_layer[i];
        s_rgba[i * 4 + 0] = (uint8_t)(v >> 16);
        s_rgba[i * 4 + 1] = (uint8_t)(v >> 8);
        s_rgba[i * 4 + 2] = (uint8_t)v;
        s_rgba[i * 4 + 3] = (uint8_t)(v >> 24);
    }
    Gfx_UpdateTexture(s_tex, s_rgba, s_lw * 4);
    Gfx_SetSpace(GFX_SPACE_PICTURE);
    Gfx_SetBlend(GFX_BLEND_ALPHA);
    Gfx_DrawTexture(s_tex, NULL, NULL);
}

const ModDef ModDef_dosmenus = {
    .slug          = SLUG,
    .init          = init,
    .menuDrawBegin = draw_begin,
    .menuFill      = fill,
    .frameUnder    = frame_drawn,      /* (part of the picture: the other mods draw over it) */
};

#endif /* SWOS_SDL_BUILD */
