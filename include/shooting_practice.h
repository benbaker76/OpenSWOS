/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* shooting_practice.h — SHOOTING PRACTICE, a mode of its own.
 *
 * A friendly, human vs computer, where the computer side is stripped down to
 * its goalkeeper (TeamGeneralInfo.spritesTable[0]) so the human's 11 can
 * practise finishing into an open defence.
 *
 * It is the main menu's SHOOTING PRACTICE button (entry 16, beside CREDITS) and
 * nothing else: press it and you are in the friendly's own flow -- its settings
 * screen, its team selection, its PLAY MATCH screen, with EDIT TACTICS, VIEW
 * OPPO, squad swaps and substitutes all working as they always do. The ONE
 * difference is the match itself, which kicks off with the computer side
 * fielding only its keeper.
 *
 * Until 2026-10-22 it was instead a PRACTICE toggle on the PLAY MATCH screen
 * (entry 69). That button is gone: it had to clear itself whenever it was off
 * screen, or a stale ON leaked into an unrelated match, and that same clearing
 * is what made a standalone mode impossible to build on top of it. The mode now
 * owns g_shootingPracticeMode below and nothing else writes it.
 *
 * BOTH targets. Nothing here depends on SDL: the strip and the kickoff hook are
 * shared match code, and this object links into the m68k binary too, so the
 * Amiga gets the same drill. (Contrast UNIFORM, which is SDL-only because it
 * opens a SWOS 2020 JSON menu the Amiga does not run.)
 */
#pragma once
#include "game_types.h"

/* Non-zero from the moment SHOOTING PRACTICE is pressed on the main menu until
 * its flow returns there. Owned by ShootingPracticeMenu alone -- no menu redraw
 * clears it, which is the whole point of the rewrite. While it is set the
 * friendly's screens say PRACTICE instead of FRIENDLY and refuse to start until
 * exactly one side is the computer. */
extern int32_t g_shootingPracticeMode;

/* The main menu's SHOOTING PRACTICE button: set the mode, run the friendly
 * flow, clear it again. */
void ShootingPracticeMenu(void);

/* 1 when the teams picked so far could start a drill -- exactly one computer
 * side and exactly one human one. The three screens that show the PLAY button
 * ask this before showing it (CheckNumTeamsAllowed, CheckNumberOfTeams,
 * SetChooseChangeTeamsEntries), so a selection the drill cannot apply to can
 * never be started rather than silently playing as an ordinary friendly. */
int16_t ShootingPractice_SelectionOk(void);

/* Latch whether THIS match is a shooting-practice one, and which side is the
 * computer, for the match's lifetime. Called once the in-game teams know who is
 * who (InitializeInGameTeamsAndStartGame). Everything below reads that latch
 * rather than the mode flag, so that a match already under way cannot be
 * changed by anything the menus do afterwards. */
void ShootingPractice_BeginMatch(void);

/* 0 = no change; else the team number (1 or 2) that must kick off instead of
 * whatever the coin toss picked, because the other side only has a goalkeeper
 * left to send to the centre circle. Applies to EVERY kickoff: match start,
 * half time, extra time, and the restart after a goal. */
int16_t ShootingPractice_ForcedKickoffTeamNum(void);

/* Marks the computer side's ten outfield players sentAway (the same flag a
 * red card sets — src/tackle_foul.c SendPlayerAway) and parks them off-pitch,
 * without the disciplinary side effects (no card, no booking stat, no SENT
 * OFF banner) since nothing happened in the match. A no-op unless the mode
 * is on and exactly one side is the computer. Called every time both teams
 * walk out (match start, second half) so the strip survives half-time. */
void ShootingPractice_StripCpuOutfieldPlayers(void);
