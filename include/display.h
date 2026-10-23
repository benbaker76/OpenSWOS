/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#ifndef DISPLAY_H
#define DISPLAY_H

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef short int16_t;
typedef int int32_t;

/* ==================================================================
 * AGA mode switch
 *
 * Default: AGA (8 bitplanes, 256 colors) with mixed per-bank source
 * depth — 4bpp sprite/menu/charset banks + 8bpp backgrounds/pitches,
 * auto-detected per bank at load (DetectBankPlanes). Comment out for
 * OCS (4 bitplanes, 16 colors) loading from grafs/.
 * ================================================================== */
#define AGA

/* ---- Graphics folder path ---- */
#ifdef AGA
#define GRAFS "grafs_aga/"
#else
#define GRAFS "grafs/"
#endif

/* ---- Hardware register addresses (for code that can't use struct Custom)
 * On the SDL host, swos_platform.h pre-defines CUSTOM_BASE to resolve
 * through the `custom` stub pointer so register pokes land in valid
 * memory instead of crashing on 0x00DFF000. */
#ifndef CUSTOM_BASE
#define CUSTOM_BASE         0x00DFF000u
#endif
#ifndef DMACONR_ADDR
#define DMACONR_ADDR        (CUSTOM_BASE + 0x002u)
#define VPOSR_ADDR          (CUSTOM_BASE + 0x004u)
#define COP1LC_ADDR         (CUSTOM_BASE + 0x080u)
#define COPJMP1_ADDR        (CUSTOM_BASE + 0x088u)
#define DMACON_ADDR         (CUSTOM_BASE + 0x096u)
#define INTENA_ADDR         (CUSTOM_BASE + 0x09Au)
#define INTREQ_ADDR         (CUSTOM_BASE + 0x09Cu)
#define COLOR00_ADDR        (CUSTOM_BASE + 0x180u)
#endif

/* ==================================================================
 * Amiga Custom Chip Register Map (struct for register-relative access)
 *
 * Using volatile struct Custom * gives the compiler struct layout info,
 * enabling efficient offset(An) addressing instead of absolute long.
 * ================================================================== */
struct Custom {
    uint16_t bltddat;       /* 0x000 */
    uint16_t dmaconr;       /* 0x002 */
    uint16_t vposr;         /* 0x004 */
    uint16_t vhposr;        /* 0x006 */
    uint16_t pad_008[4];    /* 0x008-0x00E */
    uint16_t adkconr;       /* 0x010 */
    uint16_t pad_012[5];    /* 0x012-0x01A */
    uint16_t intenar;       /* 0x01C */
    uint16_t intreqr;       /* 0x01E */
    uint16_t pad_020[16];   /* 0x020-0x03E */
    uint16_t bltcon0;       /* 0x040 */
    uint16_t bltcon1;       /* 0x042 */
    uint16_t bltafwm;       /* 0x044 */
    uint16_t bltalwm;       /* 0x046 */
    uint32_t bltcpt;        /* 0x048 */
    uint32_t bltbpt;        /* 0x04C */
    uint32_t bltapt;        /* 0x050 */
    uint32_t bltdpt;        /* 0x054 */
    uint16_t bltsize;       /* 0x058 */
    uint16_t pad_05A[3];    /* 0x05A-0x05E */
    uint16_t bltcmod;       /* 0x060 */
    uint16_t bltbmod;       /* 0x062 */
    uint16_t bltamod;       /* 0x064 */
    uint16_t bltdmod;       /* 0x066 */
    uint16_t pad_068[4];    /* 0x068-0x06E */
    uint16_t bltcdat;       /* 0x070 */
    uint16_t bltbdat;       /* 0x072 */
    uint16_t bltadat;       /* 0x074 */
    uint16_t pad_076[4];    /* 0x076-0x07C */
    uint16_t pad_07E;       /* 0x07E */
    uint32_t cop1lc;        /* 0x080 */
    uint32_t cop2lc;        /* 0x084 */
    uint16_t copjmp1;       /* 0x088 */
    uint16_t copjmp2;       /* 0x08A */
    uint16_t pad_08C[5];    /* 0x08C-0x094 */
    uint16_t dmacon;        /* 0x096 */
    uint16_t pad_098;       /* 0x098 */
    uint16_t intena;        /* 0x09A */
    uint16_t intreq;        /* 0x09C */
    uint16_t adkcon;        /* 0x09E */
    /* Audio channels 0xA0-0xDF, then 0xE0-0xFF display regs */
    uint16_t pad_0A0[112];  /* 0x0A0-0x17E */
    uint16_t color[32];     /* 0x180-0x1BE */
};

/* Global custom chip base pointer.
 * Must be volatile so reads/writes to HW registers aren't optimized away.
 * NOT packed — GCC generates byte-by-byte access for packed volatile structs.
 * The struct is naturally word-aligned (all Amiga custom regs are). */
extern volatile struct Custom *_custom;

/* Blitter busy, including the background undraw (ProcessSpriteBlitQueueAsync, display.c):
 * between two of its blits the blitter is idle for a moment while the blitter interrupt
 * starts the next one, so a wait on BBUSY alone could slip a blit in between and have its
 * registers overwritten. Every blitter wait goes through this. */
#if !defined(SWOS_SDL_BUILD)
extern volatile uint16_t g_blitQActive;
#define BLIT_BUSY() (g_blitQActive | (_custom->dmaconr & (1u << 14)))
#else
#define BLIT_BUSY() (_custom->dmaconr & (1u << 14))
#endif

/* ---- Blitter register offsets from CUSTOM_BASE (kept for asm compat) ---- */
#define BLTCON0_OFF         0x40
#define BLTCON1_OFF         0x42
#define BLTAFWM_OFF         0x44
#define BLTALWM_OFF         0x46
#define BLTCPT_OFF          0x48
#define BLTBPT_OFF          0x4C
#define BLTAPT_OFF          0x50
#define BLTDPT_OFF          0x54
#define BLTSIZE_OFF         0x58
#define BLTCMOD_OFF         0x60
#define BLTBMOD_OFF         0x62
#define BLTAMOD_OFF         0x64
#define BLTDMOD_OFF         0x66
#define BLTCDAT_OFF         0x70
#define BLTBDAT_OFF         0x72
#define BLTADAT_OFF         0x74
#define DMACONR_OFF         0x02
#define DMACON_OFF          0x96

/* ---- DMA / Blitter control ---- */
#define DMACON_BLT_ON               33792u
#define DMACON_BLT_OFF              1024u
#define DMACONR_BLTBUSY_BIT         6
#define BLT_FWM_ALL                 0xFFFFu
#define WORD_ALIGN_EVEN_MASK        0xFFFEu

