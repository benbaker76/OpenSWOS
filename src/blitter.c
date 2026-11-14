/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "blitter.h"
#include "text_render.h"
#include "display.h"
#include "swos_types.h"
#include <string.h>

/* ---- Blitter register write macros ----
 * GCC m68k 15.1.0 decomposes `volatile uint16_t = N` and similar struct-
 * member stores into byte-pair `move.b` instructions in some contexts.
 * For hardware-trigger registers like `bltsize` (writing it starts the
 * blitter), a decomposed write triggers the blitter mid-update with a
 * half-formed size value — the blit either does nothing or writes
 * garbage. The original Amiga asm used real `move.w` instructions and
 * works correctly.
 *
 * These macros wrap the writes in inline `move.w` / `move.l` asm so the
 * single-instruction store is guaranteed on m68k. On SDL the writes go
 * through plain assignment (the Custom struct lives in a stub buffer,
 * not real hardware, and the writes are no-ops anyway). We already use
 * this pattern in display.c for copper writes (`copper_write_word` /
 * `copper_write_ptr`) — see those for context. */
#if defined(__m68k__) || defined(__amigaos__) || defined(__amigaos4__)
#define BLT_W(reg, val)                                                     \
    do {                                                                    \
        uint16_t _v = (uint16_t)(val);                                      \
        volatile uint16_t *_p = &(reg);                                     \
        __asm__ volatile ("move.w %0,(%1)" : : "d"(_v), "a"(_p) : "memory");\
    } while (0)
#define BLT_L(reg, val)                                                     \
    do {                                                                    \
        uint32_t _v = (uint32_t)(val);                                      \
        volatile uint32_t *_p = &(reg);                                     \
        __asm__ volatile ("move.l %0,(%1)" : : "d"(_v), "a"(_p) : "memory");\
    } while (0)
#else
#define BLT_W(reg, val) ((void)((reg) = (uint16_t)(val)))
#define BLT_L(reg, val) ((void)((reg) = (uint32_t)(val)))
#endif

/* ---- External globals (all live in SWOS2.s) ---- */
extern uint8_t *_spriteGraphicsPtr;
extern volatile uintptr_t _frontbufferBasePtr;
extern uint16_t _cameraXMod320;
extern uint16_t _cameraYMod320;
extern volatile uint16_t _panicReasonCode;

extern void PanicHaltLoop(void) __attribute__((noreturn));

/* CharsTableHeader is defined in game_types.h */


static inline __attribute__((always_inline)) void waitBlit(void)
{
    _custom->dmacon = (uint16_t)DMACON_BLT_ON;
    (void)(_custom->dmaconr & (1u << 14));
    while (BLIT_BUSY())
        ;
    _custom->dmacon = (uint16_t)DMACON_BLT_OFF;
}

/* ================================================================
 * Mask lookup tables (originally PC-relative data in SWOS2.s)
 * ================================================================ */

/* leftMaskTable: indexed by (xBit * 2), xBit = 1..15 → entry 0..14 */
static const uint16_t leftMaskTable[15] = {
    0x8000, 0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00,
    0xFF80, 0xFFC0, 0xFFE0, 0xFFF0, 0xFFF8, 0xFFFC, 0xFFFE
};

/* partialSpanMaskTable: indexed by (overrun * 2), overrun = 1..15 → entry 0..14
   Entry 0 in asm was $4E75 (rts opcode) — never used as a mask value. */
static const uint16_t partialSpanMaskTable[15] = {
    0x0001, 0x0003, 0x0007, 0x000F, 0x001F, 0x003F, 0x007F,
    0x00FF, 0x01FF, 0x03FF, 0x07FF, 0x0FFF, 0x1FFF, 0x3FFF, 0x7FFF
};

/* tailMaskTable: indexed by (pixels & TILE_MASK) * 2, values 0..15 */
static const uint16_t tailMaskTable[16] = {
    0xFFFF, 0x8000, 0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00,
    0xFF00, 0xFF80, 0xFFC0, 0xFFE0, 0xFFF0, 0xFFF8, 0xFFFC, 0xFFFE
};

/* firstWordMaskTable / lastWordMaskTable for blitter_rectrow_masked */
static const uint16_t firstWordMaskTable[16] = {
    0xFFFF, 0x7FFF, 0x3FFF, 0x1FFF, 0x0FFF, 0x07FF, 0x03FF, 0x01FF,
    0x00FF, 0x007F, 0x003F, 0x001F, 0x000F, 0x0007, 0x0003, 0x0001
};

static const uint16_t lastWordMaskTable[16] = {
    0x8000, 0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00,
    0xFF80, 0xFFC0, 0xFFE0, 0xFFF0, 0xFFF8, 0xFFFC, 0xFFFE, 0xFFFF
};


/* ================================================================
 * blitterSpanCore  (static)
 *
 * Programs the Amiga blitter to fill/clear a horizontal span on
 * a single bitplane. Handles left edge, middle words, and right edge.
 *
 * d5 = BLTCON0 for edge blits (left/right)
 * d6 = BLTCON0 for middle words
 * ================================================================ */
