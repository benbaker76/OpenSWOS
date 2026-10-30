/*
 * sd_sources.c -- the player's own copy of the game, wherever it is.
 */
/* lstat, under -std=c99: a link is never followed when searching a folder. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "sd_sources.h"
#include "../../../include/project_identity.h"
#include "sd_manifest.h"
#include "miniz.h"
#include "sg_dir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#define JAG_ROM_SIZE (2u * 1024 * 1024)

static int32_t ends_with_nocase(const char *s, const char *suffix)
{
    size_t ls = strlen(s), lx = strlen(suffix);
    if (lx > ls)
        return 0;
    for (size_t i = 0; i < lx; i++)
        if (tolower((unsigned char)s[ls - lx + i]) != tolower((unsigned char)suffix[i]))
            return 0;
    return 1;
}

static int32_t is_dir(const char *path)
{
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static long file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return n;
}

/* SWOS 2020's game folder (<install>/swos): its program and a pitch. */
static int32_t is_s2020_game_dir(const char *dir)
{
    char p[1024];
    return Sg_FindNoCase(dir, "swos-port-Release.exe", p, sizeof p) == 0 &&
           Sg_FindNoCase(dir, "pitch1.dat", p, sizeof p) == 0 &&
           Sg_FindNoCase(dir, "pitch1.blk", p, sizeof p) == 0;
}

/* A PC game folder: SPRITE.DAT and the DATA folder's team files -- and not
 * SWOS 2020's, which has both but its own graphics. */
static int32_t is_dos_dir(const char *dir)
{
    char p[1024];
    return Sg_FindNoCase(dir, "SPRITE.DAT", p, sizeof p) == 0 &&
           Sg_FindNoCase(dir, "DATA/TEAM.000", p, sizeof p) == 0 &&
           !is_s2020_game_dir(dir);
}

/* `dir` is SWOS 2020's install or its game folder: the game folder into out. */
static int32_t find_s2020(const char *dir, char *out, uint32_t cap)
{
    char game[1024];
    if (is_s2020_game_dir(dir)) {
        snprintf(out, cap, "%s", dir);
        return 0;
    }
    if (Sg_FindNoCase(dir, "swos", game, sizeof game) == 0 && is_s2020_game_dir(game)) {
        snprintf(out, cap, "%s", game);
        return 0;
    }
    return -1;
}

/* The cartridge: 2 MB, and the game's name where the Jaguar header keeps
 * nothing useful -- so check for the LZ-packed sheets' tell-tale instead:
 * Sensible Soccer International Edition's own text. */
static int32_t is_jag_rom(const char *path)
{
    if (!(ends_with_nocase(path, ".jag") || ends_with_nocase(path, ".j64") ||
          ends_with_nocase(path, ".rom") || ends_with_nocase(path, ".bin")))
        return 0;
    if (file_size(path) != (long)JAG_ROM_SIZE)
        return 0;
    SgBuf b;
    char err[256];
    if (Sg_ReadFile(path, &b, err) != 0)
        return 0;
    int32_t found = 0;
    static const char tell[] = "SENSIBLE";
    for (uint32_t i = 0; i + sizeof tell - 1 <= b.size && !found; i++)
        if (memcmp(b.data + i, tell, sizeof tell - 1) == 0)
            found = 1;
    Sg_BufFree(&b);
    return found;
}

/* ---- the Amiga game on a hard disk image ---------------------------------- */

typedef struct {
    Adf  *adf;
    char *dir;
    uint32_t cap;
    int32_t found;
} HdfFind;

static int32_t ends_with_path_nocase(const char *path, const char *tail, size_t *prefixLen)
{
    size_t lp = strlen(path), lt = strlen(tail);
    if (lt > lp || !ends_with_nocase(path, tail))
        return 0;
    if (lp > lt && path[lp - lt - 1] != '/')
        return 0;
    *prefixLen = lp - lt;
    return 1;
}

/* The game's folder: the one with data/TEAM.000 and grafs/CJCTEAM1.RAW beside it. */
static int32_t hdf_find_cb(const char *path, uint32_t size, void *user)
{
    HdfFind *h = user;
    size_t pre;
    (void)size;
    if (!ends_with_path_nocase(path, "data/TEAM.000", &pre) || pre + 32 >= h->cap)
        return 0;
    char probe[300];
    snprintf(probe, sizeof probe, "%.*sgrafs/CJCTEAM1.RAW", (int)pre, path);
    if (Adf_FileSize(h->adf, probe) < 0)
        return 0;
    snprintf(h->dir, h->cap, "%.*s", (int)pre, path);
    h->found = 1;
    return 1;
}

