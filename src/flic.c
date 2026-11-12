/*
 * flic.c -- an Autodesk FLIC (.FLC) decoder of our own (include/flic.h; docs/DOS_VIDEO.md).
 *
 * The format (Autodesk's "FLC" of Animator Pro, as documented by Jim Kent's and others' notes):
 *
 *   the header, 128 bytes:   0 size u32 | 4 magic u16 (0xAF12) | 6 frames u16 | 8 width u16 |
 *                           10 height u16 | 12 depth u16 (8) | 14 flags | 16 speed u32 (ms) |
 *                           ... | 80 oframe1 u32 (the first frame's offset) | 84 oframe2
 *   then chunks:             size u32 | type u16 ...; a frame is type 0xF1FA:
 *                           size u32 | 0xF1FA | chunks u16 | delay u16 | reserved u16 |
 *                           width u16 | height u16 | its chunks, each size u32 | type u16 | data
 *
 * After `frames` frames comes the ring frame (the first frame again, as a delta of the last),
 * for players that loop; a clip played once never draws it. Everything is little-endian.
 */
#include "flic.h"

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

#define CHUNK_COLOR_256 4
#define CHUNK_DELTA_FLC 7
#define CHUNK_BLACK     13
#define CHUNK_BYTE_RUN  15
#define CHUNK_FLI_COPY  16
#define CHUNK_FRAME     0xF1FAu

int32_t Flic_Open(Flic *f, const uint8_t *data, uint32_t size)
{
    uint32_t i;
    for (i = 0; i < sizeof *f; i++)
        ((uint8_t *)f)[i] = 0;
    if (!data || size < 128 || rd16(data + 4) != FLIC_MAGIC_FLC || rd16(data + 12) != 8)
        return FLIC_ERR_HEADER;
    f->data = data;
    f->size = size;
    f->frames = rd16(data + 6);
    f->width = rd16(data + 8);
    f->height = rd16(data + 10);
    f->speedMs = rd32(data + 16);
    f->first = rd32(data + 80);
    if (!f->first)
        f->first = 128;             /* (some writers leave oframe1 0: the frames follow the header) */
    if (f->width == 0 || f->height == 0 || f->width > 1024 || f->height > 1024 ||
        f->first < 128 || f->first >= size)
        return FLIC_ERR_HEADER;
    f->pos = f->first;
    return 0;
}

void Flic_Rewind(Flic *f)
{
    f->pos = f->first;
    f->next = 0;
}

/* COLOR_256: packets of (skip, count) and count R G B triples; a count of 0 is 256. */
static int32_t color_256(const uint8_t *p, uint32_t n, uint8_t *palette)
{
    uint32_t at = 0, index = 0;
    if (n < 2)
        return FLIC_ERR_DATA;
    const uint32_t packets = rd16(p);
    at = 2;
    for (uint32_t k = 0; k < packets; k++) {
        if (at + 2 > n)
            return FLIC_ERR_DATA;
        index += p[at];
        uint32_t count = p[at + 1];
        if (!count)
            count = 256;
        at += 2;
        if (index + count > 256 || at + count * 3 > n)
            return FLIC_ERR_DATA;
        for (uint32_t c = 0; c < count * 3; c++)
            palette[index * 3 + c] = p[at + c];
        at += count * 3;
        index += count;
    }
    return 0;
}

/* BYTE_RUN: the whole picture, a line at a time: a count byte (not used: lines end at the width),
 * then packets -- a negative size copies that many bytes, a positive one repeats the next byte. */
static int32_t byte_run(const uint8_t *p, uint32_t n, uint8_t *pixels, uint32_t w, uint32_t h)
{
    uint32_t at = 0;
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *line = pixels + y * w;
        uint32_t x = 0;
        if (at >= n)
            return FLIC_ERR_DATA;
        at++;                                   /* (the packet count) */
        while (x < w) {
            if (at >= n)
                return FLIC_ERR_DATA;
            const int32_t s = (int8_t)p[at++];
            if (s < 0) {
                const uint32_t c = (uint32_t)-s;
                if (x + c > w || at + c > n)
                    return FLIC_ERR_DATA;
                for (uint32_t i = 0; i < c; i++)
                    line[x++] = p[at++];
            } else {
                const uint32_t c = (uint32_t)s;
                if (c == 0 || x + c > w || at >= n)
                    return FLIC_ERR_DATA;
                const uint8_t v = p[at++];
                for (uint32_t i = 0; i < c; i++)
                    line[x++] = v;
            }
        }
    }
    return 0;
}

