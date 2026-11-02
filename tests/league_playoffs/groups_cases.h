/* The parallel groups' season ends, shared by groups_mod.c (the Mod's gplay.c pg_season) and
 * groups_ours.c (src/league_groups.c): one country's table as the Mod's pgroups.table_bytes writes it
 * (count 1), its divisions' promoted / relegated counts, and random season ends over it -- every club
 * in a division (the divisions filled to their sizes, the rest out of the chain), each division's
 * table in random order, its promoted one division up and its relegated one down (the game's moves);
 * on odd seasons the clubs relegated out of the chain come back in as new identities from below (the
 * off-file pool's swaps: the Mod's ri_og, our LeagueGroups_Enter), each with another club's areas --
 * every fourth season all with ONE club's (a group crowded past its room: the 2nd choices, and the
 * clubs that make room, all used).
 * Each side prints every club's new offset per season. Arguments: TABLE PROMREL SEASONS. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { GC_PL = 8, GC_PG = 9, GC_CLUBS = 600, GC_DIVS = 64 };
static unsigned char gc_tab[1 << 16], gc_run[1 << 16];   /* the table; this season's (entering areas) */
static int gc_len, gc_world, gc_nl, gc_first[GC_PL], gc_cnt[GC_PL], gc_size[GC_DIVS], gc_npar, gc_nc, gc_nd;
static int gc_g[GC_CLUBS], gc_base[GC_CLUBS], gc_area_at[GC_CLUBS];   /* (gc_area_at: the areas' offset) */
static int gc_prom[GC_DIVS], gc_rel[GC_DIVS];
static signed char gc_slt[4096], gc_copy[4096];
static int gc_enter[64], gc_nenter;
static unsigned gc_seed;

static unsigned gc_rand(unsigned n)
{
    gc_seed = gc_seed * 1103515245u + 12345u;
    return (gc_seed >> 8) % n;
}

static void gc_load(const char *table, const char *promrel)
{
    FILE *f = fopen(table, "rb");
    if (!f || (gc_len = (int)fread(gc_tab, 1, sizeof gc_tab, f)) < 3) { fprintf(stderr, "no table %s\n", table); exit(2); }
    fclose(f);
    const unsigned char *p = gc_tab + 1;
    gc_world = p[0]; gc_nl = p[1]; p += 2;
    for (int l = 0; l < gc_nl; l++) {
        gc_first[l] = p[0]; gc_cnt[l] = p[1];
        for (int k = 0; k < p[1]; k++) gc_size[p[0] + k] = p[2 + k];
        p += 2 + p[1];
    }
    gc_nd = gc_first[gc_nl - 1] + gc_cnt[gc_nl - 1];
    gc_npar = p[0]; gc_nc = p[1] | p[2] << 8; p += 3;
    for (int i = 0; i < gc_nc; i++, p += 3 + gc_npar) {
        gc_g[i] = p[0] | p[1] << 8; gc_base[i] = p[2]; gc_area_at[i] = (int)(p + 3 - gc_tab);
    }
    f = fopen(promrel, "r");
    for (int d = 0; f && d < gc_nd; d++) if (fscanf(f, "%d", &gc_prom[d]) != 1) break;
    for (int d = 0; f && d < gc_nd; d++) if (fscanf(f, "%d", &gc_rel[d]) != 1) break;
    if (f) fclose(f);
}

/* season it: gc_slt (the old offsets), gc_copy (the game's moves), the entering clubs, gc_run */
static void gc_season(unsigned it)
{
    int order[GC_CLUBS], div[GC_CLUBS], at = 0;
    gc_seed = it * 2654435761u + 7;
    for (int i = 0; i < gc_nc; i++) order[i] = i;
    for (int i = gc_nc - 1; i > 0; i--) { int j = (int)gc_rand((unsigned)i + 1), x = order[i]; order[i] = order[j]; order[j] = x; }
    for (int d = 0; d < gc_nd; d++)             /* (filled to their sizes, in the shuffled order) */
        for (int k = 0; k < gc_size[d] && at < gc_nc; k++) div[order[at++]] = d;
    while (at < gc_nc) div[order[at++]] = gc_nd;
    memset(gc_slt, 0, sizeof gc_slt);
    memset(gc_copy, 0, sizeof gc_copy);
    for (int i = 0; i < gc_nc; i++) gc_slt[gc_g[i]] = gc_copy[gc_g[i]] = (signed char)(div[i] - gc_base[i]);
    memcpy(gc_run, gc_tab, (size_t)gc_len);
    gc_nenter = 0;
    const int crowd = (it & 3) == 3 ? (int)gc_rand((unsigned)gc_nc) : -1;
    for (int d = 0; d < gc_nd; d++) {           /* (its table: the clubs of d in the shuffled order) */
        int in[GC_CLUBS], n = 0;
        for (int k = 0; k < gc_nc; k++) if (div[order[k]] == d) in[n++] = order[k];
        int up = d > 0 ? gc_prom[d] : 0, down = gc_rel[d];
        if (up + down > n) { up = n / 2; down = n - up; }
        for (int k = 0; k < up; k++) gc_copy[gc_g[in[k]]] = (signed char)(d - 1 - gc_base[in[k]]);
        for (int k = n - down; k < n; k++) {
            const int c = in[k];
            gc_copy[gc_g[c]] = (signed char)(d + 1 - gc_base[c]);
            if (d + 1 == gc_nd && (it & 1) && gc_nenter < 64) {   /* (back as a new identity: another club's areas) */
                const int o = crowd >= 0 ? crowd : (int)gc_rand((unsigned)gc_nc);
                gc_enter[gc_nenter++] = c;
                memcpy(gc_run + gc_area_at[c], gc_tab + gc_area_at[o], (size_t)gc_npar);
            }
        }
    }
}

static void gc_print(unsigned it)
{
    printf("%u", it);
    for (int i = 0; i < gc_nc; i++) printf(" %d", gc_copy[gc_g[i]]);
    printf("\n");
}
