/*
 * platform_sdl.h -- SDL host services exposed to the ported Amiga code.
 *
 * Pure-C interface (no C++). Anything that used to talk to the Amiga
 * custom chips, CIA, DOS library, or AmigaOS goes through this header.
 */

#ifndef PLATFORM_SDL_H
#define PLATFORM_SDL_H

#include <SDL.h>

/* Native SWOS resolution. Matches display.h. */
#define PLATFORM_SCREEN_W 336
#define PLATFORM_SCREEN_H 272

/* ---- Lifecycle ----------------------------------------------------- */
/* Renderer selection.
 *
 * Software (PLATFORM_RENDERER_SOFTWARE):
 *   SDL_RENDERER_SOFTWARE. The SDL_AddTimer worker thread is allowed to
 *   call Platform_RenderFrame as a fallback during loader-intro spins
 *   that don't go through WaitForVSync. Cross-thread render is safe
 *   because software renderers aren't context-bound. PRESENTVSYNC has
 *   no effect on software renderers — frames are not synced to monitor
 *   refresh, so 50 Hz monitor users see a slow drift / beat pattern.
 *
 * Accelerated (PLATFORM_RENDERER_ACCEL):
 *   SDL_RENDERER_ACCELERATED, optionally with SDL_RENDERER_PRESENTVSYNC.
 *   Hardware contexts (D3D / OpenGL / Metal) are bound to the creating
 *   thread, so the cross-thread fallback render is suppressed; only
 *   Platform_VBlank from the main thread issues GL calls. With vsync
 *   the monitor's vblank gates SDL_RenderPresent, giving free perfect
 *   pacing on a 50 Hz display. The 50 Hz manual SDL_Delay loop in
 *   Platform_VBlank stays as a backstop so 60+ Hz monitors still gate
 *   the game at 50 ticks/sec.
 */
typedef enum {
    PLATFORM_RENDERER_SOFTWARE = 0,
    PLATFORM_RENDERER_ACCEL    = 1,
} PlatformRendererMode;

/* Fullscreen mode selector.
 *
 * NONE:      Windowed at win_scale (Alt+Enter toggles to DESKTOP at runtime).
 * DESKTOP:   SDL_WINDOW_FULLSCREEN_DESKTOP — borderless window sized to the
 *            current desktop, letterboxed via SDL_RenderSetLogicalSize. No
 *            display-mode change. Fast to toggle, but on Windows XP the
 *            taskbar can occasionally show through above this borderless
 *            window — use EXCLUSIVE if you need the screen completely
 *            covered there.
 * EXCLUSIVE: SDL_WINDOW_FULLSCREEN — true fullscreen with display-mode
 *            change. Always covers the taskbar. Slower toggle, may flicker
 *            on dual-monitor setups, and the desktop resolution is
 *            restored on exit. */
typedef enum {
    PLATFORM_FULLSCREEN_NONE      = 0,
    PLATFORM_FULLSCREEN_DESKTOP   = 1,
    PLATFORM_FULLSCREEN_EXCLUSIVE = 2,
} PlatformFullscreenMode;

enum { VIEW_STYLE_AUTO = 0, VIEW_STYLE_AMIGA = 1, VIEW_STYLE_DOS = 2 };