static void blitterSpanCore(uint8_t *plane, uint16_t xBit, uint16_t width,
                            uint16_t height, uint16_t d5, uint16_t d6)
{
    uint16_t d7, d0;

    /* ---- Left edge ---- */
    if (xBit != 0) {
        d7 = leftMaskTable[xBit - 1];
        d0 = 16 - xBit;

        if ((int16_t)(width - d0) < 0) {
            /* Span fits entirely within left edge word */
            uint16_t overrun = d0 - width;
            d7 |= partialSpanMaskTable[overrun - 1];
            width = 0;
        } else {
            width -= d0;
        }

        /* Left edge: 1-word masked blit.
           BLTBDAT holds ~mask (span pixels) even though USEB=0;
           the blitter uses BLTBDAT as constant B input in the minterm. */
        waitBlit();
        BLT_W(_custom->bltafwm, d7);
        BLT_W(_custom->bltalwm, d7);
        d7 = ~d7;
        BLT_W(_custom->bltbdat, d7);
        BLT_L(_custom->bltapt, (uintptr_t)plane);
        BLT_L(_custom->bltdpt, (uintptr_t)plane);
        BLT_W(_custom->bltamod, FB_BLT_MOD_AFTER_1_WORD);
        BLT_W(_custom->bltdmod, FB_BLT_MOD_AFTER_1_WORD);
        BLT_W(_custom->bltcon0, d5);
        BLT_W(_custom->bltcon1, 0);
        {
            uint16_t blitsize = (height << 6) | 1;
            BLT_W(_custom->bltsize, blitsize);
        }

        plane += 2;

        if (width == 0)
            return;
    }

    /* ---- Middle words ---- */
    d0 = width >> 4;
    if (d0 != 0) {
        uint16_t mod = ROW_STRIDE_BYTES - d0 * 2;

        waitBlit();
        BLT_L(_custom->bltdpt, (uintptr_t)plane);
        BLT_W(_custom->bltdmod, mod);
        BLT_W(_custom->bltcon0, d6);
        BLT_W(_custom->bltcon1, 0);
        {
            uint16_t blitsize = (height << 6) | d0;
            BLT_W(_custom->bltsize, blitsize);
        }

        plane += d0 * 2;
    }

    /* ---- Right edge ---- */
    {
        uint16_t tail = width & TILE_MASK;
        if (tail != 0) {
            d7 = ~tailMaskTable[tail];

            waitBlit();
            BLT_W(_custom->bltafwm, d7);
            BLT_W(_custom->bltalwm, d7);
            d7 = ~d7;
            BLT_W(_custom->bltbdat, d7);
            BLT_L(_custom->bltapt, (uintptr_t)plane);
            BLT_L(_custom->bltdpt, (uintptr_t)plane);
            BLT_W(_custom->bltamod, FB_BLT_MOD_AFTER_1_WORD);
            BLT_W(_custom->bltdmod, FB_BLT_MOD_AFTER_1_WORD);
            BLT_W(_custom->bltcon0, d5);
            BLT_W(_custom->bltcon1, 0);
            {
                uint16_t blitsize = (height << 6) | 1;
                BLT_W(_custom->bltsize, blitsize);
            }
        }
    }
}

/* ================================================================
 * blitterSpanCoreMasked  (static)
 *
 * Wrapper: selects d5/d6 based on color bit, then calls blitterSpanCore.
 * ================================================================ */
static void blitterSpanCoreMasked(uint8_t *plane, uint16_t xBit, uint16_t width,
                                  uint16_t height, uint16_t colorBit, uint16_t custom_unused)
{
    uint16_t d5, d6;

    (void)custom_unused;

    if (colorBit & 1) {
        d5 = BLTCON0_USEA_USED_LF_FC;
        d6 = BLTCON0_USED_LF_FF;
    } else {
        d5 = BLTCON0_USEA_USED_LF_F0;
        d6 = BLTCON0_USED_LF_00;
    }

    blitterSpanCore(plane, xBit, width, height, d5, d6);
}


/* ================================================================
 * blitterSpanAllPlanes  (static)
 *
 * Fills a horizontal span across all bitplanes.
 * Calls blitterSpanCoreMasked once per plane.
 *
 * In:
 *   d0 = x pixel
 *   d1 = y pixel row
 *   d2 = width in pixels
 *   d3 = height in rows
 *   d4 = color (NUM_PLANES-bit)
 *   a0 = framebuffer base
 * ================================================================ */
static void blitterSpan4Planes(uint16_t x, uint16_t y, uint16_t width,
                               uint16_t height, uint16_t color, uint8_t *buf)
{
    uint16_t xBit;
    uint8_t *plane;
    int16_t p;

    /* Compute plane base pointer */
    plane = buf + (uint32_t)y * ROW_STRIDE_BYTES;

    /* Word-align the x coordinate */
    plane += (x >> 3) & 0xFFFE;   /* WORD_ALIGN_EVEN_MASK = ~1 */

    xBit = x & TILE_MASK;

    /* Initial pass: prime plane 0 with USEA-only edges (no D write) and
     * USED-only middle (clears to 0). Matches the ASM's extra call to
     * align_and_prepare_left_mask before the 4 per-plane passes.
     * Edge d5 = BLTCON0_USEA_LF_FC (USEA only, USED=0 → edges don't write).
     * Middle d6 = BLTCON0_USED_LF_00 (USED=1, LF=00 → clears middle). */
    blitterSpanCore(plane, xBit, width, height,
                    BLTCON0_USEA_LF_FC, BLTCON0_USED_LF_00);

    /* Per-plane pass */
    for (p = 0; p < NUM_PLANES; p++)
    {
        blitterSpanCoreMasked(plane, xBit, width, height, color >> p, 0);
        plane += PLANE_STRIDE;
    }
}


/* ================================================================
 * FillRectangle
 *
 * Fills a rectangle on the frontbuffer with a solid color.
 * Handles vertical wrapping at SCREEN_HEIGHT (272).
 * Applies camera X/Y offsets.
 *
 * In (from asm):
 *   d0 = x, d1 = y, d2 = width, d3 = height, d4 = color
 *   a0 = frontbufferBasePtr (loaded internally)
 * ================================================================ */
#ifdef SWOS_SDL_BUILD
/* SDL build: write to the chunky 8bpp frontbuffer. 1 byte per pixel =
 * palette index. Row stride = _screen_width. */
