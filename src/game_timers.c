/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* game_timers.c — UpdateGameTimersAndCameraBreakMode + CheckIfBallOutOfPlay
 *
 * Chunk 49: 2 exported functions + internal helpers
 */

#include "game_timers.h"
#include "shooting_practice.h"
#include "dos_sound.h"
#include "game_types.h"
#include "netplay_match.h"    /* NetplayMatch_IsLive: no goal replay online (SDL) */

/* ---- Extern globals ---- */
extern uint16_t _playingPenalties;
extern uint16_t _gameState;
/* GS_* stoppage timers — dos_gameplay.c (Amiga default / DOS value): */
extern int16_t  _gsStoppageEventReload;   /* GS_GK_55_50  50 / 55 */
extern int16_t  _gsBallOutOfGameTimer;    /* GS_550_500  500 / 550 */
extern int16_t  _gsPenaltyTimer;          /* GS_110_100  100 / 110 */
extern int16_t  _gsKickoffStoppage;       /* GS_825_750  750 / 825 */
extern uint16_t _gameStatePl;
extern uint16_t _penaltiesTimer;
extern uint16_t _lastFrameTicks;
extern uint16_t _inGameCounter;
extern uint16_t _gameNotInProgressCounterWriteOnly;
extern uint16_t _stoppageTimerTotal;
extern uint16_t _stoppageTimerActive;
extern TeamGeneralInfo *_lastTeamPlayedBeforeBreak;
extern uint16_t _initialKickWriteOnlyTicks;
extern int16_t  _resultTimer;
extern uint16_t _timeVar;
extern int16_t  _statsTimer;
extern volatile uint16_t _playGame;
extern uint16_t _stoppageEventTimer;
extern uint16_t _showFansCounter;
extern uint16_t _breakCameraMode;
extern uint16_t _breakState;
extern uint16_t _goalScored;
extern volatile uint16_t _goalCameraMode;
extern uint16_t _autoSaveHighlights;
extern uint16_t _autoReplays;
extern volatile uint16_t _saveHighlightScene;
extern uint16_t _userRequestedReplay;
extern uint16_t _instantReplayFlag;
extern uintptr_t _currentScorer;
extern uint16_t _foulXCoordinate;
extern uint16_t _foulYCoordinate;
extern uint16_t _cameraDirection;
extern uint8_t  _playerTurnFlags;
extern uint16_t _whichCard;
extern Sprite  *_bookedPlayer;
extern uint16_t _cameraCoordinatesValid;
extern uint16_t _runSlower;
extern uint16_t _refState;
extern uint16_t _injuriesForever;
extern uint16_t _inSubstitutesMenu;
extern uint16_t _waitForPlayerToGoInTimer;
extern uint16_t _cameraLeavingSubsTimer;
extern uint16_t _ballOutOfGameTimer;
extern uint16_t _stoppageAuxFlag;
extern uint16_t _deadCameraBreakVar01;
extern uint16_t _deadCameraBreakVar02;
extern uint16_t _deadCameraBreakVar03;
extern uint16_t _deadCameraBreakVar04;
extern uint16_t _panicReasonCode;

extern TeamGeneralInfo _topTeamData;
extern TeamGeneralInfo _bottomTeamData;
extern Sprite          _ballSprite;
extern Sprite          _goalie1Sprite;
extern Sprite          _goalie2Sprite;
#ifdef SWOS_SDL_BUILD
extern Sprite         *_teamSpritesTable[];
#else
/* On Amiga the unified `teamSpritesTable` doesn't exist — the BSS
 * carries team1 and team2 as separate adjacent arrays. SDL aliases
 * both into one 22-entry slice via sprite_data.c, but on Amiga we
 * just dispatch by index. */
extern Sprite **_team1SpritesTable;
extern Sprite **_team2SpritesTable;
#endif

extern uint8_t  _pl1Fire;
extern uint8_t  _pl2Fire;

