/*
 * uefa_access.c -- the UEFA qualifying rounds of a world with league phases
 * (include/uefa_access.h).
 *
 * Ported from the SWOS 96/97 Mod's tools/uefaq.c, which carries this notice:
 *
 *   Copyright (c) 2026 Davide Lorigliola
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of this
 *   software and associated documentation files (the "Software"), to deal in the Software
 *   without restriction, including without limitation the rights to use, copy, modify,
 *   merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 *   permit persons to whom the Software is furnished to do so, subject to the following
 *   conditions:
 *
 *   The above copyright notice and this permission notice shall be included in all copies
 *   or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 *   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
 *   PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 *   HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 *   CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
 *   THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * As it is -- its random numbers, its order, its types (int is 32 bits on both our targets) --
 * so it reads against the Mod's line by line, and the same input gives the same lists as the
 * Mod's own code (scripts/league_playoffs_check.sh builds both and compares them). Changed:
 * eplay holds pointers (the Mod's u32 addresses are a 32-bit build's), the cup-only
 * association's file is a variable, the asm's layout tables are gone.
 *
 * The Mod's description:
 *
 * UEFA qualifying rounds of a league-phase career world (access list 2024-27, as for 2026/27).
 * The access list: places per association rank (round, code): code 0 = champion, 1 = cup winner,
 * k = k-th place. uq_season(): the season's list (absent associations, the 2 European
 * Performance Spots, the title holders' rules, the round totals rebalanced top-down from the
 * 36-club league phases), the clubs of every place, the qualifying rounds in their real order
 * (draws seeded by club coefficient, no two clubs of one association; a two-legged tie won with
 * probability s1^2 / (s1^2 + s2^2)), the drop-downs, and the three league phases.
 */
#include "uefa_access.h"

/* season.py ROUND_ORDER index of each round (LS PO POnc POch Q3 Q3nc Q3ch Q2 Q2nc Q2ch Q1) */
static const u8 RORD[NR] = { 0, 1, 2, 4, 5, 7, 8, 10, 0, 1, 4, 6, 7, 10, 0, 1, 3, 4, 6, 7, 9, 10 };
/* winners -> , losers -> (season.py FLOW) */
static const u8 WIN[NR] = { OUT, CL_LS, CL_LS, CL_PO, CL_POnc, CL_Q3, CL_Q3nc, CL_Q2, OUT, EL_LS, EL_PO, EL_PO,
                            EL_Q3, EL_Q2, OUT, CO_LS, CO_LS, CO_PO, CO_POch, CO_Q3, CO_Q3ch, CO_Q2 };
static const u8 LOSE[NR] = { OUT, EL_LS, EL_LS, EL_PO, EL_LS, EL_Q3ch, EL_Q3, SPLIT, OUT, CO_LS, CO_PO, CO_POch,
                             CO_Q3, CO_Q2, OUT, OUT, OUT, OUT, OUT, OUT, OUT, OUT };
static const u8 SCHED[] = { CL_Q1, EL_Q1, CO_Q1, OUT, CL_Q2, CL_Q2nc, EL_Q2, CO_Q2, CO_Q2ch, OUT,
                            CL_Q3, CL_Q3nc, EL_Q3, EL_Q3ch, CO_Q3, CO_Q3ch, OUT, CL_PO, CL_POnc, EL_PO, CO_PO, CO_POch,
                            OUT, OUT };

