/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "dos_mode.h"       /* _gDosGameplay: the diving-keeper ball straddle */
#include "ball_physics.h"
#include "dos_sound.h"
#include "math68k.h"       /* CalculateDeltaXAndY: uint16_t, 0xFFFF = no movement */

/* Sprite image constants */
#define SPR_BALL_SHADOW             1069

/* ---- External globals (asm data) ---- */
extern Sprite _ballSprite;               /* 110-byte sprite */
extern Sprite _ballShadowSprite;         /* 110-byte sprite */
extern TeamGeneralInfo _topTeamData;     /* TeamGeneralInfo */
extern TeamGeneralInfo _bottomTeamData;  /* TeamGeneralInfo */
extern Sprite _goalie1Sprite;            /* 110-byte sprite */
extern Sprite _goalie2Sprite;            /* 110-byte sprite */

/* Storage is uint16_t [N] in swos_amiga_data_imports.c, but the
 * frame-table walker reads the entries as int16_t (the -1 terminator
 * needs sign semantics). Re-declared here with the matching type so
 * `(int16_t *)ballMovingFrameIndices` decays the array to a pointer
 * correctly rather than going through `&scalar` indirection. */
extern int16_t _ballStaticFrameIndices[];
extern int16_t _ballMovingFrameIndices[];

extern int16_t  _kBallGroundConstant;
extern int16_t  _kBallAirConstant;
extern int32_t  _kGravityConstant;
extern uint16_t _ballSpeedBounceFactor;
extern uint16_t _ballBounceFactor;
extern int16_t  _pitchBallSpeedFactor;
extern uint16_t _writeOnlyVar04;

extern int16_t  _goalkeeperDiveBallHeights[];  /* 28 entries */
extern int16_t  _kGoalKeeperClaimingBallHeight[];  /* 7 entries */

extern uint16_t _hideBall;
extern uint16_t _gameState;
extern uint16_t _gameStatePl;
extern uint16_t _substituteInProgress;
extern uintptr_t _teamThatSubstitutes;     /* TeamGeneralInfo*-shaped pointer; retyped uintptr_t for 64-bit. */
extern TeamGeneralInfo *_lastTeamPlayedBeforeBreak; /* pointer to team */

extern int16_t  _foulXCoordinate;
extern int16_t  _foulYCoordinate;

extern int16_t  _ballNextX;
extern int16_t  _ballNextY;

extern uint16_t _currentGameTick;

extern uint16_t _injuriesForever;

extern uint16_t _panicReasonCode;

/* quadrant data labels (in SWOS2.s code section) */
extern int16_t  _ballXQuadrantLimits[];   /* 5 entries + 1 pad */
extern int16_t  _ballYQuadrantLimits[];   /* 6 entries + 1 pad */
extern int16_t  _ballQuadrantIndex;
extern int16_t  _ballXQuadrantDead;
extern int16_t  _ballYQuadrantDead;
extern int16_t  _playerXQuadrantOffset;
extern int16_t  _playerYQuadrantOffset;

/* player offset tables (in SWOS2.s code section) */
extern int16_t  _kBallPlOffsets[];        /* 16 entries */
extern int16_t  _kPlayerWithBallOffsets[]; /* 16 entries */

/* ingameTeamPlayerOffsets (in SWOS2.s data section) */
extern uint16_t _ingameTeamPlayerOffsets[];

/* ---- Ball prediction globals ---- */
extern int16_t  _ballDefensiveX;
extern int16_t  _ballDefensiveY;
extern int16_t  _ballDefensiveZ;
extern int16_t  _ballNotHighX;
extern int16_t  _ballNotHighY;
extern int16_t  _ballNotHighZ;
extern int16_t  _strikeDestX;
extern int16_t  _strikeDestY;
extern int16_t  _strikeDestZ;

extern int16_t  _ballNextGroundX;
extern int16_t  _ballNextYGroundY;
extern int16_t  _ballNextZDeadVar;

/* ---- Spin/aftertouch tables ---- */
extern int16_t  _kKickSpinFactor[];         /* 8 dirs × 2 entries (left/right) × 2 (dx,dy) = 32 words.
                                             * ASM data uses dc.b but reads with move.w — byte pairs
                                             * form big-endian int16_t values. int16_t[] is correct. */
extern int16_t  _kPassingSpinFactor[];      /* 8 dirs × 4 entries = 32 words */
extern int16_t  _kSpinMultiplierFactor[];   /* 10 entries */
extern int32_t  _kHighKickDeltaZ;
extern int16_t  _kHighKickBallSpeed;
extern int32_t  _kNormalKickDeltaZ;
extern int16_t  _kNormalKickBallSpeed;

/* ---- Extern function declarations ---- */
extern void CheckIfBallOutOfPlay(void);
extern void PlaySample3IfNotTraining(void);
extern void PlaySfxSample16(void);
extern void PanicHaltLoop(void);

/* ---- Static helpers ---- */

static void reverseDestXDirection(Sprite *spr, int32_t savedX)
{
    int16_t intX = (int16_t)(savedX >> 16);
    int16_t destX = spr->destX;
    int16_t delta = destX - intX;
    spr->destX = intX - delta;
}

static void reverseDestYDirection(Sprite *spr, int32_t savedY)
{
    int16_t intY = (int16_t)(savedY >> 16);
    int16_t destY = spr->destY;
    int16_t delta = destY - intY;
    spr->destY = intY - delta;
}


/* ==================================================================
 * ResetBothTeamSpinTimers
 * ================================================================== */
void __attribute__((used, externally_visible))
ResetBothTeamSpinTimers(void)
{
    _topTeamData.spinTimer = (int16_t)0xFFFF;
    _bottomTeamData.spinTimer = (int16_t)0xFFFF;
}


/* ==================================================================
 * GetPlayerPointerFromShirtNumber
 *   in: playerSprite (A1), teamData (A6)
 *   out: PlayerInfo pointer for this player
 *
 * ASM returns a4 = teamGamePtr + shirtIdx*61 (42 bytes before PlayerInfo).
 * C version returns the PlayerInfo directly using TeamGame struct access.
 * ================================================================== */
PlayerInfo * __attribute__((used, externally_visible))
GetPlayerPointerFromShirtNumber(Sprite *playerSprite, TeamGeneralInfo *teamData)
{
    uint16_t ord = (uint16_t)playerSprite->playerOrdinal;
    uint16_t idx = ord - 1;
    TeamGame *tg = (TeamGame *)teamData->inGameTeamPtr;
    return &tg->players[idx];
}


/* ==================================================================
 * UpdateControllingPlayer
 *   in: playerSprite (A1) - player that controls the ball
 *
 * Set ball coordinates based on controlling player direction.
 * Ball is placed 1 pixel in front of the player.
 * ================================================================== */
void __attribute__((used, externally_visible))
UpdateControllingPlayer(Sprite *playerSprite)
{
    int16_t dir = playerSprite->direction;
    int16_t idx = dir * 2; /* pairs of (x,y) */
    int16_t d1 = playerSprite->x.s.i + _kBallPlOffsets[idx];
    int16_t d2 = playerSprite->y.s.i + _kBallPlOffsets[idx + 1];

    Sprite *ball = &_ballSprite;
    ball->speed = (int16_t)0;
    ball->x.s.i = d1;
    ball->y.s.i = d2;
    ball->destX = d1;
    ball->destY = d2;
    ball->z.s.i = (int16_t)0;

    /* halve deltaZ */
    int32_t dz = ball->deltaZ.raw;
    dz >>= 1;
    ball->deltaZ.raw = (int32_t)dz;

    ResetBothTeamSpinTimers();
}


