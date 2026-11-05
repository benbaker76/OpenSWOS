/*
 * esc_confirm.c -- ESC in a match asks first: QUIT THE MATCH? RESUME / QUIT (SDL only).
 *
 * The original ends the match on ESC at once. On a handheld ESC is a shoulder trigger (the
 * PortMaster port's L2), easily pressed by accident, and a friendly then drops to its
 * REPLAY / EXIT screen with the match gone. So the match holds, as for P, and two boxes are
 * drawn over it: RESUME (the default: fire, or P) carries on, QUIT (down and fire, or ESC
 * again) ends it as before.
 *
 * Drawn into the HUD layer (platform_sdl.c), which is cleared and redrawn every frame, so
 * nothing is left on the pitch; without that layer ([video] subpixel_scroll = false) ESC
 * quits at once, as the original did. Never online or under a rig: a loop that waits for
 * input inside a frame would hang a peer, and a recorded ESC has nobody to answer it.
 */
#include "swos_types.h"

#ifdef SWOS_SDL_BUILD

#include "display.h"
#include "blitter.h"        /* FillRectangle */
#include "text_render.h"    /* DrawMenuTextCentered */
#include "replay_menu.h"    /* DrawMenuBack150x20 */
#include "replay_test.h"    /* ReplayTest_Running */
#include "srp_resim.h"      /* SrpResim_Armed */
#include "rollback_rig.h"   /* Rollback_Enabled */

extern volatile uintptr_t _frontbufferBasePtr;
extern int _menu_draw_x_offset;
extern volatile uint16_t _fire;          /* (uint16_t[1] where defined, as everywhere) */
extern int16_t _menuControlsDirection;
extern volatile uint16_t _currentKey;
extern uint8_t _bigCharsTable[];
extern int _gArcadeMode;

extern uint8_t *Platform_HudLayerBuffer(void);
extern void     Platform_HudLayerMark(int x, int y, int w, int h);
extern void     Platform_KeepFrameFresh(void);
extern int      Platform_VBlank(void);
extern void     MenuCheckControls(void);
extern int32_t  NetplayMatch_Enabled(void);
extern int32_t  CareerAutopilot_Active(void);

int EscConfirm_Wanted(void)
{
    return Platform_HudLayerBuffer() != NULL && !NetplayMatch_Enabled() && !ReplayTest_Running() &&
           !CareerAutopilot_Active() && !SrpResim_Armed() && !Rollback_Enabled() && !_gArcadeMode;
}

static void draw(int quit, int blink)
{
    uint8_t *hud = Platform_HudLayerBuffer();
    const uintptr_t saved = _frontbufferBasePtr;
    const int savedX = _menu_draw_x_offset;
    const int16_t cx = (int16_t)(_screen_width / 2), bx = (int16_t)(cx - 75);
    _frontbufferBasePtr = (uintptr_t)hud;
    _menu_draw_x_offset = 0;
    Platform_HudLayerMark(bx - 4, 72, 158, 100);
    FillRectangle((uint16_t)(bx - 4), 72, 158, 100, 0);        /* (clear the last one) */
    DrawMenuBack150x20(11, (uint16_t)bx, 78);                  /* (blue: 2 is white, the text's) */
    DrawMenuTextCentered((uint16_t)cx, 84, 2, (uint8_t *)"QUIT THE MATCH?", _bigCharsTable);
    DrawMenuBack150x20(quit ? 12 : 14, (uint16_t)bx, 112);
    DrawMenuTextCentered((uint16_t)cx, 118, 2, (uint8_t *)"RESUME", _bigCharsTable);
    DrawMenuBack150x20(quit ? 14 : 12, (uint16_t)bx, 142);
    DrawMenuTextCentered((uint16_t)cx, 148, 2, (uint8_t *)"QUIT", _bigCharsTable);
    if (blink) {                                               /* the chosen one framed */
        const uint16_t y = quit ? 142 : 112, c = 15;
        FillRectangle((uint16_t)bx, y, 150, 1, c);
        FillRectangle((uint16_t)bx, (uint16_t)(y + 19), 150, 1, c);
        FillRectangle((uint16_t)bx, y, 1, 20, c);
        FillRectangle((uint16_t)(bx + 149), y, 1, 20, c);
    }
    _frontbufferBasePtr = saved;
    _menu_draw_x_offset = savedX;
}

/* One frame of the hold: the frame on screen shown again, the pad and keys read. */
static void hold(void)
{
    Platform_KeepFrameFresh();
    Platform_VBlank();
    MenuCheckControls();
}

/* 1: quit the match. 0: carry on (the fire that chose it let go first, so it is no kick). */
int EscConfirm_Ask(void)
{
    int quit = 0, frames = 0;
    do                      /* the ESC (or the trigger) that brought it up, let go */
        hold();
    while (_currentKey == 0x1B);
    for (;; frames++) {
        draw(quit, (frames / 8) & 1);
        hold();
        if (_currentKey == 0x1B) { quit = 1; break; }          /* ESC twice */
        if (_currentKey == 0x50) { quit = 0; break; }          /* P */
        if (_menuControlsDirection == 0) quit = 0;
        else if (_menuControlsDirection == 4) quit = 1;
        if (_fire) break;
    }
    if (!quit)
        do hold(); while (_fire);
    {   /* the boxes off the layer (the next frame's HUD reset clears only what it drew) */
        uint8_t *hud = Platform_HudLayerBuffer();
        const uintptr_t saved = _frontbufferBasePtr;
        const int savedX = _menu_draw_x_offset;
        _frontbufferBasePtr = (uintptr_t)hud;
        _menu_draw_x_offset = 0;
        FillRectangle((uint16_t)(_screen_width / 2 - 79), 72, 158, 100, 0);
        _frontbufferBasePtr = saved;
        _menu_draw_x_offset = savedX;
    }
    return quit;
}

#endif /* SWOS_SDL_BUILD */
