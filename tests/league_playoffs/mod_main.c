#include <stdio.h>
#include <stdlib.h>
#include "tables.h"
void mod_dp(const unsigned short (*rows)[32][6], const int *n, unsigned short cupw, FILE *out);
void mod_gp(const unsigned short (*rows)[32][6], const int *n, int nd, const unsigned char *sp, FILE *out);
void mod_ko(FILE *out);
void mod_stages(const unsigned short (*r)[32][6], const int *n, int nk, const unsigned short (*keep)[6], int fresh);
int main(int argc, char **argv)
{
    unsigned N = argc > 2 ? (unsigned)atoi(argv[2]) : 20000;
    struct trow t[16][32]; int n[16];
    unsigned short rows[16][32][6];
    load_spec(argv[1]);
    if (argc > 3) set_shape(argv[3]);
    for (unsigned it = 0; it < N; it++) {
        make_tables(it, t, n);
        for (int d = 0; d < ND; d++) for (int k = 0; k < n[d]; k++) {
            rows[d][k][0] = t[d][k].team; rows[d][k][1] = t[d][k].glob; rows[d][k][2] = t[d][k].won;
            rows[d][k][3] = t[d][k].gf; rows[d][k][4] = t[d][k].ga; rows[d][k][5] = t[d][k].pts;
        }
        {
            struct stages st;
            unsigned short sr[4][32][6], kr[32][6];
            make_stages(it, t, n, &st);
            for (int k = 0; k < 4; k++) for (int i = 0; i < st.n[k]; i++) {
                const struct trow *x = &st.r[k][i];
                sr[k][i][0] = x->team; sr[k][i][1] = x->glob; sr[k][i][2] = x->won;
                sr[k][i][3] = x->gf; sr[k][i][4] = x->ga; sr[k][i][5] = x->pts;
            }
            for (int i = 0; i < st.nkeep; i++) { kr[i][0] = st.keep[i].team; kr[i][1] = st.keep[i].glob; kr[i][5] = st.keep[i].pts; }
            mod_stages((const unsigned short (*)[32][6])sr, st.n, st.nkeep, (const unsigned short (*)[6])kr, st.fresh);
        }
        printf("%u", it);
        if (!GP_ONLY) mod_dp((const unsigned short (*)[32][6])rows, n, cup_winner(it, t, n), stdout);
        mod_gp((const unsigned short (*)[32][6])rows, n, ND, spec, stdout);
        if (argc > 4) mod_ko(stdout);   /* (the played contests' build: 'ko') */
        printf("\n");
    }
    return 0;
}
