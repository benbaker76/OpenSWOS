/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "swos_types.h"
#include "game_types.h"   /* CompetitionSlotBuffer (Phase B raw-save size) */
#include "file_io.h"
#ifdef SWOS_SDL_BUILD
#include "diy_import_dos.h"
#endif
#include "display.h"
#include "menus.h"
#include "audio.h"
#include "menu_system.h"
#include "string_utils.h"
#include "asset_loader.h"
#include "input_text.h"
#include "endian_helpers.h"

#ifdef SWOS_SDL_BUILD
#include <string.h>
#endif

/* Stable scratch buffer for RNC decompression staging. The compressed file is
 * copied here before decoding so the decoder never reads from a volatile display
 * buffer (frontbufferBaseAlt / frontbufferBasePtr): on the Amiga the VBlank
 * ISR's double-buffer swap can flip/redraw that buffer DURING the multi-VBlank
 * decode, corrupting the compressed stream mid-decode -> RNC bitstream desync ->
 * the output cursor runs ~1 MB past the destination and stomps unrelated globals
 * (e.g. numSelectedTeams -> bogus team count -> VerifyTeamLoaded panic; found
 * 2026-06-16 via watchpoint, Amiga-only — SDL already staged here). Shared by
 * BOTH LoadFileEx decode paths. 256 KB covers any asset. */
static uint8_t s_rnc_scratch[256 * 1024];

/* ---- External globals (all live in SWOS2.s) ---- */
extern uint16_t _assetCurrentIndex;
extern uintptr_t _assetLoaderCacheFlag;
extern uint16_t _assetLoaderAltPathReady;
extern volatile uintptr_t _frontbufferBasePtr;
#ifdef SWOS_SDL_BUILD
extern volatile uintptr_t _frontbufferBaseAltPtr;
extern uint8_t _frontbufferBaseAlt[];
#endif
extern volatile uint16_t _currentGameTick;
extern void WaitForVSync(void);   /* game_utils.c — pumps audio + events on SDL */
extern volatile uint16_t _panicReasonCode;
extern uint16_t _hilNumGoals;
extern uint16_t _hilTeam1Goals;
extern uint16_t _hilTeam2Goals;
extern uint16_t _hilPitchNumber;
extern uint16_t _hilMaxSubstitutes;
extern uint8_t _editTacticsCurrentTactics[370]; /* TeamTactics — must match edit_tactics.c */
extern uint8_t _hilFileBuffer[];

/* Error strings (all in SWOS2.s data section) */
extern uint8_t _aDiskWriteProte[];
extern uint8_t _aNoDiskInDrive[];
extern uint8_t _aFileNotFound[];
extern uint8_t _aDiskFull[];
extern uint8_t _aCannotFormatGa[];
extern uint8_t _aDiskError[];
extern uint8_t _aCompetitionFil[];
extern uint8_t _aFileSavedByDif[];
extern uint8_t _aNotAHighlights[];     /* "NOT A HIGHLIGHTS FILE" */

/* String constants (in SWOS2.s data section) */
extern uint8_t _aContinue[];          /* "CONTINUE" */
extern uint8_t _aAbort[];             /* "ABORT" */

/* Insert disk / file dialog globals */
extern uint8_t _aInsertSwosDisk_0[];  /* "\x01INSERT SWOS DISK 2 IN ANY DRIVE" */
extern uint8_t _aOk[];               /* "OK" */
extern uint8_t _aInsertGameSave[];    /* "\x01INSERT GAME SAVE DISK IN DF0:" */
extern uint8_t _aInsertBlankDis[];    /* "\x01INSERT BLANK DISK IN DF0:" */
extern uint8_t _aFormatSaveDisk[];    /* "FORMAT SAVE DISK" */
extern uint8_t _aDeleteFile[];        /* "DELETE FILE" */
extern uint8_t _aSwosu[];            /* "SWOSU" */
extern uint8_t _aSwos4Dku[];         /* "swos4.dku" */
extern uint16_t _saveFileBuffer;
extern uint16_t _menuMusic;

/* File selection dialog globals */
extern uintptr_t _extension;
extern uintptr_t _menuTitle;
extern uint8_t _inputSaveFilename[];
extern uint16_t _numberOfFiles;
extern uint16_t _totalNumberOfFiles;
extern uint16_t _numMatchedFiles;
extern uint8_t _foundFilenamesBuffer[];
extern void *_foundFilesPtrTable[];
extern uint32_t _extensionsBuffer[];
extern uint16_t _savingFile;
extern uint8_t _selectFilesMenu[];
extern uint8_t _selectedFilename[];
extern uint16_t _selFilesColumn1NumItems;
extern uint16_t _selFilesColumn2NumItems;
extern uint16_t _selFilesColumn3NumItems;
extern uint16_t _selFilesColumnStartY;
extern uint16_t _selFilesColumnEndY;
extern MenuEntry _menuEntries[];

/* ---- External asm functions (C-callable bridges in swos2_unit.s) ---- */
extern int16_t CAssetSys_DispatchGetSize(uint16_t opcode, uint16_t param, void *name, void *dest, uint32_t *outFileSize);
extern int16_t CAssetSys_Dispatch(uint16_t opcode, uint16_t param, void *name, void *dest);
extern void RncDecompressV1(uint8_t *src, uint8_t *dest);
extern void RncDecompressV2(uint8_t *src, uint8_t *dest);
extern int16_t CAssetSys_DispatchWrite(uint16_t opcode, void *filename, void *src, uint32_t size);
extern void PanicHaltLoop(void);

/* ---- External C functions (already ported) ---- */
extern int16_t AssetCacheCopyByName(uint8_t *name, uint8_t *dest, uint32_t *outFileSize);
extern void AssetCacheStoreFromMemory(uint8_t *name, uint8_t *src, uint32_t size);
extern void BlitterCopyOverlayToScreen(void);

/* ---- Constants matching asm EQU ---- */
#define ASSET_OP_INIT_SOURCE0       2
#define ASSET_OP_LOAD_FILE          4
#define ASSET_OP_INIT_SOURCE2       6
#define ASSET_OP_RESOLVE_ASSET_LIST 8
#define ASSET_OP_DO_CRITICAL        10
#define ASSET_OP_OPEN_ANY_SOURCE    16
#define FPS_50HZ                    49
#define FILENAME_ENTRY_SIZE         32
#define TACTICS_SIZE                370
#include "rnc_magic.h"

