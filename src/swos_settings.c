/*
 * swos_settings.c — INI-backed persistent display + audio settings.
 *
 * Layout (SWOS.ini):
 *
 *   [video]
 *   renderer=accel        ; software | accel
 *   vsync=false           ; true | false
 *   subpixel_scroll=true  ; true | false — smooth sub-pixel scrolling
 *   scaling=smooth        ; sharp | smooth — even pixels at a non-integer scale
 *   crt_mask=off          ; off | aperture | slot | scanlines
 *   crt_mask_strength=45  ; 0-100
 *   crt_vignette=0        ; 0-100
 *   crt_corners=0         ; 0-100
 *   fullscreen=off        ; off | desktop | exclusive
 *   scale=3               ; 1..4 (windowed only)
 *
 *   [audio]
 *   mode=mono             ; mono | stereo
 *
 *   [dos]                 ; PC-only extras (Amiga is the definitive version)
 *   directory=grafs_dos   ; folder (under bin/) to load DOS assets from
 *   graphics=false        ; true | false — render DOS graphics
 *   gameplay=false        ; true | false — DOS gameplay tweaks (reserved)
 *
 * Boolean values accept on/off, true/false, yes/no, 1/0 — case-insensitive.
 * Unknown keys / sections are silently ignored so the file format can grow
 * without breaking older binaries.
 *
 * The writer (Settings_Save) is NON-DESTRUCTIVE: it rewrites the file
 * line-by-line, changing only the values of known keys that actually changed
 * and appending keys the file is missing. Comments, blank lines, commented-out
 * keys (";resolution=...") and unknown keys are preserved verbatim and in place.
 */

#include "tweaks.h"   /* [tweaks] */
#include "swos_settings.h"
#include "project_identity.h"
#include "swos_io.h"   /* swos_save_commit — Switch save durability */
#include "ini.h"
#include "dos_mode.h"   /* GAMEPLAY_*: [gameplay] style */
#include "touch_controls.h" /* TOUCH_CONTROLS_*: [input] touch_controls */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Arcade game-mode name<->index (defined in arcade.c) for [arcade] mode. */
extern const char *Arcade_ModeName(int idx);
extern int         Arcade_ModeFromName(const char *name);

static int eqi(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (tolower(ca) != tolower(cb)) return 0;
    }
    return *a == 0 && *b == 0;
}

static int parse_bool(const char *v, int *out)
{
    if (eqi(v, "on") || eqi(v, "true") || eqi(v, "yes") || eqi(v, "1")) {
        *out = 1; return 1;
    }
    if (eqi(v, "off") || eqi(v, "false") || eqi(v, "no") || eqi(v, "0")) {
        *out = 0; return 1;
    }
    return 0;
}

