/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "replay_menu.h"
#include "menu_system.h"
#include "text_render.h"
#include "blitter.h"
#include "display.h"
#include "fade.h"

extern void WaitForVSync(void);   /* game_utils.c — pumps SDL events on SDL */

/* ---- External globals (all live in SWOS2.s) ---- */
extern volatile int16_t _waitingInterrupt;
extern volatile uint16_t _currentGameTick;
extern uint16_t _paletteIndex;
extern uint16_t _replaySelected;
extern uint16_t _menuControlsDirection;
extern volatile uint16_t _fire;

extern uint8_t _frontbufferMaskBuffer[];
extern uint8_t _bigCharsTable[];
extern uint16_t _kColorTableShine[];

/* String constants in SWOS2.s */
extern uint8_t _aGrafsMenubg2Ra[];   /* "grafs/menubg2.raw" */
extern uint8_t _aReplay[];           /* "REPLAY" */
extern uint8_t _aExit[];             /* "EXIT" */

/* ---- External functions ---- */
extern int16_t LoadFileEx(uint8_t *filename, uint8_t *dest, uint32_t *outFileSize);
extern void LoadOverlayToFrontbuffer(const uint8_t *path);

/* ================================================================
 * DrawMenuBack150x20  (SWOS2.s line 18456)
 *
 * Draws a 150x20 menu background box with outer frame.
 * ================================================================ */
__attribute__((used, externally_visible))
void DrawMenuBack150x20(uint16_t color, uint16_t x, uint16_t y)
{
    uint16_t w = 150;
    uint16_t h = 20;
    DrawMenuItemBackground(color, x, y, w, h);
    DrawMenuItemOuterFrame(color, x, y, w, h);
}

/* ================================================================
 * DrawReplayContinueShiningFrame  (SWOS2.s line 18513)
 *
 * Draws an animated shining frame around a 150x20 box.
 * Uses currentGameTick to cycle through kColorTableShine colors.
 *   D0 = x position, D1 = y position
 *
 * Frame consists of 4 FillRectangle calls:
 *   - Top horizontal line:    (x, y, 0x96, 1)
 *   - Bottom horizontal line: (x, y+0x13, 0x96, 1)
 *   - Left vertical line:     (x, y, 1, 0x14)
 *   - Right vertical line:    (x+0x95, y, 1, 0x14)
 * ================================================================ */
__attribute__((used, externally_visible))
void DrawReplayContinueShiningFrame(int16_t x, int16_t y)
{
    uint16_t idx;
    uint16_t color;

    /* Compute animated color from tick */
    idx = _currentGameTick >> 1;
    idx &= 7;
    color = _kColorTableShine[idx];

    /* Top horizontal line */
    FillRectangle((uint16_t)x, (uint16_t)y, 0x96, 1, color);
    /* Bottom horizontal line */
    FillRectangle((uint16_t)x, (uint16_t)(y + 0x13), 0x96, 1, color);
    /* Left vertical line */
    FillRectangle((uint16_t)x, (uint16_t)y, 1, 0x14, color);
    /* Right vertical line */
    FillRectangle((uint16_t)(x + 0x95), (uint16_t)y, 1, 0x14, color);
}

/* ================================================================
 * DrawReplayOrExitMenu  (SWOS2.s line 18412)
 *
 * Draws the "REPLAY" and "EXIT" menu boxes twice (double-buffered).
 * Each pass: draw two 150x20 backgrounds, then two centered text
 * labels, then waits for VBlank.
 * ================================================================ */
__attribute__((used, externally_visible))
void DrawReplayOrExitMenu(void)
{
    int16_t d7;

    for (d7 = 1; d7 >= 0; d7--)
    {
        /* REPLAY background (color 14, x=101, y=106) */
        DrawMenuBack150x20(14, 101, 106);
        /* EXIT background (color 12, x=101, y=136) */
        DrawMenuBack150x20(12, 101, 136);

        /* "REPLAY" text centered (x center=176, y=112, color index=2) */
        DrawMenuTextCentered(176, 112, 2, _aReplay, _bigCharsTable);
        /* "EXIT" text centered (x center=176, y=142, color index=2) */
        DrawMenuTextCentered(176, 142, 2, _aExit, _bigCharsTable);

        /* Wait for VBlank. Use WaitForVSync (which routes to
         * Platform_VBlank on SDL) instead of the raw waitingInterrupt
         * spin — Platform_VBlank pumps SDL events, so keyboard
         * up/down gets processed; without it the user couldn't move
         * the cursor on Windows because the only event-pump path was
         * the SDL_Timer thread's >60ms-since-render fallback, which
         * never fires from a tight menu loop. */
        WaitForVSync();
    }
}

/* ================================================================
 * ReplayExitMenuAfterFriendly  (SWOS2.s line 18349)
 *
 * Shows the post-friendly-match "REPLAY / EXIT" menu.
 * Loads background image, sets up display, then runs an event loop
 * handling up/down navigation and fire to select.
 * ================================================================ */
