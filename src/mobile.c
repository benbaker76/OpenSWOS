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
 * The original game's data comes from a PC or Mac: the assets folder OpenSWOS Setup made, in
 * the player's folder (swos_io.c: Android/data/<app id>/files, iOS's Documents) -- copied over
 * Wi-Fi from the setup (src/phone_fetch.c), or by hand. Without it the game cannot start.
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "project_identity.h"
#include "swos_io.h"
#include "platform_sdl.h"   /* Platform_ShowProgress */
#include "swos_settings.h"
#include "phone_fetch.h"
#ifdef SWOS_ANDROID_BUILD
#include <jni.h>
#endif

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

#ifndef SWOS_ANDROID_BUILD
static char s_dropped[512];     /* a setup link iOS handed over (platform_sdl.c's pump) */
static int  s_playing;          /* the data check is done: the game has its data loaded */

static int is_setup_link(const char *f)
{
    return f && (strncmp(f, PROJECT_URL_SCHEME "://", strlen(PROJECT_URL_SCHEME) + 3) == 0 ||
                 strncmp(f, "https://" PROJECT_SITE "/get", strlen(PROJECT_SITE) + 12) == 0);
}

/* A file or URL iOS handed over: a setup link is kept for the start-up's check. While the game
 * plays it cannot be taken (the data is loaded, and iOS gives an app no way to start itself
 * again, as Android's RestartActivity does): it says how instead. */
/* The app delegate's (packaging/ios/app_delegate.m): keeps a setup link while the game has not
 * started, says nothing -- it is called inside the launch or the run loop, and the pump's
 * SDL_DROPFILE comes to Mobile_LinkDropped after it for the rest. */
void Mobile_LinkKept(const char *link)
{
    if (is_setup_link(link) && !s_playing)
        snprintf(s_dropped, sizeof s_dropped, "%s", link);
}

void Mobile_LinkDropped(const char *link)
{
    if (!is_setup_link(link))
        return;
    if (!s_playing) {
        snprintf(s_dropped, sizeof s_dropped, "%s", link);
        return;
    }
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, PROJECT_NAME,
                             "To copy the game's data from " PROJECT_SETUP_NAME ", close " PROJECT_NAME
                             " (swipe it away), then open the link again.", NULL);
}
#endif

/* A link from OpenSWOS Setup's Wi-Fi copy (include/phone_copy.h) that opened the app, into
 * `out`: Android keeps the intent's (OpenSWOSActivity.takeLink, at the start and on a new one),
 * iOS hands it over as a dropped "file" (SDL's openURL). 1 when there was one. */
static int pending_link(char *out, size_t cap)
{
    out[0] = 0;
#ifdef SWOS_ANDROID_BUILD
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject act = (jobject)SDL_AndroidGetActivity();
    if (env && act) {
        jclass cls = (*env)->GetObjectClass(env, act);
        jmethodID m = (*env)->GetStaticMethodID(env, cls, "takeLink", "()Ljava/lang/String;");
        if (m) {
            jstring js = (jstring)(*env)->CallStaticObjectMethod(env, cls, m);
            if (js) {
                const char *c = (*env)->GetStringUTFChars(env, js, NULL);
                if (c) {
                    snprintf(out, cap, "%s", c);
                    (*env)->ReleaseStringUTFChars(env, js, c);
                }
                (*env)->DeleteLocalRef(env, js);
            }
        }
        if ((*env)->ExceptionCheck(env))
            (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, cls);
        (*env)->DeleteLocalRef(env, act);
    }
#else
    if (s_dropped[0]) {                             /* (one the platform's pump kept) */
        snprintf(out, cap, "%s", s_dropped);
        s_dropped[0] = 0;
        return 1;
    }
    SDL_Event e;
    for (int i = 0; i < 20 && !out[0]; i++) {       /* (the run loop delivers openURL) */
        SDL_PumpEvents();
        while (SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_DROPFILE, SDL_DROPFILE) > 0) {
            if (e.drop.file && !out[0] && (strncmp(e.drop.file, PROJECT_URL_SCHEME "://", strlen(PROJECT_URL_SCHEME) + 3) == 0 ||
                                           strncmp(e.drop.file, "https://" PROJECT_SITE "/get", strlen(PROJECT_SITE) + 12) == 0))
                snprintf(out, cap, "%s", e.drop.file);
            SDL_free(e.drop.file);
        }
        if (!out[0])
            SDL_Delay(15);
    }
#endif
    return out[0] != 0;
}

