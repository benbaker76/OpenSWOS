/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "text_render.h"
#include "display.h"
#ifdef SWOS_SDL_BUILD
#include <stdint.h>
#include <stddef.h>
#endif

/* SWOS_Font glyph: body in plane 1 (colour 2), drop shadow in plane 3 (colour 8).
 * Fixed at 3 on BOTH depths — the glyph is always a 4bpp/16-colour font, so the
 * shadow lives in plane 3 whether the playfield is 4 (OCS) or 8 (AGA) planes. The
 * AGA glyph blit must target dest plane 3 for the shadow, NOT the last dest plane
 * (which is plane 3 on OCS but plane 7 = a wrong colour on AGA). */
#ifndef GLYPH_SHADOW_PLANE
#define GLYPH_SHADOW_PLANE 3
#endif

/* ---- External globals (all live in SWOS2.s) ---- */
extern uint8_t *_spriteGraphicsPtr;
extern volatile uintptr_t _frontbufferBasePtr;
extern uint16_t _cameraXMod320;
extern uint16_t _cameraYMod320;
extern volatile uint16_t _panicReasonCode;

extern void PanicHaltLoop(void) __attribute__((noreturn));


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
 * blitStringToFrontBuffer  (static — only called from DrawMenuText)
 *
 * Renders a single glyph character from a sprite font to the
 * frontbuffer using the Amiga blitter.  4-pass operation (one per
 * bitplane).  Handles vertical screen wrap.
 *
 * d0=sprIdx  d1=x  d2=y  d3=planeMask
 * ================================================================ */
#ifdef SWOS_SDL_BUILD
/* SDL build: render a glyph from chunky 8bpp sprite data to the chunky
 * 8bpp frontbuffer. Sprite data layout (built by `blitDispatch`):
 *   - 1 byte per pixel = palette index (0..NUM_COLOURS-1)
 *   - row stride = wquads * 16 bytes
 *   - palette index 0 = transparent
 *
 * Body/shadow remap: the 4bpp SWOS_Font glyph encodes the body in palette index
 * bit 1 (= colour 2) and the drop shadow in bit GLYPH_SHADOW_PLANE=3 (= colour 8),
 * FIXED regardless of the display bit depth. The `color` parameter overrides the
 * body colour; the shadow keeps the fixed menu shadow colour 8:
 *   bodyBit   = (src >> 1) & 1        -> out = color
 *   shadowBit = (src >> 3) & 1        -> out = 8   (shadow-only pixels)
 * (Using NUM_PLANES-1 here silently dropped the shadow on the 8-plane build --
 * NUM_PLANES-1 is 7 there, and the 4bpp glyph never sets bit 7.)
 *
 * Pixels with both bodyBit and shadowBit clear are transparent. */
static void blitGlyphBitplanes(uint16_t sprIdx, int16_t dstX, int16_t dstY,
                               uint16_t color)
{
    SpriteGraphics **sprGfxTable = (SpriteGraphics **)_spriteGraphicsPtr;
    SpriteGraphics *spr = sprGfxTable[sprIdx];
    if (!spr) return;

    const uint8_t *data = (const uint8_t *)(uintptr_t)spr->sprData;
    uint8_t *dstBase = (uint8_t *)(uintptr_t)_frontbufferBasePtr + _menu_draw_x_offset;
    if (!data || !dstBase) return;

    int16_t glyphW = spr->pixWidth;
    int16_t glyphH = spr->nlines;
    int16_t pitch  = spr->wquads * 16;       /* chunky row stride */

    for (int16_t py = 0; py < glyphH; py++) {
        int16_t dy = dstY + py;
        if (dy < 0 || dy >= SCREEN_HEIGHT) continue;

        const uint8_t *srcRow = data + (size_t)py * pitch;
        uint8_t       *dstRow = dstBase + (size_t)dy * FRONTBUFFER_ROW_STRIDE_BYTES;

        for (int16_t px = 0; px < glyphW; px++) {
            int16_t dx = dstX + px;
            if (dx < 0 || dx >= _screen_width) continue;

            uint8_t src = srcRow[px];
            uint8_t bodyBit   = (uint8_t)((src >> 1) & 1u);                  /* glyph body  = colour 2 */
            uint8_t shadowBit = (uint8_t)((src >> GLYPH_SHADOW_PLANE) & 1u); /* glyph shadow = colour 8 */
            if (bodyBit == 0 && shadowBit == 0) continue;

            /* Body pixels take the requested colour; shadow-only pixels take the
             * fixed menu shadow colour (8). Uses the charset's FIXED shadow plane
             * (bit 3), not NUM_PLANES-1, so the drop shadow survives on the 8-plane
             * (AGA/SDL) build. Identical to the old per-plane remap on 4-plane OCS. */
            uint8_t out = bodyBit ? (uint8_t)color
                                  : (uint8_t)(1u << GLYPH_SHADOW_PLANE);
            if (out) dstRow[dx] = out;
        }
    }
}

