/*
 * state_pool.c -- a world's state pools at the season end (include/state_pool.h): the SWOS 96/97
 * Mod's tools/gplay.c ri_season / ri_pool (MIT, Davide Lorigliola), its 'state' pools as they
 * are, over its table (compiled by scripts/world_states.py as the Mod's ripool.table_bytes).
 *
 * A state championship (Brazil 2026: the state-only clubs of the states' file) sends its n
 * relegated -- the bottom of its stage-1 ranking (the rankings' blocks, state_sim.c), else by
 * RI_RULE (0 none, 1 the weakest n, 2 a draw weighted by weakness) -- to a queue of its pool's
 * clubs, which come in in turn: the slots swap IDENTITIES (map[slot] = identity + 1; an identity
 * < 256 a slot's own record of the file, 256 + k the pool's k-th club). Never the manager's
 * club. A LEAGUE club relegated from its state leaves the state contest (its national place
 * kept) and waits in the queue as a marker 0x8000 | its entry; the club coming in takes a free
 * SPARE slot of the file, the contest's entry pointing to it (ovr[pool][entry] = the new team
 * word, 0 = the original). A marker at the queue's head brings its league club back. No spare
 * free: that league club stays (the Mod's declared approximation).
 *
 * The team words are the Mod's (file | ordinal << 8): the league's file and the states' file
 * are told apart by their file byte (scripts/world_import.py gives the states' file a number of
 * its own); conti_season.c-style glue (states.c) turns them into ours.
 */
#include "state_pool.h"

#if SWOS_WORLDS

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed char s8;

enum { RIS = 256, RF = STATE_POOL_FILES, RP = STATE_POOL_POOLS, RQT = STATE_POOL_QUEUE, RO = STATE_POOL_ENTRIES,
       RMT = STATE_POOL_MAP };

StatePoolState RI_STATE;
u8 RI_SWAPPED[RF][RIS / 8];                     /* (this season end's swapped slots) */
int RI_MGR;                                     /* (the manager's club, global number: never swapped) */
int RI_RULE = 2;
u8 RI_FILES_FILE[RF];                           /* (each table file's team file number, set by ri_season) */
static u32 ri_rng;

struct ri_file { u8 *init; u16 *map; int n; };   /* (a file's view of RI_STATE) */

static u32 ri_rand(u32 n) {
    ri_rng = ri_rng * 1103515245u + 12345u;
    return n ? (ri_rng >> 8) % n : 0;
}

static int ri_id(struct ri_file *f, int x) { return f->map[x] ? f->map[x] - 1 : x; }
static void ri_set(struct ri_file *f, int fi, int x, int id) {
    f->map[x] = id == x ? 0 : id + 1;
    RI_SWAPPED[fi][x >> 3] |= 1 << (x & 7);
}

/* a free spare of the file (pools p0 .. p0 + np - 1 of it hold its overrides), or -1 */
static int ri_spare(const u8 *sp, int ns, int file, int p0, int np) {
    for (int s = 0; s < ns; s++) {
        int w = file | sp[s] << 8, used = 0;
        for (int p = p0; p < p0 + np && p < RP; p++)
            for (int j = 0; j < RO; j++) if (RI_STATE.ovr[p][j] == w) used = 1;
        if (!used) return sp[s];
    }
    return -1;
}

