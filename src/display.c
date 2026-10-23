/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "display.h"
#include "game_types.h"
#include "swos_types.h"
#include <string.h>

/* ---- Blitter register write macros ----
 * GCC m68k 15.1.0 decomposes `volatile uint16_t = N` struct-member
 * stores into byte-pair `move.b` instructions in some contexts. For
 * hardware-trigger registers (writing `bltsize` starts the blitter),
 * the decomposed write triggers the blit mid-update with a half-formed
 * size — fill/copy operations either do nothing or write garbage.
 * Same macros as in blitter.c / sprites.c / scroll_blit.c.  */
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

/* Custom chip base pointer — set once, used by all HW register access */
volatile struct Custom *_custom = (volatile struct Custom *)0xDFF000;

/* ---- External globals (all live in SWOS2.s) ---- */

/* Frontbuffer pointers */
extern uint8_t _frontbufferBase[];
extern uint8_t _frontbufferBaseAlt[];
extern uint8_t _frontbufferMaskBuffer[];
extern volatile uintptr_t _frontbufferBasePtr;
extern volatile uintptr_t _frontbufferBaseAltPtr;

/* Hardware sprite DMA data -- named bitplanes in SWOS2.s (BSSInChip, 0x384 bytes each) */
extern uint8_t _bitPlane0Base[];
extern uint8_t _bitPlane1Base[];
extern uint8_t _bitPlane2Base[];
extern uint8_t _bitPlane3Base[];
#ifdef AGA
extern uint8_t _emptySpriteDma[];   /* sprites 4-7, never drawn (swos_globals.c) */
#endif
extern uint8_t _bitPlane0BaseAlt[];
extern uint8_t _bitPlane1BaseAlt[];
extern uint8_t _bitPlane2BaseAlt[];
extern uint8_t _bitPlane3BaseAlt[];

/* Bitplane base pointers */
extern volatile uint32_t _bitPlane0BasePtr;
extern volatile uint32_t _bitPlane1BasePtr;
extern volatile uint32_t _bitPlane2BasePtr;
extern volatile uint32_t _bitPlane3BasePtr;
#ifdef AGA
extern volatile uint32_t bitPlane4BasePtr;
extern volatile uint32_t bitPlane5BasePtr;
extern volatile uint32_t bitPlane6BasePtr;
extern volatile uint32_t bitPlane7BasePtr;
#endif

/* Copper list */
extern uint8_t _copperListBuffer[];
extern uint32_t _copperZeroLong;

/* Sprite blit queue sentinels (4 bytes each: 2-byte sentinel + padding/queue) */
extern uint32_t _kSpriteBlitQueueSentinel_A;
extern uint32_t _kSpriteBlitQueueSentinel_B;
extern volatile uintptr_t _spriteBlitQueue;

/* Camera state */
extern FixedPoint _cameraX;          /* dc.l but word-accessed (big-endian high word) */
extern FixedPoint _cameraY;          /* dc.l but word-accessed (big-endian high word) */
extern uint16_t _cameraXMod320;
extern uint16_t _cameraYMod320;
extern int16_t _oldCameraX;
extern int16_t _oldCameraY;

/* Double-buffer saved camera state (slot 0) */
extern uint16_t _dstXByteOffsetSaved0;
extern uint16_t _dstYStartLinesSaved0;
extern int16_t _oldCameraXSaved0;
extern int16_t _oldCameraYSaved0;
extern int16_t _cameraXSaved0;
extern int16_t _cameraYSaved0;

/* Double-buffer saved camera state (slot 1) */
extern uint16_t _dstXByteOffsetSaved1;
extern uint16_t _dstYStartLinesSaved1;
extern int16_t _oldCameraXSaved1;
extern int16_t _oldCameraYSaved1;
extern int16_t _cameraXSaved1;
extern int16_t _cameraYSaved1;

/* Display flags */
extern uint16_t _usePlaneOffsetFlag;
extern uint16_t _useAltBaseFlag;
extern uint16_t _paletteIndex;

/* Sprite graphics table pointer */
extern uint8_t *_spriteGraphicsPtr;

/* ================================================================
 * Helper: write a 32-bit pointer into a copper list entry.
 * Each copper move pair is 8 bytes: [reg_hi][val_hi][reg_lo][val_lo]
 * The high 16 bits of the pointer go at offset +2, low 16 bits at +6.
 *
 * Uses inline asm to guarantee word-level (move.w) writes.
 * GCC's volatile uint16_t store decomposes into byte-level move.b
 * pairs, which is incorrect for copper list entries that may be
 * read by DMA mid-update.
 * ================================================================ */
static void copper_write_ptr(uint8_t *entry, uint32_t ptr)
{
    uint16_t hi = (uint16_t)(ptr >> 16);
    uint16_t lo = (uint16_t)(ptr);
#if defined(__m68k__) || defined(__amigaos__) || defined(__amigaos4__)
    /* Amiga: force real move.w instructions — GCC's volatile uint16_t store
     * can decompose into byte-level move.b pairs which corrupt the live
     * copper list. */
    __asm__ volatile ("move.w %0,2(%1)" : : "d"(hi), "a"(entry) : "memory");
    __asm__ volatile ("move.w %0,6(%1)" : : "d"(lo), "a"(entry) : "memory");
#else
    /* SDL host: copper list is just a buffer in main RAM, plain stores fine. */
    *(volatile uint16_t *)(entry + 2) = hi;
    *(volatile uint16_t *)(entry + 6) = lo;
#endif
}

/* Helper: write a 16-bit word to a copper list buffer address.
 * Forces a move.w instruction on Amiga; plain store on SDL host. */
static void copper_write_word(uint8_t *addr, uint16_t val)
{
#if defined(__m68k__) || defined(__amigaos__) || defined(__amigaos4__)
    __asm__ volatile ("move.w %0,(%1)" : : "d"(val), "a"(addr) : "memory");
#else
    *(volatile uint16_t *)addr = val;
#endif
}

/* -DRASTER_BARS (include/raster_bars.h) paints COLOR00 per phase of the frame. COLOR00 is
 * also the grass, so the bands covered the pitch; they belong in the border strip at the
 * right of the display window, which only COLOR00 colours. So that build XORs every
 * bitplane pixel's colour index with 0x80 (BPLCON4 BPLAM) and loads the palette with its
 * halves swapped -- each group of 32 colours below goes to the bank 4 away -- so the
 * picture is unchanged and a pixel of colour 0 shows entry 128, which holds colour 0. The
 * sprites (no BPLAM) read their 16 colours from 128-143 (ESPRM/OSPRM 8), where 0-15 now
 * are. The border is not a bitplane pixel: it alone shows entry 0. */
#if defined(RASTER_BARS)
#define RB_BANK(sel)  ((uint16_t)((sel) ^ 0x8000u))
#define RB_BPLCON4    0x8088
#else
#define RB_BANK(sel)  (sel)
#define RB_BPLCON4    0x0000
#endif

/* ================================================================
 * Copper list template — const data, copied to _copperListBuffer at init.
 * OCS: 160 words (320 bytes), AGA: 658 words (1316 bytes).
 * ================================================================ */
