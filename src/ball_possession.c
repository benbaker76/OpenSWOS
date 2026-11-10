/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "tweaks.h"            /* [tweaks] contest_own_ratings */
#include "ball_possession.h"
#include "dos_sound.h"
#include "math68k.h"  /* M68kShiftLeft16: a signed left shift is UB, and the two platforms differ */
#include "game_types.h"
#include "contest_log.h"    /* --contest-log: reads only, changes nothing */

/* ---- External globals ---- */
extern Sprite           _ballSprite;
extern TeamGeneralInfo  _topTeamData;
extern TeamGeneralInfo  _bottomTeamData;

extern int16_t  _kDefaultDestinations[];     /* 8 direction deltas: dx,dy pairs */

extern int16_t  _gameState;
extern int16_t  _gameStatePl;
extern uint16_t _currentGameTick;
extern uint16_t _currentTick;
extern uint16_t _runSlower;
extern uint16_t _stoppageTimerTotal;
extern int16_t  _foulXCoordinate;
extern int16_t  _foulYCoordinate;

/* Data tables kept in SWOS2.s */
extern int16_t  _kBallSpeedPassingIncrease[];    /* int16_t[8] */
extern int16_t  _kPassingSpeedCloserThan2500;
extern int16_t  _kPassingSpeed_2500_10000;
extern int16_t  _kPassingSpeed_10000_22500;
extern int16_t  _kPassingSpeed_22500_40000;
extern int16_t  _kPassingSpeed_40000_62500;
extern int16_t  _kPassingSpeed_62500_90000;
extern int16_t  _kPassingSpeed_90000_122500;
extern int16_t  _kPassingSpeedFurtherThan122500;
extern int16_t  _kFreePassReleasingBallSpeed;
extern int16_t  _kPlayerSpeedsGameInProgress[];  /* int16_t[8] */
extern int16_t  _kPlayerSpeedsGameStopped[];     /* int16_t[8] */
extern int16_t  _kAIFailedPassChance[];          /* int16_t[8] */
extern int16_t  _kBallSpeedDeltaWhenControlled[];/* int16_t[8] */
extern int16_t  _ballControlTurnThresholds[];    /* int16_t[8] */
extern uint8_t  _plAvgTacklingBallControlDiffChance[]; /* int8_t[8] */
extern int16_t  _kBallSpeedKicking[];            /* int16_t[8] */
extern int16_t  _kBallSpeedFinishing[];          /* int16_t[8] */
extern int32_t  _kBallKickingDeltaZ;
extern int16_t  _kBallKickingSpeed;
extern int16_t  _kInjuriesSpeedHandicap[];       /* int16_t[8] */
extern GoalieSkill *_kGoalieSkillTables[];       /* ptr[8] */
extern uint32_t _teamPlOfs24Table;               /* address used as pointer value */

/* Ball dest delta tables */
extern int16_t  _kLeftThrowInBallDestDelta[];
extern int16_t  _kRightThrowInBallDestDelta[];
extern int16_t  _kPenaltyBallDestDelta[];
extern int16_t  _kUpperLeftCornerBallDestDelta[];
extern int16_t  _kUpperRightCornerBallDestDelta[];
extern int16_t  _kLowerLeftCornerBallDestDelta[];
extern int16_t  _kLowerRightCornerBallDestDelta[];

/* Already-ported functions */
/* Returns (TeamGame *) + ingameTeamPlayerOffsets[idx]: add 42 to get PlayerInfo * */
extern PlayerInfo *GetPlayerPointerFromShirtNumber(Sprite *playerSprite, TeamGeneralInfo *teamData);
extern void ResetBothTeamSpinTimers(void);
extern void PlayGoodPassSample(void);
extern void PlayKickSample(void);
extern uint16_t Rand(void);

/* ================================================================
 *  GetBallDestCoordinatesTable
 *  Returns pointer to appropriate 8x(dx,dy) ball destination delta
 *  table based on current game state (throw-in/penalty/corner/default).
 * ================================================================ */
