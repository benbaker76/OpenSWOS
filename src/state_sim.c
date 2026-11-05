/*
 * state_sim.c -- a world's state championships at the season end (include/state_sim.h): the SWOS
 * 96/97 Mod's tools/statesim.c (MIT, Davide Lorigliola) as it is -- its table (SS_TAB, compiled by
 * scripts/world_states.py as the Mod's statespec.sim_table), its rankings (ST_RANK) and its season
 * word passed in where it reads them as the DOS program's globals.
 *
 * The played state (ST_RANK block 0, written when the career club plays its own): its relegation
 * phase from its real stage-1 order; every other state of the world: its stage 1 simulated (the
 * compiled match list: groups, cross blocks, fixed calendars; strength = its best 11's prices),
 * then the same relegation phase, then its knockouts (its champion: ss_champ). Blocks: dw key (the
 * contest's first club word), db n, db 0, n x dw team words, the relegated last; the list ends
 * with dw 0. The team words are the Mod's (file | ordinal << 8, our country as the file).
 */
#include "state_sim.h"

#if SWOS_WORLDS

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define MAXN 16

u8 ss_ord[13 * MAXN];                   /* (each state's stage-1 order: club indices, table order) */
u16 ss_champ[13];                       /* (each simulated state's champion word, 0 = the played one / none) */
static u32 rng;
static u32 below(u32 n) {
    rng = rng * 1103515245u + 12345u;
    return (u32)(((unsigned long long)(rng >> 16) * n) >> 16);   /* (64-bit product: n = squared strengths) */
}

struct club { int pts, gd, gf, w; };
static struct club cl[MAXN];
static int order[MAXN], grp[MAXN], down[MAXN];
static int n, g, size;
static const u16 *strv;

static u16 rd16(const u8 *p) { return (u16)(p[0] | p[1] << 8); }

static u32 str2(int c, int home) {
    u32 s = rd16((const u8 *)&strv[c]);
    if (s < 5) s = 5;
    if (s > 4000) s = 4000;                             /* (the squares and their sums stay in 32 bits) */
    if (home) s = s * 11 / 10;
    return s * s;
}

static void match(int h, int a, int *gh, int *ga) {      /* goals by strength, draws ~ 1 in 4, home advantage */
    u32 sh = str2(h, 1), sa = str2(a, 0);
    if (below(100) < 26) { int x = below(3); *gh = x; *ga = x; return; }
    int w = below(sh + sa) < sh, gw = 1 + below(3), gl = below(gw);
    *gh = w ? gw : gl; *ga = w ? gl : gw;
}

static void play(struct club *t, int h, int a) {
    int gh, ga;
    match(h, a, &gh, &ga);
    t[h].gf += gh; t[a].gf += ga; t[h].gd += gh - ga; t[a].gd += ga - gh;
    if (gh > ga) { t[h].pts += 3; t[h].w++; }
    else if (gh < ga) { t[a].pts += 3; t[a].w++; }
    else { t[h].pts++; t[a].pts++; }
}

static int before(const struct club *t, int x, int y) {   /* points, wins, goal difference, goals, strength */
    if (t[x].pts != t[y].pts) return t[x].pts > t[y].pts;
    if (t[x].w != t[y].w) return t[x].w > t[y].w;
    if (t[x].gd != t[y].gd) return t[x].gd > t[y].gd;
    if (t[x].gf != t[y].gf) return t[x].gf > t[y].gf;
    return str2(x, 0) > str2(y, 0);
}

static void sort(const struct club *t, int *v, int m) {
    for (int i = 1; i < m; i++) {
        int x = v[i], j = i;
        while (j > 0 && before(t, x, v[j - 1])) { v[j] = v[j - 1]; j--; }
        v[j] = x;
    }
}

static int code_club(int c) {                           /* the place-th club of group X in the order, or -1 */
    int p = c / g, x = c % g;
    for (int i = 0; i < n; i++)
        if (grp[order[i]] == x && p-- == 0) return order[i];
    return -1;
}

