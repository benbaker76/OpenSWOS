/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "audio.h"
#include "audio_sfx_filter.h"
#include "dos_sound.h"
#include "dos_music.h"
#include "sfx_ids.h"
#include "endian_helpers.h"
#include "display.h"

#ifdef SWOS_SDL_BUILD
#include <stdio.h>
#include <stdlib.h>
/* RJP1 SDL backend (defined in audio_sdl.c). */
extern int  AudioSDL_StartSong(const uint8_t *song_data, int song_size,
                               const uint8_t *ins_data,  int ins_size,
                               int subsong);
extern int  AudioSDL_LoadSfxBank(const uint8_t *song_data, int song_size,
                                 const uint8_t *ins_data,  int ins_size);
extern void AudioSDL_StopSong(void);
extern void AudioSDL_SetMasterVolume(int vol_0_64);

/* Buffer capacities — must match the array sizes in src/swos_globals.c.
 * The RJP1 loader only uses these to bounds-check the .sng layout. */
#define MENU_SONG_DATA_BYTES  (8  * 1024)
#define MENU_INS_BANK_BYTES   (64 * 1024)
/* SFX bank: sfx.sng is small (~2.3 KB) but sfx.in1 is sample-heavy.
 * sfxSongData is sized 8 KB; poolplyrDatBuffer is 0x14000 = 80 KB
 * (see swos_amiga_data_stubs.c for the size rationale — sfx.in1 is
 * 71.5 KB and the original 0xCBE8 / 52 KB allocation overflowed into
 * imageCmdWorkBuf, clobbering chars sprites). */
#define SFX_SONG_DATA_BYTES   (8  * 1024)
#define SFX_INS_BANK_BYTES    (0x14000)
#endif

/* ---- External globals (all live in SWOS2.s) ---- */
extern uint16_t _audioBankLoaded;
extern volatile uint16_t _audioActive;
extern uint8_t _menuSongData[];
extern uint8_t _menuInsBank[];
extern uint8_t _aSoundMenuSng[];
extern uint8_t _aSoundMenuIns[];
extern uint8_t _sfxSongData[];
extern uint8_t _poolplyrDatBuffer[];
extern uint8_t _sfxInsBank2[];
extern uint8_t _aSoundSfxSng[];
extern uint8_t _aSoundSfxIn1[];
extern uint8_t _aSoundSfxIn2[];
extern volatile uint16_t _currentGameTick;
extern void WaitForVSync(void);   /* game_utils.c — pumps audio + events on SDL */

/* Audio system data (PC-relative in SWOS2.s code section) */
extern AudioState _audioState;
extern AudioChannel _audioChannel0;
extern AudioChannel _audioChannel1;
extern AudioChannel _audioChannel2;
extern AudioChannel _audioChannel3;

/* ---- External C functions ---- */
extern int16_t PlaySong(uint8_t *name, uint8_t *dest);  /* asset_loader.c */

/* ---- Pointer to each channel by index ---- */
static AudioChannel *audioChannels[4];

#ifdef SWOS_SDL_BUILD
/* include/audio_sfx_filter.h: netplay's gate on the effects a re-simulated frame starts. */
static AudioSfxFilter   s_sfxFilter;
static AudioSfxObserver s_sfxObserver;

void Audio_SetSfxFilter(AudioSfxFilter filter)
{
    s_sfxFilter = filter;
}

void Audio_SetSfxObserver(AudioSfxObserver observer)
{
    s_sfxObserver = observer;
}

/* dos_sound.h: something the PC game has a sound for. Kept by a recording and held
 * back on a frame played again, like the Amiga's effects, whatever the sound style. */
void Audio_DosEvent(uint8_t event, uint16_t arg)
{
    if (s_sfxObserver && event != DOS_EV_TICK)
        s_sfxObserver(AUDIO_SFX_DOS, event, arg);
    if (s_sfxFilter && !s_sfxFilter(AUDIO_SFX_DOS, event, arg))
        return;
    DosSound_Event(event, arg);
}
#endif

static void ensureChannelPtrs(void)
{
    audioChannels[0] = &_audioChannel0;
    audioChannels[1] = &_audioChannel1;
    audioChannels[2] = &_audioChannel2;
    audioChannels[3] = &_audioChannel3;
}

/* ================================================================
 * Access (Resload.s:Access) -- a track's address from its table entry
 *
 * The entry is an offset into bank section 5. The unpatched game added the two
 * (`adda.l AUDIOSTATE_BANKSECTIONPTRS+$14(a5),a1`, still in SWOS2.s as a comment).
 * The WHDLoad install our Resload.s comes from masks to 24 bits to fix the
 * highlights ("Fixes bug with Highlights play!") -- but it masks the SUM, and says
 * itself "potential issue if A1 is in fast!!!": the song data is in fast RAM, and
 * fast RAM above 16 MB (Zorro III, any accelerator) lost the top byte of every
 * track's address, so the music played a drum or two and then nothing (2026-10-13,
 * seen from Workbench, not when booted bare). Mask the OFFSET instead, then add:
 * the same address wherever the base sits below 16 MB, and right above it.
 * ================================================================ */
