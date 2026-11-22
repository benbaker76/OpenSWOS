/*
 * kit_palette.c -- the kit colour variants on the pitch: palette entries for the colours a match's
 * kits wear beyond the base colours (include/kit_variants.h, docs/PALETTE_V2.md).
 *
 * The match palette has no free entries in the PC graphics (grafs_dos, grafs_2020: their pitches
 * draw in nearly all 256), but it carries dozens of exact duplicates -- the DOS palette has 62.
 * As the Jaguar sprites do (jag_graphics.c, "THE DONATION MAP"), an entry whose colour another
 * entry has can be given away: the pitch's pixels move onto the twin when it loads
 * (KitPalette_RemapPitch) -- the same colour, so nothing looks different -- and the entry is free.
 * Here the twins must match in BOTH palettes, the Amiga graphics' (_gamePalette) and the PC's
 * (DosGfx_BuildPalette), so a change of graphics cannot tell them apart; and an entry something
 * else changes on its own is never given: the pitch condition's nine, the replay R's sixteen, the
 * top block, the entries a pitch map's colours name.
 *
 * Each match (KitPalette_NewMatch, when its kits are decided) the variants its kits wear -- the
 * players, the crowd, the HUD's kits (KitPalette_Entry) -- take entries from that pool, their colour written into both palettes. Drawing only: nothing here is match state, so
 * the two games of a net game need not agree on it.
 *
 * SDL only; not with the Jaguar sprites, which keep the donated entries for themselves.
 */
#ifdef SWOS_SDL_BUILD
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "kit_variants.h"
#include "kit_palette.h"
#include "dos_graphics.h"
#include "jag_graphics.h"
#include "mod_hooks.h"

extern uint16_t _gamePalette[];
extern uint8_t  _paletteIndices[];       /* the pitch condition's nine entries */
extern uint16_t _pitchType;

#define FIRST_ENTRY   16
#define LAST_ENTRY    239                /* (240..255: the top block, as for the Jaguar tier) */
#define R_FIRST       0x70               /* the replay R's bank */
#define R_LAST        0x7F
#define PITCH_ENTRIES 9
#define MAX_TAKEN     16

static uint8_t s_twin[256];              /* entry -> its twin, 0 = not given away */
static int32_t s_built, s_given;
static uint8_t s_taken[MAX_TAKEN];       /* the entries this match's variants hold */
static uint8_t s_takenColour[MAX_TAKEN]; /* their kit colour byte */
static int32_t s_takenCount;

int32_t KitPalette_Active(void)
{
    return KitVariants_MatchRule() == KIT_RULE_EXTENDED && !JagGfx_Enabled();
}

/* An entry something else changes by itself: never given away, never written. */
static int32_t volatile_entry(int32_t e, const uint8_t *mapEntries, int32_t mapCount)
{
    if (e < FIRST_ENTRY || e > LAST_ENTRY || (e >= R_FIRST && e <= R_LAST))
        return 1;
    for (int32_t i = 0; i < PITCH_ENTRIES; i++)
        if (_paletteIndices[i] == e)
            return 1;
    for (int32_t i = 0; i < mapCount; i++)
        if (mapEntries[i] == e)
            return 1;
    return 0;
}

/* The entries a pitch map (a mod's) sets for the condition: its colours and its cycles. */
static int32_t map_entries(uint8_t *entries)
{
    static uint8_t rgb[256][3];
    return ModHooks_PitchCondition((int32_t)_pitchType, entries, rgb, 256);
}

static void build(void)
{
    static uint8_t dos[768];
    uint8_t mapEntries[256];
    int32_t mapCount;

    if (s_built)
        return;
    s_built = 1;
    mapCount = map_entries(mapEntries);
    DosGfx_BuildPalette(4, dos);              /* (the PC palette as shipped: the condition's entries aside) */
    for (int32_t i = FIRST_ENTRY; i <= LAST_ENTRY; i++) {
        if (volatile_entry(i, mapEntries, mapCount))
            continue;
        for (int32_t j = 1; j < i; j++) {     /* the lower twin keeps the colour */
            if (j >= FIRST_ENTRY && volatile_entry(j, mapEntries, mapCount))
                continue;
            if (j < FIRST_ENTRY && j != 3 && j != 2 && j != 1)
                continue;                     /* (of 0-15 only black, white, grey: kits recolour the rest) */
            if (_gamePalette[i] == _gamePalette[j] && memcmp(dos + i * 3, dos + j * 3, 3) == 0) {
                s_twin[i] = (uint8_t)j;
                s_given++;
                break;
            }
        }
    }
    if (getenv("SWOS_KIT_PALETTE_DUMP"))
        fprintf(stderr, "[kit-palette] %d entries can be given to the kit variants\n", (int)s_given);
}