/* CheckIfBallOutOfPlay globals */
extern uint16_t _playRefereeWhistle;
extern Sprite  *_lastPlayerPlayed;
extern Sprite *_lastKeeperPlayed;
extern uint16_t _statsTeam1Goals;
extern uint16_t _statsTeam2Goals;
extern uint16_t _penaltiesState[1];
extern uint16_t _statsTeam1GoalsCopy2;
extern uint16_t _statsTeam2GoalsCopy2;
extern uint16_t _teamNumThatScored;
extern TeamGeneralInfo *_teamScoredDataPtr;
extern TeamGame *_teamScoredGamePtr;
extern uint16_t _goalCounter;
extern uint16_t _patternsGoalCounter;
extern uint16_t _goalOut;
extern uint16_t _forceLeftTeam;
extern uint16_t _cameraXVelocity;
extern uint16_t _cameraYVelocity;
extern TeamGeneralInfo *_lastTeamPlayed;

extern AnimTable _playerNormalStandingAnimTable;
extern AnimTable _aboutToThrowInAnimTable;

/* ---- Extern function declarations ---- */
extern void NextPenalty(void);
extern void PrepareForInitialKick(void);
extern void Player1StatusProc(void);
extern void Player2StatusProc(void);
extern void SetCameraMovingToShowerState(void);
extern void FirstHalfJustEnded(void);
extern void GoToHalftime(void);
extern void PlayersLeavingPitch(void);
extern void GameOver(void);
extern void SetBallPosition(int16_t ballX, int16_t ballY);
extern void ResetAnimatedPatternsForBothTeams(void);
extern void CheckIfGoalkeeperClaimedTheBall(void);
extern void SetPlayerAnimationTable(AnimTable *animTable, Sprite *sprite);
extern void PlayRefereeWhistleSample(void);
extern void PanicHaltLoop(void);
extern void GoalScored(int16_t teamGoalType, Sprite *lastPlayer);
extern void PlayGoalReplayBottomSample(void);
extern void PlayGoalKickOffSample(void);
extern uint16_t Rand(void);
extern void StopAllPlayers(void);


/* ---- Helper: negate result/time/stats timers ---- */
static void __attribute__((noinline)) negateTimers(void)
{
    _resultTimer = -_resultTimer;
    _timeVar = -(int16_t)_timeVar;
    _statsTimer = -_statsTimer;
}

/* ---- Helper: handle end_game_transmission ----
 * Returns 1 when the state was one this chain handles (the caller is done),
 * 0 when it was not. Zero is NOT "nothing to do": the asm's chain ends
 *
 *   game_not_starting:
 *           cmpi.w  #ST_CAMERA_GOING_TO_SHOWERS,(gameState).l
 *           bne.s   game_interrupted
 *
 * (SWOS2.s:25267) — an unmatched state falls THROUGH into the stoppage-timer
 * countdown rather than leaving the routine. Returning unconditionally here
 * froze the countdown for as long as a fire button was held, because the
 * gate above only asks whether fire is down, not whether this chain can do
 * anything about it. */
static int __attribute__((noinline)) handleEndGameTransmission(void)
{
    if (_gameState == ST_RESULT_ON_HALFTIME) {
        negateTimers();
        SetCameraMovingToShowerState();
        PrepareForInitialKick();
        _stoppageEventTimer = 0;
        return 1;
    }
    if (_gameState == ST_RESULT_AFTER_THE_GAME) {
        negateTimers();
        _playGame = 0;
        _stoppageEventTimer = 0;
        return 1;
    }
    if (_gameState == ST_STARTING_GAME) {
        _showFansCounter = 0;
        PrepareForInitialKick();
        _stoppageEventTimer = 0;
        return 1;
    }
    if (_gameState == ST_CAMERA_GOING_TO_SHOWERS) {
        PrepareForInitialKick();
        _stoppageEventTimer = 0;
        return 1;
    }
    return 0;   /* -> game_interrupted */
}

/* ---- Helper: check if fire was pressed to end game state ----
 * Returns 1 if end_game_transmission should be invoked, 0 if not. */
