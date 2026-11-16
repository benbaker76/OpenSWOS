/*
 * world_online.c -- online play is SWOS 96/97 whatever world the game plays offline, with no restart
 * (include/world.h, World_SnapshotBase / World_SnapshotWorld / World_OnlineEnter / World_OnlineLeave;
 * Playa, 2026-11-16).
 *
 * A world is installed once, at the start (FrontEndPreLoopSetup): its team numbering
 * (_teamsCountryNumbers), its countries and competitions (pointers in _countriesTable and
 * _competitionsTable: its own records and tables, its cups', a second team file's), its league records
 * written over 96/97's in place (world_data.c install_league) and its nationality codes
 * (_shortCountryNames). Everything else a world changes asks World_Current() each time.
 *
 * So those are kept twice: 96/97's just before the install, the world's after every install of the
 * start (the JSON's, the cups', the team databases'). MULTIPLAYER (menus.c) puts 96/97's back with
 * 96/97 played -- the team list of 96/97's continents, countries, divisions and team files; the
 * server's tag and the handshake 96/97; a match by 96/97's rules -- and the world's again after it.
 * The league records keep their addresses (a career's pointers to them stay good): only their
 * contents change hands. SDL builds with worlds (the Amiga plays no online match).
 */
#include "world.h"

#if defined(SWOS_SDL_BUILD) && SWOS_WORLDS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game_types.h"

extern uint16_t _teamsCountryNumbers[256];
extern void    *_countriesTable[262];
extern void    *_competitionsTable[256];
extern uint8_t  _shortCountryNames[214 * 3];
extern uint8_t  _lastLoadedExtNumber;

#define WO_LEAGUES 256

typedef struct {
    uint16_t teams[256];
    void    *countries[262];
    void    *comps[256];
    uint8_t  nat[214 * 3];
} WorldTables;

static WorldTables       s_base, s_world;
static CompetitionLeague *s_league[WO_LEAGUES];          /* 96/97's league records (their addresses) */
static CompetitionLeague *s_leagueBase, *s_leagueWorld; /* their contents, 96/97's and the world's */
static int32_t           s_nLeagues;
static int32_t           s_state;       /* 0 none, 1 96/97's kept, 2 both kept */
static WorldId           s_offline;     /* the world played offline, while online plays 96/97 */
static int32_t           s_online;      /* swapped to 96/97 */
static int32_t           s_depth;       /* online screens open (one opens another: the first and the last swap) */

/* A country's number (its competitions list [leagues, -2, cups, -1]): 96/97's 0..71, a world's new ones
 * 76..79 and 86..99 -- 72 is the custom teams' table, 80..85 the continents', of other layouts. */
static int32_t is_country(int32_t c)
{
    return (c >= 0 && c < 72) || (c >= 76 && c < 80) || (c >= 86 && c < 100);
}

static void take(WorldTables *t)
{
    memcpy(t->teams, _teamsCountryNumbers, sizeof t->teams);
    memcpy(t->countries, _countriesTable, sizeof t->countries);
    memcpy(t->comps, _competitionsTable, sizeof t->comps);
    memcpy(t->nat, _shortCountryNames, sizeof t->nat);
}

static void put(const WorldTables *t)
{
    memcpy(_teamsCountryNumbers, t->teams, sizeof t->teams);
    memcpy(_countriesTable, t->countries, sizeof t->countries);
    memcpy(_competitionsTable, t->comps, sizeof t->comps);
    memcpy(_shortCountryNames, t->nat, sizeof t->nat);
}

void World_SnapshotBase(void)
{
    s_state = 0;
    if (World_Current() == WORLD_9697)
        return;
    take(&s_base);
    /* every league record of 96/97's tables: [leagues, -2, cups, -1] a country */
    s_nLeagues = 0;
    for (int32_t c = 0; c < 256; c++) {
        if (!is_country(c))
            continue;
        Competition **t = (Competition **)_competitionsTable[c];
        for (int32_t k = 0; t && k < 12 && (intptr_t)t[k] != -1; k++) {
            Competition *p = t[k];
            if ((intptr_t)p == -2 || !p || p->type != 0)
                continue;
            int32_t seen = 0;
            for (int32_t i = 0; i < s_nLeagues && !seen; i++)
                seen = s_league[i] == (CompetitionLeague *)p;
            if (!seen && s_nLeagues < WO_LEAGUES)
                s_league[s_nLeagues++] = (CompetitionLeague *)p;
        }
    }
    s_leagueBase = (CompetitionLeague *)malloc((size_t)s_nLeagues * sizeof *s_leagueBase);
    s_leagueWorld = (CompetitionLeague *)malloc((size_t)s_nLeagues * sizeof *s_leagueWorld);
    if (!s_leagueBase || !s_leagueWorld)
        return;
    for (int32_t i = 0; i < s_nLeagues; i++)
        s_leagueBase[i] = *s_league[i];
    s_state = 1;
}

