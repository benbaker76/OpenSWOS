/*
 * srp_writer.c -- lay out a .SRP file. See include/srp_writer.h and
 * docs/REPLAY_FORMAT_V2.md.
 *
 * Moved out of src/replay_v2.c when the recorder needed to write the same files, so
 * a replay the server kept and the one the player's game wrote cannot differ in
 * anything but what they were given.
 */
#include "srp_writer.h"
#include "endian_helpers.h"
#include "miniz_swos.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t fourcc;
    uint8_t *data;              /* owned */
    uint32_t stored, raw;
} Chunk;

/* Raw deflate when it saves anything, else stored as it is. Takes ownership of `raw`
 * (never NULL). */
static void chunk_make(Chunk *c, uint32_t fourcc, uint8_t *raw, uint32_t rawLen, int32_t compress)
{
    c->fourcc = fourcc;
    c->raw = rawLen;
    c->data = raw;
    c->stored = rawLen;
    if (compress && rawLen > 256) {
        size_t cap = (size_t)mz_compressBound(rawLen);
        uint8_t *packed = (uint8_t *)malloc(cap);
        size_t n = packed ? tdefl_compress_mem_to_mem(packed, cap, raw, rawLen,
                                  tdefl_create_comp_flags_from_zip_params(
                                      MZ_BEST_COMPRESSION, -MZ_DEFAULT_WINDOW_BITS,
                                      MZ_DEFAULT_STRATEGY)) : 0;
        if (n > 0 && n < rawLen) {
            free(raw);
            c->data = packed;
            c->stored = (uint32_t)n;
        } else {
            free(packed);
        }
    }
}

static uint8_t *copy_of(const uint8_t *src, uint32_t len)
{
    uint8_t *p = (uint8_t *)malloc(len ? len : 1);
    if (p && len)
        memcpy(p, src, len);
    return p;
}

static uint8_t *build_audio(const SrpContent *c)
{
    uint8_t *a = (uint8_t *)malloc((size_t)c->audioCount * AUDIO_EVENT_BYTES + 1);
    uint32_t i;
    if (!a)
        return NULL;
    for (i = 0; i < c->audioCount; i++) {
        uint8_t *p = a + i * AUDIO_EVENT_BYTES;
        BE_WRITE_U32(p, c->audio[i].frame);
        p[4] = c->audio[i].kind;
        p[5] = c->audio[i].channel;
        BE_WRITE_U16(p + 6, c->audio[i].id);
    }
    return a;
}

static uint8_t *build_seek(const SrpContent *c, uint32_t *len)
{
    uint32_t count = (c->frameCount + SEEK_INTERVAL - 1) / SEEK_INTERVAL, i, ai = 0;
    uint8_t *s = (uint8_t *)malloc(8 + (size_t)count * 12);
    if (!s)
        return NULL;
    BE_WRITE_U16(s, SEEK_INTERVAL);
    BE_WRITE_U16(s + 2, 0);
    BE_WRITE_U32(s + 4, count);
    for (i = 0; i < count; i++) {
        uint32_t frame = i * SEEK_INTERVAL;
        uint8_t *p = s + 8 + i * 12;
        while (ai < c->audioCount && c->audio[ai].frame < frame)
            ai++;
        BE_WRITE_U32(p, frame);
        BE_WRITE_U32(p + 4, c->offsets[frame]);
        BE_WRITE_U32(p + 8, ai);
    }
    *len = 8 + count * 12;
    return s;
}

static uint8_t *build_chapters(const SrpContent *c, uint32_t *len)
{
    uint8_t *out = (uint8_t *)malloc(4 + (size_t)c->chapterCount * CHAPTER_BYTES);
    uint32_t i;
    if (!out)
        return NULL;
    BE_WRITE_U32(out, c->chapterCount);
    for (i = 0; i < c->chapterCount; i++) {
        uint8_t *p = out + 4 + i * CHAPTER_BYTES;
        BE_WRITE_U32(p, c->chapters[i].frame);
        p[4] = c->chapters[i].kind;
        p[5] = c->chapters[i].team;
        BE_WRITE_U16(p + 6, c->chapters[i].value);
    }
    *len = 4 + c->chapterCount * CHAPTER_BYTES;
    return out;
}

