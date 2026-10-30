/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* bench_subs.c — Chunk 39: Bench / Substitution Controls
 *
 * Ported from SWOS2.s lines 12131-13961:
 *   BenchCheckControls, LeaveSubsMenu, BenchMenuHandleControls,
 *   TrainingSwapBenchTeams, IsThisPlayerOkToSelect, FirePressedInSubsMenu,
 *   ShowFormationMenu, SubsBenchOff, SelectPlayerToSubstituteMenuHandler,
 *   CheckForThrowInAndGoalkeepersBall, SubstitutePlayer,
 *   CallUpdateSubstitutePlayerIndices, state3_handle_fire_press,
 *   MarkPlayersMenuHandler, HandleFormationMenuControls,
 *   UpdateSubstitutePlayerIndices, UpdatePlayerToBeSubstitutedPosition,
 *   IsPlayerOkToSubstitute, HandleFormationMenuMovement,
 *   FindInitialPlayerToBeSubstituted, SwapPlayerNameData,
 *   GetBenchPlayerPointer
 *
 * Per-frame controls for in-game substitution/formation/marking menus.
 * BenchCheckControls is called from GameLoop each frame.
 */

#include "bench_subs.h"
#include "dos_sound.h"
#include "game_types.h"
#include "game_state.h"
#include "goalkeeper.h"
#include "match_sim.h"
#include "player_anim.h"
#include "player_sprites.h"
#include "display.h"    /* _screen_width — widescreen bench-camera fix (no-op on Amiga) */

#ifdef SWOS_DEBUG_AUTOSKIP
#endif

/* ---- external asm globals ---- */
extern uint16_t _inSubstitutesMenu;
extern int16_t  _gsSubsMovementDelay;  /* Amiga 2 / DOS 4 — dos_gameplay.c */
extern int16_t  _gsCameraLeavingSubs;  /* Amiga 50 / DOS 55 — dos_gameplay.c */
extern int      _gDosGameplay;
/* DOS's benchPickTeamCounter — see the team alternation in BenchCheckControls.
 * The Amiga has no such variable; it tests frameCount instead. */
uint16_t _benchPickTeamCounter;
extern uintptr_t _benchTeam;          /* Competition*-shaped pointer; retyped uintptr_t for 64-bit. See project_uint32_pointer_storage_trap.md. */
extern uintptr_t _benchTeamGame;      /* TeamGame*-shaped pointer; retyped uintptr_t for 64-bit. */
extern uint16_t _subsTeamsSwapped;
extern uint16_t _subsDirection;
extern uint8_t  _subsFire;
extern uint16_t _subsFireTimer;
extern uint16_t _subsState;
extern uint16_t _benchPlayerIndex;
extern uint16_t _subsBlockFire;
extern uint16_t _subsBlockDirections;
extern uint16_t _subsMovementDelayTimer;
extern uint16_t _subsPrevDirection;
extern uint16_t _benchOff;
extern uint16_t _cameraCoordinatesValid;
extern uint16_t _waitForPlayerToGoInTimer;
extern uint16_t _cameraLeavingSubsTimer;
extern uint16_t _substituteInProgress;
extern uint16_t _refState;
extern uint16_t _injuriesForever;
extern uint16_t _statsTimer;
extern uint16_t _gameStatePl;
extern uint16_t _gameState;
extern uint16_t _whichCard;
extern uint16_t _playingPenalties;
extern uint16_t _bench1Called;
extern uint16_t _bench2Called;
extern uint16_t _frameCount;
extern uint16_t _trainingGame;
extern uint16_t _subsTrainingTopTeam;
extern uint16_t _benchY;
extern uint16_t _opponentBenchY;
extern uint16_t _plComingYTmp;
extern uint16_t _plComingY;
extern uint16_t _benchWriteOnly01;
extern uint16_t _benchXLimit;
extern uint16_t _subsMenuX;
extern uint16_t _formationMenuX;
extern uint16_t _plLeavingX;
extern uint16_t _plComingX;
extern uint16_t _benchWriteOnly02;
extern uint16_t _deadBenchVar;
extern uint16_t _leavingSubsMenu;
extern uint16_t _overlayActiveFlag;
extern uint16_t _writeOnly01;
extern uint16_t _subsWriteOnly;
extern uint16_t _goToSubsTimer;
extern uint16_t _pl1Direction;
extern uint16_t _pl2Direction;
extern uint8_t  _pl1Fire;
extern uint8_t  _pl2Fire;
extern uint16_t _subsPrevPl1Direction;
extern uint16_t _subsPrevPl2Direction;
extern uint16_t _subsPl1TapCount;
extern uint16_t _subsPl2TapCount;
extern uint16_t _pl1BenchTimeoutCounter;
extern uint16_t _pl2BenchTimeoutCounter;
extern uint16_t _pl1BenchBlockWhileHoldingDirection;
extern uint16_t _pl2BenchBlockWhileHoldingDirection;
extern int16_t  _timeVar;
extern int16_t  _resultTimer;
extern uint16_t _selectedFormationEntry;
extern uint16_t _plToBeSubstitutedPos;
extern uint16_t _plToBeSubstitutedOrd;
extern uint16_t _plToEnterGameIndex;
extern uint16_t _selectedPlayerInSubs;
extern uint16_t _team1NumSubs;
extern uint16_t _team2NumSubs;
extern uint16_t _team1GoalkeeperReplaced;
extern uint16_t _team2GoalkeeperReplaced;
extern uint16_t _team1MostFrequentFace;
extern uint16_t _team2MostFrequentFace;
extern uint16_t _maxSubsPerTeam;
extern uint16_t _gameMaxSubstitutes;
extern Sprite *_substitutedPlSprite;  /* Storage is `Sprite *` (swos_amiga_data_imports.c:283); stale uint32_t extern truncated pointer writes/reads on 64-bit. */
extern uintptr_t _teamThatSubstitutes;  /* TeamGeneralInfo*-shaped pointer; retyped uintptr_t for 64-bit. */
extern uint16_t _substitutedPlX;
extern uint16_t _substitutedPlY;
extern uint16_t _substitutedPlDestX;
extern uint16_t _substitutedPlDestY;
extern uint16_t _plSubstitutedX;
extern uint16_t _plSubstitutedY;
extern uint16_t _pl1Tactics;
extern uint16_t _pl2Tactics;
extern uint16_t _hideBall;
extern uint16_t _cameraXVelocity;
extern uint16_t _cameraYVelocity;
extern TeamGeneralInfo *_lastTeamPlayedBeforeBreak;

extern TeamGeneralInfo _topTeamData;
extern TeamGeneralInfo _bottomTeamData;
extern TeamGame       *_topTeamPtr;
extern TeamGame       *_bottomTeamPtr;
extern TeamGame _topTeamInGame;
extern TeamGame _bottomTeamInGame;
extern uint8_t  _plOrdinalsTable[];
extern uint8_t  _plNamesLengthTableCopy[];
/* 16 slots: 11 on the pitch and 5 on the bench (swos_amiga_data_imports.c). */
#define INGAME_TEAM_PLAYER_SLOTS 16
extern uint16_t _ingameTeamPlayerOffsets[INGAME_TEAM_PLAYER_SLOTS];
extern void *_positionsTable[];
extern AnimTable _playerNormalStandingAnimTable;

/* ---- Game state constants (ST_GAME_IN_PROGRESS, ST_KEEPER_HOLDS_BALL,
 *   ST_STARTING_GAME, ST_GAME_ENDED, ST_STOPPED all from game_types.h) ---- */

/* Position constants (POS_*) come from game_types.h. */

/* ---- Sprite constants ---- */
#define SPR_SELECT_SUBSTITUTE_ARROW 1170
#define SPR_SUBSTITUTES             1174
#define SPR_FORMATION               1175
#define SPR_RED_CROSS_INJURY        1176
#define SPR_RED_CARD_SMALL          139
#define SPR_YELLOW_CARD_SMALL       140

/* ---- TeamGame color offsets (used for kit color reads via field names) ---- */
/* prShirtCol(2), prStripesCol(4), prShortsCol(6), prSocksCol(8) */

/*
 * Helper: get a typed PlayerInfo pointer from a TeamGame and a raw byte offset
 * from ingameTeamPlayerOffsets[].
 *
 * ingameTeamPlayerOffsets[p] stores the byte offset within TeamGame to a position
 * 42 bytes BEFORE the actual PlayerInfo data. Adding 42 aligns to PlayerInfo start.
 * Equivalently, this is &teamGame->players[slot] where the slot index is encoded
 * in the offset.
 */
static inline PlayerInfo *piFromOffset(TeamGame *tg, uint16_t off)
{
    return &tg->players[off / sizeof(PlayerInfo)];
}

/* ---- external data ---- */
extern FixedPoint _cameraX;
extern FixedPoint _cameraY;
extern uint16_t _initialSubsState;
extern uint16_t _drawTrainingTopHalfOfTheBench;
extern uint16_t _currentGameTick;
extern uint16_t _useIndividualPlayerSkinColor;
extern uint16_t _panicReasonCode;
extern uint16_t _smallCharsTable[];
extern uint16_t _ourCoachFrames[];
extern uint16_t _opponentCoachFrames[];
extern uint16_t _benchColors[];
extern uint16_t _subsMenuBackgroundColor;
extern uint16_t _subsMenuHighlightColor;
extern uint16_t _subsMenuEntryFrameColor;
extern uint8_t  playerNumberString[];
extern uint16_t _playerFacesTable[];
extern void *_playerPositionsStringTable[];
extern void *_tacticsStringTable[];
extern void *_teamTacticsTable[];
extern uint8_t  _aFormation[];

