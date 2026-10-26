/*
 * main.c -- process entry point for BOTH targets of the united SWOS port.
 *
 * The two hosts share no startup code at all, so the file is split on
 * SWOS_SDL_BUILD (defined by include/swos_platform.h, which the SDL build
 * force-includes via -include; the m68k build never defines it):
 *
 *   SWOS_SDL_BUILD  -> the SDL2 host: init video/audio/events/gamepad, create
 *                      the window + paletted texture, then call the ported
 *                      Amiga _start / mainC bootstrap.
 *   else            -> the Amiga host: the -nostdlib libc stubs the m68k
 *                      build needs, then startShim() into the original
 *                      assembly bootstrap.
 */

#ifdef SWOS_SDL_BUILD


#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>          /* SIGHUP — see main() (Cygwin-under-gdb startup) */

#ifdef _WIN32
#include <windows.h>
#include <direct.h>          /* _chdir */
#define chdir(p) _chdir(p)
#else
#include <unistd.h>          /* readlink, chdir */
#endif

#include "test_rules.h"
#include "platform_sdl.h"
#include "swos_settings.h"
#include "swos_io.h"            /* swos_save_dir_init() — macOS-writable save dir */
#include "save_format_json.h"   /* DataJson_SelfTest */
#include "dos_mode.h"   /* gDosGraphics/gDosGameplay — defined in dos_graphics.c */
#include "display.h"    /* _screen_width, WIDESCREEN_WIDTH, SCREEN_WIDTH */
#include "replay_test.h"        /* --replay-test: recorded-game regression rig */
#include "replay_v2.h"          /* --record-replay, --view-replay: whole-match .SRP replays */
#include "online_menus.h"       /* --menu, --menu-selftest */
#include "netplay.h"            /* --netplay-selftest */
#include "build_info.h"         /* --version, and the [build] line in SWOS.log */
#include "p2p_link.h"           /* --netplay-selftest: the MULTIPLAYER menu's link */
#include "mm_client.h"          /* --matchmaker-selftest */
#include "match_record.h"        /* [netplay] record */
#include "match_report.h"        /* [netplay] reports */
#include "netplay_setup.h"      /* --netplay-selftest: the match setup round trip */
#include "netplay_live_test.h"  /* --netplay-live-pair */
#include "srp_resim.h"          /* --srp-resim */
#include "contest_log.h"        /* --contest-log */
#include "jag_graphics.h"       /* --jag-graphics */
#include "kit_art.h"            /* --kit-art */
#include "custom_team.h"        /* --custom-team-selftest */
#include "challenge.h"          /* --challenge, --challenge-selftest */
#include "dos_sound.h"          /* --sound-style, --dos-sound-selftest */
#include "dos_music.h"          /* --music, --dos-music-selftest */
#include "pitch_map.h"          /* --pitch-maps */
#include "netplay_match.h"      /* --netplay-match */
#include "rollback_rig.h"       /* --rollback */

#define WIN_SCALE 3

extern void startShim(void);

/* Anchor CWD to the directory containing the binary. The asset and
 * save paths are all relative ("data/foo.dat", "grafs/...", "save/X.CAR")
 * and assume the game directory is the working directory — matching
 * the Amiga build that always ran from its own folder. Without this,
 * launching SWOS from anywhere other than bin/ would miss every asset. */
static void chdir_to_binary(const char *argv0)
{
    char path[4096];
    char *dir;
    int got_path = 0;

#ifdef _WIN32
    /* Windows: GetModuleFileNameA(NULL, ...) returns the running .exe path. */
    DWORD n = GetModuleFileNameA(NULL, path, (DWORD)sizeof(path) - 1);
    if (n > 0 && n < sizeof(path)) {
        path[n] = 0;
        got_path = 1;
    }
#else
    /* Linux: /proc/self/exe is a symlink to the running binary. */
    ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n > 0) {
        path[n] = 0;
        got_path = 1;
    }
#endif

    if (!got_path) {
        if (!argv0) return;
        /* Fallback: copy argv[0] (may be relative). */
        snprintf(path, sizeof(path), "%s", argv0);
    }

    /* Strip the trailing component. Windows accepts both '/' and '\\'
     * in paths, and GetModuleFileNameA returns backslashes — handle
     * either, taking whichever appears later in the string. */
    {
        char *slash = strrchr(path, '/');
#ifdef _WIN32
        char *bslash = strrchr(path, '\\');
        if (bslash && (!slash || bslash > slash))
            slash = bslash;
#endif
        dir = slash;
    }
    if (!dir)
        return;
    *dir = 0;
    if (chdir(path) != 0) {
        fprintf(stderr, "warning: chdir(%s) failed\n", path);
    }
}

