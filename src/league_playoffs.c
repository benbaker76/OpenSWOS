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
#include "endian_helpers.h"

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

/* ---- A league's stage tables (the Mod's GP_SS banks, gp_stage / gp_keep) ---- */

LeagueStageBank g_leagueStages[LEAGUE_STAGE_BANKS];
static LeagueStageBank s_noBank;        /* (no room: nothing kept) */

void LeaguePlayoffs_ResetStages(void)
{
    uint8_t *p = (uint8_t *)g_leagueStages;
    for (uint32_t i = 0; i < sizeof g_leagueStages; i++)
        p[i] = 0;
}

static LeagueStageBank *bank_of(uint8_t country, int32_t make)
{
    for (int32_t i = 0; i < LEAGUE_STAGE_BANKS; i++)
        if (g_leagueStages[i].used && g_leagueStages[i].key == country)
            return &g_leagueStages[i];
    if (!make)
        return 0;
    for (int32_t i = 0; i < LEAGUE_STAGE_BANKS; i++)
        if (!g_leagueStages[i].used) {
            LeagueStageBank *b = &g_leagueStages[i];
            uint8_t *p = (uint8_t *)b;
            for (uint32_t j = 0; j < sizeof *b; j++)
                p[j] = 0;
            b->used = 1;
            b->key = country;
            return b;
        }
    return 0;
}

/* The DIY league's row in table order -> a kept row. */
static void table_row(int32_t place, LeagueStageRow *o)
{
    DIYCompetitionEx *ex = &_diyCompetitionStart;
    const DIYTeamStats *st = (const DIYTeamStats *)((uint8_t *)&ex->base + ex->matchData.league.teamEntryOffsets[place] +
                                                    DIY_STANDINGS_OFFSET);
    const TeamFileEx *team = (const TeamFileEx *)diy_resolve_team(st->teamSelOffset);
    o->team = PACK_BE_U16(team->team.countryNumber, team->team.teamOrdinal);
    o->glob = (u16)team->team.globalTeamNumber;
    o->won = st->wins;
    o->gf = st->goalsFor;
    o->ga = st->goalsAgainst;
    o->pts = st->points;
}

static void row_copy(LeagueStageRow *d, const LeagueStageRow *s)
{
    d->team = s->team; d->glob = s->glob; d->won = s->won; d->gf = s->gf; d->ga = s->ga; d->pts = s->pts;
}

/* gp_stage: stage k's table; annual: r[2] = every other stage table of this season by club,
 * by points, goal difference, goals (the earlier order on a tie). */
void LeaguePlayoffs_StageTable(uint8_t country, uint32_t k, uint32_t annual)
{
    const int32_t n = _diyCompetitionStart.base.numTeams;
    LeagueStageBank *b = bank_of(country, 1);
    if (!b || k >= LEAGUE_STAGE_TABLES || n < 1 || n > GT)
        return;
    for (int32_t i = 0; i < n; i++)
        table_row(i, &b->r[k][i]);
    b->n[k] = (u8)n;
    b->fresh |= (u8)(1 << k);
    if (!annual || k == 0 || b->n[0] != n)
        return;
    LeagueStageRow *a = b->r[2], tmp;
    for (int32_t i = 0; i < n; i++)
        row_copy(&a[i], &b->r[0][i]);
    for (int32_t q = 1; q < LEAGUE_STAGE_TABLES; q++) {
        if (q == 2 || !(b->fresh & 1 << q) || b->n[q] != n)
            continue;
        const LeagueStageRow *o2 = b->r[q];
        for (int32_t i = 0; i < n; i++)
            for (int32_t j = 0; j < n; j++)
                if (o2[j].glob == a[i].glob) {
                    a[i].won += o2[j].won; a[i].gf += o2[j].gf; a[i].ga += o2[j].ga; a[i].pts += o2[j].pts;
                }
    }
    for (int32_t i = 1; i < n; i++)
        for (int32_t j = i; j > 0; j--) {
            const int32_t dp = a[j].pts - a[j - 1].pts, dd = (a[j].gf - a[j].ga) - (a[j - 1].gf - a[j - 1].ga);
            if (dp > 0 || (dp == 0 && (dd > 0 || (dd == 0 && a[j].gf > a[j - 1].gf)))) {
                row_copy(&tmp, &a[j]); row_copy(&a[j], &a[j - 1]); row_copy(&a[j - 1], &tmp);
            } else break;
        }
    b->n[2] = (u8)n;
    b->fresh |= 4;
}

