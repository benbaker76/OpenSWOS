/*
 * stoppage.h -- stoppage time: added minutes at the end of each half (src/stoppage.c; the SWOS
 * 96/97 Mod 4.1's tools/stoppage.py, Davide and Marco's; docs/WORLD_2026_27.md, "The Mod 4.1's
 * match rules").
 *
 * At 45:00, 90:00, 105:00 and 120:00 the clock stops and shows "+N": N added minutes, from what
 * the half had -- N = base + (3 goals + 3 substitutions + 2 cards [+ dead-ball ticks / KD]) / 6,
 * capped by the era's cap for that half. They run at the 10-minute match's pace whatever the
 * match length, then the game's own end of the half (its last attack played out). A goal in them
 * is 45+2 / 90+3 in the scorers. No Rand: a deterministic count.
 *
 * In a world other than 96/97 only, with its mod's setting `stoppage_time` (default on). Online
 * it is the HOST's setting (NETPLAY_SW_STOPPAGE, include/netplay_match.h). Its state is match
 * state (sim_state.c), so a rollback, a resync and --srp-resim play it as the match did.
 */
#ifndef STOPPAGE_H
#define STOPPAGE_H

#include "swos_types.h"

/* The state, in the match state registry (sim_state.c): bytes on, phase (0 none, 1 added time
 * running, 2 over -- the game's own end, the clock still showing it), N, the added minute K, the
 * caps (first half, second half, each extra-time half) and the base; words the end minute whose
 * added time was played and the dead-ball divisor KD; longs the half's event count at its start,
 * the dead-ball ticks so far and at the half's start. */
extern uint8_t  g_stoppageB[8];
extern uint16_t g_stoppageW[2];
extern uint32_t g_stoppageL[3];

/* Whether a match starting now plays it (the world, the setting or the host's switch). */
int32_t Stoppage_Wanted(void);

/* UpdateTime's hooks (game_state.c) */
void     Stoppage_Reset(void);             /* ResetTime: a new match */
uint16_t Stoppage_Delta(uint16_t delta);   /* the clock's step: the 10-minute pace in added time */
int32_t  Stoppage_MinuteRoll(void);        /* 1: an added minute, the game's minute stays */
int32_t  Stoppage_Check(void);             /* check_game_end: 1 = in added time, skip the end */
void     Stoppage_Whistle(void);           /* an end-of-half whistle: the game's clock again */
void     Stoppage_DeadBall(uint16_t ticks);/* the ball out of play (game_timers.c) */
/* The clock's units digit shown (the end minute's + K - 1, + N waiting for the whistle), and N
 * (0: no "+N"). */
uint8_t  Stoppage_ClockOnes(uint8_t ones);
uint8_t  Stoppage_ClockPlus(void);
/* A goal now: its added minute for the scorers' list (GoalInfo.time[0]), 0 none. */
uint8_t  Stoppage_GoalMinute(void);

/* SDL: the clock picture with "+N" after it (sprite STOPPAGE_CLOCK_SPRITE), from the clock
 * sprite `clock` the game has just built; the index to show. */
#define STOPPAGE_CLOCK_SPRITE 1304
uint16_t Stoppage_ClockSprite(uint16_t clock);

#endif /* STOPPAGE_H */