/* ---- Blitter function code (minterm) constants ---- */
#define BLTCON0_USEA_LF_FC          0x08FC
#define BLTCON0_USED_LF_00          0x0100
#define BLTCON0_USEA_USED_LF_F0     0x09F0
#define BLTCON0_USEA_USED_LF_FC     0x09FC
#define BLTCON0_USED_LF_FF          0x01FF
#define BLTCON0_USED_LF_F0          496u
#define BLTCON0_USEA_USEB_USED_LF_FC 0x0DFC
#define BLTCON0_BASE_LINKED          0x0FCA  /* D = A ? B : C (cookie cut) */
#define BLTCON0_BASE_SINGLE          0x07CA  /* D = B (straight copy, USEA=0) */
#define BLTCON0_CLEAR_MASK           0x0B0A
#define BLTCON0_OR_MASK              0x0BFA

/* ---- Blitter queue sentinel ---- */
#define BLT_BLITQ_END               0xFFFFFFFFu

/* ==================================================================
 * Bitplane layout constants
 *
 * These are the core display parameters. NUM_PLANES is set by
 * the AGA define above — all derived values update automatically.
 * ================================================================== */

/* ---- Bitplane layout (base values) ---- */
#ifdef AGA
#define NUM_PLANES                  8
#define NUM_PLANES_SHIFT            3
#define NUM_PLANES_SHIFT_PLUS1      4
#else
#define NUM_PLANES                  4
#define NUM_PLANES_SHIFT            2
#define NUM_PLANES_SHIFT_PLUS1      3
#endif

#define NUM_PLANES_MINUS1           (NUM_PLANES - 1)
#define NUM_PLANES_MINUS2           (NUM_PLANES - 2)

/* ---- Sprite vs playfield bitplane depth (AGA) ----
 * The PLAYFIELD (pitch, menu backgrounds) is NUM_PLANES deep — 8 on AGA (256 colours),
 * 4 on OCS. SPRITES are 16-colour = 4 planes on BOTH targets. Sprite pixel/mask data is
 * therefore STORED at 4 planes and composited onto the playfield per-plane (the low 4
 * planes carry the sprite; the sprite's footprint in the upper playfield planes is CLEARED
 * so a 16-colour sprite reads back as colour 0-15). PITCH_NUM_PLANES aliases NUM_PLANES for
 * the playfield sites. The one exception is the SOCCER_S / replay-"R" bank (sprite indices
 * 0x447..0x486), which genuinely uses palette 0x70..0x7F -> 8 planes on AGA; spriteNumPlanes()
 * returns that per-sprite from the ordinal. On OCS everything is 4 planes. */
#define PITCH_NUM_PLANES              NUM_PLANES

/* ===== Sprite storage-depth toggle (SWOS_SPRITE_PLANES) =====
 *   4 = store sprites at their real 4-plane depth (16 colours). Fits the in-match sprite
 *       pool in memory, but a 4-plane bob can't single-blit onto the 8-plane pitch (the
 *       plane interleave mismatches), so it composites in several blits.
 *   8 = pad every sprite to the 8-plane playfield depth (the pre-4-plane original). One blit
 *       per bob, but the in-match pool overflows the buffer (needs the space fix).
 * Everything routes through SPRITE_NUM_PLANES / spriteNumPlanes(), so flipping this one value
 * switches the whole build between the two modes. Override at build time with
 * -DSWOS_SPRITE_PLANES=8 (no source edit needed). */
#ifndef SWOS_SPRITE_PLANES
#define SWOS_SPRITE_PLANES            4
#endif
#if (SWOS_SPRITE_PLANES != 4) && (SWOS_SPRITE_PLANES != 8)
#error "SWOS_SPRITE_PLANES must be 4 or 8"
#endif

#define SPRITE_NUM_PLANES             SWOS_SPRITE_PLANES
#if SWOS_SPRITE_PLANES == 8
#define SPRITE_NUM_PLANES_SHIFT       3    /* log2(8) */
#else
#define SPRITE_NUM_PLANES_SHIFT       2    /* log2(4) */
#endif
#define SPRITE_NUM_PLANES_SHIFT_PLUS1 (SPRITE_NUM_PLANES_SHIFT + 1)
#define SPRITE_NUM_PLANES_MINUS1      (SPRITE_NUM_PLANES - 1)
#define SPRITE_NUM_PLANES_MINUS2      (SPRITE_NUM_PLANES - 2)

#define SOCCER_S_SPRITE_FIRST         0x447
#define SOCCER_S_SPRITE_LAST          0x486
/* The menus' "Sensible World of Soccer" logo, cut whole from MENUS2. On AGA its ball
 * is redrawn in the 256 colours (tools/swosdata, setup/MENUS2_AGA.bmp), so it is
 * stored 8 planes deep like SOCCER_S. */
#define MENU_LOGO_SPRITE              0x0E0

/* The in-match RUNNING frames of both teams, the goalkeepers and the referee: 15 lines,
 * one word wide, linked (mask shared by the planes). On AGA they are stored 8 planes deep
 * -- the colour planes 0-3 and four planes of zeros -- with an 8-row mask, so a player is
 * ONE blit over the 8-plane playfield (DrawSpriteClipped) instead of eight: on a stock
 * A1200 the eight small blits, and the CPU waiting each one out, were half the frame
 * (2026-10-14, AmigaScope). This is the original game's own layout: SWOS2.s draws a bob
 * in one blit of NUM_PLANES rows a line. The rest of the sprites (tackles, headers,
 * falls ...) stay 4 planes deep: converting them would cost ~130 KB more chip RAM.
 * Whether a frame is here only decides its storage and speed, never its pixels: every
 * reader takes the depth from spriteNumPlanes, and the drawing falls back to the
 * per-plane path for anything not stored 8 deep with a linked mask. */
static inline int16_t spriteIsPadded8(int16_t ordinal)
{
#if defined(AGA) && !defined(SWOS_SDL_BUILD) && (SWOS_SPRITE_PLANES != 8)
    static const uint16_t runs[][2] = {
        { 0x0E3, 0x100 }, { 0x148, 0x165 }, { 0x1AD, 0x1CA }, { 0x212, 0x22F },
        { 0x277, 0x294 }, { 0x2DC, 0x2F9 }, { 0x341, 0x358 }, { 0x37B, 0x392 },
        { 0x3B5, 0x3CC }, { 0x3EF, 0x406 },
    };
    uint16_t o = (uint16_t)ordinal, i;
    if (o < 0x0E3 || o > 0x406)
        return 0;
    for (i = 0; i < sizeof runs / sizeof runs[0]; i++)
        if (o >= runs[i][0] && o <= runs[i][1])
            return 1;
#else
    (void)ordinal;
#endif
    return 0;
}

