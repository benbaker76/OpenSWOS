/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "headers.h"
#include "dos_sound.h"
#include "dos_mode.h"      /* _gDosGameplay: the aim-anim tail is Amiga-only */

extern int16_t  _gsPlayerDownTimer;   /* Amiga 50 / DOS 55 — dos_gameplay.c */
#include "game_types.h"

/* ---- External asm symbols ---- */
extern uint16_t _currentGameTick;
extern int16_t _kDefaultDestinations[16];
extern int16_t _kPlayerHeaderSpeedIncrease[8];
extern uint32_t _kBallJumpHeaderDeltaZ;
extern uint16_t _kJumpHeaderSpeed;
extern uint32_t _kHeaderLowJumpHeight;
extern uint32_t _kHeaderHighJumpHeight;
extern uint16_t _kStaticHeaderBaseBallSpeed;
extern uint16_t _kStaticHeaderPlayerSpeed;

/* Animation tables (in sprite_data.c) */
extern AnimTable _jumpHeaderHitAnimTable;
extern AnimTable _jumpHeaderAttemptAnimTable;
extern AnimTable _staticHeaderAttemptAnimTable;
extern AnimTable _staticHeaderAimAnimTable;
extern AnimTable _staticHeaderHitAnimTable;

/* Ball sprite */
extern Sprite _ballSprite;

/* Functions from other modules */
#include "player_anim.h"
extern PlayerInfo *GetPlayerPointerFromShirtNumber(Sprite *playerSprite, TeamGeneralInfo *teamData);
extern void PlayKickSample(void);
extern void ResetBothTeamSpinTimers(void);


/* ==================================================================
 * setPlayerJumpHeaderHitAnimationTable (static)
 *   from: SetPlayerJumpHeaderHitAnimationTable
 *   in: player (A1)
 *
 * Sets jump-header-hit anim if frameSwitchCounter <= 2.
 * ================================================================== */
static void setPlayerJumpHeaderHitAnimationTable(Sprite *player)
{
    if ((uint16_t)player->frameSwitchCounter > 2)
        return;
    SetPlayerAnimationTableAndPictureIndex(&_jumpHeaderHitAnimTable, player);
}


/* ==================================================================
 * doFlyingHeader (static)
 *   from: DoFlyingHeader
 *   in: player (A1), ball (A2)
 *
 * Flying header: set low jump height delta-Z, reduce ball speed to 75%.
 * ================================================================== */
static void doFlyingHeader(Sprite *player, Sprite *ball)
{
    ball->deltaZ.raw = (int32_t)_kHeaderLowJumpHeight;
    uint16_t spd = (uint16_t)ball->speed;
    uint16_t quarter = spd >> 2;
    ball->speed = (int16_t)(spd - quarter);
    setPlayerJumpHeaderHitAnimationTable(player);
}


/* ==================================================================
 * doLobHeader (static)
 *   from: DoLobHeader
 *   in: player (A1), ball (A2)
 *
 * Lob header: set high jump height delta-Z, reduce ball speed to ~94%.
 * ================================================================== */
static void doLobHeader(Sprite *player, Sprite *ball)
{
    ball->deltaZ.raw = (int32_t)_kHeaderHighJumpHeight;
    uint16_t spd = (uint16_t)ball->speed;
    uint16_t sixteenth = spd >> 4;
    ball->speed = (int16_t)(spd - sixteenth);
    setPlayerJumpHeaderHitAnimationTable(player);
}


/* ==================================================================
 * SetJumpHeaderHitAnimTable
 *   from: SetJumpHeaderHitAnimTable
 *   in: player (A1)
 *
 * Conditionally sets the jump-header-hit animation table on a player.
 * Checks: not already set, heading==0, playerDownTimer==40,
 * frameSwitchCounter<=2, currentGameTick bit 1 set.
 * ================================================================== */
