/*
 * world_cups.h -- a world's national cups as the SWOS 96/97 Mod's packs have them
 * (src/world_cups.c; docs/WORLD_2026_27.md, phase 4).
 *
 * A world cup is a fixed list of clubs in its record (as the Mod's: the pack's ties.csv): in
 * the world's first season round 1 is the real draw, its real ties first, and after it each
 * winner goes to its place in the real bracket (order1), each later round the winners of
 * ties 2k and 2k + 1 of the one before -- the real pairs, the home club first -- instead of
 * the engine's draw. A list can hold 64 clubs (the record carries the rest after it). From the second
 * season (the Mod's 4.0.1: tools/cupsf.py, gplay.c cf_build) the clubs come from the
 * divisions as they are that season, as many from each as the real list had (and as many
 * of them waiting for round 2): within a division the clubs relegated into it first, then
 * those that stayed, then the promoted, each by last season's final place; the career club
 * always plays when its division does. Round 1 is drawn among the clubs without a bye, every
 * later round too.
 *
 * Round 1 may have byes (the Coppa Italia Serie C: one tie, fifteen clubs waiting): the
 * bracket is the next power of two, the real ties first and each waiting club's tie with
 * itself after them, not played (as a world's play-off: include/world_playoff.h). The
 * record loads only its real clubs; the bracket's spare places hold its first club.
 *
 * A league cup (slot 2, England's in 96/97) is added to its country's table.
 */
#ifndef WORLD_CUPS_H
#define WORLD_CUPS_H

#include "swos_types.h"

typedef struct {
    uint8_t         country;
    uint8_t         slot;       /* 1 the national cup, 2 the league cup */
    void           *record;     /* its CompetitionCup (world_data.c) */
    uint8_t         n;          /* its clubs, listed in the record (a bracket of its teams) */
    uint8_t         ties;       /* round 1's real ties (the byes after them) */
    uint8_t         europe;     /* the winner takes the country's league-cup European place */
    uint8_t         rounds;     /* the rounds of the first season's real draw */
    const uint16_t *flips;      /* per later round, its ties played the other way round (bit k) */
    uint8_t         groups;     /* from the second season: per division of the real list, */
    const uint8_t (*quota)[3];  /* (its division, its clubs, how many of them have a bye) */
    const uint8_t  *order1;     /* round 1's ties as played (real ties first): each one's place
                                   in the bracket, where its winner goes after round 1 */
    uint8_t         numbered;   /* its rounds named by number (FIRST ROUND ..), from */
    uint8_t         firstRound; /* this one (the EFL Cup: from its 4th) */
} WorldCup;

const WorldCup *World_Cups(int32_t *count);     /* (world_data.c, generated) */

/* The career's seasons in the world (saved with it): its first plays the real draw. */
typedef struct {
    uint8_t  seasons;           /* the seasons ended (255: and more) */
    uint8_t  leagueCupSeason;   /* the season (seasons) its league cup's winner is of */
    uint16_t leagueCupWinner;   /* {country, ordinal}, 0: not decided yet */
    uint8_t  placesSeason;      /* the season (seasons) the places are of */
    uint8_t  placesCountry;     /* its country, 0: none kept */
    uint8_t  place[256];        /* per ordinal: its final place (1..31) | its division's level << 5 */
} WorldCupsState;

extern WorldCupsState g_worldCups;

/* A new career. */
void WorldCups_Reset(void);

/* The season end's competitions are done (Career_AdvanceToNextSeason_UpdateCompetitions):
 * the season to come is not the first. */
void WorldCups_SeasonEnded(void);

/* Once the competition tables are up (the SDL: after the JSON's): each world cup into its
 * country's table, registered by its id. */
void WorldCups_Install(void);

/* A cup in the country's table the career club plays in (any other cup: 1). A world cup
 * is a fixed list: a club not in it does not have it in its slot -- the original's
 * slots 1 and 2 always hold the club (FindTeamInDIY panics otherwise); it is played in
 * the background, as another country's. */
int32_t WorldCups_Plays(const uint8_t *record);

/* Career_InitCompetitionTeamSelectionFromTableHeaderSpec: the clubs to load of a cup's
 * listed `count` (a world cup's: its real ones), then the bracket's spare places. */
uint16_t WorldCups_Entrants(const uint8_t *record, uint16_t count);
void     WorldCups_Padded(const uint8_t *record);

/* InitDIYCupFromRecord's end: round 1 -- the byes laid out, a later season's draw. */
void WorldCups_CupMade(const uint8_t *record);

/* Right after the engine draws the next round (DIY_ReorderTeamGridByOrderList): the first
 * season's real bracket instead. */
void WorldCups_ArrangeNextRound(void);

/* The season end has a league's final table (league_playoffs.c, before the moves): each
 * club's place, for the next season's cups (gplay.c cf_cap_c). */
void WorldCups_KeepPlace(uint8_t country, uint16_t division, uint16_t team, uint8_t place);

/* A tie was decided (ProcessDIYKnockoutMatchResultAndCareerUpdates, played or simulated):
 * a world league cup's final -- its winner kept for the season. */
void WorldCups_TieDecided(uint16_t team1, uint16_t team2, int32_t team2Won);

/* The country's league cup winner this season, 0 if not decided (the Coppa Italia Serie
 * C's: a place in Serie C's play-off, dplay.c cup_place). */
uint16_t WorldCups_LeagueCupWinner(uint8_t country);

/* Its league cup played now, when the career club is not in it (a background cup is
 * played at the season end): for a play-off that needs its winner before. */
void WorldCups_PlayLeagueCupEarly(uint8_t country);

/* A round's name in the cup round tables (GetNextCupRoundStringIndex) for a world cup under
 * way, -1 for the engine's: its round of 16 by that name (the Mod's rnames), the rounds
 * before it numbered from the first. */
int32_t WorldCups_RoundStringIndex(uint8_t compId, uint16_t round, uint16_t rounds);

/* The season end has cached the league cup's winner for Europe: 0 if it takes no place. */
int32_t WorldCups_LeagueCupEurope(const uint8_t *record);

#endif
