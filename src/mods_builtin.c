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
extern const ModDef ModDef_flags;       /* src/mod_flags.c (SDL: it draws through the renderer) */
extern const ModDef ModDef_pitchmaps;   /* src/mod_pitchmaps.c (SDL: Tile Agent pitch maps) */
extern const ModDef ModDef_backgrounds; /* src/mod_backgrounds.c (SDL: the menus' true-colour backdrop) */
extern const ModDef ModDef_dosmenus;    /* src/mod_dosmenus.c (SDL: the PC game's menu shading) */
extern const ModDef ModDef_retrotv;     /* src/mod_retrotv.c (SDL: the picture through a slang preset) */
extern const ModDef ModDef_paletteq;    /* src/mod_paletteq.c (SDL: the picture in an old machine's colours) */
extern const ModDef ModDef_autoplay;    /* src/mod_autoplay.c (SDL: SWOS 2020's F1 AUTOPLAY) */
extern const ModDef ModDef_balltrails;  /* src/mod_balltrails.c (SDL: SWOS 2020's ball trails) */
extern const ModDef ModDef_scoreboard;  /* src/mod_scoreboard.c (SDL: SWOS 2020's new scoreboard) */
extern const ModDef ModDef_netstats;    /* src/mod_netstats.c (SDL: SWOS 2020's network quality strip, F7) */
extern const ModDef ModDef_pitchselect; /* src/mod_pitchselect.c (SDL: SWOS 2020's SELECT PITCH) */
extern const ModDef ModDef_display2020; /* src/mod_display2020.c (SDL: SWOS 2020's result display; launcher options, agent B) */
extern const ModDef ModDef_arcade;      /* src/mod_arcade.c (SDL: the arcade's game modes and their rules) */

#if defined(__has_include)
#if __has_include("fork_mods.inc")
#define FORK_MODS 1
/* its declarations: `extern const ModDef ModDef_<slug>;` lines, then FORK_MOD_LIST. The list is
 * built on the Amiga too, so a mod only the SDL build has goes in it as MOD_SDL(ModDef_<slug>)
 * (the macro is defined below, before the list expands FORK_MOD_LIST): SWOS United's
 * `#define FORK_MOD_LIST MOD_SDL(ModDef_menus2020) MOD_SDL(ModDef_unitedTicker)`. */
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
    MOD_AMIGA(ModDef_classics)
    MOD_AMIGA(ModDef_euro36)
    MOD_SDL(ModDef_flags)
    MOD_SDL(ModDef_pitchmaps)
    MOD_SDL(ModDef_backgrounds)
    MOD_SDL(ModDef_dosmenus)
    MOD_SDL(ModDef_retrotv)
    MOD_SDL(ModDef_paletteq)
    MOD_SDL(ModDef_autoplay)
    MOD_SDL(ModDef_balltrails)
    MOD_SDL(ModDef_scoreboard)
    MOD_SDL(ModDef_netstats)
    MOD_SDL(ModDef_pitchselect)
    MOD_SDL(ModDef_display2020)
    MOD_SDL(ModDef_arcade)
#ifdef FORK_MODS
    FORK_MOD_LIST                       /* "&ModDef_<slug>," a fork's mods */
#endif
    NULL,
};

