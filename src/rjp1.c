/*
 * rjp1.c -- pure-C port of cyxx's Richard Joseph Player.
 *
 * Original: C:\msys64\home\headk\rjp1player\rjp1_player.cpp (C++ class).
 * Format spec: docs/Richard Joseph Player (.sng, .ins).txt in the same
 * directory.
 *
 * Faithful translation: same field names, same control flow. The Amiga
 * Paula `(uint16 *)0xDFF096 = ...` register pokes from the original
 * are commented out in the C++ source — we follow suit. The actual
 * sample mix happens in `RJP1_MixSamples` which writes signed 8-bit
 * stereo into the SDL audio callback's buffer.
 */
#include "rjp1.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>   /* tan() for filter coefficient setup */

/* ---- Big-endian readers --------------------------------------------
 * The .sng/.ins payloads are Amiga BE; our host is x86 LE. Same
 * helpers as sys.h in the upstream player. */
static inline uint16_t be16(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return (uint16_t)((b[0] << 8) | b[1]);
}
static inline uint32_t be32(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] <<  8) |  (uint32_t)b[3];
}

/* ---- Constants from upstream ---------------------------------------- */
static const uint8_t  kSongSig[8] = { 'R','J','P','1','S','M','O','D' };
static const uint8_t  kInstSig[8] = { 'R','J','P','1', 0,0,0,0 };
static const uint16_t kPeriodsTable[] = {
    0x01C5, 0x01E0, 0x01FC, 0x021A, 0x023A, 0x025C, 0x0280, 0x02A6, 0x02D0,
    0x02FA, 0x0328, 0x0358, 0x00E2, 0x00F0, 0x00FE, 0x010D, 0x011D, 0x012E,
    0x0140, 0x0153, 0x0168, 0x017D, 0x0194, 0x01AC, 0x0071, 0x0078, 0x007F,
    0x0087, 0x008F, 0x0097, 0x00A0, 0x00AA, 0x00B4, 0x00BE, 0x00CA, 0x00D6
};
#define PERIODS_COUNT ((int)(sizeof(kPeriodsTable)/sizeof(kPeriodsTable[0])))

/* ---- Forward declarations ------------------------------------------ */
static void turn_off_channel(RJP1_Channel *ch);
static void play_channel(RJP1_Player *p, RJP1_Channel *ch);
static void turn_on_channel(RJP1_Channel *ch);
static void play_song_sequence(RJP1_Player *p, RJP1_Channel *ch);
static void modulate_volume(RJP1_Channel *ch);
static void modulate_period(RJP1_Channel *ch);
static void setup_note(RJP1_Player *p, RJP1_Channel *ch, uint16_t freq);
static void setup_instrument(RJP1_Player *p, RJP1_Channel *ch, uint8_t d0);
static void set_release(RJP1_Channel *ch);
static void modulate_volume_envelope(RJP1_Channel *ch);
static void set_sustain(RJP1_Channel *ch);
static void set_decay(RJP1_Channel *ch);
static void modulate_volume_waveform(RJP1_Channel *ch);
static void set_volume(RJP1_Channel *ch);
static void start_sequence(RJP1_Player *p, uint8_t channel_num, uint8_t seq_num);

