/*
 * league_playoffs.c -- a world's play-offs and play-outs at the season end
 * (include/league_playoffs.h).
 *
 * Ported from the SWOS 96/97 Mod's tools/dplay.c (dp_season: the Italian play-offs) and
 * tools/gplay.c (gp_season: the pack's knockout play-offs), which carry this notice:
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
 * The two decisions are ported as they are -- their random numbers, seeds, tie model and
 * order, and their types (int is 32 bits on both our targets), so they read against the
 * Mod's line by line -- and the same final tables give the same ties as the Mod's own code
 * (scripts/league_playoffs_check.sh builds both and compares them). The career club's own
 * play-off is played as a contest (the Mod's dp_lend / gp_lend: src/world_playoff.c), its
 * result standing here (po_win, out_win, cpo_win, the groups' ties played); the Coppa Italia
 * Serie C winner is the league cup's (src/world_cups.c); B teams are the Mod's sec_post; of
 * the generic kinds, promotion and play-out move clubs, placement is played and walked past.
 *
 * The rules (dplay.c, 2025-26: FIGC C.U. 39/A for Serie B, NOIF art. 49 + C.U. 114/A for
 * Serie C):
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
 * A tie is decided by the clubs' strength, (points + 20) squared (dplay) or by place and
 * tier (gplay), the home side +10%.
 */
#include "league_playoffs.h"
#include "league_groups.h"
#include "team_pool.h"
#include "world_playoff.h"
#include "world_cups.h"
#include "world_cupd.h"
#include "game_types.h"
#include "diy_competition.h"

extern uint16_t _playPromotedAnimation;
extern uint16_t _playRelegatedAnimation;
extern TeamFileEx _careerTeam;
extern DIYCompetitionEx _diyCompetitionStart;
extern uint16_t _teamsCountryNumbers[256];

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;

/* ---- The final tables, as the season end leaves each division ---- */

enum { GT = 32, GD = LEAGUE_MAX_DIVISIONS };
struct lp_row { u16 team, glob, won, gf, ga, pts; };
static struct {
    u8 nd;                      /* divisions kept this season */
    u8 n[GD];
    struct lp_row t[GD][GT];
} s_in;

void LeaguePlayoffs_Begin(void)
{
    s_in.nd = 0;
    for (int32_t d = 0; d < GD; d++)
        s_in.n[d] = 0;
}

void LeaguePlayoffs_KeepTable(const uint8_t *competition, uint16_t division)
{
    (void)competition;
    DIYCompetitionEx *ex = &_diyCompetitionStart;
    uint8_t *base = (uint8_t *)&ex->base;
    uint16_t n = (uint16_t)ex->base.numTeams;
    if (division >= GD || ex->base.competitionType != 0)
        return;
    if (n > GT)
        n = GT;
    for (uint16_t k = 0; k < n; k++) {
        const DIYTeamStats *st = (const DIYTeamStats *)(base + ex->matchData.league.teamEntryOffsets[k] +
                                                        DIY_STANDINGS_OFFSET);
        const TeamFileEx *team = (const TeamFileEx *)diy_resolve_team(st->teamSelOffset);
        struct lp_row *r = &s_in.t[division][k];
        r->team = PACK_BE_U16(team->team.countryNumber, team->team.teamOrdinal);
        r->glob = (u16)team->team.globalTeamNumber;
        r->won = st->wins;
        r->gf = st->goalsFor;
        r->ga = st->goalsAgainst;
        r->pts = st->points;
    }
    s_in.n[division] = (u8)n;
    if (division + 1 > s_in.nd)
        s_in.nd = (u8)(division + 1);
}

/* ---- The moves the play-offs make (division deltas, the Mod's) ---- */

struct lp_mv { u16 glob; s8 delta; };
static struct lp_mv s_mv[48];
static int32_t s_nmv, s_dp_moves;     /* (the Italian rules' moves come first) */

static void add_move(u16 glob, int32_t delta)
{
    if (delta && s_nmv < (int32_t)(sizeof s_mv / sizeof s_mv[0])) {
        s_mv[s_nmv].glob = glob;
        s_mv[s_nmv].delta = (s8)delta;
        s_nmv++;
    }
}

/* =====================================================================
 * dplay.c: dp_season
 * ===================================================================== */

