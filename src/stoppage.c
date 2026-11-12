/*
 * stoppage.c -- stoppage time (include/stoppage.h; docs/WORLD_2026_27.md, "The Mod 4.1's match
 * rules").
 *
 * Ported from the SWOS 96/97 Mod 4.1 (github.com/aminta/swos-9697-mod): tools/stoppage.py, an
 * idea of Davide and Marco's. Copyright (c) 2026 Davide Lorigliola, MIT licence:
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 *   and associated documentation files (the "Software"), to deal in the Software without
 *   restriction, including without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
 *   Software is furnished to do so, subject to the following conditions: The above copyright
 *   notice and this permission notice shall be included in all copies or substantial portions of
 *   the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 *
 * The Mod's hooks into UpdateTime, one for one: st_chk (check_game_end's top), st_min (the minute
 * roll), st_delta (the clock's step), st_wh (the end-of-half whistles), st_dead (the dead-ball
 * ticks), st_goal / st_fmt (a goal's time and its "45+2"), st_img (the clock picture), st_reset
 * (ResetTime). The Mod plays it in every world; OpenSWOS only in a world other than 96/97.
 */
#include "stoppage.h"
#include "world_rules.h"
#include "world.h"
#include "game_types.h"
#ifdef SWOS_SDL_BUILD
#include <stdio.h>
#endif

extern uint8_t  _gameTime[4];
extern uint16_t _gameSeconds, _copyTimeSprite, _gameType, _trainingGame;
extern uint16_t _statsTeam1Goals, _statsTeam2Goals, _team1NumSubs, _team2NumSubs;
extern TeamStatisticsData _team1StatsData, _team2StatsData;
extern uint16_t _gameLenSecondsTable[4];

uint8_t  g_stoppageB[8];
uint16_t g_stoppageW[2];
uint32_t g_stoppageL[3];

#define ST_ON    g_stoppageB[0]
#define ST_PH    g_stoppageB[1]
#define ST_N     g_stoppageB[2]
#define ST_K     g_stoppageB[3]
#define ST_CAPS  (&g_stoppageB[4])        /* first half, second half, each extra-time half, base */
#define ST_AT    g_stoppageW[0]           /* (the end minute, 45 / 90 / 105 / 120) */
#define ST_KD    g_stoppageW[1]
#define ST_SNAP  g_stoppageL[0]
#define ST_DEAD  g_stoppageL[1]
#define ST_DSNAP g_stoppageL[2]

/* The eras (the Mod's era(y)): (cap first half, second half, each extra-time half, base, KD). Caps
 * Marco's, the formula Fable's review: a goal or a substitution ~30 s, a card ~20 s (IFAB's list),
 * in units of 10 s; KD 233 in today's football (~40 s of dead ball a half -> 2 minutes). The Mod
 * picks a career's by its world, any other match the 1996-97 game's. */
static const uint8_t kEraToday[5] = { 3, 5, 1, 1, 233 };
static const uint8_t kEra1996[5]  = { 2, 3, 1, 1, 0 };

#ifdef SWOS_SDL_BUILD
#include "netplay_match.h"
extern int _gArcadeMode;
#endif

int32_t Stoppage_Wanted(void)
{
    if (!WorldRules_World() || _trainingGame)
        return 0;
#ifdef SWOS_SDL_BUILD
    if (_gArcadeMode)
        return 0;
    if (NetplayMatch_IsLive()) {            /* online: the host's, both games alike */
        const int32_t sw = NetplayMatch_HostSwitches();
        return sw >= 0 && (sw & NETPLAY_SW_STOPPAGE) != 0;
    }
#endif
    return WorldRules_On(WRULE_STOPPAGE);
}

static uint16_t minute(void)
{
    return (uint16_t)(_gameTime[1] * 100 + _gameTime[2] * 10 + _gameTime[3]);
}

void Stoppage_Reset(void)
{
    const uint8_t *era = _gameType == 4 && World_FirstYear() >= 2000 ? kEraToday : kEra1996;
    for (int32_t i = 0; i < 8; i++)
        g_stoppageB[i] = 0;
    g_stoppageW[0] = g_stoppageW[1] = 0;
    g_stoppageL[0] = g_stoppageL[1] = g_stoppageL[2] = 0;
    (void)WorldRules_On(WRULE_GK_INJURIES);     /* (the settings read before the match, never in it) */
#ifdef SWOS_SDL_BUILD
    {   /* (the "+N" clock's slot always a sprite: a rollback or resync can show it any time) */
        extern void Stoppage_ClockSlot(void);
        Stoppage_ClockSlot();
    }
#endif
    ST_ON = (uint8_t)(Stoppage_Wanted() != 0);
    if (!ST_ON)
        return;
    for (int32_t i = 0; i < 4; i++)
        ST_CAPS[i] = era[i];
    ST_KD = era[4];
}

