/*
 * arcade.h — SWOS arcade / coin-op mode (SDL-only, PC extra).
 *
 * When _gArcadeMode is set ([arcade] enabled=true in SWOS.ini, or --arcade),
 * SWOS boots straight to the attract loop instead of the front-end menu: the
 * cabinet shows attract reels + flashing INSERT COIN, takes coins, plays short
 * first-goal-wins matches, and (F2) opens an operator service menu. The Amiga
 * game is the definitive product; this is a separate coin-op layer on top of the
 * same engine. Default 0 = normal game, nothing here reachable. See docs/ARCADE.md.
 */
#ifndef SWOS_ARCADE_H
#define SWOS_ARCADE_H

#include <stdint.h>

extern int _gArcadeMode;   /* 1 = arcade/coin-op mode (boot to attract). SDL-only */

/* Runs in place of FrontEndMainLoop when _gArcadeMode is set (called from mainC
 * after all assets are loaded). Never returns to the normal front-end menu. */
void ArcadeMainLoop(void);

/* Drain edge-triggered cabinet input collected since the last call. Each out
 * receives the number of events. MAME convention: key 5 = insert coin P1,
 * key 6 = insert coin P2, F2 = service, 1 = P1 start, 2 = P2 start. Any out may
 * be NULL. Implemented in platform_sdl.c. */
void Platform_ArcadeInput(int *coin1, int *coin2, int *service,
                          int *start1, int *start2);

/* DEBUG: returns 1 once per press (edge) of the debug key (F10). During a match it
 * jumps to the high-score name-entry flow; from the attract loop it advances the
 * arcade screen cycler (arc_debug_next_screen). Implemented in platform_sdl.c. */
int Platform_ArcadeDebugWin(void);

/* Draw the attract overlay (full-screen SWOS logo, index-0 transparent, + the
 * flashing INSERT COIN / credit state) onto the current frontbuffer. Called once
 * per frame from the replay draw loop while in arcade mode so it layers over the
 * playing reel, and from the idle screen. Returns non-zero when a game has been
 * started (a start button pressed with enough credits) so the caller can leave
 * the attract reel. */
int Arcade_DrawAttractOverlay(void);

/* Composite the attract overlay (built by Arcade_DrawAttractOverlay) onto the
 * final ARGB8888 frame using the MENU palette (index 0 transparent). Called by
 * the SDL present path after the frontbuffer->RGBA conversion so the logo + text
 * keep their menu colours over a reel that is using a different palette. */
void Arcade_CompositeOverlay(uint32_t *dst, int dst_pitch_pixels, int w, int h);

/* Full-screen arcade RGBA frame (flag select). Returns non-zero if it filled dst,
 * in which case the platform skips the normal 8bpp present path. */
int Arcade_RenderFullScreen(uint32_t *dst, int dst_pitch_pixels, int w, int h);

/* Arcade game modes -- the F2 service-menu MODE setting, persisted by name as
 * [arcade] mode in SWOS.ini. The modes and their rules are the arcade mod's
 * (src/mod_arcade.c, docs/ARCADE.md "Game modes -- the arcade mod"): these are its names, as
 * its "arcade" service lists them; index 0 is the default. */
int         Arcade_ModeCount(void);
const char *Arcade_ModeName(int idx);             /* clamped to a valid name */
int         Arcade_ModeFromName(const char *name); /* case-insensitive; -1 if unknown */

/* Once per game-loop frame (src/game_loop.c): the cabinet's DEBUG instant-win key
 * (F10) in a cabinet match. The match's own rules are the arcade mod's. */
void Arcade_CabinetTick(void);


#endif /* SWOS_ARCADE_H */