enum { NT = 24, NDIV = 5 };
enum { K_DIRECT = 1, K_PRE, K_SEMI, K_FINAL, K_OUT, K_GR1 = 10, K_GR2, K_NR1, K_NR2, K_FFS, K_FFF };
struct dp_row { u16 team, glob, won, gf, ga, pts; };
static struct {
    u8 ng;
    u8 n[NDIV];
    struct dp_row t[NDIV][NT];
    u16 po_win, out_win;        /* the career club's play-off / play-out played (world_playoff.c):
                                   its winner's team number, 0: simulated */
    u16 cpo_win;                /* its Serie C play-off, the same */
    u16 cupw;                   /* the Coppa Italia Serie C winner (world_cups.c), 0 unknown */
} DP_IN;

static u32 dp_rng;
static u32 dp_below(u32 n)
{
    dp_rng = dp_rng * 1103515245u + 12345u;
    return ((dp_rng >> 16) * n) >> 16;
}

#define DIV(c) ((c) / NT)
#define DPL(c) ((c) % NT)
static const struct dp_row *dp_row_of(int c) { return &DP_IN.t[DIV(c)][DPL(c)]; }
static u8 lot[NDIV * NT];

static int dp_better(int a, int b)      /* 1 if club a is better placed than club b */
{
    const struct dp_row *x = dp_row_of(a), *y = dp_row_of(b);
    if (DPL(a) != DPL(b) || DIV(a) == DIV(b)) return DPL(a) < DPL(b);
    if (x->pts != y->pts) return x->pts > y->pts;
    if (x->won != y->won) return x->won > y->won;
    if (x->gf != y->gf) return x->gf > y->gf;
    return lot[a] < lot[b];
}

static u32 dp_str2(int c, int home)
{
    u32 s = dp_row_of(c)->pts + 20;
    if (home) s = s * 11 / 10;
    return s * s;
}

/* legs 1 or 2; host = the single match's / the return leg's home club; level = the club
 * through when level (-1: extra time + penalties, by strength) */
static int dp_tie(int kind, int host, int other, int legs, int level)
{
    u32 sh = dp_str2(host, 1), so = dp_str2(other, 0), w;
    (void)kind;
    if (dp_below(100) < (legs == 1 ? 22u : 12u)) {
        if (level >= 0) return level;
        u32 a = dp_str2(host, 0);       /* penalties: close to a coin toss */
        sh = 2 * a + so;
        so = a + 2 * so;
    }
    w = dp_below(sh + so) < sh ? (u32)host : (u32)other;
    return (int)w;
}

static void dp_move(int c, int delta) { add_move(dp_row_of(c)->glob, delta); }

static int best_of(const int *c, int n)
{
    int b = c[0];
    for (int i = 1; i < n; i++) if (dp_better(c[i], b)) b = c[i];
    return b;
}

static void sort_better(int *c, int n)
{
    for (int i = 1; i < n; i++) {
        int x = c[i], j = i;
        while (j > 0 && dp_better(x, c[j - 1])) { c[j] = c[j - 1]; j--; }
        c[j] = x;
    }
}

static void shuffle(int *c, int n)
{
    for (int i = n - 1; i > 0; i--) { int j = (int)dp_below((u32)(i + 1)), x = c[i]; c[i] = c[j]; c[j] = x; }
}

/* seeded round: seeds s[0..n-1] v the others drawn; two legs, return at the seed's,
 * level -> the seed */
static void seeded(int kind, int *s, int *o, int n, int *win)
{
    shuffle(o, n);
    for (int i = 0; i < n; i++) win[i] = dp_tie(kind, s[i], o[i], 2, s[i]);
}

int32_t LeaguePlayoffs_TestHeld;        /* (a test: the play-out held whatever the points) */

int32_t LeaguePlayoffs_OutHeld(uint16_t pts16, uint16_t pts17)
{
    return pts16 <= pts17 + 4 || LeaguePlayoffs_TestHeld;
}

#define OUT_HELD(t) LeaguePlayoffs_OutHeld((t)[15].pts, (t)[16].pts)

int32_t LeaguePlayoffs_PlayoffHeld(uint16_t pts3, uint16_t pts4)
{
    return !(pts3 > pts4 + 14) || LeaguePlayoffs_TestHeld;
}

