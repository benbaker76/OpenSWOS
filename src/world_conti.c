/*
 * world_conti.c -- the other confederations' club cups of a world career (World 2026-27):
 * CONMEBOL, CONCACAF, AFC, CAF, OFC, FIFA (include/world_conti.h; docs/WORLD_2026_27.md).
 *
 * The SWOS 96/97 Mod's tools/conti.c (MIT, Davide Lorigliola), as it is but for our types and
 * the game-structure hooks of its group stages (ct_gend / ct_gpre: the DOS game's competition
 * layout, our own glue instead): scripts/league_playoffs_check.sh builds the Mod's file and
 * this one natively and compares their editions over random seasons.
 *
 * The Mod's notice:
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
 * The Mod's own description follows.
 */
#include "world_conti.h"

#if SWOS_WORLDS

/* (the Mod's names; the header's are prefixed) */
#define NA CT_NA
#define NL CT_NL
#define NC CT_NC
#define NCUP CT_NCUP
#define NRD CT_NRD
#define NE CT_NE
#define RB CT_RB
#define OUT CT_NONE
#define CHAMP CT_CHAMP
#define NED CT_NED

/* The other confederations' club cups of a world career (World 2026-27): CONMEBOL, CONCACAF, AFC, CAF, OFC, FIFA.

   Compiled twice: for the game (tools/cobj.py: i686 freestanding, linked into an obj1 cave, called from the season
   end's asm, conti.py) and natively for the tests (tools/conti.py --test: ctypes). Every cup is data (conti.py packs
   internal/conti/<conf>.format.json + .access.json into CT_SPEC): its rounds (knockout / groups), draws, routes
   (winners, losers, group places -> another round, possibly of another cup), the access lists per association
   (league places, the cup winner, Apertura / Clausura champions, 'the next club not yet in'), the holders' places.

   ct_season(): one whole edition of every cup from the world's tables (CT_IN, filled per country at the season
   end): the entrants, then every round in calendar order (month, cup, round), draws seeded by the confederation's
   ranking, matches simulated by strength; the winners become the next edition's holders (CT_ST, in the save). The
   career club's own rounds (segments in slot 3) use the same round steps (ct_round) later. */



const u8 *CT_SPEC;
u16 *CT_LP[NCUP];                       /* each cup's contest team list in the exe (its main round, drawn order: the
                                           slot-3 chain finds the career club there at the next season start), 0 = none */
struct ct_in CT_IN;
struct ct_state CT_ST;
struct ct_out CT_OUT;

/* phase B: the edition being played, kept in the save (engine item 'CE'): its entrants (the access result), the
   career club's results so far; every step re-runs it from the start (same seed), the club's games forced, up to
   the club's next game: its segment (CT_SEG) goes into slot 3 */
struct ct_ed CT_ED;
struct ct_seg CT_SEG;
static int g_stop, g_fv;

/* --- the spec ----------------------------------------------------------------------------------------------------- */
static u8 ncup, na, fcup, frnd;         /* (test builds, CONTI_FORCE: the career club into this cup's round) */
static const u8 *cupr[NCUP];            /* first round of each cup */
static u8 nrd[NCUP];
static const u8 *acc;                   /* access entries: assoc, cup, round, code */
static u16 nacc;
static const u8 *hol;                   /* holders: src cup (0xFF: the UEFA Champions League's, the game's), dst cup,
                                           dst round | 0x80 own place | 0x40 even / 0x20 odd editions only, its slot */
static u16 g_uefa;                      /* the UEFA Champions League holder's team word (ct_go) */
static u8 nhol;
static const u8 *afile;                 /* each association's league team file */
static const u8 *rk;                    /* ranking: team word, points (u16 each) */
static const u8 *gu;                    /* guests (clubs of associations not in the world): word, assoc, -, str */
static u16 ngu;
static const u8 *areg, *s1h, *wfile, *gid;
static u16 GI[256];                     /* a guest's club index this season */
static u16 nrk;
#define RND(c, r) (cupr[c] + (r) * RB)

static void parse(void) {
    const u8 *p = CT_SPEC;
    ncup = p[0]; na = p[1]; fcup = p[2]; frnd = p[3];
    p += 4;
    for (int c = 0; c < ncup; c++) {
        nrd[c] = p[0];
        cupr[c] = p + 4;
        p += 4 + nrd[c] * RB;
    }
    nacc = p[0] | p[1] << 8;
    acc = p + 2;
    p += 2 + 4 * nacc;
    nhol = p[0];
    hol = p + 1;
    p += 1 + 4 * nhol;
    afile = p;
    p += na;
    nrk = p[0] | p[1] << 8;
    rk = p + 2;
    p += 2 + 4 * nrk;
    ngu = p[0] | p[1] << 8;             /* guest clubs: team word, association, strength (8 B each) */
    gu = p + 2;
    p += 2 + 8 * ngu;
    areg = p;                           /* each association's region (a league stage's group: ACLE West 0 / East 1) */
    p += na;
    wfile = p;                          /* the world's team files (a bitmap): the UEFA holder must be one of them */
    p += 32;
    s1h = p;                            /* each cup's 2027 edition winner (the build's run): the first holders */
    p += 8 * ncup;
    gid = p;                            /* each cup's group contest id in the exe (0: none) */
}

/* --- random, strength -------------------------------------------------------------------------------------------- */
static u32 rng;
static u32 below(u32 n) { rng = rng * 1103515245u + 12345u; return n ? ((rng >> 16) * n) >> 16 : 0; }
static u32 sq(int c) { u32 s = CT_IN.club[c - 1].str / 16; if (s < 1) s = 1; if (s > 4000) s = 4000; return s * s; }
static u32 pts(int c) { return CT_IN.club[c - 1].pts ? CT_IN.club[c - 1].pts : CT_IN.club[c - 1].str / 64; }
static int assoc(int c) { return CT_IN.club[c - 1].assoc; }

/* goals of one side: binomial(8, m / 8), m in eighths */
static int goals(u32 m8) { int g = 0; for (int i = 0; i < 8; i++) if (below(64) < m8) g++; return g; }
/* a match: home h v away a -> goals; mean 0.5 .. 2.5 by the strengths' square ratio, a home bonus */
static void match(int h, int a, int neutral, int *gh, int *ga) {
    u32 sh = sq(h), sa = sq(a);
    u32 r = (sh * 64) / (sh + sa);              /* 0..64 (the Mod's in 64 bits: sq() caps a side at
                                                   4000^2, so sh * 64 fits 32 -- the Amiga links no 64-bit division) */
    u32 mh = 4 + r / 2 + (neutral ? 0 : 2), ma = 4 + (64 - r) / 2;         /* eighths of a goal: 4..36 */
    *gh = goals(mh > 63 ? 63 : mh);
    *ga = goals(ma > 63 ? 63 : ma);
}
/* a tie a v b (b at home in the second leg when legs = 2): 1 if a goes through. level: 0 penalties, 1 extra time +
   penalties, 2 away goals then penalties (CAF), 3 away goals, then extra time (no away goals in it), then penalties
   (CONCACAF, 4.0.2) */
