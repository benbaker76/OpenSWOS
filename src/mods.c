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
static char    s_uuids[MODS_MAX][40];
static uint8_t s_canonical[MODS_MAX];
static uint8_t s_cosmetic[MODS_MAX];
static uint8_t s_online[MODS_MAX];          /* must be alike online: "gameplay" or "data" */
static char    s_versions[MODS_MAX][24];
static uint32_t s_fileHash[MODS_MAX];
static uint8_t s_jagSprites[MODS_MAX];      /* "sprites": "jaguar" */
static int32_t s_count;

int32_t Mods_Count(void) { return s_count; }
const char *Mods_Id(int32_t i) { return i >= 0 && i < s_count ? s_ids[i] : ""; }
const char *Mods_Uuid(int32_t i) { return i >= 0 && i < s_count ? s_uuids[i] : ""; }
int32_t Mods_IsCanonical(int32_t i) { return i >= 0 && i < s_count && s_canonical[i]; }
int32_t Mods_IsOn(const char *slug)
{
    for (int32_t i = 0; i < s_count; i++)
        if (strcmp(s_ids[i], slug) == 0)
            return 1;
    return 0;
}

#ifdef SWOS_SDL_BUILD
static uint32_t fnv(uint32_t h, const void *data, uint32_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    for (uint32_t i = 0; i < n; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

uint32_t Mods_OnlineSignature(char *names, uint32_t cap)
{
    /* in uuid order, so the order the mods are listed in does not matter */
    int32_t order[MODS_MAX], n = 0;
    for (int32_t i = 0; i < s_count; i++)
        if (s_online[i]) {
            int32_t k = n++;
            while (k > 0 && strcmp(s_uuids[order[k - 1]], s_uuids[i]) > 0) {
                order[k] = order[k - 1];
                k--;
            }
            order[k] = i;
        }
    if (names && cap)
        names[0] = '\0';
    if (!n)
        return 0;
    uint32_t h = 2166136261u, used = 0;
    for (int32_t k = 0; k < n; k++) {
        const int32_t i = order[k];
        h = fnv(h, s_uuids[i], (uint32_t)strlen(s_uuids[i]) + 1);
        h = fnv(h, s_versions[i], (uint32_t)strlen(s_versions[i]) + 1);
        h = fnv(h, &s_fileHash[i], sizeof s_fileHash[i]);
        if (names && used + 1 < cap)
            used += (uint32_t)snprintf(names + used, cap - used, "%s%s %s", k ? ", " : "",
                                       s_names[i][0] ? s_names[i] : s_ids[i], s_versions[i]);
    }
    return h ? h : 1;
}
const char *Mods_JaguarSprites(void)
{
    for (int32_t i = 0; i < s_count; i++)
        if (s_jagSprites[i])
            return s_ids[i];
    return NULL;
}
#else
/* (the Amiga plays no online match) */
uint32_t Mods_OnlineSignature(char *names, uint32_t cap)
{
    if (names && cap)
        names[0] = '\0';
    return 0;
}
#endif

/* The next id in `p` (separated by commas, spaces, tabs, new lines), into `out`; returns
 * where the scan stopped, or NULL at the end. An id with anything but letters, digits, '-'
 * and '_' is skipped (it names a folder: no '/', no '..'). `*star`: a '*' followed it. */
#ifdef SWOS_SDL_BUILD
#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>
/* Every file under a mod's folder, in name order: its path and its bytes (the online
 * signature's; a copy edited by hand, or half updated, is another mod). */
static int name_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static uint32_t hash_tree(const char *root, const char *rel, uint32_t h)
{
    char dir[1300];
    snprintf(dir, sizeof dir, "%s%s%s", root, rel[0] ? "/" : "", rel);
    DIR *d = opendir(dir);
    if (!d)
        return h;
    char *names[512];
    int32_t n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < 512)
        if (e->d_name[0] != '.')
            names[n++] = strdup(e->d_name);
    closedir(d);
    qsort(names, (size_t)n, sizeof names[0], name_cmp);
    for (int32_t i = 0; i < n; i++) {
        char sub[1300], full[1300];
        struct stat st;
        snprintf(sub, sizeof sub, "%s%s%s", rel, rel[0] ? "/" : "", names[i]);
        snprintf(full, sizeof full, "%s/%s", root, sub);
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
            h = hash_tree(root, sub, h);
        } else {
            FILE *f = fopen(full, "rb");
            h = fnv(h, sub, (uint32_t)strlen(sub) + 1);
            if (f) {
                uint8_t buf[8192];
                size_t got;
                while ((got = fread(buf, 1, sizeof buf, f)) > 0)
                    h = fnv(h, buf, (uint32_t)got);
                fclose(f);
            }
        }
        free(names[i]);
    }
    return h;
}
#endif

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
#include <dirent.h>
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