static uint8_t *accessRelocate(uint8_t *ptr)
{
    return _audioState.bankSectionPtrs[5] + ((uint32_t)(uintptr_t)ptr & 0x00FFFFFFu);
}

/* ================================================================
 * StopAllAudioChannels  (SWOS2.s line 9175)
 *
 * Disables all 4 audio DMA channels and clears master volume
 * and mode flags in audioState.
 * ================================================================ */
__attribute__((used, externally_visible))
void StopAllAudioChannels(void)
{
    _audioState.masterVolume = 0;
    _audioState.modeFlags = 0;

    /* Disable DMA for all 4 audio channels (bits 0-3 of DMACON) */
    _custom->dmacon = 0x000F;

    /* Zero all 4 channel volumes — use absolute addresses (not in struct) */
    *(volatile uint16_t *)(CUSTOM_BASE + AUD0VOL_OFF) = 0;
    *(volatile uint16_t *)(CUSTOM_BASE + AUD1VOL_OFF) = 0;
    *(volatile uint16_t *)(CUSTOM_BASE + AUD2VOL_OFF) = 0;
    *(volatile uint16_t *)(CUSTOM_BASE + AUD3VOL_OFF) = 0;
}

/* ================================================================
 * AudioSetChannelVolume  (SWOS2.s line 9192)
 *
 * Sets the volume for a specific channel (0-3) or all channels
 * (if channelOrAll has bit 7 set, e.g. 0xFF/0xFFFF).
 * Volume 0x40 is kept as-is; other values are masked to 0x3F.
 * ================================================================ */
__attribute__((used, externally_visible))
void AudioSetChannelVolume(uint16_t channelOrAll, uint16_t volume)
{
    uint8_t vol;

    /* Clamp: if exactly 0x40, keep it; otherwise mask to 0x3F */
    if (volume != 0x40)
        volume &= 0x3F;
    vol = (uint8_t)volume;
#ifdef SWOS_SDL_BUILD
    if (s_sfxObserver)
        s_sfxObserver(AUDIO_SFX_VOLUME, (channelOrAll & 0x80) ? 0xFF : (uint8_t)(channelOrAll & 3), vol);
    /* The PC chants follow the Amiga's chant channel (dos_sound.c). */
    if (!(channelOrAll & 0x80) && (channelOrAll & 3) == 2)
        DosSound_ChantVolume(vol);
#endif

    /* tst.b d0 / bmi — tests bit 7 of low byte */
    if (channelOrAll & 0x80)
    {
        /* Apply to all channels */
        _audioState.channelVolume[0] = vol;
        _audioState.channelVolume[1] = vol;
        _audioState.channelVolume[2] = vol;
        _audioState.channelVolume[3] = vol;
#ifdef SWOS_SDL_BUILD
        AudioSDL_SetMasterVolume(vol);
        /* Also push the per-channel attenuation to the RJP1 player so a
         * previous single-channel fade-out (e.g. the crowd-chant code
         * writing channel 2 to 0) doesn't survive into the next match.
         * On Amiga, AudioSetChannelVolume(0xFFFF, 0x40) wrote AUD0VOL..
         * AUD3VOL hardware registers — clobbering whatever fade state
         * each channel was in. The SDL backend keeps per-channel
         * attenuation in software (RJP1_SetChannelAttenuation) and we
         * have to mirror the hardware-register semantics by hand:
         * without this, restart-after-match plays out of the left side
         * only because channel 2 (right-panned) stayed at 0 from the
         * previous match's chant fade-out. */
        {
            extern void AudioSDL_SetChannelVolume(int channel, int vol_0_64);
            AudioSDL_SetChannelVolume(0, vol);
            AudioSDL_SetChannelVolume(1, vol);
            AudioSDL_SetChannelVolume(2, vol);
            AudioSDL_SetChannelVolume(3, vol);
        }
#endif
    }
    else
    {
        _audioState.channelVolume[channelOrAll & 3] = vol;
#ifdef SWOS_SDL_BUILD
        /* Without this the per-channel volume only updated the
         * Amiga-side state byte and never reached the SDL audio
         * engine — so e.g. the chant fade-out path (which sets
         * channel-2 volume to 0 over 128 frames) silently did
         * nothing and SFX_CROWD_CHANT_2 looped forever at full
         * volume because its wave had repeat_len > 0. */
        extern void AudioSDL_SetChannelVolume(int channel, int vol_0_64);
        AudioSDL_SetChannelVolume(channelOrAll & 3, vol);
#endif
    }
}

/* ================================================================
 * AudioSetMode  (SWOS2.s line 9215)
 *
 * Sets audio mode flags (bits 0-3). Bit 3 enables primary channel
 * processing; bit 2 enables secondary channel processing.
 * ================================================================ */