#endif

#ifdef SWOS_SDL_BUILD
static void blitStringToFrontBuffer(uint16_t sprIdx, int16_t x, int16_t y, uint16_t planeMask)
{
    /* planeMask is the color value here (DrawMenuText passes `color`).
     *
     * Do NOT apply cameraXMod320/cameraYMod320 on SDL. Those are Amiga
     * hardware-scroll wrap offsets used when the chip display pans its
     * bitplane base pointer — correct on Amiga but wrong for our chunky
     * frontbuffer that we redraw top-down each frame (same reasoning as
     * blitChunkySprite in sprites.c). When the in-game camera leaves
     * those offsets non-zero and we land in a menu, the text Y would
     * wrap through SCREEN_HEIGHT and multiple glyphs land on overlapping
     * rows — producing "SSEEIECTS MORE TEAMS" in place of
     * "SELECT 2 MORE TEAMS" on the post-match team-select screen. */
    blitGlyphBitplanes(sprIdx, x, y, planeMask);
}
#else
static void blitStringToFrontBuffer(uint16_t sprIdx, int16_t x, int16_t y, uint16_t planeMask)
{
    SpriteGraphics **sprGfxTable = (SpriteGraphics **)_spriteGraphicsPtr;

    /* Look up glyph sprite header */
    SpriteGraphics *spr = sprGfxTable[sprIdx];

    /* Glyph must have mask (type == 0) */
    if (spr->type != 0) {
        _panicReasonCode = 0x1556;
        PanicHaltLoop();
    }

    uint8_t *dstBase = (uint8_t *)_frontbufferBasePtr + _menu_draw_x_offset;
    uint16_t glyphHeight = (uint16_t)spr->nlines;

    /* Apply camera offset */
    x += (int16_t)_cameraXMod320;
    y += (int16_t)_cameraYMod320;
#if !defined(AGA)
    if (y >= SCREEN_HEIGHT)
        y -= SCREEN_HEIGHT;
#endif

    /* Wait for any previous blit */
    waitBlit();

    /* Compute horizontal shift and edge masks */
    uint16_t bitShift = (uint16_t)x & 0xF;
    uint16_t glyphPixMod = (uint16_t)spr->pixWidth & 0xF;
    if (glyphPixMod == 0)
        glyphPixMod = 16;
    uint16_t totalBits = bitShift + glyphPixMod;

    uint16_t blitWords = (uint16_t)spr->wquads + 1;
    uint16_t lastWordMask = 0;

    if (totalBits <= 16) {
        lastWordMask = 0xFFFF;
        blitWords--;
    }

    _custom->bltalwm = lastWordMask;
    _custom->bltafwm = 0xFFFF;

    /* BLTCON1: shift amount in bits 12-15 */
    uint16_t shiftVal = bitShift << 12;
    _custom->bltcon1 = shiftVal;

    /* Glyph must have mask pointer */
    if (spr->maskPtr == 0) {
        _panicReasonCode = 0x1556;
        PanicHaltLoop();
    }

    /* Compute vertical wrap */
    uint16_t wrapLines = 0;
    uint16_t firstHeight = glyphHeight;
#if defined(AGA)
    /* AGA: no wrap. The display reads rows [cameraYMod320, +SCREEN_HEIGHT) of the
     * 2x-tall buffer straight down, and bobs (the bars text stands on) are drawn the
     * same way. Wrapped to the top like OCS, text below the wrap row went into rows
     * that were not on screen -- the stats' last lines missing -- and stayed there,
     * outside every undraw, to scroll into view later as a stray row of dashes. */
    if ((uint16_t)y >= 2u * SCREEN_HEIGHT)
        return;
    if ((uint16_t)y + glyphHeight > 2u * SCREEN_HEIGHT)
        firstHeight = (uint16_t)(2u * SCREEN_HEIGHT - (uint16_t)y);
#else
    if ((uint16_t)y + glyphHeight > SCREEN_HEIGHT) {
        wrapLines = (uint16_t)y + glyphHeight - SCREEN_HEIGHT;
        firstHeight = glyphHeight - wrapLines;
    }
#endif

    uint16_t savedFirstHeight = firstHeight;

    /* Compute destination pointer */
    int16_t wordOffset = ((int16_t)x >> 4) << 1;
    int32_t dstXByteOffset = (int32_t)(int16_t)wordOffset;
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
    /* In a match the AGA plane row starts with the 4-byte verge margin, which the
     * display and the bobs skip: text drawn over the pitch (MATCH STATS) without it
     * stood 32px left of the bars it belongs in. */
    {
        extern volatile uint16_t _playGame;
        if (_playGame)
            dstXByteOffset += SCROLL_LEFT_MARGIN_BYTES;
    }
#endif
    uint8_t *destPtr = dstBase + (uint16_t)y * (uint32_t)ROW_STRIDE_BYTES + dstXByteOffset;

    /* BLTSIZE = height << 6 | blitWords */
    uint16_t bltsize = (firstHeight << 6) | blitWords;

    /* Compute modulos */
    uint16_t blitWidthBytes = blitWords << 1;
    uint16_t srcWquads = (uint16_t)spr->wquads;
    uint16_t srcStrideBytes = srcWquads << 1;
    uint16_t srcInterleavedBytes = srcStrideBytes << SPRITE_NUM_PLANES_SHIFT;

    _custom->bltamod = srcStrideBytes - blitWidthBytes;
    _custom->bltbmod = srcInterleavedBytes - blitWidthBytes;

    /* Per-plane dest modulo: this cookie-cut runs one plane at a time
     * (dst += PLANE_STRIDE after each), so consecutive scanlines of the SAME
     * plane are a full interleaved row (ROW_STRIDE_BYTES) apart. After writing
     * blitWidthBytes the blitter must skip ROW_STRIDE_BYTES - blitWidthBytes.
     * The old form (PLANE_STRIDE - blitWidthBytes + PITCH_DEST_MOD_UNALIGNED)
     * only equalled this while PLANE_STRIDE == SCROLL_ROW_BYTES_UNALIGNED (both
     * 44); the AGA FMODE widen to PLANE_STRIDE 52 left it 8 bytes too high, so
     * every glyph sheared down-right into horizontal streaks across the menu.
     * See docs/AGA.md §11 (coincidental-equality trap). */
    uint16_t dstMod = ROW_STRIDE_BYTES - blitWidthBytes;
    _custom->bltcmod = dstMod;
    _custom->bltdmod = dstMod;

    /* Source data pointers */
    uint32_t maskPtr   = (uint32_t)spr->maskPtr;
    uint32_t dataBase  = (uint32_t)spr->sprData;
    uint32_t plane1Ptr = dataBase + srcStrideBytes;          /* skip plane 0 */
    uint32_t planeLastPtr = plane1Ptr + (uint32_t)srcStrideBytes * (SPRITE_NUM_PLANES - 2);  /* glyph plane 3 = shadow */

restart_blit_span:
    ;
    uint16_t pMask = planeMask;
    uint8_t *dst = destPtr;

    /* One pass per DEST/playfield plane (NUM_PLANES). Plane GLYPH_SHADOW_PLANE(3) is
     * the SHADOW plane (colour 8) with the asymmetric logic; every other plane is a
     * BODY plane. Previously this was structured as "planes 0..N-2 body, final plane =
     * shadow", which put the shadow on the last dest plane — plane 3 on OCS (correct)
     * but plane 7 (a wrong colour) on the 8-plane AGA playfield. Keying the shadow to
     * plane 3 explicitly fixes the AGA drop-shadow colour while staying identical on OCS. */
    for (int16_t p = 0; p < NUM_PLANES; p++) {
        uint16_t bit = pMask & 1;
        pMask >>= 1;

        if (p == GLYPH_SHADOW_PLANE) {
            /* Shadow plane. If the requested colour also uses plane 3 → OR the mask
             * (body sets it too); else cookie-cut the glyph's shadow plane so
             * shadow-only pixels set plane 3 (= colour 8). */
            if (bit) {
                _custom->bltcon0 = shiftVal | BLTCON0_OR_MASK;
                _custom->bltapt = maskPtr;
                _custom->bltcpt = (uint32_t)dst;
                _custom->bltdpt = (uint32_t)dst;
            } else {
                _custom->bltcon0 = shiftVal | BLTCON0_BASE_LINKED;
                _custom->bltapt = maskPtr;
                _custom->bltbpt = planeLastPtr;   /* glyph shadow plane (plane 3) */
                _custom->bltcpt = (uint32_t)dst;
                _custom->bltdpt = (uint32_t)dst;
            }
        } else {
            /* Body plane: cookie-cut the glyph body where the colour bit is set,
             * else clear the footprint (transparent in that plane). */
            if (bit) {
                _custom->bltcon0 = shiftVal | BLTCON0_BASE_LINKED;
                _custom->bltapt = maskPtr;
                _custom->bltbpt = plane1Ptr;      /* glyph body plane (plane 1) */
                _custom->bltcpt = (uint32_t)dst;
                _custom->bltdpt = (uint32_t)dst;
            } else {
                _custom->bltcon0 = shiftVal | BLTCON0_CLEAR_MASK;
                _custom->bltapt = maskPtr;
                _custom->bltcpt = (uint32_t)dst;
                _custom->bltdpt = (uint32_t)dst;
            }
        }

        /* Kick blit and wait */
        _custom->bltsize = bltsize;
        waitBlit();

        dst += PLANE_STRIDE;
    }

    /* Handle vertical wrap */
    if (wrapLines != 0) {
        /* Advance source pointers past the first span */
        uint32_t maskAdvance = (uint32_t)savedFirstHeight * srcWquads * 2;
        maskPtr += maskAdvance;
        uint32_t dataAdvance = maskAdvance << NUM_PLANES_SHIFT;
        planeLastPtr += dataAdvance;
        plane1Ptr += dataAdvance;

        /* Reset dest to top of screen at same x offset */
        destPtr = dstBase + dstXByteOffset;

        /* New BLTSIZE for wrapped portion */
        bltsize = (wrapLines << 6) | (blitWidthBytes >> 1);
        wrapLines = 0;

        /* Wait for the plane 3 blit to finish before restarting */
        waitBlit();
        goto restart_blit_span;
    }
}
#endif /* !SWOS_SDL_BUILD */

