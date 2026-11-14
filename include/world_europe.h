/*
 * world_europe.h -- a world's European cups as knockouts (src/world_europe.c; docs/WORLD_2026_27.md
 * §6g): the SWOS 96/97 Mod's classic worlds (its careerworld.py "big world"), Europe 1984-85's
 * real format -- the Champions' Cup 32 clubs, the Cup Winners' Cup 32, the UEFA Cup 64, two legs a
 * round and a single final.
 *
 * The three records take 96/97's places in the career contest table (_careerContests[0..2], ids
 * 0 / 1 / 2), their lists the first season's clubs (the packs' first_europe). From the second season
 * each country sends its places (the packs' europe_places): the Champions' Cup its champion, the Cup
 * Winners' Cup its cup winner (else the cup's runner-up, else the league's next), the UEFA Cup the
 * league's next clubs; a cup's holder plays it as holder, a place of its own (places + 1 = the
 * cup's size), and a place it would have taken goes to the next club (the Mod's rule). Nothing of
 * 96/97's draw (its seed buckets, the holder swapped into a random place) plays then.
 */
#ifndef WORLD_EUROPE_H
#define WORLD_EUROPE_H

#include "game_types.h"

typedef struct {
    CompetitionCup *cup[3];         /* cc, cwc, uefa (a list past 32 carried after the record) */
    uint16_t        size[3];        /* each one's template size: the record and its whole list */
    const uint8_t (*places)[4];     /* per country: its number, its cc / cwc / uefa places */
    int32_t         count;
} WorldEuropeKO;

const WorldEuropeKO *World_EuropeKnockouts(void);   /* (the world's, src/world_baked.c; 0: none) */

/* World_InstallBaked: the three records into the career contest table and VIEW WORLD's Europe row. */
void WorldEurope_Install(const WorldEuropeKO *ko);

/* The world plays its European cups as knockouts. */
int32_t WorldEurope_Active(void);

/* A country's places (cc, cwc, uefa), 0: none. */
const uint8_t *WorldEurope_Places(uint8_t country);

/* No two clubs of one country in a tie of rounds 1 and 2 (the UEFA rule of the time, the Mod 3.0):
 * a cup's draw just made (InitDIYCupFromRecord: its record's id), and round 1 just played
 * (UpdateCompetitionProgress, the winners in their next ties). A career's cups 0..2 of a classic
 * world only. */
void WorldEurope_DrawMade(uint8_t compId);
void WorldEurope_RoundPlayed(void);

/* (A new career's first-season clubs: Career_LoadWorldEuropeTmd, src/career_teams.c.) */

#endif
