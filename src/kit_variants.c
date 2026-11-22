/*
 * kit_variants.c -- the kit colour variants' table and the EXTENDED clash rule
 * (include/kit_variants.h says what the two rules are).
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "kit_variants.h"
#include "game_types.h"
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

/* ---- kit bytes (include/kit_variants.h) ---- */

static int32_t is_purple(uint8_t colour)
{
    return colour == 10 || ((colour & 0x0F) == KIT_LEGACY_PURPLE_BASE && (colour >> 4) == KIT_LEGACY_PURPLE_VAR);
}

uint8_t KitColour_Base(uint8_t colour)
{
    const uint8_t base = colour == 10 ? KIT_LEGACY_PURPLE_BASE : colour & 0x0F;
    return base < KIT_BASE_COLOURS ? base : 0;
}

uint8_t KitColour_Var(uint8_t colour)
{
    if (colour == 10)
        return KIT_LEGACY_PURPLE_VAR;
    return (colour & 0x0F) < KIT_BASE_COLOURS ? colour >> 4 : 0;
}

uint8_t KitColour_Make(uint8_t base, uint8_t var)
{
    return base < KIT_BASE_COLOURS ? (uint8_t)(base | (var & 0x0F) << 4) : 0;
}

int32_t KitColour_Legacy(uint8_t colour)
{
    return is_purple(colour) ? 10 : KitColour_Base(colour);
}

uint8_t KitColour_FromLegacy(int32_t legacy)
{
    if (legacy == 10)
        return KitColour_Make(KIT_LEGACY_PURPLE_BASE, KIT_LEGACY_PURPLE_VAR);
    return legacy >= 0 && legacy < KIT_BASE_COLOURS ? (uint8_t)legacy : 0;
}

uint8_t KitColour_Normal(uint8_t colour)
{
    return KitColour_Make(KitColour_Base(colour), KitColour_Var(colour));
}

uint8_t KitPattern_Base(uint8_t shirtType)
{
    return (shirtType & 0x0F) <= 3 ? shirtType & 0x0F : 0;
}

uint8_t KitPattern_Ext(uint8_t shirtType)
{
    return shirtType >> 4;
}

static void each_kit_byte(TeamFile *t, int32_t baseOnly)
{
    uint8_t *colours[8] = { &t->prStripesColor, &t->prBasicColor, &t->prShortsColor, &t->prSocksColor,
                            &t->secStripesColor, &t->secBasicColor, &t->secShortsColor, &t->secSocksColor };
    uint8_t *types[2] = { &t->prShirtType, &t->secShirtType };
    for (int32_t i = 0; i < 8; i++)
        *colours[i] = baseOnly ? KitColour_Base(*colours[i]) : KitColour_Normal(*colours[i]);
    for (int32_t i = 0; i < 2; i++)
        *types[i] = baseOnly ? KitPattern_Base(*types[i])
                             : (uint8_t)(KitPattern_Base(*types[i]) | KitPattern_Ext(*types[i]) << 4);
}

void KitVariants_NormaliseTeam(struct TeamFile *team)
{
    if (team)
        each_kit_byte(team, 0);
}

void KitVariants_BaseTeam(struct TeamFile *team)
{
    if (team)
        each_kit_byte(team, 1);
}

/* ---- the kit appendix of a team file (include/kit_variants.h) ---- */

#define KIT_BYTES       10                          /* prShirtType .. secSocksColor */
#define KIT_OFFSET      ((uint32_t)offsetof(TeamFile, prShirtType))
#define RECORD_BYTES    684u
#define ORIGINAL_LIMIT  64000u                      /* the most of a team file the original loads */

static uint16_t file_count(const uint8_t *buf)
{
    uint16_t n;
    memcpy(&n, buf, 2);                             /* (host order once loaded) */
    return n <= TEAM_FILE_MAX_TEAMS ? n : 0;
}