__attribute__((used, externally_visible))
void ReplayExitMenuAfterFriendly(void)
{
    /* Widescreen: centre the menu over the (centred) background. The match loop left
     * _menu_draw_x_offset = 0 (game_loop.c); the REPLAY/EXIT box, text and shining-frame
     * draws below all read it via FillRectangle / blitStringToFrontBuffer, so without this
     * they'd sit at the screen's left edge while menubg2 (which bakes WIDE_MARGIN into its
     * own frontbuffer position) is centred. Restored before returning. WIDE_MARGIN is 0 on
     * Amiga / non-widescreen, so this is a no-op there. */
    int saved_x_offset = _menu_draw_x_offset;
    _menu_draw_x_offset = WIDE_MARGIN;

    /* Load menu background image. On Amiga, LoadFileEx writes bitplane
     * data directly into frontbufferMaskBuffer (which is bitplane-format
     * there). On SDL, frontbufferMaskBuffer is chunky 8bpp, so we have to
     * route through LoadOverlayToFrontbuffer, which decompresses to a
     * staging buffer and decodes bitplane → chunky into the mask buffer.
     * The previous direct LoadFileEx call left the top of the buffer
     * holding raw bitplane bytes (visible as noise) and the bottom
     * holding stale memory (visible as the previous-frame stadium). */
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
    /* The match's last bobs are undrawn from both buffers FIRST: the mask buffer the
     * picture loads into is the second buffer's mirror half (swos_globals.c), where the
     * window of a match -- and so an undraw -- can reach. Two swaps leave the buffers and
     * the camera state as they were. */
    ProcessSpriteBlitQueue();
    SwapBuffersAndCameraViewState();
    ProcessSpriteBlitQueue();
    SwapBuffersAndCameraViewState();
    LoadOverlayToFrontbuffer(_aGrafsMenubg2Ra);
    BlitterCopyPanelToScreen();

    /* Reset camera and double-buffer swap */
    SetCameraCoordinates(0, 0);
    SwapBuffersAndCameraViewState();
    UpdatePlanePtrsAndBlitParams();
    BlitterCopyPanelToScreen();
#else
    LoadOverlayToFrontbuffer(_aGrafsMenubg2Ra);

    ProcessSpriteBlitQueue();
    BlitterCopyPanelToScreen();

    /* Reset camera and double-buffer swap */
    SetCameraCoordinates(0, 0);
    SwapBuffersAndCameraViewState();
    UpdatePlanePtrsAndBlitParams();
    ProcessSpriteBlitQueue();
    BlitterCopyPanelToScreen();
#endif
    SwapBuffersAndCameraViewState();
    UpdatePlanePtrsAndBlitParams();

    /* Initialize state */
    _replaySelected = 1;
    _paletteIndex = 0;

#ifdef SWOS_SDL_BUILD
    /* The boxes a mod shades (the PC menu shading: ModDef menuFill) are this screen's from here,
     * not the last menu's: a stale one would paint over the picture where its colour matches. */
    { extern void ModHooks_MenuDrawBegin(void); ModHooks_MenuDrawBegin(); }
#endif
    /* Draw initial menu and fade in */
    DrawReplayOrExitMenu();
    FadeIn();

    /* Main event loop.
     * Assembly has two entry points:
     *   replay_exit_loop_draw_menu     → full redraw then wait
     *   replay_exit_loop_wait_interrupt → skip redraw, just wait
     * We use needsRedraw flag to replicate this. */
    {
        int16_t needsRedraw = 1;

        for (;;)
        {
            if (needsRedraw)
            {
                DrawReplayOrExitMenu();
                needsRedraw = 0;
            }

            /* Wait for VBlank. Use WaitForVSync (which routes to
             * Platform_VBlank on SDL) instead of the raw
             * waitingInterrupt spin so SDL events get pumped on the
             * main thread — without this the user can't move the
             * cursor on Windows. Same fix pattern as the other
             * site in this file and the season-transition debounce
             * loops in commit b0e5326. */
            WaitForVSync();

            /* Draw shining frame on selected item */
            if (_replaySelected != 0)
                DrawReplayContinueShiningFrame(0x65, 0x6A);
            else
                DrawReplayContinueShiningFrame(0x65, 0x88);

            /* Process input */
            MenuCheckControls();

            if (_menuControlsDirection == 0)
            {
                /* No direction pressed */
                if (_replaySelected != 0)
                    continue;           /* on REPLAY, just wait */
                _replaySelected = 1;     /* snap back to REPLAY */
                needsRedraw = 1;
                continue;
            }

            if (_menuControlsDirection == 4)
            {
                /* Down pressed */
                if (_replaySelected == 0)
                    continue;           /* already on EXIT, just wait */
                _replaySelected = 0;     /* switch to EXIT */
                needsRedraw = 1;
                continue;
            }

            /* Other direction (up/left/right) — check fire */
            if (_fire != 0)
            {
                FadeOutToBlack();
                _menu_draw_x_offset = saved_x_offset;
                return;
            }
            /* No fire, just wait */
        }
    }
}
