/*
 * fixture_rounds.h -- real match days for leagues of an odd number of clubs
 * (src/fixture_rounds.c; docs/WORLD_2026_27.md, phase 4).
 *
 * The original's pair tables for an odd number of clubs (_fixtureSchedulePtrTable[n - 2],
 * n = 5..23) are no rounds: the league counts n / 2 pairs as a match day, and in those tables
 * many days have a club twice and others none. A DIY league plays them anyway; a career
 * league refuses them (InitDIYCompetitionFromTableEntry, panic 0x1313), because its calendar
 * holds one match of the career club a day. A world other than 96/97 gets them as rounds.
 */
#ifndef FIXTURE_ROUNDS_H
#define FIXTURE_ROUNDS_H

#include "swos_types.h"

/* Rewrite the odd tables as rounds: the same pairs, one club at rest a day. */
void FixtureRounds_Install(void);

/* Whether a league of n clubs plays in rounds (every club at most once a match day). */
int32_t FixtureRounds_Real(int32_t n);

#endif
