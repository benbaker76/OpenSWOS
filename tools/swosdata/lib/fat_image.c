/*
 * fat_image.c -- see fat_image.h. The boot sector's BIOS parameter block, the FAT, directory
 * entries (8.3; long names, deleted entries and the volume label skipped), cluster chains.
 */
#include "fat_image.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAT_MAX_IMAGE (4u * 1024 * 1024)    /* a 2.88 MB disk is the largest there is */

struct FatImage {
    uint8_t *d;
    uint32_t size;
    uint32_t bps, spc, fatOff, rootOff, rootEntries, dataOff, clusters;
    int32_t  fat16;
};

typedef struct {
    uint32_t cluster, size;
    uint8_t  isDir;
} FatEntry;

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

FatImage *FatImage_Open(const char *path, char *err)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, 256, "%.200s: cannot be opened", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 512 || (unsigned long)n > FAT_MAX_IMAGE) {
        fclose(f);
        snprintf(err, 256, "%.200s: not a floppy disk image", path);
        return NULL;
    }
    FatImage *img = calloc(1, sizeof *img);
    if (img)
        img->d = malloc((size_t)n);
    if (!img || !img->d || fread(img->d, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        FatImage_Close(img);
        snprintf(err, 256, "%.200s: cannot be read", path);
        return NULL;
    }
    fclose(f);
    img->size = (uint32_t)n;
    const uint8_t *b = img->d;
    const uint32_t bps = le16(b + 11), spc = b[13], reserved = le16(b + 14), nfat = b[16];
    const uint32_t nroot = le16(b + 17), spf = le16(b + 22);
    uint32_t total = le16(b + 19);
    if (!total)
        total = le32(b + 32);
    /* A BIOS parameter block that makes sense, and fits the file. */
    if ((bps != 512 && bps != 1024 && bps != 2048 && bps != 4096) || !spc || (spc & (spc - 1)) ||
        !reserved || nfat < 1 || nfat > 2 || !spf || !nroot || total * bps > img->size + bps) {
        FatImage_Close(img);
        snprintf(err, 256, "%.200s: not a FAT disk image", path);
        return NULL;
    }
    img->bps = bps;
    img->spc = spc;
    img->fatOff = reserved * bps;
    img->rootOff = (reserved + nfat * spf) * bps;
    img->rootEntries = nroot;
    img->dataOff = img->rootOff + ((nroot * 32 + bps - 1) / bps) * bps;
    img->clusters = total * bps > img->dataOff ? (total * bps - img->dataOff) / (bps * spc) : 0;
    img->fat16 = img->clusters >= 4085;
    if (img->dataOff >= img->size || !img->clusters) {
        FatImage_Close(img);
        snprintf(err, 256, "%.200s: not a FAT disk image", path);
        return NULL;
    }
    return img;
}

void FatImage_Close(FatImage *img)
{
    if (!img)
        return;
    free(img->d);
    free(img);
}

/* The next cluster of a chain; >= 0xFF8 (FAT12) / 0xFFF8 (FAT16) ends it. */
static uint32_t next_cluster(const FatImage *img, uint32_t c)
{
    if (img->fat16) {
        uint32_t o = img->fatOff + c * 2;
        return o + 2 <= img->size ? le16(img->d + o) : 0xFFFF;
    }
    uint32_t o = img->fatOff + c * 3 / 2;
    if (o + 2 > img->size)
        return 0xFFF;
    uint32_t v = le16(img->d + o);
    return c & 1 ? v >> 4 : v & 0xFFF;
}

static int32_t chain_end(const FatImage *img, uint32_t c)
{
    return c < 2 || c >= (img->fat16 ? 0xFFF8u : 0xFF8u) || c - 2 >= img->clusters;
}

static const uint8_t *cluster_data(const FatImage *img, uint32_t c)
{
    uint64_t o = (uint64_t)img->dataOff + (uint64_t)(c - 2) * img->bps * img->spc;
    return o + (uint64_t)img->bps * img->spc <= img->size ? img->d + o : NULL;
}