static const uint16_t copperListTemplate[COPPERLIST_SIZE_BYTES / 2] = {
#ifdef AGA
    /* ===== AGA copper list template (8 planes, 256 colors) ===== */
    0x1401, 0xFFFE,                     /* Wait vpos >= 0x14 */
    /* 8 sprite pointer pairs */
    0x0120, 0x0000, 0x0122, 0x0000,     /* SPR0 */
    0x0124, 0x0000, 0x0126, 0x0000,     /* SPR1 */
    0x0128, 0x0000, 0x012A, 0x0000,     /* SPR2 */
    0x012C, 0x0000, 0x012E, 0x0000,     /* SPR3 */
    0x0130, 0x0000, 0x0132, 0x0000,     /* SPR4 */
    0x0134, 0x0000, 0x0136, 0x0000,     /* SPR5 */
    0x0138, 0x0000, 0x013A, 0x0000,     /* SPR6 */
    0x013C, 0x0000, 0x013E, 0x0000,     /* SPR7 */
    0x0100, 0x0210,                     /* BPLCON0: 8 bitplanes (BPU3=1) + COLOR */
    0x0102, 0x0000,                     /* BPLCON1 */
    0x0104, 0x0024,                     /* BPLCON2 */
    0x01FC, 0x0001,                     /* FMODE = BPL32 (32-bit bitplane fetch, 8bpl lores) */
    0x010C, RB_BPLCON4,                     /* BPLCON4: ESPRM=OSPRM=0 -> HW sprites index palette 0-15 (kit colours) instead of the reset-default base-16 (colours 16-31) */
    0x0108, 0x0000,                     /* BPL1MOD */
    0x010A, 0x0000,                     /* BPL2MOD */
    0x0092, 0x0020,                     /* DDFSTRT (FMODE=1: 12 longs = full 384px plane row) */
    0x0094, 0x00D0,                     /* DDFSTOP */
    0x008E, 0x2471,                     /* DIWSTRT */
    0x0090, 0x34C1,                     /* DIWSTOP */
    /* Bank 0 (colors 0..31) */
    0x0106, RB_BANK(0x0C00),                     /* BPLCON3: bank 0 */
    0x0180, 0, 0x0182, 0, 0x0184, 0, 0x0186, 0,
    0x0188, 0, 0x018A, 0, 0x018C, 0, 0x018E, 0,
    0x0190, 0, 0x0192, 0, 0x0194, 0, 0x0196, 0,
    0x0198, 0, 0x019A, 0, 0x019C, 0, 0x019E, 0,
    0x01A0, 0, 0x01A2, 0, 0x01A4, 0, 0x01A6, 0,
    0x01A8, 0, 0x01AA, 0, 0x01AC, 0, 0x01AE, 0,
    0x01B0, 0, 0x01B2, 0, 0x01B4, 0, 0x01B6, 0,
    0x01B8, 0, 0x01BA, 0, 0x01BC, 0, 0x01BE, 0,
    /* Bank 1 (colors 32..63) */
    0x0106, RB_BANK(0x2C00),
    0x0180, 0, 0x0182, 0, 0x0184, 0, 0x0186, 0,
    0x0188, 0, 0x018A, 0, 0x018C, 0, 0x018E, 0,
    0x0190, 0, 0x0192, 0, 0x0194, 0, 0x0196, 0,
    0x0198, 0, 0x019A, 0, 0x019C, 0, 0x019E, 0,
    0x01A0, 0, 0x01A2, 0, 0x01A4, 0, 0x01A6, 0,
    0x01A8, 0, 0x01AA, 0, 0x01AC, 0, 0x01AE, 0,
    0x01B0, 0, 0x01B2, 0, 0x01B4, 0, 0x01B6, 0,
    0x01B8, 0, 0x01BA, 0, 0x01BC, 0, 0x01BE, 0,
    /* Bank 2 (colors 64..95) */
    0x0106, RB_BANK(0x4C00),
    0x0180, 0, 0x0182, 0, 0x0184, 0, 0x0186, 0,
    0x0188, 0, 0x018A, 0, 0x018C, 0, 0x018E, 0,
    0x0190, 0, 0x0192, 0, 0x0194, 0, 0x0196, 0,
    0x0198, 0, 0x019A, 0, 0x019C, 0, 0x019E, 0,
    0x01A0, 0, 0x01A2, 0, 0x01A4, 0, 0x01A6, 0,
    0x01A8, 0, 0x01AA, 0, 0x01AC, 0, 0x01AE, 0,
    0x01B0, 0, 0x01B2, 0, 0x01B4, 0, 0x01B6, 0,
    0x01B8, 0, 0x01BA, 0, 0x01BC, 0, 0x01BE, 0,
    /* Bank 3 (colors 96..127) */
    0x0106, RB_BANK(0x6C00),
    0x0180, 0, 0x0182, 0, 0x0184, 0, 0x0186, 0,
    0x0188, 0, 0x018A, 0, 0x018C, 0, 0x018E, 0,
    0x0190, 0, 0x0192, 0, 0x0194, 0, 0x0196, 0,
    0x0198, 0, 0x019A, 0, 0x019C, 0, 0x019E, 0,
    0x01A0, 0, 0x01A2, 0, 0x01A4, 0, 0x01A6, 0,
    0x01A8, 0, 0x01AA, 0, 0x01AC, 0, 0x01AE, 0,
    0x01B0, 0, 0x01B2, 0, 0x01B4, 0, 0x01B6, 0,
    0x01B8, 0, 0x01BA, 0, 0x01BC, 0, 0x01BE, 0,
    /* Bank 4 (colors 128..159) */
    0x0106, RB_BANK(0x8C00),
    0x0180, 0, 0x0182, 0, 0x0184, 0, 0x0186, 0,
    0x0188, 0, 0x018A, 0, 0x018C, 0, 0x018E, 0,
    0x0190, 0, 0x0192, 0, 0x0194, 0, 0x0196, 0,
    0x0198, 0, 0x019A, 0, 0x019C, 0, 0x019E, 0,
    0x01A0, 0, 0x01A2, 0, 0x01A4, 0, 0x01A6, 0,
    0x01A8, 0, 0x01AA, 0, 0x01AC, 0, 0x01AE, 0,
    0x01B0, 0, 0x01B2, 0, 0x01B4, 0, 0x01B6, 0,
    0x01B8, 0, 0x01BA, 0, 0x01BC, 0, 0x01BE, 0,
    /* Bank 5 (colors 160..191) */
    0x0106, RB_BANK(0xAC00),
    0x0180, 0, 0x0182, 0, 0x0184, 0, 0x0186, 0,
    0x0188, 0, 0x018A, 0, 0x018C, 0, 0x018E, 0,
    0x0190, 0, 0x0192, 0, 0x0194, 0, 0x0196, 0,
    0x0198, 0, 0x019A, 0, 0x019C, 0, 0x019E, 0,
    0x01A0, 0, 0x01A2, 0, 0x01A4, 0, 0x01A6, 0,
    0x01A8, 0, 0x01AA, 0, 0x01AC, 0, 0x01AE, 0,
    0x01B0, 0, 0x01B2, 0, 0x01B4, 0, 0x01B6, 0,
    0x01B8, 0, 0x01BA, 0, 0x01BC, 0, 0x01BE, 0,
    /* Bank 6 (colors 192..223) */
    0x0106, RB_BANK(0xCC00),
    0x0180, 0, 0x0182, 0, 0x0184, 0, 0x0186, 0,
    0x0188, 0, 0x018A, 0, 0x018C, 0, 0x018E, 0,
    0x0190, 0, 0x0192, 0, 0x0194, 0, 0x0196, 0,
    0x0198, 0, 0x019A, 0, 0x019C, 0, 0x019E, 0,
    0x01A0, 0, 0x01A2, 0, 0x01A4, 0, 0x01A6, 0,
    0x01A8, 0, 0x01AA, 0, 0x01AC, 0, 0x01AE, 0,
    0x01B0, 0, 0x01B2, 0, 0x01B4, 0, 0x01B6, 0,
    0x01B8, 0, 0x01BA, 0, 0x01BC, 0, 0x01BE, 0,
    /* Bank 7 (colors 224..255) */
    0x0106, RB_BANK(0xEC00),
    0x0180, 0, 0x0182, 0, 0x0184, 0, 0x0186, 0,
    0x0188, 0, 0x018A, 0, 0x018C, 0, 0x018E, 0,
    0x0190, 0, 0x0192, 0, 0x0194, 0, 0x0196, 0,
    0x0198, 0, 0x019A, 0, 0x019C, 0, 0x019E, 0,
    0x01A0, 0, 0x01A2, 0, 0x01A4, 0, 0x01A6, 0,
    0x01A8, 0, 0x01AA, 0, 0x01AC, 0, 0x01AE, 0,
    0x01B0, 0, 0x01B2, 0, 0x01B4, 0, 0x01B6, 0,
    0x01B8, 0, 0x01BA, 0, 0x01BC, 0, 0x01BE, 0,
    /* Reset to bank 0 */
    0x0106, 0x0C00,
    0x2301, 0xFFFE,                     /* Wait vpos >= 0x23 */
    /* BPL pointers block A (8 planes) */
    0x00E0, 0, 0x00E2, 0, 0x00E4, 0, 0x00E6, 0,
    0x00E8, 0, 0x00EA, 0, 0x00EC, 0, 0x00EE, 0,
    0x00F0, 0, 0x00F2, 0, 0x00F4, 0, 0x00F6, 0,
    0x00F8, 0, 0x00FA, 0, 0x00FC, 0, 0x00FE, 0,
    /* Wait / BPL pointers block B */
    0xFFE1, 0xFFFE,                     /* Wait vhpos */
    0x0001, 0xFFFE,                     /* Wait */
    /* BPL pointers block B (8 planes) */
    0x00E0, 0, 0x00E2, 0, 0x00E4, 0, 0x00E6, 0,
    0x00E8, 0, 0x00EA, 0, 0x00EC, 0, 0x00EE, 0,
    0x00F0, 0, 0x00F2, 0, 0x00F4, 0, 0x00F6, 0,
    0x00F8, 0, 0x00FA, 0, 0x00FC, 0, 0x00FE, 0,
    0xFFFF, 0xFFFE                      /* End of copper list */
#else
    /* ===== OCS copper list template (4 planes, 16 colors) ===== */
    0x1401, 0xFFFE, 0x0120, 0x0000, 0x0122, 0x0000, 0x0124, 0x0000,
    0x0126, 0x0000, 0x0128, 0x0000, 0x012A, 0x0000, 0x012C, 0x0000,
    0x012E, 0x0000, 0x0130, 0x0000, 0x0132, 0x0000, 0x0134, 0x0000,
    0x0136, 0x0000, 0x0138, 0x0000, 0x013A, 0x0000, 0x013C, 0x0000,
    0x013E, 0x0000, 0x0100, 0x4200, 0x0102, 0x0000, 0x0104, 0x0024,
    0x01FE, 0x0000, 0x01FE, 0x0000, 0x0108, 0x0000, 0x010A, 0x0000,
    0x0092, 0x0028, 0x0094, 0x00D0, 0x008E, 0x2471, 0x0090, 0x34C1,
    0x0180, 0x0000, 0x0182, 0x0000, 0x0184, 0x0000, 0x0186, 0x0000,
    0x0188, 0x0000, 0x018A, 0x0000, 0x018C, 0x0000, 0x018E, 0x0000,
    0x0190, 0x0000, 0x0192, 0x0000, 0x0194, 0x0000, 0x0196, 0x0000,
    0x0198, 0x0000, 0x019A, 0x0000, 0x019C, 0x0000, 0x019E, 0x0000,
    0x01A0, 0x0000, 0x01A2, 0x0000, 0x01A4, 0x0000, 0x01A6, 0x0000,
    0x01A8, 0x0000, 0x01AA, 0x0000, 0x01AC, 0x0000, 0x01AE, 0x0000,
    0x01B0, 0x0000, 0x01B2, 0x0000, 0x01B4, 0x0000, 0x01B6, 0x0000,
    0x01B8, 0x0000, 0x01BA, 0x0000, 0x01BC, 0x0000, 0x01BE, 0x0000,
    0x2401, 0xFFFE, 0x00E0, 0x0000, 0x00E2, 0x0000, 0x00E4, 0x0000,
    0x00E6, 0x0000, 0x00E8, 0x0000, 0x00EA, 0x0000, 0x00EC, 0x0000,
    0x00EE, 0x0000, 0xFFE1, 0xFFFE, 0x0001, 0xFFFE, 0x00E0, 0x0000,
    0x00E2, 0x0000, 0x00E4, 0x0000, 0x00E6, 0x0000, 0x00E8, 0x0000,
    0x00EA, 0x0000, 0x00EC, 0x0000, 0x00EE, 0x0000, 0xFFFF, 0xFFFE
#endif
};

/* ================================================================
 * ClearFrontbuffers
 *
 * Zero both bitplane frontbuffers before a match starts. They carry
 * leftover menu / intro bitplanes; on AGA the 16-colour pitch tiles never
 * write the high planes (4-7), so that stale high-plane data bleeds onto
 * the white pitch lines as rainbow/cyan garbage. The incremental
 * ScrollX/Y redraw only the scrolled delta, so the bulk of the buffer must
 * start clean. No-op on SDL (chunky pitch is fully redrawn each frame).
 * ================================================================ */
__attribute__((used, externally_visible))
void ClearFrontbuffers(void)
{
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
    memset(_frontbufferBase, 0, FRONTBUFFER_DISPLAY_ALLOC_BYTES);
    memset(_frontbufferBaseAlt, 0, FRONTBUFFER_DISPLAY_ALLOC_BYTES);
#endif
}

/* ================================================================
 * MirrorFrontbuffer (AGA block-B elimination)
 *
 * Copies the visible frontbuffer region [0, SCREEN_HEIGHT) into the mirror
 * half [SCREEN_HEIGHT, 2*SCREEN_HEIGHT) of the 2x-tall buffer, so the copper's
 * single block A can display all SCREEN_HEIGHT lines contiguously from any
 * cameraYMod320 (no mid-screen 8-pointer reload). Call once after a full
 * buffer fill; the incremental scroll blits keep the mirror maintained after
 * that (ScrollY double-writes; ScrollX mirror TODO). No-op on SDL/OCS.
 * ================================================================ */
