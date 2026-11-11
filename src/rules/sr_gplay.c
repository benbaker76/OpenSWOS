/*
 * sr_gplay.c -- the pack's knockouts at the season end: the SWOS 96/97 Mod's tools/gplay.c
 * gp_season, whole, as ported to OpenSWOS (its src/league_playoffs.c) and checked against the
 * Mod's C over every pack (tests/rules/check.sh).
 *
 * Every kind (promotion, play-out, placement, title, continental, promedio, stage title) and
 * entrant (a place, a kept stage table's place, the Trofeo's finalists, the title deciders'
 * seeds, the previous play-off's loser), group rounds, the drawn pot; its results -- a title's
 * champion, a continental play-off's two, a stage title's champion and runner-up -- beside the
 * moves. Ported as it is (its random numbers, seeds, tie model and order; int is 32 bits on
 * every target); its file statics are the caller's state (st->gp). What the career club played
 * itself comes from the caller (sr_host).
 *
 * A tie is decided by the clubs' strength: the place in its division and the tier, the home
 * side +10%.
 *
 * Copyright (c) 2026 Davide Lorigliola, Ben Baker. MIT licence (LICENSE).
 */
#include "swosrules.h"

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

enum { GT = SR_CLUBS, GD = SR_DIVS, GN = 64, RESERVE = 0xFE, BEST_LOSER = 0x3F };
enum { HOST_LISTED, HOST_BETTER, HOST_DRAWN, HOST_NEUTRAL, HOST_SKIP_SAME = 0x80 };
enum { LV_BETTER, LV_ET_PENS, LV_ET_BETTER, LV_PENS, LV_AWAY_PENS, LV_AWAY_BETTER };
enum { K_TITLE = SR_KIND_TITLE, K_CONT = SR_KIND_CONTINENTAL, K_PROM = SR_KIND_PROMEDIO, K_STAGE = SR_KIND_STAGE };
enum { GS = 2, SS_BASE = 4, SS_N = SR_STAGE_TABLES };
enum { PO_SUPER = 4, PO_SKIP2 = 0x80 };    /* (a play-off's flags: the next season's supercup, not played at the
                                              season end; 'skip_second': its k-th placing that is no B team) */
enum { ABSENT = -2 };                      /* (an entrant by team word not in these tables: a group round goes on
                                              without it -- NZ's A-League reserves) */

#define LOG(...) do { if (st->gp.se && st->gp.se->host && st->gp.se->host->log) \
                          st->gp.se->host->log(st->gp.se->host->ctx, __VA_ARGS__); } while (0)

static void add_move(sr_state *st, u16 glob, int delta)
{
    if (delta && st->nmv < SR_MOVES) {
        st->mv[st->nmv].glob = glob;
        st->mv[st->nmv].delta = (int8_t)delta;
        st->mv[st->nmv].rule = SR_RULE_KNOCKOUT;
        st->nmv++;
    }
}

static u32 gp_below(sr_state *st, u32 n)
{
    st->gp.rng = st->gp.rng * 1103515245u + 12345u;
    return ((st->gp.rng >> 16) * n) >> 16;
}

#define DV(c) ((c) / GT)
#define PL(c) ((c) % GT)
static const sr_row *gp_row(const sr_state *st, int c) { return &st->t.t[DV(c)][PL(c)]; }

/* spec flag 2 'host_by_record' (st->gp.by_rec): in one division the better record */
static int gp_better(const sr_state *st, int a, int b)     /* the higher tier, then the better place */
{
    if (DV(a) != DV(b)) return DV(a) < DV(b);
    if (st->gp.by_rec) {                /* (points, wins, goal difference, goals; then the place) */
        const sr_row *x = gp_row(st, a), *y = gp_row(st, b);
        int dx = x->gf - x->ga, dy = y->gf - y->ga;
        if (x->pts != y->pts) return x->pts > y->pts;
        if (x->won != y->won) return x->won > y->won;
        if (dx != dy) return dx > dy;
        if (x->gf != y->gf) return x->gf > y->gf;
    }
    return PL(a) < PL(b);
}

static u32 gp_str2(const sr_state *st, int c, int home)    /* strength: the place in its division, the tier */
{
    int n = st->t.n[DV(c)];
    int s = 30 + 40 * (n - PL(c)) / (n ? n : 1) - 25 * DV(c);
    if (s < 5) s = 5;
    if (home) s = s * 11 / 10;
    return (u32)(s * s);
}

/* a tie: a = the single match's host / the first leg's home club (listed order), b = the other */
static int gp_tie(sr_state *st, int a, int b, int legs, int host, int level)
{
    int h = a, o = b, w;
    if (host == HOST_BETTER || host == HOST_NEUTRAL) {      /* the better placed hosts the single match / return */
        if (gp_better(st, b, a)) { h = b; o = a; }
        else { h = a; o = b; }
        if (legs == 2 || host == HOST_NEUTRAL) { int x = h; h = o; o = x; }   /* (h = the first leg's / listed) */
    } else if (host == HOST_DRAWN && gp_below(st, 2)) { h = b; o = a; }
    /* the club with home advantage: a single match's host, the return leg's home club */
    int adv = (legs == 1) ? (host == HOST_NEUTRAL ? -1 : h) : o;
    u32 sh = gp_str2(st, h, adv == h), so = gp_str2(st, o, adv == o);
    if (gp_below(st, 100) < (legs == 1 ? 22u : 12u)) {      /* level after the regulation time */
        int bp = gp_better(st, h, o) ? h : o;
        if (level == LV_BETTER || level == LV_ET_BETTER || level == LV_AWAY_BETTER) return bp;
        u32 x = gp_str2(st, h, 0), y = gp_str2(st, o, 0);     /* e.t. + penalties: close to a coin toss */
        sh = 2 * x + y; so = x + 2 * y;
    }
    w = gp_below(st, sh + so) < sh ? h : o;
    return w;
}

static int gp_series(sr_state *st, int a, int b, int level)    /* best of three: games 1 and 3 at the better placed's */
{
    int h = gp_better(st, a, b) ? a : b, o = h == a ? b : a, wh = 0, wo = 0;
    for (int g = 0; g < 3 && wh < 2 && wo < 2; g++)
        if (gp_tie(st, g == 1 ? o : h, g == 1 ? h : o, 1, HOST_LISTED, level) == h) wh++; else wo++;
    return wh == 2 ? h : o;
}

static void gp_move(sr_state *st, int c, int delta) { add_move(st, gp_row(st, c)->glob, delta); }

/* A tie the career club played (the caller's): its two clubs and its winner (team numbers) --
 * the Mod's GP_IN.pl_w; applied after the tie is simulated, so the random numbers drawn are the
 * same either way. */
static int gp_played(sr_state *st, int a, int b, int w)
{
    const u16 ta = gp_row(st, a)->team, tb = gp_row(st, b)->team;
    const sr_host *ho = st->gp.se ? st->gp.se->host : 0;
    u16 pw;
    if (ho && ho->played_tie && ho->played_tie(ho->ctx, st->gp.pidx, ta, tb, &pw))  /* (its play-off, played) */
        return pw == ta ? a : pw == tb ? b : w;
    if (!st->gp.played_w || !((ta == st->gp.played_a && tb == st->gp.played_b) ||
                              (ta == st->gp.played_b && tb == st->gp.played_a)))
        return w;
    return st->gp.played_w == ta ? a : st->gp.played_w == tb ? b : w;
}

