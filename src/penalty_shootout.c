/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* penalty_shootout.c — PENALTY SHOOTOUT training mode. See penalty_shootout.h. */

#include "penalty_shootout.h"
#include "menu_system.h"     /* MenuEntry, DrawMenu -- the SHOOTING ORDER screen */
#include "endian_helpers.h"  /* PACK_BE_U16 -- a team's number from its header */

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

/* The shooting order each side takes into the match: lineup slots 1..10 in
 * shooting order (penalty_shootout.h). The default IS the engine's own pick —
 * NextPenalty walks spritesTable 10,9,...,1 — so an untouched screen changes
 * nothing, and a CPU side (which never sees the screen) shoots as it always
 * has. */
uint8_t g_shootoutOrder[2][10];

static void shootoutOrderReset(void)
{
    int16_t side, pos;
    for (side = 0; side < 2; side++)
        for (pos = 0; pos < 10; pos++)
            g_shootoutOrder[side][pos] = (uint8_t)(10 - pos);
}

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
    shootoutOrderReset();     /* a fresh entry starts from the stock order */
    g_penaltyShootoutMode = 1;
    FriendlyFlow();
    g_penaltyShootoutMode = 0;
}

/* LATCHED for the whole match by PenaltyShootout_BeginMatch, for the same
 * reason shooting practice latches (its bug log, docs/SHOOTING_PRACTICE.md §3):
 * the match must never depend on a menu global that outlives the menu. */
static int16_t s_shootoutMatch;
static int16_t s_shootoutStarted;

/* The queue the MATCH shoots by, latched beside the mode flag. Validated on
 * the way in: anything that is not a permutation of slots 1..10 (nothing the
 * screen can produce, but the match must not trust a menu global) falls back
 * to the stock order. */
static uint8_t s_shootoutOrder[2][10];

static void latchShootoutOrder(void)
{
    int16_t side, pos;
    for (side = 0; side < 2; side++) {
        uint16_t seen = 0;
        for (pos = 0; pos < 10; pos++) {
            uint8_t slot = g_shootoutOrder[side][pos];
            if (slot < 1 || slot > 10)
                break;
            if (seen & (1u << slot))
                break;
            seen |= (1u << slot);
            s_shootoutOrder[side][pos] = slot;
        }
        if (pos < 10)                       /* not a permutation: stock order */
            for (pos = 0; pos < 10; pos++)
                s_shootoutOrder[side][pos] = (uint8_t)(10 - pos);
    }
}

void PenaltyShootout_BeginMatch(void)
{
    s_shootoutMatch = (int16_t)(g_penaltyShootoutMode != 0);
    s_shootoutStarted = 0;
    latchShootoutOrder();
}

/* NextPenalty's pick, when the match is the drill: the side about to shoot
 * has taken `attempts` kicks, so its next shooter is the queue's
 * (attempts mod 10)th player. -1 hands the pick back to the engine's own
 * countdown (any match that is not the drill). */