/* ================================================================== */
int RJP1_Load(RJP1_Player *p,
              const uint8_t *song_data, int song_size,
              const uint8_t *ins_data,  int ins_size)
{
    p->loaded = 0;
    if (!song_data || song_size < 8 + 7 * 4) return 0;
    if (!ins_data  || ins_size  < 8)         return 0;
    if (memcmp(song_data, kSongSig, 8) != 0) return 0;
    if (memcmp(ins_data,  kInstSig, 4) != 0) return 0;   /* "RJP1" + zero bytes */

    p->vars.inst_data = ins_data + 4;

    const uint8_t *q   = song_data + 8;
    const uint8_t *end = song_data + song_size;
    for (int i = 0; i < 7; ++i) {
        if (q + 4 > end) return 0;
        uint32_t _size = be32(q); q += 4;
        p->vars.song_data[i] = q;
        if (q + _size > end) return 0;
        switch (i) {
        case 0: p->instruments_count = (int)(_size / 32); break;
        case 2: p->subsongs_count    = (int)(_size /  4); break;
        default: break;
        }
        q += _size;
    }
    memset(p->channels, 0, sizeof(p->channels));
    p->vars.active_channels_mask = 0;
    p->vars.current_channel      = 0;
    /* Default per-channel attenuation to unity (64 = full volume).
     * The host scales individual channels by writing a smaller value
     * via RJP1_SetChannelAttenuation. */
    for (int i = 0; i < RJP1_NUM_CHANNELS; ++i) {
        p->channel_attenuation[i] = 64;
        if (p->channel_gain[i] <= 0 || p->channel_gain[i] > 256)
            p->channel_gain[i] = 256;       /* (the host's own, kept across loads; unset: unity) */
    }
    p->loaded = 1;
    return 1;
}

void RJP1_StopAll(RJP1_Player *p)
{
    for (int i = 0; i < RJP1_NUM_CHANNELS; ++i) {
        p->channels[i].active = 0;
        p->channels[i].hw_channel.active = 0;
        p->channels[i].hw_channel.len = 0;
    }
    p->vars.active_channels_mask = 0;
}

void RJP1_StartSong(RJP1_Player *p, uint8_t song)
{
    if (!p->loaded) return;

    /* The subsong number is a 0-based index into song_data[2]'s
     * 4-byte-per-record subsong table — matches the upstream cyxx
     * convention (rjp1player/rjp1_player.cpp:start_song). For
     * single-subsong files (e.g. MENU.SNG, subsongs_count == 1)
     * the only valid entry is 0 and the clamp handles that. */
    if (p->subsongs_count <= 0) return;
    int idx = (int)song;
    if (idx < 0) idx = 0;
    if (idx >= p->subsongs_count) idx = p->subsongs_count - 1;

    /* Clear any in-flight playback. */
    RJP1_StopAll(p);

    const uint8_t *q = p->vars.song_data[2] + (idx & 0x3F) * 4;
    for (int i = 0; i < 4; ++i) {
        uint8_t seq = *q++;
        if (seq) start_sequence(p, (uint8_t)i, seq);
    }
}

static void start_sequence(RJP1_Player *p, uint8_t channel_num, uint8_t seq_num)
{
    RJP1_Channel *ch = &p->channels[channel_num];
    p->vars.active_channels_mask |= (uint8_t)(1u << channel_num);
    if (seq_num != 0) {
        const uint8_t *a1 = (const uint8_t *)((uintptr_t)be32(p->vars.song_data[3] + seq_num * 4)
                                              + (uintptr_t)p->vars.song_data[5]);
        uint8_t d1 = *a1++;
        ch->sequence_offsets = a1;
        ch->sequence_data    = (const uint8_t *)((uintptr_t)be32(p->vars.song_data[4] + d1 * 4)
                                                 + (uintptr_t)p->vars.song_data[6]);
        ch->loop_seq_count = 6;
        ch->loop_seq_cur   = 1;
        ch->loop_seq2_cur  = 1;
        ch->active = 1;
    } else {
        ch->active = 0;
        turn_off_channel(ch);
    }
}

static void turn_off_channel(RJP1_Channel *ch)
{
    ch->hw_channel.active = 0;
}

static void play_channel(RJP1_Player *p, RJP1_Channel *ch)
{
    if (ch->active) {
        turn_on_channel(ch);
        play_song_sequence(p, ch);
        modulate_volume(ch);
        modulate_period(ch);
    }
}

static void turn_on_channel(RJP1_Channel *ch)
{
    if (ch->setup_new_note) {
        ch->setup_new_note = 0;
        ch->hw_channel.active     = 1;
        /* Start playback at the note's stored initial offset (in bytes),
         * not at byte 0 — see setup_note for why. The mixer's `pos` is
         * a 12-bit fixed-point byte index. */
        ch->hw_channel.pos        = ch->init_pos_bytes << RJP1_FRAC_BITS;
        ch->hw_channel.repeat_pos = (uint32_t)ch->repeat_pos * 2;
        ch->hw_channel.repeat_len = (uint32_t)ch->repeat_len * 2;
    }
}

