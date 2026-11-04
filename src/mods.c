/*
 * mods.c -- the enabled mods (include/mods.h, docs/MODS.md §8.1).
 *
 * This file keeps the list; the overlay itself is where each target resolves a data file:
 * swos_data_path() on SDL (src/swos_io.c), make_read_path() on the Amiga (src/resload.c).
 *
 * The Amiga does not parse mod.json -- JSON on a 68k is slow and the manifest is for the
 * installers -- so its list is the drawer's data/MODS, which OpenSWOS Setup writes only with
 * mods whose targets include the Amiga. "changes" is taken from the same file: an id with
 * a '*' after it is not cosmetic.
 */
#include "mods.h"

#include <string.h>

/* The Amiga m68k <string.h> is partial (memcpy, not strcmp): its prototype, as cal_label.c
 * gives it; the symbol is src/swos_io.c's. */
#if defined(__m68k__) || defined(__amigaos__) || defined(__amigaos4__)
extern int strcmp(const char *a, const char *b);
#else
#include <stdio.h>
#endif

static char    s_ids[MODS_MAX][MODS_ID_MAX];
static char    s_names[MODS_MAX][48];
static uint8_t s_cosmetic[MODS_MAX];
static int32_t s_count;

int32_t Mods_Count(void) { return s_count; }
const char *Mods_Id(int32_t i) { return i >= 0 && i < s_count ? s_ids[i] : ""; }

const char *Mods_OnlineBlocker(void)
{
    for (int32_t i = 0; i < s_count; i++)
        if (!s_cosmetic[i])
            return s_names[i][0] ? s_names[i] : s_ids[i];
    return NULL;
}

/* The next id in `p` (separated by commas, spaces, tabs, new lines), into `out`; returns
 * where the scan stopped, or NULL at the end. An id with anything but letters, digits, '-'
 * and '_' is skipped (it names a folder: no '/', no '..'). `*star`: a '*' followed it. */
static const char *next_id(const char *p, char *out, uint32_t cap, int *star)
{
    for (;;) {
        while (*p == ',' || *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            p++;
        if (!*p)
            return NULL;
        uint32_t n = 0;
        int ok = 1;
        *star = 0;
        while (*p && *p != ',' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
            const char c = *p++;
            if (c == '*') { *star = 1; continue; }
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '-' || c == '_'))
                ok = 0;
            if (n + 1 < cap)
                out[n++] = c;
            else
                ok = 0;
        }
        out[n] = '\0';
        if (ok && n)
            return p;
    }
}

static int known(const char *id)
{
    for (int32_t i = 0; i < s_count; i++)
        if (strcmp(s_ids[i], id) == 0)
            return 1;
    return 0;
}

#ifdef SWOS_SDL_BUILD

#include <stdlib.h>
#include <sys/stat.h>
#include "mjson.h"
#include "swos_io.h"

static char s_root[1024];
static char s_dirs[MODS_MAX][1100];

const char *Mods_Dir(int32_t i) { return i >= 0 && i < s_count ? s_dirs[i] : ""; }
const char *Mods_Root(void) { return s_root; }

static char *slurp(const char *path, int *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    char *buf = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        const long n = ftell(f);
        if (n > 0 && n < 65536 && fseek(f, 0, SEEK_SET) == 0 && (buf = (char *)malloc((size_t)n + 1)) != NULL) {
            if (fread(buf, 1, (size_t)n, f) == (size_t)n) {
                buf[n] = '\0';
                *len = (int)n;
            } else {
                free(buf);
                buf = NULL;
            }
        }
    }
    fclose(f);
    return buf;
}

/* Whether the manifest's `targets` names `want`. */
static int has_target(const char *json, int len, const char *want)
{
    char t[16];
    for (int i = 0; i < 8; i++) {
        char key[16];
        snprintf(key, sizeof key, "$.targets[%d]", i);
        if (mjson_get_string(json, len, key, t, (int)sizeof t) < 0)
            return 0;
        if (strcmp(t, want) == 0)
            return 1;
    }
    return 0;
}

