/*
 * platform_sdl.c -- SDL2 host implementation.
 *
 * Owns the SDL window, renderer, framebuffer texture, and the per-frame
 * 8bpp-chunky → ARGB8888 palette lookup.
 *
 * Bitplane-to-chunky conversion happens at load time (see
 * `LoadOverlayToFrontbuffer` in frontend.c and `blitDispatch` in
 * image_cmd.c) so the per-frame work here is just one palette lookup
 * per pixel into the streaming SDL texture.
 */

#include <stdint.h>
#include "platform_sdl.h"
#include "crt_effects.h"   /* CRT mask / vignette / curved corners */
#include "display.h"
#include "dos_mode.h"        /* gDosGraphics */
#include "dos_gameplay.h"    /* DosGameplay_TargetFps — 70 (DOS) vs 50 (Amiga) */
#include "dos_graphics.h"    /* DosGfx_BuildPalette */
#include "build_info.h"      /* the commit, in the window title */

#include <SDL.h>
#include <stdio.h>
#ifdef SWOS_WASM_BUILD
#include <emscripten.h>      /* emscripten_sleep — the ASYNCIFY yield point */
#include <emscripten/html5.h>   /* emscripten_date_now */
#include <errno.h>
#include <time.h>
#endif
#include <stdlib.h>
#include <string.h>

#ifdef SWOS_WASM_BUILD
/* SDL_GetTicks has to be monotonic in the browser too. SDL asks clock_gettime for
 * CLOCK_MONOTONIC_RAW (4); Emscripten's accepts only the WASI clocks 0-3 and answers
 * EINVAL, so SDL silently fell back to gettimeofday -- the WALL clock, which steps (NTP,
 * sleep and wake; under WSL2, all the time). One step back made the unsigned "since the
 * last progress" of a live match wrap, and its "nothing confirmed for 10 s" watchdog
 * ended a match that had just finished (a browser host against a browser on the Mac, at
 * frame 600 of 600). So the browser's clock_gettime serves the raw monotonic clock from
 * the same source as the monotonic one -- as WASI does every clock but realtime. A strong
 * definition, over musl's weak alias, so SDL's prebuilt port gets it too. */
int clock_gettime(clockid_t clk, struct timespec *ts)
{
    double ms;
    if (!ts) {
        errno = EFAULT;
        return -1;
    }
    if (clk == CLOCK_REALTIME)
        ms = emscripten_date_now();
    else if (clk >= 0)
        ms = emscripten_get_now();
    else {
        errno = EINVAL;
        return -1;
    }
    ts->tv_sec  = (time_t)(ms / 1000.0);
    ts->tv_nsec = (long)((ms - (double)ts->tv_sec * 1000.0) * 1000000.0);
    return 0;
}
#endif

/* Desktops that take a window's icon from the program: Linux (X11, Wayland) and the like.
 * The Windows builds get SWOS.ico from their resource (SWOS.rc) and the macOS app its
 * SWOS.icns, both at every size; the web and Switch builds have no window icon. */
#if !defined(_WIN32) && !defined(__CYGWIN__) && !defined(__APPLE__) && \
    !defined(SWOS_WASM_BUILD) && !defined(SWOS_SWITCH_BUILD)
#define SWOS_SET_WINDOW_ICON 1
#include "swos_window_icon.h"   /* generated from SWOS.svg by scripts/generate_icons.py */
#endif

/* The window title names the build and the platform it was built for; both come
 * from build_info.c, which is the only place either is decided. This file used
 * to detect the architecture itself for the title -- two detectors that could
 * drift apart, for one string. */

/* ---- Forward decls from audio_sdl.c / input_sdl.c ----------------- */
int  AudioSDL_Init(int mono);
void AudioSDL_Shutdown(void);
void AudioSDL_Tick(void);
void AudioSDL_PauseAndClear(void);

int  InputSDL_Init(void);
void InputSDL_Shutdown(void);
void InputSDL_HandleEvent(const SDL_Event *e);
void InputSDL_PostFrame(void);
void InputSDL_ClearKeyState(void);

/* ---- Game-side globals we read each frame --------------------------
 * The displayed image lives in `frontbufferBase` (or `Alt` after a
 * swap) as 8bpp chunky pixels — 1 byte per pixel = palette index, row
 * stride = SCREEN_WIDTH. `frontbufferBasePtr` is the live pointer to
 * the buffer the game last drew into. */
extern volatile uintptr_t _frontbufferBasePtr;
extern volatile uintptr_t _frontbufferBaseAltPtr;
extern uint8_t  _frontbufferBase[];
extern uint8_t  _frontbufferBaseAlt[];
extern uint8_t  _frontbufferMaskBuffer[];
extern uint16_t _cameraXMod320;
extern uint16_t _cameraYMod320;
extern uint16_t _useAltBaseFlag;
extern uint16_t _usePlaneOffsetFlag;

/* The copperlist holds the live palette in Amiga 0xRGB nibbles. We
 * snapshot it into a 256-entry RGBA palette each frame. */
extern uint8_t _copperListBuffer[];

/* _quitToDos lives in platform_amiga_stubs.c */
extern uint16_t _quitToDos;

/* ---- SDL state ----------------------------------------------------- */
static SDL_Window   *s_window;
static SDL_Renderer *s_renderer;

#ifdef __ANDROID__
void Platform_AndroidTextWindow(int active)
{
    static Uint32 fullscreen;
    if (!s_window) return;
    if (active) {
        fullscreen = SDL_GetWindowFlags(s_window) & SDL_WINDOW_FULLSCREEN_DESKTOP;
        /* Android ignores adjustResize for fullscreen windows. Allow the
         * surface to shrink while typing so the edited field stays visible. */
        SDL_SetWindowFullscreen(s_window, 0);
    } else {
        SDL_SetWindowFullscreen(s_window, fullscreen);
    }
}
#endif
/* A small RING of streaming textures. We upload into a DIFFERENT one each frame so
 * we never lock/refill the texture the GPU is still sampling from a recent frame.
 * That per-frame resource contention is what stalls SDL's D3D11 texture-upload
 * path (~45 ms/frame) when the frames are too cheap to hide it (arcade replays);
 * separating the CPU's upload target from the GPU's read target keeps the pipeline
 * flowing. Ring depth 4 stays a frame clear of D3D11's default 3-frame latency
 * queue. Harmless on OpenGL (which never stalled). */
#define PLATFORM_TEX_RING 4
/* The streaming texture is TALLER than the screen. Rows [0, _screen_height)
 * are the frame; the rows below are a scratch strip carrying the HUD overlay,
 * so the HUD ships to the GPU inside the SAME upload as the world and is then
 * drawn from the SAME texture. See present_frame_copy: one upload, two draws.
 * Worst case the HUD covers the whole screen (the result/scoreboard overlay),
 * hence a full second screen's worth. */
#define PLATFORM_TEX_H  (PLATFORM_SCREEN_H * 2)
static SDL_Texture  *s_textures[PLATFORM_TEX_RING];  /* RGBA8888 streaming, _screen_width x 272 */
static int           s_tex_idx;
/* Cached-RAM staging buffer: ALL per-pixel work (8bpp→RGBA, overlay blend) happens
 * here, then one SDL_UpdateTexture uploads it. Writing/reading SDL_LockTexture's
 * mapped memory directly is ~100× slower on D3D11 (write-combined/uncached), which
 * made the render 43 ms/frame; normal RAM makes it ~sub-ms. Sized _screen_width×272. */
static uint32_t     *s_frame_rgba;

/* ---- HUD layer ------------------------------------------------------------
 * Screen-pinned sprites (the clock, the controlled-player name, team names,
 * score digits, the scoreboard grid) are positioned as `Render_CameraX() + K`
 * so DrawSprites subtracts the camera and they land at a fixed spot. They must
 * NOT ride the sub-pixel scroll shift, or they wobble by up to a source pixel
 * every frame — a tester saw the clock and player name flickering as soon as
 * subpixel_scroll went in.
 *
 * They cannot be compensated at their draw site: sprite coordinates are
 * integers and the shift is a fraction. So they are diverted into their own
 * 8bpp layer (sprites.c routes any sprite with z >= HUD_LAYER_Z_MIN here by
 * pointing _frontbufferBasePtr at it) and presented UNSHIFTED on top of the
 * shifted world.
 *
 * Only the dirty rect is converted and uploaded. The HUD is a small corner
 * block; a full second 336x272 upload every frame is exactly the kind of cost
 * that made the D3D path crawl before the single-bulk-upload rework. */
static int           s_subpixel_scroll = 1;   /* [video] subpixel_scroll — see present_frame_copy */

Uint64 Platform_NowMs(void);           /* defined below; used by the freshness stamp */
static uint8_t      *s_hud_layer;      /* 8bpp, same geometry as the frontbuffer */
static int           s_hud_x0, s_hud_y0, s_hud_x1, s_hud_y1;  /* x1/y1 exclusive */
/* Where this frame's HUD ended up in the texture's scratch strip, and where it
 * belongs on screen. w == 0 means "nothing to draw". Set by compose_hud_strip. */
static int           s_hud_strip_w, s_hud_strip_h, s_hud_dst_x, s_hud_dst_y;
static Uint64        s_hud_stamp;      /* freshness — menus never draw the HUD */

/* NULL when the layer is inactive: with subpixel_scroll off there is no shift
 * to escape, so the HUD stays in the main framebuffer exactly as before. */
uint8_t *Platform_HudLayerBuffer(void)
{
    return s_subpixel_scroll ? s_hud_layer : NULL;
}

/* Called once per frame before the sprites are drawn. Clears only what was
 * dirtied last frame. */
void Platform_HudLayerReset(void)
{
    if (!s_hud_layer) return;
    if (s_hud_x1 > s_hud_x0 && s_hud_y1 > s_hud_y0) {
        for (int y = s_hud_y0; y < s_hud_y1; y++)
            SDL_memset(s_hud_layer + (size_t)y * _screen_width + s_hud_x0, 0,
                       (size_t)(s_hud_x1 - s_hud_x0));
    }
    s_hud_x0 = s_hud_y0 = 0x7FFF;
    s_hud_x1 = s_hud_y1 = 0;
}

/* Union a drawn sprite into the dirty rect — its EXACT box.
 *
 * DrawSpriteClipped and DrawSprite_entry2 both hand (d1,d2) straight to
 * blitChunkySprite, which uses them as the top-left corner, so no padding is
 * needed. This used to pad by the sprite's size on every side "in case" the
 * coordinates were a hotspot: nine times the area, which for the wide
 * scoreboard-grid sprites (168 px) ballooned the rect to nearly the whole
 * screen and turned the overlay's small sub-rect upload into a near-full-frame
 * one every frame. That is exactly the per-frame GPU cost this layer was
 * designed to avoid. One pixel of slack absorbs any off-by-one in the blit's
 * own clipping. */
void Platform_HudLayerMark(int x, int y, int w, int h)
{
    if (!s_hud_layer || w <= 0 || h <= 0) return;
    int x0 = x - 1, y0 = y - 1, x1 = x + w + 1, y1 = y + h + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > _screen_width) x1 = _screen_width;
    if (y1 > _screen_height) y1 = _screen_height;
    if (x0 < s_hud_x0) s_hud_x0 = x0;
    if (y0 < s_hud_y0) s_hud_y0 = y0;
    if (x1 > s_hud_x1) s_hud_x1 = x1;
    if (y1 > s_hud_y1) s_hud_y1 = y1;
    s_hud_stamp = Platform_NowMs();
}

/* Is there a HUD layer worth presenting this frame? Same 100 ms freshness rule
 * as the scroll residual: menus never draw these sprites, so they decay out
 * instead of leaving a stale clock floating over the frontend. */
static int hud_layer_pending(void)
{
    return s_hud_layer && s_subpixel_scroll && s_hud_stamp &&
           (Platform_NowMs() - s_hud_stamp) <= 100 &&
           s_hud_x1 > s_hud_x0 && s_hud_y1 > s_hud_y0;
}

/* ---- Two-pass scaling -----------------------------------------------------
 * A fullscreen scale is almost never a whole number: 1080/272 = 3.97, so a
 * plain nearest copy gives some source pixels 4 output pixels and their
 * neighbours 3. That unevenness is what a tester sees as wobbly text, shirt
 * numbers and clock digits, and it has been there since the first commit --
 * SDL_RenderSetLogicalSize's scaling policy has never changed.
 *
 * The error is one SOURCE pixel wide, so the fix is to make the source denser
 * before the fractional part of the scale happens:
 *
 *   pass 1  frame -> s_scale_target at a WHOLE number N, NEAREST.  Every source
 *           pixel becomes an exact NxN block, so this pass adds no error.
 *   pass 2  s_scale_target -> the screen, LINEAR.  The leftover fraction
 *           (e.g. 3.97/4 = 0.99) is resampled from an already N-times-dense
 *           image, so the residual is a fraction of a source pixel instead of
 *           a whole one.
 *
 * swos-2020 does the same thing (src/video/render.cpp: the streaming texture is
 * created at kVgaWidth * getFilterScalerSize(), and getScreenEnhancment() flips
 * SDL_HINT_RENDER_SCALE_QUALITY to "best") -- but it runs the pre-scale on the
 * CPU into a streaming texture, which multiplies the per-frame UPLOAD by N².
 * That is the regression 3b1c76f just removed, so ours is a render target: the
 * upload is untouched and pass 1 is one GPU blit.
 *
 * No bars, no cropped rows, and it costs nothing at a whole-number scale --
 * scale_pass_factor() returns 0 there and the old single copy runs unchanged,
 * which is every windowed scale. */
static int           s_scaling_smooth = 1;  /* [video] scaling = smooth | sharp */
static SDL_Texture  *s_scale_target;        /* NULL until a fractional scale needs it */
static int           s_scale_n;             /* the N s_scale_target was built at */
static int           s_scale_targets_ok = 1;/* cleared if the backend can't render to a texture */
static int           s_scale_logged;        /* log the chosen factor once, for triage */
static SDL_Color     s_palette[256];
static SDL_mutex    *s_render_mtx;       /* serializes Platform_RenderFrame calls */

static Uint64        s_perf_freq;
static Uint64        s_next_frame_perf;  /* high-res counter for 50 Hz pacing */
/* How many frames late the limiter will still catch up on, by running frames back to
 * back, before it drops the rest (Platform_SetCatchUpFrames). */
static int           s_catchup_frames = 1;
static int           s_display_hz;       /* monitor refresh, 0 = unknown. See the pacing note in Platform_VBlank. */
static double        k_frame_secs = 1.0 / 50.0;  /* refreshed per frame from the
                                                  * active game style (DOS 70 / Amiga 50) */
/* `s_last_main_render_ms` is the SDL_GetTicks() value of the last
 * render driven by Platform_VBlank (main/game thread). The SDL timer
 * callback uses this to skip its own render when the main thread is
 * already presenting complete frames — if the timer renders while the
 * main thread is mid-draw (between DrawChunkyPitch and DrawSprites),
 * the partial frontbuffer gets presented and sprites flicker in/out
 * underneath the freshly-redrawn pitch. The timer is only the primary
 * renderer during loader/intro code that spins on currentGameTick
 * without calling WaitForVSync. */
static volatile Uint32 s_last_main_render_ms;

