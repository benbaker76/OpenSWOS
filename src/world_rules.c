/*
 * world_rules.c -- the SWOS 96/97 Mod 4.1's match and bench rules in a world other than 96/97
 * (include/world_rules.h; docs/WORLD_2026_27.md, "The Mod 4.1's match rules").
 *
 * Ported from the SWOS 96/97 Mod 4.1 (github.com/aminta/swos-9697-mod): tools/cpucards.py (the
 * computer's red cards and injuries) and tools/gkinj.py (goalkeeper injuries). Copyright (c) 2026
 * Davide Lorigliola, MIT licence:
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 *   and associated documentation files (the "Software"), to deal in the Software without
 *   restriction, including without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
 *   Software is furnished to do so, subject to the following conditions: The above copyright
 *   notice and this permission notice shall be included in all copies or substantial portions of
 *   the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 *
 * The Mod plays them in every world, 1996-97 included; OpenSWOS only in a world other than
 * 96/97, whose matches stay the original game's.
 */
#include "world_rules.h"
#include "world.h"
#include "mod_api.h"
#include "game_types.h"
#include "endian_helpers.h"

extern uint16_t _gameType, _trainingGame;
extern uint8_t  _seed, _randXorIndex, _randXorKey, _seed2, _randXorIndex2, _randXorKey2;
extern TeamFileEx _careerTeam;
extern uint8_t  _currentMatchPlayers[];
extern uint8_t *GetPlayer(uint16_t teamNumber, uint16_t playerFileIndex);

#define TEAM_COMPUTER 1

static const char *const kKeys[WRULE_COUNT] = {
    "stoppage_time", "cpu_red_cards", "cpu_injuries", "goalkeeper_injuries", "bench_info",
    "opponent_squad",
};

int32_t WorldRules_World(void)
{
    return SWOS_WORLDS && World_Current() != WORLD_9697;
}

const char *WorldRules_Key(int32_t r)
{
    return (uint32_t)r < WRULE_COUNT ? kKeys[r] : "";
}

/* The world mod's slug: "world" + the world's tag (scripts/make_world_package.sh). */
static const char *slug(void)
{
    static char s[16];
    const char *t = World_Tag();
    int32_t n = 0;
    const char *w = "world";
    while (*w) s[n++] = *w++;
    while (*t && n < (int32_t)sizeof s - 1) s[n++] = *t++;
    s[n] = '\0';
    return s;
}

int32_t WorldRules_On(int32_t r)
{
    if (!WorldRules_World() || (uint32_t)r >= WRULE_COUNT)
        return 0;
    return ModApi_SettingInt(slug(), kKeys[r], 1) != 0;     /* (every one ON by default, as the Mod) */
}

/* ---- the career's copies ---- */

#define CAREER_RULES ((1u << WRULE_CPU_CARDS) | (1u << WRULE_CPU_INJURIES) | (1u << WRULE_GK_INJURIES))

uint32_t WorldRules_CareerBits(void)
{
    uint32_t b = 0;
    for (int32_t r = 0; r < WRULE_COUNT; r++)
        if ((CAREER_RULES & (1u << r)) && WorldRules_On(r))
            b |= 1u << r;
    return b;
}

void WorldRules_CareerLoaded(int32_t have, uint32_t bits)
{
    if (!WorldRules_World())
        return;
    for (int32_t r = 0; r < WRULE_COUNT; r++) {
        if (!(CAREER_RULES & (1u << r)))
            continue;
        const int32_t on = have ? (bits & (1u << r)) != 0 : 1;   /* (an older career: ON, as the Mod's) */
        if (WorldRules_On(r) != on)
            ModApi_SetSetting(slug(), kKeys[r], on ? "on" : "off");
    }
}

/* ---- CPU RED CARDS / CPU INJURIES (the Mod's cpucards.py) ---- */

extern uint16_t   _numSelectedTeams, _plgD3Param;
extern TeamFileEx _selectedTeams[];
extern void CareerStatus_CountdownTeam(TeamFileEx *team, uint16_t mode);

static uint8_t s_inDay;                 /* (WorldRules_CpuDay running: its countdowns open) */

int32_t WorldRules_CpuOpen(int32_t r)
{
    return WorldRules_On(r);
}

int32_t WorldRules_CpuAny(void)
{
    return WorldRules_On(WRULE_CPU_CARDS) || WorldRules_On(WRULE_CPU_INJURIES);
}

int32_t WorldRules_CpuCountdown(int32_t r)
{
    if (s_inDay)
        return 1;
    if (!WorldRules_On(r))
        return 0;
    return _gameType == 1 || _gameType == 2;    /* (a DIY / preset competition: after each match) */
}

void WorldRules_CpuDay(void)
{
    if (!WorldRules_CpuAny())
        return;
    s_inDay = 1;
    for (uint32_t i = 0; i < _numSelectedTeams && i < SELECTED_TEAMS_MAX; i++)
        if (_selectedTeams[i].team.teamControls == TEAM_COMPUTER)
            CareerStatus_CountdownTeam(&_selectedTeams[i], _plgD3Param);
    s_inDay = 0;
}

/* Player byte `ci` (cardsInjuries) cannot play under card mode `mode` (IsPlayerInjured's test). */
static int32_t unavailable(uint8_t ci, int16_t mode)
{
    if (mode != 1) {
        const uint8_t y = ci & 0x1C;
        if (y && y != 4)
            return 1;               /* (yellow cards: suspended) */
    }
    if (mode == 1 || mode < 0)
        if (ci & 3)
            return 1;               /* (a red card's suspension) */
    const uint8_t inj = ci & 0xE0;
    return inj && inj != 0x20;
}