static int gp_mgr_div(const sr_state *st)       /* the career club's division in the tables, or -1 */
{
    for (int d = 0; d < st->t.nd && d < GD; d++)
        for (int k = 0; k < st->t.n[d]; k++) if (st->t.t[d][k].team == st->gp.mgr && st->gp.mgr) return d;
    return -1;
}

static void gp_result(sr_state *st, int kind, int w, int l)
{
    if (st->gp.nres >= 4) return;
    sr_result *x = &st->gp.res[st->gp.nres++];
    x->kind = (u8)kind; x->div = (u8)DV(w);
    x->team[0] = gp_row(st, w)->team; x->glob[0] = gp_row(st, w)->glob;
    x->team[1] = l >= 0 ? gp_row(st, l)->team : 0xFFFF; x->glob[1] = l >= 0 ? gp_row(st, l)->glob : 0xFFFF;
}

static int stage_club(sr_state *st, int d, int stg, int a)     /* place a of kept stage stg -> its row of division d, or -1 */
{
    const sr_stage_bank *b = st->gp.ssb;
    u16 g;
    if (stg >= SS_BASE && stg < SS_BASE + SS_N) {
        int k = stg - SS_BASE;
        st->gp.ss_used = 1;
        if (!(b->fresh & 1 << k)) {             /* (not written this season: the division's final table) */
            if (d >= st->t.nd || a < 1 || a > st->t.n[d]) return -1;
            return d * GT + a - 1;
        }
        if (a < 1 || a > b->n[k] || d >= st->t.nd) return -1;
        g = b->r[k][a - 1].glob;
    } else {
        if (stg >= GS || a < 1 || a > st->gp.nst[stg] || d >= st->t.nd) return -1;
        g = st->gp.st[stg][a - 1].glob;
    }
    for (int k = 0; k < st->t.n[d]; k++) if (st->t.t[d][k].glob == g) return d * GT + k;
    return -1;
}

static int glob_club(const sr_state *st, int d, u16 g)     /* a global number -> its row of division d, or -1 */
{
    if (g == 0xFFFF || d >= st->t.nd) return -1;
    for (int k = 0; k < st->t.n[d]; k++) if (st->t.t[d][k].glob == g) return d * GT + k;
    return -1;
}

static sr_result *sres_of(sr_state *st, int d)  /* the career club's division's stage titles, or another's */
{
    if (d == gp_mgr_div(st) || (gp_mgr_div(st) < 0 && d == 0)) return st->gp.sres;
    if (st->gp.srl_div != d) {
        for (int k = 0; k < 3; k++) {
            st->gp.srl[k].kind = 0;
            st->gp.srl[k].team[0] = st->gp.srl[k].team[1] = st->gp.srl[k].glob[0] = st->gp.srl[k].glob[1] = 0xFFFF;
        }
        st->gp.srl_div = d;
    }
    return st->gp.srl;
}

/* w = stage | division << 2: the career club's division's, decided mid-season (the caller's):
 * 1 and its champion, runner-up */
static int stage_dec(sr_state *st, int w, u16 *team, u16 *glob)
{
    const sr_host *ho = st->gp.se ? st->gp.se->host : 0;
    if ((w >> 2) != gp_mgr_div(st) || !ho || !ho->stage_title) return 0;
    return ho->stage_title(ho->ctx, w & 3, team, glob) != 0;
}

static u16 stage_glob(sr_state *st, int d, int k, int j)   /* division d's stage k champion (j 0) / runner-up (1) */
{
    u16 team[2], glob[2];
    return stage_dec(st, k | d << 2, team, glob) ? glob[j] : sres_of(st, d)[k].glob[j];
}

static int title_club(sr_state *st, int d, int c)  /* the Trofeo's finalists (entrant type 5) */
{
    u16 c0 = stage_glob(st, d, 0, 0), c1 = stage_glob(st, d, 1, 0);
    if (c == 1) return glob_club(st, d, c0);
    if (c0 != c1 || c == 4) return glob_club(st, d, c1);
    return glob_club(st, d, stage_glob(st, d, c - 2, 1));
}

static int kept_pts(const sr_state *st, int stg, int c)    /* club c's points in kept stage stg (0: not in it) */
{
    u16 g = gp_row(st, c)->glob;
    if (stg >= GS) return 0;
    for (int k = 0; k < st->gp.nst[stg]; k++) if (st->gp.st[stg][k].glob == g) return st->gp.st[stg][k].pts;
    return 0;
}

static int agg_better(const sr_state *st, int stg, int a, int b)   /* the two rounds' points together, then the final table's place */
{
    int x = kept_pts(st, stg, a) + gp_row(st, a)->pts, y = kept_pts(st, stg, b) + gp_row(st, b)->pts;
    return x != y ? x > y : PL(a) < PL(b);
}

/* Malta's title decider (entrant type 6): see the Mod's gplay.c split_seed. */
static int split_seed(sr_state *st, int d, int stg, int a)
{
    if (d >= st->t.nd || st->t.n[d] < 2 || a < 1 || a > 4) return -1;
    int o1 = stage_club(st, d, stg, 1), o2 = stage_club(st, d, stg, 2), c1 = d * GT, c2 = d * GT + 1;
    if (o1 < 0 || o2 < 0) return -1;
    if (o1 == c1) return o1;
    if (o2 == c2 || (o1 == c2 && o2 == c1)) {
        int x = agg_better(st, stg, o1, c1) ? o1 : c1, y = x == o1 ? c1 : o1;
        return a == 1 || a == 4 ? x : y;
    }
    int x = o1 == c2 ? o1 : c1 == o2 ? c1 : -1;
    if (x >= 0) {
        int y = x == o1 ? c1 : o1, z = x == o1 ? o2 : c2;
        if (agg_better(st, stg, z, y)) { int t = y; y = z; z = t; }
        return a == 1 || a == 4 ? x : a == 2 ? y : z;
    }
    int q[4] = { o1, c1, o2, c2 };
    for (int i = 1; i < 4; i++)
        for (int j = i; j > 0 && agg_better(st, stg, q[j], q[j - 1]); j--) { int t = q[j]; q[j] = q[j - 1]; q[j - 1] = t; }
    return q[a - 1];
}

