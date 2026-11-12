/*
 * flic.h -- an Autodesk FLIC (.FLC) decoder of our own (src/flic.c; docs/DOS_VIDEO.md).
 *
 * The PC game's cutscenes are FLC files: 8 bits a pixel, a 256-colour palette, the first frame
 * whole and every other one a delta of the frame before. The decoder keeps no state but the
 * Flic below: the caller owns the file's bytes, the picture (width x height bytes, palette
 * indices) and the palette (768 bytes, R G B, 0-255), and hands the same picture and palette to
 * every call -- a frame changes only what differs from the one before.
 *
 * Chunks it draws: COLOR_256 (4), DELTA_FLC (7), BLACK (13), BYTE_RUN (15), FLI_COPY (16) --
 * the PC game's 98 files use 4, 7 and 15 only. Any other chunk is skipped (and counted). The
 * files are the player's own, so everything is bounds-checked: a damaged file stops with an
 * error, it never reads or writes past what it was given.
 *
 * Freestanding C99 (no libc), fixed-width types. Built on the SDL targets only (the Amiga never
 * had the cutscenes): Makefile.amiga's SDL_ONLY_SRCS.
 */
#ifndef FLIC_H
#define FLIC_H

#include <stdint.h>

#define FLIC_MAGIC_FLC 0xAF12u

typedef struct Flic {
    const uint8_t *data;
    uint32_t size;
    uint16_t width, height;
    uint16_t frames;        /* the header's: the ring frame after them is not counted */
    uint32_t speedMs;       /* the header's delay between frames, in milliseconds */
    uint32_t first;         /* the first frame's offset */
    uint32_t pos;           /* the next frame's */
    uint16_t next;          /* the next frame's number, 0 the first */
    uint32_t skipped;       /* chunks of a kind it does not draw, so far */
} Flic;

enum {
    FLIC_ERR_HEADER = -1,   /* not an FLC, or a size it cannot be */
    FLIC_ERR_FRAME  = -2,   /* a frame or chunk runs past its end, or the file's */
    FLIC_ERR_DATA   = -3    /* a chunk's data would draw outside the picture, or runs out */
};

/* 0 when `data` (size bytes, kept by the caller while it is used) is an FLC of 8 bits a pixel,
 * with a size of at most 1024 x 1024; FLIC_ERR_HEADER otherwise. */
int32_t Flic_Open(Flic *f, const uint8_t *data, uint32_t size);

/* The next frame into `pixels` (width x height bytes) and `palette` (768): 1 when a frame was
 * drawn, 0 when all `frames` of them have been (the ring frame is never drawn), < 0 a damaged
 * file (FLIC_ERR_*; `pixels` and `palette` may then be half drawn). Before the first frame the
 * caller clears both (a first frame of the PC game's draws every pixel and colour). */
int32_t Flic_Next(Flic *f, uint8_t *pixels, uint8_t *palette);

/* Back to the first frame. */
void Flic_Rewind(Flic *f);

#endif /* FLIC_H */