/* Which volume of the image at `path` has the game, and in which folder. 0 when found. */
static int32_t hdf_find_game(const char *path, int32_t *vol, char *dir, uint32_t cap)
{
    int32_t n = Adf_CountVolumes(path);
    for (int32_t i = 0; i < n; i++) {
        char err[256];
        Adf *a = Adf_OpenVolume(path, i, err);
        if (!a)
            continue;
        HdfFind h = { a, dir, cap, 0 };
        if (Adf_FileSize(a, "data/TEAM.000") >= 0 && Adf_FileSize(a, "grafs/CJCTEAM1.RAW") >= 0) {
            dir[0] = '\0';
            h.found = 1;
        } else {
            Adf_Walk(a, hdf_find_cb, &h);
        }
        Adf_Close(a);
        if (h.found) {
            *vol = i;
            return 0;
        }
    }
    return -1;
}

/* FNV-1a 64 of the Amiga team data (sd_manifest.c, g_sdAmigaSeasons), read with `rd`.
 * 0 when a file is missing. */
static uint64_t fnv_bytes(uint64_t h, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

static char s_amigaProblem[256];

const char *SdSources_AmigaProblem(void) { return s_amigaProblem; }

static uint64_t team_fingerprint(SdSources *s)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (int32_t i = 0; g_sdAmigaCopy[i]; i++) {
        const char *rel = g_sdAmigaCopy[i];
        if (strncmp(rel, "data/", 5) != 0)
            continue;
        SgBuf b;
        char err[256], lower[256];
        if (SdSources_ReadAmiga(s, rel, &b, err) != 0) {
            snprintf(s_amigaProblem, sizeof s_amigaProblem, "%s", err);
            return 0;
        }
        size_t n = strlen(rel);
        for (size_t k = 0; k <= n && k < sizeof lower; k++)
            lower[k] = (char)tolower((unsigned char)rel[k]);
        uint8_t size[4] = { (uint8_t)(b.size >> 24), (uint8_t)(b.size >> 16),
                            (uint8_t)(b.size >> 8), (uint8_t)b.size };
        h = fnv_bytes(h, (const uint8_t *)lower, n + 1);
        h = fnv_bytes(h, size, 4);
        h = fnv_bytes(h, b.data, b.size);
        Sg_BufFree(&b);
    }
    return h;
}

/* Whether the open Amiga source has `rel`, from its directory alone. */
static int32_t amiga_has(SdSources *s, const char *rel)
{
    char path[512];
    if ((s->disk1 && Adf_FileSize(s->disk1, rel) >= 0) || (s->disk2 && Adf_FileSize(s->disk2, rel) >= 0))
        return 1;
    snprintf(path, sizeof path, "%s%s", s->hdfDir, rel);
    if (s->hdfDisk && Adf_FileSize(s->hdfDisk, path) >= 0)
        return 1;
    snprintf(path, sizeof path, "%s%s", s->lhaDir, rel);
    return s->lhaArc && Lha_Find(s->lhaArc, path) >= 0;
}

const char *SdSources_AmigaSeason(SdSources *s)
{
    char err[256];
    int32_t wasOpen = s->disk1 || s->disk2 || s->hdfDisk || s->lhaArc;
    s_amigaProblem[0] = '\0';
    if (!SdSources_HaveAmiga(s))
        return NULL;
    if (SdSources_Open(s, err) != 0) {
        snprintf(s_amigaProblem, sizeof s_amigaProblem, "%s", err);
        return NULL;
    }
    /* Every file the setup takes from it, or it is some other release: the 1994
     * original's disks (no grafs/DISK22.RAW) look much like 96/97's. */
    const char *missing = NULL;
    for (int32_t i = 0; g_sdAmigaCopy[i] && !missing; i++)
        if (!amiga_has(s, g_sdAmigaCopy[i]))
            missing = g_sdAmigaCopy[i];
    uint64_t f = missing ? 0 : team_fingerprint(s);
    if (!wasOpen)
        SdSources_Close(s);
    if (missing)
        snprintf(s_amigaProblem, sizeof s_amigaProblem,
                 "%s is not in it, so it is not the 96/97 release or an update of it", missing);
    if (!f)
        return NULL;
    for (int32_t i = 0; g_sdAmigaSeasons[i].season; i++)
        if (g_sdAmigaSeasons[i].fingerprint == f)
            return g_sdAmigaSeasons[i].season;
    /* Its fingerprint, for adding it to g_sdAmigaSeasons. */
    snprintf(s_amigaProblem, sizeof s_amigaProblem, "team data fingerprint %016llx",
             (unsigned long long)f);
    return "an unknown season";
}

/* The game's folder in a WHDLoad .lha: the one with data/TEAM.000 and grafs/CJCTEAM1.RAW
 * beside it ("SWOS2425/data/"). 0 when found. */