static int tie(int a, int b, int legs, int level, int neutral) {
    int x, y, ga = 0, gb = 0, awa = 0, awb = 0;
    if (legs == 2) {
        match(a, b, 0, &x, &y); ga += x; gb += y; awb = y;
        match(b, a, 0, &x, &y); gb += x; ga += y; awa = y;
    } else {
        match(a, b, neutral, &x, &y); ga += x; gb += y;
    }
    if (ga != gb) return ga > gb;
    if ((level == 2 || level == 3) && legs == 2 && awa != awb) return awa > awb;
    if (level == 1 || level == 3) {     /* extra time */
        match(a, b, 1, &x, &y);
        x /= 3; y /= 3;
        if (x != y) return x > y;
    }
    return below(100) < 50;             /* penalties */
}

/* --- the edition ------------------------------------------------------------------------------------------------- */
static u16 E[NCUP][NRD][NE];            /* a round's entrants (club + 1), before its draw */
static u8 EV[NCUP][NRD][NE];            /* 1 = came through an earlier round ('via': the association exception) */
static u8 EK[NCUP][NRD][NE];            /* side for D_CROSS (0 = this cup's, 1 = another's) and its rank key */
static u8 EN[NCUP][NRD];
static u8 taken[NC + 1];                /* the access pass: the club already has a place (this edition) */
static u8 own[NC + 1];                  /* a holder whose league place in that cup (+1) is its own (CAF) */

static void enter(int c, int r, int club, int via, int side) {
    if (!club || r >= nrd[c] || EN[c][r] >= NE) return;
    for (int i = 0; i < EN[c][r]; i++) if (E[c][r][i] == club) return;
    int i = EN[c][r]++;
    E[c][r][i] = club; EV[c][r][i] = via; EK[c][r][i] = side;
}
/* a route byte: cup << 4 | round, OUT, CHAMP */
static void route(int from_cup, u8 rt, int club, int side_rank) {
    if (rt == OUT || rt == CHAMP || !club) return;
    int c = rt >> 4, r = rt & 15;
    enter(c, r, club, 1, (c != from_cup ? 0x80 : 0) | (side_rank & 0x7F));
}

static int in_cup(int c, int a);
static int next_club(int a) {          /* the best league club of association a not yet in */
    for (int l = 0; l < NL; l++) {
        int x = CT_IN.league[a][l];
        if (x && !taken[x]) return x;
    }
    return 0;
}
static void access(void) {
    for (int c = 0; c <= NC; c++) taken[c] = own[c] = 0;
    for (int g = 0; g < ngu && g < 256; g++) {       /* the guests: clubs of their own every season (no table) */
        const u8 *e = gu + 8 * g;
        GI[g] = 0;
        if (CT_IN.nclub >= NC) continue;
        int k = ++CT_IN.nclub;
        CT_IN.club[k - 1].id = e[0] | e[1] << 8; CT_IN.club[k - 1].assoc = e[2];
        CT_IN.club[k - 1].str = e[4] | e[5] << 8 | e[6] << 16 | e[7] << 24; CT_IN.club[k - 1].pts = 0;
        GI[g] = k;
    }
    u8 vac[NCUP * 2]; int nvac = 0;     /* holder places with no holder yet (season 1): filled after the access */
    for (int h = 0; h < nhol; h++) {    /* the holders first: their own places */
        const u8 *e = hol + 4 * h;
        int s = e[0], odd = (2028 + CT_ST.season) & 1;   /* (the edition's year: built at the season end) */
        if ((e[2] & 0x40) && odd) continue;
        if ((e[2] & 0x20) && !odd) continue;
        u16 hid = s == 0xFF ? g_uefa : CT_ST.hold[s].id;
        if (s == 0xFF && hid && hid != 0xFFFF && !(wfile[(hid & 0xFF) >> 3] & 1 << (hid & 7)))
            hid = 0;                    /* (a UEFA holder of no world file: the game's own, not a club of this world) */
        if (!hid || hid == 0xFFFF) { if (s != 0xFF && nvac < NCUP * 2) vac[nvac++] = h; continue; }
        int k = 0;                      /* the holder's club index (seen this season, else appended) */
        for (int i = 0; i < CT_IN.nclub; i++) if (CT_IN.club[i].id == hid) { k = i + 1; break; }
        if (!k && CT_IN.nclub < NC) {
            k = ++CT_IN.nclub;
            CT_IN.club[k - 1].id = hid;
            if (s == 0xFF) {            /* (the UEFA holder: no record here, as strong as the strongest club seen) */
                u32 m = 0;
                for (int i = 0; i + 1 < k; i++) if (CT_IN.club[i].str > m) m = CT_IN.club[i].str;
                CT_IN.club[k - 1].assoc = 0xFD; CT_IN.club[k - 1].str = m; CT_IN.club[k - 1].pts = 0;
            } else {
                CT_IN.club[k - 1].assoc = CT_ST.hold[s].assoc;
                CT_IN.club[k - 1].str = CT_ST.hold[s].str; CT_IN.club[k - 1].pts = CT_ST.hold[s].pts;
            }
        }
        if (k) { enter(e[1], e[2] & 0x1F, k, 0, e[3]); taken[k] = 1; if (e[2] & 0x80) own[k] = e[1] + 1; }
        /* (a holder may hold places in two cups: the Recopa; own[]: CAF, its league place in the cup is its own) */
    }
    if (fcup < ncup && CT_IN.manager && !taken[CT_IN.manager]) {   /* (test builds: the forced place) */
        enter(fcup, frnd, CT_IN.manager, 0, 0);
        taken[CT_IN.manager] = 1;
    }
    for (int i = 0; i < nacc; i++) {
        const u8 *e = acc + 4 * i;
        int a = e[0], c = e[1], r = e[2], k = e[3], club = 0;
        if (a == 0xFE) {                /* a guest's place (its real access, data's list): k = the guest */
            club = k < ngu ? GI[k] : 0;
            if (club) { enter(c, r, club, 0, 0); taken[club] = 1; }   /* (a listed club may hold two places:
                                                                            season 1's stand-in holders, the Recopa) */
            continue;
        }
        if (k == 0) club = CT_IN.league[a][0];
        else if (k == 1) club = CT_IN.cup[a];
        else if (k == C_AP) club = CT_IN.ap[a];
        else if (k == C_CL) club = CT_IN.cl[a];
        else if (k >= 2 && k <= NL) club = CT_IN.league[a][k - 1];
        if (club && taken[club] && own[club] == c + 1) { own[club] = 0; continue; }   /* (the holder's own place) */
        if (club && taken[club]) club = 0;          /* already in: the place passes down the table */
        if (!club) club = next_club(a);
        if (club) { enter(c, r, club, 0, 0); taken[club] = 1; }
    }
    for (int v = 0; v < nvac; v++) {    /* a vacant holder place: the best ranked club not yet in (declared) */
        const u8 *e = hol + 4 * vac[v];
        if (e[0] == 0xFF || (e[2] & 0x60)) continue;   /* (the Intercontinental: no stand-ins) */
        if (e[1] != e[0] && RND(e[1], e[2] & 0x1F)[0] == K_KO && nrd[e[1]] == 1) continue;   /* (a holders' match: none) */
        if (e[2] & 0x80) continue;      /* (CAF: no extra place for a holder) */
        int best = 0;
        for (int i = 1; i <= CT_IN.nclub; i++)
            if (!taken[i] && in_cup(e[1], assoc(i)) && (!best || pts(i) > pts(best))) best = i;
        if (best) { enter(e[1], e[2] & 0x1F, best, 0, 0); taken[best] = 1; }
    }
}

