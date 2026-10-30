/*
 * conti_season.h -- the season's glue of a world's other confederations' cups
 * (src/conti_season.c over src/world_conti.c; docs/WORLD_2026_27.md).
 */
#ifndef CONTI_SEASON_H
#define CONTI_SEASON_H

#include "swos_types.h"

/* The season end's glue (src/conti_season.c). */
void Conti_Country(void);               /* a country's qualifiers: its table, top 9 and cup winner */
void Conti_SeasonEnd(void);             /* every country seen: the next editions */
void Conti_Reset(void);                 /* a new career */
/* Phase B: the career club's ties (_euroCupSlotIndex 7) and main rounds (8) in slot 3. */
uint8_t *Conti_SeasonTie(uint16_t *index);      /* the season start's (0 = none) */
uint8_t *Conti_RestoreTie(uint16_t index);      /* a loaded career's */
void Conti_TieDecided(uint16_t team1, uint16_t team2, int32_t team2Won);
void Conti_Daily(void);                         /* a day played: a decided tie -> the next */
void Conti_GroupsEnd(uint8_t *diy, int32_t after);  /* a group stage's end in a main round's contest */
int32_t Conti_RoundStringIndex(uint8_t compId); /* a tie's round name (_cupRoundStringTable), -1 */
int32_t Conti_TieIsFinal(void);                /* the tie in slot 3 was its cup's final */
int32_t Conti_TrophyTier(void);                 /* slot 3's (index 7 / 8): -1 none, else its cup's rank 0..2 */
int32_t Conti_ReplacesCopa(void);               /* a world's cups: the 96/97 Copa not the career club's */

#endif
