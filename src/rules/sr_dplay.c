/*
 * sr_dplay.c -- the Italian season-end play-offs and play-outs: the SWOS 96/97 Mod's
 * tools/dplay.c dp_season, as ported to OpenSWOS (its src/league_playoffs.c) and checked against
 * the Mod's C (tests/rules/check.sh).
 *
 * Ported as it is -- its random numbers, seeds, tie model and order, and its types (int is 32
 * bits on every target) -- so it reads against dplay.c line by line, and the same final tables
 * give the same ties. Its file statics are the caller's state (st->dp).
 *
 * The rules (2025-26: FIGC C.U. 39/A for Serie B, NOIF art. 49 + C.U. 114/A for Serie C):
 *   - Serie B: the 3rd up directly if it leads the 4th by more than 14 points, else the
 *     play-off winner (3rd-8th: preliminary 5v8, 6v7 single at the better placed's, a draw
 *     -> the better placed; semis 3rd v W(6v7), 4th v W(5v8), two legs, level -> the better
 *     placed; final two legs, level -> the better placed, or extra time + penalties when
 *     level on points);
 *   - Serie B: the 17th down directly if the 16th leads it by more than 4 points, else the
 *     play-out loser (two legs, level -> the 16th, or e.t. + penalties when level on points);
 *   - Serie C: the play-off winner up (per group 5v10, 6v9, 7v8, then the 3 winners + the
 *     4th; national rounds with the thirds, the cup winner's place, the seconds; a Final
 *     Four).
 * A tie is decided by the clubs' strength, (points + 20) squared, the home side +10%.
 *
 * Copyright (c) 2026 Davide Lorigliola, Ben Baker. MIT licence (LICENSE).
 */
#include "swosrules.h"

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

enum { NT = 24, NDIV = 5 };
enum { K_DIRECT = 1, K_PRE, K_SEMI, K_FINAL, K_OUT, K_GR1 = 10, K_GR2, K_NR1, K_NR2, K_FFS, K_FFF };

void sr_reset(sr_state *st)
{
    u8 *p = (u8 *)st;
    for (u32 i = 0; i < sizeof *st; i++)
        p[i] = 0;
}

uint32_t sr_state_size(void) { return sizeof(sr_state); }

static void add_move(sr_state *st, u16 glob, int delta, int rule)
{
    if (delta && st->nmv < SR_MOVES) {
        st->mv[st->nmv].glob = glob;
        st->mv[st->nmv].delta = (int8_t)delta;
        st->mv[st->nmv].rule = (u8)rule;
        st->nmv++;
    }
}

static u32 dp_below(sr_state *st, u32 n)
{
    st->dp.rng = st->dp.rng * 1103515245u + 12345u;
    return ((st->dp.rng >> 16) * n) >> 16;
}

#define DIV(c) ((c) / NT)
#define DPL(c) ((c) % NT)
static const sr_row *dp_row_of(const sr_state *st, int c) { return &st->dp.t[DIV(c)][DPL(c)]; }

static int dp_better(const sr_state *st, int a, int b)     /* 1 if club a is better placed than club b */
{
    const sr_row *x = dp_row_of(st, a), *y = dp_row_of(st, b);
    if (DPL(a) != DPL(b) || DIV(a) == DIV(b)) return DPL(a) < DPL(b);
    if (x->pts != y->pts) return x->pts > y->pts;
    if (x->won != y->won) return x->won > y->won;
    if (x->gf != y->gf) return x->gf > y->gf;
    return st->dp.lot[a] < st->dp.lot[b];
}

static u32 dp_str2(const sr_state *st, int c, int home)
{
    u32 s = dp_row_of(st, c)->pts + 20;
    if (home) s = s * 11 / 10;
    return s * s;
}

/* legs 1 or 2; host = the single match's / the return leg's home club; level = the club
 * through when level (-1: extra time + penalties, by strength) */
static int dp_tie(sr_state *st, int kind, int host, int other, int legs, int level)
{
    u32 sh = dp_str2(st, host, 1), so = dp_str2(st, other, 0), w;
    (void)kind;
    if (dp_below(st, 100) < (legs == 1 ? 22u : 12u)) {
        if (level >= 0) return level;
        u32 a = dp_str2(st, host, 0);   /* penalties: close to a coin toss */
        sh = 2 * a + so;
        so = a + 2 * so;
    }
    w = dp_below(st, sh + so) < sh ? (u32)host : (u32)other;
    return (int)w;
}

