/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "sprites.h"
#include "math68k.h"
#include "game_types.h"
#include "display.h"
#include "deterministic.h"
#ifdef SWOS_SDL_BUILD
#include "swos_types.h"
#endif

/* ---- Blitter register write macros ----
 * GCC m68k 15.1.0 decomposes `volatile uint16_t = N` struct-member
 * stores into byte-pair `move.b` instructions in some contexts. For
 * hardware-trigger registers (writing `bltsize` starts the blitter),
 * the decomposed write triggers the blit mid-update with a half-formed
 * size — fill/copy operations either do nothing or write garbage. The
 * macros wrap each write in inline `move.w` / `move.l` asm so the
 * single-instruction store is guaranteed on m68k. Defined in blitter.c
 * too — kept local here to avoid an extra header just for these.  */
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
extern volatile uint16_t _panicReasonCode;
extern volatile uint16_t _playGame;   /* nonzero only while a match plays (AGA verge margin) */

extern Sprite **_allSpritesArray;      /* Null-terminated ((Sprite *)-1) pointer array */
extern Sprite *_sortedSprites[];        /* 78 entries max */
extern uint16_t _numSpritesToRender;

extern volatile uintptr_t _spriteBlitQueue;         /* 4-byte var holding pointer to blit queue */
extern volatile uintptr_t _frontbufferBasePtr;      /* 4-byte var holding pointer to frontbuffer */
extern uint8_t _frontbufferMaskBuffer[];  /* BSS label: mask buffer base address */
extern FixedPoint _cameraX;
extern FixedPoint _cameraY;
extern uint16_t _cameraXMod320;
extern uint16_t _cameraYMod320;
extern uint16_t _overlayActiveFlag;

/* Render stacks: xxxPtr are 4-byte pointer vars, xxxBase are BSS labels */
extern uint8_t *_renderStackPtr;
extern uint8_t _renderStackBase[];
extern uint8_t *_plane01RenderStackPtr;
extern uint8_t _plane01RenderStackBase[];
extern uint8_t *_plane23RenderStackPtr;
extern uint8_t _plane23RenderStackBase[];

/* Copper list plane pointers (4-byte vars) */
extern volatile uint32_t _bitPlane0BasePtr;
extern volatile uint32_t _bitPlane1BasePtr;
extern volatile uint32_t _bitPlane2BasePtr;
extern volatile uint32_t _bitPlane3BasePtr;
extern uint32_t _copperZeroLong;

/* Player sprite identification for DrawQueuedSprites */
extern Sprite _goalie1Sprite;

extern void PanicHaltLoop(void) __attribute__((noreturn));
extern void SaveCoordinatesForHighlights(int16_t spriteIndexOrMarker, uint16_t x, uint16_t y);


/* ---- Helper: wait for blitter idle ---- */
static inline __attribute__((always_inline)) void waitBlit(void)
{
    _custom->dmacon = (uint16_t)DMACON_BLT_ON;
    (void)(_custom->dmaconr & (1u << 14));
    while (BLIT_BUSY())
        ;
    _custom->dmacon = (uint16_t)DMACON_BLT_OFF;
}

/* ================================================================
 * CopySprite
 *
 * Copies one sprite into another at a given offset using the Amiga
 * blitter.  Handles both linked-plane (with mask) and separate-plane
 * (no mask) sprite formats.
 *
 * asm: d0=srcSprite, d1=xOffset, d2=yOffset, d3=dstSprite
 * C bridge: (srcSprite, dstSprite, xOffset, yOffset)
 * ================================================================ */
__attribute__((used, externally_visible))
void CopySprite(uint16_t srcSprite, uint16_t dstSprite,
                 uint16_t xOffset, uint16_t yOffset)
{

    SpriteGraphics **sprGfxTable = (SpriteGraphics **)_spriteGraphicsPtr;

    /* Load destination sprite header */
    SpriteGraphics *dstHdr = sprGfxTable[dstSprite];
    /* Load source sprite header */
    SpriteGraphics *srcHdr = sprGfxTable[srcSprite];

#ifdef SWOS_SDL_BUILD
    /* Chunky 8bpp byte copy. The Amiga blitter path below programs
     * `custom->blt*` registers which on SDL go to a stub buffer
     * doing nothing observable, so without this path sprite[dstSprite]
     * never actually receives sprite[srcSprite]'s pixel data and the
     * subsequent ConvertSpriteColors operates on whatever stale bytes
     * are in slot dstSprite. Symptom: stadium-menu little-player
     * sprites all rendered with the SAME shape regardless of team
     * shirt-type, and color conversion produced inconsistent results.
     *
     * Both src and dst are post-ProcessImageCommandStream chunky 8bpp;
     * each row is `wquads * 16` bytes wide, `nlines` tall. The Amiga
     * caller passes (xOffset, yOffset) in pixels for compositing; the
     * stadium-menu / edit-tactics callers all use (0, 0). A simple
     * unclipped row-by-row memcpy at the offset covers those uses. */
    {
        uint16_t srcW = (uint16_t)srcHdr->pixWidth;
        uint16_t srcH = (uint16_t)srcHdr->nlines;
        uint16_t dstStride = (uint16_t)dstHdr->wquads * 16;
        uint8_t *dst = (uint8_t *)(uintptr_t)dstHdr->sprData
                     + (size_t)yOffset * dstStride
                     + (size_t)xOffset;
        const uint8_t *src = (const uint8_t *)(uintptr_t)srcHdr->sprData;
        uint16_t srcStride = (uint16_t)srcHdr->wquads * 16;
        for (uint16_t row = 0; row < srcH; row++) {
            for (uint16_t col = 0; col < srcW; col++) {
                dst[col] = src[col];
            }
            dst += dstStride;
            src += srcStride;
        }
        return;
    }
#endif

    /* Destination tile stride in bytes (per plane row) */
    uint16_t dstStrideBytes = (uint16_t)dstHdr->wquads << 1;
    /* Destination sprite data pointer */
    uint8_t *dstData = (uint8_t *)dstHdr->sprData;
    /* Src and dst are same-depth sprites (`sp` planes: 4 for 16-colour / 8 soccer_s).
     * Was hard NUM_PLANES(8) interleave; a 4-plane sprite now steps sp plane-rows/scanline. */
    uint16_t sp = spriteNumPlanes(dstHdr->ordinal);

    /* Wait for blitter idle */
    waitBlit();

    /* Compute horizontal shift and edge masks */
    uint16_t bitShift = xOffset & 0xF;
    uint16_t srcPixMod = (uint16_t)srcHdr->pixWidth & 0xF;
    if (srcPixMod == 0)
        srcPixMod = 16;
    uint16_t totalBits = bitShift + srcPixMod;

    uint16_t blitWords = (uint16_t)srcHdr->wquads + 1;
    uint16_t lastWordMask = 0;

    if (totalBits <= 16) {
        lastWordMask = 0xFFFF;
        blitWords--;
    }

    BLT_W(_custom->bltalwm, lastWordMask);
    BLT_W(_custom->bltafwm, 0xFFFF);
    /* BLTCON1: shift amount in bits 12-15 */
    uint16_t shiftVal = bitShift << 12;
    BLT_W(_custom->bltcon1, shiftVal);
    /* Check if source has mask data (offset 4) */
    uintptr_t srcMaskPtr = (uintptr_t)srcHdr->maskPtr;
    uint16_t bltcon0;
    if (srcMaskPtr != 0) {
        /* Linked mode: cookie cut with mask */
        bltcon0 = shiftVal | BLTCON0_BASE_LINKED;
    } else {
        /* Single mode: straight copy, no mask (A = all 1s via BLTADAT) */
        bltcon0 = shiftVal | BLTCON0_BASE_SINGLE;
        BLT_W(_custom->bltadat, 0xFFFF);
    }
    BLT_W(_custom->bltcon0, bltcon0);
    /* Compute destination byte offset for xOffset */
    uint32_t wordByteOff = (uint32_t)((xOffset >> 3) & 0xFFFE);

    /* Compute y offset in interleaved plane data */
    uint16_t dstInterleavedStride = (uint16_t)(dstStrideBytes * sp);
    uint32_t yByteOff = (uint32_t)yOffset * dstInterleavedStride;
    uint32_t dstAddr = (uintptr_t)dstData + yByteOff + wordByteOff;

    BLT_L(_custom->bltcpt, dstAddr);
    BLT_L(_custom->bltdpt, dstAddr);
    /* Source sprite type: offset 8 */
    uint16_t srcType = srcHdr->type;
    uint16_t blitWidthBytes = blitWords << 1;

    if (srcType != 0) {
        /* ---- Linked / interleaved path ---- */
        uint16_t nlines = (uint16_t)srcHdr->nlines;
        uint16_t bltsize = ((uint16_t)(nlines * sp) << 6) | blitWords;

        /* Source A/B modulo */
        uint16_t srcStride = (uint16_t)srcHdr->wquads << 1;
        BLT_W(_custom->bltamod, srcStride - blitWidthBytes);
        BLT_W(_custom->bltbmod, srcStride - blitWidthBytes);
        /* Destination C/D modulo */
        BLT_W(_custom->bltcmod, dstStrideBytes - blitWidthBytes);
        BLT_W(_custom->bltdmod, dstStrideBytes - blitWidthBytes);
        /* Source pointers */
        BLT_L(_custom->bltbpt, (uint32_t)(uintptr_t)srcHdr->sprData);
        BLT_L(_custom->bltapt, (uint32_t)srcMaskPtr);
        /* Kick blit */
        BLT_W(_custom->bltsize, bltsize);
    } else {
        /* ---- Four separate planes path ---- */
        uint16_t nlines = (uint16_t)srcHdr->nlines;
        uint16_t bltsize = (nlines << 6) | blitWords;

        /* B modulo: src interleaved scanline (sp plane-rows) - blit width */
        uint16_t srcWquads = (uint16_t)srcHdr->wquads;
        uint16_t srcBmod = (uint16_t)(srcWquads * 2 * sp) - blitWidthBytes;
        BLT_W(_custom->bltbmod, srcBmod);
        /* A modulo: per-plane stride - blit width (single-plane mask) */
        uint16_t srcAmod = (srcWquads << 1) - blitWidthBytes;
        BLT_W(_custom->bltamod, srcAmod);
        /* C/D modulo: dst interleaved scanline (sp plane-rows) - blit width */
        uint16_t dstCDmod = (uint16_t)(dstStrideBytes * sp) - blitWidthBytes;
        BLT_W(_custom->bltcmod, dstCDmod);
        BLT_W(_custom->bltdmod, dstCDmod);
        /* Source data and mask pointers */
        uintptr_t srcDataPtr = (uintptr_t)srcHdr->sprData;
        uintptr_t srcMask = (uintptr_t)srcHdr->maskPtr;  /* may be 0 for single-plane */

        /* Per-plane stride for advancing between passes */
        uint32_t dstPlaneStep = (uint32_t)dstStrideBytes;
        uint32_t srcPlaneStep = (uint32_t)(srcWquads << 1);

        /* Pass 1 */
        BLT_L(_custom->bltbpt, (uint32_t)srcDataPtr);
        BLT_L(_custom->bltapt, (uint32_t)srcMask);
        BLT_W(_custom->bltsize, bltsize);
        /* Advance to plane 1 */
        dstAddr += dstPlaneStep;
        srcDataPtr += srcPlaneStep;

        /* Pass 2 */
        waitBlit();
        BLT_L(_custom->bltcpt, dstAddr);
        BLT_L(_custom->bltdpt, dstAddr);
        BLT_L(_custom->bltbpt, (uint32_t)srcDataPtr);
        BLT_L(_custom->bltapt, (uint32_t)srcMask);
        BLT_W(_custom->bltsize, bltsize);
        /* Advance to plane 2 */
        dstAddr += dstPlaneStep;
        srcDataPtr += srcPlaneStep;

        /* Pass 3 */
        waitBlit();
        BLT_L(_custom->bltcpt, dstAddr);
        BLT_L(_custom->bltdpt, dstAddr);
        BLT_L(_custom->bltbpt, (uint32_t)srcDataPtr);
        BLT_L(_custom->bltapt, (uint32_t)srcMask);
        BLT_W(_custom->bltsize, bltsize);
        /* Advance to plane 3 */
        dstAddr += dstPlaneStep;
        srcDataPtr += srcPlaneStep;

        /* Pass 4 */
        waitBlit();
        BLT_L(_custom->bltcpt, dstAddr);
        BLT_L(_custom->bltdpt, dstAddr);
        BLT_L(_custom->bltbpt, (uint32_t)srcDataPtr);
        BLT_L(_custom->bltapt, (uint32_t)srcMask);
        BLT_W(_custom->bltsize, bltsize);
    }
}

/* ================================================================
 * CopyWholeSprite
 *
 * Copies entire source sprite into destination sprite at offset (0,0).
 * ================================================================ */
__attribute__((used, externally_visible))
void CopyWholeSprite(uint16_t srcSprite, uint16_t dstSprite)
{
    CopySprite(srcSprite, dstSprite, 0, 0);
}

/* ================================================================
 * ConvertSpriteColors
 *
 * Re-palettizes a 4-plane sprite using a color lookup table.
 * Each pixel's 4-bit color index is looked up in colorTable
 * and the result is written back.
 *
 * Two modes:
 *   - Fast (*colorTable != 0): remaps ALL pixels including color 0
 *   - Slow (*colorTable == 0): remaps only non-zero pixels (preserves transparency)
 * ================================================================ */
__attribute__((used, externally_visible))
void ConvertSpriteColors(uint16_t spriteIndex, uint16_t *colorTable)
{
    SpriteGraphics **sprGfxTable = (SpriteGraphics **)_spriteGraphicsPtr;
    SpriteGraphics *spr = sprGfxTable[spriteIndex];

#ifdef SWOS_SDL_BUILD
    /* Sprites are 8bpp chunky after ProcessImageCommandStream decodes
     * them at load. Color conversion is just a 1-byte LUT walk over
     * `wquads * 16 * nlines` bytes. `colorTable[0] == 0` means "keep
     * transparent pixels (index 0) untouched"; otherwise every pixel
     * gets remapped. */
    uint16_t wquads = (uint16_t)spr->wquads;
    uint16_t nlines = (uint16_t)spr->nlines;
    uint16_t pitch  = (uint16_t)(wquads * 16);
    uint8_t *px     = (uint8_t *)spr->sprData;
    uint32_t total  = (uint32_t)pitch * nlines;
    int skipTransparent = (colorTable[0] == 0);

    for (uint32_t i = 0; i < total; i++) {
        uint8_t c = px[i];
        if (skipTransparent && c == 0) continue;
        px[i] = (uint8_t)colorTable[c];
    }
#else
    /* Amiga build — in-place re-palette of the sprite's own `sp` planes (4 for a
     * 16-colour sprite; 8 for soccer_s). Was hard NUM_PLANES(8), which gathered planes
     * 4-7 from the NEXT sprite for a 4-plane sprite. Arrays sized to the max (8). */
    uint16_t sp = spriteNumPlanes((int16_t)spriteIndex);
    /* A padded running frame (display.h, spriteIsPadded8) has 4 COLOUR planes: the
     * other 4 are zeros and stay zeros, as a 4-plane frame drops any bit above 4. */
    uint16_t cp = spriteIsPadded8((int16_t)spriteIndex) ? SPRITE_NUM_PLANES : sp;
    uint16_t wquads = (uint16_t)spr->wquads;
    uint16_t nlines = (uint16_t)spr->nlines;
    uint16_t planeRowBytes = wquads << 1;
    int32_t interplaneSkip = (int32_t)((uint16_t)(wquads * (sp - 1) * 2));
    if (*colorTable == 0) {
        if (wquads > 0x14) { _panicReasonCode = 0x982; PanicHaltLoop(); }
        if (interplaneSkip < 0) { _panicReasonCode = 0x987; PanicHaltLoop(); }
    }

    uint16_t *dataBase = (uint16_t *)spr->sprData;
    uint16_t *planes[PITCH_NUM_PLANES];
    int16_t pl;
    for (pl = 0; pl < (int16_t)cp; pl++)
        planes[pl] = (uint16_t *)((uint8_t *)dataBase + pl * planeRowBytes);

    uint16_t skipTransparent = (*colorTable == 0);
    for (uint16_t line = 0; line < nlines; line++) {
        for (uint16_t quad = 0; quad < wquads; quad++) {
            uint16_t w[PITCH_NUM_PLANES];
            for (pl = 0; pl < (int16_t)cp; pl++) w[pl] = *planes[pl];
            for (uint16_t pixel = 0; pixel < 16; pixel++) {
                uint16_t colorIdx = 0;
                for (pl = (int16_t)cp - 1; pl >= 0; pl--) {
                    colorIdx = (colorIdx << 1) | ((w[pl] >> 15) & 1);
                    w[pl] <<= 1;
                }
                if (skipTransparent && colorIdx == 0) continue;
                uint16_t newColor = colorTable[colorIdx];
                for (pl = 0; pl < (int16_t)cp; pl++)
                    if (newColor & (1 << pl)) w[pl] |= 1;
            }
            for (pl = 0; pl < (int16_t)cp; pl++) *planes[pl]++ = w[pl];
        }
        for (pl = 0; pl < (int16_t)cp; pl++)
            planes[pl] = (uint16_t *)((uint8_t *)planes[pl] + interplaneSkip);
    }
#endif
}

