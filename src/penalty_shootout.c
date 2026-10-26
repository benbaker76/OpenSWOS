/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* penalty_shootout.c — PENALTY SHOOTOUT training mode. See penalty_shootout.h. */

#include "penalty_shootout.h"

int32_t g_penaltyShootoutMode;

extern uint16_t _playingPenalties;
extern uint16_t _penalty;
extern Sprite  *_penaltyShooterSprite;
extern TeamGeneralInfo *_lastTeamPlayed;

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

/* The defending keeper at ANY penalty is its player's to throw
 * (penalty_shootout.h): the training shootout, a drawn cup tie's shootout, and
 * an in-match penalty kick. (Until 2026-10-26 it was the training mode's
 * alone — the s_shootoutMatch latch gated it.) Reads the defending side's
 * HELD direction, which processInputForPlayer writes every frame a human pad
 * is polled (team_control.c), so a direction held before the kick commits the
 * dive on the first frame the engine sees a shot.
 *
 * The window is the engine's own penalty-save window — _playingPenalties for
 * a shootout, _penalty from an in-match kick's strike until the next touch —
 * the same pair ShouldGoalkeeperDive's penalty branch tests (goalkeeper.c).
 * Outside the training drill a neutral stick falls back to the AI keeper
 * (see the switch below), so normal matches only ever gain control, never
 * lose the save the AI would have made.
 *
 * Directions are the engine's 0..7 compass (0=N clockwise to 7=NW; the dive
 * directions 2=E and 6=W are the ones GoalkeeperJumping and the kick-off
 * turnFlags already use): W=jump left, SW=dive left low, E/SE mirrored. N, S
 * and the two up-diagonals are not dives, so a thumb resting UP or DOWN alone
 * leaves the keeper standing. */
int16_t ManualKeeper_DiveInput(TeamGeneralInfo *team)
{
    if (team->playerNumber == 0)
        return -1;                      /* a CPU-run side keeps the AI keeper */
    if (_playingPenalties) {
        /* Only the DEFENDING side's keeper: the shooting side's keeper, idle
         * at the far end, runs the same goalie update and must stay the AI's. */
        if (_penaltyShooterSprite != (Sprite *)0 &&
            _penaltyShooterSprite->teamNumber == team->teamNumber)
            return -1;
    } else if (_penalty) {
        /* In-match kick: _lastTeamPlayed is the kicker's side — set beside
         * _penalty at the strike (update_players.c execute_shot/pass), and
         * every later touch clears _penalty, so the pair stays consistent. */
        if (_lastTeamPlayed == team)
            return -1;
    } else {
        return -1;                      /* open play: the AI decides as ever */
    }

    switch (team->direction) {
    case 2:   /* E  — jump right       */
    case 3:   /* SE — dive right, low  */
    case 5:   /* SW — dive left, low   */
    case 6:   /* W  — jump left        */
        return team->direction;
    default:
        /* Neutral stick. In the training drill the keeper STANDS — no guess,
         * no dive, the mode's whole point. In a real match the AI decides as
         * ever, so a player who never touches the stick keeps the keeper the
         * game always had, and only a held dive direction overrides the dice
         * (asked for after the first live build, 2026-10-26). */
        return s_shootoutMatch ? 0 : -1;
    }
}

void PenaltyShootout_MatchTick(void)
{
    if (!s_shootoutMatch || s_shootoutStarted)
        return;
    /* First frame of the loop: straight to the shootout. By now StartingMatch
     * has run (InitGameRestoreTeams calls it before GameLoop), so both teams'
     * sprites and data exist, and StartPenalties -> NextPenalty rebuilds the
     * scene wholesale anyway (InitTeamsData, StopAllPlayers, its own game
     * state and camera) -- the walk-on and the kickoff it replaces were pure
     * ceremony. Until 2026-10-26 this waited for the kickoff to be taken
     * (ST_GAME_IN_PROGRESS, the arcade's entry point), which made every
     * shootout open with a pointless kick-off whistle. */
    s_shootoutStarted = 1;
    StartPenalties();
}