static void play_song_sequence(RJP1_Player *p, RJP1_Channel *ch)
{
    const uint8_t *a1 = ch->sequence_data;
    if (!a1) return;
    --ch->loop_seq_cur;
    if (ch->loop_seq_cur == 0) {
        --ch->loop_seq2_cur;
        if (ch->loop_seq2_cur == 0) {
            int loop = 1;
            do {
                uint8_t d0 = *a1++;
                if (d0 & 0x80) {
                    switch (d0 & 7) {
                    case 0: {
                        const uint8_t *a2 = ch->sequence_offsets;
                        /* The SFX wave-id trigger path (RJP1_StartSequenceByWaveId)
                         * doesn't visit a note record, so it has no list of
                         * subsequent wave-ids to chain to — sequence_offsets
                         * is NULL. End the channel cleanly when an opcode
                         * tries to chain. */
                        if (!a2) {
                            a1 = NULL;
                            ch->active = 0;
                            p->vars.active_channels_mask &= (uint8_t)~(1u << p->vars.current_channel);
                            loop = 0;
                            break;
                        }
                        ch->loop_seq2_count = 1;
                        for (;;) {
                            d0 = *a2++;
                            if (d0 != 0) {
                                ch->sequence_offsets = a2;
                                a1 = (const uint8_t *)((uintptr_t)be32(p->vars.song_data[4] + d0 * 4)
                                                        + (uintptr_t)p->vars.song_data[6]);
                                break;
                            } else {
                                d0 = a2[0];
                                if (d0 == 0) {
                                    a1 = NULL;
                                    ch->active = 0;
                                    p->vars.active_channels_mask &= (uint8_t)~(1u << p->vars.current_channel);
                                    loop = 0;
                                    break;
                                } else if (d0 & 0x80) {
                                    d0 = a2[1];
                                    a2 = (const uint8_t *)((uintptr_t)be32(p->vars.song_data[3] + d0 * 4)
                                                            + (uintptr_t)p->vars.song_data[5]);
                                } else {
                                    a2 -= d0;
                                }
                            }
                        }
                        break;
                    }
                    case 1:
                        set_release(ch);
                        loop = 0;
                        break;
                    case 2:
                        ch->loop_seq_count = *a1++;
                        break;
                    case 3:
                        ch->loop_seq2_count = *a1++;
                        break;
                    case 4: {
                        uint8_t d = *a1++;
                        if (d != 0) setup_instrument(p, ch, d);
                        break;
                    }
                    case 5:
                        ch->volume_scale = *a1++;
                        break;
                    case 6:
                        ch->freq_step = *a1++;
                        ch->freq_inc  = be32(a1); a1 += 4;
                        ch->freq_init = 0;
                        break;
                    case 7:
                        loop = 0;
                        break;
                    }
                } else {
                    d0 >>= 1;
                    if (d0 < PERIODS_COUNT)
                        setup_note(p, ch, kPeriodsTable[d0]);
                    loop = 0;
                }
            } while (loop);
            ch->sequence_data  = a1;
            ch->loop_seq2_cur  = ch->loop_seq2_count;
        }
        ch->loop_seq_cur = ch->loop_seq_count;
    }
}

static void modulate_volume(RJP1_Channel *ch)
{
    modulate_volume_envelope(ch);
    modulate_volume_waveform(ch);
    set_volume(ch);
}

static void modulate_period(RJP1_Channel *ch)
{
    if (ch->modulate_period_data) {
        uint32_t d0 = ch->modulate_period_index;
        int period = ((int8_t)ch->modulate_period_data[d0] * (int)ch->modulate_period_init) / 128;
        period = -period;
        if (period < 0) period /= 2;
        ch->modulate_period_next = (int16_t)(period + ch->modulate_period_init);
        ++d0;
        if (d0 == ch->modulate_period_limit)
            d0 = (uint32_t)ch->modulate_period_base * 2;
        ch->modulate_period_index = d0;
    }
    if (ch->freq_step != 0) {
        ch->freq_init += ch->freq_inc;
        --ch->freq_step;
    }
    ch->hw_channel.per = (int)ch->freq_init + (int)ch->modulate_period_next;
}