/* ================================================================
 * SortSprites  (SWOS2.s line 7434)
 *
 * Improved bubble sort on sortedSprites[] by SPRITE_Y.
 * Stops early if no swaps occurred in a pass.
 * ================================================================ */
__attribute__((used, externally_visible))
void SortSprites(void)
{
    int16_t n = (int16_t)_numSpritesToRender - 2;
    if (n < 0)
        return;

    Sprite **base = _sortedSprites;

    /* Assembly does NOT shrink the inner loop — always a full pass of n+1
     * comparisons, repeating until no swaps occur (pure unoptimized bubble sort). */
    for (;;) {
        int16_t swapped = 0;
        Sprite **p = base;
        int16_t i;
        for (i = n; i >= 0; i--) {
            Sprite *s1 = p[0];
            Sprite *s2 = p[1];
            int16_t y1 = s1->y.s.i;
            int16_t y2 = s2->y.s.i;
            if (y1 > y2) {
                p[0] = s2;
                p[1] = s1;
                swapped = 1;
            }
            p++;
        }
        if (!swapped)
            return;
    }
}

/* ================================================================
 * InitDisplaySprites  (SWOS2.s line 7412)
 *
 * Copies visible sprites from allSpritesArray into sortedSprites,
 * counts them into numSpritesToRender, then sorts by Y.
 * allSpritesArray is null-terminated with 0xFFFFFFFF.
 * ================================================================ */
__attribute__((used, externally_visible))
void InitDisplaySprites(void)
{
    uint16_t count = 0;
    Sprite **dst = _sortedSprites;
    Sprite **src = _allSpritesArray;

    for (;;) {
        Sprite *spr = *src++;
        /* Sentinel is (Sprite *)-1, set in sprite_data.c at slot 79 of
         * allSpritesArray. (uintptr_t)-1 is all-bits-set so masking to
         * 32 bits still matches the original 0xFFFFFFFF sentinel on
         * either bitness.) */
        if ((uintptr_t)spr == (uintptr_t)-1)
            break;
        if (spr->visible == 0)
            continue;
        *dst++ = spr;
        count++;
    }
    _numSpritesToRender = count;
    SortSprites();
}

/* ================================================================
 * DrawSpriteClipped  (SWOS2.s line 6563)
 *
 * Main clipped sprite blitter. Clips sprite to screen bounds,
 * applies camera offset wrapping, programs the Amiga blitter
 * for cookie-cut (masked) or straight copy across 4 bitplanes.
 * Handles Y-wrap around the circular framebuffer.
 *
 * Sprite header (a0):
 *   +0  = data pointer (plane interleaved)
 *   +4  = mask pointer (0 = no mask / single-plane mode)
 *   +8  = type (SPRITE_ANIMATIONTABLE+2; nonzero = branch_linked_planes)
 *   +10 = pixel width
 *   +12 = nlines (height)
 *   +14 = TILE_STRIDE (wquads)
 *   +16 = centerX
 *   +18 = centerY
 *
 * Queue entry format (12 bytes each):
 *   +0  = mask buffer address
 *   +4  = frontbuffer address
 *   +8  = modulo/stride
 *   +10 = bltsize (4-plane height)
 * Terminated by 0xFFFFFFFF.
 * ================================================================ */

/* ---- Sprite blitter constants ---- */
#define SPRITE_MAX_Y_DRAW       257
/* SPRITE_BLTSIZE_W1, SPRITE_BLTSIZE_W2 now defined in display.h */

/* ================================================================
 * QueueMaskedSpriteBlitClipped  (SWOS2.s line 13202)
 *
 * Blits a masked player sprite (cookie-cut) into the frontbuffer.
 * Validates sprite header (nlines==15, pixWidth<=16, type!=0),
 * clamps to screen, applies camera offset with Y-wrap, then
 * programs blitter and queues a blit entry for later replay.
 *
 * Two code paths based on horizontal overlap:
 *   1-word: sprite + x_offset fits in 16 pixels
 *   2-word: sprite spans into next word
 *
 * If validation fails, falls through to DrawSpriteClipped.
 *
 * in: a0=spriteGraphics, d1=x, d2=y
 * ================================================================ */
/* ---- QueueMaskedSpriteBlitClipped: single-blit when sprite depth == pitch depth (OCS 4==4, or AGA sprites
 * toggled to 8==8) — one interleaved blit; else (AGA 4-plane sprite on 8-plane pitch)
 * per-plane composite + upper-plane clear. SWOS_SPRITE_PLANES flips this. ---- */
#if (SPRITE_NUM_PLANES == PITCH_NUM_PLANES)
__attribute__((used, externally_visible))
void QueueMaskedSpriteBlitClipped(SpriteGraphics *a0, int16_t d1, int16_t d2)
{
#ifdef SWOS_SDL_BUILD
    /* SDL: Amiga's queued/masked player-sprite blit uses a blitter
     * queue with linked-plane cookie-cut masking. On our 8bpp chunky
     * pipeline transparency is palette index 0, so there's no separate
     * mask to combine; a straight chunky draw does the right thing.
     * Route all calls — which for this function are player sprites
     * (teamNumber 1 or 2) per DrawQueuedSprites — through
     * DrawSpriteClipped → blitChunkySprite. Without this, every
     * player on the pitch renders as nothing. */
    if (!a0) return;
    DrawSpriteClipped(a0, d1, d2);
    return;
#else


    /* Validate sprite: nlines must be 15, pixWidth <= 16, type != 0 */
    if ((uint16_t)a0->nlines != 15)
        goto fallback;
    if ((uint16_t)a0->pixWidth > 16)
        goto fallback;
    if (a0->type == 0)
        goto fallback;

    /* Y must be non-negative and within drawable range */
    if (d2 < 0)
        goto fallback;
    if (d2 > SPRITE_MAX_Y_DRAW)
        goto fallback;

    /* Apply camera offsets */
    d1 += (int16_t)_cameraXMod320;
    d2 += (int16_t)_cameraYMod320;

#if defined(AGA) && !defined(SWOS_SDL_BUILD)
    /* AGA no-wrap: the two display frontbuffers are 2x tall — rows
     * [SCREEN_HEIGHT,2*SCREEN_HEIGHT) MIRROR [0,SCREEN_HEIGHT) and the copper
     * shows a window sliding by cameraYMod320. Draw the bob ONCE at its true
     * absolute buffer row (screen_y + cameraYMod, NO wrap) so it lands exactly
     * where the display reads it — whether that is the first half, the mirror
     * half, or straddling the seam. d2 now spans [0, 2*SCREEN_HEIGHT); the
     * frontbuffer is contiguous and (2*SCREEN_HEIGHT+1) rows tall, and
     * screen_y(<=SPRITE_MAX_Y_DRAW) + cameraYMod(<=SCREEN_HEIGHT) + 15 lines
     * stays inside it. The 1x-tall bg-save mask buffer is indexed per-case in
     * the undraw section below (mirror rows fold back by FB_MIRROR_OFFSET). */
#else
    /* OCS 1x buffer: wrap Y into the single visible band. */
    if (d2 >= SCREEN_HEIGHT)
        d2 -= SCREEN_HEIGHT;

    /* Re-validate Y after wrap */
    if (d2 > SPRITE_MAX_Y_DRAW) {
        /* Undo camera offsets and fall through to DrawSpriteClipped */
        d1 -= (int16_t)_cameraXMod320;
        d2 -= (int16_t)_cameraYMod320;
        goto fallback;
    }
#endif

    {
        /* Check horizontal overlap: (x & 15) + pixWidth */
        uint16_t xFrac = (uint16_t)d1 & TILE_MASK;
        uint16_t pixW = (uint16_t)a0->pixWidth;
        uint16_t overlap = xFrac + pixW;

        /* Compute blitter shift value: (x & 15) << 12 */
        uint16_t shiftVal = xFrac << 12;

        /* Compute destination address */
        uint32_t *a3 = (uint32_t *)_spriteBlitQueue;
        uint8_t *a1 = (uint8_t *)_frontbufferBasePtr + _menu_draw_x_offset;

        int16_t xWordOff = (d1 >> TILE_SHIFT_X) << 1; /* asr.w + asl.w (signed) */
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
        if (_playGame) xWordOff += SCROLL_LEFT_MARGIN_BYTES;   /* §desync: pitch verge margin, in-match only */
#endif
        int32_t xWordOffL = (int32_t)xWordOff; /* sign-extend */
        uint32_t yOff = (uint16_t)d2 * (uint32_t)ROW_STRIDE_BYTES;
        uint32_t destAddr = (uint32_t)a1 + yOff + (uint32_t)xWordOffL;

        /* Wait for blitter and program registers */
        waitBlit();

        /* BLTCON1 = shift, BLTCON0 = shift + cookie-cut minterm */
        BLT_W(_custom->bltcon1, shiftVal);
        BLT_W(_custom->bltcon0, shiftVal + BLTCON0_BASE_LINKED);
        /* Destination */
        BLT_L(_custom->bltcpt, destAddr);
        BLT_L(_custom->bltdpt, destAddr);
        /* Destination address (a1-relative) reused for the draw and undraw. */
        uint32_t destOff = destAddr - (uint32_t)a1;   /* d2*ROW_STRIDE + xWordOff */

        /* Source pointers: data=BLTBPT, mask=BLTAPT */
        BLT_L(_custom->bltbpt, (uint32_t)a0->sprData);
        BLT_L(_custom->bltapt, (uint32_t)a0->maskPtr);
        /* First word mask always all-ones */
        BLT_W(_custom->bltafwm, 0xFFFF);
        uint16_t entryMod, entryBltsize;
        if (overlap <= 16) {
            /* ---- Single-word path ---- */
            BLT_W(_custom->bltamod, 0);
            BLT_W(_custom->bltbmod, 0);
            BLT_W(_custom->bltcmod, PLANE_STRIDE_MINUS2);
            BLT_W(_custom->bltdmod, PLANE_STRIDE_MINUS2);
            entryMod     = PLANE_STRIDE_MINUS2;
            BLT_W(_custom->bltalwm, 0xFFFF);
            entryBltsize = SPRITE_BLTSIZE_W1;
        } else {
            /* ---- Two-word path ---- */
            BLT_W(_custom->bltamod, 0xFFFE); /* -2 */
            BLT_W(_custom->bltbmod, 0xFFFE);
            BLT_W(_custom->bltcmod, PLANE_STRIDE_MINUS4);
            BLT_W(_custom->bltdmod, PLANE_STRIDE_MINUS4);
            entryMod     = PLANE_STRIDE_MINUS4;
            BLT_W(_custom->bltalwm, 0);
            entryBltsize = SPRITE_BLTSIZE_W2;
        }
        /* Kick the sprite draw. The 2x-tall frontbuffer is contiguous across the
         * mirror seam, so even a bob straddling SCREEN_HEIGHT draws in this one
         * blit; only the 1x-tall bg-save undraw (below) ever needs splitting. */
        BLT_W(_custom->bltsize, entryBltsize);

        /* ---- Queue the undraw (bg-restore) entry/entries ---- */
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
        if (d2 + SPRITE_HEIGHT_LINES <= SCREEN_HEIGHT) {
            /* Entirely in the first half. */
            *(uint32_t *)((uint8_t *)a3 + 0)  = undrawTwin(destAddr);
            *(uint32_t *)((uint8_t *)a3 + 4)  = destAddr;
            *(uint16_t *)((uint8_t *)a3 + 8)  = entryMod;
            *(uint16_t *)((uint8_t *)a3 + 10) = entryBltsize;
            a3 = (uint32_t *)((uint8_t *)a3 + 12);
        } else if (d2 >= SCREEN_HEIGHT) {
            /* Entirely in the mirror half — fold the bg-save read back to the
             * matching first-half row (the 1x mask buffer has no mirror half). */
            *(uint32_t *)((uint8_t *)a3 + 0)  = undrawTwin(destAddr);
            *(uint32_t *)((uint8_t *)a3 + 4)  = destAddr;
            *(uint16_t *)((uint8_t *)a3 + 8)  = entryMod;
            *(uint16_t *)((uint8_t *)a3 + 10) = entryBltsize;
            a3 = (uint32_t *)((uint8_t *)a3 + 12);
        } else {
            /* Straddles the seam — two undraw entries, each reading first-half bg:
             * top rows [d2, SCREEN_HEIGHT) and bottom rows [SCREEN_HEIGHT, d2+15)
             * (which live at the mirror top, bg-save row 0 = xWordOff). */
            uint16_t wc       = (uint16_t)(entryBltsize & 0x3Fu);   /* word count */
            uint16_t topCount = (uint16_t)(SCREEN_HEIGHT - d2);
            uint16_t botCount = (uint16_t)((d2 + SPRITE_HEIGHT_LINES) - SCREEN_HEIGHT);
            uint16_t topSize  = (uint16_t)(((topCount * NUM_PLANES) << 6) | wc);
            uint16_t botSize  = (uint16_t)(((botCount * NUM_PLANES) << 6) | wc);
            /* top */
            *(uint32_t *)((uint8_t *)a3 + 0)  = undrawTwin(destAddr);
            *(uint32_t *)((uint8_t *)a3 + 4)  = destAddr;
            *(uint16_t *)((uint8_t *)a3 + 8)  = entryMod;
            *(uint16_t *)((uint8_t *)a3 + 10) = topSize;
            a3 = (uint32_t *)((uint8_t *)a3 + 12);
            /* bottom (mirror top; contiguous continuation of destAddr) */
            uint32_t botDest = destAddr + (uint32_t)topCount * (uint32_t)ROW_STRIDE_BYTES;
            *(uint32_t *)((uint8_t *)a3 + 0)  = undrawTwin(botDest);
            *(uint32_t *)((uint8_t *)a3 + 4)  = botDest;
            *(uint16_t *)((uint8_t *)a3 + 8)  = entryMod;
            *(uint16_t *)((uint8_t *)a3 + 10) = botSize;
            a3 = (uint32_t *)((uint8_t *)a3 + 12);
        }
#else
        /* OCS 1x buffer: d2 was wrapped into [0,SCREEN_HEIGHT); one entry. */
        *(uint32_t *)((uint8_t *)a3 + 0)  = destOff + (uint32_t)_frontbufferMaskBuffer;
        *(uint32_t *)((uint8_t *)a3 + 4)  = destAddr;
        *(uint16_t *)((uint8_t *)a3 + 8)  = entryMod;
        *(uint16_t *)((uint8_t *)a3 + 10) = entryBltsize;
        a3 = (uint32_t *)((uint8_t *)a3 + 12);
#endif

        /* Write sentinel at the (new) queue head */
        *(uint32_t *)a3 = BLT_BLITQ_END;
        _spriteBlitQueue = (uintptr_t)a3;
    }
    return;

fallback:
    DrawSpriteClipped(a0, d1, d2);
#endif /* !SWOS_SDL_BUILD */
}
#else
__attribute__((used, externally_visible))
void QueueMaskedSpriteBlitClipped(SpriteGraphics *a0, int16_t d1, int16_t d2)
{
#ifdef SWOS_SDL_BUILD
    /* SDL: Amiga's queued/masked player-sprite blit uses a blitter
     * queue with linked-plane cookie-cut masking. On our 8bpp chunky
     * pipeline transparency is palette index 0, so there's no separate
     * mask to combine; a straight chunky draw does the right thing.
     * Route all calls — which for this function are player sprites
     * (teamNumber 1 or 2) per DrawQueuedSprites — through
     * DrawSpriteClipped → blitChunkySprite. Without this, every
     * player on the pitch renders as nothing. */
    if (!a0) return;
    DrawSpriteClipped(a0, d1, d2);
    return;
#else
    /* The optimised single-interleaved-blit fast path below (the original's) needs the
     * sprite stored as deep as the playfield: one blit of nlines*NUM_PLANES rows. The
     * in-match running frames are (display.h, spriteIsPadded8, 2026-10-14), so they take
     * it -- no clipping arithmetic, no seam split of the draw, twelve register writes.
     * Every other sprite, and a running frame the fast path's own checks refuse (off the
     * top, too low), goes through DrawSpriteClipped as before. */
    if (!a0)
        return;
    if (spriteNumPlanes(a0->ordinal) != NUM_PLANES) {
        DrawSpriteClipped(a0, d1, d2);
        return;
    }

    /* Validate sprite: nlines must be 15, pixWidth <= 16, type != 0 */
    if ((uint16_t)a0->nlines != 15)
        goto fallback;
    if ((uint16_t)a0->pixWidth > 16)
        goto fallback;
    if (a0->type == 0)
        goto fallback;

    /* Y must be non-negative and within drawable range */
    if (d2 < 0)
        goto fallback;
    if (d2 > SPRITE_MAX_Y_DRAW)
        goto fallback;

    /* Apply camera offsets */
    d1 += (int16_t)_cameraXMod320;
    d2 += (int16_t)_cameraYMod320;

#if defined(AGA) && !defined(SWOS_SDL_BUILD)
    /* AGA no-wrap: the two display frontbuffers are 2x tall — rows
     * [SCREEN_HEIGHT,2*SCREEN_HEIGHT) MIRROR [0,SCREEN_HEIGHT) and the copper
     * shows a window sliding by cameraYMod320. Draw the bob ONCE at its true
     * absolute buffer row (screen_y + cameraYMod, NO wrap) so it lands exactly
     * where the display reads it — whether that is the first half, the mirror
     * half, or straddling the seam. d2 now spans [0, 2*SCREEN_HEIGHT); the
     * frontbuffer is contiguous and (2*SCREEN_HEIGHT+1) rows tall, and
     * screen_y(<=SPRITE_MAX_Y_DRAW) + cameraYMod(<=SCREEN_HEIGHT) + 15 lines
     * stays inside it. The 1x-tall bg-save mask buffer is indexed per-case in
     * the undraw section below (mirror rows fold back by FB_MIRROR_OFFSET). */
#else
    /* OCS 1x buffer: wrap Y into the single visible band. */
    if (d2 >= SCREEN_HEIGHT)
        d2 -= SCREEN_HEIGHT;

    /* Re-validate Y after wrap */
    if (d2 > SPRITE_MAX_Y_DRAW) {
        /* Undo camera offsets and fall through to DrawSpriteClipped */
        d1 -= (int16_t)_cameraXMod320;
        d2 -= (int16_t)_cameraYMod320;
        goto fallback;
    }
#endif

    {
        /* Check horizontal overlap: (x & 15) + pixWidth */
        uint16_t xFrac = (uint16_t)d1 & TILE_MASK;
        uint16_t pixW = (uint16_t)a0->pixWidth;
        uint16_t overlap = xFrac + pixW;

        /* Compute blitter shift value: (x & 15) << 12 */
        uint16_t shiftVal = xFrac << 12;

        /* Compute destination address */
        uint32_t *a3 = (uint32_t *)_spriteBlitQueue;
        uint8_t *a1 = (uint8_t *)_frontbufferBasePtr + _menu_draw_x_offset;

        int16_t xWordOff = (d1 >> TILE_SHIFT_X) << 1; /* asr.w + asl.w (signed) */
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
        if (_playGame) xWordOff += SCROLL_LEFT_MARGIN_BYTES;   /* §desync: pitch verge margin, in-match only */
#endif
        int32_t xWordOffL = (int32_t)xWordOff; /* sign-extend */
        uint32_t yOff = (uint16_t)d2 * (uint32_t)ROW_STRIDE_BYTES;
        uint32_t destAddr = (uint32_t)a1 + yOff + (uint32_t)xWordOffL;

        /* Wait for blitter and program registers */
        waitBlit();

        /* BLTCON1 = shift, BLTCON0 = shift + cookie-cut minterm */
        BLT_W(_custom->bltcon1, shiftVal);
        BLT_W(_custom->bltcon0, shiftVal + BLTCON0_BASE_LINKED);
        /* Destination */
        BLT_L(_custom->bltcpt, destAddr);
        BLT_L(_custom->bltdpt, destAddr);
        /* Destination address (a1-relative) reused for the draw and undraw. */
        uint32_t destOff = destAddr - (uint32_t)a1;   /* d2*ROW_STRIDE + xWordOff */

        /* Source pointers: data=BLTBPT, mask=BLTAPT */
        BLT_L(_custom->bltbpt, (uint32_t)a0->sprData);
        BLT_L(_custom->bltapt, (uint32_t)a0->maskPtr);
        /* First word mask always all-ones */
        BLT_W(_custom->bltafwm, 0xFFFF);
        uint16_t entryMod, entryBltsize;
        if (overlap <= 16) {
            /* ---- Single-word path ---- */
            BLT_W(_custom->bltamod, 0);
            BLT_W(_custom->bltbmod, 0);
            BLT_W(_custom->bltcmod, PLANE_STRIDE_MINUS2);
            BLT_W(_custom->bltdmod, PLANE_STRIDE_MINUS2);
            entryMod     = PLANE_STRIDE_MINUS2;
            BLT_W(_custom->bltalwm, 0xFFFF);
            entryBltsize = SPRITE_BLTSIZE_W1;
        } else {
            /* ---- Two-word path ---- */
            BLT_W(_custom->bltamod, 0xFFFE); /* -2 */
            BLT_W(_custom->bltbmod, 0xFFFE);
            BLT_W(_custom->bltcmod, PLANE_STRIDE_MINUS4);
            BLT_W(_custom->bltdmod, PLANE_STRIDE_MINUS4);
            entryMod     = PLANE_STRIDE_MINUS4;
            BLT_W(_custom->bltalwm, 0);
            entryBltsize = SPRITE_BLTSIZE_W2;
        }
        /* Kick the sprite draw. The 2x-tall frontbuffer is contiguous across the
         * mirror seam, so even a bob straddling SCREEN_HEIGHT draws in this one
         * blit; only the 1x-tall bg-save undraw (below) ever needs splitting. */
        BLT_W(_custom->bltsize, entryBltsize);

        /* ---- Queue the undraw (bg-restore) entry/entries ---- */
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
        if (d2 + SPRITE_HEIGHT_LINES <= SCREEN_HEIGHT) {
            /* Entirely in the first half. */
            *(uint32_t *)((uint8_t *)a3 + 0)  = undrawTwin(destAddr);
            *(uint32_t *)((uint8_t *)a3 + 4)  = destAddr;
            *(uint16_t *)((uint8_t *)a3 + 8)  = entryMod;
            *(uint16_t *)((uint8_t *)a3 + 10) = entryBltsize;
            a3 = (uint32_t *)((uint8_t *)a3 + 12);
        } else if (d2 >= SCREEN_HEIGHT) {
            /* Entirely in the mirror half — fold the bg-save read back to the
             * matching first-half row (the 1x mask buffer has no mirror half). */
            *(uint32_t *)((uint8_t *)a3 + 0)  = undrawTwin(destAddr);
            *(uint32_t *)((uint8_t *)a3 + 4)  = destAddr;
            *(uint16_t *)((uint8_t *)a3 + 8)  = entryMod;
            *(uint16_t *)((uint8_t *)a3 + 10) = entryBltsize;
            a3 = (uint32_t *)((uint8_t *)a3 + 12);
        } else {
            /* Straddles the seam — two undraw entries, each reading first-half bg:
             * top rows [d2, SCREEN_HEIGHT) and bottom rows [SCREEN_HEIGHT, d2+15)
             * (which live at the mirror top, bg-save row 0 = xWordOff). */
            uint16_t wc       = (uint16_t)(entryBltsize & 0x3Fu);   /* word count */
            uint16_t topCount = (uint16_t)(SCREEN_HEIGHT - d2);
            uint16_t botCount = (uint16_t)((d2 + SPRITE_HEIGHT_LINES) - SCREEN_HEIGHT);
            uint16_t topSize  = (uint16_t)(((topCount * NUM_PLANES) << 6) | wc);
            uint16_t botSize  = (uint16_t)(((botCount * NUM_PLANES) << 6) | wc);
            /* top */
            *(uint32_t *)((uint8_t *)a3 + 0)  = undrawTwin(destAddr);
            *(uint32_t *)((uint8_t *)a3 + 4)  = destAddr;
            *(uint16_t *)((uint8_t *)a3 + 8)  = entryMod;
            *(uint16_t *)((uint8_t *)a3 + 10) = topSize;
            a3 = (uint32_t *)((uint8_t *)a3 + 12);
            /* bottom (mirror top; contiguous continuation of destAddr) */
            uint32_t botDest = destAddr + (uint32_t)topCount * (uint32_t)ROW_STRIDE_BYTES;
            *(uint32_t *)((uint8_t *)a3 + 0)  = undrawTwin(botDest);
            *(uint32_t *)((uint8_t *)a3 + 4)  = botDest;
            *(uint16_t *)((uint8_t *)a3 + 8)  = entryMod;
            *(uint16_t *)((uint8_t *)a3 + 10) = botSize;
            a3 = (uint32_t *)((uint8_t *)a3 + 12);
        }
#else
        /* OCS 1x buffer: d2 was wrapped into [0,SCREEN_HEIGHT); one entry. */
        *(uint32_t *)((uint8_t *)a3 + 0)  = destOff + (uint32_t)_frontbufferMaskBuffer;
        *(uint32_t *)((uint8_t *)a3 + 4)  = destAddr;
        *(uint16_t *)((uint8_t *)a3 + 8)  = entryMod;
        *(uint16_t *)((uint8_t *)a3 + 10) = entryBltsize;
        a3 = (uint32_t *)((uint8_t *)a3 + 12);
#endif

        /* Write sentinel at the (new) queue head */
        *(uint32_t *)a3 = BLT_BLITQ_END;
        _spriteBlitQueue = (uintptr_t)a3;
    }
    return;

fallback:
    DrawSpriteClipped(a0, d1, d2);
#endif /* !SWOS_SDL_BUILD */
}
#endif