static int __attribute__((noinline)) checkFirePressedForEndGame(void)
{
    if (_gameState < ST_STARTING_GAME || _gameState > ST_GAME_ENDED)
        return 0;

    if (_topTeamData.firePressed != 0)
        return 1;
    if (_bottomTeamData.firePressed != 0)
        return 1;

    if (_topTeamData.playerCoachNumber == 1 ||
        _bottomTeamData.playerCoachNumber == 1) {
        Player1StatusProc();
        if (_pl1Fire != 0)
            return 1;
    }

    if (_topTeamData.playerCoachNumber == 2 ||
        _bottomTeamData.playerCoachNumber == 2) {
        Player2StatusProc();
        if (_pl2Fire != 0)
            return 1;
    }

    if (_gameState == ST_RESULT_AFTER_THE_GAME) {
        Player1StatusProc();
        if (_pl1Fire != 0)
            return 1;
        Player2StatusProc();
        if (_pl2Fire != 0)
            return 1;
    }

    return 0;
}

/* ---- Helper: handle stopped state dispatch ---- */
static void __attribute__((noinline)) handleStoppedState(void)
{
    if (_gameState == ST_RESULT_ON_HALFTIME) {
        negateTimers();
        SetCameraMovingToShowerState();
        return;
    }
    if (_gameState == ST_RESULT_AFTER_THE_GAME) {
        negateTimers();
        _playGame = 0;
        return;
    }
    if (_gameState == ST_STARTING_GAME) {
        PrepareForInitialKick();
        return;
    }
    if (_gameState == ST_CAMERA_GOING_TO_SHOWERS) {
        PrepareForInitialKick();
        return;
    }
    if (_gameState == ST_FIRST_HALF_ENDED) {
        FirstHalfJustEnded();
        return;
    }
    if (_gameState == ST_GOING_TO_HALFTIME) {
        negateTimers();
        GoToHalftime();
        return;
    }
    if (_gameState == ST_GAME_ENDED) {
        PlayersLeavingPitch();
        return;
    }
    if (_gameState == ST_PLAYERS_GOING_TO_SHOWER) {
        GameOver();
        return;
    }
    if (_gameState == ST_FIRST_EXTRA_STARTING) {
        PrepareForInitialKick();
        return;
    }
    if (_gameState == ST_FIRST_EXTRA_ENDED) {
        PrepareForInitialKick();
        return;
    }

    /* Default: copy gameState to gameStatePl, set breakCameraMode */
    _gameStatePl = _gameState;

    if (_gameState == ST_KEEPER_HOLDS_BALL) {
        if (_inSubstitutesMenu == 0 && _cameraLeavingSubsTimer == 0) {
            _timeVar = 31000;
        }
    }

    _breakCameraMode = 0;
}

