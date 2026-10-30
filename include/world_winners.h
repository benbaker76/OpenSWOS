/*
 * world_winners.h -- the SEASON WINNERS page (src/world_winners.c; the SWOS 96/97 Mod's
 * winners.py, its titles from fmtgame.win_lines; docs/WORLD_2026_27.md, phase 4).
 *
 * The competition view's STATISTICS gets a WINNERS entry, shown for a world league division with a
 * title beyond its table (a play-off, a stage title, a stage table) and for a state championship.
 * Its page lists each title of the viewed division: its name, then its winner this season, IN
 * PROGRESS (started, no winner yet) or TO BE PLAYED; then, on a page of its own, the last season's
 * titles of the division the career club played in (league_playoffs.h, g_leagueWinners).
 */
#ifndef WORLD_WINNERS_H
#define WORLD_WINNERS_H

#include "swos_types.h"

/* The statistics menu's onInit / onReturn: the WINNERS entry shown or hidden. */
void CompetitionStatsInit(void);

/* The viewed contest (_diyCompetitionStart) has titles on the page: its WINNERS entry shown. */
int32_t WorldWinners_Available(void);

/* WINNERS (fire): the page. */
void CompetitionStatsWinners(void);

/* The page's onInit, its NEXT PAGE, its pages (the career autopilot's test views). */
void WorldWinnersMenuInit(void);
void WorldWinnersNextPage(void);
int32_t WorldWinners_Pages(void);

/* The season end, after LeaguePlayoffs_Decide: the last season's titles it wrote, stamped with
 * the season. */
void WorldWinners_SeasonDecided(void);

/* A new career: none. */
void WorldWinners_Reset(void);

#endif
