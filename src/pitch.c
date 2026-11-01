/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "pitch.h"
#include "game_types.h"
#include "display.h"
#include "endian_helpers.h"
#include "swos_types.h"
#include "dos_mode.h"
#ifdef SWOS_SDL_BUILD
#include "dos_graphics.h"
#include "jag_graphics.h"   /* DosGfx_ParsePitchMap, DOS_TILE_BYTES */
#include "pitch_map.h"      /* [graphics] pitch_maps: the picture from a .tmj */
#include "animated_patterns.h"   /* AnimatedPatterns_*OwnCell* */
#include <stdio.h>          /* snprintf for the DOS pitch filenames */
#include <string.h>
#include <stdlib.h>         /* getenv */
#endif

/* ---- External globals (all live in SWOS2.s) ---- */
extern void *_swcPitchMapFiles[];     /* char *[6] -- pitch map filenames */
extern uint8_t _pitchDatBuffer[];        /* 9240 bytes (2310 longs) */
extern uint8_t _currentMenu[];           /* BSS base for pointer relocation */
extern uint16_t _panicReasonCode;
extern FixedPoint _cameraX;
extern FixedPoint _cameraY;
extern uint16_t _cameraXMod320;
extern uint16_t _cameraYMod320;
extern TeamGame _topTeamInGame;
extern TeamGame _bottomTeamInGame;
extern uint16_t _fansSidesSwapped;

/* OPTIONS -> PITCHES: 0 the DOS pitches the AGA build has always drawn (SWCPICHn.MAP),
 * 1 SWOS 2020's (P2020_n.MAP, made by the data setup from the player's SWOS 2020
 * install, tools/swosdata; scripts/make_pitch_aga.py is the original). The AGA Amiga only:
 * the row is hidden elsewhere (OptionsMenuBeforeDraw), and SDL picks its pitches by
 * its graphics tier. For the session, like the menu's other rows on the Amiga. */
uint16_t _pitchStyleOption;

/* ---- External functions ---- */
extern int16_t LoadFileEx(uint8_t *filename, uint8_t *dest, uint32_t *outFileSize);
extern void PanicHaltLoop(void) __attribute__((noreturn));

/* ---- Constants ---- */
#define PITCH_POINTER_COUNT     2310    /* 9240 / 4 */
#define FAN_PTR_TABLE_SIZE      21      /* 21 pointers per fan group */
#define FAN_PTR_TABLE_OFFSET    0x54    /* second group starts here */

/* Per-slot full-width pointer to the slot's chunky tile buffer. The
 * 32-bit pitchDatBuffer slot can only hold the low 32 of a 64-bit
 * heap/BSS address; that's still usable for the ANIM offset math
 * (`*a0 - baseAddr` cancels the truncated-vs-truncated subtraction),
 * but any *dereference* of the slot value on 64-bit hands the blitter
 * an unmapped low-32 address. We populate this side table at the same
 * time we fill the slot, and read through it whenever we actually need
 * to fetch tile bytes. */
/* Non-static so animated_patterns.c can read it directly (declared
 * extern there). Same shape: index = slot, value = full chunky-tile
 * pointer (NULL if not yet decoded). */
uint8_t *_s_pitchFullPtrs[PITCH_POINTER_COUNT];

/* pitchFansConvertTable: colour remap for the fan/crowd tiles. Indices 10,11 get
 * replaced with the kit each team is PLAYING IN at load (pr*, matching the
 * player sprites — see the deviation note in FixupAndColorPitchFile, and note
 * the original asm used the sec* kit); everything else is identity.
 *
 * AGA (8 planes) and SDL (8bpp chunky) fan tiles use 8-BIT colour indices, so
 * AdjustFanColor does convTable[0..255] — the table MUST be 256 entries. The old
 * 16-entry OCS table (4-bit indices) read out of bounds for any crowd pixel >=16
 * and garbled the whole crowd (only tiles using indices 0..15 came out right).
 * OCS keeps the 16-entry 4-bit table. The 256-entry table is filled to identity
 * at load in FixupAndColorPitchFile (before slots 10/11 are set to kit colours). */
#if defined(AGA) || defined(SWOS_SDL_BUILD)
static uint16_t pitchFansConvertTable[256];
#else
static uint16_t pitchFansConvertTable[16] = {
    0, 1, 2, 3, 4, 5, 6, 7,
    8, 9, 0xFFFF, 0xFFFF, 12, 13, 14, 15
};
#endif

/* ================================================================
 * AdjustFanColor  (SWOS2.s line 7737)
 *
 * Recolors a 4-plane interleaved sprite (16 pixels wide, 16 rows)
 * by replacing each pixel's 4-bit color index via convTable.
 *
 * Sprite layout: NUM_PLANES planes interleaved, each plane is 1 word per row.
 * Row stride = NUM_PLANES * 2 bytes.
 *
 * Parameters (original asm swaps a0/a1):
 *   destPattern = pointer to planar sprite data
 *   convTable   = color remap table: old_index → new_index
 * ================================================================ */
__attribute__((used, externally_visible))
#ifdef SWOS_SDL_BUILD
/* What the fan recolour gave each tile of the bank: the colours kit slots 10 and
 * 11 became, for a pitch drawn from a map to give ITS tile for the same cell the
 * same kit (src/pitch_map.c). The first recolour of a tile is the one recorded --
 * a second finds no 10 or 11 left to change unless a kit colour is 10 or 11. */
static uint8_t s_fanTileKit[PITCH_POINTER_COUNT][3];    /* set, slot 10, slot 11 */

int32_t Pitch_FanTileKit(const uint8_t *tile, uint8_t *slot10, uint8_t *slot11)
{
    extern uint8_t _pitchChunkyBuffer[];
    size_t index;
    if (!tile || tile < _pitchChunkyBuffer)
        return 0;
    index = (size_t)(tile - _pitchChunkyBuffer) / 256;
    if (index >= PITCH_POINTER_COUNT || !s_fanTileKit[index][0])
        return 0;
    *slot10 = s_fanTileKit[index][1];
    *slot11 = s_fanTileKit[index][2];
    return 1;
}
#endif

void AdjustFanColor(uint8_t *destPattern, const uint16_t *convTable)
{
#ifdef SWOS_SDL_BUILD
    {
        extern uint8_t _pitchChunkyBuffer[];
        if (destPattern >= _pitchChunkyBuffer) {
            size_t index = (size_t)(destPattern - _pitchChunkyBuffer) / 256;
            if (index < PITCH_POINTER_COUNT && !s_fanTileKit[index][0]) {
                s_fanTileKit[index][0] = 1;
                s_fanTileKit[index][1] = (uint8_t)convTable[10];
                s_fanTileKit[index][2] = (uint8_t)convTable[11];
            }
        }
    }
    /* Chunky form: destPattern is a 16×16 8bpp tile (256 bytes) — our
     * load-time decode in FixupAndColorPitchFile converts fan tiles
     * to the same format as pitch tiles. Color remap is a trivial LUT;
     * skip transparent pixels (palette index 0) to preserve see-through. */
    for (int i = 0; i < 256; i++) {
        uint8_t c = destPattern[i];
        if (c == 0) continue;
        destPattern[i] = (uint8_t)convTable[c];
    }
#else
    int16_t row, pixel, pl;
    uint16_t *plane0 = (uint16_t *)destPattern;

    for (row = 15; row >= 0; row--)
    {
        uint16_t w[NUM_PLANES];
        for (pl = 0; pl < NUM_PLANES; pl++)
            w[pl] = plane0[pl];

        for (pixel = 15; pixel >= 0; pixel--)
        {
            uint16_t colorIdx = 0;

            /* Extract N-bit color from MSBs of each plane */
            for (pl = NUM_PLANES - 1; pl >= 0; pl--) {
                colorIdx = (uint16_t)((colorIdx << 1) | ((w[pl] >> 15) & 1));
                w[pl] <<= 1;
            }

            /* Look up new color */
            uint16_t newColor = convTable[colorIdx];

            /* Insert new color bits into LSBs */
            for (pl = 0; pl < NUM_PLANES; pl++) {
                if (newColor & (1 << pl))
                    w[pl] |= 1;
            }
        }

        for (pl = 0; pl < NUM_PLANES; pl++)
            plane0[pl] = w[pl];
        plane0 += NUM_PLANES;
    }
#endif
}