__attribute__((used, externally_visible))
void MirrorFrontbuffer(void)
{
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
    /* The copy starts at the scroll's coarse byte, not at byte 0. A plane row is drawn
     * from that byte for a whole PLANE_STRIDE, so it runs on into the head of the next
     * plane's row -- and the last plane's into the head of the NEXT ROW. The first
     * bytes of the mirror half are therefore the tail of row SCREEN_HEIGHT-1's last
     * plane, which the fill has just written there; copying from byte 0 put the head
     * of row 0 (nobody's picture) over them: one row, one plane, wrong from pitch x 384
     * rightwards -- the dashed line that appeared after an instant replay and moved
     * with the pitch. Shifted, the copy takes whole rows as they are drawn. */
    uint32_t skip = ((uint32_t)(_cameraXMod320 >> 3)) & (uint32_t)(uint16_t)SCROLL_X_COARSE_ALIGN_MASK;
    uint32_t *src = (uint32_t *)(void *)(uintptr_t)(_frontbufferBasePtr + skip);
    uint32_t *dst = (uint32_t *)(void *)(uintptr_t)(_frontbufferBasePtr + skip + FB_MIRROR_OFFSET_BYTES);
    int32_t n = (SCREEN_HEIGHT * ROW_STRIDE_BYTES) / 4;
    while (n-- > 0)
        *dst++ = *src++;
#endif
}

/* ================================================================
 * InitDisplayAndCopper  (SWOS2.s line 5416)
 *
 * Sets up display: frontbuffer pointers, copper list, bitplanes,
 * DMA, blitter idle wait.
 * ================================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void InitDisplayAndCopper(void)
{
    int16_t i;

    /* Set frontbuffer base pointers */
    _frontbufferBasePtr = (uintptr_t)_frontbufferBase;
    _frontbufferBaseAltPtr = (uintptr_t)_frontbufferBaseAlt;

    /* Initialize sprite blit queue sentinels */
    _kSpriteBlitQueueSentinel_A = 0xFFFFFFFFu;
    _kSpriteBlitQueueSentinel_B = 0xFFFFFFFFu;

    /* Copy copper list template → buffer */
    {
        const uint8_t *src = (const uint8_t *)copperListTemplate;
        uint8_t *dst = _copperListBuffer;
        for (i = COPPERLIST_SIZE_BYTES - 1; i >= 0; i--)
        {
            *dst++ = *src++;
        }
    }

    /* Clear _copperZeroLong */
    _copperZeroLong = 0;

    /* Disable interrupts during vblank sync */
    _custom->intena = 0x4000u;

    /* Wait for beam position <= line 10 (top of frame) */
    {
        uint32_t vpos;
        do
        {
            vpos = *(volatile uint32_t *)&_custom->vposr;
            vpos = (vpos & 0x1FFFFu) >> 8;
        } while (vpos > 0x0Au);
    }

    /* Re-enable interrupts */
    _custom->intena = 0xC000u;

    /* Write _copperZeroLong pointer into first 8 sprite pointer entries */
    {
        uint8_t *entry = _copperListBuffer + COP_SPR_B0_OFF;
        uint32_t ptr = (uintptr_t)&_copperZeroLong;
        for (i = 7; i >= 0; i--)
        {
            copper_write_ptr(entry, ptr);
            entry += 8;
        }
    }

    /* Clear palette banks in copper list */
    {
#ifdef AGA
        /* AGA: clear all 8 banks x 32 colors */
        static const uint16_t bankOffsets[8] = {
            COP_PAL_B0_C0_VAL_OFF, COP_PAL_B1_C0_VAL_OFF,
            COP_PAL_B2_C0_VAL_OFF, COP_PAL_B3_C0_VAL_OFF,
            COP_PAL_B4_C0_VAL_OFF, COP_PAL_B5_C0_VAL_OFF,
            COP_PAL_B6_C0_VAL_OFF, COP_PAL_B7_C0_VAL_OFF
        };
        int16_t bank;
        for (bank = 0; bank < NUM_PAL_BANKS; bank++)
        {
            uint8_t *p = _copperListBuffer + bankOffsets[bank];
            for (i = COLORS_PER_BANK - 1; i >= 0; i--)
            {
                copper_write_word(p, 0);
                p += 4;
            }
        }
#else
        /* OCS: clear 2 blocks x 16 colors */
        uint8_t *p;
        p = _copperListBuffer + COP_PAL_B0_C0_VAL_OFF;
        for (i = 15; i >= 0; i--)
        {
            copper_write_word(p, 0);
            p += 4;
        }
        p = _copperListBuffer + COP_PAL_B1_C0_VAL_OFF;
        for (i = 15; i >= 0; i--)
        {
            copper_write_word(p, 0);
            p += 4;
        }
#endif
    }

    /* Set bitplane base pointers */
    _bitPlane0BasePtr = (uintptr_t)_bitPlane0Base;
    _bitPlane1BasePtr = (uintptr_t)_bitPlane1Base;
    _bitPlane2BasePtr = (uintptr_t)_bitPlane2Base;
    _bitPlane3BasePtr = (uintptr_t)_bitPlane3Base;
#ifdef AGA
    bitPlane4BasePtr = (uint32_t)_emptySpriteDma;
    bitPlane5BasePtr = (uint32_t)_emptySpriteDma;
    bitPlane6BasePtr = (uint32_t)_emptySpriteDma;
    bitPlane7BasePtr = (uint32_t)_emptySpriteDma;
#endif

    /* Clear first long of each bitplane */
    *(volatile uint32_t *)_bitPlane0Base = _copperZeroLong;
    *(volatile uint32_t *)_bitPlane1Base = _copperZeroLong;
    *(volatile uint32_t *)_bitPlane2Base = _copperZeroLong;
    *(volatile uint32_t *)_bitPlane3Base = _copperZeroLong;
#ifdef AGA
    *(volatile uint32_t *)_emptySpriteDma = _copperZeroLong;
#endif

    /* Update plane pointers and blit parameters in copper list */
    UpdatePlanePtrsAndBlitParams();

    /* Install copper list */
    _custom->cop1lc = (uintptr_t)_copperListBuffer;

    /* Strobe copper to start — original asm does: move.w (COPJMP1).l,d0
     * This is a pure read strobe; any access triggers copper PC reload. */
    {
        volatile uint16_t dummy = _custom->copjmp1;
        (void)dummy;
    }

    /* Enable blitter DMA, wait for idle, disable */
    _custom->dmacon = (uint16_t)DMACON_BLT_ON;
    (void)(_custom->dmaconr & (1u << 14));
    while (BLIT_BUSY())
    {
        /* wait for blitter idle */
    }
    _custom->dmacon = (uint16_t)DMACON_BLT_OFF;

    /* Enable copper + bitplane DMA */
    _custom->dmacon = 0x8280u;
    /* Enable sprite DMA */
    _custom->dmacon = 0x8220u;

    /* Re-strobe copper after DMA is fully enabled.
     * Some emulators (FS-UAE) may ignore the pre-DMA strobe,
     * so re-trigger to ensure the copper starts from COP1LC. */
    {
        volatile uint16_t dummy = _custom->copjmp1;
        (void)dummy;
    }
}

/* ================================================================
 * UpdatePlanePtrsAndBlitParams  (SWOS2.s line 5488)
 *
 * Writes bitplane pointers into the copper list, handles scroll
 * alignment, builds plane pointer tables for double-buffered display.
 * ================================================================ */
__attribute__((used, externally_visible))
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
/* Where the display reads the plane row from, in pixels: the scroll position, plus
 * AGA_PITCH_DISPLAY_SHIFT_PX in a match. The FMODE=1 set-up below (12 longs from DDFSTRT
 * 0x20, the pull-back long) put the picture 16px RIGHT of the scroll position: the
 * screen's left edge showed cameraX-16. Bobs sit on the pitch either way, but everything
 * placed by SCREEN coordinates stood 16px right (the result bar, the MATCH STATS bars),
 * hardware sprites needed a +16 of their own, and the view was 16px off the camera's.
 * Reading 16px further along puts screen x 0 on cameraX. In a match only: the menus'
 * placement (AGA_MENU_FG_SHIFT_BYTES) was set against the unshifted display.
 * 0 here = the display as it was (then AGA_HwSpriteXShift() is 16 again). */
#define AGA_PITCH_DISPLAY_SHIFT_PX 16
extern volatile uint16_t _playGame;
static inline uint16_t agaDisplayX(void)
{
    return (uint16_t)(_cameraXMod320 + (_playGame ? AGA_PITCH_DISPLAY_SHIFT_PX : 0));
}
int16_t AGA_HwSpriteXShift(void)
{
    return (int16_t)(_playGame ? 16 - AGA_PITCH_DISPLAY_SHIFT_PX : 16);
}
#if AGA_FMODE == 3
/* The FMODE=3 match display (display.h AGA_FMODE). Only in a match: the menus are drawn for
 * the 336px FMODE=1 display and keep it. */
int16_t AGA_Fm3Active(void)
{
    return (int16_t)(_playGame != 0);
}
/* Its fetch window and the pixel it shows at the window's left edge, as measured on FS-UAE
 * (see the FMODE=3 block in UpdatePlanePtrsAndBlitParams). Variables, not constants, so the
 * scroll-test rig can sweep them from gdb. */
int32_t g_fm3DdfStart = 0x18, g_fm3DdfStop = 0xB8, g_fm3ShowCal = 0;
#endif
#elif defined(AGA)
/* SDL has no copper: the AGA branches below still compile there, unshifted. */
static inline uint16_t agaDisplayX(void) { return _cameraXMod320; }
#endif