/* Local statics matching SWOS2.s local data (queuedLineCount, savedSrcPlaneB, savedSrcPlaneA) */
static uint16_t queuedLineCount;
static uint32_t savedSrcPlaneA;
static uint32_t savedSrcPlaneB;

#ifdef SWOS_SDL_BUILD
/* Chunky 8bpp sprite blit. Source format (built by `blitDispatch`):
 *   - 1 byte per pixel = palette index (0..15 for OCS, up to 255 AGA)
 *   - row stride = wquads * 16 bytes
 *   - palette index 0 is transparent
 *
 * Destination: 8bpp chunky frontbuffer, row stride =
 * FRONTBUFFER_ROW_STRIDE_BYTES (_screen_width on SDL). Camera offsets
 * mirror the Amiga path (cameraXMod320 / cameraYMod320, Y-wrap). */
static void blitChunkySprite(SpriteGraphics *a0, int16_t dstX, int16_t dstY)
{
    if (!a0) return;
    const uint8_t *data = (const uint8_t *)(uintptr_t)a0->sprData;
    uint8_t *dstBase = (uint8_t *)(uintptr_t)_frontbufferBasePtr;
    if (!data || !dstBase) return;

    /* Fold the menu/replay horizontal offset into the screen X so the cull and
     * per-pixel clip below operate on the actual frontbuffer column
     * [0, _screen_width). Previously the offset lived only in dstBase, which made
     * the left _menu_draw_x_offset pixels a dead zone: a re-anchored far-left
     * replay player at dstX=-107 has real column 50 (on-screen) but was killed by
     * `dstX + sprW <= 0`, and `dx >= _screen_width` let the right edge overflow
     * into the next row. Showed up as the left-edge cut-off in widescreen replays. */
    dstX = (int16_t)(dstX + _menu_draw_x_offset);

    int16_t sprW  = a0->pixWidth;
    int16_t sprH  = a0->nlines;
    int16_t pitch = a0->wquads * 16;

    /* Sanity: sprites with absurd dims (stale header / garbage sprite
     * slot) can make the inner loops walk unmapped memory. Bail early. */
    if (sprW <= 0 || sprH <= 0 || pitch <= 0) return;
    if (sprW > 512 || sprH > 512) return;

    /* Full-rect cull: if the sprite is entirely off the 336×272 screen,
     * skip it before we ever dereference srcRow. The per-pixel `continue`
     * below is not enough when dstX is so negative that `dx` wraps into
     * the visible range for huge sprites (replay frame path was passing
     * dstX=-384). */
    if (dstX >= _screen_width || dstY >= _screen_height) return;
    if (dstX + sprW <= 0 || dstY + sprH <= 0) return;

    /* cameraXMod320/cameraYMod320 are Amiga hardware-scroll wrap
     * offsets — applicable when the display pans its bitplane base
     * pointer. On SDL we redraw the whole frontbuffer each frame
     * (see pitch.c:DrawChunkyPitch), so dstX/dstY are already in
     * final screen coords. Adding the wrap offsets double-translates
     * sprites off-screen and is what was hiding the players/ball. */

    for (int16_t py = 0; py < sprH; py++) {
        int16_t dy = dstY + py;
        if (dy < 0 || dy >= _screen_height) continue;

        const uint8_t *srcRow = data + (size_t)py * pitch;
        uint8_t       *dstRow = dstBase + (size_t)dy * FRONTBUFFER_ROW_STRIDE_BYTES;

        for (int16_t px = 0; px < sprW; px++) {
            int16_t dx = dstX + px;
            if (dx < 0 || dx >= _screen_width) continue;
            uint8_t src = srcRow[px];
            if (src) dstRow[dx] = src;
        }
    }
}
#endif

/* ---- DrawSpriteClipped: single-blit when sprite depth == pitch depth (OCS 4==4, or AGA sprites
 * toggled to 8==8) — one interleaved blit; else (AGA 4-plane sprite on 8-plane pitch)
 * per-plane composite + upper-plane clear. SWOS_SPRITE_PLANES flips this. ---- */
