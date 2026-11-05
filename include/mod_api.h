/*
 * mod_api.h -- what a built-in mod may ask the game (src/mod_api.c; docs/MODS.md §16). SDL builds.
 *
 * A mod's code (src/mod_<slug>.c) runs at the hooks of include/mod_hooks.h, and asks the game what
 * it needs through these, never through the game's own globals: an engine change then changes
 * this file's insides and no mod. Everything here READS, except the frame, which a mod may draw
 * into at a hook that says so. None of it is match state, and nothing here changes any.
 *
 * Coordinates: "world" is the pitch's pixels (as a sprite's x, y); "screen" the frame's, 0,0 at
 * its top left. A world point is on screen at world - ModApi_Camera().
 */
#ifndef MOD_API_H
#define MOD_API_H

#ifdef SWOS_SDL_BUILD

#include "swos_types.h"

/* ---- when ---- */
int32_t  ModApi_InMatch(void);          /* a match is being played (its frame loop runs) */
int32_t  ModApi_InReplay(void);         /* a replay is being shown: the in-match one or VIEW's */
uint32_t ModApi_Ticks(void);            /* real time, ms: for what a mod animates by itself */

/* ---- the match ---- */
int32_t  ModApi_PitchType(void);        /* 0 frozen, 1 muddy, 2 wet, 3 soft, 4 normal, 5 dry, 6 hard */
int32_t  ModApi_MatchMinute(void);      /* the clock's minute, 0.. (120 in extra time) */

/* The players as they are now (in a match, not a replay): up to 22, team 0's first, each team's
 * goalkeeper its first. */
typedef struct {
    int16_t x, y, z;                    /* world: his feet; z his height off the ground */
    int16_t direction;                  /* 0 up the screen, clockwise to 7 */
    uint8_t team;                       /* 0, 1 */
    uint8_t index;                      /* 0 the goalkeeper .. 10 */
    uint8_t onPitch;                    /* drawn this match (not sent off and walked away) */
    uint8_t face;                       /* 0 white, 1 ginger, 2 black, 3 black with blond hair */
    const char *name;                   /* his full name */
    const char *club;                   /* his team's name */
} ModPlayer;
int32_t  ModApi_Players(ModPlayer *out, int32_t max);

/* A recorded (replay) sprite, given as the replay hooks give it (its top left in the world):
 * whether it is an outfield player's frame, and where his feet are. */
int32_t  ModApi_ReplayPlayerFeet(uint16_t image, int16_t left, int16_t top, int16_t *x, int16_t *y);

/* ---- the screen ---- */
void     ModApi_Camera(int16_t *x, int16_t *y);     /* the world point at screen 0,0 */
void     ModApi_ScreenSize(int32_t *w, int32_t *h);
/* The 8bpp frame being drawn and its row stride; only at a hook that says it may be drawn into. */
uint8_t *ModApi_Frame(int32_t *stride);

/* ---- colours ---- */
void     ModApi_PaletteColour(uint8_t index, uint8_t *r, uint8_t *g, uint8_t *b);  /* as last shown */
uint8_t  ModApi_NearestColour(uint8_t r, uint8_t g, uint8_t b);                    /* (not entry 0) */

/* ---- the mod's own files ---- */
/* The folder of the mod of that slug (its mod.json beside), or NULL when it is not on. */
const char *ModApi_ModDir(const char *slug);

#endif /* SWOS_SDL_BUILD */
#endif /* MOD_API_H */