static void fillRectChunky(uint16_t x0, uint16_t y0, uint16_t w, uint16_t h,
                           uint16_t color, uint8_t *buf)
{
    uint8_t pixel = (uint8_t)color;
    for (uint16_t dy = 0; dy < h; dy++) {
        uint16_t yy = (uint16_t)(y0 + dy);
        if (yy >= SCREEN_HEIGHT) continue;
        uint8_t *row = buf + (uint32_t)yy * FRONTBUFFER_ROW_STRIDE_BYTES;
        for (uint16_t dx = 0; dx < w; dx++) {
            uint16_t xx = (uint16_t)(x0 + dx);
            if (xx >= _screen_width) continue;
            row[xx] = pixel;
        }
    }
}
#endif

__attribute__((used, externally_visible))
void FillRectangle(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    uint8_t *buf = (uint8_t *)_frontbufferBasePtr + _menu_draw_x_offset;
    uint16_t wrapRows;
    int16_t overflow;

#ifndef SWOS_SDL_BUILD
    /* Amiga: hardware-scroll wrap offsets are part of how the display
     * pans its bitplane base pointer — coordinates need to be shifted
     * by the current wrap offset to land in the right physical row.
     * On SDL we redraw the whole frontbuffer each frame and dstX/dstY
     * are already in final screen coords, so adding the wrap offsets
     * misaligns rectangles relative to text/sprites (whose blits
     * already skip these offsets — see blitChunkySprite and
     * blitGlyphBitplanes). Without this guard, in-match menu rows
     * (substitutes panel, formation menu, bench overlays) had their
     * coloured backgrounds shifted ~7 pixels lower than the text
     * inside them. */
    x += _cameraXMod320;
    y += _cameraYMod320;

#if !defined(AGA)
    if (y >= SCREEN_HEIGHT)
        y -= SCREEN_HEIGHT;
#endif
#endif

    /* Check for vertical wrap (still needed on SDL when a caller asks
     * for a rectangle that straddles the bottom edge — rare, but the
     * wrap math is harmless when y starts in-range). */
    wrapRows = 0;
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
    /* AGA: no wrap -- the display reads the 2x-tall buffer straight down from
     * cameraYMod320 (see blitStringToFrontBuffer). Only clip at the buffer's end. */
    overflow = (int16_t)(y + h - 2 * SCREEN_HEIGHT);
    if (overflow > 0) {
        if ((uint16_t)overflow >= h)
            return;
        h -= (uint16_t)overflow;
    }
#else
    overflow = (int16_t)(y + h - SCREEN_HEIGHT);
    if (overflow > 0) {
        wrapRows = (uint16_t)overflow;
        h -= wrapRows;
    }
#endif

#ifdef SWOS_SDL_BUILD
    if (buf) {
        fillRectChunky(x, y, w, h, color, buf);
        if (wrapRows != 0)
            fillRectChunky(x, 0, w, wrapRows, color, buf);
    }
#else
    /* Blit first span */
    blitterSpan4Planes(x, y, w, h, color, buf);

    /* Blit wrapped portion (starts at row 0) */
    if (wrapRows != 0) {
        blitterSpan4Planes(x, 0, w, wrapRows, color, buf);
    }
#endif
}


/* ================================================================
 * blitterRectRowMasked  (static)
 *
 * Blits a masked rectangular region from source to destination buffer.
 * First fills the span in the dest buffer, then applies a word-aligned
 * masked blit from src (mask) to dest.
 *
 * In:
 *   d0 = x pixel, d1 = y pixel, d2 = width, d3 = height
 *   a0 = mask buffer, a1 = dest buffer
 * ================================================================ */
static void blitterRectRowMasked(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                 uint8_t *mask, uint8_t *buf)
{
#ifdef SWOS_SDL_BUILD
    /* SDL chunky path: plain rectangle copy from mask → dst. The Amiga
     * version below programs the chip blitter with a USEA+USEB+USED
     * masked minterm, which on SDL is a no-op (writes hit the stub
     * Custom struct). Without this, `DrawMenuItemBackgroundMasked`
     * never actually clears anything, and menu-entry text redraws
     * (e.g. "SELECT 2 MORE TEAMS" → "SELECT 1 MORE TEAM") end up
     * OR'd on top of each other.
     *
     * Each source byte is a palette index; index 0 = transparent on
     * sprite blits but for the menu "clear from clean panel" we want
     * to OVERWRITE unconditionally so stale text pixels disappear. */
    if (mask && buf) {
        for (uint16_t dy = 0; dy < h; dy++) {
            uint16_t yy = (uint16_t)(y + dy);
            if (yy >= SCREEN_HEIGHT) continue;
            const uint8_t *srcRow = mask + (uint32_t)yy * FRONTBUFFER_ROW_STRIDE_BYTES;
            uint8_t       *dstRow = buf  + (uint32_t)yy * FRONTBUFFER_ROW_STRIDE_BYTES;
            uint16_t       copyW  = w;
            if ((uint32_t)x + copyW > _screen_width) copyW = (uint16_t)(_screen_width - x);
            memcpy(dstRow + x, srcRow + x, copyW);
        }
    }
    return;
#else
    uint16_t wordStart, wordEnd, wordSpan;
    uint16_t fwm, lwm;
    uint16_t mod;
    uint8_t *srcRow, *dstRow;

    /* First pass: fill the span in dest with color 0 (clear) */
    blitterSpan4Planes(x, y, w, h, 0, buf);

    /* Scale y by NUM_PLANES worth of stride */
    {
        uint32_t offset = (uint32_t)(y << NUM_PLANES_SHIFT) * PLANE_STRIDE;
        mask += offset;
        buf += offset;
    }

    /* Scale h by NUM_PLANES */
    h <<= NUM_PLANES_SHIFT;

    /* Compute word boundaries */
    wordStart = x >> 4;
    wordEnd = (x + w - 1) >> 4;
    wordSpan = wordEnd - wordStart;

    /* Advance pointers to start word */
    mask += wordStart * 2;
    buf += wordStart * 2;

    /* Compute first/last word masks */
    fwm = firstWordMaskTable[x & TILE_MASK];
    lwm = lastWordMaskTable[(x + w - 1) & TILE_MASK];

    /* Wait for blitter idle */
    waitBlit();

    wordSpan += 1;

    BLT_W(_custom->bltcon0, BLTCON0_USEA_USEB_USED_LF_FC);
    BLT_W(_custom->bltcon1, 0);
    BLT_W(_custom->bltafwm, fwm);
    BLT_W(_custom->bltalwm, lwm);
    BLT_L(_custom->bltapt, (uint32_t)mask);
    BLT_L(_custom->bltbpt, (uint32_t)buf);
    BLT_L(_custom->bltdpt, (uint32_t)buf);
    mod = PLANE_STRIDE - wordSpan * 2;
    BLT_W(_custom->bltamod, mod);
    BLT_W(_custom->bltbmod, mod);
    BLT_W(_custom->bltdmod, mod);
    {
        uint16_t blitsize = (h << 6) | wordSpan;
        BLT_W(_custom->bltsize, blitsize);
    }
#endif
}


