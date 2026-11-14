/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "long_bench.h"
#include "camera.h"
#include "game_types.h"
#include "display.h"
#include "dos_mode.h"   /* _gDosGameplay: SIM_CAM_AIM_* */

/* cameraX / cameraY are FixedPoint unions (see game_types.h).
 *   cameraX.raw     = full 32-bit 16.16 fixed-point value
 *   cameraX.s.i     = integer part (high word; same storage on both LE and BE)
 *   cameraX.s.frac  = fractional part (low word)
 * MoveCamera accumulates via .raw; position consumers use .s.i. */
extern FixedPoint _cameraX;
extern FixedPoint _cameraY;
extern uint16_t _cameraCoordinatesValid;

/* ---- External globals ---- */
extern uint16_t _showFansCounter;
extern Sprite   _ballSprite;
extern uint16_t _whichCard;
extern Sprite  *_bookedPlayer;
extern uint16_t _playingPenalties;
extern uint16_t _waitForPlayerToGoInTimer;
extern uint16_t _leavingSubsMenu;
extern uint16_t _inSubstitutesMenu;
extern Sprite   _goal1TopSprite;
extern Sprite   _goal2BottomSprite;
extern uint16_t _substituteInProgress;
extern uint16_t _benchXLimit;
extern uint16_t _gameStatePl;
extern uint16_t _gameState;
extern TeamGeneralInfo *_lastTeamPlayedBeforeBreak;
extern int16_t  _cameraDirection;
extern uint16_t _trainingGame;
extern int16_t  _penaltiesState;
extern uint16_t _cameraXVelocity;
extern uint16_t _cameraYVelocity;

/* ---- External functions ---- */
extern void SetCameraCoordinates(int16_t targetX, int16_t targetY);

/* ---- Data tables (moved from asm inline data) ---- */

/* nextCameraDirections: given direction 0..7, provides x/y sign pairs.
 * Each entry is two int32_t (x_sign, y_sign) — 8 pairs total.
 * Values: -1, 0, or 1. */
static const int32_t nextCameraDirections[16] = {
    0, -1,  1, -1,  1, 0,  1, 1,
    0, 1,  -1, 1,  -1, 0,  -1, -1
};

/* Maximum camera movement per frame in 16.16 fixed-point = 5.0 pixels */
#define MAX_CAMERA_MOVEMENT  0x00050000L

/* ================================================================
 * MoveCamera  (SWOS2.s line 9625)
 *
 * Per-frame camera tracking. Priority order:
 * 1. showFansCounter active → do nothing
 * 2. Booking → lock on booked player
 * 3. Penalties → lock on penalty spot (336, 107)
 * 4. Substitution timers → lock on bench area
 * 5. Subs menu open → goal/bench state logic
 * 6. Normal game → velocity-based ball tracking
 * 7. Set pieces → narrowed bounds
 * 8. Special states → fixed camera targets
 *
 * Uses 16.16 fixed-point for smooth sub-pixel movement.
 * SetCameraCoordinates handles the integer position update.
 * ================================================================ */
