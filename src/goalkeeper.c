/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "goalkeeper.h"
#include "dos_mode.h"       /* _gDosGameplay: the dive-index expression differs */
#include "game_types.h"
#include "player_anim.h"
#include "penalty_shootout.h"   /* the drill's chosen shooting order (NextPenalty) */

/* ==================================================================
 * Chunk 48 — Goalkeeper Physics + Penalty System
 *
 * Ported functions:
 *   GetFramesNeededToCoverDistance  (static helper)
 *   ShouldGoalkeeperDive           (A1=keeper, A2=ball, A6=team → D0)
 *   GoalkeeperDeflectedBall        (A2=ball, A6=team)
 *   NextPenalty                     (no params)
 *   GoalkeeperClaimedTheBall       (A1=keeper, A2=ball, A6=team)
 *   CheckIfGoalkeeperClaimedTheBall (no params)
 * ================================================================== */

/* Game states — also defined in game_types.h, used locally for clarity */
/* ST_KEEPER_HOLDS_BALL == 3, ST_PENALTIES == 31, ST_STOPPED == 101 */

/* ---- External asm globals ---- */
extern TeamGeneralInfo _topTeamData;
extern TeamGeneralInfo _bottomTeamData;
extern Sprite          _ballSprite;
extern TeamGame        _topTeamInGame;
extern TeamGame        _bottomTeamInGame;

extern uint16_t _currentGameTick;
extern int16_t  _playingPenalties;
extern int16_t  _penalty;
extern int16_t  _cameraDirection;
extern uint8_t  _playerTurnFlags;
extern int16_t  _forceLeftTeam;
extern int16_t  _gameState;
extern int16_t  _breakCameraMode;
extern int16_t  _foulXCoordinate;
extern int16_t  _foulYCoordinate;
extern int16_t  _gameStatePl;
extern int16_t  _gameNotInProgressCounterWriteOnly;
extern int16_t  _stoppageTimerTotal;
extern int16_t  _stoppageTimerActive;
extern int16_t  _cameraXVelocity;
extern int16_t  _cameraYVelocity;
extern int16_t  _hideBall;
extern int16_t  _teamPlayingUp;
extern int16_t  _teamStarting;
extern int16_t  _penaltiesTimer;
extern int16_t  _statsTeam1Goals;
extern int16_t  _statsTeam2Goals;

extern Sprite           *_lastPlayerPlayed;
extern TeamGeneralInfo  *_lastTeamPlayed;
extern Sprite           *_lastPlayerBeforeGoalkeeper;
extern TeamGeneralInfo  *_lastTeamScored;
extern TeamGeneralInfo  *_lastTeamPlayedBeforeBreak;
extern TeamGame         *_winningTeamPtr;
extern Sprite           *_penaltyShooterSprite;

extern int16_t  _team1PenaltyGoals;
extern int16_t  _team2PenaltyGoals;
extern int16_t  _team1PenaltyAttempts;
extern int16_t  _team2PenaltyAttempts;
extern int16_t  _savedTeam1Goals;
extern int16_t  _savedTeam2Goals;
extern int16_t  _team1PenaltyShooterIndex;
extern int16_t  _team2PenaltyShooterIndex;

extern int16_t  _ballDefensiveX;
extern int16_t  _goalkeeperDiveAttemptCount;

extern int16_t  _kDefaultDestinations[16];

extern int16_t  _kGoalkeeperNearJumpSpeed;
extern int16_t  _kGoalkeeperFarJumpSpeed;
extern int16_t  _kGoalkeeperFarJumpSlowerSpeed;
extern int16_t  _ballNotHighZ;

/* Animation tables */
extern AnimTable _leftGoalieJumpingLowAnimTable;
extern AnimTable _rightGoalieJumpingLowAnimTable;
extern AnimTable _leftGoalieJumpingHighAnimTable;
extern AnimTable _rightGoalieJumpingHighAnimTable;

/* Data tables */
extern int16_t  _kKeeperPenaltySaveDistanceFar;
extern int16_t  _kKeeperPenaltySaveDistanceNear;
extern int16_t  _kKeeperSaveDistance;
extern int32_t  _kGoalkeeperDiveDeltas[8];
extern int16_t  _kGoalkeeperStrongDeflectBallSpeed;
extern int16_t  _kGoalkeeperMediumDeflectBallSpeed;
extern int16_t  _kGoalkeeperWeakDeflectBallSpeed;
extern int32_t  _kGoalkeeperDeflectDeltaZ;

