/*
 * mod_hudbox.c -- a mod's box over the match, drawn at the present: mod_hudbox.h. SDL builds.
 */
#ifdef SWOS_SDL_BUILD
#include "mod_hudbox.h"

#include <stdlib.h>
#include <string.h>

#include "display.h"            /* _screen_width, _screen_height, FRONTBUFFER_ROW_STRIDE_BYTES */
#include "gfx.h"
#include "platform_sdl.h"       /* Platform_FrameShown, Platform_PaletteRgb, Platform_NowMs */

extern volatile uintptr_t _frontbufferBasePtr;
extern int _menu_draw_x_offset;

static uint8_t *s_scr;          /* the contents drawn, at the frame buffer's stride */
static int32_t  s_scrW, s_scrH;

static int32_t scratch(int32_t rows)
{
    const int32_t w = _screen_width;
    if (s_scr && s_scrW == w && s_scrH >= rows)
        return 1;
    free(s_scr);
    s_scrW = w;
    s_scrH = rows;
    s_scr = (uint8_t *)malloc((size_t)w * (size_t)rows);
    return s_scr != NULL;
}

static void contents(uint8_t bg, int32_t rows, void (*draw)(void *), void *ctx)
{
    memset(s_scr, bg, (size_t)s_scrW * (size_t)rows);
    const uintptr_t saved = _frontbufferBasePtr;
    const int savedX = _menu_draw_x_offset;
    _frontbufferBasePtr = (uintptr_t)s_scr;
    _menu_draw_x_offset = 0;
    draw(ctx);
    _frontbufferBasePtr = saved;
    _menu_draw_x_offset = savedX;
}

void HudBox_Draw(HudBox *b, int32_t x, int32_t y, int32_t w, int32_t h, int32_t mode,
                 uint8_t r, uint8_t g, uint8_t bl, void (*draw)(void *ctx), void *ctx)
{
    const int32_t rows = h + 8;             /* (room for a glyph that hangs below the box) */
    if (w <= 0 || h <= 0 || w > _screen_width || !scratch(rows))
        return;
    if (w * h > b->cap) {
        free(b->px);
        b->px = (uint16_t *)malloc((size_t)w * (size_t)h * sizeof *b->px);
        b->cap = b->px ? w * h : 0;
        if (!b->px)
            return;
    }
    /* What the contents drew is what is the same over two backgrounds; the rest is the box's. */
    contents(0x00, rows, draw, ctx);
    for (int32_t j = 0; j < h; j++)
        for (int32_t i = 0; i < w; i++)
            b->px[j * w + i] = s_scr[(size_t)j * s_scrW + i];
    contents(0xFF, rows, draw, ctx);
    for (int32_t j = 0; j < h; j++)
        for (int32_t i = 0; i < w; i++)
            if (s_scr[(size_t)j * s_scrW + i] != b->px[j * w + i])
                b->px[j * w + i] = 0xFFFF;
    b->x = x;
    b->y = y;
    b->w = w;
    b->h = h;
    b->mode = mode;
    b->scale = 1;
    b->pal16 = NULL;
    b->bg[0] = r;
    b->bg[1] = g;
    b->bg[2] = bl;
    b->ms = Platform_NowMs();
}

void HudBox_Kept(HudBox *b)
{
    if (b->ms)
        b->ms = Platform_NowMs();
}

void HudBox_Off(HudBox *b) { b->ms = 0; }

void HudBox_Present(HudBox *b)
{
    if (!b->ms || Platform_NowMs() - b->ms > 100 || !b->px)
        return;
    const int32_t w = b->w, h = b->h;
    if (!b->tex || b->texW != w || b->texH != h) {
        if (b->tex)
            Gfx_DestroyTexture(b->tex);
        free(b->rgba);
        b->tex = Gfx_CreateTexture(w, h, 0);
        b->rgba = (uint8_t *)malloc((size_t)w * (size_t)h * 4);
        b->texW = w;
        b->texH = h;
        if (!b->tex || !b->rgba)
            return;
    }
    const uint8_t *fb = b->mode == HUDBOX_SHADE ? Platform_FrameShown() : NULL;
    for (int32_t j = 0; j < h; j++)
        for (int32_t i = 0; i < w; i++) {
            uint8_t *p = b->rgba + ((size_t)j * w + i) * 4;
            const uint16_t c = b->px[j * w + i];
            const int32_t fx = b->x + i, fy = b->y + j;
            p[3] = 255;
            if (c != 0xFFFF && b->pal16 && c < 16) {
                p[0] = (uint8_t)((b->pal16[c] >> 8 & 15) * 17);
                p[1] = (uint8_t)((b->pal16[c] >> 4 & 15) * 17);
                p[2] = (uint8_t)((b->pal16[c] & 15) * 17);
            } else if (c != 0xFFFF) {
                Platform_PaletteRgb(c, &p[0], &p[1], &p[2]);
            } else if (b->mode == HUDBOX_CLEAR) {
                p[0] = p[1] = p[2] = p[3] = 0;
            } else if (b->mode == HUDBOX_SOLID) {
                p[0] = b->bg[0];
                p[1] = b->bg[1];
                p[2] = b->bg[2];
            } else if (fb && fx >= 0 && fy >= 0 && fx < _screen_width && fy < _screen_height) {
                Platform_PaletteRgb(fb[(size_t)fy * FRONTBUFFER_ROW_STRIDE_BYTES + fx], &p[0], &p[1], &p[2]);
                p[0] = HudBox_ShadeChannel(p[0]);
                p[1] = HudBox_ShadeChannel(p[1]);
                p[2] = HudBox_ShadeChannel(p[2]);
            } else {
                p[3] = 0;
            }
        }
    Gfx_UpdateTexture(b->tex, b->rgba, w * 4);
    const GfxSpace space = Gfx_Space();
    const GfxBlend blend = Gfx_Blend();
    const int32_t k = b->scale > 1 ? b->scale : 1;
    const SDL_FRect dst = { (float)b->x, (float)b->y, (float)(w * k), (float)(h * k) };
    Gfx_SetSpace(GFX_SPACE_PICTURE);
    Gfx_SetBlend(GFX_BLEND_ALPHA);
    Gfx_DrawTexture(b->tex, NULL, &dst);
    Gfx_SetBlend(blend);
    Gfx_SetSpace(space);
}
#endif