static uint8_t *build_simv(const SrpContent *c)
{
    uint8_t *v = (uint8_t *)calloc(1, SRP_SIMV_BYTES);
    if (!v)
        return NULL;
    BE_WRITE_U16(v, SRP_SIMV_VERSION);
    v[2] = c->rules;
    v[3] = c->style;
    BE_WRITE_U32(v + 4, (uint32_t)(c->layout >> 32));
    BE_WRITE_U32(v + 8, (uint32_t)c->layout);
    BE_WRITE_U16(v + 12, c->simWidth);
    return v;
}

static uint8_t *build_hashes(const SrpContent *c)
{
    uint8_t *h = (uint8_t *)malloc((size_t)c->frameCount * SRP_HASH_BYTES + 1);
    uint32_t i;
    if (!h)
        return NULL;
    for (i = 0; i < c->frameCount; i++)
        BE_WRITE_U32(h + i * SRP_HASH_BYTES, c->hashes[i]);
    return h;
}

/* ---- PRED and SEEN: what each player saw (both halves, whichever are known) ---- */

static uint8_t *build_pred(uint32_t frames, const SrpPrediction *const half[2], uint32_t *len)
{
    uint8_t *out = (uint8_t *)malloc(SRP_PRED_HEAD + (size_t)frames * 2 + 1);
    uint32_t f, i;
    if (!out)
        return NULL;
    BE_WRITE_U16(out, SRP_PRED_VERSION);
    BE_WRITE_U16(out + 2, 0);
    BE_WRITE_U32(out + 4, frames);
    for (f = 0; f < frames; f++)
        for (i = 0; i < 2; i++)
            out[SRP_PRED_HEAD + f * 2 + i] = half[i] && half[i]->pred && f < half[i]->predFrames
                                             ? half[i]->pred[f] : SRP_PRED_UNKNOWN;
    *len = SRP_PRED_HEAD + frames * 2;
    return out;
}

/* READ: the chunk exists when either half has it. */
static int32_t has_read(const SrpPrediction *const half[2])
{
    return (half[0] && half[0]->read) || (half[1] && half[1]->read);
}

static uint8_t *build_read(uint32_t frames, const SrpPrediction *const half[2], uint32_t *len)
{
    uint8_t *out = (uint8_t *)malloc(SRP_READ_HEAD + (size_t)frames * 2 * SRP_READ_BYTES + 1);
    uint32_t f, i, b;
    if (!out)
        return NULL;
    BE_WRITE_U16(out, SRP_READ_VERSION);
    out[2] = (uint8_t)(half[0] && half[0]->read ? half[0]->delay : half[1]->delay);
    out[3] = (uint8_t)((half[0] && half[0]->read && half[0]->balance ? SRP_READ_HEAD_BALANCE1 : 0) |
                       (half[1] && half[1]->read && half[1]->balance ? SRP_READ_HEAD_BALANCE2 : 0));
    BE_WRITE_U32(out + 4, frames);
    for (f = 0; f < frames; f++)
        for (i = 0; i < 2; i++)
            for (b = 0; b < SRP_READ_BYTES; b++)
                out[SRP_READ_HEAD + (f * 2 + i) * SRP_READ_BYTES + b] =
                    half[i] && half[i]->read && f < half[i]->readFrames
                    ? half[i]->read[f * SRP_READ_BYTES + b] : SRP_READ_UNKNOWN;
    *len = SRP_READ_HEAD + frames * 2 * SRP_READ_BYTES;
    return out;
}

/* The size of the SEEN record at `p`, or 0 when it does not fit in `left` bytes. */
static uint32_t seen_record(const uint8_t *p, uint32_t left)
{
    uint32_t n;
    if (left < SRP_SEEN_REC_HEAD)
        return 0;
    n = SRP_SEEN_REC_HEAD + (uint32_t)p[5] * SRP_SEEN_OBJ_BYTES;
    return n <= left ? n : 0;
}

/* The two halves' records merged by frame, then player, and only frames the file has --
 * the same bytes whoever builds it. */
