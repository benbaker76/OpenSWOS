/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/*
 * asset_sys.c — C port of AssetSys_Dispatch and all AssetSys_* functions
 *
 * Previously these were the last block of live assembly code in SWOS2.s.
 * All dispatch goes through RobNorthenLoader_C (HDD file I/O).
 *
 * The original floppy-era code probed drives, built drive prefix strings,
 * and had complex source-slot management. In HDD mode, most of this is
 * simplified: RobNorthenLoader handles all file operations via PROGDIR:.
 */
#include "asset_loader.h"
#include "save_filename.h"   /* the save folder's listing: SAVE_LIST_* */
#include "string_utils.h"
#include "display.h"
#include "swos_types.h"  /* uintptr_t */

/* ---- External globals (from SWOS2.s data) ---- */
extern uint8_t _diskImageContext;

/* ---- External C functions ---- */
extern uint32_t RobNorthenLoader_C(uint32_t cmd, uint32_t arg1, uint8_t *name, uint8_t *buf);
extern uint32_t _rnl_d1_return;
extern void InstallAndInitLoaderBootstrap(void);

/* ---- AssetSys private state ----
 * These were PC-relative data in SWOS2.s. Now C globals.
 * Some are exported because other C code reads them.
 */
/* All slots that hold a real pointer (loaderBase, source path strings,
 * source vectors). Amiga 32-bit fits in uint32_t; on 64-bit the
 * pointer needs uintptr_t so the value round-trips correctly. */
static uintptr_t assetBasePtr;
static uintptr_t assetAltBasePtr;
static uintptr_t assetSource0;
static uintptr_t assetSource1;
static uintptr_t assetSource2;
static uintptr_t assetSource3;

/* Exported globals — referenced by asset_loader.c, file_io.c, loader_bootstrap.c */
uint16_t _assetCurrentIndex;
uint16_t _assetCurrentSourceSlot;
uintptr_t _assetSourceVec[4];
uint16_t _assetIndexStartBytes;  /* treated as 4-byte block in asm: 2 x uint16_t */
uint16_t _assetParamBlock;

static uint16_t assetSelect;
static uint16_t assetSavedSourceSlot;
static uint16_t assetSavedIndex;
static uint8_t assetFilename[50];

/* Disk image path strings */
static const uint8_t kSwos4Dk1[] = "swos4.dk1";
static const uint8_t kSwos4Dk2[] = "swos4.dk2";
static const uint8_t kSwos4Dku[] = "swos4.dku";
static const uint8_t kGrafsDisk2Raw[] = GRAFS "disk2.raw";

/* ---- Helper: build full path into assetFilename ---- */
static void assetSysPrepCopyParams(const uint8_t *filename)
{
    uint8_t *dst;
    uintptr_t srcAddr;

    srcAddr = _assetSourceVec[_assetCurrentSourceSlot];
    dst = assetFilename;

    /* Copy source prefix (drive path) if non-null */
    if (srcAddr != 0)
        dst = StringCopy((const uint8_t *)srcAddr, dst);

    /* Append the filename */
    StringCopy(filename, dst);
}

/* ================================================================
 * AssetSys_InitContext  (opcode 0)
 *
 * Sets up base pointers, source paths. In HDD mode, floppy drive
 * probing is skipped — no floppy drives present.
 * ================================================================ */
static int16_t assetSys_InitContext(uint8_t *loaderBase, const uint8_t *disk0,
                                     const uint8_t *disk1, const uint8_t *disk2,
                                     const uint8_t *disk3)
{
    uint16_t i;

    assetBasePtr = (uintptr_t)loaderBase;
    assetAltBasePtr = (uintptr_t)(loaderBase + 0x4D00);

    assetSource0 = (uintptr_t)disk0;
    assetSource1 = (uintptr_t)disk1;
    assetSource2 = (uintptr_t)disk2;
    assetSource3 = (uintptr_t)disk3;

    /* In HDD mode, no floppy drives — sourceVec stays NULL */
    for (i = 0; i < 4; i++)
        _assetSourceVec[i] = 0;

    /* Clear state */
    _assetIndexStartBytes = 0;
    _assetParamBlock = 0;
    _assetCurrentSourceSlot = 0;
    _assetCurrentIndex = 0;

    return 0;
}

/* ================================================================
 * AssetSys_InitSource0  (opcode 2) — load file, RNL cmd=0
 * ================================================================ */
static int32_t assetSys_InitSource0(const uint8_t *filename, uint8_t *dest)
{
    assetSysPrepCopyParams(filename);
    int32_t r = (int32_t)RobNorthenLoader_C(0, 0, assetFilename, dest);
    return r;
}