/* Screenshot triggers:
 *   - Hotkeys F9 / F11 / F12 / PrintScreen — set s_screenshot_request from
 *     pump_events_any_thread on KEYDOWN. F12 is the historical default but
 *     gets intercepted by some window managers (WSL, GNOME); F9/F11/PrtSc
 *     are alternates that survive most setups.
 *   - Env var SWOS_SCREENSHOT_INTERVAL=N — write every Nth rendered frame
 *     to swos_shotNNN.png, capped at 200 files. Useful when no hotkey
 *     reaches the app (in-match focus, screen recorder grabbing all
 *     function keys, headless test runs).
 * In all cases the request is processed inside Platform_RenderFrame while
 * the render mutex is held, so the captured ARGB buffer is consistent. */
static volatile int s_screenshot_request;
static int          s_screenshot_index;
static int          s_screenshot_interval;   /* 0 = disabled, N>0 = every N frames */
static int          s_present_capture;       /* 0 = disabled, N>0 = every N frames (see capture_presented_frame) */
static int          s_present_capture_from;  /* ... starting at this presented frame */
static int          s_present_stats;         /* SWOS_PRESENT_STATS=1 — report the present path's own cost */
static Uint64       s_present_ticks;
static int          s_present_frames;
static int          s_screenshot_auto_count; /* number written by interval mode */

/* Arcade coin/service input (MAME keys 5/6/F2). Edge-counted in the event pump,
 * drained by Platform_ArcadeInput. Only consumed when _gArcadeMode is set. */
static volatile int s_arcade_coin1;
static volatile int s_arcade_coin2;
static volatile int s_arcade_service;
static volatile int s_arcade_start1;   /* P1 start button */
static volatile int s_arcade_start2;   /* P2 start button */

/* Resolved cabinet key bindings — set from opts at Platform_Init via
 * SDL_GetKeyFromName; defaults are the MAME layout (5/6 coin, 1/2 start, F2 svc). */
static SDL_Keycode s_arcade_key_p1_credit = SDLK_5;
static SDL_Keycode s_arcade_key_p2_credit = SDLK_6;
static SDL_Keycode s_arcade_key_p1_start  = SDLK_1;
static SDL_Keycode s_arcade_key_p2_start  = SDLK_2;
static SDL_Keycode s_arcade_key_service   = SDLK_F2;

static void arcade_resolve_key(SDL_Keycode *dst, const char *name)
{
    if (name && name[0]) {
        SDL_Keycode k = SDL_GetKeyFromName(name);
        if (k != SDLK_UNKNOWN) *dst = k;
    }
}

void Platform_ArcadeInput(int *coin1, int *coin2, int *service,
                          int *start1, int *start2)
{
    if (coin1)   { *coin1   = s_arcade_coin1;   s_arcade_coin1   = 0; }
    if (coin2)   { *coin2   = s_arcade_coin2;   s_arcade_coin2   = 0; }
    if (service) { *service = s_arcade_service; s_arcade_service = 0; }
    if (start1)  { *start1  = s_arcade_start1;  s_arcade_start1  = 0; }
    if (start2)  { *start2  = s_arcade_start2;  s_arcade_start2  = 0; }
}

/* DEBUG: F10 key for the arcade — returns 1 once per press (edge-triggered). Polled
 * from Arcade_MatchTick (in-match: jump to the name-entry flow) and from
 * Arcade_DrawAttractOverlay (attract: step the screen cycler). The keyboard state is
 * refreshed by the event pump in Platform_VBlank. SDL-only. */
int Platform_ArcadeDebugWin(void)
{
    static int prev = 0;
    const Uint8 *ks = SDL_GetKeyboardState(NULL);
    int now  = ks ? ks[SDL_SCANCODE_F10] : 0;
    int edge = now && !prev;
    prev = now;
    return edge;
}

/* Load an indexed (8bpp paletted) BMP — the arcade FLAGS, WINNERS and SVCFONT sheets.
 * Copies up to out_cap index bytes into out8 (row-packed at width *outW) and the
 * 256-entry palette as ARGB8888 into outPal. Returns 0 on success. SDL-only. */
int Platform_LoadIndexedBMP(const char *path, uint8_t *out8, int out_cap,
                            uint32_t *outPal, int *outW, int *outH)
{
    SDL_Surface *s = SDL_LoadBMP(path);
    if (!s) { fprintf(stderr, "[arcade] SDL_LoadBMP(%s): %s\n", path, SDL_GetError()); return 1; }

    int rc = 1;
    if (s->format->BytesPerPixel == 1 && s->format->palette &&
        s->w * s->h <= out_cap) {
        for (int y = 0; y < s->h; y++)
            memcpy(out8 + (size_t)y * s->w,
                   (const uint8_t *)s->pixels + (size_t)y * s->pitch, s->w);
        int n = s->format->palette->ncolors;
        for (int i = 0; i < 256; i++) {
            if (i < n) {
                SDL_Color c = s->format->palette->colors[i];
                outPal[i] = ((uint32_t)0xFFu << 24) | ((uint32_t)c.r << 16) |
                            ((uint32_t)c.g << 8) | (uint32_t)c.b;
            } else {
                outPal[i] = 0xFF000000u;
            }
        }
        if (outW) *outW = s->w;
        if (outH) *outH = s->h;
        rc = 0;
    } else {
        fprintf(stderr, "[arcade] %s not an indexed BMP that fits (bpp=%d %dx%d)\n",
                path, s->format->BytesPerPixel, s->w, s->h);
    }
    SDL_FreeSurface(s);
    return rc;
}

/* Headless smoke-test: dump every Nth converted frame to /tmp as a PPM. */
static int           s_dump_frames;
static int           s_frame_count;

/* Renderer mode chosen at Platform_Init. The SDL_AddTimer worker only
 * issues a fallback Platform_RenderFrame when the renderer is software —
 * hardware (D3D / GL / Metal) contexts are thread-bound. */
static int           s_renderer_is_hw;
static int           s_vsync_enabled;

/* Harness TURBO (env SWOS_HARNESS_TURBO, read once in Platform_Init): when
 * set, the inferior runs frames CPU-bound instead of paced to a 50 Hz wall
 * clock. The gdb test harness drives the game frame-by-frame via `continue`,
 * so per-frame real-time waits are pure overhead — and under WSL2/gdb the
 * timer thread starves, ballooning those waits from tens-of-ms to 10–120 s
 * per cont (driver then over-runs its timeout and false-reports "stuck").
 * Turbo removes every wall-clock wait (Platform_VBlank pacing, the
 * RunInt3HandlerThrottled throttle, the 20 ms timer interval) so a cont is
 * bound by CPU work, not by a clock the scheduler stretches. No effect on
 * normal play (default off). SDL-only file; nothing to back-port to Amiga. */
static int           s_harness_turbo;

/* Set once the rollback rig's match starts (Platform_QuiesceTimerThread): the timer
 * worker stops running the ISR, the audio tick and the fallback render, so every write
 * to game memory comes from the game loop, once per frame (docs/ROLLBACK.md). */
static volatile int  s_timer_quiesced;

#ifdef __ANDROID__
/* Lifecycle events can arrive on SDL's Java thread while rendering is blocked. */
static SDL_atomic_t s_android_background;
static int SDLCALL android_lifecycle(void *unused, SDL_Event *event)
{
    (void)unused;
    if (event->type == SDL_APP_WILLENTERBACKGROUND)
        SDL_AtomicSet(&s_android_background, 1);
    else if (event->type == SDL_APP_DIDENTERFOREGROUND)
        SDL_AtomicSet(&s_android_background, 0);
    return 1;
}
#endif

/* ---- VBlank tick driver ------------------------------------------- *
 * The Amiga VBlank ISR (Int3TimerHandler) was responsible for bumping
 * `currentGameTick` once per frame. The game polls that counter in
 * busy-wait loops (delay_ticks, asset-retry loops, ...). Since we do
 * not run the ISR on SDL, those busy-waits would spin forever.
 *
 * We register an SDL_AddTimer callback that ticks the counter at 50 Hz,
 * matching the PAL VBlank rate SWOS was designed for. The timer runs
 * on SDL's own thread, so the busy-wait loops in the game code see
 * the value change. */
extern volatile uint16_t _currentGameTick;
extern void _Int3TimerHandler(void);   /* the real Amiga VBlank ISR body */

static void pump_quit_only(void);      /* defined below; used by tick_timer_cb */
static void render_frame_impl(int pump_full);   /* below; pump_full=0 on the SDL timer thread */

static SDL_TimerID s_tick_timer;
static SDL_mutex  *s_isr_mtx;          /* serialises _Int3TimerHandler */
static Uint32      s_last_isr_ms;      /* last ISR fire time, for throttle */

/* Active host pacing rate (Hz). Normally the gameplay-mode target
 * (DosGameplay_TargetFps: 70 DOS / 50 Amiga). BUT while a LOADED replay is
 * playing (_playHighlightFlag), pace at the replay's OWN recorded framerate
 * (_replayPlaybackFps) so a DOS RPL always plays at 70 Hz and a native Amiga
 * .HIL at 50 Hz, independent of the current gameplay setting. The in-match
 * instant replay (PlayInstantReplayLoop) does NOT set _playHighlightFlag, so it
 * correctly stays on the gameplay rate — it was recorded at that rate. Every
 * per-frame pacing site below reads this so the main frame clock, the worker
 * tick, and the ISR throttle all agree during replay playback. */
static int Platform_ActivePacingFps(void)
{
    extern volatile uint16_t _playHighlightFlag;
    extern int _replayPlaybackFps;
    if (_playHighlightFlag != 0 && _replayPlaybackFps > 0)
        return _replayPlaybackFps;
    return DosGameplay_TargetFps();
}

/* Run the Amiga Level-3 VBlank ISR body, throttled to ~50 Hz max and
 * guarded by a mutex so callers from different threads serialise. The
 * Amiga IRQ was hardware-preemptive at 50 Hz; on SDL we approximate that
 * by calling this from BOTH the SDL_Timer worker thread (every 20 ms by
 * default) AND from Platform_VBlank on the main thread (so any
 * WaitForVSync also drives the ISR). Whichever thread fires first wins
 * for that 20 ms window; the other call returns immediately via the
 * throttle. This guarantees fade ticks, currentGameTick, and audio
 * housekeeping make progress even when one thread is starved (most
 * commonly the SDL_Timer thread on WSL2, where main-thread spin loops
 * can monopolise CPU). */
void RunInt3HandlerThrottled(void)
{
#ifdef __ANDROID__
    if (SDL_AtomicGet(&s_android_background)) return;
#endif
    /* Turbo collapses the 18 ms throttle to 0 so currentGameTick advances on
     * every call — tick-counting waits (fades, holds, the loader-intro
     * timeout) drain at CPU speed and stop depending on the starved timer
     * thread. The loop that calls this exits as soon as the tick advances, so
     * it runs once or twice, not unbounded. */
    /* Throttle just under one frame interval: 18 ms @ 50 Hz, ~12 ms @ 70 Hz (DOS
     * or a 70 Hz replay). */
    const Uint32 thr = s_harness_turbo ? 0u : (Uint32)(1000 / Platform_ActivePacingFps() - 2);
    Uint32 now = SDL_GetTicks();
    /* Cheap pre-check before the lock — most calls will be too soon. */
    if (now - s_last_isr_ms < thr) return;
    if (!s_isr_mtx) return;            /* not initialised yet */
    if (SDL_TryLockMutex(s_isr_mtx) != 0) return;  /* other thread already in */
    /* Re-check after lock acquired (the other thread may have just run it). */
    now = SDL_GetTicks();
    if (now - s_last_isr_ms >= thr) {
        s_last_isr_ms = now;
        _Int3TimerHandler();
    }
    SDL_UnlockMutex(s_isr_mtx);
}

void Platform_QuiesceTimerThread(void)
{
    s_timer_quiesced = 1;
    /* Wait out a callback that was already running: the ISR holds s_isr_mtx, the
     * rest of the callback is bounded well inside the delay. */
    if (s_isr_mtx) {
        SDL_LockMutex(s_isr_mtx);
        SDL_UnlockMutex(s_isr_mtx);
    }
    SDL_Delay(100);
}

/* A live netplay match hands the machine back to the menus when it ends
 * (src/netplay_match.c, liveEnd): the ISR, the audio tick and the fallback render go
 * back to the timer worker. The rollback rig and the corpus runs never call it. */
void Platform_ResumeTimerThread(void)
{
    s_timer_quiesced = 0;
}

static Uint32 SDLCALL tick_timer_cb(Uint32 interval, void *param)
{
    (void)param;
    if (s_timer_quiesced)
        return interval;
    /* The original Amiga Int3 (Level-3 VBlank) ISR did all per-frame
     * housekeeping: bump currentGameTick / currentTick, run fade ticks,
     * pump audio, do buffer swap if waitingInterrupt was set, etc.
     * On the SDL host we run it from the SDL_Timer thread at 50 Hz, with
     * Platform_VBlank on the main thread also driving it as a fallback
     * for when the timer thread gets starved (common on WSL2). */
    RunInt3HandlerThrottled();

    /* Pump audio production from the worker thread regardless of
     * renderer mode. With --vsync=on the main thread can occasionally
     * spend longer than one game frame inside SDL_RenderPresent (the
     * irreducible 50→60 pulldown jitter on a 60 Hz monitor); without an
     * independent producer the audio queue would drain during those
     * long presents and you'd hear short pauses in the music. The
     * queue-fill check inside AudioSDL_Tick is mutex-guarded so
     * main + worker calling it concurrently doesn't double-advance the
     * RJP1 song state. */
    AudioSDL_Tick();

    /* The loader-intro path waits on `currentGameTick` rather than
     * `WaitForVSync`, so the main thread never reaches our render
     * hook. Drive a render directly from the timer thread only when
     * the main thread hasn't rendered recently — otherwise we race
     * the main thread's pitch/sprite draw and present a torn frame
     * (you see pitch tiles mid-update stomping over sprites).
     *
     * Regardless of whether we render, *always* pump SDL events here:
     * otherwise SDL_QUIT from the window's X button sits in the queue
     * until main happens to call Platform_RenderFrame again, which can
     * take many frames if main is mid-draw or blocked. Decoupling the
     * pump from the render means the X button responds within 20 ms. */
    Uint32 now_ms = SDL_GetTicks();
    Uint32 since  = now_ms - s_last_main_render_ms;
    /* Only software renderers tolerate render calls from this worker
     * thread. Hardware (D3D / OpenGL / Metal) contexts are bound to the
     * creating thread and silently drop or corrupt cross-thread calls;
     * for hardware mode the loader-intro path is the cost — the screen
     * may briefly look frozen during big asset decompresses, but that
     * is purely cosmetic. */
    if (
#ifdef __ANDROID__
        0 && /* Android's surface is owned by the SDL main thread. */
#endif
        !s_renderer_is_hw &&
        (s_last_main_render_ms == 0 || since > 60)) {
        /* Worker-thread render: pump_full=0 — do NOT call SDL_PollEvent/SDL_PumpEvents here.
         * Those pull from the OS window message queue and are main-thread-only on Windows;
         * doing them from this worker deadlocks the main thread. QUIT/WINDOW are drained via
         * pump_quit_only (SDL_PeepEvents, thread-safe) inside render_frame_impl. */
        render_frame_impl(0);
    } else {
        pump_quit_only();
    }

    /* Turbo: fire as fast as SDL allows (1 ms) so the loader-intro path — a
     * pure spin on currentGameTick that never calls Platform_VBlank, advanced
     * ONLY from this thread — drains in a fraction of a second instead of
     * ~1–2 min. With the RunInt3HandlerThrottled throttle also at 0, every
     * fire advances the tick. */
    /* Re-arm at the active rate so a DOS-gameplay toggle — or a loaded replay's
     * own framerate — re-paces the worker tick: 1000/70 ≈ 14 ms (DOS / 70 Hz
     * replay) or 1000/50 = 20 ms (Amiga). */
    (void)interval;
    return s_harness_turbo ? (Uint32)1 : (Uint32)(1000 / Platform_ActivePacingFps());
}

