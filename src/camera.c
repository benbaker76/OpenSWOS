/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "camera.h"
#include "game_types.h"
#include "scroll_blit.h"
#include "display.h"      /* _screen_width — widescreen pan-range clamp in MoveView */

/* ---- External globals ---- */
extern FixedPoint _cameraX;
extern FixedPoint _cameraY;
extern uint16_t _cameraXMod320;
extern uint16_t _cameraYMod320;
extern uint16_t _cameraCoordinatesValid;

/* ---- External C functions ---- */
extern void SetCameraCoordinates(int16_t targetX, int16_t targetY);

#ifdef SWOS_SDL_BUILD
/* The render origin — see display.h. `_cameraX` is the simulation camera, with
 * the constant range [0, 0x2A0-336]; the render may only pan [0, 0x2A0-W], which
 * on a wide view is much smaller. SCALE the one onto the other rather than
 * centring-and-clamping.
 *
 * Centre-and-clamp (`clamp(_cameraX - (W-336)/2, 0, 0x2A0-W)`) gave a view that
 * was PINNED for most of the camera's travel and then tracked 1:1 through a
 * narrow live window before stopping dead against the clamp:
 *
 *     W=336  live window 336 of 336 (100%)   — 4:3 and Amiga
 *     W=484  live window 188 of 336 ( 56%)
 *     W=650  live window  22 of 336 (6.5%)   — 3440x1440
 *     W=672  live window   0                 — view never moves
 *
 * At 650 the picture sat still for 94% of the camera's range, then raced 22 px
 * at the FULL simulation-camera speed (a long ball moves it 5 px in one frame)
 * and halted. That is the tester's "camera jerking around when it's finally
 * triggered... moving quickly to follow then hitting the edge of the pitch".
 *
 * Scaling spreads the same pan over the whole match: the render camera moves at
 * max/336 of the simulation camera's speed (0.33 px/frame at 650 instead of 5),
 * reaches each end exactly when the simulation camera does, and never hits a
 * hard stop. The edges are still exact, so the view cannot leave the pitch.
 *
 * Folds to the IDENTITY at 336 (max == pan), so 4:3 displays, the Amiga and the
 * replay corpus are bit-for-bit unchanged. Render-only: `_cameraX` — what the
 * cull and player selection read — is untouched (see the two-camera split).
 *
 * 16.16 because the presenter's sub-pixel shift needs the fraction: scaling
 * makes the render camera move by well under a pixel per frame, so the
 * remainder is now most of the motion rather than a rounding detail. */
static int32_t viewOriginX16(int32_t width)
{
    const int32_t pan = 0x2A0 - SCREEN_WIDTH;          /* 336 — the sim range */
    int32_t max = (int32_t)(0x2A0 - width);            /* how far we may pan */

    if (max <= 0)   return 0;                          /* whole pitch visible */
    if (max >= pan) return _cameraX.raw;               /* 336 wide: identity */

    int32_t x = (int32_t)(((int64_t)_cameraX.raw * max) / pan);
    if (x < 0) x = 0;
    else if (x > (max << 16)) x = max << 16;
    return x;
}

int32_t Render_CameraX16(void)
{
    return viewOriginX16(_screen_width);
}

int16_t Render_CameraX(void)
{
    return (int16_t)(Render_CameraX16() >> 16);
}

/* The vertical render origin: the same idea as X, for a picture TALLER than the view
 * the match is simulated in -- the DOS view with widescreen on, which simulates the PC
 * release's 320x200 and shows 272 lines of it (docs/WIDESCREEN.md, "The DOS view"). The
 * simulation camera ranges [0, 880 - simH]; the picture may only range [0, 880 - H]. So
 * the one is SCALED onto the other: the picture reaches the top and bottom of the pitch
 * exactly when the simulation camera does, and never shows past the stadium (which a
 * 272-line picture at the DOS camera's 664 did). Identity whenever the picture is no
 * taller than the simulation view -- the Amiga view, and DOS at 320x200 -- so those are
 * unchanged to the pixel. Render-only: `_cameraY`, what the game reads, is untouched. */
static int32_t viewOriginY16(int32_t height)
{
    const int32_t pan = 880 - _sim_view_height;        /* the simulation's range */
    int32_t max = 880 - height;                         /* how far the picture may pan */

    if (max <= 0)   return 0;
    if (max >= pan) return _cameraY.raw;                /* not taller: identity */

    int32_t y = (int32_t)(((int64_t)_cameraY.raw * max) / pan);
    if (y < 0) y = 0;
    else if (y > (max << 16)) y = max << 16;
    return y;
}

