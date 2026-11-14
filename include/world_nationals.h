/*
 * world_nationals.h -- a classic world's national competitions (src/world_nationals.c;
 * docs/WORLD_2026_27.md §6n): the SWOS 96/97 Mod 3.1's Europe 1984-85 -- the World Cup and the
 * European Championship cycles of the era, as a national coach's career plays them.
 *
 * The data is the pack's (packs/europe-1984-85/national/pack.json "national"; scripts/world_import.py
 * -> src/world_data_8485.c): per 4-season cycle the World Cup's and the European Championship's
 * hosts, the European qualifying groups (their teams, how many go through), the non-European
 * qualifiers in their order; the holder; 2 points a win; a 24-team World Cup (the 1986 format: 6
 * groups) and an 8-team European Championship (2 groups); each national team's nationality and home
 * league in the world (or none: no job). The game's national-coach machinery plays it, these taking
 * the places of its draws and its 96/97 tables (careerworld.py w_nc, w_cyc, W_HOST, w_fill, w_wcfill,
 * w_ecfill, w_natcups, nat_table).
 */
#ifndef WORLD_NATIONALS_H
#define WORLD_NATIONALS_H

#include "swos_types.h"

typedef struct {
    uint8_t  teams, places;         /* a qualifying group: its teams (6 at most), how many go through */
    uint16_t team[6];               /* team words, file << 8 | ordinal */
} WorldNatGroup;

typedef struct {
    uint16_t       wcHost, ecHost;
    WorldNatGroup  wcq[8], ecq[8];
    uint8_t        direct;          /* the non-European qualifiers, in order: until the World Cup is full */
    const uint16_t *directTeam;
} WorldNatCycle;

typedef struct {
    uint8_t file, ordinal;          /* a national team of the world */
    uint8_t unused;                 /* not a nation of the era: out of the table */
    uint8_t nationality;            /* its players' (and coach's) nationality, 0: none -- no job */
    uint8_t home;                   /* its home league's country number (call-ups), 0: none */
} WorldNatRule;

typedef struct {
    uint16_t holder;                /* the World Cup's at the career's start */
    uint8_t  points, wcTeams, ecTeams;
    uint8_t  cycles;
    const WorldNatCycle *const *cycle;
    uint16_t rules;
    const WorldNatRule *rule;
} WorldNationals;

const WorldNationals *World_Nationals(void);    /* (the world's, src/world_baked.c; 0: the game's own) */

/* World_InstallBaked: the World Cup's and the European Championship's formats into their records,
 * the world's national teams table. */
void WorldNationals_Install(void);

/* A new career: the world's World Cup holder (career_init.c, after the game's). */
void WorldNationals_CareerStart(void);

/* A national coach's season: this season's cycle's hosts (career_init.c, in place of the game's
 * table); 1 when the world gave them. */
int32_t WorldNationals_Hosts(void);

/* The qualifying draws: the cycle's groups, the holder and the hosts out, in place of the game's
 * draw (career_season.c); 1 when the world filled them. */
int32_t WorldNationals_FillWorldCupGroups(void);
int32_t WorldNationals_FillEuroGroups(void);

/* The season end (career_eurocups.c): this season's cycle (0: the game's own), and the groups'
 * points, places and teams again (a loaded career's records are their templates'). */
const WorldNatCycle *WorldNationals_Cycle(void);
void WorldNationals_RefreshGroups(int32_t worldCup);

#endif
