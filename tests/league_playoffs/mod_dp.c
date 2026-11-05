/* The Mod's dp_season (tools/dplay.c, included as it is): Serie B = division 1, groups 2..4. */
#include <stdio.h>
#include DPLAY_C
void mod_dp(const unsigned short (*rows)[32][6], const int *n, unsigned short cupw, FILE *out)
{
    DP_IN.ng = 3;
    DP_IN.cupw = cupw;
    for (int d = 0; d < 4; d++) {
        DP_IN.n[d] = (u8)n[1 + d];
        for (int k = 0; k < n[1 + d]; k++) {
            struct dp_row *r = &DP_IN.t[d][k];
            r->team = rows[1 + d][k][0]; r->glob = rows[1 + d][k][1]; r->won = rows[1 + d][k][2];
            r->gf = rows[1 + d][k][3]; r->ga = rows[1 + d][k][4]; r->pts = rows[1 + d][k][5];
        }
    }
    DP_IN.n[4] = 0;
    dp_season();
    for (int i = 0; i < DP_OUT.nmv; i++) fprintf(out, " d%u:%d", DP_OUT.mv[i].glob, DP_OUT.mv[i].delta);
}
