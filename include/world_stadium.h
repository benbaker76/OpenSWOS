/*
 * world_stadium.h -- a world's stadium names (src/world_stadium.c; docs/WORLD_2026_27.md).
 */
#ifndef WORLD_STADIUM_H
#define WORLD_STADIUM_H

#include "swos_types.h"

#define WORLD_STADIUM_NAME_MAX 48       /* (the Mod's: 48 small-font letters fit the screen) */

/* The ground of a match: a neutral final's venue (its competition and round as the game names
 * them, the career's season 1, 2, ...), else the home club's (its team file and ordinal); NULL:
 * none known (96/97, no stadiums.json in the world's data, the Amiga). */
const char *WorldStadium_Name(uint8_t file, uint8_t ordinal, const char *gameName,
                              const char *gameRound, int32_t season);

#endif
