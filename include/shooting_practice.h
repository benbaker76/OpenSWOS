/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* shooting_practice.h — SHOOTING PRACTICE training mode.
 *
 * A friendly, human vs computer, where the computer side is stripped down to
 * its goalkeeper (TeamGeneralInfo.spritesTable[0]) so the human's 11 can
 * practise finishing into an open defence. Toggled from the PLAY MATCH
 * screen's SHOOTING PRACTICE button (entry 69 — src/play_match_menu.c,
 * src/menu_data.c).
 *
 * BOTH targets. Nothing here depends on SDL: the strip and the kickoff hook are
 * shared match code, and this object links into the m68k binary too, so the
 * Amiga gets the same drill. (Contrast UNIFORM, which is SDL-only because it
 * opens a SWOS 2020 JSON menu the Amiga does not run.)
 */
#pragma once
#include "game_types.h"

/* The PLAY MATCH screen's toggle state. Reset to 0 by the menu itself
 * whenever the button isn't shown (not a friendly, not human-vs-computer,
 * viewing the opponent, etc.) — see UpdateShootingPracticeButton in
 * src/play_match_menu.c — so this can never leak a stale ON into a match
 * it doesn't apply to. */
extern int32_t g_playMatchShootingPractice;

/* Latch whether THIS match is a shooting-practice one, and which side is the
 * computer, for the match's lifetime. Called once the in-game teams know who is
 * who (InitializeInGameTeamsAndStartGame). Everything below reads that latch,
 * never the menu toggle above -- the menu clears that whenever its button is
 * off screen, which would otherwise switch the mode off mid-match. */
void ShootingPractice_BeginMatch(void);

/* 0 = no change; else the team number (1 or 2) that must kick off instead of
 * whatever the coin toss picked, because the other side only has a goalkeeper
 * left to send to the centre circle. Applies to EVERY kickoff: match start,
 * half time, extra time, and the restart after a goal. */
int16_t ShootingPractice_ForcedKickoffTeamNum(void);

/* Marks the computer side's ten outfield players sentAway (the same flag a
 * red card sets — src/tackle_foul.c SendPlayerAway) and parks them off-pitch,
 * without the disciplinary side effects (no card, no booking stat, no SENT
 * OFF banner) since nothing happened in the match. A no-op unless the toggle
 * is on and exactly one side is the computer. Called every time both teams
 * walk out (match start, second half) so the strip survives half-time. */
void ShootingPractice_StripCpuOutfieldPlayers(void);