__attribute__((used, externally_visible))
void AudioSetMode(uint16_t mode)
{
    mode &= 0x0F;
    _audioState.modeFlags = mode;
    _audioState.toggleFlag = 0;
    _audioState.primaryChanEnable = 0xFF;
    _audioState.secondaryChanEnable = 0xFF;

    if (!(mode & 0x08))
        _audioState.primaryChanEnable = 0;
    if (!(mode & 0x04))
        _audioState.secondaryChanEnable = 0;
}

/* ================================================================
 * ClearAudioChannelStruct  (SWOS2.s line 9164)
 *
 * Zeros out an entire audio channel structure (0xB0 = 176 bytes).
 * Uses word-sized writes (0x58 words = 88 words = 176 bytes).
 * ================================================================ */
__attribute__((optimize("no-tree-loop-distribute-patterns")))
static void clearAudioChannelStruct(AudioChannel *chan)
{
    uint16_t *p = (uint16_t *)chan;
    int16_t i;

    for (i = 0x57; i >= 0; i--)
        *p++ = 0;
}

/* Also exported for potential asm callers */
__attribute__((used, externally_visible))
void ClearAudioChannelStruct(AudioChannel *chan)
{
    clearAudioChannelStruct(chan);
}

/* ================================================================
 * InitAudioState  (SWOS2.s line 9093)
 *
 * Initializes the audio system state from a loaded audio bank.
 *   bankBase: pointer to loaded instrument/sample bank
 *   songPtr:  pointer to loaded song data
 *
 * Parses bank header to extract 7 section pointers, sets default
 * volumes/modes, relocates bank descriptor pointers, and
 * initializes all 4 audio channel structures with hardware
 * register base addresses and DMA bits.
 * ================================================================ */
