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
#include "fat_image.h"
#include "rnc_archive.h"

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

/* A .zip (a download): what of the game is in it is unpacked to a folder of our own under the
 * system's temporary folder and added like any other -- the Amiga's .adf, .hdf and .lha files, an
 * installed PC game (archive.org's has one, beside the CD's image), and an image of the PC CD
 * (.iso, .img, .bin: the commentary). */
static const char *temp_root(void)
{
    const char *t = getenv("TMPDIR");
#ifdef _WIN32
    if (!t || !*t) t = getenv("TEMP");
    if (!t || !*t) t = getenv("TMP");
#endif
    return t && *t ? t : "/tmp";
}

static int32_t starts_with_nocase(const char *s, const char *prefix)
{
    for (; *prefix; s++, prefix++)
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix))
            return 0;
    return 1;
}

/* An entry that stays inside the folder it is unpacked to, and is not a Mac's own (__MACOSX/,
 * the "._" files beside every file a Mac zips). */
static int32_t zip_entry_ok(const char *name, const char *leaf)
{
    return name[0] != '/' && name[0] != '\\' && !strchr(name, ':') && !strstr(name, "..") &&
           !starts_with_nocase(name, "__MACOSX/") && strncmp(leaf, "._", 2) != 0;
}

/* One entry to `out`, its folders made -- not again when it is there already, the same size
 * (a CD's image is hundreds of MB, and a download is offered more than once). */
static int32_t zip_extract(mz_zip_archive *z, mz_uint i, const mz_zip_archive_file_stat *st, const char *out)
{
    if ((uint64_t)file_size(out) == st->m_uncomp_size)
        return 0;
    char parent[2200];
    snprintf(parent, sizeof parent, "%s", out);
    char *slash = strrchr(parent, '/');
    if (slash) {
        *slash = '\0';
        Sg_MakeDirs(parent);
    }
    return mz_zip_reader_extract_to_file(z, i, out, 0) ? 0 : -1;
}

/* The folder of a PC game installed in the zip: the one with SPRITE.DAT and DATA/TEAM.000 (and not
 * SWOS 2020's, which has both). Its path in the zip ("SensiWs9/SOC/", "" for the top). */
static int32_t zip_dos_prefix(mz_zip_archive *z, char *prefix, uint32_t cap)
{
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(z); i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(z, i, &st) || st.m_is_directory)
            continue;
        const char *name = st.m_filename, *leaf = name + strlen(name);
        while (leaf > name && leaf[-1] != '/' && leaf[-1] != '\\')
            leaf--;
        if (!ends_with_nocase(leaf, "SPRITE.DAT") || strlen(leaf) != 10 || !zip_entry_ok(name, leaf))
            continue;
        char probe[600];
        snprintf(probe, sizeof probe, "%.*sDATA/TEAM.000", (int)(leaf - name), name);
        if (mz_zip_reader_locate_file(z, probe, NULL, 0) < 0)       /* (any case) */
            continue;
        snprintf(probe, sizeof probe, "%.*sswos-port-Release.exe", (int)(leaf - name), name);
        if (mz_zip_reader_locate_file(z, probe, NULL, 0) >= 0)
            continue;
        snprintf(prefix, cap, "%.*s", (int)(leaf - name), name);
        return 0;
    }
    return -1;
}