/* ================================================================
 * LoadFile  (SWOS2.s line 8825)
 *
 * Loads a file into the given buffer, using the frontbuffer as a
 * staging area for decompression and overlay.
 *
 * Flow:
 * 1. If assetCurrentIndex == 4, dispatch directly and return.
 * 2. Otherwise, redirect load into frontbuffer:
 *    a. Try cache first (AssetCacheCopyByName)
 *    b. If cache miss, load from disk (AssetSys_Dispatch)
 *    c. On disk failure, retry after 50-tick delay
 *    d. On success, optionally store to cache
 * 3. Check file size vs frontbuffer capacity (panic if too large)
 * 4. Decode from frontbuffer to real dest:
 *    - RNC v1: decompress via RncDecompressV1
 *    - RNC v2: decompress via RncDecompressV2
 *    - Raw: byte-copy from frontbuffer to dest
 * 5. Restore screen overlay via BlitterCopyOverlayToScreen
 *
 * Asm register convention:
 *   in:  a0 = filename, a1 = dest buffer
 *   out: d0 = 0 (success) or non-zero (error), d1 = file size
 *        Z flag reflects d0
 *
 * outFileSize: if non-NULL, receives the file size (or decompressed
 * size for RNC files). The asm stub uses this to return d1.
 * ================================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
int16_t LoadFileEx(uint8_t *filename, uint8_t *dest, uint32_t *outFileSize)
{
    int16_t result;
    uint32_t fileSize;
    uint8_t *frontbuf;
    uint32_t magic;
    uint32_t count;
    uint8_t *src;
    int16_t i;
    uint16_t tick;

    fileSize = 0;

    for (;;) {
        /* Direct dispatch if assetCurrentIndex == 4 */
        if (_assetCurrentIndex == 4) {
            result = CAssetSys_DispatchGetSize(ASSET_OP_INIT_SOURCE0, 0, filename, dest, &fileSize);
            /* The original Amiga asm disk pipeline stripped RNC headers
             * here, but the C asset dispatch returns RAW bytes on BOTH
             * targets: SDL `rob_load` and the Amiga C port's dispatch
             * alike (the older assumption that the Amiga pipeline
             * pre-decompresses only held for the asm build). So after a
             * successful direct dispatch, sniff the RNC header and
             * decompress in-place if present. The magic check makes this
             * a no-op for already-raw data, so it is safe on both ports.
             * Without it the Amiga build loaded e.g. team.080 still
             * RNC-compressed -> garbage 16-bit team count (0x524E="RN")
             * -> count^2 bubble sort ran away (national-coach WC load
             * hung in setupWorldCupQualifyingGroups). Not SDL-gated. */
            if (result == 0 && fileSize >= 18 &&
                dest[0] == 'R' && dest[1] == 'N' && dest[2] == 'C' &&
                (dest[3] == 0x01 || dest[3] == 0x02)) {
                if (fileSize <= sizeof(s_rnc_scratch)) {
                    /* RNC header offset 4 = unpacked size (big-endian). */
                    uint32_t unpacked = BE_READ_U32(dest + 4);
                    /* Manual byte copy (no memcpy: freestanding Amiga
                     * build has no <string.h> here; matches the raw-data
                     * copy path below). */
                    uint32_t k;
                    for (k = 0; k < fileSize; k++)
                        s_rnc_scratch[k] = dest[k];
                    if (dest[3] == 0x01) RncDecompressV1(s_rnc_scratch, dest);
                    else                  RncDecompressV2(s_rnc_scratch, dest);
                    fileSize = unpacked;
                }
            }
#ifdef SWOS_SDL_BUILD
            /* The Amiga `BlitterCopyOverlayToScreen` would have copied
             * the alt → primary buffer for us in the staging path. The
             * early-exit branch skipped that on Amiga because the asset
             * loader wrote directly to the displayed buffer. On SDL,
             * mirror that by stamping the displayed buffer if `dest`
             * is the alt buffer. */
            if (result == 0 && (uint8_t *)_frontbufferBasePtr != dest &&
                ((uintptr_t)dest == (uintptr_t)_frontbufferBaseAlt ||
                 (uintptr_t)dest == _frontbufferBaseAltPtr)) {
                uint32_t cap = FRONTBUFFER_SIZE_TOTAL_BYTES;
                memcpy((void *)_frontbufferBasePtr, dest,
                       fileSize < cap ? fileSize : cap);
            }
#endif
            if (outFileSize)
                *outFileSize = fileSize;
            return result;
        }

        /* Stage load through frontbuffer */
        frontbuf = (uint8_t *)_frontbufferBasePtr;

        /* Try cache first */
        if (_assetLoaderCacheFlag != 0) {
            if (AssetCacheCopyByName(filename, frontbuf, &fileSize) == 0)
                goto size_check_and_decode;
        }

        /* Load from disk into frontbuffer */
        result = CAssetSys_DispatchGetSize(ASSET_OP_INIT_SOURCE0, 0, filename, frontbuf, &fileSize);
        if (result != 0) {
            /* Load failed — retry if not index 4 */
            if (_assetCurrentIndex == 4) {
                if (outFileSize)
                    *outFileSize = fileSize;
                return result;
            }

            /* Wait 50 ticks before retry */
            for (i = FPS_50HZ; i >= 0; i--) {
                WaitForVSync();
            }
            continue; /* retry LoadFile */
        }

        /* On success, store to cache if appropriate */
        if (_assetLoaderCacheFlag != 0 && _assetLoaderAltPathReady != 0) {
            AssetCacheStoreFromMemory(filename, frontbuf, fileSize);
        }

    size_check_and_decode:
        /* Check file size vs frontbuffer capacity */
        if (fileSize > FRONTBUFFER_SIZE_TOTAL_BYTES) {
            _panicReasonCode = 0xE000u;
            PanicHaltLoop();
            /* never returns */
        }

        /* DO NOT re-read frontbufferBasePtr here. The VBlank ISR swaps
         * front/back buffers and `frontbufferBasePtr` can flip between
         * the disk-load (line ~229) and this point. Re-reading would
         * point us at the OTHER (stale) buffer and the decompressor
         * would parse old display data as RNC, picking up a garbage
         * `unpackSize` and walking the output pointer off the end of
         * fast RAM (bus error at PC inside the fill loop). Keep the
         * frontbuf we already saved before the dispatch — that's the
         * buffer the disk read wrote into. */
        magic = *(uint32_t *)frontbuf;

        if (magic == RNC_MAGIC_V1 || magic == RNC_MAGIC_V2) {
            /* Stage the COMPRESSED file into the stable scratch before decoding.
             * frontbuf is a display buffer and the VBlank double-buffer swap can
             * flip/redraw it during the multi-VBlank decode, corrupting the
             * stream mid-decode -> bitstream desync -> the output cursor runs off
             * the end of `dest` and stomps unrelated globals (see s_rnc_scratch
             * note above; this was the Amiga numSelectedTeams=0x0707 overrun).
             * Copy the on-disk (compressed) byte count, i.e. the current
             * fileSize, BEFORE it is overwritten with the unpacked size. */
            uint8_t *rncSrc = frontbuf;
            if (fileSize <= sizeof(s_rnc_scratch)) {
                uint32_t k;
                for (k = 0; k < fileSize; k++)
                    s_rnc_scratch[k] = frontbuf[k];
                rncSrc = s_rnc_scratch;
            }
            /* RNC header offset 4 = unpacked size (big-endian). A
             * native uint32 load on LE would byte-swap it (e.g.
             * 0x00007AEA becomes 0xEA7A0000 ≈ 3.9 GB) and trip the
             * > 64000 sanity check at LoadTeamFile. */
            fileSize = BE_READ_U32(frontbuf + 4);
            if (magic == RNC_MAGIC_V1)
                RncDecompressV1(rncSrc, dest);
            else
                RncDecompressV2(rncSrc, dest);
        } else {
            /* Raw data: byte-copy from frontbuffer to dest */
            src = frontbuf;
            count = fileSize;
            while (count != 0) {
                *dest++ = *src++;
                count--;
            }
        }

        /* Restore screen overlay */
        BlitterCopyOverlayToScreen();

        if (outFileSize)
            *outFileSize = fileSize;
        return 0;
    }
}

