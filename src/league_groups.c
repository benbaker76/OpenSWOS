/*
 * league_groups.c -- leagues with parallel groups at the season end (include/league_groups.h).
 *
 * The placement is the rules library's sr_pg_season (third_party/swos-competition-engine,
 * rules/sr_groups.c, its rules 11 -- moved there from here, checked against the Mod's C); this is
 * the game's side of it: the world's tables, the pool's swaps, the new offsets back. It was
 * ported from the SWOS 96/97 Mod's tools/gplay.c (pg_one, pg_level), which carries this
 * notice:
 *
 *   Copyright (c) 2026 Davide Lorigliola
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of this
 *   software and associated documentation files (the "Software"), to deal in the Software
 *   without restriction, including without limitation the rights to use, copy, modify,
 *   merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 *   permit persons to whom the Software is furnished to do so, subject to the following
 *   conditions:
 *
 *   The above copyright notice and this permission notice shall be included in all copies
 *   or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 *   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
 *   PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 *   HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 *   CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
 *   THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Changed in the port: our typed tables instead of the Mod's packed byte table and DOS
 * addresses; clubs named by their slot (the global number is the country's base plus
 * it: include/world.h); the off-file pool's swapped slots (the Mod's ri_over) enter the lowest level from
 * below, placed by their new identity's area (include/team_pool.h, LeagueGroups_Enter).
 *
 * How: the game has moved every club one division up or down its chain (its divisions'
 * promoted / relegated counts), as the original does. Here each move becomes one LEVEL:
 * into a single division as it is; into a level of groups, the club's 1st choice of group
 * for that level, then its 2nd (the Mod 4.0.5: Serie C's from where the Lega Pro put each
 * region's clubs), a full group making room by moving a club that may go elsewhere, else the
 * nearest with room, so every group keeps its size. Clubs that stay are counted in their
 * group first, then the movers are placed.
 */
#include "league_groups.h"
#include "team_pool.h"
#include "world.h"
#include "../third_party/swos-competition-engine/rules/swosrules.h"

extern uint16_t _teamsCountryNumbers[256];

static uint16_t s_enter[64];            /* this season end's slots entering from below */
static int32_t  s_entering;

void LeagueGroups_Enter(uint16_t globalNumber)
{
    if (s_entering < (int32_t)(sizeof s_enter / sizeof s_enter[0]))
        s_enter[s_entering++] = globalNumber;
}

static int32_t entering(uint16_t g)
{
    for (int32_t i = 0; i < s_entering; i++)
        if (s_enter[i] == g)
            return 1;
    return 0;
}

/* One league's season end: its table as the rules library's (a club by its index in it, its area
 * bytes this season's -- a swapped pool slot's its new identity's), sr_pg_season, the new offsets
 * back. */
static void apply_one(const LeagueGroups *lg, const int8_t *table, int8_t *scratch)
{
    static sr_pg_level level[SR_PG_LEVELS];
    static sr_pg_club club[WORLD_SLOTS];
    static int8_t old_off[WORLD_SLOTS], new_off[WORLD_SLOTS];
    static uint8_t enter[WORLD_SLOTS];
    const uint16_t base = _teamsCountryNumbers[lg->country];
    sr_pg_league pg;

    if (lg->levels > SR_PG_LEVELS || lg->clubs > WORLD_SLOTS)
        return;
    for (int32_t l = 0; l < lg->levels; l++) {
        const LeagueLevel *L = &lg->level[l];
        level[l].first = L->first;
        level[l].groups = L->groups;
        for (int32_t g = 0; g < SR_PG_GROUPS; g++)
            level[l].size[g] = g < L->groups && g < LEAGUE_GROUPS_MAX_GROUPS ? L->sizes[g] : 0;
    }
    for (uint16_t i = 0; i < lg->clubs; i++) {
        const LeagueGroupClub *c = &lg->club[i];
        const uint16_t g = (uint16_t)(base + c->slot);
        club[i].base = c->base;
        for (uint8_t p = 0; p < SR_PG_PARALLEL; p++)
            club[i].area[p] = p < 2 ? TeamPool_Area(lg->country, c->slot, p, c->area[p]) : LEAGUE_GROUPS_NO_AREA;
        old_off[i] = table[g];
        new_off[i] = scratch[g];
        enter[i] = (uint8_t)entering(g);
    }
    pg.levels = lg->levels;
    pg.level = level;
    pg.clubs = lg->clubs;
    pg.club = club;
    if (sr_pg_season(&pg, old_off, new_off, enter) != 0) {
        SWOS_DBG("[groups] country %d: refused by the rules library (sr_pg_season)\n", lg->country);
        return;
    }
    for (uint16_t i = 0; i < lg->clubs; i++)
        scratch[base + lg->club[i].slot] = new_off[i];
}

const LeagueGroups *LeagueGroups_ForCountry(uint16_t country)
{
    int32_t n = 0;
    const LeagueGroups *lg = World_LeagueGroups(&n);
    for (int32_t i = 0; i < n; i++)
        if (lg[i].country == country)
            return &lg[i];
    return 0;
}

void LeagueGroups_Apply(const int8_t *table, int8_t *scratch)
{
    int32_t n = 0;
    const LeagueGroups *lg = World_LeagueGroups(&n);
    for (int32_t i = 0; i < n; i++)
        apply_one(&lg[i], table, scratch);
    s_entering = 0;
}