/* --- draws -------------------------------------------------------------------------------------------------------- */
static u16 D[NE];                       /* the drawn order */
static u8 DV[NE], DK[NE];
static void sort_by_pts(u16 *x, u8 *v, u8 *k, int n) {
    for (int i = 1; i < n; i++) {
        u16 cx = x[i]; u8 cv = v[i], ck = k[i];
        int j = i;
        while (j > 0 && pts(cx) > pts(x[j - 1])) { x[j] = x[j - 1]; v[j] = v[j - 1]; k[j] = k[j - 1]; j--; }
        x[j] = cx; v[j] = cv; k[j] = ck;
    }
}
static void shuffle(u16 *x, u8 *v, u8 *k, int n) {
    for (int i = n - 1; i > 0; i--) {
        int j = below(i + 1);
        u16 t = x[i]; x[i] = x[j]; x[j] = t;
        u8 tv = v[i]; v[i] = v[j]; v[j] = tv;
        u8 tk = k[i]; k[i] = k[j]; k[j] = tk;
    }
}
static int conflict(int a, int b, int va, int vb, int rule) {
    if (rule == A_ANY) return 0;
    if (assoc(a) != assoc(b)) return 0;
    if (rule == A_NOVIA && (va || vb)) return 0;
    return 1;
}
/* a knockout round's pairs into D (D[2i] v D[2i+1]: the second at home in the second leg) */
static void draw_ko(int c, int r, const u8 *R) {
    int n = EN[c][r], dr = R[4], rule = R[5];
    u16 x[NE]; u8 v[NE], k[NE];
    for (int i = 0; i < n; i++) { x[i] = E[c][r][i]; v[i] = EV[c][r][i]; k[i] = EK[c][r][i]; }
    if (dr == D_PLACE) {                /* (ACLE's round of 16: in each region 1 v 8, 2 v 7, 3 v 6, 4 v 5; the better
                                           placed at home in the second leg; the group (region) in the via byte) */
        int m = 0;
        for (int g = 1; g < 32 && m < n; g++) {
            int cnt = 0;
            for (int i = 0; i < n; i++) if ((v[i] >> 1) == g) cnt++;
            for (int p = 1; p <= cnt / 2; p++) {
                int hi = -1, lo = -1;
                for (int i = 0; i < n; i++) if ((v[i] >> 1) == g) {
                    if ((k[i] & 0x7F) == p) hi = i;
                    if ((k[i] & 0x7F) == cnt + 1 - p) lo = i;
                }
                if (hi < 0 || lo < 0) continue;
                D[m] = x[lo]; DV[m] = v[lo]; DK[m] = k[lo]; m++;
                D[m] = x[hi]; DV[m] = v[hi]; DK[m] = k[hi]; m++;
            }
        }
        if (m == n) return;
    }
    if (dr == D_SLOT) {                 /* (the Intercontinental's stepladder: each club in its slot, the side key) */
        for (int i = 1; i < n; i++) { u16 t = x[i]; u8 tk = k[i], tv = v[i]; int j = i;
            while (j > 0 && (k[j - 1] & 0x7F) > (tk & 0x7F)) { x[j] = x[j - 1]; k[j] = k[j - 1]; v[j] = v[j - 1]; j--; }
            x[j] = t; k[j] = tk; v[j] = tv; }
        for (int i = 0; i < n; i++) { D[i] = x[i]; DV[i] = v[i]; DK[i] = k[i]; }
        return;
    }
    if (dr == D_ORDER || dr == D_BRACKET) {
        for (int i = 0; i < n; i++) { D[i] = x[i]; DV[i] = v[i]; DK[i] = k[i]; }
        return;
    }
    if (dr == D_SAMEPAIRS) {            /* clubs of one association against each other, the first drawn at home */
        int m = 0;
        u8 used[NE] = {0};
        shuffle(x, v, k, n);
        for (int i = 0; i < n; i++) {
            if (used[i]) continue;
            used[i] = 1;
            for (int j = i + 1; j < n; j++)
                if (!used[j] && assoc(x[j]) == assoc(x[i])) {
                    used[j] = 1;
                    D[m] = x[j]; DV[m] = v[j]; DK[m] = k[j]; m++;  /* (the first drawn plays at home: listed second) */
                    D[m] = x[i]; DV[m] = v[i]; DK[m] = k[i]; m++;
                    goto next;
                }
            D[m] = x[i]; DV[m] = v[i]; DK[m] = k[i]; m++;           /* (no partner: a bye-like odd one) */
        next:;
        }
        return;
    }
    if (dr == D_CROSS) {                /* fixed: this cup's side ranked best..worst v the other's worst..best */
        u16 a[NE], b[NE]; u8 av[NE], bv[NE], ak[NE], bk[NE];
        int na_ = 0, nb = 0;
        for (int i = 0; i < n; i++)
            if (k[i] & 0x80) { b[nb] = x[i]; bv[nb] = v[i]; bk[nb] = k[i]; nb++; }
            else { a[na_] = x[i]; av[na_] = v[i]; ak[na_] = k[i]; na_++; }
        for (int i = 1; i < na_; i++) { u16 t = a[i]; u8 tk = ak[i], tv = av[i]; int j = i;
            while (j > 0 && (ak[j - 1] & 0x7F) > (tk & 0x7F)) { a[j] = a[j - 1]; ak[j] = ak[j - 1]; av[j] = av[j - 1]; j--; }
            a[j] = t; ak[j] = tk; av[j] = tv; }
        for (int i = 1; i < nb; i++) { u16 t = b[i]; u8 tk = bk[i], tv = bv[i]; int j = i;
            while (j > 0 && (bk[j - 1] & 0x7F) > (tk & 0x7F)) { b[j] = b[j - 1]; bk[j] = bk[j - 1]; bv[j] = bv[j - 1]; j--; }
            b[j] = t; bk[j] = tk; bv[j] = tv; }
        int m = 0;
        if (!nb) {                      /* one cup's group places (OFC: A1 v B2, B1 v A2): a 1st v the next group's 2nd */
            int ng = na_ / 2;
            for (int i = 0; i < na_; i++) {
                if ((ak[i] & 0x7F) > ng) continue;
                int g = (av[i] >> 1) - 1;
                for (int j = 0; j < na_; j++)
                    if ((ak[j] & 0x7F) > ng && (av[j] >> 1) - 1 == (g + 1) % ng) {
                        D[m] = a[j]; DV[m] = av[j]; DK[m] = ak[j]; m++;
                        D[m] = a[i]; DV[m] = av[i]; DK[m] = ak[i]; m++;
                    }
            }
            return;
        }
        for (int i = 0; i < na_ && i < nb; i++) {   /* the other cup's club at home first: listed first */
            D[m] = b[nb - 1 - i]; DV[m] = bv[nb - 1 - i]; DK[m] = bk[nb - 1 - i]; m++;
            D[m] = a[i]; DV[m] = av[i]; DK[m] = ak[i]; m++;
        }
        return;
    }
    /* D_POTS2 / D_SEED12: the seeded half (D_SEED12: this cup's group winners, rank keys 1..; else by ranking)
       against the other half, drawn; the seeded club at home in the second leg (listed second) */
    if (dr == D_SEED12) {
        for (int i = 1; i < n; i++) { u16 t = x[i]; u8 tk = k[i], tv = v[i]; int j = i;
            while (j > 0 && (k[j - 1] & 0x7F) > (tk & 0x7F)) { x[j] = x[j - 1]; k[j] = k[j - 1]; v[j] = v[j - 1]; j--; }
            x[j] = t; k[j] = tk; v[j] = tv; }
    } else
        sort_by_pts(x, v, k, n);
    int h = n / 2;
    for (int tries = 0; tries < 50; tries++) {
        u16 lo[NE]; u8 lv[NE], lk[NE];
        for (int i = 0; i < n - h; i++) { lo[i] = x[h + i]; lv[i] = v[h + i]; lk[i] = k[h + i]; }
        shuffle(lo, lv, lk, n - h);
        int ok = 1;
        for (int i = 0; i < h; i++) if (conflict(x[i], lo[i], v[i], lv[i], rule)) ok = 0;
        if (ok || tries == 49) {
            for (int i = 0; i < h; i++) {
                D[2 * i] = lo[i]; DV[2 * i] = lv[i]; DK[2 * i] = lk[i];
                D[2 * i + 1] = x[i]; DV[2 * i + 1] = v[i]; DK[2 * i + 1] = k[i];
            }
            return;
        }
    }
}
/* groups into D: group g = D[g * size ..]; pots of ngroups clubs by ranking (via clubs last), the holder A1 */
static void reached(int club, int c, int r);
static int in_cup(int c, int a) {       /* an association with a place in the cup (its confederation's) */
    for (int i = 0; i < nacc; i++) {
        const u8 *e = acc + 4 * i;
        if (e[1] != c) continue;
        if (e[0] == a) return 1;
        if (e[0] == 0xFE && e[3] < ngu && gu[8 * e[3] + 2] == a) return 1;
    }
    return 0;
}
static void draw_groups(int c, int r, const u8 *R) {
    int n = EN[c][r], ng = R[6], gs = R[7], rule = R[5];
    u16 x[NE]; u8 v[NE], k[NE];
    int m = 0;
    for (int pass = 0; pass < 3; pass++)        /* holders (rank key 0x7F, set by the caller: not used), direct, via */
        for (int i = 0; i < n; i++)
            if ((pass == 1 && !EV[c][r][i]) || (pass == 2 && EV[c][r][i])) {
                x[m] = E[c][r][i]; v[m] = EV[c][r][i]; k[m] = EK[c][r][i]; m++;
            }
    int size = ng * gs;
    while (m > size) {                  /* more clubs than places (a forced test place): the weakest out */
        int w = -1;
        for (int i = 0; i < m; i++)
            if (x[i] != CT_IN.manager && (w < 0 || pts(x[i]) < pts(x[w]))) w = i;
        if (w < 0) break;
        for (int i = w; i + 1 < m; i++) { x[i] = x[i + 1]; v[i] = v[i + 1]; k[i] = k[i + 1]; }
        m--;
    }
    while (m < size && !R[23]) {        /* fewer (the world could not fill the rounds before): the best ranked
                                           club in no cup this edition (declared), so the game's contest is full */
        int b = 0;
        for (int i = 1; i <= CT_IN.nclub; i++)
            if (!taken[i] && in_cup(c, assoc(i)) && (!b || pts(i) > pts(b))) b = i;
        if (!b) break;
        taken[b] = 1;
        reached(b, c, r);
        x[m] = b; v[m] = 0; k[m] = 0; m++;
    }
    if (R[19] && areg) {                /* a league stage by region: each club in its association's region */
        sort_by_pts(x, v, k, m);
        int fill[16] = {0};
        for (int i = 0; i < NE; i++) D[i] = 0;
        for (int pass = 0; pass < 2; pass++)
            for (int i = 0; i < m; i++) {
                if (!x[i]) continue;
                int a = assoc(x[i]), g = a < na ? areg[a] : 0xFF;
                if (pass == 0 && (g >= ng || fill[g] >= gs)) continue;
                if (pass == 1) { g = 0; while (g < ng && fill[g] >= gs) g++; if (g >= ng) break; }
                D[g * gs + fill[g]] = x[i]; DV[g * gs + fill[g]] = v[i]; DK[g * gs + fill[g]] = k[i]; fill[g]++;
                x[i] = 0;
            }
        return;
    }
    int nd = 0;
    while (nd < m && !v[nd]) nd++;
    sort_by_pts(x, v, k, nd);
    int hold = R[16] && CT_ST.hold[c].id;
    if (hold)                           /* the holder first (pot 1, group A) */
        for (int i = 0; i < nd; i++)
            if (CT_IN.club[x[i] - 1].id == CT_ST.hold[c].id) {
                u16 t = x[i]; u8 tv = v[i], tk = k[i];
                for (int j = i; j > 0; j--) { x[j] = x[j - 1]; v[j] = v[j - 1]; k[j] = k[j - 1]; }
                x[0] = t; v[0] = tv; k[0] = tk;
                break;
            }
    for (int i = 0; i < NE; i++) D[i] = 0;
    for (int p = 0; p < gs; p++) {
        int base = p * ng;
        u16 px[NE]; u8 pv[NE], pk[NE];
        int pn = 0;
        for (int i = base; i < base + ng && i < m; i++) { px[pn] = x[i]; pv[pn] = v[i]; pk[pn] = k[i]; pn++; }
        if (p == 0 && hold) {
            D[0] = px[0]; DV[0] = pv[0]; DK[0] = pk[0];
            for (int i = 1; i < pn; i++) { px[i - 1] = px[i]; pv[i - 1] = pv[i]; pk[i - 1] = pk[i]; }
            pn--;
        } else
            ;
        shuffle(px, pv, pk, pn);
        for (int i = 0; i < pn; i++) {
            int best = -1, g0 = below(ng);
            for (int t = 0; t < ng; t++) {     /* (every group once, from a drawn one) */
                int g = (t + g0) % ng, s = g * gs + p;
                if (D[s]) continue;
                int bad = 0;
                for (int q = 0; q < p; q++)
                    if (D[g * gs + q] && conflict(D[g * gs + q], px[i], DV[g * gs + q], pv[i], rule)) bad = 1;
                if (!bad) { best = s; break; }
                if (best < 0) best = -2 - s;            /* (a slot with a conflict: the last resort) */
            }
            if (best < -1) best = -2 - best;
            if (best < 0)                               /* (every free slot taken by the loop's order: any free) */
                for (int g = 0; g < ng; g++) if (!D[g * gs + p]) { best = g * gs + p; break; }
            if (best >= 0) { D[best] = px[i]; DV[best] = pv[i]; DK[best] = pk[i]; }
        }
    }
}