/* DELTA_FLC (SS2): the lines that changed. A count of lines, then per line opcode words: 11..
 * skips that many lines (minus the word), 10.. sets the line's last pixel to the low byte (an
 * odd width), 00.. is the line's packet count. A packet: a column skip byte, then a signed count
 * -- positive copies that many WORDS, negative repeats the next word. */
static int32_t delta_flc(const uint8_t *p, uint32_t n, uint8_t *pixels, uint32_t w, uint32_t h)
{
    uint32_t at = 2, y = 0;
    if (n < 2)
        return FLIC_ERR_DATA;
    uint32_t lines = rd16(p);
    while (lines > 0) {
        uint16_t op;
        if (y >= h || at + 2 > n)
            return FLIC_ERR_DATA;
        op = rd16(p + at);
        at += 2;
        while (op & 0x8000) {
            if (op & 0x4000) {
                y += 0x10000u - op;             /* (the word is minus the lines to skip) */
                if (y >= h)
                    return FLIC_ERR_DATA;
            } else {
                pixels[y * w + w - 1] = (uint8_t)op;
            }
            if (at + 2 > n)
                return FLIC_ERR_DATA;
            op = rd16(p + at);
            at += 2;
        }
        uint8_t *line = pixels + y * w;
        uint32_t x = 0;
        for (uint32_t k = 0; k < op; k++) {
            if (at + 2 > n)
                return FLIC_ERR_DATA;
            x += p[at];
            const int32_t c = (int8_t)p[at + 1];
            at += 2;
            if (c >= 0) {
                const uint32_t bytes = (uint32_t)c * 2;
                if (x + bytes > w || at + bytes > n)
                    return FLIC_ERR_DATA;
                for (uint32_t i = 0; i < bytes; i++)
                    line[x++] = p[at++];
            } else {
                const uint32_t words = (uint32_t)-c;
                if (x + words * 2 > w || at + 2 > n)
                    return FLIC_ERR_DATA;
                const uint8_t a = p[at], b = p[at + 1];
                at += 2;
                for (uint32_t i = 0; i < words; i++) {
                    line[x++] = a;
                    line[x++] = b;
                }
            }
        }
        y++;
        lines--;
    }
    return 0;
}

int32_t Flic_Next(Flic *f, uint8_t *pixels, uint8_t *palette)
{
    const uint32_t w = f->width, h = f->height;
    if (f->next >= f->frames)
        return 0;
    for (;;) {                                  /* (any chunk before the frame -- a prefix -- skipped) */
        if (f->pos > f->size || f->size - f->pos < 6)
            return FLIC_ERR_FRAME;
        const uint8_t *c = f->data + f->pos;
        const uint32_t size = rd32(c);
        if (size < 6 || size > f->size - f->pos)
            return FLIC_ERR_FRAME;
        if (rd16(c + 4) != CHUNK_FRAME) {
            f->pos += size;
            f->skipped++;
            continue;
        }
        if (size < 16)
            return FLIC_ERR_FRAME;
        const uint32_t chunks = rd16(c + 6);
        uint32_t at = 16;
        for (uint32_t k = 0; k < chunks; k++) {
            if (size - at < 6)
                return FLIC_ERR_FRAME;
            const uint32_t csize = rd32(c + at);
            const uint32_t ctype = rd16(c + at + 4);
            if (csize < 6 || csize > size - at)
                return FLIC_ERR_FRAME;
            const uint8_t *d = c + at + 6;
            const uint32_t dn = csize - 6;
            int32_t rc = 0;
            switch (ctype) {
            case CHUNK_COLOR_256: rc = color_256(d, dn, palette); break;
            case CHUNK_BYTE_RUN:  rc = byte_run(d, dn, pixels, w, h); break;
            case CHUNK_DELTA_FLC: rc = delta_flc(d, dn, pixels, w, h); break;
            case CHUNK_BLACK:
                for (uint32_t i = 0; i < w * h; i++)
                    pixels[i] = 0;
                break;
            case CHUNK_FLI_COPY:
                if (dn < w * h)
                    return FLIC_ERR_DATA;
                for (uint32_t i = 0; i < w * h; i++)
                    pixels[i] = d[i];
                break;
            default:
                f->skipped++;
                break;
            }
            if (rc < 0)
                return rc;
            at += csize;
        }
        f->pos += size;
        f->next++;
        return 1;
    }
}
