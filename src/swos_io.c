/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* swos_io.c — portable I/O + heap shim. See swos_io.h. */
#include "swos_io.h"
#if defined(SWOS_SDL_BUILD) && !defined(_WIN32)
/* SDL_GetPrefPath / SDL_free — the Switch mounts save:/ through them, and
 * macOS uses SDL_GetPrefPath when a .app launch leaves HOME unset. On the
 * Switch this header also declares SDL_NintendoSwitch_CommitSaveData, which
 * the fork puts in SDL_filesystem.h rather than a header of its own. */
#include <SDL.h>
#endif
#include <stdarg.h>
#include <string.h>

/* ============================================================
 * Minimal libc fallbacks for Amiga.
 *
 * The m68k-amiga-elf toolchain (bartmanabyss build) ships with a
 * partial libc — memcpy/memset link but strlen/strcmp/memcmp/atoi
 * are not provided. Rather than chase the toolchain config, drop in
 * tiny implementations here. SDL builds use the host libc so these
 * are skipped.
 * ============================================================ */
#if defined(__m68k__) || defined(__amigaos__) || defined(__amigaos4__)

size_t strlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/* mjson uses strncmp; -Ofast inlined it as a builtin so it never needed a
 * definition, but -O0 (DBG build) emits a real call. */
int strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (n == 0) return 0;
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    while (n--) {
        if (*pa != *pb) return (int)*pa - (int)*pb;
        pa++; pb++;
    }
    return 0;
}

int atoi(const char *s) {
    int n = 0, neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    return neg ? -n : n;
}

#endif

/* ============================================================
 * Allocation. Both backings store the block size in the first 4
 * bytes; the caller receives a pointer to the byte after that.
 * swos_io_free / swos_io_realloc reverse the offset to recover
 * the size that AllocMem / malloc were given.
 * ============================================================ */

#define SWOS_HDR_SIZE 4u

#ifdef SWOS_SDL_BUILD
#include <stdio.h>
#include <stdlib.h>

void *swos_io_alloc(uint32_t sz)
{
    uint8_t *block = (uint8_t *)malloc((size_t)sz + SWOS_HDR_SIZE);
    if (!block) return (void *)0;
    *(uint32_t *)block = sz + SWOS_HDR_SIZE;
    return block + SWOS_HDR_SIZE;
}

void *swos_io_realloc(void *p, uint32_t new_sz)
{
    if (!p) return swos_io_alloc(new_sz);
    uint8_t *block = (uint8_t *)p - SWOS_HDR_SIZE;
    uint8_t *grew = (uint8_t *)realloc(block, (size_t)new_sz + SWOS_HDR_SIZE);
    if (!grew) return (void *)0;
    *(uint32_t *)grew = new_sz + SWOS_HDR_SIZE;
    return grew + SWOS_HDR_SIZE;
}

void swos_io_free(void *p)
{
    if (!p) return;
    uint8_t *block = (uint8_t *)p - SWOS_HDR_SIZE;
    free(block);
}

#else  /* !SWOS_SDL_BUILD — Amiga */

extern uint8_t *AllocMemAny(uint32_t sz);
extern void     FreeMemAny(void *addr, uint32_t sz);

void *swos_io_alloc(uint32_t sz)
{
    uint8_t *block = AllocMemAny(sz + SWOS_HDR_SIZE);
    if (!block) return (void *)0;
    *(uint32_t *)block = sz + SWOS_HDR_SIZE;
    return block + SWOS_HDR_SIZE;
}

void *swos_io_realloc(void *p, uint32_t new_sz)
{
    if (!p) return swos_io_alloc(new_sz);
    uint8_t *block = (uint8_t *)p - SWOS_HDR_SIZE;
    uint32_t old_size = *(uint32_t *)block;        /* includes header */
    uint32_t old_payload = old_size - SWOS_HDR_SIZE;

    void *out = swos_io_alloc(new_sz);
    if (!out) return (void *)0;

    uint32_t copy_n = (old_payload < new_sz) ? old_payload : new_sz;
    if (copy_n) memcpy(out, p, copy_n);

    FreeMemAny(block, old_size);
    return out;
}