int16_t * __attribute__((used, externally_visible))
GetBallDestCoordinatesTable(void)
{
    /* Throw-in states */
    if (_gameState >= ST_THROW_IN_FORWARD_RIGHT &&
        _gameState <= ST_THROW_IN_BACK_LEFT) {
        if (_foulXCoordinate > 336)
            return _kRightThrowInBallDestDelta;
        return _kLeftThrowInBallDestDelta;
    }

    /* Penalty states */
    if (_gameState == ST_PENALTY || _gameState == ST_PENALTIES)
        return _kPenaltyBallDestDelta;

    /* Corner kicks */
    if (_gameState == ST_CORNER_LEFT || _gameState == ST_CORNER_RIGHT) {
        if (_foulYCoordinate > 449) {
            /* Lower corners */
            if (_foulXCoordinate > 336)
                return _kLowerRightCornerBallDestDelta;
            return _kLowerLeftCornerBallDestDelta;
        }
        /* Upper corners */
        if (_foulXCoordinate > 336)
            return _kUpperRightCornerBallDestDelta;
        return _kUpperLeftCornerBallDestDelta;
    }

    /* Default: normal play */
    return _kDefaultDestinations;
}

/* ================================================================
 *  GetClosestNonControlledPlayerInDirection
 *  Finds the closest teammate (not controlled, not sent away, normal
 *  state) facing approximately the given direction (within +/-16/256).
 *  Returns pointer to closest player sprite, or NULL if none found.
 * ================================================================ */
Sprite * __attribute__((used, externally_visible))
GetClosestNonControlledPlayerInDirection(uint16_t direction, TeamGeneralInfo *team)
{
    Sprite    **spritesTable = team->spritesTable;
    uint16_t   dirShifted   = direction << 5;
    Sprite    *bestPlayer   = (Sprite *)0xFFFFFFFF;
    uint32_t   bestDist     = 0xFFFFFFFF;
    Sprite    *controlledPl = team->controlledPlayer;
    int16_t    i;

    /* Ascending: `movea.l (a2)+,a1` under a counting-down `dbf`. Ties on
     * ballDistance go to the LOWEST index — see UpdateControlledPlayer
     * (team_control.c) for what getting this backwards costs. */
    for (i = 0; i <= 10; i++) {
        Sprite *pl = spritesTable[i];

        /* Skip controlled player */
        if (pl == controlledPl)
            goto next;

        /* Skip sent-away players */
        if (pl->sentAway != 0)
            goto next;

        /* Only PL_NORMAL */
        if (pl->playerState != PL_NORMAL)
            goto next;

        /* Direction check: fullDirection - dirShifted must be in [240..255] or [0..16] */
        {
            uint8_t diff = (uint8_t)((uint16_t)pl->fullDirection - dirShifted);
            if (diff >= 17 && diff <= 239)
                goto next;
        }

        /* Distance check: closer than current best? */
        {
            uint32_t dist = (uint32_t)pl->ballDistance;
            if (dist >= bestDist)
                goto next;
            bestDist  = dist;
            bestPlayer = pl;
        }
next:
        ;
    }

    return bestPlayer;
}

/* ================================================================
 *  DoPass
 *  Handles passing the ball: finds target player or uses default
 *  direction, sets ball destination and speed, may botch AI passes.
 * ================================================================ */
