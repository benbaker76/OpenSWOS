/*
 * cpu_repl.c -- CPU replacements from the squad (include/cpu_repl.h).
 *
 * Ported from the SWOS 96/97 Mod 4.1's tools/cpurefill.py (github.com/aminta/swos-9697-mod).
 * Copyright (c) 2026 Davide Lorigliola, MIT licence:
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 *   and associated documentation files (the "Software"), to deal in the Software without
 *   restriction, including without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
 *   Software is furnished to do so, subject to the following conditions: The above copyright
 *   notice and this permission notice shall be included in all copies or substantial portions of
 *   the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 */
#include "cpu_repl.h"
#include "world.h"
#include "game_types.h"
#include "menu_system.h"
#ifdef SWOS_SDL_BUILD
#include <stdio.h>
#endif

extern uint16_t _gameType;
extern MenuEntry _menuEntries[];

#define GAME_TYPE_CAREER 4
#define PF_OFF 76                               /* (GetPlayer's record: the player 76 bytes in) */

CpuReplState g_cpuRepl;

void CpuRepl_LoadedWithout(void)
{
    CpuReplState z = { 0 };
    g_cpuRepl = z;                              /* INVENTED, nothing used */
}

void CpuRepl_NewCareer(void)
{
    extern const char *ModApi_Setting(const char *slug, const char *key, const char *fallback);
    CpuRepl_LoadedWithout();
    /* the world mod's setting (the Mod: a new career starts INVENTED) */
    const char *v = ModApi_Setting("world2627", "cpu_replacements", "invented");
    g_cpuRepl.fromSquad = v && (v[0] == 'f' || v[0] == 'F');
}

#if defined(SWOS_SDL_BUILD) && SWOS_WORLDS
extern uint8_t *GetPlayer(uint16_t teamNumber, uint16_t playerFileIndex);
extern int32_t  Career_WriteTeamPlayer(uint16_t teamId, uint16_t slot, const PlayerFile *player);
extern int32_t  DataJson_LoadTeamExtras(int32_t file, int32_t ordinal, PlayerFile *out, int32_t max);

#define EXTRAS_MAX 16

int32_t CpuRepl_Shown(void)
{
    return World_Current() != WORLD_9697 && _gameType == GAME_TYPE_CAREER;
}

