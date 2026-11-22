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

/* ---- The kit bytes of a team (TeamFile) ----
 * A colour byte: the base colour (0-9) in the low nibble, the variant (1-15, 0 = none) in the high
 * one. The original and every file it reads see the base colour alone (variant 0); 10 is the
 * World 2026-27 data's purple, read in as its variant of burgundy (KitColour_Normal).
 * A shirt type byte: the original's pattern (0-3) in the low nibble, a pattern of ours (1-15,
 * 0 = none) in the high one; without its sprites a pattern shows as the original's. */
uint8_t KitColour_Base(uint8_t colour);
uint8_t KitColour_Var(uint8_t colour);
uint8_t KitColour_Make(uint8_t base, uint8_t var);
/* 0-9, or 10 for purple: the index the game's 11-colour tables take (_shirtColorTable,
 * _kTeamColorsTable, the menus') -- a variant shows as its base colour there until it has its
 * own colour. */
int32_t KitColour_Legacy(uint8_t colour);
/* The other way: an index of those tables (0-10) as a colour byte (10: purple's variant). */
uint8_t KitColour_FromLegacy(int32_t legacy);
/* A colour byte as it is kept: 10 as purple's variant, an unknown base as 0. */
uint8_t KitColour_Normal(uint8_t colour);
uint8_t KitPattern_Base(uint8_t shirtType);
uint8_t KitPattern_Ext(uint8_t shirtType);

struct TeamFile;
/* Every kit byte of a team as it is kept (KitColour_Normal, a shirt type's base 0-3): after
 * it is read from any file. */
void KitVariants_NormaliseTeam(struct TeamFile *team);
/* The team as the original reads it: base colours and patterns only (for a binary file). */
void KitVariants_BaseTeam(struct TeamFile *team);

/* A team file in _teamFileBuffer (host order: the count word, then the records):
 *   TeamFileRead  -- after it is loaded (`size` bytes): every team's kit bytes as they are kept,
 *                    and the variants back from the "KVAR" appendix if the file has it;
 *   TeamFileWrite -- before it is written: the records as the original reads them (base colours
 *                    and patterns), the appendix after them; returns the bytes to write, and
 *                    keeps the teams' own kit bytes in `saved` (TEAM_FILE_MAX_TEAMS * 10) for
 *   TeamFileRestore -- after it is written. */
void     KitVariants_TeamFileRead(uint8_t *buf, uint32_t size);
uint32_t KitVariants_TeamFileWrite(uint8_t *buf, uint8_t *saved);
void     KitVariants_TeamFileRestore(uint8_t *buf, const uint8_t *saved);

int32_t KitVariants_SelfTest(void);

#endif
