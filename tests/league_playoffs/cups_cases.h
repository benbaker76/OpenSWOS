/* The cups' cases (scripts/league_playoffs_check.sh): Italy 2026-27's two cups as the
 * real lists make them (per division: its clubs, how many with a bye), the 256 clubs'
 * base divisions, and per case each club's move this season, last season's places and
 * levels, and the career club -- the same for the Mod's cf_build and our world_cups.c. */
static const int CUP_N[2] = { 16, 17 };
static const int CUP_G[2] = { 2, 3 };
static const unsigned char CUP_Q[2][3][3] = { { { 0, 13, 0 }, { 1, 3, 0 } },
                                              { { 2, 5, 5 }, { 3, 7, 7 }, { 4, 5, 3 } } };
static const int DIV_SIZE[14] = { 20, 20, 20, 20, 20, 17, 17, 17, 18, 17, 18, 17, 17, 18 };
static const int LEVEL[14] = { 0, 1, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3 };
static unsigned cc_seed;
static unsigned cc_rand(unsigned n) { cc_seed = cc_seed * 1664525u + 1013904223u; return (cc_seed >> 8) % n; }
struct cup_case {
    unsigned char base[256];
    signed char delta[256];
    unsigned char place[256];   /* place | level << 5, 0 none */
    int ranked, career;         /* career: its ordinal, -1 another country's club */
};
static void make_case(unsigned it, struct cup_case *c)
{
    int o = 0;
    cc_seed = it * 2654435761u + 7u;
    for (int d = 0; d < 14; d++)
        for (int k = 0; k < DIV_SIZE[d]; k++) c->base[o++] = (unsigned char)d;
    for (int i = 0; i < 256; i++) {
        int d = c->base[i], r = (int)cc_rand(10), nd = d;
        if (r == 0 && d > 0) nd = d - 1 - (int)(d > 4 && cc_rand(2));      /* (up a level: any group) */
        else if (r == 1 && d < 13) nd = d + 1 + (int)(d >= 2 && cc_rand(3));
        if (nd > 13) nd = 13;
        c->delta[i] = (signed char)(nd - d);
        int lv = LEVEL[d] + (int)cc_rand(3) - 1;                          /* (last season's level) */
        if (lv < 0) lv = 0;
        if (lv > 3) lv = 3;
        c->place[i] = cc_rand(12) ? (unsigned char)((1 + cc_rand(20)) | lv << 5) : 0;
    }
    c->ranked = cc_rand(5) != 0;
    c->career = cc_rand(4) ? (int)cc_rand(256) : -1;
}
