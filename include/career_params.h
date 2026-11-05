#ifndef CAREER_PARAMS_H
#define CAREER_PARAMS_H

/* swos_types.h, NOT <stdint.h>: the Amiga toolchain has no libc <stdint.h> (it provides the
 * int*_t types itself). Including <stdint.h> here breaks the Amiga build. */
#include "swos_types.h"
#include "career_capacity.h"   /* CAREER_ORIG_* / CAREER_MAX_*: the list capacities below */

/* Hard ceiling on career length = the compile-time size of seasonInformationsArray.
 * num_seasons (below) must be <= this. */
#define CAREER_MAX_SEASONS 35

/* Tunable career-mode balance knobs.
 *
 * The defaults (career_params.c) reproduce the original hard-coded SWOS values, so the
 * game behaves identically until bin/data_json/career_params.json overrides a field at
 * startup (DataJson_LoadCareerParams, SDL build). Testers edit that JSON and relaunch —
 * no rebuild. See docs/DATA_PIPELINE.md. Compiled on both ports; only the JSON *loader*
 * is SDL-only (Amiga always uses the defaults). */
typedef struct {
    /* ---- Finances: starting balance (CalculateStartingBalance) ----
     * balance = trunc16(teamValueBase * value_mult)
     *         * (pct_min + (rand8 * pct_range >> 8)) / 100 * scale */
    uint16_t start_balance_value_mult;   /* 3 */
    uint16_t start_balance_pct_min;      /* 80 */
    uint16_t start_balance_pct_range;    /* 40  -> 80..119% */
    uint16_t start_balance_scale;        /* 1000 */

    /* ---- Finances: match gate receipts (calc{League,Cup}MatchGateReceiptsFactor) ----
     * factor = (homeVal/divisor * mult_home + awayVal/divisor * mult_away) >> 4 / teamValueDivisor */
    uint16_t gate_value_divisor;         /* 4000 */
    uint16_t gate_league_mult_home;      /* 10000 */
    uint16_t gate_league_mult_away;      /* 2000 */
    uint16_t gate_cup_mult;              /* 6000 (both teams) */

    /* ---- Finances: player wages (Career_UpdatePlayerWeeklyWageCache) ----
     * wage = avgValue (×trialist%/reserve% if applicable) * 1000 / 768 * scale / wageScalingDivisor */
    uint16_t wage_trialist_pct;          /* 33 */
    uint16_t wage_reserve_pct;           /* 66 */
    uint16_t wage_scale;                 /* 96 */

    /* ---- Board / chairman expectations (ShowChairmanOpinionAboutTheSeason) ----
     * verdict category from ratio = (rank - leaguePosition) * 100 / numTeams (lower position = better) */
    int16_t  board_ratio_ok;             /* 30   ratio <  ok       -> at least cat 1 */
    int16_t  board_ratio_good;           /* 10   ratio <  good     -> at least cat 2 */
    int16_t  board_ratio_poor;           /* -10  ratio <= poor     -> at least cat 3 */
    int16_t  board_ratio_terrible;       /* -30  ratio <= terrible -> cat 4 */
    uint16_t board_percentile_low;       /* 25   position percentile <= -> verdict improves one step */
    uint16_t board_percentile_high;      /* 75   position percentile >= -> verdict worsens one step */
    uint16_t board_secondary_top2_bonus; /* 2    secondary-cup top-2 finish improves verdict */
    uint16_t board_tertiary_top2_bonus;  /* 2    tertiary-cup top-2 */
    uint16_t board_euro_top2_bonus;      /* 3    euro-cup top-2 */

    /* ---- Finances: the rest of Career+'s (each default = the original's; benefactorr's fork's
     * values in brackets) ---- */
    uint16_t wage_pct;                   /* 100  [40] a player's weekly wage, % of the original's */
    uint16_t team_value_wage_pct;        /* 100  [250] the wages the team value is made from, % of
                                          *      the weekly wages (gate and running costs follow it) */
    uint16_t running_cost_mode;          /* 0    the original's; 1 halved, then progressive brackets
                                          *      (20k at 100%, 80k at 90, 200k at 70, 300k at 55, the
                                          *      rest at 45) [1] */
    uint16_t running_cost_pct;           /* 100  [45] the weekly running costs, % of the above */
    uint16_t league_gate_mult;           /* 1    [100] the career club's league home gate, times */
    uint16_t sponsorship_mode;           /* 0    the original's random sponsor; 1 every season, the
                                          *      weekly wages times sponsorship_weeks [1] */
    uint16_t sponsorship_weeks[4];       /* by division, the top first [18 14 10 6] (mode 1) */
    uint16_t league_distribution_weeks[4]; /* 0 0 0 0 none; the season's league money by division,
                                          *      the weekly wages times this [9 7 5 3] */
    uint16_t profit_loss_sponsorship;    /* 0    [1] the sponsorship counts in the profit / loss */

    /* ---- Transfers (Career+'s in brackets) ---- */
    uint16_t transfer_quota_mode;        /* 0    the season's quota by the original's formula; 1 a
                                          *      fixed transfer_quota, half for sales, half for buys,
                                          *      and buys limited only by the pool's room [1] */
    uint16_t transfer_quota;             /* 256  (mode 1) */
    uint16_t foreign_refill_mult;        /* 1    [3] foreign players added to the market, times */
    uint16_t listed_bid_chance;          /* 11   [36] in 256, a bid for a player you listed, a week */
    uint16_t foreign_bid_chance;         /* 8    [64] in 256, a bid comes from abroad, not home */
    uint16_t domestic_buyer_mode;        /* 0    the original's buyers at home; 1 also a club with a
                                          *      player worth half the listed one's price [1] */

    /* ---- Job offers (Career+'s in brackets) ---- */
    uint16_t job_offer_expiry;           /* 1    a new offer may be withdrawn (1 in 8 a phase) [0] */
    uint16_t job_offer_first_week;       /* 0    routine offers from this season week (an offer
                                          *      when sacked or searching comes at once) [20] */
    uint16_t job_offer_waves;            /* 0    1 or 2 offers sought a week; 1 none before week 20,
                                          *      then 6, 12 from week 25, 19-20 from week 30, and
                                          *      19-20 when sacked or searching [1] */
    uint16_t job_offer_stop_season7;     /* 1    no offers once 7 seasons are complete (the
                                          *      original's career length) [0] */
    uint16_t job_offer_phase_gate;       /* 1    a club offer only in the random phase's week [0] */
    uint16_t job_offer_own_national;     /* 1    your own national team tried first [0: only when
                                          *      searching for a national job] */
    uint16_t job_offer_pass;             /* 42   in 256, an attempt goes on to look for a club [220] */
    uint16_t job_offer_rep_high_pct;     /* 100  [125] clubs up to this % of your reputation */
    uint16_t job_offer_rep_low_pct;      /* 75   [55]  and from this % */
    uint16_t job_offer_selector_clubs;   /* 0    [96] a national selector gets club offers too: in
                                          *      256, a national job looked for, else a club abroad */
    uint16_t job_offer_national_chance;  /* 0    [192] in 256, a club manager (two seasons in, the
                                          *      board not against him) is looked at for national jobs */
    uint16_t job_offer_abroad_chance;    /* 0    [128] in 256, a club offer looked for abroad first */

    /* ---- Career length ---- */
    uint16_t num_seasons;                /* 20 (default) or up to CAREER_MAX_SEASONS (35) — the
                                          * career retires after this many seasons */

    /* ---- List capacities (include/career_capacity.h): default = the original's, at most
     * CAREER_MAX_* (the original's on the Amiga). Bounds read these, never the array size. ---- */
    uint16_t foreign_pool_capacity;      /* 432  the career's foreign-market player pool */
    uint16_t foreign_listing_capacity;   /* 15   foreign players listed in the market at once */
    uint16_t transfer_list_capacity;     /* 5    your players on the transfer list */
    uint16_t job_offers_capacity;        /* 5    job offers queued, new + old together */

    /* ---- Trialists (TrainingMatch_AdjustSelectedTeamRosterForTrialists) ---- */
    uint16_t trialist_count_mode;        /* 0    count available players by the caller's flag, as
                                          *      SWOS2.s; 1 = injured AND suspended out in every
                                          *      non-training match (Career+) */
} CareerParams;

extern CareerParams _careerParams;

/* 1 when any list capacity is above the original's: the code that only a bigger list can reach
 * (GetRemainingOfferSwapSlots' underflow, the extra screen rows) checks this, so stock play is
 * the original's exactly. */
static inline int32_t CareerParams_CapacitiesRaised(void)
{
    return _careerParams.foreign_pool_capacity    > CAREER_ORIG_FOREIGN_POOL
        || _careerParams.foreign_listing_capacity > CAREER_ORIG_FOREIGN_LISTING
        || _careerParams.transfer_list_capacity   > CAREER_ORIG_TRANSFER_LIST
        || _careerParams.job_offers_capacity      > CAREER_ORIG_JOB_OFFERS;
}

#endif /* CAREER_PARAMS_H */
