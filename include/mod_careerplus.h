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

struct ModCompetitionResult;
struct ModJsonWriter;

/* PAST WINNERS (mod_careerplus_winners.c) */
void CP_Winners_Init(void);
void CP_Winners_Clear(void);
void CP_Winners_Record(const struct ModCompetitionResult *result);
void CP_Winners_Save(struct ModJsonWriter *out);
void CP_Winners_Load(const char *json, int32_t len);   /* (json NULL: none) */

/* the player rows (mod_careerplus_rows.c) */
struct ModPlayerRow;
void CP_Rows_Drawn(const struct ModPlayerRow *row);

#endif