static void setup_note(RJP1_Player *p, RJP1_Channel *ch, uint16_t freq)
{
    const uint8_t *a2 = ch->note_data;
    if (!a2) return;
    ch->modulate_period_init = (int16_t)freq;
    ch->modulate_period_next = (int16_t)freq;
    ch->freq_init = 0;
    const uint8_t *a3 = p->vars.song_data[1] + be16(a2 + 12);
    ch->envelope_data   = a3;
    ch->envelope_start  = a3[1];
    ch->envelope_scale  = (int8_t)(a3[1] - a3[0]);
    ch->envelope_end2   = a3[2];
    ch->envelope_end1   = a3[2];
    ch->envelope_mode   = 4;
    ch->hw_channel.active = 0;

    /* Per the RJP1 format spec:
     *   +0x10 (uint16)  "Initial offset / 2 (relative to value at +0)"
     *   +0x12 (uint16)  "Initial length / 2"
     * Both are stored as word offsets, so both need *2 to get byte
     * quantities. `data` stays at `wave_data` (unshifted) so the loop
     * wrap — which uses `repeat_pos * 2` as a byte offset into waveData
     * — still lands correctly. */
    uint16_t init_off_words = be16(a2 + 0x10);
    uint16_t init_len_words = be16(a2 + 0x12);
    ch->hw_channel.data = (const int8_t *)ch->wave_data;
    ch->hw_channel.len  = (uint32_t)(init_off_words + init_len_words) * 2;
    ch->init_pos_bytes  = (uint32_t)init_off_words * 2;
    ch->setup_new_note  = 1;
}

static void setup_instrument(RJP1_Player *p, RJP1_Channel *ch, uint8_t d0)
{
    if (ch->current_instrument == d0) return;
    ch->current_instrument = d0;
    const uint8_t *a2 = p->vars.song_data[0] + d0 * 32;
    ch->note_data = a2;
    ch->repeat_pos             = be16(a2 + 20);
    ch->repeat_len             = be16(a2 + 22);
    ch->volume_scale           = be16(a2 + 14);
    ch->modulate_period_base   = be16(a2 + 24);
    ch->modulate_period_index  = 0;
    ch->modulate_period_limit  = (uint32_t)be16(a2 + 26) * 2;
    ch->modulate_volume_base   = be16(a2 + 28);
    ch->modulate_volume_index  = 0;
    ch->modulate_volume_limit  = (uint32_t)be16(a2 + 30) * 2;
    ch->wave_data              = p->vars.inst_data + be32(a2);
    uint32_t off;
    off = be32(a2 + 4);
    ch->modulate_period_data = off ? (p->vars.inst_data + off) : NULL;
    off = be32(a2 + 8);
    ch->modulate_volume_data = off ? (p->vars.inst_data + off) : NULL;
}

static void set_release(RJP1_Channel *ch)
{
    const uint8_t *a2 = ch->envelope_data;
    if (!a2) return;
    ch->envelope_start = 0;
    ch->envelope_scale = (int8_t)(-(int)ch->envelope_volume);
    ch->envelope_end2  = a2[5];
    ch->envelope_end1  = a2[5];
    ch->envelope_mode  = 0xFF;
}

