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

/* ---- the menus' order of colours ---- */

static int32_t menus_extended(void)
{
#ifdef SWOS_SDL_BUILD
    return s_rule == KIT_RULE_EXTENDED;
#else
    return 0;                       /* the real Amiga: the game's 11 */
#endif
}

static uint8_t colour_byte(int32_t index);

/* As a rainbow (kKitMenuOrder, the script's): red to pink, then white to black -- colour bytes. */
static const uint8_t *menu_order(void)
{
    static uint8_t order[KIT_COLOUR_COUNT];
    static int32_t built;
    if (!built) {
        built = 1;
        for (int32_t i = 0; i < KIT_COLOUR_COUNT; i++)
            order[i] = colour_byte(kKitMenuOrder[i]);
    }
    return order;
}

int32_t KitColour_StepCount(void)
{
    return menus_extended() ? KIT_COLOUR_COUNT : 11;
}

int32_t KitColour_StepIndex(uint8_t colour)
{
    if (!menus_extended())
        return KitColour_Legacy(colour);
    colour = KitColour_Normal(colour);
    for (int32_t i = 0; i < KIT_COLOUR_COUNT; i++)
        if (menu_order()[i] == colour)
            return i;
    for (int32_t i = 0; i < KIT_COLOUR_COUNT; i++)       /* (an unknown variant: at its base) */
        if (menu_order()[i] == KitColour_Base(colour))
            return i;
    return 0;
}

uint8_t KitColour_FromStepIndex(int32_t index)
{
    if (!menus_extended())
        return KitColour_FromLegacy(index);
    return index >= 0 && index < KIT_COLOUR_COUNT ? menu_order()[index] : 0;
}

int32_t KitVariants_Count(void)
{
    return KIT_VARIANT_COUNT;
}

uint8_t KitVariants_Byte(int32_t i)
{
    return i >= 0 && i < KIT_VARIANT_COUNT ? KitColour_Make(kKitVariants[i].base, kKitVariants[i].var) : 0;
}

uint8_t KitPattern_Base(uint8_t shirtType)
{
    return (shirtType & 0x0F) <= 3 ? shirtType & 0x0F : 0;
}

uint8_t KitPattern_Ext(uint8_t shirtType)
{
    return shirtType >> 4;
}