/* ---- external functions ---- */
extern void Player1StatusProc(void);
extern void Player2StatusProc(void);
extern void InitCurrentGoalHighlight(void);
extern void CheckIfGoalkeeperClaimedTheBall(void);
extern void InitializePlayerSpriteFrameIndices(void);
extern void ApplyTeamTactics(TeamGame *teamGame);
/* SetPlayerAnimationTable declared in player_anim.h */
extern void StopAllPlayers(void);
extern void DrawSprite16Pixels(uint16_t spriteIndex, uint16_t x, uint16_t y);
extern void DrawSpriteInCameraView(uint16_t spriteIndex, uint16_t x, uint16_t y);
extern void DrawSpriteHotspotInCameraView(uint16_t spriteIndex, uint16_t x, uint16_t y);
extern void FillRectangle(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
extern uint16_t DrawMenuText(uint16_t x, uint16_t y, uint16_t color, uint8_t *text, uint8_t *charsTable);
extern uint16_t DrawMenuTextCentered(uint16_t x, uint16_t y, uint16_t color, uint8_t *text, uint8_t *charsTable);
extern void PanicHaltLoop(void);

/* ---- forward declarations ---- */
static void leaveSubsMenu(void);
static void benchMenuHandleControls(void);
static void trainingSwapBenchTeams(void);
static int16_t isThisPlayerOkToSelect(void);
static void firePressedInSubsMenu(void);
static void showFormationMenu(void);
static void subsBenchOff(void);
static void selectPlayerToSubstituteMenuHandler(void);
static void checkForThrowInAndGoalkeepersBall(void);
static void substitutePlayer(void);
static void state3HandleFirePress(void);
static void markPlayersMenuHandler(void);
static void handleFormationMenuControls(void);
static void updateSubstitutePlayerIndices(void);
static void updatePlayerToBeSubstitutedPosition(void);
static uint16_t isPlayerOkToSubstitute(void);
static void handleFormationMenuMovement(void);
static void findInitialPlayerToBeSubstituted(void);
static void swapPlayerNameData(int16_t enterIndex, int16_t leaveIndex);
static PlayerInfo *getBenchPlayerPointer(uint16_t index, uint16_t *outOrdinal);
static void drawOpponentsBench(void);
static void drawBenchPlayersAndCoach(TeamGame *teamGame, TeamGeneralInfo *team, uint16_t x, uint16_t y);
static void drawSubstitutesMenu(void);
static void drawSubstitutesMenuEntry(uint16_t y, uint16_t baseColor, int16_t selectedIndex,
                                     uint8_t *charsTable, TeamGame *teamGame, uint16_t *playerOffsets);
static void drawFormationMenu(void);
static void drawFormationMenuEntry(uint8_t *charsTable, uint16_t y, uint16_t entryColor, uint16_t entryIndex);

/* ================================================================
 * BenchCheckControls
 * (SWOS2.s line 12131)
 *
 * Per-frame substitution controls check. Called from GameLoop.
 * ================================================================ */
void __attribute__((used, externally_visible))
BenchCheckControls(void)
{
    TeamGeneralInfo *a6;

    if (_inSubstitutesMenu != 0) {
        if ((int16_t)_inSubstitutesMenu < 0)
            return;

        /* menu is active, read input from appropriate player */
        a6 = (TeamGeneralInfo *)_benchTeam;
        if (_subsTeamsSwapped != 0)
            a6 = a6->opponentsTeam;

        {
            if (a6->playerNumber == 1)
                goto read_pl1_input;
            if (a6->playerNumber == 2)
                goto read_pl2_input;
            if (a6->playerCoachNumber != 1)
                goto read_pl2_input;
        }

    read_pl1_input:
        Player1StatusProc();
        _subsDirection = _pl1Direction;
        _subsFire = _pl1Fire;
        goto menu_active_phase;

    read_pl2_input:
        Player2StatusProc();
        _subsDirection = _pl2Direction;
        _subsFire = _pl2Fire;
        goto menu_active_phase;
    }

    /* inSubstitutesMenu == 0: check if bench should activate */
    if (_benchOff != 0) {
        if (_cameraCoordinatesValid != 0)
            _benchOff = 0;
    }

    if (_waitForPlayerToGoInTimer != 0) {
        _waitForPlayerToGoInTimer--;
        return;
    }

    if (_cameraLeavingSubsTimer != 0) {
        _cameraLeavingSubsTimer--;
        return;
    }

    if (_substituteInProgress != 0)
        return;
    if (_refState != 0)
        return;
    if (_injuriesForever != 0)
        return;
    if (_statsTimer != 0)
        return;

    if (_gameStatePl == ST_GAME_IN_PROGRESS)
        goto reset_bench_tap_state;
    if (_whichCard != 0)
        goto reset_bench_tap_state;
    if (_playingPenalties != 0)
        goto reset_bench_tap_state;

    if (_gameState >= ST_STARTING_GAME && _gameState <= ST_GAME_ENDED)
        goto reset_bench_tap_state;

    /* check if either player has called the bench */
    a6 = &_topTeamData;
    if (_bench1Called != 0)
        goto activate_bench;
    a6 = &_bottomTeamData;
    if (_bench2Called != 0)
        goto activate_bench;

    /* Alternate between the two teams so each player's double-tap is sampled
     * every other frame — but the two releases count differently, and it is a
     * DIFFERENT VARIABLE, not a different constant:
     *
     *   Amiga  btst #0,(frameCount+1)      SWOS2.s:18581 — the global frame
     *                                      counter, which advances EVERY frame
     *   DOS    add benchPickTeamCounter,1  swos.asm:@@game_stopped_and_bench_
     *          test byte ptr ...,1         possible — a dedicated counter that
     *                                      advances only when this check runs
     *
     * They are not the same phase: everything above returns early during play
     * (gameStatePl == ST_GAME_IN_PROGRESS, the substitution timers, statsTimer,
     * ...), so the DOS counter stands still for most of a match while
     * frameCount keeps going. Which team gets sampled on a given stoppage frame
     * therefore differs, and with it whose taps are counted — the replay rig
     * caught us opening the substitutes menu on a double-tap the reference
     * never saw (docs/TESTS.md).
     *
     * The DOS build is a static recompilation of the Amiga one, so a difference
     * like this is a deliberate hand edit and deserves proof: benchPickTeamCounter
     * has exactly two references in the DOS listing (the add and the test, plus
     * its definition), the DOS still HAS frameCount elsewhere so the swap was
     * not forced by the conversion, and the recorded alternateTeamsTimer
     * confirms the counter's value for 926 frames. The Amiga path below is
     * unchanged. */
    {
        uint16_t pick;
        if (_gDosGameplay) {
            _benchPickTeamCounter++;
            pick = _benchPickTeamCounter;
        } else {
            pick = _frameCount;
        }
        a6 = (pick & 1) ? &_topTeamData : &_bottomTeamData;
    }

    /* check if team has a human player */
    if ((uint16_t)a6->playerNumber != 0) {
        _subsDirection = (uint16_t)a6->direction;
        _subsFire = a6->firePressed;
        goto check_double_tap;
    }

    if ((uint16_t)a6->playerCoachNumber == 0)
        return;

    if ((uint16_t)a6->playerCoachNumber != 2) {
        Player1StatusProc();
        _subsDirection = _pl1Direction;
        _subsFire = _pl1Fire;
        goto check_double_tap;
    }

    Player2StatusProc();
    _subsDirection = _pl2Direction;
    _subsFire = _pl2Fire;
    goto check_double_tap;

reset_bench_tap_state:
    _subsPrevPl1Direction = 0xFFFF;
    _subsPl1TapCount = 0;
    _pl1BenchTimeoutCounter = 0;
    _subsPrevPl2Direction = 0xFFFF;
    _subsPl2TapCount = 0;
    _pl2BenchTimeoutCounter = 0;
    _bench1Called = 0;
    _bench2Called = 0;
    return;

check_double_tap:
    /* double-tap detection for top team (a6 == &topTeamData) or bottom team */
    if (a6 == &_topTeamData) {
        /* player 1 double-tap detection */
        uint16_t d0 = _subsDirection;
        if ((int16_t)d0 >= 0) {
            if (_pl1BenchBlockWhileHoldingDirection != 0)
                return;
            if ((int16_t)_subsPrevPl1Direction >= 0) {
                if (d0 != _subsPrevPl1Direction)
                    goto pl1_reset_tap;
                _subsPl1TapCount++;
                _pl1BenchTimeoutCounter = 0;
                if (_subsPl1TapCount == 2)
                    goto activate_bench;
            }
            _subsPrevPl1Direction = _subsDirection;
            _pl1BenchBlockWhileHoldingDirection = 1;
            return;
        }
        /* direction released */
        _pl1BenchBlockWhileHoldingDirection = 0;
        _pl1BenchTimeoutCounter++;
        if (_pl1BenchTimeoutCounter != 15)
            return;
    pl1_reset_tap:
        _subsPrevPl1Direction = 0xFFFF;
        _subsPl1TapCount = 0;
        _pl1BenchTimeoutCounter = 0;
        return;
    } else {
        /* player 2 double-tap detection */
        uint16_t d0 = _subsDirection;
        if ((int16_t)d0 >= 0) {
            if (_pl2BenchBlockWhileHoldingDirection != 0)
                return;
            if ((int16_t)_subsPrevPl2Direction >= 0) {
                if (d0 != _subsPrevPl2Direction)
                    goto pl2_reset_tap;
                _subsPl2TapCount++;
                _pl2BenchTimeoutCounter = 0;
                if (_subsPl2TapCount == 2)
                    goto activate_bench;
            }
            _subsPrevPl2Direction = _subsDirection;
            _pl2BenchBlockWhileHoldingDirection = 1;
            return;
        }
        /* direction released */
        _pl2BenchBlockWhileHoldingDirection = 0;
        _pl2BenchTimeoutCounter++;
        if (_pl2BenchTimeoutCounter != 15)
            return;
    pl2_reset_tap:
        _subsPrevPl2Direction = 0xFFFF;
        _subsPl2TapCount = 0;
        _pl2BenchTimeoutCounter = 0;
        return;
    }

activate_bench:
    {
        int16_t d1, d2;
        TeamGame *a0;

#ifdef SWOS_DEBUG_AUTOSKIP
        /* Diagnostic: capture what triggered bench so we can tell
         * the difference between a real double-tap, the keyboard
         * 0x10/0x11 hotkey path, and a phantom direction cycle from
         * an input-pipeline bug. */
        extern uint16_t _bench1Called, _bench2Called;
        extern uint16_t _subsPrevPl1Direction, _subsPl1TapCount;
        extern uint16_t _subsPrevPl2Direction, _subsPl2TapCount;
        extern uint16_t _pl1BenchTimeoutCounter, _pl2BenchTimeoutCounter;
        extern uint16_t _gameState, _gameStatePl;
        SWOS_DBG(
                "[bench] ACTIVATE team=%s gameState=%u gameStatePl=%u "
                "bench1Called=%u bench2Called=%u "
                "pl1{prev=0x%04X tapCount=%u timeout=%u} "
                "pl2{prev=0x%04X tapCount=%u timeout=%u} "
                "subsDir=0x%04X playerNumber=%u\n",
                (a6 == &_topTeamData) ? "TOP" : "BOTTOM",
                (unsigned)_gameState, (unsigned)_gameStatePl,
                (unsigned)_bench1Called, (unsigned)_bench2Called,
                (unsigned)_subsPrevPl1Direction, (unsigned)_subsPl1TapCount,
                (unsigned)_pl1BenchTimeoutCounter,
                (unsigned)_subsPrevPl2Direction, (unsigned)_subsPl2TapCount,
                (unsigned)_pl2BenchTimeoutCounter,
                (unsigned)_subsDirection, (unsigned)a6->playerNumber);
#endif

        _bench1Called = 0;
        _bench2Called = 0;
        _benchTeam = (uintptr_t)a6;
        a0 = _topTeamPtr;
        if ((uint16_t)a6->teamNumber == 2)
            a0 = _bottomTeamPtr;

        _benchTeamGame = (uintptr_t)a0;
        _plToBeSubstitutedPos = 0xFFFF;
        _subsState = 0;
        _benchPlayerIndex = 0;
        _subsBlockFire = 1;
        _subsBlockDirections = 1;
        _inSubstitutesMenu = 30;
        _goToSubsTimer = 15;

        /* negate timers */
        _timeVar = -_timeVar;
        _resultTimer = -_resultTimer;
        _statsTimer = -((int16_t)_statsTimer);

        d1 = 389;   /* top bench Y */
        d2 = 485;   /* bottom bench Y */

        if (_trainingGame != 0) {
            d1 += 67;    /* training game bench coordinates */
            d2 -= 29;
            _subsTrainingTopTeam = 0;
            /* exg d1,d2 */
            { int16_t tmp = d1; d1 = d2; d2 = tmp; }
            if (a0 == &_topTeamInGame) {
                /* benchTeamGame == topTeamInGame: keep as-is */
            } else {
                _subsTrainingTopTeam = 1;
            }
            goto set_bench_coords;
        }

        /* check bit 1 of 2nd char of top team name to decide bench side */
        if ((uint8_t)_topTeamInGame.teamName[0] & 2) {
            int16_t tmp = d1; d1 = d2; d2 = tmp;
        }
        if (a0 != &_topTeamInGame) {
            int16_t tmp = d1; d1 = d2; d2 = tmp;
        }

    set_bench_coords:
        _benchY = (uint16_t)d1;
        _opponentBenchY = (uint16_t)d2;
        _plComingYTmp = 449;
        _plComingY = _plComingYTmp;
        _benchWriteOnly01 = _benchY + 50;
        _benchXLimit = 27;
        _subsMenuX = 100;
        _formationMenuX = 100;
        _plLeavingX = 39;
        _plComingX = 26;
        _benchWriteOnly02 = 35;

        checkForThrowInAndGoalkeepersBall();
        return;
    }

menu_active_phase:
    /* bench menu is active; handle timers and input dispatch */
    {
        int16_t d7;
        a6 = (TeamGeneralInfo *)_benchTeam;

        if (_cameraCoordinatesValid == 0) {
            _goToSubsTimer = 15;
            return;
        }

        if (_goToSubsTimer != 0) {
            _goToSubsTimer--;
            return;
        }

        if ((TeamGeneralInfo *)_benchTeam != a6)  /* cmpa.l benchTeam,a6 */
            return;
        if ((int16_t)_inSubstitutesMenu < 30)
            return;
        /* `tst.w (substituteInProgress); bmi SubstitutePlayer` (SWOS2.s:18623,
         * and `or ax,ax; js` in the DOS asm) — the branch is taken only when
         * the flag is NEGATIVE. A POSITIVE value (1, set the frame the
         * substitution is confirmed) falls THROUGH to the direction and fire
         * handling below. This used to return for any non-zero value, which
         * froze subsFireTimer for the whole substitution animation — the replay
         * rig caught it as a fire timer stuck at 1 where the reference counts
         * on (docs/TESTS.md). */
        if ((int16_t)_substituteInProgress < 0) {
            substitutePlayer();
            return;
        }

        d7 = (int16_t)_subsDirection;
        if (d7 < 0)
            goto clear_block;
        if (_subsBlockDirections != 0) {
            if ((uint16_t)d7 != _subsPrevDirection)
                goto clear_block;
            if (_subsState == 0)
                return;
            if (_subsPrevDirection != 0 && _subsPrevDirection != 4)
                return;
            _subsMovementDelayTimer++;
            /* Amiga 2, DOS 4 — the held-direction repeat delay (dos_gameplay.c). */
            if ((int16_t)_subsMovementDelayTimer != _gsSubsMovementDelay)
                return;
        }

    clear_block:
        _subsBlockDirections = 0;
        _subsMovementDelayTimer = 0;
        _subsPrevDirection = (uint16_t)d7;

        {
            uint16_t d0 = 0;
            if (_subsFire != 0) {
                d0 = _subsFireTimer + 1;
            }
            _subsFireTimer = d0;
        }

        if (_subsBlockFire != 0) {
            if (_subsFire != 0)
                return;
            _subsBlockFire = 0;
        }

        if (_inSubstitutesMenu == 30) {
            benchMenuHandleControls();
            return;
        }
    }
}

/* ================================================================
 * leaveSubsMenu (SWOS2.s line 12406)
 * ================================================================ */
static void leaveSubsMenu(void)
{
    InitCurrentGoalHighlight();
    _benchOff = 1;
    _cameraLeavingSubsTimer = (uint16_t)_gsCameraLeavingSubs;  /* Amiga 50 / DOS 55 */
    _leavingSubsMenu = 1;
    CheckIfGoalkeeperClaimedTheBall();
    _bench1Called = 0;
    _bench2Called = 0;
    _inSubstitutesMenu = 0;
    _subsTeamsSwapped = 0;
    _overlayActiveFlag = 0;
    _writeOnly01 = 1;
    _subsState = 0;
    _plToBeSubstitutedPos = 0xFFFF;
    _pl1BenchTimeoutCounter = 0;
    _pl2BenchTimeoutCounter = 0;
    _subsPrevPl1Direction = 0xFFFF;
    _subsPrevPl2Direction = 0xFFFF;
    _subsPl1TapCount = 0;
    _subsPl2TapCount = 0;
    _subsWriteOnly = 2;

    /* loc_10CF6E: clear deadBenchVar */
    _deadBenchVar = 0;
}

/* ================================================================
 * benchMenuHandleControls (SWOS2.s line 12431)
 * ================================================================ */
static void benchMenuHandleControls(void)
{
    TeamGeneralInfo *a0;

    if (_substituteInProgress != 0)
        return;

    if (_subsState != 0) {
        if (_subsState == 1) {
            selectPlayerToSubstituteMenuHandler();
            return;
        }
        if (_subsState == 3) {
            state3HandleFirePress();
            return;
        }
        if (_subsState == 2) {
            handleFormationMenuControls();
            return;
        }
        if (_subsState == 4) {
            markPlayersMenuHandler();
            return;
        }
        return;
    }

    /* subsState == 0: top-level bench menu */
    a0 = (TeamGeneralInfo *)_benchTeam;
    if (a0 == &_topTeamData) {
        if (_bench1Called != 0) {
            leaveSubsMenu();
            return;
        }
    } else {
        if (_bench2Called != 0) {
            leaveSubsMenu();
            return;
        }
    }

    {
        uint16_t d0 = _subsDirection;

        if (_trainingGame != 0 && _subsTrainingTopTeam != 0) {
            /* inverted controls for training top team */
            if (d0 == 0) goto move_down;
            if (d0 == 4) goto move_up;
            goto check_left_right;
        }

        if (d0 == 0) goto move_up;
        if (d0 == 4) goto move_down;

    check_left_right:
        if (d0 == 2) { leaveSubsMenu(); return; }
        if (d0 == 6) { leaveSubsMenu(); return; }
        if (_subsFire != 0) {
            firePressedInSubsMenu();
            return;
        }
        return;

    move_down:
        if (_gameState == ST_KEEPER_HOLDS_BALL)
            return;
        _subsBlockDirections = 1;
        if (_benchPlayerIndex == 5)
            return;
        /* dead code: clr.w benchPlayerIndex; bra exit_rts removed */
        do {
            _benchPlayerIndex++;
            if (_benchPlayerIndex == 6) goto scan_backward;
            if (isThisPlayerOkToSelect() >= 0) return;
        } while (1);

    move_up:
        if (_trainingGame == 0) {
            /* normal: check keeper holds ball */
        } else {
            if (_benchPlayerIndex == 0) goto training_swap;
        }
        if (_gameState == ST_KEEPER_HOLDS_BALL && _trainingGame == 0)
            return;
        _subsBlockDirections = 1;
        if (_benchPlayerIndex == 0) {
            if (_trainingGame != 0) {
            training_swap:
                trainingSwapBenchTeams();
            }
            return;
            /* dead code: benchPlayerIndex=6, fallthrough removed */
        }

    scan_backward:
        do {
            _benchPlayerIndex--;
            if (_benchPlayerIndex == 0) return;
            if (isThisPlayerOkToSelect() >= 0) return;
        } while (1);
    }
}

/* ================================================================
 * trainingSwapBenchTeams (SWOS2.s line 12526)
 * ================================================================ */
static void trainingSwapBenchTeams(void)
{
    uint16_t tmpY;
    TeamGeneralInfo *a6;
    TeamGame *a0;

    _subsTeamsSwapped ^= 1;
    _subsTrainingTopTeam ^= 1;

    tmpY = _benchY;
    _benchY = _opponentBenchY;
    _opponentBenchY = tmpY;

    a6 = (TeamGeneralInfo *)_benchTeam;
    a6 = a6->opponentsTeam;
    _benchTeam = (uintptr_t)a6;

    a0 = _topTeamPtr;
    if ((uint16_t)a6->teamNumber == 2)
        a0 = _bottomTeamPtr;
    _benchTeamGame = (uintptr_t)a0;
}

/* ================================================================
 * isThisPlayerOkToSelect (SWOS2.s line 12549)
 * Returns: negative = not OK, >= 0 = OK
 * ================================================================ */
/* The negative-position path returns the byte OFFSET, not an error.
 *
 *   tst.b  PLAYERINFOHEADER_POSITION(a0)
 *   bmi.s  locret_10D230        ; SWOS2.s:18963 — straight to the rts,
 *                               ; leaving d0 = (benchPlayerIndex+10)*61
 *
 * and the callers test the SIGN of that word (`if (... >= 0) return;`), so a
 * player with position < 0 comes out NON-NEGATIVE and is treated as SELECTABLE.
 * That reads like an original-game slip, but it is the behaviour on both
 * releases — the DOS recompile inherits it verbatim
 * (`js short @@out` then `mov ax, word ptr D0; or ax,ax; retn`,
 * swos-port/swos/swos.asm:92573). Returning the negative position instead made
 * the bench arrow skip the slot: the replay rig caught the cursor landing on
 * bench slot 2 where the reference has 1, and a substitution offered for the
 * wrong player (docs/TESTS.md). The offset is always small and positive here
 * (at most 15*61), so the sign is never in doubt. */
static int16_t isThisPlayerOkToSelect(void)
{
    uint16_t d0, d1;
    TeamGeneralInfo *a0;

    d0 = _team1NumSubs;
    d1 = _team1GoalkeeperReplaced;
    a0 = (TeamGeneralInfo *)_benchTeam;
    if ((uint16_t)a0->teamNumber != 1) {
        d0 = _team2NumSubs;
        d1 = _team2GoalkeeperReplaced;
    }

    if (d0 == _maxSubsPerTeam)
        return -1;
    /* dead code block at 10D1DE-10D1FC skipped (unreachable after bra.s) */

    /* check player status */
    {
        uint16_t idx = _benchPlayerIndex + 10;
        TeamGame *btg = (TeamGame *)_benchTeamGame;
        PlayerInfo *pi = &btg->players[idx];

        if (pi->skip != 0)
            return -1;
        if ((int8_t)pi->position < 0)
            return (int16_t)(idx * (uint16_t)sizeof(PlayerInfo));  /* d0 = offset */
        if ((uint8_t)pi->numYellowCards >= 2)
            return -1;

        return 0;
    }
}

/* ================================================================
 * firePressedInSubsMenu (SWOS2.s line 12591)
 * ================================================================ */
static void firePressedInSubsMenu(void)
{
    uint16_t d0 = _benchPlayerIndex;

    if (d0 == 0)
        goto marks_menu;

    d0--;
    if ((int16_t)d0 < 0)
        return;

    {
        uint16_t offset = _ingameTeamPlayerOffsets[d0 + 11];
        PlayerInfo *pi = piFromOffset((TeamGame *)_benchTeamGame, offset);

        if ((int8_t)pi->position < 0)
            return;
        if ((uint8_t)pi->numYellowCards >= 2)
            return;
    }

    _subsState = 1;
    _subsBlockFire = 1;
    _overlayActiveFlag = 1;
    _plToEnterGameIndex = _benchPlayerIndex + 10;

    if (_gameMaxSubstitutes > 5 && _plToEnterGameIndex == 11) {
        _plToBeSubstitutedPos = 0;
        return;
    }

    findInitialPlayerToBeSubstituted();
    return;

marks_menu:
    _selectedPlayerInSubs = 0xFFFF;
    _subsState = 4;
    _subsBlockFire = 1;
    _overlayActiveFlag = 1;
    _plToBeSubstitutedOrd = 0xFFFF;
    _plToBeSubstitutedPos = 0xFFFF;
    _selectedPlayerInSubs = 0xFFFF;
}

/* ================================================================
 * showFormationMenu (SWOS2.s line 12634)
 * ================================================================ */
static void showFormationMenu(void)
{
    TeamGeneralInfo *a0 = (TeamGeneralInfo *)_benchTeam;
    _subsState = 2;
    _subsBlockFire = 1;
    _overlayActiveFlag = 1;
    _selectedFormationEntry = (uint16_t)a0->tactics;
}

/* ================================================================
 * subsBenchOff (SWOS2.s line 12644)
 * ================================================================ */
static void subsBenchOff(void)
{
    _subsState = 0;
    _subsBlockDirections = 1;
    _subsBlockFire = 1;
    _overlayActiveFlag = 0;
    _subsWriteOnly = 2;
}

/* ================================================================
 * selectPlayerToSubstituteMenuHandler (SWOS2.s line 12654)
 * ================================================================ */
static void selectPlayerToSubstituteMenuHandler(void)
{
    if (_subsFire == 0) {
        updateSubstitutePlayerIndices();
        return;
    }

    _overlayActiveFlag = 0;
    _substituteInProgress = 1;

    {
        uint16_t *numSubsPtr;
        uint16_t *gkReplacedPtr;
        TeamGeneralInfo *a1 = (TeamGeneralInfo *)_benchTeam;

        if ((uint16_t)a1->teamNumber == 1) {
            numSubsPtr = &_team1NumSubs;
            gkReplacedPtr = &_team1GoalkeeperReplaced;
        } else {
            numSubsPtr = &_team2NumSubs;
            gkReplacedPtr = &_team2GoalkeeperReplaced;
        }

        (*numSubsPtr)++;
        if (_plToEnterGameIndex == 11)
            (*gkReplacedPtr)++;
    }

    _subsBlockFire = 1;
    _subsState = 0;
    _subsWriteOnly = 2;

    {
        TeamGeneralInfo *a0 = (TeamGeneralInfo *)_benchTeam;
        Sprite **sprTable;
        Sprite *sprPtr;

        _teamThatSubstitutes = (uintptr_t)a0;
        sprTable = a0->spritesTable;
        sprPtr = sprTable[_plToBeSubstitutedPos];
        _substitutedPlSprite = sprPtr;

        sprPtr->cards = 0;
        _substitutedPlX = (uint16_t)sprPtr->x.s.i;
        _substitutedPlY = (uint16_t)sprPtr->y.s.i;
        _substitutedPlDestX = _plLeavingX;
        _substitutedPlDestY = _plComingYTmp;
        _plSubstitutedX = 39;
        _plSubstitutedY = 449;
    }
}

/* ================================================================
 * checkForThrowInAndGoalkeepersBall (SWOS2.s line 12695)
 * ================================================================ */
static void checkForThrowInAndGoalkeepersBall(void)
{
    TeamGeneralInfo *a0 = _lastTeamPlayedBeforeBreak;
    Sprite *a1 = a0->controlledPlayer;

    if (a1 != (Sprite *)0) {
        if (a1->playerState == PL_THROW_IN) {
            _hideBall = 0;
            a1->playerState = PL_NORMAL;
            SetPlayerAnimationTable(&_playerNormalStandingAnimTable, a1);
        }
    }

    CheckIfGoalkeeperClaimedTheBall();
}

/* ================================================================
 * substitutePlayer (SWOS2.s line 12712)
 * ================================================================ */
static void __attribute__((optimize("no-tree-loop-distribute-patterns")))
substitutePlayer(void)
{
    Sprite *sprPtr = _substitutedPlSprite;
    int16_t d7, d6;
    TeamGame *a0;
    uint16_t off7, off6;
    int16_t i;

    sprPtr->injuryLevel = 0;
    sprPtr->sentAway = 0;

    d7 = (int16_t)_plToBeSubstitutedPos;
    d6 = (int16_t)_plToEnterGameIndex;
    a0 = (TeamGame *)_benchTeamGame;

    /* reset marked player if it's the one going out */
    if ((uint16_t)a0->markedPlayer == (uint16_t)d7) {
        a0->markedPlayer = (int16_t)0xFFFF;
    }
    if ((uint16_t)a0->markedPlayer == (uint16_t)d6) {
        a0->markedPlayer = (int16_t)0xFFFF;
        if (d7 != 0)
            a0->markedPlayer = (int16_t)(uint16_t)d7;
    }

    swapPlayerNameData(d6, d7);

    off6 = _ingameTeamPlayerOffsets[d6];
    off7 = _ingameTeamPlayerOffsets[d7];
    a0 = (TeamGame *)_benchTeamGame;

    /* mark leaving player as unavailable:
     * ingameTeamPlayerOffsets[p] + 42 = start of PlayerInfo p in TeamGame;
     * position field is at offset 4 within PlayerInfo */
    {
        uint8_t *tgBytes = (uint8_t *)a0;
        tgBytes[off7 + 42 + 4] = 0xFF;  /* PlayerInfo.position = 0xFF */
    }

    /* swap player info data (61 bytes) */
    {
        uint8_t *tgBytes = (uint8_t *)a0;
        for (i = 60; i >= 0; i--) {
            uint8_t tmp = tgBytes[off6 + 42 + i];
            tgBytes[off6 + 42 + i] = tgBytes[off7 + 42 + i];
            tgBytes[off7 + 42 + i] = tmp;
        }
    }

    InitializePlayerSpriteFrameIndices();
    ApplyTeamTactics((TeamGame *)_benchTeamGame);

    _waitForPlayerToGoInTimer = 100;
    _subsBlockFire = 1;
    DOS_SOUND(DOS_EV_SUBSTITUTION, 0);

    leaveSubsMenu();
}

/* ================================================================
 * state3HandleFirePress (SWOS2.s line 12762)
 * ================================================================ */
static void state3HandleFirePress(void)
{
    if (_subsFire != 0) {
        _overlayActiveFlag = 0;
        return;
        /* dead code after return: send-off logic, never reached
         * (original asm has bra.w exit_rts before it) */
    }

    updateSubstitutePlayerIndices();
}

/* ================================================================
 * markPlayersMenuHandler (SWOS2.s line 12782)
 * ================================================================ */
static void __attribute__((optimize("no-tree-loop-distribute-patterns")))
markPlayersMenuHandler(void)
{
    TeamGame *a0;
    uint16_t off;

    /* Check for a long fire press -> toggle the marked player. NOTHING HIGHLIGHTED,
     * NOTHING TO MARK: _plToBeSubstitutedPos is 0xFFFF whenever the cursor is not on a
     * player, and both originals index the table with it anyway.
     *
     * They do not even agree on where that lands. The Amiga's `move.w (a1,d0.w),d0`
     * SIGN-extends (SWOS2.s, MarkPlayersMenuHandler), so index -1 reads the two bytes
     * BEFORE the table -- the low half of a POINTER, the last dc.l of
     * teamFilenamesTable. DOS's `movzx ebx, word ptr D0` (swos.asm:91766) ZERO-extends
     * and reads 131070 bytes PAST it. Two releases, two different pieces of garbage, and
     * this port inherited the DOS one.
     *
     * Whatever it lands on gates a WRITE to markedPlayer, which is match state (it is in
     * the replay setup and travels in the netplay setup). A value that depends on what
     * the linker happened to put 128 KB away is exactly the shape of the goalkeeper
     * price bug -- there the two platforms disagreed and the match desynced. Found by
     * AddressSanitizer over the corpus: one site in 90 runs, 50 hits in a single match.
     *
     * So the read only happens for an index the table actually has. A long press with no
     * player highlighted now does nothing, which is what it means. */
    a0 = (TeamGame *)_benchTeamGame;
    if (_plToBeSubstitutedPos < INGAME_TEAM_PLAYER_SLOTS) {
        off = _ingameTeamPlayerOffsets[_plToBeSubstitutedPos];
        PlayerInfo *pi = piFromOffset(a0, off);
        if ((uint8_t)pi->numYellowCards < 2) {
            if (_subsFireTimer == 8) {
                uint16_t d0 = _plToBeSubstitutedPos;
                a0 = (TeamGame *)_benchTeamGame;
                if ((uint16_t)a0->markedPlayer == d0)
                    d0 = 0xFFFF;
                a0->markedPlayer = (int16_t)d0;
                _selectedPlayerInSubs = 0xFFFF;
                return;
            }
        }
    }

    if (_subsFire != 0) {
        if ((int16_t)_plToBeSubstitutedOrd < 0) {
            /* formation menu */
            _subsWriteOnly = 2;
            showFormationMenu();
            return;
        }

        if (_plToBeSubstitutedOrd == 0)
            return;

        if ((int16_t)_selectedPlayerInSubs < 0) {
            /* select this player */
            if (_subsFireTimer != 1)
                return;
            _selectedPlayerInSubs = _plToBeSubstitutedPos;
            return;
        }

        /* swap two players */
        {
            uint16_t selPos = _selectedPlayerInSubs;
            uint16_t curPos = _plToBeSubstitutedPos;

            if (selPos == curPos) {
                if (_subsFireTimer != 1)
                    return;
                _selectedPlayerInSubs = 0xFFFF;
                return;
            }

            {
                int16_t d7s = (int16_t)selPos;
                int16_t d6s = (int16_t)curPos;
                TeamGame *btg;
                uint16_t off6, off7;

                /* update marked player if needed */
                btg = (TeamGame *)_benchTeamGame;
                if ((uint16_t)btg->markedPlayer == (uint16_t)d6s) {
                    btg->markedPlayer = (int16_t)(uint16_t)d7s;
                } else if ((uint16_t)btg->markedPlayer == (uint16_t)d7s) {
                    btg->markedPlayer = (int16_t)(uint16_t)d6s;
                }

                swapPlayerNameData(d6s, d7s);

                off6 = _ingameTeamPlayerOffsets[d6s];
                off7 = _ingameTeamPlayerOffsets[d7s];
                btg = (TeamGame *)_benchTeamGame;

                /* swap player data (61 bytes at PlayerInfo start) */
                {
                    int16_t k;
                    uint8_t *tgBytes = (uint8_t *)btg;
                    for (k = 60; k >= 0; k--) {
                        uint8_t tmp = tgBytes[off6 + 42 + k];
                        tgBytes[off6 + 42 + k] = tgBytes[off7 + 42 + k];
                        tgBytes[off7 + 42 + k] = tmp;
                    }
                }

                /* swap sprites */
                {
                    TeamGeneralInfo *tgi = (TeamGeneralInfo *)_benchTeam;
                    Sprite **sprTable = tgi->spritesTable;
                    Sprite *spr1 = sprTable[d6s];
                    Sprite *spr2 = sprTable[d7s];
                    int16_t k;
                    int16_t ordTmp;

                    /* Swap the two sprites WHOLE. The original swaps 110 bytes, which is
                     * the whole Sprite on the Amiga; on a 64-bit build its two pointers
                     * make it 118, and 110 stopped short of the last four fields: cards,
                     * injuryLevel, tacklingTimer and sentAway stayed with the SLOT while
                     * the player moved. Swapping places with a sent-off player brought
                     * him back on, and left the man who had taken his place standing
                     * there "sent away" (ReportBot 0037); a booking or an injury changed
                     * owner the same way. */
                    {
                        uint8_t *s1 = (uint8_t *)spr1;
                        uint8_t *s2 = (uint8_t *)spr2;
                        for (k = (int16_t)sizeof(Sprite) - 1; k >= 0; k--) {
                            uint8_t tmp = s1[k];
                            s1[k] = s2[k];
                            s2[k] = tmp;
                        }
                    }

                    /* preserve original ordinals (swap back) */
                    ordTmp = spr1->playerOrdinal;
                    spr1->playerOrdinal = spr2->playerOrdinal;
                    spr2->playerOrdinal = ordTmp;
                }

                InitializePlayerSpriteFrameIndices();
                ApplyTeamTactics((TeamGame *)_benchTeamGame);

                _selectedPlayerInSubs = 0xFFFF;
                _overlayActiveFlag = 0;
                DOS_SOUND(DOS_EV_TACTICS, 0);
                subsBenchOff();
                return;
            }
        }
    }

    /* no fire: update selection indices */
    updateSubstitutePlayerIndices();
}

/* ================================================================
 * handleFormationMenuControls (SWOS2.s line 12889)
 * ================================================================ */
static void handleFormationMenuControls(void)
{
    if (_subsFire != 0) {
        TeamGeneralInfo *a0 = (TeamGeneralInfo *)_benchTeam;
        _overlayActiveFlag = 0;
        a0->tactics = (int16_t)_selectedFormationEntry;

        if ((uint16_t)a0->teamNumber == 1) {
            _pl1Tactics = _selectedFormationEntry;
        } else {
            _pl2Tactics = _selectedFormationEntry;
        }

        ApplyTeamTactics((TeamGame *)_benchTeamGame);
        DOS_SOUND(DOS_EV_TACTICS, 0);
        subsBenchOff();
        return;
    }

    handleFormationMenuMovement();
}

/* ================================================================
 * updateSubstitutePlayerIndices (SWOS2.s line 12916)
 * ================================================================ */
static void updateSubstitutePlayerIndices(void)
{
    uint16_t d7 = _subsDirection;

    if (d7 == 2) { subsBenchOff(); return; }
    if (d7 == 6) { subsBenchOff(); return; }

    /* Outside state 4, a reserve GOALKEEPER coming on (index 11) can only
     * replace the keeper, so the selection cursor is frozen — but ONLY when
     * more than five substitutes are allowed. asm (SWOS2.s:19307):
     *     cmpi.w #5,(gameMaxSubstitutes) ; bhi exit
     * i.e. leave when maxSubstitutes > 5. This was inverted (`<= 5`), and five
     * is the normal maximum, so it froze the cursor for EVERY goalkeeper
     * substitution: whoever it started on was the only player you could take
     * off. Found by the replay rig (docs/TESTS.md).
     * NOTE: this file carries two copies of this routine — the `static`
     * lowercase set below is what runs, the externally_visible uppercase set
     * is a parallel copy declared in bench_subs.h and called by nobody. Fix
     * both until they are merged. */
    if (_subsState != 4) {
        if (_plToEnterGameIndex == 11) {
            if (_gameMaxSubstitutes > 5)
                return;
        }
    }

    d7 = _subsDirection;
    if (d7 == 0) goto select_prev;
    if (d7 == 4) goto select_next;
    return;

select_next:
    _subsBlockDirections = 1;

    if (_subsState == 4) {
        if ((int16_t)_plToBeSubstitutedOrd < 0) {
            _plToBeSubstitutedOrd = 1;
            updatePlayerToBeSubstitutedPosition();
            return;
        }
    }

    if (_plToBeSubstitutedOrd == 10)
        return;

    do {
        _plToBeSubstitutedOrd++;
        if (_plToBeSubstitutedOrd == 11)
            goto dec_scan_backward;
        updatePlayerToBeSubstitutedPosition();
        if (isPlayerOkToSubstitute() == 0) return;
    } while (1);

select_prev:
    _subsBlockDirections = 1;

    if (_subsState == 4) {
        if ((int16_t)_plToBeSubstitutedOrd < 0)
            return;
        if (_plToBeSubstitutedOrd == 1) {
            _plToBeSubstitutedOrd = 0xFFFF;
            _plToBeSubstitutedPos = 0xFFFF;
            return;
        }
    }

    if (_plToBeSubstitutedOrd == 0)
        return;

dec_scan_backward:
    do {
        _plToBeSubstitutedOrd--;
        if ((int16_t)_plToBeSubstitutedOrd < 0)
            goto select_next_inc;
        updatePlayerToBeSubstitutedPosition();
        if (isPlayerOkToSubstitute() == 0) return;
    } while (1);

select_next_inc:
    do {
        _plToBeSubstitutedOrd++;
        if (_plToBeSubstitutedOrd == 11)
            goto dec_scan_backward;
        updatePlayerToBeSubstitutedPosition();
        if (isPlayerOkToSubstitute() == 0) return;
    } while (1);
}

/* ================================================================
 * updatePlayerToBeSubstitutedPosition (SWOS2.s line 12978)
 * ================================================================ */
static void updatePlayerToBeSubstitutedPosition(void)
{
    uint16_t ordinal;
    getBenchPlayerPointer(_plToBeSubstitutedOrd, &ordinal);
    _plToBeSubstitutedPos = ordinal;
}

/* ================================================================
 * isPlayerOkToSubstitute (SWOS2.s line 12989)
 * Returns: 0 = select this player (stop scanning), 1 = skip (continue)
 * Asm: d0=0 stops scan (bne loops on d0!=0), d0=1 keeps scanning.
 * ================================================================ */
static uint16_t isPlayerOkToSubstitute(void)
{
    if (_subsState == 4) {
        if (_plToBeSubstitutedOrd == 0)
            return 1;   /* skip — position 0 in formation mode */
        return 0;       /* select */
    }

    {
        TeamGame *btg = (TeamGame *)_benchTeamGame;
        uint16_t off = _ingameTeamPlayerOffsets[_plToBeSubstitutedPos];
        PlayerInfo *pi = piFromOffset(btg, off);

        /* sent-off players (2+ yellows) can't be substituted — skip */
        if ((uint8_t)pi->numYellowCards >= 2)
            return 1;

        /* in state 3: disallowSubs flag set — skip */
        if (_subsState == 3) {
            if ((int8_t)pi->field_B < 0)
                return 1;
        }

        return 0;   /* select this player */
    }
}

/* ================================================================
 * handleFormationMenuMovement (SWOS2.s line 13017)
 * ================================================================ */
static void handleFormationMenuMovement(void)
{
    uint16_t d7 = _subsDirection;

    if (d7 == 0) goto formation_left;
    if (d7 == 4) goto formation_right;
    if (d7 == 2) { subsBenchOff(); return; }
    if (d7 == 6) { subsBenchOff(); return; }
    return;

formation_right:
    _subsBlockDirections = 1;
    if ((int16_t)_selectedFormationEntry < 0) {
        _selectedFormationEntry = 0;
        return;
    }
    if (_selectedFormationEntry == 0x11)
        return;
    _selectedFormationEntry++;
    return;

formation_left:
    _subsBlockDirections = 1;
    if ((int16_t)_selectedFormationEntry < 0) {
        _selectedFormationEntry = 7;
        return;
    }
    if (_selectedFormationEntry == 0)
        return;
    _selectedFormationEntry--;
}

/* ================================================================
 * findInitialPlayerToBeSubstituted (SWOS2.s line 13055)
 * ================================================================ */
static void findInitialPlayerToBeSubstituted(void)
{
    TeamGame *btg = (TeamGame *)_benchTeamGame;
    uint16_t enterOff;
    int8_t targetPos;
    int16_t firstAvail = -1;   /* d6 */
    int16_t exactMatch = -1;   /* d5 */
    int16_t groupMatch = -1;   /* d4 */
    int16_t d1;

    enterOff = _ingameTeamPlayerOffsets[_plToEnterGameIndex];
    targetPos = piFromOffset(btg, enterOff)->position;

    for (d1 = 0; d1 < 11; d1++) {
        uint16_t ordinal;
        PlayerInfo *pi = getBenchPlayerPointer((uint16_t)d1, &ordinal);

        if ((uint8_t)pi->numYellowCards >= 2)
            goto next_player;

        /* record first available if not yet found */
        if (firstAvail < 0)
            firstAvail = d1;

        /* try group match if not yet found */
        if (groupMatch < 0) {
            int8_t plPos = pi->position;
            if (targetPos == POS_DEFENDER || targetPos == POS_RIGHT_BACK || targetPos == POS_LEFT_BACK) {
                if (plPos == POS_DEFENDER || plPos == POS_RIGHT_BACK || plPos == POS_LEFT_BACK)
                    groupMatch = d1;
            } else if (targetPos == POS_MIDFIELDER || targetPos == POS_RIGHT_WING || targetPos == POS_LEFT_WING) {
                if (plPos == POS_MIDFIELDER || plPos == POS_RIGHT_WING || plPos == POS_LEFT_WING)
                    groupMatch = d1;
            }
        }

        /* try exact position match if not yet found */
        if (exactMatch < 0) {
            if (pi->position == targetPos)
                exactMatch = d1;
        }

    next_player:
        ;
    }

    /* priority: exact > group > first available */
    {
        int16_t result = exactMatch;
        if (result < 0) {
            result = groupMatch;
            if (result < 0)
                result = firstAvail;
        }
        _plToBeSubstitutedOrd = (uint16_t)result;

        {
            uint16_t ordinal;
            getBenchPlayerPointer((uint16_t)result, &ordinal);
            _plToBeSubstitutedPos = ordinal;
        }
    }
}

/* ================================================================
 * swapPlayerNameData (SWOS2.s line 13140)
 * ================================================================ */
static void swapPlayerNameData(int16_t enterIndex, int16_t leaveIndex)
{
    uint8_t *ordTable = _plOrdinalsTable;
    uint8_t *lenTable = _plNamesLengthTableCopy;
    uint8_t tmp;

    if ((uintptr_t)_benchTeamGame != (uintptr_t)&_topTeamInGame) {
        ordTable += 16;
        lenTable += 16;
    }

    /* swap ordinals */
    tmp = ordTable[enterIndex];
    ordTable[enterIndex] = ordTable[leaveIndex];
    ordTable[leaveIndex] = tmp;

    /* swap name lengths */
    tmp = lenTable[enterIndex];
    lenTable[enterIndex] = lenTable[leaveIndex];
    lenTable[leaveIndex] = tmp;
}

/* ================================================================
 * getBenchPlayerPointer (SWOS2.s line 13941)
 *
 * Given a player index (0..10), looks up the player's position
 * in the current formation/tactics, then returns a pointer to
 * the player's info in the in-game team data.
 * Also returns the player's ordinal (0-based) via outOrdinal.
 * ================================================================ */
static PlayerInfo *getBenchPlayerPointer(uint16_t index, uint16_t *outOrdinal)
{
    uint16_t tactics;
    TeamGeneralInfo *a0;
    uint8_t *formOrder;
    uint8_t d0;
    uint16_t off;

    tactics = _pl1Tactics;
    a0 = (TeamGeneralInfo *)_benchTeam;
    if ((uint16_t)a0->teamNumber != 1)
        tactics = _pl2Tactics;

    formOrder = (uint8_t *)_positionsTable[tactics];
    d0 = formOrder[index];
    d0--;  /* convert 1-based to 0-based */

    off = _ingameTeamPlayerOffsets[d0];

    *outOrdinal = (uint16_t)d0;
    return piFromOffset((TeamGame *)_benchTeamGame, off);
}

/* ================================================================
 * DrawBenchAndSubsMenu (SWOS2.s line 13159)
 *
 * Main drawing entry for bench/substitutes menu.
 * Called from GameLoop each frame when bench is active.
 * ================================================================ */
#ifdef SWOS_SDL_BUILD
/* benchXLimit's match value, set by the activation block (SWOS2.s:18776
 * `move.w #27,(benchXLimit)`). Named here because the play-time draw runs
 * before the subs menu has ever set the global. */
#define BENCH_X_LIMIT_PLAY 27

/* The bench geometry is normally established when the subs menu OPENS (see the
 * activation block: benchY/opponentBenchY from 389/485, benchXLimit 27, and the
 * team pointers). During ordinary play none of it has been set, so the
 * play-time draw derives what it needs LOCALLY and touches no menu state — the
 * menu re-establishes all of it on entry either way, and writing those globals
 * from the render path could only corrupt a subs flow in progress. */
static int bench_occupants_visible(void)
{
    /* Same predicate the menu path uses, so the occupants appear exactly when
     * the bench furniture (pitch tiles) is on screen. */
    return (int16_t)(BENCH_X_LIMIT_PLAY + 8) > Render_CameraX();
}

static void draw_bench_occupants_only(void)
{
    /* Bench Y bases and the side convention, mirroring the activation block. */
    uint16_t topY = 389, botY = 485;
    if ((uint8_t)_topTeamInGame.teamName[0] & 2) {
        uint16_t t = topY; topY = botY; botY = t;
    }

    uint16_t savedLimit = _benchXLimit, savedBenchY = _benchY;
    uint16_t savedOppY  = _opponentBenchY, savedState = _subsState;
    uintptr_t savedTeam = _benchTeam, savedTeamGame = _benchTeamGame;
    uint16_t savedInitial = _initialSubsState;

    _benchXLimit     = BENCH_X_LIMIT_PLAY;
    _benchY          = topY;
    _opponentBenchY  = botY;
    _benchTeam       = (uintptr_t)&_topTeamData;
    _benchTeamGame   = (uintptr_t)_topTeamPtr;
    _initialSubsState = 0;      /* idle: neutral coach frames, no selection */

    drawOpponentsBench();
    /* X is _benchXLimit, NOT +12. In the menu path the `d1 += 12` immediately
     * before this call is undoing the `d1 -= 12` taken earlier to place the
     * SELECT SUBSTITUTE arrow (SWOS2.s:19596 `addi.w #12,d1` against the
     * matching subtract) -- it is a restore, not an offset. Reading it as an
     * offset drew the coach and subs 12 px to the RIGHT of the dugouts they
     * are meant to be sitting in. */
    drawBenchPlayersAndCoach((TeamGame *)_benchTeamGame,
                             (TeamGeneralInfo *)_benchTeam,
                             _benchXLimit, _benchY);

    _benchXLimit = savedLimit;   _benchY = savedBenchY;
    _opponentBenchY = savedOppY; _subsState = savedState;
    _benchTeam = savedTeam;      _benchTeamGame = savedTeamGame;
    _initialSubsState = savedInitial;
}
#endif

void __attribute__((used, externally_visible))
DrawBenchAndSubsMenu(void)
{
    uint16_t d0, d1, d2;

    if (_inSubstitutesMenu != 0)
        goto draw_bench;

    if (_leavingSubsMenu == 0) {
#ifdef SWOS_SDL_BUILD
        /* WIDESCREEN: draw the bench occupants during ordinary play.
         *
         * The original returns here, and that is correct FOR 4:3 — not because
         * the bench should be empty, but because it is never on screen. The
         * dugouts sit at pitch x 0..35 (_benchXLimit 27, +8) and MoveCamera
         * clamps its target to a 37 px margin (`#37` four times in SWOS2.s's
         * MoveCamera, boundsMargin here), so during play the leftmost visible
         * column is ~37. The only time an Amiga player ever sees the bench is
         * inside the subs menu, where these same sprites ARE drawn.
         *
         * 2b67c6f made Render_CameraX() the game camera "backed off by half the
         * extra width", so a widescreen view now extends LEFT as well as right
         * and reaches pitch x 0 — revealing a strip the original never showed,
         * with nobody on it. Reported as "empty benches! Poor team, no coach
         * available, no substitutes willing to play!".
         *
         * There is no original behaviour to be faithful to here: the situation
         * cannot arise on real hardware. Deliberate divergence, on the rule that
         * whatever is on screen has to be drawn.
         *
         * Occupants ONLY. The select arrow, drawSubstitutesMenu() and the rest
         * of draw_bench are menu UI and must not appear mid-match. */
        if (bench_occupants_visible()) {
            draw_bench_occupants_only();
        }
#endif
        return;
    }

    /* Release the leaving-subs camera lock once the view has slid the bench off
     * to the left AND the camera has reached its bench-return rest position.
     *
     * The original test held the lock while cameraX < _benchXLimit+8 (=35) —
     * i.e. until the camera panned far enough right that the bench cleared the
     * left edge. That works on 4:3/Amiga: MoveCamera's leaving-subs target
     * (priority 3, targetX=211) rests at cameraX ~43 there and sails past 35.
     *
     * But on a WIDESCREEN view the rest X clamps down to the camera's left bound
     * (~37, or panRange/2 once the view nears the pitch width), and MoveCamera's
     * 3px move-deadzone (dx>>2 == 0 for |dx|<=3) parks the camera up to 3px
     * SHORT of it — at cameraX ~34 on 16:9, ~11 on ultrawide. Both sit below 35,
     * so the lock never released and MoveCamera kept the camera pinned to the
     * bench-return target (targetY=449), killing vertical ball-follow: "camera
     * frozen mid-pitch after calling the bench" (16:9 and wider — the earlier
     * ultrawide-only guard missed 16:9).
     *
     * Fix: also release once the camera has SETTLED at its rest X (can slide no
     * closer), computed the same way MoveCamera does. On 4:3 the rest X (43) is
     * above 35 so the cameraX>=threshold branch still fires first — unchanged on
     * Amiga. No-op on Amiga (rest X 43 > threshold). */
    {
        int16_t threshold = (int16_t)(_benchXLimit + 8);       /* 35 */

        /* Mirror MoveCamera's leaving-subs target (targetX=211, boundsMargin=37)
         * and its X clamp so we know where the camera comes to rest. It has to
         * mirror the SIMULATION geometry, because `_cameraX` compared below is
         * the game camera (game_types.h): MoveCamera subtracts
         * `_sim_view_width / 2` and clamps to SIM_CAMERA_PAN_RANGE, so resting
         * X is 211-168 = 43 at every window size. Using the render width gave
         * 0 on a 672-wide view and the transition never registered as done.
         * Identical at 4:3 and on Amiga, where the two widths are both 336. */
        int16_t restX        = (int16_t)(211 - _sim_view_width / 2);
        int16_t panRange     = (int16_t)(0x2A0 - SCREEN_WIDTH);   /* SetCameraCoordinates' own constant */
        int16_t boundsMargin = 37;
        int16_t xMax         = (int16_t)(panRange - boundsMargin);
        if (xMax <= boundsMargin)      restX = (int16_t)(panRange / 2);
        else if (restX < boundsMargin) restX = boundsMargin;
        else if (restX >= xMax)        restX = xMax;

        int16_t camX = _cameraX.s.i;
        /* Keep holding only while the bench hasn't cleared the edge (camX <
         * threshold) AND the camera can still slide further right toward its
         * rest X (camX more than the 3px deadzone below restX). Once either is
         * satisfied the transition is done — release so ball-follow resumes. */
        if (camX < threshold && camX < (int16_t)(restX - 3))
            goto draw_bench;
    }

    _leavingSubsMenu = 0;
    return;

draw_bench:
    d0 = _benchXLimit;
    d0 += 8;
    /* Is the bench ON SCREEN? That is a RENDER question, so it has to be asked
     * of the render origin — the same one the sprites below are placed with.
     * DrawSpriteInCameraView/DrawSpriteHotspotInCameraView do `x -=
     * Render_CameraX()`, and since 2b67c6f that is `_cameraX - WIDE_MARGIN`,
     * i.e. up to WIDE_MARGIN px further LEFT than the simulation camera.
     *
     * Asking `_cameraX` instead made this gate lag the picture: the bench
     * furniture is pitch tiles, drawn from the render origin, so it scrolled
     * into view while this still returned — and it returns before drawing the
     * coach AND all five substitutes. Measured at 480 wide on a recorded match,
     * 19 rendered frames showed both benches bare with the camera parked at the
     * bench (camY 383 against benchY 389), then everyone appeared at once.
     * Reported as "going to the bench, for a brief moment you see empty benches
     * before the players magically appear".
     *
     * The comparisons ABOVE are a different question — where MoveCamera is
     * panning to — and correctly stay on the simulation camera. Folds to
     * identity at 4:3 and on Amiga, where game_types.h defines
     * Render_CameraX() as _cameraX.s.i.
     *
     * Measured from the SIMULATION draw view (display.h), which is this display's
     * locally: what runs past the gate writes match state (_initialSubsState, the
     * bench colours), so two netplay peers must open it on the same frame. The
     * agreed view is at least as wide as either display, so the bench is never
     * bare on screen; a narrower peer populates it a little early, off-screen. */
    if (d0 <= Sim_DrawCameraX())
        return;

    _initialSubsState = _subsState;

    if (_trainingGame != 0) {
        if (_subsTrainingTopTeam != 0)
            goto skip_opponent_1;
    }
    drawOpponentsBench();

skip_opponent_1:
    d1 = _benchXLimit;
    d2 = _benchY;
    d2 -= 21;
    d2 += 4;
    d0 = _benchPlayerIndex;
    d1 -= 12;
    d0 *= 7;

    if (_trainingGame != 0) {
        if (_subsTrainingTopTeam != 0)
            d0 = (uint16_t)(-(int16_t)d0);
    }

    if (_trainingGame != 0) {
        d0 -= 7;
        if (_subsTrainingTopTeam == 0)
            d0 += 14;
    }

    d2 += d0;

    if (_goToSubsTimer == 0 && _substituteInProgress == 0 && _subsState == 0) {
        uint16_t save_d1 = d1;
        DrawSpriteInCameraView(SPR_SELECT_SUBSTITUTE_ARROW, d1, d2);
        d1 = save_d1;
    }

    d1 += 12;
    d2 = _benchY;
    _drawTrainingTopHalfOfTheBench = _subsTrainingTopTeam;
    drawBenchPlayersAndCoach((TeamGame *)_benchTeamGame, (TeamGeneralInfo *)_benchTeam, d1, d2);

    if (_trainingGame != 0) {
        if (_subsTrainingTopTeam != 0)
            drawOpponentsBench();
    }

    if (_subsState == 1)
        drawSubstitutesMenu();
    else if (_subsState == 3)
        drawSubstitutesMenu();
    else if (_subsState == 2)
        drawFormationMenu();
    else if (_subsState == 4)
        drawSubstitutesMenu();
}

/* ================================================================
 * drawOpponentsBench (SWOS2.s line 13239)
 *
 * Draws the opponent's bench players.
 * ================================================================ */
static void drawOpponentsBench(void)
{
    uint16_t d1, d2;
    TeamGame *a0;
    TeamGeneralInfo *a1;
    uint16_t savedState;

    d1 = _benchXLimit;
    d2 = _opponentBenchY;

    a0 = &_topTeamInGame;
    if (_benchTeamGame != (uintptr_t)&_bottomTeamInGame)
        a0 = &_bottomTeamInGame;

    a1 = (TeamGeneralInfo *)_benchTeam;
    a1 = a1->opponentsTeam; /* movea.l 0(a1),a1 — get opponents team ptr */

    savedState = _subsState;
    _subsState = 0xFFFF;
    _drawTrainingTopHalfOfTheBench = _subsTrainingTopTeam;
    _drawTrainingTopHalfOfTheBench ^= 1;
    drawBenchPlayersAndCoach(a0, a1, d1, d2);
    _subsState = savedState;
}

/* ================================================================
 * drawBenchPlayersAndCoach (SWOS2.s line 13266)
 *
 * Draws coach sprite + bench player sprites.
 * teamGame = TeamGame pointer (team that requested management)
 * team     = TeamGeneralInfo pointer (team data)
 * d1 = x, d2 = y
 * ================================================================ */
static void drawBenchPlayersAndCoach(TeamGame *teamGame, TeamGeneralInfo *team, uint16_t d1, uint16_t d2)
{
    uint16_t d5 = 0, d6 = 0;
    uint16_t d3, d0, d7;
    PlayerInfo *pi;

    if ((uint16_t)team->teamNumber != 1) {
        d5 = 7;
        d6 = 12;
    }

    d2 -= 0x13;
    d2 += 4;

    /* select coach sprite base */
    d3 = 0x49E;
    if (_initialSubsState == 4 || _initialSubsState == 2) {
        if (_benchPlayerIndex == 0)
            d3 = 0x4A1;
    }

    /* animate coach from frame table */
    d0 = _currentGameTick;
    d0 &= 0x1E0;
    d0 >>= 4;
    {
        uint16_t *coachFrames = (uint16_t *)_ourCoachFrames;
        if ((int16_t)_initialSubsState < 0)
            coachFrames = (uint16_t *)_opponentCoachFrames;
        d0 = coachFrames[d0 >> 1];
    }
    d0 += d3;
    d0 += d5;

    /* draw coach sprite */
    if (_trainingGame != 0 && _drawTrainingTopHalfOfTheBench != 0)
        goto skip_coach;

    {
        uint16_t coach_d2 = d2;
        if (_trainingGame != 0) {
            coach_d2 -= 7;
            if (_subsTrainingTopTeam == 0)
                coach_d2 += 14;
        }
        DrawSpriteHotspotInCameraView(d0, d1, coach_d2);
    }

    if (_trainingGame != 0) {
        /* no adjustment needed after the draw — we used a local copy */
    }

skip_coach:
    d2 += 7;
    if (_trainingGame != 0)
        d2 += 7;

    /* advance to player 11 (first substitute) */
    pi = &teamGame->players[11];

    if (_trainingGame != 0 && _drawTrainingTopHalfOfTheBench != 0) {
        d2 -= 0x38;
        pi += 4; /* advance by 4 more players = player 15 */
    }

    for (d7 = 1; d7 < 6; d7++) {
        /* check if player is valid to draw */
        if (pi->skip != 0)
            goto next_player;
        if ((int8_t)pi->position < 0)
            goto next_player;
        if ((uint8_t)pi->numYellowCards >= 2)
            goto next_player;

        if ((int16_t)_subsState < 0)
            goto draw_normal;

        if (_substituteInProgress != 0) {
            d3 = _plToEnterGameIndex;
            d3 -= 10;
            if (d3 == d7)
                goto draw_selected;
            goto draw_normal;
        }

        if (_subsState == 1) {
            if (d7 == _benchPlayerIndex)
                goto draw_selected;
        }
        goto draw_normal;

    draw_selected:
        d0 = 0x4B5;
        if (_gameMaxSubstitutes > 5) {
            if (d7 != 1)
                goto sprite_chosen;
            goto use_gk_sprite;
        }
        if (pi->position != 0)
            goto sprite_chosen;
    use_gk_sprite:
        d0 = 0x4B2;
        goto sprite_chosen;

    draw_normal:
        d0 = 0x4AF;
        if (_gameMaxSubstitutes > 5) {
            if (d7 != 1)
                goto sprite_chosen;
            goto use_gk_normal;
        }
        if (pi->position != 0)
            goto sprite_chosen;
    use_gk_normal:
        d0 = 0x4AC;

    sprite_chosen:
        if (_useIndividualPlayerSkinColor == 0) {
            /* asm uses lea (team2GoalkeeperReplaced),a1 then indexes by teamNumber*2.
             * Layout: [0]=team2GoalkeeperReplaced, [1]=team1MostFrequentFace, [2]=team2MostFrequentFace.
             * So teamNumber 1 → team1MostFrequentFace, teamNumber 2 → team2MostFrequentFace. */
            uint16_t teamNum = (uint16_t)team->teamNumber;
            d3 = (teamNum == 1) ? _team1MostFrequentFace : _team2MostFrequentFace;
        } else {
            d3 = (uint16_t)(int16_t)(int8_t)pi->face;
        }
        d0 += d3;
        d0 += d6;
        DrawSpriteHotspotInCameraView(d0, d1, d2);

    next_player:
        d2 += 7;
        if (_trainingGame != 0 && _drawTrainingTopHalfOfTheBench != 0) {
            pi -= 2; /* go backward: +1 - 2 = -1 */
        } else {
            pi++;
        }
    }
}

/* ================================================================
 * drawSubstitutesMenu (SWOS2.s line 13413)
 *
 * Draws the substitutes selection menu overlay.
 * ================================================================ */
static void drawSubstitutesMenu(void)
{
    uint16_t d1, d2, d3, d4, d7;
    TeamGame *a2 = (TeamGame *)_benchTeamGame;
    uint8_t *charsTable = (uint8_t *)_smallCharsTable;
    uint16_t *playerOffsets = _ingameTeamPlayerOffsets;

    /* fill benchColors from team kit colors */
    {
        uint16_t *bc = _benchColors;
        bc[0] = (uint16_t)a2->prShirtCol;
        bc[1] = (uint16_t)a2->prStripesCol;
        bc[2] = (uint16_t)a2->prShortsCol;
        bc[3] = (uint16_t)a2->prSocksCol;
        bc[4] = 3;
        bc[5] = 1;
        bc[6] = 3;
        bc[7] = 1;
        bc[8] = 0xFFFF;
    }

    /* find first color not 2 or 15 */
    {
        uint16_t *scan = _benchColors;
        do { d1 = *scan++; } while (d1 == 2 || d1 == 15);
    }

    /* find second distinct color not 2 or 15 */
    {
        uint16_t *scan = _benchColors;
        do { d2 = *scan++; } while (d2 == 2 || d2 == 15 || d2 == d1);
    }

    /* find third color */
    {
        uint16_t *scan = _benchColors;
        do { d3 = *scan++; } while (d3 == d1 || d3 == d2);
    }

    _subsMenuBackgroundColor = d1;
    _subsMenuHighlightColor = d2;
    if ((int16_t)d3 < 0)
        d3 = d2;
    _subsMenuEntryFrameColor = d3;

    d2 = 36;

    /* draw "FORMATION" entry for state 3 */
    if (_subsState == 3)
        goto skip_formation_entry;

    if (_subsState == 4) {
        d4 = _subsMenuBackgroundColor;
        if ((int16_t)_plToBeSubstitutedOrd < 0)
            d4 = _subsMenuHighlightColor;
        drawSubstitutesMenuEntry(d2, d4, (int16_t)0xFFFF, charsTable, a2, playerOffsets);
        goto skip_formation_entry;
    }

    /* state 1: draw with plToEnterGameIndex as selected */
    d7 = _plToEnterGameIndex;
    d4 = _subsMenuBackgroundColor;
    drawSubstitutesMenuEntry(d2, d4, (int16_t)d7, charsTable, a2, playerOffsets);

skip_formation_entry:
    d2 += 15;

    /* draw title box - shadow */
    {
        uint16_t saved_d2 = d2;
        uint16_t bx = _subsMenuX - Render_CameraX() + 43;
        FillRectangle(bx, d2, 41, 33, 8);
        FillRectangle(bx - 2, d2 - 2, 41, 33, _subsMenuBackgroundColor);
        FillRectangle(bx - 1, d2 - 1, 39, 31, _subsMenuHighlightColor);

        /* draw title sprite */
        {
            uint16_t tx = bx - 1 + 2;
            uint16_t ty = d2 - 1 + 2;
            uint16_t spr = SPR_SUBSTITUTES;
            if (_subsState != 1 && _subsState != 4) {
                _panicReasonCode = 0x4554;
                PanicHaltLoop();
            }
            DrawSprite16Pixels(spr, tx, ty);
        }
        d2 = saved_d2;
    }

    d2 += 0x25; /* 37 */

    /* draw each player entry */
    for (d7 = 0; d7 < 11; d7++) {
        uint16_t ordinal;
        uint16_t save_d7 = d7;

        getBenchPlayerPointer(d7, &ordinal);
        d7 = ordinal;

        d4 = _subsMenuBackgroundColor;
        if (d7 == _plToBeSubstitutedPos)
            d4 = _subsMenuHighlightColor;

        if (_subsState == 4) {
            if (d7 == _selectedPlayerInSubs) {
                d4 = 10;
                if (d4 == _subsMenuBackgroundColor || d4 == _subsMenuHighlightColor) {
                    d4 = 11;
                    if (d4 == _subsMenuBackgroundColor || d4 == _subsMenuHighlightColor)
                        d4 = 14;
                }
            }
        }

        drawSubstitutesMenuEntry(d2, d4, (int16_t)d7, charsTable, a2, playerOffsets);
        d7 = save_d7;
        d2 += 10;
    }
}

/* ================================================================
 * drawSubstitutesMenuEntry (SWOS2.s line 13566)
 *
 * Draws a single entry in the substitutes menu.
 * d2 = y, d4 = base color, d7 = selected player index (-1 = "FORMATION")
 * a1 = chars table, a2 = team game, a4 = player offsets
 * ================================================================ */
static void drawSubstitutesMenuEntry(uint16_t y, uint16_t baseColor, int16_t selectedIndex,
                                     uint8_t *charsTable, TeamGame *teamGame, uint16_t *playerOffsets)
{
    uint16_t x0, d6, d5;
    int16_t d7 = selectedIndex;

    /* draw shadow rectangle */
    x0 = _subsMenuX - Render_CameraX();
    FillRectangle(x0, y, 126, 11, 8);

    /* draw frame rectangle */
    FillRectangle(x0 - 2, y - 2, 126, 11, _subsMenuEntryFrameColor);

    /* draw background rectangle */
    FillRectangle(x0 - 1, y - 1, 124, 9, baseColor);

    /* "FORMATION" entry (d7 == -1 in state 4) */
    if (_subsState == 4 && d7 == -1) {
        uint16_t cx = _subsMenuX - Render_CameraX() + 63;
        y += 1;
        DrawMenuTextCentered(cx, y, 2, _aFormation, charsTable);
        y -= 1;
        return;
    }

    /* get player offset from offsets table */
    d6 = playerOffsets[(uint16_t)d7];

    /* get shirt number — PlayerInfo is at offset (d6 + 42) from TeamGame base */
    {
        PlayerInfo *pi = piFromOffset(teamGame, d6);
        d5 = (uint16_t)(int16_t)(int8_t)pi->shirtNumber;

        /* draw face sprite if individual skin color enabled */
        if (_useIndividualPlayerSkinColor != 0) {
            uint16_t faceIdx = (uint16_t)(int16_t)(int8_t)pi->face;
            uint16_t faceSpr = _playerFacesTable[faceIdx];
            uint16_t fx = _subsMenuX - Render_CameraX();

            /* check if this player is marked — blink face sprite */
            {
                TeamGame *btg = (TeamGame *)_benchTeamGame;
                if ((uint16_t)btg->markedPlayer == (uint16_t)d7) {
                    if ((_frameCount & 4) == 0)
                        goto skip_face;
                }
            }
            DrawSprite16Pixels(faceSpr, fx, y);
        }
    skip_face:;
    }

    /* check for permanent injury (FFFE) */
    {
        TeamGeneralInfo *bt = (TeamGeneralInfo *)_benchTeam;
        Sprite **sprites = bt->spritesTable;
        Sprite *spr = sprites[(uint16_t)d7];
        if (spr->injuryLevel == (int16_t)0xFFFE) {
            uint16_t ix = _subsMenuX - Render_CameraX();
            if (_useIndividualPlayerSkinColor != 0)
                ix += 9;
            DrawSprite16Pixels(SPR_RED_CROSS_INJURY, ix, y);
            goto draw_name;
        }
    }

    /* draw shirt number */
    {
        uint16_t nx = _subsMenuX - Render_CameraX() + 4;
        if (_useIndividualPlayerSkinColor != 0)
            nx += 9;

        {
            uint8_t numStr[4];
            uint8_t *p = numStr;
            uint8_t num = (uint8_t)d5;
            if (num >= 10) {                /* (0..99: the game wrote 10..19 only, 23 as "1=") */
                *p++ = (uint8_t)('0' + num / 10 % 10);
                num %= 10;
            }
            *p = num + '0';
            p++;
            *p = 0;

            y += 1;
            DrawMenuTextCentered(nx, y, 2, numStr, charsTable);
        }
    }

    /* draw card icon if applicable (only for players 0-10) */
    if ((uint16_t)d7 < 11) {
        TeamGeneralInfo *bt = (TeamGeneralInfo *)_benchTeam;
        Sprite **sprites = bt->spritesTable;
        Sprite *spr = sprites[(uint16_t)d7];
        int16_t cards = spr->cards;
        if (cards != 0) {
            uint16_t cardSpr;
            if (cards < 0) {
                cardSpr = SPR_RED_CARD_SMALL;
            } else {
                cardSpr = SPR_YELLOW_CARD_SMALL;
            }
            y -= 1;
            {
                uint16_t cx = _subsMenuX - Render_CameraX();
                if (_useIndividualPlayerSkinColor != 0)
                    cx += 9;
                DrawSprite16Pixels(cardSpr, cx, y);
            }
            y += 1;
        }
    }

draw_name:
    /* draw player short name */
    {
        PlayerInfo *pi = piFromOffset(teamGame, d6);
        uint16_t nameX = _subsMenuX - Render_CameraX() + 9;
        if (_useIndividualPlayerSkinColor != 0)
            nameX += 9;
        DrawMenuText(nameX, y, 2, (uint8_t *)pi->shortName, charsTable);
    }

    /* draw position string */
    {
        PlayerInfo *pi = piFromOffset(teamGame, d6);
        uint8_t *posStr = (uint8_t *)_playerPositionsStringTable[(uint16_t)(int16_t)(int8_t)pi->position];
        uint16_t posX = _subsMenuX - Render_CameraX() + 116;
        DrawMenuTextCentered(posX, y, 2, posStr, charsTable);
    }
    y -= 1;

    /* draw injury indicator */
    {
        PlayerInfo *pi = piFromOffset(teamGame, d6);
        uint8_t injByte = (uint8_t)pi->injuriesBitfield;
        if (injByte == 0)
            return;
        if (injByte == 0x20)
            goto draw_injury;
        {
            uint16_t level = (injByte & 0xE0) >> 5;
            uint16_t frame = _frameCount & 7;
            if (level < frame)
                return;
        }
    draw_injury:
        {
            uint16_t ix = _subsMenuX - Render_CameraX() + 101;
            DrawSprite16Pixels(SPR_RED_CROSS_INJURY, ix, y);
        }
    }
}

/* ================================================================
 * drawFormationMenu (SWOS2.s line 13757)
 *
 * Draws the formation selection menu.
 * ================================================================ */
static void drawFormationMenu(void)
{
    uint16_t d1, d2, d3, d7;
    TeamGame *a2 = (TeamGame *)_benchTeamGame;
    uint8_t *charsTable = (uint8_t *)_smallCharsTable;

    /* fill benchColors from team kit colors (same as drawSubstitutesMenu) */
    {
        uint16_t *bc = _benchColors;
        bc[0] = (uint16_t)a2->prShirtCol;
        bc[1] = (uint16_t)a2->prStripesCol;
        bc[2] = (uint16_t)a2->prShortsCol;
        bc[3] = (uint16_t)a2->prSocksCol;
        bc[4] = 3;
        bc[5] = 1;
        bc[6] = 3;
        bc[7] = 1;
        bc[8] = 0xFFFF;
    }

    /* find first color not 2 or 15 */
    {
        uint16_t *scan = _benchColors;
        do { d1 = *scan++; } while (d1 == 2 || d1 == 15);
    }

    /* find second distinct color */
    {
        uint16_t *scan = _benchColors;
        do { d2 = *scan++; } while (d2 == 2 || d2 == 15 || d2 == d1);
    }

    /* find third color */
    {
        uint16_t *scan = _benchColors;
        do { d3 = *scan++; } while (d3 == d1 || d3 == d2);
    }

    _subsMenuBackgroundColor = d1;
    _subsMenuHighlightColor = d2;
    if ((int16_t)d3 < 0)
        d3 = d2;
    _subsMenuEntryFrameColor = d3;

    d2 = 11;
    d2 += 15;

    /* draw title box */
    {
        uint16_t saved_d2 = d2;
        uint16_t bx = _formationMenuX - Render_CameraX() + 10;
        FillRectangle(bx, d2, 41, 33, 8);
        FillRectangle(bx - 2, d2 - 2, 41, 33, _subsMenuBackgroundColor);
        FillRectangle(bx - 1, d2 - 1, 39, 31, _subsMenuHighlightColor);

        /* draw title sprite */
        {
            uint16_t tx = bx - 1 + 2;
            uint16_t ty = d2 - 1 + 2;
            DrawSprite16Pixels(SPR_FORMATION, tx, ty);
        }
        d2 = saved_d2;
    }

    d2 += 37;

    /* draw each formation entry */
    for (d7 = 0; d7 < 18; d7++) {
        uint16_t color = _subsMenuBackgroundColor;
        if (d7 == _selectedFormationEntry)
            color = _subsMenuHighlightColor;
        drawFormationMenuEntry(charsTable, d2, color, d7);
        d2 += 10;
    }
}

/* ================================================================
 * drawFormationMenuEntry (SWOS2.s line 13858)
 *
 * Draws a single formation menu entry.
 * d2 = y, d4 = entry color, d7 = entry index, a1 = chars table
 * ================================================================ */
static void drawFormationMenuEntry(uint8_t *charsTable, uint16_t y, uint16_t entryColor, uint16_t entryIndex)
{
    uint16_t x0;
    uint8_t *textStr;

    /* draw shadow */
    x0 = _formationMenuX - Render_CameraX();
    FillRectangle(x0, y, 60, 11, 8);

    /* draw frame */
    FillRectangle(x0 - 2, y - 2, 60, 11, _subsMenuEntryFrameColor);

    /* draw background */
    FillRectangle(x0 - 1, y - 1, 58, 9, entryColor);

    /* get formation name string */
    if (entryIndex < 12) {
        textStr = (uint8_t *)_tacticsStringTable[entryIndex];
    } else {
        textStr = (uint8_t *)_teamTacticsTable[entryIndex];
    }

    /* draw centered text */
    {
        uint16_t tx = _formationMenuX - Render_CameraX() + 30;
        DrawMenuTextCentered(tx, y, 2, textStr, charsTable);
    }
}

/* ================================================================
 * Chunk 117: ShowFormationMenu + SubsBenchOff
 * ================================================================ */

/* ShowFormationMenu — set state to show formation selection overlay (SWOS2.s line 10015) */
__attribute__((used, externally_visible))
void ShowFormationMenu(void)
{
    _subsState = 2;
    _subsBlockFire = 1;
    _overlayActiveFlag = 1;
    TeamGeneralInfo *team = (TeamGeneralInfo *)_benchTeam;
    _selectedFormationEntry = (uint16_t)team->tactics;
}

/* SubsBenchOff — dismiss bench/subs overlay (SWOS2.s line 10025) */
__attribute__((used, externally_visible))
void SubsBenchOff(void)
{
    _subsState = 0;
    _subsBlockDirections = 1;
    _subsBlockFire = 1;
    _overlayActiveFlag = 0;
    _subsWriteOnly = 2;
}

/* ================================================================
 * Chunk 153: GetBenchPlayerPointer  (SWOS2.s line 11315)
 *
 * Maps a bench player index (0-10) to an in-game player pointer.
 * Uses the current tactics to select the formation position table,
 * looks up the player ordinal, then computes the offset into the
 * team's in-game player data.
 *
 * Returns: ordinal (uint16_t), stores pointer in benchPlayerResultA0
 * ================================================================ */

/* Result pointer for asm bridge (asm reads this into A0) */
uint8_t *_benchPlayerResultA0;

__attribute__((used, externally_visible))
uint16_t GetBenchPlayerPointer(uint16_t index)
{
    uint16_t tactics = _pl1Tactics;
    TeamGeneralInfo *team = (TeamGeneralInfo *)_benchTeam;
    if (team->teamNumber != 1)
        tactics = _pl2Tactics;

    uint8_t *posTable = (uint8_t *)_positionsTable[tactics];
    uint16_t ordinal = (uint16_t)posTable[index] - 1;

    uint16_t offset = _ingameTeamPlayerOffsets[ordinal];
    _benchPlayerResultA0 = (uint8_t *)_benchTeamGame + offset;

    return ordinal;
}

/* ================================================================
 * UpdatePlayerToBeSubstitutedPosition  (SWOS2.s line 10351)
 *
 * Reads plToBeSubstitutedOrd, calls GetBenchPlayerPointer to get
 * the position ordinal, and stores result in plToBeSubstitutedPos.
 * ================================================================ */
__attribute__((used, externally_visible))
void UpdatePlayerToBeSubstitutedPosition(void)
{
    _plToBeSubstitutedPos = GetBenchPlayerPointer(_plToBeSubstitutedOrd);
}

/* ================================================================
 * IsPlayerOkToSubstitute  (SWOS2.s line 10359)
 *
 * Checks if the currently highlighted player can be substituted.
 * Returns: 1 = ok to substitute, 0 = do not substitute.
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t IsPlayerOkToSubstitute(void)
{
    if (_subsState == 4) {
        /* In state 4, only ordinal 0 (goalkeeper) is ok */
        if (_plToBeSubstitutedOrd == 0)
            return 1;
        return 0;
    }

    /* Check candidate player's status */
    uint16_t offset = _ingameTeamPlayerOffsets[_plToBeSubstitutedPos];
    PlayerInfo *pi = piFromOffset((TeamGame *)_benchTeamGame, offset);

    /* Red-carded player (numYellowCards >= 2) — ok to sub off */
    if ((uint8_t)pi->numYellowCards >= 2)
        return 1;

    /* In state 3, check disallow subs flag (bit 7 of field_B) */
    if (_subsState == 3) {
        if ((int8_t)pi->field_B < 0)
            return 1;
    }

    return 0;
}

