/*
 * states.h -- a world's state championships at the season end (src/states.c over state_sim.c
 * and state_pool.c; docs/WORLD_2026_27.md).
 */
#ifndef STATES_H
#define STATES_H

#include "swos_types.h"

void States_Reset(void);                /* a new career: the pools' first order, no champions */
void States_SeasonEnd(uint8_t country); /* its states simulated, their pools' swaps */
extern uint16_t g_stateChampions[13];   /* last season's champions (our team words; 0 none) */
extern uint16_t g_stateSeasons;         /* the season ends simulated (the draws' season word) */

/* S2: the career club's own state played, in slot 2 (states.c). */
extern uint8_t g_stateCup;              /* the state it plays (0xFF none; saved) */
extern uint8_t g_stateRank0[4 + 2 * 16];   /* its played ranking (the season end's block 0; saved) */
uint8_t *States_SlotContest(void);      /* the season start's: the career club's state's contest, 0 */
uint8_t *States_RestoreContest(void);   /* a loaded career's */
int32_t States_StageDays(uint8_t compId, uint16_t groups, uint16_t perGroup);
void States_StageStart(void);
int32_t States_Pair(uint8_t **team1, uint8_t **team2);
void States_GroupsEnd(uint8_t *diy, int32_t after);
void States_KnockoutStart(void);
void States_KnockoutEnd(void);

#endif