void __attribute__((used, externally_visible))
SetJumpHeaderHitAnimTable(Sprite *player)
{
    if (player->animationTable == &_jumpHeaderHitAnimTable)
        return;
    if (player->heading != 0)
        return;
    if ((uint8_t)player->playerDownTimer != 40)
        return;
    if ((uint16_t)player->frameSwitchCounter > 2)
        return;
    /* btst #1,currentGameTick+1(pc) — bit 1 of the LOW byte on BE m68k. */
    if (!(_currentGameTick & 0x02))
        return;
    SetPlayerAnimationTableAndPictureIndex(&_jumpHeaderHitAnimTable, player);
    uint16_t spd = (uint16_t)player->speed;
    player->speed = (int16_t)(spd >> 1);
}


/* ==================================================================
 * PlayerHittingJumpHeader
 *   from: PlayerHittingJumpHeader
 *   in: player (A1), team (A6)
 *
 * Hitting flying or lob header. Determines direction deflection based
 * on input vs player direction delta (mod 8), calls DoFlyingHeader or
 * DoLobHeader accordingly, then sets ball destination and speed.
 * ================================================================== */
void __attribute__((used, externally_visible))
PlayerHittingJumpHeader(Sprite *player, TeamGeneralInfo *team)
{
    team->passInProgress = 0;

    int16_t inputDir = team->currentAllowedDirection;
    if (inputDir < 0)
        inputDir = player->direction;

    Sprite *ball = &_ballSprite;
    ball->deltaZ.raw = (int32_t)_kBallJumpHeaderDeltaZ;

    /* Ball speed = player speed * 1.25 */
    uint16_t plSpeed = (uint16_t)player->speed;
    ball->speed = (int16_t)plSpeed;
    uint16_t quarter = plSpeed >> 2;
    ball->speed = (int16_t)(plSpeed + quarter);

    int16_t playerDir = player->direction;
    int16_t delta = (playerDir - inputDir);

    int16_t inputAllowed = team->currentAllowedDirection;
    int16_t finalDir;

    if (inputAllowed < 0) {
        /* No input direction: flying header, keep player direction */
        doFlyingHeader(player, ball);
        goto keep_dir;
    }

    delta &= 7;

    switch (delta) {
    case 0: /* Same direction: no height modification */
        goto keep_dir;

    case 1: /* Slight offset: decrement direction */
        goto decrement_dir;

    case 2: /* Flying header + decrement */
        doFlyingHeader(player, ball);
        goto decrement_dir;

    case 3: /* Lob header + decrement */
        doLobHeader(player, ball);
        goto decrement_dir;

    case 4: /* Opposite direction: lob header, keep direction */
        doLobHeader(player, ball);
        goto keep_dir;

    case 5: /* Lob header + increment */
        doLobHeader(player, ball);
        goto increment_dir;

    case 6: /* Flying header + increment */
        doFlyingHeader(player, ball);
        goto increment_dir;

    case 7: /* Slight offset: increment direction */
        goto increment_dir;
    }

increment_dir:
    finalDir = (player->direction + 1) & 7;
    goto apply_dir;

decrement_dir:
    finalDir = (player->direction - 1) & 7;
    goto apply_dir;

keep_dir:
    finalDir = player->direction;

apply_dir:
    finalDir &= 7;
    team->controlledPlDirection = finalDir;

    /* Set ball destination from direction lookup table */
    int16_t dirIdx = finalDir * 2; /* index into kDefaultDestinations (pairs of x,y) */
    int16_t destX = _kDefaultDestinations[dirIdx] + ball->x.s.i;
    ball->destX = destX;
    int16_t destY = _kDefaultDestinations[dirIdx + 1] + ball->y.s.i;
    ball->destY = destY;

    /* Apply heading attribute speed increase. */
    PlayerInfo *pi = GetPlayerPointerFromShirtNumber(player, team);
    int8_t headingSkill = pi->heading;
    int16_t speedBonus = _kPlayerHeaderSpeedIncrease[(int16_t)headingSkill];
    uint16_t _ballSpeed = (uint16_t)ball->speed;
    ball->speed = (int16_t)(_ballSpeed + speedBonus);

    /* Halve player speed, mark heading done */
    uint16_t pSpeed = (uint16_t)player->speed;
    player->speed = (int16_t)(pSpeed >> 1);
    player->heading = 1;

    DOS_SOUND(DOS_EV_HEADER, team->playerNumber != 0);
    { extern void ModMatch_Header(const Sprite *s); ModMatch_Header(player); }   /* (a goal's kind: mod_match.c) */
    PlayKickSample();
    ResetBothTeamSpinTimers();
}


