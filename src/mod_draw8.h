/*
 * mod_draw8.h -- small drawing into a match's 8bpp frame for the SWOS 2020 match-extras mods
 * (src/mod_autoplay.c, mod_balltrails.c, mod_scoreboard.c, mod_netstats.c, the practice mod's
 * radar): SWOS 2020's putPixel, drawFillBox, drawDarkenRect, drawFilledCircle and showUniform, at
 * SWOS 2020's palette indices (the match palette has them where SWOS 2020 does: src/mod_practice.c).
 * From matchDrawn / spriteDraw only: D8_Begin takes the frame, every call clips to it.
 */
#ifndef MOD_DRAW8_H
#define MOD_DRAW8_H

#include "mod_api.h"

typedef struct { uint8_t *fb; int32_t stride, w, h; } D8;

static inline int32_t D8_Begin(D8 *d)
{
    d->fb = ModApi_Frame(&d->stride);
    ModApi_ScreenSize(&d->w, &d->h);
    return d->fb != NULL;
}

static inline void D8_Put(const D8 *d, int32_t x, int32_t y, uint8_t c)
{
    if (x >= 0 && y >= 0 && x < d->w && y < d->h)
        d->fb[y * d->stride + x] = c;
}

/* drawFillBox(x, y, w, h): w x h from x, y */
static inline void D8_Fill(const D8 *d, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t c)
{
    for (int32_t j = 0; j < h; j++)
        for (int32_t i = 0; i < w; i++)
            D8_Put(d, x + i, y + j, c);
}

/* drawDarkenRect(x, y, w, h): x..x+w, y..y+h, inclusive -- the lower half of the palette shown in
 * its upper half (render.cpp putDarkenPixel; mod_practice.c draw_band_and_kits). */
static inline void D8_Darken(const D8 *d, int32_t x, int32_t y, int32_t w, int32_t h)
{
    for (int32_t j = y; j <= y + h; j++)
        for (int32_t i = x; i <= x + w; i++)
            if (i >= 0 && j >= 0 && i < d->w && j < d->h && d->fb[j * d->stride + i] < 128)
                d->fb[j * d->stride + i] += 128;
}

/* drawFilledCircle(x0, y0, radius) (game.cpp): its spans, as SWOS 2020 draws them */
static inline void D8_Disc(const D8 *d, int32_t x0, int32_t y0, int32_t radius, uint8_t c)
{
    int32_t x = radius, y = 0, xChange = 1 - (radius << 1), yChange = 0, radiusError = 0;
    while (x >= y) {
        for (int32_t i = x0 - x; i <= x0 + x; i++) {
            D8_Put(d, i, y0 + y, c);
            D8_Put(d, i, y0 - y, c);
        }
        for (int32_t i = x0 - y; i <= x0 + y; i++) {
            D8_Put(d, i, y0 + x, c);
            D8_Put(d, i, y0 - x, c);
        }
        y++;
        radiusError += yChange;
        yChange += 2;
        if (((radiusError << 1) + xChange) > 0) {
            x--;
            radiusError += xChange;
            xChange += 2;
        }
    }
}

/* showUniform (render.cpp): a side's kit, 8 x 8 (scale 1) -- '1' the shirt, '2' its stripes,
 * '3' the shorts, '+' the shadow (19). */
static inline void D8_Kit(const D8 *d, int32_t side, int32_t x, int32_t y, int32_t scale)
{
    static const char *const kKits[4][8] = {
        { "........", ".12221..", ".12221+.", "..222++.", "..333+..", "..333+..", "...+++..", "........" },
        { "........", ".21112..", ".21112+.", "..111++.", "..333+..", "..333+..", "...+++..", "........" },
        { "........", ".12121..", ".12121+.", "..212++.", "..333+..", "..333+..", "...+++..", "........" },
        { "........", ".11111..", ".12221+.", "..111++.", "..333+..", "..333+..", "...+++..", "........" },
    };
    int32_t type, shirt, stripes, shorts;
    if (!ModApi_MatchKit(side, &type, &shirt, &stripes, &shorts))
        return;
    const char *const *k = kKits[type & 3];
    for (int32_t j = 0; j <= 7 * scale; j++)
        for (int32_t i = 0; i <= 7 * scale; i++) {
            const int32_t jj = j / scale, ii = i / scale;
            const char c = jj < 8 && ii < 8 ? k[jj][ii] : '.';
            const int32_t col = c == '1' ? shirt : c == '2' ? stripes : c == '3' ? shorts : c == '+' ? 19 : -1;
            if (col >= 0)
                D8_Put(d, x + i, y + j, (uint8_t)col);
        }
}

#endif /* MOD_DRAW8_H */
