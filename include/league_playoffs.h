/*
 * league_playoffs.h -- a world's play-offs and play-outs at the season end
 * (src/league_playoffs.c; docs/WORLD_2026_27.md, phase 4).
 *
 * The original engine promotes and relegates by place: a division's top `promoted`, its
 * bottom `relegated`. A world's league can decide some of those places by knockout ties
 * after the season -- Serie B's third promotion by a play-off of the 3rd-8th, its fourth
 * relegation by a play-out of the 16th and 17th, a Serie C group's by play-outs. They are
 * decided here, as the SWOS 96/97 Mod decides them (ported: its dplay.c and gplay.c),
 * from every division's final table, and simulated: the career club's ties too, for now.
 *
 * The league's own record keeps BALANCED counts, so the season's one-division moves pass
 * the original's balanced-moves check as ever; after it, each club whose place the Mod's
 * rules send elsewhere gets its move (one level up, down, or none) in the scratch table,
 * which the parallel-groups pass then places (include/league_groups.h).
 */
#ifndef LEAGUE_PLAYOFFS_H
#define LEAGUE_PLAYOFFS_H

#include "swos_types.h"

/* A league's play-off rules (src/world_data.c, generated from the Mod's pack.json). */
typedef struct {
    uint8_t        country;
    uint8_t        divisions;
    const uint8_t *promoted;    /* per division, the Mod's counts by place (the play-offs */
    const uint8_t *relegated;   /* add the rest); a division of the lowest level: ignored */
    uint8_t        italy;       /* the Italian rules (dplay.c): Serie B and its groups */
    uint8_t        serieB, groupFirst, groups;
    const uint8_t *spec;        /* the generic ones (gplay.c's compiled bytes), 0-ended */
    uint8_t        seconds;     /* B teams: (its ordinal, its first team's or 0xFF) in the */
    const uint8_t (*second)[2]; /* team file; the highest level one may reach (0xFF any), */
    uint8_t        secondTop;   /* flags: 1 play-offs allowed, 2 it may leave the league */
    uint8_t        secondFlags; /* from the lowest division (the pool) */
    const char *const (*names)[2];      /* each play-off's names (long, short), spec order */
    uint8_t        nnames;
    const uint8_t *then;        /* each play-off's 'then_build': the one built when it ends + 1 */
} LeaguePlayoffs;

const LeaguePlayoffs *World_LeaguePlayoffs(int32_t *count);

/* The season end of the career country's primary league: Begin before its divisions are
 * played out, KeepTable after each (its final table in the DIY competition), Decide after
 * the balanced-moves check (DIY_ValidateLeagueScratchBuffer). */
/* A league's stage tables (the Mod's GP_SS bank: gplay.c gp_stage), kept at a fixture stage's
 * end (src/league_split.c, the split table's stage 'table'): r[k] in the table's order; the
 * knockouts' 'stage_place' entrants 4..7 read r[0..3] (Argentina: 4 the Apertura, 5 the
 * Clausura, 6 the annual table -- r[2] = r[0] + r[1] by club when the stage is 'annual').
 * One bank per league (its country), saved with the career: the career league's stages are
 * played through the season, the others' at its end. */
#define LEAGUE_STAGE_TABLES 4
#define LEAGUE_STAGE_BANKS  24
typedef struct { uint16_t team, glob, won, gf, ga, pts; } LeagueStageRow;
typedef struct {
    uint8_t used, key, fresh, pad;      /* key: the league's country; fresh: bit k = r[k] this season's */
    uint8_t n[LEAGUE_STAGE_TABLES];
    LeagueStageRow r[LEAGUE_STAGE_TABLES][32];
    uint8_t keptDiv, nKept;             /* a 'keep' stage's frozen table (its division; 0 clubs: none) */
    LeagueStageRow kept[32];
} LeagueStageBank;
extern LeagueStageBank g_leagueStages[LEAGUE_STAGE_BANKS];
void LeaguePlayoffs_ResetStages(void);
/* A fixture stage just over (league_split.c): the DIY league's table into its league's bank. */
void LeaguePlayoffs_StageTable(uint8_t country, uint32_t k, uint32_t annual);
/* A 'keep' stage's table frozen (Malta's Opening Round, its title play-off's seeds): the
 * division's kept table, read at the season end (the Mod's gp_keep). */
void LeaguePlayoffs_KeepStage(uint8_t country, uint16_t division);

const uint8_t *LeaguePlayoffs_SpecAt(uint8_t country, int32_t pidx);
int32_t LeaguePlayoffs_TopTable(uint16_t *teams, int32_t cap);
int32_t LeaguePlayoffs_ContestEntrants(uint8_t country, int32_t d, const LeagueStageRow *table, int32_t n,
                                       int32_t pidx, uint16_t *team, uint16_t *glob, uint8_t *prio,
                                       uint16_t *pts, const uint8_t **spec);

void LeaguePlayoffs_Begin(void);
void LeaguePlayoffs_KeepTable(const uint8_t *competition, uint16_t division);
void LeaguePlayoffs_Decide(uint16_t country, const int8_t *table, int8_t *scratch);

/* Serie B's play-out is held unless the 16th leads the 17th by more than 4 points
 * (dplay.c OUT_HELD); LeaguePlayoffs_TestHeld (a test switch) holds it anyway. */
int32_t LeaguePlayoffs_OutHeld(uint16_t pts16, uint16_t pts17);
/* Serie B's play-off is played unless the 3rd leads the 4th by more than 14 points. */
int32_t LeaguePlayoffs_PlayoffHeld(uint16_t pts3, uint16_t pts4);
extern int32_t LeaguePlayoffs_TestHeld;

#endif