/* ================================================================
 * AssetSys_LoadFileOp  (opcode 4) — save file, RNL cmd=1
 * arg1 carries file size for save operations.
 * ================================================================ */
static int32_t assetSys_LoadFileOp(const uint8_t *filename, uint8_t *src, uint32_t arg1)
{
    assetSysPrepCopyParams(filename);
    return (int32_t)RobNorthenLoader_C(1, arg1, assetFilename, src);
}

/* ================================================================
 * AssetSys_InitSource2  (opcode 6) — delete file, RNL cmd=2
 * ================================================================ */
static int32_t assetSys_InitSource2(const uint8_t *filename, uint8_t *dest)
{
    assetSysPrepCopyParams(filename);
    return (int32_t)RobNorthenLoader_C(2, 0, assetFilename, dest);
}

/* ================================================================
 * AssetSys_ResolveAssetList  (opcode 8) — list directory, RNL cmd=3
 * Queries available assets and filters against known source paths.
 * ================================================================ */
static int32_t assetSys_ResolveAssetList(uint8_t *listBuf)
{
    static uint16_t dummyWord;
    int32_t result;
    uint8_t *a4;
    uint8_t *a2;
    uint8_t *a3;
    int16_t count;
    uint32_t d1val;

    assetSysPrepCopyParams((const uint8_t *)&dummyWord);
    a4 = listBuf;

    /* Dispatch cmd=3 (list directory) */
    result = (int32_t)RobNorthenLoader_C(3, 0, assetFilename, a4 + 2);
    d1val = _rnl_d1_return;
    if (result != 0)
        return result;

    /* Store entry count */
    *(uint16_t *)a4 = (uint16_t)d1val;
    if (d1val == 0)
        return 0;

    count = (int16_t)(d1val - 1);

    /* Clear suffix characters for each entry */
    {
        uint8_t *p = a4;
        int16_t i;
        for (i = count; i >= 0; i--)
        {
            int16_t len = (int16_t)(signed char)p[3];
            p[4 + len] = 0;
            p += SAVE_LIST_ENTRY;  /* (0x20 the original's) */
        }
    }

    /* Filter entries: remove entries that match any known source path */
    a2 = a4;
    a3 = a4;
    count = (int16_t)(*(uint16_t *)a4 - 1);

    {
        int16_t i;
        for (i = count; i >= 0; i--)
        {
            uint8_t drop = 0;

            if (a2[2] == 2)
            {
                drop = 1;
            }
            else
            {
                if (assetSource0 != 0 &&
                    StringEquals(a2 + 4, (const uint8_t *)assetSource0) == 0)
                {
                    drop = 1;
                }
                else if (assetSource1 != 0 &&
                    StringEquals(a2 + 4, (const uint8_t *)assetSource1) == 0)
                {
                    drop = 1;
                }
                else if (assetSource2 != 0 &&
                    StringEquals(a2 + 4, (const uint8_t *)assetSource2) == 0)
                {
                    drop = 1;
                }
                else if (assetSource3 != 0 &&
                    StringEquals(a2 + 4, (const uint8_t *)assetSource3) == 0)
                {
                    drop = 1;
                }
            }

            if (drop)
            {
                (*(uint16_t *)a4)--;
            }
            else
            {
                if (a2 != a3)
                    StringCopy(a2 + 4, a3 + 4);
                a3 += SAVE_LIST_ENTRY;
            }
            a2 += SAVE_LIST_ENTRY;
        }
    }

    return 0;
}

/* ================================================================
 * AssetSys_DoCriticalOp  (opcode 10) — RNL cmd=4 with interrupts off
 * ================================================================ */
static int32_t assetSys_DoCriticalOp(const uint8_t *filename, uint8_t *dest)
{
    static uint16_t criticalDummy;
    int32_t result;

    if (filename == (const uint8_t *)0)
        filename = (const uint8_t *)&criticalDummy;

    assetSysPrepCopyParams(filename);

    /* Disable interrupts */
    *(volatile uint16_t *)INTENA_ADDR = 0x4000;

    result = (int32_t)RobNorthenLoader_C(4, 0, assetFilename, dest);

    /* Re-enable interrupts */
    *(volatile uint16_t *)INTENA_ADDR = 0xC000;

    return result;
}

/* ================================================================
 * AssetSys_Op5ThenMaybe6  (opcode 12)
 * Calls RNL cmd=5 (get file size). If successful (non-zero),
 * calls cmd=6 with the returned address. Returns 0 on success.
 * ================================================================ */