extern void Platform_RewireCustom(void);
extern void Platform_PreArmIsrGlobals(void);

/* ---- Lifecycle ----------------------------------------------------- */
/* Persisted across the run so Alt+Enter / +/- keys can read them. */
static int                    s_win_scale_current = 1;   /* clamp 1..4 */
/* The client area [video] resolution=WxH asked for, 0 when it said 'auto'.
 * Kept so leaving fullscreen restores the size the file named: the restore used
 * to always recompute it from the scale, so an Alt-Tab round trip silently threw
 * the setting away and the window came back a different size than it started
 * (reported 2026-09-22). Pressing +/- is a deliberate override and clears it. */
static int s_pinned_w, s_pinned_h;

static PlatformFullscreenMode s_fullscreen_mode   = PLATFORM_FULLSCREEN_NONE;

/* [video] widescreen = stretch: the original picture -- 336 wide the Amiga way, 320 the
 * DOS way -- pulled out to fill the display, instead of letterboxed (false) or widened to
 * show more pitch (true). SDL's logical size keeps the picture's shape, so for stretch it
 * is replaced by a per-axis scale from the picture to the window, worked out every frame:
 * SDL re-applies a logical size across window changes by itself, but not a scale. */
static int s_stretch;

static void apply_picture_scaling(void)
{
    int ow = 0, oh = 0;
    if (!s_renderer)
        return;
    if (!s_stretch) {
        SDL_RenderSetLogicalSize(s_renderer, _screen_width, _screen_height);
        return;
    }
    SDL_RenderSetLogicalSize(s_renderer, 0, 0);
    if (SDL_GetRendererOutputSize(s_renderer, &ow, &oh) == 0 && ow > 0 && oh > 0)
        SDL_RenderSetScale(s_renderer, (float)ow / (float)_screen_width,
                                       (float)oh / (float)_screen_height);
}

int Platform_Init(const PlatformInitOptions *opts)
{
    int win_scale = opts ? opts->win_scale : 1;
    PlatformFullscreenMode fs_mode =
        opts ? opts->fullscreen : PLATFORM_FULLSCREEN_NONE;
    PlatformRendererMode renderer_mode =
        opts ? opts->renderer : PLATFORM_RENDERER_ACCEL;
    int vsync = opts ? opts->vsync : 0;

    if (win_scale < 1) win_scale = 1;
    if (win_scale > 4) win_scale = 4;

    /* Which monitor to open on ([video] screen=N, 0 = primary). */
    int screen = opts ? opts->screen : 0;
    if (screen < 0) screen = 0;
#ifndef SWOS_WASM_BUILD
    /* Clamp the display index to a real one, then shrink win_scale so the
     * windowed size fits that monitor's USABLE bounds (desktop minus the
     * taskbar/dock). Without this the fixed default (WIN_SCALE=3 →
     * _screen_width*3 × 272*3) overflows a laptop/small screen — the "window is
     * bigger than my monitor" report. Windowed only; fullscreen covers the
     * display regardless. The user can still scale up with +/- to what fits. */
    {
        int nd = SDL_GetNumVideoDisplays();
        if (nd < 1) nd = 1;
        if (screen >= nd) screen = 0;
        if (fs_mode == PLATFORM_FULLSCREEN_NONE) {
            SDL_Rect ub;
            if (SDL_GetDisplayUsableBounds(screen, &ub) == 0 && ub.w > 0 && ub.h > 0) {
                while (win_scale > 1 &&
                       ((int)_screen_width * win_scale > ub.w ||
                        _screen_height * win_scale > ub.h)) {
                    win_scale--;
                }
            }
        }
    }
#endif
#ifdef SWOS_WASM_BUILD
    /* Web: fullscreen is browser-managed (F11) — SDL desktop-fullscreen at boot
     * is user-gesture-gated and would stretch the canvas — so force windowed
     * mode regardless of SWOS.ini. win_scale + vsync come from SWOS.ini (main.c
     * defaults: native scale, vsync off — the canvas CSS upscales to the
     * viewport, and we pace via emscripten_sleep). */
    fs_mode = PLATFORM_FULLSCREEN_NONE;
#endif
    s_win_scale_current = win_scale;

    if (opts) {
        arcade_resolve_key(&s_arcade_key_p1_credit, opts->arcade_key_p1_credit);
        arcade_resolve_key(&s_arcade_key_p2_credit, opts->arcade_key_p2_credit);
        arcade_resolve_key(&s_arcade_key_p1_start,  opts->arcade_key_p1_start);
        arcade_resolve_key(&s_arcade_key_p2_start,  opts->arcade_key_p2_start);
        arcade_resolve_key(&s_arcade_key_service,   opts->arcade_key_service);
    }
    s_fullscreen_mode   = fs_mode;
    /* Repoint `custom` at our stub storage *before* the tick timer
     * starts -- the first VBlank ISR firing reads custom->intreqr. */
    Platform_RewireCustom();
    Platform_PreArmIsrGlobals();

    /* SDL_WINDOW_RESIZABLE: lets the user resize/maximise the desktop window (content
     * is letterboxed by SDL_RenderSetLogicalSize below), and on macOS it's what enables
     * the green title-bar zoom/fullscreen button (greyed out without it).
     * NOT on WASM: emscripten's SDL2 sizes the <canvas> element to the SDL window when
     * it's resizable, which overrides the shell.html responsive CSS (height:100vh /
     * object-fit:contain) and pins the canvas to a small fixed size letterboxed in the
     * page. On the web the page CSS owns the canvas display size, so leave it off. */
    Uint32 wflags = SDL_WINDOW_SHOWN;
#ifndef SWOS_WASM_BUILD
    wflags |= SDL_WINDOW_RESIZABLE;
#endif
    if (fs_mode == PLATFORM_FULLSCREEN_DESKTOP) {
        /* Borderless window sized to the desktop, no display-mode
         * change. SDL_RenderSetLogicalSize letterboxes 336x272 inside
         * whatever resolution the desktop is at. Fast to toggle. On
         * Windows XP the taskbar can occasionally show through above
         * a borderless window — use --fullscreen=exclusive there. */
        wflags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    } else if (fs_mode == PLATFORM_FULLSCREEN_EXCLUSIVE) {
        /* True fullscreen with display-mode change. Always covers the
         * taskbar (even XP). Slower toggle, may flicker on multi-monitor
         * rigs, and the desktop resolution is restored on exit. */
        wflags |= SDL_WINDOW_FULLSCREEN;
    }

    /* Open (and, for fullscreen, cover) the requested monitor. CENTERED_DISPLAY
     * places the window on display `screen`; on WASM SDL only has display 0. */
    /* The build goes in the title bar as well as on the main menu: a windowed
     * screenshot then carries it even when the shot crops to the game area. */
    static char s_title[96];
    snprintf(s_title, sizeof s_title, "OpenSWOS %s (%s)", Build_Id(), Build_Platform());

    /* [video] resolution=WxH IS THE CLIENT AREA, when one is named.
     *
     * It used to set only the render ASPECT, so "resolution=320x200, scale=1" opened a
     * 436x272 window -- the number in the file appeared nowhere on screen, which is what a
     * tester reported. A resolution the game never shows you is a strange thing to ask a
     * player to set, so now it is exactly the size of the client rect and `scale` applies
     * only to 'auto'. The render is letterboxed inside it by SDL_RenderSetLogicalSize,
     * which is already how fullscreen works. */
    int client_w = (int)_screen_width * win_scale;
    int client_h = _screen_height * win_scale;
    int pinned = 0;
    s_pinned_w = s_pinned_h = 0;
    if (opts && opts->res_w > 0 && opts->res_h > 0 && fs_mode == PLATFORM_FULLSCREEN_NONE) {
        client_w = opts->res_w;
        client_h = opts->res_h;
        pinned = 1;
        s_pinned_w = client_w;
        s_pinned_h = client_h;
        /* A client area SMALLER than the picture shrinks it to fit, which is
         * almost never what was meant: "resolution=320x200" reads like "run at
         * the DOS size" but the picture is 336x272, so it comes out at 0.73x --
         * blurry, non-integer, and smaller than 1:1 (reported 2026-09-22). Say
         * so, and name the setting that does what they wanted. */
        if (client_w < (int)_screen_width || client_h < _screen_height)
            fprintf(stderr,
                "[video] %dx%d is smaller than the %dx%d picture, so it is being shrunk "
                "to fit (%.2fx). For a crisp window use 'resolution=auto' with 'scale=N'; "
                "the picture is always %d lines tall -- the render SIZE is not settable\n",
                client_w, client_h, (int)_screen_width, _screen_height,
                (double)client_w / (double)_screen_width < (double)client_h / (double)_screen_height
                    ? (double)client_w / (double)_screen_width
                    : (double)client_h / (double)_screen_height,
                _screen_height);
#ifndef SWOS_WASM_BUILD
        /* A window bigger than the monitor is not a setting, it is a mistake nobody can
         * undo from inside the game -- the title bar ends up off-screen. Clamp to the
         * usable area, keeping the shape asked for. */
        {
            SDL_Rect ub;
            if (SDL_GetDisplayUsableBounds(screen, &ub) == 0 && ub.w > 0 && ub.h > 0 &&
                (client_w > ub.w || client_h > ub.h)) {
                int fit_w = ub.w, fit_h = (int)((long)ub.w * client_h / client_w);
                if (fit_h > ub.h) { fit_h = ub.h; fit_w = (int)((long)ub.h * client_w / client_h); }
                fprintf(stderr, "[video] %dx%d does not fit this display, using %dx%d\n",
                        client_w, client_h, fit_w, fit_h);
                client_w = fit_w; client_h = fit_h;
                s_pinned_w = client_w; s_pinned_h = client_h;
            }
        }
#endif
    }
    s_window = SDL_CreateWindow(
        s_title,
        SDL_WINDOWPOS_CENTERED_DISPLAY(screen),
        SDL_WINDOWPOS_CENTERED_DISPLAY(screen),
        client_w, client_h,
        wflags);
    if (!s_window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }
#ifdef SWOS_SET_WINDOW_ICON
    {
        SDL_Surface *icon = SDL_CreateRGBSurfaceWithFormatFrom(
            (void *)k_swosWindowIcon, SWOS_WINDOW_ICON_SIZE, SWOS_WINDOW_ICON_SIZE, 32,
            SWOS_WINDOW_ICON_SIZE * 4, SDL_PIXELFORMAT_RGBA32);
        if (icon) {
            SDL_SetWindowIcon(s_window, icon);
            SDL_FreeSurface(icon);
        }
    }
#endif

#ifndef SWOS_WASM_BUILD
    /* Post-creation window fit (windowed only). The pre-creation scale clamp
     * uses SDL_GetDisplayUsableBounds, which can under-shrink (DPI virtualization
     * on Windows, work-area quirks) and CANNOT account for the title bar — that's
     * added by the OS OUTSIDE the client rect. CENTERED_DISPLAY then centers the
     * CLIENT rect, so a tall window pushes its title bar above the screen top:
     * "the window opens inside the screen but I can't see the close/maximize
     * buttons" (16" laptop, 1920x1080). Redo the fit with the REAL decorated
     * geometry: shrink the integer scale until the framed window fits the usable
     * area, then move it so the whole frame — title bar included — is on-screen. */
    if (fs_mode == PLATFORM_FULLSCREEN_NONE) {
        int disp = SDL_GetWindowDisplayIndex(s_window);
        if (disp < 0) disp = screen;
        SDL_Rect ub, db;
        int have_ub = (SDL_GetDisplayUsableBounds(disp, &ub) == 0 && ub.w > 0 && ub.h > 0);
        int have_db = (SDL_GetDisplayBounds(disp, &db) == 0 && db.w > 0 && db.h > 0);
        if (!have_ub && have_db) { ub = db; have_ub = 1; }   /* usable query failed */
        if (have_ub && have_db) {
            /* Never trust usable bounds bigger than the physical display — some
             * multi-monitor drivers return the whole virtual desktop for a single
             * display index, which is why the tester's 4x (1936x1088) window was
             * NOT shrunk below his 1920x1080 panel. Cap to the display's real
             * size/origin so the fit reflects the monitor the window is on. */
            if (ub.w > db.w) ub.w = db.w;
            if (ub.h > db.h) ub.h = db.h;
            if (ub.x < db.x) ub.x = db.x;
            if (ub.y < db.y) ub.y = db.y;
        }
        if (have_ub) {
            /* Border thicknesses; bt = title-bar height. 0 if unsupported. */
            int bt = 0, bl = 0, bb = 0, br = 0;
            SDL_GetWindowBordersSize(s_window, &bt, &bl, &bb, &br);

            /* Shrink until the DECORATED window fits the usable area. A PINNED client
             * size is left alone: the player asked for that many pixels, and shrinking it
             * to an integer multiple of the render size is exactly what this change is
             * undoing. It still gets moved on-screen below. */
            if (!pinned) {
                while (win_scale > 1 &&
                       ((int)_screen_width * win_scale + bl + br > ub.w ||
                        _screen_height * win_scale + bt + bb > ub.h)) {
                    win_scale--;
                }
                SDL_SetWindowSize(s_window,
                                  _screen_width * win_scale,
                                  _screen_height * win_scale);
            }
            s_win_scale_current = win_scale;

            /* Reposition so the full frame (client + borders) sits inside the
             * usable bounds; guarantee the title bar is never above the top. */
            int wx, wy, ww, wh;
            SDL_GetWindowPosition(s_window, &wx, &wy);
            SDL_GetWindowSize(s_window, &ww, &wh);
            int outerW = ww + bl + br, outerH = wh + bt + bb;
            if (wx - bl < ub.x)                    wx = ub.x + bl;
            if (wy - bt < ub.y)                    wy = ub.y + bt;
            if (wx - bl + outerW > ub.x + ub.w)    wx = ub.x + ub.w - outerW + bl;
            if (wy - bt + outerH > ub.y + ub.h)    wy = ub.y + ub.h - outerH + bt;
            if (wx - bl < ub.x)                    wx = ub.x + bl;   /* re-pin if too wide */
            if (wy - bt < ub.y)                    wy = ub.y + bt;   /* title bar on-screen */
            SDL_SetWindowPosition(s_window, wx, wy);
        }

        /* Diagnostic: the actual window geometry + the monitor it fits, so a
         * tester's SWOS.log tells us whether the fit worked (and DPI is sane). */
        {
            int wx = 0, wy = 0, ww = 0, wh = 0;
            SDL_GetWindowPosition(s_window, &wx, &wy);
            SDL_GetWindowSize(s_window, &ww, &wh);
            SDL_Rect ub2 = { 0, 0, 0, 0 };
            SDL_GetDisplayUsableBounds(disp, &ub2);
            fprintf(stderr,
                    "[video] window %dx%d @%d,%d scale=%d display=%d usable=%dx%d\n",
                    ww, wh, wx, wy, s_win_scale_current, disp, ub2.w, ub2.h);
        }
    }
#endif

    /* Renderer choice. Hardware (accelerated) is the default — vsync
     * gives a 50 Hz monitor user free perfect pacing. Software is
     * available as an opt-in for hosts where the hardware backend
     * misbehaves; it permits cross-thread RenderFrame calls from the
     * SDL_AddTimer worker (used as a fallback during loader-intro
     * spins). */
    Uint32 rflags;
    if (renderer_mode == PLATFORM_RENDERER_SOFTWARE) {
        rflags = SDL_RENDERER_SOFTWARE;
        s_renderer_is_hw = 0;
        s_vsync_enabled  = 0;            /* software ignores PRESENTVSYNC */
    } else {
        rflags = SDL_RENDERER_ACCELERATED;
        if (vsync) rflags |= SDL_RENDERER_PRESENTVSYNC;
        s_renderer_is_hw = 1;
        s_vsync_enabled  = vsync;
    }

    /* Optional accel-backend override ([video] render_driver). SDL_CreateRenderer
     * with index -1 picks the first driver matching SDL_HINT_RENDER_DRIVER; unset
     * = SDL's platform default (D3D11 on Windows). On Windows the D3D11 backend
     * serialises presents on CPU-idle frames, so cheap-per-frame content like the
     * arcade attract replays crawls even though live play is fine — "opengl" sides
     * around it. The env var SDL_RENDER_DRIVER overrides this too. */
    if (opts && opts->render_driver[0] &&
        SDL_strcasecmp(opts->render_driver, "auto") != 0) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, opts->render_driver);
        fprintf(stderr, "[render] requested driver '%s' via [video] render_driver\n",
                opts->render_driver);
    }

    s_renderer = SDL_CreateRenderer(s_window, -1, rflags);
    if (!s_renderer) {
        /* Fall back to software if the requested mode failed. */
        fprintf(stderr,
            "SDL_CreateRenderer (%s%s) failed: %s — falling back to software\n",
            renderer_mode == PLATFORM_RENDERER_ACCEL ? "accel" : "software",
            (renderer_mode == PLATFORM_RENDERER_ACCEL && vsync) ? "+vsync" : "",
            SDL_GetError());
        s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_SOFTWARE);
        s_renderer_is_hw = 0;
        s_vsync_enabled  = 0;
    }
    if (!s_renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return -1;
    }

    s_subpixel_scroll = opts ? opts->subpixel_scroll : 1;
    s_scaling_smooth  = opts ? opts->scaling_smooth  : 1;
    {
        CrtEffectsConfig cfx;
        cfx.mask          = opts ? (CrtMaskKind)opts->crt_mask : CRT_MASK_OFF;
        cfx.mask_strength = opts ? opts->crt_mask_strength : 0;
        cfx.vignette      = opts ? opts->crt_vignette : 0;
        cfx.corners       = opts ? opts->crt_corners : 0;
        CrtEffects_Configure(&cfx);
    }

    /* Report what we actually GOT, not what we asked for. Ruff n Tumble's loop
     * does the same (SDL_SetRenderVSync then SDL_GetRenderVSync to confirm, and
     * it warns when vsync is unavailable) and it is the right instinct: a
     * requested PRESENTVSYNC can be silently dropped by the driver or overridden
     * in the GPU control panel, and without this line a tester report saying
     * "vsync on, still stutters" is unfalsifiable.
     *
     * The refresh rate matters just as much for triage. Our pacing rate is 50 Hz
     * (70 in DOS mode); if the panel divides evenly (100/150/200/300 Hz at 50)
     * every game frame gets a whole number of refreshes and pacing is exact, so
     * any remaining unevenness is NOT a pacing problem and must be chased
     * elsewhere — see present_frame_copy and docs/AMIGA_60.md. */
    {
        SDL_RendererInfo info;
        if (SDL_GetRendererInfo(s_renderer, &info) == 0) {
            int got_vsync = (info.flags & SDL_RENDERER_PRESENTVSYNC) ? 1 : 0;
            /* Pass 1 renders into a texture. The software renderer and a few
             * exotic backends can't, so ask before relying on it rather than
             * failing per-frame. */
            s_scale_targets_ok = (info.flags & SDL_RENDERER_TARGETTEXTURE) ? 1 : 0;
            fprintf(stderr, "[render] driver=%s flags=0x%x hw=%d vsync: requested=%s actual=%s\n",
                    info.name ? info.name : "?", info.flags, s_renderer_is_hw,
                    s_vsync_enabled ? "on" : "off",
                    got_vsync ? "on" : "off");
            /* Only BELIEVE the flag on a hardware renderer. The software
             * renderer reports PRESENTVSYNC (flags 0xd) while doing nothing of
             * the sort, which is why the branch above deliberately forces
             * s_vsync_enabled = 0 for it — don't let the readback undo that. */
            if (s_renderer_is_hw) {
                if (s_vsync_enabled && !got_vsync)
                    fprintf(stderr, "[render] WARNING: vsync requested but the renderer did "
                                    "not enable it — frames are paced by the timer only\n");
                s_vsync_enabled = got_vsync;
            }
        }
        SDL_DisplayMode dm;
        if (SDL_GetCurrentDisplayMode(screen, &dm) == 0 && dm.refresh_rate > 0) {
            s_display_hz = dm.refresh_rate;
            int fps = Platform_ActivePacingFps();
            fprintf(stderr, "[render] display=%d %dx%d @%dHz, game paces at %dHz%s\n",
                    screen, dm.w, dm.h, dm.refresh_rate, fps,
                    (fps > 0 && dm.refresh_rate % fps == 0)
                        ? " (exact multiple — pacing is exact)"
                        : " (not a multiple — some pulldown judder is inherent)");
        }
        fprintf(stderr, "[render] scaling=%s%s | subpixel_scroll=%s | pacing: %s\n",
                s_scaling_smooth ? "smooth" : "sharp",
                !s_scaling_smooth ? ""
                    : !s_renderer_is_hw   ? " (inactive — hardware renderer only)"
                    : !s_scale_targets_ok ? " (inactive — backend has no render-to-texture)"
                    : "",
                s_subpixel_scroll ? "on" : "off",
                (s_vsync_enabled && s_display_hz > 0 &&
                 s_display_hz == Platform_ActivePacingFps())
                    ? "vsync alone (timer disabled — rates match)"
                    : "software timer");

#ifndef SWOS_WASM_BUILD   /* the browser paces a WASM frame; SDL_Delay there only busy-waits */
        /* What a 1 ms sleep really costs on this machine. The software timer
         * sleeps to within 2 ms of each deadline, so anything much over 2 ms
         * here makes vsync-off pacing uneven -- 15.6 is Windows' default tick,
         * i.e. the timer-resolution request did not take. Five samples, ~5 ms. */
        Uint64 t0 = SDL_GetPerformanceCounter(), worst = 0;
        for (int32_t i = 0; i < 5; i++) {
            SDL_Delay(1);
            Uint64 t1 = SDL_GetPerformanceCounter();
            if (t1 - t0 > worst) worst = t1 - t0;
            t0 = t1;
        }
        fprintf(stderr, "[render] SDL_Delay(1) sleeps up to %.1f ms\n",
                (double)worst * 1000.0 / (double)SDL_GetPerformanceFrequency());
#endif
    }

    /* HUD layer + its conversion scratch. Allocation failure is not fatal:
     * Platform_HudLayerBuffer() then returns NULL and the HUD simply stays in
     * the main framebuffer, i.e. today's behaviour with a wobble. */
    s_hud_layer = (uint8_t *)SDL_calloc(1, (size_t)_screen_width * PLATFORM_SCREEN_H);
    if (!s_hud_layer)
        fprintf(stderr, "[render] HUD layer alloc failed — the HUD will ride the "
                        "sub-pixel shift (harmless, slightly jittery)\n");
    Platform_HudLayerReset();

    /* No separate HUD texture: the overlay rides in the scratch strip of the
     * frame texture and is drawn from it. See PLATFORM_TEX_H / compose_hud_strip. */
    fprintf(stderr, "[render] hud_layer=%s\n", s_hud_layer ? "on" : "off (no buffer)");

    /* The picture's own size, both runtime (display.h): what gets letterboxed
     * into whatever client area the window has -- or stretched over it. */
    s_stretch = opts && opts->widescreen == 2;
    apply_picture_scaling();
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_EventState(SDL_MOUSEMOTION, SDL_DISABLE);

    s_tex_idx = 0;
    for (int ti = 0; ti < PLATFORM_TEX_RING; ti++) {
        s_textures[ti] = SDL_CreateTexture(
            s_renderer,
            SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING,
            _screen_width, PLATFORM_TEX_H);
        if (!s_textures[ti]) {
            fprintf(stderr, "SDL_CreateTexture[%d]: %s\n", ti, SDL_GetError());
            return -1;
        }
        (void)0;
    }
    s_frame_rgba = (uint32_t *)SDL_malloc((size_t)_screen_width * PLATFORM_TEX_H * 4);
    if (!s_frame_rgba) {
        fprintf(stderr, "frame buffer alloc failed (%d x %d)\n", _screen_width, PLATFORM_SCREEN_H);
        return -1;
    }

    /* Bring up the audio + input subsystems. Failures here are non-fatal —
     * we'd still rather see graphics than nothing. */
    (void)AudioSDL_Init(opts ? opts->audio_mono : 1);
    (void)InputSDL_Init();

    /* Bump the OS timer resolution so SDL_Delay() doesn't oversleep.
     * Windows' default scheduler tick is 15.6 ms, which means a 20 ms
     * sleep often becomes 30+ ms — making the 50 Hz pacing loop alternate
     * between 20 ms and 30 ms frames and producing the "choppy / uneven
     * framerate" testers report. SDL_HINT_TIMER_RESOLUTION asks a real SDL2
     * for `timeBeginPeriod(1)`, but it is a no-op under sdl2-compat (what
     * Cygwin ships), so main() already made that call itself —
     * Platform_RaiseTimerResolution, src/platform_timer.c. The hint stays for
     * a native SDL2 build and is harmless everywhere else. */
    SDL_SetHint(SDL_HINT_TIMER_RESOLUTION, "1");

    s_perf_freq = SDL_GetPerformanceFrequency();
    s_next_frame_perf = SDL_GetPerformanceCounter();

    s_render_mtx = SDL_CreateMutex();
    s_isr_mtx    = SDL_CreateMutex();
    s_dump_frames = (getenv("SWOS_DUMP_FRAMES") != NULL);

    s_harness_turbo = (getenv("SWOS_HARNESS_TURBO") != NULL);
    /* Harness fast-boot: skip the (slow) loader intro under the harness too. */
    { extern volatile uint8_t _skipLoaderIntro; if (s_harness_turbo) _skipLoaderIntro = 1; }
    if (s_harness_turbo)
        fprintf(stderr,
            "[harness] TURBO: 50 Hz pacing + ISR throttle + 20 ms timer "
            "disabled — frames run CPU-bound for the gdb harness (no "
            "wall-clock waits to balloon under WSL2/gdb starvation)\n");

    {
        const char *si = getenv("SWOS_SCREENSHOT_INTERVAL");
        if (si && *si) {
            int n = atoi(si);
            if (n > 0) {
                s_screenshot_interval = n;
                fprintf(stderr,
                    "[screenshot] interval mode: writing every %d frame(s) "
                    "(swos_shotNNN.png), capped at 200 files\n", n);
            }
        }
    }
    s_present_stats = (getenv("SWOS_PRESENT_STATS") != NULL);
    {
        const char *pc = getenv("SWOS_PRESENT_CAPTURE");
        const char *from = getenv("SWOS_PRESENT_CAPTURE_FROM");
        if (from && *from)
            s_present_capture_from = atoi(from);
        if (pc && *pc) {
            int n = atoi(pc);
            if (n > 0) {
                s_present_capture = n;
                fprintf(stderr,
                    "[present] capture mode: reading back the PRESENTED output every "
                    "%d frame(s) (swos_presentNNN.png), capped at 60 files\n", n);
            }
        }
    }
    fprintf(stderr,
        "[screenshot] hotkeys: F9 / F11 / F12 / PrintScreen "
        "(env SWOS_SCREENSHOT_INTERVAL=N for auto-capture every N frames, "
        "SWOS_PRESENT_CAPTURE=N for the presented output)\n");

    /* 20 ms = 50 Hz, the PAL VBlank rate the game expects. */