void __attribute__((used, externally_visible))
DoPass(Sprite *player, TeamGeneralInfo *team)
{
    int16_t  dir = player->direction;
    int16_t  savedDir = dir;
    PlayerInfo *a4;
    Sprite  *targetPlayer;
    Sprite  *ball;
    int16_t  d1;
    int16_t  toTeamMate = 0;    /* the PC game's goodPassSampleCommand == -1 */

    team->controlledPlDirection = dir;

    /* Save dir, call GetPlayerPointerFromShirtNumber, restore dir */
    a4 = GetPlayerPointerFromShirtNumber(player, team);

    /* Find closest teammate in direction */
    targetPlayer = GetClosestNonControlledPlayerInDirection((uint16_t)savedDir, team);

    if (targetPlayer == (Sprite *)0xFFFFFFFF)
        goto pass_to_preset_direction;

    /* Found a target player */
    team->passToPlayerPtr = targetPlayer;
    team->passingBall     = 1;
    team->passingToPlayer = 1;

    /* AI failed pass check: only during game in progress, only for CPU team */
    if (_gameStatePl == ST_GAME_IN_PROGRESS &&
        team->playerNumber == 0) {
        PlayerInfo *pi = a4;
        int16_t passingSkill = pi->passing;
        int16_t idx = passingSkill * 2;
        int16_t threshold;
        int16_t tickVal;

        threshold = *(int16_t *)((uint8_t *)_kAIFailedPassChance + idx);
        tickVal = (_currentGameTick & 0x1E) >> 1;

        if (tickVal < threshold) {
            /* Botch the pass: aim between ball and target */
            ball = &_ballSprite;
            {
                int16_t dx = targetPlayer->x.s.i - ball->x.s.i;
                if (!((_currentGameTick >> 5) & 1))  /* btst #5,currentGameTick+1 -> bne */
                    dx >>= 1;
                dx = M68kShiftLeft16(dx, 5);
                dx += ball->x.s.i;
                ball->destX = dx;
            }
            {
                int16_t dy = targetPlayer->y.s.i - ball->y.s.i;
                if ((_currentGameTick >> 5) & 1)     /* btst #5,currentGameTick+1 -> beq */
                    dy >>= 1;
                dy = M68kShiftLeft16(dy, 5);
                dy += ball->y.s.i;
                ball->destY = dy;
            }
            goto set_pass_speed;
        }
    }

    /* calculate_pass_to_player_delta_x_y */
    {
        ball = &_ballSprite;
        d1 = targetPlayer->x.s.i - ball->x.s.i;
        {
            int16_t d2 = targetPlayer->y.s.i - ball->y.s.i;

            if (d1 == 0 && d2 == 0)
                d1 = 1;

            /* Scale deltas until ball dest is out of pitch bounds */
            for (;;) {
                int16_t testX = ball->x.s.i + d1;
                if (testX < 0 || testX >= 672)
                    break;
                {
                    int16_t testY = ball->y.s.i + d2;
                    if (testY < 0 || testY >= 880)
                        break;
                }
                d1 = M68kShiftLeft16(d1, 1);
                d2 = M68kShiftLeft16(d2, 1);
            }

            ball->destX = ball->x.s.i + d1;
            ball->destY = ball->y.s.i + d2;
        }
    }

set_pass_speed:
    /* Determine pass speed based on distance to target */
    ball = &_ballSprite;
    {
        uint32_t dist = (uint32_t)targetPlayer->ballDistance;
        int16_t speed;

        if (dist < 2500)
            speed = _kPassingSpeedCloserThan2500;
        else if (dist < 10000)
            speed = _kPassingSpeed_2500_10000;
        else if (dist < 22500)
            speed = _kPassingSpeed_10000_22500;
        else if (dist < 40000)
            speed = _kPassingSpeed_22500_40000;
        else if (dist < 62500)
            speed = _kPassingSpeed_40000_62500;
        else if (dist < 90000)
            speed = _kPassingSpeed_62500_90000;
        else if (dist < 122500)
            speed = _kPassingSpeed_90000_122500;
        else
            speed = _kPassingSpeedFurtherThan122500;

        /* Adjust for passing skill */
        {
            PlayerInfo *pi = a4;
            speed += _kBallSpeedPassingIncrease[pi->passing];
        }
        ball->speed = speed;
    }
    toTeamMate = 1;
    goto finalize_pass;

pass_to_preset_direction:
    {
        int16_t *dt  = GetBallDestCoordinatesTable();
        int16_t  idx = savedDir * 2;  /* lsl.w #2,d7 -> 4 bytes per entry = 2 int16_t per entry */

        ball = &_ballSprite;
        ball->destX = ball->x.s.i + dt[idx];
        ball->destY = ball->y.s.i + dt[idx + 1];
        ball->speed = _kFreePassReleasingBallSpeed;
    }

finalize_pass:
    ResetBothTeamSpinTimers();

    /* If team has player number (i.e. is human-controlled), check spin timer */
    if (team->playerNumber != 0)
        team->spinTimer = 0;

    /* Set pass-in-progress flag (asm: move.w #1,$80(a6) -- $80 = 128 = passInProgress) */
    team->passInProgress = 1;

    /* Play pass sample only during in-progress or non-throw-in states */
    if (_gameStatePl == ST_GAME_IN_PROGRESS)
        goto trigger_pass;
    if (_gameState >= ST_THROW_IN_FORWARD_RIGHT &&
        _gameState <= ST_THROW_IN_BACK_LEFT)
        return;

trigger_pass:
    /* The PC game: PlayKickSample, then PlayStopGoodPassSampleIfNeeded -- a pass to
     * a team mate counts towards the "good pass" line. */
    DOS_SOUND(DOS_EV_KICK, 0);
    if (toTeamMate)
        DOS_SOUND(DOS_EV_PASS, 0);
    PlayGoodPassSample();
}

