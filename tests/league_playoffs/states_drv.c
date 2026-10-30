/* The state championships' check (scripts/league_playoffs_check.sh): the Mod's tools/statesim.c
 * (MOD: its own file, linked beside this one -- it reads SS_TAB, ST_RANK and SEASONW as the DOS
 * program's globals) or ours (src/state_sim.c, which takes them as arguments) over the same
 * table (argv[1]: the Mod's statespec.sim_table for the world's packs), N seasons; in some, the
 * first state played (block 0: its clubs in a drawn order). Prints every block, every champion,
 * every stage-1 order. */
#include <stdio.h>
#include <stdlib.h>

typedef unsigned char u8;
typedef unsigned short u16;
#ifdef MOD
u8 SS_TAB[8192];
u8 ST_RANK[1024];
u16 SEASONW;
extern void ss_season(void);
extern u16 ss_champ[13];
extern u8 ss_ord[13 * 16];
#define RUN() ss_season()
#else
#include "../../src/state_sim.c"
static u8 SS_TAB[8192], ST_RANK[1024];
static u16 SEASONW;
#define RUN() ss_season(SS_TAB, ST_RANK, SEASONW)
#endif

static unsigned r_seed = 12345;
static unsigned r_below(unsigned n) { r_seed = r_seed * 1664525u + 1013904223u; return (r_seed >> 8) % n; }

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    int seasons = argc > 2 ? atoi(argv[2]) : 50;
    if (!f || !fread(SS_TAB, 1, sizeof SS_TAB, f)) { fprintf(stderr, "no table\n"); return 2; }
    fclose(f);
    for (int s = 0; s < seasons; s++) {
        SEASONW = (u16)(1997 + s);
        for (unsigned i = 0; i < sizeof ST_RANK; i++) ST_RANK[i] = 0;
        if (s % 3 == 1) {                       /* the first state played: its clubs in a drawn order */
            int n = SS_TAB[3], ord[16];
            for (int i = 0; i < n; i++) ord[i] = i;
            for (int i = n - 1; i > 0; i--) { int j = (int)r_below((unsigned)i + 1), x = ord[i]; ord[i] = ord[j]; ord[j] = x; }
            ST_RANK[0] = SS_TAB[1]; ST_RANK[1] = SS_TAB[2]; ST_RANK[2] = (u8)n; ST_RANK[3] = 0;
            for (int i = 0; i < n; i++) {
                ST_RANK[4 + 2 * i] = SS_TAB[8 + 2 * ord[i]];
                ST_RANK[5 + 2 * i] = SS_TAB[9 + 2 * ord[i]];
            }
        }
        RUN();
        printf("season %d:", s);
        for (const u8 *b = ST_RANK; (b[0] | b[1] << 8) && b < ST_RANK + sizeof ST_RANK - 4; b += 4 + 2 * b[2]) {
            printf(" [%x", b[0] | b[1] << 8);
            for (int i = 0; i < b[2]; i++) printf(" %x", b[4 + 2 * i] | b[5 + 2 * i] << 8);
            printf("]");
        }
        printf(" champ");
        for (int k = 0; k < 13; k++) printf(" %x", ss_champ[k]);
        printf(" ord");
        for (int k = 0; k < 13 * 16; k++) printf(" %d", ss_ord[k]);
        printf("\n");
    }
    return 0;
}
