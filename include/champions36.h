#ifndef CHAMPIONS36_H
#define CHAMPIONS36_H

#include "game_types.h"

/* Experimental preset only; the career competition record is never changed. */
uint8_t *Champions36_PresetRecord(uint8_t *record);
int Champions36_Is(const DIYCompetition *competition);
int Champions36_IsLeague(const DIYCompetition *competition);
void Champions36_Fixture(unsigned index, uint8_t *home, uint8_t *away);
void Champions36_ConfigureLeague(DIYCompetitionEx *competition);
void Champions36_Advance(DIYTournamentState *tournament, DIYCompetitionEx *stage);
int Champions36_SelfTest(void);

#endif
