/*
 * world_packs.h -- another world's clubs in a friendly or a DIY competition (docs/WORLD_2026_27.md
 * §6k; the SWOS 96/97 Mod 4.1's "Friendlies and DIY competitions across worlds"). SDL builds with
 * the worlds (SWOS_WORLDS).
 *
 * The world being played goes over 96/97's tables at start-up, and its countries keep 96/97's
 * numbers: another world's ITALY would be the same team words as this one's. So another installed
 * world's country (96/97's too, while a world is played) is offered as a TEAM PACK
 * (include/team_packs.h): CHOOSE TEAMS' TEAM PACKS row lists the other worlds after the mods'
 * packs, a world its countries, and the country picked gets a pack number for the session
 * (TEAMPACK_FIRST..TEAMPACK_LAST, never a world's second files 186-197 nor a mod's pack). Its team
 * file is the world's own teams/<NAME>.json, read with every club's country number and ordinal
 * rewritten to the pack's (WorldPacks_Fix), and its league a copy of the customs league with the
 * country's divisions (48 clubs at most each, the real division names). No table of that world is
 * installed: a friendly or a DIY competition needs only the clubs. Every world mixes with every
 * other -- the numbers are the session's, not the worlds' -- so the Mod's "one world per
 * competition" is not needed.
 *
 * A .DIY / .PRE save records each pack number its teams use ("world_packs": world and file), and
 * its load gives them those numbers again (WorldPacks_UseAt). Online the other game's numbers are
 * its own: a team from it whose number is not the same club here is found by its checksum
 * (WorldPacks_FindTeam, src/custom_team.c).
 */
#ifndef WORLD_PACKS_H
#define WORLD_PACKS_H

#include <stdint.h>
#include "swos_types.h"   /* SWOS_WORLDS's default, when no makefile sets it (Makefile.switch) */

#if defined(SWOS_SDL_BUILD) && SWOS_WORLDS

/* The worlds offered besides the one being played: the installed ones, 96/97 when another is
 * played. i from 0; -1 past the last. `label`: "EUROPE 1984-85". */
int32_t     WorldPacks_WorldCount(void);
int32_t     WorldPacks_World(int32_t i, int32_t *world, const char **label);

/* A world's club countries (alphabetical): their count, and the i-th's name and file
 * ("ITALY.json"). Read once a world. */
int32_t     WorldPacks_CountryCount(int32_t world);
int32_t     WorldPacks_Country(int32_t world, int32_t i, const char **name, const char **file);

/* The pack number of a world's country file for this session -- given at the first ask, its
 * name and league installed -- or 0 (no number left, no such file). */
int32_t     WorldPacks_Use(int32_t world, const char *file);
/* The same at a given number (a save's): 0 when that number is a mod's pack or the game's. */
int32_t     WorldPacks_UseAt(int32_t number, int32_t world, const char *file);

/* A pack number's world and file, when it is one of these (1), else 0. */
int32_t     WorldPacks_Info(int32_t number, int32_t *world, const char **file);

/* The data path of a pack number's team file ("data_json/worlds/8485/teams/ITALY.json"), or NULL;
 * and its records as loaded, rewritten to the pack (country, ordinal, division). */
const char *WorldPacks_Path(int32_t number);
struct TeamFileEx;
void        WorldPacks_Fix(int32_t number, struct TeamFileEx *records, int32_t count);

/* The index FindTeam looks in, made on a thread of its own from the start-up (src/main.c). */
void        WorldPacks_IndexStart(void);

/* A club of another installed world by its checksum (a team the other game sent, numbered by its
 * session): its pack number here (given now if need be) and ordinal. 1 found, 0 not. */
int32_t     WorldPacks_FindTeam(const char *checksum, int32_t *number, int32_t *ordinal);

/* SWOS_WORLD_PACKS="2627:ITALY.json,8485:ENGLAND.json": those first, at start-up (a test's). */
void        WorldPacks_FromEnv(void);

/* --world-packs-selftest: every other installed world's countries given numbers, each file read
 * and rewritten, its clubs found again by checksum. 0 pass. */
int32_t     WorldPacks_SelfTest(void);

#endif

#endif /* WORLD_PACKS_H */