int16_t PenaltyShootout_NextShooterSlot(int16_t teamNumber, int16_t attempts)
{
    if (!s_shootoutMatch)
        return -1;
    if (teamNumber != 1 && teamNumber != 2)
        return -1;
    return (int16_t)s_shootoutOrder[teamNumber - 1][(uint16_t)attempts % 10u];
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

/* ===================================================================
 * The SHOOTING ORDER screen (_shootoutOrderMenu, menu_data.c).
 *
 * Shown by ShowPlayMatchMenu IN PLACE of the PLAY MATCH screen while the mode
 * is on (play_match_menu.c): a match that is only penalties has no use for
 * tactics, squad swaps or substitutes, so the screen asks the one question
 * that matters — who shoots when. The ten outfield players are listed as the
 * queue, positions 1..5 (the best-of-five takers) a gap above 6..10; click a
 * player and then another and they trade places. The rows are drawn with the
 * squad list's own renderer (BuildTransferPlayerMenuEntry into the menu's
 * 12-entry bank, the ViewSquad pattern), so a row reads number, fitness,
 * name, position, skills and value like every other squad screen.
 *
 * The screen keeps ShowPlayMatchMenu's contract: START SHOOTOUT is
 * PlayMatchSelected (which pad pressed it becomes _playerNumThatStarted),
 * EXIT is ExitPlayMatch's abort, and the fire-ownership flags are taken from
 * _playMatchControlFlags exactly as PlayMatchMenuInit takes them, so the
 * two-human flow (each side ordering its own queue in turn) works unchanged.
 * =================================================================== */

/* Runtime ordinals (docs/MENUS.md §11a): 0 title, 1 team name, 2 START,
 * 3 EXIT, 4..13 the queue, 14..25 the row-renderer bank. */
#define SHOOTOUT_TEAMNAME_ORDINAL 1
#define SHOOTOUT_ROW0_ORDINAL     4
#define SHOOTOUT_BANK_ORDINAL     14

extern MenuEntry _menuEntries[];
extern uint16_t _player1ClearFlag;
extern uint16_t _player2ClearFlag;
extern uint16_t _playMatchControlFlags;
extern uint16_t _isFirstPlayerChoosingTeam;
extern uint16_t _playerNumThatStarted;
extern uint16_t _exitGameFlag;
extern uint8_t  _pl1Fire;
extern uint8_t  _pl2Fire;
extern TeamFileEx *_playMatchTeam1Ptr;

extern uint8_t *GetPlayerAtIndex(uint16_t teamNumber, uint16_t ordinal);
extern void BuildTransferPlayerMenuEntry(void *item, uint8_t *player,
                                           uint16_t y, int16_t idx,
                                           uint16_t style, uint8_t *extra);
extern void DrawMultipleItems(uint16_t maxItems, MenuEntry *firstEntry);
extern void DrawMenu(void);
extern void SetExitMenuFlag(void);

/* Which side this showing of the screen orders (0 = in-game team 1), and the
 * queue position picked by the first of the two clicks (-1 = none yet). */
static int16_t s_orderSide;
static int16_t s_orderPick;

/* onInit AND onReturn: every ShowMenu re-unpacks the packed bytes, so
 * everything runtime about the screen is re-established here. The
 * fire-ownership lines are PlayMatchMenuInit's own, kept so the pad that
 * already took a side cannot also press this screen's START for the other. */
__attribute__((used, externally_visible))
void ShootoutOrderMenuInit(void)
{
    _player1ClearFlag = 0;
    _player2ClearFlag = 0;
    if (!(_playMatchControlFlags & 0x01)) _player1ClearFlag = 0xFF;
    if (!(_playMatchControlFlags & 0x02)) _player2ClearFlag = 0xFF;

    s_orderSide = _isFirstPlayerChoosingTeam ? 0 : 1;
    s_orderPick = -1;
    if (_playMatchTeam1Ptr)
        _menuEntries[SHOOTOUT_TEAMNAME_ORDINAL].fg =
            (uintptr_t)_playMatchTeam1Ptr->team.teamName;
}

/* A queue row's afterDraw: render the player at this queue position through
 * the squad-row bank, numbered by his place in the queue. The picked row is
 * drawn in the squad screens' highlight style (0xD) until the second click. */
__attribute__((used, externally_visible))
void ShootoutOrderRowDraw(void *entryPtr)
{
    MenuEntry *a5 = (MenuEntry *)entryPtr;
    int16_t pos = (int16_t)(a5->ordinal - SHOOTOUT_ROW0_ORDINAL);
    uint8_t *player = (uint8_t *)0;

    if (_playMatchTeam1Ptr && pos >= 0 && pos <= 9) {
        uint16_t tNum = PACK_BE_U16(_playMatchTeam1Ptr->team.countryNumber,
                                    _playMatchTeam1Ptr->team.teamOrdinal);
        player = GetPlayerAtIndex(tNum, g_shootoutOrder[s_orderSide][pos]);
    }
    BuildTransferPlayerMenuEntry(&_menuEntries[SHOOTOUT_BANK_ORDINAL], player,
                                 a5->y, (int16_t)(pos + 1),
                                 (pos == s_orderPick) ? 0xD : 0, (uint8_t *)0);
    DrawMultipleItems(12, &_menuEntries[SHOOTOUT_BANK_ORDINAL]);
}

/* A row's fire: first click picks a player up, the second puts him down —
 * on himself to cancel, on another to trade their places in the queue. */
__attribute__((used, externally_visible))
void ShootoutOrderRowSelected(void *entryPtr)
{
    MenuEntry *a5 = (MenuEntry *)entryPtr;
    int16_t pos = (int16_t)(a5->ordinal - SHOOTOUT_ROW0_ORDINAL);
    if (pos < 0 || pos > 9)
        return;

    if (s_orderPick < 0) {
        s_orderPick = pos;
    } else if (s_orderPick == pos) {
        s_orderPick = -1;
    } else {
        uint8_t t = g_shootoutOrder[s_orderSide][s_orderPick];
        g_shootoutOrder[s_orderSide][s_orderPick] = g_shootoutOrder[s_orderSide][pos];
        g_shootoutOrder[s_orderSide][pos] = t;
        s_orderPick = -1;
    }
    DrawMenu();
}

/* START SHOOTOUT — PlayMatchSelected's job: whoever pressed it is the player
 * number ShowPlayMatchMenu returns (SetupPlayers assigns the pads from it). */
__attribute__((used, externally_visible))
void ShootoutOrderStart(void)
{
    if (_pl1Fire)
        _playerNumThatStarted = 1;
    else if (_pl2Fire)
        _playerNumThatStarted = 2;
    else
        return;
    _player1ClearFlag = 0;
    _player2ClearFlag = 0;
    SetExitMenuFlag();
}

/* EXIT — ExitPlayMatch's abort, minus its career bookkeeping (this screen is
 * only ever reached from the drill's friendly flow, never a career). */
__attribute__((used, externally_visible))
void ShootoutOrderExit(void)
{
    _exitGameFlag = 1;
    _player1ClearFlag = 0;
    _player2ClearFlag = 0;
    SetExitMenuFlag();
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
