/*
 * state_pool.h -- a world's state pools at the season end (src/state_pool.c, the Mod's gplay.c
 * ri_season, its 'state' pools; docs/WORLD_2026_27.md).
 */
#ifndef STATE_POOL_H
#define STATE_POOL_H

#include "swos_types.h"

#define STATE_POOL_FILES   8            /* (gplay.c RF, RP, RQT, RO, RMT) */
#define STATE_POOL_POOLS   20
#define STATE_POOL_QUEUE   192
#define STATE_POOL_ENTRIES 16
#define STATE_POOL_MAP     640

/* The career's pools (the Mod's RI_STATE, saved): per file its start, per pool its queue's length
 * and its contest's overrides, the queues one after another, per file its slots' identities. */
typedef struct {
    uint8_t  init[STATE_POOL_FILES];
    uint8_t  qn[STATE_POOL_POOLS];
    uint16_t q[STATE_POOL_QUEUE];
    uint16_t ovr[STATE_POOL_POOLS][STATE_POOL_ENTRIES];
    uint16_t maps[STATE_POOL_MAP];
} StatePoolState;

extern StatePoolState RI_STATE;
extern uint8_t RI_SWAPPED[STATE_POOL_FILES][256 / 8];
extern int RI_MGR, RI_RULE;

/* One season end: t the pools' table, slt the season's league table (the draws' seed: its first
 * 2000 bytes), world its world, sr the states' rankings, baseOverride the states' file's first
 * global number (-1: the table's). */
void ri_season(const unsigned char *t, const signed char *slt, int world, const unsigned char *sr, int baseOverride);

#endif
