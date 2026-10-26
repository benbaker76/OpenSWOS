/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "update_players.h"
#include "dos_sound.h"
#include "dos_mode.h"       /* _gDosGameplay: the DOS-only clear on the keeper handoff */
#include "ball_physics.h"
#include "ball_possession.h"
#include "contest_log.h"    /* --contest-log: reads only, changes nothing */
#include "goalkeeper.h"
#include "penalty_shootout.h"   /* the manual keeper at penalties (shot_at_goal) */
#include "tackle_foul.h"
#include "headers.h"
extern int16_t _gsBallDefensiveZ;   /* GS_27_22 goalie-catch z-threshold — dos_gameplay.c */
extern int16_t _gsThrowInHideResult; /* GS_55_50 throw-in UI gate — dos_gameplay.c */
#include "player_anim.h"
#include "player_positioning.h"
#include "ai.h"
#include "math68k.h"
#include "random.h"
#include "panic.h"
#include "sfx_crowd.h"
#include "highlight.h"

/* ==================================================================
 * Chunk 55 — UpdatePlayers (~2,690 lines asm → C)
 *
 * The main per-frame player update loop. Called once per team per frame.
 * Iterates all 11 players, dispatches by player state, handles:
 *   - goalkeeper AI and diving/catching
 *   - controlled player: direction, fire, passing, kicking
 *   - throw-in mechanics
 *   - tackle state updates
 *   - heading (jump/static)
 *   - injury/tackled recovery
 *   - pass receiver following
 *   - idle player positioning
 *   - booking animations
 *   - substitution flow
 *   - post-match reactions (happy/sad)
 * Ends with per-player speed/direction/animation finalization.
 * ================================================================== */

/* ---- External globals ---- */
extern Sprite       _ballSprite;
extern Sprite       _goalie1Sprite;
extern Sprite       _goalie2Sprite;
extern TeamGeneralInfo _topTeamData;
extern TeamGeneralInfo _bottomTeamData;

extern Sprite      *_lastPlayerPlayed;
extern TeamGeneralInfo *_lastTeamPlayed;
extern Sprite      *_lastKeeperPlayed;
extern TeamGeneralInfo *_lastTeamPlayedBeforeBreak;
extern Sprite      *_lastPlayerBeforeGoalkeeper;
extern TeamGeneralInfo *_lastTeamScored;

extern int16_t      _gameStatePl;
extern int16_t      _gameState;
extern int16_t      _playingPenalties;
extern int16_t      _penaltiesTimer;
extern int16_t      _gsPenaltiesKeeperFree;
extern int16_t      _penalty;
extern int16_t      _playerHadBall;
extern int16_t      _goalOut;
extern int16_t      _fireBlocked;
extern int16_t      _inSubstitutesMenu;
extern int16_t      _injuriesForever;
extern int16_t      _hideBall;
extern int16_t      _throwInPassOrKick;
extern int16_t      _substituteInProgress;
extern Sprite      *_substitutedPlSprite;
extern int16_t      _substitutedPlDestX;
extern int16_t      _substitutedPlDestY;
extern int16_t      _plComingX;
extern int16_t      _plComingY;
extern int16_t      _plSubstitutedX;
extern int16_t      _plSubstitutedY;
extern int16_t      _stoppageTimerActive;
extern int16_t      _statsTimer;
extern int16_t      _timeVar;
extern int16_t      _resultTimer;
extern int16_t      _breakState;

#ifdef DETERMINISTIC_MODE
#include "deterministic.h"
extern volatile uint16_t panicReasonCode;
extern void PanicHaltLoop(void);
extern Sprite **team2SpritesTable;
/* Trap: detect when t2p6 destX > 600 — encode line number in panic code */
#define T2P6_DESTX_TRAP(lineid) do { \
    if (frameCount > 400 && player == team2SpritesTable[6] && player->destX > 600) { \
        panicReasonCode = (uint16_t)(lineid); \
        PanicHaltLoop(); \
    } \
} while(0)
#else
#define T2P6_DESTX_TRAP(lineid)
#endif
extern int16_t      _inGameCounter;
extern int16_t      _nobodysBallTimer;
extern int16_t      _whichCard;
extern int16_t      _refState;
extern int16_t      _goalkeeperSkipFlag;
extern uint16_t     _frameCount;
extern int16_t      _cameraDirection;
extern uint8_t      _playerTurnFlags;
extern uint16_t     _currentGameTick;
extern int32_t     *_winningTeamPtr;
extern int16_t      _forceLeftTeam;
extern Sprite      *_bookedPlayer;
extern int16_t      _foulXCoordinate;
extern int16_t      _foulYCoordinate;
extern Sprite      *_penaltyShooterSprite;

/* Ball prediction variables */
extern int16_t      _ballInUpperPenaltyArea;
extern int16_t      _ballInLowerPenaltyArea;
extern int16_t      _ballInGoalkeeperArea;
extern int16_t      _ballNextGroundX;
extern int16_t      _ballNextYGroundY;
extern int16_t      _ballDefensiveX;
extern int16_t      _ballDefensiveY;
extern int16_t      _ballDefensiveZ;
extern int16_t      _ballNotHighX;
extern int16_t      _ballNotHighY;
extern int16_t      _strikeDestX;
extern int16_t      _ballSpeed;
extern int16_t      _ballSpeedAfterSave;

/* Counters / debug vars (written but dead) */
extern int16_t      _panicReasonCode;
extern int16_t      _turnDirCopiedFromCameraCount;
extern int16_t      _deadThrowInDirectionVar;
extern int16_t      _disallowedTurnFlagsCounter;
extern int16_t      _cameraSnapToPlayerCount;
extern int16_t      _gameNotInProgressCounterWriteOnly;

/* Constant data in asm */
/* Defined here because the SDL port never imported it from Data.c:604 (=1024);
 * at -O2 the single use was dead-code-eliminated so the missing definition went
 * unnoticed, but -O0 keeps the reference and fails to link without it. */
int16_t             _kGoalkeeperSpeedWhenGameStopped = 1024;
extern int16_t      _kGoalkeeperGameSpeed;
extern int16_t      _kGoalkeeperBurstSpeed;
extern int16_t      _kGoalkeeperMoveToBallSpeed;
extern int16_t      _kGoalKeeperRecoveryStepBase;
extern int16_t      _kGoalKeeperRecoveryStepAlt;
extern int16_t      _kGoalKeeperRecoveryStepFar;
extern int16_t      _kShotAtGoalMinumumSpeed;
extern int16_t      _kPlayerGroundConstant;
extern int16_t      _kPlayerAirConstant;
extern int16_t      _kInjuryForeverSpeed;
extern int16_t      _kSubstitutedPlayerSpeed;
extern uint8_t      _goalScoredChances[];
extern GoalieSkill  _penaltyGoalieSkillTable;
extern int16_t      _kKeeperEaseBallTable[];
extern int16_t      _kDefaultDestinations[];
extern int16_t      _kBallFriction[];
extern SpriteGraphics *_spriteGraphicsPtr;

/* Animation tables (in sprite_data.c) */
extern AnimTable    _playerNormalStandingAnimTable;
extern AnimTable    _playerRunningAnimTable;
extern AnimTable    _aboutToThrowInAnimTable;
extern AnimTable    _throwInPassAnimTable;
extern AnimTable    _throwInKickAnimTable;
extern AnimTable    _plInjuredAnimTable;
extern AnimTable    _plGettingYellowCardAnimTable;
extern AnimTable    _plGettingRedCardAnimTable;
extern AnimTable    _plGetting2ndYellowCardAnimTable;
extern AnimTable    _playerLosingReactionAnimTable;
extern AnimTable    _playerWinningReactionAnimTable;
extern AnimTable    _jumpHeaderAttemptAnimTable;

/* Restart positioning tables (arrays of pointers) */
extern int16_t     *_bottomBallOutOfPlayPositions[];
extern int16_t     *_topBallOutOfPlayPositions[];

/* Game state and player state constants from game_types.h (via update_players.h) */

/* Referee / card constants (match referee.c) */
#define REF_WAITING_PLAYER      2
#define REF_ABOUT_TO_GIVE_CARD  3
#define CARD_YELLOW             1
#define CARD_RED                2

/* Sprite image index constants for goalkeeper catch frames.
 * Values from SWOS2.s:2020-2023 (862/864/992/994) and data/SWOS2.h:176-179.
 * The caught-ball poses sit at offsets {5, 7, 19, 21} from each goalie's
 * dive-sequence base — goalie 1 at 0x359, goalie 2 at 0x3CD
 * (ball_physics.c:420,428):
 *     goalie 1: 0x35E, 0x360, 0x36C, 0x36E
 *     goalie 2: 0x3D2, 0x3D4, 0x3E0, 0x3E2
 * The four named below are g1+5, g1+7, g2+19, g2+21; the compare chain in
 * goalie_diving supplies the other four (0x3D2/0x3D4/0x36C/0x36E) as literals,
 * so all 8 are covered and none is a duplicate.
 *
 * Was 0x3C4/0x3C6/0x3D0/0x3D2 (fixed 2026-07-29): that made 0x3D2 a duplicate
 * of a literal already in the chain and put 0x3C4/0x3C6 outside every dive
 * sequence, so a keeper in the caught-ball pose decayed at
 * kGoalKeeperRecoveryStepBase (112) instead of ...StepAlt (192) and slid ~70%
 * further after the catch. */
#define SPR_GOALIE1_CAUGHT_BALL_RIGHT  0x35E
#define SPR_GOALIE1_CAUGHT_BALL_LEFT   0x360
#define SPR_GOALIE2_CAUGHT_BALL_RIGHT  0x3E0
#define SPR_GOALIE2_CAUGHT_BALL_LEFT   0x3E2

/* ---- Local data (from inline labels after asm UpdatePlayers) ---- */
static Sprite *prevLastPlayer;
static TeamGeneralInfo *prevLastTeamPlayed;
static const int16_t freeKickFactorsX_data[7] = {0, 3, 4, 5, 4, 3, 0};
static int16_t freeKickDestX_local;

/* ==================================================================
 * UpdatePlayers — per-frame update for all 11 players on a team
 * ================================================================== */
