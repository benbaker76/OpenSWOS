/*
 * shirt_numbers.c -- shirt numbers to 99 (include/shirt_numbers.h; the SWOS 96/97 Mod's shirtnum.py
 * by Marco and menunum.py; docs/WORLD_2026_27.md, phase 7).
 *
 * In a match the game draws the number over the controlled player (UpdateControlledPlayerNumbers)
 * and over the booked one (the referee's card sprite) as one sprite, SPR_CORNER_FLAG_FRAME_3 + the
 * number, with sprites for 1..16 only; on the tactics pitches of the menus (the little players)
 * as 161 + the number, with sprites for 1..11 only. A bigger number picked whatever sprite came
 * next (the player mark, the goal's parts; the big shorts). Here 17..99 (a match) and 12..99 (the
 * menus) are sprites of their own, after the font's in the sprite table, made from the set's own
 * digits: two side by side, one pixel apart, centred like its "10". A match's: 2..9 as they are,
 * the narrow 1 and the 0 cut from the "10" sprite (as the game writes 10..16), made with each
 * match's sprites; the menus': the "10" has no gap, so the 1 is its "1" and the 0 the rest of the
 * "10", made when first needed (again when the menu sprites were loaded again). 0 and 100 on: no
 * sprite.
 *
 * Both targets: SDL's sprites are chunky (a byte a pixel), the Amiga's planar (each line its
 * planes' words, then the mask the same way) and in chip RAM, for the blitter.
 */
#include "shirt_numbers.h"
#include "game_types.h"
#include "display.h"
#include "swos_sections.h"
#ifdef SWOS_SDL_BUILD
#include <stdlib.h>
#endif

#define MATCH_BASE  1073                /* SPR_CORNER_FLAG_FRAME_3: + 1..16, the game's */
#define MENU_BASE   161                 /* the menu's little numbers: + 1..11, the game's */
#define NUM_LINES   8                   /* (the game's are 5-8 lines) */
#define MAX_SET     88                  /* 12..99 */

extern uint8_t *_spriteGraphicsPtr;

typedef struct { const SpriteGraphics *spr; int16_t col, width; } Glyph;

typedef struct {
    int16_t base, first, slot;          /* the game's sprites, the first new number, its slot */
    uint8_t ok;
    const SpriteGraphics *source;       /* (the "10" it was made from: a reload makes it again) */
    SpriteGraphics hdr[MAX_SET];
#ifdef SWOS_SDL_BUILD
    uint8_t pix[MAX_SET][NUM_LINES * 16];
#endif
} NumberSet;

static NumberSet s_match = { MATCH_BASE, 17, SHIRT_NUMBER_SPRITE_17 };
static NumberSet s_menu = { MENU_BASE, 12, SHIRT_NUMBER_MENU_SPRITE_12 };
#ifndef SWOS_SDL_BUILD
#define NUM_PLANES_MAX 4                /* (16-colour sprites; 22.5 KB of chip RAM) */
SWOS_CHIP_BSS static uint16_t s_planar[2][MAX_SET][NUM_LINES * NUM_PLANES_MAX * 2];
#endif

static SpriteGraphics *spr_at(int32_t index)
{
    return ((SpriteGraphics **)_spriteGraphicsPtr)[index];
}

/* Pixel (x, y) of a sprite set or not (any colour). */
static int32_t lit(const SpriteGraphics *s, int32_t x, int32_t y)
{
    if (x < 0 || x >= s->wquads * 16 || y < 0 || y >= s->nlines)
        return 0;
#ifdef SWOS_SDL_BUILD
    return s->sprData[y * s->wquads * 16 + x] != 0;
#else
    {
        const uint16_t sp = spriteNumPlanes(s->ordinal);
        const uint16_t *row = (const uint16_t *)s->sprData + (uint32_t)y * sp * s->wquads + (x >> 4);
        uint16_t any = 0;
        for (uint16_t p = 0; p < sp; p++)
            any |= row[p * s->wquads];
        return (any >> (15 - (x & 15))) & 1;
    }
#endif
}

/* The columns a sprite's ink spans, from column `from` up to the first empty column after
 * ink (a match's "10": its 1, then from there its 0). */