/* --- rounds ------------------------------------------------------------------------------------------------------- */
static void reached(int club, int c, int r) { CT_OUT.last[club - 1] = c << 4 | r; }

/* a group: double / single round robin of D[base .. base + gs) -> places into P */
static void group(int base, int gs, int grounds, int *P) {
    int pt[16] = {0}, gd[16] = {0}, gf[16] = {0};
    for (int leg = 0; leg < grounds; leg++)
        for (int i = 0; i < gs; i++)
            for (int j = i + 1; j < gs; j++) {
                int h = leg ? j : i, a = leg ? i : j, x, y;
                if (!D[base + h] || !D[base + a]) continue;
                match(D[base + h], D[base + a], 0, &x, &y);
                gf[h] += x; gf[a] += y; gd[h] += x - y; gd[a] += y - x;
                if (x > y) pt[h] += 3; else if (x < y) pt[a] += 3; else { pt[h]++; pt[a]++; }
            }
    for (int i = 0; i < gs; i++) P[i] = i;
    for (int i = 1; i < gs; i++) {
        int t = P[i], j = i;
        while (j > 0) {
            int u = P[j - 1];
            s32 d = pt[t] != pt[u] ? pt[t] - pt[u] : gd[t] != gd[u] ? gd[t] - gd[u] : gf[t] != gf[u] ? gf[t] - gf[u]
                    : (s32)(sq(D[base + t]) > sq(D[base + u])) - (s32)(sq(D[base + t]) < sq(D[base + u]));
            if (d <= 0) break;
            P[j] = u; j--;
        }
        P[j] = t;
    }
    for (int i = 0; i < gs; i++) gd[i] = gd[i] * 100 + gf[i] + pt[i] * 10000;   /* (a record key for the ranking) */
    for (int i = 0; i < gs; i++) P[16 + i] = gd[P[i]];
}