/* A UUID's text form, 8-4-4-4-12 hex digits. */
static int uuid_ok(const char *u)
{
    for (int i = 0; i < 36; i++) {
        const char c = u[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return 0;
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return 0;
        }
    }
    return u[36] == '\0';
}

/* The canonical mods: those in the game's own mods/ folder (include/mods.h). */
static struct { char id[MODS_ID_MAX]; char uuid[40]; uint8_t byDefault, cosmetic; } s_canon[MODS_MAX];

/* A rig is running (--replay-test, --srp-resim, --career-golden): it proves the original game, so
 * a canonical mod that is on by default is on only if it changes nothing but the picture; one
 * the run names itself (--mods) is still on (docs/MODS.md §26). */
static int32_t s_rig;
void Mods_SetRig(int32_t on) { s_rig = on; }
static int32_t s_canonCount;

static int in_list(const char *list, const char *id)
{
    char x[MODS_ID_MAX];
    int star;
    for (const char *p = list; p && (p = next_id(p, x, sizeof x, &star)) != NULL;)
        if (strcmp(x, id) == 0) return 1;
    return 0;
}

static int uuid_taken(const char *u)
{
    for (int32_t i = 0; i < s_count; i++)
        if (strcmp(s_uuids[i], u) == 0) return 1;
    return 0;
}

/* What a mod's folder and manifest say (`info`), or why it cannot be a mod (the return: "" when
 * it can). The checks the loader and the MODS screen share. */
static const char *read_manifest(const char *dir, int canonical, ModInfo *info)
{
    char manifest[1200], changes[16] = "";
    struct stat st;
    int len = 0, b = 0;
    info->canonical = (uint8_t)canonical;
    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode))
        return "no folder";
    snprintf(manifest, sizeof manifest, "%s/mod.json", dir);
    char *json = slurp(manifest, &len);
    if (!json)
        return "no mod.json in its folder";
    if (mjson_get_string(json, len, "$.name", info->name, (int)sizeof info->name) < 0)
        snprintf(info->name, sizeof info->name, "%s", info->slug);
    if (mjson_get_string(json, len, "$.version", info->version, (int)sizeof info->version) < 0)
        info->version[0] = '\0';
    if (mjson_get_string(json, len, "$.changes", changes, (int)sizeof changes) < 0)
        changes[0] = '\0';
    /* What it changes: "cosmetic" (how it looks), "modes" (matches of its own, a normal one
     * untouched), "career" (careers and their saves) never matter online; "gameplay" (how a
     * match plays) and "data" (the teams, the world it plays with) must be alike in both
     * players' games (Mods_OnlineSignature) -- and a mod that says nothing is not assumed
     * harmless. Canonical or not: the build's own mods can be on in one game and off in the
     * other. */
    snprintf(info->changes, sizeof info->changes, "%s", changes[0] ? changes : "gameplay");
    if (mjson_get_string(json, len, "$.sprites", info->sprites, (int)sizeof info->sprites) < 0)
        info->sprites[0] = '\0';
    info->cosmetic = (uint8_t)(strcmp(info->changes, "gameplay") != 0 && strcmp(info->changes, "data") != 0);
    (void)canonical;
    info->byDefault = (uint8_t)(mjson_get_bool(json, len, "$.default", &b) && b);
    const int uuidOk = mjson_get_string(json, len, "$.uuid", info->uuid, (int)sizeof info->uuid) >= 0 &&
                       uuid_ok(info->uuid);
    const int sdl = has_target(json, len, "sdl");
    free(json);
    if (!uuidOk)
        return "its mod.json has no \"uuid\" (every mod has one: docs/MODS.md)";
    if (!sdl)
        return "not made for this game (its targets have no \"sdl\")";
    if (!canonical)
        for (int32_t c = 0; c < s_canonCount; c++)
            if (strcmp(s_canon[c].uuid, info->uuid) == 0)
                return "its uuid is a canonical mod's";
    return "";
}