#if (SPRITE_NUM_PLANES == PITCH_NUM_PLANES)
__attribute__((used, externally_visible))
void DrawSpriteClipped(SpriteGraphics *a0, int16_t d1, int16_t d2)
{
#ifdef SWOS_SDL_BUILD
    blitChunkySprite(a0, d1, d2);
    return;
#else
    uint16_t sprType = a0->type;

    if (sprType != 0) {
        /* ---- branch_linked_planes path ---- */
        uint32_t *a3 = (uint32_t *)_spriteBlitQueue;
        uint8_t *a1 = (uint8_t *)_frontbufferBasePtr + _menu_draw_x_offset;
        int16_t d3 = a0->nlines;
        int16_t d5 = 0;

        /* Clip top */
        if (d2 < 0) {
            d5 = -d2;
            d3 += d2;
            d2 = 0;
        }
        /* Clip bottom */
        {
            int16_t d0 = d2 + d3 - SCREEN_HEIGHT;
            if (d0 > 0)
                d3 -= d0;
        }

        /* Apply camera offsets. */
        d1 += _cameraXMod320;
        d2 += _cameraYMod320;
#if !(defined(AGA) && !defined(SWOS_SDL_BUILD))
        /* OCS 1x buffer wraps Y; AGA draws no-wrap into the 2x-tall buffer (the
         * seam split below routes any overshoot into the mirror half, and a bob
         * wholly in the mirror half folds its bg-save offset back). */
        if (d2 >= SCREEN_HEIGHT)
            d2 -= SCREEN_HEIGHT;
#endif

        /* Compute edge masks and word count */
        uint16_t d6 = 0;
        uint16_t bitPos = (uint16_t)d1 & TILE_MASK;
        uint16_t sprPixMod = (uint16_t)a0->pixWidth & TILE_MASK;
        if (sprPixMod == 0)
            sprPixMod = 16;
        uint16_t totalBits = bitPos + sprPixMod;
        uint16_t d7 = (uint16_t)a0->wquads + 1;  /* WQUADS for linked planes */
        if (totalBits <= 16) {
            d6 = 0xFFFF;
            d7--;
        }

        /* Wait for blitter idle and program registers */
        waitBlit();
        BLT_W(_custom->bltalwm, d6);
        BLT_W(_custom->bltafwm, 0xFFFF);
        /* Shift amount */
        uint16_t shiftVal = ((uint16_t)d1 & TILE_MASK) << 12;
        BLT_W(_custom->bltcon1, shiftVal);
        /* BLTCON0 mode */
        uint32_t maskPtr = (uint32_t)a0->maskPtr;
        uint16_t bltcon0;
        if (maskPtr != 0) {
            bltcon0 = shiftVal | BLTCON0_BASE_LINKED;
        } else {
            bltcon0 = shiftVal | BLTCON0_BASE_SINGLE;
            BLT_W(_custom->bltadat, 0xFFFF);
        }
        BLT_W(_custom->bltcon0, bltcon0);
        /* Check for seam split */
        uint16_t d4 = 0;
        {
            int16_t overshoot = d2 + d3 - SCREEN_HEIGHT;
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
            /* No-wrap: only a bob straddling the seam (d2 < SCREEN_HEIGHT) splits
             * its tail into the mirror top. A bob wholly in the mirror half
             * (d2 >= SCREEN_HEIGHT) is one contiguous blit; its bg-save offset is
             * folded back at the main entry below. */
            if (d2 < SCREEN_HEIGHT && overshoot > 0) {
                d4 = (uint16_t)overshoot;
                d3 -= overshoot;
            }
#else
            if (overshoot > 0) {
                d4 = (uint16_t)overshoot;
                d3 -= overshoot;
            }
#endif
        }

        /* Compute destination pointer */
        int16_t xWordOff = (d1 >> TILE_SHIFT_X) << 1; /* asr.w + asl.w (signed) */
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
        if (_playGame) xWordOff += SCROLL_LEFT_MARGIN_BYTES;   /* §desync: pitch verge margin, in-match only */
#endif
        int32_t xWordOffL = (int32_t)xWordOff;  /* sign-extended */
        uint32_t d6save = (uint32_t)xWordOffL;
        uint32_t yOff = (uint16_t)d2 * (uint32_t)ROW_STRIDE_BYTES;
        uint32_t destAddr = (uint32_t)a1 + yOff + xWordOffL;

        BLT_L(_custom->bltcpt, destAddr);
        BLT_L(_custom->bltdpt, destAddr);
        /* Queue entry: mask+fb addresses */
        *(uint32_t *)((uint8_t *)a3 + 4) = destAddr;
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
        /* Fold a mirror-half dest back to its first-half bg-save row (1x mask). */
        uint32_t maskFirstHalfOff = destAddr - (uint32_t)a1;
        if (d2 >= SCREEN_HEIGHT)
            maskFirstHalfOff -= (uint32_t)FB_MIRROR_OFFSET_BYTES;
        uint32_t maskBufAddr = undrawTwin(destAddr); (void)maskFirstHalfOff;
#else
        uint32_t maskBufAddr = destAddr - (uint32_t)a1 + (uint32_t)_frontbufferMaskBuffer;
#endif
        *(uint32_t *)((uint8_t *)a3 + 0) = maskBufAddr;

        /* BLTSIZE = (nlines * NUM_PLANES) << 6 | wordCount */
        uint16_t bltsize = ((uint16_t)d3 << NUM_PLANES_SHIFT << 6) | d7;
        uint16_t blitWidthBytes = d7 << 1;

        /* Modulos */
        uint16_t tileStride = (uint16_t)a0->wquads;
        uint16_t srcMod = (tileStride << 1) - blitWidthBytes;
        BLT_W(_custom->bltamod, srcMod);
        BLT_W(_custom->bltbmod, srcMod);
        uint16_t destMod = PLANE_STRIDE - blitWidthBytes;
        BLT_W(_custom->bltcmod, destMod);
        BLT_W(_custom->bltdmod, destMod);
        /* Queue modulo */
        *(uint16_t *)((uint8_t *)a3 + 8) = destMod;

        /* Source top skip */
        uint32_t srcSkip = 0;
        if (d5 != 0) {
            srcSkip = (uint32_t)((uint16_t)d5 << NUM_PLANES_SHIFT) * (uint32_t)(tileStride << 1);
        }
        uint32_t srcData = (uint32_t)a0->sprData + srcSkip;
        BLT_L(_custom->bltbpt, srcData);
        uint32_t srcMask2 = (uint32_t)a0->maskPtr + srcSkip;
        BLT_L(_custom->bltapt, (uint32_t)srcMask2);
        /* Kick blit */
        BLT_W(_custom->bltsize, bltsize);
        *(uint16_t *)((uint8_t *)a3 + 10) = bltsize;
        a3 = (uint32_t *)((uint8_t *)a3 + 12);

        /* Handle Y-wrap tail portion */
        if (d4 != 0) {
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
            /* AGA 2x-tall buffer: the tail is the CONTIGUOUS continuation past
             * SCREEN_HEIGHT, so it belongs at the top of the MIRROR half
             * (+FB_MIRROR_OFFSET), NOT wrapped back to buffer row 0. Wrapping to
             * row 0 (the 1x-buffer assumption) is what left the "missing strip"
             * band where a bob straddles the display's mirror seam. */
            uint32_t destAddr2 = (uint32_t)a1 + (uint32_t)FB_MIRROR_OFFSET_BYTES + d6save;
#else
            uint32_t destAddr2 = (uint32_t)a1 + d6save;
#endif
            blitWidthBytes = d7; /* restore d7 (still in words) */
            /* Recompute: d7 was already halved? No, d7 is word count.
               In asm: lsr.w #1,d7 then uses d7 as half-word count for bltsize.
               Actually re-reading: d7 doesn't get halved in linked path.
               The asm does: d4 bltsize = (d4 << NUM_PLANES_SHIFT << 6) | d7 */
            uint16_t tailBltsize = ((uint16_t)d4 << NUM_PLANES_SHIFT << 6) | d7;

            *(uint32_t *)((uint8_t *)a3 + 4) = destAddr2;
            waitBlit();
            BLT_L(_custom->bltcpt, destAddr2);
            BLT_L(_custom->bltdpt, destAddr2);
            BLT_W(_custom->bltsize, tailBltsize);
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
            /* The mirror half's clean pitch == the first half's, and the bg-save
             * mask buffer is only 1x tall — so restore the tail from the FIRST-half
             * mask position (destAddr2 minus the mirror offset), not destAddr2's own
             * (out-of-range) offset. */
            uint32_t tailMask = undrawTwin(destAddr2);
#else
            uint32_t tailMask = destAddr2 - (uint32_t)a1 + (uint32_t)_frontbufferMaskBuffer;
#endif
            *(uint32_t *)((uint8_t *)a3 + 0) = tailMask;
            /* Copy modulo from previous entry */
            *(uint16_t *)((uint8_t *)a3 + 8) = *(uint16_t *)((uint8_t *)a3 - 4);
            *(uint16_t *)((uint8_t *)a3 + 10) = tailBltsize;
            a3 = (uint32_t *)((uint8_t *)a3 + 12);
        }

        /* Seal queue */
        *(uint32_t *)a3 = BLT_BLITQ_END;
        _spriteBlitQueue = (uintptr_t)a3;
        return;
    }

    /* ---- Normal (non-linked) 4-plane path ---- */
    {
        uint32_t *a3 = (uint32_t *)_spriteBlitQueue;
        uint8_t *a1 = (uint8_t *)_frontbufferBasePtr + _menu_draw_x_offset;

        /* Redundant type check removed - we know sprType == 0 here */
        int16_t d3 = a0->nlines;
        int16_t d5 = 0;

        /* Clip top */
        if (d2 < 0) {
            d5 = -d2;
            d3 += d2;
            d2 = 0;
        }
        /* Clip bottom */
        {
            int16_t d0 = d2 + d3 - SCREEN_HEIGHT;
            if (d0 > 0)
                d3 -= d0;
        }

        /* Apply camera offsets. */
        d1 += _cameraXMod320;
        d2 += _cameraYMod320;
#if !(defined(AGA) && !defined(SWOS_SDL_BUILD))
        /* OCS 1x buffer wraps Y; AGA draws no-wrap into the 2x-tall buffer. */
        if (d2 >= SCREEN_HEIGHT)
            d2 -= SCREEN_HEIGHT;
#endif

        /* Compute edge masks and word count */
        uint16_t d6 = 0;
        uint16_t bitPos = (uint16_t)d1 & TILE_MASK;
        uint16_t sprPixMod = (uint16_t)a0->pixWidth & TILE_MASK;
        if (sprPixMod == 0)
            sprPixMod = 16;
        uint16_t totalBits = bitPos + sprPixMod;
        uint16_t d7 = (uint16_t)a0->wquads + 1;  /* TILE_STRIDE for normal path */
        if (totalBits <= 16) {
            d6 = 0xFFFF;
            d7--;
        }

        /* Wait for blitter idle */
        waitBlit();
        BLT_W(_custom->bltalwm, d6);
        BLT_W(_custom->bltafwm, 0xFFFF);
        /* Shift amount */
        uint16_t shiftVal = ((uint16_t)d1 & TILE_MASK) << 12;
        BLT_W(_custom->bltcon1, shiftVal);
        /* BLTCON0 mode */
        uint32_t maskPtr = (uint32_t)a0->maskPtr;
        uint16_t bltcon0;
        if (maskPtr != 0) {
            bltcon0 = shiftVal | BLTCON0_BASE_LINKED;
        } else {
            bltcon0 = shiftVal | BLTCON0_BASE_SINGLE;
            BLT_W(_custom->bltadat, 0xFFFF);
        }
        BLT_W(_custom->bltcon0, bltcon0);
        /* Check for seam split */
        uint16_t d4 = 0;
        {
            int16_t overshoot = d2 + d3 - SCREEN_HEIGHT;
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
            /* No-wrap: split only a bob straddling the seam (d2 < SCREEN_HEIGHT).
             * A bob wholly in the mirror half is one contiguous blit (bg-save
             * offset folded back at the entry below). */
            if (d2 < SCREEN_HEIGHT && overshoot > 0) {
                d4 = (uint16_t)overshoot;
                d3 -= overshoot;
            }
#else
            if (overshoot > 0) {
                d4 = (uint16_t)overshoot;
                d3 -= overshoot;
            }
#endif
        }

        /* Save queued line count */
        queuedLineCount = (uint16_t)d3;

        /* Compute destination pointer */
        int16_t xByteOff = (d1 >> TILE_SHIFT_X) << 1; /* asr.w + asl.w (signed) */
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
        if (_playGame) xByteOff += SCROLL_LEFT_MARGIN_BYTES;   /* §desync: pitch verge margin, in-match only */
#endif
        int32_t xByteOffL = (int32_t)xByteOff;
        uint32_t d6save = (uint32_t)xByteOffL;
        uint32_t yOff = (uint16_t)d2 * (uint32_t)ROW_STRIDE_BYTES;
        uint32_t destAddr = (uint32_t)a1 + yOff + xByteOffL;

        BLT_L(_custom->bltcpt, destAddr);
        BLT_L(_custom->bltdpt, destAddr);
        uint8_t *a2 = (uint8_t *)destAddr;  /* tracks dest for multi-plane passes */

        /* Queue entry */
        *(uint32_t *)((uint8_t *)a3 + 4) = destAddr;
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
        /* Fold a mirror-half dest back to its first-half bg-save row (1x mask). */
        uint32_t maskFirstHalfOff = destAddr - (uint32_t)a1;
        if (d2 >= SCREEN_HEIGHT)
            maskFirstHalfOff -= (uint32_t)FB_MIRROR_OFFSET_BYTES;
        uint32_t maskBufAddr = undrawTwin(destAddr); (void)maskFirstHalfOff;
#else
        uint32_t maskBufAddr = destAddr - (uint32_t)a1 + (uint32_t)_frontbufferMaskBuffer;
#endif
        *(uint32_t *)((uint8_t *)a3 + 0) = maskBufAddr;

        /* BLTSIZE for 4-plane blit: (nlines * 4) << 6 | wordCount */
        uint16_t bltsize4 = ((uint16_t)d3 << NUM_PLANES_SHIFT << 6) | d7;
        *(uint16_t *)((uint8_t *)a3 + 10) = bltsize4;

        /* Per-plane bltsize: nlines << 6 | wordCount */
        uint16_t bltsize1 = ((uint16_t)d3 << 6) | d7;
        uint16_t blitWidthBytes = d7 << 1;

        /* Modulos */
        uint16_t tileStride = (uint16_t)a0->wquads;
        uint16_t srcModA = tileStride << 1;
        uint16_t d1mod = srcModA;  /* save for source B modulo */
        uint16_t srcModAB = srcModA - blitWidthBytes;
        BLT_W(_custom->bltamod, srcModAB);
        /* Source B modulo: stride * 4 planes - blit width */
        uint16_t srcModB = (d1mod << NUM_PLANES_SHIFT) - blitWidthBytes;
        BLT_W(_custom->bltbmod, srcModB);
        /* Dest C/D modulo: PLANE_STRIDE - blit width (per row, per plane) */
        uint16_t destMod = PLANE_STRIDE - blitWidthBytes;
        *(uint16_t *)((uint8_t *)a3 + 8) = destMod;
        /* Actual C/D mod includes inter-plane gap: ROW_STRIDE - PLANE_STRIDE + destMod */
        uint16_t destCDMod = destMod + (ROW_STRIDE_BYTES - PLANE_STRIDE);
        BLT_W(_custom->bltcmod, destCDMod);
        BLT_W(_custom->bltdmod, destCDMod);
        /* Source top skip */
        uint32_t srcSkip5 = 0;
        if (d5 != 0) {
            srcSkip5 = (uint32_t)((uint16_t)d5 << NUM_PLANES_SHIFT) * (uint32_t)(tileStride << 1);
        }

        /* Source plane data */
        uint32_t srcDataAddr = (uint32_t)a0->sprData + srcSkip5;
        BLT_L(_custom->bltbpt, srcDataAddr);
        uint8_t *a5ptr = (uint8_t *)srcDataAddr;

        /* Source mask */
        uint32_t srcMaskAddr = (uint32_t)a0->maskPtr + (srcSkip5 >> NUM_PLANES_SHIFT);
        BLT_L(_custom->bltapt, (uint32_t)srcMaskAddr);
        uint8_t *a4ptr = (uint8_t *)srcMaskAddr;

        a3 = (uint32_t *)((uint8_t *)a3 + 12);

        /* Kick first plane blit */
        BLT_W(_custom->bltsize, bltsize1);
        /* Save source pointers for tail portion */
        savedSrcPlaneB = (uint32_t)a4ptr;
        savedSrcPlaneA = (uint32_t)a5ptr;

        /* Blit remaining planes (1..NUM_PLANES-1) */
        uint32_t srcPlaneStep = (uint32_t)(tileStride << 1);
        a2 += PLANE_STRIDE;
        a5ptr += srcPlaneStep;

        {
            int16_t planeIdx;
            for (planeIdx = NUM_PLANES_MINUS1; planeIdx > 0; planeIdx--)
            {
                waitBlit();
                BLT_L(_custom->bltcpt, (uint32_t)a2);
                BLT_L(_custom->bltdpt, (uint32_t)a2);
                BLT_L(_custom->bltapt, (uint32_t)a4ptr);
                BLT_L(_custom->bltbpt, (uint32_t)a5ptr);
                BLT_W(_custom->bltsize, bltsize1);
                a2 += PLANE_STRIDE;
                a5ptr += srcPlaneStep;
            }
        }

        /* Handle Y-wrap tail portion */
        if (d4 != 0) {
            /* Compute source skip for remaining lines */
            uint32_t tailSrcSkipB = (uint32_t)queuedLineCount * (uint32_t)(tileStride << 1);
            uint32_t tailSrcB = savedSrcPlaneB + tailSrcSkipB;
            uint32_t tailSrcSkipA = tailSrcSkipB << NUM_PLANES_SHIFT;
            uint32_t tailSrcA = savedSrcPlaneA + tailSrcSkipA;

            waitBlit();

#if defined(AGA) && !defined(SWOS_SDL_BUILD)
            /* Contiguous continuation past SCREEN_HEIGHT → mirror top (row 0 of
             * the mirror half), NOT buffer row 0; bg-save reads the first-half
             * row (destAddr2 minus the mirror offset). Only reached when the bob
             * straddles the seam (d2 < SCREEN_HEIGHT), per the guarded d4 above. */
            uint32_t tailDest = (uint32_t)a1 + (uint32_t)FB_MIRROR_OFFSET_BYTES + d6save;
#else
            /* Dest wraps to top of buffer at column offset */
            uint32_t tailDest = (uint32_t)a1 + d6save;
#endif
            BLT_L(_custom->bltcpt, tailDest);
            BLT_L(_custom->bltdpt, tailDest);
            BLT_L(_custom->bltapt, tailSrcB);
            BLT_L(_custom->bltbpt, tailSrcA);
            /* Queue entry for tail */
            *(uint32_t *)((uint8_t *)a3 + 4) = tailDest;
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
            uint32_t tailMask = undrawTwin(tailDest);
#else
            uint32_t tailMask = tailDest - (uint32_t)a1 + (uint32_t)_frontbufferMaskBuffer;
#endif
            *(uint32_t *)((uint8_t *)a3 + 0) = tailMask;

            /* Per-plane blit size */
            uint16_t tailBltsize1 = ((uint16_t)d4 << 6) | (blitWidthBytes >> 1);
            BLT_W(_custom->bltsize, tailBltsize1);
            /* N-plane blit size for queue */
            uint16_t tailBltsizeN = ((uint16_t)d4 << NUM_PLANES_SHIFT << 6) | (blitWidthBytes >> 1);
            *(uint16_t *)((uint8_t *)a3 + 10) = tailBltsizeN;

            /* Blit remaining tail planes (1..NUM_PLANES-1) */
            tailDest += PLANE_STRIDE;
            tailSrcA += srcPlaneStep;

            {
                int16_t planeIdx;
                for (planeIdx = NUM_PLANES_MINUS1; planeIdx > 0; planeIdx--)
                {
                    waitBlit();
                    BLT_L(_custom->bltcpt, tailDest);
                    BLT_L(_custom->bltdpt, tailDest);
                    BLT_L(_custom->bltapt, tailSrcB);
                    BLT_L(_custom->bltbpt, tailSrcA);
                    BLT_W(_custom->bltsize, tailBltsize1);
                    tailDest += PLANE_STRIDE;
                    tailSrcA += srcPlaneStep;
                }
            }

            /* Copy modulo from previous entry */
            *(uint16_t *)((uint8_t *)a3 + 8) = *(uint16_t *)((uint8_t *)a3 - 4);
            a3 = (uint32_t *)((uint8_t *)a3 + 12);
        }

        /* Seal queue */
        *(uint32_t *)a3 = BLT_BLITQ_END;
        _spriteBlitQueue = (uintptr_t)a3;
    }
#endif /* !SWOS_SDL_BUILD */
}
#else
__attribute__((used, externally_visible))
void DrawSpriteClipped(SpriteGraphics *a0, int16_t d1, int16_t d2)
{
#ifdef SWOS_SDL_BUILD
    blitChunkySprite(a0, d1, d2);
    return;
#else
    uint16_t sprType = a0->type;

    if (sprType != 0) {
        /* ---- branch_linked_planes path ---- */
        uint32_t *a3 = (uint32_t *)_spriteBlitQueue;
        uint8_t *a1 = (uint8_t *)_frontbufferBasePtr + _menu_draw_x_offset;
        int16_t d3 = a0->nlines;
        int16_t d5 = 0;

        /* Clip top */
        if (d2 < 0) {
            d5 = -d2;
            d3 += d2;
            d2 = 0;
        }
        /* Clip bottom */
        {
            int16_t d0 = d2 + d3 - SCREEN_HEIGHT;
            if (d0 > 0)
                d3 -= d0;
        }

        /* Apply camera offsets. */
        d1 += _cameraXMod320;
        d2 += _cameraYMod320;
#if !(defined(AGA) && !defined(SWOS_SDL_BUILD))
        /* OCS 1x buffer wraps Y; AGA draws no-wrap into the 2x-tall buffer (the
         * seam split below routes any overshoot into the mirror half, and a bob
         * wholly in the mirror half folds its bg-save offset back). */
        if (d2 >= SCREEN_HEIGHT)
            d2 -= SCREEN_HEIGHT;
#endif

        /* Compute edge masks and word count */
        uint16_t d6 = 0;
        uint16_t bitPos = (uint16_t)d1 & TILE_MASK;
        uint16_t sprPixMod = (uint16_t)a0->pixWidth & TILE_MASK;
        if (sprPixMod == 0)
            sprPixMod = 16;
        uint16_t totalBits = bitPos + sprPixMod;
        uint16_t d7 = (uint16_t)a0->wquads + 1;  /* WQUADS for linked planes */
        if (totalBits <= 16) {
            d6 = 0xFFFF;
            d7--;
        }

        /* Wait for blitter idle and program registers */
        waitBlit();
        BLT_W(_custom->bltalwm, d6);
        BLT_W(_custom->bltafwm, 0xFFFF);
        /* Shift amount */
        uint16_t shiftVal = ((uint16_t)d1 & TILE_MASK) << 12;
        BLT_W(_custom->bltcon1, shiftVal);
        /* BLTCON0 mode */
        uint32_t maskPtr = (uint32_t)a0->maskPtr;
        uint16_t bltcon0;
        if (maskPtr != 0) {
            bltcon0 = shiftVal | BLTCON0_BASE_LINKED;
        } else {
            bltcon0 = shiftVal | BLTCON0_BASE_SINGLE;
            BLT_W(_custom->bltadat, 0xFFFF);
        }
        BLT_W(_custom->bltcon0, bltcon0);
        /* Check for seam split */
        uint16_t d4 = 0;
        {
            int16_t overshoot = d2 + d3 - SCREEN_HEIGHT;
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
            /* No-wrap: only a bob straddling the seam (d2 < SCREEN_HEIGHT) splits
             * its tail into the mirror top. A bob wholly in the mirror half
             * (d2 >= SCREEN_HEIGHT) is one contiguous blit; its bg-save offset is
             * folded back at the main entry below. */
            if (d2 < SCREEN_HEIGHT && overshoot > 0) {
                d4 = (uint16_t)overshoot;
                d3 -= overshoot;
            }
#else
            if (overshoot > 0) {
                d4 = (uint16_t)overshoot;
                d3 -= overshoot;
            }
#endif
        }

        /* Compute destination pointer */
        int16_t xWordOff = (d1 >> TILE_SHIFT_X) << 1; /* asr.w + asl.w (signed) */
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
        if (_playGame) xWordOff += SCROLL_LEFT_MARGIN_BYTES;   /* §desync: pitch verge margin, in-match only */
#endif
        int32_t xWordOffL = (int32_t)xWordOff;  /* sign-extended */
        uint32_t d6save = (uint32_t)xWordOffL;
        uint32_t yOff = (uint16_t)d2 * (uint32_t)ROW_STRIDE_BYTES;
        uint32_t destAddr = (uint32_t)a1 + yOff + xWordOffL;

        BLT_L(_custom->bltcpt, destAddr);
        BLT_L(_custom->bltdpt, destAddr);
        /* Queue entry: mask+fb addresses */
        *(uint32_t *)((uint8_t *)a3 + 4) = destAddr;
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
        /* Fold a mirror-half dest back to its first-half bg-save row (1x mask). */
        uint32_t maskFirstHalfOff = destAddr - (uint32_t)a1;
        if (d2 >= SCREEN_HEIGHT)
            maskFirstHalfOff -= (uint32_t)FB_MIRROR_OFFSET_BYTES;
        uint32_t maskBufAddr = undrawTwin(destAddr); (void)maskFirstHalfOff;
#else
        uint32_t maskBufAddr = destAddr - (uint32_t)a1 + (uint32_t)_frontbufferMaskBuffer;
#endif
        *(uint32_t *)((uint8_t *)a3 + 0) = maskBufAddr;

        /* Queue-undraw values stay 8-plane interleaved: ProcessSpriteBlitQueue replays one
         * contiguous restore over all NUM_PLANES playfield planes from the bg-save. */
        uint16_t bltsize = ((uint16_t)d3 << NUM_PLANES_SHIFT << 6) | d7;   /* queue: 8-plane */
        uint16_t blitWidthBytes = d7 << 1;
        uint16_t tileStride = (uint16_t)a0->wquads;
        uint16_t srcStrideBytes = (uint16_t)(tileStride << 1);
        uint16_t destMod = PLANE_STRIDE - blitWidthBytes;                 /* queue undraw modulo */
        *(uint16_t *)((uint8_t *)a3 + 8) = destMod;
        *(uint16_t *)((uint8_t *)a3 + 10) = bltsize;

        /* DRAW per-plane. Sprite + its interleaved mask are `sp` planes (4 for a 16-colour
         * bob / 8 soccer_s); the playfield is NUM_PLANES(8). Planes 0..sp-1 cookie-cut the
         * sprite; sp..NUM_PLANES-1 clear its footprint (16-colour bob -> colours 0-15). Per
         * plane: SOURCE steps one whole sprite scanline (sp plane-rows), DEST one whole
         * playfield scanline (ROW_STRIDE). The old single 8-plane interleaved blit read a
         * 4-plane sprite across 8 dest planes -> upper planes picked up the next sprite. */
        uint16_t sp = spriteNumPlanes(a0->ordinal);
        uint32_t srcSkip = (d5 != 0)
            ? (uint32_t)(uint16_t)(d5 * sp) * (uint32_t)srcStrideBytes : 0u;
        uint32_t srcData = (uint32_t)a0->sprData + srcSkip;
        uint32_t srcMask2 = (uint32_t)a0->maskPtr + srcSkip;
        uint16_t drawSrcMod  = (uint16_t)((uint16_t)(sp * srcStrideBytes) - blitWidthBytes);
        uint16_t drawDestMod = (uint16_t)(ROW_STRIDE_BYTES - blitWidthBytes);
        uint16_t drawBltsize = ((uint16_t)d3 << 6) | d7;                  /* nlines rows, 1 plane */
        uint16_t clearCon0 = (a0->maskPtr != 0)
            ? (uint16_t)(BLTCON0_CLEAR_MASK | shiftVal)      /* ~mask & dest */
            : (uint16_t)(BLTCON0_USED_LF_00 | shiftVal);     /* no mask: zero-fill footprint */
        if (sp == NUM_PLANES && a0->maskPtr != 0) {
            /* ONE blit: a frame stored 8 planes deep with its mask repeated per plane
             * (display.h, spriteIsPadded8). Source and destination are both a plane row
             * at a time -- the sprite's rows at its stride, the playfield's at
             * PLANE_STRIDE, which is exactly the 8-plane interleave -- so A?B:C over
             * lines*8 rows is the eight per-plane blits below in one: planes 0-3 get the
             * sprite, planes 4-7 (B is zero there) mask?0:background, as the clear blit. */
            waitBlit();
            BLT_W(_custom->bltcon0, bltcon0);
            BLT_W(_custom->bltamod, (uint16_t)(srcStrideBytes - blitWidthBytes));
            BLT_W(_custom->bltbmod, (uint16_t)(srcStrideBytes - blitWidthBytes));
            BLT_W(_custom->bltcmod, (uint16_t)(PLANE_STRIDE - blitWidthBytes));
            BLT_W(_custom->bltdmod, (uint16_t)(PLANE_STRIDE - blitWidthBytes));
            BLT_L(_custom->bltapt, srcMask2);
            BLT_L(_custom->bltbpt, srcData);
            BLT_L(_custom->bltcpt, destAddr);
            BLT_L(_custom->bltdpt, destAddr);
            BLT_W(_custom->bltsize, (uint16_t)(((uint16_t)d3 << NUM_PLANES_SHIFT << 6) | d7));
        } else
        /* The blit mode and modulos are the same for every cookie-cut plane, and again for
         * every clear plane: written once each, and only the pointers and the size per
         * plane. A blitter register keeps its value, and nothing else touches the blitter
         * while a bob is drawn (the VBlank interrupt does not). Each plane's blit must end
         * before the next starts, so the CPU still waits between them -- but now on fewer
         * register writes (2026-10-14: DrawSpriteClipped was a third of the A1200's frame). */
        {
            int16_t p;
            waitBlit();
            BLT_W(_custom->bltcon0, bltcon0);
            BLT_W(_custom->bltamod, drawSrcMod);
            BLT_W(_custom->bltbmod, drawSrcMod);
            BLT_W(_custom->bltcmod, drawDestMod);
            BLT_W(_custom->bltdmod, drawDestMod);
            for (p = 0; p < NUM_PLANES; p++) {
                uint32_t pdst = destAddr + (uint32_t)p * PLANE_STRIDE;
                if (p) waitBlit();
                if (p < (int16_t)sp) {
                    BLT_L(_custom->bltapt, srcMask2 + (uint32_t)p * srcStrideBytes);
                    BLT_L(_custom->bltbpt, srcData  + (uint32_t)p * srcStrideBytes);
                } else {
                    if (p == (int16_t)sp)
                        BLT_W(_custom->bltcon0, clearCon0);
                    BLT_L(_custom->bltapt, srcMask2);        /* plane-0 silhouette */
                }
                BLT_L(_custom->bltcpt, pdst);
                BLT_L(_custom->bltdpt, pdst);
                BLT_W(_custom->bltsize, drawBltsize);
            }
        }
        a3 = (uint32_t *)((uint8_t *)a3 + 12);

        /* Handle Y-wrap tail portion (the d4 rows that wrap past the seam). */
        if (d4 != 0) {
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
            /* AGA 2x-tall buffer: the tail is the CONTIGUOUS continuation past
             * SCREEN_HEIGHT, so it belongs at the top of the MIRROR half
             * (+FB_MIRROR_OFFSET), NOT wrapped back to buffer row 0. */
            uint32_t destAddr2 = (uint32_t)a1 + (uint32_t)FB_MIRROR_OFFSET_BYTES + d6save;
#else
            uint32_t destAddr2 = (uint32_t)a1 + d6save;
#endif
            uint16_t tailBltsize = ((uint16_t)d4 << NUM_PLANES_SHIFT << 6) | d7;  /* queue: 8-plane */

            *(uint32_t *)((uint8_t *)a3 + 4) = destAddr2;
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
            uint32_t tailMask = undrawTwin(destAddr2);
#else
            uint32_t tailMask = destAddr2 - (uint32_t)a1 + (uint32_t)_frontbufferMaskBuffer;
#endif
            *(uint32_t *)((uint8_t *)a3 + 0) = tailMask;
            *(uint16_t *)((uint8_t *)a3 + 8) = *(uint16_t *)((uint8_t *)a3 - 4);  /* 8-plane destMod */
            *(uint16_t *)((uint8_t *)a3 + 10) = tailBltsize;
            a3 = (uint32_t *)((uint8_t *)a3 + 12);

            /* DRAW the wrapped d4 rows per-plane, sourced from sprite row d3 onward. */
            uint32_t tailSrc  = srcData  + (uint32_t)d3 * (uint32_t)sp * (uint32_t)srcStrideBytes;
            uint32_t tailMaskS = srcMask2 + (uint32_t)d3 * (uint32_t)sp * (uint32_t)srcStrideBytes;
            uint16_t tailDrawBltsize = ((uint16_t)d4 << 6) | d7;             /* d4 rows, 1 plane */
            if (sp == NUM_PLANES && a0->maskPtr != 0) {
                /* One blit, as the main draw above. */
                waitBlit();
                BLT_W(_custom->bltcon0, bltcon0);
                BLT_W(_custom->bltamod, (uint16_t)(srcStrideBytes - blitWidthBytes));
                BLT_W(_custom->bltbmod, (uint16_t)(srcStrideBytes - blitWidthBytes));
                BLT_W(_custom->bltcmod, (uint16_t)(PLANE_STRIDE - blitWidthBytes));
                BLT_W(_custom->bltdmod, (uint16_t)(PLANE_STRIDE - blitWidthBytes));
                BLT_L(_custom->bltapt, tailMaskS);
                BLT_L(_custom->bltbpt, tailSrc);
                BLT_L(_custom->bltcpt, destAddr2);
                BLT_L(_custom->bltdpt, destAddr2);
                BLT_W(_custom->bltsize, (uint16_t)(((uint16_t)d4 << NUM_PLANES_SHIFT << 6) | d7));
            } else
            {
                int16_t p;      /* as above: mode and modulos once, pointers per plane */
                waitBlit();
                BLT_W(_custom->bltcon0, bltcon0);
                BLT_W(_custom->bltamod, drawSrcMod);
                BLT_W(_custom->bltbmod, drawSrcMod);
                BLT_W(_custom->bltcmod, drawDestMod);
                BLT_W(_custom->bltdmod, drawDestMod);
                for (p = 0; p < NUM_PLANES; p++) {
                    uint32_t pdst = destAddr2 + (uint32_t)p * PLANE_STRIDE;
                    if (p) waitBlit();
                    if (p < (int16_t)sp) {
                        BLT_L(_custom->bltapt, tailMaskS + (uint32_t)p * srcStrideBytes);
                        BLT_L(_custom->bltbpt, tailSrc  + (uint32_t)p * srcStrideBytes);
                    } else {
                        if (p == (int16_t)sp)
                            BLT_W(_custom->bltcon0, clearCon0);
                        BLT_L(_custom->bltapt, tailMaskS);
                    }
                    BLT_L(_custom->bltcpt, pdst);
                    BLT_L(_custom->bltdpt, pdst);
                    BLT_W(_custom->bltsize, tailDrawBltsize);
                }
            }
        }

        /* Seal queue */
        *(uint32_t *)a3 = BLT_BLITQ_END;
        _spriteBlitQueue = (uintptr_t)a3;
        return;
    }

    /* ---- Normal (non-linked) 4-plane path ---- */
    {
        uint32_t *a3 = (uint32_t *)_spriteBlitQueue;
        uint8_t *a1 = (uint8_t *)_frontbufferBasePtr + _menu_draw_x_offset;

        /* Redundant type check removed - we know sprType == 0 here */
        int16_t d3 = a0->nlines;
        int16_t d5 = 0;

        /* Clip top */
        if (d2 < 0) {
            d5 = -d2;
            d3 += d2;
            d2 = 0;
        }
        /* Clip bottom */
        {
            int16_t d0 = d2 + d3 - SCREEN_HEIGHT;
            if (d0 > 0)
                d3 -= d0;
        }

        /* Apply camera offsets. */
        d1 += _cameraXMod320;
        d2 += _cameraYMod320;
#if !(defined(AGA) && !defined(SWOS_SDL_BUILD))
        /* OCS 1x buffer wraps Y; AGA draws no-wrap into the 2x-tall buffer. */
        if (d2 >= SCREEN_HEIGHT)
            d2 -= SCREEN_HEIGHT;
#endif

        /* Compute edge masks and word count */
        uint16_t d6 = 0;
        uint16_t bitPos = (uint16_t)d1 & TILE_MASK;
        uint16_t sprPixMod = (uint16_t)a0->pixWidth & TILE_MASK;
        if (sprPixMod == 0)
            sprPixMod = 16;
        uint16_t totalBits = bitPos + sprPixMod;
        uint16_t d7 = (uint16_t)a0->wquads + 1;  /* TILE_STRIDE for normal path */
        if (totalBits <= 16) {
            d6 = 0xFFFF;
            d7--;
        }

        /* Wait for blitter idle */
        waitBlit();
        BLT_W(_custom->bltalwm, d6);
        BLT_W(_custom->bltafwm, 0xFFFF);
        /* Shift amount */
        uint16_t shiftVal = ((uint16_t)d1 & TILE_MASK) << 12;
        BLT_W(_custom->bltcon1, shiftVal);
        /* BLTCON0 mode */
        uint32_t maskPtr = (uint32_t)a0->maskPtr;
        uint16_t bltcon0;
        if (maskPtr != 0) {
            bltcon0 = shiftVal | BLTCON0_BASE_LINKED;
        } else {
            bltcon0 = shiftVal | BLTCON0_BASE_SINGLE;
            BLT_W(_custom->bltadat, 0xFFFF);
        }
        BLT_W(_custom->bltcon0, bltcon0);
        /* Check for seam split */
        uint16_t d4 = 0;
        {
            int16_t overshoot = d2 + d3 - SCREEN_HEIGHT;
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
            /* No-wrap: split only a bob straddling the seam (d2 < SCREEN_HEIGHT).
             * A bob wholly in the mirror half is one contiguous blit (bg-save
             * offset folded back at the entry below). */
            if (d2 < SCREEN_HEIGHT && overshoot > 0) {
                d4 = (uint16_t)overshoot;
                d3 -= overshoot;
            }
#else
            if (overshoot > 0) {
                d4 = (uint16_t)overshoot;
                d3 -= overshoot;
            }
#endif
        }

        /* Save queued line count */
        queuedLineCount = (uint16_t)d3;

        /* Compute destination pointer */
        int16_t xByteOff = (d1 >> TILE_SHIFT_X) << 1; /* asr.w + asl.w (signed) */
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
        if (_playGame) xByteOff += SCROLL_LEFT_MARGIN_BYTES;   /* §desync: pitch verge margin, in-match only */
#endif
        int32_t xByteOffL = (int32_t)xByteOff;
        uint32_t d6save = (uint32_t)xByteOffL;
        uint32_t yOff = (uint16_t)d2 * (uint32_t)ROW_STRIDE_BYTES;
        uint32_t destAddr = (uint32_t)a1 + yOff + xByteOffL;

        BLT_L(_custom->bltcpt, destAddr);
        BLT_L(_custom->bltdpt, destAddr);
        uint8_t *a2 = (uint8_t *)destAddr;  /* tracks dest for multi-plane passes */

        /* Queue entry */
        *(uint32_t *)((uint8_t *)a3 + 4) = destAddr;
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
        /* Fold a mirror-half dest back to its first-half bg-save row (1x mask). */
        uint32_t maskFirstHalfOff = destAddr - (uint32_t)a1;
        if (d2 >= SCREEN_HEIGHT)
            maskFirstHalfOff -= (uint32_t)FB_MIRROR_OFFSET_BYTES;
        uint32_t maskBufAddr = undrawTwin(destAddr); (void)maskFirstHalfOff;
#else
        uint32_t maskBufAddr = destAddr - (uint32_t)a1 + (uint32_t)_frontbufferMaskBuffer;
#endif
        *(uint32_t *)((uint8_t *)a3 + 0) = maskBufAddr;

        /* BLTSIZE for 4-plane blit: (nlines * 4) << 6 | wordCount */
        uint16_t bltsize4 = ((uint16_t)d3 << NUM_PLANES_SHIFT << 6) | d7;
        *(uint16_t *)((uint8_t *)a3 + 10) = bltsize4;

        /* Per-plane bltsize: nlines << 6 | wordCount */
        uint16_t bltsize1 = ((uint16_t)d3 << 6) | d7;
        uint16_t blitWidthBytes = d7 << 1;

        /* Sprite real depth (4 for a 16-colour bob / 8 soccer_s). type==0 has a SINGLE-plane
         * mask (planesFlag==0), so the A/mask modulo stays 1-plane; only the B (sprite)
         * strides scale with `sp`. */
        uint16_t sp = spriteNumPlanes(a0->ordinal);

        /* Modulos */
        uint16_t tileStride = (uint16_t)a0->wquads;
        uint16_t srcModA = tileStride << 1;
        uint16_t d1mod = srcModA;  /* save for source B modulo */
        uint16_t srcModAB = srcModA - blitWidthBytes;
        BLT_W(_custom->bltamod, srcModAB);
        /* Source B modulo: sprite scanline (sp plane-rows) - blit width. Was <<NUM_PLANES_SHIFT
         * (×8) — a 4-plane sprite steps sp plane-rows/scanline, not 8. */
        uint16_t srcModB = (uint16_t)(d1mod * sp) - blitWidthBytes;
        BLT_W(_custom->bltbmod, srcModB);
        /* Dest C/D modulo: PLANE_STRIDE - blit width (per row, per plane) */
        uint16_t destMod = PLANE_STRIDE - blitWidthBytes;
        *(uint16_t *)((uint8_t *)a3 + 8) = destMod;
        /* Actual C/D mod includes inter-plane gap: ROW_STRIDE - PLANE_STRIDE + destMod */
        uint16_t destCDMod = destMod + (ROW_STRIDE_BYTES - PLANE_STRIDE);
        BLT_W(_custom->bltcmod, destCDMod);
        BLT_W(_custom->bltdmod, destCDMod);
        /* Source top skip: d5 clipped scanlines. DATA = sp plane-rows/scanline; MASK = 1. */
        uint32_t srcSkip5  = (uint32_t)((uint16_t)(d5 * sp)) * (uint32_t)(tileStride << 1);
        uint32_t maskSkip5 = (uint32_t)d5 * (uint32_t)(tileStride << 1);

        /* Source plane data */
        uint32_t srcDataAddr = (uint32_t)a0->sprData + srcSkip5;
        BLT_L(_custom->bltbpt, srcDataAddr);
        uint8_t *a5ptr = (uint8_t *)srcDataAddr;

        /* Source mask (single plane) */
        uint32_t srcMaskAddr = (uint32_t)a0->maskPtr + maskSkip5;
        BLT_L(_custom->bltapt, (uint32_t)srcMaskAddr);
        uint8_t *a4ptr = (uint8_t *)srcMaskAddr;

        a3 = (uint32_t *)((uint8_t *)a3 + 12);

        /* Kick first plane blit */
        BLT_W(_custom->bltsize, bltsize1);
        /* Save source pointers for tail portion */
        savedSrcPlaneB = (uint32_t)a4ptr;
        savedSrcPlaneA = (uint32_t)a5ptr;

        /* Remaining planes: 1..sp-1 cookie-cut the sprite; sp..NUM_PLANES-1 clear the
         * footprint in the upper playfield planes (16-colour bob -> colours 0-15). The
         * single-plane mask a4ptr is reused (not advanced); the sprite a5ptr advances. */
        uint16_t clearCon0 = (a0->maskPtr != 0)
            ? (uint16_t)(BLTCON0_CLEAR_MASK | shiftVal)      /* ~mask & dest */
            : (uint16_t)(BLTCON0_USED_LF_00 | shiftVal);     /* no mask: zero-fill footprint */
        uint32_t srcPlaneStep = (uint32_t)(tileStride << 1);
        a2 += PLANE_STRIDE;
        a5ptr += srcPlaneStep;

        {
            int16_t planeIdx;
            for (planeIdx = 1; planeIdx < (int16_t)sp; planeIdx++)
            {
                waitBlit();
                BLT_L(_custom->bltcpt, (uint32_t)a2);
                BLT_L(_custom->bltdpt, (uint32_t)a2);
                BLT_L(_custom->bltapt, (uint32_t)a4ptr);
                BLT_L(_custom->bltbpt, (uint32_t)a5ptr);
                BLT_W(_custom->bltsize, bltsize1);
                a2 += PLANE_STRIDE;
                a5ptr += srcPlaneStep;
            }
            if (sp < NUM_PLANES) {
                waitBlit();
                BLT_W(_custom->bltcon0, clearCon0);
                for (; planeIdx < NUM_PLANES; planeIdx++) {
                    /* Wait BEFORE writing, never after the kick: the last blit then runs
                     * on while the CPU sets up the next bob. */
                    if (planeIdx != (int16_t)sp) waitBlit();
                    BLT_L(_custom->bltcpt, (uint32_t)a2);
                    BLT_L(_custom->bltdpt, (uint32_t)a2);
                    BLT_L(_custom->bltapt, (uint32_t)a4ptr);   /* single-plane silhouette */
                    BLT_W(_custom->bltsize, bltsize1);
                    a2 += PLANE_STRIDE;
                }
            }
        }

        /* Handle Y-wrap tail portion (d4 wrapped rows). */
        if (d4 != 0) {
            /* Source skip for the wrapped rows: MASK is 1-plane, DATA is sp-plane. */
            uint32_t tailSrcSkipB = (uint32_t)queuedLineCount * (uint32_t)(tileStride << 1);
            uint32_t tailSrcB = savedSrcPlaneB + tailSrcSkipB;                  /* mask */
            uint32_t tailSrcSkipA = tailSrcSkipB * (uint32_t)sp;
            uint32_t tailSrcA = savedSrcPlaneA + tailSrcSkipA;                  /* sprite */

            waitBlit();
            /* The main-draw clear pass may have left bltcon0 = CLEAR_MASK; restore linked. */
            BLT_W(_custom->bltcon0, bltcon0);

#if defined(AGA) && !defined(SWOS_SDL_BUILD)
            /* Contiguous continuation past SCREEN_HEIGHT → mirror top. */
            uint32_t tailDest = (uint32_t)a1 + (uint32_t)FB_MIRROR_OFFSET_BYTES + d6save;
#else
            uint32_t tailDest = (uint32_t)a1 + d6save;
#endif
            BLT_L(_custom->bltcpt, tailDest);
            BLT_L(_custom->bltdpt, tailDest);
            BLT_L(_custom->bltapt, tailSrcB);
            BLT_L(_custom->bltbpt, tailSrcA);
            *(uint32_t *)((uint8_t *)a3 + 4) = tailDest;
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
            uint32_t tailMask = undrawTwin(tailDest);
#else
            uint32_t tailMask = tailDest - (uint32_t)a1 + (uint32_t)_frontbufferMaskBuffer;
#endif
            *(uint32_t *)((uint8_t *)a3 + 0) = tailMask;

            uint16_t tailBltsize1 = ((uint16_t)d4 << 6) | (blitWidthBytes >> 1);
            BLT_W(_custom->bltsize, tailBltsize1);                              /* plane 0 */
            uint16_t tailBltsizeN = ((uint16_t)d4 << NUM_PLANES_SHIFT << 6) | (blitWidthBytes >> 1);
            *(uint16_t *)((uint8_t *)a3 + 10) = tailBltsizeN;                   /* queue 8-plane */

            /* planes 1..sp-1 cookie-cut; sp..NUM_PLANES-1 clear. Mask (A) reused. */
            tailDest += PLANE_STRIDE;
            tailSrcA += srcPlaneStep;
            {
                int16_t planeIdx;
                for (planeIdx = 1; planeIdx < (int16_t)sp; planeIdx++) {
                    waitBlit();
                    BLT_L(_custom->bltcpt, tailDest);
                    BLT_L(_custom->bltdpt, tailDest);
                    BLT_L(_custom->bltapt, tailSrcB);
                    BLT_L(_custom->bltbpt, tailSrcA);
                    BLT_W(_custom->bltsize, tailBltsize1);
                    tailDest += PLANE_STRIDE;
                    tailSrcA += srcPlaneStep;
                }
                if (sp < NUM_PLANES) {
                    waitBlit();
                    BLT_W(_custom->bltcon0, clearCon0);
                    for (; planeIdx < NUM_PLANES; planeIdx++) {
                        if (planeIdx != (int16_t)sp) waitBlit();    /* before, not after */
                        BLT_L(_custom->bltcpt, tailDest);
                        BLT_L(_custom->bltdpt, tailDest);
                        BLT_L(_custom->bltapt, tailSrcB);
                        BLT_W(_custom->bltsize, tailBltsize1);
                        tailDest += PLANE_STRIDE;
                    }
                }
            }

            *(uint16_t *)((uint8_t *)a3 + 8) = *(uint16_t *)((uint8_t *)a3 - 4);
            a3 = (uint32_t *)((uint8_t *)a3 + 12);
        }

        /* Seal queue */
        *(uint32_t *)a3 = BLT_BLITQ_END;
        _spriteBlitQueue = (uintptr_t)a3;
    }
#endif /* !SWOS_SDL_BUILD */
}
#endif

