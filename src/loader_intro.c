/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "swos_types.h"
#include "game_types.h"   /* DIYTournamentState (diyFileBufferCopy doubles as the 4KB hero-song staging buffer) */
#include "loader_intro.h"
#include "fade.h"
#include "display.h"
#include "input.h"
#include "audio.h"
#include <string.h>            /* memcpy/memset — both builds (Amiga loader re-stride) */
extern uint8_t _rawAssetLoadBuffer[];   /* overlay/frame staging — both builds */
#ifdef SWOS_SDL_BUILD
extern int  AudioSDL_StartSong(const uint8_t *song_data, int song_size,
                               const uint8_t *ins_data,  int ins_size,
                               int subsong);
extern void AudioSetChannelVolume(uint16_t mask, uint16_t vol);
/* Sizes of the buffers used to stage hero.sng / hero.ins. The actual
 * file payload is smaller; RJP1_Load parses headers internally and
 * uses only what it needs. */
#define HERO_SONG_BUFFER_BYTES   (4 * 1024)        /* diyFileBufferCopyBuf */
#endif

/* ---- External globals (all live in SWOS2.s) ---- */
extern uint16_t _paletteIndex;
extern uint16_t _quitToDos;
extern volatile uint16_t _currentGameTick;
extern void WaitForVSync(void);   /* game_utils.c — pumps audio + events on SDL */
extern uint8_t _pl1Fire;
extern volatile uintptr_t _frontbufferBaseAltPtr;
extern volatile uintptr_t _frontbufferBasePtr;
extern uint32_t _memBoundaryMarker;
extern uint16_t _audioBankLoaded;
extern volatile uint16_t _audioActive;
extern uint16_t _fireResetFlag;
extern volatile uint16_t _keyCount;

/* Harness fast-boot: when non-zero, ShowLoaderIntroSequence returns immediately,
 * skipping the (slow) loader intro. Seeded from SWOS_HARNESS_TURBO in
 * Platform_Init on SDL (the harness always sets that env); gdb-set by
 * amiga_gdb.sh after attach on the Amiga (the target is paused at the entry,
 * before this runs).
 *
 * SDL: kept because platform_sdl.c WRITES it (Platform_Init) — a written global
 * survives. AMIGA: it is only READ (the gdb set is invisible to the compiler),
 * and the m68k LTO/--gc-sections drops a read-only global (used/externally_visible
 * don't save it; `retain` is unsupported on this binutils). So the Amiga harness
 * does NOT use this flag — amiga_gdb.sh skips the intro with a gdb breakpoint +
 * `return` at ShowLoaderIntroSequence instead. This check then just no-ops there
 * (folded away), which is fine. */
__attribute__((used, externally_visible))
volatile uint8_t _skipLoaderIntro = 0;
extern volatile uint16_t _currentKey;
#include "intro_chip_ram.h"     /* the title tune's samples, shared with the sprite pool on the Amiga */
extern uint8_t _aSoundHeroSng[];
extern uint8_t _aSoundHeroIns[];

/* Dedicated 4 KB staging buffer for hero.sng. This used to borrow
 * diyFileBufferCopy (an 8 KB raw BSS buffer at the time). After the step-3
 * untangle diyFileBufferCopy is a ~1 KB typed DIYTournamentState struct, so
 * staging a 4 KB file there would overrun it — hero.sng gets its own buffer. */
static uint8_t heroSongBuffer[4 * 1024];

/* ---- External asm functions (C-callable bridges in swos2_unit.s) ---- */
/* Player1StatusProc declared in input.h */
extern int16_t LoadFileEx(uint8_t *filename, uint8_t *dest, uint32_t *outFileSize);
extern int16_t AssetEnsureReadyWithRetry(uint8_t *name, uint8_t *dest);  /* asset_loader.c */
extern int16_t LoadRNCToBufferNoBlit(uint8_t *name, uint8_t *dest);     /* asset_loader.c */

/* ================================================================
 * LoaderIntroEnsureMenuMusicPlaying  (SWOS2.s line 21035)
 *
 * If audio bank not yet loaded, stops audio and starts menu music
 * from the hero audio bank data. Then applies preset 0 and marks
 * audio as active.
 * ================================================================ */
