#ifndef CHAMPIONS36_H
#define CHAMPIONS36_H

#include "game_types.h"

/* The substitute records play the presets AND season mode's slot-3 euro cup;
 * the career competition records are never changed. */
uint8_t *Champions36_PresetRecord(uint8_t *record);
int Champions36_Is(const DIYCompetition *competition);
int Champions36_IsLeague(const DIYCompetition *competition);
void Champions36_Fixture(unsigned index, uint8_t *home, uint8_t *away);
void Champions36_ConfigureLeague(DIYCompetitionEx *competition);
void Champions36_Advance(DIYTournamentState *tournament, DIYCompetitionEx *stage);
int Champions36_SelfTest(void);

/* SEASON mode enrolment (docs/CHAMPIONS36.md "Season mode"). A club's tier is
 * its place in the curated 3x36 seed lists; the slot-3 euroCupSlotIndex for
 * tier t is CHAMPIONS36_EUROSLOT_BASE + t (0..2 euro legacy, 3 Copa). */
#define CHAMPIONS36_EUROSLOT_BASE 4
int Champions36_TierForTeam(uint16_t teamId);      /* 0=CL 1=EL 2=Conf, -1 none */
uint8_t *Champions36_TierRecord(unsigned tier);    /* the tier's substitute record */

/* CAREER enrolment (docs/CHAMPIONS36.md "Career mode"): the tier's substitute
 * record seeded from THIS season's qualified field (the legacy cup copy's
 * 16/32-club list, the player's club already injected by qualification),
 * topped up to 36 from the curated tiers — skipping clubs contesting either
 * of the OTHER two euro cups this season, so no club plays two competitions.
 * All three lists are BE-packed {country, ordinal} byte pairs, as the *Copy
 * records store them. Returns NULL if 36 seeds can't be assembled (the
 * caller then claims the slot with the legacy record, unchanged). */
uint8_t *Champions36_CareerRecord(unsigned tier,
                                  const uint8_t *qualified, uint16_t qualifiedCount,
                                  const uint8_t *otherA, uint16_t otherACount,
                                  const uint8_t *otherB, uint16_t otherBCount);

/* Identity-only accessor: the tier's record ADDRESS without (re)building it.
 * For pointer comparisons (is _quaternaryCompetitionPtr this tier's record?);
 * Champions36_TierRecord would re-seed the record with the curated tier as a
 * side effect, clobbering a career's qualified field. */
uint8_t *Champions36_TierRecordIdentity(unsigned tier);

#endif
