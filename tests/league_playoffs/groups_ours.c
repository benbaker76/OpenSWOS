/* Our parallel groups (src/league_groups.c) over groups_cases.h's season ends: the Mod's table as
 * our LeagueGroups (slots from the lowest global number), an entering club's areas through
 * TeamPool_Area as the pool gives them. Arguments: TABLE PROMREL SEASONS. */
#include "league_groups.h"
#include "groups_cases.h"

uint16_t _teamsCountryNumbers[256];
static LeagueLevel s_level[GC_PL];
static LeagueGroupClub s_club[GC_CLUBS];
static LeagueGroups s_lg;
static int s_gmin;

const LeagueGroups *World_LeagueGroups(int32_t *count)
{
    *count = 1;
    return &s_lg;
}

/* the slot's identity's area: this season's table (an entering club: its new identity's) */
uint8_t TeamPool_Area(uint8_t country, uint16_t slot, int32_t p, uint8_t own)
{
    (void)country;
    for (int i = 0; i < gc_nc; i++)
        if (gc_g[i] - s_gmin == slot)
            return p < gc_npar ? gc_run[gc_area_at[i] + p] : own;
    return own;
}

int main(int argc, char **argv)
{
    if (argc < 4) return 2;
    gc_load(argv[1], argv[2]);
    s_gmin = 0xFFFF;
    for (int i = 0; i < gc_nc; i++) if (gc_g[i] < s_gmin) s_gmin = gc_g[i];
    for (int l = 0; l < gc_nl; l++) {
        s_level[l].first = (uint8_t)gc_first[l];
        s_level[l].groups = (uint8_t)gc_cnt[l];
        for (int k = 0; k < gc_cnt[l] && k < LEAGUE_GROUPS_MAX_GROUPS; k++) s_level[l].sizes[k] = (uint8_t)gc_size[gc_first[l] + k];
    }
    for (int i = 0; i < gc_nc; i++) {
        s_club[i].slot = (uint16_t)(gc_g[i] - s_gmin);
        s_club[i].base = (uint8_t)gc_base[i];
        s_club[i].area[0] = gc_npar > 0 ? gc_tab[gc_area_at[i]] : LEAGUE_GROUPS_NO_AREA;
        s_club[i].area[1] = gc_npar > 1 ? gc_tab[gc_area_at[i] + 1] : LEAGUE_GROUPS_NO_AREA;
    }
    s_lg.country = 1; s_lg.levels = (uint8_t)gc_nl; s_lg.level = s_level; s_lg.clubs = (uint16_t)gc_nc; s_lg.club = s_club;
    _teamsCountryNumbers[1] = (uint16_t)s_gmin;
    for (unsigned it = 0, n = (unsigned)atoi(argv[3]); it < n; it++) {
        gc_season(it);
        for (int i = 0; i < gc_nenter; i++) LeagueGroups_Enter((uint16_t)gc_g[gc_enter[i]]);
        LeagueGroups_Apply(gc_slt, gc_copy);
        gc_print(it);
    }
    return 0;
}