int32_t KitPalette_RemapPitch(uint8_t *pixels, uint32_t count)
{
    int32_t moved = 0;

    uint8_t twin[256], mapEntries[256];
    int32_t mapCount;

    if (!pixels || JagGfx_Enabled())          /* (the Jaguar tier does its own) */
        return 0;
    build();
    memcpy(twin, s_twin, sizeof twin);
    mapCount = map_entries(mapEntries);        /* (this pitch's map turns its own entries: leave them) */
    for (int32_t i = 0; i < mapCount; i++)
        twin[mapEntries[i]] = 0;
    for (int32_t i = 0; i < s_takenCount; i++)   /* (a kit variant already painted -- the crowd: keep it) */
        twin[s_taken[i]] = 0;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t t = twin[pixels[i]];
        if (t) { pixels[i] = t; moved++; }
    }
    if (moved && getenv("SWOS_KIT_PALETTE_DUMP"))
        fprintf(stderr, "[kit-palette] pitch: %d pixels moved onto their twins\n", (int)moved);
    return moved;
}

/* ---- against the grass: a variant too like this match's pitch is drawn a little lighter or darker ----
 * The three grass entries as the pitch condition set them (ApplyPitchPaletteAndFactors). A variant
 * closer to any than GRASS_APART (CIEDE2000) takes 10 % more or less light -- away from the grass's
 * own lightness -- 20 % if that is still too close. Drawing only, and only the variants' own
 * entries: smaragd on a wet pitch, olive on a soft or hard one. */
#define GRASS_APART 10.0

static void lab_of(uint16_t rgb12, double out[3])
{
    double c[3];
    for (int32_t i = 0; i < 3; i++) {
        const double u = ((rgb12 >> (8 - 4 * i)) & 15) * 17 / 255.0;
        c[i] = u > 0.04045 ? pow((u + 0.055) / 1.055, 2.4) : u / 12.92;
    }
    const double x = (0.4124 * c[0] + 0.3576 * c[1] + 0.1805 * c[2]) / 0.95047;
    const double y = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
    const double z = (0.0193 * c[0] + 0.1192 * c[1] + 0.9505 * c[2]) / 1.08883;
#define F(t) ((t) > 0.008856 ? cbrt(t) : 7.787 * (t) + 16.0 / 116.0)
    out[0] = 116 * F(y) - 16;
    out[1] = 500 * (F(x) - F(y));
    out[2] = 200 * (F(y) - F(z));
#undef F
}

static double ciede2000(uint16_t a12, uint16_t b12)
{
    const double pi = 3.14159265358979323846, R = pi / 180;
    double a[3], b[3];
    lab_of(a12, a); lab_of(b12, b);
    const double C1 = hypot(a[1], a[2]), C2 = hypot(b[1], b[2]), Cb = (C1 + C2) / 2;
    const double G = 0.5 * (1 - sqrt(pow(Cb, 7) / (pow(Cb, 7) + pow(25, 7))));
    const double a1p = (1 + G) * a[1], a2p = (1 + G) * b[1];
    const double C1p = hypot(a1p, a[2]), C2p = hypot(a2p, b[2]);
    double h1p = atan2(a[2], a1p) / R, h2p = atan2(b[2], a2p) / R;
    if (h1p < 0) h1p += 360;
    if (h2p < 0) h2p += 360;
    const double dLp = b[0] - a[0], dCp = C2p - C1p;
    double dhp = 0;
    if (C1p * C2p != 0) { dhp = h2p - h1p; if (dhp > 180) dhp -= 360; else if (dhp < -180) dhp += 360; }
    const double dHp = 2 * sqrt(C1p * C2p) * sin(dhp / 2 * R), Lbp = (a[0] + b[0]) / 2, Cbp = (C1p + C2p) / 2;
    double hbp = h1p + h2p;
    if (C1p * C2p != 0)
        hbp = fabs(h1p - h2p) <= 180 ? (h1p + h2p) / 2 : (h1p + h2p < 360 ? (h1p + h2p + 360) / 2 : (h1p + h2p - 360) / 2);
    const double T = 1 - 0.17 * cos((hbp - 30) * R) + 0.24 * cos(2 * hbp * R) + 0.32 * cos((3 * hbp + 6) * R) - 0.20 * cos((4 * hbp - 63) * R);
    const double dth = 30 * exp(-pow((hbp - 275) / 25, 2)), RC = 2 * sqrt(pow(Cbp, 7) / (pow(Cbp, 7) + pow(25, 7)));
    const double SL = 1 + 0.015 * pow(Lbp - 50, 2) / sqrt(20 + pow(Lbp - 50, 2)), SC = 1 + 0.045 * Cbp, SH = 1 + 0.015 * Cbp * T;
    const double RT = -sin(2 * dth * R) * RC;
    return sqrt(pow(dLp / SL, 2) + pow(dCp / SC, 2) + pow(dHp / SH, 2) + RT * (dCp / SC) * (dHp / SH));
}

