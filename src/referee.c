/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/*
 * referee.c — Referee system: activation, movement, animation, booking display
 *
 * Ported from SWOS2.s: ActivateReferee, SetRefereeToLeavingState,
 * InitRefereeAnimationTable, UpdateReferee, UpdateBookedPlayerNumberSprite
 */

#include "shirt_numbers.h"
#include "referee.h"
#include "dos_sound.h"
#include "dos_mode.h"      /* _gDosGameplay: the DOS second-yellow table runs off its end */
#include "display.h"      /* SIM_REFEREE_ENTRY_BELOW: f(view) */
#include "game_types.h"
#include "math68k.h"     /* CalculateDeltaXAndY */

/* ---- Referee state machine ---- */
#define REF_INCOMING            1
#define REF_WAITING_PLAYER      2
#define REF_ABOUT_TO_GIVE_CARD  3
#define REF_BOOKING             4
#define REF_LEAVING             5

/* ---- Card types ---- */
#define CARD_YELLOW             1
#define CARD_RED                2
#define CARD_SECOND_YELLOW      3

/* ---- Sprite indices ---- */
#define SPR_YELLOW_CARD         1178
#define SPR_RED_CARD            1177
#define SPR_CORNER_FLAG_FRAME_3 1073

/* ---- Extern globals ---- */
extern Sprite   _refereeSprite;
extern Sprite   _bookedPlayerCardOrNumberSprite;
extern uint16_t _refState;
extern uint16_t _foulXCoordinate;
extern uint16_t _foulYCoordinate;
extern FixedPoint _cameraY;
extern uint16_t _kRefereeSpeed;
extern uint16_t _whichCard;
extern Sprite          *_bookedPlayer;
extern TeamGeneralInfo *_lastTeamBooked;
extern uint16_t _refTimer;
extern uint16_t _lastFrameTicks;
extern uint16_t _gameStatePl;
extern uint16_t _panicReasonCode;

/* AnimTable data (in asm code section) */
extern uint8_t _refComingAnimTable[];
extern uint8_t _refWaitingAnimTable[];
extern uint8_t _refYellowCardAnimTable[];
extern uint8_t _refRedCardAnimTable[];
extern uint8_t _refSecondYellowAnimTable[];

/* Card display lookup tables (in asm code section, after UpdateBookedPlayerNumberSprite) */
extern int8_t _yellowCardTable[];    /* 30 entries */
extern int8_t _redCardTable[];       /* 30 entries */
extern int8_t _secondYellowTable[];  /* 46 entries */

/* ---- External functions ---- */
extern uint16_t Rand(void);
extern void InitDisplaySprites(void);
extern void PanicHaltLoop(void);


/* SendPlayerAway ported to C in tackle_foul.c (still uses uint8_t* params) */
extern void SendPlayerAway(uint8_t *playerSprite, uint8_t *teamData);

/* ================================================================
 * InitRefereeAnimationTable  (SWOS2.s line 10612)
 *
 * Sets up referee sprite animation from an AnimTable pointer.
 * AnimTable layout: uint16_t frameDelay, then 8 x uint32_t frame-indices-table ptrs
 *
 * in: animTable = pointer to AnimTable (was A1 in asm)
 * ================================================================ */
__attribute__((used, externally_visible))
void InitRefereeAnimationTable(uint8_t *animTable)
{
    Sprite *a0 = &_refereeSprite;
    uint16_t dir = (uint16_t)a0->direction;
    uint16_t frameDelay = *(uint16_t *)animTable;
    /* The frame table for this direction: a packed AnimTable (game_types.h), a 2-byte
     * delay and then one pointer per direction. The asm's `dir * 4` is the size of a
     * pointer on the Amiga, not a constant: on a 64-bit host it read half of one pointer
     * and half of the next for an odd direction (a crash -- a referee arriving on a
     * diagonal, found at frame 24269 of zkz-11215003 at simulation width 484), and the
     * table for dir/2 for an even one (the wrong way round). sizeof keeps the m68k code
     * what it was. */
    int16_t *frameTablePtr = *(int16_t **)(animTable + 2 + dir * sizeof(int16_t *));

    a0->frameDelay        = (int16_t)frameDelay;
    a0->frameIndicesTable = frameTablePtr;

    if (frameTablePtr == 0) {
        _panicReasonCode = 0x1112;
        PanicHaltLoop();
        return;
    }

    a0->frameSwitchCounter = (int16_t)0xFFFF;
    a0->frameIndex         = (int16_t)0xFFFF;
    a0->cycleFramesTimer   = 1;
}