static int handler(void *user, const char *section,
                   const char *name, const char *value)
{
    PlatformInitOptions *opts = (PlatformInitOptions *)user;

    if (eqi(section, "video")) {
        if (eqi(name, "renderer")) {
            if      (eqi(value, "software")) opts->renderer = PLATFORM_RENDERER_SOFTWARE;
            else if (eqi(value, "accel"))    opts->renderer = PLATFORM_RENDERER_ACCEL;
        } else if (eqi(name, "vsync")) {
            int b;
            if (parse_bool(value, &b)) opts->vsync = b;
        } else if (eqi(name, "subpixel_scroll")) {
            int b;
            if (parse_bool(value, &b)) opts->subpixel_scroll = b;
        } else if (eqi(name, "crt_mask")) {
            if      (eqi(value, "off"))       opts->crt_mask = 0;
            else if (eqi(value, "aperture"))  opts->crt_mask = 1;
            else if (eqi(value, "slot"))      opts->crt_mask = 2;
            else if (eqi(value, "scanlines")) opts->crt_mask = 3;
        } else if (eqi(name, "crt_mask_strength")) {
            int n = atoi(value); if (n >= 0 && n <= 100) opts->crt_mask_strength = n;
        } else if (eqi(name, "crt_vignette")) {
            int n = atoi(value); if (n >= 0 && n <= 100) opts->crt_vignette = n;
        } else if (eqi(name, "crt_corners")) {
            int n = atoi(value); if (n >= 0 && n <= 100) opts->crt_corners = n;
        } else if (eqi(name, "scaling")) {
            if      (eqi(value, "smooth")) opts->scaling_smooth = 1;
            else if (eqi(value, "sharp"))  opts->scaling_smooth = 0;
        } else if (eqi(name, "render_driver")) {
            snprintf(opts->render_driver, sizeof opts->render_driver, "%s", value);
        } else if (eqi(name, "fullscreen")) {
            if      (eqi(value, "off"))       opts->fullscreen = PLATFORM_FULLSCREEN_NONE;
            else if (eqi(value, "desktop"))   opts->fullscreen = PLATFORM_FULLSCREEN_DESKTOP;
            else if (eqi(value, "exclusive")) opts->fullscreen = PLATFORM_FULLSCREEN_EXCLUSIVE;
        } else if (eqi(name, "scale")) {
            int n = atoi(value);
            if (n >= 1 && n <= 4) opts->win_scale = n;
        } else if (eqi(name, "widescreen")) {
            int b;
            if (eqi(value, "stretch"))       opts->widescreen = 2;
            else if (parse_bool(value, &b))  opts->widescreen = b;
        } else if (eqi(name, "resolution")) {
            /* "WxH" (e.g. 2560x1080) drives the render aspect, for testing other
             * resolutions without changing the desktop. "auto"/"desktop"/blank
             * (or unparseable) → 0,0 = use the actual desktop resolution. */
            int w = 0, h = 0;
            if (value && sscanf(value, "%d%*[xX]%d", &w, &h) == 2 && w > 0 && h > 0) {
                opts->res_w = w; opts->res_h = h;
            } else {
                opts->res_w = 0; opts->res_h = 0;
            }
        } else if (eqi(name, "screen")) {
            /* Which monitor to open on (0 = primary). Out-of-range is clamped to
             * a valid display at init, so a stale index just falls back to 0. */
            int n = atoi(value);
            if (n >= 0) opts->screen = n;
        }
    } else if (eqi(section, "audio")) {
        if (eqi(name, "mode")) {
            if      (eqi(value, "mono"))   opts->audio_mono = 1;
            else if (eqi(value, "stereo")) opts->audio_mono = 0;
        }
        else if (eqi(name, "style")) {
            if      (eqi(value, "amiga")) opts->sound_style = 0;
            else if (eqi(value, "pc") || eqi(value, "dos")) opts->sound_style = 1;
        }
        else if (eqi(name, "commentary_dir"))
            snprintf(opts->commentary_dir, sizeof opts->commentary_dir, "%s", value);
        else if (eqi(name, "menu_music")) {
            if      (eqi(value, "midi")) opts->menu_recording = 0;
            else if (eqi(value, "mp3"))  opts->menu_recording = 1;
        }
        else if (eqi(name, "music")) {
            if      (eqi(value, "amiga")) opts->music_style = 0;
            else if (eqi(value, "pc") || eqi(value, "dos")) opts->music_style = 1;
        }
        else if (eqi(name, "commentary")) {
            int b;
            if (eqi(value, "auto")) opts->commentary = 2;
            else if (parse_bool(value, &b)) opts->commentary = b;
        }
    } else if (eqi(section, "gameplay")) {
        int b;
        if (eqi(name, "fire_hold_lockout") && parse_bool(value, &b))
            opts->fire_hold_lockout = b;
        else if (eqi(name, "contest_own_ratings") && parse_bool(value, &b))
            opts->contest_own_ratings = b;
        else if (eqi(name, "style")) {
            if      (eqi(value, "amiga")) opts->dos_gameplay = GAMEPLAY_AMIGA;
            else if (eqi(value, "dos"))   opts->dos_gameplay = GAMEPLAY_DOS;
            else if (eqi(value, "xbox"))  opts->dos_gameplay = GAMEPLAY_XBOX;
        }
        /* `view` is gone (2026-09-29): the view is part of the style, one setting.
         * An old line is ignored rather than honoured -- a view that differs from the
         * style was a hybrid matching neither original, and online it was a second
         * rule two players had to agree on. --view on the command line still pins it,
         * for replaying a recording made that way (docs/PLAYBACK.md). */
    } else if (eqi(section, "input")) {
        if (eqi(name, "stick_deadzone")) {
            int n = atoi(value);
            if (n >= 5 && n <= 90) opts->stick_deadzone = n;
        } else if (eqi(name, "latch_presses")) {
            int b;
            if (parse_bool(value, &b)) opts->latch_presses = b;
        } else if (eqi(name, "touch_controls")) {
            if      (eqi(value, "auto"))   opts->touch_controls = TOUCH_CONTROLS_AUTO;
            else if (eqi(value, "always")) opts->touch_controls = TOUCH_CONTROLS_ALWAYS;
            else if (eqi(value, "never"))  opts->touch_controls = TOUCH_CONTROLS_NEVER;
        } else {
            static const char *const keys[5] = { "key_up", "key_down", "key_left", "key_right", "key_fire" };
            int i;
            for (i = 0; i < 5; i++)
                if (eqi(name, keys[i]))
                    snprintf(opts->key_bind[i], sizeof opts->key_bind[i], "%s", value);
        }
    } else if (eqi(section, "replays")) {
        int b;
        if (eqi(name, "auto_save") && parse_bool(value, &b))
            opts->replay_auto_save = b;
    } else if (eqi(section, "netplay")) {
        if (eqi(name, "matchmaker"))
            snprintf(opts->matchmaker, sizeof opts->matchmaker, "%s", value);
        else if (eqi(name, "name"))
            snprintf(opts->netplay_name, sizeof opts->netplay_name, "%s", value);
        else if (eqi(name, "remember_login")) {
            int b;
            if (parse_bool(value, &b))
                opts->netplay_remember = b;
        } else if (eqi(name, "record"))
            snprintf(opts->record, sizeof opts->record, "%s", value);
        else if (eqi(name, "live_cast")) {
            int b;
            if (parse_bool(value, &b))
                opts->live_cast = b;
        }
        else if (eqi(name, "reports")) {
            int b;
            if (parse_bool(value, &b))
                opts->reports = b;
        } else if (eqi(name, "prediction")) {
            int b;
            if (parse_bool(value, &b))
                opts->prediction = b;
        }
    } else if (eqi(section, "graphics")) {
        int b;
        if (eqi(name, "style")) {
            /* amiga | dos | 2020 -- one key for the art pipeline, because that is
             * what the engine has: DOS graphics plus a directory. "2020" is SWOS
             * 2020's art, which is why a path in the file was always a smell. */
            if (eqi(value, "amiga")) opts->dos_graphics = 0;
            else if (eqi(value, "dos")) {
                opts->dos_graphics = 1;
                snprintf(opts->dos_dir, sizeof opts->dos_dir, "grafs_dos");
            } else if (eqi(value, "2020")) {
                opts->dos_graphics = 1;
                snprintf(opts->dos_dir, sizeof opts->dos_dir, "grafs_2020");
            }
        }
        else if (eqi(name, "sprites")) {
            if      (eqi(value, "amiga"))  opts->jag_graphics = 0;
            else if (eqi(value, "jaguar")) opts->jag_graphics = 1;
            /* "auto" keeps the built-in default, which is the Amiga's on every
             * build (the web one included, since 2026-10-01). */
        }
        else if (eqi(name, "pitch_maps") && parse_bool(value, &b)) opts->pitch_maps = b;
        else if (eqi(name, "outfit_shading") && parse_bool(value, &b)) opts->outfit_shading = b;
        else if (eqi(name, "shadow_shading") && parse_bool(value, &b)) opts->shadow_shading = b;
        else if (eqi(name, "hair_shading") && parse_bool(value, &b)) opts->hair_shading = b;
        else if (eqi(name, "shade_step")) {
            int v = atoi(value);
            if (v >= 1 && v <= 5) opts->shade_step = v;
        }
        else if (eqi(name, "kit_art")) {
            if      (eqi(value, "amiga"))  opts->kit_art = 0;
            else if (eqi(value, "jaguar")) opts->kit_art = 1;
        }
    } else if (eqi(section, "arcade")) {
        int b;
        if (eqi(name, "enabled") && parse_bool(value, &b)) opts->arcade = b;
        else if (eqi(name, "mode") && value && value[0]) {
            int m = Arcade_ModeFromName(value); if (m >= 0) opts->arcade_mode = m;
        }
        else if (eqi(name, "game_seconds")) {
            int v = atoi(value); if (v >= 10 && v <= 600) opts->arcade_game_seconds = v;
        }
        else if (eqi(name, "golden_goal") && parse_bool(value, &b)) opts->arcade_golden_goal = b;
        else if (eqi(name, "penalties_on_draw") && parse_bool(value, &b)) opts->arcade_penalties = b;
        else if (eqi(name, "goal_replay") && parse_bool(value, &b)) opts->arcade_goal_replay = b;
        else if (eqi(name, "attract_audio") && value && value[0]) {
            if (eqi(value, "music+sfx") || eqi(value, "both")) opts->arcade_attract_audio = 2;
            else if (eqi(value, "sfx")) opts->arcade_attract_audio = 1;
            else if (eqi(value, "music")) opts->arcade_attract_audio = 0;
        }
        else if (eqi(name, "gameplay") && value && value[0]) {
            if (eqi(value, "dos")) opts->arcade_gameplay = 1;
            else if (eqi(value, "amiga")) opts->arcade_gameplay = 0;
        }
        else if (eqi(name, "key_p1_credit") && value && value[0])
            snprintf(opts->arcade_key_p1_credit, sizeof opts->arcade_key_p1_credit, "%s", value);
        else if (eqi(name, "key_p1_start") && value && value[0])
            snprintf(opts->arcade_key_p1_start, sizeof opts->arcade_key_p1_start, "%s", value);
        else if (eqi(name, "key_p2_credit") && value && value[0])
            snprintf(opts->arcade_key_p2_credit, sizeof opts->arcade_key_p2_credit, "%s", value);
        else if (eqi(name, "key_p2_start") && value && value[0])
            snprintf(opts->arcade_key_p2_start, sizeof opts->arcade_key_p2_start, "%s", value);
        else if (eqi(name, "key_service") && value && value[0])
            snprintf(opts->arcade_key_service, sizeof opts->arcade_key_service, "%s", value);
    } else if (eqi(section, "testing")) {
        int b;
        if (eqi(name, "no_injuries_cards") && parse_bool(value, &b))
            opts->no_injuries_cards = b;
    } else if (eqi(section, "options")) {
        static const char *const kLength[] = { "3", "5", "7", "10" };
        static const char *const kPitch[]  = { "seasonal", "random", "frozen", "muddy", "wet",
                                               "soft", "normal", "dry", "hard" };
        int b, i;
        if (eqi(name, "game_length")) {
            for (i = 0; i < 4; i++)
                if (value && eqi(value, kLength[i])) opts->opt_game_length = i;
        } else if (eqi(name, "pitch_type")) {
            for (i = 0; i < 9; i++)
                if (value && eqi(value, kPitch[i])) opts->opt_pitch_type = i - 2;
        } else if (eqi(name, "auto_replays") && parse_bool(value, &b))
            opts->opt_auto_replays = b;
        else if (eqi(name, "all_teams_equal") && parse_bool(value, &b))
            opts->opt_all_teams_equal = b;
        else if (eqi(name, "auto_save_highlights") && parse_bool(value, &b))
            opts->opt_auto_save_highlights = b;
        else if (eqi(name, "menu_music") && parse_bool(value, &b))
            opts->opt_menu_music = b;
    } else if (eqi(section, "general")) {
        if (eqi(name, "language") && value && value[0])
            snprintf(opts->language, sizeof opts->language, "%s", value);
        else if (eqi(name, "currency") && value && value[0])
            snprintf(opts->currency, sizeof opts->currency, "%s", value);
        else if (eqi(name, "assets_reminder_off"))
            snprintf(opts->assets_reminder_off, sizeof opts->assets_reminder_off, "%s", value ? value : "");
        else if (eqi(name, "world") && value && value[0])
            snprintf(opts->world, sizeof opts->world, "%s", value);
    } else if (eqi(section, "mods")) {
        if (eqi(name, "enabled"))
            snprintf(opts->mods, sizeof opts->mods, "%s", value ? value : "");
        else if (eqi(name, "disabled"))
            snprintf(opts->mods_disabled, sizeof opts->mods_disabled, "%s", value ? value : "");
    } else if (eqi(section, "tweaks")) {
        const uint32_t bit = Tweaks_Bit(name);
        int b;
        if (bit && parse_bool(value, &b))
            opts->tweaks = b ? (opts->tweaks | bit) : (opts->tweaks & ~bit);
    }
    /* Return nonzero on success, even for ignored keys — we don't want
     * inih to flag unknown sections as parse errors. */
    return 1;
}