/* Real stored bitplane depth of a sprite by its ordinal (sprite index). In 4-plane mode:
 * 8 for the AGA soccer_s/replay-R bank (genuinely 8-plane) and for the running frames
 * above (padded), else 4. In 8-plane mode every sprite is padded to 8 so the special
 * cases are no-ops. OCS: always 4. Used by every sprite-DATA reader so a 4-plane sprite
 * is walked at 4-plane stride, not the playfield's 8. */
static inline uint16_t spriteNumPlanes(int16_t ordinal)
{
#if defined(AGA) && !defined(SWOS_SDL_BUILD) && (SWOS_SPRITE_PLANES != 8)
    if ((uint16_t)ordinal >= SOCCER_S_SPRITE_FIRST &&
        (uint16_t)ordinal <= SOCCER_S_SPRITE_LAST)
        return PITCH_NUM_PLANES;   /* SOCCER_S bank is genuinely 8-plane */
    if ((uint16_t)ordinal == MENU_LOGO_SPRITE)
        return PITCH_NUM_PLANES;   /* the logo's AGA ball uses the 256 colours */
    if (spriteIsPadded8(ordinal))
        return PITCH_NUM_PLANES;   /* a running frame: 4 colour planes + 4 of zeros */
#else
    (void)ordinal;
#endif
    return SPRITE_NUM_PLANES;      /* 4 (default) or 8 (SWOS_SPRITE_PLANES=8) */
}

/* Frontbuffer circular scroll-buffer plane-row width (bytes = px/8).
 * AGA (FMODE=1) needs 48 B (384 px) not 44 B (352 px): FMODE=1's 32-bit fetch
 * forces a 32px-aligned bitplane pointer, so the fine scroll uses a 32px quantum
 * whose left-prime (for frac 17..31, the BPLCON1 H7 render) reads a full 32px
 * block before the visible edge. A 352px circular buffer (336 visible + 16 slack)
 * is too narrow — ScrollX's incoming right-edge write wraps into that prime region
 * and corrupts it during scroll (the "empty column" bug). 384px gives the prime a
 * whole extra 32px FMODE block of its own. OCS (FMODE=0 / 16px pointers / 16px
 * quantum) fits the 16px slack, so it stays 44. See docs/AGA.md §11. */
/* AGA (FMODE=1) = 48 B (384px); OCS = 44 B (352px). FMODE=1's 32-bit fetch forces
 * a 32px-aligned bitplane pointer, so the 0..31px fine scroll must come entirely
 * from BPLCON1 — and that needs a full 32px of FETCHED margin the 336px display
 * doesn't include. A 352px buffer/window has only 16px of slack; 384px gives the
 * 32px margin (336 visible + 32 fine-scroll + 16 blit slack). The fetch (DDFSTRT/
 * DDFSTOP in display.c) and ScrollX's leading-edge blit (scroll_blit.c) both widen
 * to keep that margin current during the incremental scroll — the piece the earlier
 * 384px attempt missed. See docs/AGA.md §11. */
/* AGA verge margin: 4 bytes (32px) of LEFT margin per plane row, holding the
 * repeated leftmost pitch column so the FMODE=1 fine-scroll -4 pull-back has valid
 * data to read at the pitch's extreme-left edge (the first coarse block). Without
 * it the first block can't use the -4 smoothing -> the ~1-tile-in "jump back to
 * the start". PLANE_STRIDE = FMODE_FETCH_BYTES(48 data) + margin(4) = 52. The data
 * lives at byte SCROLL_LEFT_MARGIN_BYTES..PLANE_STRIDE; the margin at byte 0.. */
/* 2026-10-10: the margin is GONE (0) and the AGA plane row is the 48 bytes the display
 * fetches. It existed because the FMODE=1 display stood 16px left of the camera, so at
 * the left touchline 16px of off-pitch memory were on screen. The display now starts
 * at the camera (AGA_PITCH_DISPLAY_SHIFT_PX, display.c): the long fetched to the left
 * stays under the fine-scroll delay, and what it holds is never seen. The name is kept
 * (as 0) where drawing code adds it. */
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
#define SCROLL_LEFT_MARGIN_BYTES    0
#define PLANE_STRIDE                48
#else
#define SCROLL_LEFT_MARGIN_BYTES    0
#define PLANE_STRIDE                44
#endif
/* cameraXMod320 (px) wraps at the plane-row pixel width = PLANE_STRIDE*8
 * (384 AGA / 352 OCS). Was a hardcoded 352 in camera.c / display.c. */
#define SCROLL_X_WRAP               (PLANE_STRIDE * 8)
/* AGA FMODE=1 fine-scroll (frac 17..31 / BPLCON1 H7) reads a 32px "prime" block
 * just LEFT of the visible window. ScrollX pre-blits this margin so it stays
 * current during scroll (see scroll_blit.c). Fits the 384px buffer's 48px slack
 * (336 visible + 32 prime = 368 < 384). Must be a whole tile multiple (16). */
#define SCROLL_LEFT_PRIME_PX        32
#define PLANE_STRIDE_MINUS2         (PLANE_STRIDE - 2)      /* 46 AGA / 42 OCS */
#define PLANE_STRIDE_MINUS4         (PLANE_STRIDE - 4)      /* 40 */
#define PLANE_STRIDE_DIV2           (PLANE_STRIDE >> 1)     /* 22 */
#define ROW_STRIDE_BYTES            (PLANE_STRIDE * NUM_PLANES)
#define SRC_SCANLINE_BYTES          (NUM_PLANES * 2)

/* ---- Bitplane layout (multiples) ---- */
#define PLANE_STRIDE_TIMES2         (PLANE_STRIDE * 2)      /* 88 */
#define PLANE_STRIDE_TIMES3         (PLANE_STRIDE * 3)      /* 132 */
#define PLANE_STRIDE_TIMES4         (PLANE_STRIDE * 4)      /* 176 */
#define PLANE_STRIDE_TIMES5         (PLANE_STRIDE * 5)      /* 220 */
#define PLANE_STRIDE_TIMES6         (PLANE_STRIDE * 6)      /* 264 */
#define PLANE_STRIDE_TIMES7         (PLANE_STRIDE * 7)      /* 308 */

/* ---- Image stream layout ---- */
#define IMAGE_ROW_STRIDE_BYTES      (BITPLANE_ROW_BYTES * NUM_PLANES)
#define BITPLANE_ROW_BYTES          40

/* ---- Screen / display dimensions ---- */
#define SCREEN_WIDTH                336
#define SCREEN_HEIGHT               272