void UpdatePlanePtrsAndBlitParams(void)
{
    uint32_t d0;
    uint16_t d1;
    int16_t i;

    /* Write bitplane base pointers into copper list sprite pointer slots */
    copper_write_ptr(_copperListBuffer + COP_SPR_B0_OFF, _bitPlane0BasePtr);
    copper_write_ptr(_copperListBuffer + COP_SPR_B0_OFF + 8, _bitPlane1BasePtr);
    copper_write_ptr(_copperListBuffer + COP_SPR_B0_OFF + 16, _bitPlane2BasePtr);
    copper_write_ptr(_copperListBuffer + COP_SPR_B0_OFF + 24, _bitPlane3BasePtr);
#ifdef AGA
    copper_write_ptr(_copperListBuffer + COP_SPR_B0_OFF + 32, bitPlane4BasePtr);
    copper_write_ptr(_copperListBuffer + COP_SPR_B0_OFF + 40, bitPlane5BasePtr);
    copper_write_ptr(_copperListBuffer + COP_SPR_B0_OFF + 48, bitPlane6BasePtr);
    copper_write_ptr(_copperListBuffer + COP_SPR_B0_OFF + 56, bitPlane7BasePtr);
#endif

    /* Handle horizontal scroll alignment */
#ifdef AGA
    /* FMODE=1 (32-bit / 2x fetch) scroll. The coarse cameraX byte offset (below)
     * is 4-byte / 32px aligned so the bitplane-fetch pointer is 32-bit aligned
     * (a hard FMODE=1 requirement); the residual is a BPLCON1 fine scroll.
     *
     * The 8-bit AGA lores scroll code maps to BPLCON1 as H2..H5 = pixel bits
     * 8,4,2,1 in bits 0-3(PF1)/4-7(PF2) and H6 (+16px) in bit10(PF1)/14(PF2). The
     * playfield delay D shows the pixel D to the LEFT of the fetch pointer, i.e.
     * displayed = pointer - D. With a 32px-aligned pointer at `coarse`, the clean
     * range is delay 16..31 = displayed offset 1..16 (RIGHT of the pointer, all
     * fetched). That covers only HALF of each 32px block:
     *   frac 0     : aligned, delay 0.
     *   frac 1..16 : pointer=coarse, delay = 32-frac (16..31), H6. All fetched.
     *   frac 17..31: pointer=coarse+32px (advanced one block in the plane-ptr block
     *                below), delay = 64-frac (33..47), H7. Showing the position from
     *                the NEXT fetched 32px block means the H7 delay reveals FETCHED
     *                data, not the pre-fetch pipeline garbage a plain positive delay
     *                would (that was the old "left strip"). This gives true per-pixel
     *                smoothness across the full 0..31px with NO snap and NO strip
     *                (verified strip-free + smooth at frac 24 and 31 on FS-UAE via the
     *                test-rig BPLCON1/pointer overrides). See docs/AGA.md §11. */
    /* FMODE=1 smooth horizontal scroll (the AGA analogue of the OCS path below,
     * scaled from a 16px word to a 32px long). Faithful to how OCS switches the
     * fetch window between aligned and unaligned frames:
     *   ALIGNED (frac 0): 11-long (352px) fetch, DDFSTRT 0x30, modulo ROW-44,
     *       BPLCON1 0, plane pointer = coarse. No fine delay, nothing to reveal.
     *   UNALIGNED (frac 1..31): 12-long (384px = the FULL plane row) fetch, DDFSTRT
     *       one long earlier (0x20), modulo ROW-48. The extra LEFT long sits under
     *       the BPLCON1 fine-scroll delay so it reveals FETCHED pitch, not the
     *       pre-fetch gap that used to show as the empty far-left column. delay =
     *       32-frac (1..31) via PF1H2..H5 (bits 0-3) + PF1H6 (+16, bit 10) and the
     *       PF2 mirror; NO H7.
     * The DDFSTRT change at the frac 31->0 boundary cancels the coarse pointer step,
     * and the plane-pointer pull-back (one long, ONLY when PF1H6 is set = frac 1..16;
     * see the pointer block) keeps every transition per-pixel smooth. This gives
     * smooth scroll across all 32px boundaries AND no empty column. Root-caused +
     * validated frac 0..31 static/incremental on FS-UAE 2026-07-14 (the earlier
     * H7 + pointer-advance scheme showed the right position but revealed the
     * pre-fetch gap = the column, and disagreeing pointer/delay = the jitter).
     * See RandyAGA.txt "Smooth Hardware Scrolling" + "LONG burst mode", docs/AGA.md. */
#if AGA_FMODE == 3 && !defined(SWOS_SDL_BUILD)
    /* FMODE=3: a 64px fetch unit. The window shows pitch pixels [v, v+320), v = the scroll
     * position + AGA_FM3_WINDOW_LEFT_PX. The plane pointer moves in whole units: it is the
     * unit at or before v-1, so the window starts S = v - pointer = 1..64 pixels into the
     * 384px (6 units = the whole plane row) fetched, and BPLCON1 delays the picture by
     * D = 64 - S = 0..63 -- every position reachable, one pointer per frame. */
    int32_t fm3Ptr = 0;
    if (AGA_FM3()) {
        int32_t v = (int32_t)_cameraXMod320 + AGA_FM3_WINDOW_LEFT_PX - AGA_FM3_VIEW_SHIFT_PX
                  + g_fm3ShowCal;
        uint16_t delay, lo, h6, h7;
        fm3Ptr = ((v - 1) >> 6) << 6;                      /* floor, may be -64 */
        delay = (uint16_t)(64 - (v - fm3Ptr));             /* 0..63 */
        lo = delay & 0x0Fu; h6 = (delay >> 4) & 1u; h7 = (delay >> 5) & 1u;
        copper_write_word(_copperListBuffer + COP_BPL1MOD_VAL_OFF, PITCH_DEST_MOD_FMODE);
        copper_write_word(_copperListBuffer + COP_BPL2MOD_VAL_OFF, PITCH_DEST_MOD_FMODE);
        copper_write_word(_copperListBuffer + COP_DDFSTRT_VAL_OFF, (uint16_t)g_fm3DdfStart);
        copper_write_word(_copperListBuffer + COP_DDFSTOP_VAL_OFF, (uint16_t)g_fm3DdfStop);
        copper_write_word(_copperListBuffer + COP_FMODE_VAL_OFF, 0x0003u);
        copper_write_word(_copperListBuffer + COP_DIWSTRT_VAL_OFF, 0x2481u);
        copper_write_word(_copperListBuffer + COP_DIWSTOP_VAL_OFF, 0x34C1u);
        copper_write_word(_copperListBuffer + COP_BPLCON1_VAL_OFF,
                          (uint16_t)(lo | (lo << 4) | (h6 << 10) | (h6 << 14)
                                        | (h7 << 11) | (h7 << 15)));
    } else {
        copper_write_word(_copperListBuffer + COP_FMODE_VAL_OFF, 0x0001u);
        copper_write_word(_copperListBuffer + COP_DIWSTRT_VAL_OFF, 0x2471u);
        copper_write_word(_copperListBuffer + COP_DIWSTOP_VAL_OFF, 0x34C1u);
    }
    if (!AGA_FM3()) {
#endif
    d1 = agaDisplayX() & 0x1Fu;                /* fine fraction 0..31px */
    copper_write_word(_copperListBuffer + COP_BPL1MOD_VAL_OFF, PITCH_DEST_MOD_FMODE);
    copper_write_word(_copperListBuffer + COP_BPL2MOD_VAL_OFF, PITCH_DEST_MOD_FMODE);
    copper_write_word(_copperListBuffer + COP_DDFSTRT_VAL_OFF, 0x0020u);
    copper_write_word(_copperListBuffer + COP_DDFSTOP_VAL_OFF, 0x00D0u);
    {
        /* One uniform fine-scroll delay for the WHOLE 0..31px range: delay = 32-frac
         * (frac 0 -> 32 -> PF1H7, frac 1..16 -> 31..16 -> PF1H6, frac 17..31 ->
         * 15..1 -> low bits). Encoded across PF1H2..H5 (bits 0-3) + PF1H6 (+16, bit
         * 10) + PF1H7 (+32, bit 11) and the PF2 mirror (bits 4-7,14,15). The matching
         * plane-pointer pull-back (one long when delay>=16; see the pointer block)
         * keeps the fetched extra-left long under the delay AND cancels the coarse
         * pointer step at every 32px boundary. Result: per-pixel smooth scroll across
         * frac 0..31 with NO empty column. */
        uint16_t delay = (uint16_t)(32u - d1);          /* frac 0..31 -> 32..1 */
        uint16_t lo = delay & 0x0Fu;
        uint16_t h6 = (delay >> 4) & 1u;                /* PF1H6/PF2H6 = +16px */
        uint16_t h7 = (delay >> 5) & 1u;                /* PF1H7/PF2H7 = +32px (frac 0) */
        copper_write_word(_copperListBuffer + COP_BPLCON1_VAL_OFF,
                          (uint16_t)(lo | (lo << 4) | (h6 << 10) | (h6 << 14)
                                        | (h7 << 11) | (h7 << 15)));
    }
#if AGA_FMODE == 3 && !defined(SWOS_SDL_BUILD)
    }
#endif
#else
    d1 = _cameraXMod320 & 0x0Fu;
    if (d1 == 0)
    {
        /* Aligned case */
        copper_write_word(_copperListBuffer + COP_BPL1MOD_VAL_OFF, PITCH_DEST_MOD_ALIGNED);
        copper_write_word(_copperListBuffer + COP_BPL2MOD_VAL_OFF, PITCH_DEST_MOD_ALIGNED);
        copper_write_word(_copperListBuffer + COP_DDFSTRT_VAL_OFF, 0x0030u);
        copper_write_word(_copperListBuffer + COP_BPLCON1_VAL_OFF, 0);
    }
    else
    {
        /* Unaligned case */
        uint16_t shift = 0x10u - d1;
        copper_write_word(_copperListBuffer + COP_BPL1MOD_VAL_OFF, PITCH_DEST_MOD_UNALIGNED);
        copper_write_word(_copperListBuffer + COP_BPL2MOD_VAL_OFF, PITCH_DEST_MOD_UNALIGNED);
        copper_write_word(_copperListBuffer + COP_DDFSTRT_VAL_OFF, 0x0028u);
        copper_write_word(_copperListBuffer + COP_BPLCON1_VAL_OFF, (uint16_t)(shift | (shift << 4)));
    }
#endif

    /* Build plane pointer tables at copper BPL pointer blocks A and B */
    {
        uint8_t *tableA = _copperListBuffer + COP_BPLPTR_BLOCKA_OFF;
        uint8_t *tableB = _copperListBuffer + COP_BPLPTR_BLOCKB_OFF;
        uint32_t baseOffset;
        uint32_t altOffset;

        /* Compute Y offset */
        baseOffset = (uint32_t)_cameraYMod320 * ROW_STRIDE_BYTES;

        /* Select base address */
        if (_usePlaneOffsetFlag != 0)
        {
            baseOffset += (uintptr_t)_frontbufferMaskBuffer;
        }
        else if (_useAltBaseFlag != 0)
        {
            baseOffset += _frontbufferBaseAltPtr;
        }
        else
        {
            baseOffset += _frontbufferBasePtr;
        }

        /* Add X byte offset (coarse scroll). 4-byte / 32px aligned under
         * FMODE=1 so the 32-bit fetch pointer is 32-bit aligned; 2-byte
         * otherwise. The residual sub-align px is the BPLCON1 fine scroll above. */
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
        d1 = agaDisplayX();
#else
        d1 = _cameraXMod320;
#endif
        d1 >>= 3;
        d1 &= SCROLL_X_COARSE_ALIGN_MASK;
        /* Skip the per-plane verge margin: the pitch DATA lives at byte
         * SCROLL_LEFT_MARGIN_BYTES.. of each plane row; the -4 pull-back below then
         * reads the margin (byte 0..) at coarse=0 = the pitch's extreme-left edge. */
        d1 += SCROLL_LEFT_MARGIN_BYTES;
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
        /* Outside a match (menus: scroll position 0, so the pull-back below applies)
         * the picture starts at byte 0 of the row, as it did when the row had a 4-byte
         * margin for the pull-back to land on. */
        if (!_playGame)
            d1 += 4;
#endif
        /* No H7 +32px advance: the fine scroll is the continuous 0..31 delay above,
         * and the plane pointer is pulled one long earlier below (baseOffset -= 4)
         * so the 12-long fetch's extra LEFT long sits under that delay. */
        baseOffset += d1;

        /* Compute alt offset for second table */
        if (_usePlaneOffsetFlag != 0)
        {
            altOffset = (uintptr_t)d1 + (uintptr_t)_frontbufferMaskBuffer;
        }
        else
        {
            altOffset = (uint32_t)d1 + _frontbufferBasePtr;
        }

#ifdef AGA
        /* FMODE=1 12-long fetch (see the scroll block above): pull the plane pointer
         * back one long (4 bytes / 32px) so the fetched extra-LEFT long lands under
         * the BPLCON1 fine-scroll delay -- but ONLY when the delay >= 16 (PF1H6 or
         * PF1H7 set = frac 0..16). For frac 17..31 (delay 1..15) the pointer stays at
         * coarse. This delay-conditional pull-back is what cancels the coarse pointer
         * step at each 32px boundary, making frac 31->0, 0->1 and 16->17 all per-pixel
         * smooth while keeping every fraction column-free (verified on FS-UAE). */
#if AGA_FMODE == 3 && !defined(SWOS_SDL_BUILD)
        if (AGA_FM3())
        {
            /* the FMODE=3 pointer, computed above, replaces the coarse and the pull-back */
            int32_t delta = (fm3Ptr >> 3) - (int32_t)d1;
            baseOffset += (uint32_t)delta;
            altOffset  += (uint32_t)delta;
        }
        else
#endif
        if ((32u - (uint32_t)(agaDisplayX() & 0x1Fu)) >= 16u)
        {
            /* The pull-back fetches the plane byte at coarse-4 (32px LEFT of the
             * visible edge). Mid-pitch that is the previous pitch block; at the
             * pitch's extreme-left edge (coarse=0) it is the per-plane VERGE MARGIN
             * (bytes 0.., filled by fillVergeMargin with the repeated leftmost
             * column). So the -4 smoothing now works at every camX including the
             * first coarse block -- no edge-guard, no "jump back to the start". */
            baseOffset -= 4u;
            altOffset  -= 4u;
        }
#endif

#ifdef PITCH_SCROLL_TEST
        /* gdb-pokeable coarse-pointer byte nudge — used to empirically find the
         * correct pointer offset for the FMODE=1 positive-shift regime (the
         * "left strip" bug). 0 = camera-computed pointer. */
        {
            extern int32_t g_pstPtrByteOff;
            baseOffset += (uint32_t)g_pstPtrByteOff;
            altOffset += (uint32_t)g_pstPtrByteOff;
        }
#endif

        /* Write 4 plane pointer pairs */
        for (i = NUM_PLANES_MINUS1; i >= 0; i--)
        {
            copper_write_ptr(tableA, baseOffset);
            copper_write_ptr(tableB, altOffset);
            baseOffset += PLANE_STRIDE;
            altOffset += PLANE_STRIDE;
            tableA += 8;
            tableB += 8;
        }
    }

    /* Compute and write blit line count (raster of the block-B pointer reload) */
    {
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
        /* Block-B elimination: keep the reload raster constant at the bottom of
         * the display (SCREEN_HEIGHT + DIWSTRT) regardless of cameraYMod320, so
         * the mid-screen 8-pointer reload NEVER fires within the visible region.
         * Block A alone displays all SCREEN_HEIGHT lines, reading the 2x-tall
         * mirrored frontbuffer contiguously. */
        uint16_t lineCount = SCREEN_HEIGHT + 0x24u;
#else
        uint16_t lineCount = SCREEN_HEIGHT - _cameraYMod320 + 0x24u;
#endif
        if (lineCount > 0xFFu)
        {
            copper_write_word(_copperListBuffer + COP_WAIT_VHPOS, 0xFFE1u);
        }
        else
        {
            copper_write_word(_copperListBuffer + COP_WAIT_VHPOS, 1);
        }
        lineCount &= 0xFFu;
        copper_write_word(_copperListBuffer + COP_WAIT_WAIT, (uint16_t)((lineCount << 8) | 1u));
    }

#ifdef PITCH_SCROLL_TEST
    /* Copper-register overrides for the pitch-scroll test rig (gdb-pokeable).
     * Applied here so they survive the VBlank-ISR's own UpdatePlanePtrs call.
     * A value < 0 means "leave the camera-computed value". */
    {
        extern int32_t g_pstB1Force, g_pstDsForce, g_pstDeForce, g_pstMoForce;
        if (g_pstB1Force >= 0) copper_write_word(_copperListBuffer + COP_BPLCON1_VAL_OFF, (uint16_t)g_pstB1Force);
        if (g_pstDsForce >= 0) copper_write_word(_copperListBuffer + COP_DDFSTRT_VAL_OFF, (uint16_t)g_pstDsForce);
        if (g_pstDeForce >= 0) copper_write_word(_copperListBuffer + COP_DDFSTOP_VAL_OFF, (uint16_t)g_pstDeForce);
        if (g_pstMoForce >= 0) {
            copper_write_word(_copperListBuffer + COP_BPL1MOD_VAL_OFF, (uint16_t)g_pstMoForce);
            copper_write_word(_copperListBuffer + COP_BPL2MOD_VAL_OFF, (uint16_t)g_pstMoForce);
        }
    }
#endif
}