/* Uruguay's and Peru's national title play-off (entrant type 7): see the Mod's title_seed. */
static int t_place(sr_state *st, int d, int c)
{
    for (int p = 1; d < st->t.nd && p <= st->t.n[d]; p++) if (stage_club(st, d, 6, p) == c) return p;
    return 0;
}
static int title_seed(sr_state *st, int d, int rule, int a)
{
    if (d >= st->t.nd || st->t.n[d] < 4 || a < 1 || a > 4) return -1;
    int A = stage_club(st, d, 4, 1), C = stage_club(st, d, rule == 1 ? 7 : 5, 1), t1 = stage_club(st, d, 6, 1);
    if (A < 0 || C < 0 || t1 < 0) return -1;
    if (A == C) return A;
    if (rule == 1) return a == 1 ? A : a == 2 ? C : t1;
    int pa = t_place(st, d, A), pc = t_place(st, d, C), q[4], nq = 0;
    if (pa && pa <= 2 && pc && pc <= 2) return a == 1 || a == 4 ? A : C;
    if (pa > 0 && pa <= 8) q[nq++] = A;
    if (pc > 0 && pc <= 8) q[nq++] = C;
    for (int p = 1; nq < 4 && p <= st->t.n[d]; p++) {
        int c = stage_club(st, d, 6, p);
        if (c >= 0 && c != A && c != C) q[nq++] = c;
    }
    if (nq < 4) return -1;
    int x = pa && pa <= 2 ? A : pc && pc <= 2 ? C : -1;
    if (x >= 0) {
        int y = -1, z = -1;
        for (int i = 0; i < 4; i++) {
            if (q[i] == x) continue;
            if ((q[i] == A || q[i] == C) && y < 0) y = q[i];
        }
        for (int i = 0; i < 4; i++) if (q[i] != x && q[i] != y && (z < 0 || t_place(st, d, q[i]) < t_place(st, d, z))) z = q[i];
        if (y < 0) {
            for (int i = 0; i < 4; i++) if (q[i] != x && q[i] != z) { y = q[i]; break; }
        }
        return a == 1 || a == 4 ? x : a == 2 ? y : z;
    }
    for (int i = 1; i < 4; i++)
        for (int j = i; j > 0 && t_place(st, d, q[j]) < t_place(st, d, q[j - 1]); j--) { int t = q[j]; q[j] = q[j - 1]; q[j - 1] = t; }
    return q[a == 1 ? 0 : a == 4 ? 3 : a == 2 ? 1 : 2];
}

/* a group match: goals by strength (draws ~ 1 in 4), home advantage unless neutral */
static void gmatch(sr_state *st, int h, int a, int neutral, int *gh, int *ga)
{
    u32 sh = gp_str2(st, h, !neutral), sa = gp_str2(st, a, 0);
    if (gp_below(st, 100) < 26) { int g = (int)gp_below(st, 3); *gh = g; *ga = g; return; }
    int w = gp_below(st, sh + sa) < sh, gw = 1 + (int)gp_below(st, 3), gl = (int)gp_below(st, (u32)gw);
    *gh = w ? gw : gl; *ga = w ? gl : gw;
}

static int gs_before(const sr_state *st, const sr_gs *x, const sr_gs *y)  /* points, goal difference, goals, the place */
{
    if (x->pts != y->pts) return x->pts > y->pts;
    if (x->gd != y->gd) return x->gd > y->gd;
    if (x->gf != y->gf) return x->gf > y->gf;
    return gp_better(st, x->c, y->c);
}

static void gs_copy(sr_gs *d, const sr_gs *s) { d->c = s->c; d->pts = s->pts; d->gd = s->gd; d->gf = s->gf; }

/* a group round: mem[0..m) round robin `meet` times (home and away alternate), start bonus
 * points; meet 0 = a ranking round (the members ranked by their own groups' figures, ins) */
static void group(sr_state *st, const int *mem, int m, int meet, int neutral, const u8 *bonus, int *out,
                  const sr_gs *ins, sr_gs *outs)
{
    sr_gs t[GT];
    for (int i = 0; i < m; i++) {
        t[i].c = mem[i]; t[i].pts = bonus[i]; t[i].gd = 0; t[i].gf = 0;
        if (!meet) { t[i].pts = ins[i].pts; t[i].gd = ins[i].gd; t[i].gf = ins[i].gf; }
    }
    for (int k = 0; k < meet; k++)
        for (int i = 0; i < m; i++)
            for (int j = i + 1; j < m; j++) {
                int h = (k & 1) ? j : i, a = (k & 1) ? i : j, gh, ga;
                gmatch(st, t[h].c, t[a].c, neutral, &gh, &ga);
                t[h].gf += gh; t[a].gf += ga; t[h].gd += gh - ga; t[a].gd += ga - gh;
                if (gh > ga) t[h].pts += 3; else if (gh < ga) t[a].pts += 3; else { t[h].pts++; t[a].pts++; }
            }
    for (int i = 1; i < m; i++) {
        sr_gs x;
        int j = i;
        gs_copy(&x, &t[i]);
        while (j > 0 && gs_before(st, &x, &t[j - 1])) { gs_copy(&t[j], &t[j - 1]); j--; }
        gs_copy(&t[j], &x);
    }
    for (int i = 0; i < m; i++) { out[i] = t[i].c; gs_copy(&outs[i], &t[i]); }
}

/* the drawn pot (Colombia's cuadrangulares): the entrants after the heads (one per leading
 * group round) drawn */
static void pot_perm(sr_state *st, const u8 *s, int *pm)
{
    int n = s[1], h = 0;
    for (int i = 0; i < n; i++) pm[i] = i;
    if (!(s[5] & 8)) return;
    const u8 *r = s + 6 + 4 * n;
    for (int k = 0; k < s[2] && (r[1] & 0x80); k++) { h++; r += 6 + 2 * r[0]; }
    for (int i = n - 1; i > h; i--) { int j = h + (int)gp_below(st, (u32)(i - h + 1)), x = pm[i]; pm[i] = pm[j]; pm[j] = x; }
}

/* A play-off's entrants -> clubs (division * GT + place), as gp_season resolves them; the
 * previous play-off's loser given. 0: one not known. */
static int team_code(const sr_state *st, u16 team);
static int place_code(const sr_state *st, const sr_league *lg, int d, int p, int kind);
/* an entrant of a play-off of kind `kind` -> its club, or -1 */
static int ent_code(sr_state *st, const sr_league *lg, const u8 *e, int prev_loser, int kind)
{
    const int ty = e[0], d = e[1], p = e[2];
    if (ty == 3) return stage_club(st, d, e[3], p);
    if (ty == 4) return prev_loser;
    if (ty == 5) return title_club(st, d, p);
    if (ty == 6) return split_seed(st, d, e[3], p);
    if (ty == 7) return title_seed(st, d, e[3], p);
    if (ty == 8)                        /* (4.0.5: a given club, its file and ordinal -> this game's team word) */
        return team_code(st, lg->team_file_high ? (u16)(p << 8 | e[3]) : (u16)(p | e[3] << 8));
    if (ty != 0) return -1;
    return place_code(st, lg, d, p, kind);
}

/* absent: 1 -- the season end's: a club by team word not in these tables is ABSENT (left out of
 * a group round) and does not stop the play-off; 0 -- a contest's: every entrant must be here. */