#define ZIP_MAX_IMAGES 8

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
    char dosPrefix[512];
    const int32_t haveDos = zip_dos_prefix(&z, dosPrefix, sizeof dosPrefix) == 0;
    const size_t dosLen = haveDos ? strlen(dosPrefix) : 0;
    char images[ZIP_MAX_IMAGES][2200], floppies[4][2200];
    int32_t nImages = 0, nFloppies = 0, added = 0, dosOk = haveDos;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&z); i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z, i, &st) || st.m_is_directory)
            continue;
        const char *name = st.m_filename, *leaf = name + strlen(name);
        while (leaf > name && leaf[-1] != '/' && leaf[-1] != '\\')
            leaf--;
        if (!zip_entry_ok(name, leaf))
            continue;
        char out[2200];
        /* One of the PC game's floppies (added once all are out: they go in pairs). */
        if ((ends_with_nocase(leaf, ".img") || ends_with_nocase(leaf, ".ima")) &&
            st.m_uncomp_size <= 4u * 1024 * 1024 && nFloppies < 4 &&
            !(haveDos && starts_with_nocase(name, dosPrefix))) {
            snprintf(out, sizeof out, "%s/%s", dir, leaf);
            if (zip_extract(&z, i, &st, out) == 0)
                snprintf(floppies[nFloppies++], sizeof floppies[0], "%s", out);
            continue;
        }
        /* An image of the PC game's CD (a CloneCD .img, an .iso, a raw .bin; a .cue is not
         * needed: the track is found in the image itself). */
        if ((ends_with_nocase(leaf, ".iso") || ends_with_nocase(leaf, ".img") || ends_with_nocase(leaf, ".bin")) &&
            st.m_uncomp_size > 8u * 1024 * 1024 && st.m_uncomp_size <= 1024u * 1024 * 1024 &&
            nImages < ZIP_MAX_IMAGES) {
            snprintf(out, sizeof out, "%s/cd/%s", dir, leaf);
            if (zip_extract(&z, i, &st, out) == 0)
                snprintf(images[nImages++], sizeof images[0], "%s", out);
            continue;
        }
        /* The installed PC game: the whole of its folder. */
        if (haveDos && starts_with_nocase(name, dosPrefix)) {
            snprintf(out, sizeof out, "%s/pc/%s", dir, name + dosLen);
            if (zip_extract(&z, i, &st, out) != 0)
                dosOk = 0;
            continue;
        }
        if (!(ends_with_nocase(leaf, ".adf") || ends_with_nocase(leaf, ".hdf") ||
              ends_with_nocase(leaf, ".lha")) || st.m_uncomp_size > 512u * 1024 * 1024)
            continue;
        snprintf(out, sizeof out, "%s/%s", dir, leaf);
        Sg_MakeDirs(dir);
        if (mz_zip_reader_extract_to_file(&z, i, out, 0) && SdSources_AddFile(s, out) == 0)
            added++;
    }
    mz_zip_reader_end(&z);
    if (dosOk) {
        char pc[1200];
        snprintf(pc, sizeof pc, "%s/pc", dir);
        added += SdSources_AddFile(s, pc) == 0;
    }
    for (int32_t i = 0; i < nFloppies; i++)
        added += SdSources_AddFile(s, floppies[i]) == 0;
    for (int32_t i = 0; i < nImages; i++)
        added += SdSources_AddDosCd(s, images[i]) == 0;
    return added ? 0 : -1;
}

/* ---- a world's package ------------------------------------------------------- */

static void take_world(SdSources *s, const char *path);   /* (below) */

void SdSources_AddWorld(SdSources *s, const char *path)
{
    take_world(s, path);
}

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

int32_t SdSources_Unzip(const char *zip, const char *dir)
{
    mz_zip_archive z;
    memset(&z, 0, sizeof z);
    if (!mz_zip_reader_init_file(&z, zip, 0))
        return -1;
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
    return ok ? 0 : -1;
}

int32_t SdSources_UnpackAmigaProgram(const char *zip, char *dir, uint32_t cap)
{
    const char *base = zip + strlen(zip);
    while (base > zip && base[-1] != '/' && base[-1] != '\\')
        base--;
    snprintf(dir, cap, "%s/" PROJECT_SETUP_PROGRAM "/%s", temp_root(), base);
    char probe[1400];
    snprintf(probe, sizeof probe, "%s/SWOS/SWOS2", dir);
    return SdSources_Unzip(zip, dir) == 0 && Sg_IsFile(probe) ? 0 : -1;
}