__attribute__((used, externally_visible))
void InitAudioState(uint8_t *bankBase, uint8_t *songPtr)
{
#ifdef SWOS_SDL_BUILD
    /* The real init parses the bank header byte-for-byte, but the whole
     * bank layout is 68k big-endian. Until we add proper byte-swap
     * support across audio.c / audio_irq.c, just zero the state and
     * bail so the loader-intro music path doesn't crash. Menu music
     * stays silent on SDL for now. */
    (void)bankBase;
    (void)songPtr;
    /* Clear audio state so later ISR tick doesn't see garbage. */
    {
        uint8_t *p = (uint8_t *)&_audioState;
        for (unsigned i = 0; i < sizeof(_audioState); ++i) p[i] = 0;
    }
    _audioState.masterVolume = 0;
    _audioState.modeFlags    = 0;

    /* Even though we don't parse the bank, the IRQ ticks will run and
     * may dispatch to channels later (e.g. after AudioSetMode flips
     * the enable bytes). hwRegBase must point at valid storage or the
     * AUDx writes inside loadAUDxPtrLenIfReloadNeeded* SIGSEGV. The
     * AUDxLC_ADDR macros resolve to (custom + 0xA0/B0/C0/D0) which is
     * already-allocated s_custom_storage on the SDL host. */
    ensureChannelPtrs();

    clearAudioChannelStruct(&_audioChannel0);
    _audioChannel0.hwRegBase = AUD0LC_ADDR;
    _audioChannel0.pvDmaBit = 1;
    _audioChannel0.svDmaBit = 1;

    clearAudioChannelStruct(&_audioChannel1);
    _audioChannel1.hwRegBase = AUD1LC_ADDR;
    _audioChannel1.pvDmaBit = 2;
    _audioChannel1.svDmaBit = 2;

    clearAudioChannelStruct(&_audioChannel2);
    _audioChannel2.hwRegBase = AUD2LC_ADDR;
    _audioChannel2.pvDmaBit = 4;
    _audioChannel2.svDmaBit = 4;

    clearAudioChannelStruct(&_audioChannel3);
    _audioChannel3.hwRegBase = AUD3LC_ADDR;
    _audioChannel3.pvDmaBit = 8;
    _audioChannel3.svDmaBit = 8;
    return;
#else
    uint8_t *src;
    int16_t i;
    uint8_t *descriptors;
    uint8_t *relocBase;
    uint16_t descriptorSize;

    /* songPtr + 4 -> audioState.songPtr */
    _audioState.songPtr = songPtr + 4;

    /* Parse bank header: skip 8-byte header, then 7 sections
     * each prefixed by a 4-byte size field. The bank format is
     * native big-endian from the Amiga disk, so on little-endian
     * hosts we must assemble the size byte-by-byte. */
    src = bankBase + 8;
    for (i = 0; i < 7; i++)
    {
        /* Bank section-size word is big-endian; BE_READ_U32 is a no-op
         * on Amiga BE host, byte-swaps on x86 LE. */
        uint32_t sectionSize = BE_READ_U32(src);
        src += 4;
        _audioState.bankSectionPtrs[i] = src;
        src += sectionSize;
    }

    /* Default master volume and mode */
    _audioState.masterVolume = 0x40;
    _audioState.channelVolume[0] = 0x40;
    _audioState.channelVolume[1] = 0x40;
    _audioState.channelVolume[2] = 0x40;
    _audioState.channelVolume[3] = 0x40;
    _audioState.modeFlags = 0x000D;
    _audioState.irqSubtick = 0;
    _audioState.toggleFlag = 0;
    _audioState.primaryChanEnable = 0xFF;
    _audioState.secondaryChanEnable = 0xFF;

    /* Relocate bank descriptors:
     * descriptors = bankSectionPtrs[0]
     * relocBase = songPtr (after +4 offset)
     * descriptorSize = word before descriptors (at descriptors[-4])
     * Each descriptor is 0x20 bytes with 3 pointers to relocate. */
    descriptors = _audioState.bankSectionPtrs[0];
    relocBase = _audioState.songPtr;
    /* asm: move.l -4(a2),d1 loads 32-bit, but subi.w #$20,d1 / bne
     * tests only the low 16 bits. Read low word of the 32-bit field. */
    descriptorSize = (uint16_t)(*(uint32_t *)(descriptors - 4));

    while (descriptorSize != 0)
    {
        uint32_t val;

        /* Relocate ptr at +0x00 (always nonzero) */
        val = *(uint32_t *)(descriptors + 0);
        val += (uint32_t)relocBase;
        *(uint32_t *)(descriptors + 0) = val;

        /* Relocate ptr at +0x04 (skip if zero) */
        val = *(uint32_t *)(descriptors + 4);
        if (val != 0)
            val += (uint32_t)relocBase;
        *(uint32_t *)(descriptors + 4) = val;

        /* Relocate ptr at +0x08 (skip if zero) */
        val = *(uint32_t *)(descriptors + 8);
        if (val != 0)
            val += (uint32_t)relocBase;
        *(uint32_t *)(descriptors + 8) = val;

        /* Double the word at +0x10 */
        *(uint16_t *)(descriptors + 0x10) *= 2;

        descriptors += 0x20;
        descriptorSize -= 0x20;
    }

    /* Initialize all 4 audio channels */
    ensureChannelPtrs();

    clearAudioChannelStruct(&_audioChannel0);
    _audioChannel0.hwRegBase = AUD0LC_ADDR;
    _audioChannel0.pvDmaBit = 1;
    _audioChannel0.svDmaBit = 1;

    clearAudioChannelStruct(&_audioChannel1);
    _audioChannel1.hwRegBase = AUD1LC_ADDR;
    _audioChannel1.pvDmaBit = 2;
    _audioChannel1.svDmaBit = 2;

    clearAudioChannelStruct(&_audioChannel2);
    _audioChannel2.hwRegBase = AUD2LC_ADDR;
    _audioChannel2.pvDmaBit = 4;
    _audioChannel2.svDmaBit = 4;

    clearAudioChannelStruct(&_audioChannel3);
    _audioChannel3.hwRegBase = AUD3LC_ADDR;
    _audioChannel3.pvDmaBit = 8;
    _audioChannel3.svDmaBit = 8;
#endif /* !SWOS_SDL_BUILD */
}

/* ================================================================
 * AudioDisableDMAForChannel  (SWOS2.s line 9426)
 *
 * Disables DMA for the specified audio channel by writing its
 * DMA bit to DMACON (without SET bit, so it clears).
 * ================================================================ */
__attribute__((used, externally_visible))
void AudioDisableDMAForChannel(AudioChannel *chan)
{
    _custom->dmacon = chan->pvDmaBit;
}

/* ================================================================
 * AudioChannelNoteOnOff  (SWOS2.s line 9270)
 *
 * Starts or stops a note on a specific audio channel.
 * If noteId is 0, stops the channel (disables DMA, clears enabled flag).
 * Otherwise, looks up note data and wave data from the audio bank
 * and sets up the channel for playback.
 *
 * Params: channel 0-3, noteId (0 = stop, nonzero = start note)
 * ================================================================ */