/* ================================================================
 * RectRowMaskedApplyScreenOffset
 *
 * Thin wrapper: adds camera offsets then calls blitterRectRowMasked.
 * ================================================================ */
__attribute__((used, externally_visible))
void RectRowMaskedApplyScreenOffset(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                     uint8_t *mask, uint8_t *buf)
{
#ifdef SWOS_SDL_BUILD
    /* Do NOT add cameraX/YMod320 on SDL — those are Amiga hardware-scroll
     * wrap offsets for the panning bitplane base pointer, and every other
     * SDL render path (blitChunkySprite, blitStringToFrontBuffer,
     * DrawChunkyPitch) writes at raw screen coordinates. If we add the
     * offsets here, the "clear from mask" rectangle lands at a different
     * screen location than the text that the draw routine is about to
     * write, so the old entry pixels (e.g. "SELECT 2 MORE TEAMS") stay
     * visible underneath the new text ("SELECT 1 MORE TEAM"). */
#else
    x += _cameraXMod320;
    y += _cameraYMod320;
#endif
    blitterRectRowMasked(x, y, w, h, mask, buf);
}


/* ================================================================
 * BlitSpritePlanar_Word
 *
 * Builds a 1-plane OR-combined mask from 4 interleaved bitplanes.
 * Two paths: normal (word-at-a-time) and fast (longword for 2-wide sprites).
 * If sprite type != 0 (linked), writes mask to all 4 output planes.
 * ================================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void BlitSpritePlanar_Word(uint16_t spriteIndex)
{
#ifdef SWOS_SDL_BUILD
    /* 8bpp chunky sprites have implicit transparency (pixel == 0), so
     * there is no separate mask buffer to build. This function is a
     * no-op on SDL — called at the end of Text2Sprite to finalize
     * the Amiga linked-planes mask, which doesn't exist here. */
    (void)spriteIndex;
    return;
#else
    uint8_t *base = _spriteGraphicsPtr;
    SpriteGraphics *hdr;
    uint16_t type;
    uint8_t *srcData;
    uint8_t *dstMask;
    uint16_t nlines, wquads;
    uint16_t stride2;
    int32_t advance;
    int16_t row, col;

    hdr = ((SpriteGraphics **)_spriteGraphicsPtr)[spriteIndex];
    type = hdr->type;
    srcData = hdr->sprData;
    dstMask = hdr->maskPtr;
    nlines = (uint16_t)hdr->nlines;
    wquads = (uint16_t)hdr->wquads;
    /* Sprite's real bitplane depth (4 normal / 8 soccer_s-replay). Was hard NUM_PLANES(8);
     * both srcData and (type!=0) dstMask are the sprite's own `sp`-plane buffers. */
    uint16_t sp = spriteNumPlanes(hdr->ordinal);

    stride2 = wquads * 2;  /* bytes per plane per row */
    advance = (int32_t)(stride2 * sp);  /* bytes per full row (all planes) */

    if (type == 0) {
        /* ---- 1-plane output ---- */
        if (wquads == 2) {
            /* Fast path: longword access for 2-word-wide sprites.
               OR all planes together into a single-plane mask.
               Note: dstMask is NOT advanced per row (matches asm behavior). */
            int16_t r, p;

            for (r = nlines - 1; r >= 0; r--) {
                uint32_t val = *(uint32_t *)srcData;
                for (p = 1; p < (int16_t)sp; p++)
                    val |= *(uint32_t *)(srcData + p * stride2);
                *(uint32_t *)dstMask = val;
                srcData += advance;
            }
        } else {
            /* General path: word-at-a-time */
            uint16_t wordsPerRow = wquads;
            int32_t rowSkip = (int32_t)(stride2 * (sp - 1));

            for (row = nlines - 1; row >= 0; row--) {
                for (col = wordsPerRow - 1; col >= 0; col--) {
                    uint16_t val = *(uint16_t *)srcData;
                    int16_t p;
                    for (p = 1; p < (int16_t)sp; p++)
                        val |= *(uint16_t *)(srcData + p * stride2);
                    *(uint16_t *)dstMask = val;
                    srcData += 2;
                    dstMask += 2;
                }
                srcData += rowSkip;
            }
        }
    } else {
        /* ---- sp-plane output (type != 0) ---- */
        if (wquads == 2) {
            /* Fast path: longword access, output replicated to all planes */
            int16_t r, p;

            for (r = nlines - 1; r >= 0; r--) {
                uint32_t val = *(uint32_t *)srcData;
                for (p = 1; p < (int16_t)sp; p++)
                    val |= *(uint32_t *)(srcData + p * stride2);
                for (p = 0; p < (int16_t)sp; p++)
                    *(uint32_t *)(dstMask + p * stride2) = val;
                srcData += advance;
                dstMask += advance;
            }
        } else {
            /* General path: word-at-a-time, all output planes */
            uint16_t wordsPerRow = wquads;
            int32_t rowSkip = (int32_t)(stride2 * (sp - 1));

            for (row = nlines - 1; row >= 0; row--) {
                for (col = wordsPerRow - 1; col >= 0; col--) {
                    uint16_t val = *(uint16_t *)srcData;
                    int16_t p;
                    for (p = 1; p < (int16_t)sp; p++)
                        val |= *(uint16_t *)(srcData + p * stride2);
                    for (p = 0; p < (int16_t)sp; p++)
                        *(uint16_t *)(dstMask + p * stride2) = val;
                    srcData += 2;
                    dstMask += 2;
                }
                srcData += rowSkip;
                dstMask += rowSkip;
            }
        }
    }