/* SWOS_TEST_ONLINE_WORLD=1: a digest of what online's team list reads -- the numbering, the
 * countries' names, every country's league divisions and their names -- at the start, online and
 * back: online must be a 96/97 run's, back the start's. */
static uint32_t digest(void)
{
    uint32_t h = 2166136261u;
#define MIX(p, n) do { const uint8_t *q_ = (const uint8_t *)(p); for (size_t i_ = 0; i_ < (size_t)(n); i_++) \
                       { h ^= q_[i_]; h *= 16777619u; } } while (0)
    MIX(_teamsCountryNumbers, sizeof _teamsCountryNumbers);
    for (int32_t c = 0; c < 256; c++) {
        if (!is_country(c))
            continue;
        const uint8_t *e = (const uint8_t *)_countriesTable[c];
        if (e)
            MIX(e + 1, strlen((const char *)e + 1));
        Competition **t = (Competition **)_competitionsTable[c];
        if (!t || (intptr_t)t[0] == -1 || (intptr_t)t[0] == -2 || !t[0] || t[0]->type != 0)
            continue;
        const CompetitionLeague *lg = (const CompetitionLeague *)t[0];
        MIX(&lg->header.divisionsCount, 1);
        for (int32_t d = 0; d < lg->header.divisionsCount && d < LEAGUE_MAX_DIVISIONS; d++)
            if (lg->name[2 * d])
                MIX(lg->name[2 * d], strlen(lg->name[2 * d]));
    }
#undef MIX
    return h;
}

void World_SnapshotWorld(void)
{
    if (getenv("SWOS_TEST_ONLINE_WORLD")) {
        fprintf(stderr, "[world-online] %s at the start: %08x\n", World_Tag(), (unsigned)digest());
        if (s_state == 1) {
            take(&s_world);
            for (int32_t i = 0; i < s_nLeagues; i++)
                s_leagueWorld[i] = *s_league[i];
            s_state = 2;
            World_OnlineEnter();
            fprintf(stderr, "[world-online] online: %08x (%s)\n", (unsigned)digest(), World_Tag());
            World_OnlineLeave();
            fprintf(stderr, "[world-online] back: %08x (%s)\n", (unsigned)digest(), World_Tag());
            return;
        }
    }
    if (s_state != 1)
        return;
    take(&s_world);
    for (int32_t i = 0; i < s_nLeagues; i++)
        s_leagueWorld[i] = *s_league[i];
    s_state = 2;
    fprintf(stderr, "[world] %s and 96/97 kept for online play (%d league records)\n", World_Tag(), (int)s_nLeagues);
}

void World_OnlineEnter(void)
{
    if (s_depth++ > 0 || s_online || s_state != 2 || World_Current() == WORLD_9697)
        return;
    s_offline = World_Current();
    put(&s_base);
    for (int32_t i = 0; i < s_nLeagues; i++)
        *s_league[i] = s_leagueBase[i];
    World_Set("9697");
    _lastLoadedExtNumber = 0xFF;        /* (a team file read is 96/97's: none cached) */
    s_online = 1;
    fprintf(stderr, "[world] online plays 9697 (this game %s, back after)\n", World_TagOf(s_offline));
}

void World_OnlineLeave(void)
{
    if (s_depth > 0)
        s_depth--;
    if (s_depth > 0 || !s_online)
        return;
    put(&s_world);
    for (int32_t i = 0; i < s_nLeagues; i++)
        *s_league[i] = s_leagueWorld[i];
    World_Set(World_TagOf(s_offline));
    _lastLoadedExtNumber = 0xFF;
    s_online = 0;
    fprintf(stderr, "[world] online left: %s again\n", World_Tag());
}
#else
void World_SnapshotBase(void) {}
void World_SnapshotWorld(void) {}
void World_OnlineEnter(void) {}
void World_OnlineLeave(void) {}
#endif
