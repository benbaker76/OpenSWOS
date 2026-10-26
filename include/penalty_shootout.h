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
 * then the SHOOTING ORDER screen in place of the PLAY MATCH screen (below) --
 * with the match itself being the shootout.
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

/* Per-frame: on the first frame of the match, hand it to the shootout — the
 * same StartPenalties jump a drawn cup tie makes at minute 90, made at minute
 * 0: no walk-on, no kickoff. A no-op unless the latch is set, and fires once
 * per match. */
void PenaltyShootout_MatchTick(void);

/* The friendly REPLAY path (game_loop.c end_of_match_flow) restarts the match
 * without re-entering InitializeInGameTeamsAndStartGame: call this there so
 * the one-shot is re-armed and a replayed shootout is a shootout again. */
void PenaltyShootout_MatchRestarted(void);

/* MANUAL KEEPER. At ANY penalty — the training shootout, a drawn cup tie's
 * shootout, an in-match penalty kick — the DEFENDING side's keeper belongs to
 * its player: hold LEFT to jump left, LEFT+DOWN to dive left low (RIGHT
 * mirrored). The reward for guessing is that the dive is immediate and at
 * full dive speed, where the AI keeper rolls dice on side, speed and timing;
 * the risk is the height is yours too. A NEUTRAL stick differs by context:
 * in the training drill the keeper stands (no guess, no dive — the drill's
 * point; he still catches a shot hit straight at him), while in a real match
 * it hands him back to the AI, so holding a direction is strictly opt-in.
 * (Born as this mode's drill, opened to normal matches 2026-10-26 — hence
 * the home here.)
 *
 * Asked by the goalie update at its save-attempt gate (update_players.c
 * shot_at_goal). Returns -1 when the AI should decide as ever (no penalty in
 * play, a CPU side, this keeper's own side shooting, or a neutral stick in a
 * real match); 0 for "stand" (the drill's neutral); else the held dive
 * direction (2=E, 3=SE, 5=SW, 6=W). */
int16_t ManualKeeper_DiveInput(TeamGeneralInfo *team);

/* THE SHOOTING ORDER. In this mode the PLAY MATCH screen is replaced by the
 * SHOOTING ORDER screen (_shootoutOrderMenu; ShowPlayMatchMenu makes the
 * swap): tactics, squad swaps and substitutes mean nothing to a match that is
 * only penalties, so the one choice the screen offers is who shoots when —
 * the ten outfield players as a queue, the first five being the best-of-five
 * takers, reordered by clicking one player and then another to trade their
 * places. The keeper never shoots: the engine's own shooter pick walks
 * spritesTable[10..1] and index 0 is the goalkeeper (NextPenalty).
 *
 * g_shootoutOrder[side] is that queue at menu time: lineup slots 1..10 in
 * shooting order, side 0 = the first selected team (in-game teamNumber 1).
 * Reset to the engine's own order (slot 10 first, 1 last) each time the mode
 * is entered, latched per match by PenaltyShootout_BeginMatch like the mode
 * flag itself. */
extern uint8_t g_shootoutOrder[2][10];

/* The latched queue, asked by NextPenalty (goalkeeper.c) in place of its
 * decrement-and-wrap pick: the spritesTable slot (1..10) of the side's
 * shooter for its (attempts+1)th kick, cycling back to the queue's head past
 * ten. -1 when this match is not the drill — the caller then picks the stock
 * way, so cup shootouts and netplay see nothing new. */
int16_t PenaltyShootout_NextShooterSlot(int16_t teamNumber, int16_t attempts);