void LeaguePlayoffs_KeepStage(uint8_t country, uint16_t division)
{
    const int32_t n = _diyCompetitionStart.base.numTeams;
    LeagueStageBank *b = bank_of(country, 1);
    if (!b || n < 2 || n > 21)                  /* (the Mod's keep block holds 21) */
        return;
    for (int32_t i = 0; i < n; i++) {
        table_row(i, &b->kept[i]);
        b->kept[i].won = b->kept[i].gf = b->kept[i].ga = 0;     /* (the Mod keeps places and points) */
    }
    b->keptDiv = (u8)division;
    b->nKept = (u8)n;
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

/* ---- gplay.c gp_season, whole: every kind and entrant (the kept stage tables, the Trofeo's
 * finalists, the title deciders' seeds, the previous play-off's loser), group rounds, the drawn
 * pot; its results -- a title's champion, a continental play-off's two, a stage title's
 * champion and runner-up -- beside the moves. (Not yet: Argentina's promedio, the stage titles
 * decided mid-season, a played group contest.) ---- */

enum { K_TITLE = 5, K_CONT = 6, K_PROM = 7, K_STAGE = 8, GS = 2, SS_BASE = 4 };
enum { SS_N = LEAGUE_STAGE_TABLES };

struct lp_res { u8 kind, div; u16 team[2], glob[2]; };  /* title: [0] the champion; continental: winner, loser */
static struct lp_res s_res[4], s_sres[3], s_srl[3];
static int32_t s_nres, s_srlDiv;
static u16 s_mgr;                               /* the career club (team word), 0: none */
static u8 s_nst[GS];                            /* the captured divisions' kept tables (gp_keep) */
static LeagueStageRow s_st[GS][GT];
static LeagueStageBank *SSB = &s_noBank;
static int s_ssUsed;

static int gp_mgr_div(void)                     /* the career club's division in the tables, or -1 */
{
    for (int d = 0; d < s_in.nd && d < GD; d++)
        for (int k = 0; k < s_in.n[d]; k++) if (s_in.t[d][k].team == s_mgr && s_mgr) return d;
    return -1;
}

static void gp_result(int kind, int w, int l)
{
    if (s_nres >= 4) return;
    struct lp_res *x = &s_res[s_nres++];
    x->kind = (u8)kind; x->div = (u8)DV(w);
    x->team[0] = gp_row(w)->team; x->glob[0] = gp_row(w)->glob;
    x->team[1] = l >= 0 ? gp_row(l)->team : 0xFFFF; x->glob[1] = l >= 0 ? gp_row(l)->glob : 0xFFFF;
}

static int stage_club(int d, int st, int a)     /* place a of kept stage st -> its row of division d, or -1 */
{
    u16 g;
    if (st >= SS_BASE && st < SS_BASE + SS_N) {
        int k = st - SS_BASE;
        s_ssUsed = 1;
        if (!(SSB->fresh & 1 << k)) {           /* (not written this season: the division's final table) */
            if (d >= s_in.nd || a < 1 || a > s_in.n[d]) return -1;
            return d * GT + a - 1;
        }
        if (a < 1 || a > SSB->n[k] || d >= s_in.nd) return -1;
        g = SSB->r[k][a - 1].glob;
    } else {
        if (st >= GS || a < 1 || a > s_nst[st] || d >= s_in.nd) return -1;
        g = s_st[st][a - 1].glob;
    }
    for (int k = 0; k < s_in.n[d]; k++) if (s_in.t[d][k].glob == g) return d * GT + k;
    return -1;
}

static int glob_club(int d, u16 g)              /* a global number -> its row of division d, or -1 */
{
    if (g == 0xFFFF || d >= s_in.nd) return -1;
    for (int k = 0; k < s_in.n[d]; k++) if (s_in.t[d][k].glob == g) return d * GT + k;
    return -1;
}

static struct lp_res *sres_of(int d)            /* the career club's division's stage titles, or another's */
{
    if (d == gp_mgr_div() || (gp_mgr_div() < 0 && d == 0)) return s_sres;
    if (s_srlDiv != d) {
        for (int k = 0; k < 3; k++) {
            s_srl[k].kind = 0;
            s_srl[k].team[0] = s_srl[k].team[1] = s_srl[k].glob[0] = s_srl[k].glob[1] = 0xFFFF;
        }
        s_srlDiv = d;
    }
    return s_srl;
}

static u16 stage_glob(int d, int k, int j)      /* division d's stage k champion (j 0) / runner-up (1) */
{
    return sres_of(d)[k].glob[j];               /* (decided mid-season: not yet) */
}

static int title_club(int d, int c)             /* the Trofeo's finalists (entrant type 5) */
{
    u16 c0 = stage_glob(d, 0, 0), c1 = stage_glob(d, 1, 0);
    if (c == 1) return glob_club(d, c0);
    if (c0 != c1 || c == 4) return glob_club(d, c1);
    return glob_club(d, stage_glob(d, c - 2, 1));
}

static int kept_pts(int st, int c)              /* club c's points in kept stage st (0: not in it) */
{
    u16 g = gp_row(c)->glob;
    if (st >= GS) return 0;
    for (int k = 0; k < s_nst[st]; k++) if (s_st[st][k].glob == g) return s_st[st][k].pts;
    return 0;
}

static int agg_better(int st, int a, int b)     /* the two rounds' points together, then the final table's place */
{
    int x = kept_pts(st, a) + gp_row(a)->pts, y = kept_pts(st, b) + gp_row(b)->pts;
    return x != y ? x > y : PL(a) < PL(b);
}

/* Malta's title decider (entrant type 6): see the Mod's gplay.c split_seed. */
static int split_seed(int d, int st, int a)
{
    if (d >= s_in.nd || s_in.n[d] < 2 || a < 1 || a > 4) return -1;
    int o1 = stage_club(d, st, 1), o2 = stage_club(d, st, 2), c1 = d * GT, c2 = d * GT + 1;
    if (o1 < 0 || o2 < 0) return -1;
    if (o1 == c1) return o1;
    if (o2 == c2 || (o1 == c2 && o2 == c1)) {
        int x = agg_better(st, o1, c1) ? o1 : c1, y = x == o1 ? c1 : o1;
        return a == 1 || a == 4 ? x : y;
    }
    int x = o1 == c2 ? o1 : c1 == o2 ? c1 : -1;
    if (x >= 0) {
        int y = x == o1 ? c1 : o1, z = x == o1 ? o2 : c2;
        if (agg_better(st, z, y)) { int t = y; y = z; z = t; }
        return a == 1 || a == 4 ? x : a == 2 ? y : z;
    }
    int q[4] = { o1, c1, o2, c2 };
    for (int i = 1; i < 4; i++)
        for (int j = i; j > 0 && agg_better(st, q[j], q[j - 1]); j--) { int t = q[j]; q[j] = q[j - 1]; q[j - 1] = t; }
    return q[a - 1];
}

/* Uruguay's and Peru's national title play-off (entrant type 7): see the Mod's title_seed. */
static int t_place(int d, int c)
{
    for (int p = 1; d < s_in.nd && p <= s_in.n[d]; p++) if (stage_club(d, 6, p) == c) return p;
    return 0;
}
static int title_seed(int d, int rule, int a)
{
    if (d >= s_in.nd || s_in.n[d] < 4 || a < 1 || a > 4) return -1;
    int A = stage_club(d, 4, 1), C = stage_club(d, rule == 1 ? 7 : 5, 1), t1 = stage_club(d, 6, 1);
    if (A < 0 || C < 0 || t1 < 0) return -1;
    if (A == C) return A;
    if (rule == 1) return a == 1 ? A : a == 2 ? C : t1;
    int pa = t_place(d, A), pc = t_place(d, C), q[4], nq = 0;
    if (pa && pa <= 2 && pc && pc <= 2) return a == 1 || a == 4 ? A : C;
    if (pa > 0 && pa <= 8) q[nq++] = A;
    if (pc > 0 && pc <= 8) q[nq++] = C;
    for (int p = 1; nq < 4 && p <= s_in.n[d]; p++) {
        int c = stage_club(d, 6, p);
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
        for (int i = 0; i < 4; i++) if (q[i] != x && q[i] != y && (z < 0 || t_place(d, q[i]) < t_place(d, z))) z = q[i];
        if (y < 0) {
            for (int i = 0; i < 4; i++) if (q[i] != x && q[i] != z) { y = q[i]; break; }
        }
        return a == 1 || a == 4 ? x : a == 2 ? y : z;
    }
    for (int i = 1; i < 4; i++)
        for (int j = i; j > 0 && t_place(d, q[j]) < t_place(d, q[j - 1]); j--) { int t = q[j]; q[j] = q[j - 1]; q[j - 1] = t; }
    return q[a == 1 ? 0 : a == 4 ? 3 : a == 2 ? 1 : 2];
}

/* a group match: goals by strength (draws ~ 1 in 4), home advantage unless neutral */
static void gmatch(int h, int a, int neutral, int *gh, int *ga)
{
    u32 sh = gp_str2(h, !neutral), sa = gp_str2(a, 0);
    if (gp_below(100) < 26) { int g = (int)gp_below(3); *gh = g; *ga = g; return; }
    int w = gp_below(sh + sa) < sh, gw = 1 + (int)gp_below(3), gl = (int)gp_below((u32)gw);
    *gh = w ? gw : gl; *ga = w ? gl : gw;
}

struct gs { int c, pts, gd, gf; };
static int gs_before(const struct gs *x, const struct gs *y)    /* points, goal difference, goals, the place */
{
    if (x->pts != y->pts) return x->pts > y->pts;
    if (x->gd != y->gd) return x->gd > y->gd;
    if (x->gf != y->gf) return x->gf > y->gf;
    return gp_better(x->c, y->c);
}

/* a group round: mem[0..m) round robin `meet` times (home and away alternate), start bonus
 * points; meet 0 = a ranking round (the members ranked by their own groups' figures, ins) */
static void group(const int *mem, int m, int meet, int neutral, const u8 *bonus, int *out, const struct gs *ins,
                  struct gs *outs)
{
    struct gs t[GT];
    for (int i = 0; i < m; i++) {
        t[i].c = mem[i]; t[i].pts = bonus[i]; t[i].gd = 0; t[i].gf = 0;
        if (!meet) { t[i].pts = ins[i].pts; t[i].gd = ins[i].gd; t[i].gf = ins[i].gf; }
    }
    for (int k = 0; k < meet; k++)
        for (int i = 0; i < m; i++)
            for (int j = i + 1; j < m; j++) {
                int h = (k & 1) ? j : i, a = (k & 1) ? i : j, gh, ga;
                gmatch(t[h].c, t[a].c, neutral, &gh, &ga);
                t[h].gf += gh; t[a].gf += ga; t[h].gd += gh - ga; t[a].gd += ga - gh;
                if (gh > ga) t[h].pts += 3; else if (gh < ga) t[a].pts += 3; else { t[h].pts++; t[a].pts++; }
            }
    for (int i = 1; i < m; i++) {
        struct gs x = t[i]; int j = i;
        while (j > 0 && gs_before(&x, &t[j - 1])) { t[j] = t[j - 1]; j--; }
        t[j] = x;
    }
    for (int i = 0; i < m; i++) { out[i] = t[i].c; outs[i] = t[i]; }
}

static struct gs PS[GN], ms[GT];

/* the drawn pot (Colombia's cuadrangulares): the entrants after the heads (one per leading
 * group round) drawn */
static void pot_perm(const u8 *s, int *pm)
{
    int n = s[1], h = 0;
    for (int i = 0; i < n; i++) pm[i] = i;
    if (!(s[5] & 8)) return;
    const u8 *r = s + 6 + 4 * n;
    for (int k = 0; k < s[2] && (r[1] & 0x80); k++) { h++; r += 6 + 2 * r[0]; }
    for (int i = n - 1; i > h; i--) { int j = h + (int)gp_below((u32)(i - h + 1)), x = pm[i]; pm[i] = pm[j]; pm[j] = x; }
}

static void gp_season(const u8 *s, uint8_t country)
{
    u32 h = 0;
    for (int d = 0; d < s_in.nd && d < GD; d++)
        for (int k = 0; k < s_in.n[d]; k++) h = h * 31 + s_in.t[d][k].glob + ((u32)s_in.t[d][k].pts << 9);
    gp_rng = h;
    s_nres = 0;
    for (int k = 0; k < 3; k++) {
        s_sres[k].kind = 0;
        s_sres[k].team[0] = s_sres[k].team[1] = s_sres[k].glob[0] = s_sres[k].glob[1] = 0xFFFF;
    }
    int prev_loser = -1;                        /* the last tie's loser of the play-off before (type 4) */
    s_srlDiv = -1;
    s_ssUsed = 0;
    SSB = bank_of(country, 0);
    if (!SSB)
        SSB = &s_noBank;
    for (int pidx = 0; s && *s; pidx++) {
        int kind = s[0], n = s[1], nr = s[2], wdiv = s[3], ldiv = s[4];
        const u8 *e = s + 6, *r = e + 4 * n;
        by_rec = s[5] & 2;
        if (kind == K_PROM) {                   /* (Argentina's promedio: not ported yet) */
            s = r;
            continue;
        }
        int club[GN], ok = kind == 1 || kind == 4 || kind == K_TITLE || kind == K_CONT || kind == K_STAGE;
        int W[GN], L[GN], nt = 0, last = -1, lastr = 0, nlast = 0;
        int P[GN], np = 0, glast = -1, winners = s[5] >> 4 ? s[5] >> 4 : 1;
        for (int i = 0; i < n && i < GN; i++) {
            int ty = e[4 * i], d = e[4 * i + 1], p = e[4 * i + 2];
            if (ty == 3) {
                club[i] = stage_club(d, e[4 * i + 3], p);
                if (club[i] < 0) ok = 0;
            } else if (ty == 4) {
                club[i] = prev_loser;
                if (club[i] < 0) ok = 0;
            } else if (ty == 5) {
                club[i] = title_club(d, p);
                if (club[i] < 0) ok = 0;
            } else if (ty == 6) {
                club[i] = split_seed(d, e[4 * i + 3], p);
                if (club[i] < 0) ok = 0;
            } else if (ty == 7) {
                club[i] = title_seed(d, e[4 * i + 3], p);
                if (club[i] < 0) ok = 0;
            } else if (ty != 0 || d >= s_in.nd || p < 1 || p > s_in.n[d]) ok = 0;
            else club[i] = d * GT + p - 1;
        }
        if (s[5] & 8) {                         /* (the drawn pot) */
            int pm[GN], tmp[GN];
            pot_perm(s, pm);
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
                        if (v < 0 || gp_better(L[t], v)) v = L[t];
                } else if (c < 0x40) v = c < n ? club[c] : -1;
                else if (c < 0x80) { v = (c & 0x3F) < np ? P[c & 0x3F] : -1; if (v >= 0 && i < GT) ms[i] = PS[c & 0x3F]; }
                else if (c < 0xC0) v = (c & 0x3F) < nt ? W[c & 0x3F] : -1;
                else v = (c & 0x3F) < nt ? L[c & 0x3F] : -1;
                mem[i] = v;
                if (v < 0) ok = 0;
            }
            if (legs & 0x80) {                  /* a group round */
                if (ok && ties <= GT && np + ties <= GN) {
                    group(mem, ties, legs & 0x7F, host == HOST_NEUTRAL, m + ties, P + np, ms, PS + np);
                    glast = np;
                }
                np += ties;
                last = -1;
                r = m + 2 * ties;
                continue;
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
                for (int i = 0; i < pn / 2; i++) { tmp[2 * i] = mem[pn - 1 - i]; tmp[2 * i + 1] = mem[i]; }
                for (int i = 0; i < pn; i++) mem[i] = tmp[i];
            }
            lastr = nt; nlast = ties;
            for (int i = 0; i < ties; i++) {
                if (ok && nt < GN) {
                    int a = mem[2 * i], b = mem[2 * i + 1], d = (int)gp_row(a)->pts - (int)gp_row(b)->pts;
                    int w = a == b && skip ? a                                           /* (one club: no tie) */
                          : gap && (d >= gap || -d >= gap) ? (gp_better(a, b) ? a : b)  /* (too far apart) */
                          : legs == 3 ? gp_series(a, b, level) : gp_tie(a, b, legs, host, level);
                    w = gp_played(a, b, w);
                    W[nt] = w; L[nt] = w == mem[2 * i] ? mem[2 * i + 1] : mem[2 * i];
                    last = nt; glast = -1;
                }
                nt++;
            }
            r = m + 2 * ties;
        }
        if (ldiv == RESERVE) ldiv = s_in.nd;    /* (the reserve: out of every division) */
        if (kind == K_STAGE) {                  /* a stage title: its champion / runner-up, no move */
            struct lp_res *x = (wdiv & 3) < 3 ? &sres_of(wdiv >> 2)[wdiv & 3] : 0;
            if (x && ok && last >= 0) {
                x->kind = K_STAGE; x->div = (u8)DV(W[last]);
                x->team[0] = gp_row(W[last])->team; x->glob[0] = gp_row(W[last])->glob;
                x->team[1] = L[last] != W[last] ? gp_row(L[last])->team : 0xFFFF;
                x->glob[1] = L[last] != W[last] ? gp_row(L[last])->glob : 0xFFFF;
            }
        } else if (ok && (kind == K_TITLE || kind == K_CONT) && glast >= 0) {   /* a final group: its 1st (2nd) */
            gp_result(kind, P[glast], np - glast > 1 ? P[glast + 1] : -1);
        } else if (ok && (kind == K_TITLE || kind == K_CONT) && last >= 0) {   /* the last tie's winner (loser) */
            gp_result(kind, W[last], L[last] == W[last] ? -1 : L[last]);
        } else if (ok && glast >= 0 && kind == 1) {    /* a final group: its top `winners` to winner_div */
            for (int i = 0; i < n; i++) {
                int c = club[i], to = DV(c), top = 0;
                for (int k = 0; k < winners; k++) if (P[glast + k] == c) top = 1;
                if (top) to = wdiv;
                else if (DV(c) == wdiv) to = ldiv;
                gp_move(c, to - DV(c));
            }
        } else if (ok && last >= 0 && kind == 4) {     /* playout: the last round's losers down */
            for (int t = lastr; t < lastr + nlast && t < GN; t++) gp_move(L[t], ldiv - DV(L[t]));
        } else if (ok && last >= 0 && kind == 1) {     /* the winner to winner_div; another winner_div entrant down */
            int w = W[last];
            for (int i = 0; i < n; i++) {
                int c = club[i], to = DV(c);
                if (c == w) to = wdiv;
                else if (DV(c) == wdiv) to = ldiv;
                gp_move(c, to - DV(c));
            }
        }
        prev_loser = ok && last >= 0 ? L[last] : -1;
        s = r;
    }
    if (s_ssUsed) SSB->fresh = 0;               /* (this league's stage tables used: next season's are new) */
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
    {
        extern uint16_t _selTeamNumber;
        s_mgr = _selTeamNumber;
    }
    s_nst[0] = s_nst[1] = 0;                    /* (the kept 'keep' table of this country, if any) */
    {
        LeagueStageBank *kb = bank_of((uint8_t)country, 0);
        if (kb && kb->nKept && kb->keptDiv < lp->divisions) {
            for (int32_t i = 0; i < kb->nKept; i++)
                s_st[0][i] = kb->kept[i];
            s_nst[0] = kb->nKept;
        }
    }
    gp_season(lp->spec, (uint8_t)country);
    {   /* gp_champ: a 'title' play-off's champion first in the top division's top 9, the clubs
         * above it one place down -- the qualifiers read it as the champion (a winner outside
         * the top 9: the table's 1st stays) */
        extern uint8_t _primaryQualifiedTeamGrids[];
        extern uint16_t _cachedPrimaryQualifiedTeamIds[];
        static TeamFileEx tmp;
        for (int32_t k = 0; k < s_nres; k++) {
            if (s_res[k].kind != K_TITLE)
                continue;
            int32_t j = 0;
            while (j < 9 && BE_READ_U16(_primaryQualifiedTeamGrids + (uint32_t)j * sizeof(TeamFileEx)) != s_res[k].team[0])
                j++;
            if (j == 0 || j >= 9)
                continue;
            uint8_t *g = _primaryQualifiedTeamGrids;
            const uint16_t id = _cachedPrimaryQualifiedTeamIds[j];
            tmp = *(TeamFileEx *)(g + (uint32_t)j * sizeof(TeamFileEx));
            for (int32_t i = j; i > 0; i--) {
                *(TeamFileEx *)(g + (uint32_t)i * sizeof(TeamFileEx)) = *(TeamFileEx *)(g + (uint32_t)(i - 1) * sizeof(TeamFileEx));
                _cachedPrimaryQualifiedTeamIds[i] = _cachedPrimaryQualifiedTeamIds[i - 1];
            }
            *(TeamFileEx *)g = tmp;
            _cachedPrimaryQualifiedTeamIds[0] = id;
            SWOS_DBG("[playoffs] country %d: the title play-off's champion %04X (was %d.)\n", (int)country,
                     (unsigned)s_res[k].team[0], (int)j + 1);
        }
    }

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