/* ================================================================
 * WriteFile  (SWOS2.s line 8925)
 *
 * Writes data to a file via AssetSys_Dispatch opcode 4.
 * The dispatch function uses d1 = size, which is passed via
 * the CAssetSys_DispatchWrite bridge.
 *
 * Asm register convention:
 *   in:  a0 = filename, a1 = src buffer, d1 = size
 *   out: d0 = 0 (success) or non-zero (error), Z flag reflects d0
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t WriteFile(uint8_t *filename, uint8_t *src, uint32_t size)
{
    return CAssetSys_DispatchWrite(ASSET_OP_LOAD_FILE, filename, src, size);
}

/* ================================================================
 * ShowFsErrorByCode  (SWOS2.s line 89147)
 *
 * Maps a filesystem error code to a human-readable error string
 * and shows it in the error menu.
 *
 * Error codes:
 *   0x1C   = DISK WRITE PROTECTED
 *   0x1D   = NO DISK IN DRIVE
 *   0xCD   = FILE NOT FOUND
 *   0xDD   = DISK FULL
 *   0xFF9C = CANNOT FORMAT GAME DISK
 *   other  = DISK ERROR (default)
 * ================================================================ */
__attribute__((used, externally_visible))
void ShowFsErrorByCode(uint16_t errCode)
{
    uint8_t *msg;

    if (errCode == 0x1C)
        msg = _aDiskWriteProte;
    else if (errCode == 0x1D)
        msg = _aNoDiskInDrive;
    else if (errCode == 0xCD)
        msg = _aFileNotFound;
    else if (errCode == 0xDD)
        msg = _aDiskFull;
    else if (errCode == 0xFF9C)
        msg = _aCannotFormatGa;
    else
        msg = _aDiskError;

    ShowErrorMenu(msg);
}

/* ================================================================
 * LoadCareerFile  (SWOS2.s line 121220)
 *
 * Loads a career file: saves current options, loads file into
 * careerFileBuffer, validates competition header, and cycles music.
 * On any error, restores options and reinitializes tactics.
 *
 * Returns: 0 = success, 1 = error
 * ================================================================ */
#include "save_format_json.h"

__attribute__((used, externally_visible))
int16_t LoadCareerFile(uint8_t *filename)
{
    SaveOptions();

    /* JSON is the only career format. The legacy raw-memory-dump reader was
     * retired (2026-06-10): after the careerFileBuffer overlay untangle it would
     * load the extracted fields into dead .skip holes -> broken career, and the
     * writer is already JSON-only (SaveCareerFile). A non-JSON .CAR/.SEA is now
     * unsupported (graceful error). The JSON load path runs its own pointer
     * fixups (RebuildPointersAfterLoad inside SaveJson_LoadCareer). See docs/
     * SAVE_DATA.md "Format migration / read-support policy". */
    if (SaveJson_FileIsJson(filename) != 1 ||
        SaveJson_LoadCareer(filename, NULL, 0) != 0) {
        ShowErrorMenu(_aFileSavedByDif);
        RestoreOptions();
        InitUserTactics();
        return 1;
    }
    CycleMenuMusic();
    return 0;
}

/* ================================================================
 * SaveCareerFile  (SWOS2.s line 121244)
 *
 * Saves the career file. JSON-only (see body).
 *
 * Returns: 0 = success, 1 = error (after showing error menu)
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t SaveCareerFile(uint8_t *filename)
{
    /* JSON is the only career write format. The legacy raw-memory-dump
     * writer was retired because it embedded build-specific pointer
     * values and required a careerFileBuffer-shaped on-disk layout.
     * The read path still accepts legacy .CAR (see LoadCareerFile).
     *
     * SaveJson_WriteCareer surfaces WriteFile errors via
     * ShowFsErrorByCode internally, so we just propagate its 0/1
     * result here. */
    return SaveJson_WriteCareer(filename);
}

/* ================================================================
 * LoadPresetFile  (SWOS2.s line 121272)
 *
 * Loads a preset/competition file into competitionFileBuffer.
 * Same pattern as LoadCareerFile but uses competitionFileBuffer.
 *
 * Returns: 0 = success, 1 = error
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t LoadPresetFile(uint8_t *filename)
{
    SaveOptions();

    /* Route a loaded preset to the preset menu, not the career contestMenu.
     * gameType lives at 0x12cece — BELOW every save region — so NO save type
     * persists it (a fresh preset/DIY sets it via InitCareerVariables); the
     * load path never did, so StartContest read a stale career/season gameType
     * and mis-routed -> CareerGameListCommon SIGSEGV on null
     * careerCompetitionSlotPtrTable / currentCompetitionPtr. Only caller
     * (LoadOldCompetitionMenu) resets gameType=0 on our failure return, so
     * setting it up-front is safe. See project_diy_preset_load_gametype. */
    extern uint16_t _gameType;
    _gameType = 2; /* GAME_TYPE_PRESET_COMPETITION (menus.c:2216) */

    /* JSON is the only preset format. The legacy raw read loaded a whole
     * careerFileBuffer-overlay-region .PRE dump into the now-standalone
     * competitionFileBuffer (a fixed-size CompetitionSlotBuffer) — post-untangle
     * that overflows the buffer and the typed slots no longer line up, exactly
     * like the retired career raw read (5a78f4f). JSON-only; a non-JSON .PRE is
     * unsupported (graceful error). SaveJson_LoadPreset restores the typed
     * competition slot and re-derives the tournament/cup identities itself; it
     * deliberately does NOT run the career RebuildPointersAfterLoad (a routed
     * .PRE goes to the DIY/preset menu, not the career contestMenu). See
     * docs/SAVE_DATA.md "Format migration / read-support policy". */
    if (SaveJson_FileIsJson(filename) != 1 ||
        SaveJson_LoadPreset(filename, NULL, 0) != 0) {
        ShowErrorMenu(_aFileSavedByDif);
        RestoreOptions();
        InitUserTactics();
        return 1;
    }
    CycleMenuMusic();
    return 0;
}

/* ================================================================
 * SavePresetCompetitionFile  (SWOS2.s line 121296)
 *
 * Saves a preset/competition file. JSON-only (see body).
 *
 * Returns: 0 = success, 1 = error
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t SavePresetCompetitionFile(uint8_t *filename)
{
    /* JSON is the only preset write format (mirrors SaveCareerFile). The
     * legacy raw WriteFile of sizeof(CompetitionSlotBuffer) was a raw memory
     * dump of a struct region — the exact "no raw memory dumps" violation the
     * JSON format exists to remove — and its reader is already retired
     * (LoadPresetFile is JSON-only). SaveJson_WritePreset surfaces WriteFile
     * errors via ShowFsErrorByCode internally, so we just propagate its 0/1. */
    return SaveJson_WritePreset(filename);
}

/* ================================================================
 * LoadDIYFile  (SWOS2.s line 121322)
 *
 * Loads a DIY competition file into competitionFileBuffer.
 * Identical logic to LoadPresetFile.
 *
 * Returns: 0 = success, 1 = error
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t LoadDIYFile(uint8_t *filename)
{
    SaveOptions();

    /* Route a loaded DIY competition to the DIY menu, not the career
     * contestMenu — see the gameType note in LoadPresetFile. */
    extern uint16_t _gameType;
    _gameType = 1; /* GAME_TYPE_DIY_COMPETITION (menus.c:2215) */

    /* JSON is the only DIY format. The legacy raw read loaded a whole
     * careerFileBuffer-overlay-region .DIY dump into the now-standalone
     * competitionFileBuffer (a fixed-size CompetitionSlotBuffer) — post-untangle
     * that overflows the buffer and the typed slots no longer line up, exactly
     * like the retired career raw read (5a78f4f). JSON-only; a non-JSON .DIY is
     * unsupported (graceful error). SaveJson_LoadDIY restores the typed
     * competition slot and re-derives the tournament/cup identities itself; it
     * deliberately does NOT run the career RebuildPointersAfterLoad (a routed
     * .DIY goes to the DIY menu, not the career contestMenu). See
     * docs/SAVE_DATA.md "Format migration / read-support policy". */
    /* A .DIY from the PC game or SWOS 2020 (their raw DOS dump) is imported
     * into the same state (diy_import_dos.c, SDL only: the Amiga refuses it). */