int32_t SdSources_ZipHas(const char *zip, const char *name)
{
    mz_zip_archive z;
    memset(&z, 0, sizeof z);
    if (!mz_zip_reader_init_file(&z, zip, 0))
        return 0;
    const int found = mz_zip_reader_locate_file(&z, name, NULL, 0) >= 0;
    mz_zip_reader_end(&z);
    return found;
}

static int32_t has_commentary_dir(const char *dir);
static int32_t is_cd_image_name(const char *path);
static int32_t find_dos_at(const char *root, CdImage *cd, const char *rel, char *p, uint32_t cap);

/* ---- the PC game's floppies ---------------------------------------------- */

static int32_t is_floppy_name(const char *path)
{
    return ends_with_nocase(path, ".img") || ends_with_nocase(path, ".ima") || ends_with_nocase(path, ".vfd");
}

/* Which disk of the PC game's floppy release an image is: 1 (SWOS_1.RNC, the archive's start),
 * 2 (SWOS_2.RNC), 0 neither. Told apart by what is on them, never by their names. */
static int32_t dos_floppy_disk(const char *path)
{
    char err[256];
    long n = file_size(path);
    if (n <= 0 || n > 4 * 1024 * 1024)
        return 0;
    FatImage *f = FatImage_Open(path, err);
    if (!f)
        return 0;
    int32_t disk = 0;
    SgBuf b = { 0 };
    if (FatImage_Has(f, "SWOS_1.RNC") && FatImage_Read(f, "SWOS_1.RNC", &b, err) == 0)
        disk = RncArchive_Is(b.data, b.size) ? 1 : 0;
    else if (FatImage_Has(f, "SWOS_2.RNC"))
        disk = 2;
    Sg_BufFree(&b);
    FatImage_Close(f);
    return disk;
}

/* The game off both disks into a folder of the setup's own under the system's temporary folder,
 * once: that folder, a PC game folder, in `out`. */
static int32_t floppy_game(const char *disk1, const char *disk2, char *out, uint32_t cap)
{
    char err[256], done[1200];
    const char *base = disk1 + strlen(disk1);
    while (base > disk1 && base[-1] != '/' && base[-1] != '\\')
        base--;
    snprintf(out, cap, "%s/" PROJECT_SETUP_PROGRAM "/%s.game", temp_root(), base);
    snprintf(done, sizeof done, "%s/.copied", out);          /* (a copy cut short is made again) */
    if (Sg_IsFile(done))
        return is_dos_dir(out) ? 0 : -1;
    FatImage *f1 = FatImage_Open(disk1, err), *f2 = f1 ? FatImage_Open(disk2, err) : NULL;
    SgBuf a = { 0 }, b = { 0 };
    int32_t rc = -1, files = 0;
    if (f2 && FatImage_Read(f1, "SWOS_1.RNC", &a, err) == 0 && FatImage_Read(f2, "SWOS_2.RNC", &b, err) == 0) {
        uint8_t *joined = malloc((size_t)a.size + b.size);
        if (joined) {
            memcpy(joined, a.data, a.size);
            memcpy(joined + a.size, b.data, b.size);
            rc = RncArchive_Extract(joined, a.size + b.size, out, &files, err) == 0 &&
                 Sg_WriteFile(done, "", 0, err) == 0 && is_dos_dir(out) ? 0 : -1;
            free(joined);
        }
    }
    Sg_BufFree(&a);
    Sg_BufFree(&b);
    FatImage_Close(f1);
    FatImage_Close(f2);
    return rc;
}

static char s_dosProblem[256];
const char *SdSources_DosProblem(void) { return s_dosProblem; }

/* The files the setup cannot do without (sd_manifest.c's PC list less the sounds a release may
 * not have) that `dir` has not: how many, the first in `first`. */
