/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* penalty_shootout.c — PENALTY SHOOTOUT training mode. See penalty_shootout.h. */

#include "penalty_shootout.h"

int32_t g_penaltyShootoutMode;

extern uint16_t _gameStatePl;

/* The friendly flow this mode borrows whole, WITHOUT the main-menu fixup on
 * the way out (menus.c): this handler runs under the 4-entry PRACTICE submenu,
 * where CommonMenuExit would write through main-menu entries that are not
 * there. PracticeMenuSelected makes that call once, after the submenu closes. */
extern void FriendlyFlow(void);

/* The engine's own shootout (match_setup.c), the one a drawn cup tie goes to.
 * It runs and ends itself: NextPenalty (goalkeeper.c) advances it and calls
 * EndOfGame once a side cannot be caught. */
extern void StartPenalties(void);

/* PenaltyShootoutMenu -- the PRACTICE submenu's PENALTY SHOOTOUT button.
 *
 * The whole mode is "a friendly, with the flag set", exactly as SHOOTING
 * PRACTICE is (src/shooting_practice.c, where the reuse is argued in full):
 * the flow seeds the RNG, saves/restores gameType and invalidates the team
 * cache, all of which this mode needs too. The screens it puts up read
 * g_penaltyShootoutMode to say PENALTY SHOOTOUT where they would say FRIENDLY.
 *
 * Clearing on the way out is what makes the flag safe: every path back to the
 * submenu, including an abort, returns through here. */
__attribute__((used, externally_visible))
void PenaltyShootoutMenu(void)
{
    g_penaltyShootoutMode = 1;
    FriendlyFlow();
    g_penaltyShootoutMode = 0;
}

/* LATCHED for the whole match by PenaltyShootout_BeginMatch, for the same
 * reason shooting practice latches (its bug log, docs/SHOOTING_PRACTICE.md §3):
 * the match must never depend on a menu global that outlives the menu. */
static int16_t s_shootoutMatch;
static int16_t s_shootoutStarted;

void PenaltyShootout_BeginMatch(void)
{
    s_shootoutMatch = (int16_t)(g_penaltyShootoutMode != 0);
    s_shootoutStarted = 0;
}

/* The REPLAY button after a friendly restarts the match straight through
 * InitGameRestoreTeams + GameLoop (game_loop.c end_of_match_flow), never
 * re-entering InitializeInGameTeamsAndStartGame, so BeginMatch does not run
 * again: the latch survives but the one-shot stayed spent, and the replay of a
 * shootout played as a plain friendly (reported 2026-10-26). Re-arm it; the
 * latch itself is left alone, so a replayed friendly stays a friendly. */
void PenaltyShootout_MatchRestarted(void)
{
    s_shootoutStarted = 0;
}

void PenaltyShootout_MatchTick(void)
{
    if (!s_shootoutMatch || s_shootoutStarted)
        return;
    /* Wait for the kickoff to be taken rather than jumping during the walk-on:
     * StartPenalties is only ever entered from a running match (minute 90/120
     * in game_state.c, the arcade's timer), so enter it the same way. */
    if (_gameStatePl != ST_GAME_IN_PROGRESS)
        return;
    s_shootoutStarted = 1;
    StartPenalties();
}