#ifdef SWOS_SDL_BUILD
/* ================================================================
 * dos_load_pitch  (SDL + gDosGraphics only)
 *
 * Loads a DOS pitch from bin/grafs_dos. Two files:
 *   PITCHn.BLK — the tile bank: raw N × 256-byte 16×16 8bpp chunky
 *                tiles. This is already our internal format, so we
 *                load it straight into pitchChunkyBuffer (tile t at
 *                +t*256, matching the Amiga decode path and the
 *                addressing animated_patterns.c expects).
 *   PITCHn.DAT — the tile map: an array of little-endian u32 byte
 *                offsets into the bank (tileIndex = offset / 256).
 *
 * We synthesise the per-slot pointer table s_pitchFullPtrs[] that
 * DrawChunkyPitch consumes — exactly what FixupAndColorPitchFile's
 * Amiga bitplane relocate/decode would produce — then short-circuit
 * that fixup (see FixupAndColorPitchFile). DOS files are numbered
 * PITCH1..PITCH6; mapIndex is 0-based (swcPitchMapFiles order), so the
 * DOS number is mapIndex + 1.
 * ================================================================ */
extern uint8_t _pitchChunkyBuffer[];
/* Direct asset dispatch (writes straight to dest, no frontbuffer staging).
 * ASSET_OP_INIT_SOURCE0 == 2 (see file_io.c / asset_loader.c). */
extern int16_t CAssetSys_DispatchGetSize(uint16_t opcode, uint16_t param,
                                          void *name, void *dest,
                                          uint32_t *outFileSize);
#define DOS_ASSET_OP_INIT_SOURCE0   2
/* pitchChunkyBuffer capacity in whole 16x16 tiles. */
#define DOS_PITCH_BANK_MAX_TILES    PITCH_POINTER_COUNT   /* 2310 → 591360 B */

/* DOS PITCH1..PITCH6 for LoadPitchMap's index. SWOS_PITCH_NUMBER=1..6 pins the pitch drawn,
 * beside SWOS_PITCH_TYPE's condition (highlight.c): a test can then look at every one. */
static int dos_pitch_number(uint16_t mapIndex)
{
    int pitchNum = (int)mapIndex + 1;
    const char *pin = getenv("SWOS_PITCH_NUMBER");
    if (pin && pin[0] >= '1' && pin[0] <= '6' && !pin[1])
        pitchNum = pin[0] - '0';
    if (pitchNum < 1) pitchNum = 1;
    if (pitchNum > 6) pitchNum = 6;
    return pitchNum;
}

static void dos_load_pitch(uint16_t mapIndex)
{
    char name[320];
    uint32_t blkSize = 0, datSize = 0;
    int pitchNum = dos_pitch_number(mapIndex);

    /* Tile bank — already 256-byte chunky tiles. Load via the DIRECT asset
     * dispatch, NOT LoadFileEx: LoadFileEx stages through the ~91 KB
     * frontbuffer and panics (0xE000) on anything larger. The SWOS-2020 .blk
     * banks are 190–545 KB, so they MUST bypass that staging. The direct
     * dispatch writes straight into pitchChunkyBuffer (591360 B = 2310 tiles);
     * the 2020 set's largest is pitch6 at 545536 B (2131 tiles), which fits. */
    snprintf(name, sizeof name, "%s/PITCH%d.BLK", _gDosGraphicsDir, pitchNum);
    CAssetSys_DispatchGetSize(DOS_ASSET_OP_INIT_SOURCE0, 0, name,
                               _pitchChunkyBuffer, &blkSize);

    /* Tile map — stage into pitchDatBuffer, then parse the LE offsets. */
    snprintf(name, sizeof name, "%s/PITCH%d.DAT", _gDosGraphicsDir, pitchNum);
    LoadFileEx((uint8_t *)name, _pitchDatBuffer, &datSize);

    static uint16_t tileMap[PITCH_POINTER_COUNT];
    /* The Jaguar tier keeps a few duplicate palette entries for its own colours;
     * move the pitch's pixels onto the twins so those entries are really free.
     * Identical colours, so nothing changes on screen (docs/PALETTE_V2.md). */
    {
        int32_t moved = JagGfx_RemapDonated(_pitchChunkyBuffer, (uint32_t)blkSize);
        if (moved && getenv("SWOS_JAG_DUMP"))
            fprintf(stderr, "[jag] pitch: %d pixels moved off donated entries "
                            "(same colour, so nothing looks different)\n", moved);
    }

    int n = DosGfx_ParsePitchMap(_pitchDatBuffer, datSize, tileMap,
                                 PITCH_POINTER_COUNT);

    uint32_t tileCount = blkSize / DOS_TILE_BYTES;
    /* Never point past the end of pitchChunkyBuffer, even if a future bank has
     * more tiles than the buffer holds (the 2020 set stays under this). */
    if (tileCount > DOS_PITCH_BANK_MAX_TILES) tileCount = DOS_PITCH_BANK_MAX_TILES;
    for (int i = 0; i < PITCH_POINTER_COUNT; i++) {
        if (i < n && (uint32_t)tileMap[i] < tileCount)
            _s_pitchFullPtrs[i] = _pitchChunkyBuffer +
                                 (size_t)tileMap[i] * DOS_TILE_BYTES;
        else
            _s_pitchFullPtrs[i] = (uint8_t *)0;   /* unmapped → surround */
    }

    /* Mirror the Amiga SDL pitch fixup (Step 1b in FixupAndColorPitchFile):
     * also store each slot's chunky-tile pointer (truncated to 32 bits) in the
     * pitchDatBuffer slot itself. The crowd animation (DrawAnimatedPatterns /
     * ResetAnimatedPatternsForBothTeams) reads these slots as tile offsets
     * from pitchChunkyBuffer (`*slot - pitchChunkyBuffer == tileIdx*256`) to
     * toggle fan tiles between their static and animated (waving) variants. The
     * DOS tile bank stores those pairs at adjacent indices just like the Amiga,
     * so the same toggle works. Without this the slots would hold raw .DAT
     * bytes and the toggle math would read garbage. */
    uint32_t *slots = (uint32_t *)_pitchDatBuffer;
    for (int i = 0; i < PITCH_POINTER_COUNT; i++)
        slots[i] = (uint32_t)(uintptr_t)_s_pitchFullPtrs[i];

    /* [graphics] pitch_maps: this pitch's PICTURE from <dir>/pitch<N>.tmj when there
     * is one. The files above are still what the match is played on (the crowd's
     * slots); the map is baked once the fans have their kits, at the end of
     * FixupAndColorPitchFile. */
    PitchMap_Load(_gDosGraphicsDir, pitchNum);
}
#endif /* SWOS_SDL_BUILD */

