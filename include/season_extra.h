#ifndef SEASON_EXTRA_H
#define SEASON_EXTRA_H

/* season_extra.c -- a season's competition lines past the record's four (SeasonInformations:
 * the league and three). A season can hold more competitions than that -- a world's Super Cups
 * won after the league beside the league, the cup and the Champions League -- and every one is
 * shown in MANAGEMENT RECORD (the owner, 2026-11-27): up to SEASON_EXTRA_LINES more rows, kept
 * beside the season's record (not in it: its layout is the original's) and saved by name
 * ("season_extra_lines"). Both targets. */

#include <stdint.h>
#include "game_types.h"

#define SEASON_EXTRA_LINES 2
#define SEASON_EXTRA_NAME  32

typedef struct SeasonExtraLine {
    char   name[SEASON_EXTRA_NAME];   /* "" = unused */
    int8_t code;                      /* the result code, as comp1Result..comp4Result */
} SeasonExtraLine;

void SeasonExtra_Reset(void);                                   /* a new career / a save without them */
int32_t SeasonExtra_Index(const SeasonInformations *si);        /* the season's index, -1 */
void SeasonExtra_Clear(int32_t season);
int32_t SeasonExtra_Add(int32_t season, const char *name, int8_t code);   /* 0: no room */
int32_t SeasonExtra_Count(int32_t season);
const SeasonExtraLine *SeasonExtra_Get(int32_t season, int32_t i);
int32_t SeasonExtra_Has(int32_t season, const char *name);

#endif