static const u8 *ri_pool(const u8 *e, struct ri_file *f, int fi, u16 *q, u8 *qn, const u8 *sr, int run, int pi,
                         int base, const u8 *sp, int nsp, int p0, int np) {
    int kind = e[0], n = e[1], manager = RI_MGR;
    const u8 *p = e + 2, *order;
    u16 out[64];
    int no = 0, m = 0;
    if (kind != 1) {                            /* (a league's pool: team_pool.c's; its bytes skipped) */
        int ns = p[1] | p[2] << 8;
        return p + 3 + 4 * ns + n + n;
    }
    {
        int key = p[0] | p[1] << 8, rel = p[2], ns = p[3], file = RI_FILES_FILE[fi];
        const u8 *sl = p + 4, *ps = sl + 4 * ns, *en = ps + n + 1;
        int ne = ps[n];
        order = en + 2 * ne;
        u16 *ovr = RI_STATE.ovr[pi < RP ? pi : RP - 1];
        if (run && *f->init == 1) {
            for (int i = 0; i < n; i++) q[i] = RIS + order[i];
            *qn = n;
            for (int j = 0; j < RO; j++) ovr[j] = 0;
        }
        u16 rw[16];                             /* the relegated, as the contest's current team words */
        int nr = 0;
        const u8 *b = sr;
        while (run && b && (b[0] | b[1] << 8)) {    /* statespec's rankings ('SR'): this state's last 'rel' */
            int nb = b[2];
            if ((b[0] | b[1] << 8) == key) {
                for (int k = nb - rel < 0 ? 0 : nb - rel; k < nb && nr < 16; k++) rw[nr++] = b[4 + 2 * k] | b[5 + 2 * k] << 8;
                break;
            }
            b += 4 + 2 * nb;
        }
        if (run && !(b && (b[0] | b[1] << 8)) && RI_RULE) {    /* no ranking: the fallback among the state-only */
            u8 st[RIS], pick[RIS / 8];
            int smax = 0;
            for (int i = 0; i < RIS / 8; i++) pick[i] = 0;
            for (int i = 0; i < ns; i++) {
                int id = ri_id(f, sl[4 * i]), s = 0;
                for (int jj = 0; id < RIS && jj < ns; jj++) if (sl[4 * jj] == id) s = sl[4 * jj + 3];
                for (int jj = 0; id >= RIS && jj < n; jj++) if (order[jj] == id - RIS) s = ps[jj];
                st[i] = s;
                if (s > smax) smax = s;
            }
            for (int r = 0; r < rel && nr < 16; r++) {
                u32 tot = 0;
                int best = -1;
                for (int i = 0; i < ns; i++) {
                    int o = sl[4 * i];
                    if (pick[o >> 3] & 1 << (o & 7)) continue;
                    u32 wgt = smax + 1 - st[i];
                    if (RI_RULE == 1) { if (best < 0 || st[i] < st[best]) best = i; }
                    else tot += wgt * wgt;
                }
                if (RI_RULE == 2 && tot) {
                    u32 x = ri_rand(tot);
                    for (int i = 0; i < ns; i++) {
                        int o = sl[4 * i];
                        if (pick[o >> 3] & 1 << (o & 7)) continue;
                        u32 wgt = smax + 1 - st[i];
                        if (x < wgt * wgt) { best = i; break; }
                        x -= wgt * wgt;
                    }
                }
                if (best < 0) break;
                int o = sl[4 * best];
                pick[o >> 3] |= 1 << (o & 7);
                rw[nr++] = file | o << 8;
            }
        }
        for (int r = 0; r < nr && m < *qn && no < 64; r++) {
            int j = -1;
            for (int k = 0; k < ne && k < RO; k++)
                if ((ovr[k] ? ovr[k] : (en[2 * k] | en[2 * k + 1] << 8)) == rw[r]) j = k;
            if (j < 0) continue;
            int cur = ovr[j] ? ovr[j] : (en[2 * j] | en[2 * j + 1] << 8);
            int league = (cur & 0xFF) != file, x = cur >> 8;
            if (!league && base + x == manager) continue;   /* (never the manager's club) */
            int in = q[m], outid = league ? 0x8000 | j : ri_id(f, x);
            if (in & 0x8000) {                  /* a league club comes back: its spare's identity / the spare */
                int k = in & 0xFF, sw = ovr[k];
                if (k >= RO || !sw) continue;
                ovr[k] = 0;
                if (league) ovr[j] = sw;
                else ri_set(f, fi, x, ri_id(f, sw >> 8));
            } else if (league) {                /* a league club goes: the A2 club into a free spare */
                int s = ri_spare(sp, nsp, file, p0, np);
                if (s < 0) continue;
                ri_set(f, fi, s, in);
                ovr[j] = file | s << 8;
            } else
                ri_set(f, fi, x, in);
            out[no++] = outid;
            m++;
        }
    }
    if (run && m) {                             /* the queue: drop the taken, append the outs */
        int k = 0;
        for (int i = m; i < *qn; i++) q[k++] = q[i];
        for (int i = 0; i < no && k < n; i++) q[k++] = out[i];
    }
    if (run) *qn = n;
    return order + n;
}

/* One season end (the Mod's ri_season; its seed the season's own league table, slt): t the
 * pools' table, world its world, sr the states' rankings (state_sim.c's blocks), base the states'
 * file's global numbers (our world's, not the table's). */
void ri_season(const u8 *t, const s8 *slt, int world, const u8 *sr, int baseOverride) {
    for (int fi = 0; fi < RF; fi++)
        for (int i = 0; i < RIS / 8; i++) RI_SWAPPED[fi][i] = 0;
    u32 h = 0x9E3779B9u;                        /* (the draw: the season's own table as seed) */
    for (int g = 0; g < 2000; g++) h = h * 31 + (u8)slt[g];
    ri_rng = h;
    int nf = *t++, qo = 0, pi = 0;
    for (int fi = 0; fi < nf; fi++) {
        int w = t[0], np = t[2];
        RI_FILES_FILE[fi] = t[1];
        int base = t[3] | t[4] << 8, moff = t[5] | t[6] << 8, nmap = t[7] | t[8] << 8, nsp = t[9];
        if (baseOverride >= 0) base = baseOverride;
        struct ri_file fv = { &RI_STATE.init[fi < RF ? fi : RF - 1], RI_STATE.maps + moff, nmap }, *f = &fv;
        int run = w == world && fi < RF && moff + nmap <= RMT;
        if (run && !*f->init) {                 /* (a new career / an older save: identities, the pools' first order) */
            for (int i = 0; i < nmap; i++) f->map[i] = 0;
            *f->init = 1;
        }
        const u8 *sp = t + 10;
        t = sp + nsp;
        int p0 = pi;
        for (int k = 0; k < np; k++) {
            int n = t[1];
            t = ri_pool(t, f, fi, RI_STATE.q + qo, RI_STATE.qn + pi, sr,
                        run && pi < RP && qo + n <= RQT, pi, base, sp, nsp, p0, np);
            qo += n;
            pi++;
        }
        if (run) *f->init = 2;
    }
}

#endif /* SWOS_WORLDS */