#endif /* !SWOS_SDL_BUILD for BlitSpritePlanar_Word */
}


/* ================================================================
 * BlitSpritePlanar_Word_StridedDest
 *
 * Like BlitSpritePlanar_Word but destination advances by stride2
 * per row instead of full 4-plane stride. Uses word access only.
 * ================================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void BlitSpritePlanar_Word_StridedDest(uint16_t spriteIndex)
{
#ifdef SWOS_SDL_BUILD
    /* 8bpp chunky sprites have implicit transparency (pixel == 0) so
     * no separate mask buffer exists; same rationale as the sibling
     * BlitSpritePlanar_Word. Amiga path kept for the #else branch. */
    (void)spriteIndex;
    return;
#else
    uint8_t *base = _spriteGraphicsPtr;
    SpriteGraphics *hdr;
    uint16_t type;
    uint8_t *srcData, *dstMask;
    uint16_t nlines;
    uint16_t stride2, stride3, stride4;
    int32_t srcAdvance, dstAdvance;
    int16_t row;

    hdr = ((SpriteGraphics **)_spriteGraphicsPtr)[spriteIndex];
    type = hdr->type;
    srcData = hdr->sprData;
    dstMask = hdr->maskPtr;
    nlines = (uint16_t)hdr->nlines;

    stride2 = (uint16_t)hdr->wquads * 2;
    {
    uint16_t sp = spriteNumPlanes(hdr->ordinal);  /* sprite's real depth (4/8) */
    srcAdvance = (int32_t)(stride2 * sp);
    dstAdvance = (int32_t)stride2;

    if (type == 0) {
        /* 1-plane output */
        for (row = nlines - 1; row >= 0; row--) {
            uint16_t val = *(uint16_t *)srcData;
            int16_t p;
            for (p = 1; p < (int16_t)sp; p++)
                val |= *(uint16_t *)(srcData + p * stride2);
            *(uint16_t *)dstMask = val;
            srcData += srcAdvance;
            dstMask += dstAdvance;
        }
    } else {
        /* sp-plane output */
        for (row = nlines - 1; row >= 0; row--) {
            uint16_t val = *(uint16_t *)srcData;
            int16_t p;
            for (p = 1; p < (int16_t)sp; p++)
                val |= *(uint16_t *)(srcData + p * stride2);
            for (p = 0; p < (int16_t)sp; p++)
                *(uint16_t *)(dstMask + p * stride2) = val;
            srcData += srcAdvance;
            dstMask += srcAdvance;
        }
    }
    }
#endif /* !SWOS_SDL_BUILD */
}


/* ================================================================
 * BlitSpritePlanar_Long_StridedDest
 *
 * Like BlitSpritePlanar_Word_StridedDest but uses 32-bit (longword)
 * access for wider sprites.
 * ================================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void BlitSpritePlanar_Long_StridedDest(uint16_t spriteIndex)
{
#ifdef SWOS_SDL_BUILD
    /* See sibling BlitSpritePlanar_Word — no separate mask on 8bpp. */
    (void)spriteIndex;
    return;
#else
    uint8_t *base = _spriteGraphicsPtr;
    SpriteGraphics *hdr;
    uint16_t type;
    uint8_t *srcData, *dstMask;
    uint16_t nlines;
    uint16_t stride2;
    int32_t srcAdvance, dstAdvance;
    int16_t row;

    hdr = ((SpriteGraphics **)_spriteGraphicsPtr)[spriteIndex];
    type = hdr->type;
    srcData = hdr->sprData;
    dstMask = hdr->maskPtr;
    nlines = (uint16_t)hdr->nlines;

    stride2 = (uint16_t)hdr->wquads * 2;
    {
    uint16_t sp = spriteNumPlanes(hdr->ordinal);  /* sprite's real depth (4/8) */
    srcAdvance = (int32_t)(stride2 * sp);
    dstAdvance = (int32_t)stride2;

    if (type == 0) {
        /* 1-plane output */
        for (row = nlines - 1; row >= 0; row--) {
            uint32_t val = *(uint32_t *)srcData;
            int16_t p;
            for (p = 1; p < (int16_t)sp; p++)
                val |= *(uint32_t *)(srcData + p * stride2);
            *(uint32_t *)dstMask = val;
            srcData += srcAdvance;
            dstMask += dstAdvance;
        }
    } else {
        /* sp-plane output */
        for (row = nlines - 1; row >= 0; row--) {
            uint32_t val = *(uint32_t *)srcData;
            int16_t p;
            for (p = 1; p < (int16_t)sp; p++)
                val |= *(uint32_t *)(srcData + p * stride2);
            for (p = 0; p < (int16_t)sp; p++)
                *(uint32_t *)(dstMask + p * stride2) = val;
            srcData += srcAdvance;
            dstMask += srcAdvance;
        }
    }
    }
#endif /* !SWOS_SDL_BUILD */
}