/* default list 2026/27 by rank: rank, n, n x (round, code); Kassies' matrix, the EPS places removed */
static const u8 AL[] = {
    1, 7, 0,0, 0,2, 0,3, 0,4, 15,6, 8,1, 8,5,
    2, 7, 0,0, 0,2, 0,3, 0,4, 15,6, 8,1, 8,5,
    3, 7, 0,0, 0,2, 0,3, 0,4, 15,6, 8,1, 8,5,
    4, 7, 0,0, 0,2, 0,3, 0,4, 15,6, 8,1, 8,5,
    5, 7, 0,0, 0,2, 0,3, 4,4, 15,6, 8,1, 8,5,
    6, 6, 0,0, 0,2, 4,3, 19,5, 8,1, 12,4,
    7, 5, 0,0, 4,2, 19,4, 8,1, 12,3,
    8, 5, 0,0, 4,2, 19,4, 9,1, 12,3,
    9, 5, 0,0, 4,2, 19,4, 9,1, 12,3,
    10, 5, 0,0, 6,2, 19,4, 9,1, 12,3,
    11, 5, 1,0, 6,2, 19,4, 9,1, 12,3,
    12, 5, 1,0, 6,2, 19,4, 9,1, 12,3,
    13, 5, 1,0, 6,2, 19,3, 19,4, 10,1,
    14, 5, 1,0, 6,2, 19,3, 19,4, 10,1,
    15, 5, 5,0, 6,2, 19,3, 19,4, 10,1,
    16, 4, 5,0, 19,2, 19,3, 13,1,
    17, 4, 5,0, 19,2, 19,3, 13,1,
    18, 4, 5,0, 19,2, 19,3, 13,1,
    19, 4, 5,0, 19,2, 19,3, 13,1,
    20, 4, 5,0, 19,2, 19,3, 13,1,
    21, 4, 5,0, 19,2, 19,3, 13,1,
    22, 4, 5,0, 19,2, 19,3, 13,1,
    23, 4, 7,0, 19,2, 19,3, 13,1,
    24, 4, 7,0, 19,2, 19,3, 13,1,
    25, 4, 7,0, 19,2, 19,3, 13,1,
    26, 4, 7,0, 19,2, 19,3, 13,1,
    27, 4, 7,0, 19,2, 19,3, 13,1,
    28, 4, 7,0, 19,2, 19,3, 13,1,
    29, 4, 7,0, 19,2, 19,3, 13,1,
    30, 4, 7,0, 21,3, 19,2, 13,1,
    31, 4, 7,0, 21,3, 19,2, 13,1,
    32, 4, 7,0, 21,3, 19,2, 13,1,
    33, 4, 7,0, 21,3, 19,2, 13,1,
    34, 4, 7,0, 21,2, 21,3, 19,1,
    35, 4, 7,0, 21,2, 21,3, 19,1,
    36, 4, 7,0, 21,2, 21,3, 19,1,
    37, 4, 7,0, 21,2, 21,3, 19,1,
    38, 4, 7,0, 21,2, 21,3, 19,1,
    39, 4, 7,0, 21,1, 21,2, 21,3,
    40, 4, 7,0, 21,1, 21,2, 21,3,
    41, 4, 7,0, 21,1, 21,2, 21,3,
    42, 4, 7,0, 21,1, 21,2, 21,3,
    43, 4, 7,0, 21,1, 21,2, 21,3,
    44, 1, 21,1,
    45, 4, 7,0, 21,1, 21,2, 21,3,
    46, 4, 7,0, 21,1, 21,2, 21,3,
    47, 4, 7,0, 21,1, 21,2, 21,3,
    48, 4, 7,0, 21,1, 21,2, 21,3,
    49, 4, 7,0, 21,1, 21,2, 21,3,
    50, 4, 7,0, 21,1, 21,2, 21,3,
    51, 3, 7,0, 21,1, 21,2,
    52, 3, 7,0, 21,1, 21,2,
    53, 3, 7,0, 21,1, 21,2,
    54, 3, 7,0, 21,1, 21,2,
    55, 3, 7,0, 21,1, 21,2,
    0 };

struct uq_in UQ_IN;
struct uq_out UQ_OUT;
struct uq_state *UQ_S;                  /* set by the caller (the save's area) */
struct uq_res UQ_R;
static struct { u8 rank, round, code, pad; } E[NE];
static int ne;
static u32 CRC[NA][NL + 1];             /* club coefficient of (rank, code) */
static u32 rng;

static u32 below(u32 n) {
    rng = rng * 1103515245u + 12345u;
    return ((rng >> 16) * n) >> 16;
}

static int d(int r) {
    int n = 0;
    for (int i = 0; i < ne; i++)
        n += E[i].round == r;
    return n;
}

/* key comparisons of model.py: best = max (coef, -rank, -code); by rank: min (code != CW, rank, code) */
static int better(int a, int b) {                /* a > b in (coef, -rank, -code) */
    u32 ca = CRC[E[a].rank][E[a].code], cb = CRC[E[b].rank][E[b].code];
    if (ca != cb) return ca > cb;
    if (E[a].rank != E[b].rank) return E[a].rank < E[b].rank;
    return E[a].code < E[b].code;
}
static int rkey_less(int a, int b) {             /* a < b in (code != CW, rank, code) */
    int wa = E[a].code != 1, wb = E[b].code != 1;
    if (wa != wb) return wa < wb;
    if (E[a].rank != E[b].rank) return E[a].rank < E[b].rank;
    return E[a].code < E[b].code;
}

static void up(int lo, int hi, int k, int bycoef) {
    for (; k < 0; k++) {                         /* the worst clubs of hi down to lo */
        int x = -1;
        for (int i = 0; i < ne; i++)
            if (E[i].round == hi && (x < 0 || (bycoef ? better(x, i) : rkey_less(x, i))))
                x = i;
        if (x < 0) return;
        E[x].round = lo;
    }
    for (; k > 0; k--) {                         /* the best ones of lo up to hi */
        int x = -1;
        for (int i = 0; i < ne; i++)
            if (E[i].round == lo && (x < 0 || (bycoef ? better(i, x) : rkey_less(i, x))))
                x = i;
        if (x < 0) return;
        E[x].round = hi;
    }
}

static int best_of(int (*pred)(int)) {
    int x = -1;
    for (int i = 0; i < ne; i++)
        if (pred(i) && (x < 0 || better(i, x)))
            x = i;
    return x;
}
static int p_cp_champ(int i) { return E[i].code == 0 && (E[i].round == CL_PO || E[i].round == CL_Q3 ||
                                                          E[i].round == CL_Q2 || E[i].round == CL_Q1); }