static void dp_move(sr_state *st, int c, int delta) { add_move(st, dp_row_of(st, c)->glob, delta, SR_RULE_ITALY); }

static int best_of(const sr_state *st, const int *c, int n)
{
    int b = c[0];
    for (int i = 1; i < n; i++) if (dp_better(st, c[i], b)) b = c[i];
    return b;
}

static void sort_better(const sr_state *st, int *c, int n)
{
    for (int i = 1; i < n; i++) {
        int x = c[i], j = i;
        while (j > 0 && dp_better(st, x, c[j - 1])) { c[j] = c[j - 1]; j--; }
        c[j] = x;
    }
}

static void shuffle(sr_state *st, int *c, int n)
{
    for (int i = n - 1; i > 0; i--) { int j = (int)dp_below(st, (u32)(i + 1)), x = c[i]; c[i] = c[j]; c[j] = x; }
}

/* seeded round: seeds s[0..n-1] v the others drawn; two legs, return at the seed's,
 * level -> the seed */
static void seeded(sr_state *st, int kind, int *s, int *o, int n, int *win)
{
    shuffle(st, o, n);
    for (int i = 0; i < n; i++) win[i] = dp_tie(st, kind, s[i], o[i], 2, s[i]);
}

int32_t sr_out_held(uint16_t pts16, uint16_t pts17) { return pts16 <= pts17 + 4; }
int32_t sr_playoff_held(uint16_t pts3, uint16_t pts4) { return !(pts3 > pts4 + 14); }

static void serie_b(sr_state *st)
{
    int n = st->dp.n[0];
    const sr_row *t = st->dp.t[0];
    if (n < 17) return;
    if (!(sr_playoff_held(t[2].pts, t[3].pts) || st->dp.test_held)) {
        dp_move(st, 2, -1);
    } else {
        int w58 = dp_tie(st, K_PRE, 4, 7, 1, 4), w67 = dp_tie(st, K_PRE, 5, 6, 1, 5);
        int s1 = dp_tie(st, K_SEMI, 2, w67, 2, 2), s2 = dp_tie(st, K_SEMI, 3, w58, 2, 3);
        int h = dp_better(st, s1, s2) ? s1 : s2, o = h == s1 ? s2 : s1;
        int w = dp_tie(st, K_FINAL, h, o, 2, t[DPL(h)].pts == t[DPL(o)].pts ? -1 : h);
        for (int k = 2; k < 8 && st->dp.po_win; k++)    /* (played: its real winner) */
            if (t[k].team == st->dp.po_win) w = k;
        dp_move(st, w, -1);
    }
    if (!(sr_out_held(t[15].pts, t[16].pts) || st->dp.test_held)) {
        dp_move(st, 16, 1);
    } else {
        int w = dp_tie(st, K_OUT, 15, 16, 2, t[15].pts == t[16].pts ? -1 : 15);
        if (st->dp.out_win == t[15].team) w = 15;       /* (played: its real winner) */
        if (st->dp.out_win == t[16].team) w = 16;
        dp_move(st, w == 15 ? 16 : 15, 1);
    }
}

/* the Coppa Italia Serie C winner's play-off place: the winner itself when it finished 11th
 * or lower, the 11th of its group when it is promoted or already in (2nd-10th); unknown:
 * the best 11th of the groups */
static int cup_place(const sr_state *st, int ng)
{
    int e[4];
    for (int g = 1; g <= ng && st->dp.cupw; g++)
        for (int k = 0; k < st->dp.n[g]; k++)
            if (st->dp.t[g][k].team == st->dp.cupw) return g * NT + (k >= 10 ? k : 10);
    for (int g = 0; g < ng; g++) e[g] = (g + 1) * NT + 10;
    return best_of(st, e, ng);
}

