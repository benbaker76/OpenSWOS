/*
 * mod_hudbox.h -- a mod's box over the match, drawn at the present (SDL builds).
 *
 * SWOS 2020 drew its HUD boxes (the new scoreboard, the speedrun's timer, target and difficulty)
 * over its output, after the scroll. Written into the frame buffer they go with the pitch -- the
 * frame scrolls under the camera by the sub-pixel residual and the scroll blit keeps rows from the
 * last frame -- so they flickered as the camera moved (Playa, 2026-11-11/12). A HudBox is drawn
 * over the finished picture instead (gfx.h, the picture's space): what the mod draws in it (text,
 * kits: ModApi_DrawText / D8 at the box's own (0, 0)) opaque, the rest
 *
 *   HUDBOX_SOLID  one colour (SWOS 2020's putPixelDirect(..., 180): the VGA palette's 180)
 *   HUDBOX_SHADE  the picture under it shaded as SWOS 2020's index + 128 shades it: its VGA
 *                 palette's upper half is the lower half less 15 of 63 a channel (dos_graphics.c,
 *                 kDosGamePalette), so each colour less 61 of 255 -- not the Amiga palette's
 *                 upper half, near black on grass and nothing on what is up there already.
 *
 * HudBox_Draw from matchDrawn (each match frame that shows it), HudBox_Present from frameUnder,
 * HudBox_Kept from frameKept (paused: the frame shown again). A box not drawn for 100 ms goes,
 * as the HUD layer's rule (platform_sdl.c); HudBox_Off takes it away at once.
 */
#ifndef MOD_HUDBOX_H
#define MOD_HUDBOX_H

#ifdef SWOS_SDL_BUILD
#include <stdint.h>

enum { HUDBOX_SOLID, HUDBOX_SHADE };

struct GfxTexture;

typedef struct {
    int32_t   x, y, w, h, mode;
    uint8_t   bg[3];                /* HUDBOX_SOLID's colour */
    uint16_t *px;                   /* w * h: a palette index, 0xFFFF the background */
    int32_t   cap;
    uint64_t  ms;                   /* the last match frame that drew it; 0 off */
    uint8_t  *rgba;
    struct GfxTexture *tex;
    int32_t   texW, texH;
} HudBox;

/* The box at (x, y), w x h, in picture pixels; `draw(ctx)` draws its contents at the box's own
 * (0, 0) through the frame buffer calls (they are pointed at a scratch buffer meanwhile). */
void HudBox_Draw(HudBox *b, int32_t x, int32_t y, int32_t w, int32_t h, int32_t mode,
                 uint8_t r, uint8_t g, uint8_t bl, void (*draw)(void *ctx), void *ctx);
void HudBox_Present(HudBox *b);
void HudBox_Kept(HudBox *b);
void HudBox_Off(HudBox *b);

/* SWOS 2020's shading of one colour (HUDBOX_SHADE's): each channel less 61, at least 0. */
static inline uint8_t HudBox_ShadeChannel(uint8_t c) { return (uint8_t)(c > 61 ? c - 61 : 0); }
#endif

#endif /* MOD_HUDBOX_H */
