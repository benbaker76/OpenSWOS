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

#endif