#ifdef SWOS_SDL_BUILD
/* Unpacked byte size of the pitch file LoadPitchMap last read, so the chunky
 * decode in FixupAndColorPitchFile can size the tile region. 0 = unknown. */
static uint32_t s_pitchFileSize;

/* ----------------------------------------------------------------
 * pitchTilePlanes — source bitplane depth of the loaded pitch file.
 *
 * A pitch file is a PITCH_POINTER_COUNT-entry table of byte offsets into a
 * tile-data region, followed by that region. Tiles are a fixed 16 scanlines ×
 * planes × 2 bytes, so the stride is 128 at 4bpp (OCS, grafs/) or 256 at 8bpp
 * (AGA, grafs_aga/). Nothing in the file states which, so derive it:
 *
 *   - every offset is a multiple of the stride, so a 4bpp file almost always
 *     has offsets at 128 mod 256 (measured: 1644 of 2310 in grafs/SWCPICH1)
 *     whereas an 8bpp file has none by construction
 *   - the last tile ends exactly at the end of the region, so
 *     maxOffset + stride == tileRegionBytes pins it down outright
 *
 * Both signals must agree for 8bpp; the alignment test alone decides when the
 * region size is unknown. Falls back to PITCH_NUM_PLANES if neither is
 * conclusive. `offsetOf` recovers the file offset from an already-relocated
 * slot the same way the decode loop does.
 * ---------------------------------------------------------------- */
static uint16_t pitchTilePlanes(const uint32_t *pitchPtrs, uint32_t baseLow,
                                uint32_t tileRegionBytes)
{
    uint32_t maxOffset = 0;
    int      anyHalfStride = 0;
    int      sawAny = 0;
    int32_t  k;

    for (k = 0; k < (int32_t)PITCH_POINTER_COUNT; k++) {
        uint32_t off = pitchPtrs[k] - baseLow;
        if (tileRegionBytes && off >= tileRegionBytes)
            continue;                       /* bogus/unused slot */
        sawAny = 1;
        if (off > maxOffset) maxOffset = off;
        if ((off & 255u) == 128u) anyHalfStride = 1;
    }
    if (!sawAny)
        return (uint16_t)PITCH_NUM_PLANES;

    if (tileRegionBytes) {
        if (!anyHalfStride && maxOffset + 256u == tileRegionBytes) return 8;
        if (maxOffset + 128u == tileRegionBytes)                   return 4;
    }
    return anyHalfStride ? 4 : (uint16_t)PITCH_NUM_PLANES;
}
#endif /* SWOS_SDL_BUILD */

#if defined(AGA) && !defined(SWOS_SDL_BUILD)
/* The pitch's tiles again, in FAST RAM, for the CPU (2026-10-14). The blitter must read
 * them from the chip-RAM pitch buffer, but the vertical scroll copies its new rows with
 * the CPU (ScrollYUpdateRowsAndBlit), and every read it makes from chip RAM waits for a
 * slot the 8-plane display is not using. A tile's pixels never change once
 * FixupAndColorPitchFile has coloured the fans -- the crowd animation only moves pointer
 * slots between tiles -- so a copy made there stays right for the whole match.
 * g_pitchTilesFastDelta is what to add to a tile pointer into the chip copy to reach the
 * same tile here: 0 until a copy exists. */
static uint8_t  s_pitchTilesFast[PITCHFILE_MAX_SIZE - PITCH_POINTER_COUNT * 4];
static uint32_t s_pitchTileBytes;           /* the tile area of the map just loaded */
intptr_t        g_pitchTilesFastDelta;
uint8_t        *g_pitchTilesChipBase;       /* the chip copy's first tile */
uint32_t        g_pitchTilesBytes;          /* and its size: the delta applies inside it */

static void copyPitchTilesToFast(void)
{
    const uint32_t *src = (const uint32_t *)(_pitchDatBuffer + PITCH_POINTER_COUNT * 4);
    uint32_t *dst = (uint32_t *)s_pitchTilesFast;
    uint32_t n = s_pitchTileBytes / 4u;
    while (n--)
        *dst++ = *src++;
    g_pitchTilesChipBase  = _pitchDatBuffer + PITCH_POINTER_COUNT * 4;
    g_pitchTilesBytes     = s_pitchTileBytes;
    g_pitchTilesFastDelta = (intptr_t)(s_pitchTilesFast - g_pitchTilesChipBase);
}
#endif

#if defined(AGA) && !defined(SWOS_SDL_BUILD)
/* ================================================================
 * loadPitchMap2020 -- OPTIONS -> PITCHES = 2020
 *
 * SWOS 2020's pitch as an AGA map, P2020_n.MAP: the DOS map's format, three to eight
 * times the tiles. It is stored UNPACKED and read straight into the pitch buffer:
 * LoadFileEx stages a file in the frontbuffer first (104,832 bytes), and these do
 * not fit it even packed. Returns 0 when the file cannot be read, and the caller
 * loads the DOS pitch instead -- the drop may simply not have the 2020 set.
 * ================================================================ */
extern int16_t CAssetSys_DispatchGetSize(uint16_t opcode, uint16_t param,
                                          void *name, void *dest, uint32_t *outFileSize);
#define PITCH_ASSET_OP_INIT_SOURCE0  2      /* ASSET_OP_INIT_SOURCE0 (asset_loader.c) */

static int32_t loadPitchMap2020(uint16_t mapIndex)
{
    static const char *const files[6] = {
        "grafs/p2020_1.map", "grafs/p2020_2.map", "grafs/p2020_3.map",
        "grafs/p2020_4.map", "grafs/p2020_5.map", "grafs/p2020_6.map",
    };
    uint32_t fileSize = 0;

    if (mapIndex >= 6 ||
        CAssetSys_DispatchGetSize(PITCH_ASSET_OP_INIT_SOURCE0, 0, (void *)files[mapIndex],
                                  _pitchDatBuffer, &fileSize) != 0)
        return 0;
    if (fileSize > PITCHFILE_MAX_SIZE) {    /* as LoadPitchMap: a map that does not fit */
        _panicReasonCode = 3;
        PanicHaltLoop();
    }
    s_pitchTileBytes = fileSize - PITCH_POINTER_COUNT * 4u;
    g_pitchTilesFastDelta = 0;
    return 1;
}
#endif

/* ================================================================
 * LoadPitchMap  (SWOS2.s line 5557)
 *
 * Loads a pitch map file by index into pitchDatBuffer.
 * Panics if file exceeds maximum size.
 * ================================================================ */