#ifndef SWOS_WASM_BUILD
    /* Single-threaded on WASM: Platform_VBlank already pumps the ISR + render +
     * audio every frame, so this fallback timer is redundant (and pthreads on
     * wasm need SharedArrayBuffer/COOP+COEP — avoided entirely). */
#ifdef __ANDROID__
    SDL_AddEventWatch(android_lifecycle, NULL);
#endif
    SDL_InitSubSystem(SDL_INIT_TIMER);
    s_tick_timer = SDL_AddTimer((Uint32)(1000 / Platform_ActivePacingFps()), tick_timer_cb, NULL);
#endif

    return 0;
}

void Platform_Shutdown(void)
{
#ifdef __ANDROID__
    SDL_DelEventWatch(android_lifecycle, NULL);
#endif
    if (s_tick_timer) { SDL_RemoveTimer(s_tick_timer); s_tick_timer = 0; }
    if (s_render_mtx) { SDL_DestroyMutex(s_render_mtx); s_render_mtx = NULL; }
    if (s_isr_mtx)    { SDL_DestroyMutex(s_isr_mtx);    s_isr_mtx    = NULL; }
    InputSDL_Shutdown();
    AudioSDL_Shutdown();

    for (int ti = 0; ti < PLATFORM_TEX_RING; ti++)
        if (s_textures[ti]) { SDL_DestroyTexture(s_textures[ti]); s_textures[ti] = NULL; }
    if (s_scale_target) { SDL_DestroyTexture(s_scale_target); s_scale_target = NULL; s_scale_n = 0; }
    CrtEffects_Shutdown();
    if (s_frame_rgba) { SDL_free(s_frame_rgba); s_frame_rgba = NULL; }
    if (s_hud_layer)  { SDL_free(s_hud_layer);  s_hud_layer  = NULL; }
    if (s_renderer) { SDL_DestroyRenderer(s_renderer); s_renderer = NULL; }
    if (s_window)   { SDL_DestroyWindow(s_window);     s_window = NULL; }
}

void Platform_ToggleFullscreen(void)
{
    if (!s_window) return;
    Uint32 flags = SDL_GetWindowFlags(s_window);
    int currently_fullscreen =
        (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;

    if (currently_fullscreen) {
        SDL_SetWindowFullscreen(s_window, 0);
        /* Back to the size the window actually had: the one [video] resolution
         * named, or the scale when it said 'auto'. */
        if (s_pinned_w > 0 && s_pinned_h > 0)
            SDL_SetWindowSize(s_window, s_pinned_w, s_pinned_h);
        else
            SDL_SetWindowSize(s_window,
                _screen_width * s_win_scale_current,
                _screen_height * s_win_scale_current);
        SDL_ShowCursor(SDL_ENABLE);
    } else {
        /* Toggle into the flavour the user launched with. If they
         * launched windowed, default to DESKTOP-style fullscreen since
         * that's the friendlier of the two. */
        Uint32 fs_flag = (s_fullscreen_mode == PLATFORM_FULLSCREEN_EXCLUSIVE)
                         ? SDL_WINDOW_FULLSCREEN
                         : SDL_WINDOW_FULLSCREEN_DESKTOP;
        SDL_SetWindowFullscreen(s_window, fs_flag);
        SDL_ShowCursor(SDL_DISABLE);
    }
}

int Platform_GetWindowScale(void)
{
    return s_win_scale_current;
}

PlatformFullscreenMode Platform_GetFullscreenMode(void)
{
    /* Return the *current* mode, not the launch one — Alt+Enter at
     * runtime can land us in fullscreen even if we launched windowed,
     * and we want SWOS.ini to remember that. The mode value reflects
     * the SDL window flags right now. */
    if (s_window) {
        Uint32 flags = SDL_GetWindowFlags(s_window);
        if (flags & SDL_WINDOW_FULLSCREEN) {
            /* Distinguish DESKTOP (FULLSCREEN_DESKTOP includes FULLSCREEN
             * bit) from EXCLUSIVE (FULLSCREEN bit set, DESKTOP bit not). */
            if ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP)
                return PLATFORM_FULLSCREEN_DESKTOP;
            return PLATFORM_FULLSCREEN_EXCLUSIVE;
        }
    }
    return PLATFORM_FULLSCREEN_NONE;
}