/* ================================================================
 *  PlayerKickingBall
 *  Handles kicking (shooting) the ball: sets ball dest from direction
 *  table, applies shooting/finishing skill speed bonus, plays sample.
 * ================================================================ */
void __attribute__((used, externally_visible))
PlayerKickingBall(Sprite *player, TeamGeneralInfo *team)
{
    int16_t  dir = player->direction;
    int16_t *dt;
    int16_t  idx;
    PlayerInfo *a4;
    Sprite  *ball;

    /* Store direction for later shot-on-goal check */
    team->controlledPlDirection = dir;

    dt   = GetBallDestCoordinatesTable();
    ball = &_ballSprite;
    idx  = dir * 2; /* lsl.w #2,d0 -> 4 bytes = 2 words per direction */

    ball->destX      = ball->x.s.i + dt[idx];
    ball->destY      = ball->y.s.i + dt[idx + 1];
    ball->speed      = _kBallKickingSpeed;
    ball->deltaZ.raw = _kBallKickingDeltaZ;

    ResetBothTeamSpinTimers();

    /* Check game state for shot-on-goal bonus */
    if (_gameStatePl != ST_GAME_IN_PROGRESS)
        goto check_throw_in;

    goto check_shot_on_goal;

check_throw_in:
    if (_gameState >= ST_THROW_IN_FORWARD_RIGHT &&
        _gameState <= ST_THROW_IN_BACK_LEFT)
        goto exit_kick;
    /* Fall through to shot-on-goal check */

check_shot_on_goal:
    {
        int16_t kickDir = team->controlledPlDirection;

        if (team == &_topTeamData) {
            /* Top team shoots at bottom goal */
            if (ball->y.s.i < 556)
                goto not_a_shot;
            if (kickDir != 4 && kickDir != 3 && kickDir != 5)
                goto not_a_shot;
        } else {
            /* Bottom team shoots at top goal */
            if (ball->y.s.i > 342)
                goto not_a_shot;
            if (kickDir != 0 && kickDir != 1 && kickDir != 7)
                goto not_a_shot;
        }

        /* Check if ball is in shooting area (X between 241..431) */
        if (ball->x.s.i < 241 || ball->x.s.i > 431)
            goto its_a_long_shot;

        /* Close-range finishing shot */
        if (ball->y.s.i >= 204 && ball->y.s.i < 694)
            goto its_a_long_shot;

        /* its_a_finishing_shot */
        a4 = GetPlayerPointerFromShirtNumber(player, team);
        {
            PlayerInfo *pi = a4;
            int16_t finSkill = pi->finishing;
            int16_t bonus    = _kBallSpeedFinishing[finSkill];
            ball->speed += bonus;
        }
        goto not_a_shot;

    its_a_long_shot:
        a4 = GetPlayerPointerFromShirtNumber(player, team);
        {
            PlayerInfo *pi = a4;
            int16_t shootSkill = pi->shooting;
            int16_t bonus      = _kBallSpeedKicking[shootSkill];
            ball->speed += bonus;
        }
    }

not_a_shot:
    /* Check goalkeeper/ordinal for spin timer */
    if (player->playerOrdinal == 1) {
        if (player->direction == 2 ||
            player->direction == 6)
            goto play_kick_sample;
    }

    /* Check opponent goalkeeper saved comment timer */
    {
        TeamGeneralInfo *oppTeam = team->opponentsTeam;
        if (oppTeam->goalkeeperSavedCommentTimer < 0)
            goto play_kick_sample;
    }
    team->spinTimer = 0;

play_kick_sample:
    team->passInProgress = 0;
    PlayKickSample();
    return;

exit_kick:
    return;
}

