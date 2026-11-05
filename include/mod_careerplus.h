/*
 * mod_careerplus.h -- Career+'s files (src/mod_careerplus*.c), to each other. Not an API.
 */
#ifndef MOD_CAREERPLUS_H
#define MOD_CAREERPLUS_H

#include <stdint.h>

#define CP_SLUG "careerplus"

/* the mod's random numbers (mod_careerplus.c): never the game's draws */
uint32_t CP_Rand(void);
int32_t  CP_RandBelow(int32_t n);       /* 0..n-1 */

#endif