void Platform_ViewChanged(void)
{
    if (!s_renderer) return;
    apply_picture_scaling();

    /* And the WINDOW follows the view, so the picture keeps its whole-pixel
     * scale instead of being letterboxed inside a window sized for the other
     * one: 336x272 x3 in the menus, 320x200 x3 in a DOS-view match. The window
     * used to stay put, which left bars in the match -- and then a trip through
     * fullscreen "fixed" it, because the restore recomputes from the CURRENT
     * view. Two sizes for the same state is worse than a window that resizes.
     *
     * Not when fullscreen (SDL letterboxes, which is right), and not when
     * [video] resolution pinned a client area -- that was asked for by name. */
    if (!s_window || s_pinned_w > 0) return;
    {
        Uint32 flags = SDL_GetWindowFlags(s_window);
        if (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) return;
        SDL_SetWindowSize(s_window,
                          _screen_width  * s_win_scale_current,
                          _screen_height * s_win_scale_current);
    }
}

void Platform_SetWindowScale(int scale)
{
    if (!s_window) return;
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;
    s_win_scale_current = scale;
    /* Choosing a scale by hand overrides whatever [video] resolution pinned, and
     * keeps it overridden for the rest of the session -- otherwise the next
     * fullscreen round trip would undo what the player just pressed. */
    s_pinned_w = s_pinned_h = 0;
    /* Only resize when actually windowed — fullscreen ignores the size
     * (SDL_RenderSetLogicalSize handles letterboxing for us). */
    Uint32 flags = SDL_GetWindowFlags(s_window);
    if ((flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) == 0) {
        SDL_SetWindowSize(s_window,
            _screen_width * scale,
            _screen_height * scale);
        /* Re-centre on the desktop after a size change so a 4× window
         * doesn't end up clipped against the bottom-right of the screen. */
        SDL_SetWindowPosition(s_window,
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
}

/* ---- Palette refresh ---------------------------------------------- */
/* The Amiga copper list contains "MOVE COLORxx, value" pairs. The value is
 * either 0x0RGB (OCS, 12-bit) or split high/low across two MOVE pairs (AGA).
 * For now we extract OCS-style. AGA support comes when we wire up the
 * copperlist offsets COP_PAL_B*_C*_OFF. */
/* The palette entry nearest an RGB colour, in the palette last shown: for a debugging
 * overlay drawn into the 8bpp frame (the playback page's prediction lines, replay_v2.c).
 * Entry 0 is left out -- it is the colour nothing is drawn with. */
uint8_t Platform_NearestColour(uint8_t r, uint8_t g, uint8_t b)
{
    int32_t i, best = 1, bestD = 0x7FFFFFFF;
    for (i = 1; i < 256; i++) {
        int32_t dr = s_palette[i].r - r, dg = s_palette[i].g - g, db = s_palette[i].b - b;
        int32_t d = dr * dr + dg * dg + db * db;
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return (uint8_t)best;
}

static void refresh_palette_from_copper(void)
{
    /* DOS graphics use the full 256-colour DOS palette, but ONLY during a
     * match. The intro, title and menu screens are still Amiga graphics and
     * need their normal Amiga (copper) palette — otherwise they render with the
     * wrong colours (olive backgrounds etc.). playGame is set at kickoff and
     * cleared when the match ends; playHighlightFlag does the same for HIL
     * highlight playback, which renders the in-match pitch/players but never
     * sets playGame. Either flag means the DOS pitch is on screen, so we must
     * use the DOS palette. (DOS menu backgrounds are a separate follow-up.) */
    extern volatile uint16_t _playGame, _playHighlightFlag;
    extern int32_t Pitch_DrawnThisMatch(void);
    extern void    Pitch_ResetDrawn(void);

    /* ... and only once the pitch is actually ON SCREEN, which is later than
     * "a match has started": _playGame is set on the first line of
     * StartMatchSequence, while the frontbuffer still holds the pre-match team
     * line-up that InitGameSaveTeams has yet to fade out. Without this the
     * line-up is displayed through the DOS PITCH palette for those frames --
     * the "distorted VS background for a frame or two" (reported 2026-09-22).
     * The highlights path avoids the same trap by setting its flag after the
     * fade (game_loop.c, ViewHighLightsMain); the live-match path cannot,
     * because _playGame means other things to the game logic, so the display
     * asks the pitch instead. */
    if (!_playGame && !_playHighlightFlag)
        Pitch_ResetDrawn();
    /* SWOS_VSDEBUG=1 counts the frames this gate saves. It is how the bug was
     * proved and is the cheapest regression check for it: on the rig path it
     * reports 1, and through the real menus one per frame of the VS fade. */
    {
        static int32_t dbg = -1, n;
        if (dbg < 0) dbg = getenv("SWOS_VSDEBUG") != NULL;
        if (dbg && _gDosGraphics && (_playGame || _playHighlightFlag)
            && !Pitch_DrawnThisMatch() && ++n <= 20)
            fprintf(stderr, "[vsdebug] blocked the DOS pitch palette before the pitch "
                            "was on screen (#%d)\n", n);
    }
    if (_gDosGraphics && (_playGame || _playHighlightFlag) && Pitch_DrawnThisMatch()) {
        /* The pitch-condition overlay keys off the low byte of
         * pitchNumberAndType (the high byte is which pitch graphic). */
        extern uint16_t _pitchNumberAndType;
        uint8_t rgb[768];
        DosGfx_BuildPalette((int)(_pitchNumberAndType & 0xFF), rgb);
        /* Apply the current fade level so fades work on the DOS palette like the
         * copper path does (g_fadeLevel: 0x80 bright .. 0 black; rgb*level/0x80).
         * DosGfx_BuildPalette rebuilds full-bright RGB each frame, so without
         * this scaling fade in/out had no visible effect on DOS pitches. */
        extern int g_fadeLevel;
        int fl = g_fadeLevel;
        if (fl < 0) fl = 0; else if (fl > 0x80) fl = 0x80;
        for (int i = 0; i < 256; ++i) {
            s_palette[i].r = (uint8_t)((rgb[i * 3 + 0] * fl) >> 7);
            s_palette[i].g = (uint8_t)((rgb[i * 3 + 1] * fl) >> 7);
            s_palette[i].b = (uint8_t)((rgb[i * 3 + 2] * fl) >> 7);
            s_palette[i].a = 255;
        }
        return;
    }
#ifdef AGA
    /* AGA: 256 colours across 8 banks of 32. WriteCopperPalette wrote each
     * bank's 32 value words starting at COP_PAL_Bx_C0_VAL_OFF with a 4-byte
     * stride (copper reg+val pairs), exactly like the OCS block below but for
     * 8 banks. Each value is a 12-bit 0x0RGB word; expand each nibble to 8-bit
     * (*17 maps 0xF→255). Fades are already baked into these copper values. */
    static const uint32_t aga_bank_val_off[8] = {
        COP_PAL_B0_C0_VAL_OFF, COP_PAL_B1_C0_VAL_OFF,
        COP_PAL_B2_C0_VAL_OFF, COP_PAL_B3_C0_VAL_OFF,
        COP_PAL_B4_C0_VAL_OFF, COP_PAL_B5_C0_VAL_OFF,
        COP_PAL_B6_C0_VAL_OFF, COP_PAL_B7_C0_VAL_OFF
    };
    for (int bank = 0; bank < 8; ++bank) {
        const uint8_t *base = _copperListBuffer + aga_bank_val_off[bank];
        for (int j = 0; j < 32; ++j) {
            uint16_t v = *(const uint16_t *)(base + j * 4);
            int idx = bank * 32 + j;
            s_palette[idx].r = (uint8_t)(((v >> 8) & 0x0F) * 17);
            s_palette[idx].g = (uint8_t)(((v >> 4) & 0x0F) * 17);
            s_palette[idx].b = (uint8_t)(((v >> 0) & 0x0F) * 17);
            s_palette[idx].a = 255;
        }
    }
#else
    /* OCS: two palette blocks of 16 colours immediately at known offsets.
     * Each MOVE pair is 4 bytes [reg_word][value_word], we want value_word. */
    const uint16_t *cop = (const uint16_t *)_copperListBuffer;
    /* Bank 0 starts at COP_PAL_B0_C0_VAL_OFF (0x72). Each entry is 4 bytes. */
    const uint16_t *bank0 = (const uint16_t *)(_copperListBuffer + 0x72);
    for (int i = 0; i < 16; ++i) {
        uint16_t v = bank0[i * 2];          /* skip the [reg] word */
        uint8_t  r = (uint8_t)(((v >> 8) & 0x0F) * 17);
        uint8_t  g = (uint8_t)(((v >> 4) & 0x0F) * 17);
        uint8_t  b = (uint8_t)(((v >> 0) & 0x0F) * 17);
        s_palette[i].r = r;
        s_palette[i].g = g;
        s_palette[i].b = b;
        s_palette[i].a = 255;
    }
    (void)cop;
#endif
}

/* ---- 8bpp chunky → RGBA ------------------------------------------- */
/* The frontbuffer is now 1 byte per pixel (palette index). Each frame
 * we walk SCREEN_WIDTH x SCREEN_HEIGHT bytes and look each one up in
 * the current palette to write an ARGB8888 pixel into the streaming
 * SDL texture. Same shape as swos-port-c's `updateScreen`. */
static void chunky_to_rgba(uint32_t *dst, int dst_pitch_pixels)
{
    /* Double-buffer semantics — read the *completed* frame, not the
     * one the game is currently drawing into.
     *
     * `SwapBuffersAndCameraViewState()` runs at each VBlank and flips
     * the two pointers: post-swap, `frontbufferBasePtr` is the next
     * draw target (main thread is about to fill it), and
     * `frontbufferBaseAltPtr` is the just-finished frame. Reading
     * `frontbufferBasePtr` means we're reading the buffer main is
     * writing into — any async timer render or even a slow main-path
     * render will catch it half-complete and tear sprites. Reading
     * `frontbufferBaseAltPtr` gives us a stable completed frame and
     * also eliminates the 1-frame lag that the pre-swap ↔ post-swap
     * pointer aliasing hid. `useAltBaseFlag` inverts this when the
     * game code wants to display the drawing target directly (used by
     * transient overlay paths). */
    const uint8_t *fb;
    if (_usePlaneOffsetFlag != 0)
        fb = _frontbufferMaskBuffer;
    else if (_useAltBaseFlag != 0)
        fb = (const uint8_t *)(uintptr_t)_frontbufferBasePtr;
    else
        fb = (const uint8_t *)(uintptr_t)_frontbufferBaseAltPtr;
    if (!fb) return;

    /* Originally applied the Amiga copper Y offset:
     *     fb += cameraYMod320 * FRONTBUFFER_ROW_STRIDE_BYTES;
     * That simulates the Amiga's bitplane-base-pointer vertical scroll,
     * which the match code uses alongside incremental scroll-blits into
     * the wrap region (lines 200–272 of the 273-line frontbuffer).
     *
     * On SDL we now redraw the entire visible pitch to lines 0–199
     * each frame from the chunky tile bank (pitch.c:DrawChunkyPitch),
     * so applying cameraYMod320 here compounds two different scroll
     * schemes and produces "two pitch views stitched vertically"
     * flicker. Always read from line 0 on SDL; cameraYMod320 is
     * irrelevant to the chunky frontbuffer. */
    (void)_cameraYMod320;

    for (int y = 0; y < _screen_height; ++y) {
        const uint8_t *line = fb + (size_t)y * FRONTBUFFER_ROW_STRIDE_BYTES;
        uint32_t      *out  = dst + (size_t)y * dst_pitch_pixels;
        for (int x = 0; x < _screen_width; ++x) {
            SDL_Color c = s_palette[line[x]];
            out[x] = ((uint32_t)0xFFu << 24) |
                     ((uint32_t)c.r  << 16) |
                     ((uint32_t)c.g  <<  8) |
                     ((uint32_t)c.b  <<  0);
        }
    }
}

/* Optional debug dump: every N frames write the converted chunky
 * frame to /tmp/swos_frame_NNNN.ppm. Enabled when the env var
 * SWOS_DUMP_FRAMES is set. Useful for headless smoke-testing. */
static int bytes_nonzero(const uint8_t *p, int n)
{
    int k = 0;
    for (int i = 0; i < n; ++i) if (p[i]) ++k;
    return k;
}

/* F12 screenshot writer. Called from Platform_RenderFrame while the
 * render mutex is held, so the ARGB buffer is guaranteed consistent.
 * Writes to `swos_shotNNN.png` in the current working directory.
 * PNG (via stb_image_write) is used rather than BMP so review tools
 * can read the file directly without an external converter step. */
extern int stbi_write_png(char const *filename, int w, int h, int comp,
                          const void *data, int stride_in_bytes);

static void write_screenshot(const uint32_t *argb)
{
    /* stb_image_write wants RGBA byte order in memory. Our ARGB8888 SDL
     * buffer stores pixels as 0xAARRGGBB uint32, which on LE is bytes
     * BB,GG,RR,AA — i.e. BGRA in memory. Convert to RGBA before write. */
    const int N = _screen_width * _screen_height;
    uint8_t *rgba = (uint8_t *)malloc((size_t)N * 4);
    if (!rgba) {
        fprintf(stderr, "[screenshot] malloc failed\n");
        return;
    }
    for (int i = 0; i < N; ++i) {
        uint32_t p = argb[i];
        rgba[i * 4 + 0] = (uint8_t)(p >> 16); /* R */
        rgba[i * 4 + 1] = (uint8_t)(p >>  8); /* G */
        rgba[i * 4 + 2] = (uint8_t)(p >>  0); /* B */
        rgba[i * 4 + 3] = 0xFF;               /* A (opaque) */
    }
    char path[64];
    snprintf(path, sizeof(path), "swos_shot%03d.png", s_screenshot_index++);
    if (stbi_write_png(path, _screen_width, _screen_height, 4,
                       rgba, _screen_width * 4))
        fprintf(stderr, "[screenshot] wrote %s\n", path);
    else
        fprintf(stderr, "[screenshot] stbi_write_png %s failed\n", path);
    free(rgba);
}

/* Capture what is actually PRESENTED — not what was drawn into the framebuffer.
 *
 * write_screenshot() reads `fb`, which is UPSTREAM of present_frame_copy, so it
 * cannot see the scale, the sub-pixel shift, the HUD strip or the two-pass
 * path. That blind spot is not hypothetical: a framebuffer screenshot looked
 * perfect while the GPU was drawing nothing at all (11d4918, HUD layer never
 * presented). This reads the render output itself, after every draw and before
 * the swap, so it is the only capture that can verify a present-path change.
 *
 * SWOS_PRESENT_CAPTURE=N writes every Nth frame to swos_presentNNN.png at the
 * OUTPUT resolution (letterbox bars included). */
static void capture_presented_frame(void)
{
    static int mod, count, presented;
    if (s_present_capture <= 0 || count >= 60) return;
    /* SWOS_PRESENT_CAPTURE_FROM=F: start at the F-th presented frame, so the 60 files
     * can be spent late in a run (after a match, say) rather than on the boot. */
    if (++presented < s_present_capture_from) return;
    if (++mod < s_present_capture) return;
    mod = 0;

    int ow = 0, oh = 0;
    if (SDL_GetRendererOutputSize(s_renderer, &ow, &oh) != 0 || ow <= 0 || oh <= 0)
        return;

    /* ABGR8888 is 0xAABBGGRR, i.e. R,G,B,A byte order on a little-endian host —
     * exactly what stb_image_write wants, so no conversion pass. */
    uint8_t *px = (uint8_t *)malloc((size_t)ow * oh * 4);
    if (!px) return;
    if (SDL_RenderReadPixels(s_renderer, NULL, SDL_PIXELFORMAT_ABGR8888, px, ow * 4) == 0) {
        char path[64];
        snprintf(path, sizeof path, "swos_present%03d.png", count++);
        if (stbi_write_png(path, ow, oh, 4, px, ow * 4))
            fprintf(stderr, "[present] wrote %s (%dx%d — the presented pixels)\n", path, ow, oh);
        else
            fprintf(stderr, "[present] stbi_write_png %s failed\n", path);
    } else {
        fprintf(stderr, "[present] SDL_RenderReadPixels failed: %s\n", SDL_GetError());
        s_present_capture = 0;
    }
    free(px);
}

static void maybe_dump_frame(const uint32_t *argb)
{
    if (!s_dump_frames) return;
    int n = s_frame_count++;
    if ((n & 0x0F) != 0) return;            /* every 16th frame */
    if (n > 16 * 24) return;                 /* cap at ~24 dumps */
    char path[64];
    snprintf(path, sizeof(path), "/tmp/swos_frame_%04d.ppm", n);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", _screen_width, _screen_height);
    for (int i = 0; i < _screen_width * _screen_height; ++i) {
        uint32_t p = argb[i];
        uint8_t rgb[3] = { (uint8_t)(p >> 16), (uint8_t)(p >> 8), (uint8_t)p };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);

    /* Single-line summary: count distinct colors on a mid-screen row. */
    int d = 0;
    uint32_t seen[16] = {0}; int sn = 0;
    const uint32_t *mid = argb + (_screen_height / 2) * _screen_width;
    for (int i = 0; i < _screen_width; ++i) {
        uint32_t p = mid[i] & 0xFFFFFF;
        int found = 0;
        for (int j = 0; j < sn; ++j) if (seen[j] == p) { found = 1; break; }
        if (!found && sn < 16) { seen[sn++] = p; ++d; }
    }
    fprintf(stderr, "[dump] frame=%d mid-row-distinct=%d\n", n, d);
}

/* ---- Per-frame pump (no pacing). May be called from any thread.
 * The s_render_mtx serializes the renderer + texture access between
 * the game (main) thread and the SDL_AddTimer thread. */
/* Drain SDL's event queue. Called from both the main thread (via
 * Platform_VBlank from WaitForVSync) AND the SDL_Timer thread. The
 * loader-intro spin loops on currentGameTick and never touches
 * WaitForVSync, so main-thread-only polling misses the X-button
 * SDL_QUIT during the intro. In practice SDL_PollEvent is safe to
 * call from multiple threads: the queue is internally locked, events
 * are consumed by whichever pump grabs them first, and Platform_RequestQuit
 * is safe from any thread. */
static void pump_events_any_thread(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
#ifdef __ANDROID__
        if ((ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) &&
            ev.key.keysym.sym == SDLK_AC_BACK) {
            ev.key.keysym.sym = SDLK_ESCAPE;
            ev.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
        }
#endif
        switch (ev.type) {
        case SDL_QUIT:
            Platform_RequestQuit();
            /* Hide the window immediately so the main-menu unwind
             * after a mid-match X-press happens off-screen instead
             * of flashing the menu UI for a frame or two. The audio
             * gate in TryPlayMusic stops the menu music from
             * starting; this kills the visual half. */
            if (s_window) SDL_HideWindow(s_window);
            /* Pause + clear audio at the driver. SDL queues ~80-250 ms
             * of samples ahead of the playback cursor, so without this
             * the crowd chant or whatever's playing leaks past the X
             * click while the game loop unwinds. */
            AudioSDL_PauseAndClear();
            break;
        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_CLOSE) {
                Platform_RequestQuit();
                if (s_window) SDL_HideWindow(s_window);
                AudioSDL_PauseAndClear();
            } else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                /* Alt-Tab takes focus before SDL delivers KEYUP for the
                 * Alt that triggered the switch — so without clearing
                 * here our keyState[AKEY_LALT] / keyBuffer FIFO entry
                 * stay "pressed" indefinitely. On focus return the
                 * loader-intro key-skip and the menu fire-poll see that
                 * stuck press and (a) skip the intro and (b) fire the
                 * highlighted menu entry. Drop everything; new presses
                 * after focus returns will repopulate via KEYDOWN. */
                InputSDL_ClearKeyState();
            } else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                /* Defence-in-depth: also clear on regain in case any
                 * KEYDOWN slipped in between FOCUS_LOST and the actual
                 * loss of input focus. Cheap to do. */
                InputSDL_ClearKeyState();
            }
            break;
        case SDL_KEYDOWN:
#ifdef __ANDROID__
            {
                extern volatile uint16_t _gTextInputActive;
                if (_gTextInputActive) {
                    /* Text fields own punctuation too: +/- must not scale the window. */
                    InputSDL_HandleEvent(&ev);
                    continue;
                }
            }
#endif
            /* ESC at the top-level main menu quits the game; everywhere
             * else it stays passed through to the menu/game-loop layer
             * (in-game pause/end-match menu, sub-menus' EXIT, etc.). */
            if (ev.key.repeat == 0 && ev.key.keysym.sym == SDLK_ESCAPE) {
                extern volatile uintptr_t _currentMenuPtr;
                extern uint8_t _swosMainMenu[];
                extern volatile uint16_t _playGame, _playHighlightFlag;
                extern int32_t Challenge_Armed(void);
                /* A challenge runs directly from the main-menu callback, so
                 * this pointer can still name that menu throughout the match.
                 * Let ESC reach gameplay (including load/exit transitions). */
                if (_currentMenuPtr == (uintptr_t)_swosMainMenu &&
                    !_playGame && !_playHighlightFlag && !Challenge_Armed()) {
                    Platform_RequestQuit();
                    if (s_window) SDL_HideWindow(s_window);
                    AudioSDL_PauseAndClear();
                    continue;   /* swallow — don't also reach the menu */
                }
            }
            if (ev.key.repeat == 0 &&
                (ev.key.keysym.sym == SDLK_F9 ||
                 ev.key.keysym.sym == SDLK_F11 ||
                 ev.key.keysym.sym == SDLK_F12 ||
                 ev.key.keysym.sym == SDLK_PRINTSCREEN)) {
                s_screenshot_request = 1;
            }
            /* Alt+Enter: toggle fullscreen. Swallow the event so it
             * doesn't also reach the menu input layer as ENTER (which
             * would fire the currently-selected button). */
            if (ev.key.repeat == 0 &&
                (ev.key.keysym.sym == SDLK_RETURN ||
                 ev.key.keysym.sym == SDLK_KP_ENTER) &&
                (ev.key.keysym.mod & KMOD_ALT)) {
                Platform_ToggleFullscreen();
                continue;
            }
            /* +/- keys: cycle the windowed scale 1..4. Both the typing-
             * row keys and the keypad ones work. SDLK_EQUALS is the
             * same physical key as SDLK_PLUS on most layouts (US),
             * so we accept either. Swallowed so they don't reach the
             * menu/game layer. No-op while fullscreen (the runtime
             * scaler ignores the size change). */
            if (ev.key.repeat == 0 &&
                (ev.key.keysym.sym == SDLK_EQUALS ||
                 ev.key.keysym.sym == SDLK_PLUS   ||
                 ev.key.keysym.sym == SDLK_KP_PLUS)) {
                Platform_SetWindowScale(s_win_scale_current + 1);
                continue;
            }
            if (ev.key.repeat == 0 &&
                (ev.key.keysym.sym == SDLK_MINUS ||
                 ev.key.keysym.sym == SDLK_KP_MINUS)) {
                Platform_SetWindowScale(s_win_scale_current - 1);
                continue;
            }
            /* Arcade cabinet inputs (MAME convention): 5 = coin P1, 6 = coin P2,
             * F2 = service, 1 = P1 start, 2 = P2 start. Only when arcade mode is
             * on; swallowed so they don't also reach the game/menu layer. */
            {
                extern int _gArcadeMode;
                if (ev.key.repeat == 0 && _gArcadeMode) {
                    SDL_Keycode k = ev.key.keysym.sym;
                    if (k == s_arcade_key_p1_credit) { s_arcade_coin1++;   continue; }
                    if (k == s_arcade_key_p2_credit) { s_arcade_coin2++;   continue; }
                    if (k == s_arcade_key_service)   { s_arcade_service++; continue; }
                    if (k == s_arcade_key_p1_start)  { s_arcade_start1++;  continue; }
                    if (k == s_arcade_key_p2_start)  { s_arcade_start2++;  continue; }
                }
            }
            /* fallthrough */
        case SDL_KEYUP:
        case SDL_TEXTINPUT:
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
        case SDL_CONTROLLERAXISMOTION:
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED:
        case SDL_JOYDEVICEADDED:
        case SDL_JOYDEVICEREMOVED:
            InputSDL_HandleEvent(&ev);
            break;
        default:
            break;
        }
    }
    InputSDL_PostFrame();
}