static int lp_size(int c);
static int forced(int c, int r) {
    for (int i = 0; i < CT_ED.nf; i++) if (CT_ED.f[i].c == c && CT_ED.f[i].r == r) return CT_ED.f[i].v;
    return -1;
}
static int ct_round(int c, int r) {
    const u8 *R = RND(c, r);
    int n = EN[c][r];
    if (!n) return 0;
    if (R[0] == K_KO && R[18] && 2 * n <= R[18]) {     /* (no more than the winners' places: all go through) */
        for (int i = 0; i < n; i++) { reached(E[c][r][i], c, r); route(c, R[10], E[c][r][i], EK[c][r][i] & 0x7F ? EK[c][r][i] & 0x7F : 0x7F); }
        if (R[10] == CHAMP && n == 1) {             /* (a final with one club: the Intercontinental with no UEFA holder) */
            CT_OUT.winner[c] = E[c][r][0]; CT_OUT.out_of[E[c][r][0] - 1] = 1;
        }
        CT_OUT.nent[c][r] = n;
        return 0;
    }
    if (R[0] == K_KO && R[18] && n < R[18]) {
        /* fewer clubs than the round's places (the 'outside' places the world's associations could not fill): the
           best ranked go through without playing, so the next round still gets its size (declared) */
        sort_by_pts(E[c][r], EV[c][r], EK[c][r], n);
        int b = R[18] - n;
        for (int i = 0; i < b; i++) { reached(E[c][r][i], c, r); route(c, R[10], E[c][r][i], EK[c][r][i] & 0x7F ? EK[c][r][i] & 0x7F : 0x7F); }
        for (int i = b; i < n; i++) { E[c][r][i - b] = E[c][r][i]; EV[c][r][i - b] = EV[c][r][i]; EK[c][r][i - b] = EK[c][r][i]; }
        EN[c][r] = n -= b;
    }
    while (R[0] == K_KO && R[18] && n > R[18]) {   /* more clubs than places (a forced test place in a holders' final):
                                                       the weakest out, never the career club */
        int w = -1;
        for (int i = 0; i < n; i++)
            if (E[c][r][i] != CT_IN.manager && (w < 0 || pts(E[c][r][i]) < pts(E[c][r][w]))) w = i;
        if (w < 0) break;
        for (int i = w; i + 1 < n; i++) { E[c][r][i] = E[c][r][i + 1]; EV[c][r][i] = EV[c][r][i + 1]; EK[c][r][i] = EK[c][r][i + 1]; }
        EN[c][r] = --n;
    }
    if (R[0] == K_GROUPS) { draw_groups(c, r, R); n = R[6] * R[7]; } else draw_ko(c, r, R);
    CT_OUT.nent[c][r] = n;
    for (int i = 0; i < n && i < NE; i++) { CT_OUT.ent[c][r][i] = D[i]; if (D[i]) reached(D[i], c, r); }
    if (R[17] && CT_LP[c]) {            /* the main round: the game's contest plays it with the career club */
        int m = R[0] == K_GROUPS ? R[6] * R[7] : n, ms = lp_size(c);
        if (m > ms) m = ms;
        for (int i = 0; i < m && i < 32; i++) CT_LP[c][i] = D[i] && i < n + (m - n) ? CT_IN.club[D[i] - 1].id : 0xFFFF;
    }
    int pm = -1;                        /* the career club in this round: its result forced, else its next game */
    g_fv = -1;
    if (CT_IN.manager)
        for (int i = 0; i < n; i++) if (D[i] == CT_IN.manager) pm = i;
    if (pm >= 0) {
        g_fv = forced(c, r);
        if (g_fv < 0 && g_stop && (R[17] || R[0] == K_KO)) {   /* (a group stage before the main round: simulated) */
            CT_SEG.cup = c; CT_SEG.round = r; CT_SEG.kind = R[0] | (R[17] ? 0x80 : 0);
            CT_SEG.legs = R[1] == 2 ? (R[2] == 1 || R[2] == 3 ? 0x94 : 0x84) : 0x14;   /* (the game's round byte) */
            CT_SEG.level = R[2];
            CT_SEG.month = (u8)((R[9] - 1 + (R[9] < 8 ? 12 : 0)) * 8);   /* (x 8 from January, as a contest's) */
            if (R[0] == K_KO) {
                int a = pm & ~1;
                CT_SEG.n = 2; CT_SEG.pos = pm & 1;
                CT_SEG.club[0] = CT_IN.club[D[a] - 1].id; CT_SEG.club[1] = CT_IN.club[D[a + 1] - 1].id;
            } else {
                CT_SEG.n = n < 32 ? n : 32; CT_SEG.pos = pm;
                for (int i = 0; i < CT_SEG.n; i++) CT_SEG.club[i] = D[i] ? CT_IN.club[D[i] - 1].id : 0xFFFF;
            }
            return 1;
        }
    }
    if (R[0] == K_GROUPS) {
        int ng = R[6], gs = R[7];
        /* every group, then each place's clubs ranked across the groups by record (the rank keys of the next draw) */
        static u16 byplace[16][16];
        static s32 key[16][16];
        for (int g = 0; g < ng; g++) {
            int P[32];
            group(g * gs, gs, R[8], P);
            if (pm >= 0 && g_fv > 0 && g_fv <= gs && pm / gs == g) {   /* (the career club's place, as played) */
                int loc = pm - g * gs, q0 = 0;
                while (q0 < gs && P[q0] != loc) q0++;
                int t = P[q0]; P[q0] = P[g_fv - 1]; P[g_fv - 1] = t;
            }
            for (int q = 0; q < gs; q++) {
                byplace[q][g] = D[g * gs + P[q]];
                key[q][g] = P[16 + q];
                CT_OUT.res[c][r][g * gs + P[q]] = q + 1;
            }
        }
        for (int q = 0; q < gs; q++) {
            u8 to = R[19] ? (q < R[19] ? R[20] : OUT) : (q < 4 ? R[12 + q] : OUT);   /* (a league stage: its top n) */
            if (to == OUT) continue;
            int ord[16];
            for (int g = 0; g < ng; g++) ord[g] = g;
            for (int i = 1; i < ng; i++) { int t = ord[i], j = i;
                while (j > 0 && key[q][ord[j - 1]] < key[q][t]) { ord[j] = ord[j - 1]; j--; }
                ord[j] = t; }
            for (int i = 0; i < ng; i++) {
                int club = byplace[q][ord[i]];
                if (R[21] == q + 1 && i >= R[22]) continue;   /* (only the best of this place: ACGL's 2nds) */
                route(c, to, club, R[19] ? q + 1 : q * ng + i + 1);   /* (a league stage: the key = the place) */
                if (EN[to >> 4][to & 15] && to < CHAMP)   /* (the group in the via byte: fixed ties) */
                    EV[to >> 4][to & 15][EN[to >> 4][to & 15] - 1] = 1 | (ord[i] + 1) << 1;
            }
        }
        return 0;
    }
    /* knockout: D[2i] v D[2i+1] */
    for (int i = 0; i + 1 < n; i += 2) {
        int a = D[i], b = D[i + 1];
        int w = tie(a, b, R[1], R[2], R[3]);
        if (g_fv >= 0 && (i == pm || i + 1 == pm)) w = (i == pm) ? g_fv : !g_fv;   /* (the career club's tie) */
        CT_OUT.res[c][r][i] = w;
        int win = w ? a : b, los = w ? b : a;
        if (R[10] == CHAMP) {
            CT_OUT.winner[c] = win; CT_OUT.second[c] = los;
            CT_OUT.out_of[win - 1] = 1;
        } else
            route(c, R[10], win, (DK[w ? i : i + 1] & 0x7F) ? DK[w ? i : i + 1] & 0x7F : i / 2 + 1);
        route(c, R[11], los, i / 2 + 1);
    }
    if (n & 1) route(c, R[10], D[n - 1], 0x7F);         /* (an odd one goes through) */
    return 0;
}