void swos_io_free(void *p)
{
    if (!p) return;
    uint8_t *block = (uint8_t *)p - SWOS_HDR_SIZE;
    uint32_t sz = *(uint32_t *)block;
    FreeMemAny(block, sz);
}

#endif

/* ============================================================
 * Whole-file load/save.
 * ============================================================ */

#ifdef SWOS_SDL_BUILD

/* ---- Save directory -------------------------------------------------------
 * Every save file (careers .CAR/.SEA/.PRE/.DIY, tactics .TAC, highlights .HIL,
 * edited teams .EDT, options .CFG) lives under one base directory, resolved once
 * at startup by swos_save_dir_init() and returned by swos_save_dir(). Both the
 * reader (resolve_save_path here) and the writer (resolve_path in resload.c)
 * consult it, so load and save always agree.
 *
 * Saves go to the platform's per-user data location — never inside the app
 * bundle / install dir (read-only under Gatekeeper, /Applications, Program
 * Files, etc.). See docs/SAVE.md.
 *   Windows: %USERPROFILE%\Documents\SWOS\save
 *   macOS:   ~/Library/Application Support/SWOS/save
 *   Linux:   ~/.swos/save
 *   WASM:    "save" (→ the IndexedDB-backed /save mount from wasm/pre.js)
 * If the location can't be determined or created, we fall back to the relative
 * "save" default so a dev/CLI run still works from the game dir. */
static char s_save_dir[1024] = "save";
/* The user's game folder — the PARENT of the save dir. SWOS.ini lives here (a
 * sibling of save/), so config and saves share one per-user location. Default "."
 * (= the exe's cwd) so a dev/CLI run and the WASM build keep the old relative
 * "./SWOS.ini". swos_save_dir_init() overwrites it with the real per-user dir. */
static char s_user_dir[1024] = ".";
/* Where the data setup put the original game's files (swos_assets_dir()). */
static char s_assets_dir[1024] = "";
#ifdef SWOS_SWITCH_BUILD
/* The read-only content root, from SDL_GetBasePath() — which also mounts it.
 * "rom:" until resolved so a read before init fails visibly rather than
 * silently reading the wrong place. */
static char s_data_dir[256] = "rom:";
#endif

const char *swos_save_dir(void)
{
    return s_save_dir;
}

const char *swos_user_dir(void)
{
    return s_user_dir;
}

/* Full path to SWOS.ini in the user's game folder. Returns a static buffer
 * (rebuilt each call from s_user_dir, which is set once at startup) — safe to
 * cache the pointer (it never moves). */
const char *swos_ini_path(void)
{
    static char s_ini[1088];
    snprintf(s_ini, sizeof s_ini, "%s/SWOS.ini", s_user_dir);
    return s_ini;
}

#ifndef SWOS_WASM_BUILD
#include <errno.h>
#if defined(_WIN32) && !defined(__CYGWIN__)
#include <direct.h>
#define SWOS_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define SWOS_MKDIR(p) mkdir((p), 0777)
#endif

/* mkdir -p: create every missing component of `path`. Returns 0 if the leaf
 * exists afterwards, -1 otherwise. Intermediate mkdirs on components that
 * already exist fail harmlessly and are ignored.
 *
 * A bare Windows drive prefix is skipped rather than ignored, because on our
 * Windows target it is NOT harmless -- see the comment at the skip below. */