__attribute__((used, externally_visible))
void LoadPitchMap(uint16_t mapIndex)
{
#ifdef SWOS_SDL_BUILD
    PitchMap_Drop();                /* until a map is loaded for this pitch */
    AnimatedPatterns_ClearOwnCells();   /* (Pitch_ApplyCrowdLayout marks them again) */
    if (_gDosGraphics) {
        dos_load_pitch(mapIndex);
        return;
    }
#endif
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
    if (_pitchStyleOption && loadPitchMap2020(mapIndex))
        return;
#endif
    uint8_t *filename = (uint8_t *)_swcPitchMapFiles[mapIndex];
    uint32_t fileSize = 0;
    int16_t result;

    result = LoadFileEx(filename, _pitchDatBuffer, &fileSize);
    (void)result;
#ifdef SWOS_SDL_BUILD
    s_pitchFileSize = fileSize;   /* -> pitchTilePlanes (source bitplane depth) */
#endif
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
    s_pitchTileBytes = fileSize > PITCH_POINTER_COUNT * 4u ? fileSize - PITCH_POINTER_COUNT * 4u : 0u;
    g_pitchTilesFastDelta = 0;          /* stale until FixupAndColorPitchFile copies it */
#endif

    if (fileSize > PITCHFILE_MAX_SIZE)
    {
        _panicReasonCode = 3;
        PanicHaltLoop();
    }
}

/* ================================================================
 * FixupAndColorPitchFile  (SWOS2.s line 5572)
 *
 * 1. Relocates all pitch pointer entries by adding BSS base address.
 * 2. Resets camera position.
 * 3. Recolors fan sprites for both teams using kit colors.
 * ================================================================ */