static void loaderIntroEnsureMenuMusicPlaying(void)
{
#ifdef SWOS_SDL_BUILD
    /* SDL build: route through the RJP1 player + SDL audio callback
     * (same mechanism TryPlayMusic uses for the menu song). The Amiga
     * `InitAudioState` is stubbed on SDL because it parses a 68k-BE
     * bank header we don't byte-swap, so calling it here would leave
     * the hero music silent. Hand the loaded hero.sng / hero.ins to
     * the player instead, then ramp the master volume in. */
    if (_audioBankLoaded == 0) {
        StopAllAudioChannels();
        /* HERO has 5 subsongs; the reference rjp1player plays subsong 0
         * (the full mix with vocals). Subsong 1 has the singing channel
         * sequence byte zeroed, so passing 1 here was the cause of the
         * "music plays but no singing" symptom. */
        extern int AudioSDL_StreamStart(const char *name, int loop);
        extern int AudioSDL_PcSongStart(const char *stem, int loop);
        extern int32_t DosMusic_Style(void);
        extern int32_t DosMusic_TitleMidi(void);
        /* [audio] music = pc: [audio] title_music = midi plays sound_dos/title.xmi (SWOS 2020's
         * floppy title song, from a mod) on the emulated card, else the PC CD release's title
         * song, sound_dos/title.mp3. */
        if (DosMusic_Style() && DosMusic_TitleMidi() && AudioSDL_PcSongStart("title", 0) == 0) {
        } else
        if (DosMusic_Style() && AudioSDL_StreamStart("title", 0) == 0) {
        } else
        if (AudioSDL_StartSong(heroSongBuffer, HERO_SONG_BUFFER_BYTES,
                               g_introChipRam.titleTuneSamples, (int)sizeof g_introChipRam.titleTuneSamples, 0) != 0) {
            /* Load failed — keep audio silent but mark "loaded" so we
             * don't hammer the player every vblank. */
        }
        _audioBankLoaded = 1;
    }

    AudioSetChannelVolume(0xFFFF, 0);
    _audioActive = 1;
    /* Ramp the master volume from 0 to 0x40 (full) over a few vblanks so the
     * song fades in instead of starting mid-note. SDL plays music at full
     * volume: at 0x40 there is no digital attenuation, so the 8-bit output keeps
     * full level + full precision (matching the reference rjp1player). The Amiga
     * build keeps 0x23 — there the AUDxVOL attenuation is lossless analog. */
    for (uint16_t vol = 0; vol <= 0x3E; vol += 2) {
        AudioSetChannelVolume(0xFFFF, vol);
        WaitForVSync();
    }
    AudioSetChannelVolume(0xFFFF, 0x40);
#else
    if (_audioBankLoaded == 0)
    {
        StopAllAudioChannels();
        InitAudioState(heroSongBuffer, g_introChipRam.titleTuneSamples);
        _audioBankLoaded = 1;
    }
    AudioApplyPresetToChannels(0);
    _audioActive = 1;
#endif
}

/* ================================================================
 * LoaderIntroStopMusicAndResetAudio  (SWOS2.s line 21051)
 *
 * Beeps all channels down to silence, stops audio, and clears
 * the active/loaded flags.
 *
 * SDL deviation: the asm `AudioBeepAllShort` jumps master volume
 * to 0x40 (max) and ramps down to 0 — emitting a brief loud blip
 * that's part of the original Amiga's "click" between intro and
 * menu music. On SDL with our queued audio path that blip lands as
 * a 0x23 → 0x40 step (LOUDER than playback) followed by a long
 * fade. Ramp from the current 0x23 down to 0 instead so the music
 * just fades cleanly on skip. The ramp also keeps AudioSDL_Tick
 * fed so the queue doesn't underrun during the fade.
 * ================================================================ */
static void loaderIntroStopMusicAndResetAudio(void)
{
#ifdef SWOS_SDL_BUILD
    for (int16_t vol = 0x40; vol >= 0; vol--) {   /* fade from full (SDL playback level) */
        AudioSetChannelVolume(0xFFFF, (uint16_t)vol);
        WaitForVSync();
        if (_quitToDos) break;
    }
    AudioSetChannelVolume(0xFFFF, 0);
#else
    AudioBeepAllShort();
#endif
    StopAllAudioChannels();
    _audioActive = 0;
    _audioBankLoaded = 0;
}

/* ================================================================
 * LoaderIntroLoadHeroAudioBank  (SWOS2.s line 21021)
 *
 * Loads hero song ("sound/hero.sng") into diyFileBufferCopy and
 * hero instruments ("sound/hero.ins") into audioInstrumentBank.
 * Retries the entire sequence from scratch on any failure.
 * ================================================================ */