static int32_t ink(const SpriteGraphics *s, int32_t from, int16_t *col, int16_t *width)
{
    int32_t x = from, end;
    for (; x < s->wquads * 16; x++) {
        int32_t any = 0;
        for (int32_t y = 0; y < s->nlines; y++) any |= lit(s, x, y);
        if (any) break;
    }
    if (x >= s->wquads * 16)
        return 0;
    for (end = x; end < s->wquads * 16; end++) {
        int32_t any = 0;
        for (int32_t y = 0; y < s->nlines; y++) any |= lit(s, end, y);
        if (!any) break;
    }
    *col = (int16_t)x;
    *width = (int16_t)(end - x);
    return 1;
}

static int32_t usable(const SpriteGraphics *s)
{
    return s && s->sprData && s->wquads == 1 && s->nlines > 0 && s->nlines <= NUM_LINES
#ifndef SWOS_SDL_BUILD
           && spriteNumPlanes(s->ordinal) <= NUM_PLANES_MAX
#endif
           ;
}

/* Copy glyph g's pixels to column x of the set's sprite k. */
static void put(NumberSet *set, int32_t k, const Glyph *g, int32_t x)
{
    const SpriteGraphics *s = g->spr;
    for (int32_t y = 0; y < s->nlines; y++)
        for (int32_t c = 0; c < g->width; c++) {
            const int32_t dx = x + c;
            if (dx < 0 || dx >= 16)
                continue;
#ifdef SWOS_SDL_BUILD
            set->pix[k][y * 16 + dx] = s->sprData[y * s->wquads * 16 + g->col + c];
#else
            {
                const uint16_t sp = spriteNumPlanes(s->ordinal);
                const int32_t sx = g->col + c;
                const uint16_t *src = (const uint16_t *)s->sprData + (uint32_t)y * sp * s->wquads + (sx >> 4);
                uint16_t *dst = s_planar[set == &s_menu][k] + (uint32_t)y * sp;
                for (uint16_t p = 0; p < sp; p++)
                    if ((src[p * s->wquads] >> (15 - (sx & 15))) & 1)
                        dst[p] |= (uint16_t)(0x8000u >> dx);
            }
#endif
        }
}