/* ================================================================
 * SwapBuffersAndCameraViewState  (SWOS2.s line 5613)
 *
 * Swaps between primary and alternate frontbuffers + bitplane sets.
 * Saves current camera state and restores the other frame's state.
 * ================================================================ */
__attribute__((used, externally_visible))
void SwapBuffersAndCameraViewState(void)
{
    if (_frontbufferBasePtr == (uintptr_t)_frontbufferBase)
    {
        /* Currently using primary → swap to alt */
        _bitPlane0BasePtr = (uintptr_t)_bitPlane0BaseAlt;
        _bitPlane1BasePtr = (uintptr_t)_bitPlane1BaseAlt;
        _bitPlane2BasePtr = (uintptr_t)_bitPlane2BaseAlt;
        _bitPlane3BasePtr = (uintptr_t)_bitPlane3BaseAlt;
#ifdef AGA
        bitPlane4BasePtr = (uint32_t)_emptySpriteDma;
        bitPlane5BasePtr = (uint32_t)_emptySpriteDma;
        bitPlane6BasePtr = (uint32_t)_emptySpriteDma;
        bitPlane7BasePtr = (uint32_t)_emptySpriteDma;
#endif

        *(volatile uint32_t *)_bitPlane0BaseAlt = _copperZeroLong;
        *(volatile uint32_t *)_bitPlane1BaseAlt = _copperZeroLong;
        *(volatile uint32_t *)_bitPlane2BaseAlt = _copperZeroLong;
        *(volatile uint32_t *)_bitPlane3BaseAlt = _copperZeroLong;
#ifdef AGA
        *(volatile uint32_t *)_emptySpriteDma = _copperZeroLong;
#endif

        _frontbufferBasePtr = (uintptr_t)_frontbufferBaseAlt;
        _frontbufferBaseAltPtr = (uintptr_t)_frontbufferBase;
        _spriteBlitQueue = (uintptr_t)&_kSpriteBlitQueueSentinel_B;
        /* DO NOT write 0xFFFFFFFF at the queue head here. The cursor
         * reset above is intentional — the previous frame's
         * DrawSprites calls left valid undraw entries between
         * &kSpriteBlitQueueSentinel_B and the cursor's pre-reset
         * value, and the next frame's ProcessSpriteBlitQueue is
         * supposed to walk through them. Clearing the head here
         * defeats that, and sprites smear because no undraw runs. */

        /* Save current camera state to slot 0 */
        _dstXByteOffsetSaved0 = _cameraXMod320;
        _dstYStartLinesSaved0 = _cameraYMod320;
        _oldCameraXSaved0 = _oldCameraX;
        _oldCameraYSaved0 = _oldCameraY;
        _cameraXSaved0 = _cameraX.s.i;
        _cameraYSaved0 = _cameraY.s.i;

        /* Restore camera state from slot 1 */
        _cameraXMod320 = _dstXByteOffsetSaved1;
        _cameraYMod320 = _dstYStartLinesSaved1;
        _oldCameraX = _oldCameraXSaved1;
        _oldCameraY = _oldCameraYSaved1;
        _cameraX.s.i = _cameraXSaved1;
        _cameraY.s.i = _cameraYSaved1;
    }
    else
    {
        /* Currently using alt → swap to primary */
        _bitPlane0BasePtr = (uintptr_t)_bitPlane0Base;
        _bitPlane1BasePtr = (uintptr_t)_bitPlane1Base;
        _bitPlane2BasePtr = (uintptr_t)_bitPlane2Base;
        _bitPlane3BasePtr = (uintptr_t)_bitPlane3Base;
#ifdef AGA
        bitPlane4BasePtr = (uint32_t)_emptySpriteDma;
        bitPlane5BasePtr = (uint32_t)_emptySpriteDma;
        bitPlane6BasePtr = (uint32_t)_emptySpriteDma;
        bitPlane7BasePtr = (uint32_t)_emptySpriteDma;
#endif

        *(volatile uint32_t *)_bitPlane0Base = _copperZeroLong;
        *(volatile uint32_t *)_bitPlane1Base = _copperZeroLong;
        *(volatile uint32_t *)_bitPlane2Base = _copperZeroLong;
        *(volatile uint32_t *)_bitPlane3Base = _copperZeroLong;
#ifdef AGA
        *(volatile uint32_t *)_emptySpriteDma = _copperZeroLong;
#endif

        _frontbufferBasePtr = (uintptr_t)_frontbufferBase;
        _frontbufferBaseAltPtr = (uintptr_t)_frontbufferBaseAlt;
        _spriteBlitQueue = (uintptr_t)&_kSpriteBlitQueueSentinel_A;
        /* Cursor reset only — do NOT clear the head sentinel here. See
         * comment in the alt-buffer branch above. */

        /* Save current camera state to slot 1 */
        _dstXByteOffsetSaved1 = _cameraXMod320;
        _dstYStartLinesSaved1 = _cameraYMod320;
        _oldCameraXSaved1 = _oldCameraX;
        _oldCameraYSaved1 = _oldCameraY;
        _cameraXSaved1 = _cameraX.s.i;
        _cameraYSaved1 = _cameraY.s.i;

        /* Restore camera state from slot 0 */
        _cameraXMod320 = _dstXByteOffsetSaved0;
        _cameraYMod320 = _dstYStartLinesSaved0;
        _oldCameraX = _oldCameraXSaved0;
        _oldCameraY = _oldCameraYSaved0;
        _cameraX.s.i = _cameraXSaved0;
        _cameraY.s.i = _cameraYSaved0;
    }
}

/* ================================================================
 * SetCameraCoordinates  (SWOS2.s line 6158)
 *
 * Iteratively moves cameraX.s.i/cameraXMod320 toward targetX and
 * cameraY.s.i/cameraYMod320 toward targetY, one pixel per iteration,
 * with wrapping at SCROLL_X_WRAP/272.
 * ================================================================ */
