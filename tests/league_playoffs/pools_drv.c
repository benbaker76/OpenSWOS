/* The state pools' check (scripts/league_playoffs_check.sh): the Mod's gplay.c ri_season (MOD: its
 * own file, GPLAY_C, with the stubs the check's other programs give it) or ours (src/state_pool.c),
 * over the same table (argv[1]: the Mod's ripool.table_bytes for the world's state pools) and the
 * same states' rankings (argv[2]: the Mod's statesim over its table, a season a line, hex words),
 * N seasons; a manager club among the state-only clubs some seasons; RI_RULE's fallback in others
 * (no ranking). Prints the queues, the overrides and the map after each season. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef MOD
#include GPLAY_C
const u8 SEC_TAB[1] = { 0 };
s8 SEC_SLT[4096], SEC_COPY[4096];
const u8 GP_PROM_TAB[1] = { 0 };
u16 GP_TOPW[9];
u8 GP_TOPR[9 * 684];
const u8 GP_HLT[4] = { 0 };
#define SEASON(t, slt, copy, w, sr, base) ri_season(t, slt, copy, w, sr)
#else
#include "../../src/state_pool.c"
typedef signed char s8;
#define SEASON(t, slt, copy, w, sr, base) ri_season(t, slt, w, sr, base)
#endif

static unsigned char tab[8192], sr[1024];
static signed char slt[2560], cpy[2560];

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    if (!f || !fread(tab, 1, sizeof tab, f)) return 2;
    fclose(f);
    FILE *rk = fopen(argv[2], "r");
    if (!rk) return 2;
    const int world = tab[1], base = tab[4] | tab[5] << 8;
    static char line[1 << 16];
    for (int s = 0; fgets(line, sizeof line, rk); s++) {
        for (int i = 0; i < 2560; i++) slt[i] = cpy[i] = (signed char)((i * 7 + s * 13) & 3);
        cpy[s % 2000] ^= 1;                     /* (a league that moved: a season end) */
        int k = 0;
        for (char *p = strtok(line, " \n"); p; p = strtok(0, " \n")) {
            unsigned w = (unsigned)strtoul(p, 0, 16);
            sr[k++] = (unsigned char)w; sr[k++] = (unsigned char)(w >> 8);
        }
        sr[k] = sr[k + 1] = 0;
        RI_MGR = s % 4 == 2 ? base + 60 + s % 40 : -1;  /* (some seasons a manager among the state-only) */
        RI_RULE = 2;
        SEASON(tab, slt, cpy, world, s % 7 == 5 ? 0 : sr, base);
        printf("season %d: q", s);
        for (int i = 0; i < 64; i++) printf(" %x", RI_STATE.q[i]);
        printf(" qn");
        for (int i = 0; i < 20; i++) printf(" %d", RI_STATE.qn[i]);
        printf(" ovr");
        for (int p = 0; p < 13; p++) for (int j = 0; j < 16; j++) if (RI_STATE.ovr[p][j]) printf(" %d.%d=%x", p, j, RI_STATE.ovr[p][j]);
        printf(" map");
        for (int i = 0; i < 160; i++) if (RI_STATE.maps[i]) printf(" %d=%x", i, RI_STATE.maps[i]);
        printf("\n");
    }
    return 0;
}