/* ==================================================================
 * SetStaticHeaderDirection
 *   from: SetStaticHeaderDirection
 *   in: player (A1), team (A6)
 *
 * Steers player direction toward input direction during static header
 * attempt. Adjusts direction by ±1 per frame.
 * ================================================================== */
void __attribute__((used, externally_visible))
SetStaticHeaderDirection(Sprite *player, TeamGeneralInfo *team)
{
    int16_t inputDir = team->currentAllowedDirection;
    if (inputDir < 0)
        return;
    if ((uint8_t)player->playerDownTimer > 18)
        return;
    if (player->animationTable != &_staticHeaderAttemptAnimTable)
        return;

    int16_t delta = inputDir - player->direction;
    if (delta != 0) {
        delta &= 7;
        if (delta != 4) {
            if (delta < 4) {
                /* bcs: delta < 4 → increment */
                int16_t d = player->direction + 1;
                player->direction = d;
            } else {
                /* delta > 4 → decrement */
                int16_t d = player->direction - 1;
                player->direction = d;
            }
            int16_t d = player->direction & 7;
            player->direction = d;
        }
    }

    /* The Amiga funnels all three outcomes — already facing the input, a
     * half-turn away, or one step turned — into the same tail:
     *
     *     _11288E_apply_dir_wrap_mod8:
     *             andi.w  #7,SPRITE_DIRECTION(a1)
     *     apply_static_header_aim_anim:            ; SWOS2.s:25882
     *             lea     (staticHeaderAimAnimTable).l,a0
     *             jsr     SetPlayerAnimationTableAndPictureIndex
     *
     * The DOS build has no such tail at all: the two `beq`s become `jz @@out`
     * and the turn falls through to the same bare `retn` (swos.asm:112213,
     * 112216, 112231). So a DOS static header stays on the ATTEMPT animation
     * for its whole 20 frames, where the Amiga switches to the AIM one on the
     * first frame the down-timer drops to 18.
     *
     * That is a missing tail rather than a changed constant, which is a strange
     * thing for a recompilation to do — but the corpus measures it and agrees
     * with the listing: playa-10144556's header keeps frame_delay 10 (the
     * attempt list's own `-10`) and image 612 from frame 1304 to 1312, where
     * the aim table would have put 5 there. Amiga behaviour is unchanged. */
    if (!_gDosGameplay)
        SetPlayerAnimationTableAndPictureIndex(&_staticHeaderAimAnimTable, player);
}


/* ==================================================================
 * PlayerHittingStaticHeader
 *   from: PlayerHittingStaticHeader
 *   in: player (A1), team (A6)
 *
 * Handles ball contact during static header. Adjusts player direction
 * toward input (1-2 steps), sets ball destination and speed, applies
 * heading skill bonus, negates and halves deltaZ.
 * ================================================================== */