/* The DOS release's screen: 320x200, mode 13h. Its camera clamps fall out of the
 * f(view) macros below at this height (SIM_CAMERA_MAX_Y = 664 = swos.asm:7539). */
#define DOS_VIEW_WIDTH                  320
#define DOS_VIEW_HEIGHT                 200


/* ---- Source-image sprite-sheet height ----
 * The Amiga sprite-source banks (charset, cjc*, team kits, soccer_s) are a
 * fixed 320x256 planar sheet: BITPLANE_ROW_BYTES (40) per plane row, this
 * many rows. Full-screen overlays instead use PLANE_STRIDE x SCREEN_HEIGHT. */
#define SPRITE_SHEET_ROWS           256

/* ==================================================================
 * Per-bank bitplane depth auto-detect (mixed-depth AGA support)
 *
 * A loaded bank is a full planar image; its RNC-decompressed byte size
 * (the value LoadFileEx reports via outFileSize) divided by one plane's
 * byte size (planeRowBytes * rows) yields the plane count. This lets a
 * 4bpp sprite sheet and an 8bpp background flow through the same decode
 * loop with no per-file header — the depth is inferred from the size,
 * mirroring SwosGfx's -bitplanes=auto. Falls back to the compile-time
 * NUM_PLANES on any size that isn't an exact 1..8-plane multiple.
 *
 * Used on BOTH targets: SDL decodes bitplanes -> chunky; the Amiga build
 * uses it so blitDispatch stores each sprite at its real depth (4bpp
 * players get zero-padded to 8 planes so the low nibble is a 16-colour
 * hardware sprite — see docs/AMIGA_SPRITES.md).
 * ================================================================== */
static inline uint16_t DetectBankPlanes(uint32_t byteSize, uint32_t planeRowBytes, uint32_t rows)
{
    uint32_t planeBytes = planeRowBytes * rows;
    uint32_t planes = planeBytes ? byteSize / planeBytes : 0u;
    if (planes < 1u || planes > 8u || planes * planeBytes != byteSize)
        return (uint16_t)NUM_PLANES;
    return (uint16_t)planes;
}

/* ---- Copper list layout ----
 *
 * OCS: 320 bytes (16 colors x 2 palette blocks)
 * AGA: 1316 bytes (256 colors via 8 banks x 32 colors)
 *
 * The register control section (sprite ptrs, BPLCON, modulos, DDFSTRT,
 * DIWSTRT) is at the same offsets in both templates. Only the palette
 * section and BPL pointer blocks differ.
 */
#ifdef AGA
#define COPPERLIST_SIZE_BYTES       1316
#define NUM_PAL_BANKS               8
#define COLORS_PER_BANK             32
#else
#define COPPERLIST_SIZE_BYTES       320
#define NUM_PAL_BANKS               2
#define COLORS_PER_BANK             16
#endif

/* Copper byte offsets — shared between OCS and AGA (same position) */
#define COP_SPR_B0_OFF              0x04
#define COP_BPLCON1_VAL_OFF         0x4A
#define COP_BPL1MOD_VAL_OFF         0x5A
#define COP_BPL2MOD_VAL_OFF         0x5E
#define COP_DDFSTRT_VAL_OFF         0x62
#define COP_DDFSTOP_VAL_OFF         0x66
#define COP_FMODE_VAL_OFF           0x52    /* AGA template only */
#define COP_DIWSTRT_VAL_OFF         0x6A    /* AGA template only */
#define COP_DIWSTOP_VAL_OFF         0x6E    /* AGA template only */

/* Copper byte offsets — differ between OCS and AGA */
#ifdef AGA
/* AGA: 8 palette banks, each preceded by BPLCON3 bank-select word pair */
#define COP_PAL_B0_C0_OFF          0x74
#define COP_PAL_B0_C0_VAL_OFF      0x76
#define COP_PAL_B1_C0_OFF          0xF8
#define COP_PAL_B1_C0_VAL_OFF      0xFA
#define COP_PAL_B2_C0_OFF          0x17C
#define COP_PAL_B2_C0_VAL_OFF      0x17E
#define COP_PAL_B3_C0_OFF          0x200
#define COP_PAL_B3_C0_VAL_OFF      0x202
#define COP_PAL_B4_C0_OFF          0x284
#define COP_PAL_B4_C0_VAL_OFF      0x286
#define COP_PAL_B5_C0_OFF          0x308
#define COP_PAL_B5_C0_VAL_OFF      0x30A
#define COP_PAL_B6_C0_OFF          0x38C
#define COP_PAL_B6_C0_VAL_OFF      0x38E
#define COP_PAL_B7_C0_OFF          0x410
#define COP_PAL_B7_C0_VAL_OFF      0x412
#define COP_BPLPTR_BLOCKA_OFF      0x498
#define COP_BPLPTR_BLOCKB_OFF      0x4E0
#define COP_WAIT_VHPOS             0x4D8
#define COP_WAIT_WAIT              0x4DC
#else
/* OCS: 2 palette blocks of 16 colors, no bank-select pairs */
#define COP_PAL_B0_C0_VAL_OFF      0x72
#define COP_PAL_B1_C0_VAL_OFF      0xB2
#define COP_BPLPTR_BLOCKA_OFF      0xF4
#define COP_BPLPTR_BLOCKB_OFF      0x11C
#define COP_WAIT_VHPOS             0x114
#define COP_WAIT_WAIT              0x118
#endif

/* ---- Frontbuffer layout ----
 * Amiga: bitplane-interleaved, NUM_PLANES planes of PLANE_STRIDE bytes per
 * scanline, 176 bytes/row for OCS.
 * SDL: 8bpp chunky, 1 byte per pixel = palette index (0..15 for OCS-style
 * palette). Row stride = SCREEN_WIDTH. Sprites store the same format
 * (byte = palette index, 0 = transparent). */


#ifdef SWOS_SDL_BUILD
/* Widescreen: the frontbuffer STRIDE (= visible width) is RUNTIME — _screen_width is
 * 336 (normal, == SCREEN_WIDTH) or WIDESCREEN_WIDTH (widescreen), set once at startup
 * from gWidescreen. The buffers are ALLOCATED at the max stride so their array size
 * stays a link-time constant (FRONTBUFFER_ALLOC_TOTAL_BYTES); only the row-stride
 * arithmetic is runtime. SCREEN_WIDTH stays the 336 logical/centered width (menus). */