/* Load the mod in `dir` (its slug `id`) as the next one, if its manifest allows. */
static void load_one(const char *id, const char *dir, int canonical)
{
    ModInfo info;
    memset(&info, 0, sizeof info);
    snprintf(info.slug, sizeof info.slug, "%s", id);
    if (s_count >= MODS_MAX) {
        fprintf(stderr, "[mods] %s: left out -- at most %d mods\n", id, MODS_MAX);
        return;
    }
    const char *why = read_manifest(dir, canonical, &info);
    if (!why[0] && uuid_taken(info.uuid))
        why = "another enabled mod has its uuid";
    if (why[0]) {
        fprintf(stderr, "[mods] %s: left out -- %s (%s)\n", id, why, dir);
        return;
    }
    s_cosmetic[s_count] = info.cosmetic;
    s_online[s_count] = (uint8_t)!info.cosmetic;
    s_jagSprites[s_count] = strcmp(info.sprites, "jaguar") == 0;
    snprintf(s_versions[s_count], sizeof s_versions[0], "%s", info.version);
    s_fileHash[s_count] = s_online[s_count] ? hash_tree(dir, "", 2166136261u) : 0;
    s_canonical[s_count] = (uint8_t)canonical;
    snprintf(s_names[s_count], sizeof s_names[0], "%s", info.name);
    snprintf(s_ids[s_count], sizeof s_ids[0], "%s", id);
    snprintf(s_uuids[s_count], sizeof s_uuids[0], "%s", info.uuid);
    snprintf(s_dirs[s_count], sizeof s_dirs[0], "%s", dir);
    fprintf(stderr, "[mods] on: %s (%s %s, %s, %s %s) from %s\n", id, info.name, info.version, info.changes,
            canonical ? "canonical" : "dlc", info.uuid, dir);
    s_count++;
}

int32_t Mods_Scan(ModInfo *out, int32_t max)
{
    int32_t n = 0;
    for (int pass = 0; pass < 2; pass++) {          /* the canonical ones, then the DLC */
        DIR *d = opendir(pass == 0 ? MODS_CANONICAL_DIR : s_root);
        if (!d)
            continue;
        struct dirent *e;
        while ((e = readdir(d)) != NULL && n < max) {
            char x[MODS_ID_MAX], dir[1100];
            int star;
            if (e->d_name[0] == '.' || !next_id(e->d_name, x, sizeof x, &star) || strcmp(x, e->d_name) != 0)
                continue;
            snprintf(dir, sizeof dir, "%s/%s", pass == 0 ? MODS_CANONICAL_DIR : s_root, e->d_name);
            memset(&out[n], 0, sizeof out[n]);
            snprintf(out[n].slug, sizeof out[n].slug, "%s", e->d_name);
            const char *why = read_manifest(dir, pass == 0, &out[n]);
            if (why[0] && strcmp(why, "no folder") == 0)
                continue;                           /* (a file, not a mod's folder) */
            snprintf(out[n].problem, sizeof out[n].problem, "%s", why);
            out[n].on = (uint8_t)known(e->d_name);
            n++;
        }
        closedir(d);
    }
    return n;
}

