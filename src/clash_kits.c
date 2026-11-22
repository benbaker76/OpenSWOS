/*
 * clash_kits.c -- SWOS 2020's GK/REF CLASH KITS ([graphics] clash_kits, docs/SETTINGS.md).
 *
 * The keepers' and the referee's shirt is the drop shadow's colour (palette 8, "shadow green").
 * With KIT COLOURS extended (kit_variants.h, KitVariants_KeeperColour): the keepers always in a kit
 * colour -- dark green, or the green, else grey, else any colour that differs most from both teams when one
 * is too alike -- and the referee black or the referee colour that differs most; by CIEDE2000, any
 * kit colour, a variant in its own palette entry (kit_palette.c). The rest of this comment is the
 * original rule, which ORIGINAL keeps.
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
 * with the shirt in colour 8. With CLASH KITS on, the ORIGINAL referee -- exactly that drawing
 * with the shirt black -- is drawn as that DLC referee (kRefClashKit), the colour indices as the
 * sprites hold them: the Amiga's and the PC's frames are the same drawings, their palettes
 * differ only in the RGB, so it is one for both. A referee from a DLC is never replaced: one with
 * a shirt of colour 8 gets the clash kit on it, one without keeps his colours.
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
#include "kit_variants.h"
#include "kit_palette.h"
#include "team_colors.h"     /* TeamColors_Worn */
extern uint16_t ConvertTeamColor(uint16_t color);
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

/* A kit colour byte's palette entry in this match: a variant's own (kit_palette.c), else the base
 * colour's. */
static uint8_t entry_of(uint8_t colour)
{
    const uint16_t own = KitPalette_Entry(colour);
    return (uint8_t)(own ? own : ConvertTeamColor(colour));
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

/* How many of the referee's pixels differ from the original's -- kRefClashKit with its shirt
 * black, as the game's own sprites draw him: 0 is the original, anything else a DLC's. The shirt
 * may also be in a colour this file gave it (colour 8, the clash kit's yellow or light blue): the
 * referee of a match before, should his sprites outlive it, is still the original. */
static int32_t referee_differs_from_original(void)
{
    SpriteGraphics **table = (SpriteGraphics **)_spriteGraphicsPtr;
    int32_t differ = 0;
    for (int32_t f = 0; f < kRefFrames; f++) {
        SpriteGraphics *spr = table[kRefFirst + f];
        if (!spr || !spr->sprData)
            return -1;
        const int32_t pitch = (int32_t)spr->wquads * 16;
        const uint8_t *px = (const uint8_t *)spr->sprData;
        for (int32_t y = 0; y < (int32_t)spr->nlines && y < kCellH; y++)
            for (int32_t x = 0; x < pitch && x < kCellW; x++) {
                const uint8_t want = kRefClashKit[f][y][x], have = px[y * pitch + x];
                if (want == kJersey && !(kRefProtected[f][y] & (1u << x))) {   /* (the shirt) */
                    if (have != kBlack && have != kJersey && have != kYellow && have != kLightBlue)
                        differ++;
                } else if (have != want) {
                    differ++;
                }
            }
    }
    return differ;
}

/* The original referee drawn as SWOS 2020's DLC referee-clashkit: the same figure, the shirt in
 * colour 8. */
static void put_on_clash_kit_referee(void)
{
    SpriteGraphics **table = (SpriteGraphics **)_spriteGraphicsPtr;
    for (int32_t f = 0; f < kRefFrames; f++) {
        SpriteGraphics *spr = table[kRefFirst + f];
        if (!spr || !spr->sprData)
            continue;
        const int32_t pitch = (int32_t)spr->wquads * 16;
        uint8_t *px = (uint8_t *)spr->sprData;
        for (int32_t y = 0; y < (int32_t)spr->nlines && y < kCellH; y++)
            for (int32_t x = 0; x < pitch && x < kCellW; x++)
                px[y * pitch + x] = kRefClashKit[f][y][x];
    }
}

void ClashKits_Apply(const TeamGame *team1, const TeamGame *team2)
{
    if (!g_clashKits || JagGfx_Enabled())
        return;
    /* The referee first: the original has no shirt of his own to recolour, so he is drawn as
     * the clash-kit DLC's. A DLC's referee is never replaced. */
    {
        const int32_t differ = referee_differs_from_original();
        if (differ == 0) {
            put_on_clash_kit_referee();
            fprintf(stderr, "[kits] clash kits: the original referee, drawn with the clash kit's shirt\n");
        } else {
            fprintf(stderr, "[kits] clash kits: the referee is not the original (%d pixels differ): "
                            "his own, the clash kit on a shirt of colour 8 if he has one\n", (int)differ);
        }
    }
    int32_t clash = dark_kit(team1) || dark_kit(team2);
    uint8_t gk  = !clash ? kJersey : has_colour(team1, kGreen)  || has_colour(team2, kGreen)  ? kGray : kGreen;
    uint8_t ref = !clash ? kBlack  : has_colour(team1, kYellow) || has_colour(team2, kYellow) ? kLightBlue : kYellow;
    {   /* EXTENDED (kit_variants.h): by how alike the colours look -- the keepers' and the referee's
         * each on its own, any kit colour, the one that differs most (KitVariants_KeeperColour). */
        const uint8_t *w1 = TeamColors_Worn(team1), *w2 = TeamColors_Worn(team2);
        if (w1 && w2 && KitPalette_Active()) {
            uint8_t shirts[4];
            int32_t n = 0, keeper, referee;
            shirts[n++] = w1[1];
            if (KitPattern_Base(w1[0]) != 0) shirts[n++] = w1[2];
            shirts[n++] = w2[1];
            if (KitPattern_Base(w2[0]) != 0) shirts[n++] = w2[2];
            keeper  = KitVariants_KeeperColour(shirts, n);
            referee = KitVariants_RefereeColour(shirts, n, keeper);
            gk  = entry_of((uint8_t)keeper);    /* (always a colour: dark green by default) */
            ref = referee < 0 ? kBlack  : entry_of((uint8_t)referee);
            clash = 1;
        }
    }
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