/* An entry's 8.3 name ("SWOS_1.RNC") against one part of a path, any case. */
static int32_t name_is(const uint8_t *e, const char *part, uint32_t partLen)
{
    char name[13];
    uint32_t n = 0;
    for (uint32_t i = 0; i < 8 && e[i] != ' '; i++)
        name[n++] = (char)e[i];
    if (e[8] != ' ') {
        name[n++] = '.';
        for (uint32_t i = 8; i < 11 && e[i] != ' '; i++)
            name[n++] = (char)e[i];
    }
    if (n != partLen)
        return 0;
    for (uint32_t i = 0; i < n; i++)
        if (tolower((unsigned char)name[i]) != tolower((unsigned char)part[i]))
            return 0;
    return 1;
}

/* Look through `count` entries at `p` for one name; 1 found, 0 not, -1 the directory ended. */
static int32_t scan_entries(const uint8_t *p, uint32_t count, const char *part, uint32_t partLen, FatEntry *out)
{
    for (uint32_t i = 0; i < count; i++, p += 32) {
        if (p[0] == 0)
            return -1;
        if (p[0] == 0xE5 || p[11] == 0x0F || (p[11] & 0x08))
            continue;
        if (name_is(p, part, partLen)) {
            out->cluster = le16(p + 26);
            out->size = le32(p + 28);
            out->isDir = (p[11] & 0x10) != 0;
            return 1;
        }
    }
    return 0;
}

static int32_t find(FatImage *img, const char *rel, FatEntry *out)
{
    FatEntry dir = { 0, 0, 1 };       /* cluster 0: the root directory */
    const char *p = rel;
    while (*p == '/' || *p == '\\')
        p++;
    if (!*p)
        return -1;
    while (*p) {
        const char *end = p;
        while (*end && *end != '/' && *end != '\\')
            end++;
        const uint32_t len = (uint32_t)(end - p);
        FatEntry e;
        int32_t r = 0;
        if (!dir.isDir)
            return -1;
        if (dir.cluster == 0) {
            if (img->rootOff + img->rootEntries * 32 > img->size)
                return -1;
            r = scan_entries(img->d + img->rootOff, img->rootEntries, p, len, &e);
        } else {
            const uint32_t per = img->bps * img->spc / 32;
            for (uint32_t c = dir.cluster, guard = 0; r == 0 && !chain_end(img, c) && guard < img->clusters;
                 c = next_cluster(img, c), guard++) {
                const uint8_t *data = cluster_data(img, c);
                if (!data)
                    return -1;
                r = scan_entries(data, per, p, len, &e);
            }
        }
        if (r != 1)
            return -1;
        dir = e;
        p = end;
        while (*p == '/' || *p == '\\')
            p++;
    }
    *out = dir;
    return 0;
}

int32_t FatImage_Has(FatImage *img, const char *rel)
{
    FatEntry e;
    return img && find(img, rel, &e) == 0 && !e.isDir;
}

int32_t FatImage_Read(FatImage *img, const char *rel, SgBuf *out, char *err)
{
    FatEntry e;
    if (!img || find(img, rel, &e) != 0 || e.isDir) {
        snprintf(err, 256, "%.200s is not on the disk", rel);
        return -1;
    }
    if (e.size > img->size) {
        snprintf(err, 256, "%.200s on the disk: its size is damaged", rel);
        return -1;
    }
    uint8_t *data = malloc(e.size ? e.size : 1);
    if (!data) {
        snprintf(err, 256, "out of memory");
        return -1;
    }
    const uint32_t csize = img->bps * img->spc;
    uint32_t done = 0;
    for (uint32_t c = e.cluster, guard = 0; done < e.size; c = next_cluster(img, c), guard++) {
        const uint8_t *src = chain_end(img, c) || guard >= img->clusters ? NULL : cluster_data(img, c);
        if (!src) {
            free(data);
            snprintf(err, 256, "%.200s on the disk: its clusters are damaged", rel);
            return -1;
        }
        uint32_t n = e.size - done < csize ? e.size - done : csize;
        memcpy(data + done, src, n);
        done += n;
    }
    out->data = data;
    out->size = e.size;
    return 0;
}