static int swos_make_dirs(const char *path)
{
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    size_t len = strlen(tmp);
    if (len && (tmp[len - 1] == '/' || tmp[len - 1] == '\\'))
        tmp[len - 1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            /* Never mkdir a bare drive prefix. The Windows path built above is
             * "C:/Users/...", so the first component is "C:" -- and the Windows
             * target here is CYGWIN, whose mkdir does not treat that as a drive
             * root. It creates a literal directory named "C:" in the CWD and
             * returns 0, which is where the stray bin/C: came from (and one
             * beside the game in every install). MSVC's _mkdir would reject it
             * with EEXIST, so nothing is lost by never making the call.
             * p == tmp + 2 can only ever be the leading component. */
            if (!(p == tmp + 2 && tmp[1] == ':'))
                SWOS_MKDIR(tmp);
            *p = '/';
        }
    }
    if (SWOS_MKDIR(tmp) == 0)
        return 0;
    return (errno == EEXIST) ? 0 : -1;
}
#endif  /* !SWOS_WASM_BUILD */

void swos_save_dir_init(void)
{
#if defined(SWOS_SWITCH_BUILD)
    /* Switch: SDL_GetBasePath() is what MOUNTS the read-only content —
     * nn::fs::MountRom("rom", ...) happens inside it, on first call. Nothing
     * else mounts it, so without this every fopen("rom:/...") fails and the
     * game comes up with no data at all. Do it before anything reads a file.
     * The returned string is the prefix swos_data_path() then uses, rather
     * than a hard-coded "rom:/", so the two can never disagree. */
    {
        char *base = SDL_GetBasePath();
        if (base && *base) {
            size_t n = strlen(base);
            while (n > 1 && (base[n - 1] == '/' || base[n - 1] == '\\'))
                base[--n] = '\0';
            snprintf(s_data_dir, sizeof s_data_dir, "%s", base);
        } else {
            fprintf(stderr, "[switch] SDL_GetBasePath failed: %s\n", SDL_GetError());
        }
        if (base) SDL_free(base);
    }

    /* SDL_GetPrefPath mounts the per-user save against the account the
     * player picked at launch and hands back "save:/". That needs
     * StartupUserAccount to be Required in the .nmeta — with None it returns
     * NULL and the game would start fresh every launch (docs/SWITCH.md).
     *
     * The mount is flat and small, so unlike the desktops there is no separate
     * user/ and user/save split: SWOS.ini and the .CAR files share it. */
    {
        char *pref = SDL_GetPrefPath(NULL, "SWOS");
        if (pref && *pref) {
            size_t n = strlen(pref);
            /* SDL returns a trailing separator; the rest of this file composes
             * paths as "%s/%s" and would otherwise produce "save://FOO.CAR". */
            while (n > 1 && (pref[n - 1] == '/' || pref[n - 1] == '\\'))
                pref[--n] = '\0';
            snprintf(s_user_dir, sizeof s_user_dir, "%s", pref);
            snprintf(s_save_dir, sizeof s_save_dir, "%s", pref);
        }
        if (pref) SDL_free(pref);
    }
    return;
#elif defined(SWOS_WASM_BUILD)
    /* Browser build: saves persist in the IndexedDB-backed /save mount set up in
     * wasm/pre.js. The game writes "save/…" relative to cwd "/" (chdir is skipped
     * on WASM), i.e. into /save — so the "save" default is already correct, and
     * s_user_dir stays "." so SWOS.ini resolves to /SWOS.ini (where wasm/pre.js
     * copies it from localStorage). */
    return;
#else
    /* user = the per-user game folder (holds SWOS.ini); save = user + "/save".
     * Resolving the user dir first (not just the save dir) lets SWOS.ini live
     * beside the save folder in one per-user location. Kept well under the 1024
     * static/tmp buffers so the "/save" + "/SWOS.ini" compositions never risk
     * truncation (a home path this long can't happen in practice). */
    char user[512];

# if defined(_WIN32) || defined(__CYGWIN__)
    /* Windows: %USERPROFILE%\Documents\SWOS. Forward slashes are accepted by
     * both the native CRT and Cygwin's path layer. */
    const char *profile = getenv("USERPROFILE");
    if (!profile || !*profile)
        return;                                  /* fall back to "save" / "." */
    snprintf(user, sizeof user, "%s/Documents/SWOS", profile);
    for (char *p = user; *p; p++)
        if (*p == '\\') *p = '/';
# elif defined(__ANDROID__)
    const char *internal = SDL_AndroidGetInternalStoragePath();
    if (!internal || !*internal) return;
    snprintf(user, sizeof user, "%s", internal);
# elif defined(__APPLE__)
    /* ~/Library/Application Support/SWOS, but NOT via $HOME alone.
     *
     * A .app launched from Finder gets a trimmed environment, and with HOME
     * missing every path below silently fell back to "." — which inside a
     * bundle is read-only. The symptom is the confusing one a tester hit: no
     * SWOS.ini is ever created, and one placed in the documented location is
     * never read, so every setting appears to do nothing.
     *
     * SDL_GetPrefPath asks the OS instead of the environment
     * (NSSearchPathForDirectoriesInDomains) and returns that same directory,
     * so it is a drop-in that works however the app was started. HOME is still
     * tried first: it is what the docs say and what a terminal launch uses. */
    const char *home = getenv("HOME");
    if (home && *home) {
        snprintf(user, sizeof user, "%s/Library/Application Support/SWOS", home);
    } else {
        char *pref = SDL_GetPrefPath(NULL, "SWOS");
        if (!pref || !*pref) {
            fprintf(stderr, "[settings] no HOME and SDL_GetPrefPath failed (%s) "
                            "— SWOS.ini and saves fall back to the CWD\n",
                    SDL_GetError());
            if (pref) SDL_free(pref);
            return;
        }
        {
            size_t n = strlen(pref);
            while (n > 1 && pref[n - 1] == '/') pref[--n] = '\0';
            snprintf(user, sizeof user, "%s", pref);
        }
        SDL_free(pref);
        fprintf(stderr, "[settings] HOME not set; using the OS preferences "
                        "path: %s\n", user);
    }
# else  /* Linux and other Unix */
    /* Same shape as the macOS branch above, and for the same reason: if the
     * process was started without HOME there is still an OS answer, and the
     * silent "." fallback is the worst of the options. */
    const char *home = getenv("HOME");
    if (home && *home) {
        snprintf(user, sizeof user, "%s/.swos", home);
    } else {
        char *pref = SDL_GetPrefPath(NULL, "SWOS");
        if (!pref || !*pref) {
            fprintf(stderr, "[settings] no HOME and SDL_GetPrefPath failed (%s) "
                            "— SWOS.ini and saves fall back to the CWD\n",
                    SDL_GetError());
            if (pref) SDL_free(pref);
            return;
        }
        {
            size_t n = strlen(pref);
            while (n > 1 && pref[n - 1] == '/') pref[--n] = '\0';
            snprintf(user, sizeof user, "%s", pref);
        }
        SDL_free(pref);
        fprintf(stderr, "[settings] HOME not set; using the OS preferences "
                        "path: %s\n", user);
    }
# endif

    char save[1024];
    snprintf(save, sizeof save, "%s/save", user);
    /* mkdir -p the save dir also creates its parent (the user dir), so both the
     * SWOS.ini location and the save location exist afterward. */
    if (swos_make_dirs(save) != 0) {
        /* Same dead end: without this directory there is nowhere to write, and
         * the "." fallback is read-only inside a macOS .app. errno names the
         * reason (permissions, a read-only volume, a file in the way). */
        fprintf(stderr, "[settings] cannot create %s: %s — SWOS.ini and saves "
                        "fall back to the CWD\n", save, strerror(errno));
        return;
    }
    snprintf(s_user_dir, sizeof s_user_dir, "%s", user);
    snprintf(s_save_dir, sizeof s_save_dir, "%s", save);
#endif  /* !SWOS_WASM_BUILD */
}

