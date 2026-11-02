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
 * into a single division as it is; into a level of groups, the club's 1st choice of group
 * for that level, then its 2nd (the Mod 4.0.5: Serie C's from where the Lega Pro put each
 * region's clubs), a full group making room by moving a club that may go elsewhere, else the
 * nearest with room, so every group keeps its size. Clubs that stay are counted in their
 * group first, then the movers are placed.
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

/* A club's area byte at a level of groups (the Mod's area_byte; 4.0.5): low nibble its 1st choice
 * (A = 0), high nibble its 2nd + 1 -- 0 or Fh none, Eh any group (a second team), + 8 never another
 * group ('C,B!': a northern club never in C). A raw 0..8 (a pool identity's) is a 1st choice only. */
#define AREA1(b) ((b) & 15)
static int32_t area2(int32_t b)
{
    const int32_t h = b >> 4;
    return h == 0 || h == 15 ? 15 : h == 14 ? 14 : (h & 7) - 1;
}

static int32_t area_banned(int32_t b)
{
    const int32_t h = b >> 4;
    return h != 0 && h < 14 && (h & 8);
}

/* group a, else the nearest one with room (else a) */
static int32_t with_room(int32_t a, const LeagueLevel *L, const uint8_t *occ)
{
    for (int32_t k = 0; k < LEAGUE_GROUPS_MAX_GROUPS; k++) {
        if (a + k < L->groups && occ[a + k] < L->sizes[a + k]) return a + k;
        if (a - k >= 0 && occ[a - k] < L->sizes[a - k]) return a - k;
    }
    return a;
}

/* A club's area byte at level of groups p (its slot's identity's, include/team_pool.h). */
static uint8_t area_of(const LeagueGroups *lg, const LeagueGroupClub *c, uint8_t p)
{
    return p < 2 ? TeamPool_Area(lg->country, c->slot, p, c->area[p]) : LEAGUE_GROUPS_NO_AREA;
}

/* Group a of level tl is full: a club placed in it that can leave makes room (the Mod's
 * pg_make_room) -- a second team first (to the group with room nearest to a), else a club out of
 * its 1st choice whose 1st choice has room, else one out of both choices whose 2nd has room, else
 * one in its 1st choice whose 2nd has room, else a club free to go anywhere (two choices, no ban)
 * to the nearest group with room. 1: one moved. */
static int32_t make_room(const LeagueGroups *lg, uint16_t base, int8_t *scratch, const uint8_t *pending,
                         uint16_t self, int32_t tl, int32_t a, uint8_t (*occ)[LEAGUE_GROUPS_MAX_GROUPS], uint8_t p)
{
    const LeagueLevel *L = &lg->level[tl];
    if (p >= 2)
        return 0;
    for (int32_t pass = 0; pass < 5; pass++)
        for (uint16_t j = 0; j < lg->clubs; j++) {
            const LeagueGroupClub *f = &lg->club[j];
            const uint16_t h = (uint16_t)(base + f->slot);
            if (j == self || pending[j] || scratch[h] + f->base != L->first + a)
                continue;
            const int32_t b = area_of(lg, f, p), h1 = AREA1(b), h2 = area2(b);
            int32_t w = 99;
            if (pass == 0 ? h2 == 14 : pass == 4 && h2 < L->groups && !area_banned(b))
                w = with_room(a, L, occ[tl]);
            else if (h2 != 14)
                w = pass == 1 ? (h1 != a ? h1 : 99) : pass == 2 ? (h1 != a && h2 != a ? h2 : 99)
                  : pass == 3 && h1 == a ? h2 : 99;
            if (w == a || w >= L->groups || occ[tl][w] >= L->sizes[w])
                continue;
            occ[tl][w]++;
            occ[tl][a]--;
            scratch[h] = (int8_t)(L->first + w - f->base);
            return 1;
        }
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
            pending[i] = 0;
            /* Into a level of groups: its 1st choice, then its 2nd -- with room, else room made --
               else the nearest group with room (a second team, or no choice fits). */
            const LeagueLevel *L = &lg->level[tl];
            const uint8_t p = parallel[tl];
            const int32_t ab = area_of(lg, c, p), first = AREA1(ab), second = area2(ab);
            const int32_t a = first < L->groups ? first
                            : ab == LEAGUE_GROUPS_NO_AREA ? L->groups / 2 : L->groups - 1;
            int32_t to = -1;
            for (int32_t k = 0; k < 2 && to < 0; k++) {
                const int32_t w = k ? second : first;
                if (w >= L->groups)
                    continue;
                if (occ[tl][w] < L->sizes[w] || make_room(lg, base, scratch, pending, i, tl, w, occ, p))
                    to = w;
            }
            if (to < 0)
                to = with_room(a, L, occ[tl]);
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