/* ================================================================
 * FindInitialPlayerToBeSubstituted  (SWOS2.s line 10425)
 *
 * Searches the team for a player matching the position of the player
 * about to enter the game. Priority: exact match > group match > first
 * available. Sets plToBeSubstitutedOrd and plToBeSubstitutedPos.
 * ================================================================ */
__attribute__((used, externally_visible))
void FindInitialPlayerToBeSubstituted(void)
{
    TeamGame *tg = (TeamGame *)_benchTeamGame;

    /* Get position of the player entering the game */
    uint16_t enterOff = _ingameTeamPlayerOffsets[_plToEnterGameIndex];
    int8_t targetPos = piFromOffset(tg, enterOff)->position;

    int16_t firstAvailable = -1;   /* d6 */
    int16_t exactMatch = -1;       /* d5 */
    int16_t groupMatch = -1;       /* d4 */

    for (uint16_t i = 0; i < 11; i++) {
        GetBenchPlayerPointer(i);
        PlayerInfo *pi = (PlayerInfo *)(_benchPlayerResultA0 + TEAMGAME_PLAYERS_OFFSET);

        /* Skip red-carded players */
        if ((uint8_t)pi->numYellowCards >= 2)
            continue;

        /* Record first available (if not yet found) */
        if (firstAvailable < 0)
            firstAvailable = (int16_t)i;

        /* Group match check (only if no group match found yet) */
        if (groupMatch < 0) {
            int8_t pp = pi->position;

            if (targetPos == POS_DEFENDER || targetPos == POS_RIGHT_BACK || targetPos == POS_LEFT_BACK) {
                /* Defender group: target is DEF/RB/LB */
                if (pp == POS_DEFENDER || pp == POS_RIGHT_BACK || pp == POS_LEFT_BACK)
                    groupMatch = (int16_t)i;
            } else if (targetPos == POS_MIDFIELDER || targetPos == POS_RIGHT_WING || targetPos == POS_LEFT_WING) {
                /* Midfielder group: target is MID/RW/LW */
                if (pp == POS_MIDFIELDER || pp == POS_RIGHT_WING || pp == POS_LEFT_WING)
                    groupMatch = (int16_t)i;
            }
        }

        /* Exact position match (only if no exact match found yet) */
        if (exactMatch < 0) {
            if (pi->position == targetPos)
                exactMatch = (int16_t)i;
        }
    }

    /* Pick best match: exact > group > first available */
    int16_t selected;
    if (exactMatch >= 0)
        selected = exactMatch;
    else if (groupMatch >= 0)
        selected = groupMatch;
    else
        selected = firstAvailable;

    _plToBeSubstitutedOrd = (uint16_t)selected;
    _plToBeSubstitutedPos = GetBenchPlayerPointer((uint16_t)selected);
}

