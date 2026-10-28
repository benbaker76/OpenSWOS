/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "swos_sections.h"
#include "swos_asm.h"
#include "frontend.h"
#include "game_utils.h"
#include "fade.h"
#include "display.h"
#include "asset_loader.h"
#include "loader_bootstrap.h"
#include "loader_intro.h"
#include "audio.h"
#include "menus.h"
#include "deterministic.h"
#include "challenge_gen.h"

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef short int16_t;

#define SWOS_CHIPMEMSIZE 0x00200000u
#define SWOS_FASTMEMSIZE 0x00200000u
#define SWOS_CHIPBITS (1<<(SWOS_CHIPMEMSIZE/0x80000))-1
#define SWOS_FASTBITS ((1<<(SWOS_FASTMEMSIZE/0x80000))-1)<<24
#define SWOS_MAGIC_VALUE 0x12345678u
/* Note: use FRONTBUFFER_SIZE_TOTAL_BYTES from display.h instead of hardcoded OCS value */

#define CIAA_PRA_ADDR 0x00BFE001u
#define CIAA_DDRA_ADDR 0x00BFE201u
#define CIAA_ICR_ADDR 0x00BFED01u

#define LEVEL_2_INTERRUPT_AUTOVECTOR 0x68u
#define LEVEL_3_INTERRUPT_AUTOVECTOR 0x6Cu
#define LEVEL_4_INTERRUPT_AUTOVECTOR 0x70u

extern void SafeStart(void);
extern void LoadDosLibrary(void);
extern void DetachOSDisplay(void);
extern void InitHardwareAtFrameStart(void);
extern void SetupInterrupts(void);
extern void Int2KeyboardHandler(void);
extern void Int3TimerHandler(void);
extern void Int4Handler(void);

extern uint32_t _S_VBR;
extern uint32_t _loaderBankBitmap;
extern uint16_t _chipRamBitmapEnd;
extern uint16_t _pitchDatBuffer;
extern uint16_t _currentMenu;
extern uint32_t _magicValue;
extern uint32_t _loaderMemTop;
extern uint32_t _loaderParam;
extern uint16_t _frontbufferBase;
extern uint16_t _frontbufferBaseAlt;
extern uint16_t _frontbufferMaskBuffer;

/* Pre-allocated cache memory (defined in SWOS2.s BSS, filled before hardware takeover) */
extern uint8_t *_earlyAllocCacheMem;
extern uint32_t _earlyAllocCacheSize;

extern uint8_t *AllocMemAny(uint32_t size);

SWOS_CHIP_BSS volatile uint32_t _swos_chip_bss_anchor;
SWOS_FAST_BSS volatile uint32_t _swos_fast_bss_anchor;

/* m68k exception-vector table installer. Dead code on SDL (no real
 * VBR), but kept for the Amiga port. uintptr_t so the cast to
 * `volatile uint32_t *` doesn't widen a 32-bit literal on 64-bit. */
static void set_vector(uintptr_t vbr, uintptr_t offset, void (*handler)(void))
{
    volatile uint32_t *vector = (volatile uint32_t *)(vbr + offset);
    *vector = (uintptr_t)handler;
}

/* Renamed from `_start` to avoid colliding with the hosted libc CRT
 * entry point (`_start` is the runtime entry on Linux/glibc). The SDL
 * shim never calls this directly -- it goes straight to mainC. The
 * Amiga assembly (swos2_unit.s) still does `jsr _start`, so expose
 * the same body under both names on that build. */
