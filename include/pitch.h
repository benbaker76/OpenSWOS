/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#ifndef PITCH_H
#define PITCH_H

#include "game_types.h"

void LoadPitchMap(uint16_t mapIndex);
void FixupAndColorPitchFile(void);
void AdjustFanColor(uint8_t *destPattern, const uint16_t *convTable);

/* THE CROWD AS THE MATCH PLAYS IT. The crowd animation (animated_patterns.c) picks cells
 * of the two fan rows and flips each between a still and a waving tile -- and which tile
 * a cell holds decides whether it flips, and with it how many random numbers that frame
 * draws. So the cells' tiles are match state; but they come from the pitch file each
 * player's own graphics load, and those differ (the SWOS 2020 pitches entirely, a
 * different release of the data here and there): a live match went out of step from
 * frame 1 (Elsheimy / Lauchlan, 2026-11-01). The host's game sends its crowd for every
 * pitch file in the match setup (netplay_setup.c), and both games put it in place over
 * their own once the pitch is loaded; a cell whose own tile differs is drawn still, in
 * this player's own artwork (AnimatedPatterns_SetOwnCell).
 *
 * A cell is its tile's identity as the match state hashes it (sim_state.c): 0 none, else
 * the chunky tile index + 1. PITCH_CROWD_KEEP: not known, keep this game's own. */
#define PITCH_CROWD_TOP_SLOT     42      /* rows 1-3 of the 42-column pitch table */
#define PITCH_CROWD_BOTTOM_SLOT  2142    /* rows 51-53 */
#define PITCH_CROWD_ROW_CELLS    126
#define PITCH_CROWD_CELLS        (2 * PITCH_CROWD_ROW_CELLS)
#define PITCH_CROWD_FILES        6       /* LoadPitchMap's indices */
#define PITCH_CROWD_KEEP         0xFFFFu
#ifdef SWOS_SDL_BUILD
/* Pitch file `mapIndex`'s crowd as this game would load it, without loading it: 0 if its
 * file cannot be read. */
int32_t Pitch_CrowdLayout(uint16_t mapIndex, uint16_t out[PITCH_CROWD_CELLS]);
/* After LoadPitchMap + FixupAndColorPitchFile: the match's crowd over this pitch's own. */
void    Pitch_ApplyCrowdLayout(const uint16_t in[PITCH_CROWD_CELLS]);
#endif

#endif