void Settings_Load(PlatformInitOptions *opts, const char *path)
{
    if (!opts || !path) return;
    /* ini_parse returns 0 on success, line# on parse error, -1 if the
     * file can't be opened. Missing file is fine — we'll create it on
     * Settings_Save. Parse errors don't abort load; the keys before
     * the bad line still apply. */
    int rc = ini_parse(path, handler, opts);
    if (rc > 0) {
        fprintf(stderr, "[settings] %s: parse error at line %d (continuing)\n",
                path, rc);
    }
}

static const char *renderer_str(PlatformRendererMode m)
{
    return (m == PLATFORM_RENDERER_SOFTWARE) ? "software" : "accel";
}

static const char *fullscreen_str(PlatformFullscreenMode m)
{
    switch (m) {
    case PLATFORM_FULLSCREEN_DESKTOP:   return "desktop";
    case PLATFORM_FULLSCREEN_EXCLUSIVE: return "exclusive";
    case PLATFORM_FULLSCREEN_NONE:
    default:                            return "off";
    }
}

/* ===========================================================================
 * Non-destructive writer
 *
 * settings_build() is the single source of truth for the file: section, key,
 * comment and the current value (from `opts`). Settings_Save() uses it both to
 * create a fresh file and to update an existing one in place.
 * ========================================================================= */

typedef struct { const char *section, *key, *comment, *value; } IniEntry;

static const char *crt_mask_str(int m)
{
    switch (m) {
    case 1:  return "aperture";
    case 2:  return "slot";
    case 3:  return "scanlines";
    default: return "off";
    }
}

/* settings_build() fills a stack array of this size. 50 entries as of [audio] music;
 * at 49 the four [audio] keys of the PC sounds wrote one entry past the array, and the
 * next setting saved -- OPTIONS -> GAMEPLAY first of all -- crashed the macOS build,
 * which checks its stack, and corrupted it quietly everywhere else (2026-10-05). Every
 * append is bounded now (SETTING), and a full table says so. */
#define SETTINGS_MAX 96
#define SETTING(n)  if ((n) < SETTINGS_MAX) items[(n)++]

/* Inline comment appended to a section-header line in a freshly-created file. */
static const char *section_intro(const char *sec)
{
    if (eqi(sec, "gameplay")) return "         ; deliberate departures from the original Amiga game";
    if (eqi(sec, "dos"))    return "              ; PC-only extras (Amiga is the definitive version)";
    if (eqi(sec, "arcade")) return "           ; coin-op mode (PC-only). See docs/ARCADE.md";
    return "";
}

/* Fill `items` with the canonical list and return the count. `vbuf` provides
 * scratch storage for the three non-constant values (resolution, scale,
 * game_seconds) and must outlive any use of `items`. */