extern int _screen_width;
/* The visible HEIGHT, runtime for the same reason the width is: the DOS release
 * draws 320x200 where the Amiga draws 336x272, and "100% DOS" has to mean the
 * view too -- every camera constant below is already f(view) and yields the DOS
 * numbers at 200 (SIM_CAMERA_MAX_Y = 664, which is swos.asm:7539's
 * `mov word ptr D2, 664`). Buffers stay ALLOCATED at 272 (the max), so only the
 * line counts are runtime; SCREEN_HEIGHT remains the allocation/menu height.
 * docs/DOS_VIEW.md. */
extern int _screen_height;
/* _screen_width (the live render width) is derived at startup from the monitor's
 * aspect ratio (main.c), so the picture fills any display with no bars. The buffer is
 * ALLOCATED at the MAX useful width = the full pitch world width (the view never
 * usefully exceeds the pitch; the camera clamp keys off the same 0x2A0). */
#define WIDESCREEN_WIDTH                480   /* 16:9 reference (1.76); the width is now adaptive */
#define PITCH_VIEW_MAX_WIDTH            672   /* = 0x2A0, pitch world width = max render width */
#define FRONTBUFFER_MAX_STRIDE_BYTES    PITCH_VIEW_MAX_WIDTH
#define FRONTBUFFER_ROW_STRIDE_BYTES    _screen_width
#else
/* Amiga has no widescreen: _screen_width folds to the fixed visible width at
 * compile time, so the synced per-site culls/camera math are identical to before. */
#define _screen_width                  SCREEN_WIDTH
#define _screen_height                 SCREEN_HEIGHT
#define FRONTBUFFER_MAX_STRIDE_BYTES    ROW_STRIDE_BYTES
#define FRONTBUFFER_ROW_STRIDE_BYTES    ROW_STRIDE_BYTES
#endif

/* ---- The SIMULATION view --------------------------------------------------
 * SWOS's gameplay reads the view size, not just the renderer: the `onScreen`
 * cull decides which players auto-selection may pick (GAMEPLAY.md §5, §25.2),
 * and every camera constant is derived from the view —
 *
 *      kick-off centre X   (672 - viewW) / 2      168 at 336, 176 at 320
 *      bottom start Y      664 - (viewH - 200)    592 at 272, 664 at 200
 *      camera centre Y     viewH / 2              136 at 272, 100 at 200
 *      pan range           672 - viewW            336 at 336, 352 at 320
 *      target Y clamp      664 - (viewH - 200)    592 at 272, 664 at 200
 *      position Y clamp    880 - viewH            608 at 272, 680 at 200
 *                                                 (= swos-port's kCameraMaxY)
 *
 * — which is why the port's hard-coded 168/592/0x88/0x250 are all f(336, 272),
 * and why the same formulas reproduce the DOS release's own constants at
 * (320, 200). Keeping that size separate from the RENDER width lets the
 * recorded-game replay rig (docs/TESTS.md) simulate at the geometry a
 * recording was made in while still drawing at whatever the display wants.
 *
 * Default: exactly today's behaviour — the live render width and the fixed
 * 272 line count — so nothing changes for a player unless something sets it.
 * (GAMEPLAY.md §25.4 notes that pinning this to 336x272 regardless of the
 * render width is the fix for widescreen changing CPU player selection; that
 * is a deliberate gameplay change and is NOT what the default does.) */
#ifdef SWOS_SDL_BUILD
extern int16_t _sim_view_width;
extern int16_t _sim_view_height;
#else
/* Amiga has no widescreen and no replay rig, so the simulation view IS the
 * hardware view — fold to compile-time constants and the m68k codegen for
 * every site below is byte-for-byte what it was before this existed. */
#define _sim_view_width                 SCREEN_WIDTH
#define _sim_view_height                SCREEN_HEIGHT
#endif

/* ---- The SIMULATION DRAW width ----------------------------------------------
 * The simulation view above is 336 whatever the display. But a few things the match
 * decides depend on what is DRAWN, and so on how wide the view is: which sprites in a
 * widescreen margin animate (sprites.c, f1d348c -- drawn, so they must, or they
 * slide), which crowd tiles toggle (animated_patterns.c, which also calls Rand), which
 * corner flags are placed (adverts.c) and when the bench is populated (bench_subs.c).
 * They read THIS width, never _screen_width.
 *
 * Locally it IS the render width (main.c), so a player sees exactly what f1d348c
 * gave them. The replay rig pins it to 336 -- a recording was made on a display with
 * no margin -- unless --replay-sim-width says otherwise. And a netplay session sets
 * it to one width both peers agreed (the wider of the two, P2PLink_AgreedSimWidth):
 * two players on a 4:3 and a 16:9 screen then play the same match, the narrower one
 * animating a margin it does not show. docs/NETPLAY.md, "One simulation width". */
#ifdef SWOS_SDL_BUILD
extern int32_t _sim_draw_width;
#else
#define _sim_draw_width                 SCREEN_WIDTH
#endif

/* The camera constants above, as functions of the simulation view. */
#define SIM_CAMERA_CENTRE_X   ((int16_t)((0x2A0 - _sim_view_width) / 2))
#define SIM_CAMERA_PAN_RANGE  ((int16_t)(0x2A0 - _sim_view_width))
#define SIM_CAMERA_CENTRE_Y   ((int16_t)(_sim_view_height / 2))

/* The view the camera AIMS with, which is not always the one we draw.
 *
 * MoveCamera is the same routine in both releases; what makes the DOS camera feel
 * different is that every constant is derived from a 320x200 view rather than 336x272 --
 * the ball is held 100 lines from the top instead of 136. In DOS mode the camera aims
 * with the DOS view so its MOVEMENT matches, while the clamps stay on the geometry this
 * port can actually draw (a 272-line render driven by the DOS Y clamp of 664 would scroll
 * past the bottom of the pitch). The replay rig already sets _sim_view_* to the
 * recording's own 320x200, so a DOS recording is unaffected: the two agree there. */