static int32_t dos_missing(const char *dir, char *first, uint32_t cap)
{
    int32_t n = 0;
    first[0] = '\0';
    for (int32_t i = 0; g_sdDosCopy[i].dest; i++) {
        const char *d = g_sdDosCopy[i].dest;
        if (SdManifest_DosOptional(d))
            continue;
        char p[1100];
        if (find_dos_at(dir, NULL, g_sdDosCopy[i].src, p, sizeof p) == 0)
            continue;
        int32_t again = 0;                  /* (a file converted twice is one file) */
        for (int32_t j = 0; j < i && !again; j++)
            again = strcmp(g_sdDosCopy[j].src, g_sdDosCopy[i].src) == 0;
        if (again)
            continue;
        if (!n++)
            snprintf(first, cap, "%s", g_sdDosCopy[i].src);
    }
    return n;
}

int32_t SdSources_DosFloppies(SdSources *s)
{
    if (s->dosDir[0])
        return 0;
    if (!s->dosFloppy1[0] || !s->dosFloppy2[0])
        return -1;
    char pc[sizeof s->dosDir];
    s_dosProblem[0] = '\0';
    if (floppy_game(s->dosFloppy1, s->dosFloppy2, pc, sizeof pc) != 0) {
        snprintf(s_dosProblem, sizeof s_dosProblem, "the game cannot be unpacked off the floppies (damaged disks?)");
        return -1;
    }
    /* The 96/97 floppies are an UPGRADE: they go over an installed SWOS and leave out what it
     * already has (its title, stadium and other pictures, the menu music). */
    char first[300];
    const int32_t missing = dos_missing(pc, first, sizeof first);
    if (missing) {
        snprintf(s_dosProblem, sizeof s_dosProblem,
                 "these floppies are the 96/97 upgrade, which goes over an installed SWOS: %d of the game's "
                 "files are not on them (%.60s ...)", (int)missing, first);
        return -1;
    }
    snprintf(s->dosDir, sizeof s->dosDir, "%s", pc);
    return 0;
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
            take_world(s, path);
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
    if (is_floppy_name(path) && !is_dir(path)) {
        const int32_t disk = dos_floppy_disk(path);
        if (disk) {
            char *slot = disk == 1 ? s->dosFloppy1 : s->dosFloppy2;
            if (!slot[0])
                snprintf(slot, 1024, "%s", path);
            SdSources_DosFloppies(s);
            return 0;
        }
    }
    if (is_cd_image_name(path) && !is_dir(path))
        return SdSources_AddDosCd(s, path);
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
            take_world(s, game);
            return 0;
        }
        if (has_commentary_dir(path))       /* the PC game's CD, or its sounds */
            return SdSources_AddDosCd(s, path);
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
            take_world(s, game);
            return 0;
        }
        if (has_commentary_dir(dir))
            return SdSources_AddDosCd(s, dir);
    }
    return -1;
}

/* A world's package found: the first one, or another world's (by its WORLD.ini tag) beside it --
 * one package a world, the first found of each kept. */
static void take_world(SdSources *s, const char *path)
{
    SdWorldInfo w, o;
    if (!s->world[0]) {
        snprintf(s->world, sizeof s->world, "%s", path);
        return;
    }
    if (SdSources_OpenWorld(path, &w, NULL, 0) != 0)
        return;
    if (strcmp(s->world, path) == 0 ||
        (SdSources_OpenWorld(s->world, &o, NULL, 0) == 0 && strcmp(o.tag, w.tag) == 0))
        return;
    for (int32_t i = 0; i < SD_WORLDS_MORE; i++) {
        if (!s->worldMore[i][0]) {
            snprintf(s->worldMore[i], sizeof s->worldMore[i], "%s", path);
            return;
        }
        if (strcmp(s->worldMore[i], path) == 0 ||
            (SdSources_OpenWorld(s->worldMore[i], &o, NULL, 0) == 0 && strcmp(o.tag, w.tag) == 0))
            return;
    }
}