static void print_usage(const char *argv0)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  -f, --fullscreen          Shorthand for --fullscreen=desktop. Off by\n"
        "                            default. Toggle at runtime with Alt+Enter.\n"
        "  --fullscreen=<desktop|exclusive>\n"
        "                            Fullscreen mode at startup.\n"
        "                            'desktop' = borderless letterbox, no display-\n"
        "                            mode change (default flavour). On Windows XP\n"
        "                            the taskbar can sometimes show through this\n"
        "                            borderless window.\n"
        "                            'exclusive' = true fullscreen with display-\n"
        "                            mode change, always covers the taskbar (the\n"
        "                            XP fix). Slower toggle.\n"
        "  --scale=<1|2|3|4>         Windowed scale at startup. Default: 3.\n"
        "                            Adjust at runtime with + and - keys.\n"
        "  --renderer=<software|accel>\n"
        "                            Choose SDL renderer. Default: accel.\n"
        "                            'accel' uses the platform's hardware backend\n"
        "                            (D3D/OpenGL/Metal); 'software' is a CPU-only\n"
        "                            fallback for hosts where the hardware path\n"
        "                            misbehaves.\n"
        "  --subpixel=<on|off>       Smooth sub-pixel scrolling (default on). The game\n"
        "                            draws at whole-pixel camera positions, so a true\n"
        "                            scroll of 1.5 px/frame steps 1,2,1,2. This shifts\n"
        "                            the presented frame by the remainder. Quantised to\n"
        "                            whole output pixels, so it never blurs. Turn it off\n"
        "                            to compare.\n"
        "  --scaling=<sharp|smooth>  How the frame is scaled to the window (default\n"
        "                            smooth). A fullscreen scale is rarely a whole\n"
        "                            number (1080/272 = 3.97), so a plain nearest copy\n"
        "                            gives one source pixel 4 output pixels and its\n"
        "                            neighbour 3 — uneven text and shirt numbers.\n"
        "                            'smooth' pre-multiplies the frame by a whole\n"
        "                            number first, then resamples only the leftover\n"
        "                            fraction. 'sharp' is the old single nearest copy.\n"
        "                            Both are no-ops at a whole-number scale.\n"
        "  --vsync=<on|off>          Sync frame presents to monitor vblank.\n"
        "                            Default: off. Only applies to --renderer=accel.\n"
        "                            Turn on if you see screen tearing; the manual\n"
        "                            50 Hz game-tick pace runs as a backstop\n"
        "                            regardless.\n"
        "  --audio=<mono|stereo>     Audio output mode. Default: mono.\n"
        "                            The Amiga's 4 audio channels are hard-panned\n"
        "                            (1,2 → left, 0,3 → right) which sounds\n"
        "                            lopsided on most modern playback setups.\n"
        "                            'stereo' restores the original Amiga panning.\n"
        "  --jag-graphics=<on|off>   Render the Atari Jaguar's gameplay sprites\n"
        "                            (bin/grafs_jag; scripts/make_jag_graphics.py)\n"
        "  --pitch-maps=<on|off>     Draw a DOS-graphics pitch from its Tile Agent map\n"
        "                            (<graphics dir>/pitch<N>.tmj) where there is\n"
        "                            one. Default off. The match is unchanged.\n"
        "  --outfit-shading=<on|off> Shade the kit into four tones per colour\n"
        "                            (default on). Off draws each kit colour flat,\n"
        "                            exactly as the 16-colour Amiga game does.\n"
        "  --shadow-shading=<on|off> Shade the drop shadow, tinted from the pitch\n"
        "                            it falls on (default on). Off uses SWOS's own\n"
        "                            colour 8, one near-black.\n"
        "  --hair-shading=<on|off>   Shade hair the game draws in one colour (a\n"
        "                            black head), by the same step (default on).\n"
        "                            Off draws it flat, as the Amiga game does.\n"
        "  --shade-step=<1..5>       How far apart the four kit shades are, in\n"
        "                            sixteenths (default 2). Higher is more\n"
        "                            contrast; the Jaguar's own artwork used 3.\n"
        "  --kit-art=<amiga|jaguar>  The kit drawings on the UNIFORM screen (default\n"
        "                            amiga). That screen only.\n"
        "                            Hair shading follows it.\n"
        "  --dos-graphics=<on|off>   Render the DOS-version graphics (bin/grafs_dos)\n"
        "                            instead of the Amiga graphics. Default: off.\n"
        "                            PC-only extra; the Amiga look is the default.\n"
        "  --dos-gameplay=<on|off>   Use the DOS version's gameplay tuning: a 70 Hz\n"
        "                            game tick (movement scaled 41/64 so it doesn't\n"
        "                            play faster), plus the DOS goalkeeper reach and\n"
        "                            ball-physics constants. Default: off (Amiga\n"
        "                            tuning). PC-only extra.\n"
        "  --contest-own-ratings=<on|off>\n"
        "                            The dice for a contested ball rates each side by\n"
        "                            the man actually at the ball. Default off: the\n"
        "                            original rates one side by a team-mate\n"
        "                            (docs/BALL_CONTEST.md). Offline matches only.\n"
        "  --fire-hold-lockout=<on|off>\n"
        "                            Restore an original-Amiga BUG: an OPPONENT's\n"
        "                            tackle wipes your held fire button's shot charge,\n"
        "                            and it cannot restart while the button is down, so\n"
        "                            holding fire through a challenge never shoots —\n"
        "                            you have to release and press again. Default off\n"
        "                            (fixed). Your OWN tackle still spends the press\n"
        "                            in both modes: that part is by design.\n"
        "  --cpu-cards=<on|off>      A computer side can be booked and sent off by the\n"
        "                            same rolls as a human one. Default on; the\n"
        "                            ORIGINAL (off) never cards a team with no human\n"
        "                            controller. Offline matches only by nature.\n"
        "  --dos-dir=<folder>        Folder (under bin/) to load DOS assets from.\n"
        "                            Default: grafs_dos.\n"
        "  --replay-test=<file.swr>  Replay a recorded match and assert every frame\n"
        "                            against the recording (docs/TESTS.md). Skips the\n"
        "                            front end, runs unpaced, and exits 0 on a full\n"
        "                            match, 2 on the first divergence. Build a .swr\n"
        "                            with scripts/rgd_convert.py --emit.\n"
        "  --replay-level=<scalars|teams|info|sprites>\n"
        "                            How much of each frame to compare. Default: the\n"
        "                            level the .swr was emitted at.\n"
        "  --replay-width=<N>        Render width for --replay-test. Default 336,\n"
        "                            the geometry the corpus was recorded in. Any\n"
        "                            other value runs the SAME simulation with a\n"
        "                            wider render, which is how a render/simulation\n"
        "                            leak shows up: the compared fields must not\n"
        "                            move. See scripts/replay_widecheck.sh.\n"
        "  --replay-sim-width=<N>    The SIMULATION draw width for --replay-test\n"
        "                            (display.h, _sim_draw_width): what margin\n"
        "                            animation and the crowd tiles are decided for.\n"
        "                            Default 336. Two runs at different\n"
        "                            --replay-width with the same value here must\n"
        "                            play the same match: scripts/netplay_width_test.sh.\n"
        "  --record-replay=<file>    Record the match to this .SRP (src/replay_v2.c).\n"
        "                            Every match is recorded to LATEST.SRP anyway; this\n"
        "                            names the file, and also records under --replay-test.\n"
        "  --view-replay=<file>      Play a .SRP or a .HIL headless in place of the\n"
        "                            front end and exit (0 when every frame of a .SRP\n"
        "                            played). A .HIL is a NAME in the save folder, as\n"
        "                            the highlights menu loads it. --replay-width sets\n"
        "                            the width; SWOS_REPLAY_TRACE=<file> traces a .SRP.\n"
        "  --replay-player=<file>    Watch a .SRP in real time and hold at its end;\n"
        "                            the playback page seeks, pauses and steps it\n"
        "                            (wasm/player.html, make WASM=1 PLAYER=1).\n"
        "  --replay-window           Open a window during --replay-test or\n"
        "                            --view-replay. Off by default: a replay is a batch\n"
        "                            run and a full matrix would open one window per\n"
        "                            cell. Implies --replay-paced for --view-replay.\n"
        "  --replay-paced            Play --view-replay in real time, not as fast as\n"
        "                            possible. With SWOS_PRESENT_STATS=1 it measures\n"
        "                            the replay's frame pacing.\n"
        "  --replay-selftest         Check CalculateDeltaXAndY against swos-port's\n"
        "                            reference vectors and exit. No recording needed.\n"
        "  --replay-skip=<a,b,...>   Mute every field whose name contains one of these\n"
        "                            substrings, e.g. '.starting_direction' (all 33\n"
        "                            sprites) or 'top_info.'.\n"
        "  --replay-max-errors=<N>   Keep going past a divergence and report up to N\n"
        "                            mismatching fields. Default: 1 (stop at the\n"
        "                            first — gameplay is chaotic, so everything after\n"
        "                            it is noise).\n"
        "  --menu=<name>             Open a menu that exists only as JSON before the\n"
        "                            main menu, e.g. swos2020/logInMenu (a file under\n"
        "                            data_json/menus/, docs/MENUS.md section 10).\n"
        "  --convert-teams=<in>,<out> Convert every .TEAM team record in <in> (SWOS\n"
        "                            2020, editors) to a custom team under the same name in <out>,\n"
        "                            and say why any could not be. Exits.\n"
        "  --sound-style=amiga|pc    A match's sounds: the Amiga's (default) or the PC\n"
        "                            game's samples, crowd and chants ([audio] style).\n"
        "  --commentary=auto|on|off  The PC game's commentary: auto (default) with\n"
        "                            --sound-style=pc only; on also over the Amiga's\n"
        "                            sounds ([audio] commentary).\n"
        "  --music=amiga|pc          The music: the Amiga's (default) or the PC game's --\n"
        "                            the CD's title song in the intro, menu.xmi on an\n"
        "                            emulated AdLib card in the menus ([audio] music).\n"
        "  --commentary-dir=<dir>    A folder of your own commentary, one sub-folder per\n"
        "                            category ([audio] commentary_dir; docs/DOS_SOUND.md).\n"
        "  --menu-music=midi|mp3     With --music=pc: the menus play menu.xmi live on the\n"
        "                            emulated card (default) or a recording of it,\n"
        "                            sound_dos/menu.mp3 ([audio] menu_music).\n"
        "  --input-selftest          Check the redefined keys and the press latch\n"
        "  --dos-music-selftest      Play the PC menu music through once without sound\n"
        "                            and report what it found. Exits.\n"
        "  --dos-sound-selftest      Check that every PC sound file is in sound_dos/\n"
        "                            and every table names one. Exits.\n"
        "  --challenge=<file>        Play on from a saved position: a .CHL in the save\n"
        "                            folder, written in a match by the 'C' key.\n"
        "  --challenge-selftest      Round-trip a saved position's setup and state in\n"
        "                            this process. Exits.\n"
        "  --custom-team-selftest    Check every database team against the custom team\n"
        "                            rule (names, faces, kits and line-up only) and\n"
        "                            through the team file's text. Exits.\n"
        "  --menu-selftest           Load and unpack every imported SWOS 2020 menu,\n"
        "                            check it against its JSON, and exit.\n"
        "  --version                 Print the build (commit and date) and exit.\n"
        "  --matchmaker-selftest[=host[:port]]\n"
        "                            Talk to a matchmaking server: log in, list the\n"
        "                            lobby, create and leave a game. Needs one running\n"
        "                            (scripts/run_matchmaker.sh). Default 127.0.0.1:19602.\n"
        "  --netplay-lobby=<host|join>\n"
        "                            Meet another instance on this machine over\n"
        "                            loopback and open the multiplayer lobby: pick\n"
        "                            teams and tactics, ready up, and play a match.\n"
        "                            Start one of each.\n"
        "  --netplay-selftest        Run GGPO-X with a toy game: two peers over\n"
        "                            loopback, then a synctest session, then the\n"
        "                            MULTIPLAYER menu's own link. Exits.\n"
        "  --netplay-match=<mode>    With --replay-test: play the match through the\n"
        "                            netplay frame step on the recording's inputs\n"
        "                            (docs/NETPLAY.md): direct (no GGPO), synctest[:N]\n"
        "                            (every N frames GGPO loads the state of N frames\n"
        "                            back, re-simulates, compares; 1..8), or p2p (one\n"
        "                            of two peers, with --rollback=trace and:)\n"
        "  --netplay-player=1|2      p2p: the local player.\n"
        "  --netplay-port=<port>     p2p: the local UDP port.\n"
        "  --netplay-peer=<ip:port>  p2p: the other peer.\n"
        "  --netplay-delay=<frames>  p2p: input delay. Default 0.\n"
        "  --netplay-stall=<ms>      p2p: pause now and then, up to this long (test).\n"
        "  --netplay-trace=<file>    Per-frame state hashes as finally played.\n"
        "  --netplay-timeout=<s>     Give up after this long without progress (30).\n"
        "  --rollback=<mode>         With --replay-test: roll the match back to test\n"
        "                            the rollback state registry (docs/ROLLBACK.md).\n"
        "                            check | oracle | discover | scan | trace. The\n"
        "                            recording supplies setup and inputs only.\n"
        "  --rollback-window=<K>     Frames re-simulated per window. Default 8\n"
        "                            (discover: 32).\n"
        "  --rollback-every=<N>      Frames between window starts. Default 64\n"
        "                            (discover: 1024).\n"
        "  --rollback-first=<F>      No window before frame F. Default 0.\n"
        "  --rollback-seed=<n>       RNG seed for the match. Default 0x5EED5EED.\n"
        "  --rollback-report=<file>  JSON report (scripts/rollback_report.py).\n"
        "  --rollback-trace=<file>   Per-frame state hashes, for comparing two runs.\n"
        "  --rollback-restore=all|sim  Restore both registry tiers (default) or only\n"
        "                            the simulation tier (the resync minimum).\n"
        "  --rollback-disable=<a,b>  Leave these registry entries out (trial removal).\n"
        "  --rollback-portable       Restore through the resync (portable) encoding.\n"
        "  --rollback-dump=<file>    The registry, per region, at one frame; set the\n"
        "                            frame with --rollback-dump-frame=<n> (default 0).\n"
        "  -h, --help                Show this help and exit.\n",
        argv0 ? argv0 : "SWOS");
}

/* Parse `--key=value` from `arg` against expected `key`. Returns the
 * value-side string, or NULL if `arg` doesn't start with `--key=`. */
static const char *match_kv(const char *arg, const char *key)
{
    size_t klen = strlen(key);
    if (strncmp(arg, "--", 2) != 0) return NULL;
    if (strncmp(arg + 2, key, klen) != 0) return NULL;
    if (arg[2 + klen] != '=') return NULL;
    return arg + 2 + klen + 1;
}