static int p_clq(int i) { return E[i].round > CL_LS && E[i].round < EL_LS; }
static int p_elq(int i) { return E[i].round > EL_LS && E[i].round < CO_LS; }
static u8 top[NA];
static int p_clq_top(int i) { return p_clq(i) && (E[i].code == 0 || E[i].code == top[E[i].rank]); }

static int x_split;                              /* CL Q1 losers into UECL Q2ch (the others into Q3ch) */

/* model.rebalance: the season's list in E. st[3] = holders' status (1 'league', 2 'own') */
/* the access list's one association-specific row: Liechtenstein (no league, its cup winner in UECL Q1) is row 44 in
   the real list; it takes that row wherever it ranks (an association with a cup winner and no league clubs), and a
   44th association with a league takes the regular row of ranks 43 / 45 (4.0, data's uefa401 diagnosis) */
u8 UQ_CUPONLY_FILE[4];                          /* (the Mod's: in the code page, set by uefaq.py; ours: by
                                                   the world) */
static const u8 ROW_CUPONLY[] = { CO_Q1, 1 };
static const u8 ROW_44[] = { CL_Q1, 0, CO_Q1, 1, CO_Q1, 2, CO_Q1, 3 };

static void rebalance(const u8 *eps, const u8 *st, int totals) {
    ne = 0;
    for (const u8 *a = AL; *a; a += 2 + 2 * a[1]) {
        int rk = a[0], n = a[1];
        const u8 *row = a + 2;
        if (!UQ_IN.present[rk]) continue;
        int c0 = UQ_IN.league[rk][0];
        if (UQ_CUPONLY_FILE[0] && c0 && (UQ_IN.club[c0 - 1].id & 0xFF) == UQ_CUPONLY_FILE[0]) { row = ROW_CUPONLY; n = 1; }
        else if (rk == 44 && n == 1) { row = ROW_44; n = 4; }
        for (int j = 0; j < n; j++) {
            E[ne].rank = rk;
            E[ne].round = row[2 * j];
            E[ne].code = row[2 * j + 1];
            ne++;
        }
    }
    for (int e = 0; e < 2; e++) {                /* EPS: one more league-phase place, the lower places shift */
        int rk = eps[e], n = 1;
        if (!rk || !UQ_IN.present[rk]) continue;
        for (int i = 0; i < ne; i++)
            if (E[i].rank == rk && E[i].round == CL_LS && E[i].code >= 2 && E[i].code > n)
                n = E[i].code;
        for (int i = 0; i < ne; i++)
            if (E[i].rank == rk && E[i].code >= 2 && E[i].round != CL_LS)
                E[i].code++;
        E[ne].rank = rk;
        E[ne].round = CL_LS;
        E[ne].code = n + 1;
        ne++;
    }
    if (!totals) return;
    int x;
    if (st[0] == 1 && (x = best_of(p_cp_champ)) >= 0)
        E[x].round = CL_LS;
    if (st[1] == 1) {
        for (int r = 0; r < NA; r++) top[r] = 0;
        for (int i = 0; i < ne; i++)
            if (p_clq(i) && E[i].code != 0) {   /* (model.py order: the N places, then a cup winner) */
                int k = E[i].code == 1 ? 99 : E[i].code, t = top[E[i].rank] == 1 ? 99 : top[E[i].rank];
                if (!t || k < t) top[E[i].rank] = E[i].code;
            }
        if ((x = best_of(p_clq_top)) >= 0)
            E[x].round = CL_LS;
    }
    if (st[2] == 1 && (x = best_of(p_elq)) >= 0)
        E[x].round = EL_LS;
    up(CL_Q2nc, CL_Q3nc, 16 - 2 * d(CL_Q3nc) - d(CL_Q2nc), 1);
    int t_q3 = 12, t_q2 = 24;
    while (d(CL_PO) < 10 - t_q3 / 2) {
        int lo = d(CL_Q3) ? CL_Q3 : d(CL_Q2) ? CL_Q2 : d(CL_Q1) ? CL_Q1 : -1, y = -1;
        if (lo < 0) break;
        for (int i = 0; i < ne; i++)
            if (E[i].round == lo && (y < 0 || better(i, y)))
                y = i;
        E[y].round = CL_PO;
    }
    up(CL_Q1, CL_Q2, 2 * t_q2 - 2 * d(CL_Q2) - d(CL_Q1), 1);
    int cl_q1_losers = d(CL_Q1) / 2, cl_q2nc_losers = d(CL_Q2nc) / 2;
    int cl_lp_ls_losers = 6, cl_cp_q3_losers = t_q3 / 2, cl_cp_po_losers = 5, cl_cp_q2_losers = t_q2 / 2;
    int hold_el = st[2] == 2;
    int w_po = 36 - d(EL_LS) - hold_el - cl_lp_ls_losers - cl_cp_po_losers, t_po = 2 * w_po;
    int w_q3ch = cl_cp_q2_losers / 2;
    int w_q3 = t_po - d(EL_PO) - cl_cp_q3_losers - w_q3ch, t_q3e = 2 * w_q3;
    int w_q2 = t_q3e - d(EL_Q3) - cl_q2nc_losers, t_q2e = 2 * w_q2;
    up(EL_Q1, EL_Q2, 2 * t_q2e - 2 * d(EL_Q2) - d(EL_Q1), 0);
    int el_q1_losers = d(EL_Q1) / 2;
    int el_q2_losers = w_q2, el_q3_losers = w_q3, el_po_losers = w_po, el_q3ch_losers = w_q3ch;
    int w_poch = 5, w_q3ch_c = 2 * w_poch - el_q3ch_losers, t_q3ch = 2 * w_q3ch_c;
    x_split = 2 * (cl_q1_losers - t_q3ch);
    if (x_split < 0) x_split = 0;               /* (a small world) */
    if (x_split > cl_q1_losers) x_split = cl_q1_losers;
    int w_po_c = 36 - d(CO_LS) - w_poch - el_po_losers, t_po_c = 2 * w_po_c;
    int w_q3_c = t_po_c - d(CO_PO) - el_q3_losers, t_q3_c = 2 * w_q3_c;
    int w_q2_c = t_q3_c - d(CO_Q3) - el_q2_losers, t_q2_c = 2 * w_q2_c;
    up(CO_Q1, CO_Q2, 2 * t_q2_c - 2 * d(CO_Q2) - 2 * el_q1_losers - d(CO_Q1), 0);
}

