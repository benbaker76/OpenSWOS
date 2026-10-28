/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "input.h"
#include "challenge_gen.h"

/* ---- Hardware register definitions ----
 * On the Amiga these point at the live Paula joystick + CIAA fire-button
 * registers. On the SDL build we redirect them to software state that
 * input_sdl.c writes once per SDL_PollEvent pass. */
#ifdef SWOS_SDL_BUILD
extern uint16_t _joy0Dat;
extern uint16_t _joy1Dat;
extern uint8_t  _ciaa_pra;
#define HW_JOY0DAT  _joy0Dat
#define HW_JOY1DAT  _joy1Dat
#define HW_CIAA_PRA _ciaa_pra
#else
#define HW_JOY0DAT  (*(volatile uint16_t *)0x00DFF00A)
#define HW_JOY1DAT  (*(volatile uint16_t *)0x00DFF00C)
#define HW_CIAA_PRA (*(volatile uint8_t  *)0x00BFE001)
#endif

/* ---- External globals (all live in SWOS2.s) ---- */
extern uint8_t _pl1Right;
extern uint8_t _pl1Left;
extern uint8_t _pl1Down;
extern uint8_t _pl1Up;
extern uint8_t _pl1Fire;
extern uint8_t _pl1UpRight;
extern uint8_t _pl1DownRight;
extern uint8_t _pl1DownLeft;
extern uint8_t _pl1UpLeft;
extern uint8_t _pl1LastFired;
extern uint8_t _pl1ShortFire;
extern uint16_t _pl1FireCounter;
extern uint8_t _pl1LeftPress;
extern uint8_t _pl1WasLeft;
extern uint8_t _pl1RightPress;
extern uint8_t _pl1WasRight;
extern uint8_t _pl1UpPress;
extern uint8_t _pl1WasUp;
extern uint8_t _pl1DownPress;
extern uint8_t _pl1WasDown;
extern uint16_t _pl1Direction;

/* ================================================================
 * Player1StatusProc  (SWOS2.s line 5562)
 *
 * Reads Amiga joystick port 1 hardware registers (JOY1DAT, CIAA_PRA)
 * and decodes them into direction flags, diagonal combinations,
 * press/release tracking state, and a compass direction value.
 *
 * JOY1DAT decoding (active-low inverted by sne/seq):
 *   bit 1  -> right
 *   bit 9  -> left
 *   bit 0 (after XOR with bit>>1) -> down
 *   bit 8 (after XOR with bit>>1) -> up
 *
 * CIAA_PRA bit 7 -> fire button (active low, seq inverts)
 *
 * Direction compass values:
 *   0=Up, 1=UpRight, 2=Right, 3=DownRight,
 *   4=Down, 5=DownLeft, 6=Left, 7=UpLeft,
 *   0xFFFF=no direction
 * ================================================================ */