/* ================================================================
 * HandleFormationMenuMovement  (SWOS2.s line 10368)
 *
 * Handles left/right/up/down input while in the formation menu.
 * Left/right cycle through selectedFormationEntry (0-17).
 * Up/down call SubsBenchOff to exit.
 * ================================================================ */
__attribute__((used, externally_visible))
void HandleFormationMenuMovement(void)
{
    uint16_t dir = _subsDirection;

    if (dir == 0) {
        /* Left press */
        _subsBlockDirections = 1;
        if ((int16_t)_selectedFormationEntry >= 0) {
            if (_selectedFormationEntry == 0)
                return;
            _selectedFormationEntry--;
            return;
        }
        _selectedFormationEntry = 7;
        return;
    }

    if (dir == 4) {
        /* Right press */
        _subsBlockDirections = 1;
        if ((int16_t)_selectedFormationEntry >= 0) {
            if (_selectedFormationEntry == 0x11)
                return;
            _selectedFormationEntry++;
            return;
        }
        _selectedFormationEntry = 0;
        return;
    }

    if (dir == 2 || dir == 6) {
        /* Up or down press — exit formation menu */
        SubsBenchOff();
        return;
    }

    /* Other directions: do nothing */
}

/* ================================================================
 * SwapPlayerNameData  (SWOS2.s line 10416)
 *
 * Swaps two entries in the plOrdinalsTable and plNamesLengthTableCopy
 * arrays. If the bench team is the bottom team, uses offset +16.
 * ================================================================ */
