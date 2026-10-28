/* The Coppa Italia Serie D's later seasons (scripts/league_playoffs_check.sh): per case the
 * 256 clubs' divisions last season and this one, last season's Serie D tables, the slots
 * that took a pool club -- the same for the Mod's cd_rank / cd_season / cd_make and ours. */
static const int CD_SIZE[14] = { 20, 20, 20, 20, 20, 17, 17, 17, 18, 17, 18, 17, 17, 18 };
enum { CD_FIRST = 5, CD_GROUPS = 9, CD_ROUNDS = 8 };
static unsigned cd_seed;
static unsigned cd_rand(unsigned n) { cd_seed = cd_seed * 1664525u + 1013904223u; return (cd_seed >> 8) % n; }
struct cd_case {
    unsigned char base[256];
    signed char last[256], next[256];   /* last season's / this season's division - base */
    unsigned char order[14][32];        /* last season's tables: ordinals, top first */
    int n[14];
    unsigned char arrival[32];
    int season;                         /* the season byte (the generator's seed) */
};
static void make_cd_case(unsigned it, struct cd_case *c)
{
    int o = 0;
    cd_seed = it * 2246822519u + 3u;
    for (int d = 0; d < 14; d++)
        for (int k = 0; k < CD_SIZE[d]; k++) c->base[o++] = (unsigned char)d;
    for (int d = 0; d < 14; d++) c->n[d] = 0;
    for (int i = 0; i < 256; i++) c->last[i] = c->next[i] = 0;
    for (int i = 0; i < 256; i++) {
        int d = c->base[i], ld = d, nd;
        if (cd_rand(6) == 0) ld = d >= CD_FIRST ? CD_FIRST + (int)cd_rand(CD_GROUPS) : d;   /* (moved before) */
        nd = ld;
        int r = (int)cd_rand(12);
        if (r == 0 && ld >= 2 && ld < CD_FIRST) nd = CD_FIRST + (int)cd_rand(CD_GROUPS);   /* (relegated from C) */
        else if (r == 1 && ld >= CD_FIRST) nd = 2 + (int)cd_rand(3);                       /* (promoted to C) */
        else if (r == 2 && ld >= CD_FIRST) nd = CD_FIRST + (int)cd_rand(CD_GROUPS);       /* (another group) */
        if (c->n[ld] >= 24) ld = nd = d;    /* (a table holds 24: real groups are at most 18) */
        if (c->n[ld] >= 24) continue;       /* (its own division full: dropped from both) */
        c->last[i] = (signed char)(ld - d);
        c->next[i] = (signed char)(nd - d);
        c->order[ld][c->n[ld]++] = (unsigned char)i;
    }
    for (int d = 0; d < 14; d++)
        for (int k = c->n[d] - 1; k > 0; k--) {
            int j = (int)cd_rand((unsigned)k + 1);
            unsigned char x = c->order[d][k]; c->order[d][k] = c->order[d][j]; c->order[d][j] = x;
        }
    for (int i = 0; i < 32; i++) c->arrival[i] = 0;
    for (int i = 0; i < 256; i++)
        if (c->last[i] + c->base[i] >= CD_FIRST && c->next[i] + c->base[i] >= CD_FIRST && cd_rand(30) == 0)
            c->arrival[i >> 3] |= (unsigned char)(1 << (i & 7));
    c->season = 127 + (int)cd_rand(20);
}