/* season.py slot_key: (comp, CH 0 / CL place 1 / CW 2 / other 3, round order, code) */
static u32 skey(int i) {
    int c = COMP(E[i].round), k = E[i].code;
    int cls = k == 0 ? 0 : k == 1 ? 2 : c == 0 ? 1 : 3;
    return c << 24 | cls << 16 | RORD[E[i].round] << 8 | (k >= 2 ? k : 0);
}

static u16 CLUB[NE];                    /* the club (index + 1) of each place, 0 = none */
static u8 taken[NC + 1];

/* An association's European play-off (the first 'europe' one of UQ_IN.eplay[rk]: gplay.py's format) over its
   league clubs: an entrant [0, place] = the first club from that place on neither qualified nor already an entrant
   (the places pass down), 'best without Europe' = the first such club, the league cup winner = itself when it
   finished within if_places (else as 'best without Europe'); ties by the clubs' strength (as the qualifying ties),
   a level tie per the round's rule. Its own random sequence (the season's draws stay season.py's). -> club + 1 */
static u32 erng;
static u32 ebelow(u32 n) { erng = erng * 1103515245u + 12345u; return ((erng >> 16) * n) >> 16; }
static u32 sq(int c);
static int lpos(const u16 *lg, int c) {  /* a club's league place (0 = the champion) */
    for (int l = 0; l < NL; l++) if (lg[l] == c) return l;
    return NL;
}

static int ep_run(int rk, const u8 *taken) {
    const u8 *s = UQ_IN.eplay[rk];
    const u16 *lg = UQ_IN.league[rk];
    while (s && *s) {
        int kind = s[0], n = s[1], nr = s[2];
        const u8 *e = s + 6, *r = e + 4 * n;
        if (kind == 2 && n <= 16) {
            int club[16], used[NL] = {0}, W[32], nt = 0, last = -1;
            erng = 0x45504C59u + rk * 7919u;
            for (int i = 0; i < n; i++) {
                int t = e[4 * i], from = t == 0 ? e[4 * i + 2] - 1 : 0, c = 0;
                if (t == 2 && UQ_IN.lcup[rk])
                    for (int l = 0; l < NL - 1; l++)
                        if (lg[l] == UQ_IN.lcup[rk] && l >= e[4 * i + 2] - 1 && l <= e[4 * i + 3] - 1 && !taken[lg[l]])
                            { c = lg[l]; used[l] = 1; }
                for (int l = from < 0 ? 0 : from; !c && l < NL - 1; l++)
                    if (lg[l] && !taken[lg[l]] && !used[l]) { c = lg[l]; used[l] = 1; }
                club[i] = c;
                erng = erng * 31 + c;
            }
            for (int k = 0; k < nr; k++) {
                int ties = r[0], legs = r[1], level = r[3], pn = r[4];
                const u8 *m = r + 6;
                int v[64];
                if (ties > 32) ties = 32;
                for (int i = 0; i < 2 * ties; i++) {
                    int c = m[i];
                    v[i] = c < 0x80 ? (c < n ? club[c] : 0) : c < 0xC0 && (c & 0x3F) < nt ? W[c & 0x3F] : 0;
                }                                   /* (losers' brackets: not in Europe play-offs) */
                if (pn > 2 * ties) pn = 2 * ties;
                if (pn) {                           /* best v worst: by league place, 1st v last ... */
                    int t[64];
                    for (int i = 1; i < pn; i++) {
                        int x = v[i], j = i;
                        while (j > 0 && lpos(lg, x) < lpos(lg, v[j - 1])) { v[j] = v[j - 1]; j--; }
                        v[j] = x;
                    }
                    for (int i = 0; i < pn / 2; i++) { t[2 * i] = v[pn - 1 - i]; t[2 * i + 1] = v[i]; }
                    for (int i = 0; i < pn; i++) v[i] = t[i];
                }
                for (int i = 0; i < ties; i++) {
                    int a0 = v[2 * i], b0 = v[2 * i + 1];
                    int w = a0 ? b0 ? 0 : a0 : b0;
                    if (a0 && b0) {
                        u32 a = sq(a0), b = sq(b0);
                        int bp = lpos(lg, a0) < lpos(lg, b0) ? a0 : b0;
                        if (ebelow(100) < (legs == 1 ? 22u : 12u) && (level == 0 || level == 2)) w = bp;
                        else w = ebelow(a + b) < a ? a0 : b0;
                    }
                    if (nt < 32) { W[nt] = w; last = nt; }
                    nt++;
                }
                r = m + 2 * r[0];
            }
            return last >= 0 ? W[last] : 0;
        }
        for (int k = 0; k < nr; k++) r += 6 + 2 * r[0];
        s = r;
    }
    return 0;
}