static void serie_b(void)
{
    int n = DP_IN.n[0];
    const struct dp_row *t = DP_IN.t[0];
    if (n < 17) return;
    if (!LeaguePlayoffs_PlayoffHeld(t[2].pts, t[3].pts)) {
        dp_move(2, -1);
    } else {
        int w58 = dp_tie(K_PRE, 4, 7, 1, 4), w67 = dp_tie(K_PRE, 5, 6, 1, 5);
        int s1 = dp_tie(K_SEMI, 2, w67, 2, 2), s2 = dp_tie(K_SEMI, 3, w58, 2, 3);
        int h = dp_better(s1, s2) ? s1 : s2, o = h == s1 ? s2 : s1;
        int w = dp_tie(K_FINAL, h, o, 2, t[DPL(h)].pts == t[DPL(o)].pts ? -1 : h);
        for (int k = 2; k < 8 && DP_IN.po_win; k++)     /* (played: its real winner) */
            if (t[k].team == DP_IN.po_win) w = k;
        dp_move(w, -1);
    }
    if (!OUT_HELD(t)) {
        dp_move(16, 1);
    } else {
        int w = dp_tie(K_OUT, 15, 16, 2, t[15].pts == t[16].pts ? -1 : 15);
        if (DP_IN.out_win == t[15].team) w = 15;        /* (played: its real winner) */
        if (DP_IN.out_win == t[16].team) w = 16;
        dp_move(w == 15 ? 16 : 15, 1);
    }
}

/* the Coppa Italia Serie C winner's play-off place: the winner itself when it finished 11th
 * or lower, the 11th of its group when it is promoted or already in (2nd-10th); unknown:
 * the best 11th of the groups */
static int cup_place(int ng)
{
    int e[4];
    for (int g = 1; g <= ng && DP_IN.cupw; g++)
        for (int k = 0; k < DP_IN.n[g]; k++)
            if (DP_IN.t[g][k].team == DP_IN.cupw) return g * NT + (k >= 10 ? k : 10);
    for (int g = 0; g < ng; g++) e[g] = (g + 1) * NT + 10;
    return best_of(e, ng);
}

static void serie_c(void)
{
    int ng = DP_IN.ng, q[8], nq = 0, thirds[4], seconds[4], cup, nr1[5], nr1o[5], w1[5], s2[4], o2[4], w2[4];
    if (ng < 2 || ng > 4) return;
    for (int g = 1; g <= ng; g++) if (DP_IN.n[g] < 11) return;
    for (int g = 1; g <= ng; g++) {
        int b = g * NT, r2[4];
        r2[0] = b + 3;
        for (int k = 0; k < 3; k++) r2[1 + k] = dp_tie(K_GR1, b + 4 + k, b + 9 - k, 1, b + 4 + k);
        sort_better(r2, 4);
        q[nq++] = dp_tie(K_GR2, r2[0], r2[3], 1, r2[0]);
        q[nq++] = dp_tie(K_GR2, r2[1], r2[2], 1, r2[1]);
        thirds[g - 1] = b + 2;
        seconds[g - 1] = b + 1;
    }
    cup = cup_place(ng);
    /* national R1: seeds = the thirds, the cup winner, the best group qualifier */
    {
        int bq = best_of(q, nq), ns = 0, no = 0;
        for (int g = 0; g < ng; g++) nr1[ns++] = thirds[g];
        nr1[ns++] = cup;
        nr1[ns++] = bq;
        for (int i = 0; i < nq; i++) if (q[i] != bq) nr1o[no++] = q[i];
        if (ns != no) return;           /* (3 groups: 5 v 5) */
        seeded(K_NR1, nr1, nr1o, ns, w1);
        /* national R2: seeds = the seconds, the best R1 winner */
        int bw = best_of(w1, ns), k = 0, m = 0;
        for (int g = 0; g < ng; g++) s2[k++] = seconds[g];
        s2[k++] = bw;
        for (int i = 0; i < ns; i++) if (w1[i] != bw) o2[m++] = w1[i];
        if (k != m) return;
        seeded(K_NR2, s2, o2, k, w2);
        /* Final Four: free draw, two legs, extra time + penalties */
        if (k != 4) return;
        shuffle(w2, 4);
        int f1 = dp_tie(K_FFS, w2[1], w2[0], 2, -1), f2 = dp_tie(K_FFS, w2[3], w2[2], 2, -1);
        int w = dp_tie(K_FFF, f2, f1, 2, -1);
        for (int g = 1; g <= ng && DP_IN.cpo_win; g++)          /* (played: its real winner) */
            for (int k = 0; k < DP_IN.n[g]; k++)
                if (DP_IN.t[g][k].team == DP_IN.cpo_win) w = g * NT + k;
        dp_move(w, -1);
    }
}