static int32_t count_found(const SdSources *s)
{
    return (s->adf1[0] != 0) + (s->adf2[0] != 0) + (s->hdf[0] != 0) + (s->lha[0] != 0) +
           (s->dosDir[0] != 0) + (s->dosCd[0] != 0) + (s->dosFloppy1[0] != 0) + (s->dosFloppy2[0] != 0) +
           (s->jagRom[0] != 0) + (s->s2020Dir[0] != 0) +
           (s->world[0] != 0);
}

/* ---- the folder search ---------------------------------------------------- */

#define SCAN_MAX_DEPTH 24       /* deeper than any real install; a guard, not a limit */

typedef struct {
    SdScanProgress progress;
    void          *user;
    int32_t        folders;
    int32_t        stopped;
    int32_t        maxDepth;   /* SCAN_MAX_DEPTH, or a drive scan's few levels */
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
    } else if (is_cd_image_name(name) || is_floppy_name(name)) {
        /* (a .bin is a cartridge or a CD's track; an .img a CD's or a floppy's) */
        if (s->dosCd[0] && (s->jagRom[0] || !ends_with_nocase(name, ".bin")) &&
            (s->dosDir[0] || !is_floppy_name(name)))
            return;
    } else if (!(ends_with_nocase(name, ".jag") || ends_with_nocase(name, ".j64") ||
                 ends_with_nocase(name, ".rom")) || s->jagRom[0]) {
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
        else if (!s->dosCd[0] && strcmp(s->dosDir, dir) != 0 && !has_commentary_dir(s->dosDir) &&
                 has_commentary_dir(dir))
            snprintf(s->dosCd, sizeof s->dosCd, "%s", dir);   /* (a copy with the sounds the first has not: the CD) */
        gameDir = 1;
    } else if (has_entry(list, n, "SFX", 1) && !s->dosCd[0] && has_commentary_dir(dir)) {
        snprintf(s->dosCd, sizeof s->dosCd, "%s", dir);       /* the CD, or a copy of it */
        gameDir = 1;
    } else if (has_entry(list, n, "WORLD.ini", 0) && has_entry(list, n, "competitions", 1)) {
        SdWorldInfo w;               /* (an unpacked world package) */
        char root[1024];
        if (SdSources_OpenWorld(dir, &w, root, sizeof root) == 0) {
            take_world(s, root);
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
        else if (!gameDir && depth < ctx->maxDepth && !skip_folder(list[i].name))
            scan_dir(s, p, depth + 1, ctx);
    }
    free(list);
}

int32_t SdSources_ScanDepthEx(SdSources *s, const char *dir, int32_t maxDepth, SdScanProgress progress,
                              void *user)
{
    ScanCtx ctx = { progress, user, 0, 0, maxDepth < SCAN_MAX_DEPTH ? maxDepth : SCAN_MAX_DEPTH };
    scan_dir(s, dir, 0, &ctx);
    return ctx.stopped ? -1 : count_found(s);
}