#ifdef SWOS_SDL_BUILD
    if (SaveJson_FileIsJson(filename) == 1
            ? SaveJson_LoadDIY(filename, NULL, 0) != 0
            : DiyImportDos_Load(filename) != 0) {
#else
    if (SaveJson_FileIsJson(filename) != 1 ||
        SaveJson_LoadDIY(filename, NULL, 0) != 0) {
#endif
        ShowErrorMenu(_aFileSavedByDif);
        RestoreOptions();
        InitUserTactics();
        return 1;
    }
    CycleMenuMusic();
    return 0;
}

/* ================================================================
 * SaveDIYFile  (SWOS2.s line 121344)
 *
 * Saves a DIY competition file. JSON-only (identical to
 * SavePresetCompetitionFile, just a different file_kind).
 *
 * Returns: 0 = success, 1 = error
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t SaveDIYFile(uint8_t *filename)
{
    /* JSON is the only DIY write format (mirrors SaveCareerFile). The legacy
     * raw WriteFile of sizeof(CompetitionSlotBuffer) was a raw memory dump of a
     * struct region — the exact "no raw memory dumps" violation the JSON format
     * exists to remove — and its reader is already retired (LoadDIYFile is
     * JSON-only). SaveJson_WriteDIY surfaces WriteFile errors via
     * ShowFsErrorByCode internally, so we just propagate its 0/1. */
    return SaveJson_WriteDIY(filename);
}

/* ================================================================
 * FindFilesExtensionPresent  (SWOS2.s line 8501)
 *
 * Checks if files with the given extension exist by dispatching
 * ASSET_OP_RESOLVE_ASSET_LIST through the asset system.
 *
 * Asm callers set a1 = &numberOfFiles before calling. The asset
 * dispatch writes the directory listing there: first word = count,
 * followed by 32-byte filename entries (foundFilenamesBuffer).
 *
 * Asm register convention:
 *   in:  a0 = extension string, a1 = &numberOfFiles (dest buffer)
 *   out: d0 = result from dispatch
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t FindFilesExtensionPresent(uint8_t *ext)
{
    int16_t r = CAssetSys_Dispatch(ASSET_OP_RESOLVE_ASSET_LIST, 0, ext, (void *)&_numberOfFiles);
    return r;
}

/* ================================================================
 * ReadTacticsFile  (SWOS2.s line 121232)
 *
 * Loads a tactics file into frontbuffer, copies 370 bytes to
 * editTacticsCurrentTactics, restores screen overlay.
 *
 * Asm register convention:
 *   in:  a0 = filename
 *   out: d0 = 0 (success) or 1 (error), Z flag reflects d0
 * ================================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
int16_t ReadTacticsFile(uint8_t *filename)
{
    int16_t result;
    int16_t i;

    /* JSON path: parse directly from disk into editTacticsCurrentTactics. */
    if (SaveJson_FileIsJson(filename) == 1) {
        int16_t jr = SaveJson_LoadTactics(filename, _editTacticsCurrentTactics,
                                          TACTICS_SIZE);
        if (jr != 0) {
            ShowErrorMenu(_aFileSavedByDif);
            return 1;
        }
        return 0;
    }

    result = LoadFileEx(filename, (uint8_t *)_frontbufferBasePtr, (uint32_t *)0);
    if (result != 0) {
        BlitterCopyOverlayToScreen();
        ShowFsErrorByCode((uint16_t)result);
        return 1;
    }

    {
        uint8_t *src = (uint8_t *)_frontbufferBasePtr;
        uint8_t *dst = _editTacticsCurrentTactics;
        for (i = TACTICS_SIZE - 1; i >= 0; i--) {
            *dst++ = *src++;
        }
    }

    BlitterCopyOverlayToScreen();
    return 0;
}

/* ================================================================
 * WriteTacticsFile  (SWOS2.s line 121262)
 *
 * Writes 370 bytes from buffer to a tactics file.
 *
 * Asm register convention:
 *   in:  a0 = filename, a1 = buffer
 *   out: d0 = 0 (success) or 1 (error), Z flag reflects d0
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t WriteTacticsFile(uint8_t *filename, uint8_t *buffer)
{
    int16_t result;

#ifdef SWOS_SAVE_FORMAT_JSON
    {
        int16_t jr = SaveJson_WriteTactics(filename, buffer, TACTICS_SIZE);
        if (jr == 0)
            return 0;
        /* JSON path failed — fall through to legacy writer so we
         * still produce something on disk. */
    }
#endif

    result = WriteFile(filename, buffer, TACTICS_SIZE);
    if (result == 0)
        return 0;

    ShowFsErrorByCode((uint16_t)result);
    return 1;
}

/* ================================================================
 * LoadHilFile  (SWOS2.s line 121273)
 *
 * Loads a highlights file into hilFileBuffer.
 * On error, clears hilNumGoals and shows error.
 *
 * Asm register convention:
 *   in:  a0 = filename
 *   out: d0 = 0 (success) or 1 (error)
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t LoadHilFile(uint8_t *filename)
{
    int16_t result;

    if (SaveJson_FileIsJson(filename) == 1) {
        int16_t jr = SaveJson_LoadHighlights(filename, NULL, 0);
        if (jr != 0) {
            _hilNumGoals = 0;
            ShowErrorMenu(_aFileSavedByDif);
            return 1;
        }
        return 0;
    }

#ifdef SWOS_SDL_BUILD
    {
        /* DOS / SWOS-2020 .HIL: little-endian, 3626-byte header, different sprite
         * numbering, and up to 613 KB -- more than LoadFileEx can stage. Read and
         * converted into our layout by dos_replay_load.c, so the normal replay path
         * plays it. */
        extern int DosReplay_LoadHil(uint8_t *filename);
        extern int _replayRecordingIsWide;
        extern int _replayPlaybackFps;
        _replayRecordingIsWide = 0;   /* native Amiga HIL is 336-wide; DosReplay sets it for DOS/2020 */
        _replayPlaybackFps = 50;      /* native Amiga = 50 Hz; DosReplay_LoadHil sets the file's own */
        int dr = DosReplay_LoadHil(filename);
        if (dr == 0)
            return 0;
        if (dr > 0) {
            _hilNumGoals = 0;
            ShowErrorMenu(_aNotAHighlights);
            return 1;
        }
    }
#endif
    uint32_t hilSize = 0;
    result = LoadFileEx(filename, _hilFileBuffer, &hilSize);
    if (result == 0) {
        /* Legacy on-disk format stores the 5 header u16s in BE (Amiga
         * m68k). The original loader left them BE in memory and
         * highlights_menu.c followed up with an in-place BE_TO_HOST
         * swap. Doing the swap right here means JSON loads — which
         * write host-native directly — can skip it, and there's a
         * single canonical place for the byte-order normalisation. */
        _hilNumGoals       = BE16_TO_HOST(_hilNumGoals);
        _hilTeam1Goals     = BE16_TO_HOST(_hilTeam1Goals);
        _hilTeam2Goals     = BE16_TO_HOST(_hilTeam2Goals);
        _hilPitchNumber    = BE16_TO_HOST(_hilPitchNumber);
        _hilMaxSubstitutes = BE16_TO_HOST(_hilMaxSubstitutes);
        return 0;
    }

    _hilNumGoals = 0;
    ShowFsErrorByCode((uint16_t)result);
    return 1;
}