/* The game has its data and plays: a link from now on restarts the app (OpenSWOSActivity). */
static void playing(void)
{
#ifndef SWOS_ANDROID_BUILD
    s_playing = 1;
#endif
#ifdef SWOS_ANDROID_BUILD
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject act = (jobject)SDL_AndroidGetActivity();
    if (!env || !act)
        return;
    jclass cls = (*env)->GetObjectClass(env, act);
    jmethodID m = (*env)->GetStaticMethodID(env, cls, "gamePlaying", "()V");
    if (m)
        (*env)->CallStaticVoidMethod(env, cls, m);
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, act);
#endif
}


static int have_game_data(void)
{
    const char *assets = swos_assets_dir();
    char probe[1280];
    struct stat st;
    snprintf(probe, sizeof probe, "%s/ASSETS.ini", assets);
    if (stat(probe, &st) == 0)
        return 1;
    snprintf(probe, sizeof probe, "%s/data", assets);   /* (an older setup's: no ASSETS.ini) */
    return stat(probe, &st) == 0 && S_ISDIR(st.st_mode);
}

static int ask(const char *title, const char *msg, const char *yes, const char *no)
{
    const SDL_MessageBoxButtonData buttons[] = {
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, no },
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, yes },
    };
    const SDL_MessageBoxData box = { SDL_MESSAGEBOX_INFORMATION, NULL, title, msg, no ? 2 : 1, no ? buttons : buttons + 1, NULL };
    int hit = 0;
    if (SDL_ShowMessageBox(&box, &hit) != 0)
        return 0;
    return hit == 1;
}

/* The copy from a setup, with the bar; the player's SWOS.ini read again for the options it
 * brought. 1 when it worked. */
static int copy_from(const PhoneFetchSetup *from, PlatformInitOptions *opts, const char *iniPath)
{
    char err[600];
    Platform_ShowProgress(0);
    const int ok = PhoneFetch_Copy(from, swos_assets_dir(), iniPath, Platform_ShowProgress, err, sizeof err);
    if (ok) {
        Settings_Load(opts, iniPath);
        ask(PROJECT_NAME, "The game's data is copied. Have fun!", "Play", NULL);
    } else
        ask(PROJECT_NAME ": the copy did not work", err, "OK", NULL);
    return ok;
}

/* The original game's data: made by OpenSWOS Setup on a PC or Mac and copied over -- over
 * Wi-Fi from the setup (src/phone_fetch.c), or by hand into the assets folder. A link from the
 * setup copies it (again, when there is data already). Without data, how to get it, until there
 * is some or the player gives up: 0, the game cannot start. */
int Mobile_CheckGameData(PlatformInitOptions *opts, const char *iniPath)
{
    char link[512];
    PhoneFetchSetup from;
    PhoneFetch_Tidy(swos_assets_dir());
    if (pending_link(link, sizeof link)) {
        fprintf(stderr, "[mobile] opened by %s\n", link);
        if (!PhoneFetch_ParseLink(link, &from))
            ask(PROJECT_NAME, "That link is not one from " PROJECT_SETUP_NAME ". Scan its QR code again.", "OK", NULL);
        else if (!have_game_data() || strstr(link, "&ok=1") ||    /* (asked before the restart) */
                 ask(PROJECT_NAME, "Copy the game's data from " PROJECT_SETUP_NAME " again? It replaces the data on this phone.",
                     "Copy", "Not now"))
            copy_from(&from, opts, iniPath);
    }
    while (!have_game_data()) {
        fprintf(stderr, "[mobile] no game data in %s\n", swos_assets_dir());
        /* (Short: a phone's dialog in landscape does not scroll, and its buttons go first.) */
        const char *msg =
            "The game's data comes from " PROJECT_SETUP_NAME " on your PC or Mac. Run it to its "
            "phone screen, then scan its QR code with the camera"
#ifdef SWOS_ANDROID_BUILD
            ", or tap Copy from my computer (on the same Wi-Fi)."
#else
            "."
#endif
            ;
        if (!ask(PROJECT_NAME, msg, "Copy from my computer", "Close"))
            return 0;
        if (pending_link(link, sizeof link) && PhoneFetch_ParseLink(link, &from)) {
            copy_from(&from, opts, iniPath);
            continue;
        }
        Platform_ShowProgress(0);
        if (PhoneFetch_Find(&from, 3000)) {
            char q[300];
            snprintf(q, sizeof q, "Copy the game's data from %s?", from.name);
            if (ask(PROJECT_NAME, q, "Copy", "Cancel"))
                copy_from(&from, opts, iniPath);
        } else
            ask(PROJECT_NAME, "No " PROJECT_SETUP_NAME " answered on this Wi-Fi. Open it on its phone screen "
                "and scan its QR code with this phone's camera.", "OK", NULL);
    }
    playing();
    return 1;
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
