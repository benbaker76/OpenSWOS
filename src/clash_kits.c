/*
 * clash_kits.c -- SWOS 2020's GK/REF CLASH KITS ([graphics] clash_kits, docs/SETTINGS.md).
 *
 * The keepers' and the referee's shirt is the drop shadow's colour (palette 8, "shadow green").
 * Against a dark kit -- black, blue or burgundy shirt (and stripes, for a striped one) on either
 * side -- the keepers' changes to green (grey when either team wears green) and the referee's to
 * yellow (light blue when either wears yellow); with no clash the keepers' stays and the
 * referee's is black, as in the original. As swos-2020's applyGoalkeeperAndRefereeClashFallback
 * (video/sprites.cpp): every pixel of colour 8 is recoloured EXCEPT where SWOS 2020's per-frame
 * masks say the background or the drop shadow is (src/clash_kit_masks.inc,
 * scripts/gen_clash_kit_masks.py) -- the shadow is colour 8 too and must stay. So a DLC keeper
 * or referee with another figure works as well: its shirt is whatever it draws in colour 8.
 * Judged against BOTH teams, so both keepers change together.
 *
 * The original referee is black all over -- shirt, shorts, hair and shoes alike -- so his shirt
 * cannot be told apart. SWOS 2020 needed its DLC "referee-clashkit" for him: the same referee
 * with the shirt in colour 8. With CLASH KITS on, a referee with no colour 8 outside the masks
 * (the original) is drawn as that DLC referee (kRefClashKit), the colour indices as the sprites
 * hold them -- the Amiga's and the PC's frames are the same drawings, their palettes differ only
 * in the RGB, so it is one for both. A referee that has a shirt of colour 8 already (a DLC's)
 * keeps his own.
 *
 * DRAWING ONLY: it changes sprite pixels after ConvertColorsAndTextSprites has made the match's
 * sprites; nothing the match plays by reads a pixel, so it has no netplay effect and each player
 * may have it on or off. The reserve keepers' sets (891.., 1007..) are the same 58 frames: they
 * change too (SWOS 2020 leaves them, "a known gap"). Not with the Jaguar's sprites (their own
 * art and ramps). SDL only: the Amiga build's sprites are planar and it reads no SWOS.ini.
 */
#include "swos_types.h"
#include "game_types.h"

int32_t g_clashKits = 1;     /* [graphics] clash_kits (main.c), on by default; off: the original's colours */

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
    kJersey = 8, kBlack = 3,
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

/* The referee's frames hold no colour 8 outside the masks: the original's, black all over. */
static int32_t referee_has_no_shirt(void)
{
    SpriteGraphics **table = (SpriteGraphics **)_spriteGraphicsPtr;
    for (int32_t f = 0; f < kRefFrames; f++) {
        SpriteGraphics *spr = table[kRefFirst + f];
        if (!spr || !spr->sprData)
            continue;
        const int32_t pitch = (int32_t)spr->wquads * 16;
        const uint8_t *px = (const uint8_t *)spr->sprData;
        for (int32_t y = 0; y < (int32_t)spr->nlines && y < kCellH; y++)
            for (int32_t x = 0; x < pitch && x < kCellW; x++)
                if (px[y * pitch + x] == kJersey && !(kRefProtected[f][y] & (1u << x)))
                    return 0;
    }
    return 1;
}

/* The original referee drawn as SWOS 2020's DLC referee-clashkit: the same figure, the shirt in
 * colour 8. Returns the pixels that differ from what was there other than the shirt (0 when the
 * DLC is the original as it should be: logged, a check on the drawings being the same). */
static int32_t put_on_clash_kit_referee(void)
{
    SpriteGraphics **table = (SpriteGraphics **)_spriteGraphicsPtr;
    int32_t differ = 0;
    for (int32_t f = 0; f < kRefFrames; f++) {
        SpriteGraphics *spr = table[kRefFirst + f];
        if (!spr || !spr->sprData)
            continue;
        const int32_t pitch = (int32_t)spr->wquads * 16;
        uint8_t *px = (uint8_t *)spr->sprData;
        for (int32_t y = 0; y < (int32_t)spr->nlines && y < kCellH; y++)
            for (int32_t x = 0; x < pitch && x < kCellW; x++) {
                const uint8_t want = kRefClashKit[f][y][x];
                uint8_t *p = &px[y * pitch + x];
                if (*p != want && !(want == kJersey && *p == kBlack))
                    differ++;
                *p = want;
            }
    }
    return differ;
}

void ClashKits_Apply(const TeamGame *team1, const TeamGame *team2)
{
    if (!g_clashKits || JagGfx_Enabled())
        return;
    /* The referee first: the original has no shirt of his own to recolour. */
    if (referee_has_no_shirt()) {
        const int32_t differ = put_on_clash_kit_referee();
        fprintf(stderr, "[kits] clash kits: the original referee drawn with the clash kit's shirt%s\n",
                differ ? "" : " (the same drawing)");
        if (differ)
            fprintf(stderr, "[kits] clash kits: %d of the referee's pixels differ from the original "
                            "beside the shirt\n", (int)differ);
    }
    const int32_t clash = dark_kit(team1) || dark_kit(team2);
    const uint8_t gk  = !clash ? kJersey : has_colour(team1, kGreen)  || has_colour(team2, kGreen)  ? kGray : kGreen;
    const uint8_t ref = !clash ? kBlack  : has_colour(team1, kYellow) || has_colour(team2, kYellow) ? kLightBlue : kYellow;
    if (gk != kJersey) {
        recolour(kGkTeam1, kGkFrames, kGkProtected, kJersey, gk);
        recolour(kGkTeam1Reserve, kGkFrames, kGkProtected, kJersey, gk);
        recolour(kGkTeam2, kGkFrames, kGkProtected, kJersey, gk);
        recolour(kGkTeam2Reserve, kGkFrames, kGkProtected, kJersey, gk);
    }
    recolour(kRefFirst, kRefFrames, kRefProtected, kJersey, ref);   /* (no clash: back to black) */
    if (clash)
        fprintf(stderr, "[kits] clash kits: keepers in %d, the referee in %d\n", gk, ref);
    else
        fprintf(stderr, "[kits] clash kits: no clash (shirts %d and %d)\n",
                (int)team1->prShirtCol, (int)team2->prShirtCol);
}
#else
void ClashKits_Apply(const TeamGame *team1, const TeamGame *team2) { (void)team1; (void)team2; }
#endif