static uint8_t *build_seen(uint32_t frames, const SrpPrediction *const half[2], uint32_t *len)
{
    uint32_t cap = SRP_SEEN_HEAD, at[2] = { 0, 0 }, count = 0, o = SRP_SEEN_HEAD, i;
    uint8_t *out;

    for (i = 0; i < 2; i++)
        if (half[i] && half[i]->seen)
            cap += half[i]->seenLen;
    out = (uint8_t *)malloc(cap + 1);
    if (!out)
        return NULL;
    for (;;) {
        uint32_t best = 2, bestFrame = 0, n;
        for (i = 0; i < 2; i++) {
            const uint8_t *p;
            uint32_t f;
            if (!half[i] || !half[i]->seen)
                continue;
            /* Past what the file holds, or damaged: this half is done. */
            while (at[i] < half[i]->seenLen) {
                p = half[i]->seen + at[i];
                if (!seen_record(p, half[i]->seenLen - at[i]))
                    at[i] = half[i]->seenLen;
                else if (BE_READ_U32(p) >= frames)
                    at[i] = half[i]->seenLen;
                else
                    break;
            }
            if (at[i] >= half[i]->seenLen)
                continue;
            f = BE_READ_U32(half[i]->seen + at[i]);
            if (best == 2 || f < bestFrame) {
                best = i;
                bestFrame = f;
            }
        }
        if (best == 2)
            break;
        n = seen_record(half[best]->seen + at[best], half[best]->seenLen - at[best]);
        memcpy(out + o, half[best]->seen + at[best], n);
        out[o + 4] = (uint8_t)(best + 1);       /* whose it is, by the half it came in */
        o += n;
        at[best] += n;
        count++;
    }
    BE_WRITE_U16(out, SRP_SEEN_VERSION);
    BE_WRITE_U16(out + 2, 0);
    BE_WRITE_U32(out + 4, count);
    *len = o;
    return out;
}

/* PLYR: version, then each player's name as a length byte and the bytes. */
static uint8_t *build_players(const SrpContent *c, uint32_t *len)
{
    uint8_t *out = (uint8_t *)malloc(2 + 2 * 256);
    uint32_t o = 2, i;
    if (!out)
        return NULL;
    BE_WRITE_U16(out, 1);
    for (i = 0; i < 2; i++) {
        size_t n = c->players[i] ? strlen(c->players[i]) : 0;
        if (n > 255)
            n = 255;
        out[o++] = (uint8_t)n;
        memcpy(out + o, c->players[i] ? c->players[i] : "", n);
        o += (uint32_t)n;
    }
    *len = o;
    return out;
}