__attribute__((used, externally_visible))
void UpdatePlayers(TeamGeneralInfo *team) {
    Sprite *ball = &_ballSprite;
    Sprite **sprPtr;
    int16_t i;
    int16_t ballX, ballY;

    /* ---- Preamble: save previous state ---- */
    prevLastPlayer = _lastPlayerPlayed;
    prevLastTeamPlayed = _lastTeamPlayed;

    if (_lastKeeperPlayed != (Sprite *)0) {
        if (_lastPlayerPlayed != &_goalie1Sprite &&
            _lastPlayerPlayed != &_goalie2Sprite) {
            _lastKeeperPlayed = (Sprite *)0;
        }
    }

    /* ---- Timer decrements ---- */
    /* Goalkeeper saved comment timer */
    if (team->goalkeeperSavedCommentTimer != 0) {
        if (team->goalkeeperSavedCommentTimer > 0)
            team->goalkeeperSavedCommentTimer--;
        else
            team->goalkeeperSavedCommentTimer++;
    }

    /* Pass kick timer */
    if (team->passKickTimer != 0) {
        team->passKickTimer--;
        if (team->passKickTimer == 0)
            team->passingKickingPlayer = (Sprite *)0;
    }

    /* Won the ball timer */
    if (team->wonTheBallTimer != 0)
        team->wonTheBallTimer--;

    /* Player switch timer */
    if (team->playerSwitchTimer != 0)
        team->playerSwitchTimer--;

    /* ---- Apply aftertouch ---- */
    ApplyBallAfterTouch(team);

    /* ---- Penalty area checks ---- */
    ballX = ball->x.s.i;
    ballY = ball->y.s.i;

    _ballInUpperPenaltyArea = 0;
    _ballInLowerPenaltyArea = 0;
    _ballInGoalkeeperArea = 0;

    if (ballX >= 193 && ballX <= 478 && ballY >= 129 && ballY <= 769) {
        if (ballY <= 216) {
            _ballInUpperPenaltyArea = 1;
            goto check_gk_area;
        } else if (ballY >= 682) {
            _ballInLowerPenaltyArea = 1;
            goto check_gk_area;
        }
        goto not_in_penalty;
    check_gk_area:
        if (ballX >= 273 && ballX <= 398) {
            if (ballY <= 158 || ballY >= 740)
                _ballInGoalkeeperArea = 1;
        }
    }
not_in_penalty:

    /* Reset goalOut if game in progress and ball left penalty areas */
    if (_gameStatePl == ST_GAME_IN_PROGRESS) {
        if (_goalOut) {
            if (_ballInUpperPenaltyArea == 0 && _ballInLowerPenaltyArea == 0)
                _goalOut = 0;
        }
    }

    /* ---- Update player index (round-robin) ---- */
    team->updatePlayerIndex++;
    if (team->updatePlayerIndex >= 11)
        team->updatePlayerIndex = 0;

    /* ---- Main player loop ---- */
    sprPtr = (Sprite **)team->spritesTable;

    for (i = 10; i >= 0; i--) {
        Sprite *player = *sprPtr++;

        /* Copy direction, compute isMoving */
        player->playerDirection = player->direction;
        {
            int32_t movCheck = player->deltaX.raw | player->deltaY.raw;
            player->isMoving = movCheck ? (int16_t)-1 : 0;
        }

        /* ---- State dispatch ---- */
        if (player->playerState == PL_TACKLED)
            goto player_tackled_state;
        if (player->playerState == PL_ROLLING_INJURED)
            goto player_injured_recover_tick;

        /* Check if this is a "controlled" player (controlled, goalkeeper, or pass target) */
        if (player == team->controlledPlayer)
            goto this_is_controlled_player;
        if (player->playerOrdinal == 1)
            goto this_is_controlled_player;
        if (player == team->passToPlayerPtr)
            goto this_is_controlled_player;
        if (player->playerState != PL_NORMAL)
            goto this_is_controlled_player;

        /* Check injury forever */
        if (_injuriesForever) {
            if (player->injuryLevel == (int16_t)0xFFFF)
                goto injury_forever_tick;
        }
        goto not_controlled_player;

    /* ============================================================
     * this_is_controlled_player — ball distance + height classification
     * ============================================================ */
    this_is_controlled_player:
        team->prevPlVeryCloseToBall = team->plVeryCloseToBall;
        team->plVeryCloseToBall = 0;
        team->plCloseToBall = 0;
        team->plNotFarFromBall = 0;

        {
            int32_t bd = player->ballDistance;
            if (bd <= 32)
                team->plVeryCloseToBall = 1;
            else if (bd <= 72)
                team->plCloseToBall = 1;
            else if (bd <= 2450)
                team->plNotFarFromBall = 1;
        }

        /* Ball height classification */
        team->ballLessEqual4 = 0;
        team->ball4To8 = 0;
        team->ball8To12 = 0;
        team->ball12To17 = 0;
        team->ballAbove17 = 0;
        {
            int16_t bz = ball->z.s.i;
            if (bz > 17)
                team->ballAbove17 = 1;
            else if (bz > 12)
                team->ball12To17 = 1;
            else if (bz > 8)
                team->ball8To12 = 1;
            else if (bz > 4)
                team->ball4To8 = 1;
            else
                team->ballLessEqual4 = 1;
        }

        /* ---- Player state sub-dispatch ---- */
        if (player->playerState == PL_TACKLING)
            goto tackle_update_state;
        if (player->playerState == PL_JUMP_HEADING)
            goto player_jump_heading;
        if (player->playerState == PL_STATIC_HEADING)
            goto player_static_heading;
        if (player->playerState == PL_THROW_IN)
            goto throw_in_state_handler;
        if (player->playerState == PL_GOALIE_DIVING_HIGH ||
            player->playerState == PL_GOALIE_DIVING_LOW)
            goto goalie_diving;
        if (player->playerState == PL_GOALIE_CATCHING_BALL)
            goto goalie_catching_ball;
        if (player->playerState == PL_DOWN)
            goto player_down_st_10;
        if (player->playerState == PL_BOOKED)
            goto booking_handler_waiting;
        if (player->playerState == PL_SAD ||
            player->playerState == PL_HAPPY)
            goto player_sad_or_happy;
        if (player->playerState == PL_GOALIE_CLAIMED)
            goto goalie_claimed;

        /* Goalkeeper (ordinal 1) */
        if (player->playerOrdinal == 1)
            goto player_goalkeeper;

        /* Actually controlled player */
        if (player == team->controlledPlayer)
            goto its_controlled_player;

        /* Pass target */
        if (player == team->passToPlayerPtr)
            goto receiver_expect_pass_tick;

        /* Should not reach here */
        _panicReasonCode = 2;
        PanicHaltLoop();

    /* ============================================================
     * GOALKEEPER section
     * ============================================================ */
    player_goalkeeper:
        if (_playingPenalties) {
            if (_penaltiesTimer > _gsPenaltiesKeeperFree)   /* Amiga 50 / DOS 55 — dos_gameplay.c */
                goto finalize;
        }

        UpdatePlayerShotChanceTable(player, team);

        if (_goalkeeperSkipFlag)
            goto return_last_player_block;

        _ballNextGroundX = (int16_t)0xFFFF;

        if (team->goalkeeperPlaying) {
            team->goaliePlayingOrOut = 1;
            team->ballOutOfPlayOrKeeper = 0;
            if (player == team->controlledPlayer)
                goto its_controlled_player;
            team->goalkeeperPlaying = 0;
        }

        /* goalie_not_posing_as_player */
        if (_gameStatePl != ST_GAME_IN_PROGRESS) {
            team->goaliePlayingOrOut = -1;
            team->ballOutOfPlayOrKeeper = -1;
            player->speed = _kGoalkeeperSpeedWhenGameStopped;
        } else {
            team->goaliePlayingOrOut = 0;
            team->ballOutOfPlayOrKeeper = 0;
        }
        player->speed = _kGoalkeeperGameSpeed;

        /* Check which half goalkeeper is in */
        if (player->y.s.i > 449) {
            /* lower half */
            if (team == &_topTeamData)
                goto return_last_player_block;
        } else {
            /* upper half */
            if (team == &_bottomTeamData)
                goto return_last_player_block;
        }

        /* goalie_not_catching_the_ball (always reached — catching state handled above) */
        if (player == team->controlledPlayer)
            goto its_controlled_player;
        if (player == team->passToPlayerPtr)
            goto receiver_expect_pass_tick;

        if (_gameStatePl != ST_GAME_IN_PROGRESS)
            goto not_controlled_player;

        /* Ball in penalty area check for this team */
        ballX = ball->x.s.i;
        ballY = ball->y.s.i;
        if (team == &_bottomTeamData) {
            if (!_ballInLowerPenaltyArea)
                goto clear_keeper_hold_flags_then_return;
        } else {
            if (!_ballInUpperPenaltyArea)
                goto clear_keeper_hold_flags_then_return;
        }

        /* ball_in_penalty_area */
        if (player == _lastPlayerPlayed)
            goto return_last_player_block;

        UpdateBallVariables(player, ball, team);
        CalculateBallNextGroundXYPositions(ball);

        if (_ballInGoalkeeperArea)
            goto ball_in_lower_goalkeeper_area;

        /* Ball not in goalkeeper area — skill check */
        {
            GoalieSkill *gs = team->shotChanceTable;
            int16_t tick = _currentGameTick;
            tick = (tick & 0xF0) >> 4;
            if ((uint16_t)tick >= (uint16_t)gs->field_3A)
                goto ball_standing_in_goalkeeper_area;
        }

    ball_in_lower_goalkeeper_area:
        if (_ballNextGroundX < 0)
            goto ball_standing_in_goalkeeper_area;

        {
            int16_t ngx = _ballNextGroundX;
            int16_t ngy = _ballNextYGroundY;

            if (team == &_bottomTeamData) {
                if (ngy < 682 || ngy > 761 || ngx < 193 || ngx > 478)
                    goto ball_standing_in_goalkeeper_area;
            } else {
                if (ngy < 137 || ngy > 216 || ngx < 193 || ngx > 478)
                    goto ball_standing_in_goalkeeper_area;
            }

            /* in_penalty_area: compare distances */
            {
                int32_t dkx = (int32_t)(ngx - player->x.s.i);
                int32_t dky = (int32_t)(ngy - player->y.s.i);
                int32_t distKeeper = dkx * dkx + dky * dky;

                int32_t dbx = (int32_t)(ngx - ball->x.s.i);
                int32_t dby = (int32_t)(ngy - ball->y.s.i);
                int32_t distBall = dbx * dbx + dby * dby;

                if ((distKeeper << 2) > distBall)
                    goto ball_standing_in_goalkeeper_area;

                player->destX = ngx;
                player->destY = ngy;
                player->speed = _kGoalkeeperMoveToBallSpeed;
                goto goalie_can_catch_checks;
            }
        }

    ball_standing_in_goalkeeper_area:
        if (_gameStatePl != ST_GAME_IN_PROGRESS)
            goto return_to_last_played_player;

        if (_strikeDestX < 295 || _strikeDestX > 376)
            goto shot_on_goal_or_close;
        goto goal_attempt;

    shot_on_goal_or_close:
        if ((uint32_t)player->ballDistance >= 512) goto goalkeeper_dont_throw;
        if (team->ballAbove17) goto goalkeeper_dont_throw;
        player->destX = ball->x.s.i;
        player->destY = ball->y.s.i;
        goto goalie_can_catch_checks;

    goalkeeper_dont_throw:
        if (team->ballAbove17)
            goto evaluate_catchability_and_nearest_owner;
        if (_ballInGoalkeeperArea)
            goto center_goalkeeper_on_ball_x;

        if (ball->y.s.i >= 449) {
            if (ball->deltaY.raw <= 0)
                goto evaluate_catchability_and_nearest_owner;
        } else {
            if (ball->deltaY.raw >= 0)
                goto evaluate_catchability_and_nearest_owner;
        }

        /* set_dest_x_toward_center_from_ball_x */
        {
            int16_t dx = ball->x.s.i - 336;
            dx >>= 1;
            dx += 336;
            player->destX = dx;
        }
        goto compute_dest_y_from_ball;

    center_goalkeeper_on_ball_x:
        player->destX = ball->x.s.i;

    compute_dest_y_from_ball:
        if (ball->y.s.i >= 449) {
            int16_t d = 769 - ball->y.s.i;
            d >>= 1;
            d += ball->y.s.i;
            player->destY = d;
        } else {
            int16_t d = ball->y.s.i - 129;
            d >>= 1;
            d += 129;
            player->destY = d;
        }
        {
            GoalieSkill *gs = team->shotChanceTable;
            player->speed = gs->speed_unk;
        }
        goto goalie_can_catch_checks;

    evaluate_catchability_and_nearest_owner:
        if (ball->z.s.i > 16)
            goto cannot_intercept_fallback;
        if (_ballDefensiveX < 0)
            goto cannot_intercept_fallback;
        {
            int32_t myDist = player->ballDistance;
            Sprite *cp = team->controlledPlayer;
            if (cp != (Sprite *)0) {
                if (myDist > cp->ballDistance)
                    goto cannot_intercept_fallback;
            }
            cp = team->passToPlayerPtr;
            if (cp != (Sprite *)0) {
                if (myDist > cp->ballDistance)
                    goto cannot_intercept_fallback;
            }
            {
                TeamGeneralInfo *opp = team->opponentsTeam;
                Sprite *oppCtrl = opp->controlledPlayer;
                if (oppCtrl != (Sprite *)0) {
                    if (myDist > oppCtrl->ballDistance)
                        goto cannot_intercept_fallback;
                }
                Sprite *oppPass = opp->passToPlayerPtr;
                if (oppPass != (Sprite *)0) {
                    if (myDist > oppPass->ballDistance)
                        goto cannot_intercept_fallback;
                }
            }
            /* set_dest_to_ball_defensive_coords */
            player->destX = _ballDefensiveX;
            player->destY = _ballDefensiveY;
            goto goalie_can_catch_checks;
        }

    cannot_intercept_fallback:
        goto return_last_player_block;

    goal_attempt:
        if (team == _lastTeamPlayed) {
            if (!_playerHadBall)
                goto save_attempted_set_burst_speed;
        }

        /* pre_save_possession_distance_gate */
        if ((uint32_t)player->ballDistance >= 512 || team->ballAbove17) {
            goto midrange_positioning_from_ball;
        }
        player->destX = ball->x.s.i;
        player->destY = ball->y.s.i;
        goto shot_at_goal;

    midrange_positioning_from_ball:
        if ((uint32_t)player->ballDistance > 2048 || team->ballAbove17)
            goto far_or_unfavorable_shot_branch;
        {
            int16_t dx = ball->x.s.i - 336;
            dx >>= 1;
            dx += 336;
            player->destX = dx;
        }
        if (ball->y.s.i >= 449) {
            int16_t d = 769 - ball->y.s.i;
            d >>= 1;
            d += ball->y.s.i;
            player->destY = d;
        } else {
            int16_t d = ball->y.s.i - 129;
            d >>= 1;
            d += 129;
            player->destY = d;
        }
        {
            GoalieSkill *gs = team->shotChanceTable;
            player->speed = gs->speed_unk;
        }
        goto shot_at_goal;

    far_or_unfavorable_shot_branch:
        {
            TeamGeneralInfo *opp = team->opponentsTeam;
            if (opp->playerHasBall) goto mark_opponent_and_chase;
            if (team->playerHasBall) goto mark_opponent_and_chase;
            if (team->plVeryCloseToBall) goto mark_opponent_and_chase;

            if (team == _lastTeamPlayed) {
                if (!_playerHadBall)
                    goto save_attempted_set_burst_speed;
            }

            if ((uint16_t)_kShotAtGoalMinumumSpeed < ball->speed)
                goto shot_at_goal;

            if ((uint32_t)player->ballDistance > 5000)
                goto save_not_attempted_set_run_speed;
            if (ball->z.s.i > 12)
                goto save_not_attempted_set_run_speed;
            if (ball->deltaZ.raw > 0x8000)
                goto save_not_attempted_set_run_speed;
            if (ball->deltaZ.raw < -0x8000)
                goto save_not_attempted_set_run_speed;
            goto mark_opponent_and_chase;
        }

    shot_at_goal:
        if (ball->z.s.i > 16)
            goto save_not_attempted_set_run_speed;

        /* MANUAL KEEPER: at any penalty — shootout or in-match kick — the
         * defending side's keeper is its player's to throw
         * (penalty_shootout.h). Placed above the finisher-skill roll and
         * the ShouldGoalkeeperDive gate so that in manual mode neither the
         * auto-react dive nor the AI's dice ever move him: the held direction
         * dives him immediately, at full speed, with the height the player
         * chose (DOWN = low), and no input leaves him standing — the catch
         * checks below still apply, so he holds what comes straight at him. */
        {
            int16_t manual = ManualKeeper_DiveInput(team);
            if (manual == 0) {
                player->destX = player->x.s.i;
                player->destY = player->y.s.i;
                goto goalie_can_catch_checks;
            }
            if (manual > 0) {
                int16_t d0 = (manual == 5 || manual == 6) ? 6 : 2;
                int16_t low = (manual == 3 || manual == 5);
                player->direction = d0;
                GoalkeeperJumping(d0, 0, d0, player, ball, team);
                GoalkeeperForceDiveAnim(player, team, low);
                team->passKickTimer = 0;
                team->passingKickingPlayer = (Sprite *)0;
                goto clamp_keeper_y_min;
            }
            /* -1: not this mode's keeper — the AI decides below, as ever. */
        }

        if (team->goalkeeperSavedCommentTimer > 0)
            goto goalie_save_attempt_entry;
        if ((uint32_t)player->ballDistance > 128)
            goto goalie_save_attempt_entry;
        if (team->ballAbove17)
            goto goalie_save_attempt_entry;

        {
            TeamGeneralInfo *opp = team->opponentsTeam;
            if (!opp->firePressed)
                goto goalie_save_attempt_entry;
            if (!opp->playerHasBall) {
                if (opp->passKickTimer < 22)
                    goto goalie_save_attempt_entry;
            }

            /* prepare_finisher_vs_goalie_skill_delta */
            {
                int16_t finishingSkill = 0;
                Sprite *attacker = opp->lastHeadingTacklingPlayer;
                if (attacker != (Sprite *)0) {
                    PlayerInfo *attackerInfo = GetPlayerPointerFromShirtNumber(attacker, team);
                    finishingSkill = attackerInfo->finishing;
                }
                {
                    PlayerInfo *goalieInfo = GetPlayerPointerFromShirtNumber(player, team);
                    finishingSkill -= goalieInfo->goalieSkill;
                }
                {
                    int16_t idx = finishingSkill + 7;
                    int16_t tick = _currentGameTick;
                    tick = (tick >> 1) & 0xF;
                    if ((uint8_t)tick < _goalScoredChances[idx]) {
                        /* goalie_react_goal_scored */
                        team->goalkeeperSavedCommentTimer = -5;
                        {
                            int16_t d1 = 2;
                            int16_t d0, d3;
                            if (ball->x.s.i < player->x.s.i) {
                                d0 = 6; d3 = 6;
                            } else {
                                d0 = 2; d3 = 2;
                            }
                            player->direction = d0;
                            _ballSpeed = _ballSprite.speed;
                            opp->spinTimer = (int16_t)0xFFFF;
                            if (_ballSprite.speed > 1536)
                                _ballSprite.speed = 1536;
                            GoalkeeperJumping(d0, d1, d3, player, ball, team);
                            _ballSpeedAfterSave = _ballSprite.speed;
                            team->passKickTimer = 0;
                            team->passingKickingPlayer = (Sprite *)0;
                            goto clamp_keeper_y_min;
                        }
                    } else {
                        team->goalkeeperSavedCommentTimer = 5;
                        goto goalie_save_attempt_entry;
                    }
                }
            }
        }

    goalie_save_attempt_entry:
        {
            int16_t attemptSave = ShouldGoalkeeperDive(player, ball, team);
            if (!attemptSave)
                goto save_not_attempted_set_run_speed;

            {
                int16_t d1, d0, d3;
                if (_ballDefensiveX >= player->x.s.i) {
                    /* ball right of goalie */
                    d1 = 0;
                    if (_playingPenalties || _penalty) {
                        d1 = 1;
                        if ((_currentGameTick & 6) == 0) {
                            goto choose_turn_left_for_dive;
                        }
                        d1 = 0;
                    }
                    d0 = 2; d3 = 2;
                    goto apply_facing_then_jump;
                } else {
                    /* ball left of goalie */
                    d1 = 0;
                    if (_playingPenalties || _penalty) {
                        d1 = 1;
                        if ((_currentGameTick & 0xC) == 0) {
                            d0 = 2; d3 = 2;
                            goto apply_facing_then_jump;
                        }
                        d1 = 0;
                    }
                choose_turn_left_for_dive:
                    d0 = 6; d3 = 6;
                }

            apply_facing_then_jump:
                player->direction = d0;
                GoalkeeperJumping(d0, d1, d3, player, ball, team);
                team->passKickTimer = 0;
                team->passingKickingPlayer = (Sprite *)0;
                goto clamp_keeper_y_min;
            }
        }

    save_attempted_set_burst_speed:
        player->speed = _kGoalkeeperBurstSpeed;
        goto chase_ball_set_dest;

    save_not_attempted_set_run_speed:
        {
            GoalieSkill *gs = team->shotChanceTable;
            player->speed = gs->speed_unk2;
        }
    chase_ball_set_dest:
        player->destX = _ballNotHighX;
        player->destY = _ballNotHighY;
        goto goalie_can_catch_checks;

    mark_opponent_and_chase:
        if (team->ballAbove17)
            goto save_not_attempted_set_run_speed;
        player->destX = ball->x.s.i;
        player->destY = ball->y.s.i;
        {
            GoalieSkill *gs = team->shotChanceTable;
            player->speed = gs->speed_unk;
        }
        goto goalie_can_catch_checks;

    return_to_last_played_player:
        goto return_last_player_block;

    goalie_can_catch_checks:
        if (team == _lastTeamPlayed) {
            if (!_playerHadBall)
                goto cannot_catch_exit;
        }
        if (_ballInGoalkeeperArea)
            goto attempt_catch_conditions;
        {
            GoalieSkill *gs = team->shotChanceTable;
            int16_t tick = (_currentGameTick & 0xF0) >> 4;
            if ((uint16_t)tick >= (uint16_t)gs->field_3A)
                goto cannot_catch_exit;
        }
    attempt_catch_conditions:
        if (_ballNextGroundX < 0) goto cannot_catch_exit;
        if (_ballDefensiveZ > _gsBallDefensiveZ) goto cannot_catch_exit;  /* GS_27_22 */
        if (_ballDefensiveZ <= 12) goto cannot_catch_exit;
        if ((uint32_t)player->ballDistance > 2116) goto cannot_catch_exit;
        {
            int16_t dx = player->x.s.i - _ballDefensiveX;
            if (dx > 12 || dx < -12) goto cannot_catch_exit;
        }
        {
            int16_t dy = player->y.s.i - _ballDefensiveY;
            if (dy > 12 || dy < -12) goto cannot_catch_exit;
        }
        GoalkeeperCaughtTheBall(player, team);

    cannot_catch_exit:
        if (player == _lastPlayerPlayed)
            goto clamp_keeper_y_min;
        if ((uint32_t)player->ballDistance > 72)
            goto clamp_keeper_y_min;
        if (team->ballAbove17)
            goto clamp_keeper_y_min;
        if (player->playerState == PL_GOALIE_CATCHING_BALL)
            goto prepare_catch_pose;
        if (team->ball12To17)
            goto clamp_keeper_y_min;

    prepare_catch_pose:
        player->destX = player->x.s.i;
        player->destY = player->y.s.i;
        if (team == _lastTeamPlayed && !_playerHadBall) {
            /* Goalkeeper gets ball as a player */
            UpdateBallWithControllingGoalkeeper(player);
            team->controlledPlayer = player;
            team->passToPlayerPtr = (Sprite *)0;
            team->playerSwitchTimer = 25;
            team->passingBall = 0;
            team->passingToPlayer = 0;
            player->destX = player->x.s.i;
            player->destY = player->y.s.i;
            _lastTeamPlayed = team;
            _lastPlayerPlayed = player;
            _penalty = 0;
            _playerHadBall = 0;
            team->passKickTimer = 0;
            team->passingKickingPlayer = (Sprite *)0;
            team->field_74 = 0;
            team->goalkeeperPlaying = 1;
            team->goaliePlayingOrOut = 1;
            team->ballOutOfPlayOrKeeper = 1;
            goto finalize;
        }

        /* opponent_player_touched_the_ball */
        _lastKeeperPlayed = _lastPlayerPlayed;
        _lastTeamPlayed = team;
        _lastPlayerPlayed = player;
        _penalty = 0;
        _playerHadBall = 0;
        UpdateBallWithControllingGoalkeeper(player);
        GoalkeeperClaimedTheBall(player, ball, team);

    clamp_keeper_y_min:
        if (player->destY < 129)
            player->destY = 129;
        if (player->destY > 769)
            player->destY = 769;
        goto finalize;

    return_last_player_block:
        if (_gameStatePl != ST_GAME_IN_PROGRESS)
            goto not_controlled_player;
        if ((_frameCount & 0xE) != 0)
            goto clamp_keeper_y_min;
        goto idle_player_common_path;

    clear_keeper_hold_flags_then_return:
        team->ballOutOfPlayOrKeeper = 0;
        if (player == team->passToPlayerPtr) {
            team->passToPlayerPtr = (Sprite *)0;
            team->passingBall = 0;
            team->passingToPlayer = 0;
        }
        goto return_last_player_block;

    /* ============================================================
     * BOOKING / SAD-HAPPY / DOWN / CATCHING / CLAIMED / DIVING
     * ============================================================ */
    booking_handler_waiting:
        if (_whichCard)
            goto finalize;
        /* booking_finish_reset_state */
        player->playerState = PL_NORMAL;
        SetPlayerAnimationTable(&_playerNormalStandingAnimTable, player);
        goto finalize;

    player_sad_or_happy:
        if (_gameState == ST_GOING_TO_HALFTIME || _gameState == ST_PLAYERS_GOING_TO_SHOWER) {
            player->playerState = PL_NORMAL;
            SetPlayerAnimationTable(&_playerNormalStandingAnimTable, player);
        }
        goto finalize;

    player_down_st_10:
        player->playerDownTimer--;
        if (player->playerDownTimer == 0) {
            player->playerState = PL_NORMAL;
            SetPlayerAnimationTable(&_playerNormalStandingAnimTable, player);
        }
        goto finalize;

    goalie_catching_ball:
        player->playerDownTimer--;
        if (player->playerDownTimer == 0) {
            player->playerState = PL_NORMAL;
            SetPlayerAnimationTable(&_playerNormalStandingAnimTable, player);
            if (team->goalkeeperDivingLeft) {
                team->goalkeeperDivingLeft = 0;
                UpdateBallWithControllingGoalkeeper(player);
                GoalkeeperClaimedTheBall(player, ball, team);
                ball->z.s.i = 5;
            }
            goto finalize;
        }
        /* goalie_catch_hold_if_moving */
        if (team->goalkeeperDivingLeft) /* offset 82 — SPRITE_FULLDIRECTION on team */
            goto finalize;
        /* goalie_deflect_chance_gate */
        if (!team->plVeryCloseToBall)
            goto finalize;
        {
            GoalieSkill *gs = team->shotChanceTable;
            int16_t d1 = (_currentGameTick & 0xF0) >> 4;
            d1 -= gs->field_30;
            if (d1 < 0) {
                /* claim_ball_and_freeze_speeds */
                player->speed = 0;
                ball->speed = 0;
                team->goalkeeperDivingLeft = 1;
                GoalkeeperClaimedTheBall(player, ball, team);
                DOS_SOUND(DOS_EV_KEEPER_CLAIMED, 0);
                goto finalize;
            }
            GoalkeeperDeflectedBall(ball, team);
            DOS_SOUND(DOS_EV_KEEPER_CLAIMED, 0);
        }
        goto finalize;

    goalie_claimed:
        player->playerDownTimer--;
        if (player->playerDownTimer == 0) {
            player->playerState = PL_NORMAL;
            SetPlayerAnimationTable(&_playerNormalStandingAnimTable, player);
            DOS_SOUND(DOS_EV_KEEPER_CLAIMED, 0);
        }
        goto finalize;

    goalie_diving:
        player->playerDownTimer--;
        if (player->playerDownTimer == 0) {
        recover_to_stand_after_dive:
            player->playerState = PL_NORMAL;
            SetPlayerAnimationTable(&_playerNormalStandingAnimTable, player);
            goto snap_dest;
        }
        /* diving_right_claim_window */
        if (team->goalkeeperDivingRight) {
            if ((uint8_t)player->playerDownTimer <= 42) {
                team->goalkeeperDivingRight = 0;
                UpdateBallWithControllingGoalkeeper(player);
                GoalkeeperClaimedTheBall(player, ball, team);
                ball->z.s.i = 5;
                goto recover_to_stand_after_dive;
            }
        }
        /* post_dive_extra_recovery_rules */
        if (_gameStatePl == ST_GAME_IN_PROGRESS) {
            if ((uint8_t)player->playerDownTimer <= 60)
                goto recover_to_stand_after_dive;
        }
        /* choose_speed_drop_table_entry */
        {
            int16_t recoveryStep = _kGoalKeeperRecoveryStepBase;
            if (team->field_46) {
                goto apply_goalie_slowdown;
            }

            /* Check caught sprite variants */
            {
                int16_t img = player->imageIndex;
                if (img == SPR_GOALIE1_CAUGHT_BALL_RIGHT ||
                    img == SPR_GOALIE1_CAUGHT_BALL_LEFT ||
                    img == SPR_GOALIE2_CAUGHT_BALL_RIGHT ||
                    img == SPR_GOALIE2_CAUGHT_BALL_LEFT ||
                    img == 0x3D2 || img == 0x3D4 ||
                    img == 0x36C || img == 0x36E) {
                    recoveryStep = _kGoalKeeperRecoveryStepAlt;
                    goto apply_goalie_slowdown;
                }
            }

            /* Check y offset from ball */
            {
                int16_t dy = ball->y.s.i - player->y.s.i;
                if (team == &_topTeamData) {
                    if (dy < -5) {
                        recoveryStep = _kGoalKeeperRecoveryStepFar;
                        goto apply_goalie_slowdown;
                    }
                } else {
                    if (dy > 5) {
                        recoveryStep = _kGoalKeeperRecoveryStepFar;
                        goto apply_goalie_slowdown;
                    }
                }
            }

        apply_goalie_slowdown:
            player->speed -= recoveryStep;
            if (player->speed < 0)
                player->speed = 0;

            if (_gameStatePl != ST_GAME_IN_PROGRESS)
                goto finalize;
            if (team->field_46)
                goto finalize;

            /* ball_vert_motion_gate */
            if (team == &_bottomTeamData) {
                if (ball->deltaY.raw <= 0)
                    goto finalize;
            } else {
                if (ball->deltaY.raw >= 0)
                    goto finalize;
            }

            /* check_ball_x_overlap_with_goalie */
            if (player->imageIndex < 0)
                goto finalize;
            {
                /* Amiga: base + idx*4 = byte offset for 4-byte slot.
                 * On 64-bit each slot is 8 bytes, so index as a typed
                 * pointer table instead. */
                SpriteGraphics *sg = ((SpriteGraphics **)_spriteGraphicsPtr)[player->imageIndex];
                int16_t pw = sg->pixWidth;
                int16_t px = player->x.s.i;
                if (player->deltaX.raw < 0) {
                    px -= 5; pw += 6;
                    if ((uint16_t)px >= (uint16_t)ball->x.s.i) goto finalize;
                    if ((uint16_t)(px + pw) <= (uint16_t)ball->x.s.i) goto finalize;
                } else {
                    px += 5; pw += 6;
                    if ((uint16_t)px < (uint16_t)ball->x.s.i) goto finalize;
                    if ((uint16_t)(px - pw) > (uint16_t)ball->x.s.i) goto finalize;
                }
            }

            /* low_ball_height_and_penalty_gate */
            if (ball->z.s.i > 20)
                goto finalize;

            if (_playingPenalties || _penalty) {
                int16_t dy = player->y.s.i - ball->y.s.i;
                if (dy > 5 || dy < -5)
                    goto finalize;
                {
                    int16_t s = ball->speed >> 2;
                    ball->speed -= s;
                }
            } else {
                int16_t dy = player->y.s.i - ball->y.s.i;
                if (dy > 7 || dy < -7)
                    goto finalize;
                /* DOS ONLY, and the two releases really do differ here. The DOS
                 * build starts the pass clock and clears the attacker's pass state
                 * on this non-penalty handoff, UNCONDITIONALLY:
                 *
                 *   swos-port/swos/swos.asm:114157  (cseg_805A7)
                 *     mov [esi+TeamGeneralInfo.passKickTimer], 25
                 *     mov [esi+TeamGeneralInfo.passingToPlayer], 0
                 *     mov [esi+TeamGeneralInfo.passingKickingPlayer], 0
                 *
                 * The Amiga falls straight through to the goalie flag
                 * (data/SWOS2.s:29951 non_penalty_handoff_after_save) and only
                 * reaches the CONDITIONAL clear further down in
                 * reset_possession_after_save, which is skipped whenever
                 * goalkeeperSavedCommentTimer is negative (`bmi`).
                 *
                 * The consequence is visible: with the pointer left set, the
                 * player-selection loops skip that player (they refuse
                 * passingKickingPlayer), so control does not come back to the
                 * man who shot. tests/dos/shootout-08200245 diverged on exactly
                 * that at frame 2285 -- DOS handing control back, us leaving it
                 * null -- and the shot's result banner followed 400 frames later. */
                if (_gDosGameplay) {
                    TeamGeneralInfo *opp = team->opponentsTeam;
                    team->passKickTimer = 25;
                    opp->passingToPlayer = 0;
                    opp->passingKickingPlayer = (Sprite *)0;
                }
            }

            /* flag_goalie_intervention_and_slow_player */
            /* Asm does move.b #1,$46(a6) — sets HIGH byte on big-endian.
             * Value must be 0x0100 so that &= 0xFF correctly clears it. */
            team->field_46 = 0x0100;
            {
                int16_t spd = player->speed;
                if (player->playerState == PL_GOALIE_DIVING_HIGH)
                    spd >>= 2;
                else
                    spd >>= 1;
                player->speed -= spd;
            }

            {
                GoalieSkill *gs = team->shotChanceTable;
                if (_penalty || _playingPenalties)
                    gs = &_penaltyGoalieSkillTable;

                int16_t dir = player->direction;
                _penalty = 0;
                _playerHadBall = 0;
                team->passKickTimer = 25;
                {
                    TeamGeneralInfo *opp = team->opponentsTeam;
                    opp->passingToPlayer = 0;
                    if (team->goalkeeperSavedCommentTimer >= 0)
                        opp->passingKickingPlayer = (Sprite *)0;
                }

                /* Increment goal attempts if not in goalkeeper area */
                if (!_ballInGoalkeeperArea) {
                    TeamGeneralInfo *opp = team->opponentsTeam;
                    opp->teamStatsPtr->goalAttempts++;
                }

                ResetBothTeamSpinTimers();

                /* Comment timing logic */
                {
                    int16_t d1 = (_currentGameTick & 0xF0) >> 4;
                    /* asm: `sub.w SPRITE_DIRECTION(a0),d1` then
                     * `sub.w SPRITE_SPEED(a0),d1` — and a0 is the GoalieSkill
                     * entry, not a Sprite. The disassembly is applying Sprite
                     * equates to a foreign pointer, so those are offsets 42 and
                     * 44 (`SPRITE_DIRECTION EQU 42`, `SPRITE_SPEED EQU 44` in
                     * data/SWOS2.s) = field_2A / field_2C. Reading field_0 /
                     * field_2 instead made the keeper claim the ball on frames
                     * he should have played on — playa-10090613 dropped into
                     * ST_KEEPER_HOLDS_BALL at 912 where the recording plays on
                     * (docs/TESTS.md). The sibling gate below reads `48(a0)` =
                     * field_30 and was already right. */
                    d1 -= gs->field_2A;
                    if (d1 < 0) {
                        /* comment_timing_gate_fail_dir: check X proximity */
                        int16_t xd = player->x.s.i - ball->x.s.i;
                        if (xd > 6 || xd < -6) {
                            goto redirect_to_alt_flow;
                        }
                        /* Close enough — claim ball */
                        _lastTeamPlayed = team;
                        _lastPlayerPlayed = player;
                        ball->x.s.i = player->x.s.i;
                        ball->y.s.i = player->y.s.i;
                        ball->destX = ball->x.s.i;
                        ball->destY = ball->y.s.i;
                        ball->speed = 0;
                        team->goalkeeperDivingRight = 1;
                        GoalkeeperClaimedTheBall(player, ball, team);
                        PlayMissGoalSample();
                        goto finalize;
                    }
                    d1 -= gs->field_2C;
                    if (team->goalkeeperSavedCommentTimer >= 0) {
                        if (d1 < 0)
                            goto continue_post_save_flow;
                        goto redirect_to_alt_flow;
                    } else {
                        if (d1 < 0)
                            goto redirect_to_alt_flow;
                        goto continue_post_save_flow;
                    }
                }
            }

        continue_post_save_flow:
            _lastKeeperPlayed = _lastPlayerPlayed;
            _lastTeamPlayed = team;
            _lastPlayerPlayed = player;
            {
                int16_t s = ball->speed >> 2;
                ball->speed -= s;
            }
            if (_ballSprite.speed > 1792)
                _ballSprite.speed = 1792;

            if (team->goalkeeperSavedCommentTimer < 0) {
                /* Randomize ball dest X */
                int16_t xOff = (_currentGameTick & 0xF) << 7;
                xOff -= 960;
                _ballSprite.destX += xOff;
            } else {
                /* ease_ball_toward_dest_y */
                int16_t by = ball->y.s.i;
                int16_t dy = ball->destY - by;
                int16_t shift = _kKeeperEaseBallTable[(_currentGameTick & 0xE) >> 1];
                dy >>= shift;
                dy = -dy;
                by -= dy;
                ball->destY = by;
                ball->destX = player->destX;
            }
            goto compute_team_y_offset;

        redirect_to_alt_flow:
            _lastTeamPlayed = team;
            _lastPlayerPlayed = player;
            {
                int16_t by = ball->y.s.i;
                int16_t dy = ball->destY - by;
                by -= dy;
                int16_t xOff = (_currentGameTick & 0x1F) << 4;
                xOff -= 256;
                by += xOff;
                ball->destY = by;
                ball->destX = player->destX;
                int16_t s = ball->speed >> 2;
                ball->speed -= s;
                if (_currentGameTick & 0x10) {
                    s >>= 1;
                    ball->speed -= s;
                }
            }
            if (_ballSprite.speed > 1536)
                _ballSprite.speed = 1536;

        compute_team_y_offset:
            {
                int16_t yOff = 1;
                if (team != &_topTeamData)
                    yOff = -1;
                ball->y.s.i = player->y.s.i;
                ball->y.s.i += yOff;
            }
            /* start_pass_clock_and_clear_opponent_pass */
            team->passKickTimer = 25;
            {
                TeamGeneralInfo *opp = team->opponentsTeam;
                opp->passingToPlayer = 0;
                if (team->goalkeeperSavedCommentTimer >= 0)
                    opp->passingKickingPlayer = (Sprite *)0;
            }
            PlayMissGoalSample();
            goto finalize;
        }

    /* ============================================================
     * CONTROLLED PLAYER section
     * ============================================================ */
    its_controlled_player:
        if (_playingPenalties) {
            if (_gameState != ST_PENALTIES)
                goto finalize;
        }

        /* AI controls */
        if (team->playerNumber == 0) {
            AI_SetControlsDirection(team);
        }

        /* update_control_flags_from_ball_state */
        team->allowedDirections = team->playerHasBall;
        team->playerHasBall = 0;

        /* ASM: tst.w SPRITE_DELTAY(a6) — IDA mislabeled this as ball deltaY,
         * but a6 is the TEAM pointer. Offset 50 in TeamGeneralInfo is
         * firePressed(50) + fireThisFrame(51), tested as a word. Clear
         * shooting when BOTH fire inputs are released (word == 0). */
        if (team->shooting) {
            if (team->firePressed == 0 && team->fireThisFrame == 0)
                team->shooting = 0;
        }

        /* handle_break_state_direction_and_camera */
        if (_gameStatePl == ST_GAME_IN_PROGRESS)
            goto ensure_dest_matches_pos_when_unset;

        if (team != _lastTeamPlayedBeforeBreak)
            goto finalize;

        {
            int16_t d0 = player->direction;
            if (!((1 << d0) & _playerTurnFlags)) {
                player->direction = _cameraDirection;
                _turnDirCopiedFromCameraCount++;
            }
        }

        /* validate_or_prepare_fallback_direction */
        {
            int16_t d0 = player->direction;
            if (!((1 << d0) & _playerTurnFlags)) {
                int16_t scan;
                for (scan = 7; scan >= 0; scan--) {
                    if ((1 << scan) & _playerTurnFlags)
                        break;
                }
                if (scan < 0) scan = 0;
                _cameraDirection = scan;
                player->direction = scan;
                _deadThrowInDirectionVar++;
            }
        }

        /* stoppage_timer_gate_for_controlled */
        if (team->playerNumber == 0) {
            /* Amiga 50 (SWOS2.s:30162) / DOS 55 (swos.asm:114574) — the
             * controlled-player twin of the throw-in gate below. */
            if (_stoppageTimerActive <= (uint16_t)_gsThrowInHideResult)
                goto check_any_fire_inputs;
            goto flip_time_and_result_timers;
        }
        if (team->currentAllowedDirection >= 0)
            goto block_fire_if_stats_timer;

    check_any_fire_inputs:
        if (team->firePressed || team->quickFire || team->normalFire)
            goto block_fire_if_stats_timer;
        goto init_allowed_dir_from_player_if_unset;

    block_fire_if_stats_timer:
        if (_statsTimer) {
            _fireBlocked = 1;
        }
    flip_time_and_result_timers:
        _timeVar = -_timeVar;
        _resultTimer = -_resultTimer;

    init_allowed_dir_from_player_if_unset:
        {
            int16_t d0 = team->currentAllowedDirection;
            if (d0 < 0) {
                player->direction = team->currentAllowedDirection = player->direction;
                goto ensure_dest_matches_pos_when_unset;
            }
            if (!((1 << d0) & _playerTurnFlags)) {
                player->direction = team->currentAllowedDirection = player->direction;
                goto ensure_dest_matches_pos_when_unset;
            }
            player->direction = d0;
            UpdatePlayerWithBall(player);
        }

    ensure_dest_matches_pos_when_unset:
        /* tst.w TEAMGENERALINFO_PASSTOPLAYERPTR+2(a1) = player->z.s.i */
        if (player->z.s.i != 0)
            goto finalize;

        if (team->currentAllowedDirection >= 0)
            goto maybe_grant_possession_if_close;
        player->destX = player->x.s.i;
        player->destY = player->y.s.i;

    maybe_grant_possession_if_close:
        if (_gameStatePl != ST_GAME_IN_PROGRESS)
            goto attempt_quick_pass;
        if (team->quickFire || team->normalFire)  /* asm: tst.w covers both bytes */
            goto attempt_quick_pass;
        if (team->shooting)
            goto attempt_quick_pass;

        if (team->plVeryCloseToBall)
            goto enter_possession;
        if (!team->plCloseToBall)
            goto unset_dir_header_tackle_gate;

    enter_possession:
        if (!team->allowedDirections)
            team->ballControlTurnTimer = 0;

        /* attempt_header_or_tackle */
        team->playerHasBall = 1;
        team->opponentsTeam->spinTimer = (int16_t)0xFFFF;

    unset_dir_header_tackle_gate:
        {
            int16_t d0 = team->currentAllowedDirection;
            if (d0 >= 0)
                goto start_win_ball_sequence;

            if (team->playerNumber == 0 || player->playerOrdinal == 1)
                goto jump_finalize;
            if (!team->fireThisFrame)
                goto jump_finalize;
            if (team->currentAllowedDirection >= 0)
                goto jump_finalize;
            if (!team->plVeryCloseToBall && !team->plCloseToBall && !team->plNotFarFromBall)
                goto jump_finalize;

            /* require_ball_in_header_window */
            if (!team->ball8To12 && !team->ball12To17 && !team->ballAbove17)
                goto jump_finalize;

            /* attempt_static_header_sequence */
            team->headerOrTackle = 1;
            {
                int16_t d = player->direction;
                AttemptStaticHeader(player, d);
            }
            team->lastHeadingTacklingPlayer = player;
            team->ballCanBeControlled = 0;
            goto finalize;

        jump_finalize:
            goto finalize;
        }

    start_win_ball_sequence:
        {
            int16_t d0 = team->currentAllowedDirection;
            TeamGeneralInfo *opp = team->opponentsTeam;
            if (!opp->playerHasBall)
                team->wonTheBallTimer = 0;

            if (team->wonTheBallTimer) {
                CONTEST_LOG(ContestLog_NoRoll(team, player, CONTEST_NO_SELF_LOCKED));
                goto compute_oob_dir_mask_and_block;
            }

            if (!team->plVeryCloseToBall) {
                CONTEST_LOG(ContestLog_NoRoll(team, player, CONTEST_NO_NOT_ON_IT));
                goto attempt_quick_pass;
            }
            if (team->ballAbove17 || team->ball12To17) {
                CONTEST_LOG(ContestLog_NoRoll(team, player, CONTEST_NO_BALL_HIGH));
                goto attempt_quick_pass;
            }

            if (!team->prevPlVeryCloseToBall)
                team->ballCanBeControlled = 0;

            /* validate_control_direction_consistency */
            if (d0 != team->ballControllingPlayerDirection)
                team->ballCanBeControlled = 0;

            team->ballCanBeControlled++;
            team->ballControllingPlayerDirection = d0;

            _lastTeamPlayed = team;
            _lastPlayerPlayed = player;
            _penalty = 0;
            _playerHadBall = 0;

            if (opp->playerHasBall)
                _playerHadBall = 1;

            CalculateIfPlayerWinsBall(d0, player, team);
            opp->spinTimer = (int16_t)0xFFFF;
            opp->passingToPlayer = 0;
            opp->passingKickingPlayer = (Sprite *)0;
            goto compute_oob_dir_mask_and_block;
        }

    attempt_quick_pass:
        if (!team->quickFire)
            goto abort_quick_pass;
        {
            int16_t d0 = team->currentAllowedDirection;
            if (d0 < 0) goto abort_quick_pass;
            if (!team->plVeryCloseToBall && !team->plCloseToBall)
                goto abort_quick_pass;
            if (_gameStatePl != ST_GAME_IN_PROGRESS) {
                if (!((1 << d0) & _playerTurnFlags))
                    goto abort_quick_pass;
            }
            if (player->playerOrdinal != 1 && !team->ballLessEqual4)
                goto abort_quick_pass;

            /* execute_pass_and_update_context */
            _lastTeamPlayed = team;
            _lastPlayerPlayed = player;
            _penalty = 0;
            _playerHadBall = 0;
            ball->z.s.i = 0;
            DoPass(player, team);
            team->lastHeadingTacklingPlayer = player;

            if (_gameStatePl != ST_GAME_IN_PROGRESS) {
                ThrowInDeadProc();
                _gameNotInProgressCounterWriteOnly = 0;
            }
            if (_gameState == ST_PENALTY)
                _penalty = 1;

            _gameStatePl = ST_GAME_IN_PROGRESS;
            _gameState = ST_GAME_IN_PROGRESS;
            team->ballInPlay = 1;
            team->ballOutOfPlay = 1;
            {
                TeamGeneralInfo *opp = team->opponentsTeam;
                opp->ballInPlay = 1;
                opp->ballOutOfPlay = 1;
                opp->spinTimer = (int16_t)0xFFFF;
            }
            team->controlledPlayer = (Sprite *)0;
            team->passingKickingPlayer = player;
            team->passKickTimer = 25;
            team->ballCanBeControlled = 0;
            {
                TeamGeneralInfo *opp = team->opponentsTeam;
                opp->passingToPlayer = 0;
                opp->passingKickingPlayer = (Sprite *)0;
            }
            goto snap_dest;
        }

    abort_quick_pass:
        if (!team->normalFire)
            goto handle_fire_for_tackle_or_header;
        {
            int16_t d0 = team->currentAllowedDirection;
            if (d0 < 0) goto handle_fire_for_tackle_or_header;
            if (!team->plVeryCloseToBall && !team->plCloseToBall)
                goto handle_fire_for_tackle_or_header;
            if (_gameStatePl != ST_GAME_IN_PROGRESS) {
                if (!((1 << d0) & _playerTurnFlags))
                    goto handle_fire_for_tackle_or_header;
            }
            if (player->playerOrdinal != 1 && !team->ballLessEqual4)
                goto handle_fire_for_tackle_or_header;

            /* execute_shot_and_update_context */
            _lastTeamPlayed = team;
            _lastPlayerPlayed = player;
            _penalty = 0;
            _playerHadBall = 0;
            PlayerKickingBall(player, team);
            team->lastHeadingTacklingPlayer = player;

            if (_gameStatePl != ST_GAME_IN_PROGRESS) {
                ThrowInDeadProc();
                _gameNotInProgressCounterWriteOnly = 0;
            }
            if (_gameState == ST_PENALTY)
                _penalty = 1;

            _gameStatePl = ST_GAME_IN_PROGRESS;
            _gameState = ST_GAME_IN_PROGRESS;
            team->ballInPlay = 1;
            team->ballOutOfPlay = 1;
            {
                TeamGeneralInfo *opp = team->opponentsTeam;
                opp->ballInPlay = 1;
                opp->ballOutOfPlay = 1;
                opp->spinTimer = (int16_t)0xFFFF;
            }
            team->controlledPlayer = (Sprite *)0;
            team->passingKickingPlayer = player;
            team->passKickTimer = 25;
            team->ballCanBeControlled = 0;
            team->shooting = 1;
            {
                TeamGeneralInfo *opp = team->opponentsTeam;
                opp->passingToPlayer = 0;
                opp->passingKickingPlayer = (Sprite *)0;
            }
            goto snap_dest;
        }

    handle_fire_for_tackle_or_header:
        /* Tackle attempt */
        if (player->playerOrdinal != 1 && team->fireThisFrame &&
            team->currentAllowedDirection >= 0 && team->plNotFarFromBall) {
            if (team->ballLessEqual4 || team->ball4To8) {
                Sprite *passTarget = team->passToPlayerPtr;
                if (passTarget == (Sprite *)0 ||
                    passTarget->ballDistance >= player->ballDistance) {
                    /* Tackle site 1 of 2: `begin_tackle_and_clear_control`
                     * (data/SWOS2.s:30428) = `@@begin_tackling`
                     * (swos-port/swos/swos.asm:115123). NO opponent clears here, in
                     * EITHER release -- site 2 below has them and this one does not,
                     * and that asymmetry is deliberate in both. Adding them here
                     * because site 2's asm reads like this one's took nine recordings
                     * from 100% to 11-66% on camera_y (2026-09-17). */
                    team->headerOrTackle = 1;
                    PlayerBeginTackling(team->currentAllowedDirection, player, team);
                    team->ballCanBeControlled = 0;
                    goto finalize;
                }
            }
        }

        /* evaluate_jump_header_flow */
        if (player->playerOrdinal != 1 && team->fireThisFrame &&
            team->currentAllowedDirection >= 0) {
            if (team->plVeryCloseToBall || team->plCloseToBall || team->plNotFarFromBall) {
                if (team->ball8To12 || team->ball12To17 || team->ballAbove17) {
                    team->headerOrTackle = 1;
                    PlayerAttemptingJumpHeader(team->currentAllowedDirection, player);
                    team->lastHeadingTacklingPlayer = player;
                    team->ballCanBeControlled = 0;
                    goto finalize;
                }
            }
        }

        /* gate_in_play_and_valid_dir */
        if (_gameStatePl != ST_GAME_IN_PROGRESS)
            goto finalize;
        if (team->currentAllowedDirection < 0)
            goto finalize;

    compute_oob_dir_mask_and_block:
        {
            int16_t px = player->x.s.i;
            int16_t py = player->y.s.i;
            uint8_t mask = 0;
            int16_t d0 = team->currentAllowedDirection;

            if (px < 79)  mask |= 0xE0;
            if (px > 592) mask |= 0x0E;
            if (py < 127) mask |= 0x83;
            if (py > 771) mask |= 0x38;

            if ((1 << d0) & mask) {
                player->destX = player->x.s.i;
                player->destY = player->y.s.i;
                goto finalize;
            }

            /* apply_default_dest_from_dir */
            {
                int16_t idx = d0 * 2;
                player->destX = player->x.s.i + _kDefaultDestinations[idx];
                player->destY = player->y.s.i + _kDefaultDestinations[idx + 1];
            }
            goto finalize;
        }

    /* ============================================================
     * THROW-IN section
     * ============================================================ */
    throw_in_state_handler:
        if (team->playerNumber == 0)
            AI_SetControlsDirection(team);

        /* align_throw_in_facing_with_flags */
        {
            int16_t d0 = player->direction;
            if (!((1 << d0) & _playerTurnFlags)) {
                player->direction = _cameraDirection;
                _disallowedTurnFlagsCounter++;
            }
        }
        {
            int16_t d0 = player->direction;
            if (!((1 << d0) & _playerTurnFlags)) {
                int16_t scan;
                for (scan = 7; scan >= 0; scan--) {
                    if ((1 << scan) & _playerTurnFlags)
                        break;
                }
                if (scan < 0) scan = 0;
                _cameraDirection = scan;
                player->direction = scan;
                _deadThrowInDirectionVar++;
            }
        }

        /* handle_throw_in_substitution_case */
        if (_substituteInProgress) {
            if (player == _substitutedPlSprite)
                goto throw_in_unhide_ball;
        }

        if (_gameState < ST_THROW_IN_FORWARD_RIGHT || _gameState > ST_THROW_IN_BACK_LEFT)
            goto throw_in_unhide_ball;

        if (player->playerDownTimer != 0) {
            player->playerDownTimer--;
            if (player->playerDownTimer == 0)
                goto throw_in_reset_to_idle;
            if ((uint8_t)player->playerDownTimer == 18) {
                if (_inSubstitutesMenu) {
                    SetPlayerAnimationTable(&_aboutToThrowInAnimTable, player);
                    player->playerState = PL_THROW_IN;
                    player->playerDownTimer = 0;
                    goto manage_throw_in_ui;
                }
                /* prepare_throw_in_and_route */
                ball->z.s.i = 12;
                _hideBall = 0;
                if (_throwInPassOrKick)
                    goto throw_in_commit_pass;
                goto throw_in_commit_kick;
            }
            goto finalize;
        }

    manage_throw_in_ui:
        /* Stoppage timer / fire blocking logic (same as controlled player break) */
        if (team->playerNumber == 0) {
            /* Amiga 50 (SWOS2.s:30563) / DOS 55 (swos.asm:115401) */
            if (_stoppageTimerActive <= (uint16_t)_gsThrowInHideResult)
                goto throw_in_fire_input_gate;
            goto toggle_stats_timers_throw;
        }
        if (team->currentAllowedDirection >= 0)
            goto apply_stats_hide_fire_block_throw;

    throw_in_fire_input_gate:
        if (team->firePressed || team->quickFire || team->normalFire)
            goto apply_stats_hide_fire_block_throw;
        goto ensure_throw_in_dir;

    apply_stats_hide_fire_block_throw:
        if (_statsTimer)
            _fireBlocked = 1;
    toggle_stats_timers_throw:
        _timeVar = -_timeVar;
        _resultTimer = -_resultTimer;

    ensure_throw_in_dir:
        {
            int16_t d0 = team->currentAllowedDirection;
            if (d0 >= 0) {
                if (!((1 << d0) & _playerTurnFlags)) {
                    d0 = player->direction;
                    team->currentAllowedDirection = d0;
                    goto throw_in_quick_pass;
                }
                if (d0 != player->direction) {
                    player->direction = d0;
                    SetThrowInPlayerDestinationCoordinates(player);
                    SetPlayerAnimationTable(&_aboutToThrowInAnimTable, player);
                    player->playerState = PL_THROW_IN;
                    player->playerDownTimer = 0;
                }
            } else {
                team->currentAllowedDirection = player->direction;
                d0 = team->currentAllowedDirection;
            }

        throw_in_quick_pass:
            if (team->quickFire) {
                if ((1 << d0) & _playerTurnFlags) {
                    _throwInPassOrKick = 1;
                    SetPlayerAnimationTable(&_throwInPassAnimTable, player);
                    player->playerDownTimer = 20;
                    goto finalize;
                }
            }

            /* throw_in_kick_trigger */
            if (team->normalFire) {
                if ((1 << d0) & _playerTurnFlags) {
                    _throwInPassOrKick = 0;
                    SetPlayerAnimationTable(&_throwInKickAnimTable, player);
                    player->playerDownTimer = 25;
                }
            }
            goto finalize;
        }

    throw_in_commit_pass:
        _lastTeamPlayed = team;
        _lastPlayerPlayed = player;
        _playerHadBall = 1;
        DoPass(player, team);
        team->lastHeadingTacklingPlayer = player;
        ball->deltaZ.raw = 1;
        if (_gameStatePl != ST_GAME_IN_PROGRESS) {
            ThrowInDeadProc();
            _gameNotInProgressCounterWriteOnly = 0;
        }
        goto throw_in_post_state_setup;

    throw_in_commit_kick:
        _lastTeamPlayed = team;
        _lastPlayerPlayed = player;
        _playerHadBall = 1;
        PlayerKickingBall(player, team);
        team->lastHeadingTacklingPlayer = player;
        if (_gameStatePl != ST_GAME_IN_PROGRESS) {
            ThrowInDeadProc();
            _gameNotInProgressCounterWriteOnly = 0;
        }

    throw_in_post_state_setup:
        _gameStatePl = ST_GAME_IN_PROGRESS;
        _gameState = ST_GAME_IN_PROGRESS;
        team->ballInPlay = 1;
        team->ballOutOfPlay = 1;
        {
            TeamGeneralInfo *opp = team->opponentsTeam;
            opp->ballInPlay = 1;
            opp->ballOutOfPlay = 1;
            opp->spinTimer = (int16_t)0xFFFF;
        }
        team->controlledPlayer = (Sprite *)0;
        team->passingKickingPlayer = player;
        team->passKickTimer = 25;
        team->ballCanBeControlled = 0;
        {
            TeamGeneralInfo *opp = team->opponentsTeam;
            opp->passingToPlayer = 0;
            opp->passingKickingPlayer = (Sprite *)0;
        }
        goto finalize;

    throw_in_unhide_ball:
        _hideBall = 0;
    throw_in_reset_to_idle:
        player->playerState = PL_NORMAL;
        SetPlayerAnimationTable(&_playerNormalStandingAnimTable, player);
        goto finalize;

    /* ============================================================
     * TACKLE STATE section
     * ============================================================ */
    tackle_update_state:
        if (player->tacklingTimer >= 0)
            player->tacklingTimer++;

        player->playerDownTimer--;
        if (player->playerDownTimer == 0) {
            player->playerState = PL_NORMAL;
            SetPlayerAnimationTable(&_playerNormalStandingAnimTable, player);
            goto snap_dest;
        }

        /* tackle_early_release_window */
        if (player->tacklingTimer >= 0 && team->playerNumber != 0 && !team->firePressed) {
            player->tacklingTimer = -player->tacklingTimer;
            /* asm: `cmpi.w #$FFFE,...; blt <skip>` — the branch away is taken
             * only when the timer is STRICTLY LESS than -2, so -2 itself falls
             * through and becomes -1. `> -2` left -2 alone; the replay rig read
             * it as sprite[9].tackling_timer -1 against our -2. */
            if (player->tacklingTimer >= -2)
                player->tacklingTimer = -1;
        }

        /* tackle_apply_ground_decel_or_downtime */
        if (player->speed == 0)
            goto finalize;

        player->speed -= _kPlayerGroundConstant;
        if (player->speed <= 0) {
            player->speed = 0;
            SetPlayerDowntimeAfterTackle(player, team);
            goto finalize;
        }

        /* tackle_out_of_pitch_slow_near_goalmouth */
        if (player->x.s.i < 73 || player->x.s.i > 598)
            goto tackle_edge_slowdown;

        if (player->y.s.i < 129 || player->y.s.i > 769) {
            /* Out of pitch by Y */
            if (player->x.s.i >= 275 && player->x.s.i <= 396) {
                /* Near goal — heavy slowdown. FOUR shifts, so /16: the Amiga
                 * has `lsr SPRITE_SPEED(a1)` x4 (SWOS2.s:30738-30741) and the
                 * DOS `shr [esi+Sprite.speed], 1` x4 (swos.asm:115736-115743).
                 * BOTH carry the same comment on the `ori` beneath them —
                 * "sprite speed /= 32" — and it is wrong in both, because it
                 * is one annotation from the shared IDA database. This port had
                 * implemented the comment rather than the instructions.
                 * Measured: playa-10095214 frame 4419 goes 736 -> 46|1 = 47,
                 * the recorded value; >> 5 gives the 23 we had. */
                player->speed >>= 4;
                player->speed |= 1;
                goto finalize;
            }
            if (player->y.s.i < 121 || player->y.s.i > 777)
                goto tackle_edge_slowdown;
            goto finalize;
        }

        /* tackle_in_pitch: check ball proximity and foul */
        if (_gameStatePl != ST_GAME_IN_PROGRESS)
            goto tackle_off_ball_foul_check;
        if (player->tackleState)
            goto tackle_off_ball_foul_check;
        if ((uint32_t)player->ballDistance > 64)
            goto tackle_off_ball_foul_check;

        /* tackle_check_ball_proximity */
        if (team->ballLessEqual4 || team->ball4To8) {
            /* tackle_setup_attempt_context */
            int16_t d0 = player->direction;
            _lastTeamPlayed = team;
            _lastPlayerPlayed = player;
            _penalty = 0;
            _playerHadBall = 0;
            {
                TeamGeneralInfo *opp = team->opponentsTeam;
                if (opp->playerHasBall)
                    _playerHadBall = 1;
            }
            if (player->tacklingTimer == (int16_t)0xFFFF)
                PlayerTackledTheBallWeak(player, team);
            else
                PlayerTackledTheBallStrong(player, team);

            team->lastHeadingTacklingPlayer = player;
            team->passKickTimer = 25;
            {
                TeamGeneralInfo *opp = team->opponentsTeam;
                opp->passingToPlayer = 0;
                opp->passingKickingPlayer = (Sprite *)0;
                opp->spinTimer = (int16_t)0xFFFF;
                CONTEST_LOG(ContestLog_Tackle(team, player));
                opp->wonTheBallTimer = 12;
            }
            goto finalize;
        }

    tackle_off_ball_foul_check:
        if (player->speed >= 512)
            PlayerTacklingTestFoul(player, team);
        goto finalize;

    tackle_edge_slowdown:
        {
            int16_t s = player->speed >> 3;
            player->speed -= s;
        }
        goto finalize;

    /* ============================================================
     * HEADING sections
     * ============================================================ */
    player_static_heading:
        player->playerDownTimer--;
        if (player->playerDownTimer == 0) {
            player->playerState = PL_NORMAL;
            SetPlayerAnimationTable(&_playerNormalStandingAnimTable, player);
            goto snap_dest;
        }
        player->speed -= 16;
        if (player->speed < 0) player->speed = 0;
        SetStaticHeaderDirection(player, team);
        if (_gameStatePl != ST_GAME_IN_PROGRESS) goto finalize;
        if (player->heading) goto finalize;
        if ((uint32_t)player->ballDistance > 64) goto finalize;
        if (ball->z.s.i < 8 || ball->z.s.i > 15) goto finalize;
        if (team->currentAllowedDirection < 0) goto finalize;
        {
            int16_t d0 = player->direction;
            _lastTeamPlayed = team;
            _lastPlayerPlayed = player;
            _penalty = 0;
            _playerHadBall = 1;
            PlayerHittingStaticHeader(player, team);
            team->passKickTimer = 25;
            {
                TeamGeneralInfo *opp = team->opponentsTeam;
                opp->passingToPlayer = 0;
                opp->passingKickingPlayer = (Sprite *)0;
            }
        }
        goto finalize;

    player_jump_heading:
        player->playerDownTimer--;
        if (player->playerDownTimer == 0)
            goto heading_reset_to_idle;

        if (player->speed != 0) {
            SetJumpHeaderHitAnimTable(player);
            if (player->animationTable == &_jumpHeaderAttemptAnimTable) {
                if ((uint8_t)player->playerDownTimer > 37)
                    goto apply_air_drag;
            } else {
                if ((uint8_t)player->playerDownTimer > 17)
                    goto apply_air_drag;
            }
            player->speed >>= 1;
            goto finalize;

        apply_air_drag:
            player->speed -= _kPlayerAirConstant;
            if (player->speed < 0)
                player->speed = 0;
        }

        /* heading_boundary_checks */
        if (player->x.s.i < 73 || player->x.s.i > 598)
            goto heading_oob_slowdown;
        if (player->y.s.i < 132 || player->y.s.i > 766) {
            /* Near goalmouth check */
            if (player->x.s.i >= 296 && player->x.s.i <= 375) {
                if (player->x.s.i >= 305 && player->x.s.i <= 366) {
                    /* In goal band */
                    goto heading_goal_line_band_checks;
                }
                player->speed = 0;
                player->playerDownTimer = 0;
                goto heading_reset_to_idle;
            }
        heading_goal_line_band_checks:
            if (player->y.s.i >= 129 && player->y.s.i <= 769)
                goto jump_header_try_execute;
            if (player->x.s.i >= 0x122 && player->x.s.i <= 0x17D) {
                player->speed = 0;
                player->playerDownTimer = 0;
                goto heading_reset_to_idle;
            }
            if (player->y.s.i < 0x79 || player->y.s.i > 0x309)
                goto heading_oob_slowdown;
            goto jump_header_try_execute;
        }

    jump_header_try_execute:
        if ((uint8_t)player->playerDownTimer < 42) goto finalize;
        if (_gameStatePl != ST_GAME_IN_PROGRESS) goto finalize;
        if (player->heading) goto finalize;
        if ((uint32_t)player->ballDistance > 64) goto finalize;
        if (ball->z.s.i < 8 || ball->z.s.i > 15) goto finalize;
        {
            _lastTeamPlayed = team;
            _lastPlayerPlayed = player;
            _penalty = 0;
            _playerHadBall = 1;
            PlayerHittingJumpHeader(player, team);
            team->passKickTimer = 25;
            {
                TeamGeneralInfo *opp = team->opponentsTeam;
                opp->passingToPlayer = 0;
                opp->passingKickingPlayer = (Sprite *)0;
            }
        }
        goto finalize;

    heading_reset_to_idle:
        player->playerState = PL_NORMAL;
        SetPlayerAnimationTable(&_playerNormalStandingAnimTable, player);
        goto snap_dest;

    heading_oob_slowdown:
        {
            int16_t s = player->speed >> 2;
            player->speed -= s;
            s >>= 1;
            player->speed -= s;
        }
        goto finalize;

    /* ============================================================
     * RECOVERY sections
     * ============================================================ */
    player_injured_recover_tick:
        if (_injuriesForever)
            goto finalize;
        player->playerDownTimer--;
        if (player->playerDownTimer != 0)
            goto finalize;
        player->playerState = PL_NORMAL;
        SetPlayerAnimationTable(&_playerNormalStandingAnimTable, player);
        goto snap_dest;

    player_tackled_state:
        player->playerDownTimer--;
        if (player->playerDownTimer == 0) {
            if (player->injuryLevel != 0) {
                player->speed = 0;
                player->playerState = PL_ROLLING_INJURED;
                player->playerDownTimer = (int8_t)player->injuryLevel;
                SetPlayerAnimationTable(&_plInjuredAnimTable, player);
                goto finalize;
            }
            player->playerState = PL_NORMAL;
            SetPlayerAnimationTable(&_playerNormalStandingAnimTable, player);
            goto snap_dest;
        }
        /* tackled_ground_deceleration */
        if (player->speed == 0)
            goto finalize;
        if (player->y.s.i < 159 || player->y.s.i > 739) {
            if (player->x.s.i >= 265 && player->x.s.i <= 406) {
                int16_t s = player->speed >> 2;
                player->speed -= s;
            }
        }
        player->speed -= _kPlayerGroundConstant;
        if (player->speed < 0)
            player->speed = 0;
        goto finalize;

    injury_forever_tick:
        player->speed = _kInjuryForeverSpeed;
        player->destY -= 8;
        goto finalize;

    /* ============================================================
     * RECEIVER / PASS TARGET section
     * ============================================================ */
    receiver_expect_pass_tick:
        /* Check if player dest is within pitch bounds */
        /* Note: asm uses raw offsets $1E/$22 = sprite offsets 30/34 = x.s.i/y.s.i */
        {
            int16_t rx = player->x.s.i;
            if (rx < 81 || rx > 590)
                goto clear_pass_state_oob;
            int16_t ry = player->y.s.i;
            if (ry < 129 || ry > 769)
                goto clear_pass_state_oob;
            goto pass_assist_entry_checks;
        }

    clear_pass_state_oob:
        team->passingToPlayer = 0;
        team->passingBall = 0;
        goto snap_dest;

    pass_assist_entry_checks:
        if (team->playerNumber == 0)
            goto post_pass_assist;

        /* `tst.l TEAMGENERALINFO_LONGPASS(a6)` (SWOS2.s:31006) spans longPass
         * and longSpinPass — see the note in ball_possession.c. */
        if (!team->longPass && !team->longSpinPass &&
            !team->leftSpin && !team->rightSpin)
            goto post_pass_assist;
        if (_ballSprite.speed < 512)
            goto post_pass_assist;
        if (!team->passingToPlayer)
            goto post_pass_assist;
        if (_ballSprite.direction < 0)
            goto pass_assist_set_landing;

        {
            int16_t angleDiff = (uint8_t)_ballSprite.fullDirection - (uint8_t)player->fullDirection;
            if ((uint8_t)angleDiff > 64 && (uint8_t)angleDiff < 192) {
                team->passingToPlayer = 0;
                team->passingBall = 0;
                goto post_pass_assist;
            }
            if ((uint8_t)angleDiff <= 4 || (uint8_t)angleDiff >= 252)
                goto pass_assist_set_landing;

            /* pass_assist_compute_lead_offset */
            {
                /* Also a `tst.l` over the pair (SWOS2.s:31040). This one picks
                 * the lead angle, so getting it wrong sends the receiver to a
                 * different entry of the direction-offset table entirely. */
                int8_t leadAngle = (int8_t)((team->longPass || team->longSpinPass)
                                            ? -32 : -64);
                if ((int8_t)angleDiff >= 0)
                    leadAngle = -leadAngle;

                int16_t frictionDir = _ballSprite.fullDirection + (int8_t)leadAngle + 4;
                frictionDir &= 0xFF;
                int16_t frIdx = ((frictionDir & 0xFF) >> 3) << 1;
                team->ballX = player->x.s.i + _kBallFriction[frIdx];
                team->ballY = player->y.s.i + _kBallFriction[frIdx + 1];
                goto post_pass_assist;
            }
        }

    pass_assist_set_landing:
        team->ballX = player->x.s.i;
        team->ballY = player->y.s.i;

    post_pass_assist:
        if (team->playerNumber == 0)
            AI_Kick(player, team);

        /* breakplay_control_setup_entry */
        if (_gameStatePl != ST_GAME_IN_PROGRESS) {
            if (team != _lastTeamPlayedBeforeBreak)
                goto finalize;
            if (_gameState == ST_KEEPER_HOLDS_BALL && player->playerOrdinal == 1)
                goto breakplay_sync;
            if (player->ballDistance != 0)
                goto pass_follow_decide;

        breakplay_sync:
            if (_cameraDirection < 8) {
                goto breakplay_take_control;
            }
            _cameraDirection = player->direction;
            _cameraSnapToPlayerCount++;

        breakplay_take_control:
            player->direction = _cameraDirection;
            team->controlledPlayer = team->passToPlayerPtr;
            team->passToPlayerPtr = (Sprite *)0;
            team->playerSwitchTimer = 25;
            team->passingBall = 0;
            team->passingToPlayer = 0;
            team->shooting = 0;
            ball->speed = 0; /* TEAMGENERALINFO_CURRENTALLOWEDDIRECTION(a2) = offset 44 = Sprite.speed */
            _lastTeamPlayed = team;
            _lastPlayerPlayed = player;
            _penalty = 0;
            _playerHadBall = 0;
            UpdatePlayerWithBall(player);
            team->passKickTimer = 0;
            team->passingKickingPlayer = (Sprite *)0;
            team->field_74 = 0;

            if (_gameState >= ST_THROW_IN_FORWARD_RIGHT && _gameState <= ST_THROW_IN_BACK_LEFT) {
                SetThrowInPlayerDestinationCoordinates(player);
                SetPlayerAnimationTable(&_aboutToThrowInAnimTable, player);
                player->playerState = PL_THROW_IN;
                player->playerDownTimer = 0;
                _hideBall = 1;
            }
            goto finalize;
        }

        /* inplay_try_begin_tackle */
        if (player->playerOrdinal != 1 && team->fireThisFrame &&
            team->currentAllowedDirection >= 0 && team->plNotFarFromBall) {
            if (team->ballLessEqual4 || team->ball4To8) {
                Sprite *ctrl = team->controlledPlayer;
                if (ctrl == (Sprite *)0 || ctrl->ballDistance >= player->ballDistance) {
                    /* Tackle site 2 of 2: `tackle_start_and_clear_control`
                     * (data/SWOS2.s:31139) = `cseg_82CE1`
                     * (swos-port/swos/swos.asm:116491). THIS one clears the
                     * opponent's pass state, in both releases; site 1 above does not. */
                    team->headerOrTackle = 1;
                    PlayerBeginTackling(team->currentAllowedDirection, player, team);
                    team->ballCanBeControlled = 0;
                    {
                        TeamGeneralInfo *opp = team->opponentsTeam;
                        opp->passingToPlayer = 0;
                        opp->passingKickingPlayer = (Sprite *)0;
                    }
                    goto finalize;
                }
            }
        }

        /* pass_chase_try_autoswitch */
        if (!team->ballLessEqual4)
            goto pass_follow_decide;
        if (_ballSprite.speed >= 1536) {
            if (team->plCloseToBall)
                goto autoswitch_commit;
        }
        if (!team->plVeryCloseToBall)
            goto pass_follow_decide;

    autoswitch_commit:
        team->controlledPlayer = team->passToPlayerPtr;
        team->passToPlayerPtr = (Sprite *)0;
        team->playerSwitchTimer = 25;
        team->passingBall = 0;
        team->passingToPlayer = 0;
        team->shooting = 0;
        player->destX = player->x.s.i;
        player->destY = player->y.s.i;

        CONTEST_LOG(ContestLog_TrapBefore());
        if (player->playerOrdinal == 1)
            UpdateBallWithControllingGoalkeeper(player);
        else
            UpdateControllingPlayer(player);
        CONTEST_LOG(ContestLog_Trap(team, player));

        team->passKickTimer = 0;
        team->passingKickingPlayer = (Sprite *)0;
        team->field_74 = 0;

        if (player->playerOrdinal == 1) {
            if (team != _lastTeamPlayed || !_playerHadBall) {
                team->goalkeeperPlaying = 1;
                team->goaliePlayingOrOut = 1;
                team->ballOutOfPlayOrKeeper = 1;
            } else {
                GoalkeeperClaimedTheBall(player, ball, team);
            }
        }
        _lastTeamPlayed = team;
        _lastPlayerPlayed = player;
        _penalty = 0;
        _playerHadBall = 0;
        goto finalize;

    pass_follow_decide:
        if (!team->passingBall) {
            Sprite *ctrl = team->controlledPlayer;
            if (ctrl != (Sprite *)0 && (uint32_t)ctrl->ballDistance <= 3200) {
                if (ctrl->ballDistance <= player->ballDistance) {
                    if ((uint32_t)player->ballDistance <= 3200) {
                        player->destX = player->x.s.i;
                        player->destY = player->y.s.i;
                        goto finalize;
                    }
                    goto pass_follow_update_dest;
                }
            }
            team->passingBall = 1;
        }

    pass_follow_update_dest:
        player->destX = team->ballX;
        player->destY = team->ballY;
        {
            int16_t px = player->x.s.i;
            int16_t py = player->y.s.i;
            if (px == player->destX && py == player->destY)
                goto finalize;
        }

        /* Check goalkeeper box constraints */
        if (_gameStatePl == ST_GAME_IN_PROGRESS && player->playerOrdinal == 1 && !team->goalkeeperPlaying) {
            int16_t px = player->x.s.i, py = player->y.s.i;
            if (team == &_bottomTeamData) {
                if (py < 692 || px < 203 || px > 468)
                    goto pass_follow_abort;
            } else {
                if (py > 206 || px < 203 || px > 468)
                    goto pass_follow_abort;
            }
            goto finalize;
        }

        /* goal_out_stop */
        if (_goalOut && player->playerOrdinal != 1) {
            int16_t px = player->x.s.i, py = player->y.s.i;
            if (px >= 183 && px <= 488) {
                if (py <= 226 || py >= 672)
                    goto snap_dest;
            }
        }
        goto finalize;

    pass_follow_abort:
        team->passToPlayerPtr = (Sprite *)0;
        team->passingBall = 0;
        team->passingToPlayer = 0;
        goto snap_dest;

    /* ============================================================
     * NOT CONTROLLED / IDLE PLAYER section
     * ============================================================ */
    not_controlled_player:
        if (_substituteInProgress) {
            if (player == _substitutedPlSprite)
                goto idle_player_common_path;
        }

        /* post_match_reaction_gate */
        if (team->updatePlayerIndex == i) {
            if (_gameStatePl == ST_GAME_IN_PROGRESS)
                goto idle_player_common_path;
            if (_gameState == ST_GAME_ENDED && _winningTeamPtr != (int32_t *)0) {
                if (player->playerState == PL_NORMAL && player->playerOrdinal != 1) {
                    if (player->deltaX.raw == 0 && player->deltaY.raw == 0) {
                        if (Rand() <= 64) {
                            if ((int32_t *)team->inGameTeamPtr == _winningTeamPtr) {
                                SetPlayerAnimationTable(&_playerWinningReactionAnimTable, player);
                                player->playerState = PL_HAPPY;
                            } else {
                                SetPlayerAnimationTable(&_playerLosingReactionAnimTable, player);
                                player->playerState = PL_SAD;
                            }
                        }
                    }
                }
            }
        } else {
            goto next_player;
        }

    idle_player_common_path:
        if (_gameStatePl == ST_GAME_IN_PROGRESS) {
            if (_inGameCounter <= 25) {
                if (_breakState == ST_CORNER_LEFT || _breakState == ST_CORNER_RIGHT)
                    goto next_player;
                if (_breakState >= ST_FREE_KICK_LEFT1 && _breakState <= ST_FREE_KICK_RIGHT3)
                    goto next_player;
            }
        }

        /* mark_dest_reached_phase2 */
        /* TEAMGENERALINFO_BALLY(a1) = offset 100 on Sprite = destReachedState */
        if (player->destReachedState == 1)
            player->destReachedState = 2;

        /* maybe_ai_set_controls_dir */
        if (team->controlledPlayer == (Sprite *)0) {
            if (team->playerNumber == 0)
                AI_SetControlsDirection(team);
        }

        /* booking_process_entry */
        if (player == _bookedPlayer) {
            int16_t bx = _foulXCoordinate + 21;
            int16_t by = _foulYCoordinate;
            if (bx != player->x.s.i || by != player->y.s.i) {
                player->destX = bx;
                player->destY = by;
                goto finalize;
            }
            if (_refState != REF_WAITING_PLAYER)
                goto finalize;
            _refState = REF_ABOUT_TO_GIVE_CARD;
            if (_whichCard == CARD_YELLOW)
                SetPlayerAnimationTable(&_plGettingYellowCardAnimTable, player);
            else if (_whichCard == CARD_RED)
                SetPlayerAnimationTable(&_plGettingRedCardAnimTable, player);
            else
                SetPlayerAnimationTable(&_plGetting2ndYellowCardAnimTable, player);
            player->playerState = PL_BOOKED;
            player->playerDownTimer = 1;
            PlayGameStateWhistleSample();
            goto finalize;
        }

        /* substitute_in_progress_hub */
        if (_substituteInProgress) {
            if (_substituteInProgress < 0) {
                /* try_complete_when_stationary */
                if (player == _substitutedPlSprite) {
                    player->speed = _kSubstitutedPlayerSpeed;
                    if (player->deltaX.raw == 0 && player->deltaY.raw == 0)
                        _substituteInProgress = 0;
                }
                goto choose_idle_target;
            }
            if (player == _substitutedPlSprite) {
                if (player->injuryLevel == (int16_t)0xFFFE) {
                    goto finalize_substituted;
                }
                if (_substitutedPlDestX == player->x.s.i && _substitutedPlDestY == player->y.s.i) {
                    if (_substituteInProgress == 2) {
                        goto finalize_substituted;
                    }
                    _substituteInProgress = 2;
                    _substitutedPlDestX = _plComingX;
                    _substitutedPlDestY = _plComingY;
                }
                player->destX = _substitutedPlDestX;
                player->destY = _substitutedPlDestY;
                player->speed = _kSubstitutedPlayerSpeed;
                goto finalize;

            finalize_substituted:
                _substituteInProgress = -1;
                player->x.s.i = _plSubstitutedX;
                player->y.s.i = _plSubstitutedY;
                player->speed = _kSubstitutedPlayerSpeed;
                player->sentAway = 0;
            }
        }

    choose_idle_target:
        if (player->sentAway)
            goto finalize;

        if (_playingPenalties) {
            ballX = _foulXCoordinate;
            ballY = _foulYCoordinate;
            goto prep_out_of_play_penalties;
        }

        if (_gameStatePl == ST_GAME_IN_PROGRESS) {
            SetPlayerWithNoBallDestination(player, team, ball->x.s.i, ball->y.s.i);
            T2P6_DESTX_TRAP(0xD002);
            goto enforce_free_kick_distance;
        }

        /* stash_gamestate_for_tables */
        ballX = _foulXCoordinate;
        ballY = _foulYCoordinate;
        {
            int16_t gs;
            goto prep_out_of_play_normal;

        prep_out_of_play_penalties:
            gs = ST_PENALTIES;
            goto prep_out_of_play;

        prep_out_of_play_normal:
            gs = _gameState;

        prep_out_of_play:
            if (_gameState == ST_FIRST_HALF_ENDED || _gameState == ST_GAME_ENDED)
                goto finalize;
            if (_gameState == ST_KEEPER_HOLDS_BALL ||
                _gameState == ST_GOAL_OUT_LEFT ||
                _gameState == ST_GOAL_OUT_RIGHT) {
                SetPlayerWithNoBallDestination(player, team, ballX, ballY);
                goto enforce_free_kick_distance;
            }

            /* Restart table lookup */
            {
                int16_t **posTable;
                int16_t *positions;
                int16_t ordinal;

                if (_forceLeftTeam == ST_GOAL_OUT_LEFT) {
                    if (_gameState == ST_CORNER_LEFT || _gameState == ST_CORNER_RIGHT) {
                        if (ballY < 449)
                            goto use_top_table;
                    }
                }
                if (_playingPenalties) {
                    if (team == &_bottomTeamData)
                        goto use_bottom_table;
                    goto use_top_table;
                }
                if (team == _lastTeamPlayedBeforeBreak)
                    goto use_bottom_table;

            use_top_table:
                posTable = _topBallOutOfPlayPositions;
                goto lookup_positions;
            use_bottom_table:
                posTable = _bottomBallOutOfPlayPositions;

            lookup_positions:
                positions = posTable[gs];
                if (positions == (int16_t *)0) {
                    SetPlayerWithNoBallDestination(player, team, ballX, ballY);
                    goto enforce_free_kick_distance;
                }

                /* Free kick spacing for defenders */
                freeKickDestX_local = 0;
                if (team != _lastTeamPlayedBeforeBreak &&
                    gs >= ST_FREE_KICK_LEFT1 && gs <= ST_FREE_KICK_RIGHT3 &&
                    gs != ST_FREE_KICK_LEFT1 && gs != ST_FREE_KICK_RIGHT3) {
                    if (player->playerOrdinal >= 2 && player->playerOrdinal <= 5) {
                        /* Count eligible teammates */
                        int16_t count = 2;
                        int16_t ord = 2;
                        Sprite **tbl = (Sprite **)team->spritesTable + 1;
                        while (ord != player->playerOrdinal) {
                            Sprite *s = *tbl++;
                            if (s->cards >= 0)
                                count++;
                            ord++;
                        }

                        /* Compute X offset */
                        {
                            int16_t factor = freeKickFactorsX_data[gs - 6];
                            int16_t fkx = factor << 2;
                            freeKickDestX_local = fkx;
                            fkx = factor;
                            int16_t j;
                            Sprite **tbl2 = (Sprite **)team->spritesTable + 1;
                            for (j = 3; j >= 0; j--) {
                                Sprite *s = *tbl2++;
                                if (s->cards >= 0)
                                    freeKickDestX_local -= fkx;
                            }
                        }
                        ordinal = count;
                        goto index_restart;
                    }
                }

                ordinal = player->playerOrdinal;

            index_restart:
                {
                    int16_t idx = (ordinal - 1) * 2;
                    if (positions[idx] == 22222) {
                        SetPlayerWithNoBallDestination(player, team, ballX, ballY);
                        goto enforce_free_kick_distance;
                    }

                    {
                        int16_t dx, dy;
                        if (team == &_topTeamData) {
                            dx = positions[idx];
                            dy = positions[idx + 1];
                            if (dx <= -1000) {
                                dx += 1000;
                                dx += freeKickDestX_local;
                            } else if (dx >= 1000) {
                                dx -= 1000;
                                dx += freeKickDestX_local;
                            } else {
                                dx += 5;
                                player->destX = dx + 81;
                                player->destY = dy + 129;
                                goto finalize;
                            }
                            dx += ballX;
                            dy += ballY;
                        } else {
                            dx = positions[idx];
                            dy = positions[idx + 1];
                            if (dx <= -1000) {
                                dx += 1000;
                                dx = -dx;
                                dy = -dy;
                                dx -= freeKickDestX_local;
                            } else if (dx >= 1000) {
                                dx -= 1000;
                                dx = -dx;
                                dy = -dy;
                                dx -= freeKickDestX_local;
                            } else {
                                dx = 509 - dx - 5;
                                dy = 640 - dy;
                                player->destX = dx + 81;
                                player->destY = dy + 129;
                                goto finalize;
                            }
                            dx += ballX;
                            dy += ballY;
                        }
                        player->destX = dx;
                        player->destY = dy;
                        T2P6_DESTX_TRAP(0xD001);
                        goto finalize;
                    }
                }
            }
        }

    enforce_free_kick_distance:
        if (_gameStatePl == ST_GAME_IN_PROGRESS) goto finalize;
        if (player->playerOrdinal == 1) goto finalize;
        if (team == _lastTeamPlayedBeforeBreak) goto finalize;
        {
            int32_t dx = (int32_t)(player->destX - _foulXCoordinate);
            int32_t dy = (int32_t)(player->destY - _foulYCoordinate);
            int32_t dist = dx * dx + dy * dy;
            if (dist > 4225)
                goto finalize;
            {
                int16_t fx = _foulXCoordinate;
                if (fx >= 336)
                    fx -= 70;
                else
                    fx += 70;
                player->destX = fx;
            }
        }
        goto finalize;

    /* ============================================================
     * Common exit paths
     * ============================================================ */
    snap_dest:
        player->destX = player->x.s.i;
        player->destY = player->y.s.i;

    finalize:
        /* UpdatePlayerSpeed */
        UpdatePlayerSpeed(player, team);

        /* Calculate delta X/Y from dest, current pos, speed */
        {
            int32_t outDX, outDY;
            int16_t dir = (int16_t)CalculateDeltaXAndY(
                player->destX, player->destY,
                player->x.s.i, player->y.s.i,
                player->speed, &outDX, &outDY);
            player->deltaX.raw = outDX;
            player->deltaY.raw = outDY;

            /* Keeper facing: if goalkeeper (not playing as field player) and ball in upper penalty area */
            if (player->playerOrdinal == 1 &&
                _gameStatePl == ST_GAME_IN_PROGRESS &&
                !team->goalkeeperPlaying &&
                (_ballInUpperPenaltyArea || _ballInLowerPenaltyArea)) {
                goto compute_facing_from_full;
            }

            if (dir >= 0)
                goto quantize_direction;

            /* No movement: check if controlled or passing/kicking player → preserve direction */
            if (player == team->controlledPlayer)
                goto preserve_manual_dir;
            if (player == team->passingKickingPlayer)
                goto preserve_manual_dir;

        compute_facing_from_full:
            dir = (player->fullDirection + 128) & 0xFF;

        quantize_direction:
            dir = ((dir + 16) & 0xFF) >> 5;
            player->direction = dir;

        preserve_manual_dir:
            ;
        }

        /* Compute full direction from ball/foul to player */
        {
            int16_t targetX, targetY;
            if (_gameStatePl == ST_GAME_IN_PROGRESS || _gameState > ST_THROW_IN_BACK_LEFT) {
                targetX = ball->x.s.i;
                targetY = ball->y.s.i;
            } else {
                targetX = _foulXCoordinate;
                targetY = _foulYCoordinate;
            }
            {
                int32_t dummyDX, dummyDY;
                int16_t fullDir = (int16_t)CalculateDeltaXAndY(
                    player->x.s.i, player->y.s.i,
                    targetX, targetY,
                    256, &dummyDX, &dummyDY);
                if (fullDir < 0) fullDir = 0;
                player->fullDirection = fullDir;
            }
        }

        /* Animation update for normal-state players */
        if (player->playerState != PL_NORMAL)
            goto next_player;

        {
            int8_t nowMoving = (player->deltaX.raw | player->deltaY.raw) ? -1 : 0;
            int8_t wasMoving = player->isMoving ? -1 : 0;

            if (nowMoving != wasMoving) {
                goto switch_anim;
            }
            /* Check if direction changed */
            if (player->playerDirection != player->direction)
                goto switch_anim;
            goto next_player;

        switch_anim:
            if (nowMoving)
                SetPlayerAnimationTable(&_playerRunningAnimTable, player);
            else
                SetPlayerAnimationTable(&_playerNormalStandingAnimTable, player);
        }

    next_player:
        ; /* end of loop body */
    }

    /* ---- Post-loop: goalkeeper possession tracking ---- */
    if (_gameStatePl != ST_GAME_IN_PROGRESS) {
        if (_gameState != ST_KEEPER_HOLDS_BALL)
            return;
    }

    if (_lastPlayerPlayed == &_goalie1Sprite) {
        if (prevLastPlayer != &_goalie1Sprite) {
            _lastPlayerBeforeGoalkeeper = prevLastPlayer;
            _lastTeamScored = prevLastTeamPlayed;
            _nobodysBallTimer = 50;
        }
    } else if (_lastPlayerPlayed == &_goalie2Sprite) {
        if (prevLastPlayer != &_goalie2Sprite) {
            _lastPlayerBeforeGoalkeeper = prevLastPlayer;
            _lastTeamScored = prevLastTeamPlayed;
            _nobodysBallTimer = 50;
        }
    }
}
