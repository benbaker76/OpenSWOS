/*
 * team_pool.h -- the off-file pool below a world's lowest division (src/team_pool.c;
 * docs/WORLD_2026_27.md, phase 4).
 *
 * A world's team file can be full -- World 2026-27's Italy holds its 256 clubs, Serie A to
 * Serie D -- so a club relegated out of the lowest division cannot leave its slot for one
 * coming up: there is no free slot. Its slot keeps its place, its global number, its
 * division, and takes another club's IDENTITY instead: the next of a queue of clubs below
 * the league (the Mod's Eccellenza pool, ITA_POOL.json), the club going down joining the
 * queue's end. A slot's identity is a record -- name, kits, coach, players -- either the
 * file's own for another slot, or one of the pool's.
 *
 * The identities live in a career: the map and the queue are saved with it, applied to the
 * team file each time a career loads it, and to every record of the slot the career holds
 * (its competitions' teams, the clubs it has changed). Outside a career the file is as
 * shipped.
 */
#ifndef TEAM_POOL_H
#define TEAM_POOL_H

#include "swos_types.h"

/* A world's pool (src/world_data.c, generated): the team file (country) it serves, its own
 * file (data/TEAM.<file>, or the world's teams/<json>), its clubs and their areas at the
 * levels of groups (include/league_groups.h), in the queue's first order. */
typedef struct {
    uint8_t        country;
    uint8_t        file;
    const char    *json;
    uint8_t        count;
    const uint8_t (*area)[2];
} TeamPool;

const TeamPool *World_TeamPools(int32_t *count);

#define TEAM_POOL_SLOTS 256     /* a team file's (TEAM_FILE_MAX_TEAMS) */
#define TEAM_POOL_QUEUE 64

/* The career's identities of one pooled file: map[slot] 0 = its own record, else the
 * identity + 1 (an identity < 256 is that slot's own record of the file, 256 + k the
 * pool's k-th club); the queue of identities waiting below the league. One per pooled
 * country (World 2026-27: Italy's Eccellenza; Spain's, Germany's, the Netherlands' and
 * Portugal's, which take B teams forced out of the lowest division), as the Mod's per-file
 * RI_STATE. */
typedef struct {
    uint8_t  init;              /* 0: not started (a new career) */
    uint8_t  country;
    uint8_t  queued;
    uint8_t  pad;
    uint16_t queue[TEAM_POOL_QUEUE];
    uint16_t map[TEAM_POOL_SLOTS];
} TeamPoolState;

#define TEAM_POOL_FILES 8       /* (gplay.c RF) */
extern TeamPoolState g_teamPool[TEAM_POOL_FILES];

/* The state of `country`'s pool, 0 if it has none yet (the career has not swapped it). */
TeamPoolState *TeamPool_State(uint8_t country);

/* A new career: every slot its own, the queue the pool's first order. */
void TeamPool_Reset(void);

/* After a team file is read: in a career, its swapped slots take their identities. */
void TeamPool_ApplyToFile(uint8_t country, uint8_t *teamFileBuffer);

/* At the season end: the slot of `country`'s file at `ordinal`, relegated out of the
 * lowest division, takes the queue's next identity (the club going down joins the queue).
 * Returns the identity taken, or -1 (no pool, or the queue empty). */
int32_t TeamPool_Swap(uint8_t country, uint8_t ordinal);

/* After the swaps: every record the career holds of a swapped slot gets its identity, and
 * the loaded team file is read again at its next use. */
void TeamPool_SwapsDone(void);

/* A slot's area at a level of groups (p: its index among the levels of groups), by its
 * identity: `own` is the slot's own (its file record's). */
uint8_t TeamPool_Area(uint8_t country, uint8_t ordinal, int32_t p, uint8_t own);

#endif
