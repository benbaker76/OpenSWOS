/*
 * squad_extras.c -- a world's squads beyond 16 in the career (include/squad_extras.h).
 *
 * Ported from the SWOS 96/97 Mod's tools/xtra.py (4.0.2), xbrowse.py and xleft.py (4.0.6)
 * (github.com/aminta/swos-9697-mod). Copyright (c) 2026 Davide Lorigliola, MIT licence:
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 *   and associated documentation files (the "Software"), to deal in the Software without
 *   restriction, including without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
 *   Software is furnished to do so, subject to the following conditions: The above copyright
 *   notice and this permission notice shall be included in all copies or substantial portions of
 *   the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 *
 * What the Mod patches into the DOS exe, done here in the game's own routines' C:
 *   xtra_load    -> SquadExtras_ClubLoaded   (the club's block into careerTeam's places 17..)
 *   xtra_reserves-> SquadExtras_TakeAdded    (no invented reserves for a club that got its own)
 *   xtra_wage    -> SquadExtras_IsOwnExtra   (66 %, by name against the club's block)
 *   gp_wide / mp_a6 / mp_a1 -> SquadExtras_GetPlayer (GetPlayer and the market's rows, index 16..)
 *   xt_pick      -> SquadExtras_MarketPick
 *   xt_sold, xl_sold, xl_rm, xl_job -> SquadExtras_Bought / _OwnLeaving / _JobChange
 *   xbrowse      -> transfer_market.c's BUY OTHER PLAYERS page 2 (SquadExtras_Nth)
 */
#include "squad_extras.h"
#include "bteam.h"
#include "cpu_repl.h"
#include "world.h"

#include <string.h>
#ifdef SWOS_SDL_BUILD
#include <stdio.h>
#include <stdlib.h>
#endif

SqxState g_sqx;

extern uint16_t   _gameType, _isNationalSelector, _selTeamNumber;
extern uint16_t   _careerNumPlayers;
extern TeamFileEx _careerTeam;
extern uint8_t    _currentMatchPlayers[];
extern uint16_t   Rand2(void);

#define GAME_TYPE_CAREER 4
#define PF_OFF           76

static uint8_t s_added;                 /* the club just loaded got its extras (xtra.py's xadded) */

int32_t SquadExtras_On(void)
{
#if defined(SWOS_SDL_BUILD) && SWOS_WORLDS
    return World_Current() != WORLD_9697 && _gameType == GAME_TYPE_CAREER && !_isNationalSelector;
#else
    return 0;
#endif
}

static int32_t same_name(const int8_t *a, const int8_t *b)
{
    for (int32_t i = 0; i < 23; i++) {
        if (a[i] != b[i])
            return 0;
        if (!a[i])
            return 1;
    }
    return 1;
}

static void copy_name(int8_t *dst, const int8_t *src)
{
    int32_t i = 0;
    for (; i < 22 && src[i]; i++)
        dst[i] = src[i];
    for (; i < 23; i++)
        dst[i] = 0;
}

#if defined(SWOS_SDL_BUILD) && SWOS_WORLDS

extern int32_t DataJson_LoadTeamExtras(int32_t file, int32_t ordinal, PlayerFile *out, int32_t max);

/* A few clubs' blocks, read from their team file once (xtra.py's xcache: round robin). */
#define NCACHE 8
typedef struct {
    uint16_t   team;
    uint8_t    valid, n;                /* n: the block's players (gone ones counted: the places stay) */
    uint32_t   gen;                     /* the departures' generation its `gone` was worked out for */
    uint16_t   gone;                    /* bit i: the block's player i is no longer there */
    int8_t     world;
    PlayerFile p[SQX_MAX];
} Block;
static Block    s_cache[NCACHE];
static uint8_t  s_rr;
static uint32_t s_gen = 1;              /* bumped by every departure / load */

/* (GetPlayer's view of a player who is not there: an empty record, no name) */
static uint8_t s_blank[PF_OFF + sizeof(PlayerFile)];