__attribute__((used, externally_visible))
void SetCameraCoordinates(int16_t targetX, int16_t targetY)
{
    /* Keep the _screen_width-wide view on the pitch for ANY caller — the kickoff/intro
     * and panel-restore set the camera directly, bypassing MoveCamera's clamp, so on a
     * wide view they'd run off the pitch edge until gameplay takes over. pitchWidth
     * (0x2A0) - viewWidth = the max camera X. Folds to [0, 0x150] on Amiga (_screen_width
     * = 336), the original range — a no-op for the always-on-pitch classic view. */
    {
        /* The pan range is a CONSTANT, not the render width. This is the
         * final camera write for every caller, so clamping it by
         * `_screen_width` put the display back into the simulation: `onScreen`
         * is measured from `_cameraX` (sprites.c) and `UpdateControlledPlayer`
         * will not auto-select a player whose flag is clear.
         *
         * Since b4775f3 pinned the simulation view to 336 while the render
         * stayed at the display width, the two clamps disagreed: `MoveCamera`
         * aims at `0x2A0 - _sim_view_width` = 336, and this line then cut it
         * to `0x2A0 - 484` = 188 on a 16:9 display. The camera simply stopped
         * 148px short and could not follow the ball into the right third of
         * the pitch — the ball ran out of the window, the players beside it
         * fell outside the 336-wide cull anchored on the stuck camera, and the
         * AI kept selecting men who could not reach it. That is a tester's
         * "the ball gets left and they run up to it one at a time".
         *
         * 336 rather than `SIM_CAMERA_PAN_RANGE`: both releases pan against
         * the Amiga's 336-wide buffer, the same constant the `onScreen` cull
         * uses, and the constant is what makes this fold to EXACTLY the old
         * expression wherever `_screen_width` is 336 — every 4:3 display, the
         * Amiga (the original [0, 0x150]), and the replay rig, which pins the
         * render to SCREEN_WIDTH. The rig sets `_sim_view_*` from the
         * recording, so `SIM_CAMERA_PAN_RANGE` there is 352 and would change
         * the corpus; there is no evidence for that value, so it is not used.
         * The renderer is unaffected: `DrawChunkyPitch` bounds-checks the tile
         * grid and paints the surround past the pitch edge. */
        int16_t panRange = (int16_t)(0x2A0 - SCREEN_WIDTH);
        if (panRange < 0) panRange = 0;
        if (targetX < 0) targetX = 0;
        else if (targetX > panRange) targetX = panRange;
    }

    /* Move X toward target */
    while (_cameraX.s.i != targetX)
    {
        if (_cameraX.s.i < targetX)
        {
            _cameraX.s.i += 1;
            _cameraXMod320 += 1;
            if (_cameraXMod320 == SCROLL_X_WRAP)
                _cameraXMod320 = 0;
        }
        else
        {
            _cameraX.s.i -= 1;
            _cameraXMod320 -= 1;
            if ((int16_t)_cameraXMod320 < 0)
                _cameraXMod320 = SCROLL_X_WRAP - 1;
        }
    }

    /* Move Y toward target */
    while (_cameraY.s.i != targetY)
    {
        if (_cameraY.s.i < targetY)
        {
            _cameraY.s.i += 1;
            _cameraYMod320 += 1;
            if (_cameraYMod320 == 272)
                _cameraYMod320 = 0;
        }
        else
        {
            _cameraY.s.i -= 1;
            _cameraYMod320 -= 1;
            if ((int16_t)_cameraYMod320 < 0)
                _cameraYMod320 = 271;
        }
    }

    /* FixedPoint union handles the integer/FP sync implicitly: writing
     * cameraX.s.i updates the high word of the shared storage, so
     * cameraX.raw (the int32 view) stays consistent without any manual
     * sync. This replaces the earlier split-variable hack. */
}

/* ================================================================
 * DoubleBufferSwapSequence  (SWOS2.s line 43156)
 *
 * Resets palette, zeroes camera position, swaps buffers twice
 * (updating copper plane pointers each time).
 * ================================================================ */
__attribute__((used, externally_visible))
void DoubleBufferSwapSequence(void)
{
    _paletteIndex = 0;
    SetCameraCoordinates(0, 0);
    SwapBuffersAndCameraViewState();
    UpdatePlanePtrsAndBlitParams();
    SwapBuffersAndCameraViewState();
    UpdatePlanePtrsAndBlitParams();
}

void ResetMenuViewState(void)
{
    /* Menus are painted at physical (0,0), not into the match's circular view.
     * Reset both slots together; a swap must never reintroduce a match origin.
     * The caller must redraw the complete menu before presenting it. */
    _cameraX.raw = _cameraY.raw = 0;
    _oldCameraX = _oldCameraY = 0;
    _cameraXMod320 = _cameraYMod320 = 0;
    _cameraXSaved0 = _cameraYSaved0 = 0;
    _cameraXSaved1 = _cameraYSaved1 = 0;
    _oldCameraXSaved0 = _oldCameraYSaved0 = 0;
    _oldCameraXSaved1 = _oldCameraYSaved1 = 0;
    _dstXByteOffsetSaved0 = _dstYStartLinesSaved0 = 0;
    _dstXByteOffsetSaved1 = _dstYStartLinesSaved1 = 0;
    _paletteIndex = 0;
    _usePlaneOffsetFlag = _useAltBaseFlag = 0;
}

/* ================================================================
 * BlitterCopyPanelToScreen2  (SWOS2.s line 43526)
 *
 * Copies frontbufferMaskBuffer to frontbuffer via blitter A→D copy.
 * ================================================================ */
#ifdef SWOS_SDL_BUILD
#include <string.h>
__attribute__((used, externally_visible))
void BlitterCopyPanelToScreen2(void)
{
    /* Paint the menu BG into BOTH buffers, not just the drawing target.
     *
     * Long-standing flicker bug (since the C port — present on both the
     * Amiga C port and this SDL port): on some menu transitions and
     * within-menu redraws (e.g. play-match → result, lineup formation
     * change), the user briefly sees the OLD menu's content showing
     * under the new menu's entries for 1–2 frames. Two screenshots
     * confirmed: mid-transition shows new top + bottom strips with the
     * old menu's middle visible, and a formation change momentarily
     * loses the tactic grid entirely.
     *
     * Mechanism: DrawMenu paints panel BG into the drawing target
     * (basePtr), draws entries on top, then WaitForVSync swaps. The
     * just-drawn buffer becomes displayed. BlitterCopyOverlayToScreen
     * copies the displayed frame back to the new drawing target so both
     * stay in sync — but until that copy runs, the OTHER buffer still
     * holds whatever the previous menu painted. Any draw path or async
     * present that touches the other buffer in this brief window
     * (timer-thread render, DrawSelectedMenuItemFrame, partial
     * CallDrawMenuItem on either side of a swap) shows the stale
     * content.
     *
     * The original Amiga code relied on hardware bitplane-pointer
     * swap semantics that made the same flow atomic; the C port
     * doesn't get that for free. The cheapest defensive fix is to
     * paint the BG into BOTH buffers up front — neither side can show
     * old content because both are now the same clean BG.
     *
     * Cost: one extra full-frame memcpy on every menu draw (~91 KB).
     * Negligible at 50 Hz. */
    /* (frontbufferMaskBuffer is a static array, so its address is always
     * non-null — historical NULL guard removed.) */
    uint8_t *dst = (uint8_t *)_frontbufferBasePtr;
    if (dst)
        memcpy(dst, _frontbufferMaskBuffer, FRONTBUFFER_SIZE_TOTAL_BYTES);
    uint8_t *altDst = (uint8_t *)_frontbufferBaseAltPtr;
    if (altDst && altDst != dst)
        memcpy(altDst, _frontbufferMaskBuffer, FRONTBUFFER_SIZE_TOTAL_BYTES);
}
#else
__attribute__((used, externally_visible))
void BlitterCopyPanelToScreen2(void)
{
    /* Enable blitter DMA and wait for idle */
    _custom->dmacon = (uint16_t)DMACON_BLT_ON;
    (void)(_custom->dmaconr & (1u << 14));
    while (BLIT_BUSY())
    {
        /* wait */
    }
    _custom->dmacon = (uint16_t)DMACON_BLT_OFF;

    /* Configure blitter for A→D copy */
    BLT_W(_custom->bltalwm, BLT_FWM_ALL);
    BLT_W(_custom->bltafwm, BLT_FWM_ALL);
    BLT_W(_custom->bltcon1, 0);
    BLT_W(_custom->bltcon0, (uint16_t)BLTCON0_USEA_USED_LF_F0);
#ifdef AGA
    /* AGA: per-plane blit with interleaved stride */
    BLT_W(_custom->bltamod, (uint16_t)(ROW_STRIDE_BYTES - PLANE_STRIDE));
    BLT_W(_custom->bltdmod, (uint16_t)(ROW_STRIDE_BYTES - PLANE_STRIDE));
    {
        uint8_t *srcPlane = _frontbufferMaskBuffer;
        uint8_t *dstPlane = (uint8_t *)_frontbufferBasePtr;
        int16_t plane;
        for (plane = NUM_PLANES_MINUS1; plane >= 0; plane--)
        {
            _custom->dmacon = (uint16_t)DMACON_BLT_ON;
            while (BLIT_BUSY()) {}
            _custom->dmacon = (uint16_t)DMACON_BLT_OFF;

            BLT_L(_custom->bltapt, (uint32_t)srcPlane);
            BLT_L(_custom->bltdpt, (uint32_t)dstPlane);
            BLT_W(_custom->bltsize, (uint16_t)BLTSIZE_OVERLAY_TO_SCREEN);
            srcPlane += PLANE_STRIDE;
            dstPlane += PLANE_STRIDE;
        }
    }
    /* Wait for last blit */
    _custom->dmacon = (uint16_t)DMACON_BLT_ON;
    while (BLIT_BUSY()) {}
    _custom->dmacon = (uint16_t)DMACON_BLT_OFF;
#else
    /* OCS: single full-buffer blit */
    BLT_W(_custom->bltamod, 0);
    BLT_W(_custom->bltdmod, 0);
    /* Set source and destination pointers */
    BLT_L(_custom->bltapt, (uint32_t)_frontbufferMaskBuffer);
    BLT_L(_custom->bltdpt, _frontbufferBasePtr);
    /* Kick the blit (writing BLTSIZE starts it) */
    BLT_W(_custom->bltsize, (uint16_t)BLTSIZE_OVERLAY_PANEL);
    /* Wait for blit to complete, then disable blitter DMA */
    _custom->dmacon = (uint16_t)DMACON_BLT_ON;
    (void)(_custom->dmaconr & (1u << 14));
    while (BLIT_BUSY())
    {
        /* wait */
    }
    _custom->dmacon = (uint16_t)DMACON_BLT_OFF;

    /* Reset blit size to small default */
    BLT_W(_custom->bltsize, 0x0016u);
#endif
}
#endif /* SWOS_SDL_BUILD */

/* ================================================================
 * ProcessSpriteBlitQueue  (SWOS2.s line 5522)
 *
 * Drains the sprite blit queue (a linked list of A→D blitter
 * copy entries). Each entry is 12 bytes:
 *   +0: BLTAPT (source ptr, -1 = sentinel/end)
 *   +4: BLTDPT (dest ptr)
 *   +8: BLTAMOD/BLTDMOD
 *  +10: BLTSIZE (kicks blit)
 * ================================================================ */
