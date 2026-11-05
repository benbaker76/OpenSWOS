/* Random UEFA qualifying seasons, the same in both programs (uefa_mod.c: the Mod's uefaq.c;
 * uefa_ours.c: src/uefa_access.c): the world's associations, each one's top 9 and cup winner
 * (club index (rank - 1) * 10 + slot + 1, as the Mod's uefaq.py numbers them), the title
 * holders, a career club, league cup winners and European play-offs (the packs' own, compiled:
 * uefa_specs.h); then the career club's ties one by one with random results, and the
 * coefficient points. SETEP(rk, p) sets UQ_IN.eplay[rk] the program's way. */
#include "uefa_specs.h"
static unsigned cs;
static unsigned crand(unsigned n) { cs = cs * 1664525u + 1013904223u; return (cs >> 8) % n; }
static struct uq_state S;

static void make_case(unsigned it)
{
    unsigned char *p = (unsigned char *)&UQ_IN;
    for (unsigned i = 0; i < sizeof UQ_IN; i++) p[i] = 0;
    cs = it * 2654435761u + 99;
    UQ_IN.seed = crand(0x7FFFFFFF) * 2u + crand(2);
    const int kind = crand(4), lo = kind == 0 ? 55 : kind == 1 ? 45 : 25 + crand(25);
    for (int rk = 1; rk <= 55; rk++)
        UQ_IN.present[rk] = rk <= lo ? crand(20) != 0 || rk == 1 : 0;
    UQ_IN.eps[0] = crand(3) ? 1 + crand(10) : 0;
    UQ_IN.eps[1] = crand(3) ? 1 + crand(10) : 0;
    UQ_CUPONLY_FILE[0] = 0;
    int n = 0;
    for (int rk = 1; rk <= 55; rk++) {
        if (!UQ_IN.present[rk]) continue;
        const int file = 100 + rk, nl = rk == 44 && crand(2) ? 0 : 6 + crand(4);
        for (int s = 0; s < 10; s++) {
            const int c = (rk - 1) * 10 + s + 1;
            UQ_IN.club[c - 1].id = (unsigned short)(file | s << 8);
            UQ_IN.club[c - 1].rank = (unsigned char)rk;
            UQ_IN.club[c - 1].coef = (60 - rk) * 1000u * (1 + crand(9)) + crand(1000) * (crand(4) != 0);
            UQ_IN.club[c - 1].str = 40 + crand(400);
            if (s < nl) UQ_IN.league[rk][s] = (unsigned short)c;
            if (c > n) n = c;
        }
        if (!nl) UQ_CUPONLY_FILE[0] = (unsigned char)file;     /* (a cup-only association) */
        const int cw = crand(3);
        UQ_IN.cup[rk] = (unsigned short)((rk - 1) * 10 + (cw == 0 ? 10 : 1 + crand(nl ? nl : 1)));
        if (crand(6) == 0) UQ_IN.lcup[rk] = (unsigned short)((rk - 1) * 10 + 1 + crand(nl ? nl : 1));
        if (crand(5) == 0) SETEP(rk, SPECS[crand(NSPECS)]);
    }
    UQ_IN.nclub = (unsigned short)n;
    for (int h = 0; h < 3; h++) {
        int c = crand(3) ? 1 + crand(n) : 0;
        if (c && !UQ_IN.club[c - 1].id) c = 0;
        UQ_IN.hold[h] = (unsigned short)c;
    }
    int m = 1 + crand(n);
    if (!UQ_IN.club[m - 1].id || crand(5) == 0) m = 0;
    UQ_IN.manager = (unsigned short)m;
}

static void print_case(unsigned it)
{
    printf("%u n %d %d %d st %d %d %d nent %d standin %d mt %d", it, UQ_OUT.n[0], UQ_OUT.n[1], UQ_OUT.n[2],
           UQ_OUT.status[0], UQ_OUT.status[1], UQ_OUT.status[2], UQ_OUT.nent, UQ_OUT.standin, UQ_OUT.mt_n);
    for (int k = 0; k < UQ_OUT.mt_n; k++) printf(" %d/%d/%d", UQ_OUT.mt_round[k], UQ_OUT.mt_opp[k], UQ_OUT.mt_win[k]);
    for (int c = 0; c < 3; c++) {
        printf(" |ls");
        for (int i = 0; i < UQ_OUT.n[c]; i++) printf(" %d", UQ_OUT.ls[c][i]);
        printf(" |gl");
        for (int i = 0; i < UQ_OUT.gn[c]; i++) printf(" %d", UQ_OUT.gl[c][i]);
        printf(" k%d", UQ_OUT.hkeep[c]);
    }
    printf(" |r");
    for (int c = 0; c < UQ_IN.nclub; c++)
        if (UQ_OUT.round_of[c] != NR || UQ_OUT.last[c] != NR) printf(" %d:%d,%d", c, UQ_OUT.round_of[c], UQ_OUT.last[c]);
    printf(" |br %d", S.nbr);
    for (int t = 0; t < S.nbr; t++) printf(" %d%c%04x%04x", S.br[t].r, S.br[t].aw ? '+' : '-', S.br[t].a, S.br[t].b);
    printf(" |pot");
    for (int i = 0; i < UQ_R.npot; i++) printf(" %04x", UQ_R.pot[i]);
    /* the career club's ties, played with random results */
    for (int t = 0; t < 6 && S.mark; t++) {
        uq_resolve();
        if (UQ_R.done) break;
        printf(" |tie %d %04x", UQ_R.r, UQ_R.opp);
        S.res |= (unsigned char)(crand(2) << S.k);
        S.k++;
    }
    if (S.mark) {
        uq_resolve();
        printf(" |done %d dest %d", UQ_R.done, UQ_R.dest);
        for (int c = 0; c < 3; c++) for (int i = 0; i < 36; i++) printf(" %04x", UQ_R.gl[c][i]);
    }
    uq_coef();
    printf(" |cp %u", (unsigned)UQ_NCP);
    for (unsigned i = 0; i < UQ_NCP; i++) printf(" %04x.%d.%d", UQ_CP[i].team, UQ_CP[i].units, UQ_CP[i].count);
    printf("\n");
}

static int run(int argc, char **argv)
{
    const unsigned N = argc > 1 ? (unsigned)atoi(argv[1]) : 2000;
    for (unsigned it = 0; it < N; it++) {
        make_case(it);
        UQ_S = &S;
        uq_season();
        print_case(it);
    }
    return 0;
}
