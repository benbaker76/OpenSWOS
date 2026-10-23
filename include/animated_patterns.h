/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#ifndef ANIMATED_PATTERNS_H
#define ANIMATED_PATTERNS_H

#include "game_types.h"

/* ---- DrawAnimatedPatterns ----
 * Draws animated crowd fan patterns based on game state.
 * Called once per frame (including during replays).
 * 8-state machine: no_game, ball_out, greatest_cheers, middle_cheers,
 * other_team_scored, cheers_fading, game_hasnt_started, game_in_progress.
 */
void DrawAnimatedPatterns(void);

/* ---- ResetAnimatedPatternsForBothTeams ----
 * Resets animated pitch tiles (top + bottom fan sections) back to static,
 * and clears the animatedPatterns tracking array to 0xFFFF.
 */
void ResetAnimatedPatternsForBothTeams(void);

/* ---- AnimatedPatterns_TileBase / _TileShift ----
 * Where a pitch tile SLOT points, and how big a tile is, on THIS target: the fan rows of
 * _pitchDatBuffer hold a pointer to their tile, and `(slot - base) >> shift` is the tile's
 * index. The two targets keep their pitch tiles in different places and depths -- SDL in
 * the 8bpp chunky buffer at a fixed 256 bytes a tile, the Amiga in the planar tile region
 * of _pitchDatBuffer at TILE_SIZE (128 at 4bpp, 256 at AGA's 8bpp) -- so the slot VALUES
 * are not comparable between them, while the tile INDEX is.
 *
 * That is why the match state travels a slot as its index (src/sim_state.c,
 * SIM_FIELD_TILE_SLOT): a saved position written on either target loads on the other.
 * sim_state.c used to hardcode the SDL buffer and a 256-byte stride, so on the m68k build
 * all 252 fan slots had no identity and a position could not be saved at all. */
uint32_t AnimatedPatterns_TileBase(void);
uint32_t AnimatedPatterns_TileShift(void);

#ifdef SWOS_SDL_BUILD
/* ---- AnimatedPatterns_SyncTileMirror ----
 * Rebuilds the renderer's full-width tile pointers (s_pitchFullPtrs) for pitch
 * slots [first, first+count) from the pitchDatBuffer slots, after a rollback
 * restored those slots (src/sim_state.c).
 */
void AnimatedPatterns_SyncTileMirror(int32_t first, int32_t count);
#endif

#endif