__attribute__((used, externally_visible))
void ProcessSpriteBlitQueue(void)
{
    volatile uint32_t *queue;
#if !defined(SWOS_SDL_BUILD) && defined(AGA)
    /* 2026-10-14 (AmigaScope: the undraw was ~10% of a stock A1200's frame, most of it
     * per-blit overhead around one small blit a bob):
     *  - blitter priority ON for the whole batch, not toggled around every wait: the CPU
     *    only polls here, and the bitplanes and the copper still come first;
     *  - the next entry is read BEFORE the wait, and the end of the queue needs no wait
     *    at all -- the last blit runs on, and every later blitter user waits first;
     *  - the modulos are written only when they change (they follow the bob's width). */
    uint16_t lastMod = 0xFFFFu;

    _custom->dmacon = (uint16_t)DMACON_BLT_ON;
    (void)(_custom->dmaconr & (1u << 14));
    while (BLIT_BUSY()) {}
    BLT_W(_custom->bltafwm, BLT_FWM_ALL);
    BLT_W(_custom->bltalwm, BLT_FWM_ALL);
    BLT_W(_custom->bltcon1, 0);
    BLT_W(_custom->bltcon0, (uint16_t)BLTCON0_USEA_USED_LF_F0);
    queue = (volatile uint32_t *)_spriteBlitQueue;
    for (;;) {
        uint32_t srcPtr = queue[0];
        uint16_t *entry16 = (uint16_t *)queue;
        uint16_t mod;
        if ((int32_t)srcPtr < 0)
            break;                              /* sentinel: end of queue */
        mod = entry16[4];
        (void)(_custom->dmaconr & (1u << 14));
        while (BLIT_BUSY()) {}
        BLT_L(_custom->bltapt, srcPtr);
        BLT_L(_custom->bltdpt, queue[1]);
        if (mod != lastMod) {
            /* bit 15: a tile blit (ScrollX's twin column, scroll_blit.c) -- A is one
             * contiguous tile, D steps a plane row */
            BLT_W(_custom->bltamod, (mod & QUEUE_MOD_TILE) ? 0 : mod);
            BLT_W(_custom->bltdmod, mod & (uint16_t)~QUEUE_MOD_TILE);
            lastMod = mod;
        }
        BLT_W(_custom->bltsize, entry16[5]);    /* kicks the blit */
        queue = (volatile uint32_t *)((uint8_t *)queue + 12);
    }
    _custom->dmacon = (uint16_t)DMACON_BLT_OFF;
#else


    /* Enable blitter and wait idle */
    _custom->dmacon = (uint16_t)DMACON_BLT_ON;
    (void)(_custom->dmaconr & (1u << 14));
    while (BLIT_BUSY()) {}
    _custom->dmacon = (uint16_t)DMACON_BLT_OFF;

    /* Set blitter config: A→D copy, full masks, no shift */
    BLT_W(_custom->bltafwm, BLT_FWM_ALL);
    BLT_W(_custom->bltalwm, BLT_FWM_ALL);
    BLT_W(_custom->bltcon1, 0);
    BLT_W(_custom->bltcon0, (uint16_t)BLTCON0_USEA_USED_LF_F0);
    queue = (volatile uint32_t *)_spriteBlitQueue;

    for (;;)
    {
        uint32_t srcPtr;
        uint16_t *entry16;

        /* Wait for blitter idle */
        _custom->dmacon = (uint16_t)DMACON_BLT_ON;
        (void)(_custom->dmaconr & (1u << 14));
        while (BLIT_BUSY()) {}
        _custom->dmacon = (uint16_t)DMACON_BLT_OFF;

        srcPtr = queue[0];
        if ((int32_t)srcPtr < 0)
            break; /* sentinel: end of queue */

        BLT_L(_custom->bltapt, srcPtr);
        BLT_L(_custom->bltdpt, queue[1]);
        entry16 = (uint16_t *)queue;
        BLT_W(_custom->bltamod, entry16[4]); /* +8 */
        BLT_W(_custom->bltdmod, entry16[4]); /* same mod for A and D */
        BLT_W(_custom->bltsize, entry16[5]); /* +10, kicks blit */

        queue = (volatile uint32_t *)((uint8_t *)queue + 12);
    }

#endif
    /* Reset queue: write sentinel at start */
    {
        uint32_t *queueBase = (uint32_t *)_spriteBlitQueue;
        *queueBase = 0xFFFFFFFFu;
    }
}

#if !defined(SWOS_SDL_BUILD)
/* The background undraw (2026-10-14). The same queue as ProcessSpriteBlitQueue, but the CPU
 * only starts the first blit: each later one is started by the blitter-finished interrupt
 * (Int3Handler, swos_amiga_func_stubs.c), which reads the next entry from g_blitQPtr and,
 * at the sentinel, switches itself off and clears g_blitQActive. So the CPU is free while
 * the bobs of two frames ago are wiped -- the match loop (game_loop.c) runs the frame's
 * logic meanwhile. Every blitter wait (BLIT_BUSY) waits for the queue too, and
 * BlitQueueDrain ends it: after it the queue is reset exactly as ProcessSpriteBlitQueue
 * leaves it. Nothing but the match loop uses it. */
volatile uint16_t g_blitQActive;
volatile uint32_t *g_blitQPtr;

void ProcessSpriteBlitQueueAsync(void)
{
    volatile uint32_t *queue = (volatile uint32_t *)_spriteBlitQueue;
    uint16_t *entry16 = (uint16_t *)queue;

    _custom->dmacon = (uint16_t)DMACON_BLT_ON;
    (void)(_custom->dmaconr & (1u << 14));
    while (BLIT_BUSY()) {}
    if ((int32_t)queue[0] < 0) {
        _custom->dmacon = (uint16_t)DMACON_BLT_OFF;
        return;                                 /* nothing drawn two frames ago */
    }
    BLT_W(_custom->bltafwm, BLT_FWM_ALL);
    BLT_W(_custom->bltalwm, BLT_FWM_ALL);
    BLT_W(_custom->bltcon1, 0);
    BLT_W(_custom->bltcon0, (uint16_t)BLTCON0_USEA_USED_LF_F0);
    BLT_L(_custom->bltapt, queue[0]);
    BLT_L(_custom->bltdpt, queue[1]);
    BLT_W(_custom->bltamod, (entry16[4] & QUEUE_MOD_TILE) ? 0 : entry16[4]);
    BLT_W(_custom->bltdmod, entry16[4] & (uint16_t)~QUEUE_MOD_TILE);
    /* A finished blit's request is never acknowledged outside the queue: clear it first,
     * or the interrupt would take it for this blit and start the next one on top of it. */
    _custom->intreq = 0x0040u;
    g_blitQPtr = queue + 3;
    g_blitQActive = 1;
    BLT_W(_custom->bltsize, entry16[5]);        /* kicks the first blit */
    _custom->intena = 0x8040u;                  /* BLIT: the interrupt starts the rest */
}

void BlitQueueDrain(void)
{
    _custom->dmacon = (uint16_t)DMACON_BLT_ON;
    (void)(_custom->dmaconr & (1u << 14));
    while (BLIT_BUSY()) {}
    _custom->dmacon = (uint16_t)DMACON_BLT_OFF;
    *(uint32_t *)_spriteBlitQueue = 0xFFFFFFFFu;
}
#endif

/* ================================================================
 * BlitterCopyPanelToScreen  (SWOS2.s line 6591)
 *
 * Copies frontbufferMaskBuffer to frontbuffer (panel/overlay blit).
 * Same as BlitterCopyPanelToScreen2 but uses CUSTOM base register
 * style (identical logic).
 * ================================================================ */
#ifdef SWOS_SDL_BUILD
__attribute__((used, externally_visible))
void BlitterCopyPanelToScreen(void)
{
    uint8_t *dst = (uint8_t *)_frontbufferBasePtr;
    /* (frontbufferMaskBuffer is a static array, address always non-null.) */
    if (dst)
        memcpy(dst, _frontbufferMaskBuffer, FRONTBUFFER_SIZE_TOTAL_BYTES);
}
#else
__attribute__((used, externally_visible))
void BlitterCopyPanelToScreen(void)
{
    /* Wait for blitter idle */
    _custom->dmacon = (uint16_t)DMACON_BLT_ON;
    (void)(_custom->dmaconr & (1u << 14));
    while (BLIT_BUSY()) {}
    _custom->dmacon = (uint16_t)DMACON_BLT_OFF;

    /* Configure: A→D copy, full masks */
    BLT_W(_custom->bltalwm, BLT_FWM_ALL);
    BLT_W(_custom->bltafwm, BLT_FWM_ALL);
    BLT_W(_custom->bltcon1, 0);
    BLT_W(_custom->bltcon0, (uint16_t)BLTCON0_USEA_USED_LF_F0);
#ifdef AGA
    /* AGA: per-plane blit with interleaved stride */
    BLT_W(_custom->bltamod, (uint16_t)(ROW_STRIDE_BYTES - PLANE_STRIDE));
    BLT_W(_custom->bltdmod, (uint16_t)(ROW_STRIDE_BYTES - PLANE_STRIDE));
    {
        uint8_t *srcPlane = _frontbufferMaskBuffer;
        uint8_t *dstPlane = (uint8_t *)_frontbufferBasePtr;
        int16_t plane;
        for (plane = NUM_PLANES_MINUS1; plane >= 0; plane--)
        {
            _custom->dmacon = (uint16_t)DMACON_BLT_ON;
            while (BLIT_BUSY()) {}
            _custom->dmacon = (uint16_t)DMACON_BLT_OFF;

            BLT_L(_custom->bltapt, (uint32_t)srcPlane);
            BLT_L(_custom->bltdpt, (uint32_t)dstPlane);
            BLT_W(_custom->bltsize, (uint16_t)BLTSIZE_OVERLAY_TO_SCREEN);
            srcPlane += PLANE_STRIDE;
            dstPlane += PLANE_STRIDE;
        }
    }
    _custom->dmacon = (uint16_t)DMACON_BLT_ON;
    while (BLIT_BUSY()) {}
    _custom->dmacon = (uint16_t)DMACON_BLT_OFF;
#else
    /* OCS: single full-buffer blit */
    BLT_W(_custom->bltamod, 0);
    BLT_W(_custom->bltdmod, 0);
    /* Source = frontbufferMaskBuffer, Dest = frontbuffer */
    BLT_L(_custom->bltapt, (uint32_t)_frontbufferMaskBuffer);
    BLT_L(_custom->bltdpt, _frontbufferBasePtr);
    BLT_W(_custom->bltsize, (uint16_t)BLTSIZE_OVERLAY_PANEL);
    /* Wait, then kick small trailing blit */
    _custom->dmacon = (uint16_t)DMACON_BLT_ON;
    (void)(_custom->dmaconr & (1u << 14));
    while (BLIT_BUSY()) {}
    _custom->dmacon = (uint16_t)DMACON_BLT_OFF;
    BLT_W(_custom->bltsize, 0x0016u);
#endif
}
#endif /* SWOS_SDL_BUILD */

