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

#endif