/* ==================================================================
 * UpdateBallWithControllingGoalkeeper
 *   in: playerSprite (A1) - goalkeeper holding the ball
 *
 * Same as UpdateControllingPlayer but deltaZ is forced negative
 * (bring the ball down).
 * ================================================================== */
void __attribute__((used, externally_visible))
UpdateBallWithControllingGoalkeeper(Sprite *playerSprite)
{
    int16_t dir = playerSprite->direction;
    int16_t idx = dir * 2;
    int16_t d1 = playerSprite->x.s.i + _kBallPlOffsets[idx];
    int16_t d2 = playerSprite->y.s.i + _kBallPlOffsets[idx + 1];

    Sprite *ball = &_ballSprite;
    ball->speed = (int16_t)0;
    ball->x.s.i = d1;
    ball->y.s.i = d2;
    ball->destX = d1;
    ball->destY = d2;

    /* halve deltaZ, force negative (bring ball down) */
    int32_t dz = ball->deltaZ.raw;
    dz >>= 1;
    if (dz >= 0)
        dz = -dz;
    ball->deltaZ.raw = (int32_t)dz;

    ResetBothTeamSpinTimers();
}


/* ==================================================================
 * UpdatePlayerWithBall
 *   in: playerSprite (A1) - player sprite
 *
 * Offset player position relative to ball by direction-based table.
 * ================================================================== */
void __attribute__((used, externally_visible))
UpdatePlayerWithBall(Sprite *playerSprite)
{
    int16_t d1 = _ballSprite.x.s.i;
    int16_t d2 = _ballSprite.y.s.i;

    int16_t dir = playerSprite->direction;
    int16_t idx = dir * 2;
    d1 += _kPlayerWithBallOffsets[idx];
    d2 += _kPlayerWithBallOffsets[idx + 1];

    playerSprite->x.s.i = d1;
    playerSprite->y.s.i = d2;
    playerSprite->destX = d1;
    playerSprite->destY = d2;

    ResetBothTeamSpinTimers();
}


/* ==================================================================
 * SetThrowInPlayerDestinationCoordinates
 *   in: playerSprite (A1)
 *
 * Position throw-in player 3 pixels offset from ball.
 * ================================================================== */
void __attribute__((used, externally_visible))
SetThrowInPlayerDestinationCoordinates(Sprite *playerSprite)
{
    int16_t d1 = _ballSprite.x.s.i;
    if ((uint16_t)d1 < 336)
        d1 -= 3;
    else
        d1 += 3;

    playerSprite->x.s.i = d1;
    playerSprite->destX = d1;

    int16_t by = _ballSprite.y.s.i;
    playerSprite->y.s.i = by;
    playerSprite->destY = by;
}


/* ==================================================================
 * InjuryUpdateTickGuard
 *
 * If injuriesForever is set, skip the actual injury update (the asm
 * code after this function is dead). This is just a guard stub.
 * ================================================================== */
void __attribute__((used, externally_visible))
InjuryUpdateTickGuard(void)
{
    /* Original: tst.w (injuriesForever); beq.w *+4; rts
       When injuriesForever == 0, falls through to injury update code.
       When non-zero, returns immediately (skips injury update).
       Since the injury update code following this is still in asm,
       this C function is the complete guard; the asm stub will handle
       the fall-through case. */
    (void)_injuriesForever; /* nothing to do in C — asm stub handles flow */
}


/* ==================================================================
 * CalculateNextBallPosition
 *
 * Predicts where the ball will land (Z becomes negative) by
 * integrating position with scaled deltas. Stores result in
 * ballNextX / ballNextY.
 * ================================================================== */
void __attribute__((used, externally_visible))
CalculateNextBallPosition(void)
{
    Sprite *ball = &_ballSprite;
    int32_t d5 = ball->x.raw;
    int32_t d6 = ball->y.raw;
    int32_t d7 = ball->z.raw;

    if ((uint16_t)ball->speed == 0)
        goto store;

    {
        int32_t d1 = ball->deltaX.raw;
        int32_t d2 = ball->deltaY.raw;
        int32_t d3 = ball->deltaZ.raw;
        int32_t d4 = _kGravityConstant;

        if (d3 > 0)
            goto scale8;

        {
            int16_t zInt = ball->z.s.i;
            if ((uint16_t)zInt <= 20)
                goto scale1;
            if ((uint16_t)zInt <= 30)
                goto scale2;
            if ((uint16_t)zInt <= 35)
                goto scale4;
        }

    scale8:
        d1 = M68kShiftLeft32(d1, 3);
        d2 = M68kShiftLeft32(d2, 3);
        d4 = M68kShiftLeft32(d4, 3);
        d7 >>= 3;
        do {
            d5 += d1;
            d6 += d2;
            d3 -= d4;
            d7 += d3;
        } while (d7 >= 0);
        goto store;

    scale4:
        d1 = M68kShiftLeft32(d1, 2);
        d2 = M68kShiftLeft32(d2, 2);
        d4 = M68kShiftLeft32(d4, 2);
        d7 >>= 2;
        do {
            d5 += d1;
            d6 += d2;
            d3 -= d4;
            d7 += d3;
        } while (d7 >= 0);
        goto store;

    scale2:
        d1 = M68kShiftLeft32(d1, 1);
        d2 = M68kShiftLeft32(d2, 1);
        d4 = M68kShiftLeft32(d4, 1);
        d7 >>= 1;
        do {
            d5 += d1;
            d6 += d2;
            d3 -= d4;
            d7 += d3;
        } while (d7 >= 0);
        goto store;

    scale1:
        do {
            d5 += d1;
            d6 += d2;
            d3 -= d4;
            d7 += d3;
        } while (d7 >= 0);
    }

store:
    /* swap words to get integer part */
    _ballNextX = (int16_t)((uint32_t)d5 >> 16);
    _ballNextY = (int16_t)((uint32_t)d6 >> 16);
}


/* ==================================================================
 * DoGoalkeeperSprites
 *
 * When a goalkeeper is diving, position the ball relative to the
 * goalkeeper sprite using lookup tables for ball height.
 * ================================================================== */

/* ==================================================================
 * placeBallOnDivingKeeper — the ball rides the diving keeper's sprite.
 *
 * The two releases do NOT agree here, and the reason is the endianness of
 * the 16.16 layout rather than anything anyone decided:
 *
 *   Amiga  SWOS2.s:  move.l SPRITE_X(a5),(ballSprite+SPRITE_X).l
 *   DOS    swos.asm:110889/110934:  mov eax,[esi+(Sprite.x+2)]
 *                                   mov ballSprite.x+2, eax
 *
 * Both are one 32-bit move of "the x at its integer word". On the m68k the
 * integer word is the START of the field, so the long carries x's integer AND
 * x's fraction. On x86 it is the SECOND word, so the same long carries x's
 * integer and then runs straight on into **y's fraction**. Same instruction,
 * two different pairs of fields.
 *
 * So this is a conversion artefact, not a design difference, and the Amiga
 * side is the one that reads as intended — which is why the Amiga path here is
 * the plain `.raw` copy and the DOS path spells the straddle out. Measured:
 * playa-10144556 frame 1042, where the keeper claims it and the recording's
 * ball keeps its own x fraction and takes the keeper's y fraction.
 * ================================================================== */