static int resolve_entrants(sr_state *st, const sr_league *lg, const u8 *s, int prev_loser, int *club, int absent)
{
    int n = s[1], ok = 1;
    const u8 *e = s + 6;
    for (int i = 0; i < n && i < GN; i++) {
        club[i] = ent_code(st, lg, e + 4 * i, prev_loser, s[0]);
        if (club[i] < 0 && absent && e[4 * i] == 8) club[i] = ABSENT;
        else if (club[i] < 0) ok = 0;
    }
    return ok;
}

/* Argentina's relegation (the Mod's promedio, gplay.c; LPF reglamento 2026 art. 26, AFA statute
 * art. 93): kind 7 (no entrants, no rounds; winner_div byte = the season's matches counted,
 * loser_div = where the relegated go): the last of the promedios (points per match over this
 * season's annual table and the two before, top-flight seasons only) and the last of the annual
 * table (the stage bank's 2) go down; a tie in a relegation place -> a play-off match at a
 * neutral ground. The history per club (se->promedio, the caller's) is the league's seed at a
 * new career, shifted after every season end. */
static sr_promedio_club *pr_find(sr_promedio *pr, u16 g)
{
    for (int i = 0; i < pr->n; i++) if (pr->c[i].glob == g) return &pr->c[i];
    return 0;
}

static void pr_seed(sr_promedio *pr, const sr_league *lg)
{
    pr->n = 0;
    for (int i = 0; i < lg->promedio_seeds && i < SR_PROMEDIO_CLUBS; i++) {
        const sr_promedio_seed *t = &lg->promedio_seed[i];
        sr_promedio_club *c = &pr->c[pr->n++];
        c->glob = t->glob;
        c->p[0] = t->points2; c->m[0] = t->matches2; c->p[1] = t->points1; c->m[1] = t->matches1;
    }
    pr->init = 1;
}

static void promedio(sr_state *st, const sr_league *lg, int d, int mnow, int ldiv)
{
    sr_promedio *pr = st->gp.se->promedio;
    const sr_stage_bank *b = st->gp.ssb;
    if (!pr) return;
    if (!pr->init) pr_seed(pr, lg);
    int n = b->n[2];
    const sr_row *a = b->r[2];
    if (!(b->fresh & 4) || n != st->t.n[d]) {   /* (no annual table this season: the league's final table) */
        n = st->t.n[d];
        a = 0;
    }
    st->gp.ss_used = 1;
    if (n < 3) return;
#define PR_GLOB(i) (a ? a[i].glob : st->t.t[d][i].glob)
#define PR_PTS(i) (a ? a[i].pts : st->t.t[d][i].pts)
    u32 P[GT], M[GT];
    for (int i = 0; i < n; i++) {
        sr_promedio_club *c = pr_find(pr, PR_GLOB(i));
        P[i] = PR_PTS(i) + (c ? c->p[0] + c->p[1] : 0);
        M[i] = (u32)mnow + (c ? c->m[0] + c->m[1] : 0);
    }
    int w = 0, w2 = -1;                         /* the worst promedio (P / M), then the next worst */
    for (int i = 1; i < n; i++) if (P[i] * M[w] < P[w] * M[i] || (P[i] * M[w] == P[w] * M[i] && i > w)) w = i;
    for (int i = 0; i < n; i++) if (i != w && (w2 < 0 || P[i] * M[w2] < P[w2] * M[i])) w2 = i;
    int down1 = w;
    if (w2 >= 0 && P[w2] * M[w] == P[w] * M[w2]) {          /* level: a play-off match, the loser goes down */
        int ca = glob_club(st, d, PR_GLOB(w)), cb = glob_club(st, d, PR_GLOB(w2));
        if (ca >= 0 && cb >= 0) down1 = gp_tie(st, ca, cb, 1, HOST_NEUTRAL, LV_ET_PENS) == ca ? w2 : w;
    }
    int l = n - 1;                              /* the annual table's last, the promedio's relegated apart */
    if (l == down1) l--;
    int l2 = l - 1 == down1 ? l - 2 : l - 1, down2 = l;
    if (l2 >= 0 && PR_PTS(l2) == PR_PTS(l)) {
        int ca = glob_club(st, d, PR_GLOB(l)), cb = glob_club(st, d, PR_GLOB(l2));
        if (ca >= 0 && cb >= 0) down2 = gp_tie(st, ca, cb, 1, HOST_NEUTRAL, LV_ET_PENS) == ca ? l2 : l;
    }
    int c1 = glob_club(st, d, PR_GLOB(down1)), c2 = glob_club(st, d, PR_GLOB(down2));
    LOG("[promedio] down by the promedio %04X (%u / %u), the annual table's last %04X\n",
        c1 >= 0 ? (unsigned)gp_row(st, c1)->team : 0u, (unsigned)P[down1], (unsigned)M[down1],
        c2 >= 0 ? (unsigned)gp_row(st, c2)->team : 0u);
    if (c1 >= 0) gp_move(st, c1, ldiv - d);
    if (c2 >= 0) gp_move(st, c2, ldiv - d);
    sr_promedio_club *nx = st->gp.nx;           /* next season's history: this season and the one before */
    int nn = 0;
    for (int i = 0; i < n && nn < SR_PROMEDIO_CLUBS; i++) {
        if (i == down1 || i == down2) continue;
        sr_promedio_club *c = pr_find(pr, PR_GLOB(i)), *o = &nx[nn++];
        o->glob = PR_GLOB(i);
        o->p[0] = c ? c->p[1] : 0; o->m[0] = c ? c->m[1] : 0;
        o->p[1] = (u16)PR_PTS(i); o->m[1] = (u8)mnow;
    }
    for (int i = 0; i < nn; i++) {
        pr->c[i].glob = nx[i].glob;
        pr->c[i].p[0] = nx[i].p[0]; pr->c[i].p[1] = nx[i].p[1];
        pr->c[i].m[0] = nx[i].m[0]; pr->c[i].m[1] = nx[i].m[1];
    }
    pr->n = (u8)nn;
    pr->init = 1;
#undef PR_GLOB
#undef PR_PTS
}

static int team_code(const sr_state *st, u16 team)  /* a team word -> its club, or -1 (gplay.c team_club) */
{
    for (int d = 0; d < st->t.nd && d < GD; d++)
        for (int k = 0; k < st->t.n[d]; k++)
            if (st->t.t[d][k].team == team) return d * GT + k;
    return -1;
}

/* ---- B teams as play-off entrants (gplay.c place_club, sec_inelig, sec_up_bad) ---- */

static int lv_of(const sr_levels *lv, int d)    /* division d's level (sr_post.c level_of) */
{
    if (!lv || !lv->levels)
        return d;
    for (int l = lv->levels - 1; l >= 0; l--)
        if (d >= lv->first[l])
            return l;
    return 0;
}

static int rec_move(const sr_league *lg, const sr_state *st, int d, int k)  /* the record's move, a level */
{
    if (lg->promoted && k < lg->promoted[d] && d > 0) return -1;
    if (lg->relegated && k >= st->t.n[d] - lg->relegated[d]) return 1;
    return 0;
}