int main(int argc, char **argv)
{
    /* Line-buffer stdout. Every self-test and harness in here reports progress
     * as it goes, and stdout to a FILE or a PIPE is block buffered -- so a run
     * that is killed, hangs, or is watched from another terminal shows nothing,
     * and the last thing it printed (the thing that says where it stopped) is
     * exactly what is lost. Three separate diagnoses in this codebase have been
     * slowed down by that. It costs nothing on a run that completes. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    /* Before anything sleeps: on Windows every SDL_Delay is otherwise rounded up
     * to a 15.6 ms tick (src/platform_timer.c). That includes the self-tests
     * below, which is why --netplay-selftest looked hung on Windows. */
    (void)Platform_RaiseTimerResolution();

/* Switch: the headers define SIGHUP but nnSdk exports no signal(), so the
 * #ifdef alone is not the right test — the link fails on `undefined symbol:
 * signal`. There is no controlling terminal on a console either, which is what
 * this is for. */
#if defined(SIGHUP) && !defined(SWOS_SWITCH_BUILD)
    /* Cygwin under the VS Code gdb launcher wires stdio as pipes (not a Cygwin
     * pty), so at startup the runtime sees the controlling terminal "hang up"
     * and delivers SIGHUP — whose default action kills us before the window
     * even opens (gdb reports "terminated with signal SIGHUP", exit code 0).
     * This is a GUI app and doesn't need the terminal, so ignore it. No effect
     * on a normal run, which never receives SIGHUP. */
    signal(SIGHUP, SIG_IGN);
#endif

    /* Anchor CWD to the binary's directory FIRST so the data/ grafs/ sound/
     * trees resolve relative to the exe regardless of where the user launched
     * from. (SWOS.ini and SWOS.log no longer live here — they go to the per-user
     * game folder; see swos_save_dir_init() + the freopen below.)
     * WASM: there is no binary dir — the data is --preload-file'd into MEMFS at
     * /data, /grafs, … and cwd is already "/", so skip the chdir.
     * Switch: the content is a read-only mount at rom:/ and there is no chdir
     * to make; swos_data_path() (swos_io.h) anchors the data reads instead. */
#if defined(__ANDROID__)
    /* The Activity unpacks the test runtime before starting SDL_main. */
    {
        const char *root = SDL_AndroidGetInternalStoragePath();
        char runtime[1024];
        if (!root) return 1;
        snprintf(runtime, sizeof runtime, "%s/runtime", root);
        if (chdir(runtime) != 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Cannot open runtime: %s", runtime);
            return 1;
        }
    }
#elif !defined(SWOS_WASM_BUILD) && !defined(SWOS_SWITCH_BUILD)
    chdir_to_binary(argv ? argv[0] : NULL);
#endif

    /* Resolve the per-user game folder + save dir NOW, before Settings_Load, so
     * SWOS.ini reads/writes from the same per-user location as the save/ folder
     * (~/.swos, Documents/SWOS, ~/Library/Application Support/SWOS). Creates the
     * folder if missing; if it can't be resolved, falls back to the exe dir.
     * (Was called later, after SDL_Init — moved up so the ini lookup uses it.) */
    swos_save_dir_init();

#ifdef SWOS_SDL_BUILD
    /* Data-file JSON loader self-test (docs/DATA_INTERNAL_JSON.md). Runs before
     * any SDL/game init and exits — `SWOS_DATA_JSON_SELFTEST=1 ./bin/SWOS`. */
    if (getenv("SWOS_DATA_JSON_SELFTEST"))
        return DataJson_SelfTest();
#endif

    /* 1) Hard-coded defaults — used when SWOS.ini doesn't exist yet
     * and no CLI flags override. */
    PlatformInitOptions opts;
    opts.win_scale  = WIN_SCALE;
    opts.fullscreen = PLATFORM_FULLSCREEN_NONE;
    opts.renderer   = PLATFORM_RENDERER_ACCEL;
    opts.vsync      = 0;          /* default off — opt-in only */
    opts.subpixel_scroll = 1;     /* default on — smooth sub-pixel scrolling */
    opts.scaling_smooth  = 1;     /* default on — even pixels at a non-integer scale */
    opts.crt_mask = 0;            /* CRT effects all default OFF — opt-in */
    opts.crt_mask_strength = 45;
    opts.crt_vignette = 0;
    opts.crt_corners = 0;
    opts.render_driver[0] = '\0'; /* "" = auto (SDL default backend) */
    opts.res_w = 0; opts.res_h = 0;  /* 0,0 = auto (fill the desktop aspect) */
    opts.screen = 0;                 /* target display index (0 = primary) */
    opts.audio_mono = 1;
    opts.sound_style = 0;         /* the Amiga's sounds */
    opts.music_style = 0;         /* the Amiga's menu music */
    opts.menu_recording = 0;      /* with the PC's music: menu.xmi on the emulated chip */
    opts.commentary_dir[0] = '\0';
    opts.commentary = 2;          /* auto: with the PC's sounds */
    opts.dos_graphics = 0;        /* default Amiga graphics */
    /* ... and not the Jaguar's either, on any build. The browser one played with
     * it by default until 2026-10-01; it still ships the banks, so `[graphics]
     * sprites = jaguar` in its SWOS.ini turns it on. */
    opts.jag_graphics = 0;
    opts.pitch_maps = 0;          /* the pitch from the game's own files */
    /* Shading defaults (docs/PALETTE_V2.md §5). Step 2 rather than the Jaguar's
     * own ~3.3: at that depth a near-white sock bottoms out at 37.5% and reads
     * as dark grey. */
    opts.view_style = VIEW_STYLE_AUTO;   /* follow the gameplay style */
    opts.widescreen = 1;                 /* fill the display, as it always has */
    opts.outfit_shading = 1;
    opts.shadow_shading = 1;
    opts.hair_shading = 1;
    opts.shade_step = 2;
    opts.dos_gameplay = 0;        /* default Amiga gameplay */
    opts.stick_deadzone = 25;     /* percent: about what the old per-axis threshold was */
    opts.latch_presses = 1;       /* a short fire press is never lost between two reads */
    memset(opts.key_bind, 0, sizeof opts.key_bind);   /* no keys of the player's own */
    opts.fire_hold_lockout = 0;   /* default: the original's fire-hold bug is FIXED */
    opts.contest_own_ratings = 0; /* default: the original's roll, team-mate's rating and all */
    opts.cpu_cards = 1;           /* default: the original's CPU card immunity is FIXED */
    opts.replay_auto_save = 0;    /* default: LATEST.SRP only, no dated copy per match */
    opts.netplay_name[0] = '\0';
    opts.netplay_remember = 1;    /* REMEMBER LOGIN INFO: on, as the login screen shows it */
    opts.assets_reminder_off[0] = '\0';  /* remind about older game data (src/assets_check.c) */
    opts.matchmaker[0] = '\0';    /* default: a matchmaking server on this machine */
    opts.record[0] = '\0';        /* default: "server" once SWOS.ini is read (below) */
    opts.netplay_balance = 0;     /* [netplay] balance: off unless asked for (docs/NETPLAY_BALANCE.md) */
    opts.prediction = 1;          /* default: a live match's replay keeps what each player saw */
    opts.no_injuries_cards = 0;   /* [testing] no_injuries_cards: the real rules unless asked (test_rules.h) */
#ifdef SWOS_TEST_RULES
    opts.reports = 1;             /* a tester build: match reports come back to us */
#else
    opts.reports = 0;             /* a public release: the player opts in (SEND MATCH REPORTS) */
#endif
    opts.arcade = 0;              /* default off — normal front-end menu */
    opts.arcade_mode = 0;         /* GOLDEN GOAL */
    opts.arcade_game_seconds = 45;
    opts.arcade_golden_goal = 1;
    opts.arcade_penalties = 1;
    opts.arcade_goal_replay = 1;
    opts.arcade_attract_audio = 2;   /* music+sfx (HERO + reel crowd/SFX mixed) */
    opts.arcade_gameplay = 0;        /* amiga gameplay feel */
    snprintf(opts.arcade_key_p1_credit, sizeof opts.arcade_key_p1_credit, "%s", "5");
    snprintf(opts.arcade_key_p1_start,  sizeof opts.arcade_key_p1_start,  "%s", "1");
    snprintf(opts.arcade_key_p2_credit, sizeof opts.arcade_key_p2_credit, "%s", "6");
    snprintf(opts.arcade_key_p2_start,  sizeof opts.arcade_key_p2_start,  "%s", "2");
    snprintf(opts.arcade_key_service,   sizeof opts.arcade_key_service,   "%s", "F2");
    snprintf(opts.dos_dir, sizeof opts.dos_dir, "%s", "grafs_dos");
    const char *replay_file = NULL, *replay_level = NULL, *replay_skip = NULL;
    int replay_max_errors = 1;
    int replay_window = 0;
    int replay_paced = 0;       /* --view-replay in real time: --replay-paced / --replay-window */
    int replay_width = 0;   /* 0 = the corpus geometry (SCREEN_WIDTH) */
    int replay_sim_width = 0;   /* 0 = SCREEN_WIDTH: no margin, as the corpus was recorded */
    const char *record_replay = NULL, *view_replay = NULL;   /* src/replay_v2.c */
    snprintf(opts.language, sizeof opts.language, "%s", "en");
    snprintf(opts.currency, sizeof opts.currency, "%s", "pound");

#ifdef SWOS_WASM_BUILD
    /* Web defaults (SWOS.ini still overrides these): native render scale — the
     * canvas CSS upscales to the viewport, so a high SDL win_scale just wastes
     * GPU — and vsync off (we pace via emscripten_sleep, not SDL vsync). */
    opts.win_scale = 1;
    opts.vsync     = 0;
#endif

    /* 2) SWOS.ini (in the per-user game folder — swos_ini_path()) overrides
     * defaults. On the web it's copied from localStorage into /SWOS.ini by
     * wasm/pre.js before we get here (edit it via the in-page settings panel).
     * Missing file is fine — defaults are used, and step 4 writes a fresh one. */
    const char *ini_path = swos_ini_path();
    /* Say WHICH file, and whether it was there. swos_save_dir_init() falls back
     * to "." if the per-user folder cannot be resolved or created, and silently
     * — so an edited SWOS.ini that "does nothing" is usually the game reading a
     * different one. Print it before loading so the answer is in the log even
     * if the load then fails. */
    {
        FILE *probe = fopen(ini_path, "rb");
        fprintf(stderr, "[settings] SWOS.ini: %s (%s)\n",
                ini_path, probe ? "found" : "not found, using defaults");
        if (probe) fclose(probe);
    }
    Settings_Load(&opts, ini_path);

    /* Expose the live options + ini path to the arcade service menu so its
     * settings (game rules) can be written straight back to SWOS.ini. */
    { extern PlatformInitOptions *g_arcadeOpts; extern const char *g_arcadeIniPath;
      g_arcadeOpts = &opts; g_arcadeIniPath = ini_path; }

    /* 3) CLI overrides INI (highest precedence — explicit user intent). */
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        const char *v;
        if (strcmp(a, "-f") == 0 || strcmp(a, "--fullscreen") == 0) {
            opts.fullscreen = PLATFORM_FULLSCREEN_DESKTOP;
        } else if ((v = match_kv(a, "fullscreen")) != NULL) {
            if (strcmp(v, "desktop") == 0)        opts.fullscreen = PLATFORM_FULLSCREEN_DESKTOP;
            else if (strcmp(v, "exclusive") == 0) opts.fullscreen = PLATFORM_FULLSCREEN_EXCLUSIVE;
            else if (strcmp(v, "off") == 0)       opts.fullscreen = PLATFORM_FULLSCREEN_NONE;
            else {
                fprintf(stderr, "Invalid --fullscreen value '%s' (expected desktop|exclusive|off)\n", v);
                return 1;
            }
        } else if ((v = match_kv(a, "scale")) != NULL) {
            int n = atoi(v);
            if (n < 1 || n > 4) {
                fprintf(stderr, "Invalid --scale value '%s' (expected 1|2|3|4)\n", v);
                return 1;
            }
            opts.win_scale = n;
        } else if ((v = match_kv(a, "renderer")) != NULL) {
            if (strcmp(v, "software") == 0)      opts.renderer = PLATFORM_RENDERER_SOFTWARE;
            else if (strcmp(v, "accel") == 0)    opts.renderer = PLATFORM_RENDERER_ACCEL;
            else {
                fprintf(stderr, "Invalid --renderer value '%s' (expected software|accel)\n", v);
                return 1;
            }
        } else if ((v = match_kv(a, "vsync")) != NULL) {
            if (strcmp(v, "on") == 0 || strcmp(v, "1") == 0)       opts.vsync = 1;
            else if (strcmp(v, "off") == 0 || strcmp(v, "0") == 0) opts.vsync = 0;
            else {
                fprintf(stderr, "Invalid --vsync value '%s' (expected on|off)\n", v);
                return 1;
            }
        } else if ((v = match_kv(a, "subpixel")) != NULL) {
            if (strcmp(v, "on") == 0 || strcmp(v, "1") == 0)       opts.subpixel_scroll = 1;
            else if (strcmp(v, "off") == 0 || strcmp(v, "0") == 0) opts.subpixel_scroll = 0;
            else {
                fprintf(stderr, "Invalid --subpixel value '%s' (expected on|off)\n", v);
                return 1;
            }
        } else if ((v = match_kv(a, "scaling")) != NULL) {
            if (strcmp(v, "smooth") == 0)     opts.scaling_smooth = 1;
            else if (strcmp(v, "sharp") == 0) opts.scaling_smooth = 0;
            else {
                fprintf(stderr, "Invalid --scaling value '%s' (expected sharp|smooth)\n", v);
                return 1;
            }
        } else if ((v = match_kv(a, "audio")) != NULL) {
            if (strcmp(v, "mono") == 0)        opts.audio_mono = 1;
            else if (strcmp(v, "stereo") == 0) opts.audio_mono = 0;
            else {
                fprintf(stderr, "Invalid --audio value '%s' (expected mono|stereo)\n", v);
                return 1;
            }
        } else if ((v = match_kv(a, "dos-graphics")) != NULL) {
            if (strcmp(v, "on") == 0 || strcmp(v, "1") == 0)       opts.dos_graphics = 1;
            else if (strcmp(v, "off") == 0 || strcmp(v, "0") == 0) opts.dos_graphics = 0;
            else {
                fprintf(stderr, "Invalid --dos-graphics value '%s' (expected on|off)\n", v);
                return 1;
            }
        } else if ((v = match_kv(a, "contest-own-ratings")) != NULL) {
            opts.contest_own_ratings = (strcmp(v, "on") == 0 || strcmp(v, "1") == 0);
        } else if ((v = match_kv(a, "fire-hold-lockout")) != NULL) {
            if (strcmp(v, "on") == 0 || strcmp(v, "1") == 0)       opts.fire_hold_lockout = 1;
            else if (strcmp(v, "off") == 0 || strcmp(v, "0") == 0) opts.fire_hold_lockout = 0;
            else {
                fprintf(stderr, "Invalid --fire-hold-lockout value '%s' (expected on|off)\n", v);
                return 1;
            }
        } else if ((v = match_kv(a, "cpu-cards")) != NULL) {
            if (strcmp(v, "on") == 0 || strcmp(v, "1") == 0)       opts.cpu_cards = 1;
            else if (strcmp(v, "off") == 0 || strcmp(v, "0") == 0) opts.cpu_cards = 0;
            else {
                fprintf(stderr, "Invalid --cpu-cards value '%s' (expected on|off)\n", v);
                return 1;
            }
        } else if ((v = match_kv(a, "jag-graphics")) != NULL) {
            /* The Atari Jaguar's gameplay art, from bin/grafs_jag (docs/JAGUAR.md). */
            if (strcmp(v, "on") == 0 || strcmp(v, "1") == 0)       opts.jag_graphics = 1;
            else if (strcmp(v, "off") == 0 || strcmp(v, "0") == 0) opts.jag_graphics = 0;
            else {
                fprintf(stderr, "Invalid --jag-graphics value '%s' (expected on|off)\n", v);
                return 1;
            }
        } else if ((v = match_kv(a, "dos-gameplay")) != NULL) {
            if (strcmp(v, "on") == 0 || strcmp(v, "1") == 0)       opts.dos_gameplay = 1;
            else if (strcmp(v, "off") == 0 || strcmp(v, "0") == 0) opts.dos_gameplay = 0;
            else {
                fprintf(stderr, "Invalid --dos-gameplay value '%s' (expected on|off)\n", v);
                return 1;
            }
        } else if (strcmp(a, "--version") == 0) {
            /* So a tester can paste one line instead of describing their download. */
            printf("%s\n", Build_Version());
            return 0;
        } else if ((v = match_kv(a, "dos-dir")) != NULL) {
            snprintf(opts.dos_dir, sizeof opts.dos_dir, "%s", v);
        } else if ((v = match_kv(a, "replay-test")) != NULL) {
            replay_file = v;
        } else if ((v = match_kv(a, "replay-level")) != NULL) {
            replay_level = v;
        } else if ((v = match_kv(a, "replay-width")) != NULL) {
            replay_width = atoi(v);
        } else if ((v = match_kv(a, "replay-sim-width")) != NULL) {
            replay_sim_width = atoi(v);
        } else if ((v = match_kv(a, "record-replay")) != NULL) {
            record_replay = v;
        } else if ((v = match_kv(a, "view-replay")) != NULL) {
            view_replay = v;
        } else if ((v = match_kv(a, "replay-player")) != NULL) {
            /* --view-replay to be watched and scrubbed: the playback page's (wasm/player.html). */
            view_replay = v;
            replay_window = 1;
            replay_paced = 1;
            ReplayV2_PlayerMode(1);
#ifdef __EMSCRIPTEN__
            /* SDL's keyboard on the canvas alone, so the page's own keys (Space, the arrows,
             * [ ], V) stay the page's. A hint, read when the video starts -- the page cannot
             * say it: Module.keyboardListeningElement is SDL1's, and a newer Emscripten
             * refuses the whole page for passing it. */
            SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas");
#endif
            /* Straight to the recording: paced, the intro would play first, a minute of it. */
            { extern volatile uint8_t _skipLoaderIntro; _skipLoaderIntro = 1; }
        } else if ((v = match_kv(a, "contest-log")) != NULL) {
            if (ContestLog_Open(v) != 0) {
                fprintf(stderr, "--contest-log: cannot write %s\n", v);
                return 1;
            }
        } else if ((v = match_kv(a, "srp-resim")) != NULL) {
            /* Play a recorded match again and check every frame (src/srp_resim.c). */
            if (SrpResim_Arm(v) != 0) {
                fprintf(stderr, "--srp-resim needs a .SRP file\n");
                return 1;
            }
            { extern volatile uint8_t _skipLoaderIntro; _skipLoaderIntro = 1; }
        } else if ((v = match_kv(a, "challenge")) != NULL) {
            /* Play on from a saved position: the name of a .CHL in the save folder
             * (src/challenge.c). Only remembered here -- the save folder is not known
             * until later, so a read now would look in the wrong place. */
            Challenge_Request(v);
            { extern volatile uint8_t _skipLoaderIntro; _skipLoaderIntro = 1; }
        } else if (strcmp(a, "--challenge-selftest") == 0) {
            /* Round-trips the setup block and the match state in this process, and says
             * which field failed if one does. Needs no game data, so it runs here and
             * exits -- it scribbles over the globals it covers on purpose. */
            return Challenge_SelfTest() ? 1 : 0;
        } else if (strcmp(a, "--replay-window") == 0) {
            replay_window = 1;
        } else if (strcmp(a, "--replay-paced") == 0) {
            replay_paced = 1;
        } else if (strcmp(a, "--replay-selftest") == 0) {
            return ReplayTest_MathSelfTest();
        } else if ((v = match_kv(a, "widescreen")) != NULL) {
            opts.widescreen = strcmp(v, "stretch") == 0 ? 2 :
                              !(strcmp(v, "off") == 0 || strcmp(v, "0") == 0);
        } else if ((v = match_kv(a, "view")) != NULL) {
            if      (strcmp(v, "auto") == 0)  opts.view_style = VIEW_STYLE_AUTO;
            else if (strcmp(v, "amiga") == 0) opts.view_style = VIEW_STYLE_AMIGA;
            else if (strcmp(v, "dos") == 0)   opts.view_style = VIEW_STYLE_DOS;
            else { fprintf(stderr, "--view must be auto, amiga or dos (got %s)\n", v); return 1; }
        } else if ((v = match_kv(a, "pitch-maps")) != NULL) {
            opts.pitch_maps = (strcmp(v, "on") == 0 || strcmp(v, "1") == 0);
        } else if ((v = match_kv(a, "outfit-shading")) != NULL) {
            opts.outfit_shading = !(strcmp(v, "off") == 0 || strcmp(v, "0") == 0);
        } else if ((v = match_kv(a, "shadow-shading")) != NULL) {
            opts.shadow_shading = !(strcmp(v, "off") == 0 || strcmp(v, "0") == 0);
        } else if ((v = match_kv(a, "hair-shading")) != NULL) {
            opts.hair_shading = !(strcmp(v, "off") == 0 || strcmp(v, "0") == 0);
        } else if ((v = match_kv(a, "shade-step")) != NULL) {
            int st = atoi(v);
            if (st < 1 || st > 5) {
                fprintf(stderr, "--shade-step must be 1..5 (got %s)\n", v);
                return 1;
            }
            opts.shade_step = st;
        } else if ((v = match_kv(a, "kit-art")) != NULL) {
            if      (strcmp(v, "amiga") == 0)  opts.kit_art = 0;
            else if (strcmp(v, "jaguar") == 0) opts.kit_art = 1;
            else { fprintf(stderr, "--kit-art must be amiga or jaguar (got %s)\n", v); return 1; }
        } else if ((v = match_kv(a, "menu")) != NULL) {
            OnlineMenus_SetStartMenu(v);
            /* Same reason as --matchmaker-menu: this flag exists to reach a menu,
             * and the loader intro plays in front of it in real time. */
            { extern volatile uint8_t _skipLoaderIntro; _skipLoaderIntro = 1; }
        } else if (strcmp(a, "--menu-selftest") == 0) {
            return OnlineMenus_SelfTest();
        } else if ((v = match_kv(a, "convert-teams")) != NULL) {
            static char dirs[2048];
            char *comma;
            snprintf(dirs, sizeof dirs, "%s", v);
            comma = strchr(dirs, ',');
            if (!comma) {
                fprintf(stderr, "--convert-teams=<folder of .TEAM files>,<folder for the .json>\n");
                return 1;
            }
            *comma = '\0';
            return CustomTeam_ConvertDir(dirs, comma + 1) ? 0 : 1;
        } else if ((v = match_kv(a, "sound-style")) != NULL) {
            if      (strcmp(v, "amiga") == 0) opts.sound_style = 0;
            else if (strcmp(v, "pc") == 0 || strcmp(v, "dos") == 0) opts.sound_style = 1;
            else { fprintf(stderr, "--sound-style must be amiga or pc (got %s)\n", v); return 1; }
        } else if ((v = match_kv(a, "commentary-dir")) != NULL) {
            snprintf(opts.commentary_dir, sizeof opts.commentary_dir, "%s", v);
        } else if ((v = match_kv(a, "menu-music")) != NULL) {
            if      (strcmp(v, "midi") == 0) opts.menu_recording = 0;
            else if (strcmp(v, "mp3") == 0)  opts.menu_recording = 1;
            else { fprintf(stderr, "--menu-music must be midi or mp3 (got %s)\n", v); return 1; }
        } else if ((v = match_kv(a, "music")) != NULL) {
            if      (strcmp(v, "amiga") == 0) opts.music_style = 0;
            else if (strcmp(v, "pc") == 0 || strcmp(v, "dos") == 0) opts.music_style = 1;
            else { fprintf(stderr, "--music must be amiga or pc (got %s)\n", v); return 1; }
        } else if ((v = match_kv(a, "commentary")) != NULL) {
            opts.commentary = strcmp(v, "auto") == 0 ? 2
                            : strcmp(v, "off") != 0 && strcmp(v, "0") != 0 && strcmp(v, "false") != 0;
        } else if (strcmp(a, "--input-selftest") == 0) {
            extern int InputSDL_SelfTest(void);
            return InputSDL_SelfTest() ? 1 : 0;
        } else if (strcmp(a, "--dos-music-selftest") == 0) {
            return DosMusic_SelfTest() ? 1 : 0;
        } else if (strcmp(a, "--dos-sound-selftest") == 0) {
            return DosSound_SelfTest() ? 1 : 0;
        } else if (strcmp(a, "--custom-team-selftest") == 0) {
            return CustomTeam_SelfTest() ? 0 : 1;
        } else if (strncmp(a, "--matchmaker-selftest", 21) == 0) {
            /* --matchmaker-selftest[=host[:port]] — talks to a real server, so
             * scripts/run_matchmaker.sh has to be running. Defaults to the local
             * one on its default port. */
            const char *host = MmClient_DefaultHost();
            uint16_t port = MmClient_DefaultPort();
            static char hostbuf[64];
            if (a[21] == '=') {
                const char *spec = a + 22;
                const char *colon = strrchr(spec, ':');
                if (colon) {
                    size_t n = (size_t)(colon - spec);
                    if (n >= sizeof hostbuf)
                        n = sizeof hostbuf - 1;
                    memcpy(hostbuf, spec, n);
                    hostbuf[n] = '\0';
                    host = hostbuf;
                    port = (uint16_t)atoi(colon + 1);
                } else if (spec[0]) {
                    snprintf(hostbuf, sizeof hostbuf, "%s", spec);
                    host = hostbuf;
                }
            }
            return MmClient_SelfTest(host, port);
        } else if (strncmp(a, "--matchmaker-menu", 17) == 0 &&
                   (a[17] == '\0' || a[17] == '=')) {
            OnlineMenus_SetAutoLogin(a[17] == '=' ? a + 18 : "PLAYER");
            /* Same reason as --netplay-live-pair: this flag exists to reach the
             * online menu, and the loader intro plays in real time in front of
             * it. Two machines driven through matchmaking spent it twice. */
            { extern volatile uint8_t _skipLoaderIntro; _skipLoaderIntro = 1; }
        } else if ((v = match_kv(a, "netplay-lobby")) != NULL) {
            if (OnlineMenus_SetLoopbackLobby(v) != 0) {
                fprintf(stderr, "--netplay-lobby must be 'host' or 'join'\n");
                return 1;
            }
            { extern volatile uint8_t _skipLoaderIntro; _skipLoaderIntro = 1; }
        } else if ((v = match_kv(a, "netplay-state-diff")) != NULL) {
            return NetplayLiveTest_StateDiff(v);
        } else if (!strcmp(a, "--team-audit")) {
            extern void TeamAudit_Arm(void);
            TeamAudit_Arm();
            { extern volatile uint8_t _skipLoaderIntro; _skipLoaderIntro = 1; }
            /* Headless and silent, for the same reason the replay rig is: this is a batch
             * run that prints a hash, and it has no business opening a window. The audit
             * runs after Phase 9, so without this the game comes all the way up first. */
#ifdef _WIN32
            _putenv("SWOS_HARNESS_TURBO=1");
            _putenv("SWOS_NO_LOG=1");
            if (!getenv("SDL_VIDEODRIVER")) _putenv("SDL_VIDEODRIVER=dummy");
            if (!getenv("SDL_AUDIODRIVER")) _putenv("SDL_AUDIODRIVER=dummy");
#else
            setenv("SWOS_HARNESS_TURBO", "1", 1);
            setenv("SWOS_NO_LOG", "1", 1);
            if (!getenv("SDL_VIDEODRIVER")) setenv("SDL_VIDEODRIVER", "dummy", 1);
            if (!getenv("SDL_AUDIODRIVER")) setenv("SDL_AUDIODRIVER", "dummy", 1);
#endif
        } else if ((v = match_kv(a, "netplay-state-stream-diff")) != NULL) {
            return NetplayLiveTest_StreamDiff(v);
        } else if ((v = match_kv(a, "netplay-live-pair")) != NULL) {
            if (NetplayLiveTest_Arm(v) != 0) {
                fprintf(stderr, "--netplay-live-pair must be 'host' or 'join'\n");
                return 1;
            }
            /* Skip the loader intro, as the replay rig does: it plays in real time
             * and cost each peer ~108 s before the test reached its first line. */
            { extern volatile uint8_t _skipLoaderIntro; _skipLoaderIntro = 1; }
        } else if ((v = match_kv(a, "matchmaker-pair")) != NULL) {
            /* --matchmaker-pair=host|join — two processes, one each, against a
             * running server. scripts/matchmaker_pair_test.sh drives both. */
            if (strcmp(v, "host") != 0 && strcmp(v, "join") != 0) {
                fprintf(stderr, "--matchmaker-pair must be 'host' or 'join'\n");
                return 1;
            }
            return MmClient_PairTest(v, MmClient_DefaultHost(), MmClient_DefaultPort());
        } else if (strcmp(a, "--netplay-selftest") == 0) {
            /* The library, then the connection the MULTIPLAYER menu makes over it.
             * Both run whatever the other did, so one report covers both. */
            int32_t ggpoFailed  = Netplay_SelfTest();
            int32_t linkFailed  = P2PLink_SelfTest();
            int32_t setupFailed = NetplaySetup_SelfTest();
            return (ggpoFailed || linkFailed || setupFailed) ? 1 : 0;
        } else if (strncmp(a, "--netplay-", 10) == 0) {
            if (NetplayMatch_ParseArg(a) != 1) {
                fprintf(stderr, "Invalid argument: %s\n", a);
                return 1;
            }
        } else if ((v = match_kv(a, "replay-skip")) != NULL) {
            replay_skip = v;
        } else if ((v = match_kv(a, "replay-max-errors")) != NULL) {
            replay_max_errors = atoi(v);
            if (replay_max_errors < 1) {
                fprintf(stderr, "Invalid --replay-max-errors value '%s' (expected >= 1)\n", v);
                return 1;
            }
        } else if (strncmp(a, "--rollback", 10) == 0) {
            if (Rollback_ParseArg(a) != 1) {
                fprintf(stderr, "Invalid argument: %s\n", a);
                return 1;
            }
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", a);
            print_usage(argv[0]);
            return 1;
        }
    }

    /* A replay test is a batch run: unpace the frame loop (the game clock is
     * driven per-iteration by the rig, not by the wall clock), skip the loader
     * intro, and keep the log on the terminal where the test output belongs.
     * Both are read inside Platform_Init, so they must be set before it. */
    if (Rollback_Enabled() && !replay_file) {
        fprintf(stderr, "--rollback needs --replay-test=<file.swr> (the match's setup and inputs)\n");
        return 1;
    }
    if (NetplayMatch_Enabled() && !replay_file) {
        fprintf(stderr, "--netplay-match needs --replay-test=<file.swr> (the match's setup and inputs)\n");
        return 1;
    }
    if (NetplayMatch_Enabled() && Rollback_Intervenes()) {
        fprintf(stderr, "--netplay-match takes only the rollback rig's watching options "
                        "(--rollback=trace, -trace, -dump, -save-state): the others rewind the "
                        "match GGPO is playing\n");
        return 1;
    }
    if (NetplayMatch_ArgError()) {
        fprintf(stderr, "%s\n", NetplayMatch_ArgError());
        return 1;
    }
    if (NetplayMatch_IsP2P() && (!Rollback_Enabled() || Rollback_WatchesFrames())) {
        fprintf(stderr, "--netplay-match=p2p plays in the rollback rig's environment: give it "
                        "--rollback=trace and no other --rollback option (--netplay-trace writes "
                        "the peer's trace)\n");
        return 1;
    }
    if (replay_file) {
#if defined(SWOS_WASM_BUILD)
        /* The browser's SDL has no dummy drivers -- it draws to its canvas, which
         * is where a replay in a browser is meant to go anyway (it was
         * "SDL_Init: dummy not available" and no game at all). */
        setenv("SWOS_NO_LOG", "1", 1);
#elif defined(_WIN32)
        _putenv("SWOS_HARNESS_TURBO=1");
        _putenv("SWOS_NO_LOG=1");
        if (!replay_window && !getenv("SDL_VIDEODRIVER"))
            _putenv("SDL_VIDEODRIVER=dummy");
        if (!getenv("SDL_AUDIODRIVER"))
            _putenv("SDL_AUDIODRIVER=dummy");
#else
        setenv("SWOS_HARNESS_TURBO", "1", 1);
        setenv("SWOS_NO_LOG", "1", 1);
        /* Headless by default: a replay is a batch run, and a full matrix opens
         * one window per cell. The dummy video driver keeps every buffer and
         * blit real — the game still renders into the frontbuffer, which is
         * what the simulation reads back — it just never opens a window.
         * --replay-window opts back in to watching it. Respect an
         * SDL_VIDEODRIVER the caller set explicitly. */
        if (!replay_window && !getenv("SDL_VIDEODRIVER"))
            setenv("SDL_VIDEODRIVER", "dummy", 1);
        /* Silent too, and unconditionally — a replay never wants to be heard,
         * and a matrix run is 48 of them. The dummy driver still opens a
         * device and still drains the queue on its own clock, so the audio
         * pump and everything it shares a tick with behave exactly as they do
         * with a real card; it just goes nowhere. Respect an explicit
         * SDL_AUDIODRIVER from the caller. */
        if (!getenv("SDL_AUDIODRIVER"))
            setenv("SDL_AUDIODRIVER", "dummy", 1);
#endif
        /* The rollback rig plays its own match and leaves gameplay, the fire-hold
         * lockout and audio to the command line -- and needs no audio device or pump
         * thread writing game memory behind the game loop's back (docs/ROLLBACK.md).
         * Nor does a netplay match, which re-simulates frames. */
        if (Rollback_Enabled() || NetplayMatch_Enabled())
            SDL_setenv("SWOS_AUDIO_OFF", "1", 1);
        if (!Rollback_Enabled())
        opts.dos_gameplay = 1;   /* the corpus is DOS gameplay (docs/TESTS.md 5c) */
        /* The recordings were made against a reference implementation of the
         * ORIGINAL, so a replay has to PLAY the original — including its
         * fire-hold lockout bug. Without this the rig diverges on quick_fire /
         * normal_fire the first time a tackle lands on a held button, which is
         * ~55 times a match. Not overridable: a replay is a comparison, not a
         * game. See _gOriginalFireHoldLockout in team_control.c.
         *
         * WHEN THE NEW RECORDING FORMAT LANDS, THIS BECOMES PER FILE.
         * The owner's call (2026-09-13): new recordings are made with the fix
         * ACTIVE, because fixed is the new norm. So this constant is only right
         * for the .RGD corpus — read the setting out of the recording's own
         * header instead and fall back to 1 for a format that has no such
         * field (every .RGD). Do NOT decide it by format: that exact shortcut
         * was already paid for once with the replay framerate, where the
         * authored rate turned out to be per FILE (a gameStyle byte) and
         * hardcoding it per format broke the reels that disagreed —
         * see project_replay_framerate_decouple. */
        if (!Rollback_Enabled())
        opts.fire_hold_lockout = 1;
        /* And the original's roll: its recordings only replay with the squad-slot lookup
         * (docs/BALL_CONTEST.md -- playa-10090613 parts at frame 7,009 without it). */
        if (!Rollback_Enabled())
        opts.contest_own_ratings = 0;
        /* And the original's CPU card immunity: the computer side of a corpus
         * match was never carded when the recordings were made, and a card
         * changes everything after it (the referee walks over, play restarts). */
        if (!Rollback_Enabled())
        opts.cpu_cards = 0;
        /* Replay at the port's OWN native view, not the tester's monitor. The
         * original reason given here — "SWOS's camera constants are all derived
         * from the view size" — no longer holds: b4775f3 pinned _sim_view_width
         * to SCREEN_WIDTH (below), so the SIMULATION is supposed to be
         * width-independent and only the renderer draws wider. The pin stays as
         * the DEFAULT because that is the geometry the corpus was recorded in.
         *
         * --replay-width=N lifts it deliberately. That is not a curiosity: if
         * the simulation really is width-independent then every compared field
         * must be identical at any N, so a divergence at N != 336 is a
         * render/simulation leak and nothing else. Three tester-reported bugs in
         * one day were widescreen-only and invisible here. */
        opts.res_w = (replay_width > 0) ? replay_width : SCREEN_WIDTH;
        opts.res_h = SCREEN_HEIGHT;
    }
    if (view_replay) {
        /* A batch run like --replay-test: headless, silent and unpaced unless asked.
         * --replay-paced (or a window, which is for watching) keeps the real frame
         * clock -- what SWOS_PRESENT_STATS measures a replay's pacing against. */
        const int unpaced = !(replay_paced || replay_window);
#if defined(SWOS_WASM_BUILD)
        /* Except in a browser, whose SDL has no dummy drivers and whose canvas is
         * where the replay is meant to be watched anyway. */
        (void)unpaced;
        setenv("SWOS_NO_LOG", "1", 1);
#elif defined(_WIN32)
        if (unpaced)
            _putenv("SWOS_HARNESS_TURBO=1");
        _putenv("SWOS_NO_LOG=1");
        if (!replay_window && !getenv("SDL_VIDEODRIVER"))
            _putenv("SDL_VIDEODRIVER=dummy");
        if (!getenv("SDL_AUDIODRIVER"))
            _putenv("SDL_AUDIODRIVER=dummy");
#else
        if (unpaced)
            setenv("SWOS_HARNESS_TURBO", "1", 1);
        setenv("SWOS_NO_LOG", "1", 1);
        if (!replay_window && !getenv("SDL_VIDEODRIVER"))
            setenv("SDL_VIDEODRIVER", "dummy", 1);
        if (!getenv("SDL_AUDIODRIVER"))
            setenv("SDL_AUDIODRIVER", "dummy", 1);
#endif
        if (replay_width > 0) {
            opts.res_w = replay_width;
            opts.res_h = SCREEN_HEIGHT;
        }
        if (ReplayV2_ArmViewTest(view_replay) != 0)
            return 1;
    }
    if (SrpResim_Armed()) {
        /* The match is played by the rules it was recorded with, whatever SWOS.ini says;
         * headless, silent and unlogged like the replay rig, and with no audio pump
         * writing game memory behind the step's back (docs/ROLLBACK.md). */
        int recView;
        if (SrpResim_Rules(&opts.dos_gameplay, &opts.fire_hold_lockout, &recView) != 0)
            return 1;
        if (recView >= 0)
            opts.view_style = recView ? VIEW_STYLE_DOS : VIEW_STYLE_AMIGA;
#if defined(SWOS_WASM_BUILD)
        /* The browser's SDL has no dummy drivers: it draws to its canvas, as it would. */
        setenv("SWOS_NO_LOG", "1", 1);
#elif defined(_WIN32)
        _putenv("SWOS_HARNESS_TURBO=1");
        _putenv("SWOS_NO_LOG=1");
        if (!getenv("SDL_VIDEODRIVER")) _putenv("SDL_VIDEODRIVER=dummy");
        if (!getenv("SDL_AUDIODRIVER")) _putenv("SDL_AUDIODRIVER=dummy");
#else
        setenv("SWOS_HARNESS_TURBO", "1", 1);
        setenv("SWOS_NO_LOG", "1", 1);
        if (!getenv("SDL_VIDEODRIVER")) setenv("SDL_VIDEODRIVER", "dummy", 1);
        if (!getenv("SDL_AUDIODRIVER")) setenv("SDL_AUDIODRIVER", "dummy", 1);
#endif
        SDL_setenv("SWOS_AUDIO_OFF", "1", 1);
    }

    /* Whole-match recording (src/replay_v2.c). */
    ReplayRec_Configure(record_replay, opts.replay_auto_save);

    /* The matchmaking server the MULTIPLAYER menu logs in to (src/mm_client.c).
     * SWOS_MM_HOST / SWOS_MM_PORT still win, so a test can point one process
     * somewhere else without touching the player's SWOS.ini. */
    MmClient_SetConfiguredServer(opts.matchmaker);
    /* And where a live match is recorded, if anywhere (SWOS_RECORD still wins). On by
     * default: an empty record= -- every SWOS.ini written before this, too -- means
     * "server", which records the matches the matchmaker arranges, to it (match_record.c).
     * "off" turns it off. Not in a browser: its recording is not wired yet. */