#if !defined(SWOS_WASM_BUILD) && !defined(SWOS_SWITCH_BUILD)
/* SWOS_ASSETS, else <user dir>/assets. Read here rather than in
 * swos_save_dir_init so it is right even when that returned early (no HOME):
 * then the user dir is "." and the assets folder "./assets". */
static void assets_dir_init(void)
{
    static int s_done;
    if (s_done) return;
    s_done = 1;
    const char *env = getenv("SWOS_ASSETS");
    if (env && *env)
        snprintf(s_assets_dir, sizeof s_assets_dir, "%s", env);
    else
        snprintf(s_assets_dir, sizeof s_assets_dir, "%s/assets", s_user_dir);
    size_t n = strlen(s_assets_dir);
    while (n > 1 && (s_assets_dir[n - 1] == '/' || s_assets_dir[n - 1] == '\\'))
        s_assets_dir[--n] = '\0';
}
#endif

static void resolve_save_path(const uint8_t *name, char *out, uint32_t out_sz)
{
    const char *src = (const char *)name;
    const char *colon = strchr(src, ':');
    if (colon) src = colon + 1;
    snprintf(out, out_sz, "%s/%s", swos_save_dir(), src);
}

static int load_alloc_path(const char *path, uint8_t **out_buf, uint32_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 1;

    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return 1; }

    uint8_t *buf = (uint8_t *)swos_io_alloc((uint32_t)n + 1u);
    if (!buf) { fclose(f); return 1; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { swos_io_free(buf); return 1; }
    buf[n] = '\0';

    *out_buf = buf;
    *out_size = (uint32_t)n;
    return 0;
}