/* ================================================================
 * DrawSprite_entry2  (SWOS2.s line 6313)
 *
 * Draws sprite at (d1, d2) using DrawSpriteClipped, then appends
 * a queue terminator. Saves/restores spriteBlitQueue around the
 * clipped draw.
 * ================================================================ */
__attribute__((used, externally_visible))
void DrawSprite_entry2(SpriteGraphics *a0, int16_t d1, int16_t d2)
{
    uintptr_t savedQueue = _spriteBlitQueue;
    DrawSpriteClipped(a0, d1, d2);
    _spriteBlitQueue = savedQueue;
    *(uint32_t *)savedQueue = BLT_BLITQ_END;
}

/* ================================================================
 * DrawSprite16Pixels  (SWOS2.s line 6324)
 *
 * Looks up sprite by index and draws it at (d1, d2).
 * ================================================================ */
__attribute__((used, externally_visible))
void DrawSprite16Pixels(uint16_t spriteIndex, int16_t x, int16_t y)
{
    SpriteGraphics **sprGfxTable = (SpriteGraphics **)_spriteGraphicsPtr;
    SpriteGraphics *spr = sprGfxTable[spriteIndex];
    DrawSprite_entry2(spr, x, y);
}

/* ================================================================
 * DrawSpriteCentered  (SWOS2.s line 6338)
 *
 * Like DrawSprite16Pixels but adjusts x,y by the sprite's center
 * offsets before drawing.
 * ================================================================ */