int32_t SdSources_ScanEx(SdSources *s, const char *dir, SdScanProgress progress, void *user)
{
    ScanCtx ctx = { progress, user, 0, 0, SCAN_MAX_DEPTH };
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
    CdImage_Close(s->cdImage);
    s->disk1 = s->disk2 = s->hdfDisk = NULL;
    s->lhaArc = NULL;
    s->cdImage = NULL;
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

/* A file of the PC game where a GOG install keeps it -- else by its name where the other
 * releases do: the floppy version keeps its sound effects in SFX/ itself (no FX/ or HARD/), and
 * a copy of an installed CD, or of the CD, may hold them anywhere of these. `root` a folder, or
 * NULL for the CD image `cd`. 0 found: its path in p (on the image for `cd`). */
static int32_t find_dos_at(const char *root, CdImage *cd, const char *rel, char *p, uint32_t cap)
{
    static const char *const kAlso[] = { "SFX", "SFX/FX", "SFX/PIERCE", "HARD", "FX", "PIERCE", "" };
    const char *name = strrchr(rel, '/');
    name = name ? name + 1 : rel;
    for (int32_t i = -1; i < (int32_t)(sizeof kAlso / sizeof kAlso[0]); i++) {
        char alt[300];
        if (i < 0)
            snprintf(alt, sizeof alt, "%s", rel);
        else
            snprintf(alt, sizeof alt, "%s%s%s", kAlso[i], kAlso[i][0] ? "/" : "", name);
        if (root ? Sg_FindNoCase(root, alt, p, cap) == 0 : CdImage_Has(cd, alt) == 1) {
            if (!root)
                snprintf(p, cap, "%s", alt);
            return 0;
        }
    }
    return -1;
}

/* The commentary's first line: what tells the CD (or a folder with its sounds) from one without. */
#define DOS_COMMENTARY_PROBE "SFX/PIERCE/M10_D_.RAW"

/* A folder with the PC commentary in it, where any release or copy keeps it. */
static int32_t has_commentary_dir(const char *dir)
{
    char p[1024];
    return is_dir(dir) && !is_s2020_game_dir(dir) && find_dos_at(dir, NULL, DOS_COMMENTARY_PROBE, p, sizeof p) == 0;
}

static int32_t is_cd_image_name(const char *path)
{
    return ends_with_nocase(path, ".iso") || ends_with_nocase(path, ".img") ||
           ends_with_nocase(path, ".bin") || ends_with_nocase(path, ".cue");
}

/* An image of the PC game's CD: a data track with the commentary on it. */
static int32_t is_dos_cd_image(const char *path)
{
    char err[256], p[300];
    CdImage *cd = is_cd_image_name(path) ? CdImage_Open(path, err) : NULL;
    int32_t ok = cd && find_dos_at(NULL, cd, DOS_COMMENTARY_PROBE, p, sizeof p) == 0;
    CdImage_Close(cd);
    return ok;
}

/* The game's own files off an image of the CD into a folder of the setup's own under the system's
 * temporary folder, once -- not SFX/ nor the cutscenes (ARC/), both read off the image as they are
 * needed (SdSources_ReadDos),
 * the boot disk or the sound card drivers: that folder, a PC game folder, in `out`. 0 when the
 * image has the game on it. */
static int32_t cd_image_game(const char *image, char *out, uint32_t cap)
{
    char err[256];
    CdImage *cd = CdImage_Open(image, err);
    if (!cd)
        return -1;
    int32_t rc = -1;
    if (CdImage_Has(cd, "SPRITE.DAT") == 1 && CdImage_Has(cd, "DATA/TEAM.000") == 1) {
        const char *base = image + strlen(image);
        while (base > image && base[-1] != '/' && base[-1] != '\\')
            base--;
        char done[1200];
        snprintf(out, cap, "%s/" PROJECT_SETUP_PROGRAM "/%s.game", temp_root(), base);
        snprintf(done, sizeof done, "%s/.copied", out);         /* (a copy cut short is made again) */
        static const char *const kSkip[] = { "SFX", "ARC", "BOOT", "ENSONIQ", NULL };   /* (ARC: the cutscenes) */
        if (Sg_IsFile(done) || (CdImage_ExtractTree(cd, out, kSkip, err) == 0 &&
                                Sg_WriteFile(done, "", 0, err) == 0))
            rc = is_dos_dir(out) ? 0 : -1;
    }
    CdImage_Close(cd);
    return rc;
}

int32_t SdSources_AddDosCd(SdSources *s, const char *path)
{
    if (ends_with_nocase(path, ".zip")) {      /* a download of the CD: its image, unpacked */
        SdSources t;
        memset(&t, 0, sizeof t);
        if (add_zip(&t, path) != 0 || !t.dosCd[0])
            return -1;
        if (!s->dosCd[0])
            memcpy(s->dosCd, t.dosCd, sizeof s->dosCd);
        if (!s->dosDir[0] && t.dosDir[0])
            memcpy(s->dosDir, t.dosDir, sizeof s->dosDir);
        return 0;
    }
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", path);
    if (!has_commentary_dir(dir) && !is_dos_cd_image(path)) {
        /* A file in the folder: its folder. */
        char *slash = strrchr(dir, '/'), *bslash = strrchr(dir, '\\');
        if (bslash && (!slash || bslash > slash))
            slash = bslash;
        if (!slash || is_dir(path))
            return -1;
        *slash = '\0';
        if (!has_commentary_dir(dir))
            return -1;
    }
    if (!s->dosCd[0]) {
        snprintf(s->dosCd, sizeof s->dosCd, "%s", dir);
        CdImage_Close(s->cdImage);
        s->cdImage = NULL;
    }
    /* The whole game is on the CD: with no PC game folder yet, the CD is that too. */
    if (!s->dosDir[0]) {
        if (is_dir(dir)) {
            if (is_dos_dir(dir))
                snprintf(s->dosDir, sizeof s->dosDir, "%s", dir);
        } else {
            char pc[sizeof s->dosDir];
            if (cd_image_game(dir, pc, sizeof pc) == 0)
                snprintf(s->dosDir, sizeof s->dosDir, "%s", pc);
        }
    }
    return 0;
}

int32_t SdSources_HaveDosCd(const SdSources *s) { return s->dosCd[0] != 0; }

/* The CD in a drive: its drive's top folder only, never searched (this runs every few seconds
 * while the setup watches). */
static int32_t try_cd_root(SdSources *s, const char *root)
{
    return has_commentary_dir(root) && SdSources_AddDosCd(s, root) == 0 && s->dosCd[0];
}

int32_t SdSources_FindCdDrive(SdSources *s)
{
    if (s->dosCd[0])
        return 0;
#ifdef _WIN32
    /* The CD drives with a disc in (a mounted .iso is one too); no "insert a disk" box for one
     * without. */
    UINT old = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    DWORD drives = GetLogicalDrives();
    int32_t found = 0;
    for (int32_t i = 0; i < 26 && !found; i++) {
        char root[4] = { (char)('A' + i), ':', '\\', 0 };
        if (!(drives & (1u << i)) || GetDriveTypeA(root) != DRIVE_CDROM ||
            !GetVolumeInformationA(root, NULL, 0, NULL, NULL, NULL, NULL, 0))
            continue;
        char top[4] = { (char)('A' + i), ':', '/', 0 };
        found = try_cd_root(s, top);
    }
    SetErrorMode(old);
    return found ? 0 : -1;
#else
    /* Where a CD is mounted: /Volumes on a Mac; /media(/<user>), /run/media/<user> and
     * /mnt/cdrom on Linux. */
    static const char *const kMounts[] = { "/Volumes", "/media", "/run/media", NULL };
    if (try_cd_root(s, "/mnt/cdrom") || try_cd_root(s, "/cdrom"))
        return 0;
    for (int32_t m = 0; kMounts[m]; m++) {
        int32_t n;
        Entry *list = list_dir(kMounts[m], &n);
        for (int32_t i = 0; i < n; i++) {
            if (!list[i].isDir || list[i].name[0] == '.')
                continue;
            char p[1100];
            Sg_PathJoin(p, sizeof p, kMounts[m], list[i].name);
            if (try_cd_root(s, p)) {
                free(list);
                return 0;
            }
            if (m == 0)
                continue;
            int32_t n2;                  /* (/media/<user>/<disc>) */
            Entry *sub = list_dir(p, &n2);
            for (int32_t j = 0; j < n2; j++) {
                char q[1400];
                Sg_PathJoin(q, sizeof q, p, sub[j].name);
                if (sub[j].isDir && sub[j].name[0] != '.' && try_cd_root(s, q)) {
                    free(sub);
                    free(list);
                    return 0;
                }
            }
            free(sub);
        }
        free(list);
    }
    return -1;
#endif
}

int32_t SdSources_HaveCommentary(const SdSources *s)
{
    /* The same look-up the conversion makes, on a copy that opens nothing of the caller's. */
    SdSources t;
    memset(&t, 0, sizeof t);
    memcpy(t.dosDir, s->dosDir, sizeof t.dosDir);
    memcpy(t.dosCd, s->dosCd, sizeof t.dosCd);
    SgBuf b = { 0 };
    char err[256];
    int32_t ok = SdSources_ReadDos(&t, DOS_COMMENTARY_PROBE, &b, err) == 0;
    Sg_BufFree(&b);
    CdImage_Close(t.cdImage);
    return ok;
}

int32_t SdSources_ReadDos(SdSources *s, const char *rel, SgBuf *out, char *err)
{
    char p[1024];
    if (s->dosDir[0] && find_dos_at(s->dosDir, NULL, rel, p, sizeof p) == 0)
        return Sg_ReadFile(p, out, err);
    if (s->dosCd[0]) {
        if (is_dir(s->dosCd)) {
            if (find_dos_at(s->dosCd, NULL, rel, p, sizeof p) == 0)
                return Sg_ReadFile(p, out, err);
        } else {
            if (!s->cdImage && !(s->cdImage = CdImage_Open(s->dosCd, err)))
                return -1;
            if (find_dos_at(NULL, s->cdImage, rel, p, sizeof p) == 0)
                return CdImage_Read(s->cdImage, p, out, err);
        }
    }
    snprintf(err, 256, "%s is not in the PC game folder%s", rel, s->dosCd[0] ? " or on its CD" : "");
    return -1;
}

/* ---- the PC data from SWOS 2020 (no PC game, no Amiga version) ---------- */

int32_t SdSources_PcFromSwos2020(const SdSources *s)
{
    return s->s2020Dir[0] && !s->dosDir[0] && !SdSources_HaveAmiga(s);
}

/* The 96/97 teams SWOS 2020 keeps for its online play: its own DATA/ is a later season. */
#define S2020_TEAMS_9697 "MP/DB/96_97"

int32_t SdSources_Swos2020Has9697Teams(const SdSources *s)
{
    char p[1024];
    return s->s2020Dir[0] && Sg_FindNoCase(s->s2020Dir, S2020_TEAMS_9697 "/DATA/TEAM.000", p, sizeof p) == 0;
}

int32_t SdSources_ReadSwos2020Pc(SdSources *s, const char *rel, SgBuf *out, char *err)
{
    char p[1024], alt[300];
    const char *name = strrchr(rel, '/');
    name = name ? name + 1 : rel;
    /* The PC game's CD, when there is one, for the sounds: the originals and the commentary. */
    if (s->dosCd[0] && (!strncmp(rel, "SFX/", 4) || !strncmp(rel, "HARD/", 5)) &&
        SdSources_ReadDos(s, rel, out, err) == 0)
        return 0;
    if (!strncmp(rel, "SFX/PIERCE/", 11)) {           /* the commentary: SWOS 2020 has none */
        snprintf(err, 256, "%s: SWOS 2020 has no commentary", rel);
        return -1;
    }
    if (!strncmp(rel, "SFX/FX/", 7) || !strncmp(rel, "HARD/", 5))
        snprintf(alt, sizeof alt, "audio/fx/%s", name);   /* its effects and crowd, in one folder */
    else if (!strncmp(rel, "DATA/", 5) && SdSources_Swos2020Has9697Teams(s))
        snprintf(alt, sizeof alt, S2020_TEAMS_9697 "/%s", rel);
    else
        snprintf(alt, sizeof alt, "%s", rel);
    if (Sg_FindNoCase(s->s2020Dir, alt, p, sizeof p) != 0) {
        snprintf(err, 256, "%.200s is not in SWOS 2020's game folder", alt);
        return -1;
    }
    return Sg_ReadFile(p, out, err);
}
