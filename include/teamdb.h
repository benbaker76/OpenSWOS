/*
 * teamdb.h -- a team database mod: another season's teams in 96/97's game, as SWOS 2020's team
 * database DLC (src/teamdb.c; docs/MODS.md §10.4). SDL builds.
 */
#ifndef TEAMDB_H
#define TEAMDB_H

#include "swos_types.h"

#ifdef SWOS_SDL_BUILD
/* Once, after the competition graph is installed (frontend.c): when an enabled mod has
 * data_json/teamdb.json, its leagues' fix-ups, cups' draw lists, nationality codes and season. */
void     TeamDb_Apply(void);
int32_t  TeamDb_On(void);
/* Its "season_year" (the career's first season, World_FirstYear), 0 without one. */
uint16_t TeamDb_SeasonYear(void);
#endif

#endif /* TEAMDB_H */