/* ================================================================
 *  CalculateIfPlayerWinsBall
 *  Resolves ball possession when two players contest it, or updates
 *  ball position when controlled by the current player.
 *
 *  Asm register flow for ball possession contest:
 *    Entry: d0=dir, a1=player, a6=team ("our" team)
 *    a0 = *(a6+0) = opponent team
 *    FIRST GetPlayerPointerFromShirtNumber: a1=opp controlled player, a6=our team
 *      -> cross-team read (opponent's shirt# looked up in our team's data) -- original game bug
 *    a6 switched to opponent team
 *    SECOND GetPlayerPointerFromShirtNumber: a1=opp controlled player, a6=opp team
 *      -> correct read
 *    sub.b d0,d1 -> skill difference
 *    If diff<0: negate, switch a6 back via OPPONENTSTEAM
 *    Random check determines which team (tracked by a6) wins the ball
 * ================================================================ */
/* WHOSE RATING THE ROLL TAKES FOR THE TEAM THAT TRIGGERS IT.
 *
 * Both originals read it from the wrong man. The roll is entered with a1 = this team's
 * player and a6 = this team; it loads the OPPONENT's man into a1 (to test there is one)
 * and calls GetPlayerPointerFromShirtNumber without putting its own back, so the lookup
 * is "the opponent's man's line-up number, in OUR team": a team-mate who is nowhere near
 * the ball. SWOS2.s:23439-23443, and the same in swos.asm (whose author wrote "A1 ->
 * opponent's controlled player, but A6 -> this team?" beside it). The second lookup, for
 * the opponent, is right. docs/BALL_CONTEST.md, "Whose ratings the roll uses".
 *
 * The fix rates this team by the man who is actually at the ball: `[tweaks]
 * contest_own_ratings` (include/tweaks.h; ON by default since 2026-11-10, off in the rigs,
 * where a recording of the original only replays with the original's roll), or
 * -DSWOS_CONTEST_OWN_RATINGS=1 for a build with no SWOS.ini (the Amiga's, through EXTRA_CFLAGS).
 *
 * OFFLINE ONLY for now. Two netplay peers must roll the same dice, and a recording must
 * be replayed by the rule it was played with; neither the handshake nor the .SRP says
 * which rule that is yet, so an online match and every re-simulation play the original's
 * whatever this says (contestOwnRatings below). */
#ifndef SWOS_CONTEST_OWN_RATINGS
#define SWOS_CONTEST_OWN_RATINGS 0
#endif
int _gContestOwnRatings = SWOS_CONTEST_OWN_RATINGS;

static int contestOwnRatings(void)
{
#ifdef SWOS_SDL_BUILD
    extern int32_t NetplayMatch_Enabled(void);
    if (NetplayMatch_Enabled())
        return 0;
    if (Tweaks_On(TWEAK_CONTEST_OWN_RATINGS))
        return 1;
#endif
    return _gContestOwnRatings;
}