static void loaderIntroLoadHeroAudioBank(void)
{
#ifdef SWOS_SDL_BUILD
    /* No Amiga music installed (main.c): nothing to stage. */
    { extern int _gNoAmigaSound; if (_gNoAmigaSound) { _audioBankLoaded = 0; return; } }
#endif
retry:
    _audioBankLoaded = 0;
    if (AssetEnsureReadyWithRetry(_aSoundHeroSng, heroSongBuffer) != 0)
        goto retry;
    if (LoadRNCToBufferNoBlit(_aSoundHeroIns, g_introChipRam.titleTuneSamples) != 0)
        goto retry;
}

#ifdef SWOS_SDL_BUILD
/* Arcade mode skips the loader intro, so the HERO title song is never staged.
 * Load hero.sng / hero.ins and start playback (subsong 0, faded in) on demand —
 * used by the arcade attract loop. Reuses the two intro helpers so the staging
 * buffers and subsong choice stay in one place. SDL-only (arcade is). */
void Arcade_PlayHeroMusic(void)
{
    loaderIntroLoadHeroAudioBank();
    loaderIntroEnsureMenuMusicPlaying();
}
#endif

/* ================================================================
 * LoaderIntroLoadFrameToFrontbufferAlt  (SWOS2.s line 21365)
 *
 * Loads a raw image file into the alternate frontbuffer.
 *
 * Amiga: bitplane data goes straight into the alt frontbuffer
 *   (which is itself bitplane-format) — display picks it up as-is.
 *
 * SDL:   the alt frontbuffer is 8bpp chunky (palette index per
 *   byte) so writing raw bitplane bytes there makes
 *   `chunky_to_rgba` paint garbage. Stage the file through
 *   `rawAssetLoadBuffer` and decode NUM_PLANES interleaved planes
 *   into chunky bytes — same loop LoadOverlayToFrontbuffer uses
 *   for menubg.raw. Frame layout matches the menu BG: PLANE_STRIDE
 *   bytes/plane row, NUM_PLANES planes, decode SCREEN_WIDTH pixels
 *   per scanline.
 * ================================================================ */
static void loaderIntroLoadFrameToFrontbufferAlt(const uint8_t *filename)
{
#ifdef SWOS_SDL_BUILD
    uint32_t frameSize = 0;
    LoadFileEx((uint8_t *)filename, _rawAssetLoadBuffer, &frameSize);

    uint8_t *dstAlt  = (uint8_t *)_frontbufferBaseAltPtr;
    uint8_t *dstBase = (uint8_t *)_frontbufferBasePtr;
    if (!dstAlt) return;

    /* Same full-screen layout as LoadOverlayToFrontbuffer: detect the
     * frame's bitplane depth from its decompressed size (4bpp OCS / 8bpp
     * AGA) so loader frames decode at their own depth, not NUM_PLANES. */
    uint16_t framePlanes    = DetectBankPlanes(frameSize, PLANE_STRIDE, SCREEN_HEIGHT);
    uint32_t frameRowStride = (uint32_t)PLANE_STRIDE * framePlanes;
    memset(dstAlt, 0, FRONTBUFFER_SIZE_TOTAL_BYTES);
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        const uint8_t *rowSrc = _rawAssetLoadBuffer + (size_t)y * frameRowStride;
        uint8_t       *rowDst = dstAlt + (size_t)y * FRONTBUFFER_ROW_STRIDE_BYTES + WIDE_MARGIN;
        for (int px = 0; px < SCREEN_WIDTH; px++) {
            int bit  = 7 - (px & 7);
            int byte = px >> 3;
            uint8_t idx = 0;
            for (int p = 0; p < framePlanes; p++) {
                uint8_t planeByte = rowSrc[p * PLANE_STRIDE + byte];
                idx |= (uint8_t)(((planeByte >> bit) & 1u) << p);
            }
            rowDst[px] = idx;
        }
    }

    /* Mirror alt → base so the post-swap frame the renderer reads is
     * always the decoded image, not whatever stale chunky data was in
     * the other buffer. LoadFileEx does the same mirror when dest is
     * alt; we have to do it ourselves here because we staged through
     * rawAssetLoadBuffer to run the bitplane decode. Without this the
     * intro frame appears for ~1/50 s then gets replaced by the prior
     * buffer contents on the next vsync swap → visible flicker. */
    if (dstBase && dstBase != dstAlt) {
        memcpy(dstBase, dstAlt, FRONTBUFFER_SIZE_TOTAL_BYTES);
    }