__attribute__((used, externally_visible))
void SwapPlayerNameData(uint16_t enterIndex, uint16_t leaveIndex)
{
    uint8_t *ordinals = (uint8_t *)&_plOrdinalsTable;
    uint8_t *nameLens = (uint8_t *)&_plNamesLengthTableCopy;

    if (_benchTeamGame != (uintptr_t)&_topTeamInGame) {
        ordinals += 16;
        nameLens += 16;
    }

    /* Swap ordinals */
    uint8_t tmp = ordinals[enterIndex];
    ordinals[enterIndex] = ordinals[leaveIndex];
    ordinals[leaveIndex] = tmp;

    /* Swap name lengths */
    tmp = nameLens[enterIndex];
    nameLens[enterIndex] = nameLens[leaveIndex];
    nameLens[leaveIndex] = tmp;
}

/* ================================================================
 * Chunk 156: TrainingSwapBenchTeams + LeaveSubsMenu +
 *            CheckForThrowInAndGoalkeepersBall
 * ================================================================ */

/* ---- TrainingSwapBenchTeams  (SWOS2.s line 9907) ----
 * Swaps the active bench team during a training match.
 * Toggles swap flags, swaps Y positions, switches to opponent team,
 * and updates benchTeamGame pointer.
 */
__attribute__((used, externally_visible))
void TrainingSwapBenchTeams(void)
{
    _subsTeamsSwapped ^= 1;
    _subsTrainingTopTeam ^= 1;

    /* Swap bench Y positions */
    uint16_t tmp = _benchY;
    _benchY = _opponentBenchY;
    _opponentBenchY = tmp;

    /* Switch to opponent team */
    TeamGeneralInfo *team = (TeamGeneralInfo *)_benchTeam;
    team = team->opponentsTeam;
    _benchTeam = (uintptr_t)team;

    /* Set benchTeamGame based on team number */
    TeamGame *gamePtr = _topTeamPtr;
    if (team->teamNumber == 2)
        gamePtr = _bottomTeamPtr;
    _benchTeamGame = (uintptr_t)gamePtr;
}