/* Lightweight pump used by the tick timer when the main thread is
 * rendering — only services window/quit events and hotkeys. Leaves game
 * input (keydown/keyup into keyState + InputSDL_PostFrame) to the main
 * thread so the 50 Hz timer thread doesn't race with it. */
static void pump_quit_only(void)
{
    SDL_Event ev;
    while (SDL_PeepEvents(&ev, 1, SDL_GETEVENT,
                          SDL_QUIT, SDL_QUIT) > 0) {
        Platform_RequestQuit();
        AudioSDL_PauseAndClear();
    }
    while (SDL_PeepEvents(&ev, 1, SDL_GETEVENT,
                          SDL_WINDOWEVENT, SDL_WINDOWEVENT) > 0) {
        if (ev.window.event == SDL_WINDOWEVENT_CLOSE)
            Platform_RequestQuit();
    }
}

/* ---- Sub-pixel scroll presentation ---------------------------------------
 * The game draws at the INTEGER camera position, so a true scroll velocity of
 * 1.5 px/frame renders as an alternating 1,2,1,2 px step (38% of consecutive
 * moving frames change step size — measured on the corpus). Shifting the
 * finished frame by the fractional remainder makes that motion continuous.
 * Everything on screen is drawn relative to the camera, so ONE uniform shift
 * is correct for the whole scene; camera.c publishes the residual.
 *
 * TWO things this must not break:
 *
 * 1. ONE upload, ONE main copy. A previous release tanked on Windows/D3D
 *    because of how the frame reached the GPU; the fix was the single bulk
 *    SDL_UpdateTexture from cached RAM above. This adds no upload and no
 *    per-pixel work — just a float dest rect on the same copy.
 *
 * 2. NO BLUR. We render nearest-neighbour (SDL_HINT_RENDER_SCALE_QUALITY "0")
 *    and a fractional dest under nearest would either snap back to whole
 *    source pixels or, with linear filtering, smear the pixel art. So the
 *    shift is QUANTISED TO WHOLE OUTPUT PIXELS: at integer scale N we get
 *    N steps per source pixel, exactly representable, still crisp. On a 4-5x
 *    window that is a 4-5x finer scroll than today for free.
 *
 * The shift uncovers a sliver at the trailing edge (< 1 source pixel wide).
 * It is filled by re-copying the edge column/row into it — clamp-to-edge, and
 * visually indistinguishable from the neighbouring pixels. Stretching the main
 * rect instead would make nearest duplicate an interior column whose position
 * moves with the fraction, i.e. a shimmer. */
/* camera.c, SDL-only — declared here rather than pulling in game_types.h,
 * which this file deliberately does not include. */
extern int Render_ScrollSubpixel(uint16_t *fx, uint16_t *fy);

/* Compose this frame's HUD into the texture's scratch strip, in the SAME cached
 * RAM buffer the world was just converted into, so it goes to the GPU in the one
 * upload below. Palette index 0 becomes alpha 0, which is what keeps this a real
 * overlay rather than a box stamped on the pitch.
 *
 * ONE UPLOAD is the rule here. Per-pixel work belongs in cached RAM and the frame
 * reaches the GPU in a single SDL_UpdateTexture -- writing the mapped texture
 * directly is ~100x slower on D3D11 and once cost 43 ms/frame (see s_frame_rgba).
 * An overlay in its own texture meant a second upload every frame; folding it into
 * the strip keeps the upload count at one and costs only the extra rows it uses.
 *
 * Returns the number of scratch rows used, so the upload can cover exactly
 * PLATFORM_SCREEN_H + that and no more. */
static int compose_hud_strip(uint32_t *fb, int fbpx)
{
    s_hud_strip_w = s_hud_strip_h = 0;
    if (!hud_layer_pending()) return 0;

    const int x0 = s_hud_x0, y0 = s_hud_y0;
    const int rw = s_hud_x1 - x0, rh = s_hud_y1 - y0;
    if (rw <= 0 || rh <= 0 || rh > PLATFORM_SCREEN_H || rw > fbpx) return 0;

    for (int y = 0; y < rh; y++) {
        const uint8_t *src = s_hud_layer + (size_t)(y0 + y) * _screen_width + x0;
        uint32_t      *dst = fb + (size_t)(PLATFORM_SCREEN_H + y) * fbpx;
        for (int x = 0; x < rw; x++) {
            uint8_t ci = src[x];
            if (ci) {
                SDL_Color c = s_palette[ci];
                dst[x] = ((uint32_t)0xFFu << 24) | ((uint32_t)c.r << 16) |
                         ((uint32_t)c.g << 8)    | (uint32_t)c.b;
            } else {
                dst[x] = 0u;            /* index 0 = transparent */
            }
        }
    }
    s_hud_strip_w = rw; s_hud_strip_h = rh;
    s_hud_dst_x   = x0; s_hud_dst_y   = y0;
    return rh;
}

/* Draw the HUD strip back to its screen position, UNSHIFTED -- the whole point
 * of the layer. Same texture as the world, so no extra upload; blending is
 * enabled only for this draw so the opaque world copy stays on the fast path.
 *
 * `n` is the whole-number source->destination scale of whatever we are drawing
 * into: 1 when that is the screen (SDL's logical size does the scaling for us),
 * or the pre-scale factor when it is the two-pass render target. */
static void present_hud_strip(SDL_Texture *tex, int n)
{
    if (s_hud_strip_w <= 0) return;
    SDL_Rect src = { 0, PLATFORM_SCREEN_H, s_hud_strip_w, s_hud_strip_h };
    SDL_Rect dst = { s_hud_dst_x * n, s_hud_dst_y * n,
                     s_hud_strip_w * n, s_hud_strip_h * n };
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_RenderCopy(s_renderer, tex, &src, &dst);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
}

/* Draw the world -- shifted by the sub-pixel remainder -- and then the HUD on
 * top of it, unshifted, into whatever is currently the render target.
 *
 * `n` is that target's whole-number scale (1 = straight to the screen). qx/qy
 * are the shift ALREADY quantised, in the destination's own units. */
