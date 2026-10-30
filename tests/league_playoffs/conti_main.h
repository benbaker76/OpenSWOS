/* The conti check's driver (tests/league_playoffs/conti_mod.c / conti_ours.c include it after
 * their conti.c): random seasons of a synthetic world -- per association 18 clubs, strength
 * by size, a random final table and cup winner -- as the Mod's tools/conti.py test makes them,
 * with its own LCG; each season ends the previous edition, plays the next one and prints every
 * cup's winner, runner-up, round sizes and drawn entrants. The spec: argv[1] (the Mod's
 * conti.load over the world's data). */
#include <stdio.h>
#include <stdlib.h>

static unsigned r_seed;
static unsigned r_below(unsigned n) { r_seed = r_seed * 1664525u + 1013904223u; return (r_seed >> 8) % n; }
static unsigned char spec_buf[1 << 16];
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
    }
    return 0;
}
