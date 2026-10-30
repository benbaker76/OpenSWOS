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

/* A title of the SEASON WINNERS page (src/world_winners.c; the Mod's winners.py): division
 * (0xFE: a state championship), its title (0: the contest's own name), its source as the Mod's
 * fmtgame.win_lines writes it. */
typedef struct {
    uint8_t     division;
    const char *title;
    const char *src;
} LeagueWinLine;

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
    const uint8_t (*rounds)[12];        /* each play-off's game rounds' names (_cupRoundStringTable's
                                         * indices, 0xFF the game's): the pack's 'round_names' */
    const char *const *records;         /* each play-off's season record line, won (the pack's 'record') */
    const char *const *combined;        /* the stage titles won together: a line per mask of stages 0..2,
                                         * 8 a division (32; 0 none) -- or 0 */
    const LeagueWinLine *winners;       /* the SEASON WINNERS page's titles, division order */
    uint8_t        nwinners;
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

/* Argentina's promedio history (league_playoffs.c, the Mod's PR_STATE): each top-flight club's
 * points and matches of the two seasons before, by global number; saved ("promedio"). */
#define PROMEDIO_CLUBS 48
typedef struct { uint16_t glob, p[2]; uint8_t m[2]; } LeaguePromedioClub;
typedef struct { uint8_t init, n, pad[2]; LeaguePromedioClub c[PROMEDIO_CLUBS]; } LeaguePromedio;
extern LeaguePromedio g_promedio;
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
/* The same over several divisions' tables (divs[0] the club's, then the ones simulated early). */
int32_t LeaguePlayoffs_ContestEntrantsEx(uint8_t country, int32_t nt, const int32_t *divs,
                                         const LeagueStageRow *const *tables, const int32_t *ns, int32_t pidx,
                                         uint16_t *team, uint16_t *glob, uint8_t *prio, uint16_t *pts,
                                         const uint8_t **spec);
/* A group contest (gplay.c t2_shape: a title of g groups of m entrants, meet meetings, then nk
 * knockout stages of kt ties, the first taking q -- the groups' winners and best runners-up --
 * kb their round bytes): play-off pidx's shape, 0 when it is not one. */
typedef struct { uint8_t g, m, meet, nk, q, kt[4], kb[4]; } LeagueGroupShape;
int32_t LeaguePlayoffs_GroupShape(uint8_t country, int32_t pidx, LeagueGroupShape *shape);
/* The career club's play-off at its league's end (the Mod's gp_need, a career): the first playable
 * one in spec order with the club (`club` in division d's final table) among its entrants -- its
 * index, the other divisions its entrants come from (to simulate now: at most two; nine for a
 * group contest -- other[] holds 9) in other[]; -2 - its index when it is a group contest; -1 none. */
int32_t LeaguePlayoffs_ClubPlayoff(uint8_t country, int32_t d, const LeagueStageRow *table, int32_t n,
                                   uint16_t club, uint8_t *other, int32_t *nother);
/* Right after ContestEntrants: a round-1 tie simulated at the contest's build, the career club
 * waiting (the Mod's presim): the seed (hseed of game round 0), then each tie of entrants a, b
 * (legs 3: a best of three); the winner, a or b. */
void LeaguePlayoffs_ContestSeed(int32_t pidx, const uint16_t *team, int32_t n);
int32_t LeaguePlayoffs_ContestTie(int32_t a, int32_t b, int32_t legs, int32_t host, int32_t level);

/* The SEASON WINNERS page's last season (the Mod's WN_ST, written by gp_season for the career club's
 * country): 'M' the division the club played in, 'L' each division's 1st, 'K' each play-off's winner,
 * 0x80 | play-off: a title play-off's last three ties' winners (idx: the tie); team words. `fresh`
 * when this season end wrote it (world_winners.c stamps the season); saved with the career. */
#define LEAGUE_WINNERS_MAX 40
typedef struct { uint8_t type, idx; uint16_t team; } LeagueWinnersEntry;
typedef struct { uint8_t season, n, fresh, pad; LeagueWinnersEntry e[LEAGUE_WINNERS_MAX]; } LeagueWinners;
extern LeagueWinners g_leagueWinners;

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
