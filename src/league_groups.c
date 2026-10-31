/*
 * league_groups.c -- leagues with parallel groups at the season end (include/league_groups.h).
 *
 * Ported from the SWOS 96/97 Mod's tools/gplay.c (pg_one, pg_level), which carries this
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
 * into a single division as it is; into a level of groups, the group of the club's area
 * for that level, else the nearest with room, so every group keeps its size. Clubs that
 * stay are counted in their group first, then the movers are placed.
 */
#include "league_groups.h"
#include "team_pool.h"
#include "world.h"

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

/* The level of division d (levels, past the last one: the level below the chain). */
static int32_t level_of(int32_t d, const LeagueGroups *lg)
{
    const LeagueLevel *last = &lg->level[lg->levels - 1];
    if (d >= last->first + last->groups)
        return lg->levels;
    for (int32_t l = lg->levels - 1; l >= 0; l--)
        if (d >= lg->level[l].first)
            return l;
    return 0;
}

static void apply_one(const LeagueGroups *lg, const int8_t *table, int8_t *scratch)
{
    uint8_t occ[LEAGUE_GROUPS_MAX_LEVELS][LEAGUE_GROUPS_MAX_GROUPS] = { { 0 } };
    uint8_t parallel[LEAGUE_GROUPS_MAX_LEVELS];   /* its index among the levels of groups */
    uint8_t pending[WORLD_SLOTS] = { 0 };         /* by club */
    const uint16_t base = _teamsCountryNumbers[lg->country];
    int32_t np = 0;

    if (lg->levels > LEAGUE_GROUPS_MAX_LEVELS || lg->clubs > WORLD_SLOTS)
        return;
    for (int32_t l = 0; l < lg->levels; l++)
        parallel[l] = lg->level[l].groups > 1 ? (uint8_t)np++ : LEAGUE_GROUPS_NO_AREA;

    for (int32_t pass = 0; pass < 2; pass++)
        for (uint16_t i = 0; i < lg->clubs; i++) {
            const LeagueGroupClub *c = &lg->club[i];
            const uint16_t g = (uint16_t)(base + c->slot);
            const int32_t o = table[g] + c->base, n = scratch[g] + c->base;
            const int32_t in = entering(g);     /* a new identity from below: */
            const int32_t lo = in ? lg->levels : level_of(o, lg);
            const int32_t tl = in ? lg->levels - 1 : n < o ? lo - 1 : n > o ? lo + 1 : lo;
            if (pass == 0) {
                if (tl < 0 || tl >= lg->levels)
                    continue;                   /* off the chain: as the game moved it */
                const LeagueLevel *L = &lg->level[tl];
                if (tl == lo) {                 /* staying: counted in its group */
                    if (o - L->first < L->groups)
                        occ[lo][o - L->first]++;
                } else if (L->groups == 1)
                    scratch[g] = (int8_t)(L->first - c->base);
                else
                    pending[i] = 1;
                continue;
            }
            if (!pending[i])
                continue;
            /* Into a level of groups: the area's group, else the nearest with room. */
            const LeagueLevel *L = &lg->level[tl];
            const uint8_t p = parallel[tl];
            const int32_t area = p < 2 ? TeamPool_Area(lg->country, c->slot, p, c->area[p])
                                       : LEAGUE_GROUPS_NO_AREA;
            int32_t a = area < L->groups ? area
                      : area == LEAGUE_GROUPS_NO_AREA ? L->groups / 2 : L->groups - 1;
            int32_t to = a;
            for (int32_t k = 0; k < LEAGUE_GROUPS_MAX_GROUPS; k++) {
                if (a + k < L->groups && occ[tl][a + k] < L->sizes[a + k]) { to = a + k; break; }
                if (a - k >= 0 && occ[tl][a - k] < L->sizes[a - k]) { to = a - k; break; }
            }
            occ[tl][to]++;
            scratch[g] = (int8_t)(L->first + to - c->base);
        }
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
