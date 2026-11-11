/*
 * sr_cups.c -- a cup's clubs from the second season on: the SWOS 96/97 Mod's tools/gplay.c
 * cf_build (4.0.1, CF_SEED 1 as released), as ported to OpenSWOS (its src/world_cups.c build)
 * and checked against the Mod's C (tests/rules/check.sh).
 *
 * The first season plays the real draw (the pack's lists); from the second, each cup keeps its
 * real list's shape -- per division, how many clubs and byes -- and takes them from where the
 * clubs are now: the ones relegated into the division first, then those that stayed, then the
 * promoted, then those without a place; within each, by last season's place.
 *
 * Copyright (c) 2026 Davide Lorigliola, Ben Baker. MIT licence (LICENSE).
 */
#include "swosrules.h"

typedef uint16_t u16;
typedef uint32_t u32;

int32_t sr_cup_entrants(const sr_quota *q, int32_t nq, int32_t n, int32_t nclubs, const uint16_t *slot,
                        const uint8_t *division, const uint8_t *place, const uint8_t *level,
                        int32_t career_slot, uint16_t *out)
{
    u16 byes[SR_CUP_CLUBS], cand[SR_CLUB_SLOTS];
    u32 key[SR_CLUB_SLOTS];
    int32_t at = 0, nb = 0;
    if (nclubs > SR_CLUB_SLOTS)
        return 0;
    for (int32_t g = 0; g < nq; g++) {
        const int32_t dv = q[g].division, want = q[g].clubs, wb = q[g].byes;
        const int32_t nl = level[dv];
        int32_t m = 0;
        /* the division's clubs this season, by slot */
        for (int32_t i = 0; i < nclubs; i++)
            if (division[i] == dv)
                cand[m++] = slot[i];
        if (m < want)
            return 0;
        /* relegated into it, stayed, promoted, without a place; then the place, the slot */
        for (int32_t i = 0; i < m; i++) {
            const int32_t v = place ? place[cand[i]] : 0;
            const int32_t r = v ? (v & 31) : 0xFF, ol = v ? v >> 5 : nl;
            const int32_t grp = !v ? 3 : ol < nl ? 0 : ol == nl ? 1 : 2;
            key[i] = (u32)grp << 17 | (u32)r << 9 | cand[i];
        }
        for (int32_t i = 1; i < m; i++)
            for (int32_t j = i; j > 0 && key[j - 1] > key[j]; j--) {
                const u32 kk = key[j]; key[j] = key[j - 1]; key[j - 1] = kk;
                const u16 x = cand[j]; cand[j] = cand[j - 1]; cand[j - 1] = x;
            }
        for (int32_t i = 0; career_slot >= 0 && i < m; i++)     /* the career club first */
            if (cand[i] == career_slot) {
                const u16 x = cand[i];
                for (int32_t j = i; j > 0; j--) cand[j] = cand[j - 1];
                cand[0] = x;
                break;
            }
        for (int32_t k = 0; k < want; k++) {
            if (k >= want - wb) { if (nb < SR_CUP_CLUBS) byes[nb++] = cand[k]; }
            else if (at < SR_CUP_CLUBS) out[at++] = cand[k];
        }
    }
    for (int32_t k = 0; k < nb && at < SR_CUP_CLUBS; k++)
        out[at++] = byes[k];                    /* (the byes after round 1's clubs) */
    return at == n ? at : 0;
}