static void build(NumberSet *set)
{
    const SpriteGraphics *ten = spr_at(set->base + 10);
    Glyph digit[10];
    int16_t c10, w10, c0, w0;
    set->ok = 0;
    set->source = ten;
    if (!usable(ten) || !ink(ten, 0, &c10, &w10))
        return;
    if (set == &s_menu) {                   /* (its "10" has no gap: the "1", then the rest) */
        const SpriteGraphics *one = spr_at(set->base + 1);
        int16_t c1, w1;
        if (!usable(one) || !ink(one, 0, &c1, &w1))
            return;
        digit[1].spr = one; digit[1].col = c1; digit[1].width = w1;
        digit[0].spr = ten; digit[0].col = (int16_t)(c10 + w1); digit[0].width = (int16_t)(w10 - w1);
        c0 = digit[0].col; w0 = digit[0].width;
        if (w0 <= 0)
            return;
    } else {
        if (!ink(ten, c10 + w10, &c0, &w0))
            return;
        digit[1].spr = ten; digit[1].col = c10; digit[1].width = w10;
        digit[0].spr = ten; digit[0].col = c0;  digit[0].width = w0;
    }
    for (int32_t d = 2; d <= 9; d++) {
        digit[d].spr = spr_at(set->base + d);
        if (!usable(digit[d].spr) || !ink(digit[d].spr, 0, &digit[d].col, &digit[d].width))
            return;
    }
    const int32_t inkTen = c0 + w0 - c10;       /* (the "10": its ink's span, centred on it) */
    for (int32_t n = set->first; n <= 99; n++) {
        const int32_t k = n - set->first;
        const Glyph *a = &digit[n / 10], *b = &digit[n % 10];
        const int32_t w = a->width + 1 + b->width;
        int32_t x = c10 + (inkTen - w) / 2;
        if (x + w > 16) x = 16 - w;
        if (x < 0) x = 0;
        SpriteGraphics *h = &set->hdr[k];
        *h = *ten;
        h->ordinal = (int16_t)(set->slot + k);
        h->pixWidth = (int16_t)(x + w);         /* (wider than the "10": its centre moved by half) */
        h->centerX = (int16_t)(ten->centerX + (x + w / 2) - (c10 + inkTen / 2));
#ifdef SWOS_SDL_BUILD
        for (int32_t i = 0; i < NUM_LINES * 16; i++) set->pix[k][i] = 0;
        h->sprData = set->pix[k];
        h->maskPtr = 0;
        put(set, k, a, x);
        put(set, k, b, x + a->width + 1);
#else
        {
            uint16_t *data = s_planar[set == &s_menu][k];
            const uint16_t sp = spriteNumPlanes(ten->ordinal);
            for (int32_t i = 0; i < NUM_LINES * NUM_PLANES_MAX * 2; i++) data[i] = 0;
            h->sprData = (uint8_t *)data;
            put(set, k, a, x);
            put(set, k, b, x + a->width + 1);
            h->maskPtr = 0;
            if (ten->maskPtr) {                 /* (the mask as the game's: its planes OR'd, per line or */
                const uint16_t reps = ten->type ? sp : 1;   /*  per plane line) */
                uint16_t *m = data + (uint32_t)ten->nlines * sp;
                h->maskPtr = (uint8_t *)m;
                for (int32_t y = 0; y < ten->nlines; y++) {
                    uint16_t any = 0;
                    for (uint16_t p = 0; p < sp; p++) any |= data[y * sp + p];
                    for (uint16_t r = 0; r < reps; r++) *m++ = any;
                }
            }
        }
#endif
        ((SpriteGraphics **)_spriteGraphicsPtr)[set->slot + k] = h;
    }
    set->ok = 1;
#ifdef SWOS_SDL_BUILD
    if (getenv("SWOS_TEST_SHIRT_NUMBERS")) {   /* test: the "10" and some new ones, as text */
        static const int8_t show[] = { 10, 12, 17, 20, 23, 38, 41, 59, 77, 99 };
        for (uint32_t i = 0; i < sizeof show; i++) {
            const int32_t idx = show[i] < set->first ? set->base + show[i] : set->slot + show[i] - set->first;
            const SpriteGraphics *h = spr_at(idx);
            SWOS_DBG("[shirt-numbers] %s %d (sprite %d, %dx%d, centre %d,%d):\n", set == &s_menu ? "menu" : "match",
                     show[i], (int)idx, h->pixWidth, h->nlines, h->centerX, h->centerY);
            for (int32_t y = 0; y < h->nlines; y++) {
                char line[17];
                for (int32_t x = 0; x < 16; x++) line[x] = lit(h, x, y) ? '#' : '.';
                line[16] = 0;
                SWOS_DBG("[shirt-numbers]   %s\n", line);
            }
        }
    }
#endif
}

void ShirtNumbers_Build(void)
{
    build(&s_match);
}

int16_t ShirtNumber_Sprite(int8_t number)
{
#ifdef SWOS_SDL_BUILD
    {   /* test (SWOS_TEST_SHIRT_NUMBERS=1): every number shown + 30, to see 31..46 in a match */
        static const char *e = (const char *)1;
        if (e == (const char *)1) e = getenv("SWOS_TEST_SHIRT_NUMBERS");
        if (e && number >= 1 && number <= 16) number = (int8_t)(number + 30);
    }
#endif
    if (number >= 1 && number < s_match.first)
        return (int16_t)(MATCH_BASE + number);
    if (number >= s_match.first && number <= 99 && s_match.ok)
        return (int16_t)(s_match.slot + number - s_match.first);
    return -1;
}

int16_t ShirtNumber_MenuSprite(int8_t number)
{
    if (number >= 1 && number < s_menu.first)
        return (int16_t)(MENU_BASE + number);
    if (number < s_menu.first || number > 99)
        return -1;
    if (!s_menu.ok || s_menu.source != spr_at(MENU_BASE + 10))
        build(&s_menu);
    return s_menu.ok ? (int16_t)(s_menu.slot + number - s_menu.first) : -1;
}
