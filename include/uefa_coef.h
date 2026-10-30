/*
 * uefa_coef.h -- a world's UEFA coefficients and qualifying rounds (src/uefa_coef.c;
 * docs/WORLD_2026_27.md, phase 5): the SWOS 96/97 Mod's tools/uefacoef.py and the season-end
 * glue of tools/uefaq.py (MIT, Davide Lorigliola) around src/uefa_access.c.
 *
 * Points are in eighths (units): a league-phase win 16, a draw 8; Champions League league-phase
 * participation 48; the league phase's place bonus (24 up: 2 a place, the Conference League 1 for
 * places 9-24); each knockout round reached from the round of 16 12 / 8 / 4; a knockout tie's
 * winner 24, its loser 8, the final's winner 16; a qualifying tie's winner 12, its loser 4. A
 * club's coefficient is its points of the last 5 seasons, at least a fifth of its association's;
 * an association's season is its clubs' points / its clubs (thousandths of points), its
 * coefficient 5 seasons. The seeds (pack.json uefa_coef) start a career.
 *
 * The season end: the first European country closes the season (the finals, the qualifying ties,
 * the ranking, the 5-season ring), every country gives its top 9 and its cup winner, then the
 * access list's qualifying rounds fill the three league phases (the holders join after).
 */
#ifndef UEFA_COEF_H
#define UEFA_COEF_H

#include "swos_types.h"

/* A world's seeds (src/world_data.c, scripts/world_import.py). */
typedef struct {
    uint8_t  country;                   /* its league's team file */
    uint32_t seasons[5];                /* thousandths of points, oldest first */
} WorldUefaCountry;
typedef struct {
    uint8_t  country, ordinal;
    uint16_t units;                     /* a season's (the coefficient * 8 / 5) */
} WorldUefaClub;
typedef struct {
    const WorldUefaCountry *countries;  /* the world's UEFA associations, pack order */
    int32_t nCountries;
    const WorldUefaClub *clubs;
    int32_t nClubs;
    uint8_t cupOnly;                    /* the association with a cup winner and no league (Liechtenstein) */
} WorldUefaSeeds;
const WorldUefaSeeds *World_UefaSeeds(void);

#define UEFA_COEF_CLUBS 1024
typedef struct {
    uint16_t team;                      /* BE {country, ordinal}; 0 = free */
    uint16_t pts;                       /* this season's units */
    uint16_t hist[5];                   /* the ring's */
} UefaClubCoef;
typedef struct {
    uint8_t  init, ring, done, pad;
    uint8_t  teams[256];                /* an association's clubs this season (its divisor) */
    uint32_t pts[256];                  /* its units this season */
    uint32_t hist[5][256];              /* thousandths of points, the ring */
    uint16_t nClubs;
    UefaClubCoef club[UEFA_COEF_CLUBS];
    uint8_t  prevN[3];                  /* a cup's previous knockout round */
    uint16_t prev[3][16];
    uint16_t fin[3][2];                 /* a cup's final */
    uint8_t  phaseEnded[3];
} UefaCoefState;
extern UefaCoefState g_uefaCoef;
extern struct uq_state g_uqState;      /* (uefa_access.h: the season's qualifying bracket) */

void UefaCoef_Reset(void);              /* a new career / a save without it */

/* The league phases' hooks (src/league_phase.c); team words BE. compId 0..2. */
void UefaCoef_StageStart(uint8_t compId, const uint16_t *teams, int32_t n);
void UefaCoef_Result(uint8_t compId, uint16_t home, uint16_t away, uint16_t g1, uint16_t g2);
void UefaCoef_PhaseEnd(uint8_t compId, const uint16_t *order);  /* 36, best first */
void UefaCoef_Draw(uint8_t compId, int32_t stage, const uint16_t *teams, int32_t n);
uint32_t UefaCoef_PotKey(uint8_t compId, uint16_t team, uint32_t value);
uint32_t UefaCoef_Club(uint16_t team);  /* units, 5 seasons, the association floor included */
int32_t UefaCoef_On(void);              /* the world's cups are league phases with seeds */

/* The season end (src/career_eurocups.c, src/career_season.c). */
void UefaQual_Country(void);            /* a country's top 9 + cup winner (_primaryQualifiedTeamGrids) */
void UefaQual_Fill(void);               /* the qualifying rounds, the three lists */
int32_t UefaQual_KeepsUeclHolder(void); /* the UECL holder is in the Champions League list already */
uint16_t UefaQual_Listed(void);         /* clubs it put in the lists (the holders join after) */

#endif
