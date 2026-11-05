/*
 * mods_builtin.c -- the built-in mods (include/mod_api.h ModDef; docs/MODS.md §16). Both targets:
 * the Amiga lists those of them that are ported there (MOD_AMIGA below).
 *
 * One line a mod: its ModDef, defined in its own src/mod_<slug>.c. A fork lists its own mods in
 * src/fork_mods.inc (a file OpenSWOS does not have), so merging OpenSWOS never meets a fork's
 * list and a fork never edits this one.
 */
#include "mod_api.h"


#include <stddef.h>

extern const ModDef ModDef_weather;     /* src/mod_weather.c */
extern const ModDef ModDef_hair;        /* src/mod_hair.c */
extern const ModDef ModDef_practice;    /* src/mod_practice.c */
extern const ModDef ModDef_speedrun;    /* src/mod_speedrun.c */
extern const ModDef ModDef_challenges;  /* src/mod_challenges.c */
extern const ModDef ModDef_classics;    /* src/mod_classics.c */
extern const ModDef ModDef_euro36;      /* src/mod_euro36.c */

#if defined(__has_include)
#if __has_include("fork_mods.inc")
#define FORK_MODS 1
/* its declarations: `extern const ModDef ModDef_<slug>;` lines, then FORK_MOD_LIST */
#include "fork_mods.inc"
#endif
#endif

/* MOD_SDL: a mod only the SDL build has yet; MOD_AMIGA: one built on both targets. */
#ifdef SWOS_SDL_BUILD
#define MOD_SDL(m) &m,
#else
#define MOD_SDL(m)
#endif
#define MOD_AMIGA(m) &m,

const ModDef *const g_builtinMods[] = {
    MOD_SDL(ModDef_weather)
    MOD_SDL(ModDef_hair)
    MOD_AMIGA(ModDef_practice)
    MOD_AMIGA(ModDef_speedrun)
    MOD_AMIGA(ModDef_challenges)
    MOD_SDL(ModDef_classics)
    MOD_AMIGA(ModDef_euro36)
#ifdef FORK_MODS
    FORK_MOD_LIST                       /* "&ModDef_<slug>," a fork's mods */
#endif
    NULL,
};