/* ---- Helper: break camera mode state machine ---- */
static void __attribute__((noinline)) handleBreakCameraMode(TeamGeneralInfo *a6)
{
    Sprite *a5 = &_ballSprite;
    int16_t i;
    Sprite **sprTable;
    Sprite *a1;

    if (_inSubstitutesMenu != 0)
        return;
    if (_waitForPlayerToGoInTimer != 0)
        return;

    switch (_breakCameraMode) {

    case 0: /* wait for ball to stop */
        if (a5->deltaX.raw != 0)
            break;
        if (a5->deltaY.raw != 0)
            break;
        _breakCameraMode = 1;
        if (_gameState == ST_KEEPER_HOLDS_BALL)
            break;
        _stoppageEventTimer = (uint16_t)_gsStoppageEventReload;   /* GS_GK_55_50 */
        if (_goalCameraMode != 0)
            _stoppageEventTimer = 75;
        break;

    case 1: /* handle replay/highlight, set ball position */
        if (_playingPenalties == 0 && _inSubstitutesMenu == 0) {
            if (_goalCameraMode != 0) {
                if (_autoSaveHighlights != 0)
                    _saveHighlightScene = 1;
                if (_autoReplays != 0
#ifdef SWOS_SDL_BUILD
                    /* Not in an online match: the replay is a loop of its own outside the
                     * frame step GGPO repeats, so it cannot be rolled back, and the session
                     * would end at the first goal (docs/NETPLAY.md, "Rematch"). */
                    && !NetplayMatch_IsLive()
#endif
                    ) {
                    _userRequestedReplay = 0;
                    _instantReplayFlag = 1;
                }
            }
        }
        _currentScorer = 0;
        if (_playingPenalties != 0 && _gameState != ST_PENALTIES)
            break;
        if (_gameState != ST_KEEPER_HOLDS_BALL)
            SetBallPosition((int16_t)_foulXCoordinate, (int16_t)_foulYCoordinate);
        _breakCameraMode = 2;
        break;

    case 2: /* wait for card animation, reset players */
        if (_whichCard != 0)
            break;
        if (_cameraCoordinatesValid == 0)
            break;
        _goalScored = 0;
        if (_gameState == ST_KEEPER_HOLDS_BALL) {
            _gameStatePl = 0x66;
            _inGameCounter = 0;
            _breakState = _gameState;
        }
        if (_topTeamData.resetControls == 0) {
            sprTable = _topTeamData.spritesTable;
            for (i = 10; i >= 0; i--) {
                a1 = *sprTable++;
                a1->destReachedState = 1;
            }
        }
        if (_bottomTeamData.resetControls == 0) {
            sprTable = _bottomTeamData.spritesTable;
            for (i = 10; i >= 0; i--) {
                a1 = *sprTable++;
                a1->destReachedState = 1;
            }
        }
        if (_gameState == ST_KEEPER_HOLDS_BALL) {
            _goalie1Sprite.destReachedState = 3;
            _goalie2Sprite.destReachedState = 3;
        }
        _breakCameraMode = 4;
        break;

    case 4: /* wait for subs menu to close */
        if (_inSubstitutesMenu != 0)
            break;
        _breakCameraMode = 3;
        break;

    case 3: /* wait for all players to reach destination */
        if (_refState != 0)
            break;
        if (_injuriesForever != 0)
            break;
        for (i = 0; i < 22; i++) {
#ifdef SWOS_SDL_BUILD
            a1 = _teamSpritesTable[i];
#else
            a1 = (i < 11) ? _team1SpritesTable[i]
                          : _team2SpritesTable[i - 11];
#endif
            if (_gameState != ST_PLAYERS_TO_INITIAL_POSITIONS &&
                _gameState != ST_PENALTY &&
                _gameState != ST_PENALTIES) {
                if (a1->onScreen == 0)
                    continue;
            }
            if (a1->destReachedState != 3)
                return;  /* early exit: not all players at dest */
        }
        _runSlower = 0;
        if (_goalCameraMode != 0)
            ResetAnimatedPatternsForBothTeams();
        _breakCameraMode = 5;
        break;

    case 5: /* clear card/booking state */
        _whichCard = 0;
        _bookedPlayer = (Sprite *)0;
        _breakCameraMode = 6;
        break;

    case 6: /* clear ball-out-of-game timer, set timeVar */
        _ballOutOfGameTimer = 0;
        if (_gameState != ST_KEEPER_HOLDS_BALL)
            _timeVar = 31000;
        _breakCameraMode = 7;
        break;

    case 7: /* ball in play / throw-in setup */
        a6->ballInPlay = 1;
        _ballOutOfGameTimer += 1;
        if (_ballOutOfGameTimer >= _gsBallOutOfGameTimer) {   /* GS_550_500 */
            _deadCameraBreakVar01 += 1;
            _deadCameraBreakVar02 = _gameStatePl;
            _deadCameraBreakVar03 = _gameState;
            CheckIfGoalkeeperClaimedTheBall();
            _deadCameraBreakVar04 += 1;
            break;
        }
        if (a6->controlledPlayer == 0)
            break;
        a1 = a6->controlledPlayer;
        if (a1->playerState == PL_THROW_IN)
            SetPlayerAnimationTable(&_aboutToThrowInAnimTable, a1);
        else
            SetPlayerAnimationTable(&_playerNormalStandingAnimTable, a1);
        a6->ballOutOfPlay = 1;
        _breakCameraMode = 8;
        if (_gameState == ST_KEEPER_HOLDS_BALL)
            break;
        if (_inSubstitutesMenu != 0)
            break;
        _resultTimer = 31000;
        break;

    case 8: /* whistle and set waiting on player */
        _stoppageAuxFlag = 0;
        if (_gameState != ST_KEEPER_HOLDS_BALL)
            PlayRefereeWhistleSample();
        _goalCameraMode = 0;
        _gameStatePl = ST_WAITING_ON_PLAYER;
        _inGameCounter = 0;
        _breakState = _gameState;
        break;

    default:
        /* assert_failed: unknown breakCameraMode */
        _panicReasonCode = 0x0B;
        PanicHaltLoop();
        break;
    }
}


