/*
 * mods_builtin.c -- the built-in mods (include/mod_api.h ModDef; docs/MODS.md §16). SDL builds.
 *
 * One line a mod: its ModDef, defined in its own src/mod_<slug>.c. A fork lists its own mods in
 * src/fork_mods.inc (a file OpenSWOS does not have), so merging OpenSWOS never meets a fork's
 * list and a fork never edits this one.
 */
#include "mod_api.h"

#ifdef SWOS_SDL_BUILD

#include <stddef.h>

extern const ModDef ModDef_weather;     /* src/mod_weather.c */
extern const ModDef ModDef_hair;        /* src/mod_hair.c */
extern const ModDef ModDef_practice;    /* src/mod_practice.c */
extern const ModDef ModDef_speedrun;    /* src/mod_speedrun.c */
extern const ModDef ModDef_challenges;  /* src/mod_challenges.c */
extern const ModDef ModDef_classics;    /* src/mod_classics.c */
extern const ModDef ModDef_ticker;      /* src/mod_ticker.c */

#if defined(__has_include)
#if __has_include("fork_mods.inc")
#define FORK_MODS 1
/* its declarations: `extern const ModDef ModDef_<slug>;` lines, then FORK_MOD_LIST */
#include "fork_mods.inc"
#endif
#endif

const ModDef *const g_builtinMods[] = {
    &ModDef_weather,
    &ModDef_hair,
    &ModDef_practice,
    &ModDef_speedrun,
    &ModDef_challenges,
    &ModDef_classics,
    &ModDef_ticker,
#ifdef FORK_MODS
    FORK_MOD_LIST                       /* "&ModDef_<slug>," a fork's mods */
#endif
    NULL,
};

#endif /* SWOS_SDL_BUILD */