__attribute__((used, externally_visible))
void DrawSpriteCentered(uint16_t spriteIndex, int16_t x, int16_t y)
{
    SpriteGraphics **sprGfxTable = (SpriteGraphics **)_spriteGraphicsPtr;
    SpriteGraphics *spr = sprGfxTable[spriteIndex];
    x -= spr->centerX;
    y -= spr->centerY;
    DrawSprite_entry2(spr, x, y);
}

/* ================================================================
 * DrawSpriteInCameraView  (SWOS2.s line 6520)
 *
 * Draws a sprite in camera-relative coordinates with bounds
 * checking. Skips draw if sprite is fully outside view.
 * ================================================================ */
__attribute__((used, externally_visible))
void DrawSpriteInCameraView(uint16_t spriteIndex, int16_t x, int16_t y)
{
    SpriteGraphics **sprGfxTable = (SpriteGraphics **)_spriteGraphicsPtr;
    SpriteGraphics *spr = sprGfxTable[spriteIndex];

    x -= Render_CameraX();
    y -= Render_CameraY();

    /* Cull if completely off-screen */
    if (x >= _screen_width)
        return;
    if (y >= SCREEN_HEIGHT)
        return;
    {
        int16_t negWidth = -spr->pixWidth;
        if (x <= negWidth)
            return;
    }
    {
        int16_t negHeight = -spr->nlines;
        if (y <= negHeight)
            return;
    }

    DrawSpriteClipped(spr, x, y);
}

/* ================================================================
 * DrawSpriteHotspotInCameraView  (SWOS2.s line 6504)
 *
 * Like DrawSpriteInCameraView but adjusts position by the sprite's
 * center (hotspot) offsets first.
 * ================================================================ */
__attribute__((used, externally_visible))
void DrawSpriteHotspotInCameraView(uint16_t spriteIndex, int16_t x, int16_t y)
{
    SpriteGraphics **sprGfxTable = (SpriteGraphics **)_spriteGraphicsPtr;
    SpriteGraphics *spr = sprGfxTable[spriteIndex];

    x -= spr->centerX;
    y -= spr->centerY;

    /* Fall through to DrawSpriteInCameraView logic (with spr already resolved) */
    x -= Render_CameraX();
    y -= Render_CameraY();

    if (x >= _screen_width)
        return;
    if (y >= SCREEN_HEIGHT)
        return;
    {
        int16_t negWidth = -spr->pixWidth;
        if (x <= negWidth)
            return;
    }
    {
        int16_t negHeight = -spr->nlines;
        if (y <= negHeight)
            return;
    }

    DrawSpriteClipped(spr, x, y);
}