__attribute__((used, externally_visible))
void AudioChannelNoteOnOff(uint16_t channel, uint16_t noteId)
{
    AudioChannel *chan;
    uint8_t *noteTable;
    uint32_t noteOffset;
    uint8_t *noteData;
    uint8_t waveIdx;
    uint32_t waveOffset;
    uint8_t *waveTable;
    uint8_t *waveData;

    ensureChannelPtrs();

    /* Set channel change bit */
    _audioState.channelChangeFlags |= (uint8_t)(1 << (channel & 3));

    /* Get channel pointer */
    chan = audioChannels[channel & 3];

    if (noteId == 0)
    {
        /* Stop channel */
        chan->pvEnabled = 0;
        AudioDisableDMAForChannel(chan);
        return;
    }

    /* Look up note data from bank section 3 */
    noteTable = _audioState.bankSectionPtrs[3];  /* offset 0x10 from state */
    noteOffset = noteId * 4;
    noteData = *(uint8_t **)(noteTable + noteOffset);

    /* Access: the table entry is an offset into bank section 5 */
    noteData = accessRelocate(noteData);

    /* First byte of note data is the wave index */
    waveIdx = *noteData++;
    chan->pvNoteScript = (uintptr_t)noteData;

    /* Look up wave data from bank section 4, relocate with section 6 */
    waveTable = _audioState.bankSectionPtrs[4];  /* offset 0x14 from state */
    waveOffset = (uint32_t)waveIdx * 4;
    waveData = *(uint8_t **)(waveTable + waveOffset);
    waveData = (uint8_t *)((uintptr_t)waveData + (uintptr_t)_audioState.bankSectionPtrs[6]);

    chan->pvWaveData = (uintptr_t)waveData;
    chan->pvStepPeriod = 6;
    chan->pvScriptTimer = 1;
    chan->pvPeriodCounter = 1;
    chan->pvEnabled = 0xFF;
}

/* ================================================================
 * AudioApplyPresetToChannels  (SWOS2.s line 9236)
 *
 * Applies a preset instrument/note configuration to all 4 channels.
 * Looks up preset data from audioPresetChannelCmdTable (bankSectionPtrs[2]).
 * Each preset is 4 bytes (one per channel). Zero = skip, nonzero = note ID.
 * ================================================================ */
__attribute__((used, externally_visible))
void AudioApplyPresetToChannels(uint16_t presetId)
{
#ifdef SWOS_SDL_BUILD
    /* Walk the preset table just like the Amiga byte-poking engine:
     * one 4-byte preset record = one noteId per channel; each non-zero
     * noteId fires a sample on its channel after a song_data[3]+[5]
     * indirection. AudioSDL_TriggerSfxPreset does the whole walk. */
    extern void AudioSDL_TriggerSfxPreset(int preset_id);
    if (s_sfxObserver)
        s_sfxObserver(AUDIO_SFX_PRESET, 0xFF, (uint16_t)(presetId & 0x3F));
    if (s_sfxFilter && !s_sfxFilter(AUDIO_SFX_PRESET, 0xFF, (uint16_t)(presetId & 0x3F)))
        return;
    if (DosSound_Live())
        return;                 /* the PC sounds have this match */
    AudioSDL_TriggerSfxPreset((int)(presetId & 0x3F));   /* 6-bit preset id, see Amiga &= 0x3F */
    return;
#else
    uint8_t *presetTable;
    uint8_t *preset;
    uint16_t noteId;

    presetId &= 0x3F;

    /* bankSectionPtrs[2] is the preset table pointer */
    presetTable = _audioState.bankSectionPtrs[2];
    preset = presetTable + (uint32_t)presetId * 4;

    /* Channel 0 */
    noteId = preset[0];
    if (noteId != 0)
        AudioChannelNoteOnOff(0, noteId);

    /* Channel 1 */
    noteId = preset[1];
    if (noteId != 0)
        AudioChannelNoteOnOff(1, noteId);

    /* Channel 2 */
    noteId = preset[2];
    if (noteId != 0)
        AudioChannelNoteOnOff(2, noteId);

    /* Channel 3 */
    noteId = preset[3];
    if (noteId != 0)
        AudioChannelNoteOnOff(3, noteId);
#endif /* !SWOS_SDL_BUILD */
}

/* ================================================================
 * AudioChannelQueueNextWaveIfEnabled  (SWOS2.s line 9308)
 *
 * Queues a next wave for playback on a channel, but only if
 * secondary channel processing (modeFlags bit 2) is enabled.
 *
 * Params: channel 0-3, waveId (index into bank section 4)
 * ================================================================ */