/* ---- LeaveSubsMenu  (SWOS2.s line 9787) ----
 * Clean exit from substitution menu. Resets all subs-related state,
 * re-initializes goal highlight, triggers bench-off camera transition.
 * Falls through to loc_10CF6E which clears deadBenchVar.
 */
__attribute__((used, externally_visible))
void LeaveSubsMenu(void)
{
    InitCurrentGoalHighlight();
    _benchOff = 1;
    _cameraLeavingSubsTimer = (uint16_t)_gsCameraLeavingSubs;  /* Amiga 50 / DOS 55 */
    _leavingSubsMenu = 1;
    CheckIfGoalkeeperClaimedTheBall();
    _bench1Called = 0;
    _bench2Called = 0;
    _inSubstitutesMenu = 0;
    _subsTeamsSwapped = 0;
    _overlayActiveFlag = 0;
    _writeOnly01 = 1;
    _subsState = 0;
    _plToBeSubstitutedPos = 0xFFFF;
    _pl1BenchTimeoutCounter = 0;
    _pl2BenchTimeoutCounter = 0;
    _subsPrevPl1Direction = 0xFFFF;
    _subsPrevPl2Direction = 0xFFFF;
    _subsPl1TapCount = 0;
    _subsPl2TapCount = 0;
    _subsWriteOnly = 2;
    _deadBenchVar = 0;  /* from loc_10CF6E tail */
}

