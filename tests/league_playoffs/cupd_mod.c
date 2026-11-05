/* The Mod's cd_rank, cd_season, cd_make (tools/gplay.c, included as it is). */
#include <stdio.h>
#include <stdlib.h>
#include GPLAY_C
#include "cupd_cases.h"
const u8 SEC_TAB[1] = { 0 };
s8 SEC_SLT[4096], SEC_COPY[4096];
const u8 GP_PROM_TAB[1] = { 0 };
u16 GP_TOPW[9];
u8 GP_TOPR[9 * 684];
const u8 GP_HLT[4] = { 0 };
u8 GP_SEAS;
static u8 tab[16], dt[4 + 256];
static s8 slt[512], copy[512];
int main(int argc, char **argv)
{
    unsigned N = argc > 1 ? (unsigned)atoi(argv[1]) : 20000;
    struct cd_case cc;
    tab[0] = 5; tab[1] = CD_ROUNDS; tab[2] = 20;
    GP_CDT = tab;
    for (unsigned it = 0; it < N; it++) {
        make_cd_case(it, &cc);
        dt[0] = CD_FIRST; dt[1] = CD_GROUPS; dt[2] = 0; dt[3] = 0;
        for (int i = 0; i < 256; i++) dt[4 + i] = cc.base[i];
        GP_CDD = dt;
        for (int d = 0; d < GD; d++) GP_IN.n[d] = 0;
        for (int d = 0; d < 14; d++) {
            GP_IN.n[d] = (u8)cc.n[d];
            for (int k = 0; k < cc.n[d]; k++) GP_IN.t[d][k].team = (u16)(20 | cc.order[d][k] << 8);
        }
        CD_ST.magic = 0x4443;
        cd_rank();
        for (int i = 0; i < 256; i++) { slt[i] = cc.last[i]; copy[i] = cc.next[i]; }
        RI_FILES_FILE[0] = 20;
        for (int i = 0; i < 32; i++) RI_SWAPPED[0][i] = cc.arrival[i];
        cd_season(tab, dt, slt, copy, 5);
        GP_SLT = copy;
        GP_SEAS = (u8)cc.season;
        GP_CDFIRST = 0;
        cd_make();
        printf("%u %u:", it, CD_ST.np);
        for (int t = 0; t < CD_ST.np + 64; t++) printf(" %04x.%04x", CD_ST.gen[2 * t], CD_ST.gen[2 * t + 1]);
        printf("\n");
    }
    return 0;
}