static void placeBallOnDivingKeeper(Sprite *gkSprite, int16_t yOff)
{
    if (_gDosKeepers) {         /* the DOS keeper: DOS, and XBOX (dos_mode.h) */
        _ballSprite.x.s.i    = gkSprite->x.s.i;
        _ballSprite.y.s.frac = gkSprite->y.s.frac;
    } else {
        _ballSprite.x.raw = gkSprite->x.raw;
    }
    _ballSprite.y.s.i = (int16_t)(gkSprite->y.s.i + yOff);
    _ballSprite.z.s.i = (int16_t)0;
    _ballSprite.deltaZ.raw = (int32_t)0;
    _ballSprite.speed = (int16_t)0;
}

void __attribute__((used, externally_visible))
DoGoalkeeperSprites(void)
{
    TeamGeneralInfo *team;
    int16_t yOff;
    Sprite *gkSprite;
    int16_t baseImgIdx;

    /* Check top team diving right */
    team = &_topTeamData;
    if (team->goalkeeperDivingRight != 0) {
        yOff = 1;
        goto diving_right;
    }
    if (team->goalkeeperDivingLeft != 0)
        goto goalie1_diving_left;

    /* Check bottom team */
    team = &_bottomTeamData;
    if (team->goalkeeperDivingRight != 0) {
        yOff = -1;
        goto diving_right;
    }
    if (team->goalkeeperDivingLeft != 0)
        goto goalie2_diving_left;

    return; /* no goalkeeper diving */

diving_right:
    /* Determine which goalie sprite and base image index */
    gkSprite = &_goalie1Sprite;
    baseImgIdx = 0x359;
    {
        int16_t img = gkSprite->imageIndex;
        if (img >= 0x393 && img < 0x3AF)
            baseImgIdx = 0x393;
    }
    if (team->teamNumber != 1) {
        gkSprite = &_goalie2Sprite;
        baseImgIdx = 0x3CD;
        {
            int16_t img = gkSprite->imageIndex;
            if (img >= 0x407 && img < 0x423)
                baseImgIdx = 0x407;
        }
    }
    goto position_ball_diving;

goalie1_diving_left:
    yOff = 1;
    goto diving_left_common;
goalie2_diving_left:
    yOff = -1;
diving_left_common:
    gkSprite = &_goalie1Sprite;
    baseImgIdx = 0x359;
    if (team->teamNumber != 1) {
        gkSprite = &_goalie2Sprite;
        baseImgIdx = 0x3CD;
    }
    {
        placeBallOnDivingKeeper(gkSprite, yOff);

        /* Look up ball height from claiming table using frameSwitchCounter */
        int16_t fsc = gkSprite->frameSwitchCounter;
        int16_t ballZ = _kGoalKeeperClaimingBallHeight[fsc];
        _ballSprite.z.s.i = ballZ;
    }
    return;

position_ball_diving:
    {
        placeBallOnDivingKeeper(gkSprite, yOff);

        /* Compute frame offset from base */
        int16_t frameOff = gkSprite->imageIndex - baseImgIdx;
        if ((uint16_t)frameOff >= 28) { /* 28 entries in goalkeeperDiveBallHeights */
            /* frame offset out of range — panic */
            _panicReasonCode = 0x237;
            PanicHaltLoop();
        }

        /* Look up ball height from dive table */
        int16_t ballZ = _goalkeeperDiveBallHeights[frameOff];
        _ballSprite.z.s.i = ballZ;
    }
}


/* ==================================================================
 * UpdateBall
 *
 * Core ball physics: animation, movement, bouncing, goal collisions,
 * shadow update, quadrant calculation for AI tactics.
 *
 * RE bug fix: line 9517 in SWOS2.s has `move.l #(frontbufferBaseAlt+$1CAA),d3`
 * which is an IDA address-as-constant bug. The correct value is 0x00010002
 * (positive counterpart of 0xFFFEFFFE used on the Z>5 branch).
 * ================================================================== */
