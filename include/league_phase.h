/*
 * league_phase.h -- the 36-club league phase of a world's European cups (src/league_phase.c;
 * docs/WORLD_2026_27.md, phase 5).
 *
 * The UEFA format from 2024-25, as the SWOS 96/97 Mod plays it (tools/lp36.py, MIT, Davide
 * Lorigliola): 36 clubs in pots (the contest's order = pot order) meet 8 different opponents,
 * one at home and one away from each of 4 pots of 9 -- the Conference League: 6, one from each
 * of 6 pots of 6, 3 at home -- never a club of their own country, at most 2 of one country, on
 * matchdays of 18 matches (every club once a day); one table of 36, the clubs 1-8 to the round
 * of 16, 9-24 to the knockout play-offs.
 */
#ifndef LEAGUE_PHASE_H
#define LEAGUE_PHASE_H

#include "swos_types.h"

#define LEAGUE_PHASE_CLUBS   36
#define LEAGUE_PHASE_MATCHES 144        /* (4 pots: 8 matchdays of 18; 6 pots: 108, 6 of 18) */

/* The pots (the Mod's lp_mkdraw): the 36 by key, strongest first (stable), dealt into the pots
 * in order, each slot the strongest club left whose country has fewer than 3 clubs in that pot
 * (the Conference League: 2) -- else the strongest left. order[p] = the club drawn at position
 * p (pot p / 9, or p / 6). */
void LeaguePhase_Pots(const uint32_t *key, const uint8_t *country, int32_t six, uint8_t *order);

/* The draw: country[c] of the 36 clubs in pot order (a byte that is equal for one country),
 * six = the Conference League's 6 pots of 6. Writes the matches (home, away) matchday by
 * matchday into cal: 144 (4 pots) or 108 (6 pots). Deterministic from seed. A field no draw
 * fits (too many clubs of one country: the Mod's draw would find none) is drawn with the
 * country rules relaxed, the cap of 2 first, then the own country. Returns 1 + how far it
 * relaxed (1: every rule held), 0 if none was found at all. */
int32_t LeaguePhase_Draw(const uint8_t *country, int32_t six, uint32_t seed, uint8_t (*cal)[2]);

/* The draw's rules (the Mod's lp36.check / check6), for the tests: 0 = every one holds, else the
 * number of the first rule broken. */
int32_t LeaguePhase_Check(const uint8_t *country, int32_t six, const uint8_t (*cal)[2]);

/* A club's figures for the ranking (its row's, and the league phase's own: away goals, away
 * wins, disciplinary points; its club coefficient; its name's place in name order). */
typedef struct {
    uint16_t points, goalsFor, goalsAgainst, wins;
    uint8_t  awayGoals, awayWins, discipline;
    uint8_t  nameRank;                  /* (how many of the 36 sort before it by name) */
    uint32_t coef;
} LeaguePhaseRow;

/* The table of the 36 (the Mod's lp_rank; UEFA regulations 2025-26 art. 18): points, goal
 * difference, goals, away goals, wins, away wins, then the opponents' points, goal
 * difference and goals (every match of the draw, played or not), fewer disciplinary points,
 * the club coefficient, the contest order; while the phase runs (finished 0) after the away
 * wins only the names. order[place] = the row, best first. */
void LeaguePhase_Rank(const LeaguePhaseRow *row, const uint8_t (*cal)[2], int32_t matches, int32_t finished,
                      uint8_t *order);

/* How far the last draw relaxed the country rules (0: not at all). */
int32_t LeaguePhase_Relaxed(void);

/* ---- the league phase in the engine (a world's European cups: type-2 contests whose first
 * stage is 2 groups of 18, the Mod's careerworld LP_STAGES) ---- */

/* The stage that DIYTournament_RecalculateMatchesPerTeam gives its days: ours -> its matchdays
 * (8, the Conference League 6), else 0. */
int32_t LeaguePhase_StageDays(uint8_t compId, uint16_t groups, uint16_t perGroup);

/* DIY_LoadCurrentCompetitionSlot, the group stage's league started: ours -> its counters, the
 * pots and the draw, the away figures cleared (the Mod's lp_start). */
void LeaguePhase_StageStart(void);

/* getCurrentFixtureTeamPtrs: ours -> the match's rows from the draw (1), else 0 (lp_pair). */
int32_t LeaguePhase_Pair(uint8_t **team1, uint8_t **team2);

/* DIY_PlayOrSimulateLeagueFixture, the result in the table: the away figures (lp_res). */
void LeaguePhase_Result(uint8_t *team1, uint8_t *team2, uint16_t goals1, uint16_t goals2);

/* DIY_SortLeagueTableByStandings: ours -> the one table of 36 over both groups, places 1-18
 * in the first group's rows, 19-36 in the second's (1: done), else 0 (lp_sort). */
int32_t LeaguePhase_Sort(void);

/* DIY_BuildSeededBracketAndUpdateScores, at the group stage's end: before (after 0) the final
 * table; after (1) the contest list's first 24: the play-off ties, then 1st-8th (lp_gend). */
void LeaguePhase_GroupsEnd(uint8_t *diy, int32_t after);

/* DIY_ShuffleTeamsAndBuildCompTeamList: a knockout stage after ours keeps its order (1: no
 * shuffle), the round of 16 its play-off winners against the seeds (lp_draw); else 0. */
int32_t LeaguePhase_KnockoutDraw(uint8_t *diy, uint16_t numTeams);

#endif