/* --- the season end's input (the game's records) ------------------------------------------------------------------ */
void ct_season(void);
#define REC 0x2AC                       /* a club record: team word at 0, the 16 players from 76, 38 bytes each */
static u16 TAB[NL];                     /* the last top division's final order (ct_table) */
static u8 ntab;
static u32 rstr(const u8 *rec) { u32 s = 0; for (int i = 0; i < 16; i++) s += rec[76 + 38 * i + 32]; return s; }
static u32 rpts(u16 id) {
    for (int i = 0; i < nrk; i++) if ((rk[4 * i] | rk[4 * i + 1] << 8) == id) return rk[4 * i + 2] | rk[4 * i + 3] << 8;
    return 0;
}
static int add_club(u16 id, int a, u32 str) {
    for (int i = 0; i < CT_IN.nclub; i++) if (CT_IN.club[i].id == id) return i + 1;
    if (CT_IN.nclub >= NC) return 0;
    int i = CT_IN.nclub++;
    CT_IN.club[i].id = id; CT_IN.club[i].assoc = a; CT_IN.club[i].str = str; CT_IN.club[i].pts = rpts(id);
    return i + 1;
}
/* cseg_927A2's input (DIY: +31h clubs, +6Dh rows in place order (offsets, 18 bytes each), +12Dh team words) */
void ct_table(const u8 *diy) {
    int n = diy[0x31] | diy[0x32] << 8;
    if (n > NL) n = NL;
    for (int i = 0; i < n; i++) {
        int row = (diy[0x6D + 2 * i] | diy[0x6E + 2 * i] << 8) / 18;
        TAB[i] = diy[0x12D + 2 * row] | diy[0x12E + 2 * row] << 8;
    }
    ntab = n;
}
/* (OpenSWOS: ct_table's input as team words, the Mod's file | ordinal << 8, in place order --
   our competition layout is not the DOS game's) */