#ifndef __EMSCRIPTEN__
    if (!opts.record[0])
        snprintf(opts.record, sizeof opts.record, "server");
#endif
    MatchRecord_SetConfigured(opts.record);
    /* And whether a match report leaves this machine (include/match_report.h). */
    MatchReport_SetEnabled(opts.reports);
    { extern void ReplayRec_SetPredictionKept(int32_t on); ReplayRec_SetPredictionKept(opts.prediction); }
    { extern void NetplayMatch_SetBalance(int32_t on); NetplayMatch_SetBalance(opts.netplay_balance); }
    /* The testers' rule, for offline play (include/test_rules.h). Said in the log: a
     * report from a career played without injuries is read differently. */
    TestRules_SetSetting(opts.no_injuries_cards);
    {   /* ... or from the environment, for one run (the harness drives careers with it;
         * the name is the old compile-time flag's, which the docs and scripts still use) */
        const char *e = getenv("SWOS_TEST_NO_INJURIES_CARDS");
        if (e && e[0])
            TestRules_SetSetting(e[0] != '0');
    }
    if (TestRules_Setting())
        fprintf(stderr, "[testing] no_injuries_cards = true: offline matches book and injure nobody\n");

    /* Publish DOS-mode flags for the game code (default = Amiga graphics). */
    _gDosGraphics = opts.dos_graphics;
    JagGfx_SetEnabled(opts.jag_graphics);
    PitchMap_SetEnabled(opts.pitch_maps);
    JagGfx_SetShading(opts.outfit_shading, opts.shadow_shading, opts.hair_shading, opts.shade_step);
    KitArt_SetJaguar(opts.kit_art);
    { extern void InputSDL_SetStickDeadzone(int percent); InputSDL_SetStickDeadzone(opts.stick_deadzone); }
    {
        extern void InputSDL_SetPressLatch(int on);
        extern int InputSDL_SetKeyBinding(int which, const char *name);
        int i;
        InputSDL_SetPressLatch(opts.latch_presses);
        for (i = 0; i < 5; i++)
            InputSDL_SetKeyBinding(i, opts.key_bind[i]);
    }
    /* SWOS_SOUND_STYLE=pc, SWOS_COMMENTARY=on: for the test scripts, which pass no options. */
    { const char *e = getenv("SWOS_SOUND_STYLE");
      if (e) opts.sound_style = strcmp(e, "pc") == 0;
      e = getenv("SWOS_COMMENTARY_DIR");
      if (e) snprintf(opts.commentary_dir, sizeof opts.commentary_dir, "%s", e);
      e = getenv("SWOS_MUSIC");
      if (e) opts.music_style = strcmp(e, "pc") == 0;
      e = getenv("SWOS_COMMENTARY");
      if (e) opts.commentary = strcmp(e, "auto") == 0 ? 2 : strcmp(e, "off") != 0; }
    /* The data setup (tools/swosdata) given the PC game but not the Amiga disks
     * makes no Amiga music, effects or loading screens: play the PC game's sound
     * and music whatever SWOS.ini says, and skip the screens. Without this the
     * loaders ask for the missing files for ever. */
    {
        extern int swos_data_exists(const char *rel);
        extern int _gNoAmigaSound, _gNoAmigaScreens;
        extern volatile uint8_t _skipLoaderIntro;
        if (!swos_data_exists("sound/SFX.SNG")) {
            _gNoAmigaSound = 1;
            opts.sound_style = 1;
            opts.music_style = 1;
            fprintf(stderr, "[assets] no Amiga music or effects (sound/): "
                            "playing the PC game's sound and music\n");
        }
        if (!swos_data_exists("grafs_aga/LOADER1.RAW") && !swos_data_exists("grafs/LOADER1.RAW")) {
            _gNoAmigaScreens = 1;
            _skipLoaderIntro = 1;
            fprintf(stderr, "[assets] no Amiga loading screens: skipping the intro\n");
        }
    }
    DosSound_SetCommentaryDir(opts.commentary_dir);
    DosSound_SetStyle(opts.sound_style);
    DosMusic_SetStyle(opts.music_style);
    DosMusic_SetMenuRecording(opts.menu_recording);
    DosSound_SetCommentary(opts.commentary);
    { extern int _gArcadeMode; _gArcadeMode = opts.arcade; }
    _gDosGameplay = opts.dos_gameplay;
    { extern int _gOriginalFireHoldLockout;
      _gOriginalFireHoldLockout = opts.fire_hold_lockout; }
    { extern int _gContestOwnRatings;
      _gContestOwnRatings = opts.contest_own_ratings; }
    { extern int _gCpuTeamsGetCards;
      extern uint16_t _cpuCardsOption;   /* the OPTIONS row's display mirror */
      _gCpuTeamsGetCards = opts.cpu_cards;
      _cpuCardsOption = (uint16_t)(opts.cpu_cards != 0); }
    /* The OPTIONS menu draws its GAMEPLAY row from a uint16_t mirror (the
     * string-table header reads its index as one). Seed it here so the row
     * shows what SWOS.ini / --dos-gameplay actually selected; the toggle
     * itself keeps the two in step (ChangeGameplayStyle in menus.c). */
    { extern uint16_t _gameplayStyleOption;
      _gameplayStyleOption = (uint16_t)(opts.dos_gameplay != 0); }

    /* And say which style is actually in force, so "it ran in Amiga mode" is
     * something a tester can read off rather than infer from feel. */
    fprintf(stderr, "[gameplay] %s\n", _gDosGameplay
            ? "DOS (70fps physics and timers, DOS formations)"
            : "AMIGA (the shipped default)");
    /* Say it out loud — this one is a deliberate departure from the original,
     * so a tester comparing against real SWOS can see which half he is on. */
    fprintf(stderr, "[gameplay] contested ball: %s\n", opts.contest_own_ratings
            ? "each side rated by the man at the ball (contest_own_ratings; offline matches only)"
            : "the original's roll (one side rated by a team-mate, as both releases do)");
    fprintf(stderr, "[gameplay] fire-hold lockout: %s\n", opts.fire_hold_lockout
            ? "ON — original Amiga behaviour (an opponent's tackle kills your shot charge)"
            : "off — fixed (an opponent's tackle no longer kills your shot charge)");
    fprintf(stderr, "[gameplay] CPU cards: %s\n", opts.cpu_cards
            ? "on — a computer side can be booked and sent off (the original never carded it)"
            : "off — the original's rule (a team with no human controller is never carded)");
    /* _screen_width (the live render width) + _menu_draw_x_offset (UI centering)
     * are derived from the monitor's aspect ratio just after SDL_Init — see below. */
    if (opts.dos_dir[0])
        snprintf(_gDosGraphicsDir, sizeof _gDosGraphicsDir, "%s", opts.dos_dir);
    /* SWOS 2020's pitches come from the player's own SWOS 2020 install (the data
     * setup copies them); without them, draw the PC game's. SWOS.ini keeps "2020". */
    if (_gDosGraphics && strcmp(_gDosGraphicsDir, "grafs_2020") == 0) {
        extern int swos_data_exists(const char *rel);
        if (!swos_data_exists("grafs_2020/pitch1.dat")) {
            snprintf(_gDosGraphicsDir, sizeof _gDosGraphicsDir, "%s", "grafs_dos");
            fprintf(stderr, "[assets] no SWOS 2020 pitches (grafs_2020/): drawing the PC game's\n");
        }
    }

    /* Redirect stderr to SWOS.log in the per-user game folder (beside SWOS.ini
     * and save/ — swos_user_dir(), resolved above). Saves the tester from having
     * to remember `2> SWOS.log`, and keeps the log out of a possibly read-only
     * install dir. Works the same from gdb, Explorer, or a terminal. Disable by
     * setting SWOS_NO_LOG=1 to send stderr back to the console. Unbuffered so
     * lines flush immediately and we don't lose context on a crash. */