uint8_t *Srp_Build(const SrpContent *c, uint32_t *outLen)
{
    Chunk chunks[14];
    uint32_t n = 0, total, off, i, seekLen = 0, chapLen = 0, parts = 5, predLen = 0, seenLen = 0,
             plyrLen = 0, readLen = 0, predAt = 0, readAt = 0;
    uint8_t *raw[14], *file, *p, *plyr = NULL;
    int32_t predicted = c->prediction[0] || c->prediction[1];
    int32_t named = c->players[0] || c->players[1];
    int32_t missing = 0;
    int32_t resumable = c->setup && c->setupLen && c->kickoff && c->kickoffLen &&
                        c->inputs && c->hashes && c->frameCount;

    /* Every chunk's raw bytes first: nothing is laid out unless all of them exist. */
    memset(raw, 0, sizeof raw);
    raw[0] = copy_of(c->meta, c->metaLen);
    raw[1] = copy_of(c->frms, c->frmsLen);
    raw[2] = build_audio(c);
    raw[3] = build_seek(c, &seekLen);
    raw[4] = build_chapters(c, &chapLen);
    if (named && !(plyr = build_players(c, &plyrLen)))
        missing = 1;
    if (resumable) {
        raw[5] = build_simv(c);
        raw[6] = copy_of(c->setup, c->setupLen);
        raw[7] = copy_of(c->kickoff, c->kickoffLen);
        raw[8] = copy_of(c->inputs, c->frameCount * SRP_INPUT_BYTES);
        raw[9] = build_hashes(c);
        parts = 10;
    }
    if (predicted) {
        predAt = parts;
        raw[parts] = build_pred(c->frameCount, c->prediction, &predLen);
        raw[parts + 1] = build_seen(c->frameCount, c->prediction, &seenLen);
        parts += 2;
        if (has_read(c->prediction)) {
            readAt = parts;
            raw[parts++] = build_read(c->frameCount, c->prediction, &readLen);
        }
    }
    memset(chunks, 0, sizeof chunks);
    for (i = 0; i < parts; i++)
        if (!raw[i])
            missing = 1;
    if (missing) {
        for (i = 0; i < parts; i++)
            free(raw[i]);
        free(plyr);
        return NULL;
    }
    chunk_make(&chunks[n++], CHUNK_META, raw[0], c->metaLen, 0);
    chunk_make(&chunks[n++], CHUNK_FRMS, raw[1], c->frmsLen, 1);
    chunk_make(&chunks[n++], CHUNK_AUDI, raw[2], c->audioCount * AUDIO_EVENT_BYTES, 1);
    chunk_make(&chunks[n++], CHUNK_SEEK, raw[3], seekLen, 0);
    chunk_make(&chunks[n++], CHUNK_CHAP, raw[4], chapLen, 0);
    /* Before PRED and SEEN, which Srp_WithPrediction replaces at the end: every other
     * chunk keeps its place, so a file rewritten with the other half is still this one. */
    if (plyr)
        chunk_make(&chunks[n++], CHUNK_PLYR, plyr, plyrLen, 0);
    if (resumable) {
        /* The state packet is deflated already; the hashes do not compress. */
        chunk_make(&chunks[n++], CHUNK_SIMV, raw[5], SRP_SIMV_BYTES, 0);
        chunk_make(&chunks[n++], CHUNK_SETP, raw[6], c->setupLen, 1);
        chunk_make(&chunks[n++], CHUNK_STAT, raw[7], c->kickoffLen, 0);
        chunk_make(&chunks[n++], CHUNK_INPT, raw[8], c->frameCount * SRP_INPUT_BYTES, 1);
        chunk_make(&chunks[n++], CHUNK_HASH, raw[9], c->frameCount * SRP_HASH_BYTES, 0);
    }
    if (predicted) {
        chunk_make(&chunks[n++], CHUNK_PRED, raw[predAt], predLen, 1);
        chunk_make(&chunks[n++], CHUNK_SEEN, raw[predAt + 1], seenLen, 1);
        if (readAt)
            chunk_make(&chunks[n++], CHUNK_READ, raw[readAt], readLen, 1);
    }

    total = SWRP_HEADER_BYTES + n * SWRP_DIR_BYTES;
    for (i = 0; i < n; i++)
        total += chunks[i].stored;
    file = (uint8_t *)malloc(total);
    if (file) {
        memcpy(file, "SWRP", 4);
        BE_WRITE_U16(file + 4, SWRP_VERSION);
        BE_WRITE_U16(file + 6, SWRP_FLAG_AUDIO | SWRP_FLAG_SEEK | SWRP_FLAG_CHAPTERS);
        BE_WRITE_U16(file + 8, c->tickRate);
        BE_WRITE_U16(file + 10, c->quirks);
        BE_WRITE_U16(file + 12, 0x2A0);
        BE_WRITE_U16(file + 14, 0x370);
        BE_WRITE_U32(file + 16, c->frameCount);
        BE_WRITE_U16(file + 20, (uint16_t)n);
        BE_WRITE_U16(file + 22, 0);
        BE_WRITE_U32(file + 28, 0);
        off = SWRP_HEADER_BYTES + n * SWRP_DIR_BYTES;
        for (i = 0; i < n; i++) {
            p = file + SWRP_HEADER_BYTES + i * SWRP_DIR_BYTES;
            BE_WRITE_U32(p, chunks[i].fourcc);
            BE_WRITE_U32(p + 4, off);
            BE_WRITE_U32(p + 8, chunks[i].stored);
            BE_WRITE_U32(p + 12, chunks[i].raw);
            memcpy(file + off, chunks[i].data, chunks[i].stored);
            off += chunks[i].stored;
        }
        BE_WRITE_U32(file + 24, (uint32_t)mz_crc32(MZ_CRC32_INIT, file + SWRP_HEADER_BYTES,
                                                   total - SWRP_HEADER_BYTES));
        *outLen = total;
    }
    for (i = 0; i < n; i++)
        free(chunks[i].data);
    return file;
}