static void modulate_volume_envelope(RJP1_Channel *ch)
{
    if (ch->envelope_mode) {
        int16_t d0 = (int16_t)ch->envelope_scale;
        if (d0) {
            /* envelope_end1/end2 are uint8 counters, commonly 254
             * (~5 s at 50 Hz) for long decay slides. Treat them
             * unsigned — an int8 cast would flip values 128..254
             * negative, breaking the divide and producing wild
             * volume oscillation between 0 and 64. */
            uint8_t d1 = ch->envelope_end1;
            if (d1 == 0) {
                d0 = 0;
            } else {
                d0 = (int16_t)(d0 * d1);
                d1 = ch->envelope_end2;
                if (d1 == 0) d0 = 0;
                else         d0 = (int16_t)(d0 / d1);
            }
        }
        ch->envelope_volume = (uint8_t)((int)ch->envelope_start - (int)d0);
        --ch->envelope_end1;
        if (ch->envelope_end1 == 0xFF) {
            switch (ch->envelope_mode) {
            case 0: break;
            case 2: set_sustain(ch); break;
            case 4: set_decay(ch); break;
            case 0xFF: set_sustain(ch); break;
            default: break;
            }
            return;
        }
    }
    ch->volume = (int16_t)ch->envelope_volume;
}

static void set_sustain(RJP1_Channel *ch) { ch->envelope_mode = 0; }

static void set_decay(RJP1_Channel *ch)
{
    const uint8_t *a1 = ch->envelope_data;
    if (!a1) return;
    ch->envelope_start = a1[3];
    ch->envelope_scale = (int8_t)(a1[3] - a1[1]);
    ch->envelope_end2  = a1[4];
    ch->envelope_end1  = a1[4];
    ch->envelope_mode  = 2;
}

static void modulate_volume_waveform(RJP1_Channel *ch)
{
    if (ch->modulate_volume_data) {
        uint32_t d0 = ch->modulate_volume_index;
        int d1 = ((int8_t)ch->modulate_volume_data[d0] * (int)ch->volume) / 128;
        ch->volume = (int16_t)((int)ch->volume + d1);
        ++d0;
        if (d0 == ch->modulate_volume_limit)
            d0 = (uint32_t)ch->modulate_volume_base * 2;
        ch->modulate_volume_index = d0;
    }
}

static void set_volume(RJP1_Channel *ch)
{
    int v = ((int)ch->volume * (int)ch->volume_scale) / 64;
    if (v > 64) v = 64;
    if (v < 0)  v = 0;
    ch->volume = (int16_t)v;
    ch->hw_channel.vol = v;
}

void RJP1_HandleTick(RJP1_Player *p)
{
    if (!p->loaded) return;
    for (int i = 0; i < RJP1_NUM_CHANNELS; ++i) {
        p->vars.current_channel = (uint8_t)i;
        play_channel(p, &p->channels[i]);
    }
}

int RJP1_IsFinished(const RJP1_Player *p)
{
    if (!p->loaded) return 0;
    if (p->vars.active_channels_mask != 0) return 0;   /* a sequence is still running */
    for (int i = 0; i < RJP1_NUM_CHANNELS; ++i)         /* or a sample is still ringing out */
        if (p->channels[i].hw_channel.active && p->channels[i].hw_channel.len > 2)
            return 0;
    return 1;
}

void RJP1_SetChannelAttenuation(RJP1_Player *p, int channel, int vol_0_64)
{
    if (channel < 0 || channel >= RJP1_NUM_CHANNELS) return;
    if (vol_0_64 < 0)  vol_0_64 = 0;
    if (vol_0_64 > 64) vol_0_64 = 64;
    p->channel_attenuation[channel] = vol_0_64;
}

void RJP1_PresetEntry(RJP1_Player *p, int presetId, uint8_t out_note_ids[4])
{
    out_note_ids[0] = out_note_ids[1] = out_note_ids[2] = out_note_ids[3] = 0;
    if (!p->loaded) return;
    if (presetId < 0) return;
    /* Preset table at song_data[2] — 4 bytes per preset, mirroring
     * what Amiga AudioApplyPresetToChannels reads. song_data[2] is
     * sized as `subsongs_count * 4`; bounds-check against that. */
    if (presetId >= p->subsongs_count) return;
    const uint8_t *src = p->vars.song_data[2] + (uint32_t)presetId * 4;
    out_note_ids[0] = src[0];
    out_note_ids[1] = src[1];
    out_note_ids[2] = src[2];
    out_note_ids[3] = src[3];
}