static void dp_season(void)
{
    u32 h = 0;
    for (int d = 0; d < NDIV; d++)      /* the seed: the tables (a save gives the same results) */
        for (int k = 0; k < DP_IN.n[d] && k < NT; k++) {
            const struct dp_row *r = &DP_IN.t[d][k];
            h = h * 31 + r->glob;
            h = h * 31 + r->pts + (r->gf << 8);
        }
    dp_rng = h;
    for (int i = 0; i < NDIV * NT; i++) lot[i] = (u8)dp_below(256);
    serie_b();
    serie_c();
}

/* =====================================================================
 * gplay.c: gp_season, its promotion and play-out kinds
 * ===================================================================== */

enum { GN = 64, RESERVE = 0xFE, BEST_LOSER = 0x3F };
enum { HOST_LISTED, HOST_BETTER, HOST_DRAWN, HOST_NEUTRAL, HOST_SKIP_SAME = 0x80 };
enum { LV_BETTER, LV_ET_PENS, LV_ET_BETTER, LV_PENS, LV_AWAY_PENS, LV_AWAY_BETTER };

static u32 gp_rng;
static u32 gp_below(u32 n)
{
    gp_rng = gp_rng * 1103515245u + 12345u;
    return ((gp_rng >> 16) * n) >> 16;
}

#define DV(c) ((c) / GT)
#define PL(c) ((c) % GT)
static const struct lp_row *gp_row(int c) { return &s_in.t[DV(c)][PL(c)]; }

static int by_rec;                      /* spec flag 2 'host_by_record': in one division the better record */
static int gp_better(int a, int b)      /* the higher tier, then the better place */
{
    if (DV(a) != DV(b)) return DV(a) < DV(b);
    if (by_rec) {                       /* (points, wins, goal difference, goals; then the place) */
        const struct lp_row *x = gp_row(a), *y = gp_row(b);
        int dx = x->gf - x->ga, dy = y->gf - y->ga;
        if (x->pts != y->pts) return x->pts > y->pts;
        if (x->won != y->won) return x->won > y->won;
        if (dx != dy) return dx > dy;
        if (x->gf != y->gf) return x->gf > y->gf;
    }
    return PL(a) < PL(b);
}

static u32 gp_str2(int c, int home)     /* strength: the place in its division, the tier */
{
    int n = s_in.n[DV(c)];
    int s = 30 + 40 * (n - PL(c)) / (n ? n : 1) - 25 * DV(c);
    if (s < 5) s = 5;
    if (home) s = s * 11 / 10;
    return (u32)(s * s);
}

/* a tie: a = the single match's host / the first leg's home club (listed order), b = the other */
static int gp_tie(int a, int b, int legs, int host, int level)
{
    int h = a, o = b, w;
    if (host == HOST_BETTER || host == HOST_NEUTRAL) {      /* the better placed hosts the single match / return */
        if (gp_better(b, a)) { h = b; o = a; }
        else { h = a; o = b; }
        if (legs == 2 || host == HOST_NEUTRAL) { int x = h; h = o; o = x; }   /* (h = the first leg's / listed) */
    } else if (host == HOST_DRAWN && gp_below(2)) { h = b; o = a; }
    /* the club with home advantage: a single match's host, the return leg's home club */
    int adv = (legs == 1) ? (host == HOST_NEUTRAL ? -1 : h) : o;
    u32 sh = gp_str2(h, adv == h), so = gp_str2(o, adv == o);
    if (gp_below(100) < (legs == 1 ? 22u : 12u)) {          /* level after the regulation time */
        int bp = gp_better(h, o) ? h : o;
        if (level == LV_BETTER || level == LV_ET_BETTER || level == LV_AWAY_BETTER) return bp;
        u32 x = gp_str2(h, 0), y = gp_str2(o, 0);             /* e.t. + penalties: close to a coin toss */
        sh = 2 * x + y; so = x + 2 * y;
    }
    w = gp_below(sh + so) < sh ? h : o;
    return w;
}