uint8_t *Srp_WithPrediction(const uint8_t *file, uint32_t fileLen, const SrpPrediction *p1,
                            const SrpPrediction *p2, uint32_t *outLen)
{
    const SrpPrediction *half[2];
    Chunk added[3];
    uint32_t count, frames, i, kept = 0, total, off, predLen = 0, seenLen = 0, readLen = 0, adds = 2;
    uint8_t *pred, *seen, *read = NULL, *out, *p;

    half[0] = p1;
    half[1] = p2;
    if (fileLen < SWRP_HEADER_BYTES || memcmp(file, "SWRP", 4) != 0)
        return NULL;
    count = BE_READ_U16(file + 20);
    frames = BE_READ_U32(file + 16);
    if (SWRP_HEADER_BYTES + count * SWRP_DIR_BYTES > fileLen)
        return NULL;
    total = SWRP_HEADER_BYTES;
    for (i = 0; i < count; i++) {
        const uint8_t *d = file + SWRP_HEADER_BYTES + i * SWRP_DIR_BYTES;
        uint32_t fourcc = BE_READ_U32(d), o = BE_READ_U32(d + 4), stored = BE_READ_U32(d + 8);
        if (o > fileLen || stored > fileLen - o)
            return NULL;
        if (fourcc == CHUNK_PRED || fourcc == CHUNK_SEEN || fourcc == CHUNK_READ)
            continue;
        total += SWRP_DIR_BYTES + stored;
        kept++;
    }
    pred = build_pred(frames, half, &predLen);
    seen = build_seen(frames, half, &seenLen);
    if (has_read(half))
        read = build_read(frames, half, &readLen);
    if (!pred || !seen || (has_read(half) && !read)) {
        free(pred);
        free(seen);
        free(read);
        return NULL;
    }
    /* Made exactly as Srp_Build makes them, so the file is the one it would have built. */
    chunk_make(&added[0], CHUNK_PRED, pred, predLen, 1);
    chunk_make(&added[1], CHUNK_SEEN, seen, seenLen, 1);
    total += 2 * SWRP_DIR_BYTES + added[0].stored + added[1].stored;
    if (read) {
        chunk_make(&added[2], CHUNK_READ, read, readLen, 1);
        total += SWRP_DIR_BYTES + added[2].stored;
        adds = 3;
    }
    out = (uint8_t *)malloc(total);
    if (out) {
        uint32_t n = 0;
        memcpy(out, file, SWRP_HEADER_BYTES);
        BE_WRITE_U16(out + 20, (uint16_t)(kept + adds));
        off = SWRP_HEADER_BYTES + (kept + adds) * SWRP_DIR_BYTES;
        for (i = 0; i < count; i++) {
            const uint8_t *d = file + SWRP_HEADER_BYTES + i * SWRP_DIR_BYTES;
            uint32_t fourcc = BE_READ_U32(d), stored = BE_READ_U32(d + 8);
            if (fourcc == CHUNK_PRED || fourcc == CHUNK_SEEN || fourcc == CHUNK_READ)
                continue;
            p = out + SWRP_HEADER_BYTES + n++ * SWRP_DIR_BYTES;
            BE_WRITE_U32(p, fourcc);
            BE_WRITE_U32(p + 4, off);
            BE_WRITE_U32(p + 8, stored);
            BE_WRITE_U32(p + 12, BE_READ_U32(d + 12));
            memcpy(out + off, file + BE_READ_U32(d + 4), stored);
            off += stored;
        }
        for (i = 0; i < adds; i++) {
            p = out + SWRP_HEADER_BYTES + n++ * SWRP_DIR_BYTES;
            BE_WRITE_U32(p, added[i].fourcc);
            BE_WRITE_U32(p + 4, off);
            BE_WRITE_U32(p + 8, added[i].stored);
            BE_WRITE_U32(p + 12, added[i].raw);
            memcpy(out + off, added[i].data, added[i].stored);
            off += added[i].stored;
        }
        BE_WRITE_U32(out + 24, (uint32_t)mz_crc32(MZ_CRC32_INIT, out + SWRP_HEADER_BYTES,
                                                  total - SWRP_HEADER_BYTES));
        *outLen = total;
    }
    free(added[0].data);
    free(added[1].data);
    if (read)
        free(added[2].data);
    return out;
}