__attribute__((used, externally_visible))
void FixupAndColorPitchFile(void)
{
    uint32_t *pitchPtrs;
    int16_t i;
    /* Pitch-file pointer-table entries are byte offsets from the start
     * of the tile-data region, which begins right after the 9240-byte
     * pointer table. The original Amiga used `currentMenu` here because
     * it aliased the same RAM block as pitchDatBuffer; our C build keeps
     * them separate, so we must compute the tile-data base from the
     * actual pitch buffer for both targets. */
    uint32_t baseAddr = (uint32_t)(uintptr_t)(_pitchDatBuffer + PITCH_POINTER_COUNT * 4);
    TeamGame *teamData;
    uint16_t kitPrimary, kitSecondary;

#ifdef SWOS_SDL_BUILD
    /* DOS pitch: s_pitchFullPtrs was already built in LoadPitchMap from the BLK
     * tile bank + DAT tile map, so there is no bitplane pointer table to
     * relocate (Step 1) or decode (Step 1b) — but everything from Step 2 on
     * still applies, and skipping it is what left DOS-graphics crowds in the
     * raw artwork colours.
     *
     * This used to `return` here, on the grounds that "AdjustFanColor indexes a
     * 16-entry LUT with pixel values that can be 0..255". That is no longer
     * true on this target: pitchFansConvertTable is 256 entries under
     * `AGA || SWOS_SDL_BUILD` and is filled to identity before slots 10/11 are
     * set, so remapping 256-colour data is safe and touches nothing else.
     *
     * The symptom that found it: with dos_graphics on, the crowd came out with
     * palette index 11 (0x000F, pure blue) and index 10 (0x0F00, red) still in
     * place — i.e. the kit slots unconverted — at exactly the artwork's own
     * 16.4% / 4.6% frequencies. A tester reported "a small group of crowd
     * graphics not getting colored correctly"; measured against his screenshot
     * every one of the seven palette indices matched the artwork histogram, so
     * the crowd was not partly wrong, it was entirely unrecoloured. */
    const int dosPitch = (_gDosGraphics != 0);
    memset(s_fanTileKit, 0, sizeof s_fanTileKit);
#else
    const int dosPitch = 0;
#endif

    if (!dosPitch) {
    /* Step 1: Relocate all pointers in pitchDatBuffer.
     * SWOS Amiga pitch files store each mapping entry as a 32-bit
     * big-endian byte offset into the tile-data region. On x86 LE
     * a native uint32 load would byte-swap the value (offset 0x80
     * becomes 0x80000000), so read each entry via BE_READ_U32. */
    pitchPtrs = (uint32_t *)_pitchDatBuffer;
    /* Count UP, not down. The backward loop made GCC use
     * `pitchDatBuffer - 4` (a negative-addend symbol reference) as the
     * loop terminator. On the m68k-amiga-elf toolchain that produces a
     * mis-relocated immediate (the loader applies a delta that's off
     * by 0x200), and the loop ends early — leaving slots 0..127 still
     * holding their raw file offsets and the renderer dereferences
     * them as pointers into low memory. Iterating forward terminates
     * at `pitchDatBuffer + PITCH_POINTER_COUNT*4` (positive addend),
     * which gets relocated correctly. */
    for (int32_t k = 0; k < (int32_t)PITCH_POINTER_COUNT; k++)
    {
#ifdef SWOS_SDL_BUILD
        uint32_t offsetBE = BE_READ_U32((const uint8_t *)&pitchPtrs[k]);
        pitchPtrs[k] = offsetBE + baseAddr;
#else
        pitchPtrs[k] += baseAddr;
#endif
    }
    (void)i;

#ifdef SWOS_SDL_BUILD
    /* Step 1b (SDL only): decode each TILE_SIZE-byte NUM_PLANES-bitplane tile
     * to a 256-byte 8bpp chunky tile. Chunky tiles live in pitchChunkyBuffer,
     * indexed by **original Amiga tile index** (offset / TILE_SIZE) — NOT by
     * pointer slot. Indexing by tile-type means many pitch entries that
     * reference the same tile share one chunky copy, AND the
     * `(entry - chunkyBase) / 256` math used by DrawAnimatedPatterns
     * recovers the same tile index the Amiga path computed via
     * `(entry - currentMenu) / TILE_SIZE`. That keeps the fan-tile animation
     * (toggling tile pairs (1,2),(3,4),...,(21,22) between static and
     * animated variants) functional.
     *
     * Bitplane tile layout: 16 scanlines × (planes × 2 bytes) = tileSize bytes.
     * Each scanline is `plane0_word, plane1_word, ...` packing 16 horizontal
     * pixels as bit N of each plane word.
     *
     * DEPTH IS DETECTED PER FILE (pitchTilePlanes), not hard-coded and not
     * fixed at compile time. This block used to assume the OCS 4bpp layout
     * (128-byte tiles, 8-byte scanlines) while display.h defines AGA on this
     * build, so GRAFS is grafs_aga/ and the pitch actually loaded is 8bpp: it
     * read only planes 0-3 of each scanline, losing every pixel's high nibble,
     * and strided tiles at half their real size so the tile index was wrong
     * too — the pitch drew with recognisable geometry but wrong colours (white
     * lines came out blue, the crowd scrambled).
     *
     * Detecting rather than using PITCH_NUM_PLANES directly keeps the ORIGINAL
     * 4bpp OCS pitches loadable: drop a grafs/ SWCPICH*.MAP in (or run a build
     * that resolves to grafs/) and it still decodes, on the same binary. Same
     * philosophy as DetectBankPlanes for sprite/overlay banks. */
    {
        extern uint8_t _pitchChunkyBuffer[];
        uint8_t *tileDataBase = _pitchDatBuffer + PITCH_POINTER_COUNT * 4;
        /* Track which tile-type indices have been decoded already. */
        static uint8_t tileDecoded[PITCH_POINTER_COUNT];
        for (i = 0; i < PITCH_POINTER_COUNT; i++)
            tileDecoded[i] = 0;

        /* Reconstruct each slot's real (64-bit) tile pointer.
         *
         * Step 1 wrote `pitchPtrs[i] = offsetBE + baseAddr` where
         * baseAddr is `(uint32_t)(uintptr_t)tileDataBase` — truncated
         * to 32 bits. On 32-bit hosts that's lossless; on 64-bit the
         * slot now holds (low-32 of tileDataBase) + offsetBE, which
         * looks like a valid pointer in the low 4 GB but isn't mapped.
         *
         * The original file offset is recovered by subtracting the
         * truncated base from the truncated slot — both halves cancel
         * to give the same offset Step 1 read out of the file. Add it
         * back to the *real* tileDataBase to get the real pointer. */
        uint32_t baseLow = (uint32_t)(uintptr_t)tileDataBase;
        /* Source depth of THIS file: 4 (OCS grafs/) or 8 (AGA grafs_aga/). */
        uint32_t tileRegion  = s_pitchFileSize > PITCH_POINTER_COUNT * 4u
                             ? s_pitchFileSize - PITCH_POINTER_COUNT * 4u : 0u;
        uint16_t srcPlanes   = pitchTilePlanes(pitchPtrs, baseLow, tileRegion);
        /* Never gather more planes than the pw[] below holds. Only bites on a
         * hypothetical 4-plane (non-AGA) build fed an 8bpp pitch — that build
         * resolves grafs/ only, so it can't normally happen; degrade to the low
         * nibble rather than overrun. */
        if (srcPlanes > PITCH_NUM_PLANES) srcPlanes = (uint16_t)PITCH_NUM_PLANES;
        uint32_t srcRowBytes = (uint32_t)srcPlanes * 2u;          /* one scanline  */
        uint32_t srcTileSize = srcRowBytes * TILE_HEIGHT;         /* 128 or 256    */
        /* End of the tile stream = end of what LoadPitchMap actually read. This
         * was a hard-coded `_pitchDatBuffer + 64*1024`, which every 4bpp OCS map
         * fits inside (largest 45592) but 5 of the 6 8bpp AGA maps do NOT
         * (63256..78104). Tiles past the 64K mark failed the guard below and got
         * zero-filled, and palette index 0 is grass — so they drew as flat
         * grass-coloured rectangles scattered over the pitch. The buffer itself
         * was never the limit (PITCHFILE_MAX_SIZE / FRONTBUFFER_ALLOC_TOTAL_BYTES,
         * ~82-128K) and the load-size check passed; only this bound was stale. */
        uint8_t *tileDataEnd = tileRegion ? tileDataBase + tileRegion
                                          : _pitchDatBuffer + 64 * 1024;

        for (i = 0; i < PITCH_POINTER_COUNT; i++) {
            uint32_t slotLow    = pitchPtrs[i];
            uint32_t fileOffset = slotLow - baseLow;
            uint8_t *bitplane   = tileDataBase + fileOffset;
            /* Guard against bogus pointers (shouldn't happen, but the
             * file could be malformed or the stub still indexed). */
            if (bitplane < tileDataBase || bitplane + srcTileSize > tileDataEnd) {
                /* Fall back to per-entry indexing for invalid pointers
                 * so we at least don't share garbage between tiles. */
                uint32_t safeIdx = (uint32_t)i;
                uint8_t *chunky = _pitchChunkyBuffer + safeIdx * 256;
                for (int k = 0; k < 256; k++) chunky[k] = 0;
                pitchPtrs[i] = (uint32_t)(uintptr_t)chunky;
                _s_pitchFullPtrs[i] = chunky;
                continue;
            }

            uint32_t tileIdx = (uint32_t)((bitplane - tileDataBase) / srcTileSize);
            if (tileIdx >= PITCH_POINTER_COUNT) {
                /* offset out of bounds → safe fallback */
                uint8_t *chunky = _pitchChunkyBuffer + (size_t)i * 256;
                for (int k = 0; k < 256; k++) chunky[k] = 0;
                pitchPtrs[i] = (uint32_t)(uintptr_t)chunky;
                _s_pitchFullPtrs[i] = chunky;
                continue;
            }

            uint8_t *chunky = _pitchChunkyBuffer + (size_t)tileIdx * 256;

            if (!tileDecoded[tileIdx]) {
                for (int row = 0; row < TILE_HEIGHT; row++) {
                    const uint8_t *rowSrc = bitplane + (uint32_t)row * srcRowBytes;
                    /* Read srcPlanes plane words (big-endian from file). Sized
                     * PITCH_NUM_PLANES (the deepest we ever see) and filled to
                     * srcPlanes, so a 4bpp OCS pitch yields indices 0-15 and an
                     * 8bpp AGA one the full 0-255. */
                    uint16_t pw[PITCH_NUM_PLANES];
                    for (int pl = 0; pl < srcPlanes; pl++)
                        pw[pl] = (uint16_t)(((uint16_t)rowSrc[pl * 2] << 8) | rowSrc[pl * 2 + 1]);
                    uint8_t *dst = chunky + row * 16;
                    for (int px = 0; px < 16; px++) {
                        int bit = 15 - px;
                        uint8_t idx = 0;
                        for (int pl = 0; pl < srcPlanes; pl++)
                            idx |= (uint8_t)(((pw[pl] >> bit) & 1u) << pl);
                        dst[px] = idx;
                    }
                }
                tileDecoded[tileIdx] = 1;
            }
            pitchPtrs[i] = (uint32_t)(uintptr_t)chunky;
            _s_pitchFullPtrs[i] = chunky;
        }
    }
    /* Same as the DOS path: move the pitch off the duplicate entries the Jaguar
     * tier keeps for itself. Identical colours, so nothing changes on screen
     * (docs/PALETTE_V2.md). */
    JagGfx_RemapDonated(_pitchChunkyBuffer, (uint32_t)(PITCH_POINTER_COUNT * 256));
#endif

    }

    /* Step 2: Reset camera */
    _cameraX.s.i = 0;
    _cameraY.s.i = 0;
    _cameraXMod320 = 0;
    _cameraYMod320 = 0;

    /* Step 3: Set up fan pointer groups.
     *
     * The Amiga code aliased the in-buffer 32-bit slot table as two
     * fan-group pointers (group A at slot 0, group B at slot
     * FAN_PTR_TABLE_OFFSET/4 = 21). On 64-bit the slot's truncated
     * pointer can't be dereferenced, so the SDL path resolves to the
     * full-width pointers via the side table populated in Step 1b.
     * On Amiga, s_pitchFullPtrs is NOT populated (Step 1b is gated
     * out) — use the in-buffer slot table directly, matching the
     * original asm. */
#ifdef SWOS_SDL_BUILD
    uint8_t **fanGroupA = &_s_pitchFullPtrs[0];
    uint8_t **fanGroupB = &_s_pitchFullPtrs[FAN_PTR_TABLE_OFFSET / 4];
#else
    uint8_t **fanGroupA = (uint8_t **)_pitchDatBuffer;
    uint8_t **fanGroupB = (uint8_t **)(_pitchDatBuffer + FAN_PTR_TABLE_OFFSET);
#endif

    _fansSidesSwapped = 0;
    /* If topTeamInGame's team name first byte is odd, swap groups */
    if (_topTeamInGame.teamName[0] & 1)
    {
        uint8_t **tmp = fanGroupA;
        fanGroupA = fanGroupB;
        fanGroupB = tmp;
        _fansSidesSwapped = 1;
    }

#if defined(AGA) || defined(SWOS_SDL_BUILD)
    /* Fill the 256-entry remap to identity so AdjustFanColor (which indexes it
     * with an 8-bit crowd colour) preserves every colour except the kit slots
     * 10/11 set below. Without this the crowd is garbled (see table decl). */
    {
        int16_t c;
        for (c = 0; c < 256; c++)
            pitchFansConvertTable[c] = (uint16_t)c;
    }
#endif

    /* Step 4: Recolor top team fans (21 sprites).
     *
     * DELIBERATE DEVIATION FROM SWOS2.s — the fans wear the kit the team is
     * actually PLAYING IN, not its second-choice kit.
     *
     * The original reads topTeamInGame+$C/$E/$10 (SWOS2.s:6389-6396), i.e. the
     * sec* fields at TeamGame offsets 12/14/16. But the PLAYER sprites in this
     * port are coloured from the pr* fields (color_sprites.c:325/335/357/414,
     * which hold the converted in-game palette index), so reading sec* here put
     * the crowd in a kit nobody on the pitch was wearing.
     *
     * Measured on a real match: the fan tiles use palette indices 10 and 11
     * (140 px / 349 px across the 21 tiles), and the team's sec* values were
     * shirt=11, stripes=15, shorts=11 — so slot 11 was remapped to itself and
     * the crowd simply kept the artwork's own colour. Reported by a tester as
     * "crowd with wrong shirt colors, none of the teams has blue shirts".
     *
     * Owner's call, and the AGA tier already departs from the Amiga asm here:
     * the crowd must match the shirts on the pitch.
     *
     * SECOND HALF OF THE SAME DEVIATION (2026-09-11). The fan artwork paints
     * most shirts with palette index 11 and a scattered few with index 10, so
     * the crowd is deliberately two-tone. The asm sets slot 10 from stripes and,
     * when a shirt has no stripes, FALLS BACK TO THE SHORTS COLOUR
     * (SWOS2.s:6390-6396: `cmp.w $C(a0),d0 / bne / move.w $10(a0),d0`). Reading
     * pr* instead of sec* fixed slot 11 but left that fallback pointing at the
     * shorts of the kit actually being worn — so a red-shirted team in blue
     * shorts put a handful of BLUE fans in a red crowd, and a tester reported
     * "a small group of crowd graphics not getting colored correctly". Measured
     * from his screenshot: the odd fans are one pure colour (rgb 11,36,251),
     * individual figures interleaved in the front row, and no team on the pitch
     * wore blue.
     *
     * So take slot 10 from the STRIPES colour with no shorts fallback.
     * ConvertTeamKitColors sets prStripesCol == prShirtCol for an ORDINARY
     * shirt (team_colors.c, matching SWOS2.s:54090), so this is self-folding:
     *   plain shirt   -> slot 10 == slot 11, the crowd is uniformly the shirt
     *   striped shirt -> slot 10 is the other stripe colour, still two-tone
     * The crowd now only ever wears colours that are on the shirts. */
    teamData = &_topTeamInGame;
    kitPrimary = (uint16_t)teamData->prShirtCol;
    kitSecondary = (uint16_t)teamData->prStripesCol;

    pitchFansConvertTable[11] = kitPrimary;     /* offset 0x16 */
    pitchFansConvertTable[10] = kitSecondary;   /* offset 0x14 */

    for (i = FAN_PTR_TABLE_SIZE - 1; i >= 0; i--)
    {
        AdjustFanColor(fanGroupB[i], pitchFansConvertTable);
    }

    /* Step 5: Recolor bottom team fans (21 sprites). Same deviation as Step 4. */
    teamData = &_bottomTeamInGame;
    kitPrimary = (uint16_t)teamData->prShirtCol;
    kitSecondary = (uint16_t)teamData->prStripesCol;

    pitchFansConvertTable[11] = kitPrimary;
    pitchFansConvertTable[10] = kitSecondary;

    for (i = 20; i >= 0; i--)
    {
        AdjustFanColor(fanGroupA[i], pitchFansConvertTable);
    }

#ifdef SWOS_SDL_BUILD
    /* Step 6: catch crowd cells whose tile ISN'T one of those 42.
     *
     * SDL only: s_pitchFullPtrs is the full-width side table built in Step 1b
     * (or by dos_load_pitch), and the Amiga path does not populate it.
     *
     * Steps 4/5 recolour 21+21 POINTERS — map row 0 — and everything else in
     * the stands only changes because it happens to point at the same tile
     * DATA. On the Amiga pitches that holds. On the DOS banks it does not:
     * measured across bin/grafs_dos/PITCH1..6.DAT, row 51 (the bottom crowd's
     * front row) uses tiles at the very end of the bank that row 0 never
     * references —
     *
     *     PITCH1  row 51  tile 239 (52 kit px), tile 240 (67 kit px)
     *     PITCH2  row 51  tile 223 (52 kit px), tile 224 (67 kit px)
     *
     * — so those blocks of fans kept the raw artwork and showed up as a small
     * patch of the wrong colour in the bottom-right corner. That is the
     * long-running "small group of crowd graphics not getting colored
     * correctly" report; f7f193a fixed the 99% of it that was the whole
     * recolour being skipped, and this is the remainder.
     *
     * Which kit a stray tile should wear is DERIVED, never assumed: find a
     * neighbouring cell in the SAME ROW whose tile did get recoloured, and
     * reuse that group's table. That stays correct when fansSidesSwapped flips
     * the groups, and when a pitch assigns the bands the other way round.
     *
     * SCOPE IS PER ROW, NOT PER HALF (fixed 2026-09-12). The first cut swept
     * every cell in each half of the grid, i.e. the whole ground — so it also
     * recoloured artwork that merely happens to use the kit slots 10/11 but is
     * not crowd. Measured on both tiers (Amiga grafs_aga and grafs_dos, same
     * counts): rows 0-3 and 51-54 are the crowd; the uncovered cells holding
     * kit pixels are row 5 x16, row 7 x2, row 47 x3 and row 51 x2. Row 5 is
     * the ADVERTISING HOARDINGS — "SOCCER" is painted in slot 10 and the
     * '96/'97 board's background in slot 11 — so the boards took the playing
     * teams' shirt colours and a tester reported "the adboards colors are
     * messed up". Only row 51's two cells were ever the target.
     *
     * So sweep only rows that already contain a recoloured crowd cell. That
     * keeps the row-51 fix (row 51 has 40 covered cells to derive from) and
     * leaves rows 5/7/47 alone, without hard-coding which rows are stands. */
    {
        /* The map is a flat 42 x 55 grid; see DrawChunkyPitch. */
        #define PITCH_GRID_COLS 42
        #define PITCH_GRID_ROWS 55
        uint16_t topTbl[2], botTbl[2];      /* {slot10, slot11} per group */
        int8_t   rowGroup[PITCH_GRID_ROWS]; /* -1 = not a crowd row, else 1=A 0=B */
        int32_t  r, c, k;

        topTbl[0] = (uint16_t)_topTeamInGame.prStripesCol;
        topTbl[1] = (uint16_t)_topTeamInGame.prShirtCol;
        botTbl[0] = (uint16_t)_bottomTeamInGame.prStripesCol;
        botTbl[1] = (uint16_t)_bottomTeamInGame.prShirtCol;

        /* Which group (if any) does each row's crowd wear? A row with no
         * recoloured cell at all is not a stand and is left untouched. */
        for (r = 0; r < PITCH_GRID_ROWS; r++) {
            rowGroup[r] = -1;
            for (c = 0; c < PITCH_GRID_COLS && rowGroup[r] < 0; c++) {
                uint8_t *q = _s_pitchFullPtrs[r * PITCH_GRID_COLS + c];
                if (!q) continue;
                for (k = 0; k < FAN_PTR_TABLE_SIZE; k++) {
                    if (fanGroupA[k] == q) { rowGroup[r] = 1; break; }
                    if (fanGroupB[k] == q) { rowGroup[r] = 0; break; }
                }
            }
        }

        for (r = 0; r < PITCH_GRID_ROWS; r++) {
            if (rowGroup[r] < 0) continue;

            /* Step 5 left fanGroupA's colours in the table; Step 4 used
             * fanGroupB's. Reload whichever this row needs. */
            pitchFansConvertTable[10] = rowGroup[r] ? botTbl[0] : topTbl[0];
            pitchFansConvertTable[11] = rowGroup[r] ? botTbl[1] : topTbl[1];

            for (c = 0; c < PITCH_GRID_COLS; c++) {
                uint8_t *q = _s_pitchFullPtrs[r * PITCH_GRID_COLS + c];
                int32_t covered = 0, dup = 0;
                if (!q) continue;
                for (k = 0; k < FAN_PTR_TABLE_SIZE; k++)
                    if (fanGroupA[k] == q || fanGroupB[k] == q) { covered = 1; break; }
                if (covered) continue;
                /* Tiles repeat across cells — only convert each one once.
                 * Scan the crowd rows already swept (and this row so far);
                 * a match in a non-crowd row means nothing, since those are
                 * never converted. */
                for (int32_t rr = 0; rr <= r && !dup; rr++) {
                    if (rowGroup[rr] < 0) continue;
                    int32_t cmax = (rr == r) ? c : PITCH_GRID_COLS;
                    for (int32_t cc = 0; cc < cmax; cc++)
                        if (_s_pitchFullPtrs[rr * PITCH_GRID_COLS + cc] == q) { dup = 1; break; }
                }
                if (dup) continue;
                AdjustFanColor(q, pitchFansConvertTable);
            }
        }
        #undef PITCH_GRID_COLS
        #undef PITCH_GRID_ROWS
    }

    /* The pitch from its map, now that every fan tile of the bank has its kit. */
    if (dosPitch)
        PitchMap_Bake();
#endif
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
    copyPitchTilesToFast();     /* the fans are coloured: the tiles are final */
#endif
}

