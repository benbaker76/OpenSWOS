#ifndef KIT_VARIANTS_H
#define KIT_VARIANTS_H

/*
 * kit_variants.h -- the kit colour variants and the rule for which kits are too alike.
 *
 * The data is scripts/kit_variants/kit_variants.json, made into src/kit_variants_table.inc by
 * scripts/gen_kit_variants.py: the 10 base colours, their variants, and which colours are too
 * alike (CIEDE2000 < 36, worked out by the script, so every machine gives the same answer).
 *
 * Two rules decide whether two kit colours clash (team_colors.c AreBasicColorsConflicting,
 * AreShortsAndStripeColorsConflicting):
 *   - ORIGINAL: the original's two hand-made tables;
 *   - EXTENDED: the table worked out by CIEDE2000, for every colour (the default; [graphics]
 *     kit_colours). The real Amiga always plays EXTENDED.
 * A net game -- and a recording played again -- takes the rule from the match setup
 * (netplay_setup.c), so both games choose the same kits; a setup from before has none and is
 * played by the ORIGINAL rule, as it was recorded.
 */

#include <stdint.h>

#define KIT_RULE_ORIGINAL  0
#define KIT_RULE_EXTENDED  1

/* [graphics] kit_colours: this game's own choice, offline. */
void    KitVariants_SetRule(int32_t rule);
int32_t KitVariants_Rule(void);
/* The rule the match being set up is played by: the setup's in a net game or a replayed
 * recording, this game's own otherwise. */
int32_t KitVariants_MatchRule(void);

/* A kit colour byte -> its colour index (0-9 the base colours, then the variants), or -1. */
int32_t KitVariants_ColourIndex(uint8_t colour);
/* EXTENDED: 1 when the two kit colour bytes are too alike. */
int32_t KitVariants_Similar(uint8_t colour1, uint8_t colour2);

int32_t KitVariants_SelfTest(void);

#endif
