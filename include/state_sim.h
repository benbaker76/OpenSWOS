/*
 * state_sim.h -- a world's state championships at the season end (src/state_sim.c, the Mod's
 * statesim.c; docs/WORLD_2026_27.md).
 */
#ifndef STATE_SIM_H
#define STATE_SIM_H

#include "swos_types.h"

#define STATE_SIM_STATES 13
#define STATE_SIM_CLUBS 16
#define STATE_SIM_RANK (STATE_SIM_STATES * (4 + 2 * STATE_SIM_CLUBS) + 2)   /* (ST_RANK's room: every block + 0) */

/* One season end: SS_TAB the country's states, ST_RANK block 0 the played state's real order (dw 0
 * none) -> every state's block, the relegated last; SEASONW the season (the draws' seed). */
void ss_season(const unsigned char *SS_TAB, unsigned char *ST_RANK, unsigned short SEASONW);
extern unsigned char ss_ord[STATE_SIM_STATES * STATE_SIM_CLUBS];
extern unsigned short ss_champ[STATE_SIM_STATES];

#endif