void __attribute__((used, externally_visible))
CalculateIfPlayerWinsBall(uint16_t dir, Sprite *player, TeamGeneralInfo *team)
{
    Sprite *ball;
    PlayerInfo *a4;

    team->passInProgress = 0;

    /* Check if opponent has won the ball recently */
    {
        TeamGeneralInfo *oppTeam = team->opponentsTeam;  /* a0 = *(a6+0) */

        if (oppTeam->wonTheBallTimer != 0) {
            CONTEST_LOG(ContestLog_NoRoll(team, player, CONTEST_NO_OPP_LOCKED));
            goto set_default_ball_target;
        }

        if (oppTeam->playerHasBall == 0) {
            CONTEST_LOG(ContestLog_NoRoll(team, player, CONTEST_NO_OPP_NOT_ON_BALL));
            goto set_default_ball_target;
        }

        if (oppTeam->currentAllowedDirection < 0) {
            CONTEST_LOG(ContestLog_NoRoll(team, player, CONTEST_NO_OPP_STILL));
            goto set_default_ball_target;
        }

        /* Ball possession contest -- track a6 register explicitly */
        {
            TeamGeneralInfo *a6_reg = team;  /* a6 starts as our team */
            Sprite          *oppControlled;
            uint8_t          d1_skill, d0_skill;
            int8_t           diff;

            /* a1 = *(a0 + controlledPlayer) = opponent's controlled player */
            oppControlled = oppTeam->controlledPlayer;
            if (oppControlled == (Sprite *)0) {
                CONTEST_LOG(ContestLog_NoRoll(team, player, CONTEST_NO_OPP_NOBODY));
                goto skip_possession;
            }

            /* FIRST GetPlayerPointerFromShirtNumber:
               a1 = opponent's controlled player, a6 = our team (cross-team!) */
            a4 = GetPlayerPointerFromShirtNumber(contestOwnRatings() ? player : oppControlled, a6_reg);
            {
                PlayerInfo *pi = a4;
                d1_skill = (uint8_t)((pi->tackling + pi->ballControl) >> 1);
            }

            /* Switch a6 to opponent team: movea.l OPPONENTSTEAM(a6),a6 */
            a6_reg = a6_reg->opponentsTeam;

            /* a1 = *(a6 + controlledPlayer) = opponent team's controlled player */
            {
                Sprite *oppControlled2 = a6_reg->controlledPlayer;
                if (oppControlled2 == (Sprite *)0)
                    goto skip_possession;

                /* SECOND GetPlayerPointerFromShirtNumber:
                   a1 = opponent's controlled player, a6 = opponent team (correct read) */
                a4 = GetPlayerPointerFromShirtNumber(oppControlled2, a6_reg);
            }
            {
                PlayerInfo *pi = a4;
                d0_skill = (uint8_t)((pi->tackling + pi->ballControl) >> 1);
            }

            /* sub.b d0,d1; bpl calc_tackle_skill_difference */
            diff = (int8_t)(d1_skill - d0_skill);
            if (diff >= 0) {
                /* a6 stays as opponent team */
            } else {
                diff   = -diff;  /* neg.b d1 */
                a6_reg = a6_reg->opponentsTeam;  /* switch back */
            }

            /* Random chance based on skill difference */
            {
                int16_t  diff_ext = (int16_t)(int8_t)diff;  /* ext.w d1 */
                uint16_t rnd      = Rand() & 0x1F;
                if (rnd < (uint8_t)_plAvgTacklingBallControlDiffChance[diff_ext]) {
                    /* bcs: award to a6_reg */
                } else {
                    a6_reg = a6_reg->opponentsTeam;
                }
                CONTEST_LOG(ContestLog_Roll(team, player, d1_skill, d0_skill,
                                            (uint8_t)_plAvgTacklingBallControlDiffChance[diff_ext],
                                            rnd, a6_reg));
            }

            /* award_ball_possession */
            a6_reg->wonTheBallTimer = 12;
            ball = &_ballSprite;
            ball->speed = 0;
            ball->destX = ball->x.s.i;
            ball->destY = ball->y.s.i;
            return;
        }

    skip_possession:
        ;  /* Fall through to set_default_ball_target */
    }

set_default_ball_target:
    team->controlledPlDirection = (int16_t)dir;
    ball = &_ballSprite;

    /* Check if ball is rising (deltaZ > 0) -> force deltaZ to -1 */
    if (ball->deltaZ.raw > 0)
        ball->deltaZ.raw = -1;

    /* Direction 0 special case: nudge ball X toward player */
    if (dir == 0) {
        int16_t dx = ball->x.s.i - player->x.s.i;
        if (dx < 4 && dx > -4) {
            if (dx >= 0)
                ball->x.s.i = ball->x.s.i + 1;
            else
                ball->x.s.i = ball->x.s.i - 1;
            goto calculate_ball_destination;
        }
    }

calculate_ball_destination:
    {
        int16_t *defDest = _kDefaultDestinations;
        int16_t  idx     = (int16_t)dir * 2;

        ball->destX = player->x.s.i + defDest[idx];
        ball->destY = player->y.s.i + defDest[idx + 1];
    }

    /* Get player info for ball control skill */
    a4 = GetPlayerPointerFromShirtNumber(player, team);

    /* Set ball speed: player speed + optional ball control delta */
    {
        int16_t speedDelta = 0;
        if (_currentTick & 2) {  /* btst #1,currentTick+1 */
            PlayerInfo *pi = a4;
            int16_t bcSkill = pi->ballControl;
            speedDelta = _kBallSpeedDeltaWhenControlled[bcSkill];
        }
        speedDelta += player->speed;
        ball->speed = speedDelta;
    }

    /* Angle boost: if fullDirection diverges from direction*32 by [64..192], add 256 to speed.
       Asm: cmpi.b #64,d0; bge boost; cmpi.b #192,d0; bgt no_boost -> boost for [64,192] */
    {
        uint8_t angleDiff = (uint8_t)((uint16_t)player->fullDirection + 128);
        uint8_t dirBase   = (uint8_t)(player->direction * 32);
        uint8_t diff      = angleDiff - dirBase;
        if (diff >= 64 && diff <= 192)
            ball->speed += 256;
    }

    /* Check if player changed direction while controlling ball */
    {
        int16_t ctrlDir = team->controlledPlDirection;
        if (ctrlDir == player->direction)
            goto finalize;

        /* Player is turning -- increment turn timer */
        team->ballControlTurnTimer = team->ballControlTurnTimer + 1;

        /* Check turn threshold based on ball control skill */
        {
            PlayerInfo *pi = a4;
            int16_t bcSkill  = pi->ballControl;
            int16_t threshold = _ballControlTurnThresholds[bcSkill];
            if (threshold > team->ballControlTurnTimer)
                goto finalize;
        }

        /* Lost ball on turn */
        CONTEST_LOG(ContestLog_Turn(team, player, team->ballControlTurnTimer,
                                    _ballControlTurnThresholds[a4->ballControl]));
        team->wonTheBallTimer = 8;
    }

finalize:
    ResetBothTeamSpinTimers();
}