/* season.py assign() for every association: CLUB[] from E; skip[] = holders taking their own place */
static void assign(const u16 *skip) {
    static short ord[NE];
    for (int i = 0; i < ne; i++) CLUB[i] = 0;
    for (int rk = 1; rk < NA; rk++) {
        int n = 0;
        for (int i = 0; i < ne; i++)
            if (E[i].rank == rk) {
                int j = n++;                    /* insertion by slot key */
                while (j > 0 && skey(ord[j - 1]) > skey(i)) { ord[j] = ord[j - 1]; j--; }
                ord[j] = i;
            }
        if (!n) continue;
        for (int c = 0; c <= UQ_IN.nclub; c++) taken[c] = 0;
        for (int h = 0; h < 3; h++) if (skip[h]) taken[skip[h]] = 1;
        const u16 *lg = UQ_IN.league[rk];
        int lce = -1;                           /* the league cup winner's / the European play-off's place: */
        if (UQ_IN.lcup[rk] || UQ_IN.eplay[rk]) /* the association's last UECL one */
            for (int j = 0; j < n; j++)
                if (COMP(E[ord[j]].round) == 2 && (lce < 0 || E[ord[j]].code > E[lce].code)) lce = ord[j];
        for (int j = 0; j < n; j++) {
            int i = ord[j], club = 0, ep;
            if (E[i].code == 0 && lg[0] && !taken[lg[0]])
                club = lg[0];
            else if (E[i].code == 1 && UQ_IN.cup[rk] && !taken[UQ_IN.cup[rk]])
                club = UQ_IN.cup[rk];
            else if (i == lce && UQ_IN.eplay[rk] && (ep = ep_run(rk, taken)) && !taken[ep])
                club = ep;                      /* (its European play-off's winner) */
            else if (i == lce && UQ_IN.lcup[rk] && !taken[UQ_IN.lcup[rk]])  /* (already qualified: the next league club) */
                club = UQ_IN.lcup[rk];
            else
                for (int l = 0; l < NL - 1; l++)
                    if (lg[l] && !taken[lg[l]]) { club = lg[l]; break; }
            if (club) { CLUB[i] = club; taken[club] = 1; }
        }
    }
}

/* the rounds' entrants */
static u16 RL[NR][128];
static u8 RN[NR];
static void add(int r, int club) { if (RN[r] < 128) RL[r][RN[r]++] = club; }

static int seed_less(int a, int b) {            /* (-coef, id) */
    u32 ca = UQ_IN.club[a - 1].coef, cb = UQ_IN.club[b - 1].coef;
    if (ca != cb) return ca > cb;
    return UQ_IN.club[a - 1].id < UQ_IN.club[b - 1].id;
}

static u32 sq(int c) { u32 s = UQ_IN.club[c - 1].str / 16; if (s < 1) s = 1; return s * s; }

