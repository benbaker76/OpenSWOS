/* The Mod's cf_build (tools/gplay.c, included as it is, CF_SEED 1 as its release builds). */
#include <stdio.h>
#include <stdlib.h>
#include GPLAY_C
#include "cups_cases.h"
const u8 SEC_TAB[1] = { 0 };
s8 SEC_SLT[4096], SEC_COPY[4096];
const u8 GP_PROM_TAB[1] = { 0 };
u16 GP_TOPW[9];
u8 GP_TOPR[9 * 684];
const u8 GP_HLT[4] = { 0 };
static u8 tab[4096];
static s8 slt[4096];
int main(int argc, char **argv)
{
    unsigned N = argc > 1 ? (unsigned)atoi(argv[1]) : 20000;
    struct cup_case cc;
    for (unsigned it = 0; it < N; it++) {
        make_case(it, &cc);
        int at = 0;
        tab[at++] = 5; tab[at++] = 126; tab[at++] = 2;          /* world, its first season, cups */
        for (int c = 0; c < 2; c++) {
            tab[at++] = (u8)(10 + c); tab[at++] = (u8)CUP_N[c]; tab[at++] = (u8)CUP_G[c]; tab[at++] = 0;
            for (int g = 0; g < CUP_G[c]; g++)
                for (int b = 0; b < 3; b++) tab[at++] = CUP_Q[c][g][b];
        }
        tab[at++] = 1;                                          /* one league file */
        tab[at++] = 0; tab[at++] = 0; tab[at++] = 20; tab[at++] = 0; tab[at++] = 1;   /* base 0, file 20, 256 */
        for (int d = 0; d < 16; d++) tab[at++] = (u8)(d < 14 ? LEVEL[d] : 0);
        for (int i = 0; i < 256; i++) { tab[at++] = (u8)i; tab[at++] = cc.base[i]; }
        CF_TAB = tab;
        for (int i = 0; i < 256; i++) slt[i] = cc.delta[i];
        GP_SLT = slt;
        CF_ST.magic = CF_MAGIC; CF_ST.file = 20; CF_ST.season = cc.ranked ? 126 : 120;
        for (int i = 0; i < 256; i++) CF_ST.rank[i] = cc.place[i];
        u32 cteam = cc.career < 0 ? 21 : (u32)(20 | cc.career << 8);
        printf("%u", it);
        for (int c = 0; c < 2; c++) {
            u16 out[64];
            int n = cf_build(10 + c, 127, cteam, out);
            printf(" |");
            for (int k = 0; k < n; k++) printf(" %u", out[k] >> 8);
        }
        printf("\n");
    }
    return 0;
}
