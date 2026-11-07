/*
 * cd_image.c -- see cd_image.h. ISO 9660 only as far as reading a file by its path takes:
 * the primary volume descriptor's root, directory records, file extents.
 */
#ifndef _WIN32
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#endif
#include "cd_image.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define CD_DATA      2048u
#define CD_MAX_FILE  (64u * 1024 * 1024)   /* nothing on the disc the setup reads is near it */

struct CdImage {
    FILE    *f;
    uint32_t sector;        /* bytes a sector in the file: 2048, 2352 or 2336 */
    uint32_t offset;        /* where in a sector its 2048 bytes of data start */
    uint32_t rootLba, rootSize;
};

typedef struct {
    uint32_t lba, size;
    uint8_t  isDir;
} CdEntry;

static int32_t seek64(FILE *f, uint64_t off)
{
#ifdef _WIN32
    return _fseeki64(f, (__int64)off, SEEK_SET) == 0 ? 0 : -1;
#else
    return fseeko(f, (off_t)off, SEEK_SET) == 0 ? 0 : -1;
#endif
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int32_t read_sector(CdImage *cd, uint32_t lba, uint8_t *buf)
{
    if (seek64(cd->f, (uint64_t)lba * cd->sector + cd->offset) != 0)
        return -1;
    return fread(buf, 1, CD_DATA, cd->f) == CD_DATA ? 0 : -1;
}

/* The .bin a .cue names first (its data track), beside the .cue unless the path is whole. */
static int32_t cue_bin(const char *cue, char *out, uint32_t cap)
{
    FILE *f = fopen(cue, "rb");
    if (!f)
        return -1;
    char line[1200];
    int32_t rc = -1;
    while (rc != 0 && fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (strncmp(p, "FILE", 4) != 0 || (p[4] != ' ' && p[4] != '\t'))
            continue;
        p += 5;
        while (*p == ' ' || *p == '\t')
            p++;
        char name[1024];
        uint32_t n = 0;
        if (*p == '"') {
            for (p++; *p && *p != '"' && n + 1 < sizeof name; p++)
                name[n++] = *p;
        } else {
            for (; *p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && n + 1 < sizeof name; p++)
                name[n++] = *p;
        }
        name[n] = '\0';
        if (!n)
            continue;
        int32_t whole = name[0] == '/' || name[0] == '\\' || (n > 2 && name[1] == ':');
        const char *slash = strrchr(cue, '/'), *bslash = strrchr(cue, '\\');
        if (bslash && (!slash || bslash > slash))
            slash = bslash;
        if (whole || !slash)
            snprintf(out, cap, "%s", name);
        else
            snprintf(out, cap, "%.*s/%s", (int)(slash - cue), cue, name);
        rc = 0;
    }
    fclose(f);
    return rc;
}

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

CdImage *CdImage_Open(const char *path, char *err)
{
    char bin[1100];
    if (ends_with_nocase(path, ".cue")) {
        if (cue_bin(path, bin, sizeof bin) != 0) {
            snprintf(err, 256, "%.200s: it names no track file", path);
            return NULL;
        }
        path = bin;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, 256, "%.200s: cannot be opened", path);
        return NULL;
    }
    /* The dumps there are: cooked, raw Mode 1, raw Mode 2 Form 1, and Mode 2 without its sync. */
    static const uint32_t kLayout[][2] = { { 2048, 0 }, { 2352, 16 }, { 2352, 24 }, { 2336, 8 } };
    CdImage *cd = calloc(1, sizeof *cd);
    if (!cd) {
        fclose(f);
        snprintf(err, 256, "out of memory");
        return NULL;
    }
    cd->f = f;
    uint8_t pvd[CD_DATA];
    for (uint32_t i = 0; i < sizeof kLayout / sizeof kLayout[0]; i++) {
        cd->sector = kLayout[i][0];
        cd->offset = kLayout[i][1];
        if (read_sector(cd, 16, pvd) == 0 && pvd[0] == 1 && memcmp(pvd + 1, "CD001", 5) == 0) {
            cd->rootLba = le32(pvd + 156 + 2);
            cd->rootSize = le32(pvd + 156 + 10);
            return cd;
        }
    }
    CdImage_Close(cd);
    snprintf(err, 256, "%.200s: not a CD image with files on it", path);
    return NULL;
}

void CdImage_Close(CdImage *cd)
{
    if (!cd)
        return;
    if (cd->f)
        fclose(cd->f);
    free(cd);
}

/* An ISO name ("PIERCE", "M10_D_.RAW;1") against one part of a path, any case. */
static int32_t name_is(const uint8_t *name, uint32_t len, const char *part, uint32_t partLen)
{
    const uint8_t *semi = memchr(name, ';', len);
    if (semi)
        len = (uint32_t)(semi - name);
    if (len && name[len - 1] == '.')
        len--;
    if (len != partLen)
        return 0;
    for (uint32_t i = 0; i < len; i++)
        if (tolower(name[i]) != tolower((unsigned char)part[i]))
            return 0;
    return 1;
}

static int32_t find_in_dir(CdImage *cd, const CdEntry *dir, const char *part, uint32_t partLen, CdEntry *out)
{
    uint8_t buf[CD_DATA];
    uint32_t sectors = (dir->size + CD_DATA - 1) / CD_DATA;
    for (uint32_t s = 0; s < sectors; s++) {
        if (read_sector(cd, dir->lba + s, buf) != 0)
            return -1;
        for (uint32_t off = 0; off + 34 <= CD_DATA;) {
            uint32_t len = buf[off];
            if (!len || off + len > CD_DATA)
                break;                       /* the rest of the sector is padding */
            uint32_t nameLen = buf[off + 32];
            if (33 + nameLen <= len && !(nameLen == 1 && buf[off + 33] <= 1) &&
                name_is(buf + off + 33, nameLen, part, partLen)) {
                out->lba = le32(buf + off + 2);
                out->size = le32(buf + off + 10);
                out->isDir = (buf[off + 25] & 2) != 0;
                return 0;
            }
            off += len;
        }
    }
    return -1;
}