/* The role group (cpurefill.py: positionAndFace >> 5 = 0 / 1-3 / 4-6 / 7). */
static int32_t role(const PlayerFile *p)
{
    const int32_t pos = (p->positionAndFace & PF_POSITION_MASK) >> 5;
    return pos == 0 ? 0 : pos <= 3 ? 1 : pos <= 6 ? 2 : 3;
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

static uint16_t *used_mask(uint16_t team, int32_t make)
{
    for (int32_t i = 0; i < g_cpuRepl.n; i++)
        if (g_cpuRepl.used[i].team == team)
            return &g_cpuRepl.used[i].mask;
    if (!make)
        return 0;
    if (g_cpuRepl.n == CPU_REPL_CLUBS) {        /* (full: the oldest club's record goes) */
        for (int32_t i = 1; i < CPU_REPL_CLUBS; i++)
            g_cpuRepl.used[i - 1] = g_cpuRepl.used[i];
        g_cpuRepl.n--;
    }
    g_cpuRepl.used[g_cpuRepl.n].team = team;
    g_cpuRepl.used[g_cpuRepl.n].mask = 0;
    return &g_cpuRepl.used[g_cpuRepl.n++].mask;
}

/* The pick (cpurefill.py): the same role's most valuable, else any outfield player's (a goalkeeper
 * only for a goalkeeper), the lowest place on a tie; used players and names in the 16 skipped
 * (squad: the club's 16 -- NULL: not checked). -1: none. */
static int32_t pick(const PlayerFile *extras, int32_t n, int32_t want, uint16_t used, uint16_t team,
                    uint16_t slot, int32_t squad)
{
    int32_t best = -1, bestSame = 0;
    for (int32_t i = 0; i < n; i++) {
        const PlayerFile *c = &extras[i];
        if (!c->playerName[0] || (used >> i & 1))
            continue;
        const int32_t r = role(c);
        if ((want == 0) != (r == 0))            /* a goalkeeper only for a goalkeeper */
            continue;
        int32_t taken = 0;                      /* (a name already in the club's 16) */
        for (uint16_t k = 0; squad && k < 16 && !taken; k++)
            if (k != slot)
                taken = same_name(((const PlayerFile *)(GetPlayer(team, k) + PF_OFF))->playerName, c->playerName);
        if (taken)
            continue;
        const int32_t same = r == want;
        if (best < 0 || same > bestSame ||
            (same == bestSame && (uint8_t)c->price > (uint8_t)extras[best].price))
            best = i, bestSame = same;
    }
    return best;
}

int32_t CpuRepl_AfterSale(uint16_t team, uint16_t slot)
{
    if (!g_cpuRepl.fromSquad || !CpuRepl_Shown() || slot >= 16)
        return 0;
    PlayerFile extras[EXTRAS_MAX];
    const int32_t n = DataJson_LoadTeamExtras(team >> 8, team & 0xFF, extras, EXTRAS_MAX);
    if (n <= 0)
        return 0;
    const PlayerFile *hole = (const PlayerFile *)(GetPlayer(team, slot) + PF_OFF);
    const uint16_t *used = used_mask(team, 0);
    /* (the invented player keeps the sold one's position) */
    const int32_t best = pick(extras, n, role(hole), used ? *used : 0, team, slot, 1);
    if (best < 0)
        return 0;
    PlayerFile p = extras[best];
    p.goalStats[0] = p.goalStats[1] = p.goalStats[2] = p.goalStats[3] = 0;
    p.cardsInjuries = 0;
    if (Career_WriteTeamPlayer(team, slot, &p) != 0)
        return 0;
    *used_mask(team, 1) |= (uint16_t)(1u << best);
    fprintf(stderr, "[cpu-repl] %04x slot %u: %.22s from its squad (place %d)\n", (unsigned)team,
            (unsigned)slot, (const char *)p.playerName, (int)(17 + best));
    return 1;
}

/* --cpu-repl-selftest=<file>,<ordinal>: a club's squad players 17.. as the world's team file has
 * them, and whom each role would take (none used, the 16 not checked). */
int32_t CpuRepl_SelfTest(int32_t file, int32_t ordinal)
{
    static const char *const kRole[4] = { "GK", "DEF", "MID", "FWD" };
    PlayerFile extras[EXTRAS_MAX];
    const int32_t n = DataJson_LoadTeamExtras(file, ordinal, extras, EXTRAS_MAX);
    printf("[cpu-repl] world %s, club %d/%d: %d squad players from place 17\n", World_Tag(), (int)file,
           (int)ordinal, (int)n);
    for (int32_t i = 0; i < n; i++)
        printf("  %2d  %-22.22s %-3s price %d\n", (int)(17 + i), (const char *)extras[i].playerName,
               kRole[role(&extras[i])], (int)(uint8_t)extras[i].price);
    for (int32_t r = 0; r < 4; r++) {
        const int32_t b = pick(extras, n, r, 0, 0, 0, 0);
        printf("  a %s sold: %s\n", kRole[r], b < 0 ? "(invented)" : (const char *)extras[b].playerName);
    }
    return n > 0 ? 0 : 1;
}
#else
int32_t CpuRepl_Shown(void)
{
    return 0;
}

int32_t CpuRepl_AfterSale(uint16_t team, uint16_t slot)
{
    (void)team; (void)slot;
    return 0;
}
#endif

/* BUY PLAYER's button (entry 9, right of EXIT): the choice for this career. */
static void label(void)
{
    static const char *const k[2] = { "CPU REPL.: INVENTED", "CPU REPL.: FROM SQUAD" };
    _menuEntries[9].fg = (uintptr_t)k[g_cpuRepl.fromSquad ? 1 : 0];
}

void CpuRepl_MenuInit(void)
{
    if (!CpuRepl_Shown())
        return;
    _menuEntries[9].isInvisible = 0;
    _menuEntries[7].rightEntry = 9;
    label();
}

__attribute__((used, externally_visible))
void BuyPlayerCpuReplacements(void)
{
    g_cpuRepl.fromSquad ^= 1;
    label();
    DrawMenuItem(&_menuEntries[9]);
}