static int gp_series(int a, int b, int level)   /* best of three: games 1 and 3 at the better placed's */
{
    int h = gp_better(a, b) ? a : b, o = h == a ? b : a, wh = 0, wo = 0;
    for (int g = 0; g < 3 && wh < 2 && wo < 2; g++)
        if (gp_tie(g == 1 ? o : h, g == 1 ? h : o, 1, HOST_LISTED, level) == h) wh++; else wo++;
    return wh == 2 ? h : o;
}

static void gp_move(int c, int delta) { add_move(gp_row(c)->glob, delta); }

/* A tie the career club played (world_playoff.c): its two clubs and its winner (team
 * numbers) -- the Mod's GP_IN.pl_w; applied after the tie is simulated, so the random
 * numbers drawn are the same either way. */
static u16 s_playedA, s_playedB, s_playedW;
static int gp_played(int a, int b, int w)
{
    const u16 ta = gp_row(a)->team, tb = gp_row(b)->team;
    if (!s_playedW || !((ta == s_playedA && tb == s_playedB) || (ta == s_playedB && tb == s_playedA)))
        return w;
    return s_playedW == ta ? a : s_playedW == tb ? b : w;
}

/* The spec's knockout play-offs, in order (world_import.py compiles only the kinds played
 * here: promotion 1 and play-out 4, entrants [division, place], rounds of ties). */