static double grass_apart(uint16_t rgb12, double *grassL)
{
    double least = 1e9, sumL = 0, lab[3];
    for (int32_t i = 0; i < 3; i++) {
        const uint16_t g = _gamePalette[_paletteIndices[i]] & 0x0FFF;
        const double d = ciede2000(rgb12, g);
        if (d < least) least = d;
        lab_of(g, lab);
        sumL += lab[0];
    }
    if (grassL) *grassL = sumL / 3;
    return least;
}

static uint16_t scaled(uint16_t rgb12, double f)
{
    uint16_t out = 0;
    for (int32_t i = 0; i < 3; i++) {
        double v = ((rgb12 >> (8 - 4 * i)) & 15) * 17 * f;
        v = v < 0 ? 0 : v > 255 ? 255 : v;
        out |= (uint16_t)((int32_t)(v / 17 + 0.5) << (8 - 4 * i));
    }
    return out;
}

static uint16_t against_grass(uint16_t rgb12)
{
    double grassL, lab[3];
    if (grass_apart(rgb12, &grassL) >= GRASS_APART)
        return rgb12;
    lab_of(rgb12, lab);
    const double dir = lab[0] >= grassL ? 1 : -1;      /* away from the grass's lightness */
    uint16_t c = scaled(rgb12, 1 + 0.1 * dir);
    if (grass_apart(c, NULL) < GRASS_APART)
        c = scaled(rgb12, 1 + 0.2 * dir);
    if (getenv("SWOS_KIT_PALETTE_DUMP"))
        fprintf(stderr, "[kit-palette] %03X too like the grass: drawn %03X\n", rgb12, c);
    return c;
}

static void put_colour(int32_t e, uint16_t rgb12)
{
    _gamePalette[e] = rgb12;
    DosGfx_SetExtraColour(e, (uint8_t)(((rgb12 >> 8) & 15) * 17), (uint8_t)(((rgb12 >> 4) & 15) * 17),
                          (uint8_t)((rgb12 & 15) * 17));
}

/* The pitch condition's colours are in (ApplyPitchPaletteAndFactors): the variants this match wears
 * against its grass, whichever came first. */
void KitPalette_PitchApplied(void)
{
    for (int32_t i = 0; i < s_takenCount; i++)
        put_colour(s_taken[i], against_grass(KitVariants_Rgb12(s_takenColour[i])));
}

void KitPalette_NewMatch(void)
{
    s_takenCount = 0;
}

/* The palette entry a kit colour byte is drawn in this match: 0 for a base colour (the game's own
 * entry), or when there is no entry to give. */
uint16_t KitPalette_Entry(uint8_t colour)
{
    const int32_t index = KitVariants_ColourIndex(colour);
    uint16_t rgb12;
    uint8_t mapEntries[256];
    int32_t mapCount;

    if (index < 10 || !KitPalette_Active())          /* (0-9: a base colour) */
        return 0;
    colour = KitColour_Normal(colour);
    for (int32_t i = 0; i < s_takenCount; i++)
        if (s_takenColour[i] == colour)
            return s_taken[i];
    if (s_takenCount >= MAX_TAKEN)        /* (a replay's kits on top of a match's: start afresh) */
        s_takenCount = 0;
    build();
    mapCount = map_entries(mapEntries);
    rgb12 = KitVariants_Rgb12(colour);
    for (int32_t e = LAST_ENTRY; e >= FIRST_ENTRY; e--) {    /* high first: out of the way */
        int32_t used = 0;
        if (!s_twin[e] || volatile_entry(e, mapEntries, mapCount))
            continue;
        for (int32_t i = 0; i < s_takenCount; i++)
            used |= s_taken[i] == e;
        if (used)
            continue;
        put_colour(e, against_grass(rgb12));
        s_taken[s_takenCount] = (uint8_t)e;
        s_takenColour[s_takenCount++] = colour;
        if (getenv("SWOS_KIT_PALETTE_DUMP"))
            fprintf(stderr, "[kit-palette] colour %02X -> entry %d (%03X)\n", colour, (int)e, rgb12);
        return (uint16_t)e;
    }
    return 0;
}