/* ================================================================
 * DarkenRectangle  (SWOS2.s line 6555)
 *
 * Darkens a rectangle by drawing the sprite's high-bit overlay.
 * Resolves sprite by index and falls through to DrawSpriteClipped.
 * In assembly this falls through to DrawSpriteClipped via bra *+4;
 * the sprite type at offset 8 is non-zero for darken sprites, which
 * triggers the branch_linked_planes path.
 * ================================================================ */
__attribute__((used, externally_visible))
void DarkenRectangle(uint16_t spriteIndex, int16_t x, int16_t y)
{
    SpriteGraphics **sprGfxTable = (SpriteGraphics **)_spriteGraphicsPtr;
    SpriteGraphics *spr = sprGfxTable[spriteIndex];
    DrawSpriteClipped(spr, x, y);
}

/* ================================================================
 * DrawQueuedSprites  (SWOS2.s line 6971)
 *
 * Pops sprites from the main render stack and dispatches them
 * to the appropriate draw function based on sprite type and
 * player range.
 *
 * Render stack entries (12 bytes, pushed upward):
 *   +0 = sprite struct pointer (a1/a4)
 *   +4 = sprite graphics pointer (a0)
 *   +8 = x (int16_t)
 *   +10 = y (int16_t)
 * ================================================================ */
__attribute__((used, externally_visible))
/* Lowest z that means "screen-pinned HUD", not a world object. The HUD sprites
 * are assigned 8000/9000/10000+ in game_hud.c and game_state.c; pitch-derived
 * world depths never come near it. */
#define HUD_LAYER_Z_MIN 8000

void DrawQueuedSprites(void)
{
    uint8_t *a2 = (uint8_t *)_renderStackPtr;

    /* Stack entry on disk: [Sprite *a1][SpriteGraphics *a0][int16 d1][int16 d2]
     * On 32-bit each pointer is 4 B (total 12 B/entry); on 64-bit each is 8 B
     * (total 20 B/entry). Reads back must use sizeof(void *) — earlier
     * a2 -= 4 unconditionally was reading half a pointer on 64-bit. */
    while (a2 != _renderStackBase) {
        a2 -= 2;
        int16_t d2 = *(int16_t *)a2;
        a2 -= 2;
        int16_t d1 = *(int16_t *)a2;
        a2 -= sizeof(SpriteGraphics *);
        SpriteGraphics *a0 = *(SpriteGraphics **)a2;
        a2 -= sizeof(Sprite *);
        Sprite *a4 = *(Sprite **)a2;

        /* Check if highlight recording needed */
        if (a4->teamNumber != 0) {
            int16_t d0 = a0->ordinal;
            SaveCoordinatesForHighlights(d0, (uint16_t)d1, (uint16_t)d2);
        }

#ifdef SWOS_SDL_BUILD
        /* SWOS_REPLAY_TRACE (src/replay_v2.c): what the live match drew, to compare a
         * recording's playback against. */
        if (a4->z.s.i < HUD_LAYER_Z_MIN) {
            extern void ReplayTrace_LiveSprite(int16_t ordinal, int16_t x, int16_t y);
            ReplayTrace_LiveSprite(a0->ordinal, d1, d2);
        }
#endif

        /* Check if a4 is a player sprite (teamNumber 1 or 2).
         * Original asm used address range check (goalie1Sprite..bottomCornerLeftPositions),
         * but sprites moved from asm to C data sections so addresses are no longer contiguous. */
        if (a4->teamNumber == 1 || a4->teamNumber == 2) {
            QueueMaskedSpriteBlitClipped(a0, d1, d2);
            continue;
        }

        /* Screen-pinned HUD sprites go to their own layer so the sub-pixel
         * scroll shift cannot move them — see the HUD layer notes in
         * platform_sdl.c. They are identified by z: every one of them is set
         * to 8000 (controlled-player name), 9000 (scoreboard grid) or 10000+
         * (team names, score digits, dash, half-time result), while world
         * sprites carry pitch-derived depths far below that. They are already
         * sorted last for the same reason, so this changes only WHERE they are
         * written, never the order they are drawn in.
         *
         * The diversion is a framebuffer swap rather than a change to the blit:
         * blitChunkySprite takes its destination from _frontbufferBasePtr, so
         * pointing that at the layer needs no new blit path and cannot affect
         * the world sprites at all. */
#ifdef SWOS_SDL_BUILD
        uint8_t  *hudLayer = NULL;
        /* uintptr_t, NOT uint32_t: _frontbufferBasePtr is a volatile uintptr_t
         * and s_hud_layer is a 64-bit heap pointer — narrowing it here would
         * lose the top half and blit into nowhere. */
        uintptr_t savedFbBase = 0;
        if (a4->z.s.i >= HUD_LAYER_Z_MIN) {
            extern uint8_t *Platform_HudLayerBuffer(void);
            extern void     Platform_HudLayerMark(int, int, int, int);
            hudLayer = Platform_HudLayerBuffer();
            if (hudLayer) {
                savedFbBase = _frontbufferBasePtr;
                _frontbufferBasePtr = (uintptr_t)hudLayer;
                Platform_HudLayerMark((int)d1 + _menu_draw_x_offset, (int)d2,
                                      a0 ? a0->pixWidth : 0, a0 ? a0->nlines : 0);
            }
        }
#endif
        /* Check sprite save flag: if SPRITE_SAVESPRITE ($48) in the sprite struct */
        if (a4->saveSprite != 0) {
            DrawSprite_entry2(a0, d1, d2);
        } else {
            DrawSpriteClipped(a0, d1, d2);
        }
#ifdef SWOS_SDL_BUILD
        if (hudLayer)
            _frontbufferBasePtr = savedFbBase;
#endif
    }
}

/* ================================================================
 * RenderSpriteBandPlanePair  (SWOS2.s line 7069)
 *
 * Renders a single sprite into a hardware sprite band copper list
 * pair (a4/a5). Programs VPOS/HPOS/CTL words and blits sprite
 * data into the copper list buffers.
 *
 * in: a0 = sprite graphics, d1 = x, d2 = y
 *     a4/a5 = copper list plane pair pointers (advanced on exit)
 *     a6 = CUSTOM base
 * ================================================================ */
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
/* HW-sprite X re-centre (AGA only): the FMODE=1 display shows the bitplanes (pitch, bobs,
 * menus) re-centred +16px vs OCS; hardware sprites use raw screen coords via the fixed 0x70
 * reference and never got that +16px, so they landed a tile left of the bobs (the on-pitch
 * "twitch" + ball/shadow separation). Add the same +16px. */
extern int16_t AGA_HwSpriteXShift(void);   /* display.c: 16, or 0 where the display itself is corrected */
#endif

static void renderSpriteBandPlanePair(SpriteGraphics *sprGfx, int16_t x, int16_t y,
                                       uint16_t **pA4, uint16_t **pA5)
{

    uint16_t *a4 = *pA4;
    uint16_t *a5 = *pA5;

    int16_t d3 = y + sprGfx->nlines;

#if defined(AGA) && !defined(SWOS_SDL_BUILD)
    x += AGA_HwSpriteXShift();   /* re-centre HW sprites onto the +16px-shifted bobs/pitch */
#endif
    /* Add display offsets */
    x += 0x70;    /* horizontal display start */
    y += 0x24;    /* vertical display start */
    d3 += 0x24;

    /* VPOS high byte | HPOS/2 */
    uint16_t d4 = ((uint16_t)y & 0xFF) << 8;
    uint16_t hpos = ((uint16_t)x >> 1) & 0xFF;
    uint16_t posWord = d4 + hpos;
    *a4++ = posWord;
    *a5++ = posWord;

    /* CTL word: VSTOP low byte << 8, with carry bits */
    uint16_t ctlD4 = ((uint16_t)d3 & 0xFF) << 8;
    if ((uint16_t)y & 0x100)
        ctlD4 |= 4;   /* VPOS bit 8 -> CTL bit 2 */
    if ((uint16_t)d3 & 0x100)
        ctlD4 |= 2;   /* VSTOP bit 8 -> CTL bit 1 */
    if ((uint16_t)x & 1)
        ctlD4 |= 1;   /* HPOS low bit -> CTL bit 0 */

    *a4++ = ctlD4;
    uint16_t ctlA5 = ctlD4 | 0x80;  /* bset #7 on low byte of a5 entry: ATTACH bit */
    *a5++ = ctlA5;

    /* Blit sprite data into copper list buffers */
    /* First plane pair */
    waitBlit();
    /* The source steps a whole sprite scanline: the sprite's own depth, 8 for a padded
     * running frame (display.h, spriteIsPadded8), 4 for the rest. */
    BLT_W(_custom->bltamod, (spriteNumPlanes(sprGfx->ordinal) - 2) * 2);
    BLT_L(_custom->bltapt, (uint32_t)(uintptr_t)sprGfx->sprData);
    BLT_L(_custom->bltdpt, (uint32_t)(uintptr_t)a4);
    BLT_W(_custom->bltsize, sprGfx->bltsize);
    /* Second plane pair: source at data+4 */
    uintptr_t srcPlane2 = (uintptr_t)sprGfx->sprData + 4;
    waitBlit();
    BLT_L(_custom->bltapt, (uint32_t)srcPlane2);
    BLT_L(_custom->bltdpt, (uint32_t)(uintptr_t)a5);
    BLT_W(_custom->bltsize, sprGfx->bltsize);
    /* Advance past sprite data: nlines * 4 bytes (2 words per line) */
    uint16_t nlines = (uint16_t)sprGfx->nlines;
    uint16_t dataBytes = nlines << 2;
    a4 += (dataBytes >> 1);
    a5 += (dataBytes >> 1);

    *pA4 = a4;
    *pA5 = a5;
}

/* ================================================================
 * RenderSpriteBandsToCopperLists  (SWOS2.s line 7007)
 *
 * Processes plane01 and plane23 render stacks, rendering each
 * sprite band entry into the copper list sprite buffers.
 * ================================================================ */
__attribute__((used, externally_visible))
void RenderSpriteBandsToCopperLists(void)
{
#ifdef SWOS_SDL_BUILD
    /* SDL has no Amiga copper-list / bitplane infrastructure — every
     * write below targets bitPlane{0..3}BasePtr (Amiga chip memory)
     * via Amiga blitter regs that don't exist here. On 64-bit the
     * pointer-as-uint32 globals also give us truncated addresses that
     * SIGSEGV when written. Sprites on SDL are drawn through the
     * chunky 8bpp path in DrawQueuedSprites (already called by
     * DrawSprites above us), so this function is a no-op. */
    return;
#else


    /* Set up blitter for straight copy (A->D) */
    waitBlit();
    BLT_W(_custom->bltalwm, 0xFFFF);
    BLT_W(_custom->bltafwm, 0xFFFF);
    BLT_W(_custom->bltcon1, 0);
    BLT_W(_custom->bltcon0, BLTCON0_USEA_USED_LF_F0);
    /* HW-sprite band reads only planes 0-3 (attached pair). Source scanline stride is
     * now the sprite's real 4-plane row (8 B), so after each 2-word (4 B) plane-pair
     * read, skip (SPRITE_NUM_PLANES-2)*2 = 4 to the next line. Was (NUM_PLANES-2)*2 = 12,
     * which only worked because sprites were padded to 8 planes (16 B rows). */
    BLT_W(_custom->bltamod, (SPRITE_NUM_PLANES - 2) * 2);
    BLT_W(_custom->bltdmod, 0);
    /* ---- Process plane 0/1 render stack ---- */
    uint16_t *a4 = (uint16_t *)_bitPlane0BasePtr;
    uint16_t *a5 = (uint16_t *)_bitPlane1BasePtr;
    uint8_t *a3 = (uint8_t *)_plane01RenderStackPtr;

    while (a3 != _plane01RenderStackBase) {
        a3 -= 2;
        int16_t d2 = *(int16_t *)a3;
        a3 -= 2;
        int16_t d1 = *(int16_t *)a3;
        a3 -= sizeof(SpriteGraphics *);
        SpriteGraphics *a0 = *(SpriteGraphics **)a3;
        a3 -= sizeof(uint8_t *);
        uint8_t *a1 = *(uint8_t **)a3;

        if (*(uint16_t *)a1 != 0) {
            int16_t d0 = a0->ordinal;
            SaveCoordinatesForHighlights(d0, (uint16_t)d1, (uint16_t)d2);
        }

        renderSpriteBandPlanePair(a0, d1, d2, &a4, &a5);
    }

    /* Terminate plane 0/1 copper lists */
    *(uint32_t *)a4 = _copperZeroLong;
    *(uint32_t *)a5 = _copperZeroLong;

    /* ---- Process plane 2/3 render stack ---- */
    a4 = (uint16_t *)_bitPlane2BasePtr;
    a5 = (uint16_t *)_bitPlane3BasePtr;
    a3 = (uint8_t *)_plane23RenderStackPtr;

    while (a3 != _plane23RenderStackBase) {
        a3 -= 2;
        int16_t d2 = *(int16_t *)a3;
        a3 -= 2;
        int16_t d1 = *(int16_t *)a3;
        a3 -= sizeof(SpriteGraphics *);
        SpriteGraphics *a0 = *(SpriteGraphics **)a3;
        a3 -= sizeof(uint8_t *);
        uint8_t *a1 = *(uint8_t **)a3;

        if (*(uint16_t *)a1 != 0) {
            int16_t d0 = a0->ordinal;
            SaveCoordinatesForHighlights(d0, (uint16_t)d1, (uint16_t)d2);
        }

        renderSpriteBandPlanePair(a0, d1, d2, &a4, &a5);
    }

    /* Terminate plane 2/3 copper lists */
    *(uint32_t *)a4 = _copperZeroLong;
    *(uint32_t *)a5 = _copperZeroLong;
#endif /* !SWOS_SDL_BUILD */
}

/* ================================================================
 * BuildSpriteBandAndGeneralRenderStacks  (SWOS2.s line 7130)
 *
 * Iterates sorted sprites, culls off-screen ones, and distributes
 * visible sprites into one of three render stacks:
 *   - plane01 band (hardware sprite band, top-most Y)
 *   - plane23 band (hardware sprite band, second-top Y)
 *   - general render stack (software blitter sprites)
 *
 * Uses occlusion buckets (16-pixel wide columns) to determine
 * whether a narrow sprite (<=16px) can use a hardware band or
 * must fall back to the general stack.
 * ================================================================ */

/* Local data matching SWOS2.s (plane01BandTopY, plane23BandTopY at lines 7294-7297) */
static int16_t plane01BandTopY_local;
static int16_t plane23BandTopY_local;

/* Occlusion buckets: In asm, occlMinYBuckets (5 words) and occlMinYBucketsActive
   (26 words) are contiguous. Negative bucket indices (from sprites partially off
   the left screen edge) read/write into the guard region (occlMinYBuckets).
   We MUST keep them contiguous to avoid undefined behavior from index -1 writes.

   SDL widescreen: 26 active buckets only cover 26*16 = 416 px. With a widescreen
   render (_screen_width up to PITCH_VIEW_MAX_WIDTH = 672 = 42 columns) a sprite
   near the right edge indexes past the array, and the `a3[col] = d2` write below
   corrupts whatever the linker placed next in BSS — it was stomping stb's
   stbi__flip_vertically_on_write flag, which silently inverted every in-match
   screenshot while leaving the live display correct. Size the active region for the
   max render width plus a wide-sprite margin (and clamp the span below as a
   belt-and-braces guard). Amiga keeps the exact 26-word asm layout. */
#ifdef SWOS_SDL_BUILD
#define OCCL_ACTIVE_BUCKETS 48   /* >= ceil(PITCH_VIEW_MAX_WIDTH/16)=42 cols + edge margin */
#else
#define OCCL_ACTIVE_BUCKETS 26   /* original asm layout (336 px screen) */
#endif
static int16_t occlBuckets_combined[5 + OCCL_ACTIVE_BUCKETS];
#define occlMinYBuckets_local   (occlBuckets_combined)
#define occlMinYBucketsActive_local (occlBuckets_combined + 5)