static int32_t lha_find_game(const char *path, char *dir, uint32_t cap)
{
    char err[256];
    LhaArchive *a = Lha_Open(path, err);
    int32_t rc = -1;
    if (!a)
        return -1;
    for (int32_t i = 0; i < Lha_Count(a) && rc != 0; i++) {
        size_t pre;
        const char *p = Lha_Path(a, i);
        if (!ends_with_path_nocase(p, "data/TEAM.000", &pre) || pre + 32 >= cap)
            continue;
        char probe[300];
        snprintf(probe, sizeof probe, "%.*sgrafs/CJCTEAM1.RAW", (int)pre, p);
        if (Lha_Find(a, probe) >= 0) {
            snprintf(dir, cap, "%.*s", (int)pre, p);
            rc = 0;
        }
    }
    Lha_Free(a);
    return rc;
}

/* How good an Amiga source is: 3 the 96/97 release, 2 another season we know, 1 a season
 * read whole but not one we know, 0 one whose team data cannot be read (a damaged disk). */
static int32_t quality(SdSources *t)
{
    const char *season = SdSources_AmigaSeason(t);
    if (!season)
        return 0;
    if (strcmp(season, "96/97") == 0)
        return 3;
    return strcmp(season, "an unknown season") == 0 ? 1 : 2;
}

/* One Amiga source only, and the best one found: a second replaces the first only when it
 * is better (ASWOS keeps a season an image; a Downloads folder may hold a damaged ADF zip
 * beside a good hard disk image). `kind` 'h' a hard disk image, 'l' a .lha; for a pair of
 * floppies, the caller fills t. 1 = take the new one (the others are cleared). */
static int32_t amiga_better(SdSources *s, SdSources *t)
{
    char problem[256];
    if (!SdSources_HaveAmiga(s)) {
        /* Nothing usable yet: a lone floppy waiting for its other half gives way. */
        s->adf1[0] = s->adf2[0] = '\0';
        return 1;
    }
    int32_t had = quality(s);
    snprintf(problem, sizeof problem, "%s", s_amigaProblem);
    int32_t now = quality(t);
    if (now <= had) {
        snprintf(s_amigaProblem, sizeof s_amigaProblem, "%s", problem);
        return 0;
    }
    s->adf1[0] = s->adf2[0] = s->hdf[0] = s->lha[0] = '\0';
    return 1;
}

static int32_t amiga_take(SdSources *s, char kind, const char *path)
{
    SdSources t;
    if ((kind == 'h' && strcmp(s->hdf, path) == 0) || (kind == 'l' && strcmp(s->lha, path) == 0))
        return 0;
    memset(&t, 0, sizeof t);
    snprintf(kind == 'h' ? t.hdf : t.lha, 1024, "%s", path);
    return amiga_better(s, &t);
}

/* A .zip (a download of disk images): the .adf, .hdf and .lha files in it are unpacked to
 * a folder of our own under the system's temporary folder and added like any other. */
static const char *temp_root(void)
{
    const char *t = getenv("TMPDIR");
#ifdef _WIN32
    if (!t || !*t) t = getenv("TEMP");
    if (!t || !*t) t = getenv("TMP");
#endif
    return t && *t ? t : "/tmp";
}

static int32_t add_zip(SdSources *s, const char *path)
{
    mz_zip_archive z;
    memset(&z, 0, sizeof z);
    if (!mz_zip_reader_init_file(&z, path, 0))
        return -1;
    const char *base = path + strlen(path);
    while (base > path && base[-1] != '/' && base[-1] != '\\')
        base--;
    char dir[1100];
    snprintf(dir, sizeof dir, "%s/" PROJECT_SETUP_PROGRAM "/%s", temp_root(), base);
    int32_t added = 0;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&z); i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z, i, &st) || st.m_is_directory)
            continue;
        const char *name = st.m_filename, *leaf = name + strlen(name);
        while (leaf > name && leaf[-1] != '/' && leaf[-1] != '\\')
            leaf--;
        if (!(ends_with_nocase(leaf, ".adf") || ends_with_nocase(leaf, ".hdf") ||
              ends_with_nocase(leaf, ".lha")) || st.m_uncomp_size > 512u * 1024 * 1024)
            continue;
        char out[1400];
        Sg_MakeDirs(dir);
        snprintf(out, sizeof out, "%s/%s", dir, leaf);
        if (mz_zip_reader_extract_to_file(&z, i, out, 0) && SdSources_AddFile(s, out) == 0)
            added++;
    }
    mz_zip_reader_end(&z);
    return added ? 0 : -1;
}

/* ---- a world's package ------------------------------------------------------- */

typedef struct {
    char    name[260];
    uint8_t isDir;
} Entry;
static Entry *list_dir(const char *dir, int32_t *count);