static void gp_season(const u8 *s)
{
    u32 h = 0;
    for (int d = 0; d < s_in.nd && d < GD; d++)
        for (int k = 0; k < s_in.n[d]; k++) h = h * 31 + s_in.t[d][k].glob + ((u32)s_in.t[d][k].pts << 9);
    gp_rng = h;
    for (; s && *s; ) {
        int kind = s[0], n = s[1], nr = s[2], wdiv = s[3], ldiv = s[4];
        const u8 *e = s + 6, *r = e + 4 * n;
        by_rec = s[5] & 2;
        int club[GN], ok = kind == 1 || kind == 4;
        int W[GN], L[GN], nt = 0, last = -1, lastr = 0, nlast = 0;
        for (int i = 0; i < n && i < GN; i++) {
            int ty = e[4 * i], d = e[4 * i + 1], p = e[4 * i + 2];
            if (ty != 0 || d >= s_in.nd || p < 1 || p > s_in.n[d]) ok = 0;
            else club[i] = d * GT + p - 1;
        }
        for (int k = 0; k < nr; k++) {
            int ties = r[0], legs = r[1], host = r[2] & 0x7F, skip = r[2] & HOST_SKIP_SAME, level = r[3];
            int pairing = r[4], gap = r[5];
            const u8 *m = r + 6;
            int mem[2 * GN];
            if (legs & 0x80) {          /* a group round (m members, m bonus points: 2 x ties
                                         * bytes, as a knockout round's): not played here */
                ok = 0;
                r = m + 2 * ties;
                continue;
            }
            for (int i = 0; i < 2 * ties; i++) {
                int c = m[i], v = -1;
                if (c == BEST_LOSER) {          /* the best placed loser of the previous round */
                    for (int t = lastr; t < lastr + nlast && t < nt; t++)
                        if (v < 0 || gp_better(L[t], v)) v = L[t];
                } else if (c < 0x40) v = c < n ? club[c] : -1;
                else if (c < 0x80) v = -1;      /* (a group placing: none here) */
                else if (c < 0xC0) v = (c & 0x3F) < nt ? W[c & 0x3F] : -1;
                else v = (c & 0x3F) < nt ? L[c & 0x3F] : -1;
                mem[i] = v;
                if (v < 0) ok = 0;
            }
            if (ok && (pairing & 0x80)) {   /* a draw: the first p members shuffled, paired */
                int pn = (pairing & 0x7F) <= 2 * ties ? pairing & 0x7F : 2 * ties;
                for (int i = pn - 1; i > 0; i--) { int j = (int)gp_below((u32)(i + 1)), x = mem[i]; mem[i] = mem[j]; mem[j] = x; }
            } else if (ok && pairing) {     /* best v worst: sorted, then 1st v last, 2nd v second last ... */
                int pn = pairing <= 2 * ties ? pairing : 2 * ties;
                for (int i = 1; i < pn; i++) {
                    int x = mem[i], j = i;
                    while (j > 0 && gp_better(x, mem[j - 1])) { mem[j] = mem[j - 1]; j--; }
                    mem[j] = x;
                }
                int tmp[2 * GN];
                for (int i = 0; i < pn; i++) tmp[i] = mem[i];  /* (an odd pn keeps its last) */
                for (int i = 0; i < pn / 2; i++) { tmp[2 * i] = mem[pn - 1 - i]; tmp[2 * i + 1] = mem[i]; }
                for (int i = 0; i < pn; i++) mem[i] = tmp[i];
            }
            lastr = nt; nlast = ties;
            for (int i = 0; i < ties; i++) {
                if (ok && nt < GN) {
                    int a = mem[2 * i], b = mem[2 * i + 1], d = (int)gp_row(a)->pts - (int)gp_row(b)->pts;
                    int w = a == b && skip ? a                                           /* (one club: no tie) */
                          : gap && (d >= gap || -d >= gap) ? (gp_better(a, b) ? a : b)  /* (too far apart: no tie) */
                          : legs == 3 ? gp_series(a, b, level) : gp_tie(a, b, legs, host, level);
                    w = gp_played(a, b, w);
                    W[nt] = w; L[nt] = w == mem[2 * i] ? mem[2 * i + 1] : mem[2 * i];
                    last = nt;
                }
                nt++;
            }
            r = m + 2 * ties;
        }
        if (ldiv == RESERVE) ldiv = s_in.nd;    /* (the reserve: out of every division) */
        if (ok && last >= 0 && kind == 4) {     /* playout: the last round's losers down */
            for (int t = lastr; t < lastr + nlast && t < GN; t++) gp_move(L[t], ldiv - DV(L[t]));
        } else if (ok && last >= 0 && kind == 1) {      /* the winner to winner_div; another winner_div entrant down */
            int w = W[last];
            for (int i = 0; i < n; i++) {
                int c = club[i], to = DV(c);
                if (c == w) to = wdiv;
                else if (DV(c) == wdiv) to = ldiv;
                gp_move(c, to - DV(c));
            }
        }
        s = r;
    }
}

/* =====================================================================
 * The decision: the Mod's final places, written as one-level moves
 * ===================================================================== */

static int32_t sign(int32_t x) { return x < 0 ? -1 : x > 0; }

static s8 s_want[GD][GT];               /* each club's move, a level: -1 up, 1 down */

/* ---- B teams (the Mod's sec_post, gplay.c): in level terms ---- */

static int32_t level_of(const LeagueGroups *lg, int32_t d)
{
    if (!lg)
        return d;
    for (int32_t l = lg->levels - 1; l >= 0; l--)
        if (d >= lg->level[l].first)
            return l;
    return 0;
}

static int32_t find_club(u16 g, int32_t *d, int32_t *k)
{
    for (*d = 0; *d < s_in.nd; (*d)++)
        for (*k = 0; *k < s_in.n[*d]; (*k)++)
            if (s_in.t[*d][*k].glob == g)
                return 1;
    return 0;
}

/* B team at division db: going up one level is not allowed -- above `top`, or into or
 * above its first team's level after this season's moves */
static int32_t up_bad(const LeaguePlayoffs *lp, const LeagueGroups *lg, int32_t i, int32_t db, u16 base)
{
    const int32_t target = level_of(lg, db) - 1;
    int32_t df, kf;
    if (lp->secondTop != 0xFF && target < lp->secondTop)
        return 1;
    return lp->second[i][1] != 0xFF && find_club((u16)(base + lp->second[i][1]), &df, &kf) &&
           target <= level_of(lg, df) + s_want[df][kf];
}