__attribute__((used, externally_visible))
void Player1StatusProc(void)
{
    uint16_t joy, decoded;

    /* ---- Read JOY1DAT and decode directions ---- */
    joy = HW_JOY1DAT;

    /* sne: set 0xFF if bit set, 0x00 otherwise */
    _pl1Right = (joy & (1 << 1)) ? 0xFF : 0x00;
    _pl1Left  = (joy & (1 << 9)) ? 0xFF : 0x00;

    /* XOR trick for vertical: d1 = d0 >> 1; d0 ^= d1 */
    decoded = joy ^ (joy >> 1);
    _pl1Down = (decoded & (1 << 0)) ? 0xFF : 0x00;
    _pl1Up   = (decoded & (1 << 8)) ? 0xFF : 0x00;

    /* Fire button: CIAA_PRA bit 7, active low (seq = set if equal/zero) */
    _pl1Fire = (HW_CIAA_PRA & (1 << 7)) ? 0x00 : 0xFF;

#ifndef SWOS_SDL_BUILD
    if (ChallengeGen_Armed()) {
        _pl1Fire = ChallengeGen_Fire() ? 0xFF : 0;
        _pl1Right = _pl1Left = _pl1Down = _pl1Up = 0;
    }
#endif

    /* ---- Compute diagonal combinations ---- */
    _pl1UpRight  = _pl1Up   & _pl1Right;
    _pl1DownRight = _pl1Down & _pl1Right;
    _pl1DownLeft  = _pl1Down & _pl1Left;
    _pl1UpLeft    = _pl1Up   & _pl1Left;

    /* ---- Fire button press/release tracking ---- */
    if (_pl1LastFired != 0)
    {
        /* Was previously pressed */
        _pl1ShortFire = 0;
        if (_pl1Fire == 0)
        {
            /* Released now */
            _pl1LastFired = 0;
            _pl1FireCounter = (uint16_t)(-(int16_t)_pl1FireCounter);
        }
        else
        {
            /* Still held */
            if (_pl1FireCounter != 0)
            {
                _pl1FireCounter--;
            }
        }
    }
    else
    {
        /* Was not previously pressed */
        _pl1ShortFire = _pl1Fire;
        _pl1LastFired = _pl1Fire;
        if (_pl1Fire != 0)
        {
            _pl1FireCounter = 0xFFFF;
        }
    }

    /* ---- Left press/release tracking ---- */
    if (_pl1WasLeft != 0)
    {
        _pl1LeftPress = 0;
        if (_pl1Left == 0)
            _pl1WasLeft = 0;
    }
    if (_pl1WasLeft == 0)
    {
        _pl1LeftPress = _pl1Left;
        _pl1WasLeft = _pl1Left;
    }

    /* ---- Right press/release tracking ---- */
    if (_pl1WasRight != 0)
    {
        _pl1RightPress = 0;
        if (_pl1Right == 0)
            _pl1WasRight = 0;
    }
    if (_pl1WasRight == 0)
    {
        _pl1RightPress = _pl1Right;
        _pl1WasRight = _pl1Right;
    }

    /* ---- Up press/release tracking ---- */
    if (_pl1WasUp != 0)
    {
        _pl1UpPress = 0;
        if (_pl1Up == 0)
            _pl1WasUp = 0;
    }
    if (_pl1WasUp == 0)
    {
        _pl1UpPress = _pl1Up;
        _pl1WasUp = _pl1Up;
    }

    /* ---- Down press/release tracking ---- */
    if (_pl1WasDown != 0)
    {
        _pl1DownPress = 0;
        if (_pl1Down == 0)
            _pl1WasDown = 0;
    }
    if (_pl1WasDown == 0)
    {
        _pl1DownPress = _pl1Down;
        _pl1WasDown = _pl1Down;
    }

    /* ---- Compute compass direction ---- */
    {
        uint16_t dir;

        if (_pl1UpRight)
            dir = 1;
        else if (_pl1DownRight)
            dir = 3;
        else if (_pl1DownLeft)
            dir = 5;
        else if (_pl1UpLeft)
            dir = 7;
        else if (_pl1Up)
            dir = 0;
        else if (_pl1Right)
            dir = 2;
        else if (_pl1Down)
            dir = 4;
        else if (_pl1Left)
            dir = 6;
        else
            dir = 0xFFFF;

        _pl1Direction = dir;
    }
}

/* ---- Player 2 globals (all live in SWOS2.s) ---- */
extern uint8_t _pl2Right;
extern uint8_t _pl2Left;
extern uint8_t _pl2Down;
extern uint8_t _pl2Up;
extern uint8_t _pl2Fire;
extern uint8_t _pl2UpRight;
extern uint8_t _pl2DownRight;
extern uint8_t _pl2DownLeft;
extern uint8_t _pl2UpLeft;
extern uint8_t _pl2LastFired;
extern uint8_t _pl2ShortFire;
extern uint16_t _pl2FireCounter;
extern uint8_t _pl2LeftPress;
extern uint8_t _pl2WasLeft;
extern uint8_t _pl2RightPress;
extern uint8_t _pl2WasRight;
extern uint8_t _pl2UpPress;
extern uint8_t _pl2WasUp;
extern uint8_t _pl2DownPress;
extern uint8_t _pl2WasDown;
extern uint16_t _pl2Direction;