/* ================================================================
 *  UpdatePlayerSpeed
 *  Sets player speed based on skill, game state, injuries,
 *  ball possession, and pass-to-target adjustments.
 * ================================================================ */
void __attribute__((used, externally_visible))
UpdatePlayerSpeed(Sprite *player, TeamGeneralInfo *team)
{
    PlayerInfo *a4;
    PlayerInfo *pi;
    int16_t     speed;
    int16_t     speedSkill;

    /* Only for PL_NORMAL players */
    if (player->playerState != PL_NORMAL)
        return;

    /* During game in progress, goalkeeper can only have speed if controlled */
    if (_gameStatePl == ST_GAME_IN_PROGRESS) {
        if (player->playerOrdinal == 1) {
            if (player != team->controlledPlayer)
                return;
        }
    }

    /* Look up base speed from player speed skill */
    a4 = GetPlayerPointerFromShirtNumber(player, team);
    pi = a4;
    speedSkill = pi->speed;
    if (_gameStatePl == ST_GAME_IN_PROGRESS)
        speed = _kPlayerSpeedsGameInProgress[speedSkill];
    else
        speed = _kPlayerSpeedsGameStopped[speedSkill];
    player->speed = speed;

    /* RunSlower penalty: reduce by 37.5% (quarter + eighth) */
    if (_runSlower != 0) {
        int16_t curSpeed = player->speed;
        int16_t quarter  = curSpeed >> 2;
        curSpeed -= quarter;
        curSpeed -= (quarter >> 1);
        player->speed = curSpeed;
    }

    /* Injury speed handicap (only for human or coached teams) */
    if (team->playerNumber != 0 || team->playerCoachNumber != 0) {
        uint8_t injuries = (uint8_t)pi->injuriesBitfield;
        if (injuries != 0) {
            int16_t handicapIdx = (injuries & 0xE0) >> 4;
            int16_t handicap    = _kInjuriesSpeedHandicap[handicapIdx / 2];
            player->speed += handicap;
        }
    }

    /* Ball possession speed penalty: 12.5% slower when has ball */
    if (player == team->controlledPlayer) {
        if (team->playerHasBall != 0) {
            int16_t curSpeed = player->speed;
            curSpeed -= (curSpeed >> 3);
            player->speed = curSpeed;
        }
    }

    /* Pass-to-target player speed adjustment */
    if (team->playerNumber == 0)
        goto no_pass_target;

    if (player != team->passToPlayerPtr)
        goto no_pass_target;

    if (team->passingToPlayer == 0)
        goto no_pass_target;

    /* Check if it's a long pass or spin.
     * `tst.l TEAMGENERALINFO_LONGPASS(a6)` (SWOS2.s:23703) is a LONG test on a
     * WORD field, so it covers `longSpinPass` at 126 as well — the pair is
     * non-zero if either is. Same idiom at :27564, :31006 and :31040. */
    if (team->longPass == 0 &&
        team->longSpinPass == 0 &&
        team->leftSpin == 0 &&
        team->rightSpin == 0)
        goto no_pass_target;

    /* Check ball is still moving */
    if (_ballSprite.speed == 0)
        goto no_pass_target;

    /* Check if player is facing approximately same direction as ball */
    {
        int16_t ballDir   = _ballSprite.fullDirection;
        /* Low byte of fullDirection (big-endian low byte = value & 0xFF) */
        int8_t  playerDir = (int8_t)(player->fullDirection & 0xFF);
        int8_t  diff      = (int8_t)(ballDir - playerDir);

        if (diff > 7 || diff < -7)
            goto no_pass_target;

        if (diff > 5 || diff < -5)
            player->speed = 512;
        else
            player->speed = 256;
    }

no_pass_target:
    /* Game state speed adjustments */
    if (_gameStatePl == ST_GAME_IN_PROGRESS)
        goto calculate_acceleration;

    if (_gameState == ST_FIRST_HALF_ENDED || _gameState == ST_GAME_ENDED)
        goto reduce_speed;

    if (_gameState == ST_GOING_TO_HALFTIME || _gameState == ST_PLAYERS_GOING_TO_SHOWER)
        goto adjust_speed_for_stoppage;

    goto calculate_acceleration;

adjust_speed_for_stoppage:
    {
        int16_t  factor   = _stoppageTimerTotal * 4;
        if (factor > 100)
            factor = 100;
        {
            uint16_t curSpeed = (uint16_t)player->speed;
            uint32_t scaled   = (uint32_t)curSpeed * (uint16_t)factor;
            player->speed = (int16_t)(scaled / 100);
        }
    }
    goto calculate_acceleration;

reduce_speed:
    {
        int16_t reduction = _stoppageTimerTotal * 32;
        int16_t curSpeed  = player->speed - reduction;
        if (curSpeed < 0)
            curSpeed = 0;
        player->speed = curSpeed;
    }

calculate_acceleration:
    /* Frame delay = (1280 - speed) / 128 + 6, clamped to min 6 */
    {
        int16_t d0 = 1280 - player->speed;
        if (d0 < 0)
            d0 = 0;
        d0 >>= 7;
        d0 += 6;
        player->frameDelay = d0;
    }
}

/* ================================================================
 *  UpdatePlayerShotChanceTable
 *  Sets team's shot chance table based on whether the controlled
 *  player is a goalkeeper (skill-indexed) or outfield player.
 * ================================================================ */
void __attribute__((used, externally_visible))
UpdatePlayerShotChanceTable(Sprite *player, TeamGeneralInfo *team)
{
    PlayerInfo *pi = GetPlayerPointerFromShirtNumber(player, team);

    if (pi->position != 0) {
        /* Not a goalkeeper -- use default outfield table */
        team->shotChanceTable = (GoalieSkill *)&_teamPlOfs24Table;
        return;
    }

    /* Goalkeeper: index by goalie skill, MASKED to the low 3 bits (0-7) — the original
     * engine masks here. match_sim.c's gkSkill `> 7 -> 7` clamp only fires for certain
     * teamControls combos (needClamp), so a friendly's unclamped skill (8-16 from a high
     * price) would otherwise index past the 8-entry kGoalieSkillTables and store a wild
     * pointer -> SIGSEGV later in UpdatePlayers (gs->field_3A). */
    {
        uint16_t skill = (uint16_t)(uint8_t)pi->goalieSkill;
        team->shotChanceTable = _kGoalieSkillTables[skill & 7];
    }
}