/* a club skipped for a promotion place: a B team that may not go up */
static int32_t ineligible(const LeaguePlayoffs *lp, const LeagueGroups *lg, int32_t d, int32_t k, u16 base)
{
    for (int32_t i = 0; i < lp->seconds; i++)
        if (s_in.t[d][k].glob == (u16)(base + lp->second[i][0]))
            return !(lp->secondFlags & 1) || up_bad(lp, lg, i, d, base);
    return 0;
}

static void second_teams(const LeaguePlayoffs *lp, const LeagueGroups *lg, u16 base)
{
    const int32_t lowestLevel = lg ? lg->levels - 1 : lp->divisions - 1;
    for (int32_t i = 0; i < lp->seconds; i++) {
        const u16 gb = (u16)(base + lp->second[i][0]);
        int32_t db, kb, df = 0, kf = 0;
        if (!find_club(gb, &db, &kb))
            continue;
        const int32_t lb = level_of(lg, db), nb = lb + s_want[db][kb], dl = s_want[db][kb];
        const int32_t rf = lp->second[i][1] != 0xFF && find_club((u16)(base + lp->second[i][1]), &df, &kf);
        const int32_t nf = rf ? level_of(lg, df) + s_want[df][kf] : -1;
        const int32_t lowest = lb == lowestLevel;
        const int32_t coupled = rf && nf >= (nb < lb ? lb : nb) && nb <= lb;
        if (coupled && (!lowest || (lp->secondFlags & 2))) {
            s_want[db][kb] = 1;                 /* the first team in its level: one down */
            SWOS_DBG("[playoffs] B team (global %u) down: its first team in its level\n", (unsigned)gb);
            if (!lowest)                        /* the best placed relegated of its division stays */
                for (int32_t k = 0; k < s_in.n[db]; k++)
                    if (k != kb && s_want[db][k] > 0) { s_want[db][k] = 0; break; }
        } else if (nb < lb && (coupled || up_bad(lp, lg, i, db, base))) {
            s_want[db][kb] = 0;                 /* up, not allowed: held */
            SWOS_DBG("[playoffs] B team (global %u) held: may not go up\n", (unsigned)gb);
        } else
            continue;
        if (nb < lb)                            /* its promotion: the next club of its division */
            for (int32_t k = 0; k < s_in.n[db]; k++) {
                if (k == kb || s_want[db][k] != 0 || ineligible(lp, lg, db, k, base))
                    continue;
                s_want[db][k] = (s8)dl;
                break;
            }
    }
}