static int settings_build(const PlatformInitOptions *opts, IniEntry *items, char vbuf[8][24])
{
    int scale_now = Platform_GetWindowScale();
    PlatformFullscreenMode fs_now = Platform_GetFullscreenMode();
    if (scale_now < 1 || scale_now > 4) scale_now = opts->win_scale;

    if (opts->res_w > 0 && opts->res_h > 0)
        snprintf(vbuf[0], 24, "%dx%d", opts->res_w, opts->res_h);
    else
        snprintf(vbuf[0], 24, "auto");
    snprintf(vbuf[1], 24, "%d", scale_now);
    snprintf(vbuf[2], 24, "%d", opts->arcade_game_seconds > 0 ? opts->arcade_game_seconds : 45);
    snprintf(vbuf[3], 24, "%d", opts->screen > 0 ? opts->screen : 0);
    snprintf(vbuf[4], 24, "%d", opts->crt_mask_strength);
    snprintf(vbuf[5], 24, "%d", opts->crt_vignette);
    snprintf(vbuf[6], 24, "%d", opts->crt_corners);

    /* Keep every comment below INI_MAX_LINE (200, include/ini.h): the writer
     * happily emits a longer line and the READER then reports "parse error at
     * line N" on our own generated file. Cost me one round-trip to notice. */
    int n = 0;
    SETTING(n) = (IniEntry){ "video",  "renderer",   "; renderer: software | accel  (hardware-accelerated)", renderer_str(opts->renderer) };
    SETTING(n) = (IniEntry){ "video",  "vsync",      "; vsync: true | false  (accel renderer only)", opts->vsync ? "true" : "false" };
    SETTING(n) = (IniEntry){ "video",  "subpixel_scroll", "; subpixel_scroll: true | false. Smooths scrolling by shifting the frame by the camera's sub-pixel remainder. Never blurs. Off = compare", opts->subpixel_scroll ? "true" : "false" };
    SETTING(n) = (IniEntry){ "video",  "scaling",    "; scaling: sharp | smooth. A fullscreen scale is rarely a whole number (1080/272 = 3.97), so a plain nearest copy makes some pixels bigger than others. smooth evens them", opts->scaling_smooth ? "smooth" : "sharp" };
    SETTING(n) = (IniEntry){ "video",  "crt_mask",   "; crt_mask: off | aperture | slot | scanlines. Synthetic CRT shadow-mask drawn over the finished frame. Costs one full-screen draw however many effects are on", crt_mask_str(opts->crt_mask) };
    SETTING(n) = (IniEntry){ "video",  "crt_mask_strength", "; crt_mask_strength: 0-100. How dark the grille's off-phase gets. 0 = invisible, 100 = black between phosphors", vbuf[4] };
    SETTING(n) = (IniEntry){ "video",  "crt_vignette", "; crt_vignette: 0-100. Darkens towards the corners like a tube's edges. 0 = off", vbuf[5] };
    SETTING(n) = (IniEntry){ "video",  "crt_corners", "; crt_corners: 0-100. Rounded-glass corner size. 0 = square screen", vbuf[6] };
    SETTING(n) = (IniEntry){ "video",  "render_driver", "; render_driver: auto | opengl | direct3d11 | direct3d | software-backend. auto = SDL default (D3D11 on Windows). Use opengl if replays/attract run in slow motion on Windows", opts->render_driver[0] ? opts->render_driver : "auto" };
    SETTING(n) = (IniEntry){ "video",  "fullscreen", "; fullscreen: off | desktop | exclusive", fullscreen_str(fs_now) };
    SETTING(n) = (IniEntry){ "video",  "scale",      "; scale: window scale 1-4 (windowed only)", vbuf[1] };
    SETTING(n) = (IniEntry){ "video",  "widescreen",
        "; widescreen: true | false | stretch. true fills the display's shape, showing\n"
        "; more of the pitch at the sides; false draws the view's own width -- 336 the\n"
        "; Amiga way, 320 the DOS way -- with bars; stretch draws that same picture\n"
        "; pulled out to fill the display. It does NOT change the game either way: the\n"
        "; simulation sees a fixed view, so the extra columns are margin it cannot use.\n",
        opts->widescreen == 2 ? "stretch" : opts->widescreen ? "true" : "false" };

    SETTING(n) = (IniEntry){ "video",  "resolution", "; resolution: WxH (e.g. 2560x1080) drives the render ASPECT, or 'auto' for the desktop.\n; It is not the render SIZE: the picture is always 272 lines tall and 336..672 wide,\n; so a wider shape shows more pitch and a small one (320x200) just clamps to 336x272", vbuf[0] };
    SETTING(n) = (IniEntry){ "video",  "screen",     "; screen: monitor index to open on, 0 = primary. Auto-clamped if out of range; window size auto-fits that screen", vbuf[3] };
    SETTING(n) = (IniEntry){ "audio",  "mode",       "; mode: mono | stereo", opts->audio_mono ? "mono" : "stereo" };
    SETTING(n) = (IniEntry){ "audio",  "style",
        "; style: amiga | pc. A match's sounds: the Amiga's, or the PC game's sampled\n"
        "; effects, crowd and chants (sound_dos/). The menus' music is the Amiga's either\n"
        "; way. It changes nothing in a match: online, each player hears their own.\n",
        opts->sound_style ? "pc" : "amiga" };
    SETTING(n) = (IniEntry){ "audio",  "music",
        "; music: amiga | pc. The music: the Amiga's, or the PC game's -- the CD\n"
        "; release's title song in the intro, and menu.xmi on an emulated AdLib card with\n"
        "; the game's own instruments in the menus.\n",
        opts->music_style ? "pc" : "amiga" };
    SETTING(n) = (IniEntry){ "audio",  "commentary_dir",
        "; commentary_dir: a folder of your own commentary, one sub-folder per category\n"
        "; (goal, own_goal, corner, ... -- docs/DOS_SOUND.md), .wav, .mp3 or .raw files of\n"
        "; any length. Empty: sound_dos/commentary. A category without files keeps the PC\n"
        "; game's lines.\n",
        opts->commentary_dir };
    SETTING(n) = (IniEntry){ "audio",  "menu_music",
        "; menu_music: midi | mp3. With music = pc, the menus' music is the game's\n"
        "; menu.xmi played live on the emulated card (midi), or a recording of it,\n"
        "; sound_dos/menu.mp3 (mp3).\n",
        opts->menu_recording ? "mp3" : "midi" };
    SETTING(n) = (IniEntry){ "audio",  "commentary",
        "; commentary: auto | true | false. The PC game's commentary. auto (the\n"
        "; default): with style = pc only. true: always, over the Amiga's sounds too.\n",
        opts->commentary == 2 ? "auto" : opts->commentary ? "true" : "false" };
    snprintf(vbuf[7], 24, "%d", opts->stick_deadzone);
    SETTING(n) = (IniEntry){ "input", "stick_deadzone",
        "; stick_deadzone: 5-90. How far an analog stick must be pushed, in percent of\n"
        "; its travel, before it counts as a direction. Raise it if the stick drifts or\n"
        "; feels twitchy; lower it for a lighter touch. Past it, the stick gives whichever\n"
        "; of the eight directions it points nearest to. The D-pad is not affected.",
        vbuf[7] };
    SETTING(n) = (IniEntry){ "input", "latch_presses",
        "; latch_presses: true | false. The match reads the fire button once a frame.\n"
        "; true (the default): a press that was down and up again between two reads\n"
        "; still counts as pressed for one frame, so a very short tap is never lost.\n"
        "; false: only what is held at the moment of the read counts, as before.",
        opts->latch_presses ? "true" : "false" };
    SETTING(n) = (IniEntry){ "input", "touch_controls",
        "; touch_controls: auto | always | never -- a phone's on-screen stick and buttons.\n"
        "; auto (the default): hidden while a game controller is played, back on a touch;\n"
        "; always: always on screen; never: never (a handheld with controls of its own).\n"
        "; OPTIONS -> CONTROLS sets it. The desktops have none.",
        opts->touch_controls == TOUCH_CONTROLS_NEVER ? "never"
        : opts->touch_controls == TOUCH_CONTROLS_ALWAYS ? "always" : "auto" };
    SETTING(n) = (IniEntry){ "input", "key_up",
        "; key_up, key_down, key_left, key_right, key_fire: your own keys, BESIDE the\n"
        "; built-in ones (the cursor keys; either CTRL or ENTER to fire), which keep\n"
        "; working. A key's name as SDL writes it: W, A, S, D, Space, Left Shift,\n"
        "; Right Ctrl, Keypad 8 ... Empty: none. A key named here is a control and\n"
        "; nothing else: it no longer does what it did in the game (W called the bench,\n"
        "; S the statistics), except in a text field. Use these rather than an external\n"
        "; key remapper, which feeds the game key presses of its own timing.",
        opts->key_bind[0] };
    SETTING(n) = (IniEntry){ "input", "key_down",  NULL, opts->key_bind[1] };
    SETTING(n) = (IniEntry){ "input", "key_left",  NULL, opts->key_bind[2] };
    SETTING(n) = (IniEntry){ "input", "key_right", NULL, opts->key_bind[3] };
    SETTING(n) = (IniEntry){ "input", "key_fire",  NULL, opts->key_bind[4] };
    SETTING(n) = (IniEntry){ "gameplay", "fire_hold_lockout",
        "; fire_hold_lockout: true | false — true restores a BUG IN THE ORIGINAL\n"
        "; AMIGA GAME: an OPPONENT's tackle wipes your held fire button's shot charge,\n"
        "; and it cannot restart while the button is down, so holding fire through a\n"
        "; challenge never shoots until you let go and press again. Default false\n"
        "; (fixed). Your OWN tackle still spends the press either way — that half is\n"
        "; deliberate in the original. See team_control.c.",
        opts->fire_hold_lockout ? "true" : "false" };
    SETTING(n) = (IniEntry){ "gameplay", "contest_own_ratings",
        "; contest_own_ratings: true | false. When two players contest the ball the game\n"
        "; rolls a dice weighted by tackling and ball control. The ORIGINAL rates one of the\n"
        "; two sides by a team-mate (the player with the opponent's line-up number), in both\n"
        "; the Amiga and PC releases. true rates each side by the man actually at the ball.\n"
        "; Default false: the original. OFFLINE matches only -- an online match always\n"
        "; plays the original's. See docs/BALL_CONTEST.md.",
        opts->contest_own_ratings ? "true" : "false" };


    SETTING(n) = (IniEntry){ "gameplay", "style",
        "; style: amiga | dos | xbox. The Amiga release is the definitive one and what\n"
        "; this port is reverse engineered from; dos is the PC release's 70 FPS physics,\n"
        "; timers and formations, and its 320x200 view -- the view is part of how it\n"
        "; plays (what is on screen decides which players the game picks), so the\n"
        "; style sets both; xbox is SWOS 2020's XBOX style: 60 FPS, the Amiga's ball\n"
        "; physics and view, the DOS goalkeepers. Online, the game plays the HOST's style.\n",
        opts->dos_gameplay == GAMEPLAY_DOS ? "dos" : opts->dos_gameplay == GAMEPLAY_XBOX ? "xbox" : "amiga" };
    SETTING(n) = (IniEntry){ "replays", "auto_save",
        "; auto_save: true | false — every match is recorded to LATEST.SRP in the save\n"
        "; folder and can be watched from HIGHLIGHTS -> LOAD. true also keeps a dated\n"
        "; copy of each (MMDDHHMM.SRP), so a match is not overwritten by the next one.",
        opts->replay_auto_save ? "true" : "false" };
    SETTING(n) = (IniEntry){ "netplay", "matchmaker",
        "; matchmaker: the server MULTIPLAYER logs in to, as \"host\" or \"host:port\"\n"
        "; (default port 19602). Empty means the public server, " PROJECT_MM_HOST ". You can type it\n"
        "; on the login screen instead -- it is saved back here.",
        opts->matchmaker };
    SETTING(n) = (IniEntry){ "netplay", "name",
        "; name: your name on the MULTIPLAYER login screen. Typed there and saved back\n"
        "; here while REMEMBER LOGIN INFO is on.",
        opts->netplay_name };
    SETTING(n) = (IniEntry){ "netplay", "remember_login",
        "; remember_login: true | false -- the login screen's REMEMBER LOGIN INFO switch.",
        opts->netplay_remember ? "true" : "false" };
    SETTING(n) = (IniEntry){ "netplay", "record",
        "; record: stream every live match to a recorder, which keeps it and compares the\n"
        "; two players' frames to catch a desync: \"server\" (the default: the matchmaker\n"
        "; above, for the matches it arranges), a recorder of your own as \"host\" or\n"
        "; \"host:port\" (every match), or \"off\". Empty means the default.",
        opts->record };
    SETTING(n) = (IniEntry){ "netplay", "live_cast",
        "; live_cast: true | false -- let LIVE CAST's viewers watch your online matches on the\n"
        "; recorder (false keeps them off its list; recording for desync checks goes on).",
        opts->live_cast ? "true" : "false" };
    SETTING(n) = (IniEntry){ "netplay", "reports",
        "; reports: true | false -- after an online match, send both players' SWOS.log and\n"
        "; HOW WAS THE MATCH? to the other player and the server, zipped (a copy is kept in\n"
        "; save/netplay either way). false: nothing leaves this machine.",
        opts->reports ? "true" : "false" };
    SETTING(n) = (IniEntry){ "netplay", "prediction",
        "; prediction: true | false -- keep what each player SAW of an online match in its\n"
        "; replay: how many of the other player's inputs were guesses on each frame, the\n"
        "; rollbacks, and where players were drawn against where they really were. The\n"
        "; playback page shows it (docs/PLAYBACK.md). A replay plays the same without it.",
        opts->prediction ? "true" : "false" };


    SETTING(n) = (IniEntry){ "graphics", "style",
        "; style: amiga | dos | 2020. Which artwork the pitch, HUD and menus are drawn\n"
        "; from. Gameplay is unaffected -- this is only what it looks like.\n",
        !opts->dos_graphics ? "amiga" :
        (opts->dos_dir[0] && !strcmp(opts->dos_dir, "grafs_2020")) ? "2020" : "dos" };

    SETTING(n) = (IniEntry){ "graphics", "sprites",
        "; sprites: auto | amiga | jaguar. The Atari Jaguar redrew the players at 256\n"
        "; colours -- same animations, more shades (docs/JAGUAR.md). auto is the\n"
        "; built-in default, amiga.\n",
        opts->jag_graphics ? "jaguar" : "amiga" };


    SETTING(n) = (IniEntry){ "graphics", "pitch_maps",
        "; pitch_maps: true | false. With style = dos, draw a pitch from its Tile Agent\n"
        "; map (grafs_dos/pitch<N>.tmj) where there is one, and from the game's own files\n"
        "; where there is not (docs/PITCH_MAPS.md). Only the picture: the match itself,\n"
        "; online and in replays, is the same either way.\n",
        opts->pitch_maps ? "true" : "false" };

    SETTING(n) = (IniEntry){ "graphics", "outfit_shading",
        "; outfit_shading: true | false. Shade the kit into four tones per colour, the\n"
        "; way the Jaguar art does. false draws each kit colour flat, exactly as the\n"
        "; 16-colour Amiga game does.\n",
        opts->outfit_shading ? "true" : "false" };

    SETTING(n) = (IniEntry){ "graphics", "shadow_shading",
        "; shadow_shading: true | false. Shade the drop shadow, tinted from the grass of\n"
        "; the pitch being played on. false uses SWOS's own colour 8 -- one near-black\n"
        "; that reads on any pitch, and costs the palette nothing.\n",
        opts->shadow_shading ? "true" : "false" };

    SETTING(n) = (IniEntry){ "graphics", "hair_shading",
        "; hair_shading: true | false. Shade hair the game draws in a single colour (a\n"
        "; black head), by shade_step like the kit. false draws it flat, exactly as the\n"
        "; 16-colour Amiga game does. Hair the game shades itself (ginger, blond) is\n"
        "; the game's own either way.\n",
        opts->hair_shading ? "true" : "false" };

    {
        static char stepBuf[8];
        snprintf(stepBuf, sizeof stepBuf, "%d", opts->shade_step);
        SETTING(n) = (IniEntry){ "graphics", "shade_step",
            "; shade_step: 1..5. How far apart the four kit shades are, in sixteenths:\n"
            "; 16, 16-N, 16-2N, 16-3N. The Jaguar's own artwork used about 3, which takes\n"
            "; a near-white sock down to 37.5% and reads as dark grey; 2 keeps it white.\n"
            "; Hair shading uses it too: black hair's highlight is lifted by 3 x N.\n",
            stepBuf };
    }
    SETTING(n) = (IniEntry){ "graphics", "kit_art",
        "; kit_art: amiga | jaguar. The shirts, shorts and socks drawn on the UNIFORM\n"
        "; screen: the Amiga's flat drawings, or the Atari Jaguar's shaded ones (shaded by\n"
        "; shade_step, like its sprites). That screen only: it changes nothing in a match,\n"
        "; and does not follow the sprites setting.\n",
        opts->kit_art ? "jaguar" : "amiga" };
    SETTING(n) = (IniEntry){ "arcade", "enabled",    "; enabled: true | false — boot straight to attract mode, no main menu", opts->arcade ? "true" : "false" };
    SETTING(n) = (IniEntry){ "arcade", "mode",       "; mode: arcade game mode (F2 service menu). Currently: GOLDEN GOAL", Arcade_ModeName(opts->arcade_mode) };
    SETTING(n) = (IniEntry){ "arcade", "game_seconds", "; game rules (F2 service menu). game_seconds 10..600;  golden_goal/penalties_on_draw/goal_replay: true | false", vbuf[2] };
    SETTING(n) = (IniEntry){ "arcade", "golden_goal", NULL, opts->arcade_golden_goal ? "true" : "false" };
    SETTING(n) = (IniEntry){ "arcade", "penalties_on_draw", NULL, opts->arcade_penalties ? "true" : "false" };
    SETTING(n) = (IniEntry){ "arcade", "goal_replay", NULL, opts->arcade_goal_replay ? "true" : "false" };
    SETTING(n) = (IniEntry){ "arcade", "attract_audio",
        "; attract_audio: music (HERO only) | sfx (reel crowd only) | music+sfx (both, default)",
        opts->arcade_attract_audio == 2 ? "music+sfx" : opts->arcade_attract_audio == 1 ? "sfx" : "music" };
    SETTING(n) = (IniEntry){ "arcade", "gameplay",   "; gameplay: amiga (50fps feel) | dos (70fps). Picks attract_mode/<mode> reels too", opts->arcade_gameplay ? "dos" : "amiga" };
    SETTING(n) = (IniEntry){ "arcade", "key_p1_credit", "; cabinet key bindings (SDL key names; MAME layout 5/6 = coin P1/P2, 1/2 = start, F2 = service)", opts->arcade_key_p1_credit[0] ? opts->arcade_key_p1_credit : "5" };
    SETTING(n) = (IniEntry){ "arcade", "key_p1_start", NULL, opts->arcade_key_p1_start[0]  ? opts->arcade_key_p1_start  : "1" };
    SETTING(n) = (IniEntry){ "arcade", "key_p2_credit", NULL, opts->arcade_key_p2_credit[0] ? opts->arcade_key_p2_credit : "6" };
    SETTING(n) = (IniEntry){ "arcade", "key_p2_start", NULL, opts->arcade_key_p2_start[0]  ? opts->arcade_key_p2_start  : "2" };
    SETTING(n) = (IniEntry){ "arcade", "key_service", NULL, opts->arcade_key_service[0]   ? opts->arcade_key_service   : "F2" };
#ifdef SWOS_TEST_RULES
    SETTING(n) = (IniEntry){ "testing", "no_injuries_cards",
        "; no_injuries_cards: true | false -- FOR TESTING A CAREER: nobody is booked, sent off,\n"
        "; injured or suspended, so VIEW RESULT runs through a season without stopping.\n"
        "; Offline only: an online match always plays the real rules.",
        opts->no_injuries_cards ? "true" : "false" };
#endif
    SETTING(n) = (IniEntry){ "general","language",   "; language: en | de | fr | es | it | nl", opts->language[0] ? opts->language : "en" };
    SETTING(n) = (IniEntry){ "general","currency",   "; currency: pound | dollar | euro", opts->currency[0] ? opts->currency : "pound" };
    SETTING(n) = (IniEntry){ "general","assets_reminder_off",
        "; assets_reminder_off: set by \"Don't remind me\" on the reminder that your game data was made by\n"
        "; an older " PROJECT_SETUP_NAME " -- the data versions it was about. Empty: remind (src/assets_check.c).",
        opts->assets_reminder_off };
    SETTING(n) = (IniEntry){ "general","world",
        "; world: 9697 | 2627 -- the football world: its countries, clubs and competitions\n"
        "; (docs/WORLD_2026_27.md): SWOS 96/97, or World 2026-27 once " PROJECT_SETUP_NAME " has\n"
        "; imported its data. A save is played in the world it was made in.",
        opts->world[0] ? opts->world : "9697" };
    SETTING(n) = (IniEntry){ "mods",   "enabled",
        "; enabled: the mods to play with, by their folder's name in the mods folder beside this file,\n"
        "; in that order: a file two of them have comes from the first, e.g. enabled = classics, hall.\n"
        "; Empty: none (docs/MODS.md). A downloaded (DLC) mod that is not cosmetic keeps the game off line.",
        opts->mods };
    SETTING(n) = (IniEntry){ "mods",   "disabled",
        "; disabled: the game's own (canonical) mods to turn off, by name; they are on by default.",
        opts->mods_disabled };
    SETTING(n) = (IniEntry){ "tweaks", "cpu_cards",
        "; [tweaks]: small changes to the original's behaviour, each off by default and only off line\n"
        "; (online, and in the test rigs, the original plays): docs/MODS.md.\n"
        "; cpu_cards: true | false -- the computer's players are booked and sent off too\n"
        "; (the original cards only a side a human controls).",
        (opts->tweaks & TWEAK_CPU_CARDS) ? "true" : "false" };
    SETTING(n) = (IniEntry){ "tweaks", "no_teammate_tackles",
        "; no_teammate_tackles: true | false -- a team-mate leaves your player on the ball alone\n"
        "; (in the original the nearest team-mate runs onto the ball and can slide or take it off you).",
        (opts->tweaks & TWEAK_NO_TEAMMATE_TACKLES) ? "true" : "false" };
    SETTING(n) = (IniEntry){ "tweaks", "tackle_controlled_player",
        "; tackle_controlled_player: true | false -- fire slides the player you control\n"
        "; (in the original a nearer team-mate may slide instead).",
        (opts->tweaks & TWEAK_TACKLE_CONTROLLED_PLAYER) ? "true" : "false" };
    SETTING(n) = (IniEntry){ "tweaks", "more_fouls",
        "; more_fouls: true | false -- one borderline tackle in 16, from the side, is a foul\n"
        "; (the original gives a foul only from behind; benefactorr's Career+).",
        (opts->tweaks & TWEAK_MORE_FOULS) ? "true" : "false" };
    SETTING(n) = (IniEntry){ "tweaks", "ai_injury_subs",
        "; ai_injury_subs: true | false -- the computer replaces an injured player at a stoppage\n"
        "; (the original's computer never substitutes; benefactorr's Career+).",
        (opts->tweaks & TWEAK_AI_INJURY_SUBS) ? "true" : "false" };
    {   /* [options]: unset = the game's defaults (SetDefaultOptions) */
        static const char *const kLength[] = { "3", "5", "7", "10" };
        static const char *const kPitch[]  = { "seasonal", "random", "frozen", "muddy", "wet",
                                               "soft", "normal", "dry", "hard" };
        int len = opts->opt_game_length, pitch = opts->opt_pitch_type;
#define OPT_BOOL(v, def) ((v) == OPTION_UNSET ? (def) : (v)) ? "true" : "false"
        SETTING(n) = (IniEntry){ "options", "game_length",
            "; The OPTIONS screen, as it was left (a loaded career plays its own).\n"
            "; game_length: 3 | 5 | 7 | 10 (minutes)",
            kLength[len >= 0 && len < 4 ? len : 0] };
        SETTING(n) = (IniEntry){ "options", "pitch_type",
            "; pitch_type: seasonal | random | frozen | muddy | wet | soft | normal | dry | hard",
            kPitch[pitch >= -2 && pitch <= 6 ? pitch + 2 : 0] };
        SETTING(n) = (IniEntry){ "options", "auto_replays", "; auto_replays, all_teams_equal, auto_save_highlights, menu_music: true | false",
            OPT_BOOL(opts->opt_auto_replays, 1) };
        SETTING(n) = (IniEntry){ "options", "all_teams_equal", NULL, OPT_BOOL(opts->opt_all_teams_equal, 0) };
        SETTING(n) = (IniEntry){ "options", "auto_save_highlights", NULL, OPT_BOOL(opts->opt_auto_save_highlights, 1) };
        SETTING(n) = (IniEntry){ "options", "menu_music", NULL, OPT_BOOL(opts->opt_menu_music, 1) };
#undef OPT_BOOL
    }
    if (n >= SETTINGS_MAX)
        fprintf(stderr, "[settings] the settings table is full (%d): raise SETTINGS_MAX\n", n);
    return n;
}