void __attribute__((used, externally_visible))
PlayerHittingStaticHeader(Sprite *player, TeamGeneralInfo *team)
{
    team->passInProgress = 0;

    /* Skip direction adjustment if already in hit anim */
    if (player->animationTable == &_staticHeaderHitAnimTable)
        goto apply_hit;

    /* Compare input direction with player direction */
    int16_t inputDir = team->currentAllowedDirection;
    int16_t playerDir = player->direction;
    int16_t delta = inputDir - playerDir;
    if (delta == 0)
        goto apply_hit;

    delta &= 7;
    if (delta == 4)
        goto apply_hit;

    if (delta < 4) {
        /* bcs: delta < 4 → increment path (turn toward input) */
        inputDir = team->currentAllowedDirection;
        int16_t d = player->direction + 1;
        player->direction = (int16_t)(d & 7);
        if (player->direction != inputDir) {
            d = player->direction + 1;
            player->direction = d;
        }
    } else {
        /* delta > 4 → decrement path */
        inputDir = team->currentAllowedDirection;
        int16_t d = player->direction - 1;
        player->direction = (int16_t)(d & 7);
        if (player->direction != inputDir) {
            d = player->direction - 1;
            player->direction = d;
        }
    }

    /* Final wrap to 0-7 */
    player->direction = (int16_t)(player->direction & 7);

apply_hit:
    SetPlayerAnimationTableAndPictureIndex(&_staticHeaderHitAnimTable, player);

    /* Determine shooting direction: use input if available, else player dir */
    int16_t shootDir = team->currentAllowedDirection;
    if (shootDir < 0)
        shootDir = player->direction;

    Sprite *ball = &_ballSprite;
    team->controlledPlDirection = shootDir;

    /* Set ball destination from direction table */
    int16_t dirIdx = shootDir * 2;
    int16_t destX = _kDefaultDestinations[dirIdx] + ball->x.s.i;
    ball->destX = destX;
    int16_t destY = _kDefaultDestinations[dirIdx + 1] + ball->y.s.i;
    ball->destY = destY;

    /* Set base ball speed for static header */
    ball->speed = (int16_t)_kStaticHeaderBaseBallSpeed;

    /* Apply heading attribute speed increase. */
    PlayerInfo *pi = GetPlayerPointerFromShirtNumber(player, team);
    int8_t headingSkill = pi->heading;
    int16_t speedBonus = _kPlayerHeaderSpeedIncrease[(int16_t)headingSkill];
    uint16_t ballSpeed = (uint16_t)ball->speed;
    ball->speed = (int16_t)(ballSpeed + speedBonus);

    /* Negate and halve deltaZ */
    int32_t dz = ball->deltaZ.raw;
    dz = (-dz) >> 1;
    ball->deltaZ.raw = dz;

    /* Mark heading done */
    player->heading = 1;

    DOS_SOUND(DOS_EV_HEADER, team->playerNumber != 0);
    { extern void ModMatch_Header(const Sprite *s); ModMatch_Header(player); }   /* (a goal's kind: mod_match.c) */
    PlayKickSample();
    ResetBothTeamSpinTimers();
}


/* ==================================================================
 * AttemptStaticHeader
 *   from: AttemptStaticHeader
 *   in: player (A1), dir (D0)
 *
 * Player jumps in place trying to hit static header. Sets animation,
 * destination, speed, state, and down timer.
 * ================================================================== */
void __attribute__((used, externally_visible))
AttemptStaticHeader(Sprite *player, uint16_t dir)
{
    player->heading = 0;
    player->direction = (int16_t)dir;

    /* Set destination from direction table */
    int16_t dirIdx = (int16_t)(dir * 2);
    int16_t destX = _kDefaultDestinations[dirIdx] + player->x.s.i;
    player->destX = destX;
    int16_t destY = _kDefaultDestinations[dirIdx + 1] + player->y.s.i;
    player->destY = destY;

    player->speed = (int16_t)_kStaticHeaderPlayerSpeed;

    SetPlayerAnimationTable(&_staticHeaderAttemptAnimTable, player);
    player->playerState = PL_STATIC_HEADING;
    player->playerDownTimer = 20;
}


/* ==================================================================
 * PlayerAttemptingJumpHeader
 *   from: PlayerAttemptingJumpHeader
 *   in: dir (D0), player (A1)
 *
 * Player starts jumping to attempt to head the ball. Sets animation,
 * state, down timer, destination, and speed.
 * ================================================================== */
void __attribute__((used, externally_visible))
PlayerAttemptingJumpHeader(uint16_t dir, Sprite *player)
{
    player->heading = 0;
    player->direction = (int16_t)dir;

    SetPlayerAnimationTable(&_jumpHeaderAttemptAnimTable, player);
    player->playerDownTimer = (int8_t)_gsPlayerDownTimer;   /* Amiga 50 / DOS 55 */
    player->playerState = PL_JUMP_HEADING;

    /* Set destination from direction table */
    int16_t dirIdx = (int16_t)(dir * 2);
    int16_t destX = _kDefaultDestinations[dirIdx] + player->x.s.i;
    player->destX = destX;
    int16_t destY = _kDefaultDestinations[dirIdx + 1] + player->y.s.i;
    player->destY = destY;

    player->speed = (int16_t)_kJumpHeaderSpeed;
}