void Mods_Init(const char *list)
{
    const char *env = getenv("SWOS_MODS");
    s_count = 0;
    if (env && env[0])
        snprintf(s_root, sizeof s_root, "%s", env);
    else if (swos_user_dir()[0])
        snprintf(s_root, sizeof s_root, "%s/mods", swos_user_dir());
    else
        snprintf(s_root, sizeof s_root, "mods");
    if (!list || !list[0]) {
        fprintf(stderr, "[mods] none enabled ([mods] enabled in SWOS.ini; mods are folders in %s)\n", s_root);
        return;
    }
    char id[MODS_ID_MAX];
    int star;
    for (const char *p = list; (p = next_id(p, id, sizeof id, &star)) != NULL;) {
        char manifest[1200];
        struct stat st;
        if (s_count >= MODS_MAX) {
            fprintf(stderr, "[mods] %s: left out -- at most %d mods\n", id, MODS_MAX);
            continue;
        }
        if (known(id))
            continue;
        snprintf(s_dirs[s_count], sizeof s_dirs[0], "%s/%s", s_root, id);
        if (stat(s_dirs[s_count], &st) != 0 || !S_ISDIR(st.st_mode)) {
            fprintf(stderr, "[mods] %s: left out -- no folder %s\n", id, s_dirs[s_count]);
            continue;
        }
        snprintf(manifest, sizeof manifest, "%s/mod.json", s_dirs[s_count]);
        int len = 0;
        char *json = slurp(manifest, &len);
        if (!json) {
            fprintf(stderr, "[mods] %s: left out -- no mod.json in its folder\n", id);
            continue;
        }
        char changes[16] = "", version[24] = "";
        if (!has_target(json, len, "sdl")) {
            fprintf(stderr, "[mods] %s: left out -- not made for this game (its targets have no \"sdl\")\n", id);
            free(json);
            continue;
        }
        if (mjson_get_string(json, len, "$.name", s_names[s_count], (int)sizeof s_names[0]) < 0)
            s_names[s_count][0] = '\0';
        if (mjson_get_string(json, len, "$.changes", changes, (int)sizeof changes) < 0)
            changes[0] = '\0';
        if (mjson_get_string(json, len, "$.version", version, (int)sizeof version) < 0)
            version[0] = '\0';
        free(json);
        /* Anything but an explicit "cosmetic" counts as changing the game: a mod that says
         * nothing is not assumed harmless online. */
        s_cosmetic[s_count] = strcmp(changes, "cosmetic") == 0;
        snprintf(s_ids[s_count], sizeof s_ids[0], "%s", id);
        fprintf(stderr, "[mods] on: %s (%s %s, %s) from %s\n", id, s_names[s_count][0] ? s_names[s_count] : id,
                version, changes[0] ? changes : "changes not said: treated as gameplay", s_dirs[s_count]);
        s_count++;
    }
}

#else  /* the Amiga */

/* (data/MODS is read by src/resload.c at its first file read, with the DOS calls it is
 *  already inside: a file read from here would nest the interrupt swap every read does.) */
static char s_dirs[MODS_MAX][MODS_ID_MAX + 6];

const char *Mods_Dir(int32_t i) { return i >= 0 && i < s_count ? s_dirs[i] : ""; }
const char *Mods_Root(void) { return "mods"; }

void Mods_Init(const char *list)
{
    char id[MODS_ID_MAX];
    int star;
    s_count = 0;
    for (const char *p = list; p && (p = next_id(p, id, sizeof id, &star)) != NULL;) {
        if (s_count >= MODS_MAX || known(id))
            continue;
        uint32_t n = 0;
        const char *pre = "mods/";
        while (*pre) s_dirs[s_count][n++] = *pre++;
        for (const char *q = id; *q; ) s_dirs[s_count][n++] = *q++;
        s_dirs[s_count][n] = '\0';
        for (n = 0; id[n]; n++) s_ids[s_count][n] = id[n];
        s_ids[s_count][n] = '\0';
        s_names[s_count][0] = '\0';
        s_cosmetic[s_count] = !star;
        s_count++;
    }
}

#endif