void SquadExtras_Changed(void)
{
    if (++s_gen == 0)
        s_gen = 1;
}

/* Where the departed list holds this player (by place and name, else by name: a newer package
 * may have moved him), or -1. */
static int32_t left_index(const Block *b, const SqxLeft *e)
{
    const int32_t at = (int32_t)e->index - 16;
    if (at >= 0 && at < b->n && same_name(b->p[at].playerName, e->name))
        return at;
    for (int32_t i = 0; i < b->n; i++)
        if (same_name(b->p[i].playerName, e->name))
            return i;
    return -1;
}

static void work_out_gone(Block *b)
{
    uint16_t gone = 0;
    for (uint16_t k = 0; k < g_sqx.n; k++)
        if (g_sqx.left[k].team == b->team) {
            const int32_t i = left_index(b, &g_sqx.left[k]);
            if (i >= 0)
                gone |= (uint16_t)(1u << i);
        }
    for (int32_t i = 0; i < b->n; i++) {
        if (!b->p[i].playerName[0])
            gone |= (uint16_t)(1u << i);
        if (BTeam_Holds(b->team, (uint16_t)(16 + i)))   /* (called up from a B team: bteam.c) */
            gone |= (uint16_t)(1u << i);
    }
    for (int32_t i = 0; i < g_cpuRepl.n; i++)           /* (in its 16 now: cpu_repl.c) */
        if (g_cpuRepl.used[i].team == b->team)
            gone |= g_cpuRepl.used[i].mask;
    b->gone = gone;
    b->gen = s_gen;
}

static Block *block(uint16_t team)
{
    const int8_t w = (int8_t)World_Current();
    for (int32_t i = 0; i < NCACHE; i++) {
        Block *b = &s_cache[i];
        if (b->valid && b->team == team && b->world == w) {
            if (b->gen != s_gen)
                work_out_gone(b);
            return b;
        }
    }
    Block *b = &s_cache[s_rr];
    s_rr = (uint8_t)((s_rr + 1) % NCACHE);
    memset(b, 0, sizeof *b);
    b->team = team;
    b->world = w;
    b->valid = 1;
    const int32_t n = DataJson_LoadTeamExtras(team >> 8, team & 0xFF, b->p, SQX_MAX);
    b->n = (uint8_t)(n > 0 ? n : 0);
    work_out_gone(b);
    return b;
}

void SquadExtras_ClubLoaded(void)
{
    s_added = 0;
    if (!SquadExtras_On())
        return;
    const Block *b = block(_selTeamNumber);
    int32_t added = 0;
    for (int32_t i = 0; i < b->n && _careerNumPlayers < 16 + SQX_MAX; i++) {
        const PlayerFile *p = &b->p[i];
        /* listed as gone (bought, left the club, written into its record at a job change), or a
         * name already in the record just copied (xleft.py xl_read) */
        if ((b->gone >> i) & 1)
            continue;
        int32_t taken = 0;
        for (int32_t k = 0; k < 16 && !taken; k++)
            taken = same_name(_careerTeam.players[k].playerName, p->playerName);
        if (taken)
            continue;
        const uint16_t slot = _careerNumPlayers;
        *(PlayerFile *)((uint8_t *)&_careerTeam + PF_OFF + slot * sizeof(PlayerFile)) = *p;
        _currentMatchPlayers[slot] = (uint8_t)slot;
        _careerNumPlayers++;
        added++;
    }
    s_added = (uint8_t)(added > 0);
    if (added)
        fprintf(stderr, "[squads] %04x: %d of its players 17.. join the squad (%u players)\n",
                (unsigned)_selTeamNumber, (int)added, (unsigned)_careerNumPlayers);
}

int32_t SquadExtras_IsOwnExtra(const PlayerFile *p)
{
    if (!SquadExtras_On() || !p->playerName[0])
        return 0;
    const Block *b = block(_selTeamNumber);
    for (int32_t i = 0; i < b->n; i++)
        if (same_name(b->p[i].playerName, p->playerName))
            return 1;
    return 0;
}