#else
    /* Amiga: RE-STRIDE the 44-byte-plane-row frame into the ALT frontbuffer's
     * PLANE_STRIDE(52) slots. The frame FILES are authored at the pre-FMODE
     * 44-byte plane row; the AGA widen made the frontbuffer PLANE_STRIDE 52, so
     * the old verbatim `LoadFileEx(..., _frontbufferBaseAltPtr, 0)` misaligned
     * every plane row -> garbled loader/intro screens. Identical fix to
     * LoadOverlayToFrontbuffer; plane count from the decompressed size so 4bpp/
     * 8bpp frames both load. On OCS (PLANE_STRIDE==44) it's a straight copy.
     * docs/AGA.md §11. */
    enum { LOADER_SRC_PLANE_STRIDE = 44 };
    /* AGA FMODE=1 displays static byte-0 content 16px (2 bytes) left of the
     * OCS/FMODE=0 position, so a full-screen loader frame drawn at byte 0 lands
     * 16px left of centre -- the SAME offset the menu foreground gets fixed with
     * in DrawMenu (menu_system.c AGA_MENU_FG_SHIFT_BYTES). Shift the frame right by
     * LOADER_X_SHIFT_BYTES to re-centre it. 0 on OCS. */
#ifdef AGA
    enum { LOADER_X_SHIFT_BYTES = 2 };
#else
    enum { LOADER_X_SHIFT_BYTES = 0 };
#endif
    uint8_t *dstAlt = (uint8_t *)_frontbufferBaseAltPtr;
    if (!dstAlt) return;
    uint32_t frameSize = 0;
    LoadFileEx((uint8_t *)filename, _rawAssetLoadBuffer, &frameSize);
    uint32_t framePlanes = frameSize / ((uint32_t)LOADER_SRC_PLANE_STRIDE * SCREEN_HEIGHT);
    if (framePlanes == 0 || framePlanes > (uint32_t)NUM_PLANES)
        framePlanes = NUM_PLANES;
    /* No full-screen memset (that caused the black-flash); the re-stride overwrites
     * the whole visible area. Per plane row: clear the LOADER_X_SHIFT_BYTES left
     * margin (kept black), then place the 44-byte frame row shifted right by it.
     * Off-screen padding past the frame stays stale but is never displayed. */
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        const uint8_t *src = _rawAssetLoadBuffer +
                             (size_t)y * LOADER_SRC_PLANE_STRIDE * framePlanes;
        uint8_t *dst = dstAlt + (size_t)y * ROW_STRIDE_BYTES;
        for (uint32_t p = 0; p < framePlanes; p++) {
            uint8_t *pd = dst + (size_t)p * PLANE_STRIDE;
            if (LOADER_X_SHIFT_BYTES) memset(pd, 0, LOADER_X_SHIFT_BYTES);
            memcpy(pd + LOADER_X_SHIFT_BYTES,
                   src + (size_t)p * LOADER_SRC_PLANE_STRIDE,
                   LOADER_SRC_PLANE_STRIDE);
        }
    }
    /* Mirror the decoded frame ALT -> BASE. This is the real intro-flicker fix:
     * the original `LoadFileEx(..., alt)` mirrored to base IMPLICITLY (LoadFileEx
     * copies alt->base when the dest is the alt buffer), but our staged re-stride
     * writes ALT only. The display double-buffer swaps every vblank, so with the
     * frame in ALT alone it strobed between the frame (alt) and the stale/other
     * buffer (base). Copying alt->base puts the frame in BOTH buffers so every swap
     * shows it. (Same reason the SDL branch above mirrors.) */
    {
        uint8_t *dstBase = (uint8_t *)_frontbufferBasePtr;
        if (dstBase && dstBase != dstAlt)
            memcpy(dstBase, dstAlt, FRONTBUFFER_SIZE_TOTAL_BYTES);
    }
#endif
}

/* ================================================================
 * LoaderIntroWaitForFireOrTimeout  (SWOS2.s line 21337)
 *
 * Polls player 1 input each frame. Returns non-zero if fire was
 * pressed or _quitToDos set; returns 0 if the timeout (d7 frames)
 * expired without interruption.
 *
 * Original asm: d7 = timeout count (dbf-style, so iterates d7+1 times).
 * Each iteration: poll input, wait 1 tick.
 * ================================================================ */