/* ================================================================
 * BlitGlyphToSpriteBitmap
 *
 * Uses the Amiga blitter to render a glyph (character sprite) into
 * a destination sprite bitmap. Programs the blitter for each of
 * 4 bitplanes with appropriate minterm/function codes.
 *
 * In:
 *   glyphIndex     - source glyph sprite index
 *   xOff           - x pixel offset in destination sprite
 *   yOff           - y offset (row) in destination sprite
 *   planeMask      - 4-bit mask: which planes get the glyph color
 *   dstSpriteIndex - destination sprite index
 * ================================================================ */
#ifdef SWOS_SDL_BUILD
/* Chunky 8bpp rewrite of BlitGlyphToSpriteBitmap. The Amiga version
 * programs the blitter with bltcon0/bltcon1/bltsize to stamp a 1-bit
 * glyph mask into the 4 bitplanes of a destination sprite, setting
 * pixels to the color encoded by `planeMask`. On our 8bpp pipeline,
 * every sprite — both glyph and destination — is a flat byte array
 * where each byte is a palette index (0 = transparent). Rewriting as
 * a straight per-pixel copy: wherever the glyph pixel is non-zero,
 * write `planeMask` (the 0..15 color value) into the destination
 * sprite at (xOff+x, yOff+y).
 *
 * This replaces the entire Amiga blitter setup; the `custom->blt*`
 * writes in the #else branch write into a dummy struct on SDL and
 * never actually move pixels, which is why name labels / stars came
 * out as the initial checkerboard fill. */
