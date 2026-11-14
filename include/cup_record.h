/*
 * cup_record.h -- what a world career keeps of its cup matches for the cups' SUMMARY (src/cup_record.c;
 * docs/WORLD_2026_27.md 6m), as the SWOS 96/97 Mod 4.1 keeps it (its STAT > SUMMARY, ctview's record):
 *
 *   - the career club's European cup (a world's league-phase cup, slot 3): the league phase's final
 *     table (36 clubs, points) and every knockout match after it (the play-offs to the final, all the
 *     contest's ties), this season's;
 *   - the other confederations' cups (conti_season.c's contests, CONTI_TIE / CONTI_MAIN): every match
 *     the game played of them (the career club's group and ties: their scores, which the conti engine
 *     does not have), this edition's.
 *
 * Kept with the career (save_format_json.c "cup_record"); a save without it starts empty. Worlds only:
 * 96/97 records nothing.
 */
#ifndef CUP_RECORD_H
#define CUP_RECORD_H

#include "swos_types.h"
#include "game_types.h"

#define CUP_RECORD_TABLE 36
#define CUP_RECORD_GAMES 96             /* (the European knockouts: 45 at most) */
#define CUP_RECORD_CONTI 160            /* (a conti main round's groups: 96, its knockouts) */

enum {                                  /* a match's flags */
    CUPREC_LEG2 = 1,                    /* a tie's second leg */
    CUPREC_ET = 2,                      /* extra time played */
    CUPREC_PENS = 4,                    /* penalties (p1 / p2) */
    CUPREC_DECIDED = 8,                 /* the tie was decided by this match */
    CUPREC_T2WON = 16,                  /* ... the second club (t2) went through */
    CUPREC_LEAGUE = 32,                 /* a group / league match (not a tie's) */
};

typedef struct {
    uint8_t  round;                     /* European: its knockout stage, 1 the first; conti: cup << 4 | round */
    uint8_t  flags;
    uint8_t  g1, g2, p1, p2;            /* the match's goals (extra time in), penalties */
    uint16_t t1, t2;                    /* team numbers {country, ordinal}, t1 at home */
} CupRecGame;

typedef struct {
    uint8_t    euroSeason;              /* the season (world_cups.h seasons + 1) its table was kept in, 0: none */
    uint8_t    euroComp;                /* the contest's id */
    uint8_t    nTable, nGames;
    uint16_t   table[CUP_RECORD_TABLE]; /* the league phase's final order */
    uint8_t    points[CUP_RECORD_TABLE];
    CupRecGame games[CUP_RECORD_GAMES];
    uint8_t    contiEdition;            /* the conti edition (CT_ST.season + 1) it is of, 0: none */
    uint8_t    nConti;
    CupRecGame conti[CUP_RECORD_CONTI];
} CupRecordState;

extern CupRecordState g_cupRecord;

void CupRecord_Reset(void);             /* a new career, a load */
void CupRecord_SeasonStart(void);       /* a season's start: the European record starts again */
/* A match the engine played or simulated in the contest under way (diy_competition.c): the knockout
 * function's (its flags byte, the penalties) or the league's. */
void CupRecord_Match(const TeamFileEx *t1, const TeamFileEx *t2, uint16_t g1, uint16_t g2, uint16_t flags,
                     uint16_t p1, uint16_t p2, int32_t league);
/* The league phase's final table (league_phase.c, at its end): team numbers in place order, points. */
void CupRecord_LeaguePhaseEnd(uint8_t compId, const uint16_t *teams, const uint16_t *points, int32_t n);

/* The record that is this season's / this edition's (NULL: none). */
const CupRecordState *CupRecord_Euro(void);
const CupRecordState *CupRecord_Conti(void);

#endif