/* Forward decl — turn_off_channel is defined later in this file. */
static void turn_off_channel(RJP1_Channel *ch);

/* Common channel setup once `sequence_data` has been pointed at the
 * resolved script blob. Mirrors the tail of `start_sequence`. */
static void start_sequence_with_data(RJP1_Player *p, RJP1_Channel *ch,
                                     int channel, const uint8_t *seq_data,
                                     const uint8_t *seq_offsets)
{
    ch->sequence_data    = seq_data;
    ch->sequence_offsets = seq_offsets;
    ch->loop_seq_count   = 6;
    ch->loop_seq_cur     = 1;
    ch->loop_seq2_cur    = 1;
    ch->active           = 1;
    p->vars.active_channels_mask |= (uint8_t)(1u << channel);
}

void RJP1_StartSequenceByNoteId(RJP1_Player *p, int channel, int noteId)
{
    if (!p->loaded) return;
    if (channel < 0 || channel >= RJP1_NUM_CHANNELS) return;
    RJP1_Channel *ch = &p->channels[channel];

    if (noteId == 0) {
        ch->active = 0;
        turn_off_channel(ch);
        return;
    }

    /* Same shape as the existing start_sequence (used by music): one
     * indirection through song_data[3]+[5] to reach the note record,
     * its first byte selects a wave/sequence whose data lives in
     * song_data[4]+[6]. The post-increment puts sequence_offsets at
     * the byte AFTER the wave-id selector so play_song_sequence's
     * "next sequence" walk reads downstream bytes for chained plays. */
    const uint8_t *a1 = (const uint8_t *)((uintptr_t)be32(p->vars.song_data[3] + (uint32_t)noteId * 4)
                                          + (uintptr_t)p->vars.song_data[5]);
    uint8_t d1 = *a1++;
    const uint8_t *seq_data = (const uint8_t *)((uintptr_t)be32(p->vars.song_data[4] + (uint32_t)d1 * 4)
                                                + (uintptr_t)p->vars.song_data[6]);
    start_sequence_with_data(p, ch, channel, seq_data, a1);
}

void RJP1_StartSequenceByWaveId(RJP1_Player *p, int channel, int waveId)
{
    if (!p->loaded) return;
    if (channel < 0 || channel >= RJP1_NUM_CHANNELS) return;
    RJP1_Channel *ch = &p->channels[channel];

    if (waveId == 0) {
        ch->active = 0;
        turn_off_channel(ch);
        return;
    }

    /* Direct path — `AudioChannelQueueNextWaveIfEnabled` skips the
     * note-record indirection and goes straight from waveId to a
     * wave-script pointer. There's no sequence_offsets in this path
     * because no note record was visited; play_song_sequence's
     * case-0 opcode handler short-circuits when sequence_offsets
     * is NULL and ends the channel. */
    const uint8_t *seq_data = (const uint8_t *)((uintptr_t)be32(p->vars.song_data[4] + (uint32_t)waveId * 4)
                                                + (uintptr_t)p->vars.song_data[6]);
    start_sequence_with_data(p, ch, channel, seq_data, NULL);
}