__attribute__((used, externally_visible))
/* ------------------------------------------------------------------
 * Animation visibility — separate from `onScreen`, on purpose.
 *
 * `onScreen` is a SIMULATION input: UpdateControlledPlayer will not
 * auto-select a player whose onScreen is clear, so it is pinned to the
 * simulation view and must not follow the window size (GAMEPLAY.md 25.2).
 * Animation is a PRESENTATION concern and has to follow what is actually
 * DRAWN: `setNextPlayerFrame` skips a sprite whose flag is clear while
 * `movePlayer` still moves it, so a sprite that is drawn but not flagged
 * moves without animating — it slides.
 *
 * The port widens the view HORIZONTALLY only (_screen_width >= SCREEN_WIDTH;
 * the line count is fixed), so this test takes the simulation DRAW width
 * (display.h: the render width locally, one agreed width in netplay) and the
 * simulation HEIGHT. In a replay the rig pins that width to SCREEN_WIDTH and
 * sets the simulation height from the recording, which makes this identical to
 * `onScreen` — so the corpus is unaffected — while at a 16:9 render (484 wide)
 * the extra margin animates instead of sliding.
 * ------------------------------------------------------------------ */
/* Not static: the animation gate reads this list a frame after it is built, so it is
 * match state, and src/sim_state.c registers it (SPRITE_ANIMVIS_MAX: sprites.h). */
const Sprite *g_spriteAnimVisible[SPRITE_ANIMVIS_MAX];
int16_t       g_spriteAnimVisibleCount;

/* Which margin animates is decided for the SIMULATION DRAW width (display.h), not
 * the render width. Locally they are the same -- f1d348c: a sprite in a widescreen
 * margin is drawn, so it has to animate, or it slides along frozen, and that is not
 * to be given up. The replay rig pins it to 336, because a recording was made with no
 * margin, which keeps the corpus exact and leaves every other render -> simulation
 * leak visible to scripts/replay_widecheck.sh. A netplay session sets it to one width
 * both peers agreed, so a 4:3 and a 16:9 player animate the same margin. (It used to
 * be the render width everywhere but the rig, which made two such peers diverge.) */

/* The players walking off (to half time, or the showers at the end) leave by the right touchline,
 * and the original's screen -- 336 wide, the game camera's -- loses them there: they walk out of
 * the picture and stop beyond it. A wider view still has room for them, and showed them standing
 * in a row at the edge (tester report 0045, 2026-10-30). So while they walk off, a world sprite
 * past that window's right edge is not drawn, nor recorded (replay_v2.c), as on the original;
 * nothing the game decides depends on it (onScreen stays as it is). */
int Sprite_PastWalkOffEdge(const Sprite *s, const SpriteGraphics *g)
{
    extern uint16_t _gameState;
    if (_gameState != ST_GOING_TO_HALFTIME && _gameState != ST_PLAYERS_GOING_TO_SHOWER)
        return 0;
    if (s->z.s.i >= HUD_LAYER_Z_MIN)
        return 0;
    return (int16_t)(s->x.s.i - g->centerX - _cameraX.s.i) >= SCREEN_WIDTH;
}

int Sprite_AnimVisible(const Sprite *s)
{
    int16_t i;
    for (i = 0; i < g_spriteAnimVisibleCount; i++)
        if (g_spriteAnimVisible[i] == s)
            return 1;
    return 0;
}

void BuildSpriteBandAndGeneralRenderStacks(void)
{
    int16_t d7 = (int16_t)_numSpritesToRender;
    g_spriteAnimVisibleCount = 0;
    if (d7 == 0)
        return;

    uint8_t *a4 = (uint8_t *)_plane01RenderStackBase;
    uint8_t *a5 = (uint8_t *)_plane23RenderStackBase;
    uint8_t *a6loc = (uint8_t *)_renderStackBase;

    /* Initialize all occlusion bucket words to 0x7FFF (asm did 31 move.w's; on SDL the
     * active region is larger for widescreen, so initialize the whole array). */
    {
        int i;
        for (i = 0; i < (int)(sizeof(occlBuckets_combined) / sizeof(occlBuckets_combined[0])); i++)
            occlBuckets_combined[i] = 0x7FFF;
    }

    plane01BandTopY_local = 0x7D00;
    plane23BandTopY_local = 0x7D00;

    int16_t *a3 = occlMinYBucketsActive_local;

    /* Walk sorted sprites from back to front (highest index first) */
    Sprite **a2 = _sortedSprites + d7;
    d7--;

#if defined(DETERMINISTIC_MODE) && DETERMINISTIC_SPRITE_LIMIT > 0
    int16_t spritesEnqueued = 0;
#endif

    while (a2 != _sortedSprites) {
        a2--;
        Sprite *a1 = *a2;

        int16_t sprIdx = a1->imageIndex;
        if (sprIdx < 0) {
            /* Asm: bmi.w dec_and_loop — skip without touching onScreen */
            d7--;
            if (d7 < 0) break;
            continue;
        }

        /* Look up sprite graphics */
        SpriteGraphics **sprGfxTable = (SpriteGraphics **)_spriteGraphicsPtr;
        SpriteGraphics *a0 = sprGfxTable[(uint16_t)sprIdx];

        /* Camera-relative position with hotspot adjustment */
        /* The cull is measured from the GAME camera, the placement from the
         * render origin; identical at 336 wide (display.h). */
        int16_t d1sim = a1->x.s.i - a0->centerX - _cameraX.s.i;
        int16_t d1    = a1->x.s.i - a0->centerX - Render_CameraX();
        /* Y the same: `onScreen` from the game camera, placement from the render origin
         * (identical unless the picture is taller than the view: camera.c). */
        int16_t d2sim = a1->y.s.i - a0->centerY - _cameraY.s.i - a1->z.s.i;
        int16_t d2 = a1->y.s.i - a0->centerY - Render_CameraY();
        d2 -= a1->z.s.i;
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
        /* the FMODE=3 window's HUD layout (display.h): drawing only, after the cull */
        if (a1->z.s.i >= HUD_LAYER_Z_MIN && AGA_FM3())
            d1 = (int16_t)(d1 + AGA_FM3_HUD_SHIFT_PX);
#endif

        /* Two culls, deliberately: one decides GAMEPLAY, the other DRAWING.
         *
         * `onScreen` is a simulation input — UpdateControlledPlayer will not
         * auto-select a player whose onScreen is clear (GAMEPLAY.md §5, §25.2)
         * — so it must NOT depend on the display. Keying it off the live
         * render width meant a widescreen player faced a different CPU: a
         * 650-wide view admits men the Amiga culls, one of them is nearer the
         * ball, and the selection changes. GAMEPLAY.md §25.4.
         *
         * The window is 336 wide and _sim_view_height tall, and the width is a
         * CONSTANT rather than the view: measured against the DOS recordings,
         * whose visible screen is 320 wide, a sprite is recorded onScreen out
         * to camera-relative x 335 and off from 336 (42032 samples across
         * zkz-12065730, per-image centerX/pixWidth taken from the DOS banks —
         * clean separation, no overlap). Both releases cull to a 336-wide
         * buffer; only the height follows the display (200 DOS, 272 Amiga),
         * which is also what the same measurement gives: on out to y 199.
         *
         * Whether to ENQUEUE the sprite is a rendering question and keys off
         * the render width, so a wide display still draws everything it can
         * fit — the renderer draws wider than the simulation sees, which is
         * exactly what §25.5 prescribes. The render view is never narrower
         * than the simulation view (main.c clamps _screen_width to at least
         * SCREEN_WIDTH), so this only ever draws MORE, never less. */
        {
            int16_t negW = -a0->pixWidth;
            int16_t negH = -a0->nlines;
            int onScreen = !(d1sim >= SCREEN_WIDTH || d2sim >= _sim_view_height ||
                             d1sim <= negW || d2sim <= negH);
            int inRenderView = !(d1 >= _screen_width || d2 >= _screen_height ||
                                 d1 <= negW || d2 <= negH);
            a1->onScreen = (int16_t)onScreen;
            /* Only ever ADD to what `onScreen` already allows, and only when
             * the simulation draw width really is wider than the simulation
             * view. At `_sim_draw_width == SCREEN_WIDTH` this list stays empty
             * and the animation gate is bit-for-bit what it was -- which is the
             * case the replay rig runs in, so the corpus cannot be perturbed.
             * Measured from that width's origin, not the render's: the list is
             * match state (sim_state.c), so it must not depend on this display.
             * And world sprites only: a HUD sprite (z >= HUD_LAYER_Z_MIN) is placed
             * for this display's width, so whether it fell inside the margin
             * depended on the display -- and nothing animates it anyway. */
            if (!onScreen && _sim_draw_width > SCREEN_WIDTH && a1->z.s.i < HUD_LAYER_Z_MIN) {
                int16_t d1draw = a1->x.s.i - a0->centerX - Sim_DrawCameraX();
                /* The rows too, where the picture that goes with that width is taller
                 * than the view (the DOS view, widescreen: Sim_DrawHeight in camera.c).
                 * Otherwise this is d2sim against the view's height, as it always was. */
                int16_t drawH = Sim_DrawHeight();
                int16_t d2draw = drawH == _sim_view_height ? d2sim
                               : (int16_t)(a1->y.s.i - a0->centerY - Sim_DrawCameraY() - a1->z.s.i);
                int animVis = !(d1draw >= _sim_draw_width || d2draw >= drawH ||
                                d1draw <= negW || d2draw <= negH);
                if (animVis && g_spriteAnimVisibleCount < SPRITE_ANIMVIS_MAX)
                    g_spriteAnimVisible[g_spriteAnimVisibleCount++] = a1;
            }
            if (!inRenderView || Sprite_PastWalkOffEdge(a1, a0)) {
                d7--;
                if (d7 < 0) break;
                continue;
            }
        }

#if defined(DETERMINISTIC_MODE) && DETERMINISTIC_SPRITE_LIMIT > 0
        /* Cap enqueued sprites per frame to keep C and ASM frame timing in sync.
         * Still mark as onScreen=1 so game logic continues to update them. */
        if (spritesEnqueued >= DETERMINISTIC_SPRITE_LIMIT) {
            a1->onScreen = 1;
            d7--;
            if (d7 < 0) break;
            continue;
        }
#endif

        /* Check if overlay is active or sprite is wider than 16px.
         *
         * Narrow sprites (pixWidth ≤ 16) are normally routed to the
         * Amiga hardware sprite bands (plane01/plane23 stacks, drawn
         * by RenderSpriteBandsToCopperLists via the copper list). On
         * SDL the copper-list path is a no-op (writes to `custom->`
         * registers that don't back a framebuffer), so any sprite that
         * lands in a band would silently vanish — that's why player
         * bodies and the ball render nothing. Force every sprite onto
         * the general render stack so DrawQueuedSprites handles them
         * via blitChunkySprite. */
#ifdef SWOS_SDL_BUILD
        if (1) {
#else
        /* FMODE=3 (display.h AGA_FMODE): the fetch starts at DDFSTRT $18, before the DMA
         * slots of the band's sprites, so every sprite is a bob */
        if (_overlayActiveFlag != 0 || a0->pixWidth > 16 || AGA_FM3()) {
#endif
            /* Wide sprite: update occlusion buckets and go to general stack */
            /* `>> 4` then `<< 1` on a negative x: the shift left is UB (include/math68k.h). */
            int16_t leftBucket = M68kShiftLeft16((int16_t)(d1 >> 4), 1);
            int16_t rightBucket = M68kShiftLeft16((int16_t)((d1 + a0->pixWidth - 1) >> 4), 1);
#ifdef SWOS_SDL_BUILD
            /* Clamp the column span to the occlusion array. a3 = occlBuckets_combined+5,
             * so the valid a3 index is [-5, OCCL_ACTIVE_BUCKETS-1]; a sprite spanning past
             * the (widescreen) right edge or far off the left edge must not write past it. */
            if (leftBucket  < -10)                            leftBucket  = -10;
            if (rightBucket > 2 * (OCCL_ACTIVE_BUCKETS - 1))  rightBucket = 2 * (OCCL_ACTIVE_BUCKETS - 1);
#endif
            int16_t d3 = leftBucket;
            while (d3 <= rightBucket) {
                if (d2 < a3[d3 >> 1])
                    a3[d3 >> 1] = d2;
                d3 += 2;
            }

            a1->field_56 = 0;

            /* Push to general render stack — pointer slots are
             * sizeof(void *) wide (4 B on 32-bit, 8 B on 64-bit). */
            *(Sprite **)a6loc = a1;          a6loc += sizeof(Sprite *);
            *(SpriteGraphics **)a6loc = a0;  a6loc += sizeof(SpriteGraphics *);
            *(int16_t *)a6loc = d1;          a6loc += 2;
            *(int16_t *)a6loc = d2;          a6loc += 2;
        } else {
            /* Narrow sprite (<=16px): try to assign to a hardware band */
            int16_t leftBucket = (d1 >> 4) << 1;
            int16_t rightBucket = ((d1 + a0->pixWidth - 1) >> 4) << 1;

            /* Check if sprite top is above screen (can't use bands) */
            if (d2 < 0) {
                goto enqueue_general;
            }

            /* Check bottom edge against occlusion buckets */
            int16_t d3 = d2 + a0->nlines;
            if (d3 > a3[leftBucket >> 1] || d3 > a3[rightBucket >> 1]) {
                goto enqueue_general;
            }

#if !defined(AGA)
            /* Try plane23 band first (top-most).
             *
             * AGA-ONLY EXCLUSION: plane23 is drawn by spr2+3 (RenderSpriteBandsToCopperLists
             * blits it into bitPlane2/3 = SPR2/SPR3PT). Under AGA 8-plane lores the pitch
             * bitplane fetch (DDFSTRT=$20, fetch@0x24) steals spr3's DMA slot (0x25/0x27) —
             * proven cycle-exact (dma_profile.py) and unavoidable at 8 planes (see docs/AGA.md
             * sec 11c). An attached spr2+3 sprite with spr3 dead loses planes 2/3 and renders
             * white/black. Only spr0+1 (plane01) survives the steal, so on AGA we DON'T use the
             * plane23 band: sprites that would land here fall through to plane01 (steal-safe) or
             * the general/bob stack, which renders correctly. OCS (4-plane) has no steal — it
             * keeps both bands. */
            if (d3 < plane23BandTopY_local) {
                a1->field_56 = 2;
                plane23BandTopY_local = d2;
                *(Sprite **)a5 = a1;             a5 += sizeof(Sprite *);
                *(SpriteGraphics **)a5 = a0;     a5 += sizeof(SpriteGraphics *);
                *(int16_t *)a5 = d1;             a5 += 2;
                *(int16_t *)a5 = d2;             a5 += 2;
                goto post_enqueue;
            }
#endif

            /* Try plane01 band (the steal-safe spr0+1 pair) */
            if (d3 < plane01BandTopY_local) {
                a1->field_56 = 1;
                plane01BandTopY_local = d2;
                *(Sprite **)a4 = a1;             a4 += sizeof(Sprite *);
                *(SpriteGraphics **)a4 = a0;     a4 += sizeof(SpriteGraphics *);
                *(int16_t *)a4 = d1;             a4 += 2;
                *(int16_t *)a4 = d2;             a4 += 2;
                goto post_enqueue;
            }

            /* Falls through to general stack */
enqueue_general:
            a1->field_56 = 0;
            *(Sprite **)a6loc = a1;          a6loc += sizeof(Sprite *);
            *(SpriteGraphics **)a6loc = a0;  a6loc += sizeof(SpriteGraphics *);
            *(int16_t *)a6loc = d1;          a6loc += 2;
            *(int16_t *)a6loc = d2;          a6loc += 2;

post_enqueue:
            /* Update occlusion bucket minimums */
            if (d2 < a3[leftBucket >> 1])
                a3[leftBucket >> 1] = d2;
            if (d2 < a3[rightBucket >> 1])
                a3[rightBucket >> 1] = d2;
        }

        /* onScreen was already decided by the SIMULATION cull above, and this
         * used to overwrite it with 1 for anything that reached the enqueue.
         * That was harmless while the two culls were the same test, and wrong
         * the moment they are not: a sprite outside the simulation's window
         * but inside the render view gets drawn (correct) and must still read
         * as off-screen to the game (which this line was undoing). */
#if defined(DETERMINISTIC_MODE) && DETERMINISTIC_SPRITE_LIMIT > 0
        spritesEnqueued++;
#endif
        d7--;
        if (d7 < 0) break;
        continue;
    }

    /* Commit stack pointers */
    _plane01RenderStackPtr = a4;
    _plane23RenderStackPtr = a5;
    _renderStackPtr = a6loc;
}

/* ================================================================
 * DrawSprites  (SWOS2.s line 7322)
 *
 * Top-level sprite rendering: sort -> build render stacks ->
 * draw queued (blitter) sprites -> render bands to copper lists.
 * ================================================================ */
__attribute__((used, externally_visible))
void DrawSprites(void)
{
#ifdef SWOS_SDL_BUILD
    { extern void Platform_HudLayerReset(void); Platform_HudLayerReset(); }
#endif
    SortSprites();
    BuildSpriteBandAndGeneralRenderStacks();
    DrawQueuedSprites();
    RenderSpriteBandsToCopperLists();
}
