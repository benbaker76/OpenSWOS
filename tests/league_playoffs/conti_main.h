/* The conti check's driver (tests/league_playoffs/conti_mod.c / conti_ours.c include it after
 * their conti.c): random seasons of a synthetic world -- per association 18 clubs, strength
 * by size, a random final table and cup winner -- as the Mod's tools/conti.py test makes them,
 * with its own LCG; each season ends the previous edition, plays the next one and prints every
 * cup's winner, runner-up, round sizes and drawn entrants. The spec: argv[1] (the Mod's
 * conti.load over the world's data). Then, for every cup whose main round is a group stage, a
 * group stage's end in the game's contest (random rows, points, the career club among them): the
 * Mod's ct_gpre / ct_gend over the DOS structures (CONTI_C: built -m32, its records' addresses),
 * ours over the same as arrays -- the club's place and the next stage's list. */
#include <stdio.h>
#include <stdlib.h>

static unsigned r_seed;
static unsigned r_below(unsigned n) { r_seed = r_seed * 1664525u + 1013904223u; return (r_seed >> 8) % n; }
static unsigned char spec_buf[1 << 16];
static unsigned char g_diy[0x2B3 + 64 * 18 + 16], g_diyc[0x59 + 80], g_sel[64 * 684];
static void put16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void group_ends(int season)
{
    parse();
    for (int c = 0; c < ncup; c++) {
        int mr = -1;
        for (int r = 0; r < nrd[c]; r++) if (RND(c, r)[17]) mr = r;
        if (mr < 0 || RND(c, mr)[0] != K_GROUPS) continue;
        int ng = RND(c, mr)[6], gs = RND(c, mr)[7], n = ng * gs, need = (gs - 1) * (RND(c, mr)[8] ? RND(c, mr)[8] : 2);
        if (n > 64) continue;
        unsigned char row[64];
        unsigned short word[64], pts[64], played[64], wgo[64], pgo[64], playgo[64];
        for (int i = 0; i < n; i++) row[i] = (unsigned char)i;
        for (int i = n - 1; i > 0; i--) { int j = (int)r_below((unsigned)i + 1); unsigned char x = row[i]; row[i] = row[j]; row[j] = x; }
        for (int r = 0; r < n; r++) {
            word[r] = (unsigned short)((150 + c) | r << 8);
            pts[r] = (unsigned short)r_below(19);
            played[r] = (unsigned short)need;
        }
        if (season % 5 == 4) played[r_below((unsigned)n)]--;      /* (a group not complete yet) */
        for (int i = 0; i < n; i++) { wgo[i] = word[row[i]]; pgo[i] = pts[row[i]]; playgo[i] = played[row[i]]; }
        unsigned short mgr = CT_ED.mgr;
        CT_ED.mgr = word[r_below((unsigned)n)];
        unsigned char list[80];
        for (int i = 0; i < 80; i++) list[i] = 0xEE;
        unsigned p1, p2;
#ifdef CONTI_C
        for (unsigned i = 0; i < sizeof g_diy; i++) g_diy[i] = 0;
        for (int i = 0; i < 80; i++) g_diyc[0x59 + i] = 0xEE;
        g_diy[0x2D] = gid[c];
        put16(g_diy + 0x4F, (unsigned)ng); put16(g_diy + 0x51, (unsigned)gs);
        for (int i = 0; i < n; i++) put16(g_diy + 0x6D + 2 * i, row[i] * 18u);
        for (int r = 0; r < n; r++) {
            unsigned char *q = g_diy + 0x2B3 + r * 18;
            unsigned off = (unsigned)r * 684;
            q[0] = (unsigned char)off; q[1] = (unsigned char)(off >> 8); q[2] = q[3] = 0;
            put16(q + 4, played[r]); put16(q + 16, pts[r]);
            put16(g_diy + 0x12D + 2 * r, word[r]);
            put16(g_sel + off, word[r]);
        }
        p1 = ct_gpre(g_diy);
        p2 = ct_gend(g_diy, g_diyc, (u32)(unsigned long)g_sel);
        for (int i = 0; i < 80; i++) list[i] = g_diyc[0x59 + i];
        for (int i = 0; i < n; i++) row[i] = (unsigned char)((g_diy[0x6D + 2 * i] | g_diy[0x6E + 2 * i] << 8) / 18);
#else
        p1 = ct_gplace(c, ng, gs, wgo, playgo);
        p2 = ct_gend(c, ng, gs, row, wgo, pgo, list);
#endif
        CT_ED.mgr = mgr;
        printf("  cup %d groups: %u %u", c, p1, p2);
        for (int i = 0; i < 32; i++) printf(" %x", list[i]);
        printf(" |");
        for (int i = 0; i < n; i++) printf(" %u", row[i]);
        printf("\n");
    }
}
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    int seasons = argc > 2 ? atoi(argv[2]) : 50;
    unsigned nspec = f ? (unsigned)fread(spec_buf, 1, sizeof spec_buf, f) : 0;
    if (!nspec) { fprintf(stderr, "no spec\n"); return 2; }
    fclose(f);
    CT_SPEC = spec_buf;
    const int na = spec_buf[1];
    for (int season = 0; season < seasons; season++) {
        r_seed = 7u * 100u + (unsigned)season;
        ct_finish();
        { unsigned char *z = (unsigned char *)&CT_IN; for (unsigned i = 0; i < sizeof CT_IN; i++) z[i] = 0; }
        CT_IN.seed = 700 + season;
        int n = 0;
        for (int a = 0; a < na && a < NA; a++) {
            int first = n;
            for (int k = 0; k < 18 && n < NC - 300; k++, n++) {
                CT_IN.club[n].id = (unsigned short)((100 + a) | k << 8);
                CT_IN.club[n].assoc = (unsigned char)a;
                CT_IN.club[n].str = 1500 + r_below(1500) - 60 * k + (a < 2 ? 1500 : 0);
                CT_IN.club[n].pts = 100 + r_below(8900);
            }
            int m = n - first, ord[18];
            for (int i = 0; i < m; i++) ord[i] = first + i;
            for (int i = m - 1; i > 0; i--) { int j = (int)r_below((unsigned)i + 1), x = ord[i]; ord[i] = ord[j]; ord[j] = x; }
            for (int l = 0; l < m && l < NL; l++) CT_IN.league[a][l] = (unsigned short)(ord[l] + 1);
            if (m) CT_IN.cup[a] = (unsigned short)(ord[r_below(m < 7 ? (unsigned)m : 7u)] + 1);
        }
        CT_IN.nclub = (unsigned short)n;
        ct_season();
        printf("season %d:", season);
        for (int c = 0; c < CT_OUT.ncup; c++) {
            printf(" [%d w%u s%u", c, CT_OUT.winner[c] ? CT_IN.club[CT_OUT.winner[c] - 1].id : 0,
                   CT_OUT.second[c] ? CT_IN.club[CT_OUT.second[c] - 1].id : 0);
            for (int r = 0; r < NRD; r++) {
                if (!CT_OUT.nent[c][r]) continue;
                printf(" r%d:%u:", r, CT_OUT.nent[c][r]);
                for (int i = 0; i < CT_OUT.nent[c][r] && i < NE; i++)
                    printf("%x%s", CT_OUT.ent[c][r][i] ? CT_IN.club[CT_OUT.ent[c][r][i] - 1].id : 0,
                           i + 1 < CT_OUT.nent[c][r] ? "," : "");
            }
            printf("]");
        }
        printf("\n");
        group_ends(season);
    }
    return 0;
}