/* Functions from other modules */
extern uint16_t Rand(void);
extern void     PlayKickSample(void);
extern void     ResetBothTeamSpinTimers(void);
extern void     InitTeamsData(void);
extern void     StopAllPlayers(void);
extern void     UpdatePlayerWithBall(uint8_t *player);
extern void     EndOfGame(void);
extern void     PlayersLeavingPitch(void);


/* ==================================================================
 * getFramesNeededToCoverDistance (static)
 *   from: GetFramesNeededToCoverDistance
 *   in:  movementDelta (D0, fixed 16.16), distance (D4, integer)
 *   out: frames needed (D7, integer)
 *
 * Division D4/D0 via loop subtraction. If D0 < 1.0, scales it up
 * along with the loop step to limit iterations.
 * Returns 0 if D0 == 0.
 * ================================================================== */
static int16_t getFramesNeededToCoverDistance(int32_t movementDelta, int16_t distance)
{
    int32_t speed;
    int16_t step;
    int32_t dist32;
    int16_t frames;

    if (movementDelta == 0)
        return 0;

    speed = movementDelta;
    if (speed < 0)
        speed = -speed;

    step = 1;
    while ((uint32_t)speed < 0x10000u) {
        speed <<= 1;
        step <<= 1;
    }

    /* Convert distance to 16.16 fixed point (swap word) */
    dist32 = (int32_t)((uint32_t)(uint16_t)distance << 16);

    frames = 0;
    do {
        frames += step;
        dist32 -= speed;
    } while (dist32 >= 0);

    return frames;
}


/* ==================================================================
 * ShouldGoalkeeperDive
 *   from: ShouldGoalkeeperDive
 *   in:  keeper (A1), ball (A2), team (A6)
 *   out: 1 = should dive, 0 = no dive
 * ================================================================== */
int16_t __attribute__((used, externally_visible))
ShouldGoalkeeperDive(Sprite *keeper, Sprite *ball, TeamGeneralInfo *team)
{
    int16_t dy;
    int16_t dx;
    int16_t randVal;
    int16_t threshold;
    int16_t framesY, framesX, framesDive;
    GoalieSkill *shotTable;
    int16_t skillIndex;
    int32_t diveDelta;

    /* Compute signed distance ball.Y - keeper.Y, negate for bottom team */
    dy = ball->y.s.i - keeper->y.s.i;
    if (team != &_topTeamData)
        dy = -dy;

    /* Ball in front of keeper (dy < 0): if within 10 pixels, dive */
    if (dy < 0) {
        if (dy < -10)
            return 0;   /* too far in front */
        return 1;       /* close enough, dive */
    }

    /* dy >= 0 : ball is behind or level with keeper */
    if (dy < 0)
        return 0;       /* unreachable but mirrors asm bmi branch */

    /* Penalty distance gate */
    if (_playingPenalties || _penalty) {
        threshold = _kKeeperPenaltySaveDistanceFar;
        randVal = (int16_t)Rand();
        if (randVal & 0x18) {
            threshold = _kKeeperPenaltySaveDistanceNear;
        }
        if (dy > threshold)
            return 0;
        return 1;
    }

    /* Open play distance gate */
    threshold = _kKeeperSaveDistance;
    if (dy > threshold)
        return 0;

    /* Compute absolute ball dy for frames calculation */
    {
        int16_t absBallDy = ball->y.s.i - keeper->y.s.i;
        if (absBallDy < 0) absBallDy = -absBallDy;

        framesY = getFramesNeededToCoverDistance(ball->deltaY.raw, absBallDy);
        if (framesY == 0)
            return 0;
    }

    /* Compute frames for keeper to reach ballDefensiveX */
    dx = _ballDefensiveX - keeper->x.s.i;
    if (dx < 0) dx = -dx;

    framesX = getFramesNeededToCoverDistance(keeper->deltaX.raw, dx);
    if (framesX == 0)
        return 0;

    /* If keeper can reach defensive X faster than ball reaches keeper Y, no dive */
    if ((uint16_t)framesX <= (uint16_t)framesY)
        return 0;

    /* Evaluate dive using goalkeeper skill from shot chance table */
    dx = _ballDefensiveX - keeper->x.s.i;
    if (dx < 0) dx = -dx;

    shotTable = team->shotChanceTable;
    /* How the keeper's reach entry is picked is one of the few places where the
     * two releases' listings genuinely disagree, so it is switched rather than
     * chosen:
     *
     *   Amiga  SWOS2.s:26266  andi.w #$3C,d0 / lsr.w #1,d0   -> (tick >> 2) & 15
     *   DOS    swos.asm:106639  and D0,0Fh / shl D0,1        ->  tick & 15
     *   DOS    swos.asm:106644  dec ax / jns / mov ax,0      -> entry - 1, floored at 0
     *
     * The Amiga's index advances every fourth tick and the DOS's every tick;
     * the DOS then steps one entry DOWN its (already lower) reach table. Both
     * encodings are unambiguous, and the corpus measures the difference:
     * zkz-10140228 frame 1108 has the keeper diving in the recording and
     * standing here, and the DOS expression is what makes him dive.
     * The Amiga path is untouched — see docs/DOS_GAMEPLAY.md. */
    if (_gDosGameplay) {
        skillIndex = shotTable->index[(uint16_t)_currentGameTick & 0xF];
        skillIndex--;
        if (skillIndex < 0)
            skillIndex = 0;
    } else {
        /* tick & 0x3C -> 0,4,8,...,60; >> 2 -> 0..15 word index */
        skillIndex = shotTable->index[((uint16_t)_currentGameTick & 0x3C) >> 2];
    }

    diveDelta = _kGoalkeeperDiveDeltas[skillIndex];
    _goalkeeperDiveAttemptCount++;

    framesDive = getFramesNeededToCoverDistance(diveDelta, dx);
    if (framesDive == 0)
        return 0;
    if ((uint16_t)framesDive < (uint16_t)framesY)
        return 0;

    return 1;
}