void __attribute__((used, externally_visible))
UpdateBall(void)
{
    Sprite *ball = &_ballSprite;

    /* ---- Hide ball if requested ---- */
    if (_hideBall) {
        ball->imageIndex = (int16_t)-1;
        _ballShadowSprite.imageIndex = (int16_t)-1;
        goto has_picture;
    }

    /* ---- Show shadow ---- */
    _ballShadowSprite.imageIndex = SPR_BALL_SHADOW;

    /* ---- Select frame table (static vs moving) ---- */
    if (ball->deltaX.raw != 0 || ball->deltaY.raw != 0) {
        /* ball is moving */
        if (ball->frameIndicesTable != _ballMovingFrameIndices) {
            ball->frameIndicesTable = _ballMovingFrameIndices;
            ball->frameIndex = (int16_t)0;
        }
    } else {
        /* ball is stationary */
        if (ball->frameIndicesTable != _ballStaticFrameIndices) {
            ball->frameIndicesTable = _ballStaticFrameIndices;
            ball->frameIndex = (int16_t)0;
        }
    }

    /* ---- Advance ball animation frame ---- */
    {
        uint16_t speed = (uint16_t)ball->speed;
        if (speed != 0) {
            uint16_t decr = (speed >> 9) + 1; /* speed/512 + 1 */
            int16_t timer = ball->cycleFramesTimer - (int16_t)decr;
            ball->cycleFramesTimer = timer;
            if (timer < 0) {
                ball->frameIndex = ball->frameIndex + 1;
                ball->cycleFramesTimer = ball->frameDelay;
                goto extract_frame;
            }
        }
        /* stopped or timer not expired */
        if (ball->imageIndex >= 0)
            goto has_picture;
    }

extract_frame:
    {
        int16_t *table = ball->frameIndicesTable;
        int16_t fi = ball->frameIndex;
        int16_t frame = table[fi];
        if (frame >= 0) {
            ball->imageIndex = frame;
        } else {
            /* negative = reset loop */
            ball->frameIndex = (int16_t)0;
            frame = table[0];
            ball->imageIndex = frame;
        }
    }

has_picture:
    ;
    /* Save positions before movement for bounce recovery */
    int32_t savedX = ball->x.raw;
    int32_t savedY = ball->y.raw;
    int32_t savedZ = ball->z.raw;

    /* ---- Calculate movement direction ---- */
    {
        int16_t destX = ball->destX;
        int16_t destY = ball->destY;
        int16_t srcX  = ball->x.s.i;
        int16_t srcY  = ball->y.s.i;
        int16_t speed = ball->speed;

        int32_t outDX, outDY;
        /* The result is a direction 0..255, or 0xFFFF meaning "not moving"
         * (math68k.h). The branch below depends on that sentinel reading as
         * NEGATIVE, so the unsigned return is narrowed to int16_t here, once,
         * in the open.
         *
         * This file used to declare the function as returning int16_t, which it
         * does not. On x86_64 the caller happened to sign-extend and behaved; on
         * arm64 (macOS) it saw 65535, took the >= 0 branch and wrote the ball's
         * fullDirection on every frame the ball was still. Two peers on the two
         * platforms desynced on frame 1 of the first netplay match between them. */
        uint16_t rawDir = CalculateDeltaXAndY(destX, destY, srcX, srcY, speed, &outDX, &outDY);
        int16_t dir = (int16_t)rawDir;

        ball->deltaX.raw = (int32_t)outDX;
        ball->deltaY.raw = (int32_t)outDY;

        if (dir >= 0) {
            ball->fullDirection = dir;
            dir = (int16_t)(((uint16_t)(dir + 16) & 0xFF) >> 5);
        }
        ball->direction = dir;
    }

    /* ---- Apply friction ---- */
    {
        int16_t friction = _kBallGroundConstant;
        if (_topTeamData.playerHasBall == 0 &&
            _bottomTeamData.playerHasBall == 0) {
            friction += _pitchBallSpeedFactor;
        }
        if (ball->z.s.i != 0) {
            friction = _kBallAirConstant;
        }
        int16_t spd = ball->speed - friction;
        if (spd < 0) spd = 0;
        ball->speed = spd;
    }

    /* ---- Apply deltas to position ---- */
    {
        int32_t dx = ball->deltaX.raw;
        int32_t dy = ball->deltaY.raw;
        int32_t dz = ball->deltaZ.raw;

        ball->x.raw = ball->x.raw + dx;
        ball->y.raw = ball->y.raw + dy;

        /* ---- Keeper holds ball: special Z handling ---- */
        if (_gameState == ST_KEEPER_HOLDS_BALL) {
            if (_substituteInProgress) {
                TeamGeneralInfo *subTeam = (TeamGeneralInfo *)_teamThatSubstitutes;
                if (subTeam == _lastTeamPlayedBeforeBreak) {
                    Sprite *ctrlPl = subTeam->controlledPlayer;
                    if (ctrlPl == 0) {
                        ball->z.s.i = (int16_t)0;
                        goto clear_deltaz;
                    }
                }
            }

            /* Keeper holds ball: move ball with player */
            if ((uint16_t)ball->speed != 0) {
                _writeOnlyVar04++;
                Sprite *ctrlPlayer = _lastTeamPlayedBeforeBreak->controlledPlayer;
                UpdateBallWithControllingGoalkeeper(ctrlPlayer);
            }

            {
                int16_t bz = ball->z.s.i;
                if (bz == 5) goto clear_deltaz;
                if ((uint16_t)bz < 6) {
                    dz = 0x00020004;
                } else {
                    dz = (int32_t)0xFFFEFFFE;
                }
            }
            goto apply_z;
        }

        /* ---- Normal ball: apply gravity ---- */
        if (dz == 0)
            goto after_z;

        dz -= _kGravityConstant;
        dz |= 1;

    apply_z:
        ball->z.raw = ball->z.raw + dz;
        if (ball->z.raw >= 0)
            goto after_z;

        /* Ball hit the ground — bounce */
        {
            uint16_t spd = (uint16_t)ball->speed;
            uint32_t decr = ((uint32_t)spd * (uint32_t)_ballSpeedBounceFactor) >> 8;
            ball->speed = (int16_t)(spd - (uint16_t)decr);
        }
        ball->z.s.i = (int16_t)0;

        /* Reverse and dampen deltaZ */
        dz = -dz;
        {
            int32_t frac = dz >> 8;
            uint32_t dampen = (uint32_t)(uint16_t)frac * (uint32_t)_ballBounceFactor;
            dz -= (int32_t)dampen;
            dz |= 1;
        }

        if ((uint32_t)dz <= 0xA000) {
            goto clear_deltaz;
        }

        /* Play bounce sound (sample 0x10, not crowd "oooh" 0x2C) */
        PlaySfxSample16();
        goto after_z;

    clear_deltaz:
        dz = 0;

    after_z:
        ball->deltaZ.raw = (int32_t)dz;
    }

    /* ---- Out-of-play boundary checks when game not in progress ---- */
    if (_gameStatePl != ST_GAME_IN_PROGRESS) {
        /* X boundary */
        int16_t bx = ball->x.s.i;
        if (bx < 53 || bx > 618) {
            int16_t destX = ball->destX;
            int16_t delta = destX - bx;
            ball->destX = bx - delta;
            uint16_t spd = (uint16_t)ball->speed;
            ball->speed = (int16_t)(spd >> 1);
            ball->x.raw = savedX;
            ball->y.raw = savedY;
            ball->z.raw = savedZ;
        }

        /* Y boundary */
        int16_t by = ball->y.s.i;
        if (by < 100 || by > 799) {
            int16_t destY = ball->destY;
            int16_t delta = destY - by;
            ball->destY = by - delta;
            uint16_t spd = (uint16_t)ball->speed;
            ball->speed = (int16_t)(spd >> 1);
            ball->x.raw = savedX;
            ball->y.raw = savedY;
            ball->z.raw = savedZ;
        }
    }

    /* ---- Goal area collision detection ---- */
    {
        int16_t bx = ball->x.s.i - 1;
        int16_t by = ball->y.s.i;
        int16_t bz = ball->z.s.i;

        if (by >= 129 && by <= 769)
            goto skip_goal_collision;

        /* Upper goal area (Y <= 128) */
        if (by <= 128) {
            if (by <= 112)
                goto check_upper_goal_inner;
            /* Y in 113..128 */
            if (bz > 19) goto skip_goal_collision;
            if (bx <= 295 || bx > 372) goto skip_goal_collision;
            if (by > 123) {
                if (bz > 15) goto upper_crossbar;
            } else {
                /* Y in 113..123 */
                if (bz > 10) goto upper_crossbar;
            }
            /* Ball inside upper goal */
            if (bx < 302 || bx > 366) goto side_post_bounce;
            if (by < 119) goto back_net_bounce;
            goto skip_goal_collision;

        check_upper_goal_inner:
            goto skip_goal_collision; /* Y < 112, outside goal */
        }

        /* Lower goal area (Y >= 770) */
        if (by < 770) goto skip_goal_collision;
        if (by >= 785) goto skip_goal_collision;
        /* Y in 770..784 */
        if (bz > 19) goto skip_goal_collision;
        if (bx <= 295 || bx > 372) goto skip_goal_collision;
        if (bz > 15) goto lower_crossbar;
        if (bx < 302 || bx > 366) goto side_post_bounce;
        if (by > 778) goto back_net_bounce;
        goto skip_goal_collision;

    upper_crossbar:
        {
            /* Check if ball is going up (deltaZ fractional part) */
            int32_t dzFull = ball->z.raw; /* full 32-bit Z */
            int16_t dzHigh = (int16_t)(dzFull >> 16);
            if (dzHigh <= 15) {
                /* Slow stop on crossbar */
                ball->deltaZ.raw = (int32_t)1;
                ball->speed = (int16_t)0;
                ball->x.raw = savedX;
                ball->y.raw = savedY;
                ball->z.raw = savedZ;
                goto skip_goal_collision;
            }
            /* Fast deflection off crossbar */
            ball->deltaZ.raw = (int32_t)1;
            {
                int16_t newDestY = ball->y.s.i - 1000;
                ball->destY = newDestY;
            }
            ball->speed = (int16_t)512;
            ball->z.raw = savedZ;
            goto skip_goal_collision;
        }

    lower_crossbar:
        {
            int32_t dzFull = ball->z.raw;
            int16_t dzHigh = (int16_t)(dzFull >> 16);
            if (dzHigh <= 15) {
                ball->deltaZ.raw = (int32_t)1;
                ball->speed = (int16_t)0;
                ball->x.raw = savedX;
                ball->y.raw = savedY;
                ball->z.raw = savedZ;
                goto skip_goal_collision;
            }
            ball->deltaZ.raw = (int32_t)1;
            {
                int16_t newDestY = ball->y.s.i + 1000;
                ball->destY = newDestY;
            }
            ball->speed = (int16_t)512;
            ball->z.raw = savedZ;
            goto skip_goal_collision;
        }

    back_net_bounce:
        reverseDestYDirection(ball, savedY);
        ResetBothTeamSpinTimers();
        {
            uint16_t spd = (uint16_t)ball->speed;
            ball->speed = (int16_t)(spd >> 3);
        }
        ball->x.raw = savedX;
        ball->y.raw = savedY;
        goto skip_goal_collision;

    side_post_bounce:
        reverseDestXDirection(ball, savedX);
        ResetBothTeamSpinTimers();
        {
            uint16_t spd = (uint16_t)ball->speed;
            ball->speed = (int16_t)(spd >> 2);
        }
        ball->x.raw = savedX;
        ball->y.raw = savedY;
    }

skip_goal_collision:
    /* ---- Goalpost deflection (game in progress) ---- */
    if (_gameStatePl == ST_GAME_IN_PROGRESS) {
        int16_t bx = ball->x.s.i - 1;
        int16_t by = ball->y.s.i;
        int16_t bz = ball->z.s.i;
        int16_t d4;
        int16_t dosPostSide = 0;    /* the PC game's commentary: the side of a post, not the bar */
        int32_t deltaY;

        /* Near a goal line: Y 129..132 or 766..769 (SWOS2.s:14807). */
        if (by > 128 && by <= 132)
            goto goalline_check;
        if (by >= 766 && by < 770)
            goto goalline_check;
        goto after_goalpost;

    goalline_check:
        if (bz > 19) goto after_goalpost;
        if (bx <= 295 || bx > 372) goto after_goalpost;

        /* Three arms, and the port used to have only two of them. `d4` is the
         * same expression in each — a 16-unit step off the game tick — but
         * WHERE it is added is what separates them (SWOS2.s:14829-14869, and
         * swos.asm:110374-110438 the same):
         *
         *   above post level (z > 15), weak Y  -> loc_109878, the crossbar:
         *       bounce Z, restore it, and shift the ball a pixel down the
         *       pitch. Nothing is added to a destination.
         *   at post level (z <= 15) and outside the goal mouth, weak Y
         *                                    -> loc_109810's fall-through:
         *       reverse the Y destination and add d4 to DESTY.
         *   either, strong Y                 -> loc_10984C:
         *       reverse the Y destination and add d4 to DESTX.
         *
         * The middle arm was missing: a weak Y at post level fell into the
         * crossbar, so a ball resting against a post drifted a pixel down the
         * pitch every frame instead of rattling along it. zkz-12080203 f11493
         * is one frame of that — recorded destY -390 and y unmoved against
         * our destY 478 and y one pixel further on. */
        if (bz > 15)
            goto above_post_level;
        if (bx >= 302 && bx <= 366)
            goto after_goalpost;   /* inside the goal mouth: no post to hit */

        /* loc_109810 — the side of a post */
        dosPostSide = 1;
        d4 = (int16_t)(_currentGameTick & 0x1F);
        d4 = (int16_t)((d4 << 4) - 0x100);
        deltaY = ball->deltaY.raw;
        if (deltaY < (int32_t)0xFFFFB000 || deltaY > 0x5000)
            goto strong_y_deflection;
        reverseDestYDirection(ball, savedY);
        ResetBothTeamSpinTimers();
        ball->destY = (int16_t)(ball->destY + d4);
        goto goalpost_speed_reduce;

    above_post_level:
        /* loc_1097E6 — level with the crossbar */
        dosPostSide = 0;
        d4 = (int16_t)(_currentGameTick & 0x1F);
        d4 = (int16_t)((d4 << 4) - 0x100);
        deltaY = ball->deltaY.raw;
        if (deltaY < (int32_t)0xFFFFB000 || deltaY > 0x5000)
            goto strong_y_deflection;
        goto crossbar_bounce;

    strong_y_deflection:
        /* loc_10984C — shared by both, and it only fires when the ball is
         * travelling INTO the goal line it is nearest. */
        if (by <= 449) {
            if (deltaY >= 0)
                goto after_goalpost;
        } else {
            if (deltaY < 0)
                goto after_goalpost;
        }
        reverseDestYDirection(ball, savedY);
        ResetBothTeamSpinTimers();
        ball->destX = (int16_t)(ball->destX + d4);
        goto goalpost_speed_reduce;

    crossbar_bounce:
        /* loc_109878 */
        ball->deltaZ.raw = -ball->deltaZ.raw;
        ball->z.raw = savedZ;
        savedY += 0x10000;

    goalpost_speed_reduce:
        /* loc_109886 */
        /* The PC game: a line about the post or the bar (it keeps which in
         * goalTypeScored), then PlayMissGoalSample. */
        DOS_SOUND(DOS_EV_WOODWORK, dosPostSide);
        DOS_SOUND(DOS_EV_MISS_CROWD, 0);
        PlaySample3IfNotTraining();
        {
            uint16_t spd = (uint16_t)ball->speed;
            ball->speed = (int16_t)(spd - (spd >> 2));
        }
        ball->x.raw = savedX;
        ball->y.raw = savedY;
    }

after_goalpost:
    /* ---- Check if ball out of play (game in progress only) ---- */
    if (_gameStatePl == ST_GAME_IN_PROGRESS) {
        int16_t bx = ball->x.s.i;
        int16_t by = ball->y.s.i;
        if (bx < 81 || bx > 590 || by < 129 || by > 769) {
            CheckIfBallOutOfPlay();
        }
    }

    /* ---- Update ball shadow position ---- */
    {
        Sprite *shadow = &_ballShadowSprite;
        uint16_t halfZ = (uint16_t)ball->z.s.i >> 1;
        int16_t sx = ball->x.s.i + (int16_t)halfZ + 1;
        shadow->x.s.i = sx;
        uint16_t quarterZ = halfZ >> 1;
        int16_t sy = ball->y.s.i + (int16_t)quarterZ + 1 - 10;
        shadow->y.s.i = sy;
        shadow->z.s.i = (int16_t)-10;
    }

    /* ---- Calculate next ball position (for AI) ---- */
    CalculateNextBallPosition();

    /* ---- Calculate ball quadrant for tactics ---- */
    {
        int16_t qx, qy;

        if (_gameStatePl == ST_GAME_IN_PROGRESS ||
            _gameState == ST_FIRST_HALF_ENDED ||
            _gameState == ST_GAME_ENDED) {
            qx = _ballNextX;
            qy = _ballNextY;
        } else {
            qx = _foulXCoordinate;
            qy = _foulYCoordinate;
            if (_gameState == ST_KEEPER_HOLDS_BALL ||
                _gameState == ST_GOAL_OUT_LEFT ||
                _gameState == ST_GOAL_OUT_RIGHT) {
                qx = 336;
                qy = 449;
            }
        }

        /* X quadrant: 4 thresholds -> quadrant 0..4
           asm pattern: cmp.w (a1)+,d1; bcs branch; addq d0/d3
           After loop: sub.w -4(a1),d1 subtracts limits[i-1] (the entry before
           the unmatched threshold). For i=0 it reads pad data (=81). */
        int16_t xQuad = 0;
        int16_t d3 = 0;
        int16_t xRefLimit;
        {
            int16_t i;
            for (i = 0; i < 4; i++) {
                if ((uint16_t)qx < (uint16_t)_ballXQuadrantLimits[i])
                    break;
                xQuad++;
                d3++;
            }
            if (i == 0)
                xRefLimit = 81; /* pad_111202 data: dc.b 0,81 -> word 0x0051 */
            else if (i == 4)
                xRefLimit = _ballXQuadrantLimits[3]; /* lea 2(a1),a1 then -4(a1) */
            else
                xRefLimit = _ballXQuadrantLimits[i - 1];
        }
        _ballXQuadrantDead = xQuad;
        _playerXQuadrantOffset = (int16_t)(((int16_t)(qx - xRefLimit - 51) * 5) / 15);

        /* Y quadrant: 6 thresholds -> quadrant 0..6, d3 increments by 5 each */
        int16_t yQuad = 0;
        int16_t yRefLimit;
        {
            int16_t i;
            for (i = 0; i < 6; i++) {
                if ((uint16_t)qy < (uint16_t)_ballYQuadrantLimits[i])
                    break;
                yQuad++;
                d3 += 5;
            }
            if (i == 0)
                /* The asm's `sub.w -4(a1),d2` with a1 one past the table base
                 * reads the word BEFORE ballYQuadrantLimits — which is the last
                 * entry of ballXQuadrantLimits, $0081 = 129: not an X limit at
                 * all but this loop's missing leading bound. (The X loop's own
                 * leading bound sits in pad_111202 the same way.) The DOS build
                 * spells the layout out: `ballYQuadrantLimits dw 129, 220, 312,
                 * ...` with the loop starting at +2. This used to fall back to
                 * ballYQuadrantLimits[0] = 220, which put every outfield
                 * player's target 30 pixels too far up the pitch whenever the
                 * ball was in the topmost Y band. */
                yRefLimit = _ballXQuadrantLimits[4]; /* 0x0081 = 129 */
            else if (i == 6)
                yRefLimit = _ballYQuadrantLimits[5]; /* lea 2(a1),a1 then -4(a1) */
            else
                yRefLimit = _ballYQuadrantLimits[i - 1];
        }
        _ballYQuadrantDead = yQuad;
        _playerYQuadrantOffset = (int16_t)(((int16_t)(qy - yRefLimit - 45) * 5) / 15);
        _ballQuadrantIndex = d3;
    }
}