typedef struct {
    int                    win_scale;        /* 1, 2, 3, 4 */
    PlatformFullscreenMode fullscreen;       /* none / desktop / exclusive */
    PlatformRendererMode   renderer;         /* software / accelerated */
    int                    vsync;            /* 0 = off (default), 1 = on (accel only) */
    int                    subpixel_scroll;  /* 1 = shift the presented frame by the camera's sub-pixel
                                              * remainder so scrolling is continuous instead of stepping
                                              * 1,2,1,2 px (default 1). Quantised to whole output pixels,
                                              * so it never blurs. PC-only; see platform_sdl.c. */
    int                    scaling_smooth;   /* 1 = two-pass scaling (default): pre-multiply the frame
                                              * by a whole number with NEAREST, then let the leftover
                                              * fraction resample LINEARLY, so a non-integer fullscreen
                                              * scale stops giving one source pixel 4 output pixels and
                                              * its neighbour 3. 0 = the old single nearest copy.
                                              * No-ops when the output is already a whole multiple
                                              * (every windowed scale is). PC-only; see platform_sdl.c. */
    /* CRT presentation effects (PC-only; see include/crt_effects.h). All are
     * applied to the FINISHED frame, so none of them touch the simulation. */
    int                    crt_mask;         /* 0 off, 1 aperture, 2 slot, 3 scanlines */
    int                    crt_mask_strength;/* 0..100 — how dark the grille's off-phase is */
    int                    crt_vignette;     /* 0..100 — corner darkening, 0 = off */
    int                    crt_corners;      /* 0..100 — rounded-glass corner size, 0 = square */
    char                   render_driver[16];/* accel backend override: "" / "auto" = SDL default (D3D11 on Windows);
                                              * "opengl" / "direct3d11" / "direct3d" / "metal" force one. Windows D3D11
                                              * serialises presents on CPU-idle frames (replays crawl); "opengl" avoids it. */
    int                    res_w, res_h;     /* resolution driving the render aspect; 0,0 = auto (desktop). PC-only */
    int                    screen;           /* target display index (0 = primary). The window opens on / fullscreens on this monitor and its size is auto-fitted to that screen's usable bounds. Clamped to a valid display. PC-only */
    int                    audio_mono;       /* 1 = down-mix to mono (default), 0 = Amiga panned */
    int                    dos_graphics;     /* 1 = render DOS-version graphics. PC-only */
    int                    jag_graphics;     /* 1 = the Atari Jaguar's sprites (bin/grafs_jag) */
    /* Sprite shading (docs/PALETTE_V2.md §5). Both off is the 16-colour Amiga
     * look exactly, which is the point: the shaded sprites become the default
     * renderer and the flat look becomes a setting. */
    /* Which view the match is played with. NOT a display setting: the view size
     * feeds the onScreen cull, so it decides which players auto-selection may
     * pick -- "amiga" and "dos" are different games, which is the point.
     * AUTO follows the gameplay style, so one switch gives one authentic
     * experience and the hybrids have to be asked for by name. */
    int                    view_style;
    /* 1 = widen the render to the display's shape, showing more margin at the
     * sides; 0 = the view's own width (336 Amiga, 320 DOS), pillarboxed; 2 =
     * that own width STRETCHED to fill the display. Unlike view_style this does
     * NOT change the game: the simulation view is pinned either way, so the
     * extra columns are margin the simulation cannot see. */
    int                    widescreen;
    int                    pitch_maps;       /* 1 = a DOS pitch's picture from <dir>/pitch<N>.tmj (pitch_map.h) */
    int                    outfit_shading;   /* 1 = shade the kit, 0 = flat team colour */
    int                    shadow_shading;   /* 1 = shade the drop shadow, 0 = SWOS's colour 8 */
    int                    hair_shading;     /* 1 = shade hair the game leaves flat (black), 0 = as the game has it */
    int                    shade_step;       /* sixteenths between kit shades: 16, 16-N, 16-2N, 16-3N; hair too */
    int                    kit_art;          /* the UNIFORM screen's kit drawings: 0 = the Amiga's, 1 = the Jaguar's (kit_art.h) */
    int                    sound_style;      /* a match's sounds: 0 = the Amiga's, 1 = the PC game's samples (dos_sound.h) */
    char                   commentary_dir[260]; /* the folder of commentary categories; "" = sound_dos/commentary */
    int                    menu_recording;   /* with music_style 1: 1 = play sound_dos/menu.mp3, 0 = menu.xmi on the emulated chip */
    int                    music_style;      /* the menus' music: 0 = the Amiga's, 1 = the PC game's menu.xmi (dos_music.h) */
    int                    commentary;       /* the PC game's commentary: 0 off, 1 on (over the Amiga's sounds too), 2 auto = with sound_style 1 */
    int                    dos_gameplay;     /* 1 = DOS gameplay tweaks (framerate etc.). PC-only; reserved */
    int                    contest_own_ratings; /* 1 = a contested ball's dice rates each side by the man at the
                                              * ball; 0 (default) = the original's squad-slot lookup
                                              * (ball_possession.c, _gContestOwnRatings). Offline only. */
    int                    fire_hold_lockout;/* 1 = keep the ORIGINAL Amiga bug where an OPPONENT's tackle
                                              * kills your held fire button's shot charge for good. 0 (default)
                                              * = fixed. See _gOriginalFireHoldLockout in team_control.c */
    int                    cpu_cards;        /* 1 (default) = a computer side can be booked and sent off;
                                              * 0 = the ORIGINAL's rule where a team with no human controller
                                              * never gets a card. See _gCpuTeamsGetCards in tackle_foul.c */
    int                    replay_auto_save; /* 1 = keep a dated .SRP of every match, not only LATEST.SRP */
    char                   matchmaker[80];   /* [netplay] matchmaker: the server MULTIPLAYER logs in to,
                                              * "host" or "host:port". Typed on the login menu and saved
                                              * there; empty = a server on this machine. */
    int                    stick_deadzone;   /* [input] stick_deadzone: how far an analog stick must be
                                              * pushed, in percent of its travel, before it counts */
    char                   key_bind[5][24];  /* [input] key_up/_down/_left/_right/_fire: the player's own
                                              * keys, SDL scancode names, beside the built-in ones.
                                              * Empty = none (input_sdl.c) */
    int                    latch_presses;    /* [input] latch_presses: a fire press that came and went
                                              * between two reads still counts (default on) */
    char                   netplay_name[33]; /* [netplay] name: the name typed on the MULTIPLAYER login
                                              * screen, kept while REMEMBER LOGIN INFO is on */
    int                    netplay_remember; /* [netplay] remember_login: that switch (default on) */
    char                   record[80];       /* [netplay] record: stream every live match to a
                                              * recorder -- "server" (the matchmaker), "host[:port]",
                                              * or empty/"off". include/match_record.h */
    int                    prediction;       /* [netplay] prediction: PRED/SEEN in a live match's .SRP */
    int                    no_injuries_cards; /* [testing] no_injuries_cards: offline only (test_rules.h) */
    int                    netplay_balance;  /* [netplay] balance: the two players share the guessing (docs/NETPLAY_BALANCE.md) */
    int                    reports;          /* [netplay] reports: SEND MATCH REPORTS -- both players'
                                              * logs and ratings to the server after an online
                                              * match. include/match_report.h */
    int                    arcade;           /* 1 = arcade/coin-op mode — boot to attract. PC-only. See docs/ARCADE.md */
    int                    arcade_mode;         /* arcade game mode index (0 = GOLDEN GOAL) */
    int                    arcade_game_seconds; /* arcade match length in seconds (default 45) */
    int                    arcade_golden_goal;  /* 1 = first goal wins the match (default 1) */
    int                    arcade_penalties;    /* 1 = penalty shootout when drawn (default 1) */
    int                    arcade_goal_replay;  /* 1 = replay goals (drives _autoReplays; default 1) */
    int                    arcade_attract_audio;/* attract audio: 0 = music (HERO), 1 = sfx (reel crowd), 2 = music+sfx (default) */
    int                    arcade_gameplay;     /* arcade gameplay feel: 0 = amiga (50fps), 1 = dos (70fps) */
    char                   arcade_key_p1_credit[16]; /* cabinet key bindings (SDL key names; */
    char                   arcade_key_p1_start[16];  /*   default MAME layout 5/6/1/2/F2). */
    char                   arcade_key_p2_credit[16]; /*   Empty -> compiled default. PC-only */
    char                   arcade_key_p2_start[16];
    char                   arcade_key_service[16];
    char                   dos_dir[256];     /* DOS asset dir (default "grafs_dos"). PC-only */
    char                   language[16];     /* UI language code, e.g. "en" — selects data_json/translations/<lang>.json */
    char                   assets_reminder_off[64]; /* [general] assets_reminder_off: the data versions whose "update your
                                                     * game data" reminder was dismissed (src/assets_check.c), "" none */
    char                   currency[8];      /* "pound" (default) or "dollar" — money symbol */
} PlatformInitOptions;

