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
 * The reminder is a message box with "Don't remind me", which keeps the data versions it
 * was about in SWOS.ini ([general] assets_reminder_off): the same reminder is not shown
 * again, a later setup that changes a part again is. Nothing is shown headless (the test
 * rigs), in the arcade cabinet, in the web build (its data is built in) or on the Switch.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <SDL.h>

#include "assets_check.h"
#include "assets_ini.h"
#include "swos_io.h"            /* swos_assets_dir */
#include "swos_settings.h"      /* Settings_Save */

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
                snprintf(out, cap, "no %s (an older OpenSWOS Setup)", ASSETS_INI_NAME);
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
    fprintf(stderr, "[assets] game data made by an older OpenSWOS Setup: %s\n", why);
    version_signature(sig, sizeof sig);
    if (getenv("SWOS_NO_ASSETS_CHECK") || opts->arcade ||
        !driver || strcmp(driver, "dummy") == 0 || strcmp(driver, "offscreen") == 0)
        return;
    if (strcmp(opts->assets_reminder_off, sig) == 0)
        return;                     /* "Don't remind me" for these very versions */

    static const SDL_MessageBoxButtonData kButtons[] = {
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "OK" },
        { 0, 1, "Don't remind me" },
    };
    SDL_MessageBoxData box;
    memset(&box, 0, sizeof box);
    box.flags      = SDL_MESSAGEBOX_INFORMATION;
    box.title      = "OpenSWOS: update your game data";
    box.message    = "Your game data was made by an older OpenSWOS Setup, and this version of "
                     "OpenSWOS makes some of it differently.\n\n"
                     "Open OpenSWOS Setup and click Update. It remembers where your copy of the "
                     "game is, so it only takes a moment.\n\n"
                     "The game still plays meanwhile; some pictures may be out of date.";
    box.numbuttons = (int)(sizeof kButtons / sizeof kButtons[0]);
    box.buttons    = kButtons;
    int pressed = 0;
    if (SDL_ShowMessageBox(&box, &pressed) == 0 && pressed == 1) {
        snprintf(opts->assets_reminder_off, sizeof opts->assets_reminder_off, "%s", sig);
        if (iniPath)
            Settings_Save(opts, iniPath);
        fprintf(stderr, "[assets] reminder off for data versions %s\n", sig);
    }
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
                 "SWOS.ini asks for World 2026-27 (world = %s), but its data is not installed.\n\n"
                 "Download the World 2026-27 package, then add it in OpenSWOS Setup and click "
                 "Install.\n\n"
                 "SWOS 96/97 is played meanwhile.", s_worldTag);
    else
        snprintf(text, sizeof text,
                 "Your World 2026-27 data is not the one this version of OpenSWOS was made for "
                 "(it is %s, this version's is %s).\n\n"
                 "Download the World 2026-27 package for this version, then add it in OpenSWOS "
                 "Setup and click Install.\n\n"
                 "Until then a World 2026-27 career may not play correctly: leagues and clubs can "
                 "be missing or wrong.", s_worldHave, s_worldWant);
    fprintf(stderr, "[world] message box: %s\n", s_worldState == 1 ? "not installed" : "other data");
    SDL_ShowSimpleMessageBox(s_worldState == 1 ? SDL_MESSAGEBOX_INFORMATION : SDL_MESSAGEBOX_WARNING,
                             s_worldState == 1 ? "OpenSWOS: World 2026-27 is not installed"
                                               : "OpenSWOS: update World 2026-27",
                             text, NULL);
#endif
}