#ifdef SWOS_SWITCH_BUILD
    /* Switch: don't redirect. stderr on a devkit reaches the target log through
     * the SDK, and a log FILE on the save: mount would burn the user's save
     * quota and need committing to survive anyway. `ControlTarget read-log`
     * is where the output goes — start it BEFORE launching (docs/SWITCH.md). */
#else
    if (getenv("SWOS_NO_LOG") == NULL) {
        char log_path[1088];
        snprintf(log_path, sizeof log_path, "%s/SWOS.log", swos_user_dir());
        FILE *log = freopen(log_path, "w", stderr);
        if (log) {
            setvbuf(log, NULL, _IONBF, 0);
        }
    }
#endif

    /* First line of SWOS.log: which build wrote it. Deliberately AFTER the
     * redirect above -- printed any earlier it goes to a terminal the tester
     * does not have, and the log they actually send us is the thing that has to
     * answer "which build is this?" on its own. */
    fprintf(stderr, "[build] %s\n", Build_Version());

    /* Load the UI translation table for the selected language
     * (data_json/translations/<lang>.json) now that the CWD is the binary dir
     * and stderr is the log. tr(symbol) resolves menu/UI text through it; a
     * missing file just leaves every lookup falling back to the compiled
     * string symbol. Must run before the menu/competition install reads it. */
    {
        extern void Translations_Load(const char *lang);
        extern void Countries_Localize(void);
        extern void Currency_Set(const char *name);
        Translations_Load(opts.language);
        Countries_Localize();
        Currency_Set(opts.currency);   /* pound (stock £) | dollar ($) | euro (€) */
    }

    if (_gDosGraphics)
        fprintf(stderr, "[dos] DOS graphics mode ON (bin/%s)\n", _gDosGraphicsDir);

    SDL_SetMainReady();

    /* Become per-monitor DPI-aware on Windows BEFORE SDL_Init (Windows reads the
     * process DPI awareness at init). Without this SDL2 runs DPI-UNAWARE, so on a
     * scaled display (a 16" laptop at 150% is the Windows default) Windows
     * virtualizes coordinates: SDL_CreateWindow sizes are LOGICAL pixels that
     * Windows then scales UP — a 3x window (1452x816 logical) becomes ~2178x1224
     * physical and overflows a 1920x1080 panel, hiding the title bar. It also
     * broke our fit clamp: SDL_GetDisplayUsableBounds reports PHYSICAL pixels
     * while the window is sized in LOGICAL pixels, so the two never agree. Making
     * the process DPI-aware puts everything in physical pixels, so win_scale and
     * the usable-bounds clamp are consistent and the window fits its monitor.
     * Set via the raw hint name so it compiles on any SDL2 (the macro is 2.24+)
     * and is ignored on Linux/WASM/older SDL. */
    SDL_SetHint("SDL_WINDOWS_DPI_AWARENESS", "permonitorv2");