/* ===========================================================================
 * UpdateGameTimersAndCameraBreakMode
 *
 * Main game timer dispatcher and break camera mode state machine.
 * Called once per frame from the game loop.
 * =========================================================================== */
__attribute__((used, externally_visible))
void UpdateGameTimersAndCameraBreakMode(void)
{
    TeamGeneralInfo *a6;
    uint16_t d0;

    /* --- Penalty timer --- */
    if (_playingPenalties != 0) {
        if (_gameState != ST_PENALTIES) {
            _penaltiesTimer += 1;
            if (_penaltiesTimer == _gsPenaltyTimer)   /* GS_110_100 */
                NextPenalty();
        }
    }

    /* --- Game in progress: update in-game counter --- */
    if (_gameStatePl == ST_GAME_IN_PROGRESS) {
        _inGameCounter += _lastFrameTicks;
        return;
    }

    /* --- Game not in progress: update stoppage timers --- */
    _gameNotInProgressCounterWriteOnly += 1;
    d0 = _lastFrameTicks;
    _stoppageTimerTotal += d0;

    if (_gameStatePl == ST_WAITING_ON_PLAYER) {
        _stoppageTimerActive += d0;
        a6 = _lastTeamPlayedBeforeBreak;
        if (a6->playerNumber == 0) {
            if (_stoppageTimerActive >= _gsKickoffStoppage) {   /* GS_825_750 */
                PrepareForInitialKick();
                _initialKickWriteOnlyTicks += 1;
            }
        }
        return;
    }

    /* --- Check fire for end-game transmission --- */
    if (checkFirePressedForEndGame() && handleEndGameTransmission())
        return;

    /* --- game_interrupted: stoppage event timer countdown --- */
    if (_stoppageEventTimer != 0) {
        _stoppageEventTimer -= _lastFrameTicks;
        if ((int16_t)_stoppageEventTimer > 0)
            return;
        _stoppageEventTimer = 0;
    }

    /* event_triggered */
    a6 = _lastTeamPlayedBeforeBreak;

    if (_gameStatePl == ST_STOPPED) {
        handleStoppedState();
        return;
    }

    /* Game running: break camera mode state machine */
    handleBreakCameraMode(a6);
}


/* ---- Helper: write restart globals and stop players ---- */
static void __attribute__((noinline)) writeRestartGlobalsAndStopPlayers(
    TeamGeneralInfo *team, int16_t d1, int16_t d2, int16_t d3, uint8_t d4)
{
    if (_forceLeftTeam == 1)
        team = &_topTeamData;

    _gameStatePl = ST_STOPPED;
    _gameNotInProgressCounterWriteOnly = 0;
    _foulXCoordinate = (uint16_t)d1;
    _foulYCoordinate = (uint16_t)d2;
    _cameraDirection = (uint16_t)d3;
    _playerTurnFlags = d4;
    _lastTeamPlayedBeforeBreak = team;
    _stoppageTimerTotal = 0;
    _stoppageTimerActive = 0;
    StopAllPlayers();
    _cameraXVelocity = 0;
    _cameraYVelocity = 0;
}