void LeaguePlayoffs_Decide(uint16_t country, const int8_t *table, int8_t *scratch)
{
    int32_t count = 0;
    const LeaguePlayoffs *all = World_LeaguePlayoffs(&count), *lp = 0;
    for (int32_t i = 0; i < count; i++)
        if (all[i].country == country)
            lp = &all[i];
    if (!lp || s_in.nd != lp->divisions)
        return;
    for (int32_t d = 0; d < lp->divisions; d++)
        if (!s_in.n[d])
            return;                     /* (a division not played out this season) */
    /* every club's final place, before the moves: the next season's cups (world_cups.c) */
    for (int32_t d = 0; d < lp->divisions; d++)
        for (int32_t k = 0; k < s_in.n[d]; k++) {
            WorldCups_KeepPlace((uint8_t)country, (uint16_t)d, s_in.t[d][k].team, (uint8_t)(k + 1));
            CupD_KeepPlace((uint8_t)country, (uint16_t)d, s_in.t[d][k].team, k, s_in.n[d]);
        }

    s_nmv = 0;
    if (lp->italy) {
        DP_IN.ng = lp->groups;
        DP_IN.po_win = DP_IN.out_win = DP_IN.cpo_win = 0;
        DP_IN.cupw = WorldCups_LeagueCupWinner((uint8_t)country);
        if (g_worldPlayoff.kind == WORLD_PLAYOFF_C_PLAYOFF)
            DP_IN.cpo_win = g_worldPlayoff.winner;
        if (g_worldPlayoff.kind == WORLD_PLAYOFF_B_PLAYOUT)
            DP_IN.out_win = g_worldPlayoff.winner;
        if (g_worldPlayoff.kind == WORLD_PLAYOFF_B_PLAYOFF)
            DP_IN.po_win = g_worldPlayoff.winner;
        for (int32_t d = 0; d < NDIV; d++) {
            const int32_t src = d == 0 ? lp->serieB : lp->groupFirst + d - 1;
            DP_IN.n[d] = 0;
            if (d > lp->groups || src >= lp->divisions)
                continue;
            DP_IN.n[d] = s_in.n[src] < NT ? s_in.n[src] : NT;
            for (int32_t k = 0; k < DP_IN.n[d]; k++) {
                const struct lp_row *x = &s_in.t[src][k];
                struct dp_row *y = &DP_IN.t[d][k];
                y->team = x->team; y->glob = x->glob; y->won = x->won;
                y->gf = x->gf; y->ga = x->ga; y->pts = x->pts;
            }
        }
        dp_season();
    }
    s_dp_moves = s_nmv;
    s_playedA = s_playedB = s_playedW = 0;
    if (g_worldPlayoff.kind == WORLD_PLAYOFF_GROUP_TIE && g_worldPlayoff.winner) {
        s_playedA = g_worldPlayoff.team[0];
        s_playedB = g_worldPlayoff.team[1];
        s_playedW = g_worldPlayoff.winner;
    }
    gp_season(lp->spec);

    /* Each club's move as the Mod makes it: its record's by place, the play-offs' on top. */
    const LeagueGroups *lg = LeagueGroups_ForCountry(country);
    const int32_t lowest = lg ? lg->level[lg->levels - 1].first : lp->divisions - 1;
    const u16 base = _teamsCountryNumbers[country], manager = (u16)_careerTeam.team.globalTeamNumber;
    for (int32_t d = 0; d < lp->divisions; d++) {
        const int32_t n = s_in.n[d];
        for (int32_t k = 0; k < n; k++) {
            const u16 g = s_in.t[d][k].glob;
            int32_t want = 0;
            if (k < lp->promoted[d] && d > 0)
                want = -1;
            else if (k >= n - lp->relegated[d])
                want = 1;
            for (int32_t i = 0; i < s_nmv; i++)
                if (s_mv[i].glob == g) {
                    want += s_mv[i].delta;
                    SWOS_DBG("[playoffs] division %d place %d (global %u): %s by %s\n", (int)d,
                             (int)k + 1, (unsigned)g, s_mv[i].delta < 0 ? "up" : "down",
                             i < s_dp_moves ? "the Italian rules" : "a knockout");
                }
            s_want[d][k] = (s8)sign(want);
        }
    }
    second_teams(lp, lg, base);

    /* A club whose move differs from the season's (scratch) gets the Mod's, one level; out
     * of the lowest division, below the league is the pool: the slot stays and takes the
     * next club of it (team_pool.c), never the manager's. */
    u8 out[TEAM_POOL_SLOTS / 8];
    int32_t career = 0x7FFF;
    for (int32_t i = 0; i < TEAM_POOL_SLOTS / 8; i++)
        out[i] = 0;
    for (int32_t d = 0; d < lp->divisions; d++)
        for (int32_t k = 0; k < s_in.n[d]; k++) {
            const u16 g = s_in.t[d][k].glob;
            int32_t want = s_want[d][k];
            if (want > 0 && d >= lowest) {
                if (g != manager && g >= base && g - base < TEAM_POOL_SLOTS)
                    out[(g - base) >> 3] |= (u8)(1 << ((g - base) & 7));
                want = 0;
            }
            if (sign(scratch[g] - table[g]) != want)
                scratch[g] = (int8_t)(table[g] + want);
            if (g == manager)
                career = want;
        }
    for (int32_t o = 0; o < TEAM_POOL_SLOTS; o++)      /* (the slots in their order, as the Mod's) */
        if (out[o >> 3] & (1 << (o & 7))) {
            const int32_t in = TeamPool_Swap((u8)country, (u8)o);
            if (in >= 0) {
                LeagueGroups_Enter((u16)(base + o));
                CupD_NoteArrival((uint8_t)country, (uint8_t)o);     /* (the Coppa D's entry order) */
                SWOS_DBG("[pool] slot %d (global %u) -> identity %d\n", (int)o, (unsigned)(base + o), (int)in);
            }
        }
    TeamPool_SwapsDone();
    if (career != 0x7FFF) {
        _playPromotedAnimation = career < 0;
        _playRelegatedAnimation = career > 0;
    }
}
