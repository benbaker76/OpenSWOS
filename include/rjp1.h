/*
 * rjp1.h -- pure-C port of the Richard Joseph Player (.sng + .ins).
 *
 * Adapted from cyxx's C++ player (C:\msys64\home\headk\rjp1player), which
 * is itself a clean reimplementation of the Amiga RJP1 sound module
 * format used by Sensible World of Soccer for music and SFX.
 *
 * The player owns a small global state. To play a song call:
 *   RJP1_Load(song_data, song_size, ins_data, ins_size);
 *   RJP1_StartSong(subsong);   // 1..N
 *
 * The SDL audio callback drives playback by calling, for each output
 * buffer:
 *   while (bytes_left) {
 *       if (samples_left == 0) {
 *           RJP1_HandleTick();
 *           samples_left = SAMPLE_RATE * 2 / 50;   // bytes until next 50 Hz tick
 *       }
 *       int n = MIN(samples_left, bytes_left);
 *       RJP1_MixSamples(SAMPLE_RATE, buf, n);
 *       buf += n; bytes_left -= n; samples_left -= n;
 *   }
 *
 * Output is signed 8-bit stereo at any host sample rate.
 */
#ifndef RJP1_H
#define RJP1_H

#include "game_types.h"   /* for uint8_t / uint16_t / int16_t / int32_t / uint32_t */

#define RJP1_NUM_CHANNELS 4
#define RJP1_PAULA_FREQ   3546897   /* PAL (SWOS is European) */
#define RJP1_FRAC_BITS    12

typedef struct RJP1_PaulaChannel {
    int      active;
    const int8_t *data;
    uint32_t len;
    int      per;
    int      vol;
    uint32_t repeat_pos;
    uint32_t repeat_len;
    uint32_t pos;
    /* 3-stage cascaded one-pole low-pass filter state, modelling the
     * Amiga A1200 analog RC chain. Without this the broadband loop-end
     * transients in some RJP1 samples produce a buzzy click that
     * masks the underlying tone — most audible on the vocal samples
     * in HERO.SNG, where it sounded like the singing channel was
     * outright missing. Cyxx player upstream uses the same filter. */
    float    filter_state[3];
} RJP1_PaulaChannel;

typedef struct RJP1_Channel {
    RJP1_PaulaChannel hw_channel;
    /* Note's initial play position in BYTES (stored as `initial offset / 2`
     * in the sample list, so *2 to get bytes). setup_note stashes it here
     * and turn_on_channel copies it to hw_channel.pos (in the fractional
     * form used by the mixer). Without this, the Amiga initial-offset
     * field was being treated as a byte offset of `data` instead of a
     * word offset into waveData — music samples with non-zero initial
     * offset played back from the wrong byte. */
    uint32_t       init_pos_bytes;
    const uint8_t *wave_data;
    const uint8_t *modulate_period_data;
    const uint8_t *modulate_volume_data;
    const uint8_t *envelope_data;
    uint16_t volume_scale;
    int16_t  volume;
    uint16_t modulate_period_base;
    uint32_t modulate_period_limit;
    uint32_t modulate_period_index;
    uint16_t modulate_volume_base;
    uint32_t modulate_volume_limit;
    uint32_t modulate_volume_index;
    uint16_t repeat_pos;
    uint16_t repeat_len;
    uint8_t  freq_step;
    uint32_t freq_inc;
    uint32_t freq_init;
    const uint8_t *note_data;
    const uint8_t *sequence_offsets;
    const uint8_t *sequence_data;
    uint8_t  loop_seq_count;
    uint8_t  loop_seq_cur;
    uint8_t  loop_seq2_count;
    uint8_t  loop_seq2_cur;
    int      active;
    int16_t  modulate_period_init;
    int16_t  modulate_period_next;
    int      setup_new_note;
    uint8_t  envelope_mode;
    int8_t   envelope_scale;
    uint8_t  envelope_end1;
    uint8_t  envelope_end2;
    uint8_t  envelope_start;
    uint8_t  envelope_volume;
    uint8_t  current_instrument;
} RJP1_Channel;