static int16_t loaderIntroWaitForFireOrTimeout(int16_t timeout)
{
    int16_t remaining = timeout;

    while (remaining >= 0)
    {
        uint16_t tick;

        Player1StatusProc();

        if (_pl1Fire != 0)
            return 1;
        if (_quitToDos != 0)
            return 1;

        WaitForVSync();

        remaining--;
    }

    return 0;
}

/* ================================================================
 * LoaderIntroShowFrameFadeInOut  (SWOS2.s line 21287)
 *
 * Loads a frame to the alt frontbuffer, fades in, waits for
 * fire/timeout, then fades out (unless interrupted).
 * Returns non-zero if fire/quit interrupted, 0 if timeout.
 * ================================================================ */
static int16_t loaderIntroShowFrameFadeInOut(const uint8_t *filename, int16_t timeout)
{
    if (_quitToDos != 0) return 1;

    loaderIntroLoadFrameToFrontbufferAlt(filename);
#ifndef AGA
    FadeIn();
#endif

    if (loaderIntroWaitForFireOrTimeout(timeout) != 0)
        return 1;

#ifndef AGA
    /* Skip the fade-out if the user closed the window — it just adds
     * ~1 s of unresponsive black-screen between the click and exit. */
    if (_quitToDos == 0)
        FadeOutToBlack();
#endif
    return 0;
}

/* ================================================================
 * LoaderIntroShowFrameWithMusic  (SWOS2.s line 21305)
 *
 * Same as ShowFrameFadeInOut but also ensures menu music is playing
 * before the fade-in.
 * ================================================================ */
static int16_t loaderIntroShowFrameWithMusic(const uint8_t *filename, int16_t timeout)
{
    if (_quitToDos != 0) return 1;

    loaderIntroLoadFrameToFrontbufferAlt(filename);
    loaderIntroEnsureMenuMusicPlaying();
#ifndef AGA
    FadeIn();
#endif

    if (loaderIntroWaitForFireOrTimeout(timeout) != 0)
        return 1;

#ifndef AGA
    if (_quitToDos == 0)
        FadeOutToBlack();
#endif
    return 0;
}

/* ================================================================
 * ShowLoaderIntroSequence  (SWOS2.s line 21170)
 *
 * Displays the loader intro animation sequence: 13 frames with
 * fade in/out effects, music, and fire-button skip support.
 *
 * Flow:
 *   1. Sanity-check memBoundaryMarker address
 *   2. Initialize camera and display
 *   3. Poll fire button (skip intro if pressed)
 *   4. Load hero audio bank
 *   5. Show frames loader00..loader10 with fade in/out
 *   6. Show final loader1.raw frame with extended timeout
 *   7. Stop music and reset audio
 *
 * Early exit paths:
 *   - Fire pressed before frames → jump to loader1.raw, fade in, return
 *   - Load fail on first 2 frames → fade out, show loader1.raw, fade in
 *   - Load fail on later frames → fade out, show loader1.raw, fade in,
 *     then stop music
 * ================================================================ */