/* ---- Helper: handle goal scoring ---- */
static void __attribute__((noinline)) handleGoalScored(TeamGeneralInfo *teamData, uint16_t goalType, uint16_t teamNum)
{
    Sprite *a0;
    int16_t d0, d1;

    a0 = _lastPlayerPlayed;
    if (_lastKeeperPlayed != 0) {
        if (a0 == &_goalie1Sprite || a0 == &_goalie2Sprite)
            a0 = _lastKeeperPlayed;
    }
    _statsTeam1GoalsCopy2 = _statsTeam1Goals;
    _statsTeam2GoalsCopy2 = _statsTeam2Goals;
    GoalScored((int16_t)goalType, a0);
    _goalCameraMode = 1;
    _teamNumThatScored = teamNum;

    /* finalize_goal_replay_side */
    _teamScoredDataPtr = teamData;
    _teamScoredGamePtr = teamData->inGameTeamPtr;

    {
        TeamGeneralInfo *restartTeam = teamData->opponentsTeam;
        int16_t forced = ShootingPractice_ForcedKickoffTeamNum();
        int16_t camDir;
        uint8_t turnFlags;

        /* Goals prepare their restart here, without PrepareForInitialKick.
         * In shooting practice the human takes every kickoff, including after
         * scoring. Choose the team before deriving direction and turn flags. */
        if (forced)
            restartTeam = (_topTeamData.teamNumber == forced)
                ? &_topTeamData : &_bottomTeamData;

        if (restartTeam == &_topTeamData) {
            camDir = 4;
            turnFlags = 124;
        } else {
            camDir = 0;
            turnFlags = 199;
        }

        /* The PC game: PlayGoalComment or PlayOwnGoalComment, the crowd, and the
         * chant about the new score (LoadCrowdChantSample, at the replay). */
        DOS_SOUND(a0->teamNumber != goalType ? DOS_EV_OWN_GOAL : DOS_EV_GOAL, (int16_t)_penaltiesState[0] == -1);
        DOS_SOUND(DOS_EV_GOAL_CROWD, 0);
        DOS_SOUND(DOS_EV_SCORE, (_statsTeam1Goals & 0xFF) << 8 | (_statsTeam2Goals & 0xFF));
        PlayGoalReplayBottomSample();
        _playRefereeWhistle = 0;

        /* Goal counter bias system */
        d0 = (int16_t)Rand();
        d0 = (uint16_t)d0 >> 1;
        d0 += 100;
        d1 = d0;

        if (_playingPenalties == 0) {
            int16_t scoreDiff = (int16_t)_statsTeam1Goals - (int16_t)_statsTeam2Goals;

            if (scoreDiff == 0) {
                d1 = 200;
            } else {
                int16_t preDiff = (int16_t)_statsTeam1GoalsCopy2 - (int16_t)_statsTeam2GoalsCopy2;
                if (preDiff == 0) {
                    d1 = 300;
                } else if (preDiff == 1 || preDiff == -1) {
                    int16_t newDiff = (int16_t)_statsTeam1Goals - (int16_t)_statsTeam2Goals;
                    if (newDiff == 2 || newDiff == -2)
                        d1 = 200;
                    else
                        d1 = 100;
                } else {
                    d1 = 100;
                }
            }
            d1 += (int16_t)Rand();
        }

        _goalCounter = (uint16_t)d1;
        _patternsGoalCounter = 1;

        _gameState = ST_PLAYERS_TO_INITIAL_POSITIONS;
        _breakCameraMode = 0xFFFF;
        writeRestartGlobalsAndStopPlayers(restartTeam, 336, 449, camDir, turnFlags);
    }
}


/* ===========================================================================
 * CheckIfBallOutOfPlay
 *
 * Detects when the ball has left the pitch boundaries and determines the
 * restart type: goal, corner kick, goal kick, or throw-in.
 * =========================================================================== */