#ifdef SWOS_SDL_BUILD
#define SIM_CAM_AIM_WIDTH     ((int16_t)(_gDosGameplay ? 320 : _sim_view_width))
#define SIM_CAM_AIM_HEIGHT    ((int16_t)(_gDosGameplay ? 200 : _sim_view_height))
#else
/* Amiga: one geometry, folded at compile time -- the m68k codegen is unchanged. */
#define SIM_CAM_AIM_WIDTH     SCREEN_WIDTH
#define SIM_CAM_AIM_HEIGHT    SCREEN_HEIGHT
#endif
/* The half-time / full-time RESULT PANEL's vertical anchor — ShowResult's 246.
 *
 * This is NOT a DOS/Amiga constant pair: swos.asm ShowResult adds the same 246,
 * the same +10000/z=10000, the same -1015/z=9000 grid chain and the same +32 row
 * pitch that SWOS2.s does, and the two releases' sprite metadata is identical
 * (spr 212 centerY=5, spr 138 centerY=0, measured in both tiers). Only X differs
 * (the DOS grid's right half sits at 152 = 320-168 where the Amiga's is at 168).
 *
 * It is one constant expressed against the view -- exactly the case
 * docs/DOS_GAMEPLAY.md's proof-standard section warns about. Working the z's
 * through, the panel's top grid row lands at `P + d7 - 15` and the banner at
 * `P + d7 - 30`. At 272 with P=246 that is 217 and 202, which is what the game
 * measurably draws. The panel is 96 rows tall (3 rows x 32) and is MEANT to run
 * off the bottom, showing its top 55 rows; anchoring P to the view keeps exactly
 * that much visible at any height instead of pushing the whole thing off a
 * 200-line screen (ReportBot 0007: "the scoreboard is not fully visible").
 *
 * 246 at 272 lines -- byte-identical to the literal it replaces -- and 174 at 200. */
#define SIM_RESULT_PANEL_Y    ((int16_t)(_sim_view_height - 26))
/* How much taller the PICTURE is than the view the match is simulated in: 0 except in
 * the DOS view with widescreen on (320x200 simulated, 272 lines shown), where it is 72.
 * A HUD element anchored to the BOTTOM of the screen adds it (the result panel, the
 * name bar's bottom corners); one anchored to the top does not. Drawing only. */
#ifdef SWOS_SDL_BUILD
#define RENDER_EXTRA_Y        ((int16_t)(_screen_height > _sim_view_height ? \
                                         _screen_height - _sim_view_height : 0))
#else
#define RENDER_EXTRA_Y        0
#endif

/* The camera DESTINATION clamp (where MoveCamera may aim). */
#define SIM_CAMERA_MAX_Y      ((int16_t)(664 - (_sim_view_height - 200)))
#define SIM_CAMERA_MAX_Y_TRN  ((int16_t)(616 - (_sim_view_height - 200)))
/* The camera POSITION clamp — keeps the view inside the 880-tall pitch. 0x260
 * at 272 lines; swos-port's kCameraMaxY = 680 at 200. */
#define SIM_CAMERA_POS_MAX_Y  ((int16_t)(880 - _sim_view_height))
/* "The camera has arrived at the bench view" — MoveCamera's subs-menu test,
 * camera Y within +/-10 of where the bench target (449) sits once centred.
 * 303..323 at 272 lines, and 339..359 at 200 — which is exactly what the DOS
 * release's own MoveCamera compares against, so this is one more f(view)
 * constant rather than a platform difference. */
/* Where ActivateReferee parks the referee when he enters from BELOW the view:
 * `addi.w #287,d2` on the Amiga (SWOS2.s:15172) against `add word ptr D2, 215`
 * on the DOS build (swos.asm:102740). 287 - 215 = 72 = the view-height
 * difference, so it is the bottom edge plus 15, not a literal. (The sibling
 * `- 20` for entering from ABOVE is the top edge and is 20 in both.) */
#define SIM_REFEREE_ENTRY_BELOW ((int16_t)(_sim_view_height + 15))

/* The camera Y that puts the bench (449) in the middle of the view: 313 at
 * 272 lines, 349 at 200 — which is what `ViewHighLightsMain` parks the camera
 * at before the first highlight (`move.w #313,d2`, SWOS2.s:20826, against
 * `mov word ptr D2, 349`, swos.asm:9090). */
#define SIM_CAMERA_BENCH_Y    ((int16_t)(0x1C1 - SIM_CAMERA_CENTRE_Y))
#define SIM_CAMERA_BENCH_Y_LO ((int16_t)(SIM_CAMERA_BENCH_Y - 10))
#define SIM_CAMERA_BENCH_Y_HI ((int16_t)(SIM_CAMERA_BENCH_Y + 10))

#define FRONTBUFFER_VISIBLE_LINES       SCREEN_HEIGHT
#define FRONTBUFFER_TOTAL_LINES         (SCREEN_HEIGHT + 1)
/* AGA block-B elimination: ONLY the two display frontbuffers (base/alt) are 2x
 * tall. Rows [SCREEN_HEIGHT, 2*SCREEN_HEIGHT) MIRROR rows [0, SCREEN_HEIGHT), so
 * the copper's single block A displays all SCREEN_HEIGHT lines starting at any
 * cameraYMod320 CONTIGUOUSLY — no mid-screen 8-pointer reload, hence no block-B
 * fetch race under FMODE=1. The mask, pitch buffer, and every FRONTBUFFER_*
 * size macro stay 1x (so their memsets/copies and PITCHFILE_MAX_SIZE are
 * unchanged). The scroll blits double-write each row into the mirror at offset
 * FB_MIRROR_OFFSET_BYTES. See docs/AGA.md section 12. */
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
/* +2, not +1: the menus' mask buffer lives in the second buffer's mirror half from row
 * SCREEN_HEIGHT+1 on, FRONTBUFFER_TOTAL_LINES rows (swos_globals.c). */
#define FRONTBUFFER_DISPLAY_LINES       (2 * SCREEN_HEIGHT + 2)
#define FB_MIRROR_OFFSET_BYTES          (SCREEN_HEIGHT * ROW_STRIDE_BYTES)
#else
#define FRONTBUFFER_DISPLAY_LINES       (SCREEN_HEIGHT + 1)
#endif
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
/* Where a bob's background is undrawn FROM: its twin row in the other half of the 2x-tall
 * buffer (2026-10-14; it was the separate 1x mask buffer). Nothing but the pitch is ever
 * in a twin: every bob, text and box is drawn inside the window [cameraYMod320, +272),
 * whose twins all lie outside it, and everything that draws the pitch -- the vertical
 * scroll's rows, the column of a horizontal one (copied at once), the crowd's tiles --
 * writes both twins. So the twin of the rows a bob covers IS the clean pitch under it,
 * and the mask, kept up to date beside the twins, was a third copy of every scroll. */
#define undrawTwin(dest) \
    ((uint32_t)(dest) - (uint32_t)_frontbufferBasePtr < (uint32_t)FB_MIRROR_OFFSET_BYTES \
        ? (uint32_t)(dest) + (uint32_t)FB_MIRROR_OFFSET_BYTES \
        : (uint32_t)(dest) - (uint32_t)FB_MIRROR_OFFSET_BYTES)