__attribute__((used, externally_visible))
void AudioChannelQueueNextWaveIfEnabled(uint16_t channel, uint16_t waveId)
{
#ifdef SWOS_SDL_BUILD
    /* Mirror Amiga: bankSectionPtrs[4]+[6] indirection on waveId.
     * Without this, kicks come out as whistles and vice-versa.
     * AudioSDL_TriggerSfxByWaveId does the lookup + trigger. */
    extern void AudioSDL_TriggerSfxByWaveId(int channel, int wave_id);
    if (s_sfxObserver)
        s_sfxObserver(AUDIO_SFX_WAVE, (uint8_t)(channel & 3), waveId);
    if (s_sfxFilter && !s_sfxFilter(AUDIO_SFX_WAVE, (uint8_t)(channel & 3), waveId))
        return;
    if (DosSound_Live()) {
        /* The PC sounds have this match. The one thing taken from the Amiga's is
         * WHEN a crowd chant starts: PlayCrowdChants queues it on channel 2. */
        extern uint16_t _crowdSamples[4];
        int32_t slot;
        for (slot = 0; (channel & 3) == 2 && slot < 4; slot++)
            if (_crowdSamples[slot] == waveId)
                DosSound_ChantStart(slot);
        return;
    }
    /* GateKickSampleAndCrowd (SWOS2.s 12019): a kick, pass or bounce does not replace a
     * sound still playing on its channel, unless that sound is one of the two crowd
     * reactions. On the Amiga the test is bit 7 of the audio interrupt's result -- "the
     * wave queued on channel 3 has not reached its end" -- which is never set here, so
     * every bounce after full time cut the final whistle short, and every kick cut the
     * one before it. Decided HERE, on what is heard, and by the wave: the four the gate
     * is called with are started nowhere else, a recording replays them through this
     * routine, and the match must not read the mixer (docs/NETPLAY.md, "Sound during a
     * rollback"). */
    {
        extern int AudioSDL_SfxChannelBusy(int channel);
        static uint16_t s_lastWave[4];
        const int ch = (int)(channel & 3);
        if ((waveId == SFX_BALL_BOUNCE_1 || waveId == SFX_BALL_PASSED_OR_TACKLED ||
             waveId == SFX_BALL_KEEPER_SAFE_2 || waveId == SFX_BALL_SHOT_HEADER_LOW ||
             waveId == SFX_BALL_BOUNCE_3) &&
            s_lastWave[ch] != SFX_CROWD_KEEPERSAVES && s_lastWave[ch] != SFX_CROWD_NEARMISS &&
            AudioSDL_SfxChannelBusy(ch)) {
            static int trace = -1;      /* SWOS_SFX_GATE_TRACE=1: what the gate held back */
            if (trace < 0)
                trace = getenv("SWOS_SFX_GATE_TRACE") != NULL;
            if (trace)
                fprintf(stderr, "[sfx] wave %u held back: wave %u is still playing on channel %d\n",
                        waveId, s_lastWave[ch], ch);
            return;
        }
        s_lastWave[ch] = waveId;
    }
    AudioSDL_TriggerSfxByWaveId((int)(channel & 3), (int)waveId);
    return;
#else
    AudioChannel *chan;
    uint8_t *waveTable;
    uint32_t waveOffset;
    uint8_t *waveData;

    /* Check modeFlags bit 2 (audioFlags bit 2 in asm) */
    if (!(_audioState.modeFlags & 0x04))
        return;

    ensureChannelPtrs();

    /* Set channel change bit (upper nibble: bit d0+4) */
    _audioState.channelChangeFlags |= (uint8_t)(1 << ((channel & 3) + 4));

    chan = audioChannels[channel & 3];

    /* Look up wave data */
    waveTable = _audioState.bankSectionPtrs[4];
    waveOffset = (uint32_t)waveId * 4;
    waveData = *(uint8_t **)(waveTable + waveOffset);
    waveData = (uint8_t *)((uint32_t)waveData + (uint32_t)_audioState.bankSectionPtrs[6]);

    chan->svWaveData = (uint32_t)waveData;
    chan->svStepPeriod = 6;
    chan->svScriptTimer = 1;
    chan->svPeriodCounter = 1;
#endif /* !SWOS_SDL_BUILD */
}

/* ================================================================
 * AudioBeepAllShort  (SWOS2.s line 10978)
 *
 * Fades audio volume from 0x40 down to 0, waiting 2 game ticks
 * per step. Then sets final volume to 0.
 * ================================================================ */
__attribute__((used, externally_visible))
void AudioBeepAllShort(void)
{
    int16_t vol;

    for (vol = 0x40; vol >= 0; vol--)
    {
        int16_t i;

        AudioSetChannelVolume(0xFFFF, (uint16_t)vol);

        /* 2 vblanks per step. WaitForVSync (vs. raw spin on currentGameTick)
         * keeps AudioSDL_Tick fed during the fade so the queue doesn't drain. */
        for (i = 1; i >= 0; i--) {
            WaitForVSync();
        }
    }

    AudioSetChannelVolume(0xFFFF, 0);
}

/* ================================================================
 * InitMenuMusic  (SWOS2.s line 11167)
 * ================================================================ */
__attribute__((used, externally_visible))
void InitMenuMusic(void)
{
#ifdef SWOS_SDL_BUILD
    /* No Amiga music installed (main.c): the PC game's plays instead. */
    { extern int _gNoAmigaSound;
      if (_gNoAmigaSound) { _audioBankLoaded = 0; _audioActive = 0; return; } }
#endif
retry:
    _audioBankLoaded = 0;
    _audioActive = 0;
    if (PlaySong(_aSoundMenuSng, _menuSongData) != 0)
        goto retry;
    if (PlaySong(_aSoundMenuIns, _menuInsBank) != 0)
        goto retry;
}

/* ================================================================
 * TryPlayMusic  (SWOS2.s line 11189)
 * ================================================================ */
