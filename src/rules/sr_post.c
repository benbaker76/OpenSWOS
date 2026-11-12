/*
 * sr_post.c -- the season's moves: each club's one-level move from its record's place and the
 * play-offs, then the B-team rules (the SWOS 96/97 Mod's tools/gplay.c sec_post), as ported to
 * OpenSWOS (its src/league_playoffs.c) and checked against the Mod's C (tests/rules/check.sh).
 *
 * The Mod works on division numbers (SEC_COPY - SEC_SLT); OpenSWOS and the library on levels, a
 * level of groups counting as one -- the same moves.
 *
 * Copyright (c) 2026 Davide Lorigliola, Ben Baker. MIT licence (LICENSE).
 */
#include "swosrules.h"

typedef uint8_t  u8;
typedef uint16_t u16;
typedef int8_t   s8;

static int32_t sign(int32_t x) { return x < 0 ? -1 : x > 0; }

static int32_t level_of(const sr_levels *lv, int32_t d)
{
    if (!lv || !lv->levels)
        return d;
    for (int32_t l = lv->levels - 1; l >= 0; l--)
        if (d >= lv->first[l])
            return l;
    return 0;
}

static int32_t find_club(const sr_state *st, u16 g, int32_t *d, int32_t *k)
{
    for (*d = 0; *d < st->t.nd; (*d)++)
        for (*k = 0; *k < st->t.n[*d]; (*k)++)
            if (st->t.t[*d][*k].glob == g)
                return 1;
    return 0;
}

/* B team at division db: going up one level is not allowed -- above `top`, or into or
 * above its first team's level after this season's moves */
static int32_t up_bad(const sr_state *st, const sr_league *lp, const sr_levels *lv, int32_t i, int32_t db, u16 base)
{
    const int32_t target = level_of(lv, db) - 1;
    int32_t df, kf;
    if (lp->second_top != 0xFF && target < lp->second_top)
        return 1;
    return lp->second[i][1] != 0xFF && find_club(st, (u16)(base + lp->second[i][1]), &df, &kf) &&
           target <= level_of(lv, df) + st->want[df][kf];
}

/* a club skipped for a promotion place: a B team that may not go up */
static int32_t ineligible(const sr_state *st, const sr_league *lp, const sr_levels *lv, int32_t d, int32_t k, u16 base)
{
    for (int32_t i = 0; i < lp->seconds; i++)
        if (st->t.t[d][k].glob == (u16)(base + lp->second[i][0]))
            return !(lp->second_flags & 1) || up_bad(st, lp, lv, i, d, base);
    return 0;
}

void sr_b_teams(sr_state *st, const sr_league *lp, const sr_levels *lv, uint16_t base)
{
    const int32_t lowestLevel = lv && lv->levels ? lv->levels - 1 : lp->divisions - 1;
    for (int32_t i = 0; i < lp->seconds; i++) {
        const u16 gb = (u16)(base + lp->second[i][0]);
        int32_t db, kb, df = 0, kf = 0;
        if (!find_club(st, gb, &db, &kb))
            continue;
        const int32_t lb = level_of(lv, db), nb = lb + st->want[db][kb], dl = st->want[db][kb];
        const int32_t rf = lp->second[i][1] != 0xFF && find_club(st, (u16)(base + lp->second[i][1]), &df, &kf);
        const int32_t nf = rf ? level_of(lv, df) + st->want[df][kf] : -1;
        const int32_t lowest = lb == lowestLevel;
        const int32_t coupled = rf && nf >= (nb < lb ? lb : nb) && nb <= lb;
        if (coupled && (!lowest || (lp->second_flags & 2))) {
            st->want[db][kb] = 1;               /* the first team in its level: one down */
            if (!lowest)                        /* the best placed relegated of its division stays */
                for (int32_t k = 0; k < st->t.n[db]; k++)
                    if (k != kb && st->want[db][k] > 0) { st->want[db][k] = 0; break; }
        } else if (nb < lb && (coupled || up_bad(st, lp, lv, i, db, base))) {
            st->want[db][kb] = 0;               /* up, not allowed: held */
        } else
            continue;
        if (nb < lb)                            /* its promotion: the next club of its division */
            for (int32_t k = 0; k < st->t.n[db]; k++) {
                if (k == kb || st->want[db][k] != 0 || ineligible(st, lp, lv, db, k, base))
                    continue;
                st->want[db][k] = (s8)dl;
                break;
            }
    }
}

void sr_final_moves(sr_state *st, const sr_league *lg, const sr_levels *lv, uint16_t base)
{
    for (int32_t d = 0; d < lg->divisions && d < SR_DIVS; d++) {
        const int32_t n = st->t.n[d];
        for (int32_t k = 0; k < n; k++) {
            const u16 g = st->t.t[d][k].glob;
            int32_t want = 0;
            if (k < lg->promoted[d] && d > 0)
                want = -1;
            else if (k >= n - lg->relegated[d])
                want = 1;
            for (int32_t i = 0; i < st->nmv; i++)
                if (st->mv[i].glob == g)
                    want += st->mv[i].delta;
            st->want[d][k] = (s8)sign(want);
        }
    }
    sr_b_teams(st, lg, lv, base);
}