int  Platform_Init(const PlatformInitOptions *opts);
void Platform_Shutdown(void);

/* Toggle / set fullscreen at runtime. Alt+Enter calls this internally;
 * exposed here in case a future settings menu needs a programmatic hook.
 * Toggles between windowed and the fullscreen flavour the user launched
 * with (--fullscreen=desktop or =exclusive). */
void Platform_ToggleFullscreen(void);

/* Set the windowed-mode integer scale factor (1..4) at runtime. Called
 * from the +/- key handlers. No-op while fullscreen. */
void Platform_SetWindowScale(int scale);

/* The picture's size has changed (the DOS view is 320x200, menus are 336x272),
 * so the presenter must letterbox the new shape. The WINDOW is deliberately not
 * resized: it stays the size the menus need, and a smaller match view sits
 * inside it, rather than the window jumping about at every kick-off. */
void Platform_ViewChanged(void);

/* Snapshot the current windowed scale (1..4) and current fullscreen
 * mode. Used by the settings-save path so SWOS.ini round-trips reflect
 * runtime adjustments (Alt+Enter, +/-) — not just whatever was on the
 * command line at launch. */
int                    Platform_GetWindowScale(void);
PlatformFullscreenMode Platform_GetFullscreenMode(void);

/* Full-fat vblank: pump events, render the bitplanes, present, and
 * pace to the next 50 Hz boundary. Intended to run on the main thread
 * from game-loop waits. Returns non-zero if the user closed the window. */