static void serie_c(sr_state *st)
{
    int ng = st->dp.ng, q[8], nq = 0, thirds[4], seconds[4], cup, nr1[5], nr1o[5], w1[5], s2[4], o2[4], w2[4];
    if (ng < 2 || ng > 4) return;
    for (int g = 1; g <= ng; g++) if (st->dp.n[g] < 11) return;
    for (int g = 1; g <= ng; g++) {
        int b = g * NT, r2[4];
        r2[0] = b + 3;
        for (int k = 0; k < 3; k++) r2[1 + k] = dp_tie(st, K_GR1, b + 4 + k, b + 9 - k, 1, b + 4 + k);
        sort_better(st, r2, 4);
        q[nq++] = dp_tie(st, K_GR2, r2[0], r2[3], 1, r2[0]);
        q[nq++] = dp_tie(st, K_GR2, r2[1], r2[2], 1, r2[1]);
        thirds[g - 1] = b + 2;
        seconds[g - 1] = b + 1;
    }
    cup = cup_place(st, ng);
    /* national R1: seeds = the thirds, the cup winner, the best group qualifier */
    {
        int bq = best_of(st, q, nq), ns = 0, no = 0;
        for (int g = 0; g < ng; g++) nr1[ns++] = thirds[g];
        nr1[ns++] = cup;
        nr1[ns++] = bq;
        for (int i = 0; i < nq; i++) if (q[i] != bq) nr1o[no++] = q[i];
        if (ns != no) return;           /* (3 groups: 5 v 5) */
        seeded(st, K_NR1, nr1, nr1o, ns, w1);
        /* national R2: seeds = the seconds, the best R1 winner */
        int bw = best_of(st, w1, ns), k = 0, m = 0;
        for (int g = 0; g < ng; g++) s2[k++] = seconds[g];
        s2[k++] = bw;
        for (int i = 0; i < ns; i++) if (w1[i] != bw) o2[m++] = w1[i];
        if (k != m) return;
        seeded(st, K_NR2, s2, o2, k, w2);
        /* Final Four: free draw, two legs, extra time + penalties */
        if (k != 4) return;
        shuffle(st, w2, 4);
        int f1 = dp_tie(st, K_FFS, w2[1], w2[0], 2, -1), f2 = dp_tie(st, K_FFS, w2[3], w2[2], 2, -1);
        int w = dp_tie(st, K_FFF, f2, f1, 2, -1);
        for (int g = 1; g <= ng && st->dp.cpo_win; g++)     /* (played: its real winner) */
            for (int j = 0; j < st->dp.n[g]; j++)
                if (st->dp.t[g][j].team == st->dp.cpo_win) w = g * NT + j;
        dp_move(st, w, -1);
    }
}

static void dp_season(sr_state *st)
{
    u32 h = 0;
    for (int d = 0; d < NDIV; d++)      /* the seed: the tables (a save gives the same results) */
        for (int k = 0; k < st->dp.n[d] && k < NT; k++) {
            const sr_row *r = &st->dp.t[d][k];
            h = h * 31 + r->glob;
            h = h * 31 + r->pts + (r->gf << 8);
        }
    st->dp.rng = h;
    for (int i = 0; i < NDIV * NT; i++) st->dp.lot[i] = (u8)dp_below(st, 256);
    serie_b(st);
    serie_c(st);
}

int32_t sr_italy(sr_state *st, const sr_league *lg, const sr_season *se)
{
    st->nmv = st->dp_moves = 0;         /* (the season end's first rules: the moves start here) */
    if (!lg->italy)
        return 0;
    if (sr_league_check(lg, 0))
        return -1;                      /* (refused: nothing played) */
    st->dp.ng = lg->groups;
    st->dp.po_win = se->po_win;
    st->dp.out_win = se->out_win;
    st->dp.cpo_win = se->cpo_win;
    st->dp.cupw = se->cup_winner;
    st->dp.test_held = se->test_held;
    for (int32_t d = 0; d < NDIV; d++) {    /* Serie B, then Serie C's groups */
        const int32_t src = d == 0 ? lg->serie_b : lg->group_first + d - 1;
        st->dp.n[d] = 0;
        if (d > lg->groups || src >= lg->divisions)
            continue;
        st->dp.n[d] = st->t.n[src] < NT ? st->t.n[src] : NT;
        for (int32_t k = 0; k < st->dp.n[d]; k++) {
            const sr_row *x = &st->t.t[src][k];
            sr_row *y = &st->dp.t[d][k];
            y->team = x->team; y->glob = x->glob; y->won = x->won;
            y->gf = x->gf; y->ga = x->ga; y->pts = x->pts;
        }
    }
    dp_season(st);
    st->dp_moves = st->nmv;
    return st->nmv;
}
