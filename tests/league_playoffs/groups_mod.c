/* The Mod's parallel groups (tools/gplay.c pg_season, included as it is) over groups_cases.h's season
 * ends. Arguments: TABLE PROMREL SEASONS. */
#include GPLAY_C
#include "groups_cases.h"
const u8 SEC_TAB[1] = { 0 };
s8 SEC_SLT[4096], SEC_COPY[4096];
const u8 GP_PROM_TAB[1] = { 0 };
u16 GP_TOPW[9];
u8 GP_TOPR[9 * 684];
const u8 GP_HLT[4] = { 0 };

int main(int argc, char **argv)
{
    if (argc < 4) return 2;
    gc_load(argv[1], argv[2]);
    for (unsigned it = 0, n = (unsigned)atoi(argv[3]); it < n; it++) {
        gc_season(it);
        ri_no = gc_nenter;
        for (int i = 0; i < gc_nenter; i++) ri_og[i] = (u16)gc_g[gc_enter[i]];
        pg_season(gc_run, gc_slt, gc_copy, gc_world);
        gc_print(it);
    }
    return 0;
}