const char *KitPattern_File(uint8_t shirtType, int32_t amiga)
{
    const uint8_t ext = KitPattern_Ext(shirtType);
    if (!ext)
        return 0;
    for (int32_t i = 0; i < KIT_PATTERN_COUNT; i++)
        if (kKitPatterns[i].ext == ext)
            return kKitPatternFiles[i][amiga ? 1 : 0];
    return 0;
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

/* ---- clash kits (include/kit_variants.h) ---- */

/* The least difference of colour index `c` to the shirts (and to `other`, a colour index, or -1). */
static int32_t least_apart(int32_t c, const int32_t *shirt, int32_t count, int32_t other)
{
    int32_t least = 255;
    for (int32_t i = 0; i < count; i++)
        if (shirt[i] >= 0 && kKitDistance[c][shirt[i]] < least)
            least = kKitDistance[c][shirt[i]];
    if (other >= 0 && kKitDistance[c][other] < least)
        least = kKitDistance[c][other];
    return least;
}

/* Of `choices` the colour that differs most; if even that one is too alike, of every colour. */
static int32_t most_apart(const uint8_t *choices, int32_t n, const int32_t *shirt, int32_t count, int32_t other)
{
    int32_t best = -1, bestApart = -1;
    for (int32_t i = 0; i < n; i++) {
        const int32_t apart = least_apart(choices[i], shirt, count, other);
        if (apart > bestApart) { bestApart = apart; best = choices[i]; }
    }
    if (bestApart < KIT_SIMILAR_BELOW)
        for (int32_t c = 0; c < KIT_COLOUR_COUNT; c++) {
            const int32_t apart = least_apart(c, shirt, count, other);
            if (apart > bestApart) { bestApart = apart; best = c; }
        }
    return best;
}

static uint8_t colour_byte(int32_t index)
{
    return index < KIT_BASE_COLOURS ? (uint8_t)index
                                    : KitColour_Make(kKitVariants[index - KIT_BASE_COLOURS].base,
                                                     kKitVariants[index - KIT_BASE_COLOURS].var);
}

static int32_t shirt_indices(const uint8_t *shirts, int32_t count, int32_t *out)
{
    int32_t n = 0;
    for (int32_t i = 0; i < count && n < 8; i++)
        out[n++] = KitVariants_ColourIndex(shirts[i]);
    return n;
}

int32_t KitVariants_KeeperColour(const uint8_t *shirts, int32_t count)
{
    int32_t shirt[8], n = shirt_indices(shirts, count, shirt), clash = 0;
    for (int32_t i = 0; i < n; i++)
        clash |= shirt[i] >= 0 && ((kKitSimilar[KIT_KEEPER_DEFAULT] >> shirt[i]) & 1u);
    if (!clash)
        return colour_byte(KIT_KEEPER_DEFAULT);     /* his own: dark green (kit_variants.json) */
    /* By tiers -- greens, then greys: of the first tier that has a colour apart
     * from both teams, the one that differs most; failing all, any colour. */
    for (int32_t t = 0, from = 0; t < KIT_KEEPER_TIERS; from = kKitKeeperTierEnd[t++]) {
        int32_t best = -1, bestApart = -1;
        for (int32_t i = from; i < kKitKeeperTierEnd[t]; i++) {
            const int32_t apart = least_apart(kKitKeeperChoices[i], shirt, n, -1);
            if (apart > bestApart) { bestApart = apart; best = kKitKeeperChoices[i]; }
        }
        if (bestApart >= KIT_SIMILAR_BELOW)
            return colour_byte(best);
    }
    return colour_byte(most_apart(kKitKeeperChoices, KIT_KEEPER_CHOICES, shirt, n, -1));
}

int32_t KitVariants_RefereeColour(const uint8_t *shirts, int32_t count, int32_t keeper)
{
    int32_t shirt[8], n = shirt_indices(shirts, count, shirt), clash = 0;
    const int32_t black = 2, kept = keeper >= 0 ? KitVariants_ColourIndex((uint8_t)keeper) : KIT_KEEPER_DEFAULT;
    for (int32_t i = 0; i < n; i++)
        clash |= shirt[i] >= 0 && ((kKitSimilar[black] >> shirt[i]) & 1u);
    /* A keeper who had to change to a colour too like black (navy, burgundy, black itself ...): the
     * referee changes too. Not for the keepers' own dark green, beside his black as ever. */
    if (kept >= 0 && kept != KIT_KEEPER_DEFAULT && ((kKitSimilar[black] >> kept) & 1u))
        clash = 1;
    if (!clash)
        return -1;
    return colour_byte(most_apart(kKitRefereeChoices, KIT_REFEREE_CHOICES, shirt, n, kept));
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
    /* the menus' order: every colour once, each base before its variants, there and back */
    {
        const int32_t was = s_rule;
        s_rule = KIT_RULE_EXTENDED;
        if (menus_extended()) {
            if (KitColour_StepCount() != KIT_COLOUR_COUNT) {
                fails++; fprintf(stderr, "[kit-variants] FAIL: the menus' order\n");
            }
            for (int32_t i = 0; i < KitColour_StepCount(); i++) {    /* every colour once, there and back */
                const uint8_t c = KitColour_FromStepIndex(i);
                if (KitColour_StepIndex(c) != i) {
                    fails++; fprintf(stderr, "[kit-variants] FAIL: step %d\n", (int)i);
                }
            }
        }
        s_rule = KIT_RULE_ORIGINAL;
        if (KitColour_StepCount() != 11 || KitColour_FromStepIndex(10) != KitColour_Normal(10) ||
            KitColour_StepIndex(KitColour_Make(5, 1)) != 5) {
            fails++; fprintf(stderr, "[kit-variants] FAIL: the original's 11 steps\n");
        }
        s_rule = was;
    }
    /* clash kits: the keepers in dark green unless a shirt is too alike, then another green away
     * from it; the referee black unless a shirt is dark, then a colour away from shirts and keepers */
    {
        const uint8_t red[2] = { 4, 9 }, darkGreen[1] = { colour_byte(KIT_KEEPER_DEFAULT) }, black[2] = { 2, 1 };
        const int32_t own = KitVariants_KeeperColour(red, 2);
        const int32_t other = KitVariants_KeeperColour(darkGreen, 1);
        const int32_t ref = KitVariants_RefereeColour(black, 2, own);
        if (own != colour_byte(KIT_KEEPER_DEFAULT) || KitVariants_RefereeColour(red, 2, own) != -1) {
            fails++; fprintf(stderr, "[kit-variants] FAIL: keepers' and referee's own colours\n");
        }
        if (other == own || KitVariants_Similar((uint8_t)other, darkGreen[0])) {
            fails++; fprintf(stderr, "[kit-variants] FAIL: the keepers against dark green (%d)\n", (int)other);
        }
        if (ref < 0 || KitVariants_Similar((uint8_t)ref, 2) || KitVariants_Similar((uint8_t)ref, 1) ||
            KitVariants_Similar((uint8_t)ref, (uint8_t)own)) {
            fails++; fprintf(stderr, "[kit-variants] FAIL: the referee against black and white (%d)\n", (int)ref);
        }
        {   /* a keeper in black (his last resort): the referee must not stay black beside him */
            const uint8_t two[2] = { 0, 1 };
            if (KitVariants_RefereeColour(two, 2, 2) < 0) {
                fails++; fprintf(stderr, "[kit-variants] FAIL: the referee stays black beside a black keeper\n");
            }
        }
    }
    /* patterns: the original's have no file of ours; one of ours without a sprite set yet none either */
    if (KitPattern_File(2, 0) || KitPattern_File(2, 1) || (KIT_PATTERN_COUNT && !kKitPatternFiles[0][0] &&
        KitPattern_File((uint8_t)(kKitPatterns[0].fallback | kKitPatterns[0].ext << 4), 0))) {
        fails++; fprintf(stderr, "[kit-variants] FAIL: a pattern's file\n");
    }
    printf("[kit-variants] self-test %s\n", fails ? "FAILED" : "passed");
    return fails != 0;
}
