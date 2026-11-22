/*
 * kit_variants.c -- the kit colour variants' table and the EXTENDED clash rule
 * (include/kit_variants.h says what the two rules are).
 */
#include <stdio.h>
#include "kit_variants.h"
#include "kit_variants_table.inc"
#ifdef SWOS_SDL_BUILD
#include "netplay_match.h"
#include "netplay_setup.h"
#endif

static int32_t s_rule = KIT_RULE_EXTENDED;   /* [graphics] kit_colours; the real Amiga keeps it */

void KitVariants_SetRule(int32_t rule)
{
    s_rule = rule == KIT_RULE_ORIGINAL ? KIT_RULE_ORIGINAL : KIT_RULE_EXTENDED;
}

int32_t KitVariants_Rule(void)
{
    return s_rule;
}

int32_t KitVariants_MatchRule(void)
{
#ifdef SWOS_SDL_BUILD
    if (NetplayMatch_Enabled())
        return NetplaySetup_KitRule();
#endif
    return s_rule;
}

/* Colour byte: the base colour (0-9) in the low nibble, the variant (1-15, 0 = none) in the high
 * one; 10 is the World 2026-27 data's purple (KIT_LEGACY_PURPLE_*). */
int32_t KitVariants_ColourIndex(uint8_t colour)
{
    uint8_t base = colour & 0x0F, var = colour >> 4;
    if (colour == 10) {
        base = KIT_LEGACY_PURPLE_BASE;
        var  = KIT_LEGACY_PURPLE_VAR;
    }
    if (base >= KIT_BASE_COLOURS)
        return -1;
    if (var == 0)
        return base;
    for (int32_t i = 0; i < KIT_VARIANT_COUNT; i++)
        if (kKitVariants[i].base == base && kKitVariants[i].var == var)
            return KIT_BASE_COLOURS + i;
    return base;    /* an unknown variant (a newer build's): its base colour */
}

int32_t KitVariants_Similar(uint8_t colour1, uint8_t colour2)
{
    const int32_t a = KitVariants_ColourIndex(colour1), b = KitVariants_ColourIndex(colour2);
    if (a < 0 || b < 0)
        return 0;
    return (int32_t)((kKitSimilar[a] >> b) & 1u);
}

/* The table's own sense, checked: every colour like itself, likeness both ways, the base rows
 * the base table, purple found, and a few pairs this rule is known to give. */
int32_t KitVariants_SelfTest(void)
{
    int32_t fails = 0;
    for (int32_t i = 0; i < KIT_COLOUR_COUNT; i++) {
        if (!((kKitSimilar[i] >> i) & 1u)) { fails++; fprintf(stderr, "[kit-variants] FAIL: colour %d not like itself\n", (int)i); }
        for (int32_t j = 0; j < KIT_COLOUR_COUNT; j++)
            if (((kKitSimilar[i] >> j) & 1u) != ((kKitSimilar[j] >> i) & 1u)) {
                fails++; fprintf(stderr, "[kit-variants] FAIL: %d/%d one way only\n", (int)i, (int)j);
            }
    }
    for (int32_t i = 0; i < KIT_BASE_COLOURS; i++)
        if ((kKitSimilar[i] & 0x3FFu) != kKitSimilarBase[i]) { fails++; fprintf(stderr, "[kit-variants] FAIL: base row %d\n", (int)i); }
    for (int32_t i = 0; i < KIT_PATTERN_COUNT; i++)
        if (kKitPatterns[i].ext < 1 || kKitPatterns[i].ext > 15 || kKitPatterns[i].fallback > 3) { fails++; fprintf(stderr, "[kit-variants] FAIL: pattern %d\n", (int)i); }
    if (KitVariants_ColourIndex(10) < KIT_BASE_COLOURS) { fails++; fprintf(stderr, "[kit-variants] FAIL: 10 is not purple\n"); }
    /* base colours: 0 grey 1 white 2 black 3 orange 4 red 5 blue 6 burgundy 7 light blue 8 green 9 yellow */
    static const struct { uint8_t a, b, alike; } k_known[] = {
        { 0, 1, 1 }, { 3, 4, 1 }, { 8, 9, 1 },           /* grey/white, orange/red, green/yellow */
        { 2, 5, 0 }, { 2, 8, 0 }, { 3, 9, 0 }, { 5, 7, 0 }, /* black/blue, black/green, orange/yellow, blue/light blue */
        { 1, 2, 0 }, { 4, 8, 0 },
    };
    for (size_t k = 0; k < sizeof k_known / sizeof k_known[0]; k++)
        if (KitVariants_Similar(k_known[k].a, k_known[k].b) != k_known[k].alike) {
            fails++; fprintf(stderr, "[kit-variants] FAIL: %d/%d should be %s\n", k_known[k].a, k_known[k].b,
                             k_known[k].alike ? "alike" : "apart");
        }
    printf("[kit-variants] self-test %s\n", fails ? "FAILED" : "passed");
    return fails != 0;
}