/* ==================================================================
 * GoalkeeperDeflectedBall
 *   from: GoalkeeperDeflectedBall
 *   in:  ball (A2), team (A6)
 *
 * Sets ball destination with random scatter, picks deflect speed based
 * on goalkeeper skill, adds random Z delta.
 * ================================================================== */
void __attribute__((used, externally_visible))
GoalkeeperDeflectedBall(Sprite *ball, TeamGeneralInfo *team)
{
    int16_t baseDir;
    int16_t destX, destY;
    int16_t scatter;
    GoalieSkill *shotTable;
    int16_t d1;
    int16_t speed;
    int32_t deltaZ;
    int32_t randZ;

    /* Pick base direction: bottom team = index 0, top team = index 4 */
    baseDir = 0;
    if (team != &_bottomTeamData)
        baseDir = 4;

    /* kDefaultDestinations: 8 pairs of (dx, dy) */
    destX = ball->x.s.i + _kDefaultDestinations[baseDir * 2];
    ball->destX = destX;
    destY = ball->y.s.i + _kDefaultDestinations[baseDir * 2 + 1];
    ball->destY = destY;

    /* Random X scatter: (tick & 0x1F) * 32 - 512 */
    scatter = (int16_t)(((uint16_t)_currentGameTick & 0x1F) << 5) - 512;
    ball->destX = (int16_t)(ball->destX + scatter);

    /* Pick deflect speed based on goalkeeper skill */
    shotTable = team->shotChanceTable;
    d1 = (int16_t)(((uint16_t)_currentGameTick & 0x3C) >> 2);
    d1 -= shotTable->field_34;
    if (d1 < 0) {
        speed = _kGoalkeeperStrongDeflectBallSpeed;
    } else {
        d1 -= shotTable->field_36;
        if (d1 < 0) {
            speed = _kGoalkeeperMediumDeflectBallSpeed;
        } else {
            speed = _kGoalkeeperWeakDeflectBallSpeed;
        }
    }

    /* Add random speed variation: (tick & 0x1FF) - 256 + speed */
    {
        int16_t randSpeed = (int16_t)((uint16_t)_currentGameTick & 0x1FF) - 256;
        ball->speed = (int16_t)(randSpeed + speed);
    }

    /* Compute delta Z with random scatter */
    deltaZ = _kGoalkeeperDeflectDeltaZ;
    randZ = (int32_t)((uint32_t)_currentGameTick & 0x7F) << 8;
    randZ -= 16384;
    deltaZ += randZ;
    ball->deltaZ.raw = deltaZ;

    ResetBothTeamSpinTimers();
    PlayKickSample();
}