int32_t Render_CameraY16(void)
{
    return viewOriginY16(_screen_height);
}

int16_t Render_CameraY(void)
{
    return (int16_t)(Render_CameraY16() >> 16);
}

/* A picture origin back to the simulation camera that maps onto it: the inverse, for
 * a recorded DRAWN camera (the in-match instant replay's) handed back to _cameraY. */
int16_t Render_SimCameraYFromRender(int16_t renderY)
{
    const int32_t pan = 880 - _sim_view_height;
    int32_t max = 880 - _screen_height;
    int32_t y;
    if (max <= 0)        y = 0;
    else if (max >= pan) y = renderY;
    else                 y = ((int32_t)renderY * pan + max / 2) / max;
    if (y < 0) y = 0;
    else if (y > pan) y = pan;
    return (int16_t)y;
}

/* The same origin for the SIMULATION draw width (display.h): where a view of that
 * width would draw from. It is what the match's draw-dependent decisions measure from,
 * so two peers with different displays measure from the same place. A wider view's
 * window contains every narrower one's, so the wider width sees at least what each
 * peer draws. Identical to Render_CameraX() whenever the two widths are. */
int16_t Sim_DrawCameraX(void)
{
    return (int16_t)(viewOriginX16(_sim_draw_width) >> 16);
}

/* The HEIGHT that goes with the simulation draw width, and its origin. A picture wider
 * than the simulation view is, in the DOS view, also TALLER than it: widescreen there
 * shows SCREEN_HEIGHT lines of a match simulated in 200 (main.c, WIDESCREEN.md "The DOS
 * view"). So whoever animates the side margin must animate the rows above and below
 * the 200 as well, or the men drawn there hold their last pose -- lying on the ground,
 * mid-tackle (ReportBot 0018). Derived from the simulation draw width, never from this
 * display's own height: two netplay peers agree that width, so they agree this, and a
 * peer whose own picture is 200 lines animates rows it does not show. The simulation
 * view's own height whenever the draw width is the view's (the rig, a 4:3 peer pair,
 * every Amiga-view match), where nothing changes. */
int16_t Sim_DrawHeight(void)
{
    if (_sim_draw_width > SCREEN_WIDTH && _sim_view_height < SCREEN_HEIGHT)
        return SCREEN_HEIGHT;
    return _sim_view_height;
}

int16_t Sim_DrawCameraY(void)
{
    return (int16_t)(viewOriginY16(Sim_DrawHeight()) >> 16);
}
#endif

/* ---- Sub-pixel scroll residual -------------------------------------------
 * `_cameraX/_cameraY` are 16.16, but everything is DRAWN at the integer part,
 * so a true camera velocity of e.g. 1.5 px/frame renders as an alternating
 * 1,2,1,2 px step. Measured on the corpus: 38% of consecutive moving frames
 * change step size, and a long ball peaks at 5 px in one 20 ms frame. That
 * quantisation is what testers describe as "not 100% fluid" — it is NOT a
 * frame-pacing problem and no vsync setting touches it.
 *
 * We publish the residual (true position - drawn position) so the presenter
 * can shift the finished frame by that fraction. Everything on screen is drawn
 * relative to the camera, so one uniform shift is correct for the whole scene.
 *
 * Published as the raw 16.16 fraction, not a float: this file is shared with
 * the m68k build and the fixed-width-int rule applies. platform_sdl.c does the
 * float maths, and only it knows the output scale.
 *
 * A TIMESTAMP, not a consume-and-clear flag: several presents can happen per
 * ScrollToCurrent (waitVBlankSync spins, the timer-thread fallback), and
 * zeroing on the first one would reintroduce exactly the 1 px jitter this
 * removes. Menus never publish, so they decay to zero and present unshifted. */
#ifdef SWOS_SDL_BUILD
static uint16_t s_scroll_frac_x, s_scroll_frac_y;
static uint64_t s_scroll_frac_ms;

void Render_PublishScrollSubpixel(void)
{
    extern uint64_t Platform_NowMs(void);

    /* The residual is the fraction of the RENDER camera, which since the
     * proportional map is no longer the fraction of `_cameraX` — at 650 wide
     * the render camera advances ~1/15 as fast, so its fraction IS most of the
     * motion. Take both the drawn integer and the remainder from the same
     * 16.16 value so they cannot disagree.
     *
     * No clamp-mirroring any more: the map only saturates at the two ends of
     * the pitch, where the value stops changing and the fraction is a constant
     * sub-pixel offset rather than a stale smear. */
    int32_t truex = Render_CameraX16();
    s_scroll_frac_x = (uint16_t)((uint32_t)truex & 0xFFFFu);

    /* Y the same way, from the vertical render origin (identity unless the picture is
     * taller than the simulation view). At the very top pitch.c pins `_cameraY.s.i` to 0
     * without clearing the fraction, so a pinned camera can carry a stale <1 px residual:
     * harmless, a constant sub-pixel offset that self-corrects once the camera moves. */
    s_scroll_frac_y = (uint16_t)((uint32_t)Render_CameraY16() & 0xFFFFu);

    s_scroll_frac_ms = Platform_NowMs();
}