__attribute__((used, externally_visible))
void TryPlayMusic(void)
{
    uint16_t vol;
    uint16_t tick;

    /* Don't kick off music if the user just hit the X button. Without
     * this gate, pressing X mid-match causes the menu unwind to call
     * CycleMenuMusic → TryPlayMusic, starting the menu song just
     * before the process exits — audible as a brief musical blip. */
    extern uint16_t _quitToDos;
    if (_quitToDos != 0)
        return;

    if (_audioActive != 0)
        return;
#ifdef SWOS_SDL_BUILD
    DosSound_Stop();        /* back in the menus: no match's crowd under their music */
#endif

#ifdef SWOS_SDL_BUILD
    /* On SDL the RJP1 player produces samples directly into the SDL
     * audio callback. Load (once) and start subsong 1, then fade in via
     * the master-volume ramp. */
    if (_audioBankLoaded == 0) {
        extern int AudioSDL_PcMusicStart(void);
        /* [audio] music = pc: the PC game's menu.xmi (src/dos_music.c). */
        extern int AudioSDL_StreamStart(const char *name, int loop);
        if (DosMusic_Style() && DosMusic_MenuRecording() && AudioSDL_StreamStart("menu", 1) == 0) {
            /* [audio] menu_music = mp3: a recording of it, sound_dos/menu.mp3 */
        } else
        if (DosMusic_Style() && AudioSDL_PcMusicStart() == 0) {
        } else
        if (AudioSDL_StartSong(_menuSongData, MENU_SONG_DATA_BYTES,
                               _menuInsBank,  MENU_INS_BANK_BYTES, 1) != 0) {
            /* Load failed — keep audio silent but mark "loaded" so we
             * don't hammer the loader every vblank. */
        }
        _audioBankLoaded = 1;
    }

    AudioSetChannelVolume(0xFFFF, 0);
    _audioActive = 1;

    for (vol = 0; vol <= 0x3E; vol += 2) {   /* SDL: ramp to full (0x40) — see loader_intro.c */
        AudioSetChannelVolume(0xFFFF, vol);
        WaitForVSync();
    }
    AudioSetChannelVolume(0xFFFF, 0x40);
    return;
#else
    if (_audioBankLoaded == 0)
    {
        StopAllAudioChannels();
        InitAudioState(_menuSongData, _menuInsBank);
        _audioBankLoaded = 1;
    }

    AudioSetChannelVolume(0xFFFF, 0);
    AudioApplyPresetToChannels(0);
    _audioActive = 1;

    for (vol = 0; vol <= 0x22; vol += 2)
    {
        AudioSetChannelVolume(0xFFFF, vol);
        WaitForVSync();
    }

    AudioSetChannelVolume(0xFFFF, 0x23);
#endif
}

/* ================================================================
 * ClearAudioActiveFlag  (SWOS2.s line 10916)
 * ================================================================ */
__attribute__((used, externally_visible))
void ClearAudioActiveFlag(void)
{
    _audioActive = 0;
}

/* ================================================================
 * StopAudioAndClearFlags  (SWOS2.s line 10922)
 * ================================================================ */
__attribute__((used, externally_visible))
void StopAudioAndClearFlags(void)
{
    DOS_SOUND(DOS_EV_MATCH_END, 0);
    StopAllAudioChannels();
    _audioActive = 0;
    _audioBankLoaded = 0;
}

/* ================================================================
 * LoadSfxResources  (SWOS2.s line 10880)
 * ================================================================ */