/* ==================================================================
 * NextPenalty
 *   from: NextPenalty
 *
 * Advances penalty shootout state. Checks for end conditions,
 * sets up next round, picks shooter.
 * ================================================================== */
void __attribute__((used, externally_visible))
NextPenalty(void)
{
    int16_t totalAttempts;
    int16_t *shooterIdx;

    totalAttempts = _team1PenaltyAttempts + _team2PenaltyAttempts;

    if (totalAttempts >= 10) {
        /* After 10 total attempts, check if both teams have taken equal */
        if (_team1PenaltyAttempts != _team2PenaltyAttempts)
            goto setup_next;
        /* Equal attempts: end if goals differ */
        if (_team1PenaltyGoals != _team2PenaltyGoals)
            goto end_shootout;
        goto setup_next;
    }

    /* Before 10 attempts: check if either team can't catch up */
    {
        int16_t team1Remaining = 5 - _team1PenaltyAttempts + _team1PenaltyGoals;
        if ((uint16_t)team1Remaining < (uint16_t)_team2PenaltyGoals)
            goto end_shootout;

        int16_t team2Remaining = 5 - _team2PenaltyAttempts + _team2PenaltyGoals;
        if ((uint16_t)team2Remaining < (uint16_t)_team1PenaltyGoals)
            goto end_shootout;
    }

setup_next:
    _ballSprite.z.s.i = 0;

    /* Toggle teamPlayingUp: neg then add 3 (flips 1↔2) */
    _teamPlayingUp = -_teamPlayingUp + 3;
    _teamStarting  = -_teamStarting + 3;

    _hideBall = 0;
    InitTeamsData();

    _gameState = ST_PENALTIES;
    _breakCameraMode = -1;
    _cameraDirection = 0;          /* north */
    _playerTurnFlags = 0x83;       /* NW, N, NE */
    _foulXCoordinate = 336;        /* upper white spot */
    _foulYCoordinate = 187;
    _gameStatePl = ST_STOPPED;
    _gameNotInProgressCounterWriteOnly = 0;

    _lastTeamPlayedBeforeBreak = &_bottomTeamData;
    _stoppageTimerTotal = 0;
    _stoppageTimerActive = 0;
    StopAllPlayers();
    _cameraXVelocity = 0;
    _cameraYVelocity = 0;
    _penaltiesTimer = 0;

    /* Pick shooter index: decrement, wrap 0→10 */
    shooterIdx = &_team1PenaltyShooterIndex;
    if (_bottomTeamData.teamNumber != 1)
        shooterIdx = &_team2PenaltyShooterIndex;

    /* PENALTY SHOOTOUT (the training drill) shoots in the order chosen on its
     * SHOOTING ORDER screen instead of the countdown. Written through the same
     * sim variable so the sprite pick below, and a save of the match, see
     * nothing new; -1 is every match that is not the drill. */
    {
        int16_t orderedSlot = PenaltyShootout_NextShooterSlot(
            _bottomTeamData.teamNumber,
            (_bottomTeamData.teamNumber == 1) ? _team1PenaltyAttempts
                                              : _team2PenaltyAttempts);
        if (orderedSlot > 0) {
            *shooterIdx = orderedSlot;
        } else {
            (*shooterIdx)--;
            if (*shooterIdx == 0)
                *shooterIdx = 10;
        }
    }

    /* Load penalty shooter sprite pointer */
    {
        int16_t idx = *shooterIdx;
        _penaltyShooterSprite = _bottomTeamData.spritesTable[idx];
    }

    /* Increment attempt counter for the appropriate team */
    if (_bottomTeamData.teamNumber == 1)
        _team1PenaltyAttempts++;
    else
        _team2PenaltyAttempts++;
    return;

end_shootout:
    _playingPenalties = 0;
    _statsTeam1Goals = _savedTeam1Goals;
    _statsTeam2Goals = _savedTeam2Goals;

    /* The two releases end a shoot-out differently, and it is not a constant.
     *
     *   end_penalty_shootout:                       ; SWOS2.s:27760
     *           ...
     *           lea     (topTeamInGame).l,a0
     *           move.w  (team1PenaltyGoals).l,d0
     *           cmp.w   (team2PenaltyGoals).l,d0
     *           bhi.s   set_winning_team_pointer
     *           lea     (bottomTeamInGame).l,a0
     *   set_winning_team_pointer:
     *           move.l  a0,(winningTeamPtr).l
     *           bsr.w   EndOfGame
     *
     *   @@finish_penalties:                         ; swos.asm:104434
     *           ...
     *           call    PlayersLeavingPitch
     *           retn
     *
     * The DOS build names no winner and goes straight to
     * ST_PLAYERS_GOING_TO_SHOWER, skipping ST_GAME_ENDED entirely. It has both
     * routines and calls `EndOfGame` from `UpdateTime` (swos.asm:98001), so
     * nothing about the conversion forced this. Measured on zkz-10140228,
     * which ends in a shoot-out: ST_GAME_ENDED appears on NONE of its 39066
     * frames, and ST_PLAYERS_GOING_TO_SHOWER begins at f38395 — the exact
     * frame our `EndOfGame` fired — and runs 275 frames, which is
     * `PlayersLeavingPitch`'s own DOS reload.
     *
     * `winningTeamPtr` is not cosmetic on the Amiga side: it is what
     * `UpdatePlayers`' post-match gate reads to pick the winning or losing
     * reaction animation. */
    if (_gDosGameplay) {
        PlayersLeavingPitch();
        return;
    }

    /* Determine winner */
    if (_team1PenaltyGoals > _team2PenaltyGoals)
        _winningTeamPtr = &_topTeamInGame;
    else
        _winningTeamPtr = &_bottomTeamInGame;

    EndOfGame();
}