#ifdef __ANDROID__
    /* A sensor must never claim P1 ahead of the handheld's real controls. */
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS |
                  SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    /* (swos_save_dir_init() now runs earlier, before Settings_Load — see above.) */

    /* Match the render aspect to `resolution` (or the desktop when resolution=auto):
     * width = SCREEN_HEIGHT * (w/h), clamped to [SCREEN_WIDTH .. the full pitch width].
     * The render fills any aspect — 4:3, 16:9, 21:9, ultrawide — with no letterbox bars,
     * and any arbitrary resolution works (it need not match the monitor). Needs
     * SDL_INIT_VIDEO (above); runs before the window/texture (Platform_Init below). */
    {
        /* Clamp the requested display index ([video] screen=N) to a valid one —
         * 0 = primary. The SAME index is used here for the render aspect AND by
         * Platform_Init to place + size the window, so the aspect always matches
         * the monitor the window opens on (fixes "sized from monitor 0, dropped
         * on a different screen, overflows it" on multi-monitor rigs). */
        int nd = SDL_GetNumVideoDisplays();
        if (nd < 1) nd = 1;
        if (opts.screen < 0 || opts.screen >= nd) opts.screen = 0;

    /* THE DOS VIEW. 320x200, as the PC release draws it, and as a whole: the
     * renderer AND the simulation, because the view size feeds the onScreen cull
     * (GAMEPLAY.md §25.4). Drawing 200 lines while the simulation believed 272
     * would position the camera for 272 and crop what it culled in -- less than
     * the game thinks is visible, which is worse than either original.
     *
     * Every camera constant is already f(view) and lands on the DOS numbers here:
     * SIM_CAMERA_MAX_Y = 664 - (200-200) = 664, which is swos.asm:7539's
     * `mov word ptr D2, 664`, and SIM_CAMERA_POS_MAX_Y = 880-200 = 680, which is
     * swos-port's kCameraMaxY. SIM_CAM_AIM_HEIGHT already used 200 under DOS
     * gameplay before any of this existed. */
    int dosView;
    {
        dosView = (opts.view_style == VIEW_STYLE_DOS) ||
                      (opts.view_style == VIEW_STYLE_AUTO && opts.dos_gameplay);
        /* Remembered, not applied: the MENUS are 272 lines tall (Amiga artwork),
         * so switching the whole game to 200 cuts their bottom rows off -- which
         * is exactly what it did when this was set at startup. The view changes
         * at kick-off and changes back after (game_loop.c). */
        { extern int _gDosView; _gDosView = dosView; }
        if (dosView)
            ;   /* reported below, once the display shape is known */
        if (opts.view_style == VIEW_STYLE_AMIGA && opts.dos_gameplay)
            fprintf(stderr, "[video] DOS gameplay with the Amiga view (--view): a mix that "
                            "matches neither original -- without --view the style sets both\n");
        if (opts.view_style == VIEW_STYLE_DOS && !opts.dos_gameplay)
            fprintf(stderr, "[video] the DOS view with Amiga gameplay (--view): a mix that "
                            "matches neither original -- without --view the style sets both\n");
    }

        int dw, dh;
        if (opts.res_w > 0 && opts.res_h > 0) {
            /* SWOS.ini [video] resolution=WxH — any aspect, monitor-independent. */
            dw = opts.res_w; dh = opts.res_h;
        } else {
            /* 'auto': derive the render aspect from the chosen display's desktop
             * mode, but DON'T store it back into opts — leaving res_w/res_h at 0
             * keeps the file's "resolution=auto" intact when Settings_Save runs
             * (otherwise 'auto' would be rewritten as the resolved WxH). */
            SDL_DisplayMode dm;
            dw = 16; dh = 9;   /* 16:9 fallback if the query fails */
            if (SDL_GetDesktopDisplayMode(opts.screen, &dm) == 0 && dm.h > 0) { dw = dm.w; dh = dm.h; }
        }
        /* The DOS view is 200 lines, so its widescreen width is 200 * the display
         * shape -- about 356 on 16:9 -- not 272 * it. Same rule as the Amiga view,
         * different height. The SIMULATION stays pinned at 320x200 either way
         * (MatchViewEnter), so a wider window shows more of the margins and plays
         * exactly the same game: the port's whole widescreen bargain. */
        {
            extern int _dos_view_width;
            int dosw = (int)((long)DOS_VIEW_HEIGHT * dw / dh);
            dosw = (dosw + 1) & ~1;
            if (dosw > dw)                 dosw = dw & ~1;
            if (dosw < DOS_VIEW_WIDTH)     dosw = DOS_VIEW_WIDTH;
            if (dosw > PITCH_VIEW_MAX_WIDTH) dosw = PITCH_VIEW_MAX_WIDTH;
            if (opts.widescreen != 1) dosw = DOS_VIEW_WIDTH;   /* the PC release's own (stretched or not) */
            _dos_view_width = dosw;
        }
        int w = (int)((long)SCREEN_HEIGHT * dw / dh);
        int aspectW;
        w = (w + 1) & ~1;                          /* round up to even */
        aspectW = w;
        /* NEVER WIDER THAN THE DISPLAY IT IS FILLING. The width above is "how much game
         * fits this shape at 272 lines", which is the point on a 2560x1080 monitor -- but
         * a small resolution means a small screen, not a wide one. `resolution=320x200`
         * (someone comparing against DOS, which really is 320x200) has an aspect of
         * 1.60:1, and 272 * 1.60 = 436, so the game came up 436x272: WIDER than either
         * original, showing more pitch than the thing being compared against. Reported by
         * a tester 2026-09-18, and reproduced exactly. */
        if (w > dw) w = dw & ~1;
        if (w < SCREEN_WIDTH)          w = SCREEN_WIDTH;
        if (w > PITCH_VIEW_MAX_WIDTH)  w = PITCH_VIEW_MAX_WIDTH;
        if (opts.widescreen != 1)      w = SCREEN_WIDTH;   /* the Amiga's own width (stretched or not) */
        _screen_width = w;
        /* WIDESCREEN IN THE DOS VIEW means the widescreen PICTURE: the same w x 272 as the
         * Amiga view's, showing more pitch in both directions -- not the 200-line DOS
         * picture widened, which only zoomed in (ReportBot 0013: "when the match starts it
         * throws you back to DOS resolution"). The match is still simulated at the PC
         * release's 320x200 (MatchViewEnter pins _sim_view_*); only the picture is taller,
         * mapped onto that camera by Render_CameraY. Without widescreen: 320x200 as ever. */
        {
            extern int _dos_view_width, _dos_view_height;
            if (opts.widescreen == 1) {
                _dos_view_width = w;
                _dos_view_height = SCREEN_HEIGHT;
            }
            if (dosView)
                fprintf(stderr, "[video] DOS view: %dx%d in a match%s; the simulation is "
                                "the PC release's %dx%d either way\n",
                        _dos_view_width, _dos_view_height,
                        _dos_view_height > DOS_VIEW_HEIGHT ? " -- widescreen, showing more pitch" : "",
                        DOS_VIEW_WIDTH, DOS_VIEW_HEIGHT);
        }
        fprintf(stderr, "[video] render %dx%d (screen %d/%d, resolution %dx%d, %.2f:1)\n",
                _screen_width, _screen_height, opts.screen, nd, dw, dh, (double)dw / dh);
        /* Say so when the asked-for size is not what came out, because "I set 320x200 and
         * got 436x272" is otherwise a silent surprise. The height is not negotiable: the
         * frontbuffer, the HUD and every menu are 272 lines (PLATFORM_SCREEN_H), and the
         * width has a floor of 336 for the same reason. */
        /* Only when the number LOOKS like a render size (<= the widest the game draws).
         * 1920x1080 is obviously a monitor and needs no explaining; 320x200 and 640x480
         * are exactly what someone types when they mean "run at this size". */
        if (opts.res_w > 0 && opts.res_h > 0 && opts.res_w <= PITCH_VIEW_MAX_WIDTH &&
            (_screen_width != opts.res_w || _screen_height != opts.res_h))
            fprintf(stderr, "[video] %dx%d is the render size, not %dx%d: the height is "
                            "always %d (the HUD and menus are that tall) and the width is "
                            "%d..%d%s\n",
                    _screen_width, _screen_height, opts.res_w, opts.res_h, _screen_height,
                    SCREEN_WIDTH, PITCH_VIEW_MAX_WIDTH,
                    aspectW > dw ? " -- capped at the width you asked for" : "");
    }
    /* The SIMULATION view (display.h) is the port's own fixed geometry, NOT the
     * render width. SWOS reads the view size from inside the simulation — the
     * onScreen cull decides which players auto-selection may pick — so tying
     * it to the render width made a widescreen display change CPU behaviour:
     * a 650-wide view admits men the Amiga culls, and the nearest-to-the-ball
     * choice changes. Pinning it here means every display plays the same game
     * and the renderer simply draws wider (GAMEPLAY.md §25.4/§25.5).
     * The replay rig overrides it to the geometry a recording was made in. */
    _sim_view_width  = SCREEN_WIDTH;
    _sim_view_height = SCREEN_HEIGHT;


    /* What the match's draw-dependent decisions are made for (display.h): the render
     * width, so a wide display animates its margins. A recording was made with no
     * margin, so the rig pins 336 unless told otherwise; a netplay session sets the
     * width both peers agreed (NetplayMatch_StartLive). */
    _sim_draw_width = replay_file ? (replay_sim_width > 0 ? replay_sim_width : SCREEN_WIDTH)
                                  : _screen_width;
    if (_sim_draw_width < SCREEN_WIDTH)        _sim_draw_width = SCREEN_WIDTH;
    if (_sim_draw_width > PITCH_VIEW_MAX_WIDTH) _sim_draw_width = PITCH_VIEW_MAX_WIDTH;

    _menu_draw_x_offset = WIDE_MARGIN;

    /* Arm the replay rig before the bootstrap: SWOS.c enters it in place of
     * the front-end loop once the game data is loaded. Parsing here means a
     * bad path or a stale .swr fails immediately, not after the intro. */
    if (replay_file &&
        ReplayTest_Load(replay_file, replay_level, replay_max_errors, replay_skip) != 0) {
        SDL_Quit();
        return 1;
    }

    if (Platform_Init(&opts) != 0) {
        fprintf(stderr, "Platform_Init failed\n");
        SDL_Quit();
        return 1;
    }

    /* Game data an older OpenSWOS Setup made (ASSETS.ini): say so, once. */
    { extern void AssetsCheck_Run(PlatformInitOptions *opts, const char *iniPath);
      AssetsCheck_Run(&opts, ini_path); }

    /* Hand off to the (ported) Amiga bootstrap. startShim runs _start()
     * (hardware init -- now no-ops via stubs) then mainC() which is the
     * actual game loop. The game pumps SDL via Platform_VBlank() callbacks. */
    startShim();

    /* Persist settings WITHOUT clobbering a hand-edited SWOS.ini: create the
     * file if it doesn't exist, otherwise only add keys it's missing (e.g. new
     * settings from a newer build). Existing values/comments are left as the
     * user wrote them. Done BEFORE Platform_Shutdown destroys the window, since
     * the getters used for any newly-added scale/fullscreen keys read SDL flags. */
    Settings_AppendMissing(&opts, ini_path);

    MatchReport_Flush(5000);        /* a match report still on its way to the server */
    Platform_Shutdown();
    SDL_Quit();
    return 0;
}