#endif
#define FRONTBUFFER_SIZE_VISIBLE_BYTES  (FRONTBUFFER_ROW_STRIDE_BYTES * FRONTBUFFER_VISIBLE_LINES)
#define FRONTBUFFER_SIZE_TOTAL_BYTES    (FRONTBUFFER_ROW_STRIDE_BYTES * FRONTBUFFER_TOTAL_LINES)
/* Compile-time max allocation (max stride) for the frontbuffer array declarations. */
#define FRONTBUFFER_ALLOC_TOTAL_BYTES   (FRONTBUFFER_MAX_STRIDE_BYTES * FRONTBUFFER_TOTAL_LINES)
/* Allocation for the two DISPLAY frontbuffers (base/alt) only — 2x tall on AGA
 * for the block-B mirror, 1x elsewhere. */
#define FRONTBUFFER_DISPLAY_ALLOC_BYTES (FRONTBUFFER_MAX_STRIDE_BYTES * FRONTBUFFER_DISPLAY_LINES)
#define FB_COPY_WORDS_MINUS1            ((FRONTBUFFER_SIZE_TOTAL_BYTES >> 1) - 1)
#define FB_BLT_MOD_AFTER_1_WORD         (FRONTBUFFER_ROW_STRIDE_BYTES - 2)

/* Widescreen menu centering: 336-wide menu content is drawn centered in the wider
 * frame by shifting the frontbuffer write base by WIDE_MARGIN. _menu_draw_x_offset is
 * set to WIDE_MARGIN only around the menu entry-draw loop (so the full-width panel
 * background and the in-match pitch stay at offset 0). 0 / WIDE_MARGIN=0 on Amiga. */
#define WIDE_MARGIN                     ((_screen_width - SCREEN_WIDTH) / 2)
extern int _menu_draw_x_offset;

/* In-match HUD box: the score panel at the bottom and the controlled-player
 * name + clock at the top-left span the DISPLAY rather than the 336-wide
 * (4:3) region, but are capped at the 16:9 width so on an ultrawide they do
 * not sit at the far edges. Aligning them to 4:3 kept them near the middle,
 * where the score panel obstructs the view when attacking the goal with fast
 * long balls.
 *
 *   <= 16:9   HUD_WIDTH == _screen_width, HUD_MARGIN == 0  -> fills the screen
 *   ultrawide HUD_WIDTH == WIDESCREEN_WIDTH (480, the 16:9 reference)
 *             centred, HUD_MARGIN == the leftover / 2
 *   Amiga     _screen_width folds to 336 -> HUD_MARGIN == 0, unchanged.
 *
 * Only LEFT/RIGHT-anchored HUD elements need this. Anything centred in the old
 * box already resolves to screen centre (WIDE_MARGIN + 168 == _screen_width/2),
 * so the team names, score digits and dash are left alone. */
#ifdef SWOS_SDL_BUILD
#define HUD_WIDTH                       (_screen_width < WIDESCREEN_WIDTH \
                                         ? _screen_width : WIDESCREEN_WIDTH)
#define HUD_MARGIN                      ((_screen_width - HUD_WIDTH) / 2)
/* The same box for the simulation draw width (_sim_draw_width), for the one HUD
 * decision that is match state: whether the extra tile is in the draw list. */
#define SIM_HUD_WIDTH                   (_sim_draw_width < WIDESCREEN_WIDTH \
                                         ? _sim_draw_width : WIDESCREEN_WIDTH)
#else
/* Amiga has no widescreen (WIDESCREEN_WIDTH is SDL-only), so fold both to
 * compile-time constants — byte-identical geometry to before. */
#define HUD_WIDTH                       SCREEN_WIDTH
#define HUD_MARGIN                      0
#define SIM_HUD_WIDTH                   SCREEN_WIDTH
#endif

/* ---- Tile / scroll-blit constants ---- */
#define TILE_STRIDE                 14
#define TILE_SHIFT_X                4
#define TILE_HEIGHT                 16
#define TILE_SIZE                   (TILE_HEIGHT * SRC_SCANLINE_BYTES)
#define TILE_SIZE_SHIFT             (NUM_PLANES_SHIFT_PLUS1 + TILE_SHIFT_X)  /* log2(TILE_SIZE): 7 for OCS, 8 for AGA */
#define MAX_FAN_TILE_INDEX          22
#define MAX_FAN_TILE_OFFSET         (MAX_FAN_TILE_INDEX * TILE_SIZE)
#define TILE_MASK                   15
/* Pitch-MAP source row stride: 42 tile-columns x 4-byte pointer = 168.
 * This is a property of the pitch map (42x55 tiles), NOT the frontbuffer plane
 * row — it must stay 168 even when PLANE_STRIDE widens to 48 for AGA. (It
 * coincidentally equalled PLANE_STRIDE_MINUS2*4 while PLANE_STRIDE was 44.) */
#define PITCH_MAP_COLS              42
#define PITCH_PTR_ROW_BYTES         (PITCH_MAP_COLS * 4)
/* ScrollY dest row-advance modulo = ROW_STRIDE - (bytes written per scroll row).
 * The Y row copy writes a FIXED 22 words (unaligned) / 21 words (aligned) = 44/42
 * bytes — that's the 336px visible pitch in 16px tile-columns (+1 partial), a
 * property of the display width, NOT PLANE_STRIDE. It only coincidentally equalled
 * ROW_STRIDE-PLANE_STRIDE while PLANE_STRIDE was 44; with the AGA widen to 48 that
 * left each row 4 bytes short → Y-scroll skew. Keep the literal 44/42. */
#define SCROLL_ROW_BYTES_UNALIGNED  44
#define SCROLL_ROW_BYTES_ALIGNED    42
#define PITCH_DEST_MOD_UNALIGNED    (ROW_STRIDE_BYTES - SCROLL_ROW_BYTES_UNALIGNED)
#define PITCH_DEST_MOD_ALIGNED      (ROW_STRIDE_BYTES - SCROLL_ROW_BYTES_ALIGNED)
/* FMODE=1 (32-bit / BPL32) bitplane fetch for the 8bpl-lores AGA pitch.
 * The pitch is read as 12 fetches x 32px = 384px = 48 bytes per plane per
 * scanline = the FULL plane row (DDFSTRT=0x20, DDFSTOP=0xD0), so the modulo to
 * reach the next scanline's plane is ROW_STRIDE - 48 = 336. This MATCHES the
 * ScrollY fill, which already writes 48 B/plane. (A previous revision fetched
 * only 11 longs / 44 B / modulo 340; that under-fetched, leaving the leftmost
 * (delay-32)px of each scrolled frame BEFORE the fetch pointer -> they showed
 * background colour 0 = the persistent "empty far-left column". 2026-07-14: root-
 * caused via live copper/geometry read + RandyAGA.txt LONG-burst rule, fixed by
 * fetching the 12th long AND pointing the fetch one long earlier so that extra
 * long lands under the H6/H7 fine-scroll delay — see display.c UpdatePlanePtrs.)
 * The coarse cameraX byte offset and the tile-blit dest must both be 4-byte
 * (32px) aligned so the 32-bit fetch pointer is 32-bit aligned; the residual
 * 0..31px is applied via BPLCON1's AGA extended playfield-scroll bits (H6/H7).
 * AGA (8bpl) only; OCS keeps 16px. Copper is Amiga-only, so on SDL these are
 * inert. */