static void play(int r) {
    static u16 t[128], ta[64], tb[64];
    int n = RN[r];
    RN[r] = 0;
    if (!n) return;
    for (int i = 0; i < n; i++) {               /* sorted, seeded first */
        int j = i, c = RL[r][i];
        while (j > 0 && seed_less(c, t[j - 1])) { t[j] = t[j - 1]; j--; }
        t[j] = c;
    }
    int k = 0;
    if (n & 1) {                                /* a bye for the best club (a small world's odd round) */
        if (t[0] == UQ_IN.manager && n > 1) {   /* (not the career club: it plays its ties) */
            u16 x = t[0]; t[0] = t[1]; t[1] = x;
        }
        UQ_OUT.last[t[0] - 1] = r;
        if (WIN[r] != OUT) add(WIN[r], t[0]);
        k = 1;
        n--;
    }
    int h = n / 2;
    u16 *s = t + k, *u = t + k + h;
    for (int a = 0; a < 200; a++) {
        for (int i = h - 1; i > 0; i--) {
            int j = below(i + 1);
            u16 x = u[i]; u[i] = u[j]; u[j] = x;
        }
        int ok = 1;
        for (int i = 0; i < h; i++)
            if (UQ_IN.club[s[i] - 1].rank == UQ_IN.club[u[i] - 1].rank) { ok = 0; break; }
        if (ok) break;
    }
    for (int i = 0; i < h; i++) {
        if (below(2)) { ta[i] = s[i]; tb[i] = u[i]; } else { ta[i] = u[i]; tb[i] = s[i]; }
    }
    for (int i = 0; i < h; i++) {
        int a = ta[i], b = tb[i];
        u32 pa = sq(a), pb = sq(b);
        int w = below(1000) < pa * 1000 / (pa + pb) ? a : b, l = w == a ? b : a;
        UQ_OUT.last[a - 1] = UQ_OUT.last[b - 1] = r;
        if (UQ_S && UQ_S->nbr < NBR) {
            int j = UQ_S->nbr++;
            UQ_S->br[j].r = r;
            UQ_S->br[j].aw = w == a;
            UQ_S->br[j].a = UQ_IN.club[a - 1].id;
            UQ_S->br[j].b = UQ_IN.club[b - 1].id;
        }
        if ((a == UQ_IN.manager || b == UQ_IN.manager) && UQ_OUT.mt_n < 6) {
            int k = UQ_OUT.mt_n++;
            UQ_OUT.mt_round[k] = r;
            UQ_OUT.mt_opp[k] = a == UQ_IN.manager ? b : a;
            UQ_OUT.mt_win[k] = w == UQ_IN.manager;
        }
        if (WIN[r] != OUT) add(WIN[r], w);
        if (LOSE[r] == SPLIT)
            add(RN[CO_Q2ch] < x_split ? CO_Q2ch : CO_Q3ch, l);
        else if (LOSE[r] != OUT)
            add(LOSE[r], l);
    }
}

void uq_resolve(void);

static u16 phi(u16 x, const u16 *from, const u16 *to, int n) {
    for (int i = 0; i < n; i++) if (from[i] == x) return to[i];
    return x;
}
static void setphi(u16 x, u16 y, u16 *from, u16 *to, int *n) {
    for (int i = 0; i < *n; i++) if (from[i] == x) { to[i] = y; return; }
    from[*n] = x;
    to[*n] = y;
    ++*n;
}