static void draw_world_and_hud(SDL_Texture *tex, int n, float qx, float qy)
{
    const float w = (float)(_screen_width * n);
    const float h = (float)(_screen_height * n);

    /* The texture is TALLER than the screen (PLATFORM_TEX_H) -- the rows below
     * hold the HUD scratch strip -- so every world draw must name the visible
     * region explicitly. A NULL src would stretch the strip into the picture. */
    const SDL_Rect worldSrc = { 0, 0, _screen_width, _screen_height };

    if (qx == 0.0f && qy == 0.0f) {
        if (n == 1) {
            SDL_RenderCopy(s_renderer, tex, &worldSrc, NULL);   /* unchanged fast path */
        } else {
            SDL_FRect d = { 0.0f, 0.0f, w, h };
            SDL_RenderCopyF(s_renderer, tex, &worldSrc, &d);
        }
        present_hud_strip(tex, n);
        return;
    }

    /* The frame was drawn at floor(camera); the true camera is that much
     * further along, so the picture belongs that much further BACK. */
    SDL_FRect dst = { -qx, -qy, w, h };
    SDL_RenderCopyF(s_renderer, tex, &worldSrc, &dst);

    if (qx > 0.0f) {                    /* right sliver: clamp the last column */
        SDL_Rect  src = { _screen_width - 1, 0, 1, _screen_height };
        SDL_FRect d   = { w - qx, -qy, qx, h + qy };
        SDL_RenderCopyF(s_renderer, tex, &src, &d);
    }
    if (qy > 0.0f) {                    /* bottom sliver: clamp the last row */
        SDL_Rect  src = { 0, _screen_height - 1, _screen_width, 1 };
        SDL_FRect d   = { -qx, h - qy, w + qx, qy };
        SDL_RenderCopyF(s_renderer, tex, &src, &d);
    }

    /* The HUD goes on LAST and UNSHIFTED -- the whole point of the layer. */
    present_hud_strip(tex, n);
}

/* The whole number to pre-multiply the frame by before the final resample, or
 * 0 to skip the two-pass path entirely. See the s_scale_target comment block.
 *
 * 0 when the output is already a whole multiple of the logical size, which is
 * every windowed scale -- nearest is exact there and the extra pass would only
 * cost a blit. Rounding to NEAREST rather than up keeps the leftover fraction
 * as close to 1:1 as possible (5.29 -> 5 resamples by 1.06, not 6 -> 0.88). */
static int scale_pass_factor(void)
{
    if (!s_scaling_smooth || !s_scale_targets_ok) return 0;

    /* HARDWARE ONLY. On a GPU pass 1 is one blit the driver runs asynchronously.
     * On the software renderer it is real CPU work in the calling thread, and
     * measured here it costs 2.0 ms/frame at 3440x1440 against 0.006 ms for the
     * single copy (SDL defers the single copy to RenderPresent; a render target
     * cannot be deferred) — 10% of a 20 ms frame before pass 2's linear resample
     * is even counted. The software renderer is the fallback for hosts where the
     * hardware path already misbehaves; it does not get to pay for this. */
    if (!s_renderer_is_hw) return 0;

    float sx = 1.0f, sy = 1.0f;
    SDL_RenderGetScale(s_renderer, &sx, &sy);

    /* Below 1.5x there is not enough output resolution for the pre-scale to
     * buy anything, and at 1x it would only blur. */
    if (sx < 1.5f || sy < 1.5f) return 0;

    /* Already whole on both axes? Then the plain copy is already exact. */
    if (SDL_fabsf(sx - SDL_floorf(sx + 0.5f)) < 0.001f &&
        SDL_fabsf(sy - SDL_floorf(sy + 0.5f)) < 0.001f) return 0;

    float s = (sx > sy) ? sx : sy;
    int   n = (int)SDL_floorf(s + 0.5f);
    if (n < 2) n = 2;
    if (n > 8) n = 8;                   /* 8x of 672x272 is a 46 MB target; enough */
    return n;
}

/* The pass-1 target at factor `n`, rebuilt when `n` changes (a resize or a
 * fullscreen toggle moves the output scale). NULL disables the two-pass path
 * for the rest of the run -- a backend that cannot give us one now will not
 * start being able to. */
static SDL_Texture *scale_target_for(int n)
{
    /* ...or when the PICTURE changes size: a DOS-view match is 320x200 where the menus
     * are 336x272. Keyed on the factor alone, a window where both round to the same
     * one (640x480 stretched: 1.9 and 2.4) kept the menus' target -- the match was
     * drawn into its top-left corner, with the last menu still showing around it
     * (ReportBot 0029's follow-up). */
    static int target_w, target_h;
    if (s_scale_target && s_scale_n == n &&
        target_w == _screen_width && target_h == _screen_height) return s_scale_target;

    if (s_scale_target) {
        SDL_DestroyTexture(s_scale_target);
        s_scale_target = NULL;
        s_scale_n      = 0;
    }

    SDL_Texture *t = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_TARGET,
                                       _screen_width * n, _screen_height * n);
    if (!t) {
        fprintf(stderr, "[render] scaling=smooth: %dx%d render target failed (%s) "
                        "— falling back to the single nearest copy\n",
                _screen_width * n, _screen_height * n, SDL_GetError());
        s_scale_targets_ok = 0;
        return NULL;
    }
    /* LINEAR is the whole point: pass 2 resamples the leftover fraction. The
     * frame textures stay NEAREST -- pass 1 hits them at exactly n, so nearest
     * is not an approximation there, it is the correct answer. */
    SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);

    s_scale_target = t;
    s_scale_n      = n;
    target_w       = _screen_width;
    target_h       = _screen_height;
    if (!s_scale_logged) {
        float sx = 1.0f, sy = 1.0f;
        SDL_RenderGetScale(s_renderer, &sx, &sy);
        fprintf(stderr, "[render] scaling=smooth: output scale %.3fx%.3f is not whole — "
                        "pre-scaling %dx (target %dx%d), then resampling %.3f\n",
                sx, sy, n, _screen_width * n, _screen_height * n, sx / (float)n);
        s_scale_logged = 1;
    }
    return t;
}

static void present_frame_copy(SDL_Texture *tex)
{
    uint16_t fx = 0, fy = 0;
    int shift = s_subpixel_scroll && Render_ScrollSubpixel(&fx, &fy) && (fx || fy);

    int          n  = scale_pass_factor();
    SDL_Texture *rt = n ? scale_target_for(n) : NULL;

    if (rt && SDL_SetRenderTarget(s_renderer, rt) == 0) {
        /* Pass 1. The shift quantises to whole TARGET pixels, i.e. 1/n of a
         * source pixel -- finer than the old floor(outputScale) steps, and
         * exact rather than a fraction that has to survive another scale. */
        float qx = 0.0f, qy = 0.0f;
        if (shift) {
            qx = SDL_floorf((float)fx / 65536.0f * (float)n + 0.5f);
            qy = SDL_floorf((float)fy / 65536.0f * (float)n + 0.5f);
        }
        draw_world_and_hud(tex, n, qx, qy);

        /* Pass 2. Back on the screen SDL restores the logical size it saved
         * when we set the target, so the aspect and letterbox behave exactly
         * as they did before this path existed. */
        SDL_SetRenderTarget(s_renderer, NULL);
        SDL_RenderCopy(s_renderer, rt, NULL, NULL);
        return;
    }
    if (rt) SDL_SetRenderTarget(s_renderer, NULL);   /* the set failed — undo it */

    /* Single-pass: the original path, unchanged.
     *
     * Below 2x there is NO sub-pixel resolution -- the quantiser could only
     * return 0 or a whole pixel, which does not smooth anything. It merely
     * rounds the camera instead of flooring it, and rounding shifts the phase
     * of the 1,2,1,2 pattern without making it any more even, while still
     * costing the trailing-edge sliver. So leave 1x completely alone. */
    float sx = 1.0f, sy = 1.0f, qx = 0.0f, qy = 0.0f;
    SDL_RenderGetScale(s_renderer, &sx, &sy);
    if (shift) {
        if (sx >= 2.0f) qx = SDL_floorf((float)fx / 65536.0f * sx + 0.5f) / sx;
        if (sy >= 2.0f) qy = SDL_floorf((float)fy / 65536.0f * sy + 0.5f) / sy;
    }
    draw_world_and_hud(tex, 1, qx, qy);
}

/* Screenshots and frame dumps read the FRAMEBUFFER, which no longer contains
 * the HUD — it lives in the overlay and is composited by the GPU at present
 * time. Paste it back into a throwaway copy so a capture still looks like the
 * screen. A copy, not fb itself: compositing in place would put the HUD in the
 * world texture as well, and it would then be drawn twice — once shifted,
 * once not — as a ghost. Captures are rare, so the allocation is free.
 * Returns fb unchanged when there is nothing to composite; free via
 * screenshot_frame_release. */
static uint32_t *screenshot_frame_with_hud(uint32_t *fb)
{
    if (!hud_layer_pending()) return fb;
    /* Only the visible frame, not the scratch strip below it. */
    size_t n = (size_t)_screen_width * _screen_height;
    uint32_t *tmp = (uint32_t *)SDL_malloc(n * 4);
    if (!tmp) return fb;                       /* capture without the HUD */
    SDL_memcpy(tmp, fb, n * 4);
    for (int y = s_hud_y0; y < s_hud_y1; y++) {
        const uint8_t *src = s_hud_layer + (size_t)y * _screen_width;
        uint32_t      *dst = tmp + (size_t)y * _screen_width;
        for (int x = s_hud_x0; x < s_hud_x1; x++) {
            uint8_t ci = src[x];
            if (!ci) continue;
            SDL_Color c = s_palette[ci];
            dst[x] = ((uint32_t)0xFFu << 24) | ((uint32_t)c.r << 16) |
                     ((uint32_t)c.g << 8)    | (uint32_t)c.b;
        }
    }
    return tmp;
}

static void screenshot_frame_release(uint32_t *shot, uint32_t *fb)
{
    if (shot != fb) SDL_free(shot);
}

static void render_frame_impl(int pump_full)
{
    /* SDL_PollEvent -> SDL_PumpEvents pulls from the OS window message queue, which on
     * Windows MUST run on the window-owning (main) thread. The SDL_AddTimer worker calls
     * this as a software-renderer fallback (tick_timer_cb) when the main thread stalls
     * >60ms, so off the main thread we pass pump_full=0 and only drain already-queued
     * QUIT/WINDOW events (pump_quit_only / SDL_PeepEvents, thread-safe). Doing the full pump
     * from the worker deadlocks the main thread; an F12 screenshot (which holds s_render_mtx
     * across the blocking PNG file write, stalling main past 60ms) was the reliable trigger. */
    if (pump_full)
        pump_events_any_thread();
    else
        pump_quit_only();
#ifdef SWOS_WASM_BUILD
    /* The ONLY audio pump on WASM (no timer/worker thread), once per jittery
     * emscripten_sleep frame. AudioSDL_Tick mixes one 20 ms tick and early-
     * returns at target, and its silence-pad path is compiled out here — so call
     * it repeatedly to fill the queue to QUEUE_TARGET_MS with REAL audio. The
     * 16 cap (>QUEUE_TARGET_MS/20) bounds the work; extra calls just no-op. */
    for (int i = 0; i < 16; ++i) AudioSDL_Tick();
#else
    AudioSDL_Tick();
#endif

    if (!s_renderer || !s_textures[0]) return;

    SDL_LockMutex(s_render_mtx);

    /* Upload into the next texture in the ring (not the one the GPU is still
     * sampling from a recent frame) — see the s_textures[] comment. */
    SDL_Texture *tex = s_textures[s_tex_idx];
    s_tex_idx = (s_tex_idx + 1) % PLATFORM_TEX_RING;

    /* Render the current Amiga framebuffer INTO CACHED RAM (s_frame_rgba), never
     * into the texture's mapped memory — that's the D3D11 write-combined trap. The
     * buffer is tightly packed so the row pitch in pixels is exactly _screen_width. */
    refresh_palette_from_copper();

    uint32_t *fb   = s_frame_rgba;
    const int fbpx = _screen_width;              /* row pitch, in pixels */

    /* Arcade flag-select / service screens render a full RGBA frame; when one is
     * active it replaces the normal 8bpp path. Otherwise convert the framebuffer
     * and (in attract) composite the logo/INSERT-COIN overlay. */
    int arcade_full = 0;
    { extern int _gArcadeMode;
      extern int Arcade_RenderFullScreen(uint32_t *, int, int, int);
      if (_gArcadeMode)
          arcade_full = Arcade_RenderFullScreen(fb, fbpx, _screen_width, _screen_height); }
    if (!arcade_full) {
        chunky_to_rgba(fb, fbpx);
        { extern int _gArcadeMode;
          extern void Arcade_CompositeOverlay(uint32_t *, int, int, int);
          if (_gArcadeMode)
              Arcade_CompositeOverlay(fb, fbpx, _screen_width, _screen_height); }
    }

    /* Screenshots read the packed RAM buffer directly (always the right stride now,
     * so the old pitch-mismatch skip is gone). Only write the F12 shot from the
     * main-thread render (pump_full); the timer-thread fallback would race the draw. */
    if (s_dump_frames) {
        uint32_t *shot = screenshot_frame_with_hud(fb);
        maybe_dump_frame(shot);
        screenshot_frame_release(shot, fb);
    }
    if (pump_full && s_screenshot_request) {
        uint32_t *shot = screenshot_frame_with_hud(fb);
        write_screenshot(shot);
        screenshot_frame_release(shot, fb);
        s_screenshot_request = 0;
    }
    if (s_screenshot_interval > 0) {
        static int frame_mod, presented, from = -1, cap = 200;
        /* SWOS_SCREENSHOT_FROM=N: start the interval shots at the N-th present, so the
         * 200 of them can cover one moment of a long replay (docs/NETPLAY_GHOSTING.md).
         * SWOS_SCREENSHOT_MAX=N raises the 200, e.g. to film a whole highlight. */
        if (from < 0) {
            const char *v = getenv("SWOS_SCREENSHOT_FROM");
            from = (v && v[0]) ? atoi(v) : 0;
            v = getenv("SWOS_SCREENSHOT_MAX");
            if (v && atoi(v) > 0) cap = atoi(v);
        }
        if (++presented < from)
            goto shots_done;
        if (++frame_mod >= s_screenshot_interval) {
            frame_mod = 0;
            if (s_screenshot_auto_count < cap) {
                uint32_t *shot = screenshot_frame_with_hud(fb);
                write_screenshot(shot);
                screenshot_frame_release(shot, fb);
                s_screenshot_auto_count++;
            } else if (s_screenshot_auto_count == cap) {
                fprintf(stderr,
                    "[screenshot] interval mode hit the %d-shot cap; disabling\n", cap);
                s_screenshot_interval = 0;
                s_screenshot_auto_count++;
            }
        }
    }
shots_done:

    /* Compose the HUD overlay into the scratch strip of the SAME buffer, then
     * hand frame + overlay to the GPU in ONE bulk upload from cached RAM. The
     * rect covers only the rows actually used, so a frame with a small HUD
     * uploads barely more than the screen itself. */
    {
        int stripRows = compose_hud_strip(fb, _screen_width);
        SDL_Rect up = { 0, 0, _screen_width, PLATFORM_SCREEN_H + stripRows };
        SDL_UpdateTexture(tex, &up, fb, _screen_width * 4);
    }

    if (s_stretch)
        apply_picture_scaling();       /* the window may have changed size */
    SDL_RenderClear(s_renderer);
    /* SWOS_PRESENT_STATS=1 reports what the present path costs. The draw calls
     * are what changed here (two-pass scaling adds a blit); RenderPresent is
     * deliberately excluded because on a vsynced hardware renderer it is mostly
     * the wait, which would drown the signal. On the software renderer the
     * pixels are pushed inside these calls, so this IS the cost there. */
    if (s_present_stats) {
        Uint64 t0 = SDL_GetPerformanceCounter();
        present_frame_copy(tex);
        s_present_ticks += SDL_GetPerformanceCounter() - t0;
        if (++s_present_frames >= 250) {
            fprintf(stderr, "[present] %.3f ms/frame in the present path (%d frames)\n",
                    1000.0 * (double)s_present_ticks / (double)s_perf_freq / s_present_frames,
                    s_present_frames);
            s_present_ticks = 0; s_present_frames = 0;
        }
    } else {
        present_frame_copy(tex);
    }
    /* CRT effects go on LAST, over the finished frame and its HUD, so the
     * grille and the glass edge sit on the picture rather than under it. */
    CrtEffects_Apply(s_renderer);
    capture_presented_frame();     /* after every draw, before the swap */
    SDL_RenderPresent(s_renderer);

    SDL_UnlockMutex(s_render_mtx);
}