/* ================================================================
 * DoSaveHighlights  (SWOS2.s line 121287)
 *
 * Saves highlights data. Size = hilNumGoals * 19000 + 2254 (header).
 *
 * Asm register convention:
 *   in:  a0 = filename
 *   out: d0 = 0 (success) or 1 (error)
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t DoSaveHighlights(uint8_t *filename)
{
    uint32_t size;
    int16_t result;

#ifdef SWOS_SAVE_FORMAT_JSON
    /* Dispatch BEFORE the legacy BE swap dance so the JSON writer
     * sees the live host-native u16 header values. */
    {
        int16_t jr = SaveJson_WriteHighlights(filename);
        if (jr == 0) return 0;
    }
#endif

    size = (uint32_t)_hilNumGoals * 19000u + 2254u;

    /* HIL file header is big-endian on disk for Amiga compatibility.
     * Swap the 5 uint16 header fields host→BE for the duration of the
     * write, then swap back so in-memory state stays in host order for
     * the rest of the game. No-op on the Amiga BE build. */
    _hilNumGoals       = HOST_TO_BE16(_hilNumGoals);
    _hilTeam1Goals     = HOST_TO_BE16(_hilTeam1Goals);
    _hilTeam2Goals     = HOST_TO_BE16(_hilTeam2Goals);
    _hilPitchNumber    = HOST_TO_BE16(_hilPitchNumber);
    _hilMaxSubstitutes = HOST_TO_BE16(_hilMaxSubstitutes);

    result = WriteFile(filename, _hilFileBuffer, size);

    _hilNumGoals       = BE16_TO_HOST(_hilNumGoals);
    _hilTeam1Goals     = BE16_TO_HOST(_hilTeam1Goals);
    _hilTeam2Goals     = BE16_TO_HOST(_hilTeam2Goals);
    _hilPitchNumber    = BE16_TO_HOST(_hilPitchNumber);
    _hilMaxSubstitutes = BE16_TO_HOST(_hilMaxSubstitutes);

    if (result == 0)
        return 0;

    ShowFsErrorByCode((uint16_t)result);
    return 1;
}

/* ================================================================
 * DiskMenuRet  (SWOS2.s line 90480)
 *
 * No-op callback referenced by diskMenu data (onInit/onReturn).
 * ================================================================ */
__attribute__((used, externally_visible))
void DiskMenuRet(void)
{
}

/* ================================================================
 * ShowInsertDisk2Dialog  (SWOS2.s line 121038)
 *
 * Loops: tries AssetSvcLoaderOp2 to check disk readiness. If not
 * ready, shows a "INSERT SWOS DISK 2" prompt with OK (no abort).
 * Loops until disk is found.
 *
 * Asm register convention:
 *   in:  a0 = header string (title for the prompt)
 *   out: none (returns when disk is ready)
 * ================================================================ */
__attribute__((used, externally_visible))
void ShowInsertDisk2Dialog(uint8_t *header)
{
    for (;;) {
        if (AssetSvcLoaderOp2() == 0)
            return; /* disk ready */
        /* Show prompt — no abort button (null), always retry */
        DoContinueAbortMenu(header, _aInsertSwosDisk_0, _aOk, (uint8_t *)0, 1);
    }
}

/* ================================================================
 * Asset_PromptInsertSaveDiskDf0  (SWOS2.s line 121053)
 *
 * Prompts user to insert save disk. If user clicks OK, tries to
 * open the save disk source. Retries on disk error, aborts if
 * user clicks ABORT.
 *
 * Asm register convention:
 *   in:  a0 = title string (displayed as prompt header)
 *   out: d0 = 0 (disk opened successfully) or non-zero (user aborted)
 *        Z flag reflects d0
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t Asset_PromptInsertSaveDiskDf0(uint8_t *title)
{
    int16_t result;

    for (;;) {
        result = DoContinueAbortMenu(title, _aInsertGameSave,
                                      _aOk, (uint8_t *)_aAbort, 0);
        if (result != 0)
            return result; /* user aborted */

        result = CAssetSys_Dispatch(ASSET_OP_OPEN_ANY_SOURCE, 0,
                                     (void *)0, (void *)0);
        if (result == 0)
            return 0; /* disk opened successfully */
        /* disk error — retry the prompt */
    }
}

/* ================================================================
 * FormatSaveDiskMenu  (SWOS2.s line 90593)
 *
 * Menu callback: prompts to insert blank disk, formats it,
 * writes swos4.dku header, and restores music.
 *
 * Asm register convention:
 *   no params, no return (menu callback)
 * ================================================================ */
__attribute__((used, externally_visible))
void FormatSaveDiskMenu(void)
{
    int16_t result;

    for (;;) {
        result = DoContinueAbortMenu((uint8_t *)_aFormatSaveDisk,
                                      _aInsertBlankDis,
                                      (uint8_t *)_aContinue,
                                      (uint8_t *)_aAbort, 1);
        if (result != 0)
            return; /* user aborted */

        result = CAssetSys_Dispatch(ASSET_OP_OPEN_ANY_SOURCE, 0,
                                     (void *)0, (void *)0);
        if (result == 0)
            break; /* disk ready */
        if (result == 0x1D)
            continue; /* no disk — retry prompt */
        ShowFsErrorByCode((uint16_t)result);
        return;
    }

    /* Disk ready — format (DO_CRITICAL) */
    StopMusic();
    result = CAssetSys_Dispatch(ASSET_OP_DO_CRITICAL, 0,
                                 (void *)_aSwosu, (void *)0);
    if (_menuMusic != 0) {
        TryPlayMusic();
    }
    if (result != 0) {
        ShowFsErrorByCode((uint16_t)result);
        return;
    }

    /* Load swos4.dku save file header (2 bytes) */
    result = CAssetSys_DispatchWrite(ASSET_OP_LOAD_FILE,
                                      (void *)_aSwos4Dku,
                                      (void *)&_saveFileBuffer, 2);
    if (result != 0) {
        ShowFsErrorByCode((uint16_t)result);
    }
}

/* ================================================================
 * DeleteFileMenu  (SWOS2.s line 90638)
 *
 * Menu callback: prompts for save disk, shows file list, deletes
 * selected file.
 *
 * Asm register convention:
 *   no params, no return (menu callback)
 * ================================================================ */
__attribute__((used, externally_visible))
void DeleteFileMenu(void)
{
    int16_t result;

    if (Asset_PromptInsertSaveDiskDf0(_aDeleteFile) != 0)
        return;

    if (GetFilenameAndExtension(0, _aDeleteFile, _aDeleteFile) != 0)
        return;

    /* The file to delete comes back in `_selectedFilename`, and it has to be
     * PASSED ON: opcode 6 is the delete, and it deletes whatever name it is
     * given. The asm makes this implicit — GetFilenameAndExtension ends
     * `lea (selectedFilename).l,a0 / rts` (SWOS2.s:93604..), so a0 still holds
     * the chosen name when DeleteFileMenu calls AssetSys_Dispatch two lines
     * later. Porting that as a plain `!= 0` status check dropped the register,
     * and passing 0 here asked the loader to delete the empty string — the menu
     * ran, reported nothing, and left the file alone ("DELETE FILE does not
     * work"). */
    result = CAssetSys_Dispatch(ASSET_OP_INIT_SOURCE2, 0,
                                 (void *)_selectedFilename, (void *)0);
    if (result != 0) {
        ShowFsErrorByCode((uint16_t)result);
    }
}