int32_t SquadExtras_Count(uint16_t team)
{
    if (!SquadExtras_On() || team == _selTeamNumber)
        return 0;
    return block(team)->n;
}

PlayerFile *SquadExtras_Player(uint16_t team, uint16_t index)
{
    if (!SquadExtras_On() || index < 16 || index >= 16 + SQX_MAX || team == _selTeamNumber)
        return NULL;
    Block *b = block(team);
    const int32_t i = index - 16;
    return i < b->n && !((b->gone >> i) & 1) ? &b->p[i] : NULL;
}

uint8_t *SquadExtras_GetPlayer(uint16_t team, uint16_t index)
{
    if (!SquadExtras_On() || index < 16)
        return NULL;
    PlayerFile *p = SquadExtras_Player(team, index);
    if (p)
        return (uint8_t *)p - PF_OFF;
    memset(s_blank, 0, sizeof s_blank);
    return s_blank;
}

PlayerFile *SquadExtras_BenchPlayer(uint16_t team, int32_t n, uint16_t *index)
{
    if (World_Current() == WORLD_9697 || n < 0)
        return NULL;
    Block *b = block(team);
    const int32_t career = SquadExtras_On();
    for (int32_t i = 0; i < b->n; i++) {
        if (!b->p[i].playerName[0] || (career && ((b->gone >> i) & 1)))
            continue;
        if (n-- == 0) {
            *index = (uint16_t)(16 + i);
            return &b->p[i];
        }
    }
    return NULL;
}