int swos_io_load_alloc(const uint8_t *name, uint8_t **out_buf, uint32_t *out_size)
{
    char path[1280];
    resolve_save_path(name, path, sizeof(path));
    return load_alloc_path(path, out_buf, out_size);
}

int swos_io_load_data_alloc(const char *name, uint8_t **out_buf, uint32_t *out_size)
{
    char path[1280];
    return load_alloc_path(swos_data_path(name, path, sizeof(path)), out_buf, out_size);
}

int swos_io_save(const uint8_t *name, const uint8_t *buf, uint32_t size)
{
    char path[1280];
    resolve_save_path(name, path, sizeof(path));

    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    size_t put = fwrite(buf, 1, size, f);
    fclose(f);
    /* Closed before committing — the commit requires it. */
    swos_save_commit();
    return (put == size) ? 0 : 1;
}

/* ---- Data root + save durability (see swos_io.h) -------------------- */

#if !defined(SWOS_WASM_BUILD) && !defined(SWOS_SWITCH_BUILD)
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>   /* stat()/struct stat below — already pulled in above for the
                         * Cygwin/Linux/macOS SWOS_MKDIR branch, but not for raw MinGW
                         * (_WIN32 && !__CYGWIN__), which mingw-w64 supports fine too */

/* Find `rel` under `root` matching every component without regard to case,
 * writing the real path to out. 0 when it is there. An exact stat first: that
 * is the common case and costs one call. */
static int find_nocase(const char *root, const char *rel, char *out, uint32_t cap)
{
    struct stat st;
    snprintf(out, cap, "%s/%s", root, rel);
    if (stat(out, &st) == 0) return 0;

    char cur[1024];
    snprintf(cur, sizeof cur, "%s", root);
    const char *p = rel;
    while (*p) {
        while (*p == '/') p++;
        const char *end = strchr(p, '/');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (!len) break;
        DIR *d = opendir(cur);
        if (!d) return -1;
        struct dirent *e;
        int found = 0;
        while ((e = readdir(d)) != NULL) {
            if (strlen(e->d_name) == len && strncasecmp(e->d_name, p, len) == 0) {
                size_t n = strlen(cur);
                snprintf(cur + n, sizeof cur - n, "/%s", e->d_name);
                found = 1;
                break;
            }
        }
        closedir(d);
        if (!found) return -1;
        p += len;
    }
    snprintf(out, cap, "%s", cur);
    return 0;
}
#endif