#if defined(__m68k__) || defined(__amigaos__) || defined(__amigaos4__)
__attribute__((alias("swos_c_start"), used, externally_visible))
void _start(void);
#endif
__attribute__((used, externally_visible)) void swos_c_start(void)
{
    SafeStart();
    LoadDosLibrary();
    DetachOSDisplay();
    InitHardwareAtFrameStart();

    /* These two zero-fill loops walk between pairs of SWOS2.s BSS
     * labels that were originally guaranteed adjacent — the first
     * scrubs the chip-RAM bank-bitmap metadata from `loaderBankBitmap`
     * to `chipRamBitmapEnd`; the second clears the pitch tile-pattern
     * region from `pitchDatBuffer` to `currentMenu`. With the data
     * migrated to discrete C globals neither pair is contiguous, so
     * the loops walk the full 32-bit address space and hang. Both are
     * redundant under the m68k-amiga-elf CRT (it zero-fills BSS at
     * startup); SDL also doesn't enter `swos_c_start` so it never
     * ran them either. Skip until we restore the original adjacency
     * via a shared backing buffer + .equ aliases (the same pattern
     * used by `careerFileBufferBuf`).
     *
     * uintptr_t locals so the global addresses don't truncate on
     * 64-bit before being cast back to volatile uint16_t* for the
     * byte-by-byte zero-fill (kept for when this comes back). */
    (void)&_loaderBankBitmap;
    (void)&_chipRamBitmapEnd;
    (void)&_pitchDatBuffer;
    (void)&_currentMenu;

    _magicValue = SWOS_MAGIC_VALUE;
    /* Bank scan disabled: assetLoaderBank0Base is in BSS (relocated),
     * so computed bank addresses (base + N*0x80000) point to invalid memory.
     * Set loaderMemTop = 0 to skip the bank scan in AssetLoaderInitAltPathAndBanks.
     * The main 195KB cache region is sufficient. */
    _loaderMemTop = 0;
    *(volatile uint32_t *)&_loaderBankBitmap = 0;
    _loaderParam = SWOS_FASTBITS;

    _custom->color[0] = 0x0u;

    volatile uint16_t *front = &_frontbufferBase;
    volatile uint16_t *alt = &_frontbufferBaseAlt;
    volatile uint16_t *mask = &_frontbufferMaskBuffer;

    /* Prime the display buffers. Original SWOS2.s wrote 0xF0F0 here (a visible
     * 4px-on/4px-off bitplane pattern) — harmless on OCS because the 44-byte
     * content covers the whole 352px line before the buffer is ever shown. On
     * the AGA FMODE-widened buffer (PLANE_STRIDE 52 / 416px line) the content no
     * longer spans the extra columns, so the 0xF0F0 prime showed through as
     * yellow vertical stripes (full screen before the first frame draws, and a
     * band wherever a frame doesn't cover). Clear to 0 instead — the m68k prime
     * value was arbitrary; black is invisible against the border. */
    for (uint32_t bytes = FRONTBUFFER_SIZE_TOTAL_BYTES; bytes != 0u; bytes -= 2u)
    {
        *front++ = 0x0000u;
        *alt++ = 0x0000u;
        *mask++ = 0x0000u;
    }

    _custom->dmacon = 0x8340u;
    _custom->dmacon = (uint16_t)DMACON_BLT_OFF;

    set_vector(_S_VBR, LEVEL_2_INTERRUPT_AUTOVECTOR, Int2KeyboardHandler);
    set_vector(_S_VBR, LEVEL_3_INTERRUPT_AUTOVECTOR, Int3TimerHandler);

#ifdef DETERMINISTIC_MODE
    InitDeterministicMode();
#endif
    set_vector(_S_VBR, LEVEL_4_INTERRUPT_AUTOVECTOR, Int4Handler);

    SetupInterrupts();

    *(volatile uint8_t *)CIAA_ICR_ADDR = 0x88u;
    _custom->intena = 0xC008u;
    *(volatile uint8_t *)CIAA_ICR_ADDR = 0x82u;
    *(volatile uint8_t *)CIAA_DDRA_ADDR |= 0x02u;
    *(volatile uint8_t *)CIAA_PRA_ADDR |= 0x02u;
}

/* ---- Timing constant ---- */
#define FPS_50HZ 49

/* ---- External globals for mainC ---- */
extern uint16_t _assetLoaderAltPathReady;
extern uint16_t _paletteIndex;
extern uint16_t _loaderModeFlag;
extern uint16_t _hilNumGoals;
extern volatile uint16_t _currentGameTick;

extern uint8_t _kTitleLogoColors[];
extern uint8_t _kTitleScreenColors[];
extern uint8_t _kTitleTextColors[];

extern uint16_t _titleLogoPalette[];
extern uint16_t _titleScreenPalette[];
extern uint16_t _titleTextPalette[];

extern uint16_t _bootTemp0;
extern uint16_t _bootTemp1;
extern uint16_t _bootTemp2;
extern uint16_t _bootTemp3;
extern uint16_t _bootTemp4;
extern uint16_t _bootTemp5;
extern uint16_t _bootTemp6;

extern uint16_t _introMenuImageScript[];
#include "intro_chip_ram.h"     /* the sprite pool, shared with the title tune on the Amiga */
extern uint8_t *_imageStreamCursor;
extern uint8_t *_menuImageStreamBaseEnd;

extern uint16_t _quitToDos;