/* Create a full default file. Only used when SWOS.ini doesn't exist yet. */
static void settings_write_full(const PlatformInitOptions *opts, const char *path)
{
    IniEntry items[SETTINGS_MAX];
    char vbuf[8][24];
    int n = settings_build(opts, items, vbuf);

    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "[settings] cannot create %s: %s\n", path, strerror(errno));
        return;
    }
    fprintf(f,
        "; SWOS — display, audio and gameplay settings.\n"
        "; Hand-edit freely: SWOS only updates values you change and adds missing\n"
        "; keys; your comments, blank lines and commented-out keys are preserved.\n"
        "; Booleans accept true/false, on/off, yes/no, 1/0 (case-insensitive).\n");
    const char *cur = NULL;
    for (int i = 0; i < n; i++) {
        if (!cur || !eqi(cur, items[i].section)) {
            cur = items[i].section;
            fprintf(f, "\n[%s]%s\n", cur, section_intro(cur));
        }
        if (items[i].comment) fprintf(f, "%s\n", items[i].comment);
        fprintf(f, "%s=%s\n", items[i].key, items[i].value);
    }
    fclose(f);
    swos_save_commit();   /* as Settings_Save does -- a fresh ini must survive the launch */
}

/* Trim a value string (after '=') into `out`: skip leading blanks, stop at
 * end-of-line, drop trailing blanks. */