/* ================================================================
 * ActivateReferee  (SWOS2.s line 10553)
 *
 * Positions referee sprite at foul location with random offset,
 * starts incoming animation.
 * ================================================================ */
__attribute__((used, externally_visible))
void ActivateReferee(void)
{
    Sprite *a0 = &_refereeSprite;
    int16_t d1 = (int16_t)_foulXCoordinate + 28;
    int16_t d2 = (int16_t)_foulYCoordinate + 5;

    a0->destX = d1;
    a0->destY = d2;

    uint16_t rval = Rand();
    int16_t d0 = (int16_t)(rval >> 3);

    if (_foulXCoordinate >= 336) {
        d0 = -d0;
    }

    /* Position above or below camera based on foul Y */
    if (_foulYCoordinate <= 449) {
        /* Foul in upper half: referee enters from bottom — one view height
         * below the camera plus 15. Was a hard-coded 287, which is the Amiga's
         * 272-high view; the DOS build writes 215 for its 200-high one. */
        d2 = (int16_t)_cameraY.s.i + SIM_REFEREE_ENTRY_BELOW;
    } else {
        /* Foul in lower half: referee enters from top */
        d2 = (int16_t)_cameraY.s.i - 20;
    }

    d1 = d1 + d0;
    a0->x.s.i    = d1;
    a0->y.s.i    = d2;
    /* Clear fractional parts */
    a0->x.s.frac = 0;
    a0->y.s.frac = 0;

    a0->speed   = (int16_t)_kRefereeSpeed;
    a0->visible = 1;

    InitDisplaySprites();
    InitRefereeAnimationTable(_refComingAnimTable);
    _refState = REF_INCOMING;
}

/* ================================================================
 * SetRefereeToLeavingState  (SWOS2.s line 10590)
 *
 * Sets referee destination to off-screen and starts leaving anim.
 * ================================================================ */
__attribute__((used, externally_visible))
void SetRefereeToLeavingState(void)
{
    Sprite *a0 = &_refereeSprite;
    uint16_t rval = Rand();
    int16_t d0 = (int16_t)(rval >> 2) - 32;   /* range -32..31 */

    int16_t d2;
    if (_foulYCoordinate <= 449) {
        d2 = 770;
    } else {
        d2 = 129;
    }

    d0 = d0 + a0->x.s.i;
    a0->destX = d0;
    a0->destY = d2;

    InitRefereeAnimationTable(_refComingAnimTable);
    _refState = REF_LEAVING;
}

/* ================================================================
 * UpdateReferee  (SWOS2.s line 10630)
 *
 * Per-frame referee update: animation cycling, movement toward
 * destination, state machine (incoming→waiting→card→booking→leaving).
 * ================================================================ */
