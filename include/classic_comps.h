#ifndef CLASSIC_COMPS_H
#define CLASSIC_COMPS_H

#include <stdint.h>

/* CLASSICS (SDL builds, src/classic_comps.c), the row above WORLD CUP in PRESET
 * COMPETITION's world list (teams.c SelectTeamsFinalMenu): a group, then a competition
 * from assets/classic/<group>/, then who controls each team. 1 when a competition is
 * loaded, and PresetCompetitionMenu is to start it (StartContest); 0 when the player
 * backed out, to the world list. */
int32_t ClassicComps_Choose(void);

/* 1 once, after ClassicComps_Choose loaded one: the preset chooser has closed and
 * that competition is to be started. */
int32_t ClassicComps_TakeReady(void);

#endif