static void copy_value(const char *v, char *out, size_t outsz)
{
    while (*v == ' ' || *v == '\t') v++;
    size_t len = 0;
    while (v[len] && v[len] != '\n' && v[len] != '\r' && len + 1 < outsz) len++;
    while (len > 0 && (v[len-1] == ' ' || v[len-1] == '\t')) len--;
    memcpy(out, v, len);
    out[len] = '\0';
}

/* Write every still-missing key that belongs to section `sec` (comment + value),
 * marking each seen. Called when Pass 2 leaves a section block, so a missing key
 * is inserted into the FIRST existing occurrence of its section rather than in a
 * brand-new duplicate section at the end of the file. */
static void flush_section_missing(FILE *out, const char *sec,
                                  const IniEntry *items, int n, unsigned char *seen)
{
    for (int k = 0; k < n; k++) {
        if (seen[k] || !eqi(items[k].section, sec)) continue;
        if (items[k].comment) fprintf(out, "%s\n", items[k].comment);
        fprintf(out, "%s=%s\n", items[k].key, items[k].value);
        seen[k] = 1;
    }
}

/*
 * Non-destructive save. If `path` is absent, write a full default file.
 * Otherwise rewrite it line-by-line: update ONLY the values of known keys whose
 * value changed, drop duplicate occurrences of a known key, add each missing key
 * to the end of the FIRST block of its section (a new section only if the file
 * has none), and preserve everything else (comments, blank lines, commented-out
 * keys, unknown keys, ordering) verbatim. If nothing changed, nothing is missing
 * and nothing is duplicated, the file is left untouched.
 */
