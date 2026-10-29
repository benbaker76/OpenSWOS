/* Random B-team cases, the same in both programs: Italy 2026-27's 14 divisions, the three B
 * teams and their first teams each put at a random place (a B team below its first team's
 * division), the moves of the Mod's record by place. */
static const int SZ[14] = { 20, 20, 20, 20, 20, 17, 17, 17, 18, 17, 18, 17, 17, 18 };
static const int MPROM[14] = { 0, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
static const int MREL[14] = { 3, 3, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2 };
static const int LVL[14] = { 0, 1, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3 };
static const int BT[3][2] = { { 44, 5 }, { 67, 6 }, { 89, 0 } };
enum { CBASE = 424 };
static unsigned cs;
static unsigned crand(unsigned n) { cs = cs * 1664525u + 1013904223u; return (cs >> 8) % n; }
/* glob[d][k], move[d][k] (-1 up, 1 down, 0) */
static void make_case(unsigned it, unsigned short glob[14][32], int move[14][32])
{
    cs = it * 2654435761u + 7;
    for (int d = 0; d < 14; d++)
        for (int k = 0; k < SZ[d]; k++) {
            glob[d][k] = (unsigned short)(1000 + d * 32 + k);
            move[d][k] = k < MPROM[d] && d > 0 ? -1 : k >= SZ[d] - MREL[d] ? 1 : 0;
        }
    for (int i = 0; i < 3; i++) {
        int df = (int)crand(4), db, kf, kb;          /* first team: A, B or a C group */
        if (df == 3) df = 2 + (int)crand(3);
        db = df + 1 + (int)crand(13 - df);           /* its B team: any division below */
        if (db > 13) db = 13;
        do { kf = (int)crand(SZ[df]); } while (glob[df][kf] < 1000);
        do { kb = (int)crand(SZ[db]); } while (glob[db][kb] < 1000 || (db == df && kb == kf));
        glob[df][kf] = (unsigned short)(CBASE + BT[i][1]);
        glob[db][kb] = (unsigned short)(CBASE + BT[i][0]);
    }
}

/* The other countries with B teams (World 2026-27: no groups, a pool below the lowest
 * division, no play-offs for a B team): each league's shape, its B teams (their ordinals,
 * their first teams'), each first team at a random place of any division but the lowest,
 * its B team in one below; the moves of the Mod's record by place. Each country's clubs are
 * global numbers of their own (base + ordinal), its SEC_TAB row in mod_sec.c. */
typedef struct {
    const char *name;
    int country, base, nd, sz[3], prom[3], rel[3], nb, bt[4][2], top, flags;
} SecShape;
static const SecShape SHAPES[4] = {
    { "ESP", 35, 1600, 2, { 20, 22 }, { 0, 2 }, { 3, 0 }, 1, { { 31, 9 } }, 255, 2 },
    { "GER", 14, 1900, 3, { 18, 18, 20 }, { 0, 2, 2 }, { 2, 2, 0 }, 2, { { 46, 3 }, { 48, 4 } }, 2, 2 },
    { "NED", 25, 2200, 2, { 18, 20 }, { 0, 2 }, { 2, 0 }, 4, { { 21, 0 }, { 26, 5 }, { 31, 6 }, { 34, 4 } }, 1, 2 },
    { "POR", 29, 2500, 2, { 18, 18 }, { 0, 2 }, { 2, 0 }, 3, { { 20, 0 }, { 25, 2 }, { 28, 1 } }, 255, 2 },
};
static void make_shape_case(const SecShape *s, unsigned it, unsigned short glob[14][32], int move[14][32])
{
    cs = it * 2654435761u + 11 + (unsigned)s->country;
    for (int d = 0; d < s->nd; d++)
        for (int k = 0; k < s->sz[d]; k++) {
            glob[d][k] = (unsigned short)(1000 + d * 32 + k);
            move[d][k] = k < s->prom[d] && d > 0 ? -1 : k >= s->sz[d] - s->rel[d] ? 1 : 0;
        }
    for (int i = 0; i < s->nb; i++) {
        int df = (int)crand((unsigned)s->nd - 1), db = df + 1 + (int)crand((unsigned)(s->nd - 1 - df)), kf, kb;
        do { kf = (int)crand((unsigned)s->sz[df]); } while (glob[df][kf] < 1000);
        do { kb = (int)crand((unsigned)s->sz[db]); } while (glob[db][kb] < 1000);
        glob[df][kf] = (unsigned short)(s->base + s->bt[i][1]);
        glob[db][kb] = (unsigned short)(s->base + s->bt[i][0]);
    }
}