/* Returns 0 (and leaves *fx/*fy at 0) unless a match frame published recently. */
int Render_ScrollSubpixel(uint16_t *fx, uint16_t *fy)
{
    extern uint64_t Platform_NowMs(void);
    *fx = 0; *fy = 0;
    if (!s_scroll_frac_ms) return 0;
    /* 100 ms ~= five 50 Hz frames. Long enough to survive a stall, short
     * enough that leaving the match drops the shift within a few menu frames. */
    if (Platform_NowMs() - s_scroll_frac_ms > 100) return 0;
    *fx = s_scroll_frac_x; *fy = s_scroll_frac_y;
    return 1;
}

/* Paused, the same frame is shown again and nothing publishes: keep its fraction, or the
 * picture would step by part of a pixel 100 ms after P (Platform_KeepFrameFresh). */
void Render_KeepScrollSubpixel(void)
{
    extern uint64_t Platform_NowMs(void);
    if (s_scroll_frac_ms)
        s_scroll_frac_ms = Platform_NowMs();
}
#endif

/* ================================================================
 * ScrollToCurrent  (SWOS2.s line 5625)
 *
 * Sets cameraCoordinatesValid = 1, then calls ScrollX and ScrollY
 * to blit pitch columns/rows matching the current camera position.
 * ================================================================ */
/* Set while MoveView/InitCameraMoveView is panning the camera to a target (match-setup
 * pitch pan, replay seek). During such a pan ScrollToCurrent is driven pixel-by-pixel and
 * expects plain incremental ScrollX+ScrollY — the left-edge full-refill below must NOT fire
 * (it rewrites _oldCameraX/_oldCameraY, derailing the pan and blacking the match on entry).
 * The seam fix is only needed for LIVE gameplay scrolling (game_loop's ScrollToCurrent). */
static uint8_t s_inCameraPan = 0;

__attribute__((used, externally_visible))
void ScrollToCurrent(void)
{
    _cameraCoordinatesValid = 1;
#ifdef SWOS_SDL_BUILD
    /* SDL: incremental scroll blits are no-ops (see scroll_blit.c).
     * Instead, redraw the whole visible pitch from the chunky tile
     * bank each call — matches swos-port-c's whole-screen chunky
     * pitch redraw shape. */
    extern void DrawChunkyPitch(void);
    DrawChunkyPitch();
    /* One publish site for the whole game: this is THE per-frame pitch scroll
     * (game_loop.c:693). Menus don't come through here, so they never get a
     * sub-pixel shift. */
    Render_PublishScrollSubpixel();
#else
    /* The FMODE left-verge "column strip" fix used to force a full-screen ScrollY refill here
     * (expensive: ~284k cycles/frame at the left edge, and it clobbered _oldCameraX/_oldCameraY).
     * It now lives in ScrollXUpdateColumnsAndBlit as a cheap margin-only ScrollYVergeRefresh()
     * — see scroll_blit.c. So ScrollToCurrent is just the plain incremental scroll again, and it
     * works in the rig too (the old fix was _playGame-gated and never ran there). */
    ScrollXUpdateColumnsAndBlit();
    ScrollYUpdateRowsAndBlit();
#endif
}

/* ================================================================
 * MoveView  (SWOS2.s line 5576)
 *
 * Iteratively moves cameraX.s.i toward newScreenX (one pixel per step,
 * with wrap), calling ScrollToCurrent each step. Once X matches,
 * does the same for cameraY.s.i toward newScreenY.
 *
 * 68k semantics: each increment/decrement adjusts both cameraX.s.i and
 * cameraXMod320 (wrapping at SCROLL_X_WRAP), and cameraY.s.i/cameraYMod320
 * (wrapping at 272).
 * ================================================================ */