const char *swos_data_path(const char *rel, char *buf, uint32_t buf_sz)
{
#ifdef SWOS_SWITCH_BUILD
    /* The content is mounted read-only at rom:/ and there is no chdir, so the
     * relative paths the rest of the game uses have to be anchored here. */
    if (!rel || !buf || buf_sz == 0) return rel;
    if (strchr(rel, ':')) return rel;                /* already anchored */
    snprintf(buf, buf_sz, "%s/%s", s_data_dir, rel);
    return buf;
#elif defined(SWOS_WASM_BUILD)
    (void)buf; (void)buf_sz;
    return rel;
#else
    if (!rel || !buf || buf_sz == 0) return rel;
    if (rel[0] == '/' || rel[0] == '\\' || strchr(rel, ':')) return rel;   /* already anchored */
    assets_dir_init();
    if (s_assets_dir[0] && find_nocase(s_assets_dir, rel, buf, buf_sz) == 0)
        return buf;
    return rel;
#endif
}

const char *swos_data_relative(const char *path)
{
    const char *roots[2] = { swos_data_root(), swos_assets_dir() };
    for (int i = 0; i < 2; i++) {
        size_t n = strlen(roots[i]);
        if (n && strncmp(path, roots[i], n) == 0 && (path[n] == '/' || path[n] == '\\')) {
            path += n;
            while (*path == '/' || *path == '\\') path++;
            return path;
        }
    }
    return path;
}

int _gNoAmigaSound;
int _gNoAmigaScreens;

int swos_data_exists(const char *rel)
{
#if defined(SWOS_WASM_BUILD) || defined(SWOS_SWITCH_BUILD)
    (void)rel;
    return 1;
#else
    char buf[1024], found[1024];
    const char *p = swos_data_path(rel, buf, (uint32_t)sizeof buf);
    if (p == buf)
        return 1;                       /* in the assets folder */
    return find_nocase(".", rel, found, (uint32_t)sizeof found) == 0;
#endif
}

const char *swos_assets_dir(void)
{
#if !defined(SWOS_WASM_BUILD) && !defined(SWOS_SWITCH_BUILD)
    assets_dir_init();
#endif
    return s_assets_dir;
}

const char *swos_data_root(void)
{
#ifdef SWOS_SWITCH_BUILD
    return s_data_dir;          /* "rom:" once swos_save_dir_init() has run */
#else
    return "";                  /* every other target chdir()s — no prefix */
#endif
}

void swos_save_commit(void)
{
#ifdef SWOS_SWITCH_BUILD
    /* Not durable until committed, and SDL will not do it for you. The mount
     * name matches what SDL_GetPrefPath mounted ("save"). Files must already be
     * CLOSED -- every caller does that first.
     *
     * A failure here costs the player whatever they just saved, so it is said
     * out loud: silent is how you get "my career vanished" with nothing in the
     * log to explain it. */
    if (!SDL_NintendoSwitch_CommitSaveData("save"))
        fprintf(stderr, "[save] commit to save: FAILED (%s) -- what was just "
                        "written will not survive this launch\n", SDL_GetError());
#endif
}

#else  /* Amiga */

extern uint32_t RobNorthenLoader_C(uint32_t cmd, uint32_t arg1,
                                    uint8_t *name, uint8_t *buf);

#define ROBN_LOAD      0u
#define ROBN_SAVE      1u
#define ROBN_GET_SIZE  5u

