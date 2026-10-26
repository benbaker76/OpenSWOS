/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* penalty_shootout.h — PENALTY SHOOTOUT, a mode of its own.
 *
 * A friendly whose match is nothing but the engine's own penalty shootout —
 * the one a drawn cup tie already goes to (StartPenalties/NextPenalty). The
 * teams walk out, the kickoff is taken, and the first live frame hands the
 * match to the shootout, which runs and ends itself exactly as it does after
 * a drawn cup final (EndOfGame once a side cannot be caught).
 *
 * It is the PRACTICE submenu's PENALTY SHOOTOUT button (the main menu's
 * PRACTICE row, beside SHOOTING PRACTICE) and nothing else: press it and you
 * are in the friendly's own flow -- its settings screen, its team selection,
 * its PLAY MATCH screen -- with the ONE difference being the match itself.
 * Any selection a friendly can start, a shootout can: both sides shoot, so
 * human v human and human v computer both make sense and nothing extra is
 * gated (contrast SHOOTING PRACTICE, which needs exactly one computer side).
 *
 * BOTH targets, like shooting practice: the latch and the tick are shared
 * match code and this object links into the m68k binary too.
 */
#pragma once
#include "game_types.h"

/* Non-zero from the moment PENALTY SHOOTOUT is pressed on the PRACTICE submenu
 * until its flow returns there. Owned by PenaltyShootoutMenu alone. While it is
 * set the friendly's screens say PENALTY SHOOTOUT instead of FRIENDLY. */
extern int32_t g_penaltyShootoutMode;

/* The PRACTICE submenu's PENALTY SHOOTOUT button: set the mode, run the
 * friendly flow, clear it again. Runs FriendlyFlow, not FriendlyMenu: a
 * submenu handler must not reach CommonMenuExit (docs/MENUS.md §12). */
void PenaltyShootoutMenu(void);

/* Latch whether THIS match is a shootout, once, as the in-game teams are set
 * up (InitializeInGameTeamsAndStartGame) — the same shape as
 * ShootingPractice_BeginMatch and for the same reason: what a match plays by
 * is decided at kick-off, never read live from a menu global. */
void PenaltyShootout_BeginMatch(void);

/* Per-frame: once the kickoff has been taken (first ST_GAME_IN_PROGRESS
 * frame), start the shootout — the same mid-match StartPenalties jump the
 * arcade's timer makes (Arcade_MatchTick), so the engine is on a proven path.
 * A no-op unless the latch is set, and fires once per match. */
void PenaltyShootout_MatchTick(void);

/* The friendly REPLAY path (game_loop.c end_of_match_flow) restarts the match
 * without re-entering InitializeInGameTeamsAndStartGame: call this there so
 * the one-shot is re-armed and a replayed shootout is a shootout again. */
void PenaltyShootout_MatchRestarted(void);