void uq_season(void) {
    static const u8 none[3] = { 0, 0, 0 };
    static u16 own[3];
    rng = UQ_IN.seed;
    if (UQ_S) { UQ_S->mark = 0; UQ_S->nbr = 0; UQ_S->k = 0; UQ_S->res = 0; UQ_S->s = 0; }
    for (int c = 0; c < UQ_IN.nclub; c++) { UQ_OUT.round_of[c] = NR; UQ_OUT.last[c] = NR; }
    own[0] = own[1] = own[2] = 0;
    rebalance(UQ_IN.eps, none, 0);              /* where would the holders qualify domestically */
    assign(own);
    for (int h = 0; h < 3; h++) {
        int c = UQ_IN.hold[h], dom = NR;
        UQ_OUT.status[h] = 0;
        if (!c) continue;
        for (int i = 0; i < ne; i++)
            if (CLUB[i] == c) dom = E[i].round;
        int league = h < 2 ? dom == CL_LS : (dom == EL_LS || dom == CL_LS);   /* (UECL holder: its EL league-phase place, or
                                                                              a higher one: the Champions League's, 4.0.2) */
        UQ_OUT.status[h] = league ? 1 : 2;
        if (!league) own[h] = c;
    }
    assign(own);                                /* the club coefficients of the places */
    for (int r = 0; r < NA; r++)
        for (int k = 0; k <= NL; k++) CRC[r][k] = 0;
    for (int i = 0; i < ne; i++)
        if (CLUB[i]) CRC[E[i].rank][E[i].code] = UQ_IN.club[CLUB[i] - 1].coef;
    rebalance(UQ_IN.eps, UQ_OUT.status, 1);
    assign(own);
    UQ_OUT.nent = ne;
    for (int r = 0; r < NR; r++) RN[r] = 0;
    for (int rk = 1; rk < NA; rk++)             /* (rank order, as season.py; the order does not matter) */
        for (int i = 0; i < ne; i++)
            if (E[i].rank == rk && CLUB[i]) {
                add(E[i].round, CLUB[i]);
                UQ_OUT.round_of[CLUB[i] - 1] = E[i].round;
            }
    int m = UQ_IN.manager;
    if (m && UQ_IN.force && UQ_IN.force < NR) {   /* (test builds) the manager's club into another round */
        for (int r = 0; r < NR; r++)
            for (int i = 0; i < RN[r]; i++)
                if (RL[r][i] == m) RL[r][i] = RL[r][--RN[r]];
        add(UQ_IN.force, m);
        UQ_OUT.round_of[m - 1] = UQ_IN.force;
    }
    UQ_OUT.mt_n = 0;
    UQ_OUT.standin = 0;
    int standin_c = 0;
    static const u8 HR[3] = { CL_LS, CL_LS, EL_LS };
    for (int h = 0; h < 3; h++)
        if (own[h]) { add(HR[h], own[h]); UQ_OUT.round_of[own[h] - 1] = HR[h]; }
    for (const u8 *p = SCHED; *p != OUT || p[1] != OUT; p++)
        if (*p != OUT) play(*p);
    /* a small world (fewer than ~50 associations): a league phase short of 36 takes the best clubs by coefficient
       not in any league phase (of the clubs seen: the top 9 and the cup winner of each association, the holders) */
    static const u8 LS[3] = { CL_LS, EL_LS, CO_LS };
    for (int c = 0; c <= UQ_IN.nclub; c++) taken[c] = 0;
    for (int c = 0; c < 3; c++)
        for (int i = 0; i < RN[LS[c]]; i++) taken[RL[LS[c]][i]] = 1;
    for (int c = 0; c < 3; c++) {
        int r = LS[c];
        while (RN[r] < 36) {
            int b = 0;
            for (int k = 1; k <= UQ_IN.nclub; k++)
                if (!taken[k] && UQ_IN.club[k - 1].id && (!b || seed_less(k, b))) b = k;
            if (!b) break;
            taken[b] = 1;
            add(r, b);
        }
        int n = RN[r] < 36 ? RN[r] : 36;
        UQ_OUT.n[c] = n;
        /* the manager's club still has its ties to play: a stand-in holds its place (the best club by coefficient
           in no league phase, with a record: index < 550) until the qualifying rounds are over */
        if (m && UQ_OUT.mt_n && UQ_OUT.round_of[m - 1] != CL_LS && UQ_OUT.round_of[m - 1] != EL_LS &&
            UQ_OUT.round_of[m - 1] != CO_LS)
            for (int i = 0; i < n; i++)
                if (RL[r][i] == m) {
                    int b = 0;
                    for (int k = 1; k <= 550 && k <= UQ_IN.nclub; k++)
                        if (!taken[k] && UQ_IN.club[k - 1].id && (!b || seed_less(k, b))) b = k;
                    if (b) { taken[b] = 1; RL[r][i] = b; UQ_OUT.standin = b; standin_c = c; }
                }
        for (int i = 0; i < n; i++) UQ_OUT.ls[c][i] = RL[r][i];
    }
    /* the game's lists: exactly 36 - its routed holders, whatever happened above (the season end's record count
       checks would stop the game): holders out, then the best unused clubs in / the last ones out */
    static const u8 ROUTE[3] = { 0, 0, 1 };     /* CL holder -> CL, EL holder -> CL, UECL holder -> EL */
    for (int h = 0; h < 3; h++) {               /* 4.0.2: the UECL holder who qualified for the Champions League league phase
                                                   (domestic title / place): its Champions League place stays, no EL one */
        UQ_OUT.hkeep[h] = 0;
        if (h == 2 && UQ_IN.hold[2] && UQ_OUT.status[2] == 1)
            for (int i = 0; i < UQ_OUT.n[0]; i++)
                if (UQ_OUT.ls[0][i] == UQ_IN.hold[2]) UQ_OUT.hkeep[2] = 1;
    }
    for (int c = 0; c <= UQ_IN.nclub; c++) taken[c] = 0;
    for (int h = 0; h < 3; h++) if (UQ_IN.hold[h] && !UQ_OUT.hkeep[h]) taken[UQ_IN.hold[h]] = 1;
    for (int c = 0; c < 3; c++) {
        int want = 36, n = 0;
        for (int h = 0; h < 3; h++) if (UQ_IN.hold[h] && !UQ_OUT.hkeep[h] && ROUTE[h] == c) want--;
        for (int i = 0; i < UQ_OUT.n[c] && n < want; i++) {
            int k = UQ_OUT.ls[c][i];
            if (!taken[k]) { taken[k] = 1; UQ_OUT.gl[c][n++] = k; }
        }
        UQ_OUT.gn[c] = n;
    }
    for (int c = 0; c < 3; c++) {
        int want = 36;
        for (int h = 0; h < 3; h++) if (UQ_IN.hold[h] && !UQ_OUT.hkeep[h] && ROUTE[h] == c) want--;
        while (UQ_OUT.gn[c] < want) {
            int b = 0;
            for (int k = 1; k <= 550 && k <= UQ_IN.nclub; k++)
                if (!taken[k] && UQ_IN.club[k - 1].id && (!b || seed_less(k, b))) b = k;
            if (!b) break;
            taken[b] = 1;
            UQ_OUT.gl[c][UQ_OUT.gn[c]++] = b;
        }
    }
    if (!UQ_S) return;
    UQ_S->m = m ? UQ_IN.club[m - 1].id : 0;
    UQ_S->s = UQ_OUT.standin ? UQ_IN.club[UQ_OUT.standin - 1].id : 0;
    UQ_S->sc = standin_c;
    for (int c = 0; c < 3; c++) {
        UQ_S->gn[c] = UQ_OUT.gn[c];
        for (int i = 0; i < UQ_OUT.gn[c] && i < 36; i++) UQ_S->gl[c][i] = UQ_IN.club[UQ_OUT.gl[c][i] - 1].id;
    }
    UQ_S->mark = 0x5155;                /* 'UQ' */
    /* every opponent the career club may meet, whatever its results (their records go into the save now) */
    UQ_R.npot = 0;
    if (!UQ_S->m || !UQ_OUT.mt_n) return;
    for (u32 seq = 0; seq < 64; seq++) {
        UQ_S->k = 0;
        UQ_S->res = 0;
        for (int t = 0; t < 6; t++) {
            uq_resolve();
            if (UQ_R.done) break;
            int in = 0;
            for (int i = 0; i < UQ_R.npot; i++) in |= UQ_R.pot[i] == UQ_R.opp;
            if (!in && UQ_R.npot < 16) UQ_R.pot[UQ_R.npot++] = UQ_R.opp;
            UQ_S->res |= (seq >> t & 1) << t;
            UQ_S->k++;
        }
    }
    UQ_S->k = 0;
    UQ_S->res = 0;
}