/* ==================================================================
 * UpdateBallVariables
 *   from: UpdateBallVariables
 *   in:  keeper (A1), ball (A2), team (A6)
 *
 * Integrates ball trajectory forward to predict where it crosses the
 * goalkeeper's Y position, then samples at fixed Y windows:
 *   Top team:    Y=keeper, Y-135, Y-129
 *   Bottom team: Y=keeper, Y+763, Y+769
 * Results stored in ballDefensiveX/Y/Z, ballNotHighX/Y/Z, strikeDestX/Y/Z.
 * For horizontal movement, predicts at keeper's X position.
 * ================================================================== */
void __attribute__((used, externally_visible))
UpdateBallVariables(Sprite *keeper, Sprite *ball, TeamGeneralInfo *team)
{
    int32_t d1 = ball->x.raw;
    int32_t d2 = ball->y.raw;
    int32_t d3 = ball->z.raw;
    int32_t d4 = ball->deltaX.raw;
    int32_t d5 = ball->deltaY.raw;
    int32_t d6 = ball->deltaZ.raw;
    int32_t d7 = _kGravityConstant;

    /* Check ball Y velocity direction */
    if (d5 < (int32_t)0xFFFF0000)
        goto ball_moving_toward_top_goal;
    if (d5 > (int32_t)0x10000)
        goto ball_moving_toward_bottom_goal;
    /* Y velocity small, check X */
    if (d4 < (int32_t)0xFFFF0000)
        goto ball_going_left;
    if (d4 > (int32_t)0x10000)
        goto ball_going_right;
    goto sample_defense_at_current_ball_pos;

ball_moving_toward_top_goal:
    if (team != &_topTeamData)
        goto sample_defense_at_current_ball_pos;
    d2 -= keeper->y.raw;
    if (d2 >= 0) {
        /* Ball is below keeper, integrate until crossing */
        do {
            d6 -= d7;
            d3 += d6;
            d1 += d4;
            d2 += d5;
        } while (d2 >= 0);
        d1 -= d4;
        d2 -= d5;
        d3 -= d6;
        d2 += keeper->y.raw;
        {
            int16_t zi = (int16_t)((uint32_t)d3 >> 16);
            _ballDefensiveX = (int16_t)((uint32_t)d1 >> 16);
            _ballDefensiveY = (int16_t)((uint32_t)d2 >> 16);
            if (zi < 0) zi = 0;
            _ballDefensiveZ = zi;
        }
    } else {
        /* Ball already past keeper Y */
        _ballDefensiveX = ball->x.s.i;
        _ballDefensiveY = ball->y.s.i;
        _ballDefensiveZ = ball->z.s.i;
        d2 += keeper->y.raw;
    }

    /* check_above_135px_window */
    d2 -= (int32_t)0x870000; /* subtract 135.0 */
    if (d2 >= 0) {
        /* integrate_to_135px_ahead */
        do {
            d6 -= d7;
            d3 += d6;
            d1 += d4;
            d2 += d5;
        } while (d2 >= 0);
        d1 -= d4;
        d3 -= d6;
        d2 = (int32_t)0x870000; /* set to 135.0 */
        {
            int16_t zi = (int16_t)((uint32_t)d3 >> 16);
            _ballNotHighX = (int16_t)((uint32_t)d1 >> 16);
            _ballNotHighY = (int16_t)((uint32_t)d2 >> 16);
            if (zi < 0) zi = 0;
            _ballNotHighZ = zi;
        }
    } else {
        /* Already within 135px window */
        _ballNotHighX = _ballDefensiveX;
        _ballNotHighY = _ballDefensiveY;
        _ballNotHighZ = _ballDefensiveZ;
        d2 += (int32_t)0x870000;
    }

    /* keeper_attempt_static_header_catch: check 129px window */
    d2 -= (int32_t)0x810000; /* subtract 129.0 */
    if (d2 >= 0) {
        /* integrate_to_129px_ahead */
        do {
            d6 -= d7;
            d3 += d6;
            d1 += d4;
            d2 += d5;
        } while (d2 >= 0);
        d1 -= d4;
        d3 -= d6;
        d2 = (int32_t)0x810000;
        {
            int16_t zi = (int16_t)((uint32_t)d3 >> 16);
            _strikeDestX = (int16_t)((uint32_t)d1 >> 16);
            _strikeDestY = (int16_t)((uint32_t)d2 >> 16);
            if (zi < 0) zi = 0;
            _strikeDestZ = zi;
        }
    } else {
        _strikeDestX = 0;
        _strikeDestY = 0;
        _strikeDestZ = 0;
    }
    return;

ball_moving_toward_bottom_goal:
    if (team != &_bottomTeamData)
        goto sample_defense_at_current_ball_pos;
    d2 -= keeper->y.raw;
    if (d2 < 0) {
        /* Ball is above keeper, integrate until crossing */
        do {
            d6 -= d7;
            d3 += d6;
            d1 += d4;
            d2 += d5;
        } while (d2 < 0);
        d1 -= d4;
        d2 -= d5;
        d3 -= d6;
        d2 += keeper->y.raw;
        {
            int16_t zi = (int16_t)((uint32_t)d3 >> 16);
            _ballDefensiveX = (int16_t)((uint32_t)d1 >> 16);
            _ballDefensiveY = (int16_t)((uint32_t)d2 >> 16);
            if (zi < 0) zi = 0;
            _ballDefensiveZ = zi;
        }
    } else {
        /* Ball already past keeper Y */
        _ballDefensiveX = ball->x.s.i;
        _ballDefensiveY = ball->y.s.i;
        _ballDefensiveZ = ball->z.s.i;
        d2 += keeper->y.raw;
    }

    /* check_plus_498px_threshold: 763.0 */
    d2 -= (int32_t)0x2FB0000; /* subtract 763.0 */
    if (d2 < 0) {
        /* integrate_to_plus_498px */
        do {
            d6 -= d7;
            d3 += d6;
            d1 += d4;
            d2 += d5;
        } while (d2 < 0);
        d1 -= d4;
        d3 -= d6;
        d2 = (int32_t)0x2FB0000; /* set to 763.0 */
        {
            int16_t zi = (int16_t)((uint32_t)d3 >> 16);
            _ballNotHighX = (int16_t)((uint32_t)d1 >> 16);
            _ballNotHighY = (int16_t)((uint32_t)d2 >> 16);
            if (zi < 0) zi = 0;
            _ballNotHighZ = zi;
        }
    } else {
        /* Already past 763px window */
        _ballNotHighX = _ballDefensiveX;
        _ballNotHighY = _ballDefensiveY;
        _ballNotHighZ = _ballDefensiveZ;
        d2 += (int32_t)0x2FB0000;
    }

    /* check_between_plus_497_plus_505px_window: 769.0 */
    d2 -= (int32_t)0x3010000; /* subtract 769.0 */
    if (d2 < 0) {
        /* integrate_to_plus_505px */
        do {
            d6 -= d7;
            d3 += d6;
            d1 += d4;
            d2 += d5;
        } while (d2 < 0);
        d1 -= d4;
        d3 -= d6;
        d2 = (int32_t)0x3010000;
        {
            int16_t zi = (int16_t)((uint32_t)d3 >> 16);
            _strikeDestX = (int16_t)((uint32_t)d1 >> 16);
            _strikeDestY = (int16_t)((uint32_t)d2 >> 16);
            if (zi < 0) zi = 0;
            _strikeDestZ = zi;
        }
    } else {
        _strikeDestX = 0;
        _strikeDestY = 0;
        _strikeDestZ = 0;
    }
    return;

ball_going_left:
    d1 -= keeper->x.raw;
    if (d1 < 0) {
        /* Ball already left of keeper, no prediction */
        d1 += keeper->x.raw;
        goto sample_defense_at_current_ball_pos;
    }
    /* Ball right of keeper, integrate left past keeper X */
    do {
        d6 -= d7;
        d3 += d6;
        d2 += d5;
        d1 += d4;
    } while (d1 >= 0);
    d1 -= d4;
    d2 -= d5;
    d3 -= d6;
    d1 += keeper->x.raw;
    {
        int16_t zi = (int16_t)((uint32_t)d3 >> 16);
        _ballDefensiveX = (int16_t)((uint32_t)d1 >> 16);
        _ballDefensiveY = (int16_t)((uint32_t)d2 >> 16);
        if (zi < 0) zi = 0;
        _ballDefensiveZ = zi;
    }
    goto finalize_not_high_and_clear_strike;

ball_going_right:
    d1 -= keeper->x.raw;
    if (d1 >= 0) {
        /* Ball already right of keeper, no prediction */
        d1 += keeper->x.raw;
        goto sample_defense_at_current_ball_pos;
    }
    /* Ball left of keeper, integrate right past keeper X */
    do {
        d6 -= d7;
        d3 += d6;
        d2 += d5;
        d1 += d4;
    } while (d1 < 0);
    d1 -= d4;
    d2 -= d5;
    d3 -= d6;
    d1 += keeper->x.raw;
    {
        int16_t zi = (int16_t)((uint32_t)d3 >> 16);
        _ballDefensiveX = (int16_t)((uint32_t)d1 >> 16);
        _ballDefensiveY = (int16_t)((uint32_t)d2 >> 16);
        if (zi < 0) zi = 0;
        _ballDefensiveZ = zi;
    }
    goto finalize_not_high_and_clear_strike;

sample_defense_at_current_ball_pos:
    _ballDefensiveX = ball->x.s.i;
    _ballDefensiveY = ball->y.s.i;
    _ballDefensiveZ = ball->z.s.i;
finalize_not_high_and_clear_strike:
    _ballNotHighX = _ballDefensiveX;
    _ballNotHighY = _ballDefensiveY;
    _ballNotHighZ = _ballDefensiveZ;
    _strikeDestX = 0;
    _strikeDestY = 0;
    _strikeDestZ = 0;
}