__attribute__((used, externally_visible))
void MoveView(int16_t newScreenX, int16_t newScreenY)
{
    uint8_t savedPan = s_inCameraPan;
    s_inCameraPan = 1;   /* disable the live-gameplay seam refill during this pan */
    /* These clamps map a DOS-336 recorded replay camera into our wider view and
     * must run ONLY for loaded HIL/RPL playback (_playHighlightFlag). highlight.c
     * calls MoveView directly with the recorded X; the live in-game camera — and
     * the in-game instant replay, recorded in our own space — does its own
     * clamping (SetCameraCoordinates), so applying these here clamped the up-pan
     * to the subs bench, which legitimately leaves the playing-field Y range. */
    extern uint16_t _playHighlightFlag;
#ifdef SWOS_SDL_BUILD
    /* Not for a .SRP (src/replay_v2.c): it recorded our own simulation camera, which
     * needs no re-fitting -- and the replay rig simulates a DOS recording in a 200-line
     * view, whose camera runs past 0x260, so the clamp moved the picture. */
    extern int32_t ReplayV2_Loaded(void);
    if (_playHighlightFlag != 0 && !ReplayV2_Loaded()) {
#else
    if (_playHighlightFlag != 0) {
#endif
        /* X: 0x2A0 (pitch world width) - _screen_width is the max camera X — the
         * same clamp SetCameraCoordinates applies. No-op at 4:3 / on Amiga. */
        int16_t panRange = SIM_CAMERA_PAN_RANGE;
        if (panRange < 0) panRange = 0;
        if (newScreenX < 0) newScreenX = 0;
        else if (newScreenX > panRange) newScreenX = panRange;

        /* Y: the live camera's vertical range [0x10, 0x260]. A DOS HIL captured
         * in a 200px-tall view reaches ~0x295, which on our taller (272px)
         * playback view would pan past the bottom into the green surround.
         * This one stays keyed to the PLAYBACK view (0x260), not the
         * simulation view: it exists precisely to re-fit a foreign recording
         * to the window we are drawing in. */
        if (newScreenY < 0x10) newScreenY = 0x10;
        else if (newScreenY > 0x260) newScreenY = 0x260;
    }

    /* ---- Phase 1: move X ---- */
    /* Original asm uses do-while: ScrollToCurrent fires before the exit
     * check, so it always runs at least once and also runs at the final
     * target position.  This ensures the last tile boundary crossing is
     * blitted — critical for replay camera panning. */
    for (;;) {
        ScrollToCurrent();
        if (_cameraX.s.i == newScreenX)
            break;
        if (_cameraX.s.i < newScreenX) {
            /* Move right */
            _cameraX.s.i += 1;
            _cameraXMod320 += 1;
            if (_cameraXMod320 == SCROLL_X_WRAP)
                _cameraXMod320 = 0;
        } else {
            /* Move left */
            _cameraX.s.i -= 1;
            _cameraXMod320 -= 1;
            if ((int16_t)_cameraXMod320 < 0)
                _cameraXMod320 = SCROLL_X_WRAP - 1;
        }
    }

    /* ---- Phase 2: move Y ---- */
    for (;;) {
        ScrollToCurrent();
        if (_cameraY.s.i == newScreenY)
            break;
        if (_cameraY.s.i < newScreenY) {
            /* Move down */
            _cameraY.s.i += 1;
            _cameraYMod320 += 1;
            if (_cameraYMod320 == 272)
                _cameraYMod320 = 0;
        } else {
            /* Move up */
            _cameraY.s.i -= 1;
            _cameraYMod320 -= 1;
            if ((int16_t)_cameraYMod320 < 0)
                _cameraYMod320 = 271;
        }
    }
    s_inCameraPan = savedPan;
}

/* ================================================================
 * InitCameraMoveView  (SWOS2.s line 5557)
 *
 * Initializes camera by scrolling to two known positions (targetX,272)
 * and (cameraX.s.i,0), then calls MoveView to reach the actual target.
 *
 * Register semantics from asm:
 *   D1 = target X (passed by caller)
 *   D2 = target Y (saved, then restored for MoveView)
 * ================================================================ */
__attribute__((used, externally_visible))
void InitCameraMoveView(int16_t targetX, int16_t targetY)
{
    uint8_t savedPan = s_inCameraPan;
    s_inCameraPan = 1;   /* these setup ScrollToCurrent calls are a pan, not live scroll */

    /* First pass: set camera to (targetX, 272), scroll */
    SetCameraCoordinates(targetX, 272);
    ScrollToCurrent();

    /* Second pass: set camera to (cameraX.s.i, 0), scroll */
    SetCameraCoordinates(_cameraX.s.i, 0);
    ScrollToCurrent();

    /* Final: iteratively move to (cameraX.s.i, targetY) */
    MoveView(_cameraX.s.i, targetY);

    s_inCameraPan = savedPan;
}