int32_t SquadExtras_Checksum(uint16_t team, uint8_t out[8])
{
    extern void TeamDb_ChecksumPlayers(const PlayerFile *players, int32_t count, char out[17]);
    PlayerFile p[SQX_MAX];
    char text[17];
    if (World_Current() == WORLD_9697)
        return 0;
    const int32_t n = DataJson_LoadTeamExtras(team >> 8, team & 0xFF, p, SQX_MAX);
    if (n <= 0)
        return 0;
    TeamDb_ChecksumPlayers(p, n, text);
    for (int32_t i = 0; i < 8; i++) {
        unsigned v = 0;
        sscanf(text + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
    return 1;
}

PlayerFile *SquadExtras_BenchRecord(uint16_t team, uint16_t index)
{
    if (World_Current() == WORLD_9697 || index < 16 || index >= 16 + SQX_MAX)
        return NULL;
    Block *b = block(team);
    const int32_t i = index - 16;
    return i < b->n && b->p[i].playerName[0] ? &b->p[i] : NULL;
}

int32_t SquadExtras_Nth(uint16_t team, int32_t n)
{
    if (!SquadExtras_On() || team == _selTeamNumber)
        return -1;
    const Block *b = block(team);
    for (int32_t i = 0; i < b->n; i++)
        if (!((b->gone >> i) & 1) && n-- == 0)
            return 16 + i;
    return -1;
}

uint16_t SquadExtras_MarketPick(uint16_t team, uint16_t pick)
{
    if (!SquadExtras_On() || team == _selTeamNumber)
        return pick;
    const Block *b = block(team);
    if (!b->n)
        return pick;                    /* (no extras: no draw, the game as it was) */
    const uint16_t k = (uint16_t)((Rand2() & 0xFF) % (16u + b->n));
    if (k < 16 || ((b->gone >> (k - 16)) & 1))
        return pick;
    return k;
}

void SquadExtras_Left(uint16_t team, uint16_t index, const int8_t *name)
{
    if (index < 16 || index >= 16 + SQX_MAX)
        return;
    for (uint16_t k = 0; k < g_sqx.n; k++)
        if (g_sqx.left[k].team == team && g_sqx.left[k].index == index && same_name(g_sqx.left[k].name, name))
            return;
    SqxLeft *e;
    if (g_sqx.n < SQX_LEFT_MAX) {
        e = &g_sqx.left[g_sqx.n++];
    } else {                            /* (full: the oldest entry goes, xleft.py's ring) */
        e = &g_sqx.left[g_sqx.ring];
        g_sqx.ring = (uint16_t)((g_sqx.ring + 1) % SQX_LEFT_MAX);
    }
    e->team = team;
    e->index = (uint8_t)index;
    copy_name(e->name, name);
    SquadExtras_Changed();
    fprintf(stderr, "[squads] %04x place %u: %.22s is gone from the club's players 17..\n",
            (unsigned)team, (unsigned)(index + 1), (const char *)name);
}

/* The managed club's own extras: where his name is in its block (16..), or -1. */
static int32_t own_index(const int8_t *name)
{
    const Block *b = block(_selTeamNumber);
    for (int32_t i = 0; i < b->n; i++)
        if (same_name(b->p[i].playerName, name))
            return 16 + i;
    return -1;
}

void SquadExtras_OwnLeaving(uint16_t slot)
{
    extern uint8_t *GetPlayer(uint16_t teamNumber, uint16_t playerFileIndex);
    if (!SquadExtras_On() || slot >= 32)
        return;
    const PlayerFile *p = (const PlayerFile *)(GetPlayer(_selTeamNumber, slot) + PF_OFF);
    if (!p->playerName[0])
        return;
    const int32_t at = own_index(p->playerName);
    if (at >= 0)
        SquadExtras_Left(_selTeamNumber, (uint16_t)at, p->playerName);
}

void SquadExtras_JobChange(void)
{
    extern uint8_t *GetPlayerAtIndex(uint16_t teamNumber, uint16_t ordinal);
    if (!SquadExtras_On())
        return;
    for (uint16_t k = 0; k < 16 && k < _careerNumPlayers; k++) {
        const PlayerFile *p = (const PlayerFile *)(GetPlayerAtIndex(_selTeamNumber, k) + PF_OFF);
        const int32_t at = p->playerName[0] ? own_index(p->playerName) : -1;
        if (at >= 0)                    /* (in the record now: his copy 17.. must not come back) */
            SquadExtras_Left(_selTeamNumber, (uint16_t)at, p->playerName);
    }
}

int32_t SquadExtras_Bought(uint16_t team, uint16_t index, const int8_t *name)
{
    if (index < 16)
        return 0;
    SquadExtras_Left(team, index, name);
    return 1;
}

/* Test (SWOS_TEST_BUY_EXTRA=1): at the first in-season hub the national market's first player 17..
 * is bought -- the squad's side of a purchase (Career_RemovePlayerAndAddAsTrialist), no money --
 * so a golden career shows him join, his club get no replacement and him never offered again. */
void SquadExtras_TestRequests(void)
{
    extern uint8_t  _homeMarketPlayers[];
    extern uint16_t _numHomeMarketPlayers;
    extern void Career_RemovePlayerAndAddAsTrialist(uint16_t sourceTeam, uint16_t playerOrd);
    static int32_t done, viewed;
    if (!viewed && getenv("SWOS_TEST_VIEWSQUAD") && SquadExtras_On()) {
        /* (test: the squad view of the national market's first club with players 17..: its page 2,
         *  menus.c ViewSquadInit photographs it) */
        extern uint8_t  _homeMarketPlayers[];
        extern uint16_t _numHomeMarketPlayers;
        extern int16_t ShowSquad(TeamFile *teamPtr, uint8_t *title, uint32_t maskLo, uint32_t maskHi,
                                 uint16_t arg2, uint16_t arg3);
        extern TeamFileEx *GetTeamPointer(uint16_t teamId);
        viewed = 1;
        for (uint16_t i = 0; i < _numHomeMarketPlayers; i++) {
            const uint8_t *e = &_homeMarketPlayers[i * 4];
            const uint16_t team = (uint16_t)(e[0] << 8 | e[1]);
            if (team != _selTeamNumber && SquadExtras_Nth(team, 0) >= 0) {
                fprintf(stderr, "[squads] test: the squad view of %04x, page 2\n", (unsigned)team);
                ShowSquad((TeamFile *)GetTeamPointer(team), (uint8_t *)"VIEW SQUAD", 0, 0, 1, 0xFFFF);
                break;
            }
        }
    }
    if (done || !getenv("SWOS_TEST_BUY_EXTRA") || !SquadExtras_On())
        return;
    done = 1;
    for (uint16_t i = 0; i < _numHomeMarketPlayers; i++) {
        const uint8_t *e = &_homeMarketPlayers[i * 4];
        if (e[2] >= 16 && _careerNumPlayers < 30) {
            const uint16_t team = (uint16_t)(e[0] << 8 | e[1]), at = e[2];
            const PlayerFile *p = SquadExtras_Player(team, at);
            fprintf(stderr, "[squads] test purchase: %.22s (%04x place %u)\n", p ? (const char *)p->playerName : "?",
                    (unsigned)team, (unsigned)at + 1);
            Career_RemovePlayerAndAddAsTrialist(team, at);
            return;
        }
    }
    fprintf(stderr, "[squads] test purchase: no player 17.. on the national market\n");
}

int32_t SquadExtras_SelfTest(int32_t file, int32_t ordinal)
{
    const uint16_t team = (uint16_t)(file << 8 | ordinal);
    PlayerFile p[SQX_MAX];
    const int32_t n = DataJson_LoadTeamExtras(file, ordinal, p, SQX_MAX);
    printf("[squads] world %s, club %d/%d: %d players from place 17\n", World_Tag(), (int)file, (int)ordinal,
           (int)n);
    for (int32_t i = 0; i < n; i++)
        printf("  %2d  %-22.22s price %3d  shirt %2d\n", (int)(17 + i), (const char *)p[i].playerName,
               (int)(uint8_t)p[i].price, (int)(uint8_t)p[i].shirtNumber);
    (void)team;
    return n > 0 ? 0 : 1;
}

#else   /* the Amiga, or no worlds: the team files hold 16 */

void SquadExtras_Changed(void) {}
void SquadExtras_ClubLoaded(void) { s_added = 0; }
int32_t SquadExtras_IsOwnExtra(const PlayerFile *p) { (void)p; return 0; }
int32_t SquadExtras_Count(uint16_t team) { (void)team; return 0; }
PlayerFile *SquadExtras_Player(uint16_t team, uint16_t index) { (void)team; (void)index; return 0; }
uint8_t *SquadExtras_GetPlayer(uint16_t team, uint16_t index) { (void)team; (void)index; return 0; }
int32_t SquadExtras_Nth(uint16_t team, int32_t n) { (void)team; (void)n; return -1; }
PlayerFile *SquadExtras_BenchPlayer(uint16_t team, int32_t n, uint16_t *index)
{
    (void)team; (void)n; (void)index;
    return 0;
}
PlayerFile *SquadExtras_BenchRecord(uint16_t team, uint16_t index) { (void)team; (void)index; return 0; }
int32_t SquadExtras_Checksum(uint16_t team, uint8_t out[8]) { (void)team; (void)out; return 0; }
uint16_t SquadExtras_MarketPick(uint16_t team, uint16_t pick) { (void)team; return pick; }
void SquadExtras_Left(uint16_t team, uint16_t index, const int8_t *name)
{
    (void)team; (void)index; (void)name; (void)copy_name; (void)same_name;
}
void SquadExtras_OwnLeaving(uint16_t slot) { (void)slot; }
void SquadExtras_JobChange(void) {}
int32_t SquadExtras_Bought(uint16_t team, uint16_t index, const int8_t *name)
{
    (void)team; (void)name;
    return index >= 16;
}
int32_t SquadExtras_SelfTest(int32_t file, int32_t ordinal) { (void)file; (void)ordinal; return 1; }
void SquadExtras_TestRequests(void) {}

#endif

void SquadExtras_Reset(void)
{
    g_sqx.n = g_sqx.ring = 0;
    SquadExtras_Changed();
}

int32_t SquadExtras_TakeAdded(void)
{
    const int32_t a = s_added;
    s_added = 0;
    return a;
}
