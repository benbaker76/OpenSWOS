/* The Mod's gp_season (tools/gplay.c, included as it is) over one season's tables. */
#include <stdio.h>
#include <stdlib.h>
#include GPLAY_C
const u8 SEC_TAB[1] = { 0 };            /* (no second teams: play-outs take them anyway) */
s8 SEC_SLT[4096], SEC_COPY[4096];
const u8 GP_PROM_TAB[1] = { 0 };
u16 GP_TOPW[9];
u8 GP_TOPR[9 * 684];
const u8 GP_HLT[4] = { 0 };
void mod_stages(const unsigned short (*r)[32][6], const int *n, int nk, const unsigned short (*keep)[6], int fresh)
{
    for (int k = 0; k < 4; k++) {
        GP_SSB[0].n[k] = (u8)n[k];
        for (int i = 0; i < n[k]; i++) {
            struct gp_row *x = &GP_SSB[0].r[k][i];
            x->team = r[k][i][0]; x->glob = r[k][i][1]; x->won = r[k][i][2];
            x->gf = r[k][i][3]; x->ga = r[k][i][4]; x->pts = r[k][i][5];
        }
    }
    GP_SSB[0].fresh = (u8)fresh;
    GP_IN.nst[0] = (u8)nk;
    GP_IN.nst[1] = 0;
    for (int i = 0; i < nk; i++) {
        struct gp_row *x = &GP_IN.st[0][i];
        x->team = keep[i][0]; x->glob = keep[i][1]; x->won = 0; x->gf = 0; x->ga = 0; x->pts = keep[i][5];
    }
}
void mod_gp(const unsigned short (*rows)[32][6], const int *n, int nd, const u8 *sp, FILE *out)
{
    GP_IN.nd = (u8)nd;
    for (int d = 0; d < nd; d++) {
        GP_IN.n[d] = (u8)n[d];
        for (int k = 0; k < n[d]; k++) {
            struct gp_row *r = &GP_IN.t[d][k];
            r->team = rows[d][k][0]; r->glob = rows[d][k][1]; r->won = rows[d][k][2];
            r->gf = rows[d][k][3]; r->ga = rows[d][k][4]; r->pts = rows[d][k][5];
        }
    }
    GP_IN.spec = sp;
    gp_season();
    for (int i = 0; i < GP_OUT.nmv; i++) fprintf(out, " g%u:%d", GP_OUT.mv[i].glob, GP_OUT.mv[i].delta);
    for (int i = 0; i < GP_OUT.nres; i++)
        fprintf(out, " T%u:%u/%u", GP_OUT.res[i].kind, GP_OUT.res[i].glob[0], GP_OUT.res[i].glob[1]);
    for (int k = 0; k < 3; k++)
        if (GP_OUT.sres[k].kind) fprintf(out, " S%d:%u/%u", k, GP_OUT.sres[k].glob[0], GP_OUT.sres[k].glob[1]);
}