void ct_table_words(const u16 *w, int n) {
    if (n > NL) n = NL;
    for (int i = 0; i < n; i++) TAB[i] = w[i];
    ntab = n;
}
void ct_finish(void);
void ct_begin(void) {                   /* the season end's first country: this season's edition finished first */
    ct_finish();
    u8 *b = (u8 *)&CT_IN;
    for (u32 i = 0; i < sizeof(CT_IN); i++) b[i] = 0;
    ntab = 0;
}
/* one country of the season end (after its season: the top 9 records in stand, the cup winner's record) */
void ct_country(const u8 *stand, const u8 *cuprec) {
    if (!CT_SPEC) return;
    parse();
    u16 champ = stand[0] | stand[1] << 8;
    if (!champ || champ == 0xFFFF) return;
    int a = -1;
    for (int i = 0; i < na; i++) if (afile[i] == (champ & 0xFF)) a = i;
    if (a < 0) return;
    u32 last = 0;
    int n = ntab;
    if (!n || TAB[0] != champ) {        /* (no table of this division seen: the top 9 records alone) */
        n = 0;
        for (int k = 0; k < 9; k++) { u16 w = stand[k * REC] | stand[k * REC + 1] << 8; if (!w || w == 0xFFFF) break; TAB[n++] = w; }
    }
    for (int k = 0; k < n && k < NL; k++) {
        u32 s = last * 15 / 16;         /* (below the 9th: no record here, a little weaker than the one above) */
        if (k < 9 && (stand[k * REC] | stand[k * REC + 1] << 8) == TAB[k]) s = rstr(stand + k * REC);
        last = s;
        CT_IN.league[a][k] = add_club(TAB[k], a, s);
    }
    u16 cw = cuprec[0] | cuprec[1] << 8;
    if (cw && cw != 0xFFFF && (cw & 0xFF) == (champ & 0xFF))
        CT_IN.cup[a] = add_club(cw, a, rstr(cuprec));
    ntab = 0;
}
/* the season end, every country seen: the editions (seed, the career club's team word) */
void ct_go(u32 manager, u32 seed, u32 uefa) {
    g_uefa = (u16)uefa;
    if (!CT_SPEC) return;
    CT_IN.seed = seed ^ (CT_ST.season * 7919u);
    CT_IN.manager = 0;
    for (int i = 0; i < CT_IN.nclub; i++) if (CT_IN.club[i].id == (u16)manager) CT_IN.manager = i + 1;
    if (!CT_IN.manager && manager && manager != 0xFFFF) {   /* (its country's table: the top 9 records only, the
                                                               career club below them: as its association's weakest) */
        parse();
        for (int a = 0; a < na; a++)
            if (afile[a] == (manager & 0xFF)) {
                u32 s = 0;
                int k = 0;
                while (k < NL && CT_IN.league[a][k]) k++;
                if (k) s = CT_IN.club[CT_IN.league[a][k - 1] - 1].str * 15 / 16;
                CT_IN.manager = add_club((u16)manager, a, s);
                if (CT_IN.manager && k < NL) CT_IN.league[a][k] = CT_IN.manager;
                break;
            }
    }
    ct_season();
}