/* a tie: legs 1 = a single match at a's, 2 = two legs (b at home first); level -> the stronger by a coin weighted
   2:1 (extra time + penalties) */
static int tie(int a, int b, int legs, u32 sb_out) {
    int ga = 0, gb = 0, x, y;
    u32 sa = str2(a, 0), sb = b >= 0 ? str2(b, 0) : sb_out;
    for (int l = 0; l < legs; l++) {
        int home_a = legs == 1 || l == 1;
        u32 h = home_a ? sa * 121 / 100 : sa, o = home_a ? sb : sb * 121 / 100;
        if (below(100) < 26) { x = below(3); y = x; }
        else { int w = below(h + o) < h, gw = 1 + below(3), gl = below(gw); x = w ? gw : gl; y = w ? gl : gw; }
        ga += x; gb += y;
    }
    if (ga != gb) return ga > gb ? a : b;
    return below(3 * (sa + sb)) < 2 * sa + sb ? a : b;
}

/* the stages after the first (statespec.ko_program), for a simulated state: the champion's club, or -1 */
static int cgrp[MAXN];
static int pcode(const int *p, int pn, int pg, int c) {   /* code over order p (groups pg, cgrp) -> club */
    int pl = c / pg, x = c % pg;
    for (int i = 0; i < pn; i++)
        if (cgrp[p[i]] == x && pl-- == 0) return p[i];
    return -1;
}
static int knockouts(const u8 **tp) {
    const u8 *t = *tp;
    int ns = *t++, champ = -1;
    int P[MAXN], pn = n, pg = g, W[MAXN], nw = 0, B[MAXN], nb = 0, rk[MAXN];
    for (int i = 0; i < n; i++) { P[i] = order[i]; cgrp[i] = grp[i]; rk[order[i]] = i; }
    for (int st = 0; st < ns; st++) {
        int kind = *t++;
        if (kind == 1) {                                /* a later group stage */
            int g2 = *t++, sz = *t++, meet = *t++, cross = *t++, q = *t++;
            int L[MAXN], ng[MAXN];
            for (int k = 0; k < q; k++) L[k] = pcode(P, pn, pg, *t++);
            struct club q2[MAXN];
            for (int i = 0; i < n; i++) q2[i].pts = q2[i].gd = q2[i].gf = q2[i].w = 0;
            for (int k = 0; k < q; k++) if (L[k] >= 0) ng[L[k]] = k / (sz ? sz : 1);
            for (int r = 0; r < meet; r++)
                for (int i = 0; i < q; i++)
                    for (int j = i + 1; j < q; j++) {
                        if (L[i] < 0 || L[j] < 0) continue;
                        int same = ng[L[i]] == ng[L[j]];
                        if (cross ? same : !same) continue;
                        if (r & 1) play(q2, L[j], L[i]); else play(q2, L[i], L[j]);
                    }
            int k2 = 0, gv[MAXN], pos[MAXN];
            for (int x = 0; x < g2; x++) {
                int m = 0;
                for (int k = 0; k < q; k++) if (L[k] >= 0 && ng[L[k]] == x) gv[m++] = L[k];
                sort(q2, gv, m);
                for (int i = 0; i < m; i++) pos[gv[i]] = i;
            }
            for (int p = 0; p < q && k2 < q; p++) {
                int at = k2;
                for (int k = 0; k < q; k++) if (L[k] >= 0 && pos[L[k]] == p) P[k2++] = L[k];
                sort(q2, P + at, k2 - at);
            }
            pn = k2; pg = g2;
            for (int k = 0; k < pn; k++) cgrp[P[k]] = ng[P[k]];
        } else {                                        /* a knockout round */
            int N = *t++, b = *t++, legs = *t++, mode = *t++, host = mode & 0x80;
            mode &= 0x7F;
            int L[MAXN];
            for (int k = 0; k < N; k++) {
                int c = *t++;
                if (mode == 0) L[k] = pcode(P, pn, pg, c);
                else if (mode == 3) L[k] = c < pn ? P[c] : -1;     /* (positions in the merit order) */
                else if (mode == 1) L[k] = c < nw ? W[c] : (c - nw < nb ? B[c - nw] : -1);
                else L[k] = c < nw ? W[c] : -1;
            }
            nw = 0;
            for (int k = 0; k + 1 < N - b; k += 2) {
                if (L[k] < 0 || L[k + 1] < 0) { W[nw++] = L[k] >= 0 ? L[k] : L[k + 1]; continue; }
                if (host) {                                 /* 'host' better: by stage-1 place (rk) */
                    int sw = legs == 1 ? rk[L[k + 1]] < rk[L[k]] : rk[L[k + 1]] > rk[L[k]];
                    if (sw) { int x = L[k]; L[k] = L[k + 1]; L[k + 1] = x; }
                }
                W[nw++] = legs == 1 ? tie(L[k], L[k + 1], 1, 0)    /* (a single match at the first listed's, */
                                    : tie(L[k + 1], L[k], legs, 0); /* two legs: the first listed at home first) */
            }
            nb = 0;
            for (int k = N - b; k < N; k++) B[nb++] = L[k];
            if (nw == 1 && !nb) champ = W[0];
        }
    }
    *tp = t;
    return champ;
}