void Mods_Init(const char *enabled, const char *disabled)
{
    const char *env = getenv("SWOS_MODS");
    s_count = s_canonCount = 0;
    if (env && env[0])
        snprintf(s_root, sizeof s_root, "%s", env);
    else if (swos_user_dir()[0])
        snprintf(s_root, sizeof s_root, "%s/mods", swos_user_dir());
    else
        snprintf(s_root, sizeof s_root, "mods");

    /* The canonical mods, beside the game: their slugs, uuids and "default". */
    DIR *d = opendir(MODS_CANONICAL_DIR);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL && s_canonCount < MODS_MAX) {
            char path[1200], x[MODS_ID_MAX];
            int len = 0, star, b = 0;
            if (e->d_name[0] == '.' || !next_id(e->d_name, x, sizeof x, &star) || strcmp(x, e->d_name) != 0)
                continue;
            snprintf(path, sizeof path, "%s/%s/mod.json", MODS_CANONICAL_DIR, e->d_name);
            char *json = slurp(path, &len);
            if (!json)
                continue;
            snprintf(s_canon[s_canonCount].id, sizeof s_canon[0].id, "%s", e->d_name);
            if (mjson_get_string(json, len, "$.uuid", s_canon[s_canonCount].uuid, (int)sizeof s_canon[0].uuid) < 0)
                s_canon[s_canonCount].uuid[0] = '\0';
            s_canon[s_canonCount].byDefault = mjson_get_bool(json, len, "$.default", &b) && b;
            {
                char changes[24] = "";
                mjson_get_string(json, len, "$.changes", changes, (int)sizeof changes);
                s_canon[s_canonCount].cosmetic = strcmp(changes, "cosmetic") == 0;
            }
            free(json);
            s_canonCount++;
        }
        closedir(d);
    }

    /* The player's list first, in its order: a DLC from the player's folder, else a canonical
     * mod of that slug; then each canonical mod that is on by default and not disabled. */
    char id[MODS_ID_MAX], dir[1100];
    int star;
    for (const char *p = enabled; p && (p = next_id(p, id, sizeof id, &star)) != NULL;) {
        struct stat st;
        if (known(id))
            continue;
        snprintf(dir, sizeof dir, "%s/%s", s_root, id);
        if (stat(dir, &st) == 0 && S_ISDIR(st.st_mode)) {
            load_one(id, dir, 0);
            continue;
        }
        int32_t c;
        for (c = 0; c < s_canonCount && strcmp(s_canon[c].id, id) != 0; c++)
            ;
        if (c < s_canonCount) {
            snprintf(dir, sizeof dir, "%s/%s", MODS_CANONICAL_DIR, id);
            load_one(id, dir, 1);
        } else {
            fprintf(stderr, "[mods] %s: left out -- no folder %s/%s and no canonical mod of that name\n", id, s_root, id);
        }
    }
    for (int32_t c = 0; c < s_canonCount; c++)
        if (s_canon[c].byDefault && !known(s_canon[c].id) && !in_list(disabled, s_canon[c].id) &&
            (!s_rig || s_canon[c].cosmetic)) {
            snprintf(dir, sizeof dir, "%s/%s", MODS_CANONICAL_DIR, s_canon[c].id);
            load_one(s_canon[c].id, dir, 1);
        }
    if (!s_count)
        fprintf(stderr, "[mods] none on ([mods] enabled in SWOS.ini; DLC mods are folders in %s, canonical ones in %s)\n",
                s_root, MODS_CANONICAL_DIR);
}

#else  /* the Amiga */

/* (data/MODS is read by src/resload.c at its first file read, with the DOS calls it is
 *  already inside: a file read from here would nest the interrupt swap every read does.) */
static char s_dirs[MODS_MAX][MODS_ID_MAX + 6];

const char *Mods_Dir(int32_t i) { return i >= 0 && i < s_count ? s_dirs[i] : ""; }
const char *Mods_Root(void) { return "mods"; }

void Mods_Init(const char *list, const char *disabled)
{
    char id[MODS_ID_MAX];
    int star;
    (void)disabled;             /* (the setup writes data/MODS with what is on) */
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
