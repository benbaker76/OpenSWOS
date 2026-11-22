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
        _gamePalette[e] = rgb12;
        DosGfx_SetExtraColour(e, (uint8_t)(((rgb12 >> 8) & 15) * 17), (uint8_t)(((rgb12 >> 4) & 15) * 17),
                              (uint8_t)((rgb12 & 15) * 17));
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
    printf("[kit-palette] self-test %s (%d entries to give)\n", fails ? "FAILED" : "passed", (int)s_given);
    return fails != 0;
}

#endif /* SWOS_SDL_BUILD */