#if defined(AGA) && !defined(SWOS_SDL_BUILD)
/* The match's BlitterCopyPanelToScreen (2026-10-14): what the bench drew over the pitch is
 * wiped by copying the window back from its twin rows (undrawTwin, display.h), which hold
 * nothing but the pitch -- the 1x mask buffer it copied from is no longer kept in a match.
 * A display row r shows bytes [r*ROW + coarse, (r+1)*ROW + coarse): its last plane runs on
 * into the head of the next row. So the copy is two runs of whole rows shifted by the
 * coarse byte: the window's rows below SCREEN_HEIGHT (and row SCREEN_HEIGHT's head) from
 * the mirror half, the rest from the top half. Rows the camera has moved onto since this
 * buffer was last scrolled come back stale and are written by this frame's scroll. */
static void linearBlitCopy(uint32_t src, uint32_t dst, uint32_t bytes)
{
    uint16_t lines = (uint16_t)(bytes / FMODE_FETCH_BYTES);
    if (lines == 0)
        return;
    _custom->dmacon = (uint16_t)DMACON_BLT_ON;
    (void)(_custom->dmaconr & (1u << 14));
    while (BLIT_BUSY()) {}
    BLT_W(_custom->bltafwm, BLT_FWM_ALL);
    BLT_W(_custom->bltalwm, BLT_FWM_ALL);
    BLT_W(_custom->bltcon1, 0);
    BLT_W(_custom->bltcon0, (uint16_t)BLTCON0_USEA_USED_LF_F0);
    BLT_W(_custom->bltamod, 0);
    BLT_W(_custom->bltdmod, 0);
    BLT_L(_custom->bltapt, src);
    BLT_L(_custom->bltdpt, dst);
    *(volatile uint16_t *)(CUSTOM_BASE + 0x05Cu) = lines;                            /* BLTSIZV */
    *(volatile uint16_t *)(CUSTOM_BASE + 0x05Eu) = (uint16_t)(FMODE_FETCH_BYTES / 2); /* BLTSIZH: go */
    while (BLIT_BUSY()) {}
    _custom->dmacon = (uint16_t)DMACON_BLT_OFF;
}

void RestoreWindowFromTwins(void)
{
    uint32_t fb = (uint32_t)_frontbufferBasePtr;
    uint32_t y = _cameraYMod320;
    uint32_t coarse = ((uint32_t)(_cameraXMod320 >> 3)) & (uint32_t)(uint16_t)SCROLL_X_COARSE_ALIGN_MASK;
    uint32_t a = fb + y * ROW_STRIDE_BYTES + coarse;
    linearBlitCopy(a + FB_MIRROR_OFFSET_BYTES, a, (SCREEN_HEIGHT - y) * ROW_STRIDE_BYTES);
    linearBlitCopy(fb + coarse, fb + FB_MIRROR_OFFSET_BYTES + coarse, y * ROW_STRIDE_BYTES);
}
#endif

/* ================================================================
 * HWSprite_LoadFromSpriteGraphics  (SWOS2.s line 6619)
 *
 * Clears a hardware sprite using the blitter. Looks up sprite data
 * from spriteGraphicsPtr table, then does a D-only fill (ADAT=0).
 *
 * Sprite entry layout: +0 = pointer to sprite data, +12 = height,
 * +14 = stride (TILE_STRIDE field, used as width in words).
 * ================================================================ */
__attribute__((used, externally_visible))
void HWSprite_LoadFromSpriteGraphics(uint16_t spriteIndex)
{
#ifdef SWOS_SDL_BUILD
    /* Chunky rewrite: zero the sprite's pixel buffer so subsequent
     * BlitGlyphToSpriteBitmap calls stamp onto a clean background.
     * The Amiga version programs bltdpt + D-only fill; our sprite
     * data is a flat 8bpp byte array of `wquads * 16 * nlines` bytes. */
    SpriteGraphics **table = (SpriteGraphics **)(uintptr_t)_spriteGraphicsPtr;
    SpriteGraphics *spr = table[spriteIndex];
    if (!spr) return;
    uint8_t *data = spr->sprData;
    if (!data) return;
    size_t bytes = (size_t)spr->wquads * 16 * (size_t)spr->nlines;
    memset(data, 0, bytes);
#else
    uint32_t *gfxTable = (uint32_t *)_spriteGraphicsPtr;
    uint8_t *sprEntry;
    uint32_t destPtr;
    uint16_t height;
    uint16_t stride;
    uint16_t blitSize;

    sprEntry = (uint8_t *)gfxTable[spriteIndex];

    /* Wait for blitter idle */
    _custom->dmacon = (uint16_t)DMACON_BLT_ON;
    (void)(_custom->dmaconr & (1u << 14));
    while (BLIT_BUSY()) {}
    _custom->dmacon = (uint16_t)DMACON_BLT_OFF;

    /* Configure: D-only fill (LF=F0 with no source channels) */
    BLT_W(_custom->bltalwm, BLT_FWM_ALL);
    BLT_W(_custom->bltafwm, BLT_FWM_ALL);
    BLT_W(_custom->bltcon1, 0);
    BLT_W(_custom->bltcon0, (uint16_t)BLTCON0_USED_LF_F0);
    BLT_W(_custom->bltadat, 0); /* fill with zeros */
    BLT_W(_custom->bltdmod, 0);
    /* Dest = sprite data pointer (first long of sprite entry) */
    destPtr = *(uint32_t *)sprEntry;
    BLT_L(_custom->bltdpt, destPtr);
    /* BLTSIZE row field = height * (real plane count): the sprite's data buffer is
     * `height * sp` plane-rows. Was hard NUM_PLANES(8); a 16-colour sprite is stored
     * at 4 planes now, so clearing 8 would over-clear into the next sprite. */
    height = *(uint16_t *)(sprEntry + 12);
    stride = *(uint16_t *)(sprEntry + 14); /* TILE_STRIDE offset */
    {
        uint16_t sp = spriteNumPlanes((int16_t)spriteIndex);
        blitSize = (uint16_t)(((uint16_t)(height * sp) << 6) | stride);
    }
    BLT_W(_custom->bltsize, blitSize);
#endif
}

/* ================================================================
 * FlashColor0Delay  (SWOS2.s line 5490)
 *
 * Debug/diagnostic: sets COLOR00 to a given color, loops for
 * loopCount iterations, then restores COLOR00 from copper list.
 * ================================================================ */
__attribute__((used, externally_visible))
void FlashColor0Delay(uint32_t loopCount, uint16_t color)
{
    volatile uint16_t *copperColor0 = (volatile uint16_t *)(_copperListBuffer + COP_PAL_B0_C0_VAL_OFF);

    _custom->color[0] = color;
    while (--loopCount != 0)
    {
        _custom->color[0] = color; /* keep writing to prevent optimizer from removing */
    }
    _custom->color[0] = *copperColor0;
}

/* ================================================================
 * WriteCopperPalette
 *
 * Writes a full palette into the _copperListBuffer color value slots.
 * AGA: 256 colors across 8 banks of 32 colors each.
 * OCS: 16 colors into both palette blocks (B0 and B1).
 * ================================================================ */
/* Set while a match is being played (StartMatchSequence). Gates the AGA
 * sprite-palette mirror below so it only fires in-match. */
extern volatile uint16_t _playGame;
__attribute__((used, externally_visible))
void WriteCopperPalette(const uint16_t *palette)
{
#ifdef AGA
    static const uint16_t bankValOffsets[8] = {
        COP_PAL_B0_C0_VAL_OFF, COP_PAL_B1_C0_VAL_OFF,
        COP_PAL_B2_C0_VAL_OFF, COP_PAL_B3_C0_VAL_OFF,
        COP_PAL_B4_C0_VAL_OFF, COP_PAL_B5_C0_VAL_OFF,
        COP_PAL_B6_C0_VAL_OFF, COP_PAL_B7_C0_VAL_OFF
    };
    int16_t bank;
    for (bank = 0; bank < 8; bank++)
    {
        uint8_t *dst = _copperListBuffer + bankValOffsets[bank];
        const uint16_t *src = palette + (bank * 32);
        int16_t i;
        for (i = 31; i >= 0; i--)
        {
            copper_write_word(dst, *src++);
            dst += 4; /* stride: reg+val pairs = 4 bytes */
        }
    }
    /* Mirror colours 0-15 into 16-31, but ONLY in-match (_playGame). In-match
     * players are drawn as AGA attached hardware sprites, which read colours
     * 16-31 — so they must see the same 16-colour kit palette as the playfield's
     * colours 0-15 (where ConvertTeamColor puts the kit colours). This is the AGA
     * analogue of the OCS two-block write (which stamps the 16-colour palette into
     * both blocks). The intro, menus and loading screens are 8bpp images that
     * legitimately use colours 16-31 as distinct palette entries and have no
     * hardware-sprite players, so mirroring there corrupts them. */
    if (_playGame != 0)
    {
        uint8_t *dst = _copperListBuffer + bankValOffsets[0] + 16 * 4; /* colour 16 */
        int16_t j;
        for (j = 0; j < 16; j++)
        {
            copper_write_word(dst, palette[j]);
            dst += 4;
        }
    }
#else
    uint8_t *palA = _copperListBuffer + COP_PAL_B0_C0_VAL_OFF;
    uint8_t *palB = _copperListBuffer + COP_PAL_B1_C0_VAL_OFF;
    int16_t i;
    for (i = 15; i >= 0; i--)
    {
        uint16_t color = *palette++;
        copper_write_word(palA, color);
        copper_write_word(palB, color);
        palA += 4;
        palB += 4;
    }
#endif
}

#ifdef AGA
/* ================================================================
 * WriteCopperPitchPalette (AGA only)
 *
 * Applies 9 pitch-specific color entries into gamePalette using
 * paletteIndices, then pushes the full 256-color gamePalette into
 * the copper buffer via WriteCopperPalette.
 * ================================================================ */
extern uint16_t _gamePalette[];
extern uint16_t _pitchNumberAndType;
extern const uint16_t *_pitchPalettePtrs[];
extern uint8_t _paletteIndices[];

__attribute__((used, externally_visible))
void WriteCopperPitchPalette(void)
{
    /* pitchNumberAndType packs pitch *number* in the high byte and pitch
     * *type* in the low byte. The palette lookup uses only the type, so
     * mask to 0xFF — matches asm `andi.w #$FF,d1` at SWOS2.s line 15938.
     * Without the mask, any non-zero pitch number makes the uint16 > 6
     * and the clamp below silently substitutes the default "normal"
     * (type 4) palette — symptom: HIL replays after a CAREER session
     * show normal grass even when the saved HIL specified a different
     * pitch type. Fresh boot worked only because pitch number happened
     * to be 0 then. */
    uint16_t pitchIdx = _pitchNumberAndType & 0xFF;
    const uint16_t *pitchPal;
    int16_t i;

    if (pitchIdx > 6)
        pitchIdx = 4; /* default to "normal" if out of range */

    pitchPal = _pitchPalettePtrs[pitchIdx];

    /* Apply 9 entries into gamePalette */
    for (i = 0; i < 9; i++)
    {
        uint16_t idx = _paletteIndices[i];
        _gamePalette[idx] = pitchPal[i];
    }

    WriteCopperPalette(_gamePalette);
}
#endif
