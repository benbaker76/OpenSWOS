/* career_params.c — runtime-tunable career-mode balance knobs.
 *
 * These defaults are the original hard-coded SWOS values; the game behaves exactly as
 * before unless bin/data_json/career_params.json overrides a field at startup
 * (DataJson_LoadCareerParams in save_format_json.c, SDL build). Compiled on both ports. */
#include "career_params.h"

CareerParams _careerParams = {
    /* finances: starting balance */
    .start_balance_value_mult = 3,
    .start_balance_pct_min    = 80,
    .start_balance_pct_range  = 40,
    .start_balance_scale      = 1000,

    /* finances: gate receipts */
    .gate_value_divisor    = 4000,
    .gate_league_mult_home = 10000,
    .gate_league_mult_away = 2000,
    .gate_cup_mult         = 6000,

    /* finances: wages */
    .wage_trialist_pct = 33,
    .wage_reserve_pct  = 66,
    .wage_scale        = 96,

    /* board / chairman expectations */
    .board_ratio_ok       = 30,
    .board_ratio_good     = 10,
    .board_ratio_poor     = -10,
    .board_ratio_terrible = -30,
    .board_percentile_low  = 25,
    .board_percentile_high = 75,
    .board_secondary_top2_bonus = 2,
    .board_tertiary_top2_bonus  = 2,
    .board_euro_top2_bonus      = 3,

    /* the rest of Career+'s finances: the original's */
    .wage_pct = 100,
    .team_value_wage_pct = 100,
    .running_cost_mode = 0,
    .running_cost_pct = 100,
    .league_gate_mult = 1,
    .sponsorship_mode = 0,
    .sponsorship_weeks = { 0, 0, 0, 0 },
    .league_distribution_weeks = { 0, 0, 0, 0 },
    .profit_loss_sponsorship = 0,

    /* transfers: the original's */
    .transfer_quota_mode = 0,
    .transfer_quota = 256,
    .foreign_refill_mult = 1,
    .listed_bid_chance = 11,
    .foreign_bid_chance = 8,
    .domestic_buyer_mode = 0,

    /* career length */
    .num_seasons = 20,

    /* list capacities: the original's (include/career_capacity.h) */
    .foreign_pool_capacity    = CAREER_ORIG_FOREIGN_POOL,
    .foreign_listing_capacity = CAREER_ORIG_FOREIGN_LISTING,
    .transfer_list_capacity   = CAREER_ORIG_TRANSFER_LIST,
    .job_offers_capacity      = CAREER_ORIG_JOB_OFFERS,

    /* trialists: the caller's availability flag, as the original */
    .trialist_count_mode = 0,
};
