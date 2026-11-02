/*
 * mobile.c -- what the phone builds (Android, iOS: SWOS_MOBILE_BUILD) do differently at start-up
 * (docs/MOBILE.md).
 *
 * The project's own data -- the tracked files of bin/ every drop ships (scripts/stage_runtime.sh) --
 * is the game's working folder, as bin/ is on a desktop. On iOS it is the app bundle itself. An
 * Android APK's assets are not files, so the build puts a manifest beside them (a stamp, then
 * "size path" for each file: packaging/android/app/build.gradle.kts) and they are copied out into
 * the app's internal storage on the first start, and again whenever the stamp changes (a new
 * version).
 *
 * The original game's data the player copies over themselves, from a PC: the assets folder
 * OpenSWOS Setup made, into the player's folder (swos_io.c: Android/data/<app id>/files, iOS's
 * Documents). Without it the game cannot start, and says where it goes.
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "project_identity.h"
#include "swos_io.h"

#ifdef SWOS_MOBILE_BUILD

#ifdef SWOS_ANDROID_BUILD
/* The whole of one APK asset (SDL reads the APK for a relative path). The caller frees it. */
static char *read_asset(const char *path, size_t *size)
{
    SDL_RWops *in = SDL_RWFromFile(path, "rb");
    char *buf = NULL;
    size_t len = 0, cap = 0, got;
    if (!in)
        return NULL;
    for (;;) {
        if (len + 65536 + 1 > cap) {
            char *more = (char *)realloc(buf, cap = (len + 65536 + 1) * 2);
            if (!more) { free(buf); SDL_RWclose(in); return NULL; }
            buf = more;
        }
        got = SDL_RWread(in, buf + len, 1, 65536);
        if (got == 0)
            break;
        len += got;
    }
    SDL_RWclose(in);
    buf[len] = '\0';
    *size = len;
    return buf;
}

static void make_dirs(const char *path)
{
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    mkdir(tmp, 0777);
}

static int write_file(const char *path, const char *data, size_t size)
{
    char dir[1024];
    const char *slash = strrchr(path, '/');
    if (slash) {
        snprintf(dir, sizeof dir, "%.*s", (int)(slash - path), path);
        make_dirs(dir);
    }
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    int ok = fwrite(data, 1, size, f) == size;
    return fclose(f) == 0 && ok ? 0 : -1;
}

/* The game's data, out of the APK into <internal>/game: done again only when the manifest
 * differs from the copy's (a new version), the manifest written last so an interrupted copy is
 * redone. The folder, or NULL. */
static const char *android_data_dir(void)
{
    static char root[1024];
    const char *internal = SDL_AndroidGetInternalStoragePath();
    size_t msize = 0, hsize = 0;
    char path[1280];
    if (!internal)
        return NULL;
    snprintf(root, sizeof root, "%s/game", internal);
    char *manifest = read_asset("game.manifest", &msize);
    if (!manifest) {
        fprintf(stderr, "[mobile] the APK has no game.manifest: its data is missing\n");
        return NULL;
    }
    snprintf(path, sizeof path, "%s/game.manifest", root);
    FILE *f = fopen(path, "rb");
    char *have = NULL;
    if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n > 0 && (have = (char *)malloc((size_t)n)) != NULL)
            hsize = fread(have, 1, (size_t)n, f);
        fclose(f);
    }
    int same = have && hsize == msize && memcmp(have, manifest, msize) == 0;
    free(have);
    if (same) {
        free(manifest);
        return root;
    }
    fprintf(stderr, "[mobile] unpacking the game's data into %s\n", root);
    int files = 0;
    char *line = strchr(manifest, '\n');            /* (past the stamp) */
    while (line && *++line) {
        char *end = strchr(line, '\n');
        if (end) *end = '\0';
        char *rel = strchr(line, ' ');
        if (rel) {
            size_t size = 0;
            char *data = read_asset(rel + 1, &size);
            snprintf(path, sizeof path, "%s/%s", root, rel + 1);
            if (!data || write_file(path, data, size) != 0) {
                fprintf(stderr, "[mobile] could not unpack %s\n", rel + 1);
                free(data);
                free(manifest);
                return NULL;
            }
            free(data);
            files++;
        }
        if (!end)
            break;
        *end = '\n';
        line = end;
    }
    /* (the manifest itself, whole: read again -- its lines were cut above) */
    free(manifest);
    manifest = read_asset("game.manifest", &msize);
    snprintf(path, sizeof path, "%s/game.manifest", root);
    if (!manifest || write_file(path, manifest, msize) != 0) {
        free(manifest);
        return NULL;
    }
    free(manifest);
    fprintf(stderr, "[mobile] %d files unpacked\n", files);
    return root;
}
#endif

/* Before SDL_Init: landscape only; Android's back button kept for the game (ESC: input_sdl.c)
 * rather than closing it; the accelerometer NOT a joystick -- it would take player 1's place;
 * iOS's home bar out of the way until swiped. */
void Mobile_SetHints(void)
{
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
    SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "2");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");      /* (the controls read the fingers) */
}

const char *Mobile_DataDir(void)
{
#ifdef SWOS_ANDROID_BUILD
    return android_data_dir();
#else
    static char dir[1024];
    char *base = SDL_GetBasePath();             /* (the app bundle) */
    if (!base)
        return NULL;
    snprintf(dir, sizeof dir, "%s", base);
    SDL_free(base);
    return dir;
#endif
}

/* The original game's data: copied from a PC into the assets folder. Without it, where it goes,
 * and 0 (the game cannot start). */
int Mobile_CheckGameData(void)
{
    const char *assets = swos_assets_dir();
    char probe[1280];
    struct stat st;
    snprintf(probe, sizeof probe, "%s/ASSETS.ini", assets);
    if (stat(probe, &st) == 0)
        return 1;
    snprintf(probe, sizeof probe, "%s/data", assets);   /* (an older setup's: no ASSETS.ini) */
    if (stat(probe, &st) == 0 && S_ISDIR(st.st_mode))
        return 1;
    fprintf(stderr, "[mobile] no game data in %s\n", assets);
    char msg[1600];
    snprintf(msg, sizeof msg,
             PROJECT_NAME " needs the original game's data, made from your own copy of the game "
             "by " PROJECT_SETUP_NAME " on a PC or Mac.\n\n"
#ifdef SWOS_ANDROID_BUILD
             "Run " PROJECT_SETUP_NAME ", then connect this phone to the computer by USB and copy "
             "the setup's \"assets\" folder into:\n\nAndroid/data/" PROJECT_APP_ID "/files\n\n"
#else
             "Run " PROJECT_SETUP_NAME ", then copy the setup's \"assets\" folder into this app's "
             "folder: in the Files app, On My iPhone > " PROJECT_NAME " (or from a Mac, in "
             "Finder's view of the phone, Files > " PROJECT_NAME ").\n\n"
#endif
             "Then start " PROJECT_NAME " again.\n\n(%s)", assets);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, PROJECT_NAME ": copy your game data", msg, NULL);
    return 0;
}

/* A text field is open (input_text.c): the system's keyboard, or not. */
void Mobile_TextInput(int on)
{
    if (on)
        SDL_StartTextInput();
    else
        SDL_StopTextInput();
}

#endif /* SWOS_MOBILE_BUILD */