#define FMODE_FETCH_BYTES           48

/* AGA_FMODE: the bitplane fetch mode of the MATCH pitch (2026-10-14). 3 = 64-bit fetch (64px
 * unit), the default since 2026-10-16: the 8-plane display takes half the chip-bus slots it
 * does at 1 (48 a line instead of 96), so the blitter and the CPU get more -- about a tenth of
 * the frame, measured. A 64-bit fetch must start a whole unit before the first visible pixel
 * and cannot start before DDFSTRT $18, so the window is 320px, hpos $81-$1C1 -- the width of
 * the DOS release -- and the hardware sprites past sprite 0 are lost, so every sprite is a bob.
 * Every scroll position stays reachable (BPLCON1 scrolls a whole 64px unit). 1 = BPL32 (32px
 * unit), the 336px window the AGA build had before: `make -f Makefile.amiga
 * EXTRA_CFLAGS=-DAGA_FMODE=1`. Menus keep FMODE=1 either way. AGA_Fm3Active() (display.c) is
 * true while the match display is the FMODE=3 one. */
#ifndef AGA_FMODE
#define AGA_FMODE 3
#endif
#if defined(AGA) && !defined(SWOS_SDL_BUILD) && AGA_FMODE == 3
int16_t AGA_Fm3Active(void);
#define AGA_FM3()                   AGA_Fm3Active()
#else
#define AGA_FM3()                   0
#endif
#define AGA_FM3_WINDOW_LEFT_PX      16      /* the 320px window starts 16px into the 336px one */
/* ... but shows the pitch 8px further left than that: the 336px picture centred, 8px off each
 * side, as the DOS release's 320px screen does (render only -- _cameraX, and so what the game
 * counts as on screen, is untouched). That moves everything drawn on the pitch, the HUD
 * included, 8px right on screen; AGA_FM3_HUD_SHIFT_PX moves the HUD (screen-pinned sprites,
 * z >= 8000) further still. */
#define AGA_FM3_VIEW_SHIFT_PX       8
#define AGA_FM3_HUD_SHIFT_PX        0
#define PITCH_DEST_MOD_FMODE        (ROW_STRIDE_BYTES - FMODE_FETCH_BYTES)
#ifdef AGA
#define SCROLL_X_COARSE_ALIGN_MASK  0xFFFCu   /* 4-byte / 32px for FMODE=1 */
#else
#define SCROLL_X_COARSE_ALIGN_MASK  0xFFFEu   /* 2-byte / 16px (OCS) */
#endif
#define BLTDMOD_PLANE_STEP          PLANE_STRIDE_MINUS2
#define BLTSIZE_TILE_COL            (((TILE_HEIGHT * NUM_PLANES) << 6) | 1)
#define BLTSIZE_COL_16LINES_ALLPLANES  BLTSIZE_TILE_COL

/* ---- Panel overlay ---- */
#define PANEL_HEIGHT_LINES          30
#define BLTSIZE_OVERLAY_PANEL       (((PANEL_HEIGHT_LINES * NUM_PLANES) << 6) | PLANE_STRIDE_DIV2)
#define BLTSIZE_OVERLAY_TO_SCREEN   ((SCREEN_HEIGHT << 6) | PLANE_STRIDE_DIV2)

/* ---- Sprite blit ---- */
#define SPRITE_HEIGHT_LINES         15
#define SPRITE_BLIT_HEIGHT_LINES    (SPRITE_HEIGHT_LINES * NUM_PLANES)
#define SPRITE_BLTSIZE_W1           ((SPRITE_BLIT_HEIGHT_LINES << 6) | 1)
#define SPRITE_BLTSIZE_W2           ((SPRITE_BLIT_HEIGHT_LINES << 6) | 2)

/* ---- File info ---- */
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
/* Dedicated pitch buffer, in chip RAM (the blitter scrolls the pitch from it). The
 * DOS pitches (SWCPICHn.MAP) are <=78 KB; SWOS 2020's (P2020_n.MAP, OPTIONS ->
 * PITCHES) are 197-542 KB, the largest pitch 6 at 554,776 bytes. The room came from
 * the title tune sharing its chip RAM with the sprite pool (include/intro_chip_ram.h).
 * Both the load-size check (pitch.c) and _pitchDatBuffer allocation use this;
 * scripts/make_pitch_aga.py checks the maps against it. */
#define PITCHFILE_MAX_SIZE          (544u * 1024u)
#else
#define PITCHFILE_MAX_SIZE          FRONTBUFFER_SIZE_TOTAL_BYTES
#endif

/* ---- Copper list byte-offset helper ---- */
/* NOTE: Do NOT use COPPER_W for writes — GCC decomposes volatile uint16_t
 * stores into byte-level move.b pairs. Use copper_write_word() instead. */
#define COPPER_W(offset) (*(volatile uint16_t *)((uint8_t *)_copperListBuffer + (offset)))

/* ---- Function declarations ---- */
void InitDisplayAndCopper(void);
void ClearFrontbuffers(void);
void UpdatePlanePtrsAndBlitParams(void);
void SwapBuffersAndCameraViewState(void);
void SetCameraCoordinates(int16_t targetX, int16_t targetY);
void DoubleBufferSwapSequence(void);
/* Discard the match's two scroll origins before a complete menu redraw. */
void ResetMenuViewState(void);
void BlitterCopyPanelToScreen2(void);
void ProcessSpriteBlitQueue(void);
void ProcessSpriteBlitQueueAsync(void);
/* Undraw-queue entry modulo flag (AGA): the entry is a tile blit -- A reads one contiguous
 * tile (modulo 0), D steps by the rest of the word. Set by ScrollX's queued twin column. */
#define QUEUE_MOD_TILE 0x8000u
void BlitQueueDrain(void);
void BlitterCopyPanelToScreen(void);
void HWSprite_LoadFromSpriteGraphics(uint16_t spriteIndex);
void FlashColor0Delay(uint32_t loopCount, uint16_t color);
void WriteCopperPalette(const uint16_t *palette);
#ifdef AGA
void WriteCopperPitchPalette(void);
#endif

#endif