uint16_t Stoppage_Delta(uint16_t delta)
{
    return ST_PH == 1 ? _gameLenSecondsTable[3] : delta;    /* (the 10-minute match's pace) */
}

int32_t Stoppage_MinuteRoll(void)
{
    if (ST_PH != 1)
        return 0;
    ST_K++;
    _copyTimeSprite = 1;                    /* (the clock's minute runs) */
    return 1;
}

/* 3 (goals + substitutions made) + 2 cards, both teams: the half's events in 10-second units. */
static uint32_t score(void)
{
    return 3u * (uint32_t)(_statsTeam1Goals + _statsTeam2Goals + _team1NumSubs + _team2NumSubs) +
           2u * (uint32_t)(uint16_t)(_team1StatsData.bookings + _team1StatsData.sendingsOff +
                                     _team2StatsData.bookings + _team2StatsData.sendingsOff);
}

int32_t Stoppage_Check(void)
{
    const uint16_t m = minute();
    int32_t cap;

    if (!ST_ON)
        return 0;
    if (m == 1) {                           /* a match starts */
        ST_PH = 0;
        ST_AT = 0;
        ST_SNAP = score();
        ST_DSNAP = ST_DEAD;
        return 0;
    }
    if (m == 46 || m == 91 || m == 106) {   /* a half's first minute: its dead-ball time from here */
        ST_DSNAP = ST_DEAD;
        return 0;
    }
    if (ST_PH == 1) {
        if (ST_K <= ST_N)
            return 1;
        ST_PH = 2;                          /* over: the game's own end of the half */
        _copyTimeSprite = 1;
        return 0;
    }
    if (ST_PH != 0 || m == ST_AT || (int16_t)_gameSeconds < 0)
        return 0;                           /* (played already; the end counter's pass) */
    if (m == 45)
        cap = 0;
    else if (m == 90)
        cap = 1;
    else if (m == 105 || m == 120)
        cap = 2;
    else
        return 0;
    ST_AT = m;
    {
        const uint32_t now = score();
        uint32_t units = now - ST_SNAP, n;
        ST_SNAP = now;
        if (ST_KD)
            units += (ST_DEAD - ST_DSNAP) / ST_KD;
        n = ST_CAPS[3] + units / 6;
        if (n > ST_CAPS[cap])
            n = ST_CAPS[cap];
        if (!n)
            return 0;                       /* (a cap of 0: the game's end) */
        ST_N = (uint8_t)n;
    }
    ST_K = 1;
    ST_PH = 1;
    _copyTimeSprite = 1;
#ifdef SWOS_SDL_BUILD
    fprintf(stderr, "[stoppage] %u': +%u (events %u, dead-ball ticks %u)\n", (unsigned)m, (unsigned)ST_N,
            (unsigned)(ST_SNAP), (unsigned)(ST_DEAD - ST_DSNAP));
#endif
    return 1;
}

void Stoppage_Whistle(void)
{
    if (!ST_ON)
        return;
    ST_PH = 0;
    _copyTimeSprite = 1;
}

void Stoppage_DeadBall(uint16_t ticks)
{
    ST_DEAD += ticks;
}

uint8_t Stoppage_ClockOnes(uint8_t ones)
{
    return (uint8_t)(ST_PH == 1 ? ones + ST_K - 1 : ST_PH == 2 ? ones + ST_N : ones);
}

uint8_t Stoppage_ClockPlus(void)
{
    return ST_PH ? ST_N : 0;
}

uint8_t Stoppage_GoalMinute(void)
{
    return ST_PH == 1 ? ST_K : ST_PH == 2 ? ST_N : 0;
}

/* ---- the clock picture with "+N" ---- */

#ifdef SWOS_SDL_BUILD
extern uint8_t *_spriteGraphicsPtr;
extern void CopySprite(uint16_t srcIdx, uint16_t dstIdx, uint16_t xOff, uint16_t yOff);