/* ================================================================
 * ExitDisk  (SWOS2.s line 90654)
 *
 * Menu callback: shows insert disk 2 dialog, then exits menu.
 *
 * Asm register convention:
 *   no params, no return (menu callback)
 * ================================================================ */
__attribute__((used, externally_visible))
void ExitDisk(void)
{
    ShowInsertDisk2Dialog(_aFormatSaveDisk);
    SetExitMenuFlag();
}

/* ================================================================
 * SelectFileToSaveDialog  (SWOS2.s line 89141)
 *
 * Shows file selection dialog for saving. Scans file list from
 * asset system, filters by known extensions (.DIY, .PRE, .SEA,
 * .CAR, .TAC, .HIL), and shows menu for user selection.
 *
 * Asm register convention:
 *   in:  d0 = filter extension (uint32_t, e.g. '.DIY' = 0x2E444959)
 *        a0 = default filename string (copied to save dialog input)
 *        a1 = menu title string
 *   out: d0 = 0 (file selected) or 1 (error/no selection)
 *        selectedFilename global contains the chosen filename
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t SelectFileToSaveDialog(uint32_t filterExt, uint8_t *defaultFilename,
                                uint8_t *menuTitleStr)
{
    int16_t result;
    uint16_t count;
    uint8_t *fileEntry;
    void **ptrTableCursor;
    uint32_t *extTableCursor;
    uint8_t *scan;
    uint32_t ext;
    uint16_t i;

    _extension = filterExt;
    _menuTitle = (uintptr_t)menuTitleStr;
    StringCopy(defaultFilename, _inputSaveFilename);

    result = FindFilesExtensionPresent((uint8_t *)0);
    if (result != 0) {
        ShowFsErrorByCode((uint16_t)result);
        return 1;
    }

    _totalNumberOfFiles = 0;
    _numMatchedFiles = 0;
    count = _numberOfFiles;

    if (count != 0) {
        count--;
        fileEntry = _foundFilenamesBuffer;
        ptrTableCursor = _foundFilesPtrTable;
        extTableCursor = _extensionsBuffer;

        for (i = count; ; i--) {
            /* Find the '.' in the filename */
            scan = fileEntry;
            while (*scan != 0x2E) /* '.' */
                scan++;

            /* Build 4-byte extension from '.' + 3 chars */
            ext = ((uint32_t)scan[0] << 24) |
                  ((uint32_t)scan[1] << 16) |
                  ((uint32_t)scan[2] << 8) |
                  (uint32_t)scan[3];

            /* Check against known extensions */
            if (ext == 0x2E444959u || /* .DIY */
                ext == 0x2E505245u || /* .PRE */
                ext == 0x2E534541u || /* .SEA */
                ext == 0x2E434152u || /* .CAR */
                ext == 0x2E544143u || /* .TAC */
                ext == 0x2E48494Cu)   /* .HIL */
            {
                _totalNumberOfFiles++;
                if (ext == _extension && _numMatchedFiles < 45) {   /* the menu's 45 slots */
                    _numMatchedFiles++;
                    *ptrTableCursor++ = fileEntry;
                    *extTableCursor++ = ext;
                }
            }

            fileEntry += FILENAME_ENTRY_SIZE;
            if (i == 0) break;
        }
    }

    _savingFile = 1;
    ShowMenu(_selectFilesMenu);

    if (_selectedFilename[0] != 0)
        return 0; /* file selected */

    return 1; /* no selection */
}

/* ================================================================
 * SelectFileOfExtension -- the LOAD picker, showing ONE extension
 *
 * The same screen the saved competitions use (_selectFilesMenu), scanned the same way,
 * but the list is filtered to `ext` alone rather than the whole save-file whitelist: a
 * CHALLENGES screen that also listed careers and tactics would be a file browser, not a
 * list of positions. Returns 0 with _selectedFilename set, or 1 if nothing was chosen.
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t SelectFileOfExtension(uint32_t ext, uint8_t *menuTitleStr)
{
    int16_t result;
    uint16_t count, i;
    uint8_t *fileEntry, *scan;
    void **ptrTableCursor;
    uint32_t *extTableCursor, thisExt;

    _extension = ext;
    _menuTitle = (uintptr_t)menuTitleStr;
    _inputSaveFilename[0] = 0;
    _selectedFilename[0] = 0;

    result = FindFilesExtensionPresent((uint8_t *)0);
    if (result != 0) {
        ShowFsErrorByCode((uint16_t)result);
        return 1;
    }

    _totalNumberOfFiles = 0;
    _numMatchedFiles = 0;
    count = _numberOfFiles;
    if (count != 0) {
        count--;
        fileEntry = _foundFilenamesBuffer;
        ptrTableCursor = _foundFilesPtrTable;
        extTableCursor = _extensionsBuffer;
        for (i = count; ; i--) {
            scan = fileEntry;
            while (*scan != 0x2E)        /* '.' */
                scan++;
            thisExt = ((uint32_t)scan[0] << 24) | ((uint32_t)scan[1] << 16) |
                      ((uint32_t)scan[2] << 8) | (uint32_t)scan[3];
            if (thisExt == ext && _numMatchedFiles < 45) {   /* the menu's 45 slots */
                _totalNumberOfFiles++;
                _numMatchedFiles++;
                *ptrTableCursor++ = fileEntry;
                *extTableCursor++ = thisExt;
            }
            fileEntry += FILENAME_ENTRY_SIZE;
            if (i == 0) break;
        }
    }

    _savingFile = 0;
    ShowMenu(_selectFilesMenu);

    return (_selectedFilename[0] != 0) ? 0 : 1;
}

/* ================================================================
 * GetFilenameAndExtension  (SWOS2.s line 89211)
 *
 * Shows file selection dialog for loading/opening. Similar to
 * SelectFileToSaveDialog but adds ALL matching files (not just
 * the filter extension), and sets savingFile=0.
 *
 * Asm register convention:
 *   in:  d0 = preferred extension (uint32_t, or 0 for "all")
 *        a0 = default filename string
 *        a1 = menu title string
 *   out: d0 = 0 (file selected) or 1 (error/no selection)
 *        selectedFilename global contains the chosen filename
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t GetFilenameAndExtension(uint32_t preferredExt, uint8_t *defaultFilename,
                                 uint8_t *menuTitleStr)
{
    int16_t result;
    uint16_t count;
    uint8_t *fileEntry;
    void **ptrTableCursor;
    uint32_t *extTableCursor;
    uint8_t *scan;
    uint32_t ext;
    uint16_t i;

    _extension = preferredExt;
    _menuTitle = (uintptr_t)menuTitleStr;
    StringCopy(defaultFilename, _inputSaveFilename);

    result = FindFilesExtensionPresent((uint8_t *)0);
    if (result != 0) {
        ShowFsErrorByCode((uint16_t)result);
        return 1;
    }

    _totalNumberOfFiles = 0;
    _numMatchedFiles = 0;
    count = _numberOfFiles;

    if (count != 0) {
        count--;
        fileEntry = _foundFilenamesBuffer;
        ptrTableCursor = _foundFilesPtrTable;
        extTableCursor = _extensionsBuffer;

        for (i = count; ; i--) {
            /* Find the '.' in the filename */
            scan = fileEntry;
            while (*scan != 0x2E) /* '.' */
                scan++;

            /* Build 4-byte extension from '.' + 3 chars */
            ext = ((uint32_t)scan[0] << 24) |
                  ((uint32_t)scan[1] << 16) |
                  ((uint32_t)scan[2] << 8) |
                  (uint32_t)scan[3];

            /* Check against known extensions */
            /* The menu has 45 slots (_foundFilesPtrTable, _extensionsBuffer) and
             * nothing else stops at them: a save folder with more saves wrote past both.
             * Automatically saved replays ([replays] auto_save) make that likely. */
            if (_totalNumberOfFiles >= 45)
                break;
            if (ext == 0x2E444959u || /* .DIY */
                ext == 0x2E505245u || /* .PRE */
                ext == 0x2E534541u || /* .SEA */
                ext == 0x2E434152u || /* .CAR */
                ext == 0x2E544143u || /* .TAC */
                ext == 0x2E48494Cu || /* .HIL */
                ext == 0x2E52504Cu    /* .RPL (PC whole-match replay) */
#ifdef SWOS_SDL_BUILD
                || ext == 0x2E535250u /* .SRP (OpenSWOS whole-match replay) */
#endif
                )
            {
                _totalNumberOfFiles++;
                _numMatchedFiles++;
                *ptrTableCursor++ = fileEntry;
                *extTableCursor++ = ext;
            }

            fileEntry += FILENAME_ENTRY_SIZE;
            if (i == 0) break;
        }
    }

    _savingFile = 0;
    ShowMenu(_selectFilesMenu);

    if (_selectedFilename[0] != 0)
        return 0; /* file selected */

    return 1; /* no selection */
}