__attribute__((used, externally_visible))
void CheckIfBallOutOfPlay(void)
{
    int16_t d1, d2, d3;
    uint8_t d4;
    TeamGeneralInfo *a6;
    TeamStatisticsData *a0;

    _playRefereeWhistle = 0;

    if (_gameStatePl == ST_STOPPED) {
        /* exit_check_whistle */
        if (_playRefereeWhistle != 0)
            PlayRefereeWhistleSample();
        return;
    }

    _playRefereeWhistle = 1;

    d1 = _ballSprite.x.s.i;
    d2 = _ballSprite.y.s.i;
    d3 = _ballSprite.z.s.i;
    d1 -= 1;

    /* --- Check if ball is in goal window: Z <= 15, X in [302..366] --- */
    if (d3 <= 15 && d1 >= 302 && d1 <= 366) {
        if (d2 <= 449) {
            /* Ball above centerline — goal at top */
            a6 = &_bottomTeamData;
            if (_bottomTeamData.teamNumber == 1) {
                handleGoalScored(a6, 1, 1);
            } else {
                handleGoalScored(a6, 2, 2);
            }
        } else {
            /* Ball below centerline — goal at bottom */
            a6 = &_topTeamData;
            if (_topTeamData.teamNumber == 2) {
                handleGoalScored(a6, 2, 2);
            } else {
                handleGoalScored(a6, 1, 1);
            }
        }
        /* writeRestartGlobalsAndStopPlayers was called inside handleGoalScored */
        if (_playRefereeWhistle != 0)
            PlayRefereeWhistleSample();
        return;
    }

    /* not_in_goal_window: re-read ball position */
    d1 = _ballSprite.x.s.i;
    d2 = _ballSprite.y.s.i;
    d3 = _ballSprite.z.s.i;
    d3 += 2;

    /* Check if ball is near goal area for goal-kick sample */
    if (_ballSprite.speed >= 768 &&
        d1 >= 290 && d1 <= 381 && d3 <= 25) {
        PlayGoalKickOffSample();
        _playRefereeWhistle = 0;
    }

    /* --- Determine restart type based on ball position --- */
    a6 = &_topTeamData;

    if (d2 < 129) {
        /* Ball past top end line */
        if (_lastTeamPlayed == a6) {
            /* Same team kicked it out — corner for opponent */
            a6 = &_bottomTeamData;
            if (d1 < 336) {
                /* set_corner_left_upper */
                d1 = 86; d2 = 134; d3 = 2; d4 = 0x1C;
                _gameState = ST_CORNER_LEFT;
            } else {
                /* set_corner_right_upper */
                d1 = 585; d2 = 134; d3 = 6; d4 = 0x70;
                _gameState = ST_CORNER_RIGHT;
            }
            _breakCameraMode = 0xFFFF;
            a0 = a6->teamStatsPtr;
            a0->cornersWon += 1;
            DOS_SOUND(DOS_EV_CORNER, 0);
        } else {
            /* Opposite team — goal kick */
            if (d1 < 336) {
                /* setup_goal_out_right_top_half */
                d1 = 276; d2 = 154; d3 = 4; d4 = 0x7C;
                _gameState = ST_GOAL_OUT_RIGHT;
            } else {
                /* setup_goal_out_left_top_half */
                d1 = 396; d2 = 154; d3 = 4; d4 = 0x7C;
                _gameState = ST_GOAL_OUT_LEFT;
            }
            _breakCameraMode = 0xFFFF;
            if (a6->playerNumber == 0)
                d4 &= 0xBB;
            _goalOut = 1;
        }
        writeRestartGlobalsAndStopPlayers(a6, d1, d2, d3, d4);
        if (_playRefereeWhistle != 0)
            PlayRefereeWhistleSample();
        return;
    }

    a6 = &_bottomTeamData;

    if (d2 > 769) {
        /* Ball past bottom end line */
        if (_lastTeamPlayed == a6) {
            /* Same team kicked it out — corner for opponent */
            a6 = &_topTeamData;
            if (d1 < 336) {
                /* set_corner_right_lower */
                if (_forceLeftTeam == 1) {
                    /* set_corner_left_upper */
                    d1 = 86; d2 = 134; d3 = 2; d4 = 0x1C;
                    _gameState = ST_CORNER_LEFT;
                } else {
                    d1 = 86; d2 = 764; d3 = 2; d4 = 7;
                    _gameState = ST_CORNER_RIGHT;
                }
            } else {
                /* d1 >= 336 */
                if (_forceLeftTeam == 1) {
                    /* set_corner_right_upper */
                    d1 = 585; d2 = 134; d3 = 6; d4 = 0x70;
                    _gameState = ST_CORNER_RIGHT;
                } else {
                    d1 = 585; d2 = 764; d3 = 6; d4 = 0xC1;
                    _gameState = ST_CORNER_LEFT;
                }
            }
            _breakCameraMode = 0xFFFF;
            a0 = a6->teamStatsPtr;
            a0->cornersWon += 1;
            DOS_SOUND(DOS_EV_CORNER, 0);
        } else {
            /* Opposite team — goal kick */
            if (d1 < 336) {
                /* setup_goal_out_left_bottom_half */
                if (_forceLeftTeam == 1) {
                    /* setup_goal_out_right_top_half */
                    d1 = 276; d2 = 154; d3 = 4; d4 = 0x7C;
                    _gameState = ST_GOAL_OUT_RIGHT;
                } else {
                    d1 = 276; d2 = 744; d3 = 0; d4 = 0xC7;
                    _gameState = ST_GOAL_OUT_LEFT;
                }
            } else {
                /* d1 >= 336 */
                if (_forceLeftTeam == 1) {
                    /* setup_goal_out_left_top_half */
                    d1 = 396; d2 = 154; d3 = 4; d4 = 0x7C;
                    _gameState = ST_GOAL_OUT_LEFT;
                } else {
                    d1 = 396; d2 = 744; d3 = 0; d4 = 0xC7;
                    _gameState = ST_GOAL_OUT_RIGHT;
                }
            }
            _breakCameraMode = 0xFFFF;
            if (a6->playerNumber == 0)
                d4 &= 0xBB;
            _goalOut = 1;
        }
        writeRestartGlobalsAndStopPlayers(a6, d1, d2, d3, d4);
        if (_playRefereeWhistle != 0)
            PlayRefereeWhistleSample();
        return;
    }

    /* --- Ball is in throw-in band (Y: 129..769) --- */
    {
        TeamGeneralInfo *throwTeam = _lastTeamPlayed;
        TeamGeneralInfo *opTeam = throwTeam->opponentsTeam;

        if (d1 >= 336) {
            /* Right side */
            d1 = 590;
            d3 = 6;
            d4 = 0xF1;

            if (opTeam != &_topTeamData) {
                /* opponent is bottom team: zone based on Y */
                if (d2 < 342)       _gameState = ST_THROW_IN_BACK_LEFT;
                else if (d2 < 556)  _gameState = ST_THROW_IN_CENTER_LEFT;
                else                _gameState = ST_THROW_IN_FORWARD_LEFT;
            } else {
                /* opponent is top team */
                if (d2 < 342)       _gameState = ST_THROW_IN_FORWARD_RIGHT;
                else if (d2 < 556)  _gameState = ST_THROW_IN_CENTER_RIGHT;
                else                _gameState = ST_THROW_IN_BACK_RIGHT;
            }
        } else {
            /* Left side */
            d1 = 81;
            d3 = 2;
            d4 = 0x1F;

            if (opTeam != &_topTeamData) {
                /* opponent is bottom team */
                if (d2 < 342)       _gameState = ST_THROW_IN_BACK_RIGHT;
                else if (d2 < 556)  _gameState = ST_THROW_IN_CENTER_RIGHT;
                else                _gameState = ST_THROW_IN_FORWARD_RIGHT;
            } else {
                /* opponent is top team */
                if (d2 < 342)       _gameState = ST_THROW_IN_FORWARD_LEFT;
                else if (d2 < 556)  _gameState = ST_THROW_IN_CENTER_LEFT;
                else                _gameState = ST_THROW_IN_BACK_LEFT;
            }
        }

        _breakCameraMode = 0xFFFF;
        DOS_SOUND(DOS_EV_THROW_IN, 0);
        a6 = opTeam;  /* ASM: movea.l (a6),a6 — use the team GETTING the throw-in */
    }

    writeRestartGlobalsAndStopPlayers(a6, d1, d2, d3, d4);

    if (_playRefereeWhistle != 0)
        PlayRefereeWhistleSample();
}