void Settings_Save(const PlatformInitOptions *opts, const char *path)
{
    if (!opts || !path) return;

    IniEntry items[SETTINGS_MAX];
    char vbuf[8][24];
    int n = settings_build(opts, items, vbuf);

    /* Slurp the existing file. */
    static char lines[400][256];
    int n_lines = 0;
    FILE *f = fopen(path, "r");
    if (!f) { settings_write_full(opts, path); return; }   /* absent -> create it */
    while (n_lines < 400 && fgets(lines[n_lines], (int)sizeof lines[0], f))
        n_lines++;
    fclose(f);

    /* Pass 1: detect whether anything actually needs writing, and which keys
     * the file already has. This keeps an unchanged file (the common case)
     * byte-for-byte identical — not even the mtime moves. */
    unsigned char seen[SETTINGS_MAX];
    memset(seen, 0, sizeof seen);
    int dirty = 0;
    {
        char cur_sec[32] = "";
        for (int i = 0; i < n_lines; i++) {
            const char *t = lines[i];
            while (*t == ' ' || *t == '\t') t++;
            if (*t == '[') {
                const char *e = strchr(t, ']');
                if (e) {
                    int len = (int)(e - (t + 1));
                    if (len < 0) len = 0; else if (len > 31) len = 31;
                    memcpy(cur_sec, t + 1, (size_t)len); cur_sec[len] = '\0';
                }
                continue;
            }
            const char *eq;
            if (*t == ';' || *t == '#' || *t == '\0' || *t == '\n' || *t == '\r'
                || (eq = strchr(t, '=')) == NULL)
                continue;
            int klen = (int)(eq - t);
            while (klen > 0 && (t[klen-1] == ' ' || t[klen-1] == '\t')) klen--;
            char key[48];
            if (klen > 47) klen = 47;
            memcpy(key, t, (size_t)klen); key[klen] = '\0';
            for (int k = 0; k < n; k++) {
                if (eqi(cur_sec, items[k].section) && eqi(key, items[k].key)) {
                    if (seen[k]) { dirty = 1; break; }  /* duplicate known key -> clean it */
                    seen[k] = 1;
                    char fileval[300];
                    copy_value(eq + 1, fileval, sizeof fileval);
                    if (strcmp(fileval, items[k].value) != 0) dirty = 1;
                    break;
                }
            }
        }
        for (int k = 0; k < n; k++) if (!seen[k]) { dirty = 1; break; }
    }
    if (!dirty) return;   /* nothing to change — leave the file exactly as it is */

    /* Pass 2: rewrite, updating changed values and preserving everything else. */
    memset(seen, 0, sizeof seen);
    FILE *out = fopen(path, "w");
    if (!out) {
        fprintf(stderr, "[settings] cannot write %s: %s\n", path, strerror(errno));
        return;
    }
    char cur_sec[32] = "";
    for (int i = 0; i < n_lines; i++) {
        char *ln = lines[i];
        const char *t = ln;
        while (*t == ' ' || *t == '\t') t++;

        if (*t == '[') {                                   /* section header */
            /* Leaving cur_sec: drop any of its still-missing keys in HERE, at the
             * end of this (the first) block of the section — not in a duplicate
             * section appended to the file's tail. */
            if (cur_sec[0]) flush_section_missing(out, cur_sec, items, n, seen);
            const char *e = strchr(t, ']');
            if (e) {
                int len = (int)(e - (t + 1));
                if (len < 0) len = 0; else if (len > 31) len = 31;
                memcpy(cur_sec, t + 1, (size_t)len); cur_sec[len] = '\0';
            }
            fputs(ln, out);
            continue;
        }
        const char *eq;
        if (*t == ';' || *t == '#' || *t == '\0' || *t == '\n' || *t == '\r'
            || (eq = strchr(t, '=')) == NULL) {
            fputs(ln, out);                                /* comment / blank / no '=' */
            continue;
        }
        int klen = (int)(eq - t);
        while (klen > 0 && (t[klen-1] == ' ' || t[klen-1] == '\t')) klen--;
        char key[48];
        if (klen > 47) klen = 47;
        memcpy(key, t, (size_t)klen); key[klen] = '\0';

        int idx = -1;
        for (int k = 0; k < n; k++)
            if (eqi(cur_sec, items[k].section) && eqi(key, items[k].key)) { idx = k; break; }
        if (idx < 0) { fputs(ln, out); continue; }         /* unknown key -> verbatim */

        if (seen[idx]) continue;    /* duplicate of a known key already written -> drop it */

        seen[idx] = 1;
        char fileval[300];
        copy_value(eq + 1, fileval, sizeof fileval);
        if (strcmp(fileval, items[idx].value) == 0) {
            fputs(ln, out);                                /* unchanged -> verbatim */
        } else {
            size_t prefix = (size_t)(eq - ln) + 1;         /* keep "key=" verbatim */
            fwrite(ln, 1, prefix, out);
            fprintf(out, "%s\n", items[idx].value);        /* ...replace the value */
        }
    }
    /* EOF: flush the final section's still-missing keys into its block. */
    if (cur_sec[0]) flush_section_missing(out, cur_sec, items, n, seen);

    /* Append keys whose section never appeared in the file at all (a genuinely
     * new section), with a fresh header. Keys for sections already present were
     * injected in-place above, so this never duplicates an existing section. */
    int missing = 0;
    for (int k = 0; k < n; k++) if (!seen[k]) missing++;
    if (missing) {
        fputs("\n; --- keys added by a newer SWOS build (defaults; edit as you like) ---\n", out);
        const char *cur = NULL;
        for (int k = 0; k < n; k++) {
            if (seen[k]) continue;
            if (!cur || !eqi(cur, items[k].section)) {
                cur = items[k].section;
                fprintf(out, "[%s]\n", cur);
            }
            if (items[k].comment) fprintf(out, "%s\n", items[k].comment);
            fprintf(out, "%s=%s\n", items[k].key, items[k].value);
        }
    }
    fclose(out);
    /* Closed first — the Switch's save commit requires it, and without the
     * commit SWOS.ini reads back fine all session and is gone next launch.
     * Nothing on any other target (swos_io.h). */
    swos_save_commit();
}

