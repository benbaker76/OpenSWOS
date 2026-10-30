/*
 * states.c -- a world's state championships at the season end (include/states.h; docs/
 * WORLD_2026_27.md): Brazil's estaduais, as the SWOS 96/97 Mod's statesim / ripool glue runs
 * them (its ss_entry, ri_season, st_post): the simulation's table with the contests' overrides
 * (a club that came in through a spare in the place of a league club relegated from its state),
 * every state's stage 1 and knockouts simulated (state_sim.c), then the state pools' swaps
 * (state_pool.c): the identities of the state-only clubs' slots, into the country's file
 * (team_pool.c). The career club's own state is simulated too, until it plays it (its block 0).
 *
 * The tables' team words are the Mod's two files' (World_States): a league club the country's
 * number and its ordinal, a state-only club statesFile and its ordinal among them (ours:
 * firstState + it).
 */
#include "states.h"
#include "state_sim.h"
#include "state_pool.h"
#include "team_pool.h"
#include "world.h"
#include "game_types.h"

#if SWOS_WORLDS

extern uint16_t _teamsCountryNumbers[256];
extern uint8_t _diyLeagueSeasonStateTable[];
extern TeamFileEx _careerTeam;

uint16_t g_stateChampions[STATE_SIM_STATES];    /* (last season's: each state's champion, our team word) */
uint16_t g_stateSeasons;                        /* (the season ends simulated: the season word's, saved) */
static uint8_t s_tab[4096];
static uint8_t s_rank[STATE_SIM_RANK + 16];

static uint16_t ours(const WorldStates *ws, uint16_t w)
{
    if (!w)
        return 0;
    const uint8_t file = (uint8_t)w, o = (uint8_t)(w >> 8);
    return (uint16_t)(ws->country << 8 | (file == ws->statesFile ? ws->firstState + o : o));
}

void States_Reset(void)
{
    uint8_t *p = (uint8_t *)&RI_STATE;
    for (uint32_t i = 0; i < sizeof RI_STATE; i++)
        p[i] = 0;
    for (int32_t s = 0; s < STATE_SIM_STATES; s++)
        g_stateChampions[s] = 0;
    g_stateSeasons = 0;
}

void States_SeasonEnd(uint8_t country)
{
    const WorldStates *ws = World_States(country);
    if (!ws || ws->simBytes > sizeof s_tab)
        return;
    /* the table, each state's entries as its contest has them now (the Mod's st_ovr) */
    for (uint32_t i = 0; i < ws->simBytes; i++)
        s_tab[i] = ws->sim[i];
    for (int32_t s = 0; s < ws->states && s < STATE_POOL_POOLS; s++) {
        uint8_t *w = s_tab + ws->wordsAt[s];
        const int32_t n = w[-5];                /* (+2 of the 7-byte header: its clubs) */
        for (int32_t j = 0; j < n && j < STATE_POOL_ENTRIES; j++)
            if (RI_STATE.ovr[s][j]) {
                w[2 * j] = (uint8_t)RI_STATE.ovr[s][j];
                w[2 * j + 1] = (uint8_t)(RI_STATE.ovr[s][j] >> 8);
            }
    }
    for (uint32_t i = 0; i < sizeof s_rank; i++)
        s_rank[i] = 0;
    ss_season(s_tab, s_rank, (uint16_t)(2026 + g_stateSeasons++));
    for (int32_t s = 0; s < ws->states && s < STATE_SIM_STATES; s++)
        g_stateChampions[s] = ours(ws, ss_champ[s]);

    /* the pools: the identities of the states' file's slots, into ours */
    const int32_t base = _teamsCountryNumbers[country] + ws->firstState;
    RI_MGR = (int16_t)_careerTeam.team.globalTeamNumber;
    ri_season(ws->pools, (const signed char *)_diyLeagueSeasonStateTable, ws->world, s_rank, base);
    const int32_t nmap = ws->pools[8] | ws->pools[9] << 8;
    int32_t swapped = 0;
    for (int32_t i = 0; i < nmap && ws->firstState + i < 256; i++)
        if (RI_SWAPPED[0][i >> 3] & (1 << (i & 7))) {
            const int32_t id = RI_STATE.maps[i] ? RI_STATE.maps[i] - 1 : i;
            TeamPool_Assign(country, (uint8_t)(ws->firstState + i),
                            (uint16_t)(id < 256 ? ws->firstState + id : id));
            swapped++;
        }
    if (swapped)
        TeamPool_SwapsDone();
    SWOS_DBG("[states] country %d: %d states simulated, %d slots swapped; champions", (int)country, (int)ws->states,
             (int)swapped);
    for (int32_t s = 0; s < ws->states && s < STATE_SIM_STATES; s++)
        SWOS_DBG(" %04X", (unsigned)g_stateChampions[s]);
    SWOS_DBG("\n");
}

#else

void States_Reset(void) { }
void States_SeasonEnd(uint8_t country) { (void)country; }

#endif /* SWOS_WORLDS */
