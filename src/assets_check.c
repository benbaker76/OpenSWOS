/*
 * assets_check.c -- at start-up, tell the player when their game data was made by an
 * older OpenSWOS Setup (SDL builds).
 *
 * OpenSWOS Setup writes ASSETS.ini in the assets folder (include/assets_ini.h): the data
 * version of every part it made. A part's version goes up only when the setup makes it
 * differently, so the game asks the player to run the setup again only then. Without
 * ASSETS.ini, an assets folder with data in it was made by a setup from before the file
 * existed: that counts as out of date too. No assets folder at all (a developer's bin/
 * holding the data) is never a reminder.
 *
 * The reminder is a dialog of the game's own (MenuDialog_Show, include/menu_dialog.h), shown
 * by the front end before the main menu (AssetsCheck_ShowNotice): LAUNCH SETUP starts
 * OpenSWOS Setup -- when it is beside the game -- and closes the game, so the setup can replace
 * the data; CONTINUE plays on; DON'T REMIND ME keeps the data versions it was about in SWOS.ini
 * ([general] assets_reminder_off): the same reminder is not shown again, a later setup that
 * changes a part again is. The world's package (AssetsCheck_World) is said on the same screen.
 * Nothing is shown headless (the test rigs), in the arcade cabinet, in the web build (its data
 * is built in) or on the Switch. Until 2026-11-08 both were the OS's message boxes.
 */
#include <stdint.h>
#include "project_identity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <SDL.h>

#include "assets_check.h"
#include "assets_ini.h"
#include "swos_io.h"            /* swos_assets_dir */
#include "swos_settings.h"      /* Settings_Save */
#include "menu_dialog.h"        /* MenuDialog_Show */
#include <spawn.h>
#include <unistd.h>

extern char **environ;

/* ---- the notice: what the front end says before the main menu ---- */

enum { NOTICE_NONE, NOTICE_ASSETS, NOTICE_WORLD };
static int32_t s_notice;
static char    s_noticeTitle[48], s_noticeText[640], s_noticeSig[64];
static PlatformInitOptions *s_opts;
static const char *s_iniPath;
static int32_t s_quitForSetup;

#define AC_PART_KEY(name, key, ver) key,
#define AC_PART_VER(name, key, ver) ver,
static const char *const kPartKey[]     = { ASSETS_PARTS(AC_PART_KEY) };
static const int32_t     kPartVersion[] = { ASSETS_PARTS(AC_PART_VER) };
#undef AC_PART_KEY
#undef AC_PART_VER
enum { PART_COUNT = (int32_t)(sizeof kPartVersion / sizeof kPartVersion[0]) };

static int32_t is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
}