/* ---- CheckForThrowInAndGoalkeepersBall  (SWOS2.s line 10068) ----
 * End-of-break handler: if the controlled player was in throw-in state,
 * transition them back to normal. Then check if goalkeeper claimed ball.
 */
__attribute__((used, externally_visible))
void CheckForThrowInAndGoalkeepersBall(void)
{
    Sprite *player = _lastTeamPlayedBeforeBreak->controlledPlayer;
    if (player != 0) {
        if (player->playerState == PL_THROW_IN) {
            _hideBall = 0;
            player->playerState = PL_NORMAL;
            SetPlayerAnimationTable(&_playerNormalStandingAnimTable, player);
        }
    }
    CheckIfGoalkeeperClaimedTheBall();
}

/* ================================================================
 * Chunk 157: IsThisPlayerOkToSelect + FirePressedInSubsMenu +
 *            HandleFormationMenuControls + UpdateSubstitutePlayerIndices +
 *            CallUpdateSubstitutePlayerIndices + state3_handle_fire_press
 * ================================================================ */

/* ---- IsThisPlayerOkToSelect  (SWOS2.s line 9896) ----
 * Checks if the bench player at benchPlayerIndex is valid for selection.
 * Returns 0 = OK, -1 = not OK. Caller checks sign flag (bmi).
 */
__attribute__((used, externally_visible))
int16_t IsThisPlayerOkToSelect(void)
{
    uint16_t numSubs = _team1NumSubs;
    TeamGeneralInfo *team = (TeamGeneralInfo *)_benchTeam;
    if (team->teamNumber != 1)
        numSubs = _team2NumSubs;

    /* Max substitutions reached? */
    if (numSubs == _maxSubsPerTeam)
        return -1;

    /* Compute player data pointer: (benchPlayerIndex + 10) * 61 */
    uint16_t playerIdx = _benchPlayerIndex + 10;
    uint32_t offset = (uint32_t)playerIdx * PLAYERINFO_SIZE;
    PlayerInfo *pi = (PlayerInfo *)((uint8_t *)_benchTeamGame + offset + TEAMGAME_PLAYERS_OFFSET);

    /* Already substituted? (pi->skip repurposed as substitution flag) */
    if (pi->skip != 0)
        return -1;

    /* Inactive player: the asm falls straight to the rts with d0 still holding
     * the OFFSET, and callers test its sign — so this is NON-NEGATIVE, i.e.
     * selectable. See the note on isThisPlayerOkToSelect above. */
    if (pi->position < 0)
        return (int16_t)(uint16_t)offset;

    /* Red card or worse? */
    if ((uint8_t)pi->numYellowCards >= 2)
        return -1;

    return 0;
}

/* ---- FirePressedInSubsMenu  (SWOS2.s line 9938) ----
 * Handles fire button press in bench menu (subsState == 0).
 * benchPlayerIndex == 0: enters mark-players mode (state 4).
 * benchPlayerIndex >= 1: starts substitution (state 1).
 */
__attribute__((used, externally_visible))
void FirePressedInSubsMenu(void)
{
    uint16_t idx = _benchPlayerIndex;

    if (idx == 0) {
        /* Mark players mode */
        _selectedPlayerInSubs = 0xFFFF;
        _subsState = 4;
        _subsBlockFire = 1;
        _overlayActiveFlag = 1;
        _plToBeSubstitutedOrd = 0xFFFF;
        _plToBeSubstitutedPos = 0xFFFF;
        _selectedPlayerInSubs = 0xFFFF;
        return;
    }

    /* Check if bench player is valid for substitution */
    uint16_t ordinal = idx + 10;
    uint16_t offset = _ingameTeamPlayerOffsets[ordinal];
    PlayerInfo *pi = piFromOffset((TeamGame *)_benchTeamGame, offset);

    /* Inactive player? */
    if (pi->position < 0)
        return;

    /* Red card? */
    if ((uint8_t)pi->numYellowCards >= 2)
        return;

    /* Start substitution process */
    _subsState = 1;
    _subsBlockFire = 1;
    _overlayActiveFlag = 1;
    _plToEnterGameIndex = idx + 10;

    if (_gameMaxSubstitutes > 5 && _plToEnterGameIndex == 11) {
        _plToBeSubstitutedPos = 0;
        return;
    }

    FindInitialPlayerToBeSubstituted();
}

/* ---- HandleFormationMenuControls  (SWOS2.s line 10216) ----
 * Handles controls while in formation selection menu (subsState == 2).
 * Fire applies the selected formation; otherwise delegates to movement.
 */
__attribute__((used, externally_visible))
void HandleFormationMenuControls(void)
{
    if (_subsFire == 0) {
        /* No fire — handle movement */
        HandleFormationMenuMovement();
        return;
    }

    /* Fire pressed — apply selected formation */
    _overlayActiveFlag = 0;
    TeamGeneralInfo *team = (TeamGeneralInfo *)_benchTeam;
    team->tactics = (int16_t)_selectedFormationEntry;

    if (team->teamNumber == 1)
        _pl1Tactics = _selectedFormationEntry;
    else
        _pl2Tactics = _selectedFormationEntry;

    ApplyTeamTactics((TeamGame *)_benchTeamGame);
    DOS_SOUND(DOS_EV_TACTICS, 0);
    SubsBenchOff();
}

/* ---- UpdateSubstitutePlayerIndices  (SWOS2.s line 10243) ----
 * Cycles through substitute candidates based on direction input.
 * Direction 0 = previous, 4 = next, 2/6 = exit bench.
 * Scans forward/backward with wrap-around to find valid candidates.
 */
