/*
 * mod_hooks.h -- where a built-in (source) mod's code runs (docs/MODS.md §9, §16). SDL builds.
 *
 * A built-in mod is code in the game, with a canonical manifest (bin/mods/<slug>/mod.json): its
 * hooks run only while that mod is on (Mods_IsOn). The hooks are fixed points of the engine, never
 * patches of its code, so an engine change does not break a mod. They draw or read; none may write
 * match state (a frame here is never re-simulated: it is already the one shown).
 */
#ifndef MOD_HOOKS_H
#define MOD_HOOKS_H

#ifdef SWOS_SDL_BUILD
#include <SDL.h>

/* A frame of the game is on the renderer (world, sprites and HUD), in its logical size; before
 * the CRT effects, the touch controls and the swap. platform_sdl.c's present. */
void ModHooks_FrameDrawn(SDL_Renderer *renderer);

/* A match frame's pitch is drawn into the frame being made (src/game_loop.c, after
 * ScrollToCurrent) and its sprites are not yet: what is drawn here is under the players. The
 * frame may be one GGPO re-simulates (and never shows), so a hook here only draws. */
void ModHooks_PitchDrawn(void);
#endif

#endif /* MOD_HOOKS_H */