static char *trim(char *t)
{
    while (*t == ' ' || *t == '\t') t++;
    char *e = t + strlen(t);
    while (e > t && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = '\0';
    return t;
}

/* The parts ASSETS.ini lists and their versions (0: not listed). 0 when it was read. */
static int32_t read_versions(const char *assetsDir, int32_t version[PART_COUNT])
{
    char path[1024], line[1200], section[32] = "";
    FILE *f;
    memset(version, 0, sizeof(int32_t) * PART_COUNT);
    snprintf(path, sizeof path, "%s/%s", assetsDir, ASSETS_INI_NAME);
    if ((f = fopen(path, "r")) == NULL)
        return -1;
    while (fgets(line, sizeof line, f)) {
        char *t = trim(line), *eq;
        if (*t == '[') {
            char *e = strchr(t, ']');
            if (e) { *e = '\0'; snprintf(section, sizeof section, "%s", t + 1); }
        } else if (strcmp(section, "data") == 0 && (eq = strchr(t, '=')) != NULL) {
            *eq = '\0';
            const char *key = trim(t);
            for (int32_t i = 0; i < PART_COUNT; i++)
                if (strcmp(key, kPartKey[i]) == 0)
                    version[i] = (int32_t)atoi(trim(eq + 1));
        }
    }
    fclose(f);
    return 0;
}

/* Which parts are out of date, for the log: "dos_files 1<2, aga_pc 0<1". "" when none. */
static void outdated_parts(const char *assetsDir, char *out, uint32_t cap)
{
    int32_t version[PART_COUNT];
    out[0] = '\0';
    if (read_versions(assetsDir, version) != 0) {
        /* No ASSETS.ini: made before the setup wrote one -- if anything is there. */
        static const char *const kTrees[] = { "data", "grafs", "grafs_aga", "grafs_dos", "sound" };
        char path[1024];
        for (uint32_t i = 0; i < sizeof kTrees / sizeof kTrees[0]; i++) {
            snprintf(path, sizeof path, "%s/%s", assetsDir, kTrees[i]);
            if (is_dir(path)) {
                snprintf(out, cap, "no %s (an older " PROJECT_SETUP_NAME ")", ASSETS_INI_NAME);
                return;
            }
        }
        return;
    }
    for (int32_t i = 0; i < PART_COUNT; i++) {
        if (version[i] <= 0 || version[i] >= kPartVersion[i])
            continue;               /* not made, or up to date */
        uint32_t n = (uint32_t)strlen(out);
        snprintf(out + n, cap - n, "%s%s %d<%d", n ? ", " : "", kPartKey[i],
                 (int)version[i], (int)kPartVersion[i]);
    }
}

/* This build's data versions, the "Don't remind me" key: "1,1,2,1,1,1,1". */
static void version_signature(char *out, uint32_t cap)
{
    out[0] = '\0';
    for (int32_t i = 0; i < PART_COUNT; i++) {
        uint32_t n = (uint32_t)strlen(out);
        snprintf(out + n, cap - n, "%s%d", i ? "," : "", (int)kPartVersion[i]);
    }
}

void AssetsCheck_Run(PlatformInitOptions *opts, const char *iniPath)
{
#if defined(SWOS_WASM_BUILD) || defined(SWOS_SWITCH_BUILD)
    (void)opts; (void)iniPath;
#else
    const char *driver = SDL_GetCurrentVideoDriver();
    const char *dir = swos_assets_dir();
    char why[256], sig[64];

    if (!dir || !dir[0] || !is_dir(dir))
        return;                     /* no assets folder: the data is beside the game */
    outdated_parts(dir, why, sizeof why);
    if (!why[0])
        return;
    fprintf(stderr, "[assets] game data made by an older " PROJECT_SETUP_NAME ": %s\n", why);
    version_signature(sig, sizeof sig);
    if (getenv("SWOS_NO_ASSETS_CHECK") || opts->arcade ||
        !driver || strcmp(driver, "dummy") == 0 || strcmp(driver, "offscreen") == 0)
        return;
    if (strcmp(opts->assets_reminder_off, sig) == 0)
        return;                     /* "Don't remind me" for these very versions */

    s_notice = NOTICE_ASSETS;
    s_opts = opts;
    s_iniPath = iniPath;
    snprintf(s_noticeSig, sizeof s_noticeSig, "%s", sig);
    snprintf(s_noticeTitle, sizeof s_noticeTitle, "Update your game data");
    /* (the middle paragraph is the screen's: it depends on whether the setup is beside the game) */
#endif
}

/* ---- the world's data package (include/world.h; OpenSWOS Setup installs it) ---- */

static int32_t s_worldState;
static char    s_worldTag[16], s_worldHave[40], s_worldWant[40];

void AssetsCheck_SetWorld(int32_t state, const char *tag, const char *have, const char *want)
{
    s_worldState = state;
    snprintf(s_worldTag, sizeof s_worldTag, "%s", tag ? tag : "");
    snprintf(s_worldHave, sizeof s_worldHave, "%s", have && have[0] ? have : "unknown");
    snprintf(s_worldWant, sizeof s_worldWant, "%s", want ? want : "");
}

void AssetsCheck_World(PlatformInitOptions *opts)
{
#if defined(SWOS_WASM_BUILD) || defined(SWOS_SWITCH_BUILD)
    (void)opts;
#else
    const char *driver = SDL_GetCurrentVideoDriver();
    char text[640];
    if (!s_worldState || getenv("SWOS_NO_ASSETS_CHECK") || opts->arcade ||
        !driver || strcmp(driver, "dummy") == 0 || strcmp(driver, "offscreen") == 0)
        return;
    /* (World 2026-27 is the only world besides 96/97: its name, its package's) */
    if (s_worldState == 1)
        snprintf(text, sizeof text,
                 "SWOS.ini asks for World 2026-27 (world = %s), but its data is not installed.|"
                 "Download the World 2026-27 package, then add it in %s and click Install.|"
                 "SWOS 96-97 is played meanwhile.", s_worldTag, PROJECT_SETUP_NAME);
    else
        snprintf(text, sizeof text,
                 "Your World 2026-27 data is not the one this version was made for: it is %s, this "
                 "version needs %s.|Download the World 2026-27 package for this version, then add it "
                 "in %s and click Install.|Until then a World 2026-27 career may not play correctly: "
                 "leagues and clubs can be missing or wrong.", s_worldHave, s_worldWant, PROJECT_SETUP_NAME);
    fprintf(stderr, "[world] notice: %s\n", s_worldState == 1 ? "not installed" : "other data");
    if (s_notice == NOTICE_ASSETS)
        return;                         /* (one screen at a start: the data first) */
    s_notice = NOTICE_WORLD;
    s_opts = opts;
    snprintf(s_noticeTitle, sizeof s_noticeTitle,
             s_worldState == 1 ? "World 2026-27 is not installed" : "Update World 2026-27");
    snprintf(s_noticeText, sizeof s_noticeText, "%s", text);
#endif
}

/* ---- the notice, shown (MenuDialog_Show: include/menu_dialog.h) ---- */
#if !defined(SWOS_WASM_BUILD) && !defined(SWOS_SWITCH_BUILD)

/* OpenSWOS Setup beside the game: openswos-setup(.exe) in the game's folder; on a Mac the
 * setup's app beside SWOS.app (the drop's OpenSWOS/ folder) or beside a bare binary. Never on
 * a phone (its data comes from a PC's). 1 with its path. */
int32_t AssetsCheck_FindSetup(char *out, uint32_t cap)
{
#if defined(SWOS_MOBILE_BUILD)
    (void)out; (void)cap;
    return 0;
#else
    char *base = SDL_GetBasePath();
    struct stat st;
    if (!base)
        return 0;
#if defined(__APPLE__)
    snprintf(out, cap, "%s../../../%s.app", base, PROJECT_SETUP_NAME);     /* (base: SWOS.app/Contents/Resources/) */
    if (stat(out, &st) != 0)
        snprintf(out, cap, "%s%s.app", base, PROJECT_SETUP_NAME);
#elif defined(__CYGWIN__) || defined(_WIN32)
    snprintf(out, cap, "%s%s.exe", base, PROJECT_SETUP_PROGRAM);
#else
    snprintf(out, cap, "%s%s", base, PROJECT_SETUP_PROGRAM);
#endif
    SDL_free(base);
    return stat(out, &st) == 0;
#endif
}

/* Start OpenSWOS Setup (on its own: it outlives the game). 1 when it started. */
int32_t AssetsCheck_LaunchSetup(void)
{
    char path[1024];
    pid_t pid;
    if (!AssetsCheck_FindSetup(path, sizeof path))
        return 0;
#if defined(__APPLE__)
    char *argv[] = { "/usr/bin/open", "-a", path, NULL };
#else
    char *argv[] = { path, NULL };
#endif
    if (posix_spawn(&pid, argv[0], NULL, NULL, argv, environ) != 0) {
        fprintf(stderr, "[assets] could not start %s\n", path);
        return 0;
    }
    fprintf(stderr, "[assets] started %s\n", path);
    return 1;
}

int32_t AssetsCheck_ShowNotice(void)
{
    char setup[1024], text[sizeof s_noticeText];
    const char *buttons[3];
    int32_t n = 0, iSetup = -1, iNever = -1;
    if (s_notice == NOTICE_NONE)
        return 0;
    const int32_t haveSetup = AssetsCheck_FindSetup(setup, sizeof setup);
    if (s_notice == NOTICE_ASSETS)
        snprintf(text, sizeof text,
                 "Your game data was made by an older %s, and this version makes some of it "
                 "differently.|%s It remembers where your copy of the game is, so it only takes a "
                 "moment.|The game still plays meanwhile: some pictures may be out of date.",
                 PROJECT_SETUP_NAME, haveSetup
                     ? "LAUNCH SETUP opens it and closes the game: click Update there."
                     : "Run " PROJECT_SETUP_NAME " and click Update.");
    else
        snprintf(text, sizeof text, "%s", s_noticeText);
    if (haveSetup) { iSetup = n; buttons[n++] = "LAUNCH SETUP"; }
    buttons[n++] = "CONTINUE";
    if (s_notice == NOTICE_ASSETS) { iNever = n; buttons[n++] = "DO NOT REMIND ME"; }
    const int32_t chosen = MenuDialog_Show(s_noticeTitle, text, buttons, n, 0);
    if (chosen >= 0 && chosen == iSetup && AssetsCheck_LaunchSetup()) {
        s_quitForSetup = 1;             /* (the game closes so the setup can replace the data) */
    } else if (chosen >= 0 && chosen == iNever && s_opts) {
        snprintf(s_opts->assets_reminder_off, sizeof s_opts->assets_reminder_off, "%s", s_noticeSig);
        if (s_iniPath)
            Settings_Save(s_opts, s_iniPath);
        fprintf(stderr, "[assets] reminder off for data versions %s\n", s_noticeSig);
    }
    s_notice = NOTICE_NONE;
    return 1;
}

int32_t AssetsCheck_QuitForSetup(void) { return s_quitForSetup; }

#else
int32_t AssetsCheck_FindSetup(char *out, uint32_t cap) { (void)out; (void)cap; return 0; }
int32_t AssetsCheck_LaunchSetup(void) { return 0; }
int32_t AssetsCheck_ShowNotice(void) { return 0; }
int32_t AssetsCheck_QuitForSetup(void) { return 0; }
#endif