/* ==================================================================
 * GoalkeeperClaimedTheBall
 *   from: GoalkeeperClaimedTheBall
 *   in:  keeper (A1), ball (A2), team (A6)
 *
 * Sets keeper-holds-ball state, camera direction, turn flags,
 * game state, stoppage, and optionally passes control to keeper.
 * ================================================================== */
void __attribute__((used, externally_visible))
GoalkeeperClaimedTheBall(Sprite *keeper, Sprite *ball, TeamGeneralInfo *team)
{
    _lastPlayerBeforeGoalkeeper = _lastPlayerPlayed;
    _lastTeamScored = _lastTeamPlayed;

    ball->speed = 0;

    if (team == &_bottomTeamData) {
        _cameraDirection = 0;         /* north */
        _playerTurnFlags = 0xC7;      /* left/right/up combo */
    } else {
        _cameraDirection = 4;         /* south */
        _playerTurnFlags = 0x7C;      /* left/right/down combo */
    }

    /* If no human player, mask off left+right from turn flags */
    if (team->playerNumber == 0)
        _playerTurnFlags &= 0xBB;     /* clear bits for left and right */

    /* forceLeftTeam override */
    if (_forceLeftTeam == 1)
        team = &_topTeamData;

    _gameState = ST_KEEPER_HOLDS_BALL;
    _breakCameraMode = -1;
    _foulXCoordinate = _ballSprite.x.s.i;
    _foulYCoordinate = _ballSprite.y.s.i;
    _gameStatePl = ST_STOPPED;
    _gameNotInProgressCounterWriteOnly = 0;
    _lastTeamPlayedBeforeBreak = team;
    _stoppageTimerTotal = 0;
    _stoppageTimerActive = 0;
    StopAllPlayers();

    /* If keeper is not currently diving, give control to keeper */
    if (team->goalkeeperDivingRight != 0)
        return;
    if (team->goalkeeperDivingLeft != 0)
        return;

    team->controlledPlayer = keeper;
    team->ballOutOfPlayOrKeeper = 1;
    keeper->direction = (int16_t)_cameraDirection;
    team->goaliePlayingOrOut = 1;
    UpdatePlayerWithBall((uint8_t *)keeper);

    _cameraXVelocity = 0;
    _cameraYVelocity = 0;
}


/* ==================================================================
 * CheckIfGoalkeeperClaimedTheBall
 *   from: CheckIfGoalkeeperClaimedTheBall
 *
 * If gameState == ST_KEEPER_HOLDS_BALL: re-call GoalkeeperClaimedTheBall
 * with the team's goalkeeper sprite. Otherwise enter break idle state.
 * ================================================================== */
void __attribute__((used, externally_visible))
CheckIfGoalkeeperClaimedTheBall(void)
{
    if (_gameState == ST_KEEPER_HOLDS_BALL) {
        TeamGeneralInfo *team = _lastTeamPlayedBeforeBreak;
        Sprite *keeper = team->spritesTable[0];  /* first sprite = goalkeeper */
        GoalkeeperClaimedTheBall(keeper, &_ballSprite, team);
        return;
    }

    /* enter_break_idle_state */
    _breakCameraMode = -1;
    _gameStatePl = ST_STOPPED;
    _gameNotInProgressCounterWriteOnly = 0;
    _stoppageTimerTotal = 0;
    _stoppageTimerActive = 0;
    StopAllPlayers();
    _cameraXVelocity = 0;
    _cameraYVelocity = 0;
}