extern void AssetSys_InitDiskImageSources(void);
extern int16_t AssetSvcLoaderOp2(void);
extern int16_t AssetSvcLoaderOp3(void);
extern void ProcessImageCommandStream(const uint16_t *cmdBuf, uint8_t *workBuf);
extern void MenuProc(void);
extern int16_t LoadFileEx(uint8_t *filename, uint8_t *dest, uint32_t *outFileSize);

/* delay_ticks: reset game tick counter, wait N vblanks via WaitForVSync.
 * Original Amiga was a busy spin on currentGameTick; on SDL that starves
 * the timer thread on WSL2 and hangs indefinitely. */
extern void WaitForVSync(void);

static void delay_ticks(uint16_t ticks)
{
    _currentGameTick = 0;
    while (_currentGameTick < ticks)
        WaitForVSync();
}

/* ================================================================
 * mainC  (was mainAsm in SWOS2.s / _mainAsm in frontend.c)
 *
 * Main game boot sequence. Initializes display, loads assets,
 * shows loader intro, builds menu image stream, and enters
 * the front-end main loop.
 * ================================================================ */
__attribute__((used, externally_visible))
void mainC(void)
{
    /* ---- Phase 1: Hardware init ---- */
    AssetLoaderResetState();
    _assetLoaderAltPathReady = 0;
    InitDisplayAndCopper();
    AssetSys_InitDiskImageSources();
    InitSkinAndDefaultPointers();
#ifndef SWOS_SDL_BUILD
    if (ChallengeGen_Boot() < 0) return;
#endif

    /* ---- Phase 2: Build palettes ---- */
    
#ifdef AGA
    {
        extern uint16_t _menuPalette[];
        WriteCopperPalette(_menuPalette);
    }
#else
    BuildPaletteFromColors(_kTitleLogoColors, _titleLogoPalette);
    BuildPaletteFromColors(_kTitleScreenColors, _titleScreenPalette);
    BuildPaletteFromColors(_kTitleTextColors, _titleTextPalette);
#endif

    /* ---- Phase 3: Boot temps ---- */

    _bootTemp0 = 1;
    _bootTemp1 = 0;
    _bootTemp2 = 0;
    _bootTemp3 = 0;
    _bootTemp4 = 0;
    _bootTemp5 = 0;
    _bootTemp6 = 0;

    /* ---- Phase 4: Loader intro ---- */
    _loaderModeFlag = 1;
#ifdef SWOS_SDL_BUILD
    /* Arcade: no intro/credits — boot straight to attract (asset loading below
     * still runs; only the visible intro animation is skipped). */
    {
        /* The intro is where a player with more pads than players picks theirs: the
         * one fire is pressed on becomes player 1 (input_sdl.c). */
        extern int _gArcadeMode;
        extern void InputSDL_SetIntroClaim(int on);
        if (!_gArcadeMode) {
            InputSDL_SetIntroClaim(1);
            ShowLoaderIntroSequence();
            InputSDL_SetIntroClaim(0);
        }
    }
#else
#ifndef PITCH_SCROLL_TEST
    ShowLoaderIntroSequence();   /* skipped in the scroll-test rig: boot straight to the pitch */
#endif
#endif
    AssetLoaderInitAltPathAndBanks();
    AssetPrecacheCore();
    InstallAndInitLoaderBootstrap();
    FadeOutToBlack();
    _loaderModeFlag = 0;

    /* ---- Phase 5: Show title screens (skipped in arcade mode) ---- */
#ifdef SWOS_SDL_BUILD
    { extern int _gArcadeMode; if (_gArcadeMode) goto skip_title_screens; }
    /* No Amiga disk screens installed (main.c). */
    { extern int _gNoAmigaScreens; if (_gNoAmigaScreens) goto skip_title_screens; }
#endif
    DoubleBufferSwapSequence();
    _paletteIndex = 0;
    LoadOverlayToFrontbuffer((const uint8_t *)GRAFS "olddisk1.raw");
    BlitterCopyPanelToScreen2();
    WaitForVSync();

    LoadOverlayToFrontbuffer((const uint8_t *)GRAFS "disk22.raw");
    _paletteIndex = 0;
    FadeIn();
#ifdef SWOS_SDL_BUILD
skip_title_screens: ;
#endif

    /* ---- Phase 6: Poll loader op3 with 50Hz delays ---- */
    
    do
    {
        delay_ticks(FPS_50HZ);
    } while (AssetSvcLoaderOp3() != 0);

    InstallAndRunLoaderBootstrap_Op3();
    FadeOutToBlack();

    /* ---- Phase 7: Poll loader op2 ---- */
    
    if (AssetSvcLoaderOp2() != 0)
    {
        BlitterCopyPanelToScreen2();
        WaitForVSync();
        _paletteIndex = 0;
        FadeIn();

        do
        {
            delay_ticks(FPS_50HZ);
        } while (AssetSvcLoaderOp2() != 0);

        FadeOutToBlack();
    }

    /* ---- Phase 8: Build intro menu assets ---- */

    /* Fill in the runtime filename-pointer slots in our image scripts.
     * The script bodies are transcribed from the IDA-exported Data.c
     * with their pointer halves zeroed (we don't have symbolic refs
     * from the disassembler). This applies to BOTH the SDL host build
     * AND the Amiga build — the function's name is historical
     * (originally only the SDL path patched these). Without this call
     * on Amiga, the first 0x7000 opcode in introMenuImageScript reads
     * NULL as the filename pointer; StringCopy(NULL, assetFilename)
     * truncates the buffer to byte 0 and the subsequent os_DosOpen on
     * an empty filename fails. */
    extern void SwosSDL_InitImageScripts(void);
    SwosSDL_InitImageScripts();
    ProcessImageCommandStream(_introMenuImageScript, g_introChipRam.spritePool);
    /* Map the font-extension glyphs (accented capitals, full ASCII, $) into the
     * char tables now that their sprites have been extracted by the script. */
    { extern void FontExt_ApplyConv(void); FontExt_ApplyConv(); }
    /* Force this write through to memory — LTO must not eliminate it.
     * menuImageStreamBaseEnd is read later by ConvertColorsAndTextSprites. */
    *(uint8_t * volatile *)&_menuImageStreamBaseEnd =
        *(uint8_t * volatile *)&_imageStreamCursor;
    AssetPrecacheMenuPack();
    _assetLoaderAltPathReady = 1;
    _hilNumGoals = 0;

    /* ---- Phase 9: Enter front-end loop (or arcade attract loop) ---- */
    if (ChallengeGen_Armed()) {
        ChallengeGen_Run();
        return;
    }
#ifdef SWOS_SDL_BUILD
    {
        extern int  NetplayLiveTest_Armed(void);
        extern void NetplayLiveTest_Run(void);
        extern int  ReplayTest_Armed(void);
        extern void ReplayTest_Run(void);
        /* Same hook, same reason as the replay rig: a match needs its data
         * loaded, and neither wants the front end. */
        if (NetplayLiveTest_Armed())
            NetplayLiveTest_Run();   /* plays a live match, then exits */
        {
            extern int32_t SrpResim_Armed(void);
            extern void    SrpResim_Run(void);
            if (SrpResim_Armed())
                SrpResim_Run();       /* plays a recorded match again, then exits */
        }
        {
            /* --challenge=<file>: play on from a saved position instead of the front end
             * (src/challenge.c). Here for the same reason as the rigs above -- the match
             * needs its data loaded, and this wants no menus. */
            extern const char *Challenge_Requested(void);
            extern int32_t     Challenge_Arm(const uint8_t *name);
            extern void        Challenge_Play(void);
            const char *want = Challenge_Requested();
            if (want) {
                if (Challenge_Arm((const uint8_t *)want) != 0)
                    return;
                Challenge_Play();
                return;
            }
        }
        {
            extern int32_t ReplayV2_ViewTestArmed(void);
            extern void    ReplayV2_ViewTestRun(void);
            if (ReplayV2_ViewTestArmed())
                ReplayV2_ViewTestRun();   /* plays a .SRP, then exits */
        }
        {
            extern int32_t TeamAudit_Armed(void);
            extern void    TeamAudit_Run(void);
            if (TeamAudit_Armed()) {
                TeamAudit_Run();   /* hashes every team's player prices, then exits */
                return;
            }
        }
        if (ReplayTest_Armed()) {
            ReplayTest_Run();   /* replays a recording, then exits the process */
            return;
        }
    }
    {
        extern int _gArcadeMode;
        extern void ArcadeMainLoop(void);
        if (_gArcadeMode) {
            ArcadeMainLoop();   /* coin-op: boot to attract, never the main menu */
            return;
        }
    }
#endif
#ifdef PITCH_SCROLL_TEST
    {
        extern void PitchScrollTest(void);
        PitchScrollTest();   /* dedicated AGA pitch-scroll test rig, never returns */
        return;
    }
#endif
    FrontEndMainLoop();
}