__attribute__((used, externally_visible))
void UpdateReferee(void)
{
    if (_refState == 0)
        return;

    Sprite *a0 = &_refereeSprite;

    if (a0->visible == 0)
        return;

    /* If off-screen, handle position update separately */
    if (a0->onScreen == 0)
        goto off_screen_update;

    /* ---- Animation frame cycling ---- */
    a0->cycleFramesTimer = (int16_t)((uint16_t)a0->cycleFramesTimer - 1);
    if ((uint16_t)a0->cycleFramesTimer == 0) {
        a0->frameIndex += 1;
        a0->cycleFramesTimer = a0->frameDelay;

        int16_t *frameTable = a0->frameIndicesTable;

    decode_frame:;
        int16_t frameIdx = a0->frameIndex;
        int16_t frameVal = frameTable[frameIdx];

        if (frameVal >= 0) {
            /* Normal frame: set image index, bump switch counter */
            a0->frameSwitchCounter += 1;
            a0->imageIndex = frameVal;
        } else {
            /* Negative: animation command */
            if (frameVal == (int16_t)0xFC19) {
                /* -999: restart from frame 0 */
                a0->frameIndex = 0;
                goto decode_frame;
            } else if (frameVal == (int16_t)0xFF9B) {
                /* -101: hold (step back one frame) */
                a0->frameIndex -= 1;
                /* fall through to state dispatch */
            } else if (frameVal <= (int16_t)0xFF9C) {
                /* <= -100: relative jump (add 100+frameVal to index) */
                a0->frameIndex += (int16_t)(100 + frameVal);
                goto decode_frame;
            } else {
                /* -1..-99: new frame delay */
                int16_t newDelay = (int16_t)(-frameVal);
                a0->frameDelay       = newDelay;
                a0->cycleFramesTimer = newDelay;
                a0->frameIndex      += 1;
                goto decode_frame;
            }
        }
    }

    /* ---- State dispatch ---- */
state_dispatch:
    if (_refState == REF_ABOUT_TO_GIVE_CARD) {
        /* Transition to REF_BOOKING with appropriate card animation */
        _refState = REF_BOOKING;
        if (_whichCard == CARD_RED) {
            InitRefereeAnimationTable(_refRedCardAnimTable);
            DOS_SOUND(DOS_EV_RED_CARD, 0);
        } else if (_whichCard == CARD_SECOND_YELLOW) {
            InitRefereeAnimationTable(_refSecondYellowAnimTable);
            DOS_SOUND(DOS_EV_RED_CARD, 0);
        } else {
            InitRefereeAnimationTable(_refYellowCardAnimTable);
            DOS_SOUND(DOS_EV_YELLOW_CARD, 0);
        }
        return;
    }

    if (_refState == REF_INCOMING) {
        goto move_referee;
    }

    if (_refState != REF_LEAVING) {
        return;
    }

    /* REF_LEAVING: if off-screen, hide and deactivate */
    if (a0->onScreen == 0) {
        _refState   = 0;
        a0->visible = 0;
        InitDisplaySprites();
        return;
    }

    /* ---- Move referee toward destination ---- */
move_referee:;
    a0->speed = (int16_t)_kRefereeSpeed;
    uint16_t savedDir = (uint16_t)a0->direction;

    int16_t destX = a0->destX;
    int16_t destY = a0->destY;
    int16_t curX  = a0->x.s.i;
    int16_t curY  = a0->y.s.i;
    int16_t speed = a0->speed;

    int32_t outDeltaX, outDeltaY;
    uint16_t outDir = CalculateDeltaXAndY(destX, destY, curX, curY, speed,
                                            &outDeltaX, &outDeltaY);

    a0->deltaX.raw = outDeltaX;
    a0->deltaY.raw = outDeltaY;

    /* Convert direction: (d0 + 16) & 0xFF >> 5 → 0..7 */
    uint16_t newDir = ((outDir + 16) & 0xFF) >> 5;
    a0->direction = (int16_t)newDir;

    if (newDir != savedDir) {
        InitRefereeAnimationTable(_refComingAnimTable);
    }

    /* ---- Clamp X toward destX ---- */
    {
        int32_t dx = a0->deltaX.raw;
        if (dx != 0) {
            a0->x.raw += dx;     /* add.l d0,SPRITE_X(a0) — adds to full 32-bit (int:frac) */
            if (dx < 0) {
                if (a0->x.s.i <= destX) {
                    a0->x.s.i      = destX;
                    a0->deltaX.raw = 0;
                }
            } else {
                if (a0->x.s.i >= destX) {
                    a0->x.s.i      = destX;
                    a0->deltaX.raw = 0;
                }
            }
        }
    }

    /* ---- Clamp Y toward destY ---- */
    {
        int32_t dy = a0->deltaY.raw;
        if (dy != 0) {
            a0->y.raw += dy;
            if (dy < 0) {
                if (a0->y.s.i <= destY) {
                    a0->y.s.i      = destY;
                    a0->deltaY.raw = 0;
                }
            } else {
                if (a0->y.s.i >= destY) {
                    a0->y.s.i      = destY;
                    a0->deltaY.raw = 0;
                }
            }
        }
    }

    /* ---- Check if destination reached → transition to REF_WAITING_PLAYER ---- */
    if (a0->deltaX.raw != 0 || a0->deltaY.raw != 0)
        return;

    _refState      = REF_WAITING_PLAYER;
    a0->direction = 6;
    InitRefereeAnimationTable(_refWaitingAnimTable);
    return;

    /* ---- Off-screen position update (keep moving even when off-screen) ---- */
off_screen_update:
    if (_gameStatePl != ST_GAME_IN_PROGRESS)
        goto state_dispatch;

    /* Clamp X */
    {
        int16_t dstX = a0->destX;
        int32_t dx = a0->deltaX.raw;
        if (dx != 0) {
            a0->x.raw += dx;
            if (dx < 0) {
                if (a0->x.s.i <= dstX) {
                    a0->x.s.i      = dstX;
                    a0->deltaX.raw = 0;
                }
            } else {
                if (a0->x.s.i >= dstX) {
                    a0->x.s.i      = dstX;
                    a0->deltaX.raw = 0;
                }
            }
        }
    }

    /* Clamp Y */
    {
        int16_t dstY = a0->destY;
        int32_t dy = a0->deltaY.raw;
        if (dy != 0) {
            a0->y.raw += dy;
            if (dy < 0) {
                if (a0->y.s.i <= dstY) {
                    a0->y.s.i      = dstY;
                    a0->deltaY.raw = 0;
                }
            } else {
                if (a0->y.s.i >= dstY) {
                    a0->y.s.i      = dstY;
                    a0->deltaY.raw = 0;
                }
            }
        }
    }
    return;
}