/* --- the season --------------------------------------------------------------------------------------------------- */
static int lp_size(int c) {             /* the contest list's places: its main round's (a group stage, else its field) */
    for (int r = 0; r < nrd[c]; r++) {
        const u8 *R = RND(c, r);
        if (R[17]) { int m = R[0] == K_GROUPS ? R[6] * R[7] : R[18]; return m < 32 ? m : 32; }
    }
    return 0;
}
static void lp_clear(int c) {           /* (only its own places: the Recopa's list has 2, the next struct follows) */
    for (int i = 0, m = lp_size(c); i < m; i++) CT_LP[c][i] = 0xFFFF;
}
static void clear_run(void) {
    for (int c = 0; c < NCUP; c++) for (int r = 0; r < NRD; r++) EN[c][r] = 0;
    CT_OUT.ncup = ncup;
    for (int i = 0; i < NC; i++) { CT_OUT.last[i] = OUT; CT_OUT.out_of[i] = 0; }
    for (int c = 0; c < NCUP; c++) { CT_OUT.winner[c] = CT_OUT.second[c] = 0;
        for (int r = 0; r < NRD; r++) { CT_OUT.nent[c][r] = 0; for (int i = 0; i < NE; i++) { CT_OUT.ent[c][r][i] = 0; CT_OUT.res[c][r][i] = 0; } } }
}
static u32 rpts(u16 id);
static void ed_add(int x, int cr, int side) {
    if (CT_ED.ne >= NED) return;
    u16 id = CT_IN.club[x - 1].id;
    u32 st = CT_IN.club[x - 1].str / 16;            /* (strength / 16 in 12 bits: sq() caps it at 4000; the top 4
                                                       bits: the entry's slot, the Intercontinental's stepladder) */
    u16 sv = (u16)((st > 0xFFF ? 0xFFF : st) | (side & 15) << 12);
    CT_ED.e[CT_ED.ne].id0 = (u8)id; CT_ED.e[CT_ED.ne].id1 = (u8)(id >> 8);
    CT_ED.e[CT_ED.ne].cr = (u8)cr;
    CT_ED.e[CT_ED.ne].s0 = (u8)sv; CT_ED.e[CT_ED.ne].s1 = (u8)(sv >> 8);
    CT_ED.ne++;
}
static int assoc_of(u16 id) {           /* a team word's association: its file's, else a guest's, else none (0xFD) */
    for (int a = 0; a < na; a++) if (afile[a] && afile[a] == (id & 0xFF)) return a;
    for (int g = 0; g < ngu; g++) if ((gu[8 * g] | gu[8 * g + 1] << 8) == id) return gu[8 * g + 2];
    return 0xFD;
}
static void snapshot(void) {            /* the edition's entrants (after access) + the reserve clubs for group fills */
    static u8 seen[NC + 1];
    for (int i = 0; i <= NC; i++) seen[i] = 0;
    CT_ED.ne = 0; CT_ED.nf = 0;
    CT_ED.seed = rng;
    CT_ED.mgr = CT_IN.manager ? CT_IN.club[CT_IN.manager - 1].id : 0;
    for (int c = 0; c < ncup; c++)
        for (int r = 0; r < nrd[c]; r++)
            for (int i = 0; i < EN[c][r]; i++) { seen[E[c][r][i]] = 1; ed_add(E[c][r][i], c << 4 | r, EK[c][r][i]); }
    for (int c = 0; c < ncup; c++)      /* up to 4 reserves per cup: its associations' best clubs in no cup */
        for (int k = 0; k < 4; k++) {
            int b = 0;
            for (int i = 1; i <= CT_IN.nclub; i++)
                if (!taken[i] && !seen[i] && in_cup(c, assoc(i)) && (!b || pts(i) > pts(b))) b = i;
            if (!b) break;
            seen[b] = 1;
            ed_add(b, 0xFF, 0);
        }
    CT_ED.mark = 0x4543;
}
static int run(int stop) {              /* the edition from its entrants, the career club's results forced; stop: at
                                           its next unplayed game (CT_SEG), 1 returned */
    static u16 idx[NED];
    CT_IN.nclub = 0;
    CT_IN.manager = 0;
    for (int i = 0; i < CT_ED.ne; i++) {    /* the clubs: each team word once */
        u16 id = CT_ED.e[i].id0 | CT_ED.e[i].id1 << 8, sv = CT_ED.e[i].s0 | CT_ED.e[i].s1 << 8;
        int k = 0;
        for (int j = 0; j < CT_IN.nclub; j++) if (CT_IN.club[j].id == id) { k = j + 1; break; }
        if (!k) {
            k = ++CT_IN.nclub;
            CT_IN.club[k - 1].id = id; CT_IN.club[k - 1].assoc = (u8)assoc_of(id);
            CT_IN.club[k - 1].str = (u32)(sv & 0xFFF) * 16; CT_IN.club[k - 1].pts = rpts(id);
            taken[k] = 0;
            if (CT_ED.mgr && id == CT_ED.mgr) CT_IN.manager = k;
        }
        idx[i] = k;
        if (CT_ED.e[i].cr != 0xFF) taken[k] = 1;
    }
    rng = CT_ED.seed ? CT_ED.seed : 1;
    clear_run();
    for (int i = 0; i < CT_ED.ne; i++)
        if (CT_ED.e[i].cr != 0xFF) enter(CT_ED.e[i].cr >> 4, CT_ED.e[i].cr & 15, idx[i], 0, CT_ED.e[i].s1 >> 4);
    g_stop = stop;
    CT_SEG.kind = 0xFF;
    /* every round in calendar order: month (8 = August first, the season's order), then cup, then round */
    for (int mo = 0; mo < 12; mo++) {
        int month = (mo + 7) % 12 + 1;
        for (int c = 0; c < ncup; c++)
            for (int r = 0; r < nrd[c]; r++)
                if (RND(c, r)[9] == month && ct_round(c, r)) return 1;
    }
    return 0;
}
void ct_finish(void) {                  /* this season's edition finished, as played: the next one's holders (before
                                           the season end's tables fill CT_IN: run() uses CT_IN for the edition) */
    if (!CT_SPEC) return;
    if (CT_ED.mark != 0x4543 && CT_ST.mark != 0x5443) {   /* the first season end: the 2027 editions (the build's) */
        parse();
        for (int c = 0; c < ncup; c++) {
            const u8 *e = s1h + 8 * c;
            u16 w = e[0] | e[1] << 8;
            if (!w || w == 0xFFFF) continue;
            CT_ST.hold[c].id = w; CT_ST.hold[c].assoc = e[2];
            CT_ST.hold[c].str = e[4] | e[5] << 8 | e[6] << 16 | e[7] << 24; CT_ST.hold[c].pts = 0;
        }
        return;
    }
    if (CT_ED.mark != 0x4543) return;
    parse();
    run(0);
    for (int c = 0; c < ncup; c++) {
        int w = CT_OUT.winner[c];
        if (!w) continue;
        CT_ST.hold[c].id = CT_IN.club[w - 1].id; CT_ST.hold[c].assoc = CT_IN.club[w - 1].assoc;
        CT_ST.hold[c].str = CT_IN.club[w - 1].str; CT_ST.hold[c].pts = CT_IN.club[w - 1].pts;
    }
    CT_ED.mark = 0;
}
void ct_season(void) {                  /* the season end (after ct_finish and the tables): the next edition built */
    parse();
    CT_ED.mark = 0;
    rng = CT_IN.seed ? CT_IN.seed : 1;
    for (int c = 0; c < NCUP; c++) for (int r = 0; r < NRD; r++) EN[c][r] = 0;
    for (int c = 0; c < ncup; c++) if (CT_LP[c]) lp_clear(c);
    access();
    snapshot();
    CT_ST.mark = 0x5443; CT_ST.season++;
    run(1);
}
void ct_result(u32 v) {                 /* the career club's game in CT_SEG ended: ko 1 won / 0 lost, groups its place */
    if (!CT_SPEC || CT_ED.mark != 0x4543 || CT_SEG.kind == 0xFF) return;
    parse();
    if (CT_ED.nf < 16) {
        CT_ED.f[CT_ED.nf].c = CT_SEG.cup; CT_ED.f[CT_ED.nf].r = CT_SEG.round; CT_ED.f[CT_ED.nf].v = (u8)v;
        CT_ED.nf++;
    }
    run(1);
}
void ct_resume(void) {                  /* after a load: CT_SEG again from the saved edition */
    if (!CT_SPEC || CT_ED.mark != 0x4543) { CT_SEG.kind = 0xFF; return; }
    parse();
    run(1);
}

#define OFF(t, f) ((u32) __builtin_offsetof(struct t, f))
const u32 CT_LAYOUT[] = { sizeof(struct ct_in), sizeof(struct ct_state), sizeof(struct ct_out),
                          OFF(ct_in, league), OFF(ct_in, cup), OFF(ct_in, ap), OFF(ct_in, cl), OFF(ct_in, club),
                          OFF(ct_out, winner), OFF(ct_out, second), OFF(ct_out, last), OFF(ct_out, nent),
                          OFF(ct_out, ent), OFF(ct_out, res), NA, NL, NC, NCUP, NRD, NE,
                          sizeof(struct ct_ed), sizeof(struct ct_seg) };

#endif /* SWOS_WORLDS */