__attribute__((used, externally_visible))
void BlitGlyphToSpriteBitmap(uint16_t glyphIndex, uint16_t xOff, uint16_t yOff,
                              uint16_t planeMask, uint16_t dstSpriteIndex)
{
    SpriteGraphics **table = (SpriteGraphics **)(uintptr_t)_spriteGraphicsPtr;
    SpriteGraphics *srcHdr = table[glyphIndex];
    SpriteGraphics *dstHdr = table[dstSpriteIndex];
    {
        extern int _debug_sprite_load;
        if (_debug_sprite_load) {
            SWOS_DBG("[blit] glyph=0x%03X -> dst=0x%03X @(%u,%u) pm=%u srcHdr=%p dstHdr=%p\n",
                    glyphIndex, dstSpriteIndex, xOff, yOff, planeMask,
                    (void*)srcHdr, (void*)dstHdr);
        }
    }
    if (!srcHdr || !dstHdr) return;

    const uint8_t *src = srcHdr->sprData;
    uint8_t       *dst = (uint8_t       *)(uintptr_t)dstHdr->sprData;
    if (!src || !dst) return;

    int16_t srcW   = srcHdr->pixWidth;  /* actual width, may not be 16-aligned */
    int16_t srcH   = srcHdr->nlines;
    int16_t srcPitch = srcHdr->wquads * 16;
    int16_t dstPitch = dstHdr->wquads * 16;
    int16_t dstH   = dstHdr->nlines;
    int16_t dstW   = dstHdr->wquads * 16;  /* stored pitch; pixWidth may clip */

    /* Body/shadow remap — mirrors `blitGlyphBitplanes` in text_render.c.
     * Glyph fonts encode the text body in palette bit 1 (colour 2) and the
     * drop-shadow in the charset's FIXED plane, GLYPH_SHADOW_PLANE (bit 3 /
     * colour 8). planeMask selects the body colour; the shadow always lands on
     * the shadow plane so it reads a different palette entry (typically black).
     *
     * This used to key the shadow off NUM_PLANES-1, which is only the same bit
     * on a 4-plane OCS build. Once `#define AGA` made NUM_PLANES 8 for SDL,
     * NUM_PLANES-1 became bit 7 while the charset still stores its shadow at
     * bit 3 — so shadowBit was ALWAYS 0 and no shadow pixel was ever written.
     * Sprite-baked text (in-match player names via Text2Sprite, team names)
     * rendered flat white. b28c52e fixed exactly this in the frontbuffer text
     * path (text_render.c) but did not touch this one, which is why menu and
     * HUD text kept their shadows while the player name lost its. */
    uint16_t color = (uint16_t)(planeMask & 0xFF);

    for (int16_t y = 0; y < srcH; y++) {
        int16_t dy = (int16_t)(yOff + y);
        if (dy < 0 || dy >= dstH) continue;
        const uint8_t *srcRow = src + (size_t)y * srcPitch;
        uint8_t       *dstRow = dst + (size_t)dy * dstPitch;
        for (int16_t x = 0; x < srcW; x++) {
            int16_t dx = (int16_t)(xOff + x);
            if (dx < 0 || dx >= dstW) continue;

            uint8_t s = srcRow[x];
            uint8_t bodyBit   = (uint8_t)((s >> 1) & 1u);                  /* body   = colour 2 */
            uint8_t shadowBit = (uint8_t)((s >> GLYPH_SHADOW_PLANE) & 1u); /* shadow = colour 8 */
            if (bodyBit == 0 && shadowBit == 0)
                continue;

            /* Body pixels take the requested colour; shadow-only pixels take
             * the fixed shadow colour. Identical to blitGlyphBitplanes(). */
            uint8_t out = bodyBit ? (uint8_t)color
                                  : (uint8_t)(1u << GLYPH_SHADOW_PLANE);

            if (out) dstRow[dx] = out;
        }
    }
}
#else
__attribute__((used, externally_visible))
void BlitGlyphToSpriteBitmap(uint16_t glyphIndex, uint16_t xOff, uint16_t yOff,
                              uint16_t planeMask, uint16_t dstSpriteIndex)
{
    uint8_t *base = _spriteGraphicsPtr;
    SpriteGraphics *srcHdr, *dstHdr;
    uint8_t *dstBase;
    uint16_t dstRowBytes;
    int32_t dstRowAdv;
    uint16_t glyphHeight;
    uint16_t xBit, glyphPixWidth;
    uint16_t blitWords, lastWordMask;
    uint16_t bltconShift;
    uint16_t blitsize;
    uint8_t *maskPtr, *glyphData, *glyphPlane3;
    int32_t xWordOff;
    uint8_t *dstPtr;
    uint16_t srcAMod, srcBMod, dstMod;
    uint16_t srcStride2;
    int16_t i;

    /* Look up source glyph header */
    srcHdr = ((SpriteGraphics **)_spriteGraphicsPtr)[glyphIndex];

    /* Validate: type must be 0 (separate planes) */
    if (srcHdr->type != 0) {
        _panicReasonCode = 0x1566;
        PanicHaltLoop();
    }

    /* Look up destination sprite header */
    dstHdr = ((SpriteGraphics **)_spriteGraphicsPtr)[dstSpriteIndex];

    /* Destination row stride in bytes (per plane) */
    dstRowBytes = (uint16_t)dstHdr->wquads * 2;
    dstRowAdv = (int32_t)dstRowBytes;

    /* Destination data pointer */
    dstBase = dstHdr->sprData;

    /* Glyph height */
    glyphHeight = (uint16_t)srcHdr->nlines;

    /* Enable then wait for blitter idle */
    waitBlit();

    /* Compute x shift and word span */
    xBit = xOff & TILE_MASK;

    /* Glyph pixel width (masked to 4 bits, 0 means 16) */
    glyphPixWidth = (uint16_t)srcHdr->pixWidth & TILE_MASK;
    if (glyphPixWidth == 0)
        glyphPixWidth = 16;

    /* blitWords = number of destination words to touch */
    blitWords = (uint16_t)srcHdr->wquads;
    blitWords += 1;

    /* Check if glyph + shift fits in fewer words */
    lastWordMask = 0;
    if (xBit + glyphPixWidth <= 16) {
        lastWordMask = 0xFFFF;
        blitWords -= 1;
    }

    BLT_W(_custom->bltalwm, lastWordMask);
    BLT_W(_custom->bltafwm, 0xFFFF);
    /* Shift amount → bltcon1 (descending=0, shift in bits 12-15 of con0) */
    bltconShift = xBit << 12;
    BLT_W(_custom->bltcon1, bltconShift);
    /* Validate mask pointer exists */
    maskPtr = srcHdr->maskPtr;
    if (maskPtr == 0) {
        _panicReasonCode = 0x1567;
        PanicHaltLoop();
    }

    /* Compute destination pointer: dstBase + xWordOff + yOff * rowStride * 4planes */
    xWordOff = (int32_t)((int16_t)(xOff >> 4) * 2);
    {
        uint32_t yRowOff = (uint32_t)yOff * dstRowBytes;
        yRowOff <<= SPRITE_NUM_PLANES_SHIFT;   /* dest text/name sprite is 4-plane */
        dstPtr = dstBase + yRowOff + xWordOff;
    }

    /* Compute blitsize: height in 6-bit field + words */
    blitsize = (glyphHeight << 6) + blitWords;

    /* Source word count */
    blitWords *= 2;  /* now in bytes */

    /* Source modulo: srcStride = wquads * 2, per row */
    srcStride2 = (uint16_t)srcHdr->wquads * 2;

    /* Source A (mask) modulo: stride2 - blitWords */
    srcAMod = srcStride2 - blitWords;

    /* Source B (glyph) modulo: stride2 * SPRITE_NUM_PLANES - blitWords (4bpp font glyph) */
    srcBMod = srcStride2 * SPRITE_NUM_PLANES - blitWords;

    BLT_W(_custom->bltamod, srcAMod);
    BLT_W(_custom->bltbmod, srcBMod);
    /* Dest modulo: dstRowBytes * SPRITE_NUM_PLANES - blitWords (4-plane text sprite) */
    dstMod = dstRowBytes * SPRITE_NUM_PLANES - blitWords;
    BLT_W(_custom->bltcmod, dstMod);
    BLT_W(_custom->bltdmod, dstMod);
    /* Glyph data: advance to plane 1, and compute last plane pointer */
    glyphData = srcHdr->sprData;      /* sprData (plane 0 start) */
    glyphData += (int32_t)srcStride2;            /* advance to plane 1 */
    glyphPlane3 = glyphData + (int32_t)srcStride2 * (SPRITE_NUM_PLANES - 2);  /* glyph plane 3 = shadow */

    /* ---- Blit passes 0 through SPRITE_NUM_PLANES-2 (4-plane text sprite) ---- */
    for (i = SPRITE_NUM_PLANES - 2; i >= 0; i--) {
        /* ASM: lsr.w #1,d5 / bcc — checks the bit shifted OUT (carry flag).
         * We must check the old LSB before shifting. */
        uint16_t bit = planeMask & 1;
        planeMask >>= 1;

        if (bit) {
            /* Cookie-cut: D = (A & B) | (~A & C), BLTCON0 = $FCA + shift */
            uint16_t con0 = bltconShift + BLTCON0_BASE_LINKED;
            BLT_W(_custom->bltcon0, con0);
            BLT_L(_custom->bltapt, (uint32_t)maskPtr);
            BLT_L(_custom->bltbpt, (uint32_t)glyphData);
            BLT_L(_custom->bltcpt, (uint32_t)dstPtr);
            BLT_L(_custom->bltdpt, (uint32_t)dstPtr);
        } else {
            /* Clear-through-mask: D = ~A & C, BLTCON0 = $B0A + shift */
            uint16_t con0 = bltconShift + BLTCON0_CLEAR_MASK;
            BLT_W(_custom->bltcon0, con0);
            BLT_L(_custom->bltapt, (uint32_t)maskPtr);
            BLT_L(_custom->bltcpt, (uint32_t)dstPtr);
            BLT_L(_custom->bltdpt, (uint32_t)dstPtr);
        }

        /* Kick blit */
        BLT_W(_custom->bltsize, blitsize);
        /* Wait for blit to complete */
        waitBlit();

        /* Advance destination to next plane */
        dstPtr += dstRowAdv;
    }

    /* ---- Final pass (last plane): asymmetric ---- */
    {
    uint16_t bit3 = planeMask & 1;
    planeMask >>= 1;

    if (bit3) {
        /* OR-mask: D = (A & B) | C, BLTCON0 = $BFA + shift */
        uint16_t con0 = bltconShift + BLTCON0_OR_MASK;
        BLT_W(_custom->bltcon0, con0);
        BLT_L(_custom->bltapt, (uint32_t)maskPtr);
        BLT_L(_custom->bltcpt, (uint32_t)dstPtr);
        BLT_L(_custom->bltdpt, (uint32_t)dstPtr);
    } else {
        /* Cookie-cut with last plane data: D = (A & B) | (~A & C), use glyphPlane3 */
        uint16_t con0 = bltconShift + BLTCON0_BASE_LINKED;
        BLT_W(_custom->bltcon0, con0);
        BLT_L(_custom->bltapt, (uint32_t)maskPtr);
        BLT_L(_custom->bltbpt, (uint32_t)glyphPlane3);
        BLT_L(_custom->bltcpt, (uint32_t)dstPtr);
        BLT_L(_custom->bltdpt, (uint32_t)dstPtr);
    }

    /* Kick final blit, and wait for it. SWOS2.s leaves it running ("the next blit will
     * sync"), but what comes next after the LAST glyph is no blit: the CPU builds the
     * sprite's mask from these planes (BlitSpritePlanar_*). On the A1200 the CPU got
     * there before this blit -- the shadow plane -- had finished, so the mask had no
     * shadow under the last character of a name. */
    BLT_W(_custom->bltsize, blitsize);
    waitBlit();
    }
}
#endif /* !SWOS_SDL_BUILD for BlitGlyphToSpriteBitmap */