int  Platform_VBlank(void);
/* Platform_VBlank without running the VBlank interrupt: present the finished frame and
 * pace. For a caller that runs the interrupt itself, once per simulated frame. */
int  Platform_Present(void);
/* The entry of the palette last shown nearest an RGB colour (not entry 0). */
uint8_t Platform_NearestColour(uint8_t r, uint8_t g, uint8_t b);
/* Make the next frame `us` microseconds longer than the frame rate says -- netplay's frame
 * pacing (src/netplay_match.c). The limiter keeps deadlines, so a plain sleep between
 * frames is taken back out of the next one and changes nothing; this moves the deadline.
 * When the display itself paces (vsync at the game's rate) there is no deadline to move,
 * and it sleeps instead. */
void Platform_DelayNextFrame(uint32_t us);
/* How many frames late the frame limiter still catches up on (1 = drop any backlog
 * past one frame, the default). A live match sets its prediction window. */
void Platform_SetCatchUpFrames(int frames);

/* A function Platform_VBlank calls every frame, whatever is on screen -- or NULL.
 * For a connection that must not go quiet while a modal menu owns the loop: the
 * netplay lobby's link while a player browses the team list (src/online_menus.c).
 * Keep it cheap, and keep it away from game state: it also runs on the frames of a
 * local match. */
void Platform_SetFrameHook(void (*hook)(void));

/* Just pump SDL events, render bitplanes, and present. No pacing.
 * Safe to call from the SDL_AddTimer thread. */
void Platform_RenderFrame(void);

/* Run the Amiga Level-3 VBlank ISR body, throttled to ~50 Hz max and
 * mutex-guarded so concurrent calls from the timer thread and the main
 * thread serialise. Use this from main-thread spin loops on flags the
 * ISR sets (fadeState, currentGameTick, waitingInterrupt) when calling
 * WaitForVSync isn't appropriate (e.g. you don't want to render a
 * frame). On WSL2 the SDL_Timer thread can be starved indefinitely by
 * a busy main thread, so the spin needs to drive the ISR itself. */
void RunInt3HandlerThrottled(void);

/* Force a quit (set by SDL_QUIT or escape). The Amiga code polls
 * `_quitToDos` -- we set it from here. */
void Platform_RequestQuit(void);

/* ---- Hardware register stubs --------------------------------------- *
 * The Amiga code talks to chip registers through `volatile struct Custom *custom`.
 * We provide a real allocated struct so writes are no-ops but reads return
 * something sensible. The struct layout matches display.h.
 *
 * NOTE: display.h declares `extern volatile struct Custom *custom;` and the
 * struct itself, so we don't redeclare them here -- platform_amiga_stubs.c
 * provides the storage and pointer. */

/* ---- Misc helpers -------------------------------------------------- */
/* Returns the host's high-resolution monotonic millisecond counter. */
Uint64 Platform_NowMs(void);

/* Ask the OS for a 1 ms sleep granularity (src/platform_timer.c). Only Windows
 * needs it; call it before anything sleeps. Returns 0 if the request failed. */
int32_t Platform_RaiseTimerResolution(void);

#endif /* PLATFORM_SDL_H */