typedef struct { TeamFileEx *team; uint8_t order[16]; } LineupSlot;
static LineupSlot s_lineups[2][2];

static void swap_unavailable(LineupSlot *slot, TeamFileEx *t, int16_t mode)
{
    slot->team = 0;
    if (!t || t->team.teamControls != TEAM_COMPUTER || !WorldRules_CpuAny())
        return;
    slot->team = t;
    for (int32_t i = 0; i < 16; i++)
        slot->order[i] = t->team.playerNumbers[i];
    uint8_t *order = t->team.playerNumbers;
    for (int32_t s = 0; s < 11; s++) {
        const PlayerFile *p = &t->players[order[s] & 15];
        if (!unavailable(p->cardsInjuries, mode))
            continue;
        const int32_t keeper = (p->positionAndFace & PF_POSITION_MASK) == 0;
        for (int32_t any = 0; any < 2; any++) {
            int32_t j;
            for (j = 11; j < 16; j++) {
                const PlayerFile *q = &t->players[order[j] & 15];
                if (unavailable(q->cardsInjuries, mode))
                    continue;
                if (!any && ((q->positionAndFace & PF_POSITION_MASK) == 0) != keeper)
                    continue;
                break;
            }
            if (j < 16) {
                const uint8_t x = order[s];
                order[s] = order[j];
                order[j] = x;
                break;
            }
        }
    }
}

void WorldRules_CpuLineupsBegin(int32_t which, TeamFileEx *a, TeamFileEx *b, int16_t mode)
{
    if (!WorldRules_World())
        return;
    swap_unavailable(&s_lineups[which & 1][0], a, mode);
    swap_unavailable(&s_lineups[which & 1][1], b, mode);
}

void WorldRules_CpuLineupsEnd(int32_t which)
{
    for (int32_t k = 0; k < 2; k++) {
        LineupSlot *slot = &s_lineups[which & 1][k];
        if (!slot->team)
            continue;
        for (int32_t i = 0; i < 16; i++)
            slot->team->team.playerNumbers[i] = slot->order[i];
        slot->team = 0;
    }
}

/* ---- goalkeeper injuries (the Mod's gkinj.py) ---- */

#define GK_ODDS 25                      /* 1 in 25 per team per match */
#define GK_LVL1 0x40                    /* out of the next match */
#define GK_LVL2 0x60                    /* ... the next two */

uint32_t g_gkInjurySeed;

void WorldRules_NewCareer(void)
{
    g_gkInjurySeed = 0;
}

/* The next xorshift32, seeded at the first roll from the game's two generators' states -- read,
 * not drawn, so the game's own sequence is untouched (the Mod seeded it from the BIOS tick). */
static uint32_t gk_rng(void)
{
    uint32_t x = g_gkInjurySeed;
    if (!x) {
        x = ((uint32_t)_seed | (uint32_t)_randXorIndex << 8 | (uint32_t)_randXorKey << 16) ^
            (((uint32_t)_seed2 | (uint32_t)_randXorIndex2 << 8 | (uint32_t)_randXorKey2 << 16) << 8) ^
            0x9E3779B9u;
        if (!x)
            x = 1;
    }
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_gkInjurySeed = x;
    return x;
}

/* The roll for keeper record `pf` of `team`. */
static void gk_apply(PlayerFile *pf, const TeamFileEx *team)
{
    if (!WorldRules_On(WRULE_GK_INJURIES))
        return;
    /* the computer's keeper: only with CPU INJURIES on, and in a competition (a friendly has no
     * next match; the modes that count the computer's injuries down) */
    if (team->team.teamControls == TEAM_COMPUTER && (!WorldRules_On(WRULE_CPU_INJURIES) || _gameType < 1))
        return;
    if (pf->cardsInjuries & 0xE0)       /* (already injured: no roll) */
        return;
    if (gk_rng() % GK_ODDS != 0)
        return;
    pf->cardsInjuries = (uint8_t)((pf->cardsInjuries & 0x1F) | ((gk_rng() & 0x10000u) ? GK_LVL2 : GK_LVL1));
}

void WorldRules_GkAfterMatch(TeamGame *tg, TeamFileEx *team)
{
    const PlayerInfo *pi = &tg->players[0];     /* (the keeper on the pitch at the final whistle: a
                                                 *  substitution swaps the whole PlayerInfo) */
    if (!WorldRules_World() || _trainingGame || pi->skip || pi->isInjured)
        return;
    uint8_t *base = GetPlayer(PACK_BE_U16(team->team.countryNumber, team->team.teamOrdinal),
                              (uint16_t)(int8_t)pi->playerIndex);
    gk_apply((PlayerFile *)(base + sizeof(TeamFile)), team);
}

void WorldRules_GkAfterSim(TeamFileEx *team, const uint8_t *remap)
{
    (void)remap;
    if (!WorldRules_World() || _trainingGame)
        return;
    /* the line-up's first: the career club's match order, any other the file's */
    const int32_t idx = (int8_t)(team == &_careerTeam ? _currentMatchPlayers[0] : team->team.playerNumbers[0]);
    if (idx < 0 || idx > 15)
        return;
    gk_apply(&team->players[idx], team);
}