static void block(u8 *out, u16 key, const u16 *words, int is_played) {
    (void)is_played;
    out[0] = (u8)key; out[1] = (u8)(key >> 8); out[2] = (u8)n; out[3] = 0;
    int k = 4;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < n; i++)
            if (down[order[i]] == pass) {
                u16 w = rd16((const u8 *)&words[order[i]]);
                out[k++] = (u8)w; out[k++] = (u8)(w >> 8);
            }
}

void ss_season(const u8 *SS_TAB, u8 *ST_RANK, u16 SEASONW) {
    const u8 *t = SS_TAB;
    int ns = *t++;
    u8 *out = ST_RANK;
    int have0 = rd16(ST_RANK) && ST_RANK[2];
    u16 key0 = rd16(ST_RANK);
    u8 b0[2 * MAXN];
    int n0 = have0 ? ST_RANK[2] : 0;
    for (int i = 0; i < 2 * n0 && i < 2 * MAXN; i++) b0[i] = ST_RANK[4 + i];
    rng = 0x9E3779B9u ^ ((u32)SEASONW * 2654435761u);
    for (int i = 0; i < 2 * n0; i++) rng = rng * 31 + b0[i];
    u8 *first = out + (have0 ? 0 : 0);
    u8 *next = first + (have0 ? 4 + 2 * n0 : 0);
    for (int s = 0; s < ns; s++) {
        u16 key = rd16(t);
        n = t[2]; g = t[3]; size = t[4];
        int ndown = t[5], q = t[6];
        t += 7;
        const u16 *words = (const u16 *)t; t += 2 * n;
        strv = (const u16 *)t; t += 2 * n;
        int nm = rd16(t); t += 2;
        const u8 *ms = t; t += 2 * nm;
        int played = have0 && key == key0;
        for (int i = 0; i < n; i++) {
            cl[i].pts = cl[i].gd = cl[i].gf = cl[i].w = 0;
            grp[i] = size ? i / size : 0;
            down[i] = 0;
            order[i] = i;
        }
        if (played) {                                   /* the real stage-1 order */
            int k = 0;
            for (int j = 0; j < n0 && k < n; j++) {
                u16 w = (u16)(b0[2 * j] | b0[2 * j + 1] << 8);
                for (int i = 0; i < n; i++)
                    if (rd16((const u8 *)&words[i]) == w) { order[k++] = i; break; }
            }
            if (k != n) played = 0, k = 0;             /* (a word not in the table: simulate instead) */
        }
        if (!played) {
            for (int m = 0; m < nm; m++) play(cl, ms[2 * m], ms[2 * m + 1]);
            int gv[MAXN], k = 0;                        /* each group sorted; the order place-major, merit within */
            int pos[MAXN];
            for (int x = 0; x < g; x++) {
                int m = 0;
                for (int i = 0; i < n; i++) if (grp[i] == x) gv[m++] = i;
                sort(cl, gv, m);
                for (int i = 0; i < m; i++) pos[gv[i]] = i;
            }
            for (int p = 0; p < n && k < n; p++) {
                int at = k;
                for (int i = 0; i < n; i++) if (pos[i] == p) order[k++] = i;
                sort(cl, order + at, k - at);
            }
            if (q > 0 && q < n) {                       /* as st_gend: the qualifiers (the first q, place-major) */
                sort(cl, order, q);                     /* and the others each by the whole table */
                sort(cl, order + q, n - q);
            }
        }
        if (s < 13)
            for (int i = 0; i < MAXN; i++) ss_ord[s * MAXN + i] = i < n ? (u8)order[i] : 0xFF;
        int type = *t++;                                /* the relegation phase */
        if (type == 0) {
            for (int i = n - ndown; i < n; i++) if (i >= 0) down[order[i]] = 1;
        } else if (type == 1) {
            int legs = *t++, nt = *t++;
            for (int k = 0; k < nt; k++, t += 2) {
                int a = code_club(t[0]), b = code_club(t[1]);
                if (a < 0 || b < 0) continue;
                int w = tie(a, b, legs, 0);
                down[w == a ? b : a] = 1;
            }
        } else if (type == 2) {
            int meet = *t++, m = *t++;
            int mem[MAXN];
            for (int k = 0; k < m; k++) mem[k] = code_club(*t++);
            int nb = *t++;
            const u8 *bc = t; t += nb;
            int bonus = *t++, extra = *t++;
            u32 xstr = (u32)*t++ * 4;
            struct club q[MAXN];
            for (int i = 0; i < n; i++) q[i].pts = q[i].gd = q[i].gf = q[i].w = 0;
            int best = -1;                              /* the bonus: the first of the candidates in the order */
            for (int i = 0; i < n && best < 0; i++)
                for (int k = 0; k < nb; k++)
                    if (code_club(bc[k]) == order[i]) { best = order[i]; break; }
            if (best >= 0) q[best].pts += bonus;
            for (int r = 0; r < meet; r++)
                for (int i = 0; i < m; i++)
                    for (int j = i + 1; j < m; j++)
                        if (mem[i] >= 0 && mem[j] >= 0) {
                            if (r & 1) play(q, mem[j], mem[i]); else play(q, mem[i], mem[j]);
                        }
            int v[MAXN], mv = 0;
            for (int k = 0; k < m; k++) if (mem[k] >= 0) v[mv++] = mem[k];
            sort(q, v, mv);
            for (int k = mv - ndown; k < mv; k++) if (k >= 0) down[v[k]] = 1;
            if (extra && extra <= mv && !down[v[extra - 1]] && tie(v[extra - 1], -1, 2, xstr * xstr) != v[extra - 1])
                down[v[extra - 1]] = 1;
        } else if (type == 3) {
            int pa = t[0] - 1, pb = t[1] - 1, gap = t[2];
            t += 3;
            for (int i = n - (ndown - 1); i < n; i++) if (i >= 0 && i > pb) down[order[i]] = 1;
            if (pa >= 0 && pb < n) {
                int a = order[pa], b = order[pb];
                if (!played && cl[a].pts - cl[b].pts <= gap) down[tie(a, b, 2, 0) == a ? b : a] = 1;
                else down[b] = 1;
            }
        }
        int ch = knockouts(&t);                         /* (parsed for every state; the played one's is the
                                                           game's own) */
        if (s < 13) ss_champ[s] = (!played && ch >= 0) ? rd16((const u8 *)&words[ch]) : 0;
        if (played) block(first, key, words, 1);
        else { block(next, key, words, 0); next += 4 + 2 * n; }
    }
    next[0] = next[1] = 0;
    if (!have0) { /* (block 0 empty: the simulated ones start there) */ }
}

#else

void ss_season(const unsigned char *SS_TAB, unsigned char *ST_RANK, unsigned short SEASONW)
{
    (void)SS_TAB; (void)ST_RANK; (void)SEASONW;
}

#endif /* SWOS_WORLDS */