/* the bracket with the career club's real results: every result unlike the simulated one swaps the club with its
   opponent for the rest of the bracket (as UEFA's draws made before the results: "winner of tie X") */
void uq_resolve(void) {
    struct uq_state *S = UQ_S;
    static u16 from[16], to[16];        /* the map bracket club -> real club (identity elsewhere) */
    int nm = 0, j = 0;
    u16 m = S->m;
    UQ_R.done = 0;
    #define PHI(x) phi(x, from, to, nm)
    for (int t = 0; t < S->nbr; t++) {
        u16 a = PHI(S->br[t].a), b = PHI(S->br[t].b);
        if (a != m && b != m) continue;
        u16 opp = a == m ? b : a;
        if (j == S->k) {                /* the next tie to play */
            UQ_R.r = S->br[t].r;
            UQ_R.opp = opp;
            return;
        }
        int simwon = (a == m) == S->br[t].aw;
        if (simwon != (S->res >> j & 1) && nm < 15) {   /* swap the club and its opponent from here on */
            u16 bm = a == m ? S->br[t].a : S->br[t].b, bo = a == m ? S->br[t].b : S->br[t].a;
            setphi(bm, opp, from, to, &nm);
            setphi(bo, m, from, to, &nm);
        }
        j++;
    }
    UQ_R.done = 1;                      /* the final lists: the written ones, the stand-in back to the club, mapped */
    UQ_R.dest = 3;
    for (int c = 0; c < 3; c++)
        for (int i = 0; i < 36; i++) {
            u16 w = i < S->gn[c] ? S->gl[c][i] : 0;
            if (w && S->s && w == S->s && c == S->sc) w = m;
            w = w ? PHI(w) : 0;
            UQ_R.gl[c][i] = w;
            if (w == m) UQ_R.dest = c;
        }
    #undef PHI
}

/* uq_coef(): the coefficient points of the season's qualifying ties (season end, before uq_season replaces the
   bracket, emptied here). Real rule (2024-27): qualifying points count half for the association. The game does not see the
   simulated ties' matches: a tie gives its winner 1.5 points and its loser 0.5 (half of the knockout ties' 3 / 1),
   the career club's ties by its real results. UQ_CP: (team word, units in eighths, 1 = a club the association's
   divisor counts here: it played qualifying and reached no league phase). */
struct uq_cp UQ_CP[2 * NBR];
u32 UQ_NCP;
void uq_coef(void) {
    struct uq_state *S = UQ_S;
    static u16 from[16], to[16];
    int nm = 0, j = 0;
    UQ_NCP = 0;
    if (!S || !S->nbr) return;          /* (mark: 0 once the career club's ties are over; nbr: 0 = counted) */
    uq_resolve();
    u16 m = S->m;
    #define PHI(x) phi(x, from, to, nm)
    for (int t = 0; t < S->nbr; t++) {
        u16 a = PHI(S->br[t].a), b = PHI(S->br[t].b), w = S->br[t].aw ? a : b;
        if (a == m || b == m) {
            u16 opp = a == m ? b : a;
            int simwon = (a == m) == S->br[t].aw, won = j < S->k ? S->res >> j & 1 : simwon;
            w = won ? m : opp;
            if (simwon != won && nm < 15) {
                u16 bm = a == m ? S->br[t].a : S->br[t].b, bo = a == m ? S->br[t].b : S->br[t].a;
                setphi(bm, opp, from, to, &nm);
                setphi(bo, m, from, to, &nm);
            }
            j++;
        }
        for (int s = 0; s < 2; s++) {
            u16 c = s ? b : a;
            int cnt = 0;
            if (UQ_R.done) {            /* counted once: its first tie, when no league phase has it */
                cnt = 1;
                for (u32 i = 0; i < UQ_NCP && cnt; i++) if (UQ_CP[i].team == c) cnt = 0;
                for (int l = 0; l < 3 && cnt; l++)
                    for (int i = 0; i < 36; i++) if (UQ_R.gl[l][i] == c) { cnt = 0; break; }
            }
            UQ_CP[UQ_NCP].team = c;
            UQ_CP[UQ_NCP].units = c == w ? 12 : 4;
            UQ_CP[UQ_NCP++].count = cnt;
        }
    }
    S->nbr = 0;                         /* (once: uq_season writes the next bracket) */
    #undef PHI
}