/* WORLD.ini's text -> info. 0 when it names a world. */
static int32_t world_ini(const char *text, size_t len, SdWorldInfo *info)
{
    memset(info, 0, sizeof *info);
    size_t i = 0;
    while (i < len) {
        char line[256];
        size_t n = 0;
        while (i < len && text[i] != '\n' && n + 1 < sizeof line)
            line[n++] = text[i++];
        while (i < len && text[i] != '\n')
            i++;
        i++;
        line[n] = '\0';
        char *eq = strchr(line, '=');
        if (line[0] == ';' || !eq)
            continue;
        char *k = line, *v = eq + 1, *e = eq;
        while (e > k && (e[-1] == ' ' || e[-1] == '\t')) e--;
        *e = '\0';
        while (*v == ' ' || *v == '\t') v++;
        e = v + strlen(v);
        while (e > v && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) e--;
        *e = '\0';
        if (strcmp(k, "tag") == 0) snprintf(info->tag, sizeof info->tag, "%s", v);
        else if (strcmp(k, "name") == 0) snprintf(info->name, sizeof info->name, "%s", v);
        else if (strcmp(k, "fingerprint") == 0) snprintf(info->fingerprint, sizeof info->fingerprint, "%s", v);
    }
    for (const char *t = info->tag; *t; t++)
        if (!isalnum((unsigned char)*t))
            return -1;
    return info->tag[0] ? 0 : -1;
}

/* The folder holding WORLD.ini: `dir` itself or a folder in it. */
static int32_t world_root(const char *dir, char *out, uint32_t cap)
{
    char probe[1400];
    if (Sg_FindNoCase(dir, "WORLD.ini", probe, sizeof probe) == 0) {
        snprintf(out, cap, "%s", dir);
        return 0;
    }
    int32_t n;
    Entry *list = list_dir(dir, &n);
    int32_t rc = -1;
    for (int32_t i = 0; i < n && rc; i++) {
        char sub[1400];
        if (!list[i].isDir)
            continue;
        Sg_PathJoin(sub, sizeof sub, dir, list[i].name);
        if (Sg_FindNoCase(sub, "WORLD.ini", probe, sizeof probe) == 0) {
            snprintf(out, cap, "%s", sub);
            rc = 0;
        }
    }
    free(list);
    return rc;
}

/* A .zip with a WORLD.ini at its top or one folder down: that entry's text. */
static int32_t zip_world_ini(mz_zip_archive *z, SdWorldInfo *info)
{
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(z); i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(z, i, &st) || st.m_is_directory || st.m_uncomp_size > 4096)
            continue;
        const char *name = st.m_filename, *leaf = name + strlen(name);
        int32_t depth = 0;
        for (const char *c = name; *c; c++)
            depth += *c == '/' || *c == '\\';
        while (leaf > name && leaf[-1] != '/' && leaf[-1] != '\\')
            leaf--;
        if (depth > 1 || !(tolower((unsigned char)leaf[0]) == 'w' && ends_with_nocase(leaf, "world.ini") &&
                           strlen(leaf) == 9))
            continue;
        size_t len = 0;
        char *text = (char *)mz_zip_reader_extract_to_heap(z, i, &len, 0);
        int32_t rc = text ? world_ini(text, len, info) : -1;
        mz_free(text);
        return rc;
    }
    return -1;
}

static int32_t unpack_zip(mz_zip_archive *z, const char *dir)
{
    int32_t ok = 1;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(z) && ok; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(z, i, &st))
            continue;
        const char *name = st.m_filename;
        /* Its own folders only: no entry may climb out of the one it is unpacked to. */
        if (name[0] == '/' || name[0] == '\\' || strchr(name, ':') || strstr(name, ".."))
            continue;
        char out[2048];
        snprintf(out, sizeof out, "%s/%s", dir, name);
        if (st.m_is_directory) {
            Sg_MakeDirs(out);
            continue;
        }
        char parent[2048];
        snprintf(parent, sizeof parent, "%s", out);
        char *slash = strrchr(parent, '/');
        if (slash) {
            *slash = '\0';
            Sg_MakeDirs(parent);
        }
        ok = mz_zip_reader_extract_to_file(z, i, out, 0);
    }
    return ok ? 0 : -1;
}

int32_t SdSources_OpenWorld(const char *path, SdWorldInfo *info, char *dir, uint32_t cap)
{
    char root[1400];
    if (is_dir(path)) {
        if (world_root(path, root, sizeof root) != 0)
            return -1;
    } else if (ends_with_nocase(path, ".zip")) {
        mz_zip_archive z;
        memset(&z, 0, sizeof z);
        if (!mz_zip_reader_init_file(&z, path, 0))
            return -1;
        int32_t rc = zip_world_ini(&z, info);
        if (rc == 0 && dir) {
            const char *base = path + strlen(path);
            while (base > path && base[-1] != '/' && base[-1] != '\\')
                base--;
            char tmp[1400];
            snprintf(tmp, sizeof tmp, "%s/" PROJECT_SETUP_PROGRAM "/%s", temp_root(), base);
            rc = unpack_zip(&z, tmp) == 0 && world_root(tmp, root, sizeof root) == 0 ? 0 : -1;
        }
        mz_zip_reader_end(&z);
        if (rc != 0)
            return -1;
        if (!dir)
            return 0;
    } else {
        return -1;
    }
    char ini[1500];
    if (Sg_FindNoCase(root, "WORLD.ini", ini, sizeof ini) != 0)
        return -1;
    SgBuf b;
    char err[256];
    if (Sg_ReadFile(ini, &b, err) != 0)
        return -1;
    int32_t rc = world_ini((const char *)b.data, b.size, info);
    Sg_BufFree(&b);
    if (rc == 0 && dir)
        snprintf(dir, cap, "%s", root);
    return rc;
}