/* ================================================================
 * Text2Sprite
 *
 * Renders text into a sprite bitmap. Iterates over each character,
 * looks up the glyph sprite, and blits it into the destination sprite.
 *
 * In:
 *   xOff           - initial x pixel offset in destination sprite
 *   yOff           - y offset (for EGA compatibility, usually 0)
 *   dstSpriteIndex - destination sprite index
 *   text           - null-terminated ASCII string
 *   charsTable     - character table (spacing, conversion, sprite indices)
 *
 * Out:
 *   Returns 0 if OK (text fits), 1 if text wider than sprite.
 *   *outTotalWidth = final x position (total pixel width).
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t Text2Sprite(uint16_t xOff, uint16_t yOff, uint16_t dstSpriteIndex,
                      uint8_t *text, uint8_t *charsTable, uint16_t planeMask,
                      uint16_t *outTotalWidth)
{
    SpriteGraphics **table = (SpriteGraphics **)_spriteGraphicsPtr;
    SpriteGraphics *dstHdr;
    uint16_t textWidth;
    uint16_t spriteIndexOffset;
    uint16_t charSpacing;
    uint16_t spaceWidth;
    uint16_t *convTable;
    uint8_t ch;

    /* Get total text width (GetTextWidth already ported to C) */
    textWidth = GetTextWidth(text, charsTable);
    textWidth += xOff;

    /* Check if text fits in destination sprite.
     * Amiga: `*(SpriteGraphics**)(base + idx*4)`. On 64-bit each slot
     * in spriteGraphicsPtr is 8 bytes; index as a typed pointer table
     * so the stride is correct on both targets. */
    dstHdr = table[dstSpriteIndex];
    {
        extern int _debug_sprite_load;
        if (_debug_sprite_load) {
            SWOS_DBG("[text] dstIdx=0x%03X text='%.16s' width=%u dstPixW=%d dstHdr=%p\n",
                    dstSpriteIndex, (const char *)text, textWidth,
                    dstHdr ? dstHdr->pixWidth : -1, (void*)dstHdr);
        }
    }
    if (textWidth > (uint16_t)dstHdr->pixWidth)
        return 1;  /* error: text wider than sprite */

    /* Extract char table fields */
    CharsTableHeader *ct = (CharsTableHeader *)charsTable;
    spriteIndexOffset = (uint16_t)ct->spriteIndexOffset;
    charSpacing = (uint16_t)ct->charSpacing;
    spaceWidth = (uint16_t)ct->spaceWidth;
    convTable = (uint16_t *)(ct + 1);

    /* Process each character */
    while ((ch = *text++) != 0) {
        if (ch == ' ' || ch == '_') {     /* ('_' a space: a typed name's) */
            xOff += spaceWidth;
        } else {
            uint16_t glyphIdx;
            SpriteGraphics *glyphHdr;
            uint16_t glyphWidth;
            uint16_t nextX;

            /* Map ASCII to glyph index */
            glyphIdx = (uint16_t)convTable[ch - ' '] + spriteIndexOffset;

            /* Get glyph pixel width */
            glyphHdr = ((SpriteGraphics **)_spriteGraphicsPtr)[glyphIdx];
            glyphWidth = (uint16_t)glyphHdr->pixWidth;
            nextX = xOff + glyphWidth + charSpacing;

            /* Blit glyph into destination sprite */
            BlitGlyphToSpriteBitmap(glyphIdx, xOff, yOff, planeMask, dstSpriteIndex);

            xOff = nextX;
        }
    }

    /* Build OR-combined mask for the final sprite */
    BlitSpritePlanar_Word(dstSpriteIndex);

    /* Return total width to caller via output parameter */
    if (outTotalWidth)
        *outTotalWidth = xOff;

    return 0;
}