__attribute__((used, externally_visible))
void LoadSfxResources(void)
{
#ifdef SWOS_SDL_BUILD
    /* No Amiga effects installed (main.c): the PC game's are used instead. */
    { extern int _gNoAmigaSound;
      if (_gNoAmigaSound) { StopAllAudioChannels(); _audioBankLoaded = 0; return; } }
#endif
retry:
    StopAllAudioChannels();
    _audioBankLoaded = 0;
    if (PlaySong(_aSoundSfxSng, _sfxSongData) != 0)
        goto retry;
#ifndef SWOS_SDL_BUILD
    /* Amiga path: SFX.IN1 / SFX.IN2 are loaded into poolplyrDatBuffer
     * and sfxInsBank2 (which are BSS-adjacent on Amiga, forming the
     * single contiguous instrument blob RJP1 expects). On SDL we
     * load both into s_sfx_combined_ins below — and we MUST NOT load
     * SFX.IN1 into poolplyrDatBuffer here, because that buffer is
     * also used as the destination for `data/poolplyr.dat` (the
     * foreign-player pool). Loading SFX over it corrupts the pool
     * and the next foreign-market visit walks SFX bytes interpreted
     * as PoolPlayer records, eventually SIGSEGV'ing in
     * FindPoolplyrDatPlayer past the buffer end. */
    if (PlaySong(_aSoundSfxIn1, _poolplyrDatBuffer) != 0)
        goto retry;
    if (PlaySong(_aSoundSfxIn2, _sfxInsBank2) != 0)
        goto retry;
#endif

#ifdef SWOS_SDL_BUILD
    /* The original Amiga relies on `poolplyrDatBuffer` (= SFX.IN1) and
     * `sfxInsBank2` (= SFX.IN2) being adjacent in BSS — RJP1
     * instrument records carry 32-bit byte offsets that index into
     * the combined IN1+IN2 blob (see rjp1player main.cpp:215, which
     * concatenates them on load). Our SDL linker places IN1 and IN2
     * in different translation units, so they're scattered across
     * BSS and offsets that should reach into IN2 land in unrelated
     * memory — producing the intermittent "clicking loop" the user
     * heard on chants like 0x3E whose sample lives past IN1's end.
     *
     * Fix: rebuild a contiguous IN1+IN2 blob and hand THAT to the
     * RJP1 player. We use LoadFileEx (rather than PlaySong) because
     * it returns the file size, which we need to know where IN2
     * starts. Sized 256 KB to comfortably hold both banks
     * (71500 + 81068 = 152568 bytes today, with headroom). */
    extern int16_t LoadFileEx(uint8_t *, uint8_t *, uint32_t *);
    static uint8_t s_sfx_combined_ins[256 * 1024];
    uint32_t in1_size = 0, in2_size = 0;
    if (LoadFileEx(_aSoundSfxIn1, s_sfx_combined_ins, &in1_size) != 0)
        goto retry;
    if (in1_size + 1 >= sizeof(s_sfx_combined_ins))
        goto retry;   /* impossible with sized files, but defensive */
    if (LoadFileEx(_aSoundSfxIn2, s_sfx_combined_ins + in1_size, &in2_size) != 0)
        goto retry;

    /* Hand the combined bank to the RJP1 player so AudioSDL_TriggerSfx
     * (called from AudioApplyPresetToChannels /
     * AudioChannelQueueNextWaveIfEnabled) has samples to fire. The
     * Amiga byte-poking engine in audio_irq.c is stubbed on SDL —
     * this is what makes in-match SFX audible. */
    AudioSDL_LoadSfxBank(_sfxSongData, SFX_SONG_DATA_BYTES,
                         s_sfx_combined_ins, in1_size + in2_size);
#endif
}

/* ================================================================
 * EnsureSfxLoadedAndStart  (SWOS2.s line 10899)
 * ================================================================ */
__attribute__((used, externally_visible))
void EnsureSfxLoadedAndStart(void)
{
    if (_audioBankLoaded == 0)
    {
        StopAllAudioChannels();
        InitAudioState(_sfxSongData, _poolplyrDatBuffer);
        _audioBankLoaded = 1;
    }
    AudioSetChannelVolume(0xFFFF, 0x40);
    _audioActive = 1;
}

/* ================================================================
 * StopMusic  (SWOS2.s line 10947)
 * ================================================================ */
__attribute__((used, externally_visible))
void StopMusic(void)
{
    int16_t vol;

    if (_audioActive == 0)
        return;

#ifdef SWOS_SDL_BUILD
    for (vol = 0x40; vol >= 0; vol--)   /* SDL plays music at full; fade from there */
#else
    for (vol = 0x23; vol >= 0; vol--)
#endif
    {
        int16_t i;

        AudioSetChannelVolume(0xFFFF, (uint16_t)vol);

        for (i = 1; i >= 0; i--) {
            WaitForVSync();
        }
    }

    AudioSetChannelVolume(0xFFFF, 0);
    _audioActive = 0;
#ifdef SWOS_SDL_BUILD
    AudioSDL_StopSong();
    _audioBankLoaded = 0;   /* next TryPlayMusic will re-load the song */
#endif
}

#ifdef SWOS_SDL_BUILD
/* OPTIONS -> MENU MUSIC off: StopMusic's fade, run by the mixer (AudioSDL_FadeOutSong),
 * so the menu does not stop for it -- StopMusic's loop held it ~2.6 s. 72 ticks, the
 * Amiga's fade (0x23 steps of two frames). */
void StopMusicInBackground(void)
{
    extern void AudioSDL_FadeOutSong(int ticks);

    if (_audioActive == 0)
        return;
    _audioState.channelVolume[0] = _audioState.channelVolume[1] = 0;
    _audioState.channelVolume[2] = _audioState.channelVolume[3] = 0;
    AudioSDL_FadeOutSong(72);
    _audioActive = 0;
    _audioBankLoaded = 0;   /* next TryPlayMusic will re-load the song */
}
#endif

/* ================================================================
 * CycleMenuMusic  (SWOS2.s line 69209)
 * ================================================================ */
__attribute__((used, externally_visible))
void CycleMenuMusic(void)
{
    extern uint16_t _menuMusic;
    if (_menuMusic == 0)
        StopMusic();
    else
        TryPlayMusic();
}

/* ================================================================
 * TryRestartingMusic  (SWOS2.s line 10935)
 * ================================================================ */
__attribute__((used, externally_visible))
void TryRestartingMusic(void)
{
    if (_audioActive == 0)
        TryPlayMusic();
    else
        StopMusic();
}