/* ================================================================
 * Player2StatusProc  (SWOS2.s line 5580)
 *
 * Reads Amiga joystick port 0 hardware registers (JOY0DAT, CIAA_PRA)
 * and decodes them into direction flags, diagonal combinations,
 * press/release tracking state, and a compass direction value.
 *
 * Identical logic to Player1StatusProc but reads JOY0DAT (port 0)
 * and CIAA_PRA bit 6 (second fire button), writing to pl2* globals.
 * ================================================================ */
__attribute__((used, externally_visible))
void Player2StatusProc(void)
{
    uint16_t joy, decoded;

    /* ---- Read JOY0DAT and decode directions ---- */
    joy = HW_JOY0DAT;

    _pl2Right = (joy & (1 << 1)) ? 0xFF : 0x00;
    _pl2Left  = (joy & (1 << 9)) ? 0xFF : 0x00;

    decoded = joy ^ (joy >> 1);
    _pl2Down = (decoded & (1 << 0)) ? 0xFF : 0x00;
    _pl2Up   = (decoded & (1 << 8)) ? 0xFF : 0x00;

    /* Fire button: CIAA_PRA bit 6, active low */
    _pl2Fire = (HW_CIAA_PRA & (1 << 6)) ? 0x00 : 0xFF;

    /* ---- Compute diagonal combinations ---- */
    _pl2UpRight   = _pl2Up   & _pl2Right;
    _pl2DownRight = _pl2Down & _pl2Right;
    _pl2DownLeft  = _pl2Down & _pl2Left;
    _pl2UpLeft    = _pl2Up   & _pl2Left;

    /* ---- Fire button press/release tracking ---- */
    if (_pl2LastFired != 0)
    {
        _pl2ShortFire = 0;
        if (_pl2Fire == 0)
        {
            _pl2LastFired = 0;
            _pl2FireCounter = (uint16_t)(-(int16_t)_pl2FireCounter);
        }
        else
        {
            if (_pl2FireCounter != 0)
            {
                _pl2FireCounter--;
            }
        }
    }
    else
    {
        _pl2ShortFire = _pl2Fire;
        _pl2LastFired = _pl2Fire;
        if (_pl2Fire != 0)
        {
            _pl2FireCounter = 0xFFFF;
        }
    }

    /* ---- Left press/release tracking ---- */
    if (_pl2WasLeft != 0)
    {
        _pl2LeftPress = 0;
        if (_pl2Left == 0)
            _pl2WasLeft = 0;
    }
    if (_pl2WasLeft == 0)
    {
        _pl2LeftPress = _pl2Left;
        _pl2WasLeft = _pl2Left;
    }

    /* ---- Right press/release tracking ---- */
    if (_pl2WasRight != 0)
    {
        _pl2RightPress = 0;
        if (_pl2Right == 0)
            _pl2WasRight = 0;
    }
    if (_pl2WasRight == 0)
    {
        _pl2RightPress = _pl2Right;
        _pl2WasRight = _pl2Right;
    }

    /* ---- Up press/release tracking ---- */
    if (_pl2WasUp != 0)
    {
        _pl2UpPress = 0;
        if (_pl2Up == 0)
            _pl2WasUp = 0;
    }
    if (_pl2WasUp == 0)
    {
        _pl2UpPress = _pl2Up;
        _pl2WasUp = _pl2Up;
    }

    /* ---- Down press/release tracking ---- */
    if (_pl2WasDown != 0)
    {
        _pl2DownPress = 0;
        if (_pl2Down == 0)
            _pl2WasDown = 0;
    }
    if (_pl2WasDown == 0)
    {
        _pl2DownPress = _pl2Down;
        _pl2WasDown = _pl2Down;
    }

    /* ---- Compute compass direction ---- */
    {
        uint16_t dir;

        if (_pl2UpRight)
            dir = 1;
        else if (_pl2DownRight)
            dir = 3;
        else if (_pl2DownLeft)
            dir = 5;
        else if (_pl2UpLeft)
            dir = 7;
        else if (_pl2Up)
            dir = 0;
        else if (_pl2Right)
            dir = 2;
        else if (_pl2Down)
            dir = 4;
        else if (_pl2Left)
            dir = 6;
        else
            dir = 0xFFFF;

        _pl2Direction = dir;
    }
}