static int32_t assetSys_Op5ThenMaybe6(const uint8_t *sourceDesc)
{
    int32_t result;
    assetSysPrepCopyParams(sourceDesc);

    result = (int32_t)RobNorthenLoader_C(5, 0, assetFilename, (uint8_t *)assetAltBasePtr);
    if (result != 0)
    {
        /* Op 5 returned non-zero — do op 6 with it */
        /* Asm path: op-5 returned a 32-bit address; op-6 takes it as a
         * pointer. Double-cast preserves the zero-extended 32-bit value
         * on 64-bit (matching the Amiga behaviour where pointer == 32-bit
         * uint). Only used by asset-loader paths that are dead on SDL. */
        RobNorthenLoader_C(6, 0, (uint8_t *)(uintptr_t)(uint32_t)result, (uint8_t *)0);
        return 0;
    }
    return 1;
}

/* ================================================================
 * AssetSys_SelectAndOpen  (opcode 14)
 *
 * In HDD mode, the floppy drive probing loop is dead code
 * (unconditional bra.w skips it). Just sets assetCurrentIndex.
 * ================================================================ */
static int16_t assetSys_SelectAndOpen(uint16_t param)
{
    _assetCurrentIndex = param;
    return 0;
}

/* ================================================================
 * AssetSys_OpenAnySource  (opcode 16)
 *
 * Original: probes floppy drives via Op5ThenMaybe6 (cmd=5).
 * In HDD mode, floppy probing always fails (no drives), so the
 * function always returns 0 (success). Our C rob_get_file_size
 * returns real HDD file sizes (non-zero), which would incorrectly
 * trigger the failure path. Just return 0 for HDD mode.
 * ================================================================ */
static int16_t assetSys_OpenAnySource(void)
{
    /* Original ASM probes floppy drives. On HDD (no floppies), all probes
     * fail, leaving assetCurrentIndex = 4 (set before probing). This
     * enables direct file dispatch (no frontbuffer staging) which is
     * required for files larger than the frontbuffer (e.g. career saves). */
    _assetCurrentIndex = 4;
    return 0;
}

/* ================================================================
 * CAssetSys_Dispatch — replaces the asm bridge in swos2_unit.s
 *
 * C callers pass: opcode, param, name, dest
 * The asm bridge previously shuffled these into d7/d0/a0/a1 for
 * the asm jump table. Now we dispatch directly.
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t CAssetSys_Dispatch(uint16_t opcode, uint16_t param, void *name, void *dest)
{
    int16_t result;

    assetSelect = opcode;

    switch (opcode)
    {
    case 2:  result = (int16_t)assetSys_InitSource0((const uint8_t *)name, (uint8_t *)dest); break;
    case 6:  result = (int16_t)assetSys_InitSource2((const uint8_t *)name, (uint8_t *)dest); break;
    case 8:  result = (int16_t)assetSys_ResolveAssetList((uint8_t *)dest); break;
    case 10: result = (int16_t)assetSys_DoCriticalOp((const uint8_t *)name, (uint8_t *)dest); break;
    case 12: result = (int16_t)assetSys_Op5ThenMaybe6((const uint8_t *)name); break;
    case 14: result = assetSys_SelectAndOpen(param); break;
    case 16: result = assetSys_OpenAnySource(); break;
    default: result = 0; break;
    }

    assetSelect = 0xFFFF;
    return result;
}

/* ================================================================
 * CAssetSys_DispatchGetSize — like Dispatch but also returns
 * file size via _rnl_d1_return → *outFileSize
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t CAssetSys_DispatchGetSize(uint16_t opcode, uint16_t param, void *name, void *dest, uint32_t *outFileSize)
{
    int16_t result = CAssetSys_Dispatch(opcode, param, name, dest);
    if (outFileSize)
        *outFileSize = _rnl_d1_return;
    return result;
}

/* ================================================================
 * CAssetSys_DispatchWrite — write variant, passes file size as arg1
 * opcode is always LOAD_FILE (4), arg1=size goes to RNL cmd=1
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t CAssetSys_DispatchWrite(uint16_t opcode, void *filename, void *src, uint32_t size)
{
    int16_t result;

    assetSelect = opcode;
    result = (int16_t)assetSys_LoadFileOp((const uint8_t *)filename, (uint8_t *)src, size);
    assetSelect = 0xFFFF;

    return result;
}

/* ================================================================
 * AssetSys_InitDiskImageSources  (replaces asm wrapper)
 *
 * Called from mainC during boot. Sets up the asset system with
 * disk image paths, then installs the loader bootstrap.
 * ================================================================ */
__attribute__((used, externally_visible))
void AssetSys_InitDiskImageSources(void)
{
    assetSys_InitContext(&_diskImageContext,
                         kSwos4Dk1, kSwos4Dk2,
                         kGrafsDisk2Raw, kSwos4Dku);
    InstallAndInitLoaderBootstrap();
}