#define CLOCK_DIGIT_0  217                  /* (SPR_CUP_WINNER: the clock's big digits, game_state.c) */
#define CLOCK_W        64
#define CLOCK_LINES    16

static SpriteGraphics s_clock;
static uint8_t s_clockPix[CLOCK_LINES * CLOCK_W];

/* The Mod's '+': (x, y, white 1 / shadow 0) */
static const uint8_t kPlus[][3] = {
    { 2, 2, 1 }, { 2, 3, 1 }, { 3, 3, 0 }, { 0, 4, 1 }, { 1, 4, 1 }, { 2, 4, 1 }, { 3, 4, 1 }, { 4, 4, 1 },
    { 1, 5, 0 }, { 2, 5, 1 }, { 3, 5, 0 }, { 4, 5, 0 }, { 5, 5, 0 }, { 2, 6, 1 }, { 3, 6, 0 }, { 3, 7, 0 },
};

static SpriteGraphics *spr(uint16_t i)
{
    return ((SpriteGraphics **)_spriteGraphicsPtr)[i];
}

/* The clock digits' two colours: the most used (the face) and the next (its shadow). */
static void digit_colours(uint8_t *white, uint8_t *shadow)
{
    uint16_t n[256] = { 0 };
    const SpriteGraphics *d = spr(CLOCK_DIGIT_0 + 8);
    *white = 2;
    *shadow = 8;
    if (!d || !d->sprData)
        return;
    for (int32_t y = 0; y < d->nlines; y++)
        for (int32_t x = 0; x < d->pixWidth; x++)
            n[d->sprData[y * d->wquads * 16 + x]]++;
    n[0] = 0;
    for (int32_t c = 1, best = 0, next = 0; c < 256; c++) {
        if (n[c] > best) {
            next = best;
            *shadow = *white;
            best = n[c];
            *white = (uint8_t)c;
        } else if (n[c] > next) {
            next = n[c];
            *shadow = (uint8_t)c;
        }
    }
}

void Stoppage_ClockSlot(void)
{
    SpriteGraphics **slot = (SpriteGraphics **)_spriteGraphicsPtr;
    if (slot && !slot[STOPPAGE_CLOCK_SPRITE]) {
        s_clock.sprData = s_clockPix;
        s_clock.wquads = CLOCK_W / 16;
        s_clock.ordinal = STOPPAGE_CLOCK_SPRITE;
        slot[STOPPAGE_CLOCK_SPRITE] = &s_clock;
    }
}

uint16_t Stoppage_ClockSprite(uint16_t clock)
{
    const SpriteGraphics *base = spr(clock);
    const uint8_t plus = Stoppage_ClockPlus();
    if (!plus || !base || !base->sprData || base->nlines > CLOCK_LINES || base->pixWidth + 18 > CLOCK_W)
        return clock;
    s_clock = *base;
    s_clock.sprData = s_clockPix;
    s_clock.maskPtr = 0;
    s_clock.wquads = CLOCK_W / 16;
    s_clock.pixWidth = (int16_t)(base->pixWidth + 18);
    s_clock.ordinal = STOPPAGE_CLOCK_SPRITE;
    for (uint32_t i = 0; i < sizeof s_clockPix; i++)
        s_clockPix[i] = 0;
    ((SpriteGraphics **)_spriteGraphicsPtr)[STOPPAGE_CLOCK_SPRITE] = &s_clock;
    CopySprite(clock, STOPPAGE_CLOCK_SPRITE, 0, 0);
    {
        uint8_t white, shadow;
        const int32_t x0 = base->pixWidth + 3;
        digit_colours(&white, &shadow);
        for (uint32_t i = 0; i < sizeof kPlus / sizeof kPlus[0]; i++)
            if (kPlus[i][1] < s_clock.nlines)
                s_clockPix[kPlus[i][1] * CLOCK_W + x0 + kPlus[i][0]] = kPlus[i][2] ? white : shadow;
    }
    if (plus <= 9)
        CopySprite((uint16_t)(CLOCK_DIGIT_0 + plus), STOPPAGE_CLOCK_SPRITE,
                   (uint16_t)((base->pixWidth + 10) & ~1), 0);
    return STOPPAGE_CLOCK_SPRITE;
}
#else
uint16_t Stoppage_ClockSprite(uint16_t clock)
{
    return clock;           /* (the Amiga: the running minute, without the "+N") */
}
#endif
