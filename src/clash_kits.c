/*
 * clash_kits.c -- SWOS 2020's GK/REF CLASH KITS ([graphics] clash_kits, docs/SETTINGS.md).
 *
 * The keepers wear the drop-shadow's colour (palette 8, "shadow green") and the referee black
 * (3). Against a dark kit -- black, blue or burgundy shirt (and stripes, for a striped one) on
 * either side -- both keepers change to green (grey when either team wears green) and the
 * referee to yellow (light blue when either wears yellow), as swos-2020's
 * applyGoalkeeperAndRefereeClashFallback (video/sprites.cpp) does. Judged against BOTH teams,
 * so both keepers change together.
 *
 * DRAWING ONLY: it recolours sprite pixels after ConvertColorsAndTextSprites has made the
 * match's sprites; nothing the match plays by reads a pixel, so it has no netplay effect and
 * each player may have it on or off. The shadow, drawn in the same colour 8, is kept by
 * SWOS 2020's per-frame masks (src/clash_kit_masks.inc, scripts/gen_clash_kit_masks.py): the
 * frames are the same drawings in the Amiga's sprites and the DOS ones. Like SWOS 2020's,
 * the jersey is every unmasked pixel of the colour, so the referee's black hair goes with it.
 * The reserve keepers' sets (891.., 1007..) are the same 58 frames: they change too (SWOS
 * 2020 leaves them, "a known gap"). Not with the Jaguar's sprites (their own art and ramps).
 * SDL only: the Amiga build's sprites are planar and it reads no SWOS.ini.
 */
#include "swos_types.h"
#include "game_types.h"

int32_t g_clashKits = 0;     /* [graphics] clash_kits (main.c); off: the original's colours */

#ifdef SWOS_SDL_BUILD
#include <stdio.h>
#include "jag_graphics.h"    /* JagGfx_Enabled */
#include "clash_kit_masks.inc"

extern uint8_t *_spriteGraphicsPtr;

enum {
    kCellW = 16, kCellH = 22,
    kGkFrames = 58, kRefFrames = 11,
    kGkTeam1 = 833, kGkTeam1Reserve = 891, kGkTeam2 = 949, kGkTeam2Reserve = 1007,
    kRefFirst = 1159,
    kJersey = 8, kRefShirt = 3,
    kGray = 1, kGreen = 14, kLightBlue = 13, kYellow = 15,
};

static int32_t is_dark(int16_t c) { return c == 3 || c == 11 || c == 12; }

static int32_t dark_kit(const TeamGame *t)
{
    if (t->prShirtType == 0)
        return is_dark(t->prShirtCol);
    return is_dark(t->prShirtCol) && is_dark(t->prStripesCol);
}

static int32_t has_colour(const TeamGame *t, int16_t c)
{
    return t->prShirtCol == c || (t->prShirtType != 0 && t->prStripesCol == c);
}

static void recolour(int32_t first, int32_t frames, const uint16_t (*prot)[kCellH],
                     uint8_t from, uint8_t to)
{
    SpriteGraphics **table = (SpriteGraphics **)_spriteGraphicsPtr;
    for (int32_t f = 0; f < frames; f++) {
        SpriteGraphics *spr = table[first + f];
        if (!spr || !spr->sprData)
            continue;
        const int32_t pitch = (int32_t)spr->wquads * 16;
        uint8_t *px = (uint8_t *)spr->sprData;
        for (int32_t y = 0; y < (int32_t)spr->nlines && y < kCellH; y++)
            for (int32_t x = 0; x < pitch && x < kCellW; x++)
                if (px[y * pitch + x] == from && !(prot[f][y] & (1u << x)))
                    px[y * pitch + x] = to;
    }
}

void ClashKits_Apply(const TeamGame *team1, const TeamGame *team2)
{
    if (!g_clashKits || JagGfx_Enabled())
        return;
    if (!dark_kit(team1) && !dark_kit(team2)) {
        fprintf(stderr, "[kits] clash kits: no clash (shirts %d and %d)\n",
                (int)team1->prShirtCol, (int)team2->prShirtCol);
        return;     /* no clash: the keepers' shadow green and the referee's black stay */
    }
    const uint8_t gk  = has_colour(team1, kGreen)  || has_colour(team2, kGreen)  ? kGray : kGreen;
    const uint8_t ref = has_colour(team1, kYellow) || has_colour(team2, kYellow) ? kLightBlue : kYellow;
    recolour(kGkTeam1, kGkFrames, kGkProtected, kJersey, gk);
    recolour(kGkTeam1Reserve, kGkFrames, kGkProtected, kJersey, gk);
    recolour(kGkTeam2, kGkFrames, kGkProtected, kJersey, gk);
    recolour(kGkTeam2Reserve, kGkFrames, kGkProtected, kJersey, gk);
    recolour(kRefFirst, kRefFrames, kRefProtected, kRefShirt, ref);
    fprintf(stderr, "[kits] clash kits: keepers in %d, the referee in %d\n", gk, ref);
}
#else
void ClashKits_Apply(const TeamGame *team1, const TeamGame *team2) { (void)team1; (void)team2; }
#endif