int32_t SdSources_UnpackAmigaProgram(const char *zip, char *dir, uint32_t cap)
{
    mz_zip_archive z;
    memset(&z, 0, sizeof z);
    if (!mz_zip_reader_init_file(&z, zip, 0))
        return -1;
    const char *base = zip + strlen(zip);
    while (base > zip && base[-1] != '/' && base[-1] != '\\')
        base--;
    snprintf(dir, cap, "%s/" PROJECT_SETUP_PROGRAM "/%s", temp_root(), base);
    int32_t ok = 1;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&z) && ok; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z, i, &st))
            continue;
        const char *name = st.m_filename;
        /* Its own folders only: no entry may climb out of the one it is unpacked to. */
        if (name[0] == '/' || name[0] == '\\' || strchr(name, ':') || strstr(name, ".."))
            continue;
        char out[1400];
        snprintf(out, sizeof out, "%s/%s", dir, name);
        if (st.m_is_directory) {
            Sg_MakeDirs(out);
            continue;
        }
        char parent[1400];
        snprintf(parent, sizeof parent, "%s", out);
        char *slash = strrchr(parent, '/');
        if (slash) {
            *slash = '\0';
            Sg_MakeDirs(parent);
        }
        ok = mz_zip_reader_extract_to_file(&z, i, out, 0);
    }
    mz_zip_reader_end(&z);
    char probe[1400];
    snprintf(probe, sizeof probe, "%s/SWOS/SWOS2", dir);
    return ok && Sg_IsFile(probe) ? 0 : -1;
}

