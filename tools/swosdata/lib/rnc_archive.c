/*
 * rnc_archive.c -- see rnc_archive.h. The packed files are unpacked by swosgfx's Sg_RncUnpack,
 * which checks both of each file's CRCs.
 */
#include "rnc_archive.h"
#include "sg_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

/* CRC-16, poly 0xA001 reflected, init 0: RNC's (the same as each packed file's). */
static uint16_t crc16(const uint8_t *p, uint32_t n)
{
    uint16_t crc = 0;
    while (n--) {
        crc ^= *p++;
        for (int32_t i = 0; i < 8; i++)
            crc = (uint16_t)(crc & 1 ? (crc >> 1) ^ 0xA001 : crc >> 1);
    }
    return crc;
}

int32_t RncArchive_Is(const uint8_t *data, uint32_t size)
{
    return size >= 12 && memcmp(data, "RNCA", 4) == 0;
}

/* A NUL-ended string inside [p, end): its length, or -1 when it runs past the end. */
static int32_t str_len(const uint8_t *p, const uint8_t *end)
{
    const uint8_t *z = memchr(p, 0, (size_t)(end - p));
    return z ? (int32_t)(z - p) : -1;
}

/* One file's data at `off` to `path`. */
static int32_t extract_one(const uint8_t *data, uint32_t size, uint32_t off, const char *path, char *err)
{
    if (off >= size || size - off < 8 || memcmp(data + off, "RNC", 3) != 0) {
        snprintf(err, 256, "%.200s: not where the archive says it is (damaged)", path);
        return -1;
    }
    const uint8_t method = data[off + 3];
    if (method == 0) {                       /* stored: "RNC\0", its size, the bytes */
        const uint32_t n = be32(data + off + 4);
        if (n > size - off - 8) {
            snprintf(err, 256, "%.200s: runs past the end of the archive (damaged)", path);
            return -1;
        }
        return Sg_WriteFile(path, data + off + 8, n, err);
    }
    if (method != 1 && method != 2) {
        snprintf(err, 256, "%.200s: packed in a way this does not know (RNC method %u)", path, (unsigned)method);
        return -1;
    }
    SgBuf in = { (uint8_t *)data + off, size - off }, out = { 0 };
    if (Sg_RncUnpack(&in, &out, err) != 0)
        return -1;
    int32_t rc = Sg_WriteFile(path, out.data, out.size, err);
    Sg_BufFree(&out);
    return rc;
}

int32_t RncArchive_Extract(const uint8_t *data, uint32_t size, const char *dest, int32_t *files, char *err)
{
    *files = 0;
    if (!RncArchive_Is(data, size)) {
        snprintf(err, 256, "not an RNC archive");
        return -1;
    }
    const uint32_t listEnd = be16(data + 4);
    if (listEnd < 8 || listEnd > size) {
        snprintf(err, 256, "the archive's folder list runs past its end (damaged)");
        return -1;
    }
    if (crc16(data + 8, listEnd - 8) != be16(data + 6)) {
        snprintf(err, 256, "the archive's folder list does not match its CRC (damaged)");
        return -1;
    }
    const uint8_t *p = data + 8, *end = data + listEnd;
    while (p + 3 <= end) {
        /* (the end is past the folder's 00h: the next folder starts there) */
        const uint8_t *filesEnd = data + be16(p);
        p += 2;
        int32_t n = str_len(p, end);
        if (n < 0 || filesEnd > end || filesEnd <= p + n + 1 || filesEnd[-1] != 0) {
            snprintf(err, 256, "the archive's folder list is damaged");
            return -1;
        }
        char folder[256];
        snprintf(folder, sizeof folder, "%.*s", (int)n, (const char *)p);
        for (char *c = folder; *c; c++)
            if (*c == '\\')
                *c = '/';
        p += n + 1;
        while (p < filesEnd - 1 && *p) {
            n = str_len(p, filesEnd);
            if (n < 0 || p + n + 1 + 4 > filesEnd - 1) {
                snprintf(err, 256, "the archive's file list is damaged");
                return -1;
            }
            char rel[600], path[1600];
            snprintf(rel, sizeof rel, "%s%.*s", folder, (int)n, (const char *)p);
            const uint32_t off = be32(p + n + 1);
            p += n + 1 + 4;
            /* Its own folders only: no name may climb out of dest. */
            if (rel[0] == '/' || strchr(rel, ':') || strstr(rel, "..")) {
                snprintf(err, 256, "the archive names a file outside its folder: %.200s", rel);
                return -1;
            }
            snprintf(path, sizeof path, "%s/%s", dest, rel);
            if (extract_one(data, size, off, path, err) != 0)
                return -1;
            (*files)++;
        }
        p = filesEnd;
    }
    return 0;
}