/* ---- Chunk 140: File Selection Helpers ---- */

/* Extern: string copy utility */
extern uint8_t *StringCopy(const uint8_t *src, uint8_t *dst);

/* Copy filename to selectedFilename, extract 3-char extension into packed uint32_t,
   and signal menu exit. Extension is packed as: '.', c1, c2, c3 (big-endian). */
static void CopyFilenameExtensionAndSetExitMenuFlag(uint8_t *filename) {
    StringCopy(filename, _selectedFilename);
    /* Scan for '.' in the copied filename */
    uint8_t *p = _selectedFilename;
    while (*p++ != '.')
        ;
    /* Build packed 4-byte extension: '.', char1, char2, char3 */
    uint32_t ext = '.';
    ext = (ext << 8) | *p++;
    ext = (ext << 8) | *p++;
    ext = (ext << 8) | *p++;
    _extension = ext;
    SetExitMenuFlag();
}

/* Menu callback: select the save filename and exit the menu. */
__attribute__((used, externally_visible))
void SelectFilenameToSaveAndExit(void) {
    CopyFilenameExtensionAndSetExitMenuFlag(_inputSaveFilename);
}

/* Append the stored extension to filename, copy to selectedFilename, and signal menu exit.
   Called from asm SelectFilesInputSaveFilenameOnSelect when user enters a name.
   `extension` is a packed uint32 in big-endian char order — i.e. for
   '.CAR' the value is 0x2E434152 = ('.' << 24) | ('C' << 16) | ('A' << 8) | 'R',
   so the high byte is the leading '.' and the low byte is the last
   character. Extracting via shifts is endian-safe; reading the four
   raw bytes via `(uint8_t *)&extension` would emit them in reverse on
   an LE host (e.g. "RAC." appended after the user's "TEST"). */
__attribute__((used, externally_visible))
void AddExtension(uint8_t *filename) {
    uint8_t *p = StringCopy(filename, _selectedFilename);
    *p++ = (uint8_t)(_extension >> 24); /* '.' */
    *p++ = (uint8_t)(_extension >> 16);
    *p++ = (uint8_t)(_extension >> 8);
    *p++ = (uint8_t)(_extension);
    *p = 0;
    SetExitMenuFlag();
}

/* Menu onSelect callback: compute file index from entry ordinal (3-column layout)
   and select that file. */
__attribute__((used, externally_visible))
void SelFilesOnSelectCommon(MenuEntry *entry) {
    uint16_t idx = entry->ordinal;
    idx -= 1;
    if (idx >= 30) {
        idx -= 15;
        idx += _selFilesColumn2NumItems;
    }
    if (idx >= 15) {
        idx -= 15;
        idx += _selFilesColumn1NumItems;
    }
    uint8_t *filename = (uint8_t *)_foundFilesPtrTable[idx];
    CopyFilenameExtensionAndSetExitMenuFlag(filename);
}

/* Reposition file list menu entries: find max column height, center vertically,
   and assign Y positions for 3 columns × 15 entries each. */
__attribute__((used, externally_visible))
void RepositionSelectFileEntries(void) {
    /* Find maximum items across all 3 columns */
    uint16_t maxItems = _selFilesColumn1NumItems;
    if (_selFilesColumn2NumItems > maxItems)
        maxItems = _selFilesColumn2NumItems;
    if (_selFilesColumn3NumItems > maxItems)
        maxItems = _selFilesColumn3NumItems;

    /* Column start Y: entry[0].y, plus height if entry[0] is visible */
    uint16_t startY = _menuEntries[0].y;
    if (!_menuEntries[0].isInvisible)
        startY += _menuEntries[0].dwHeight;
    _selFilesColumnStartY = startY;

    /* Column end Y: from entry[48] if saving, entry[50] otherwise */
    if (_savingFile)
        _selFilesColumnEndY = _menuEntries[48].y;
    else
        _selFilesColumnEndY = _menuEntries[50].y;

    /* Calculate centered Y start */
    uint16_t totalHeight = maxItems * 9;
    uint16_t availableHeight = _selFilesColumnEndY - _selFilesColumnStartY;
    uint16_t yOffset = (uint16_t)(availableHeight - totalHeight) >> 1;
    uint16_t colStartY = _selFilesColumnStartY + yOffset;

    /* Position entries: 3 columns × 15 entries, starting at menuEntries[1] */
    MenuEntry *e = &_menuEntries[1];
    uint16_t col;
    for (col = 0; col < 3; col++) {
        uint16_t y = colStartY;
        uint16_t row;
        for (row = 0; row < 15; row++) {
            e->y = y;
            y += 9;
            e++;
        }
    }
}

/* ---- Chunk 141: SelFilesDrawItemDescription ---- */

__attribute__((used, externally_visible))
void SelFilesDrawItemDescription(MenuEntry *entry) {
    uint16_t idx = entry->ordinal;
    idx -= 1;
    if (idx >= 30) {
        idx -= 15;
        idx += _selFilesColumn2NumItems;
    }
    if (idx >= 15) {
        idx -= 15;
        idx += _selFilesColumn1NumItems;
    }

    uint32_t ext = _extensionsBuffer[idx];
    MenuEntry *descEntry = &_menuEntries[46];

    const uint8_t *descStr;
    if (ext == 0x2E444959)       /* '.DIY' */
        descStr = (const uint8_t *)"DIY";
    else if (ext == 0x2E505245)  /* '.PRE' */
        descStr = (const uint8_t *)"PRESET";
    else if (ext == 0x2E534541)  /* '.SEA' */
        descStr = (const uint8_t *)"SEASON";
    else if (ext == 0x2E434152)  /* '.CAR' */
        descStr = (const uint8_t *)"CAREER";
    else if (ext == 0x2E544143)  /* '.TAC' */
        descStr = (const uint8_t *)"TACT";
    else if (ext == 0x2E52504C || ext == 0x2E535250)  /* '.RPL', '.SRP' — whole-match replays */
        descStr = (const uint8_t *)"REPLAY";
    else if (ext == 0x2E43484C)  /* '.CHL' — a saved position (src/challenge.c) */
        descStr = (const uint8_t *)"MATCH";
    else
        descStr = (const uint8_t *)"HIGH";

    descEntry->fg = (uintptr_t)descStr;
    descEntry->x = entry->x + 0x39;
    descEntry->y = entry->y;
    descEntry->isInvisible = 0;
    DrawMenuItem(descEntry);
    descEntry->isInvisible = 1;
}