__attribute__((used, externally_visible))
void ShowLoaderIntroSequence(void)
{
    if (_skipLoaderIntro)            /* harness fast-boot: skip the intro */
        return;

    if ((uintptr_t)&_memBoundaryMarker > 0x100000u) { }

    SetCameraCoordinates(0, 0);
    SwapBuffersAndCameraViewState();
    UpdatePlanePtrsAndBlitParams();
    SwapBuffersAndCameraViewState();
    UpdatePlanePtrsAndBlitParams();

    Player1StatusProc();
    if (_pl1Fire != 0)
    {
        /* Fire pressed path: just show loader1.raw and fade in */
#ifndef AGA
        _paletteIndex = 3;
#endif
        loaderIntroLoadFrameToFrontbufferAlt((const uint8_t *)GRAFS "loader1.raw");
        FadeIn();
        return;
    }

    loaderIntroLoadHeroAudioBank();

#ifndef AGA
    _paletteIndex = 6;
#endif

    if (loaderIntroShowFrameFadeInOut((const uint8_t *)GRAFS "loader00.raw", 0xFA) != 0)
        goto load_fail_early;

    /* Frame 1: loader01.raw */
    if (loaderIntroShowFrameFadeInOut((const uint8_t *)GRAFS "loader01.raw", 0xFA) != 0)
        goto load_fail_early;

    /* Frame 2: loader1.raw (with music) */
#ifndef AGA
    _paletteIndex = 3;
#endif
    if (loaderIntroShowFrameWithMusic((const uint8_t *)GRAFS "loader1.raw", 0xFA) != 0)
        goto exit_cleanup;

    /* Frame 3: loader2.raw */
#ifndef AGA
    _paletteIndex = 5;
#endif
    if (loaderIntroShowFrameFadeInOut((const uint8_t *)GRAFS "loader2.raw", 0xFA) != 0)
        goto load_fail_mid;
    if (loaderIntroShowFrameFadeInOut((const uint8_t *)GRAFS "loader76.raw", 0xFA) != 0)
        goto load_fail_mid;
    /* Frames 4-12: loader3..loader10 */
    if (loaderIntroShowFrameFadeInOut((const uint8_t *)GRAFS "loader3.raw", 0xFA) != 0)
        goto load_fail_mid;
    if (loaderIntroShowFrameFadeInOut((const uint8_t *)GRAFS "loader4.raw", 0xFA) != 0)
        goto load_fail_mid;
    if (loaderIntroShowFrameFadeInOut((const uint8_t *)GRAFS "loader5.raw", 0xFA) != 0)
        goto load_fail_mid;
    if (loaderIntroShowFrameFadeInOut((const uint8_t *)GRAFS "loader6.raw", 0xFA) != 0)
        goto load_fail_mid;
    if (loaderIntroShowFrameFadeInOut((const uint8_t *)GRAFS "loader7.raw", 0xFA) != 0)
        goto load_fail_mid;
    if (loaderIntroShowFrameFadeInOut((const uint8_t *)GRAFS "loader8a.raw", 0xFA) != 0)
        goto load_fail_mid;
    if (loaderIntroShowFrameFadeInOut((const uint8_t *)GRAFS "loader8b.raw", 0xFA) != 0)
        goto load_fail_mid;
    if (loaderIntroShowFrameFadeInOut((const uint8_t *)GRAFS "loader9.raw", 0xFA) != 0)
        goto load_fail_mid;
    if (loaderIntroShowFrameFadeInOut((const uint8_t *)GRAFS "loader10.raw", 0xFA) != 0)
        goto load_fail_mid;

    /* Final frame: loader1.raw with extended timeout (0x6D6 = 1750 frames) */
#ifndef AGA
    _paletteIndex = 3;
#endif
    loaderIntroShowFrameFadeInOut((const uint8_t *)GRAFS "loader1.raw", 0x6D6);
    /* Fall through to exit_cleanup (ignore return value) */

exit_cleanup:
    loaderIntroStopMusicAndResetAudio();
    /* The user pressed ENTER (or whatever fires) to skip the intro and
     * is likely still holding the key when control returns. Without
     * this, the next MenuProc iteration sees fire=1 and immediately
     * fires the highlighted main-menu entry (FRIENDLY GAME by
     * default). fireResetFlag tells MenuCheckControls to suppress
     * fire/shortFire until the key is released, then auto-clears.
     *
     * Also drain the keyboard FIFO. The skip ENTER landed as
     * AKEY_ENTER in keyBuffer when SDL pumped the KEYDOWN event, and
     * nothing called GetKey() during the rest of the loader phases
     * (asset precache / disk-image fade / op3 polling) — so the
     * scancode is still queued when MenuProc finally runs. The
     * `currentKey == 0x0D` path inside MenuCheckControls runs AFTER
     * the fireResetFlag clear and would set fire=1 from that stale
     * scancode, which is what dropped the user into FRIENDLY GAME on
     * intro skip. Zero the count + currentKey so the buffer can't
     * retroactively trigger anything. */
    _fireResetFlag = 1;
    _keyCount = 0;
    _currentKey = 0;
    return;

load_fail_mid:
    /* Fire pressed → show loader1 splash and continue.
     * Window-X pressed → bypass the splash and exit fast. */
    if (_quitToDos == 0) {
        FadeOutToBlack();
#ifndef AGA
        _paletteIndex = 3;
#endif
        loaderIntroLoadFrameToFrontbufferAlt((const uint8_t *)GRAFS "loader1.raw");
        FadeIn();
    }
    goto exit_cleanup;

load_fail_early:
    if (_quitToDos == 0) {
        FadeOutToBlack();
#ifndef AGA
        _paletteIndex = 3;
#endif
        loaderIntroLoadFrameToFrontbufferAlt((const uint8_t *)GRAFS "loader1.raw");
        FadeIn();
    }
    /* See exit_cleanup for the rationale. */
    _fireResetFlag = 1;
    _keyCount = 0;
    _currentKey = 0;
    return;
}