__attribute__((used, externally_visible))
void UpdateSubstitutePlayerIndices(void)
{
    uint16_t dir = _subsDirection;

    /* Left/right = exit bench */
    if (dir == 2 || dir == 6) {
        SubsBenchOff();
        return;
    }

    /* Guard: outside state 4, a reserve GOALKEEPER coming on (index 11) can
     * only replace the keeper, so the selection cursor is frozen — but ONLY
     * when more than five substitutes are allowed. asm (SWOS2.s:19307):
     *     cmpi.w #5,(gameMaxSubstitutes) ; bhi exit
     * i.e. leave when maxSubstitutes > 5. This was inverted here (`<= 5`), and
     * since five is the normal maximum that froze the cursor for EVERY
     * goalkeeper substitution: whoever it started on was the only player you
     * could take off. Found by the replay rig — a DOS recording steps the
     * cursor to lineup position 1 while we stayed on the keeper and
     * substituted him instead (docs/TESTS.md). */
    if (_subsState != 4) {
        if (_plToEnterGameIndex == 11 && _gameMaxSubstitutes > 5)
            return;
    }

    dir = _subsDirection;
    if (dir == 4)
        goto select_next;
    if (dir == 0)
        goto select_prev;
    return;

select_next:
    _subsBlockDirections = 1;
    if (_subsState == 4 && (int16_t)_plToBeSubstitutedOrd < 0) {
        _plToBeSubstitutedOrd = 1;
        UpdatePlayerToBeSubstitutedPosition();
        return;
    }
    if (_plToBeSubstitutedOrd == 10)
        return;
    goto inc_forward;

select_prev:
    _subsBlockDirections = 1;
    if (_subsState == 4) {
        if ((int16_t)_plToBeSubstitutedOrd < 0)
            return;
        if (_plToBeSubstitutedOrd == 1) {
            _plToBeSubstitutedOrd = 0xFFFF;
            _plToBeSubstitutedPos = 0xFFFF;
            return;
        }
    }
    if (_plToBeSubstitutedOrd == 0)
        return;
    goto dec_backward;

inc_forward:
    _plToBeSubstitutedOrd++;
    if (_plToBeSubstitutedOrd == 11)
        goto dec_backward;
    UpdatePlayerToBeSubstitutedPosition();
    if (IsPlayerOkToSubstitute() != 0)
        goto inc_forward;
    return;

dec_backward:
    _plToBeSubstitutedOrd--;
    if ((int16_t)_plToBeSubstitutedOrd < 0)
        goto inc_forward;
    UpdatePlayerToBeSubstitutedPosition();
    if (IsPlayerOkToSubstitute() != 0)
        goto dec_backward;
}

/* ---- CallUpdateSubstitutePlayerIndices  (SWOS2.s line 10083) ----
 * Thin wrapper that calls UpdateSubstitutePlayerIndices.
 */
__attribute__((used, externally_visible))
void CallUpdateSubstitutePlayerIndices(void)
{
    UpdateSubstitutePlayerIndices();
}

/* ---- state3_handle_fire_press  (SWOS2.s line 10089) ----
 * Handles fire button in subsState == 3 (substitute confirmation).
 * Fire pressed: clears overlay and exits.
 * Fire not pressed: delegates to UpdateSubstitutePlayerIndices.
 * Note: dead code after bra.w in original asm is omitted.
 */
__attribute__((used, externally_visible))
void state3_handle_fire_press(void)
{
    if (_subsFire != 0) {
        _overlayActiveFlag = 0;
        return;
    }
    UpdateSubstitutePlayerIndices();
}

/* ================================================================
 * Chunk 158: SelectPlayerToSubstituteMenuHandler + SubstitutePlayer +
 *            MarkPlayersMenuHandler
 * ================================================================ */

/* ---- SelectPlayerToSubstituteMenuHandler  (SWOS2.s line 9922) ----
 * Handles fire button in subsState == 1 (player selected for substitution).
 * Fire: confirms substitution — increments sub counters, sets up sprite
 * animation for the leaving player, records positions.
 * No fire: delegates to CallUpdateSubstitutePlayerIndices.
 */
__attribute__((used, externally_visible))
void SelectPlayerToSubstituteMenuHandler(void)
{
    if (_subsFire == 0) {
        CallUpdateSubstitutePlayerIndices();
        return;
    }

    _overlayActiveFlag = 0;
    _substituteInProgress = 1;

    /* Increment substitution counters for the correct team */
    TeamGeneralInfo *team = (TeamGeneralInfo *)_benchTeam;
    if (team->teamNumber == 1) {
        _team1NumSubs++;
        if (_plToEnterGameIndex == 11)
            _team1GoalkeeperReplaced++;
    } else {
        _team2NumSubs++;
        if (_plToEnterGameIndex == 11)
            _team2GoalkeeperReplaced++;
    }

    _subsBlockFire = 1;
    _subsState = 0;
    _subsWriteOnly = 2;

    /* Set up substitution sprite and animation data */
    uint16_t pos = _plToBeSubstitutedPos;
    _teamThatSubstitutes = _benchTeam;
    Sprite **table = team->spritesTable;
    Sprite *sprite = table[pos];
    _substitutedPlSprite = sprite;
    sprite->cards = 0;
    _substitutedPlX = sprite->x.s.i;
    _substitutedPlY = sprite->y.s.i;
    _substitutedPlDestX = _plLeavingX;
    _substitutedPlDestY = _plComingYTmp;
    _plSubstitutedX = 39;
    _plSubstitutedY = 449;
}

/* ---- SubstitutePlayer  (SWOS2.s line 9968) ----
 * Performs the actual player substitution:
 *   - Clears injury/sent-away on the leaving player's sprite
 *   - Updates marked player tracking
 *   - Swaps name/ordinal data between the two players
 *   - Marks leaving player's position as inactive (0xFF)
 *   - Byte-swaps 61 bytes of PlayerInfo between the two slots
 *   - Rebuilds sprite frame indices, re-applies tactics
 *   - Leaves the subs menu
 */
__attribute__((used, externally_visible))
void SubstitutePlayer(void)
{
    Sprite *sprite = _substitutedPlSprite;
    sprite->injuryLevel = 0;
    sprite->sentAway = 0;

    uint16_t leaving = _plToBeSubstitutedPos;  /* d7 — on-field player going off */
    uint16_t entering = _plToEnterGameIndex;   /* d6 — bench player coming on */
    TeamGame *tg = (TeamGame *)_benchTeamGame;

    /* Update marked player tracking */
    if (tg->markedPlayer == (int16_t)leaving)
        tg->markedPlayer = -1;
    if (tg->markedPlayer == (int16_t)entering) {
        tg->markedPlayer = -1;
        if (leaving != 0)
            tg->markedPlayer = (int16_t)leaving;
    }

    SwapPlayerNameData(entering, leaving);

    /* Look up raw byte offsets */
    uint16_t enterOff = _ingameTeamPlayerOffsets[entering];
    uint16_t leaveOff = _ingameTeamPlayerOffsets[leaving];

    /* Mark leaving player's position as inactive before swap */
    uint8_t *base = (uint8_t *)tg;
    base[leaveOff + 46] = 0xFF;  /* PLAYERINFOHEADER_POSITION */

    /* Swap 61 bytes of PlayerInfo data (offset 42 from player block start) */
    uint8_t *pE = base + enterOff + 42;
    uint8_t *pL = base + leaveOff + 42;
    for (int16_t i = 0; i < 61; i++) {
        uint8_t tmp = pE[i];
        pE[i] = pL[i];
        pL[i] = tmp;
    }

    InitializePlayerSpriteFrameIndices();
    ApplyTeamTactics((TeamGame *)_benchTeamGame);
    _waitForPlayerToGoInTimer = 100;
    _subsBlockFire = 1;
    DOS_SOUND(DOS_EV_SUBSTITUTION, 0);
    LeaveSubsMenu();
}

/* ---- MarkPlayersMenuHandler  (SWOS2.s line 10022) ----
 * Handles controls in subsState == 4 (mark/swap players mode).
 *
 * Fire hold (8 frames) on valid player: toggles mark on that player.
 * Fire press with negative ord: enters formation menu.
 * Fire with two selected players: swaps their positions, data, and sprites.
 * No fire: delegates to UpdateSubstitutePlayerIndices.
 */
__attribute__((used, externally_visible))
void MarkPlayersMenuHandler(void)
{
    /* Check if current player can be marked (no red card) and fire held long enough */
    uint16_t offset = _ingameTeamPlayerOffsets[_plToBeSubstitutedPos];
    PlayerInfo *pi = piFromOffset((TeamGame *)_benchTeamGame, offset);
    TeamGame *tg = (TeamGame *)_benchTeamGame;

    if ((uint8_t)pi->numYellowCards < 2 && _subsFireTimer == 8) {
        /* Toggle mark on current player */
        uint16_t pos = _plToBeSubstitutedPos;
        if (tg->markedPlayer == (int16_t)pos)
            pos = 0xFFFF;  /* unmark */
        tg->markedPlayer = (int16_t)pos;
        _selectedPlayerInSubs = 0xFFFF;
        return;
    }

    /* Fire not pressed — delegate to movement/scanning */
    if (_subsFire == 0) {
        UpdateSubstitutePlayerIndices();
        return;
    }

    /* Fire pressed with negative ord → formation menu */
    if ((int16_t)_plToBeSubstitutedOrd < 0) {
        _subsWriteOnly = 2;
        ShowFormationMenu();
        return;
    }

    /* Ord == 0 → exit */
    if (_plToBeSubstitutedOrd == 0)
        return;

    /* No player selected yet? */
    if ((int16_t)_selectedPlayerInSubs < 0) {
        /* First fire press → select current player */
        if (_subsFireTimer == 1)
            _selectedPlayerInSubs = _plToBeSubstitutedPos;
        return;
    }

    /* Same player selected again? */
    if (_selectedPlayerInSubs == _plToBeSubstitutedPos) {
        /* Deselect on short press */
        if (_subsFireTimer == 1)
            _selectedPlayerInSubs = 0xFFFF;
        return;
    }

    /* Two different players selected — swap them */
    uint16_t player1 = _selectedPlayerInSubs;    /* d7 — first selected */
    uint16_t player2 = _plToBeSubstitutedPos;    /* d6 — second selected */

    /* Update marked player to follow the swap */
    if (tg->markedPlayer == (int16_t)player2) {
        tg->markedPlayer = (int16_t)player1;
    } else if (tg->markedPlayer == (int16_t)player1) {
        tg->markedPlayer = (int16_t)player2;
    }

    SwapPlayerNameData(player2, player1);

    /* Swap PlayerInfo data (61 bytes) */
    uint16_t off1 = _ingameTeamPlayerOffsets[player2];
    uint16_t off2 = _ingameTeamPlayerOffsets[player1];
    uint8_t *base = (uint8_t *)tg;
    uint8_t *p1 = base + off1 + 42;
    uint8_t *p2 = base + off2 + 42;
    for (int16_t i = 0; i < 61; i++) {
        uint8_t tmp = p1[i];
        p1[i] = p2[i];
        p2[i] = tmp;
    }

    /* Swap Sprite data, preserving playerOrdinal: the WHOLE sprite, not the Amiga's 110
     * bytes -- on 64-bit that left cards, injuryLevel, tacklingTimer and sentAway behind
     * (markPlayersMenuHandler says what that did). */
    TeamGeneralInfo *benchTm = (TeamGeneralInfo *)_benchTeam;
    Sprite *sprite1 = benchTm->spritesTable[player2];
    Sprite *sprite2 = benchTm->spritesTable[player1];
    uint8_t *s1 = (uint8_t *)sprite1;
    uint8_t *s2 = (uint8_t *)sprite2;
    for (int16_t i = 0; i < (int16_t)sizeof(Sprite); i++) {
        uint8_t tmp = s1[i];
        s1[i] = s2[i];
        s2[i] = tmp;
    }
    /* Restore playerOrdinal — each sprite keeps its own ordinal */
    int16_t tmpOrd = sprite1->playerOrdinal;
    sprite1->playerOrdinal = sprite2->playerOrdinal;
    sprite2->playerOrdinal = tmpOrd;

    InitializePlayerSpriteFrameIndices();
    ApplyTeamTactics((TeamGame *)_benchTeamGame);
    _selectedPlayerInSubs = 0xFFFF;
    _overlayActiveFlag = 0;
    SubsBenchOff();
}

/* ================================================================
 * Chunk 159: BenchMenuHandleControls
 * ================================================================ */

/* ---- BenchMenuHandleControls  (SWOS2.s line 9792) ----
 * Main per-frame dispatcher for all bench menu controls.
 * Routes to state-specific handlers based on subsState.
 * In idle state (0): handles direction, fire, training team swap.
 */
__attribute__((used, externally_visible))
void BenchMenuHandleControls(void)
{
    if (_substituteInProgress != 0)
        return;

    /* Dispatch based on current subs menu state */
    if (_subsState != 0) {
        if (_subsState == 1) { SelectPlayerToSubstituteMenuHandler(); return; }
        if (_subsState == 3) { state3_handle_fire_press(); return; }
        if (_subsState == 2) { HandleFormationMenuControls(); return; }
        if (_subsState == 4) { MarkPlayersMenuHandler(); return; }
        return;
    }

    /* subsState == 0: bench idle — check if bench was dismissed */
    if (_benchTeam == (uintptr_t)&_topTeamData) {
        if (_bench1Called != 0) { LeaveSubsMenu(); return; }
    } else {
        if (_bench2Called != 0) { LeaveSubsMenu(); return; }
    }

    uint16_t dir = _subsDirection;

    /* Training top-team reverses up/down direction mapping */
    if (_trainingGame != 0 && _subsTrainingTopTeam != 0) {
        if (dir == 0) goto handle_inc;
        if (dir == 4) goto handle_dec;
    } else {
        if (dir == 0) goto handle_dec;
        if (dir == 4) goto handle_inc;
    }

    /* Left/right = leave, fire = start substitution */
    if (dir == 2 || dir == 6) { LeaveSubsMenu(); return; }
    if (_subsFire != 0) { FirePressedInSubsMenu(); return; }
    return;

handle_inc:
    /* Scroll up in bench list (increment benchPlayerIndex) */
    if (_gameState == ST_KEEPER_HOLDS_BALL) return;
    _subsBlockDirections = 1;
    if (_benchPlayerIndex == 5) return;  /* already at max */
    goto inc_scan;

inc_scan:
    _benchPlayerIndex++;
    if (_benchPlayerIndex == 6) goto dec_scan;  /* wrap around */
    if (IsThisPlayerOkToSelect() < 0) goto inc_scan;
    return;

handle_dec:
    /* Scroll down in bench list (decrement benchPlayerIndex) */
    if (_trainingGame == 0 || _benchPlayerIndex != 0) {
        if (_gameState == ST_KEEPER_HOLDS_BALL) return;
    }
    _subsBlockDirections = 1;
    if (_benchPlayerIndex != 0) goto dec_scan;
    /* At index 0 in training: swap teams */
    if (_trainingGame != 0) TrainingSwapBenchTeams();
    return;

dec_scan:
    _benchPlayerIndex--;
    if (_benchPlayerIndex == 0) return;
    if (IsThisPlayerOkToSelect() < 0) goto dec_scan;
}