typedef struct RJP1_Vars {
    const uint8_t *inst_data;
    const uint8_t *song_data[7];
    uint8_t  active_channels_mask;
    uint8_t  current_channel;
} RJP1_Vars;

typedef struct RJP1_Player {
    RJP1_Vars    vars;
    RJP1_Channel channels[RJP1_NUM_CHANNELS];
    int          subsongs_count;
    int          instruments_count;
    int          loaded;
    /* Per-channel post-attenuation, 0..64. Applied in the mixer on top
     * of the wave-script's hw->vol so the host can scale individual
     * channels (e.g. fade the crowd-chant channel) without poking the
     * RJP1 internals. Default 64 = unity. */
    int          channel_attenuation[RJP1_NUM_CHANNELS];
    /* Per-channel volume the player chose (0..256, 256 = unity): the music's, or a sound
     * kind's -- the crowd chants' channel, the effects' (audio_sdl.c, [audio] *_volume). */
    int          channel_gain[RJP1_NUM_CHANNELS];
} RJP1_Player;

/* Returns 1 on success, 0 on failure. */
int  RJP1_Load(RJP1_Player *p,
               const uint8_t *song_data, int song_size,
               const uint8_t *ins_data,  int ins_size);

void RJP1_StartSong(RJP1_Player *p, uint8_t song);
void RJP1_StopAll(RJP1_Player *p);

/* Tick at 50 Hz for music, advances pattern playback. */
void RJP1_HandleTick(RJP1_Player *p);

/* Non-zero once the song has fully ended: every sequence has stopped
 * (active_channels_mask == 0) AND no hardware channel is still playing a
 * sample. Used by the host to loop a one-shot song (e.g. HERO, which has no
 * internal loop-back and otherwise falls silent when it finishes). */
int RJP1_IsFinished(const RJP1_Player *p);

/* Mix `len` bytes (== `len/2` stereo frames) of signed-8-bit-stereo
 * samples into `buf`. Caller is expected to have memset(buf, 0, ...)
 * first; this function does the per-channel mix-and-clip. */
void RJP1_MixSamples(RJP1_Player *p, int sample_rate, int8_t *buf, int len);

/* Start a sequence on `channel` by note id — equivalent to the
 * Amiga `AudioChannelNoteOnOff(channel, noteId)`. Does the
 * song_data[3]+[5] → first-byte → song_data[4]+[6] indirection
 * (same as RJP1's internal start_sequence used by music). The
 * resolved sequence_data is fed to the regular play_channel /
 * play_song_sequence pipeline so the SFX plays exactly the same
 * way music does. No-op if the player isn't loaded.
 *
 * noteId == 0 stops the channel (Amiga convention). */
void RJP1_StartSequenceByNoteId(RJP1_Player *p, int channel, int noteId);

/* Start a wave sequence on `channel` by wave id — equivalent to
 * the Amiga `AudioChannelQueueNextWaveIfEnabled(channel, waveId)`.
 * Skips the song_data[3]+[5] step and goes directly:
 *   sequence_data = song_data[4]+[6]+(BE32 at song_data[4]+waveId*4)
 *
 * waveId == 0 stops the channel. */
void RJP1_StartSequenceByWaveId(RJP1_Player *p, int channel, int waveId);

/* Returns a 4-byte preset record (channel_noteIds[0..3]) from
 * song_data[2] + presetId*4. Used by AudioApplyPresetToChannels. */
void RJP1_PresetEntry(RJP1_Player *p, int presetId, uint8_t out_note_ids[4]);

/* Set per-channel post-attenuation (0..64). 64 = unity, 0 = mute.
 * Applied in the mixer on top of the wave-script's hw->vol — this is
 * the hook for per-channel volume control from outside the player
 * (e.g. AudioSetChannelVolume's chant fade-in/fade-out path).
 * Out-of-range channels and out-of-range volumes are clamped. */
void RJP1_SetChannelAttenuation(RJP1_Player *p, int channel, int vol_0_64);

#endif /* RJP1_H */