int swos_io_load_alloc(const uint8_t *name, uint8_t **out_buf, uint32_t *out_size)
{
    uint32_t sz = RobNorthenLoader_C(ROBN_GET_SIZE, 0, (uint8_t *)name, (uint8_t *)0);
    if (sz == 0) return 1;

    uint8_t *buf = (uint8_t *)swos_io_alloc(sz + 1u);
    if (!buf) return 1;

    uint32_t rc = RobNorthenLoader_C(ROBN_LOAD, 0, (uint8_t *)name, buf);
    if (rc != 0) { swos_io_free(buf); return 1; }
    buf[sz] = '\0';

    *out_buf = buf;
    *out_size = sz;
    return 0;
}

/* Amiga: the game runs from its own directory and resload owns the save
 * device, so both of these are nothing. Defined anyway so the shared call
 * sites need no #ifdef. */
int swos_io_load_data_alloc(const char *name, uint8_t **out_buf, uint32_t *out_size)
{
    /* Bundled .DAT files are not redirected to save/ by resload. */
    return swos_io_load_alloc((const uint8_t *)name, out_buf, out_size);
}

const char *swos_data_path(const char *rel, char *buf, uint32_t buf_sz)
{
    (void)buf; (void)buf_sz;
    return rel;
}

const char *swos_data_root(void) { return ""; }

void swos_save_commit(void) { }

int swos_io_save(const uint8_t *name, const uint8_t *buf, uint32_t size)
{
    uint32_t rc = RobNorthenLoader_C(ROBN_SAVE, size, (uint8_t *)name,
                                      (uint8_t *)buf);
    return (rc == 0) ? 0 : 1;
}

#endif

/* ============================================================
 * Minimal snprintf: %s / %d / %u / literal characters.
 *
 * The codebase's format strings are simple — prefix + filename or
 * prefix + integer. A handwritten formatter avoids pulling in stdio
 * on Amiga and is small enough to live here. Output is always
 * NUL-terminated; the return value matches stdio snprintf (number
 * of characters that *would* have been written excluding NUL, so
 * the caller can detect truncation by comparing to bufsz).
 * ============================================================ */

static uint32_t fmt_uint(char *buf, uint32_t bufsz, uint32_t *pos, uint32_t v)
{
    char tmp[16];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    /* tmp holds digits in reverse — emit forwards. */
    uint32_t emitted = 0;
    for (int i = n - 1; i >= 0; i--) {
        if (*pos + 1u < bufsz) buf[(*pos)++] = tmp[i];
        emitted++;
    }
    return emitted;
}

int swos_io_snprintf(char *buf, uint32_t bufsz, const char *fmt, ...)
{
    if (bufsz == 0) return 0;
    va_list ap;
    va_start(ap, fmt);
    uint32_t pos = 0;
    uint32_t total = 0;
    const char *p = fmt;
    while (*p) {
        if (*p != '%') {
            if (pos + 1u < bufsz) buf[pos++] = *p;
            total++;
            p++;
            continue;
        }
        p++;  /* skip '%' */
        if (*p == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (*s) {
                if (pos + 1u < bufsz) buf[pos++] = *s;
                total++;
                s++;
            }
        } else if (*p == 'd') {
            int v = va_arg(ap, int);
            uint32_t uv;
            if (v < 0) {
                if (pos + 1u < bufsz) buf[pos++] = '-';
                total++;
                uv = (uint32_t)(-(long)v);
            } else {
                uv = (uint32_t)v;
            }
            total += fmt_uint(buf, bufsz, &pos, uv);
        } else if (*p == 'u') {
            uint32_t v = va_arg(ap, unsigned int);
            total += fmt_uint(buf, bufsz, &pos, v);
        } else if (*p == '%') {
            if (pos + 1u < bufsz) buf[pos++] = '%';
            total++;
        } else {
            /* Unsupported specifier — emit verbatim. */
            if (pos + 1u < bufsz) buf[pos++] = '%';
            total++;
            if (*p && pos + 1u < bufsz) { buf[pos++] = *p; total++; }
        }
        if (*p) p++;
    }
    buf[pos] = '\0';
    va_end(ap);
    return (int)total;
}