void KitVariants_TeamFileRead(uint8_t *buf, uint32_t size)
{
    const uint16_t n = file_count(buf);
    const uint32_t end = 2 + (uint32_t)n * RECORD_BYTES;
    uint8_t *app = buf + end;

    if (size >= end + 6 && memcmp(app, "KVAR", 4) == 0) {
        const uint16_t m = (uint16_t)(app[4] << 8 | app[5]);
        if (m == n && size >= end + 6 + (uint32_t)n * KIT_BYTES)
            for (uint16_t i = 0; i < n; i++)
                memcpy(buf + 2 + i * RECORD_BYTES + KIT_OFFSET, app + 6 + i * KIT_BYTES, KIT_BYTES);
    }
    for (uint16_t i = 0; i < n; i++)
        KitVariants_NormaliseTeam((TeamFile *)(buf + 2 + i * RECORD_BYTES));
}

uint32_t KitVariants_TeamFileWrite(uint8_t *buf, uint8_t *saved)
{
    const uint16_t n = file_count(buf);
    const uint32_t end = 2 + (uint32_t)n * RECORD_BYTES;
    const uint32_t withAppendix = end + 6 + (uint32_t)n * KIT_BYTES;
    int32_t any = 0;

    for (uint16_t i = 0; i < n; i++) {
        uint8_t *kit = buf + 2 + i * RECORD_BYTES + KIT_OFFSET;
        memcpy(saved + i * KIT_BYTES, kit, KIT_BYTES);
        for (int32_t k = 0; k < KIT_BYTES; k++)
            any |= kit[k] >> 4;
        KitVariants_BaseTeam((TeamFile *)(buf + 2 + i * RECORD_BYTES));
    }
    /* The appendix only when a kit has more than the original knows -- and only where the
     * original can still load the file (it stops above 64000 bytes; one of more than 93 teams
     * it cannot load anyway). */
    if (!any || (end <= ORIGINAL_LIMIT && withAppendix > ORIGINAL_LIMIT))
        return end;
    memcpy(buf + end, "KVAR", 4);
    buf[end + 4] = (uint8_t)(n >> 8);
    buf[end + 5] = (uint8_t)n;
    memcpy(buf + end + 6, saved, (size_t)n * KIT_BYTES);
    return withAppendix;
}

void KitVariants_TeamFileRestore(uint8_t *buf, const uint8_t *saved)
{
    const uint16_t n = file_count(buf);
    for (uint16_t i = 0; i < n; i++)
        memcpy(buf + 2 + i * RECORD_BYTES + KIT_OFFSET, saved + i * KIT_BYTES, KIT_BYTES);
}

uint16_t KitVariants_Rgb12(uint8_t colour)
{
    const int32_t index = KitVariants_ColourIndex(colour);
    return index >= KIT_BASE_COLOURS ? kKitVariants[index - KIT_BASE_COLOURS].rgb : 0;
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
    /* the kit bytes: purple read in as its variant, back to 10 for the old tables, base colours alone */
    {
        const uint8_t violet = KitColour_Normal(10);
        if (KitColour_Base(violet) != KIT_LEGACY_PURPLE_BASE || KitColour_Var(violet) != KIT_LEGACY_PURPLE_VAR ||
            KitColour_Legacy(violet) != 10 || KitColour_Legacy(10) != 10 || KitColour_FromLegacy(10) != violet) {
            fails++; fprintf(stderr, "[kit-variants] FAIL: purple's colour byte\n");
        }
        for (int32_t c = 0; c < KIT_BASE_COLOURS; c++)
            if (KitColour_Normal((uint8_t)c) != c || KitColour_Legacy((uint8_t)c) != c || KitColour_FromLegacy(c) != c) {
                fails++; fprintf(stderr, "[kit-variants] FAIL: base colour %d's byte\n", (int)c);
            }
        if (KitColour_Legacy(KitColour_Make(5, 1)) != 5 || KitColour_Base(0x35) != 5 || KitPattern_Base(0x52) != 2 ||
            KitPattern_Ext(0x52) != 5 || KitPattern_Base(7) != 0) {
            fails++; fprintf(stderr, "[kit-variants] FAIL: a variant's or a pattern's byte\n");
        }
    }
    printf("[kit-variants] self-test %s\n", fails ? "FAILED" : "passed");
    return fails != 0;
}