#else /* !SWOS_SDL_BUILD -- Amiga m68k host */

/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "swos_asm.h"

__attribute__((used, externally_visible)) volatile uint16_t *_pPanicReasonCode = &_panicReasonCode;

/* Minimal libc stubs needed when LTO/Ofast optimizes loops into library calls */
__attribute__((optimize("no-tree-loop-distribute-patterns")))
void *memcpy(void *dest, const void *src, unsigned long len) {
    char *d = (char *)dest;
    const char *s = (const char *)src;
    while (len--)
        *d++ = *s++;
    return dest;
}

__attribute__((optimize("no-tree-loop-distribute-patterns")))
void *memset(void *dest, int val, unsigned long len) {
    unsigned char *ptr = (unsigned char *)dest;
    while (len-- > 0)
        *ptr++ = (unsigned char)val;
    return dest;
}

__attribute__((optimize("no-tree-loop-distribute-patterns")))
void *memmove(void *dest, const void *src, unsigned long len) {
    char *d = (char *)dest;
    const char *s = (const char *)src;
    if (d < s) {
        while (len--)
            *d++ = *s++;
    } else {
        const char *lasts = s + (len - 1);
        char *lastd = d + (len - 1);
        while (len--)
            *lastd-- = *lasts--;
    }
    return dest;
}

int main(void)
{
    startShim();
    return 0;
}
#endif /* SWOS_SDL_BUILD */
