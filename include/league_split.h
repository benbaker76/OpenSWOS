/*
 * league_split.h -- a world league division's mid-season split (src/league_split.c;
 * docs/WORLD_2026_27.md, phase 8).
 *
 * After its first match days (each pair `games` times), the table at that day is frozen
 * into groups of consecutive places -- Cyprus: 1-6 and 7-14 -- each of which plays on among
 * itself (no more rounds, a single or a double round robin), its points kept, halved
 * (rounded up or down) or halved from those won against the group's clubs; the final table
 * is ranked group by group, the points before rounding a tie-break. The days after the split
 * are the SWOS 96/97 Mod's (tools/split.py, compiled by scripts/world_split.py: the same
 * bytes), and so are the hosts of a single round robin ("balance": the club that hosted the
 * other fewer times; "reverseK": the club away in their K-th meeting). A division can also
 * carry only its own number of meetings (Scotland's lower divisions: 4).
 *
 * The engine's league runs on: its days counted and paired by the table from the split on;
 * the career club's calendar (the team selection key cache) rewritten for those days at the
 * freeze. The state is the league's own (LeagueSplitState, in the tail of its match days'
 * weeks), so it goes wherever the league goes -- its slot, the save, a scratch walk.
 */
#ifndef LEAGUE_SPLIT_H
#define LEAGUE_SPLIT_H

#include "swos_types.h"

/* A world's split divisions (src/world_data.c, generated): the country, the division of
 * its league record, the table (the Mod's SP_TAB entry bytes). */
typedef struct {
    uint8_t        country;
    uint8_t        division;
    const uint8_t *table;
} LeagueSplit;

const LeagueSplit *World_LeagueSplits(int32_t *count);

/* InitDIYCompetitionFromTableEntry, before DIY_RecomputeCompetitionDerivedFields: a split
 * division's cycles (its meetings, + 1 with a split: the engine's league must not end at
 * it); after it (`after` 1): its days and their weeks, the split's state. */
void LeagueSplit_Init(uint8_t country, uint16_t division, int32_t after);

/* getCurrentFixtureTeamPtrs: from the split on, the match's rows from the table (1), or 0
 * (the engine's pairing). */
int32_t LeagueSplit_Pair(uint8_t **team1, uint8_t **team2);

/* DIY_AdvanceLeagueProgressAfterMatch, after the played counts: from the split on, the
 * day's counters (1: done), or 0 (the engine's). */
int32_t LeagueSplit_Count(void);

/* DIY_PlayOrSimulateLeagueFixture, the result in the table: the head-to-head points, and a
 * split due after this match (the groups from the table with it). */
void LeagueSplit_AfterResult(uint8_t *team1, uint8_t *team2, uint16_t goals1, uint16_t goals2);

/* DIY_SortLeagueTableByStandings, after its sort: the groups, then the tie-break. */
void LeagueSplit_Sort(void);

/* The career calendar's walk of the season (DIY_RecordLeagueFixtureResultAndAdvanceProgress):
 * no split in it -- the places are the rows until the split is played. */
void LeagueSplit_Walking(int32_t on);

/* Career_AutoSimulateLeagueFixturesUntilHumanTeam: after the split the career club may play
 * no more today (a shorter group, an odd group's rest): 1 = the rest of the day was
 * simulated, the caller returns. */
int32_t LeagueSplit_RestOfDayWithoutCareerClub(void);
int32_t LeagueSplit_Pages(uint16_t *groups, uint16_t *perGroup);   /* a table of > 22: its zones as pages */

/* The career league's head-to-head points (a split league's, the career's; saved). */
#define LEAGUE_SPLIT_H2H 64
extern uint8_t g_leagueSplitH2H[LEAGUE_SPLIT_H2H];

#endif