static int32_t find(CdImage *cd, const char *rel, CdEntry *out)
{
    CdEntry e = { cd->rootLba, cd->rootSize, 1 };
    const char *p = rel;
    while (*p) {
        while (*p == '/' || *p == '\\')
            p++;
        if (!*p)
            break;
        const char *end = p;
        while (*end && *end != '/' && *end != '\\')
            end++;
        if (!e.isDir || find_in_dir(cd, &e, p, (uint32_t)(end - p), &e) != 0)
            return -1;
        p = end;
    }
    *out = e;
    return 0;
}

int32_t CdImage_Has(CdImage *cd, const char *rel)
{
    CdEntry e;
    if (!cd || find(cd, rel, &e) != 0)
        return 0;
    return e.isDir ? 2 : 1;
}

int32_t CdImage_Read(CdImage *cd, const char *rel, SgBuf *out, char *err)
{
    CdEntry e;
    if (!cd || find(cd, rel, &e) != 0 || e.isDir) {
        snprintf(err, 256, "%.200s is not on the CD", rel);
        return -1;
    }
    if (e.size > CD_MAX_FILE) {
        snprintf(err, 256, "%.200s on the CD: too big to be the game's", rel);
        return -1;
    }
    uint8_t *data = malloc(e.size ? e.size : 1);
    if (!data) {
        snprintf(err, 256, "out of memory");
        return -1;
    }
    uint8_t buf[CD_DATA];
    for (uint32_t done = 0, s = 0; done < e.size; s++) {
        if (read_sector(cd, e.lba + s, buf) != 0) {
            free(data);
            snprintf(err, 256, "%.200s on the CD: cannot be read", rel);
            return -1;
        }
        uint32_t n = e.size - done < CD_DATA ? e.size - done : CD_DATA;
        memcpy(data + done, buf, n);
        done += n;
    }
    out->data = data;
    out->size = e.size;
    return 0;
}

/* ---- the whole tree ---------------------------------------------------- */

static int32_t write_file(CdImage *cd, const CdEntry *e, const char *out, char *err)
{
    if (e->size > CD_MAX_FILE) {
        snprintf(err, 256, "%.200s on the CD: too big to be the game's", out);
        return -1;
    }
    FILE *f = fopen(out, "wb");
    if (!f) {
        snprintf(err, 256, "cannot write %.200s", out);
        return -1;
    }
    uint8_t buf[CD_DATA];
    int32_t rc = 0;
    for (uint32_t done = 0, s = 0; done < e->size && rc == 0; s++) {
        uint32_t n = e->size - done < CD_DATA ? e->size - done : CD_DATA;
        if (read_sector(cd, e->lba + s, buf) != 0 || fwrite(buf, 1, n, f) != n)
            rc = -1;
        done += n;
    }
    if (fclose(f) != 0)
        rc = -1;
    if (rc != 0)
        snprintf(err, 256, "%.200s: cannot be copied off the CD", out);
    return rc;
}

static int32_t skipped(const char *name, const char *const *skip)
{
    for (; skip && *skip; skip++) {
        const char *a = name, *b = *skip;
        while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b))
            a++, b++;
        if (!*a && !*b)
            return 1;
    }
    return 0;
}

static int32_t extract_dir(CdImage *cd, const CdEntry *dir, const char *dest, const char *const *skip,
                           int32_t depth, char *err)
{
    if (depth > 16 || Sg_MakeDirs(dest) != 0) {
        snprintf(err, 256, "cannot make %.200s", dest);
        return -1;
    }
    uint8_t buf[CD_DATA];
    uint32_t sectors = (dir->size + CD_DATA - 1) / CD_DATA;
    for (uint32_t s = 0; s < sectors; s++) {
        if (read_sector(cd, dir->lba + s, buf) != 0) {
            snprintf(err, 256, "the CD's folders cannot be read");
            return -1;
        }
        for (uint32_t off = 0; off + 34 <= CD_DATA;) {
            uint32_t len = buf[off];
            if (!len || off + len > CD_DATA)
                break;
            uint32_t nameLen = buf[off + 32];
            const uint8_t *nm = buf + off + 33;
            if (33 + nameLen <= len && !(nameLen == 1 && nm[0] <= 1)) {
                char name[256];
                uint32_t n = 0;
                for (; n < nameLen && n + 1 < sizeof name && nm[n] != ';'; n++)
                    name[n] = (char)nm[n];
                if (n && name[n - 1] == '.')
                    n--;
                name[n] = '\0';
                CdEntry e = { le32(buf + off + 2), le32(buf + off + 10), (uint8_t)((buf[off + 25] & 2) != 0) };
                /* (a name that would climb out of dest is not one the disc should have) */
                if (n && !strchr(name, '/') && !strchr(name, '\\') && strcmp(name, "..") != 0 &&
                    !(depth == 0 && skipped(name, skip))) {
                    char out[1400];
                    snprintf(out, sizeof out, "%s/%s", dest, name);
                    if (e.isDir ? extract_dir(cd, &e, out, skip, depth + 1, err) : write_file(cd, &e, out, err))
                        return -1;
                }
            }
            off += len;
        }
    }
    return 0;
}

int32_t CdImage_ExtractTree(CdImage *cd, const char *dest, const char *const *skip, char *err)
{
    CdEntry root = { cd->rootLba, cd->rootSize, 1 };
    return extract_dir(cd, &root, dest, skip, 0, err);
}