/* Back-compat alias for the startup/shutdown path: now identical to the
 * non-destructive Settings_Save. */
void Settings_AppendMissing(const PlatformInitOptions *opts, const char *path)
{
    Settings_Save(opts, path);
}

/* --game-options-selftest: every value OpenSWOS Setup may write (include/game_options.h) read
 * by this file's reader and written back by its writer, unchanged. Fails when the game renames
 * a key or a value the setup still writes. 0 when every one comes back. */
#include "game_options.h"
int Settings_GameOptionsSelfTest(void)
{
    char in[512], out[512], line[512];
    const char *tmp = getenv("TMPDIR");
    int32_t i, k, bad = 0, checked = 0;

    snprintf(in, sizeof in, "%s/swos-game-options-in.ini", tmp && *tmp ? tmp : "/tmp");
    snprintf(out, sizeof out, "%s/swos-game-options-out.ini", tmp && *tmp ? tmp : "/tmp");
    for (i = 0; i < GAME_OPTION_COUNT(kGameOptions); i++) {
        const GameOption *o = &kGameOptions[i];
        for (k = 0; k < o->count; k++) {
            PlatformInitOptions opts;
            FILE *f = fopen(in, "w");
            char got[64] = "";
            int32_t inSection = 0;
            if (!f) { fprintf(stderr, "game-options: cannot write %s\n", in); return 1; }
            fprintf(f, "[%s]\n%s=%s\n", o->section, o->key, o->values[k].value);
            fclose(f);
            memset(&opts, 0, sizeof opts);
            Settings_Load(&opts, in);
            if (!strcmp(o->section, "video") && !strcmp(o->key, "fullscreen")) {
                /* (written from the window's own state, not the setting: read it as loaded) */
                snprintf(got, sizeof got, "%s", opts.fullscreen == PLATFORM_FULLSCREEN_DESKTOP ? "desktop"
                                                : opts.fullscreen == PLATFORM_FULLSCREEN_EXCLUSIVE ? "exclusive" : "off");
            } else {
                Settings_Save(&opts, out);
                if (!(f = fopen(out, "r"))) { fprintf(stderr, "game-options: cannot read %s\n", out); return 1; }
                while (fgets(line, sizeof line, f)) {
                    size_t n = strlen(o->key);
                    if (line[0] == '[')
                        inSection = !strncmp(line + 1, o->section, strlen(o->section)) && line[1 + strlen(o->section)] == ']';
                    else if (inSection && !strncmp(line, o->key, n) && line[n] == '=') {
                        sscanf(line + n + 1, "%63[^ \t\r\n;]", got);
                        break;
                    }
                }
                fclose(f);
            }
            checked++;
            if (strcmp(got, o->values[k].value)) {
                fprintf(stderr, "game-options: [%s] %s=%s came back as '%s'\n", o->section, o->key,
                        o->values[k].value, got);
                bad++;
            }
        }
    }
    remove(in);
    remove(out);
    printf("game-options self-test: %d of %d values come back%s\n", (int)(checked - bad), (int)checked,
           bad ? " -- FAILED" : "");
    return bad ? 1 : 0;
}
