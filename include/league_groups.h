/*
 * league_groups.h -- leagues with parallel groups at the season end
 * (src/league_groups.c; docs/WORLD_2026_27.md, phase 4).
 *
 * The original engine moves a club one DIVISION up or down its league's chain. A world's
 * league can have a level of parallel groups -- the World 2026-27's Serie C is three groups
 * side by side under Serie B -- where a club relegated into the level belongs in the group
 * of its area, and a group's champion goes up a LEVEL, not into the next group. The game's
 * moves are made as always; this turns each into a level move afterwards.
 */
#ifndef LEAGUE_GROUPS_H
#define LEAGUE_GROUPS_H

#include "swos_types.h"

#define LEAGUE_GROUPS_MAX_LEVELS 8
#define LEAGUE_GROUPS_MAX_GROUPS 9      /* (the World 2026-27's Serie D) */
#define LEAGUE_GROUPS_NO_AREA    0xFF

/* One level of the league's chain: a single division (groups 1) or groups side by side,
 * divisions first .. first + groups - 1, each sizes[i] clubs; a level of groups has a name
 * of its own ("SERIE D"), the team picker's row for it. */
typedef struct {
    uint8_t     first;
    uint8_t     groups;
    uint8_t     sizes[LEAGUE_GROUPS_MAX_GROUPS];
    const char *name;
} LeagueLevel;

/* A club that can play at a level of groups: its slot in the country (include/world.h: its
 * ordinal in the country's team file, 256 + it in a second file), its division in that file
 * (what the league table's offset is from), and its area at each level of groups, top-down
 * (LEAGUE_GROUPS_NO_AREA: none). */
typedef struct {
    uint16_t slot;
    uint8_t  base;
    uint8_t  area[2];
} LeagueGroupClub;

typedef struct {
    uint8_t                country;
    uint8_t                levels;
    const LeagueLevel     *level;
    uint16_t               clubs;
    const LeagueGroupClub *club;
} LeagueGroups;

/* The world's leagues with groups (src/world_data.c, generated); *count of them. */
const LeagueGroups *World_LeagueGroups(int32_t *count);

/* The league with groups of a country, NULL for one without. */
const LeagueGroups *LeagueGroups_ForCountry(uint16_t country);

/* A slot that takes a club from below the league this season end (a world's pool,
 * include/team_pool.h): it enters the lowest level, in the group of its new identity's area.
 * Kept until LeagueGroups_Apply. */
void LeagueGroups_Enter(uint16_t globalNumber);

/* At the season's turn, before the new league table is taken from the scratch copy:
 * `table` the season's (old) offsets, `scratch` the game's new ones, by global number. */
void LeagueGroups_Apply(const int8_t *table, int8_t *scratch);

#endif