static int b_index(const sr_state *st, const sr_league *lg, int c)    /* which B team row c is, or -1 */
{
    const sr_row *r = gp_row(st, c);
    const u16 base = st->gp.se ? st->gp.se->base : 0;
    for (int i = 0; i < lg->seconds; i++)
        if (r->glob == (u16)(base + lg->second[i][0]) || (lg->second_team && r->team == lg->second_team[i]))
            return i;
    return -1;
}

/* a row skipped as a play-off entrant (not a play-out): a B team that may not play play-offs, or
 * may not go up -- above second_top, or into or above its first team's level after the record */
static int b_inelig(const sr_state *st, const sr_league *lg, int c)
{
    const int i = lg->seconds && st->gp.se && st->gp.se->b_in_ko ? b_index(st, lg, c) : -1;
    const sr_levels *lv = st->gp.se ? st->gp.se->levels : 0;
    if (i < 0)
        return 0;
    if (!(lg->second_flags & 1))
        return 1;
    const int target = lv_of(lv, DV(c)) - 1;
    if (lg->second_top != 0xFF && target < lg->second_top)
        return 1;
    if (lg->second[i][1] == 0xFF)
        return 0;
    const u16 gf = (u16)(st->gp.se->base + lg->second[i][1]);
    for (int d = 0; d < st->t.nd && d < GD; d++)
        for (int k = 0; k < st->t.n[d]; k++)
            if (st->t.t[d][k].glob == gf)
                return target <= lv_of(lv, d) + rec_move(lg, st, d, k);
    return 0;
}

/* [div, place] -> its row, B teams passed over (gplay.c place_club) but in a play-out (kind 4) */
static int place_code(const sr_state *st, const sr_league *lg, int d, int p, int kind)
{
    if (d >= st->t.nd || d >= GD || p < 1 || p > st->t.n[d])
        return -1;
    if (kind == 4 || !lg || !lg->seconds)
        return d * GT + p - 1;
    for (int k = 0; k < st->t.n[d]; k++)
        if (!b_inelig(st, lg, d * GT + k) && !--p)
            return d * GT + k;
    return -1;
}

/* gplay.c's WN_ST (the SEASON WINNERS page's last season): written by gp_season's pass over the
 * career club's league only. */
static void wn_add(sr_state *st, int type, int idx, u16 team)
{
    sr_winners *wn = st->gp.se->winners;
    if (!st->gp.wn_on || !wn || team == 0xFFFF || wn->n >= SR_WINNERS_MAX) return;
    sr_winners_entry *x = &wn->e[wn->n++];
    x->type = (u8)type; x->idx = (u8)idx; x->team = team;
}

/* The season's input (gp_season's GP_IN): the career club, its played tie, the stage tables and
 * the kept 'keep' table of a division of the league. */
static void gp_begin(sr_state *st, const sr_season *se)
{
    st->gp.se = se;
    st->gp.mgr = se->mgr;
    st->gp.played_a = se->tie_a; st->gp.played_b = se->tie_b; st->gp.played_w = se->tie_w;
    st->gp.ssb = se->bank ? se->bank : &st->gp.no_bank;
    st->gp.srl_div = -1;
}