#ifdef SWOS_SDL_BUILD
/* ================================================================
 * DrawChunkyPitch (SDL only)
 *
 * Whole-visible-pitch redraw from the chunky tile bank. Replaces the
 * Amiga incremental scroll blits (_Scroll{X,Y}UpdateRowsAndBlit, now
 * no-op on SDL).
 *
 * Layout (see AmigaTools.cs ReadPitchMap):
 *   - 42 cols × 55 rows = 2310 tile mappings indexed from pointer 0
 *   - Each tile is 16×16 palette-index bytes (256 bytes chunky)
 *
 * Camera position is in pixels (cameraX.s.i / cameraY.s.i). We pad by one
 * tile in each direction so tiles partially on-screen render fully.
 * ================================================================ */
/* Layout (per AmigaTools.cs): the pitch map is a flat 42-col × 55-row
 * grid of 16×16 tile mappings — every pointer in pitchDatBuffer is a
 * real tile entry, indexed from row 0. The first 42 happen to reference
 * fan-sprite tiles that AdjustFanColor recolors at load, but that
 * shares tile *data* across mapping entries, not a separate pointer
 * prefix. Rendering must start at index 0 so pitch world Y=0 lines up
 * with overlay sprites that use the same coordinate space. */
#define PITCH_PTR_START     0
#define PITCH_GRID_COLS     42
#define PITCH_GRID_ROWS     55
#define PITCH_TILE_SIZE     16