/* The pool, checked without a match: enough entries to give, every twin the same colour in both
 * palettes, a variant's entry carrying its colour, and a pitch moved only onto its twins. */
int32_t KitPalette_SelfTest(void)
{
    static uint8_t dos[768];
    uint8_t px[256];
    int32_t fails = 0;

    build();
    DosGfx_BuildPalette(4, dos);
    if (s_given < 12) { fails++; fprintf(stderr, "[kit-palette] FAIL: only %d entries to give\n", (int)s_given); }
    for (int32_t i = 0; i < 256; i++)
        if (s_twin[i] && (_gamePalette[i] != _gamePalette[s_twin[i]] || memcmp(dos + i * 3, dos + s_twin[i] * 3, 3))) {
            fails++; fprintf(stderr, "[kit-palette] FAIL: %d is not its twin's colour\n", (int)i);
        }
    for (int32_t i = 0; i < 256; i++)
        px[i] = (uint8_t)i;
    KitPalette_RemapPitch(px, 256);
    for (int32_t i = 0; i < 256; i++)
        if (px[i] != i && px[i] != s_twin[i]) { fails++; fprintf(stderr, "[kit-palette] FAIL: %d moved off its twin\n", (int)i); }
    if (KitPalette_Active()) {
        const uint8_t navy = KitColour_Make(5, 1), violet = KitColour_Normal(10);
        uint16_t e1, e2;
        KitPalette_NewMatch();
        e1 = KitPalette_Entry(navy);
        e2 = KitPalette_Entry(violet);
        if (!e1 || !e2 || e1 == e2 || KitPalette_Entry(navy) != e1 || KitPalette_Entry(4) != 0) {
            fails++; fprintf(stderr, "[kit-palette] FAIL: entries %u/%u\n", (unsigned)e1, (unsigned)e2);
        } else if (_gamePalette[e1] != KitVariants_Rgb12(navy) || !s_twin[e1] || px[e1] == e1) {
            fails++; fprintf(stderr, "[kit-palette] FAIL: navy's entry %u\n", (unsigned)e1);
        }
        KitPalette_NewMatch();
    }
    {   /* against the grass: smaragd on a wet pitch moves away, olive on a soft one, mint on a normal one stays */
        static const struct { uint16_t grass[3]; uint8_t colour; int32_t moves; } k_case[] = {
            { { 0x390, 0x790, 0x390 }, 0x58, 1 },     /* wet, smaragd (green's variant 5) */
            { { 0x370, 0x770, 0x370 }, 0x69, 1 },     /* soft, olive (yellow's variant 6) */
            { { 0x790, 0x690, 0x990 }, 0x38, 0 },     /* normal, mint (green's variant 3) */
        };
        uint16_t kept[3];
        for (int32_t i = 0; i < 3; i++) kept[i] = _gamePalette[_paletteIndices[i]];
        for (size_t k = 0; k < sizeof k_case / sizeof k_case[0]; k++) {
            for (int32_t i = 0; i < 3; i++) _gamePalette[_paletteIndices[i]] = k_case[k].grass[i];
            const uint16_t own = KitVariants_Rgb12(k_case[k].colour), drawn = against_grass(own);
            if ((drawn != own) != k_case[k].moves || (k_case[k].moves && grass_apart(drawn, NULL) <= grass_apart(own, NULL))) {
                fails++; fprintf(stderr, "[kit-palette] FAIL: against the grass, case %d (%03X -> %03X)\n", (int)k, own, drawn);
            }
        }
        for (int32_t i = 0; i < 3; i++) _gamePalette[_paletteIndices[i]] = kept[i];
    }
    printf("[kit-palette] self-test %s (%d entries to give)\n", fails ? "FAILED" : "passed", (int)s_given);
    return fails != 0;
}

#endif /* SWOS_SDL_BUILD */