/* ================================================================
 * UpdateBookedPlayerNumberSprite  (SWOS2.s line 11127)
 *
 * Shows card or shirt number sprite above booked player during
 * booking animation. Uses time-sliced lookup tables to alternate
 * between card display and shirt number display.
 *
 * Table values: 0=hide, CARD_YELLOW(1)=yellow card, CARD_RED(2)=red card,
 *               other positive=show shirt number, -1=sequence end
 * ================================================================ */
__attribute__((used, externally_visible))
void UpdateBookedPlayerNumberSprite(void)
{
    Sprite *a0 = &_bookedPlayerCardOrNumberSprite;
    a0->imageIndex = (int16_t)0xFFFF;  /* default: hide */

    if (_whichCard == 0)
        return;
    if (_refState != REF_BOOKING)
        return;

    Sprite *playerSprite = _bookedPlayer;
    if (playerSprite->playerState != PL_BOOKED)
        return;

    /* Advance timer */
    _refTimer += _lastFrameTicks;
    uint16_t tableIdx = _refTimer >> 3;

    /* Select card display table.
     *
     * The second-yellow table is where the two releases part company, and it
     * is a bug in the DOS build rather than a choice. The Amiga's is 46 bytes
     * and terminates properly:
     *
     *      secondYellowTable:                          ; SWOS2.s:16637
     *              dc.b 0,9,0,9,0,9,0,9,0,9,0,9,0,0,0,0
     *              dc.b 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
     *              dc.b 0,0,0,0,0,0,0,0,0,0,0,0,-1,0    ; -1 at index 44
     *
     * The DOS one is 27 bytes and has NO terminator at all — 23 on one line
     * and 4 on the next (swos.asm:245189) — so `secondYellowTable[27]` reads
     * straight into `unusedPalTable01`, which the assembler places next.
     * The yellow and red tables are 30 bytes with their -1 at index 29 in
     * BOTH builds; only this one is short.
     *
     * That overrun is visible behaviour, and zkz-10174427 shows it frame for
     * frame. `refTimer >> 3` indexes the table, the booking commits at f21711:
     *
     *      indices 0..11   refTimer   0..95    0/9 -> the number blinks 8 on, 8 off
     *      indices 12..26  refTimer  96..215   zeros -> hidden
     *      index 27        refTimer 216..223   96 -> SHOWN   <- past the end
     *      index 28        refTimer 224..231    3 -> SHOWN
     *      index 29        refTimer 232        153, and as a SIGNED byte that
     *                                          is -103 -> the negative
     *                                          sentinel: the referee leaves
     *                                          and the player is sent off
     *
     * and the recording's `sprite[29].image_index` turns on at f21927 and off
     * at f21943, which is refTimer 216 and 232 exactly. The Amiga instead
     * stays hidden until its real -1 at index 44, refTimer 352 — so a DOS
     * second yellow ends 120 frames sooner than an Amiga one.
     *
     * Reproduced by giving the DOS path its own array: the 27 real bytes
     * followed by the palette bytes that actually sit behind them. */
    static const int8_t k_dosSecondYellowTable[46] = {
        0, 9, 0, 9, 0, 9, 0, 9, 0, 9, 0, 9,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   /* ends here: 27 bytes */
        /* unusedPalTable01 (swos.asm:245192), read as signed bytes */
        (int8_t)96,  (int8_t)3,   (int8_t)153, (int8_t)9,   (int8_t)255,
        (int8_t)15,  0,           0,           (int8_t)33,  (int8_t)7,
        (int8_t)64,  (int8_t)10,  (int8_t)113, (int8_t)15,  (int8_t)80,
        (int8_t)2,   (int8_t)48,  0,           (int8_t)112
    };

    const int8_t *table;
    if (_whichCard == CARD_YELLOW) {
        table = _yellowCardTable;
    } else if (_whichCard == CARD_RED) {
        table = _redCardTable;
    } else {
        table = _gDosGameplay ? k_dosSecondYellowTable : _secondYellowTable;
    }

    int8_t entry = table[tableIdx];

    if (entry == CARD_YELLOW) {
        /* Show yellow card image */
        a0->imageIndex = SPR_YELLOW_CARD;
        playerSprite = _bookedPlayer;
    } else if (entry == CARD_RED) {
        /* Show red card image */
        a0->imageIndex = SPR_RED_CARD;
        playerSprite = _bookedPlayer;
    } else if (entry < 0) {
        /* -1 sentinel: sequence done, referee leaves */
        SetRefereeToLeavingState();

        /* bit 1 of whichCard = red card → send player off */
        if (_whichCard & 2) {
            SendPlayerAway((uint8_t *)_bookedPlayer, (uint8_t *)_lastTeamBooked);
        }

        _whichCard    = 0;
        _bookedPlayer = 0;
        return;
    } else if (entry == 0) {
        /* 0 = show nothing */
        return;
    } else {
        /* Positive non-card value: show shirt number */
        playerSprite = _bookedPlayer;
        TeamGeneralInfo *teamInfo  = _lastTeamBooked;
        TeamGame        *inGameTeam = teamInfo->inGameTeamPtr;
        uint16_t ordinal = (uint16_t)playerSprite->playerOrdinal;
        int8_t shirtNum = inGameTeam->players[ordinal - 1].shirtNumber;
        int16_t sprIdx  = ShirtNumber_Sprite(shirtNum);     /* (1..99: shirt_numbers.c) */
        a0->imageIndex  = sprIdx;
    }

    /* Position card/number sprite above booked player */
    a0->x.s.i  = playerSprite->x.s.i;
    a0->z.s.i  = 20;
    a0->y.s.i  = playerSprite->y.s.i;
}