extern uint8_t _pitchDatBuffer[];
extern volatile uintptr_t _frontbufferBasePtr;

__attribute__((used, externally_visible))
/* How many times the pitch has been drawn since the match (or replay) began.
 *
 * The display asks this before switching to the DOS pitch palette, because
 * "a match has started" and "the pitch is on screen" are not the same moment:
 * _playGame is set on the FIRST line of StartMatchSequence, while the frontbuffer
 * still holds the pre-match team line-up and InitGameSaveTeams has yet to fade it
 * out. Switching palettes there showed that line-up through the pitch palette --
 * the "distorted VS background for a frame or two" (reported 2026-09-22).
 *
 * Two, not one: the frame being PRESENTED is the completed one (the alt buffer,
 * platform_sdl.c chunky_to_rgba), so the first pitch draw lands in the buffer
 * that is not on screen yet. By the second, the presented buffer has a pitch in
 * it. The same reasoning the highlights path applies by ordering its flag. */
static int32_t s_pitchDrawn;
int32_t Pitch_DrawnThisMatch(void) { return s_pitchDrawn >= 2; }
void    Pitch_ResetDrawn(void) { s_pitchDrawn = 0; }

void DrawChunkyPitch(void)
{
    if (s_pitchDrawn < 2) s_pitchDrawn++;

    uint8_t  *fb        = (uint8_t *)(uintptr_t)_frontbufferBasePtr;
    if (!fb) return;

    /* Both runtime: 336x272 for the Amiga view, 320x200 for the DOS one. The
     * old comments here said 320/200 -- inherited from the DOS source this was
     * ported from, and wrong for every build since. */
    const int fbW = FRONTBUFFER_ROW_STRIDE_BYTES;
    const int fbH = _screen_height;

    int startTileX = (int)Render_CameraX() / PITCH_TILE_SIZE;
    int startTileY = (int)Render_CameraY() / PITCH_TILE_SIZE;
    int offsetX    = (int)Render_CameraX() % PITCH_TILE_SIZE;
    int offsetY    = (int)Render_CameraY() % PITCH_TILE_SIZE;
    if (offsetX < 0) { offsetX += PITCH_TILE_SIZE; startTileX--; }
    if (offsetY < 0) { offsetY += PITCH_TILE_SIZE; startTileY--; }

    int tilesAcross = fbW / PITCH_TILE_SIZE + 2;
    int tilesDown   = fbH / PITCH_TILE_SIZE + 2;
    const int mapped = PitchMap_Active();

    for (int ty = 0; ty < tilesDown; ty++) {
        int gridY = startTileY + ty;
        int screenY = ty * PITCH_TILE_SIZE - offsetY;
        for (int tx = 0; tx < tilesAcross; tx++) {
            int gridX = startTileX + tx;
            int screenX = tx * PITCH_TILE_SIZE - offsetX;

            /* Off-pitch area: paint the surround color (palette 0). */
            uint8_t *tile = (uint8_t *)0;
            if (gridX >= 0 && gridX < PITCH_GRID_COLS &&
                gridY >= 0 && gridY < PITCH_GRID_ROWS) {
                int ptrIdx = PITCH_PTR_START + gridY * PITCH_GRID_COLS + gridX;
                tile = (ptrIdx >= 0 && ptrIdx < PITCH_POINTER_COUNT)
                         ? _s_pitchFullPtrs[ptrIdx] : (uint8_t *)0;
                /* Drawn from a map: the map's tile for this cell, seated or standing
                 * as the engine's own tile there is (src/pitch_map.c). */
                if (mapped && ptrIdx >= 0 && ptrIdx < PITCH_POINTER_COUNT)
                    tile = (uint8_t *)PitchMap_Tile(ptrIdx, tile);
            }

            for (int row = 0; row < PITCH_TILE_SIZE; row++) {
                int py = screenY + row;
                if (py < 0 || py >= fbH) continue;
                uint8_t *dstRow = fb + py * fbW;
                if (tile) {
                    const uint8_t *srcRow = tile + row * PITCH_TILE_SIZE;
                    for (int col = 0; col < PITCH_TILE_SIZE; col++) {
                        int px = screenX + col;
                        if (px < 0 || px >= fbW) continue;
                        dstRow[px] = srcRow[col];
                    }
                } else {
                    for (int col = 0; col < PITCH_TILE_SIZE; col++) {
                        int px = screenX + col;
                        if (px < 0 || px >= fbW) continue;
                        dstRow[px] = 0;
                    }
                }
            }
        }
    }
}
#endif