int32_t SdSources_AddFile(SdSources *s, const char *path)
{
    char err[256];
    if (ends_with_nocase(path, ".hdf")) {
        int32_t vol;
        char dir[256];
        if (hdf_find_game(path, &vol, dir, sizeof dir) != 0)
            return -1;
        if (amiga_take(s, 'h', path))
            snprintf(s->hdf, sizeof s->hdf, "%s", path);
        return 0;
    }
    if (ends_with_nocase(path, ".lha") || ends_with_nocase(path, ".lzh")) {
        char dir[256];
        if (lha_find_game(path, dir, sizeof dir) != 0)
            return -1;
        if (amiga_take(s, 'l', path))
            snprintf(s->lha, sizeof s->lha, "%s", path);
        return 0;
    }
    if (ends_with_nocase(path, ".zip")) {
        SdWorldInfo w;
        if (SdSources_OpenWorld(path, &w, NULL, 0) == 0) {
            if (!s->world[0])
                snprintf(s->world, sizeof s->world, "%s", path);
            return 0;
        }
        return add_zip(s, path);
    }
    if (ends_with_nocase(path, ".adf")) {
        Adf *a = Adf_Open(path, err);
        if (!a)
            return -1;
        int32_t rc = -1;
        char p[1024];
        const char *vol = Adf_VolumeName(a);
        uint32_t n;
        /* Disk 1 holds the game, disk 2 the rest of the data. Volume names
         * first; a renamed disk still has its files (a cracked release calls its
         * disk 2 "HOODLUM DISK 2": the teams and the menu pictures, no program). */
        uint8_t *probe = Adf_ReadFile(a, "SWOS2", &n, err);
        int32_t looks1 = probe != NULL || strcmp(vol, "SWOS") == 0;
        free(probe);
        int32_t looks2 = !looks1 && (strcmp(vol, "SWOS2") == 0 ||
                                     (Adf_FileSize(a, "data/TEAM.000") >= 0 &&
                                      Adf_FileSize(a, "grafs/CJCTEAM1.RAW") >= 0));
        char *slot = looks1 ? s->adf1 : looks2 ? s->adf2 : NULL;
        if (slot && !slot[0]) {
            const char *other = looks1 ? s->adf2 : s->adf1;
            /* This disk completes a pair while a hard disk image or a .lha is the
             * Amiga source: the pair only if it is the better one. */
            if (other[0] && (s->hdf[0] || s->lha[0])) {
                SdSources t;
                memset(&t, 0, sizeof t);
                snprintf(looks1 ? t.adf1 : t.adf2, 1024, "%s", path);
                snprintf(looks1 ? t.adf2 : t.adf1, 1024, "%s", other);
                if (!amiga_better(s, &t)) {
                    /* Not the better: neither of the pair stays, so no lone disk of it
                     * is ever read beside the source that won. */
                    (looks1 ? s->adf2 : s->adf1)[0] = '\0';
                    Adf_Close(a);
                    return 0;
                }
                /* amiga_better cleared the old source, the other disk with it: back. */
                snprintf(looks1 ? s->adf2 : s->adf1, 1024, "%s", looks1 ? t.adf2 : t.adf1);
            }
            snprintf(slot, 1024, "%s", path);
            rc = 0;
        }
        (void)p;
        Adf_Close(a);
        return rc;
    }
    if (is_jag_rom(path)) {
        if (!s->jagRom[0])
            snprintf(s->jagRom, sizeof s->jagRom, "%s", path);
        return 0;
    }
    if (is_dir(path)) {
        if (is_dos_dir(path)) {
            if (!s->dosDir[0])
                snprintf(s->dosDir, sizeof s->dosDir, "%s", path);
            return 0;
        }
        char game[1024];
        if (find_s2020(path, game, sizeof game) == 0) {
            if (!s->s2020Dir[0])
                snprintf(s->s2020Dir, sizeof s->s2020Dir, "%s", game);
            return 0;
        }
        SdWorldInfo w;
        if (SdSources_OpenWorld(path, &w, game, sizeof game) == 0) {
            if (!s->world[0])
                snprintf(s->world, sizeof s->world, "%s", game);
            return 0;
        }
        return -1;
    }
    /* A file inside the PC game folder or SWOS 2020's: take its folder. */
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    char *bslash = strrchr(dir, '\\');
    if (bslash && (!slash || bslash > slash))
        slash = bslash;
    if (slash) {
        *slash = '\0';
        if (is_dos_dir(dir)) {
            if (!s->dosDir[0])
                snprintf(s->dosDir, sizeof s->dosDir, "%s", dir);
            return 0;
        }
        char game[1024];
        if (find_s2020(dir, game, sizeof game) == 0) {
            if (!s->s2020Dir[0])
                snprintf(s->s2020Dir, sizeof s->s2020Dir, "%s", game);
            return 0;
        }
        SdWorldInfo w;               /* (its WORLD.ini, or a file beside it) */
        if (SdSources_OpenWorld(dir, &w, game, sizeof game) == 0 && strcmp(game, dir) == 0) {
            if (!s->world[0])
                snprintf(s->world, sizeof s->world, "%s", game);
            return 0;
        }
    }
    return -1;
}

static int32_t count_found(const SdSources *s)
{
    return (s->adf1[0] != 0) + (s->adf2[0] != 0) + (s->hdf[0] != 0) + (s->lha[0] != 0) +
           (s->dosDir[0] != 0) + (s->jagRom[0] != 0) + (s->s2020Dir[0] != 0) + (s->world[0] != 0);
}

/* ---- the folder search ---------------------------------------------------- */

#define SCAN_MAX_DEPTH 24       /* deeper than any real install; a guard, not a limit */

typedef struct {
    SdScanProgress progress;
    void          *user;
    int32_t        folders;
    int32_t        stopped;
} ScanCtx;

/* Everything there is to find. An Amiga hard disk image does not end the search while
 * it is not the 96/97 release: there may be one that is, further on. */
static int32_t all_found(const SdSources *s)
{
    return SdSources_HaveAmiga(s) && (s->adf1[0] && s->adf2[0]) && s->dosDir[0] && s->jagRom[0] &&
           s->s2020Dir[0] && s->world[0];
}

static int32_t contains_nocase(const char *s, const char *word)
{
    size_t lw = strlen(word);
    for (; *s; s++) {
        size_t i = 0;
        while (i < lw && s[i] && tolower((unsigned char)s[i]) == word[i])
            i++;
        if (i == lw)
            return 1;
    }
    return 0;
}

/* Folders no copy of the game is in: hidden ones, and Windows' own. */
static int32_t skip_folder(const char *name)
{
    return name[0] == '.' || name[0] == '$' ||
           contains_nocase(name, "system volume information");
}

/* A file met on the way. An .adf is opened only when its volume name is one
 * of the game's disks, or its file name says it is the game (a renamed disk):
 * a folder of a thousand other Amiga games costs a thousand root blocks. */
