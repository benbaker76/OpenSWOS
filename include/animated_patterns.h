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

#ifdef SWOS_SDL_BUILD
/* ---- AnimatedPatterns_SyncTileMirror ----
 * Rebuilds the renderer's full-width tile pointers (s_pitchFullPtrs) for pitch
 * slots [first, first+count) from the pitchDatBuffer slots, after a rollback
 * restored those slots (src/sim_state.c).
 */
void AnimatedPatterns_SyncTileMirror(int32_t first, int32_t count);

/* A crowd cell whose tile is the match's (the host's, Pitch_ApplyCrowdLayout) and not this
 * pitch's own: the animation goes on flipping its slot -- that is the match -- but the cell
 * is drawn as this game loaded it, still. Cleared by LoadPitchMap. */
void AnimatedPatterns_ClearOwnCells(void);
void AnimatedPatterns_SetOwnCell(int32_t slot);
#endif

#endif