static void gp_season(sr_state *st, const sr_league *lg)
{
    const u8 *s = lg->spec;
    u16 pw0 = 0, pw1 = 0;
    u32 h = 0;
    for (int d = 0; d < st->t.nd && d < GD; d++)
        for (int k = 0; k < st->t.n[d]; k++) h = h * 31 + st->t.t[d][k].glob + ((u32)st->t.t[d][k].pts << 9);
    st->gp.rng = h;
    st->gp.nres = 0;
    for (int k = 0; k < 3; k++) {
        st->gp.sres[k].kind = 0;
        st->gp.sres[k].team[0] = st->gp.sres[k].team[1] = st->gp.sres[k].glob[0] = st->gp.sres[k].glob[1] = 0xFFFF;
    }
    int prev_loser = -1;                        /* the last tie's loser of the play-off before (type 4) */
    st->gp.srl_div = -1;
    st->gp.ss_used = 0;
    st->gp.wn_on = 0;                           /* (the career club's league: its titles into WN_ST) */
    int wn_div = 0;
    for (int d = 0; d < st->t.nd && d < GD && st->gp.mgr; d++)
        for (int k = 0; k < st->t.n[d]; k++) if (st->t.t[d][k].team == st->gp.mgr) { st->gp.wn_on = 1; wn_div = d; }
    if (st->gp.se->preview)
        st->gp.wn_on = 1;                       /* (the preview: the play-offs' entries only, appended) */
    else if (st->gp.wn_on && st->gp.se->winners) {
        const sr_stage_bank *b = st->gp.ssb;
        st->gp.se->winners->n = 0; st->gp.se->winners->fresh = 1;
        wn_add(st, 'M', wn_div, st->gp.mgr);    /* (the division it played in: the last season's page) */
        for (int d = 0; d < st->t.nd && d < GD; d++)
            if (st->t.n[d])
                wn_add(st, 'L', d, d == 0 && (b->fresh & 4) && b->n[2] == st->t.n[0] ? b->r[2][0].team
                                                                                    : st->t.t[d][0].team);
    }
    for (int pidx = 0; s && *s; pidx++) {
        int kind = s[0], n = s[1], nr = s[2], wdiv = s[3], ldiv = s[4];
        const u8 *e = s + 6, *r = e + 4 * n;
        st->gp.by_rec = s[5] & 2;
        if (kind == K_PROM) {                   /* (no entrants, no rounds) */
            if (!st->gp.se->preview)            /* (the preview: no promedio -- its history is written) */
                promedio(st, lg, 0, wdiv, ldiv);
            s = r;
            continue;
        }
        int club[GN], ok = kind == 1 || kind == 4 || kind == K_TITLE || kind == K_CONT || kind == K_STAGE;
        u16 dteam[2], dglob[2];
        int dec = kind == K_STAGE && stage_dec(st, wdiv, dteam, dglob);    /* (decided mid-season) */
        if (dec) ok = 0;
        if (st->gp.se->preview && (pidx + 1 == st->gp.se->own_pi[0] || pidx + 1 == st->gp.se->own_pi[1]))
            ok = 0;                             /* (the preview: the career club's own) */
        if (s[5] & PO_SUPER) { ok = 0; dec = 0; }   /* (Mexico's Campeon de Campeones: the next season's supercup) */
        st->gp.pidx = pidx;
        int W[GN], L[GN], nt = 0, last = -1, lastr = 0, nlast = 0, played_t2 = 0;
        int P[GN], np = 0, glast = -1, kog = 0, winners = s[5] >> 4 & 7 ? s[5] >> 4 & 7 : 1;
        /* (kog: a knockout round after a group -- its last tie decides: NZ's Grand Final, the Poule) */
        if (!resolve_entrants(st, lg, s, prev_loser, club, 1))
            ok = 0;
        {   /* each play-off's own random stream, from its entrants' rows (4.0.5 rc19: the preview at the
               league's end and the season end decide the same, whatever the other divisions' tables) */
            u32 h2 = 0x9E3779B9u * (u32)(pidx + 1) + (u32)kind;
            for (int i = 0; i < n && i < GN; i++)
                h2 = h2 * 31 + (club[i] >= 0 ? gp_row(st, club[i])->team + ((u32)gp_row(st, club[i])->pts << 16) : 0xFFFFu);
            st->gp.rng = h2;
        }
        if (s[5] & 8) {                         /* (the drawn pot) */
            int pm[GN], tmp[GN];
            pot_perm(st, s, pm);
            for (int i = 0; i < n && i < GN; i++) tmp[i] = club[pm[i]];
            for (int i = 0; i < n && i < GN; i++) club[i] = tmp[i];
        }
        for (int k = 0; k < nr; k++) {
            int ties = r[0], legs = r[1], host = r[2] & 0x7F, skip = r[2] & HOST_SKIP_SAME, level = r[3];
            int pairing = r[4], gap = r[5];
            const u8 *m = r + 6;
            int mem[2 * GN], gm = legs & 0x80 ? ties : 2 * ties;     /* (a group round: ties = its members) */
            for (int i = 0; i < gm; i++) {
                int c = m[i], v = -1;
                if (c == BEST_LOSER) {          /* the best placed loser of the previous round */
                    for (int t = lastr; t < lastr + nlast && t < nt; t++)
                        if (v < 0 || gp_better(st, L[t], v)) v = L[t];
                } else if (c < 0x40) v = c < n ? club[c] : -1;
                else if (c < 0x80 && (s[5] & PO_SKIP2)) {     /* 'skip_second': the k-th placing that is no B team */
                    int k = c & 0x3F, j = 0;
                    for (; j < np; j++) if (!b_inelig(st, lg, P[j]) && !k--) break;
                    v = j < np ? P[j] : -1;
                    if (v >= 0 && i < GT) gs_copy(&st->gp.ms[i], &st->gp.ps[j]);
                } else if (c < 0x80) { v = (c & 0x3F) < np ? P[c & 0x3F] : -1; if (v >= 0 && i < GT) gs_copy(&st->gp.ms[i], &st->gp.ps[c & 0x3F]); }
                else if (c < 0xC0) v = (c & 0x3F) < nt ? W[c & 0x3F] : -1;
                else v = (c & 0x3F) < nt ? L[c & 0x3F] : -1;
                mem[i] = v;
                if (v < 0 && !(v == ABSENT && (legs & 0x80))) ok = 0;
            }
            if (legs & 0x80) {                  /* a group round, its ABSENT members left out */
                u8 bon[GT];
                int gm2 = 0;
                for (int i = 0; i < ties && i < GT; i++)
                    if (mem[i] != ABSENT) { mem[gm2] = mem[i]; bon[gm2] = m[ties + i]; gs_copy(&st->gp.ms[gm2], &st->gp.ms[i]); gm2++; }
                if (ok && gm2 <= GT && np + gm2 <= GN) {
                    group(st, mem, gm2, legs & 0x7F, host == HOST_NEUTRAL, bon, P + np, st->gp.ms, st->gp.ps + np);
                    glast = np;
                }
                np += gm2;
                last = -1;
                r = m + 2 * ties;
                continue;
            }
            if (ok && (pairing & 0x80)) {   /* a draw: the first p members shuffled, paired */
                int pn = (pairing & 0x7F) <= 2 * ties ? pairing & 0x7F : 2 * ties;
                for (int i = pn - 1; i > 0; i--) { int j = (int)gp_below(st, (u32)(i + 1)), x = mem[i]; mem[i] = mem[j]; mem[j] = x; }
            } else if (ok && pairing) {     /* best v worst: sorted, then 1st v last, 2nd v second last ... */
                int pn = pairing <= 2 * ties ? pairing : 2 * ties;
                for (int i = 1; i < pn; i++) {
                    int x = mem[i], j = i;
                    while (j > 0 && gp_better(st, x, mem[j - 1])) { mem[j] = mem[j - 1]; j--; }
                    mem[j] = x;
                }
                int tmp[2 * GN];
                for (int i = 0; i < pn / 2; i++) { tmp[2 * i] = mem[pn - 1 - i]; tmp[2 * i + 1] = mem[i]; }
                for (int i = 0; i < pn; i++) mem[i] = tmp[i];
            }
            lastr = nt; nlast = ties;
            if (glast >= 0) kog = 1;
            for (int i = 0; i < ties; i++) {
                if (ok && nt < GN) {
                    int a = mem[2 * i], b = mem[2 * i + 1], d = (int)gp_row(st, a)->pts - (int)gp_row(st, b)->pts;
                    int w = a == b && skip ? a                                               /* (one club: no tie) */
                          : gap && (d >= gap || -d >= gap) ? (gp_better(st, a, b) ? a : b)  /* (too far apart) */
                          : legs == 3 ? gp_series(st, a, b, level) : gp_tie(st, a, b, legs, host, level);
                    w = gp_played(st, a, b, w);
                    W[nt] = w; L[nt] = w == mem[2 * i] ? mem[2 * i + 1] : mem[2 * i];
                    last = nt; glast = -1;
                }
                nt++;
            }
            r = m + 2 * ties;
        }
        if (ldiv == RESERVE) ldiv = st->t.nd;   /* (the reserve: out of every division) */
        if (kind == K_STAGE) {                  /* a stage title: its champion / runner-up, no move */
            sr_result *x = (wdiv & 3) < 3 ? &sres_of(st, wdiv >> 2)[wdiv & 3] : 0;
            if (x && dec) {
                x->kind = K_STAGE; x->div = (u8)(wdiv >> 2);
                x->team[0] = dteam[0]; x->team[1] = dteam[1];
                x->glob[0] = dglob[0]; x->glob[1] = dglob[1];
            } else if (x && ok && last >= 0) {
                x->kind = K_STAGE; x->div = (u8)DV(W[last]);
                x->team[0] = gp_row(st, W[last])->team; x->glob[0] = gp_row(st, W[last])->glob;
                x->team[1] = L[last] != W[last] ? gp_row(st, L[last])->team : 0xFFFF;
                x->glob[1] = L[last] != W[last] ? gp_row(st, L[last])->glob : 0xFFFF;
            }
        } else if (kind == K_TITLE && st->gp.se->host && st->gp.se->host->played_title &&
                   (played_t2 = st->gp.se->host->played_title(st->gp.se->host->ctx, pidx, &pw0, &pw1) &&
                                team_code(st, pw0) >= 0)) {
            gp_result(st, kind, team_code(st, pw0), team_code(st, pw1));    /* (played: a group contest's final) */
            LOG("[playoffs] play-off %d: the group contest played, its champion %04X (runner-up %04X)\n",
                pidx, (unsigned)pw0, (unsigned)pw1);
        } else if (ok && (kind == K_TITLE || kind == K_CONT) && glast >= 0 && !kog) {   /* a final group: its 1st (2nd) */
            gp_result(st, kind, P[glast], np - glast > 1 ? P[glast + 1] : -1);
        } else if (ok && (kind == K_TITLE || kind == K_CONT) && last >= 0) {   /* the last tie's winner (loser) */
            gp_result(st, kind, W[last], L[last] == W[last] ? -1 : L[last]);
        } else if (ok && glast >= 0 && kind == 1) {    /* a final group: its top `winners` to winner_div */
            for (int i = 0; i < n; i++) {
                int c = club[i], to = DV(c), top = 0;
                for (int k = 0; k < winners; k++) if (P[glast + k] == c) top = 1;
                if (top) to = wdiv;
                else if (DV(c) == wdiv) to = ldiv;
                gp_move(st, c, to - DV(c));
            }
        } else if (ok && last >= 0 && kind == 4) {     /* playout: the last round's losers down */
            for (int t = lastr; t < lastr + nlast && t < GN; t++) gp_move(st, L[t], ldiv - DV(L[t]));
        } else if (ok && last >= 0 && kind == 1) {     /* the winner to winner_div; another winner_div entrant down */
            int w = W[last];
            for (int i = 0; i < n; i++) {
                int c = club[i], to = DV(c);
                if (c == w) to = wdiv;
                else if (DV(c) == wdiv) to = ldiv;
                gp_move(st, c, to - DV(c));
            }
        }
        if (kind == K_STAGE)                    /* (WN_ST: this play-off's winner) */
            wn_add(st, 'K', pidx, (wdiv & 3) < 3 && sres_of(st, wdiv >> 2)[wdiv & 3].kind ? sres_of(st, wdiv >> 2)[wdiv & 3].team[0]
                                                                                        : 0xFFFF);
        else if (kind == K_TITLE && played_t2)
            wn_add(st, 'K', pidx, pw0 ? pw0 : 0xFFFF);
        else if (ok && kind != 4 && (glast >= 0 || last >= 0))
            wn_add(st, 'K', pidx, gp_row(st, glast >= 0 && !(kog && last >= 0) ? P[glast] : W[last])->team);
        if (ok && kind == K_TITLE && glast < 0)  /* (a title play-off's last three ties: the MLS conference finals) */
            for (int t = nt > 3 ? nt - 3 : 0; t < nt && t < GN; t++) wn_add(st, 0x80 | (pidx & 0x7F), t, gp_row(st, W[t])->team);
        prev_loser = ok && last >= 0 ? L[last] : -1;
        s = r;
    }
    st->gp.wn_on = 0;
    if (st->gp.ss_used && !st->gp.se->preview)
        st->gp.ssb->fresh = 0;                  /* (this league's stage tables used: next season's are new) */
}