static void scan_file(SdSources *s, const char *path, const char *name)
{
    if (ends_with_nocase(name, ".hdf") || ends_with_nocase(name, ".lha") || ends_with_nocase(name, ".zip")) {
        /* Only an image named for the game: a folder of other Amiga hard disks is not
         * opened one by one. */
        if (!contains_nocase(name, "swos") && !contains_nocase(name, "sensible"))
            return;
    } else if (ends_with_nocase(name, ".adf")) {
        char vol[32];
        if (s->adf1[0] && s->adf2[0])
            return;
        if (Adf_PeekVolumeName(path, vol) != 0)
            return;
        if (strcmp(vol, "SWOS") != 0 && strcmp(vol, "SWOS2") != 0 &&
            !contains_nocase(name, "swos") && !contains_nocase(name, "sensible"))
            return;
    } else if (!(ends_with_nocase(name, ".jag") || ends_with_nocase(name, ".j64") ||
                 ends_with_nocase(name, ".rom") || ends_with_nocase(name, ".bin")) || s->jagRom[0]) {
        return;
    }
    SdSources_AddFile(s, path);
}


/* One folder's entries, links left out (a junction or symbolic link could lead
 * back up, so it is never followed). malloc'd; *count entries. */
static Entry *list_dir(const char *dir, int32_t *count)
{
    Entry *list = NULL;
    int32_t n = 0, cap = 0;
#ifdef _WIN32
    char pattern[1100];
    Sg_PathJoin(pattern, sizeof pattern, dir, "*");
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        *count = 0;
        return NULL;
    }
    do {
        const char *name = fd.cFileName;
        int32_t link = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        int32_t isDirectory = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    DIR *d = opendir(dir);
    if (!d) {
        *count = 0;
        return NULL;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        const char *name = e->d_name;
        char full[1100];
        struct stat st;
        Sg_PathJoin(full, sizeof full, dir, name);
        if (lstat(full, &st) != 0)
            continue;
        int32_t link = S_ISLNK(st.st_mode);
        int32_t isDirectory = S_ISDIR(st.st_mode);
#endif
        if (!link && strcmp(name, ".") != 0 && strcmp(name, "..") != 0 && strlen(name) < sizeof list->name) {
            if (n == cap) {
                cap = cap ? cap * 2 : 64;
                Entry *grown = realloc(list, sizeof *list * (size_t)cap);
                if (!grown)
                    break;
                list = grown;
            }
            snprintf(list[n].name, sizeof list[n].name, "%s", name);
            list[n].isDir = (uint8_t)isDirectory;
            n++;
        }
#ifdef _WIN32
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    }
    closedir(d);
#endif
    *count = n;
    return list;
}

static int32_t has_entry(const Entry *list, int32_t n, const char *name, int32_t isDir)
{
    for (int32_t i = 0; i < n; i++) {
        if (list[i].isDir != isDir)
            continue;
        const char *a = list[i].name, *b = name;
        while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b))
            a++, b++;
        if (!*a && !*b)
            return 1;
    }
    return 0;
}

static void scan_dir(SdSources *s, const char *dir, int32_t depth, ScanCtx *ctx)
{
    if (ctx->stopped || all_found(s))
        return;
    ctx->folders++;
    if (ctx->progress && ctx->progress(ctx->user, ctx->folders, dir) != 0) {
        ctx->stopped = 1;
        return;
    }
    int32_t n;
    Entry *list = list_dir(dir, &n);

    /* The game's own folders are found whole; nothing else is inside them (and
     * SWOS 2020's holds PC graphics of its own, which are not the PC game). The
     * full checks run only where the listing hints at one, so a folder is
     * listed once, not once a check. */
    int32_t gameDir = 0;
    if (has_entry(list, n, "SPRITE.DAT", 0) && has_entry(list, n, "DATA", 1) && is_dos_dir(dir)) {
        if (!s->dosDir[0])
            snprintf(s->dosDir, sizeof s->dosDir, "%s", dir);
        gameDir = 1;
    } else if (has_entry(list, n, "WORLD.ini", 0) && has_entry(list, n, "competitions", 1)) {
        SdWorldInfo w;               /* (an unpacked world package) */
        char root[1024];
        if (SdSources_OpenWorld(dir, &w, root, sizeof root) == 0) {
            if (!s->world[0])
                snprintf(s->world, sizeof s->world, "%s", root);
            gameDir = 1;
        }
    } else if (has_entry(list, n, "swos", 1) || has_entry(list, n, "swos-port-Release.exe", 0)) {
        char game[1024];
        if (find_s2020(dir, game, sizeof game) == 0) {
            if (!s->s2020Dir[0])
                snprintf(s->s2020Dir, sizeof s->s2020Dir, "%s", game);
            gameDir = 1;
        }
    }
    for (int32_t i = 0; i < n && !ctx->stopped && !all_found(s); i++) {
        char p[1100];
        Sg_PathJoin(p, sizeof p, dir, list[i].name);
        if (!list[i].isDir)
            scan_file(s, p, list[i].name);
        else if (!gameDir && depth < SCAN_MAX_DEPTH && !skip_folder(list[i].name))
            scan_dir(s, p, depth + 1, ctx);
    }
    free(list);
}