#ifdef SWOS_SDL_BUILD
/* ================================================================
 * The crowd as the match plays it (include/pitch.h)
 * ================================================================ */
extern void RncDecompressV1(uint8_t *src, uint8_t *dest);
extern void RncDecompressV2(uint8_t *src, uint8_t *dest);
extern void *_swcPitchMapFiles[];

static int32_t crowd_slot(int32_t cell)
{
    return cell < PITCH_CROWD_ROW_CELLS ? PITCH_CROWD_TOP_SLOT + cell
                                        : PITCH_CROWD_BOTTOM_SLOT + cell - PITCH_CROWD_ROW_CELLS;
}

/* A file, unpacked, in a buffer of its own (free it); NULL if it cannot be read. Straight
 * through the asset dispatch, as the DOS pitch's bank is: LoadFileEx stages through the
 * frontbuffer, which is the lobby's screen when the host builds the setup. */
static uint8_t *crowd_file(const char *name, uint32_t *size)
{
    uint8_t *raw = (uint8_t *)malloc(PITCHFILE_MAX_SIZE), *out;
    uint32_t n = 0, unpacked;
    if (!raw)
        return NULL;
    if (CAssetSys_DispatchGetSize(DOS_ASSET_OP_INIT_SOURCE0, 0, (void *)name, raw, &n) != 0 || n < 8) {
        free(raw);
        return NULL;
    }
    if (raw[0] != 'R' || raw[1] != 'N' || raw[2] != 'C' || (raw[3] != 1 && raw[3] != 2)) {
        *size = n;
        return raw;
    }
    unpacked = BE_READ_U32(raw + 4);            /* (LoadFileEx: methods 1 and 2) */
    out = unpacked && unpacked <= PITCHFILE_MAX_SIZE ? (uint8_t *)malloc(unpacked) : NULL;
    if (out) {
        if (raw[3] == 1) RncDecompressV1(raw, out);
        else             RncDecompressV2(raw, out);
    }
    free(raw);
    *size = unpacked;
    return out;
}

int32_t Pitch_CrowdLayout(uint16_t mapIndex, uint16_t out[PITCH_CROWD_CELLS])
{
    uint32_t size = 0;
    uint8_t *buf;
    int32_t cell;

    if (mapIndex >= PITCH_CROWD_FILES)
        return 0;
    if (_gDosGraphics) {
        /* dos_load_pitch: the .DAT's tile map; a cell past the map draws the surround (none). */
        char name[320];
        static uint16_t tileMap[PITCH_POINTER_COUNT];
        int n;
        snprintf(name, sizeof name, "%s/PITCH%d.DAT", _gDosGraphicsDir, dos_pitch_number(mapIndex));
        buf = crowd_file(name, &size);
        if (!buf)
            return 0;
        n = DosGfx_ParsePitchMap(buf, size, tileMap, PITCH_POINTER_COUNT);
        free(buf);
        for (cell = 0; cell < PITCH_CROWD_CELLS; cell++) {
            const int32_t slot = crowd_slot(cell);
            out[cell] = slot < n && tileMap[slot] < DOS_PITCH_BANK_MAX_TILES ? (uint16_t)(tileMap[slot] + 1) : 0;
        }
        return 1;
    }
    /* FixupAndColorPitchFile, Steps 1 and 1b: a slot's tile is its file offset over the
     * file's tile size, the slot's own number for an offset past the tiles. */
    buf = crowd_file((const char *)_swcPitchMapFiles[mapIndex], &size);
    if (!buf)
        return 0;
    if (size <= PITCH_POINTER_COUNT * 4u) {
        free(buf);
        return 0;
    }
    {
        static uint32_t rel[PITCH_POINTER_COUNT];
        const uint32_t region = size - PITCH_POINTER_COUNT * 4u, baseLow = 0x10000u;
        uint32_t planes, tileSize;
        int32_t k;
        for (k = 0; k < (int32_t)PITCH_POINTER_COUNT; k++)
            rel[k] = BE_READ_U32(buf + 4 * k) + baseLow;
        planes = pitchTilePlanes(rel, baseLow, region);
        if (planes > PITCH_NUM_PLANES)
            planes = PITCH_NUM_PLANES;
        tileSize = planes * 2u * TILE_HEIGHT;
        for (cell = 0; cell < PITCH_CROWD_CELLS; cell++) {
            const int32_t slot = crowd_slot(cell);
            const uint32_t off = rel[slot] - baseLow;
            uint32_t tile = (uint32_t)slot;
            if (off + tileSize <= region && off / tileSize < PITCH_POINTER_COUNT)
                tile = off / tileSize;
            out[cell] = (uint16_t)(tile + 1);
        }
    }
    free(buf);
    return 1;
}

void Pitch_ApplyCrowdLayout(const uint16_t in[PITCH_CROWD_CELLS])
{
    uint32_t *slots = (uint32_t *)_pitchDatBuffer;
    const uint32_t base = (uint32_t)(uintptr_t)_pitchChunkyBuffer;
    int32_t cell, theirs = 0;

    for (cell = 0; cell < PITCH_CROWD_CELLS; cell++) {
        const int32_t slot = crowd_slot(cell);
        const uint32_t v = slots[slot];
        uint16_t own;
        if (in[cell] == PITCH_CROWD_KEEP)
            continue;
        own = v == 0 ? 0 : ((v - base) & 0xFFu) ? 0xFFFEu : (uint16_t)(((v - base) >> 8) + 1);
        if (own == in[cell])
            continue;
        slots[slot] = in[cell] ? base + ((uint32_t)(in[cell] - 1) << 8) : 0;
        AnimatedPatterns_SetOwnCell(slot);
        theirs++;
    }
    if (theirs)
        fprintf(stderr, "[crowd] %d of %d crowd cells are the host's, not this pitch's own: "
                        "drawn still, in this game's artwork\n", (int)theirs, PITCH_CROWD_CELLS);
}
#endif /* SWOS_SDL_BUILD */
