/*
 * world.h -- which football world the game plays (docs/WORLD_2026_27.md §4).
 *
 * A world is a data set: its countries, clubs, competitions and season rules. 96/97 is
 * world 0 -- the original game, and what every save and every build without a world
 * knows -- so a byte that was 0 before worlds existed already means 96/97.
 *
 * The world is chosen by `[general] world` in SWOS.ini (SDL); the Amiga reads no
 * SWOS.ini and plays 96/97. It is written into every save that holds teams or
 * competitions ("world"), and a save of another world is refused; online, the two
 * games compare it at the handshake (src/p2p_link.c) and the server lists a game only
 * to players of the same world (src/mm_client.c).
 */
#ifndef WORLD_H
#define WORLD_H

#include "swos_types.h"   /* the fixed-width types, on both targets */

typedef enum {
    WORLD_9697 = 0,     /* Sensible World of Soccer 96/97 */
    WORLD_2627,         /* the SWOS 96/97 Mod's World 2026-27 (docs/WORLD_2026_27.md): its data
                         * in data_json/worlds/2627/ over 96/97's, file by file and record by
                         * record (src/save_format_json.c); phase 3 holds Italy's leagues */
    WORLD_COUNT
} WorldId;

WorldId     World_Current(void);
#if !SWOS_WORLDS
/* (SWOS_WORLDS=0, include/swos_types.h: 96/97 alone -- a constant, every world branch folds
 * away) */
#define World_Current() WORLD_9697
#endif
const char *World_Tag(void);                 /* the current world's: "9697" */
const char *World_TagOf(WorldId w);          /* NULL for an unknown id */
int32_t     World_FromTag(const char *tag);  /* the id, or -1 */
int32_t     World_Set(const char *tag);      /* 0, or -1 (unknown: unchanged) */

/* The world's baked competition records over 96/97's (src/world_data.c, made by
 * scripts/world_import.py): on both targets, at start-up, before anything reads the
 * competition tables; the SDL JSON install (data_json/worlds/<tag>/) runs after it. */
void        World_InstallBaked(void);

/* A world's countries new to the game (src/world_data.c): its continent lists for the
 * career picker (region 80..85; 96/97's with them in), its season end's countries (0xFF-
 * ended; 0: 96/97's own lists), and a new country's team file under the world's folder
 * ("teams/<NAME>.json", the SDL's; 0: not one -- the Amiga reads data/TEAM.nnn). */
const uint8_t *World_ContinentCountries(uint8_t region);
const uint8_t *World_SeasonCountries(void);
const char    *World_CountryTeamFile(uint8_t country);

#ifndef SWOS_SDL_BUILD
/* The Amiga reads no SWOS.ini: a drawer made for a world says so in data/WORLD (its tag;
 * OpenSWOS Setup writes it). None: 96/97. */
void        World_ReadFromData(void);
#endif

/* The other confederations' cups' spec (src/world_data.c; src/world_conti.c's CT_SPEC), 0: none. */
const uint8_t *World_ContiSpec(void);
/* Its cups' names, in the spec's order, and each round's name (_cupRoundStringTable, by key). */
typedef struct WorldContiCup {
    const char *name, *shortName;
    uint8_t round[8];
    uint8_t rank;                       /* in its confederation: 0 its first (a Champions Cup's trophy), 1, 2 */
    uint8_t contest;                    /* its main round a contest of the game's (0: a stepladder, ties only) */
} WorldContiCup;
const WorldContiCup *World_ContiCups(int32_t *count);

/* Argentina's promedio history (src/league_playoffs.c; the Mod's GP_PROM_TAB): each club's points and
 * matches in the top flight the two seasons before the world's first (0 / 0: not there), by its
 * ordinal in the country's team file. */
typedef struct WorldPromedio {
    uint8_t ordinal, matches2, matches1;
    uint16_t points2, points1;
} WorldPromedio;
const WorldPromedio *World_Promedio(uint8_t country, int32_t *count);

/* A country's state championships (Brazil's; src/states.c): the season end's simulation table
 * (state_sim.c) and its pools' (state_pool.c), their team words the Mod's two files' -- the
 * league's clubs the country's number, the state-only clubs statesFile with their ordinal among
 * them (ours: firstState + it). */
typedef struct WorldStates {
    uint8_t country, statesFile, world, firstState, states;
    uint16_t simBytes;
    const uint8_t *sim, *pools;
    const uint16_t *wordsAt;            /* each state's words in sim (its contest's entries: the overrides') */
} WorldStates;
const WorldStates *World_States(uint8_t country);

#endif