int32_t SdSources_ScanEx(SdSources *s, const char *dir, SdScanProgress progress, void *user)
{
    ScanCtx ctx = { progress, user, 0, 0 };
    scan_dir(s, dir, 0, &ctx);
    return ctx.stopped ? -1 : count_found(s);
}

int32_t SdSources_Scan(SdSources *s, const char *dir)
{
    return SdSources_ScanEx(s, dir, NULL, NULL);
}

int32_t SdSources_HaveAmiga(const SdSources *s)
{
    return (s->adf1[0] && s->adf2[0]) || s->hdf[0] || s->lha[0];
}
int32_t SdSources_HaveDos(const SdSources *s) { return s->dosDir[0] != 0; }
int32_t SdSources_HaveJaguar(const SdSources *s) { return s->jagRom[0] != 0; }
int32_t SdSources_HaveSwos2020(const SdSources *s) { return s->s2020Dir[0] != 0; }
int32_t SdSources_HaveWorld(const SdSources *s) { return s->world[0] != 0; }

int32_t SdSources_Open(SdSources *s, char *err)
{
    /* The floppies only as a pair: a lone disk is never read beside another source. */
    if (s->adf1[0] && s->adf2[0]) {
        if (!s->disk1 && !(s->disk1 = Adf_Open(s->adf1, err)))
            return -1;
        if (!s->disk2 && !(s->disk2 = Adf_Open(s->adf2, err)))
            return -1;
    }
    /* The hard disk image: only when there is no pair of floppies to read instead. */
    if (s->hdf[0] && !(s->adf1[0] && s->adf2[0]) && !s->hdfDisk) {
        if (hdf_find_game(s->hdf, &s->hdfVolume, s->hdfDir, sizeof s->hdfDir) != 0) {
            snprintf(err, 256, "%.200s: the game is not on it", s->hdf);
            return -1;
        }
        if (!(s->hdfDisk = Adf_OpenVolume(s->hdf, s->hdfVolume, err)))
            return -1;
    }
    /* A WHDLoad .lha: only when there is neither of those. */
    if (s->lha[0] && !(s->adf1[0] && s->adf2[0]) && !s->hdf[0] && !s->lhaArc) {
        if (lha_find_game(s->lha, s->lhaDir, sizeof s->lhaDir) != 0) {
            snprintf(err, 256, "%.200s: the game is not in it", s->lha);
            return -1;
        }
        if (!(s->lhaArc = Lha_Open(s->lha, err)))
            return -1;
    }
    return 0;
}

void SdSources_Close(SdSources *s)
{
    Adf_Close(s->disk1);
    Adf_Close(s->disk2);
    Adf_Close(s->hdfDisk);
    Lha_Free(s->lhaArc);
    s->disk1 = s->disk2 = s->hdfDisk = NULL;
    s->lhaArc = NULL;
}

int32_t SdSources_ReadAmiga(SdSources *s, const char *rel, SgBuf *out, char *err)
{
    Adf *disks[2] = { s->disk1, s->disk2 };
    char why[256] = "";
    for (int32_t i = 0; i < 2; i++) {
        if (!disks[i])
            continue;
        uint32_t n;
        char e2[256];
        uint8_t *d = Adf_ReadFile(disks[i], rel, &n, e2);
        if (d) {
            out->data = d;
            out->size = n;
            return 0;
        }
        if (strstr(e2, "damaged"))
            snprintf(why, sizeof why, "%.200s (disk %d)", e2, (int)i + 1);
    }
    if (why[0]) {
        snprintf(err, 256, "%s", why);
        return -1;
    }
    if (s->hdfDisk) {
        uint32_t n;
        char path[512];
        snprintf(path, sizeof path, "%s%s", s->hdfDir, rel);
        uint8_t *d = Adf_ReadFile(s->hdfDisk, path, &n, err);
        if (d) {
            out->data = d;
            out->size = n;
            return 0;
        }
    }
    if (s->lhaArc) {
        char path[512];
        snprintf(path, sizeof path, "%s%s", s->lhaDir, rel);
        int32_t i = Lha_Find(s->lhaArc, path);
        uint32_t n;
        uint8_t *d = i >= 0 ? Lha_Read(s->lhaArc, i, &n, err) : NULL;
        if (d) {
            out->data = d;
            out->size = n;
            return 0;
        }
    }
    snprintf(err, 256, "%s is not on the Amiga disks", rel);
    return -1;
}

int32_t SdSources_ReadDos(SdSources *s, const char *rel, SgBuf *out, char *err)
{
    char p[1024];
    if (!s->dosDir[0] || Sg_FindNoCase(s->dosDir, rel, p, sizeof p) != 0) {
        snprintf(err, 256, "%s is not in the PC game folder", rel);
        return -1;
    }
    return Sg_ReadFile(p, out, err);
}
