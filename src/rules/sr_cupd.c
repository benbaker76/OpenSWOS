/*
 * sr_cupd.c -- the Coppa Italia Serie D: a later season's first two rounds, the SWOS 96/97 Mod's
 * tools/gplay.c cd_rank, cd_season and cd_make, as ported to OpenSWOS (its src/world_cupd.c) and
 * checked against the Mod's C (tests/rules/check.sh).
 *
 * The first season plays the real draw (the pack's cupd.json table). From the second, Serie D's
 * clubs enter by last season's end -- those relegated from Serie C and the pool's new clubs
 * first, then by their place from the bottom of their group -- into a preliminary round and a
 * first round of 64, each tie paired within a group where it can be.
 *
 * Copyright (c) 2026 Davide Lorigliola, Ben Baker. MIT licence (LICENSE).
 */
#include "swosrules.h"

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

#define CD_REF(i) ((u16)((i) << 8 | 0xFF))     /* (a tie as a side: a file is never 255) */

static u32 below(u32 *rng, u32 n)
{
    *rng = *rng * 1103515245u + 12345u;
    return ((*rng >> 16) * n) >> 16;
}

void sr_cupd_end_reset(sr_cupd_end *e)
{
    for (int32_t i = 0; i < SR_CLUB_SLOTS; i++) {
        e->place[i] = 0;
        e->last_div[i] = 0xFF;
    }
    for (int32_t i = 0; i < SR_CLUB_SLOTS / 8; i++)
        e->arrival[i] = 0;
}

void sr_cupd_keep_place(sr_cupd_end *e, uint8_t first, uint8_t groups, uint16_t division, uint16_t slot,
                        int32_t place, int32_t clubs)
{
    if (slot >= SR_CLUB_SLOTS)
        return;
    e->last_div[slot] = (u8)division;
    if (division >= first && division < first + groups)
        e->place[slot] = (u8)(2 + (clubs - 1 - place));        /* (place 0 the top) */
}

void sr_cupd_note_arrival(sr_cupd_end *e, uint16_t slot)
{
    if (slot < SR_CLUB_SLOTS)
        e->arrival[slot >> 3] |= (u8)(1 << (slot & 7));
}

/* m sides -> ties from side[at]: shuffled, each paired with the next of its group, else the
 * next (gplay.c cd_pair). */
static void cd_pair(u32 *rng, u16 *side, u8 *sg, int32_t m, int32_t at, sr_cupd_rounds *out)
{
    for (int32_t i = m - 1; i > 0; i--) {
        const int32_t j = (int32_t)below(rng, (u32)i + 1);
        const u16 x = side[i]; const u8 y = sg[i];
        side[i] = side[j]; side[j] = x; sg[i] = sg[j]; sg[j] = y;
    }
    u8 used[256];
    for (int32_t i = 0; i < m; i++)
        used[i] = 0;
    for (int32_t i = 0; i < m; i++) {
        if (used[i])
            continue;
        int32_t j = -1;
        for (int32_t k = i + 1; k < m && j < 0; k++)
            if (!used[k] && sg[k] != 0xFF && sg[k] == sg[i]) j = k;
        for (int32_t k = i + 1; k < m && j < 0; k++)
            if (!used[k]) j = k;
        used[i] = 1;
        out->side[at++] = side[i];
        if (j >= 0) { used[j] = 1; out->side[at++] = side[j]; } else out->side[at++] = 0;
    }
}

int32_t sr_cupd_make(const sr_cupd_end *e, uint32_t season, uint8_t rounds, uint8_t first, uint8_t groups,
                  int32_t nslots, const uint8_t *division, const uint16_t *word, sr_cupd_rounds *out)
{
    const int32_t df = first, dc = groups;
    u16 ord[SR_CLUB_SLOTS], key[SR_CLUB_SLOTS], side[256];
    u8 grp[SR_CLUB_SLOTS], sg[256], pg[SR_CUPD_PRE];
    int32_t n = 0;
    u32 rng = season * 104729u + 17;
    if (rounds < 2 || rounds > 8 || first + groups > SR_DIVS)   /* (sr_check.c: 1 << (rounds - 2) ties fit) */
        return -1;
    if (nslots > SR_CLUB_SLOTS)
        nslots = SR_CLUB_SLOTS;
    for (int32_t i = 0; i < nslots; i++) {
        const int32_t dv = division[i];
        if (dv == 0xFF || dv < df || dv >= df + dc)
            continue;
        int32_t v = 0x40;                                       /* (unknown) */
        if (e) {
            const int32_t od = e->last_div[i];
            if (e->arrival[i >> 3] & (1 << (i & 7)))
                v = 1;                                          /* (a pool club's identity) */
            else if (od == 0xFF)
                v = 0x40;                                       /* (in no table: unknown) */
            else if (od < df || od >= df + dc)
                v = 1;                                          /* (relegated from Serie C) */
            else if (e->place[i] >= 2)
                v = e->place[i];                                /* (from the bottom of its group) */
        }
        ord[n] = (u16)i;
        grp[n] = (u8)(dv - df);
        key[n] = (u16)(v << 8 | below(&rng, 256));
        n++;
    }
    for (int32_t i = 1; i < n; i++)                             /* (the entry order: by key) */
        for (int32_t j = i; j > 0 && key[j - 1] > key[j]; j--) {
            u16 k = key[j]; key[j] = key[j - 1]; key[j - 1] = k;
            k = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = k;
            const u8 o = grp[j]; grp[j] = grp[j - 1]; grp[j - 1] = o;
        }
    const int32_t n1 = 1 << (rounds - 2);
    int32_t p = n - 2 * n1, m = 0;
    if (p < 0) p = 0;
    if (p > SR_CUPD_PRE) p = SR_CUPD_PRE;
    out->np = (u8)p;
    for (int32_t k = 0; k < 2 * p; k++) { side[m] = word[ord[k]]; sg[m++] = grp[k]; }
    cd_pair(&rng, side, sg, m, 0, out);
    for (int32_t t = 0; t < p; t++) {                           /* (a preliminary tie's group: its first side's) */
        pg[t] = 0xFF;
        for (int32_t k = 0; k < 2 * p; k++) if (word[ord[k]] == out->side[2 * t]) pg[t] = grp[k];
    }
    m = 0;
    for (int32_t t = 0; t < p; t++) { side[m] = CD_REF(t); sg[m++] = pg[t]; }
    for (int32_t k = 2 * p; k < n && m < 2 * n1; k++) { side[m] = word[ord[k]]; sg[m++] = grp[k]; }
    while (m < 2 * n1) { side[m] = 0; sg[m++] = 0xFF; }
    cd_pair(&rng, side, sg, m, 2 * p, out);
    return n;
}