/* ================================================================
 * GetTextWidth
 *
 * Measures the pixel width of a text string using a CharTable.
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t GetTextWidth(uint8_t *text, uint8_t *charsTable)
{
    CharsTableHeader *ct = (CharsTableHeader *)charsTable;
    uint16_t sprIdxOff = (uint16_t)ct->spriteIndexOffset;
    uint16_t charSpacing = (uint16_t)ct->charSpacing;
    uint16_t *convTable = (uint16_t *)(ct + 1);
    uint16_t spaceWidth = (uint16_t)ct->spaceWidth;
    SpriteGraphics **sprGfxTable = (SpriteGraphics **)_spriteGraphicsPtr;

    uint16_t width = 0;
    uint8_t ch;

    /* TEMP diagnostic — narrow to the player-name path: smallCharsTable
     * (sprIdxOff=0), text starting with a digit, and only print the
     * first 8 unique strings. The match-init makePlayerNameSprite
     * calls produce bogus widths even though identical lookups via
     * the frontend menus return sane values, which suggests sprite
     * slots that hold glyphs get reloaded between phases. */
    while ((ch = *text++) != 0) {
        if (ch == ' ' || ch == '_') {     /* ('_' a space: a typed name's, input_text.c) */
            width += spaceWidth;
        } else {
            uint16_t idx = (uint16_t)convTable[(uint8_t)(ch - ' ')];
            idx += sprIdxOff;
            SpriteGraphics *sprHeader = sprGfxTable[idx];
            uint16_t pixWidth = (uint16_t)sprHeader->pixWidth;
            width += pixWidth + charSpacing;
        }
    }

    return width;
}