/* ---- Chunk 142: SelectFilesInputSaveFilenameOnSelect ---- */
/* SWOS2.s line 63964 — handle save filename input in file selection dialog */
extern uint16_t _allowExtraCharsFlag;

__attribute__((used, externally_visible))
void SelectFilesInputSaveFilenameOnSelect(MenuEntry *entry)
{
    if (_totalNumberOfFiles == 45) {
        ShowErrorMenu((uint8_t *)"CAN ONLY SAVE MAXIMUM 45 FILES ON DISK");
        return;
    }

    uint8_t *filename = (uint8_t *)entry->fg;
    _allowExtraCharsFlag = 1;
    uint16_t result = InputText(9, (uint8_t *)entry);
    _allowExtraCharsFlag = 0;

    if (result != 0)
        return;

    /* Check if string has any non-space characters */
    uint8_t *p = filename;
    while (*p != 0) {
        if (*p != 0x20) {
            AddExtension(filename);
            return;
        }
        p++;
    }

    /* All spaces or empty — restore default name from inputSaveFilename */
    MenuEntry *entry48 = &_menuEntries[48];
    uint8_t *dst = (uint8_t *)entry48->fg;
    StringCopyUntilChar(_inputSaveFilename, dst, '.');
    CallDrawMenuItem(entry48);
}

/* ================================================================
 * SelectFilesOnReturn  (SWOS2.s line 57336)
 *
 * Called on return/draw for the file selection menu.
 * Sets up title, save entries, calculates 3-column layout,
 * and assigns filenames to visible menu entries.
 * ================================================================ */
__attribute__((used, externally_visible))
void SelectFilesOnReturn(void)
{
    /* Set menu title in entry 0 */
    _menuEntries[0].fg = _menuTitle;

    if (_savingFile != 0) {
        /* Show save filename entry (entry 48) */
        _menuEntries[48].isInvisible = 0;
        uint8_t *str = (uint8_t *)_menuEntries[48].fg;
        *str = 0;
        StringCopyUntilChar(_inputSaveFilename, str, '.');

        /* Show input box (entry 47) */
        _menuEntries[47].isInvisible = 0;

        if (_inputSaveFilename[0] != 0) {
            /* Show save button (entry 49) */
            _menuEntries[49].isInvisible = 0;
        }
    }
    /* note: the "not saving + no files" case still falls through to column calc */

    /* Calculate 3-column layout */
    if (_numMatchedFiles <= 8) {
        _selFilesColumn1NumItems = 0;
        _selFilesColumn2NumItems = _numMatchedFiles;
        _selFilesColumn3NumItems = 0;
    } else {
        uint16_t perCol = _numMatchedFiles / 3;
        uint16_t remainder = _numMatchedFiles % 3;
        _selFilesColumn1NumItems = perCol;
        _selFilesColumn2NumItems = perCol;
        _selFilesColumn3NumItems = perCol;
        if (remainder == 1) {
            _selFilesColumn2NumItems++;
        } else if (remainder == 2) {
            _selFilesColumn1NumItems++;
            _selFilesColumn2NumItems++;
        }
    }

    RepositionSelectFileEntries();
    /* Assign filenames to menu entries */
    void **filePtr = _foundFilesPtrTable;
    MenuEntry *entry = &_menuEntries[1]; /* entry at offset 0x38 = index 1 */
    uint16_t d6 = 0;

    for (;;) {
        entry->isInvisible = 1;

        if (d6 >= _numberOfFiles)
            goto nextEntry;

        /* Find the right column slot for this file */
        for (;;) {
            uint16_t ordinal = entry->ordinal;
            int16_t idx = (int16_t)(ordinal - 1);
            uint16_t col = (uint16_t)((int32_t)idx / 15);  /* divu.w #$F */
            uint16_t row = (uint16_t)((int32_t)idx % 15);

            uint16_t colMax;
            if (col == 0) {
                colMax = _selFilesColumn1NumItems;
            } else if (col == 1) {
                colMax = _selFilesColumn2NumItems;
            } else {
                colMax = _selFilesColumn3NumItems;
            }

            if (row >= colMax) {
                /* Entry not in visible range — skip to next entry */
                entry++;
                if (entry->ordinal == 0x2E)
                    return;
                entry->isInvisible = 1;
                continue;
            }

            /* Entry is in range — assign filename */
            entry->isInvisible = 0;
            uint8_t *filename = (uint8_t *)*filePtr++;
            uint8_t *dst = (uint8_t *)entry->fg;

            /* Copy filename until '.' */
            while (*filename != 0) {
                *dst++ = *filename++;
                if (*(filename - 1) == '.') {
                    dst--;
                    break;
                }
            }
            *dst = 0;
            break;
        }

    nextEntry:
        entry++;
        if (entry->ordinal == 0x2E)
            return;
        d6++;
    }
}

/* ================================================================
 * SelectFilesInit  (SWOS2.s line 57279)
 *
 * Called on init for the file selection menu.
 * Sets initial cursor position, sorts files by extension/name,
 * then calls SelectFilesOnReturn to set up display.
 * ================================================================ */
__attribute__((used, externally_visible))
void SelectFilesInit(void)
{
    /* Set initial menu entry based on mode */
    if (_savingFile != 0) {
        if (_inputSaveFilename[0] != 0)
            SetCurrentEntry(49);
        else
            SetCurrentEntry(48);
    } else {
        if (_numMatchedFiles == 0)
            SetCurrentEntry(50);
    }

    /* Bubble sort files by extension then name */
    int16_t count = (int16_t)_numMatchedFiles - 2;
    if (count >= 0) {
        uint16_t swapped;
        do {
            swapped = 0;
            void **ptrCur = _foundFilesPtrTable;
            uint32_t *extCur = _extensionsBuffer;

            for (int16_t i = count; i >= 0; i--) {
                uint8_t *name1 = (uint8_t *)ptrCur[0];
                uint32_t ext1 = extCur[0];
                uint32_t ext2 = extCur[1];

                if (ext1 > ext2) {
                    goto doSwap;
                } else if (ext1 == ext2) {
                    /* Extensions equal — compare names */
                    uint8_t *name2 = (uint8_t *)ptrCur[1];
                    uint8_t *p1 = name1;
                    uint8_t *p2 = name2;
                    for (;;) {
                        uint8_t c = *p1++;
                        if (c == 0) break;       /* end of name1 → no swap */
                        uint8_t c2 = *p2++;
                        if (c < c2) break;        /* name1 < name2 → no swap */
                        if (c > c2) goto doSwap;  /* name1 > name2 → swap */
                        /* equal → continue */
                    }
                }
                goto noSwap;

            doSwap:
                /* Swap pointer table entries */
                ptrCur[0] = ptrCur[1];
                ptrCur[1] = name1;
                /* Swap extension entries */
                extCur[0] = ext2;
                extCur[1] = ext1;
                swapped = 1;

            noSwap:
                ptrCur++;
                extCur++;
            }
        } while (swapped);
    }

    /* Fall through to OnReturn */
    SelectFilesOnReturn();
}