/* ==================================================================
 * GoalkeeperJumping
 *   from: GoalkeeperJumping
 *   in:  D0=diveDir, D1=speedVariant, D3=destDir,
 *        A1=keeper, A2=ball, A6=team
 *
 * Sets goalkeeper dive speed (near/far/random), selects low or high
 * dive animation based on ballNotHighZ, sets destination from
 * kDefaultDestinations table.
 * ================================================================== */
void __attribute__((used, externally_visible))
GoalkeeperJumping(int16_t diveDir, int16_t speedVariant, int16_t destDir,
                   Sprite *keeper, Sprite *ball, TeamGeneralInfo *team)
{
    /* Set speed based on ball distance and speed variant */
    if (keeper->ballDistance <= 128) {
        keeper->speed = _kGoalkeeperNearJumpSpeed;
    } else {
        keeper->speed = _kGoalkeeperFarJumpSpeed;
        if (speedVariant != 0) {
            keeper->speed = _kGoalkeeperFarJumpSlowerSpeed;
            if (speedVariant != 1) {
                int16_t spd = (int16_t)(_currentGameTick & 0xFF);
                spd += _kGoalkeeperNearJumpSpeed;
                keeper->speed = spd;
            }
        }
    }

    /* Clear field_46 high byte (big-endian) and goalkeeperDivingRight */
    team->field_46 &= 0xFF;
    team->goalkeeperDivingRight = 0;

    /* Choose dive animation based on ballNotHighZ */
    if (_ballNotHighZ > 5) {
        /* High dive */
        keeper->playerState = (uint8_t)PL_GOALIE_DIVING_HIGH;
        if (team == &_bottomTeamData)
            SetPlayerAnimationTable(&_rightGoalieJumpingHighAnimTable, keeper);
        else
            SetPlayerAnimationTable(&_leftGoalieJumpingHighAnimTable, keeper);
    } else {
        /* Low dive */
        keeper->playerState = (uint8_t)PL_GOALIE_DIVING_LOW;
        if (team == &_bottomTeamData)
            SetPlayerAnimationTable(&_rightGoalieJumpingLowAnimTable, keeper);
        else
            SetPlayerAnimationTable(&_leftGoalieJumpingLowAnimTable, keeper);
    }

    /* Set downtime and controlled player direction */
    keeper->playerDownTimer = (int8_t)75;
    team->controlledPlDirection = diveDir;

    /* Set destination from kDefaultDestinations using destDir */
    int16_t destIdx = destDir * 2;
    keeper->destX = keeper->x.s.i + _kDefaultDestinations[destIdx];
    keeper->destY = keeper->y.s.i + _kDefaultDestinations[destIdx + 1];
}


/* ==================================================================
 * GoalkeeperForceDiveAnim — OpenSWOS, for PENALTY SHOOTOUT's manual keeper
 *
 * GoalkeeperJumping above picks the high or low dive from the ball's
 * height, which is the right call for the AI: it knows where the ball
 * is. The manual keeper guesses the height along with the side (DOWN
 * held = low), so after GoalkeeperJumping has set everything else up,
 * this re-picks the state + animation pair from the player's choice —
 * the same selection, keyed on the input instead of _ballNotHighZ.
 * ================================================================== */
void GoalkeeperForceDiveAnim(Sprite *keeper, TeamGeneralInfo *team, int16_t low)
{
    if (low) {
        keeper->playerState = (uint8_t)PL_GOALIE_DIVING_LOW;
        if (team == &_bottomTeamData)
            SetPlayerAnimationTable(&_rightGoalieJumpingLowAnimTable, keeper);
        else
            SetPlayerAnimationTable(&_leftGoalieJumpingLowAnimTable, keeper);
    } else {
        keeper->playerState = (uint8_t)PL_GOALIE_DIVING_HIGH;
        if (team == &_bottomTeamData)
            SetPlayerAnimationTable(&_rightGoalieJumpingHighAnimTable, keeper);
        else
            SetPlayerAnimationTable(&_leftGoalieJumpingHighAnimTable, keeper);
    }
}