void RJP1_MixSamples(RJP1_Player *p, int sample_rate, int8_t *buf, int len)
{
    if (!p->loaded) return;
    /* Amiga channel → stereo lane mapping (cyxx player): 1,2 = left ; 0,3 = right */
    static const int amiga_channel_map[] = { 1, 0, 0, 1 };

    /* Per-voice low-pass filter coefficient. Recomputed lazily when the
     * sample rate changes. 6000 Hz cutoff matches A1200 analog chain (and
     * the cyxx reference player's default). Bilinear-corrected:
     *   omega = 2*pi*cutoff/rate
     *   omega = 2 * tan(omega/2)
     *   a     = 1 / (1 + 1/omega) */
    static float s_filter_a    = 0.0f;
    static int   s_filter_rate = 0;
    if (sample_rate != s_filter_rate) {
        s_filter_rate = sample_rate;
        const double cutoff = 6000.0;
        double omega = 2.0 * 3.14159265358979 * cutoff / (double)sample_rate;
        omega = 2.0 * tan(omega * 0.5);
        s_filter_a = (float)(1.0 / (1.0 + 1.0 / omega));
    }
    const float a    = s_filter_a;
    const float oneA = 1.0f - s_filter_a;

    for (int i = 0; i < RJP1_NUM_CHANNELS; ++i) {
        RJP1_PaulaChannel *hw = &p->channels[i].hw_channel;
        if (!hw->active || hw->len <= 2 || !hw->data || hw->per == 0) continue;
        /* Skip channels muted by the host (attenuation 0). Saves the
         * per-sample work and stops a looping wave's filter state from
         * accumulating. */
        const int chAtten = p->channel_attenuation[i];
        if (chAtten == 0) continue;
        const int chGain = p->channel_gain[i] > 0 && p->channel_gain[i] <= 256 ? p->channel_gain[i] : 256;
        const uint32_t pos_inc = (uint32_t)((((uint64_t)RJP1_PAULA_FREQ / (uint32_t)hw->per) << RJP1_FRAC_BITS) / (uint32_t)sample_rate);
        for (int q = 0; q < len; q += 2) {
            int b = buf[q + amiga_channel_map[i]];
            uint32_t pos = hw->pos >> RJP1_FRAC_BITS;
            if (pos >= hw->len) {
                if (hw->repeat_len <= 2) {
                    hw->len = 2;
                    break;
                }
                /* Preserve overshoot (integer + fractional) when wrapping.
                 * Naive `pos = repeat_pos` discards up to one sample of
                 * phase per loop iteration, which accumulates into
                 * audible wobble on sustained synth loops. */
                uint32_t len_frac       = (uint32_t)hw->len << RJP1_FRAC_BITS;
                uint32_t overshoot_frac = hw->pos - len_frac;
                hw->pos = ((uint32_t)hw->repeat_pos << RJP1_FRAC_BITS) + overshoot_frac;
                hw->len = hw->repeat_pos + hw->repeat_len;
                pos     = hw->pos >> RJP1_FRAC_BITS;
            }

            /* Linear interpolation between current and next sample.
             * Without this, sustained voiced samples (HERO vocal track
             * lives on a channel that uses one of these) become a
             * stair-step waveform whose harmonic content gets aliased
             * away when the host sample-rate-converter low-passes,
             * leaving an inaudible / heavily-attenuated channel. */
            uint32_t pos_next = pos + 1;
            if (pos_next >= hw->len) {
                pos_next = (hw->repeat_len > 2) ? hw->repeat_pos : pos;
            }
            int s0   = hw->data[pos];
            int s1   = hw->data[pos_next];
            uint32_t frac = hw->pos & ((1u << RJP1_FRAC_BITS) - 1u);
            int sample_lerp = s0 + ((s1 - s0) * (int)frac >> RJP1_FRAC_BITS);
            /* Two-stage volume: wave-script's hw->vol scales the raw
             * sample, then per-channel attenuation gates the channel.
             * The chant fade-in/out path drives chAtten; the
             * wave-script's set_volume drives hw->vol. */
            int raw  = sample_lerp * hw->vol * chAtten / (64 * 64);
            raw = raw * chGain / 256;       /* (the player's volume for its kind) */

            /* 3-stage cascaded one-pole low-pass — matches the analog
             * chain on real Amigas more closely. */
            float fy = (float)raw;
            hw->filter_state[0] = a * fy                  + oneA * hw->filter_state[0];
            hw->filter_state[1] = a * hw->filter_state[0] + oneA * hw->filter_state[1];
            hw->filter_state[2] = a * hw->filter_state[1] + oneA * hw->filter_state[2];
            fy = hw->filter_state[2];

            b += (int)fy;
            if (b < -128) b = -128;
            else if (b > 127) b = 127;
            buf[q + amiga_channel_map[i]] = (int8_t)b;
            hw->pos += pos_inc;
        }
    }
}