int32_t sr_knockouts(sr_state *st, const sr_league *lg, const sr_season *se)
{
    const int32_t before = st->nmv;
    if (sr_league_check(lg, 0) || sr_season_check(se, 0))
        return -1;                              /* (not sound: nothing played) */
    gp_begin(st, se);
    st->gp.nst[0] = st->gp.nst[1] = 0;          /* (the kept 'keep' table, if any) */
    {
        const sr_stage_bank *kb = se->bank;
        if (kb && kb->n_kept && kb->kept_div < lg->divisions) {
            for (int32_t i = 0; i < kb->n_kept; i++) {
                const sr_row *x = &kb->kept[i];
                sr_row *y = &st->gp.st[0][i];
                y->team = x->team; y->glob = x->glob; y->won = x->won; y->gf = x->gf; y->ga = x->ga; y->pts = x->pts;
            }
            st->gp.nst[0] = kb->n_kept;
        }
    }
    gp_season(st, lg);
    return st->nmv - before;
}

/* =====================================================================
 * The career club's own play-off (ko_build, gp_need, t2_shape)
 * ===================================================================== */

const uint8_t *sr_spec_at(const sr_league *lg, int32_t pidx)
{
    if (!lg || !lg->spec || pidx < 0)
        return 0;
    const u8 *s = lg->spec;
    for (int32_t i = 0; i < pidx && *s; i++) {
        const u8 *r = s + 6 + 4 * s[1];
        if (s[0] != K_PROM)
            for (int k = 0; k < s[2]; k++) r += 6 + 2 * r[0];
        s = r;
    }
    return *s ? s : 0;
}

/* The tables a career has at its league's end (the club's division d first, then the ones simulated
 * early) as the season end's input: no other division. */
static int ce_tables(sr_state *st, const sr_season *se, int32_t nt, const int32_t *divs, const sr_row *const *tables,
                     const int32_t *ns)
{
    const int32_t d = divs[0];
    int32_t nd = 0;
    for (int32_t t = 0; t < nt; t++) {
        if (divs[t] < 0 || divs[t] >= GD || ns[t] < (t ? 1 : 2) || ns[t] > GT)
            return 0;
        if (divs[t] + 1 > nd)
            nd = divs[t] + 1;
    }
    st->t.nd = (u8)nd;
    for (int32_t j = 0; j < nd; j++)
        st->t.n[j] = 0;
    for (int32_t t = 0; t < nt; t++) {
        st->t.n[divs[t]] = (u8)ns[t];
        for (int32_t k = 0; k < ns[t]; k++) {
            sr_row *r = &st->t.t[divs[t]][k];
            r->team = tables[t][k].team; r->glob = tables[t][k].glob; r->won = tables[t][k].won;
            r->gf = tables[t][k].gf; r->ga = tables[t][k].ga; r->pts = tables[t][k].pts;
        }
    }
    gp_begin(st, se);
    st->gp.nst[0] = st->gp.nst[1] = 0;
    if (se->bank && se->bank->n_kept && se->bank->kept_div == d) {
        for (int32_t i = 0; i < se->bank->n_kept; i++) {
            const sr_row *x = &se->bank->kept[i];
            sr_row *y = &st->gp.st[0][i];
            y->team = x->team; y->glob = x->glob; y->won = x->won; y->gf = x->gf; y->ga = x->ga; y->pts = x->pts;
        }
        st->gp.nst[0] = se->bank->n_kept;
    }
    return 1;
}

int32_t sr_contest_entrants(sr_state *st, const sr_league *lg, const sr_season *se, int32_t nt,
                            const int32_t *divs, const sr_row *const *tables, const int32_t *ns, int32_t pidx,
                            uint16_t *team, uint16_t *glob, uint8_t *prio, uint16_t *pts, const uint8_t **spec)
{
    const u8 *s = sr_league_check(lg, 0) || sr_season_check(se, 0) ? 0 : sr_spec_at(lg, pidx);
    if (!s || nt < 1 || nt > 10 || !ce_tables(st, se, nt, divs, tables, ns))
        return 0;
    int *club = st->gp.ce_club;
    const u8 fresh = st->gp.ssb->fresh;
    st->gp.by_rec = s[5] & 2;
    const int ok = s[1] <= SR_CONTEST_MAX && resolve_entrants(st, lg, s, -1, club, 0);
    st->gp.ssb->fresh = fresh;                  /* (read only: the season end uses them again) */
    if (!ok)
        return 0;
    const u8 *e = s + 6;
    for (int32_t i = 0; i < s[1]; i++) {
        team[i] = gp_row(st, club[i])->team;
        glob[i] = gp_row(st, club[i])->glob;
        pts[i] = gp_row(st, club[i])->pts;
        prio[i] = (u8)(e[4 * i] == 3 ? e[4 * i + 2] - 1 : club[i] > 255 ? 255 : club[i]);
    }
    if (st->gp.by_rec)                          /* (the better record hosts: the ranks by it, ko_build) */
        for (int32_t i = 0; i < s[1]; i++) {
            prio[i] = 0;
            for (int32_t j = 0; j < s[1]; j++)
                if (j != i && gp_better(st, club[j], club[i])) prio[i]++;
        }
    st->gp.ce_n = s[1];
    *spec = s;
    return s[1];
}