__attribute__((used, externally_visible))
void MoveCamera(void)
{
    int16_t boundsMargin;    /* d1 — controls how tight the camera clamp is */
    int16_t targetX;         /* d3 — camera target X */
    int16_t targetY;         /* d4 — camera target Y */
    int32_t ballDeltaX;      /* d3 in velocity path — 32-bit */
    int32_t ballDeltaY;      /* d4 in velocity path — 32-bit */

    if (_showFansCounter != 0)
        return;

    /* Default: load ball sprite deltas for velocity path */
    ballDeltaX = _ballSprite.deltaX.raw;
    ballDeltaY = _ballSprite.deltaY.raw;

    /* ---- Priority 1: Booking ---- */
    if (_whichCard != 0) {
        Sprite *bookedSpr = _bookedPlayer;
        targetX = bookedSpr->x.s.i;
        targetY = bookedSpr->y.s.i;
        _cameraXVelocity = 0;
        _cameraYVelocity = 0;
        boundsMargin = 37;
        goto offset_and_clamp_to_view;
    }

    /* ---- Priority 2: Penalties ---- */
    if (_playingPenalties != 0) {
        targetX = 336;
        targetY = 107;
        _cameraXVelocity = 0;
        _cameraYVelocity = 0;
        boundsMargin = 0;
        goto offset_and_clamp_to_view;
    }

    /* ---- Priority 3: Substitution timers ---- */
    if (_waitForPlayerToGoInTimer != 0)
        goto bench_camera_mode;

    if (_leavingSubsMenu != 0) {
        boundsMargin = 37;
        targetX = 211;
        targetY = 449;
        _cameraXVelocity = 0;
        _cameraYVelocity = 0;
        goto offset_and_clamp_to_view;
    }

    /* ---- Priority 4: Substitutes menu open ---- */
    if (_inSubstitutesMenu != 0) {
        boundsMargin = 37;
        {
            int16_t camY = (int16_t)(_cameraY.raw >> 16);
            /* (a long bench's two rows: the camera aims 1 px lower, at the halfway line between the
             *  long dugouts, and one that stops 1..3 px short -- MoveCamera's step there is 0, so it
             *  never stood still and the bench took no keys -- is put on it: the Mod's bx_camlo) */
            const int16_t lb = LongBench_TwoRows() ? (int16_t)(LB_CAM_Y - 0x1C1) : 0;
            if (lb) {
                const int16_t dest = (int16_t)(SIM_CAMERA_BENCH_Y + lb);
                if (camY >= dest - 3 && camY < dest) {
                    _cameraY.raw = (int32_t)((uint32_t)(uint16_t)dest << 16);
                    camY = dest;
                }
            }
            if (camY >= SIM_CAMERA_BENCH_Y_LO + lb && camY <= SIM_CAMERA_BENCH_Y_HI + lb) {
                /* The camera has reached the bench view (the asm's literal
                 * 303..323 — see SIM_CAMERA_BENCH_Y_*, which is f(view) and
                 * reproduces the DOS release's own 339..359 at 200 lines). */
                boundsMargin = 37;

                /* Check if goal1TopSprite is visible */
                if (_goal1TopSprite.imageIndex != (int16_t)0xFFFF) {
                    if (_goal1TopSprite.onScreen != 0)
                        goto bench_camera_target_and_zero_vel;
                }

                /* Check if goal2BottomSprite is visible */
                if (_goal2BottomSprite.imageIndex != (int16_t)0xFFFF) {
                    if (_goal2BottomSprite.onScreen != 0)
                        goto bench_camera_target_and_zero_vel;
                }

                /* Check if substitute is in progress */
                if (_substituteInProgress != 0)
                    goto bench_camera_mode;

                boundsMargin = 0;
                goto bench_camera_target_and_zero_vel;
            }
        }
        goto bench_camera_target_and_zero_vel;
    }

    /* ---- Priority 5: Pre-play camera direction ---- */
    if (_gameStatePl != ST_GAME_IN_PROGRESS) {
        TeamGeneralInfo *teamPtr = _lastTeamPlayedBeforeBreak;
        if (teamPtr != (TeamGeneralInfo *)0) {
            Sprite *spritePtr = teamPtr->controlledPlayer;
            if (spritePtr != (Sprite *)0) {
                int16_t dir = spritePtr->direction;
                /* Look up camera direction vector from table */
                ballDeltaX = nextCameraDirections[dir * 2];
                ballDeltaY = nextCameraDirections[dir * 2 + 1];
                goto apply_accel_limits_setup;
            }
        }
        /* Fallback: use global cameraDirection */
        if (_cameraDirection >= 0) {
            int16_t dir = _cameraDirection;
            ballDeltaX = nextCameraDirections[dir * 2];
            ballDeltaY = nextCameraDirections[dir * 2 + 1];
        } else {
            ballDeltaX = 0;
            ballDeltaY = 0;
        }
    }

    /* ---- Velocity update: accelerate toward ball movement direction ---- */
apply_accel_limits_setup:
    {
        int16_t accel = 2;       /* d0 */
        int16_t maxXVel = 40;    /* d1 = $28 */
        int16_t maxYVel = 40;    /* d2 = $28 */

        /* Update X velocity */
        if (ballDeltaX != 0) {
            if (ballDeltaX > 0) {
                /* Ball moving right → accelerate X positive */
                if ((int16_t)_cameraXVelocity != maxXVel)
                    _cameraXVelocity += accel;
            } else {
                /* Ball moving left → accelerate X negative */
                if ((int16_t)_cameraXVelocity != -maxXVel)
                    _cameraXVelocity -= accel;
            }
        }

        /* Update Y velocity */
        if (ballDeltaY != 0) {
            if (ballDeltaY > 0) {
                /* Ball moving down → accelerate Y positive */
                if ((int16_t)_cameraYVelocity != maxYVel)
                    _cameraYVelocity += accel;
            } else {
                /* Ball moving up → accelerate Y negative */
                if ((int16_t)_cameraYVelocity != -maxYVel)
                    _cameraYVelocity -= accel;
            }
        }

        /* ---- Bounds margin selection ---- */
        boundsMargin = 63;   /* d1 = $3F, default */

        if (_gameStatePl == ST_GAME_IN_PROGRESS)
            goto state_based_camera_target;

        boundsMargin = 63;   /* redundant but matches asm */

        /* Narrow bounds for set pieces (corners and throw-ins) */
        if (_gameState == ST_CORNER_LEFT || _gameState == ST_CORNER_RIGHT)
            goto narrow_bounds_for_set_pieces;
        if (_gameState >= ST_THROW_IN_FORWARD_RIGHT && _gameState <= ST_THROW_IN_BACK_LEFT)
            goto narrow_bounds_for_set_pieces;

        goto state_based_camera_target;
    }

narrow_bounds_for_set_pieces:
    boundsMargin = 37;   /* d1 = $25 */

state_based_camera_target:
    /* Check for special game states that use fixed camera targets */
    if (_gameState >= ST_STARTING_GAME && _gameState <= ST_GAME_ENDED) {
        if (_gameState == ST_STARTING_GAME)
            goto tunnel_halftime_camera_target;
        if (_gameState == ST_CAMERA_GOING_TO_SHOWERS)
            goto tunnel_halftime_camera_target;
        if (_gameState == ST_FIRST_HALF_ENDED)
            goto target_ball_position;
        if (_gameState == ST_GAME_ENDED) {
            if (_penaltiesState < 0)
                goto center_camera_fulltime;
            goto target_ball_position;
        }
        if (_gameState == ST_GOING_TO_HALFTIME)
            goto tunnel_halftime_camera_target;
        if (_gameState == ST_PLAYERS_GOING_TO_SHOWER)
            goto tunnel_halftime_camera_target;
        if (_gameState == ST_RESULT_AFTER_THE_GAME)
            goto results_screen_camera_target;

        /* Other states in ST_STARTING_GAME..ST_GAME_ENDED range */
center_camera_fulltime:
        targetX = 0x150;
        targetY = 0x1C1;
        _cameraXVelocity = 0;
        _cameraYVelocity = 0;
        goto offset_and_clamp_to_view;
    }

target_ball_position:
    /* Use ball's integer position */
    targetX = _ballSprite.x.s.i;
    targetY = _ballSprite.y.s.i;
    goto offset_and_clamp_to_view;

results_screen_camera_target:
    targetX = 0x150;
    targetY = 0x81;
    _cameraXVelocity = 0;
    _cameraYVelocity = 0;
    goto offset_and_clamp_to_view;

tunnel_halftime_camera_target:
    targetX = 0x24E;
    targetY = 0x1C1;
    _cameraXVelocity = 0;
    _cameraYVelocity = 0;
    goto offset_and_clamp_to_view;

bench_camera_mode:
    boundsMargin = 0x33;    /* 51 */
    goto bench_camera_target_and_zero_vel;

bench_camera_target_and_zero_vel:
    targetX = (int16_t)_benchXLimit;
    targetY = LongBench_TwoRows() ? LB_CAM_Y : 0x1C1;     /* (long_bench.c: the Mod's bx_camy) */
    _cameraXVelocity = 0;
    _cameraYVelocity = 0;
    /* fall through to offset_and_clamp_to_view */

offset_and_clamp_to_view:
    /* Center the view: subtract half screen dimensions */
    /* Centre the view on the target. Both halves come from the SIMULATION view
     * (display.h), not the render width: 168/136 at the port's own 336x272,
     * 160/100 at the DOS release's 320x200.
     *
     * DOS MODE AIMS WITH THE DOS VIEW. MoveCamera itself is the same routine in both
     * releases -- same branches, same easing (delta >> 2, times 0x4000, 16.16), and the
     * same cap (swos.asm's `maxCameraMovement dd 50000h` is our MAX_CAMERA_MOVEMENT) --
     * so there is no separate DOS algorithm to port. What differs is the view every
     * constant is derived from, and the half-view above is the one that decides where the
     * camera AIMS: the ball is held 100 lines from the top in DOS and 136 in this port,
     * so the camera sits lower, starts moving at a different moment and reaches its
     * limits at different times. That is the movement difference, and it is this line.
     *
     * The CLAMPS below deliberately keep using _sim_view_*, which is the geometry we
     * actually draw: a 272-line render driven by the DOS Y clamp (664) would scroll 56
     * lines past the bottom of the 880-tall pitch. So the camera moves like DOS and stops
     * where this port can still draw -- position differs at the edges, by design. */
    targetX -= (SIM_CAM_AIM_WIDTH / 2);
    targetY -= (int16_t)(SIM_CAM_AIM_HEIGHT / 2);

    /* Apply camera velocity */
    targetX += (int16_t)_cameraXVelocity;
    targetY += (int16_t)_cameraYVelocity;

    /* Clamp X so the _screen_width-wide view stays on the pitch. */
    {
        /* pitchWidth(0x2A0=672) - viewWidth = how far the camera may pan and keep the
         * view on the pitch (=0x150 at 336). On wide/ultrawide displays the view nears
         * the pitch width and this range collapses or inverts — then just center the
         * view on the pitch (the whole pitch is visible; the camera can't pan). */
        int16_t panRange = SIM_CAMERA_PAN_RANGE;
        int16_t xMax = panRange - boundsMargin;
        if (xMax <= boundsMargin) {
            targetX = (int16_t)(panRange / 2);
        } else {
            if (targetX < boundsMargin)
                targetX = boundsMargin;
            if (targetX >= xMax)
                targetX = xMax;
        }
    }

    /* Clamp Y based on game mode */
    if (_trainingGame != 0) {
        if (targetY <= 0x50)
            targetY = 0x50;
        if (targetY >= SIM_CAMERA_MAX_Y_TRN)   /* 0x220 = 544 at 272 lines */
            targetY = SIM_CAMERA_MAX_Y_TRN;
    } else {
        if (targetY <= 0x10)
            targetY = 0x10;
        if (targetY >= SIM_CAMERA_MAX_Y)       /* 0x250 = 592 at 272 lines */
            targetY = SIM_CAMERA_MAX_Y;
    }

    /* ---- Compute fixed-point delta from current camera position ---- */
    {
        int16_t cameraIntX = (int16_t)(_cameraX.raw >> 16);
        int16_t cameraIntY = (int16_t)(_cameraY.raw >> 16);
        int16_t dx = targetX - cameraIntX;
        int16_t dy = targetY - cameraIntY;

        if (dx == 0 && dy == 0)
            return;   /* No movement needed */

        /* Scale delta to 16.16 fixed-point */
        int32_t fixedDx = 0;
        if (dx != 0) {
            int16_t scaledDx = dx >> 2;   /* asr.w #2 */
            fixedDx = (int32_t)0x4000 * (int32_t)scaledDx;
        }

        int32_t fixedDy = 0;
        if (dy != 0) {
            int16_t scaledDy = dy >> 2;   /* asr.w #2 */
            fixedDy = (int32_t)0x4000 * (int32_t)scaledDy;
        }

        /* Clamp to ±MAX_CAMERA_MOVEMENT */
        if (fixedDx >= MAX_CAMERA_MOVEMENT)
            fixedDx = MAX_CAMERA_MOVEMENT;
        if (fixedDx <= -MAX_CAMERA_MOVEMENT)
            fixedDx = -MAX_CAMERA_MOVEMENT;
        if (fixedDy >= MAX_CAMERA_MOVEMENT)
            fixedDy = MAX_CAMERA_MOVEMENT;
        if (fixedDy <= -MAX_CAMERA_MOVEMENT)
            fixedDy = -MAX_CAMERA_MOVEMENT;

        /* Add to current fixed-point camera position */
        int32_t newXFixed = fixedDx + _cameraX.raw;
        int32_t newYFixed = fixedDy + _cameraY.raw;

        /* Write fractional part only (low 16 bits of fixed-point result).
         * On big-endian 68k, cameraX.raw high word = integer, low word = fraction.
         * We update only the fraction here; SetCameraCoordinates handles integer. */
        /* Keep only the fractional half (low 16 bits) for accumulation.
         * The integer part gets written by SetCameraCoordinates below.
         * With FixedPoint union this is a direct field write — on BE it
         * targets the low word of the uint32, on LE it targets the low
         * two bytes of the struct, both of which ARE the frac field. */
        _cameraX.s.frac = (uint16_t)newXFixed;
        _cameraY.s.frac = (uint16_t)newYFixed;

        /* Extract integer part (swap d1/d2 in asm) */
        int16_t newIntX = (int16_t)(newXFixed >> 16);
        int16_t newIntY = (int16_t)(newYFixed >> 16);

        /* Final clamp of integer position */
        if (newIntX < 0)
            newIntX = 0;
        if (newIntX >= SIM_CAMERA_PAN_RANGE)   /* pitchWidth - viewWidth */
            newIntX = SIM_CAMERA_PAN_RANGE;

        if (newIntY < 0x10)
            newIntY = 0x10;
        if (newIntY >= SIM_CAMERA_POS_MAX_Y)   /* 0x260 = 880 - 272 lines */
            newIntY = SIM_CAMERA_POS_MAX_Y;

        SetCameraCoordinates(newIntX, newIntY);
        _cameraCoordinatesValid = 0;
    }
}