void Platform_RenderFrame(void)
{
    render_frame_impl(1);   /* public entry: runs on the main thread, full SDL event pump */
}

/* ---- Full vblank: render + 50 Hz pacing ---------------------------
 * Called from WaitForVSync on the game/main thread. Pumps SDL events
 * here (main thread only) before rendering the frame. */
static void (*s_frame_hook)(void);

void Platform_SetFrameHook(void (*hook)(void))
{
    s_frame_hook = hook;
}

/* SWOS_KEY_SCRIPT="<frame>:<key> <frame>:<key> ..." (or comma-separated) -- press each key on that frame of
 * Platform_VBlank and release it three frames later. Key names are SDL's (Return,
 * Escape, Up, Down, Left, Right, Space ...). For driving a menu with nobody at the
 * keyboard: pair it with SWOS_PRESENT_CAPTURE to see what the keys did. Frame counts
 * depend on load times, so leave generous gaps. */
enum { kKeyScriptMax = 128 };
static struct { uint32_t frame; SDL_Keycode key; } s_key_script[kKeyScriptMax];
static int      s_key_script_len = -1;      /* -1 = not read yet */
static uint32_t s_key_script_frame;

static void push_key(SDL_Keycode key, int down)
{
    SDL_Event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    ev.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    ev.key.keysym.sym = key;
    ev.key.keysym.scancode = SDL_GetScancodeFromKey(key);
    SDL_PushEvent(&ev);
}

static void key_script_tick(void)
{
    if (s_key_script_len < 0) {
        const char *spec = getenv("SWOS_KEY_SCRIPT");
        s_key_script_len = 0;
        while (spec && *spec && s_key_script_len < kKeyScriptMax) {
            char name[32];
            unsigned frame;
            int used = 0;
            /* Entries are separated by spaces OR commas: a test harness that passes
             * environment through a word-split variable (relay_test.sh's HOST_ENV)
             * cannot carry spaces. */
            while (*spec == ',' || *spec == ' ')
                spec++;
            if (sscanf(spec, "%u:%31[^ ,]%n", &frame, name, &used) != 2 || used == 0)
                break;
            s_key_script[s_key_script_len].frame = frame;
            s_key_script[s_key_script_len].key   = SDL_GetKeyFromName(name);
            if (s_key_script[s_key_script_len].key == SDLK_UNKNOWN)
                fprintf(stderr, "[keys] SWOS_KEY_SCRIPT: unknown key '%s'\n", name);
            else
                s_key_script_len++;
            spec += used;
        }
        if (s_key_script_len > 0)
            fprintf(stderr, "[keys] SWOS_KEY_SCRIPT: %d key presses\n", s_key_script_len);
    }
    if (s_key_script_len == 0)
        return;
    for (int i = 0; i < s_key_script_len; i++) {
        if (s_key_script[i].frame == s_key_script_frame) {
            push_key(s_key_script[i].key, 1);
            fprintf(stderr, "[keys] frame %u: %s\n", (unsigned)s_key_script_frame,
                    SDL_GetKeyName(s_key_script[i].key));
        }
        if (s_key_script[i].frame + 3 == s_key_script_frame)
            push_key(s_key_script[i].key, 0);
    }
    s_key_script_frame++;
}

int Platform_VBlank(void)
{
    /* Drive the Amiga Level-3 ISR from the main thread too, in case the
     * SDL_Timer worker thread is starved (common on WSL2 where main-
     * thread spin loops can monopolise CPU). The throttle inside the
     * wrapper means double-firing from both threads is harmless. */
    RunInt3HandlerThrottled();
    if (s_frame_hook)
        s_frame_hook();
    return Platform_Present();
}

#ifdef SWOS_WASM_BUILD
/* Platform_DelayNextFrame's wait, for the browser's own pacing (Platform_Present). */
static double s_wasm_extra_ms;
#endif

/* Catch-up after a stall. Offline the limiter drops a backlog of more than one frame:
 * a burst of frames after a hitch only looks worse. A LIVE match sets it to its
 * prediction window (netplay_match.c) and back to 1 when it ends, on thd79's
 * advice: there a game that stalled and then dropped the frames comes back BEHIND,
 * and it is the OTHER player who pays -- held at the prediction limit through the
 * stall, then slowed by time sync until the two are level. Catching up on as many
 * frames as the other game can be ahead puts the cost on the player who stalled. */
void Platform_SetCatchUpFrames(int frames)
{
    s_catchup_frames = frames < 1 ? 1 : frames;
}

void Platform_DelayNextFrame(uint32_t us)
{
#ifdef SWOS_WASM_BUILD
    /* The browser paces with its own deadline, below, not s_next_frame_perf: without
     * this a netplay time sync's "slow down a little" went nowhere, the browser that
     * ran ahead stayed ahead, and its peer rolled back (a clean two-browser match:
     * 31 rollbacks on one side, 1 on the other, 2 waits asked for and not taken). */
    s_wasm_extra_ms += us / 1000.0;
    return;
#endif
    if (s_vsync_enabled && s_display_hz > 0 && s_display_hz == Platform_ActivePacingFps()) {
        if (us >= 1000)
            SDL_Delay(us / 1000);
        return;
    }
    s_next_frame_perf += (Uint64)us * s_perf_freq / 1000000u;
}

/* Platform_VBlank without the VBlank interrupt: show the finished frame and pace. A
 * netplay match runs the interrupt inside its frame step instead, exactly once per
 * simulated frame (src/netplay_match.c). */
/* SWOS_PRESENT_STATS=1, the other half: how EVENLY frames are shown. The cost above
 * says whether a frame fits its budget; this says whether the frames arrive on a
 * steady beat, which is what a player sees as smooth or stuttering. Measured between
 * successive main-thread presents, against the rate the game is pacing at, so a
 * replay at its own recorded rate is judged against that rate. */
static void present_interval_stats(void)
{
    static Uint64 s_last, s_sum, s_min, s_max;
    static int32_t s_n, s_long, s_short;
    Uint64 now = SDL_GetPerformanceCounter();
    if (s_last) {
        Uint64 d = now - s_last;
        double period = (double)s_perf_freq / (double)Platform_ActivePacingFps();
        if (!s_n || d < s_min) s_min = d;
        if (d > s_max) s_max = d;
        s_sum += d;
        if ((double)d > period * 1.5) s_long++;
        if ((double)d < period * 0.5) s_short++;
        if (++s_n >= 250) {
            double ms = 1000.0 / (double)s_perf_freq;
            fprintf(stderr, "[present] interval %.2f ms mean, %.2f..%.2f ms, target %.2f ms "
                            "(%d Hz): %d long (>1.5x), %d short (<0.5x) of %d\n",
                    (double)s_sum / s_n * ms, (double)s_min * ms, (double)s_max * ms,
                    period * ms, Platform_ActivePacingFps(), s_long, s_short, s_n);
            s_sum = s_max = 0; s_n = s_long = s_short = 0;
        }
    }
    s_last = now;
}

int Platform_Present(void)
{
#ifdef __ANDROID__
    while (SDL_AtomicGet(&s_android_background) && !_quitToDos) {
        pump_events_any_thread();
        SDL_Delay(20);
    }
#endif
    /* Stamp first so the async timer callback sees a recent main-thread
     * render as soon as we enter RenderFrame — prevents the timer from
     * firing a concurrent render while we're holding the mutex. */
    s_last_main_render_ms = SDL_GetTicks();
    /* Here, not in Platform_VBlank: a live netplay match presents its frames
     * directly, so a key script ticked only from VBlank stopped at kick-off and
     * could never press a key during an online match. */
    key_script_tick();
    if (s_present_stats)
        present_interval_stats();
    Platform_RenderFrame();

#ifdef SWOS_WASM_BUILD
    /* The browser owns the event loop, so a C program must hand control back
     * every frame or the tab freezes. ASYNCIFY rewinds the C stack here and
     * resumes later — turning "render this frame, return to the browser" out of
     * the same blocking loop the native build runs. This ONE yield paces every
     * blocking loop in the game (match loop, menus, fades, replays), so no
     * per-loop rewrite is needed.
     *
     * Pace to a FIXED frame period that ABSORBS the frame's own work time. A plain
     * emscripten_sleep(period) sleeps the whole period AFTER rendering, so every
     * frame actually took work + period and the real rate fell well below target
     * (~35 fps). And because WASM has no timer thread, the game clock
     * (_currentGameTick) advances exactly once per Platform_VBlank — so a low frame
     * rate makes the whole game run in slow-motion. Sleep only (deadline - now), with
     * an accumulating deadline that self-corrects for setTimeout jitter; if a slow
     * frame overruns, resync instead of building up a backlog (no spiral of death). */
    {
        double period = 1000.0 / Platform_ActivePacingFps();
        static double s_frame_deadline;
        double now = emscripten_get_now();
        if (s_frame_deadline <= 0.0) s_frame_deadline = now;   /* first frame */
        s_frame_deadline += period;
        double sleep_ms = s_frame_deadline - now;
        if (sleep_ms < 1.0) {                 /* work overran the frame budget */
            sleep_ms = 1.0;                   /* still yield so the browser paints + polls input */
            /* Drop the backlog -- beyond a live match's catch-up (Platform_SetCatchUpFrames),
             * which runs back to back, as the native limiter does. */
            if (now - s_frame_deadline > period * (s_catchup_frames - 1))
                s_frame_deadline = now + period - period * (s_catchup_frames - 1);
        } else if (sleep_ms > period) {       /* first frame / clock jumped back */
            sleep_ms = period;
            s_frame_deadline = now + period;
        }
        /* A wait netplay asked for (Platform_DelayNextFrame): this frame waits longer
         * and the whole schedule moves with it, as the native limiter's does. */
        if (s_wasm_extra_ms > 0.0) {
            double extra = s_wasm_extra_ms > 100.0 ? 100.0 : s_wasm_extra_ms;
            s_wasm_extra_ms = 0.0;
            sleep_ms += extra;
            s_frame_deadline += extra;
        }
        emscripten_sleep((unsigned)(sleep_ms + 0.5));
    }
    return _quitToDos ? 1 : 0;
#endif

    /* 50 Hz pacing — wait until the next frame boundary on the
     * high-resolution counter. Hybrid sleep + spin: SDL_Delay() for the
     * bulk of the wait (cheap on CPU, releases the core), then a busy
     * spin for the last ~2 ms to land on the deadline accurately. Even
     * with SDL_HINT_TIMER_RESOLUTION=1, SDL_Delay can still wake up
     * 1–2 ms late depending on what the OS scheduler is doing, and that
     * jitter is exactly what looks like "uneven framerate" to a player.
     *
     * Catch-up cap: if we fell behind by more than one frame (e.g. the
     * main thread blocked on a slow asset load), reset the deadline to
     * "now" instead of trying to render N frames back-to-back to catch
     * up — which would just make the next batch of frames feel even
     * more uneven. The 50 Hz audio tick is locked to the same loop so
     * skipping a catch-up keeps music time-aligned with game state. */
    /* Turbo skips the 50 Hz real-time wait entirely: under gdb we drive the
     * inferior frame-by-frame and want frames CPU-bound, not paced to a wall
     * clock the WSL2 scheduler stretches unpredictably. Skipping the
     * SDL_Delay + spin removes the per-frame real-time cost that
     * "timer-thread starvation" was ballooning to 10–120 s per cont. */
    if (s_harness_turbo)
        return _quitToDos ? 1 : 0;

    /* PACE ONCE, NEVER TWICE.
     *
     * When vsync is on AND the monitor refreshes at exactly our frame rate,
     * SDL_RenderPresent above already blocked for precisely one frame — it IS
     * the clock, and it is a better one than any timer, because it is the
     * display's own. Sleeping to a software deadline on top of that is pure
     * damage:
     *
     *  - It adds latency. The sleep sits between the present and the next
     *    frame's input poll, so everything the player does is seen that much
     *    later. A tester on a 50 Hz screen reported exactly this: "with vsync
     *    ON everything is totally smooth... but the input lag seems pretty
     *    huge, bigger than the +0.5 frames you expect from vsync".
     *  - It drifts. Our deadline advances by exactly 1/fps while the panel
     *    runs at its own slightly-different rate, so the gap between the two
     *    walks through the whole frame interval and periodically pushes the
     *    next present past a refresh — a hitch, then it repeats.
     *
     * Only when the two rates are EQUAL. At 60 Hz with a 50 Hz game, or any
     * multiple, present-alone would run the game at the monitor's rate, so the
     * limiter still has to gate it (SDL2's renderer vsync is swap-interval 1;
     * there is no "present every Nth refresh"). This is what Ruff n Tumble's
     * loop does too — vsync XOR a precise sleep, never both.
     *
     * The deadline is kept fresh so a later mode change (a DOS-gameplay
     * toggle, a replay at another rate) does not resume against a stale one. */
    if (s_vsync_enabled && s_display_hz > 0 &&
        s_display_hz == Platform_ActivePacingFps()) {
        s_next_frame_perf = SDL_GetPerformanceCounter();
        return _quitToDos ? 1 : 0;
    }

    /* Read the frame rate live so an in-game DOS-gameplay toggle — or a loaded
     * replay's own recorded framerate — re-paces each frame. */
    k_frame_secs = 1.0 / (double)Platform_ActivePacingFps();
    s_next_frame_perf += (Uint64)((double)s_perf_freq * k_frame_secs);
    Uint64 now = SDL_GetPerformanceCounter();

    {
    const Sint64 frame_perf = (Sint64)((double)s_perf_freq * k_frame_secs);
    if ((Sint64)(now - s_next_frame_perf) > frame_perf * s_catchup_frames) {
        /* Further behind than we catch up on (one frame, or a live match's prediction
         * window: Platform_SetCatchUpFrames) — drop the rest, and run what is left
         * back to back. */
        s_next_frame_perf = now - (Uint64)(frame_perf * (s_catchup_frames - 1));
    } else {
        /* Sleep almost-to-deadline, leaving a 2 ms margin for the spin. */
        const Uint64 spin_margin = (Uint64)((double)s_perf_freq * 0.002);
        if (s_next_frame_perf > now + spin_margin) {
            double secs = (double)(s_next_frame_perf - now - spin_margin)
                          / (double)s_perf_freq;
            Uint32 ms = (Uint32)(secs * 1000.0);
            if (ms > 0) SDL_Delay(ms);
        }
        /* Spin out the remainder. SDL_Delay typically wakes 0–2 ms late
         * with TIMER_RESOLUTION=1; this loop is at most ~2 ms of spin. */
        while (SDL_GetPerformanceCounter() < s_next_frame_perf) {
            /* tight loop — SDL_GetPerformanceCounter is a CPU register read */
        }
    }
    }

    return _quitToDos ? 1 : 0;
}

Uint64 Platform_NowMs(void)
{
    return SDL_GetTicks64();
}