/* ==================================================================
 * CalculateBallNextGroundXYPositions
 *   from: CalculateBallNextGroundXYPositions
 *   in:  ball (A2)
 *
 * Predicts where the ball will land by integrating with decreasing Z
 * subtraction steps (20→13). Stores result in ballNextGroundX/Y.
 * Sets ballNextGroundX = -1 if ball is stationary.
 * ================================================================== */
void __attribute__((used, externally_visible))
CalculateBallNextGroundXYPositions(Sprite *ball)
{
    int32_t d1 = ball->x.raw;
    int32_t d2 = ball->y.raw;
    int32_t d3 = ball->z.raw;
    int32_t d4 = ball->deltaX.raw;
    int32_t d5 = ball->deltaY.raw;

    /* Check if ball has meaningful velocity */
    if (d5 < -(int32_t)0x10000 || d5 > (int32_t)0x10000 ||
        d4 < -(int32_t)0x10000 || d4 > (int32_t)0x10000) {
        /* ball_moving */
        int32_t d6 = ball->deltaZ.raw;
        int32_t d7 = _kGravityConstant;
        int32_t d0 = (int32_t)0x140000; /* 20.0 */

        /* Try decreasing Z subtraction values */
        for (;;) {
            d3 -= d0;
            if (d3 >= 0)
                break; /* z_in_range */
            if (d0 == (int32_t)0xD0000) { /* 13.0 */
                _ballNextGroundX = -1;
                return;
            }
            d3 += d0;
            d0 -= (int32_t)0x10000;
        }

        /* z_in_range: integrate until z goes negative */
        do {
            d1 += d4;
            d2 += d5;
            d6 -= d7;
            d3 += d6;
        } while (d3 >= 0);

        d3 += d0;
        _ballNextGroundX = (int16_t)((uint32_t)d1 >> 16);
        _ballNextYGroundY = (int16_t)((uint32_t)d2 >> 16);
        _ballNextZDeadVar = 0;
        return;
    }

    /* ball_standing */
    _ballNextGroundX = -1;
}