/* ================================================================
 * DrawMenuText
 *
 * Renders a text string to the frontbuffer character-by-character.
 * Returns the final x position (next character would start here).
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t DrawMenuText(uint16_t x, uint16_t y, uint16_t color,
                       uint8_t *text, uint8_t *charsTable)
{
    CharsTableHeader *ct = (CharsTableHeader *)charsTable;
    uint16_t sprIdxOff = (uint16_t)ct->spriteIndexOffset;
    uint16_t charSpacing = (uint16_t)ct->charSpacing;
    uint16_t *convTable = (uint16_t *)(ct + 1);
    uint16_t spaceWidth = (uint16_t)ct->spaceWidth;
    SpriteGraphics **sprGfxTable = (SpriteGraphics **)_spriteGraphicsPtr;

    int16_t curX = (int16_t)x;
    int16_t nextX = curX;
    uint8_t ch;

    while ((ch = *text++) != 0) {
        if (ch == ' ' || ch == '_') {     /* ('_' a space: a typed name's, input_text.c) */
            nextX = curX + (int16_t)spaceWidth;
            if (curX < 0) {
                curX = nextX;
                continue;
            }
            /* The live width, not SCREEN_WIDTH: menus are centred in whatever the
             * render width is (362 at 4:3 here), so a right-aligned entry ends past
             * 336 and lost its last glyph ("CONNECTE"). On the Amiga the two are the
             * same number (display.h). */
            if ((uint16_t)nextX >= _screen_width)
                break;
            curX = nextX;
            continue;
        }

        /* Convert character to sprite index */
        uint16_t idx = (uint16_t)convTable[(uint8_t)(ch - ' ')];
        idx += sprIdxOff;
        uint16_t sprIdx = idx;

        SpriteGraphics *sprHeader = sprGfxTable[idx];
        uint16_t pixWidth = (uint16_t)sprHeader->pixWidth;
        nextX = curX + (int16_t)pixWidth + (int16_t)charSpacing;

        if (curX < 0) {
            curX = nextX;
            continue;
        }
        if ((uint16_t)nextX >= _screen_width)
            break;

        blitStringToFrontBuffer(sprIdx, curX, (int16_t)y, color);
        curX = nextX;
    }

    return (uint16_t)nextX;
}

/* ================================================================
 * DrawMenuTextRightAligned
 *
 * Measures text width, then draws right-aligned at x.
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t DrawMenuTextRightAligned(uint16_t x, uint16_t y, uint16_t color,
                                   uint8_t *text, uint8_t *charsTable)
{
    uint16_t textWidth = GetTextWidth(text, charsTable);
    uint16_t startX = x - textWidth + 1;
    return DrawMenuText(startX, y, color, text, charsTable);
}

/* ================================================================
 * DrawMenuTextCentered
 *
 * Measures text width, then draws centered at x.
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t DrawMenuTextCentered(uint16_t x, uint16_t y, uint16_t color,
                               uint8_t *text, uint8_t *charsTable)
{
    uint16_t textWidth = GetTextWidth(text, charsTable);
    uint16_t halfWidth = (textWidth + 1) >> 1;
    uint16_t startX = x - halfWidth;
    return DrawMenuText(startX, y, color, text, charsTable);
}