/* gplay.c playable: a knockout a career club can play (promotion, placement, play-out, title,
 * continental, stage; entrants by place or stage, members entrants / W / L, no group round, a
 * best-of-three not last). */
static int gp_playable(const u8 *s)
{
    int n = s[1], nr = s[2], nt = 0;
    const u8 *e = s + 6, *r = e + 4 * n;
    if ((s[0] < 1 || (s[0] > 6 && s[0] != K_STAGE) || s[0] == 2) || n > 24 || nr > 8 || nr < 1)
        return 0;
    if (s[5] & PO_SUPER) return 0;      /* (played as the next season's national supercup) */
    for (int i = 0; i < n; i++)
        if (e[4 * i] != 0 && e[4 * i] != 3 && e[4 * i] != 5 && e[4 * i] != 6) return 0;
    for (int k = 0; k < nr; k++) {
        if (r[1] < 1 || r[1] > 3 || (r[1] == 3 && k == nr - 1)) return 0;
        for (int i = 0; i < 2 * r[0]; i++)
            if ((r[6 + i] >= BEST_LOSER && r[6 + i] < 0x80) || (k == 0 && r[6 + i] >= 0x80)) return 0;
        nt += r[0];
        r += 6 + 2 * r[0];
    }
    return nt <= 24;
}

int32_t sr_group_shape_of(const uint8_t *s, sr_group_shape *o)
{
    sr_group_shape x;
    int n, nr, k = 0, ent = 0;
    const u8 *r;
    if (!s) return 0;
    n = s[1]; nr = s[2]; r = s + 6 + 4 * n;
    if (s[0] != K_TITLE || nr < 2) return 0;
    for (int i = 0; i < n; i++) if (s[6 + 4 * i] != 0 && s[6 + 4 * i] != 8) return 0;   /* ([div, place], a club: NZ) */
    x.g = 0; x.m = 0; x.meet = 0; x.nk = 0; x.q = 0;
    for (; k < nr && (r[1] & 0x80) && (r[1] & 0x7F); k++) {
        if (x.g && (r[0] != x.m || (r[1] & 0x7F) != x.meet)) return 0;
        x.m = r[0]; x.meet = r[1] & 0x7F; x.g++;
        for (int i = 0; i < r[0]; i++) if (r[6 + i] != ent++) return 0;
        r += 6 + 2 * r[0];
    }
    for (; k < nr && r[1] == 0x80; k++) r += 6 + 2 * r[0];
    for (; k < nr; k++) {
        if ((r[1] & 0x80) || r[1] < 1 || r[1] > 2 || x.nk >= 4) return 0;
        if (!x.nk) x.q = 2 * r[0];
        x.kt[x.nk] = r[0];
        x.kb[x.nk++] = (u8)((r[1] == 2 ? 0x80 : 0x01) | (r[3] == LV_PENS ? 0x04 : r[3] == LV_ET_PENS ? 0x14 :
                                                       r[3] == LV_ET_BETTER ? 0x10 : 0));
        r += 6 + 2 * r[0];
    }
    if (x.g < 1 || !x.nk || ent != n || x.q < x.g || x.q > 2 * x.g || x.g * x.m > 24) return 0;
    if (o) {
        o->g = x.g; o->m = x.m; o->meet = x.meet; o->nk = x.nk; o->q = x.q;
        for (int i = 0; i < 4; i++) { o->kt[i] = i < x.nk ? x.kt[i] : 0; o->kb[i] = i < x.nk ? x.kb[i] : 0; }
    }
    return 1;
}

int32_t sr_club_playoff(sr_state *st, const sr_league *lg, const sr_season *se, int32_t d, const sr_row *table,
                        int32_t n, uint16_t club, uint8_t *other, int32_t *nother)
{
    const sr_row *tables[1] = { table };
    const int32_t divs[1] = { d }, ns[1] = { n };
    int m = -1;
    *nother = 0;
    if (sr_league_check(lg, 0) || sr_season_check(se, 0) || !ce_tables(st, se, 1, divs, tables, ns))
        return -1;
    for (int32_t k = 0; k < n; k++)
        if (table[k].team == club) m = k;
    if (m < 0)
        return -1;
    const u8 fresh = st->gp.ssb->fresh;
    int32_t found = -1;
    for (int32_t pidx = 0; found < 0; pidx++) {
        const u8 *s = sr_spec_at(lg, pidx), *e = s ? s + 6 : 0;
        if (!s)
            break;
        const int t2 = sr_group_shape_of(s, 0);
        if ((!gp_playable(s) && !t2) || s[0] == K_STAGE)
            continue;                           /* (stage titles: built mid-season) */
        int in = 0, ok = 1, no = 0;
        st->gp.by_rec = s[5] & 2;
        for (int i = 0; i < s[1]; i++) {
            const int c = ent_code(st, lg, e + 4 * i, -1, s[0]);
            if (c == d * GT + m) in = 1;
        }
        if (!in)
            continue;
        for (int i = 0; i < s[1] && ok; i++) {  /* the other divisions: simulated now (at most 2; a group */
            const int dd = e[4 * i + 1];        /*  contest's at most 9) */
            int j = 0;
            if (dd == d) continue;
            while (j < no && other[j] != dd) j++;
            if (j == no) { if (no < (t2 ? 9 : 2)) other[no++] = (u8)dd; else ok = 0; }
        }
        if (!ok)
            continue;
        *nother = no;
        found = t2 ? -2 - pidx : pidx;
    }
    st->gp.ssb->fresh = fresh;                  /* (read only) */
    return found;
}

/* The contest's round-1 ties simulated at its build, the career club waiting in round 1 (the
 * Mod's ko_build 'presim', build_game): its draw seeded as hseed (the play-off index, the
 * entrants' team words), entrants a, b of the last sr_contest_entrants; the winner (a or b). */
void sr_contest_seed(sr_state *st, int32_t pidx, const uint16_t *team, int32_t n)
{
    u32 h = (u32)pidx;
    for (int32_t i = 0; i < n; i++)
        h = h * 31 + team[i];
    st->gp.rng = h;
}

int32_t sr_contest_tie(sr_state *st, int32_t a, int32_t b, int32_t legs, int32_t host, int32_t level)
{
    if (a < 0 || b < 0 || a >= st->gp.ce_n || b >= st->gp.ce_n)
        return a;
    const int ca = st->gp.ce_club[a], cb = st->gp.ce_club[b];
    const int w = legs == 3 ? gp_series(st, ca, cb, level) : gp_tie(st, ca, cb, legs, host, level);
    return w == ca ? a : b;
}
