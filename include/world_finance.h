/*
 * world_finance.h -- a world's club money (src/world_finance.c; docs/WORLD_2026_27.md, "The Mod
 * 4.1's money"): the SWOS 96/97 Mod 4.1's TV and commercial income and its UEFA prize money (its
 * tools/finance.py and tools/uprize.c / uprize2.c, MIT, Davide Lorigliola).
 *
 * Worlds only: in 96/97 (and on a SWOS_WORLDS=0 build) every function here does nothing, so the
 * original game's money is untouched.
 */
#ifndef WORLD_FINANCE_H
#define WORLD_FINANCE_H

#include "swos_types.h"

/* ---- TV and commercial income (the Mod's finance.py) ----
 * With every weekly charge (ApplyWeeklyFinancialDeductions) the career club also receives its
 * share of the season's TV and commercial money: its level's base (3.0 M a season in the top
 * division, 1.2 M, 0.5 M, then 0.2 M; a level of parallel groups counts once) / the season's
 * charges, + 1.1 x the wage bill of the charge. */
int32_t WorldFinance_On(void);                  /* a world's career: the income and the gate cap */
void    WorldFinance_Charge(void);              /* one weekly charge */
void    WorldFinance_PeriodStart(void);         /* old balance = new: a statement period begins */
void    WorldFinance_Loaded(void);              /* a career loaded: the period's share unknown */
/* The in-season statement's SPONSORS + TV RIGHTS row: this period's share (or, for a period that
 * began before a load, what balances the statement: new - old - the rows' total). *show 0: no row. */
int32_t WorldFinance_PeriodTv(int32_t rowsTotal, int32_t *show);

/* ---- UEFA prize money (the Mod's uprize.c, uprize2.c) ----
 * The career club's league-phase European cup (Champions League, Europa League, Conference
 * League) pays UEFA's 2026/27 distribution at 1/20: the league phase's participation (with the
 * value pillar), each win and draw, the ranking pillar at its end, each knockout stage reached,
 * the cup won. Credited as competition bonuses. What is paid is kept in the save (an older save:
 * what its club has already reached counts as paid). */
typedef struct {
    uint8_t ok;                         /* 0: an older save -- its first event counts as paid what is reached */
    uint8_t cup;                        /* the cup (0..2) and the season stamp (_seasonPlaying + 1) paid */
    uint8_t season;
    uint8_t flags;                      /* 1 participation, 2 won, 4 the ranking pillar */
    uint8_t wins, draws;                /* the league-phase wins / draws paid */
    uint8_t stage;                      /* the furthest knockout stage paid (1 = the play-offs) */
    uint8_t off;                        /* 1: the prizes switched off (and the season start's euro-cup term back) */
} UefaPrizeState;
extern UefaPrizeState g_uefaPrize;

int32_t UefaPrize_On(void);                     /* a league-phase world's career, the prizes on */
void    UefaPrize_NewCareer(void);              /* nothing reached yet */
void    UefaPrize_LoadedWithout(void);          /* a save without the item: an older one */
/* LeaguePhase_Result: a league-phase result of the career club's row, already in its played / wins /
 * draws (won / drawn: this result's); words the 36 rows' clubs. */
void    UefaPrize_Result(uint8_t compId, int32_t row, uint16_t played, uint16_t wins, uint16_t draws,
                         int32_t won, int32_t drawn, const uint16_t *words);
void    UefaPrize_PhaseEnd(uint8_t compId, const uint16_t *finalOrder);         /* 36 words, best first */
void    UefaPrize_Stage(uint8_t compId, int32_t stage, int32_t numTeams, const uint16_t *teams);
void    UefaPrize_CupWon(uint8_t compId, uint16_t winner);
/* Career_InitializeNewSeasonFinances: the season start's euro-cup term is not paid for a UEFA
 * cup while the prizes are (nothing paid twice). */
int32_t UefaPrize_SkipSeasonStartTerm(uint16_t euroCupSlotIndex);
/* The in-season statement shows a UEFA PRIZE MONEY row for the period's competition bonuses. */
int32_t UefaPrize_ShowRow(void);
int32_t UefaPrize_Shown(void);                  /* CLUB BUSINESS shows the switch: a league-phase world's career */

/* ---- Financial fair play (the Mod 4.0.6 / 4.1's ffp.py, Marco's idea) ----
 * At each season start of a club staying on, its wage bill at the season's end (the wages per
 * charge x that season's charges) against the income of the season just ended (its gates, its
 * sponsorship, the TV base -- not the share that grows with the wages -- the new season's
 * competition bonuses and the chairman's money): over 40 % closes the market while the wages stay
 * over it, and costs a fine of half the excess (rounded down to thousands), told in a memo and
 * shown on the season-start statement. A switch per career (CLUB BUSINESS): ON for a new career
 * (the world mod's setting "financial_fair_play"), OFF for a save made before it. Nothing is
 * checked in a club's first season or for a season begun before a load of an older save. */
typedef struct {
    uint8_t  on;                        /* the rule on for this career */
    uint8_t  block;                     /* sanctioned this season: purchases refused while over the limit */
    uint8_t  valid;                     /* inc covers the whole season (0: an older save loaded mid-season) */
    int32_t  inc;                       /* the season's gates and TV base so far */
    int32_t  iprev;                     /* last season's income I (0: none of ours) */
    int32_t  fine;                      /* the fine taken at this season's start */
    uint16_t nch;                       /* the season's match charges: the season-end wage bill's */
} FfpState;
extern FfpState g_ffp;

void    Ffp_NewCareer(void);
void    Ffp_LoadedWithout(void);                /* a save without the item: the rule off */
void    Ffp_Gate(int32_t credited);             /* a gate credited to the career club */
void    Ffp_SeasonStartBefore(void);            /* InitializeNewSeason, before Career_InitializeNewSeasonFinances */
void    Ffp_SeasonStartAfter(void);             /* ... after it: the judgement, the fine, the memo */
int32_t Ffp_PurchasesBlocked(void);             /* BuyPlayerMenu: the market closed */
int32_t Ffp_Shown(void);                        /* CLUB BUSINESS shows the ratio and the switch */
void    Ffp_RatioText(char *out, int32_t size); /* "WAGES/INCOME 37% (MAX 40%)" */

#endif