/* ==================================================================
 * ApplyBallAfterTouch
 *   from: ApplyBallAfterTouch
 *   in:  team (A6)
 *
 * Applies spin/aftertouch to the ball after a kick or pass.
 * Two main paths: kick (no passInProgress) and pass (passInProgress).
 * Spin detection: compares current direction vs controlled direction.
 * High kick detection at spinTimer == 4.
 * Timer increments to 10 then resets to 0xFFFF (disabled).
 * ================================================================== */
void __attribute__((used, externally_visible))
ApplyBallAfterTouch(TeamGeneralInfo *team)
{
    Sprite *ball;
    int16_t spinSide;

    if (team->passInProgress != 0)
        goto passing_now;

    /* ---- Kick path ---- */
    {
        TeamGeneralInfo *oppTeam = team->opponentsTeam;
        if (oppTeam->goalkeeperSavedCommentTimer < 0)
            goto kick_reset_spin;
        if (_gameStatePl != ST_GAME_IN_PROGRESS) {
            if (_gameState == ST_KEEPER_HOLDS_BALL)
                goto kick_reset_spin;
        }
    }

    {
        int16_t spinTimer = team->spinTimer;
        if (spinTimer < 0)
            return;
        if (spinTimer == 0) {
            team->leftSpin = 0;
            team->rightSpin = 0;
        }
    }

    ball = &_ballSprite;

    if (team->leftSpin != 0) {
        spinSide = 0;
        goto kick_calc_spin;
    }
    if (team->rightSpin != 0) {
        spinSide = 4;
        goto kick_calc_spin;
    }

    /* Detect spin start */
    {
        int16_t curDir = team->currentAllowedDirection;
        if (curDir < 0)
            goto kick_not_starting_spin;
        int16_t diff = team->controlledPlDirection - curDir;
        if (diff == 0)
            goto kick_not_starting_spin;
        diff &= 7;
        if (diff == 4)
            goto kick_not_starting_spin;
        if ((uint16_t)diff < 4) {
            team->leftSpin = 1;
            spinSide = 0;
        } else {
            team->rightSpin = 1;
            spinSide = 4;
        }
    }

kick_calc_spin:
    {
        int16_t dir = team->controlledPlDirection;
        int16_t tableIdx = dir * 4 + (spinSide >> 1);
        int16_t multiplier = _kSpinMultiplierFactor[team->spinTimer];
        int16_t spinDx = _kKickSpinFactor[tableIdx];
        ball->destX += (int16_t)(spinDx * multiplier);
        int16_t spinDy = _kKickSpinFactor[tableIdx + 1];
        ball->destY += (int16_t)(spinDy * multiplier);
    }

kick_not_starting_spin:
    /* Check for high kick at spinTimer == 4 */
    if (team->spinTimer != 4)
        goto kick_increase_timer;

    {
        int16_t curDir = team->currentAllowedDirection;
        if (curDir < 0)
            goto kick_normal_not_high;
        int16_t diff = team->controlledPlDirection - curDir;
        if (diff == 0)
            goto kick_increase_timer;
        diff &= 7;
        if (diff == 2 || diff == 6)
            goto kick_normal_not_high;
        if ((uint16_t)diff < 3 || (uint16_t)diff > 5)
            goto kick_increase_timer;
        /* High kick */
        ball->deltaZ.raw = _kHighKickDeltaZ;
        ball->speed = _kHighKickBallSpeed;
        goto kick_speed_adjust;
    }

kick_normal_not_high:
    ball->deltaZ.raw = _kNormalKickDeltaZ;
    ball->speed = _kNormalKickBallSpeed;

kick_speed_adjust:
    {
        int16_t ctrlDir = team->controlledPlDirection;
        if (ctrlDir == 0 || ctrlDir == 4) {
            /* Decrease by quarter: speed * 3/4 */
            int16_t spd = ball->speed;
            spd = spd - ((uint16_t)spd >> 2);
            ball->speed = spd;
        } else if (ctrlDir & 1) {
            /* Odd direction: speed * 7/8 */
            int16_t spd = ball->speed;
            int16_t quarter = (uint16_t)spd >> 2;
            int16_t result = spd - quarter;
            int16_t eighth = (uint16_t)spd >> 3;
            result += eighth;
            ball->speed = result;
        }
    }

kick_increase_timer:
    team->spinTimer++;
    if (team->spinTimer == 10)
        goto kick_reset_spin;
    return;

kick_reset_spin:
    team->spinTimer = (int16_t)0xFFFF;
    return;

    /* ---- Pass path ---- */
passing_now:
    {
        TeamGeneralInfo *oppTeam = team->opponentsTeam;
        if (oppTeam->goalkeeperSavedCommentTimer < 0)
            goto pass_reset_spin;
        if (_gameStatePl != ST_GAME_IN_PROGRESS) {
            if (_gameState == ST_KEEPER_HOLDS_BALL)
                goto pass_reset_spin;
        }
    }

    {
        int16_t spinTimer = team->spinTimer;
        if (spinTimer < 0)
            return;
        if (spinTimer == 0) {
            team->leftSpin = 0;
            team->rightSpin = 0;
            team->longPass = 0;
            team->longSpinPass = 0;
        }
    }

    ball = &_ballSprite;

    if (team->leftSpin != 0) {
        spinSide = 0;
        goto pass_calc_spin;
    }
    if (team->rightSpin != 0) {
        spinSide = 4;
        goto pass_calc_spin;
    }

    /* Detect spin start for pass */
    {
        int16_t curDir = team->currentAllowedDirection;
        if (curDir < 0)
            goto pass_no_spin;
        int16_t diff = team->controlledPlDirection - curDir;
        if (diff == 0)
            goto pass_no_spin;
        diff &= 7;
        if (diff == 4)
            goto pass_no_spin;
        if ((uint16_t)diff < 4) {
            team->leftSpin = 1;
            spinSide = 0;
        } else {
            team->rightSpin = 1;
            spinSide = 4;
        }
    }

pass_calc_spin:
    {
        int16_t ballDir = _ballSprite.direction;
        int16_t tableIdx = ballDir * 4 + (spinSide >> 1);
        int16_t multiplier = _kSpinMultiplierFactor[team->spinTimer];
        int16_t spinDx = _kPassingSpinFactor[tableIdx];
        ball->destX += (int16_t)(spinDx * multiplier);
        int16_t spinDy = _kPassingSpinFactor[tableIdx + 1];
        ball->destY += (int16_t)(spinDy * multiplier);
    }

pass_no_spin:
    /* Check for long pass */
    if (team->longPass != 0 || team->longSpinPass != 0)
        goto pass_update_timer;

    {
        int16_t curDir = team->currentAllowedDirection;
        if (curDir < 0)
            goto pass_holding_left_or_right;
        int16_t diff = team->controlledPlDirection - curDir;
        if (diff == 0)
            goto pass_update_timer;
        diff &= 7;
        if (diff == 2 || diff == 6)
            goto pass_holding_left_or_right;
        if ((uint16_t)diff < 3 || (uint16_t)diff > 5)
            goto pass_update_timer;

        /* Long spin pass */
        team->longSpinPass = 1;
        {
            int16_t spd = ball->speed;
            spd += (uint16_t)spd >> 3;
            ball->speed = spd;
        }
        goto pass_update_timer;
    }

pass_holding_left_or_right:
    team->longPass = 1;
    {
        int16_t spd = ball->speed;
        spd += (uint16_t)spd >> 3;
        ball->speed = spd;
    }

pass_update_timer:
    team->spinTimer++;
    if (team->spinTimer == 10)
        goto pass_reset_spin;
    return;

pass_reset_spin:
    team->spinTimer = (int16_t)0xFFFF;
}
