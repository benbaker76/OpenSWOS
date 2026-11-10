/*
 * dos_music.c -- the PC game's menu music. See include/dos_music.h and docs/DOS_SOUND.md.
 *
 * The PC game hands menu.xmi to the Miles Sound System (AIL), whose AdLib driver plays
 * it on the sound card's FM chip with the instruments of sample.ad. This is that, in
 * four small parts, none of them taken from a copyleft player:
 *
 *   - an XMI reader. XMI is MIDI events in IFF chunks with two differences: a delay is
 *     the sum of the bytes below 0x80 that precede an event, and a note-on carries its
 *     own length, so there are no note-offs;
 *   - a reader for sample.ad, a Miles "global timbre library": an index of
 *     {patch, bank, offset} ended by FF FF, each offset a 14-byte record -- length,
 *     a transposition, and the eleven register values of a two-operator voice. Bank 0
 *     is the melodic instruments by program number, bank 127 the drums by key;
 *   - a driver: nine two-operator voices, given to notes as they come;
 *   - the chip, third_party/opal, and the Miles driver's note-to-frequency and
 *     velocity-to-level arithmetic, third_party/opl_models (model_ail.c).
 *
 * The song asks little: four melodic channels and the drum channel, channel volume set
 * five times, no pitch bend, no pan, no sustain. What it does not use is not here.
 */
#ifdef SWOS_SDL_BUILD

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dos_music.h"
#include "swos_io.h"
#include "endian_helpers.h"
#include "../third_party/opal/opal.h"
#include "../third_party/opl_models/opl_models.h"

/* The song's ticks in ten seconds. XMI's own unit is a 120th of a second, but that is
 * not how fast the PC game plays this file: it sets the Miles sequencer's service rate
 * to its frame timer (MDI_SERVICE_RATE = 1193181 / 17045 = 70, InitAIL22khz in
 * swos.asm) and the sequencer steps one tick a service. The song agrees -- its tempo
 * event says 88 beats a minute and a beat is 47.75 ticks, which is 70.0 a second. A
 * recording of the PC menu music (SWOS 2020's menu.mp3) runs a little faster still,
 * 93.5 beats a minute at the same pitch, which is 74.4 ticks a second; it is what a PC
 * was heard to do, so it is what is matched here (docs/DOS_SOUND.md, "The music"). */
#define TICKS_PER_10_SECONDS 744
/* SWOS_MUSIC_TICKS=<ticks in ten seconds>: another rate, to compare by ear (700 is the
 * game's own arithmetic). */
static uint32_t s_ticksPer10s = TICKS_PER_10_SECONDS;
#define VOICES              18      /* an OPL3: the Miles driver for a Sound Blaster Pro 2 or 16 */
#define DRUM_CHANNEL        9
#define DRUM_BANK           127
#define TIMBRE_BYTES        14

/* The modulator's operator slot of each voice of a register set; the carrier is three
 * on. Voices 9 to 17 are the chip's second set, at register + 0x100. */
static const uint8_t k_voiceOp[9] = { 0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12 };
#define OP_REG(v, reg)  ((uint16_t)(((v) >= 9 ? 0x100 : 0) + (reg) + k_voiceOp[(v) % 9]))
#define CH_REG(v, reg)  ((uint16_t)(((v) >= 9 ? 0x100 : 0) + (reg) + (v) % 9))

typedef struct {
    int32_t        on;          /* key down */
    int32_t        channel, key;
    int32_t        velocity;
    uint32_t       offTick;     /* the tick the note ends */
    uint32_t       stamp;       /* when it was started or released: the oldest is taken first */
    uint16_t       freq;        /* F-number and block, as written */
    const uint8_t *timbre;
} Voice;

static int32_t  s_style;
static int32_t  s_loaded;       /* the bank: 0 not tried, 1 ready, -1 missing or not understood */
static uint8_t *s_bank;
static uint32_t s_bankLen;
/* The songs: menu.xmi (the menus) and title.xmi (the intro, [audio] title_music = midi, from a
 * mod: SWOS 2020's floppy title song), each read once. */
static struct { const char *stem; int32_t loaded; uint8_t *xmi; const uint8_t *events; uint32_t len; } s_song[2] = {
    { "menu" }, { "title" },
};
static int32_t  s_loop = 1;     /* the song playing repeats; else it stops at its end */
static const uint8_t *s_events;
static uint32_t s_eventsLen;

static Opal     s_chip;
static int32_t  s_chipRate;
static int32_t  s_playing;
static Voice    s_voice[VOICES];
static uint8_t  s_program[16], s_volume[16];
static uint32_t s_pos, s_tick, s_nextTick, s_stamp, s_acc;

/* What the self-test reports. */
static struct { uint32_t notes, steals, noTimbre, peak, loops; } s_stat;

/* ---- files ---- */

static uint8_t *read_file(const char *rel, uint32_t *len)
{
    char anchored[1024];
    FILE *f = fopen(swos_data_path(rel, anchored, (uint32_t)sizeof anchored), "rb");
    uint8_t *data = NULL;
    int32_t size;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    size = (int32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size > 0 && size < 1024 * 1024) {
        data = (uint8_t *)malloc((size_t)size);
        if (data && fread(data, 1, (size_t)size, f) != (size_t)size) {
            free(data);
            data = NULL;
        }
    }
    fclose(f);
    if (data)
        *len = (uint32_t)size;
    return data;
}

/* The EVNT chunk of the first song: FORM XDIR, then CAT XMID { FORM XMID { TIMB, EVNT } }.
 * Chunk sizes are big-endian and chunks are padded to an even length. */
static int32_t find_events(const uint8_t *d, uint32_t len)
{
    /* (It leaves the song in s_events/s_eventsLen; DosMusic_LoadSong keeps it and puts back
     * the one that was there, which may be playing.) */
    uint32_t p = 0;
    while (p + 8 <= len) {
        uint32_t size = BE_READ_U32(d + p + 4);
        if (memcmp(d + p, "FORM", 4) == 0 || memcmp(d + p, "CAT ", 4) == 0) {
            if (p + 12 <= len && memcmp(d + p + 8, "XDIR", 4) == 0)
                p += 8 + size + (size & 1);     /* the directory: step over it */
            else
                p += 12;                        /* a container: go inside */
            continue;
        }
        if (memcmp(d + p, "EVNT", 4) == 0) {
            if (p + 8 + size > len)
                return 0;
            s_events = d + p + 8;
            s_eventsLen = size;
            return 1;
        }
        p += 8 + size + (size & 1);
    }
    return 0;
}

static const uint8_t *find_timbre(int32_t bank, int32_t patch)
{
    uint32_t p;
    for (p = 0; p + 6 <= s_bankLen; p += 6) {
        uint32_t at;
        if (s_bank[p] == 0xFF && s_bank[p + 1] == 0xFF)
            break;
        if (s_bank[p] != patch || s_bank[p + 1] != bank)
            continue;
        at = (uint32_t)s_bank[p + 2] | (uint32_t)s_bank[p + 3] << 8 |
             (uint32_t)s_bank[p + 4] << 16 | (uint32_t)s_bank[p + 5] << 24;     /* little-endian: a PC file */
        if (at + TIMBRE_BYTES > s_bankLen || s_bank[at] != TIMBRE_BYTES || s_bank[at + 1] != 0)
            return NULL;                        /* not a two-operator timbre */
        return s_bank + at;
    }
    return NULL;
}

int32_t DosMusic_Load(void)
{
    uint32_t xmiLen = 0;

    if (s_loaded)
        return s_loaded > 0;
    s_loaded = -1;
    s_bank = read_file("sound_dos/sample.ad", &s_bankLen);
    if (!s_bank) {
        fprintf(stderr, "[music] sound_dos/sample.ad is missing: the Amiga's music is used\n");
        return 0;
    }
    if (!find_timbre(0, 0)) {
        fprintf(stderr, "[music] sound_dos/sample.ad is not what it should be: the Amiga's music is used\n");
        return 0;
    }
    s_loaded = 1;
    if (!DosMusic_LoadSong("menu")) {
        s_loaded = -1;
        return 0;
    }
    s_events = s_song[0].events;        /* the menu song until another is started */
    s_eventsLen = s_song[0].len;
    return 1;
}

static int32_t song_index(const char *stem)
{
    int32_t i;
    for (i = 0; i < (int32_t)(sizeof s_song / sizeof s_song[0]); i++)
        if (strcmp(s_song[i].stem, stem) == 0)
            return i;
    return -1;
}

int32_t DosMusic_LoadSong(const char *stem)
{
    const int32_t i = song_index(stem);
    char rel[64];
    uint32_t len = 0;

    if (i < 0 || s_loaded < 0 || (!s_loaded && !s_bank && !DosMusic_Load()))
        return 0;
    if (s_song[i].loaded)
        return s_song[i].loaded > 0;
    s_song[i].loaded = -1;
    snprintf(rel, sizeof rel, "sound_dos/%s.xmi", stem);
    const uint8_t *was = s_events;
    const uint32_t wasLen = s_eventsLen;
    s_song[i].xmi = read_file(rel, &len);
    if (!s_song[i].xmi) {
        fprintf(stderr, "[music] %s is missing\n", rel);
        return 0;
    }
    if (!find_events(s_song[i].xmi, len)) {
        s_events = was;
        s_eventsLen = wasLen;
        fprintf(stderr, "[music] %s is not what it should be\n", rel);
        return 0;
    }
    s_song[i].events = s_events;
    s_song[i].len = s_eventsLen;
    s_events = was;
    s_eventsLen = wasLen;
    s_song[i].loaded = 1;
    return 1;
}

/* ---- the chip ---- */

static void write_levels(int32_t v)
{
    const Voice *vc = &s_voice[v];
    const uint8_t *t = vc->timbre;
    struct OPLVolume_t vol;

    memset(&vol, 0, sizeof vol);
    vol.vel = (uint_fast8_t)vc->velocity;
    vol.chVol = s_volume[vc->channel];
    vol.chExpr = 127;
    vol.masterVolume = 127;
    vol.voiceMode = (t[8] & 1) ? OPLVoice_MODE_2op_AM : OPLVoice_MODE_2op_FM;
    vol.tlMod = t[4] & 0x3F;
    vol.tlCar = t[10] & 0x3F;
    vol.doMod = (t[8] & 1) != 0;        /* both operators are heard: both are scaled */
    vol.doCar = 1;
    oplModel_ailVolume(&vol);
    Opal_Port(&s_chip, OP_REG(v, 0x40), (uint8_t)((t[4] & 0xC0) | vol.tlMod));
    Opal_Port(&s_chip, OP_REG(v, 0x43), (uint8_t)((t[10] & 0xC0) | vol.tlCar));
}

static void key_off(int32_t v)
{
    if (!s_voice[v].on)
        return;
    Opal_Port(&s_chip, CH_REG(v, 0xB0), (uint8_t)((s_voice[v].freq >> 8) & 0x1F));
    s_voice[v].on = 0;
    s_voice[v].stamp = ++s_stamp;
}

static void note_on(int32_t channel, int32_t key, int32_t velocity, uint32_t length)
{
    const uint8_t *t;
    int32_t v, best = -1, note, playing = 0;
    uint32_t mul = 0;

    /* Sixteen bytes of sample.ad's record: [2] transposes a melodic instrument, and for
     * a drum is the one note it is played at. */
    if (channel == DRUM_CHANNEL) {
        t = find_timbre(DRUM_BANK, key);
        note = t ? t[2] : 0;
    } else {
        t = find_timbre(0, s_program[channel]);
        note = t ? key + (int8_t)t[2] : 0;
    }
    s_stat.notes++;
    if (!t) {
        s_stat.noTimbre++;
        return;
    }

    /* A voice that is silent, the longest silent first; with none, the oldest note is cut. */
    for (v = 0; v < VOICES; v++) {
        playing += s_voice[v].on;
        if (!s_voice[v].on && (best < 0 || s_voice[v].stamp < s_voice[best].stamp))
            best = v;
    }
    if (best < 0) {
        for (v = 0; v < VOICES; v++)
            if (best < 0 || s_voice[v].stamp < s_voice[best].stamp)
                best = v;
        key_off(best);
        s_stat.steals++;
    } else if ((uint32_t)playing + 1 > s_stat.peak) {
        s_stat.peak = (uint32_t)playing + 1;
    }
    v = best;

    s_voice[v].channel = channel;
    s_voice[v].key = key;
    s_voice[v].velocity = velocity;
    s_voice[v].timbre = t;
    s_voice[v].offTick = s_tick + (length ? length : 1);
    s_voice[v].stamp = ++s_stamp;
    s_voice[v].freq = oplModel_ailFreq((double)note, &mul);

    Opal_Port(&s_chip, CH_REG(v, 0xB0), 0);                    /* key up before the new sound */
    Opal_Port(&s_chip, OP_REG(v, 0x20), t[3]);
    Opal_Port(&s_chip, OP_REG(v, 0x60), t[5]);
    Opal_Port(&s_chip, OP_REG(v, 0x80), t[6]);
    Opal_Port(&s_chip, OP_REG(v, 0xE0), t[7]);
    Opal_Port(&s_chip, CH_REG(v, 0xC0), (uint8_t)(t[8] | 0x30));   /* both speakers */
    Opal_Port(&s_chip, OP_REG(v, 0x23), t[9]);
    Opal_Port(&s_chip, OP_REG(v, 0x63), t[11]);
    Opal_Port(&s_chip, OP_REG(v, 0x83), t[12]);
    Opal_Port(&s_chip, OP_REG(v, 0xE3), t[13]);
    s_voice[v].on = 1;
    write_levels(v);
    Opal_Port(&s_chip, CH_REG(v, 0xA0), (uint8_t)(s_voice[v].freq & 0xFF));
    Opal_Port(&s_chip, CH_REG(v, 0xB0), (uint8_t)(0x20 | ((s_voice[v].freq >> 8) & 0x1F)));
}

static void restart(void)
{
    int32_t i;

    Opal_Init(&s_chip, s_chipRate);
    Opal_Port(&s_chip, 0x01, 0x20);         /* the waveforms a timbre selects are allowed */
    Opal_Port(&s_chip, 0x08, 0x00);
    Opal_Port(&s_chip, 0xBD, 0x00);         /* melodic mode: the drums are ordinary voices */
    memset(s_voice, 0, sizeof s_voice);
    for (i = 0; i < 16; i++) {
        s_program[i] = 0;
        s_volume[i] = 127;
    }
    s_pos = 0;
    s_tick = s_nextTick = 0;
    s_stamp = 0;
}

/* ---- the sequencer ---- */

static uint32_t read_length(void)
{
    uint32_t n = 0;
    while (s_pos < s_eventsLen) {
        uint8_t b = s_events[s_pos++];
        n = n << 7 | (b & 0x7F);
        if (!(b & 0x80))
            break;
    }
    return n;
}

/* One tick, a 120th of a second. */
static void tick(void)
{
    int32_t v, guard = 0;

    for (v = 0; v < VOICES; v++)
        if (s_voice[v].on && s_tick >= s_voice[v].offTick)
            key_off(v);

    while (s_tick >= s_nextTick) {
        uint8_t status;

        if (s_pos >= s_eventsLen) {             /* the end without an end-of-track: again */
        again:
            if (++guard > 2)
                return;                         /* a song with nothing in it */
            for (v = 0; v < VOICES; v++)
                key_off(v);
            if (!s_loop) {                      /* the title song: once */
                s_playing = 0;
                return;
            }
            s_pos = 0;
            s_nextTick = s_tick;
            s_stat.loops++;
            continue;
        }
        status = s_events[s_pos];
        if (status < 0x80) {                    /* a delay: they add up */
            s_nextTick += status;
            s_pos++;
            continue;
        }
        s_pos++;
        switch (status & 0xF0) {
        case 0x90: {
            int32_t key = s_events[s_pos] & 0x7F, velocity = s_events[s_pos + 1] & 0x7F;
            uint32_t length;
            s_pos += 2;
            length = read_length();
            if (velocity)
                note_on(status & 0x0F, key, velocity, length);
            break;
        }
        case 0xB0:
            if (s_events[s_pos] == 7) {         /* channel volume: the notes sounding follow it */
                s_volume[status & 0x0F] = s_events[s_pos + 1] & 0x7F;
                for (v = 0; v < VOICES; v++)
                    if (s_voice[v].on && s_voice[v].channel == (status & 0x0F))
                        write_levels(v);
            }
            s_pos += 2;
            break;
        case 0xC0:
            s_program[status & 0x0F] = s_events[s_pos] & 0x7F;
            s_pos += 1;
            break;
        case 0xD0:
            s_pos += 1;
            break;
        case 0x80: case 0xA0: case 0xE0:
            s_pos += 2;
            break;
        default:                                /* 0xF0..0xFF */
            if (status == 0xFF) {
                uint8_t type = s_events[s_pos++];
                uint32_t n = read_length();
                if (type == 0x2F)
                    goto again;                 /* end of track */
                s_pos += n;
            } else {
                s_pos += read_length();         /* system exclusive */
            }
            break;
        }
    }
    s_tick++;
}

/* ---- the audio layer's side ---- */

void DosMusic_Start(void)
{
    DosMusic_StartSong("menu", 1);
}

void DosMusic_StartSong(const char *stem, int32_t loop)
{
    const int32_t i = song_index(stem);
    if (!DosMusic_Load() || i < 0 || !DosMusic_LoadSong(stem))
        return;
    s_events = s_song[i].events;
    s_eventsLen = s_song[i].len;
    s_loop = loop != 0;
    if (!s_chipRate)
        s_chipRate = 44100;
    {
        const char *e = getenv("SWOS_MUSIC_TICKS");
        int32_t n = e ? atoi(e) : 0;
        if (n >= 300 && n <= 2400)
            s_ticksPer10s = (uint32_t)n;
    }
    restart();
    s_acc = 0;
    s_playing = 1;
}

void DosMusic_Stop(void)
{
    s_playing = 0;
}

int32_t DosMusic_Playing(void)
{
    return s_playing;
}

void DosMusic_Render(int16_t *out, int32_t frames, int32_t rate, int32_t gain)
{
    int32_t i;

    if (!s_playing || rate <= 0)
        return;
    if (rate != s_chipRate) {
        s_chipRate = rate;
        Opal_SetSampleRate(&s_chip, rate);
    }
    for (i = 0; i < frames; i++) {
        int16_t l, r;
        int32_t a, b;
        for (s_acc += s_ticksPer10s; s_acc >= (uint32_t)rate * 10; s_acc -= (uint32_t)rate * 10)
            tick();
        Opal_Sample(&s_chip, &l, &r);
        a = out[i * 2] + l * gain / 256;
        b = out[i * 2 + 1] + r * gain / 256;
        out[i * 2] = (int16_t)(a < -32768 ? -32768 : a > 32767 ? 32767 : a);
        out[i * 2 + 1] = (int16_t)(b < -32768 ? -32768 : b > 32767 ? 32767 : b);
    }
}

void DosMusic_SetStyle(int32_t pc)
{
    s_style = pc != 0;
}

int32_t DosMusic_Style(void)
{
    return s_style;
}

static int32_t s_menuRecording;

void DosMusic_SetMenuRecording(int32_t on)
{
    s_menuRecording = on != 0;
}

int32_t DosMusic_MenuRecording(void)
{
    return s_menuRecording;
}

static int32_t s_titleMidi;

void DosMusic_SetTitleMidi(int32_t on)
{
    s_titleMidi = on != 0;
}

int32_t DosMusic_TitleMidi(void)
{
    return s_titleMidi;
}

/* ---- self-test ---- */

int32_t DosMusic_SelfTest(void)
{
    static int16_t buf[44100 / 50 * 2];
    int32_t failures = 0, i, n;
    uint32_t ticks = 0, peakOut = 0;
    double sum = 0;
    uint32_t samples = 0;

    if (!DosMusic_Load()) {
        fprintf(stderr, "[dos-music] FAIL: the song or the instruments did not load\n");
        return 1;
    }
    s_chipRate = 44100;
    restart();
    s_acc = 0;
    s_playing = 1;
    memset(&s_stat, 0, sizeof s_stat);
    /* Once through, as it would be played: 50 blocks a second until the song comes round. */
    for (n = 0; n < 50 * 600 && s_stat.loops == 0; n++) {
        memset(buf, 0, sizeof buf);
        DosMusic_Render(buf, 44100 / 50, 44100, 256);
        for (i = 0; i < 44100 / 50 * 2; i++) {
            uint32_t a = (uint32_t)(buf[i] < 0 ? -buf[i] : buf[i]);
            if (a > peakOut)
                peakOut = a;
            sum += (double)buf[i] * buf[i];
            samples++;
        }
    }
    ticks = s_tick;
    s_playing = 0;
    if (s_stat.loops == 0) {
        fprintf(stderr, "[dos-music] FAIL: the song never ended\n");
        failures++;
    }
    {
        /* The A above middle C through the Miles arithmetic: F-number * 49716 / 2^(20 - block). */
        uint32_t mul = 0;
        uint16_t f = oplModel_ailFreq(69.0, &mul);
        double hz = (f & 0x3FF) * 49716.0 / (double)(1u << (20 - ((f >> 10) & 7)));
        if (hz < 436.0 || hz > 444.0) {
            fprintf(stderr, "[dos-music] FAIL: key 69 comes out at %.1f Hz, not 440\n", hz);
            failures++;
        }
    }
    if (s_stat.notes == 0 || peakOut == 0) {
        fprintf(stderr, "[dos-music] FAIL: nothing was played\n");
        failures++;
    }
    {
        double rms = samples ? sum / samples : 0, root = 0;
        /* a square root without the maths library */
        if (rms > 0) {
            root = rms;
            for (i = 0; i < 40; i++)
                root = (root + rms / root) / 2;
        }
        fprintf(stderr, "[dos-music] %u notes in %u ticks (%.1f s), %u of them for drums sample.ad has no "
                "sound for (silent on the PC too); %u voices at most, %u notes cut short for a voice; "
                "output peak %u, rms %.0f: %s\n", s_stat.notes, ticks, ticks * 10.0 / TICKS_PER_10_SECONDS,
                s_stat.noTimbre, s_stat.peak, s_stat.steals, peakOut, root, failures ? "FAILED" : "PASSED");
    }
    /* The intro's song ([audio] title_music = midi), when a mod brings sound_dos/title.xmi: it
     * plays, and once -- it stops at its end instead of coming round. */
    if (DosMusic_LoadSong("title")) {
        uint32_t notes;
        DosMusic_StartSong("title", 0);
        memset(&s_stat, 0, sizeof s_stat);
        for (n = 0; n < 50 * 600 && s_playing; n++) {
            memset(buf, 0, sizeof buf);
            DosMusic_Render(buf, 44100 / 50, 44100, 256);
        }
        notes = s_stat.notes;
        if (s_playing || notes == 0) {
            fprintf(stderr, "[dos-music] FAIL: title.xmi %s\n", s_playing ? "did not stop at its end" : "played nothing");
            failures++;
        }
        s_playing = 0;
        fprintf(stderr, "[dos-music] title.xmi: %u notes in %.1f s, once: %s\n", notes,
                s_tick * 10.0 / TICKS_PER_10_SECONDS, failures ? "FAILED" : "PASSED");
    } else
        fprintf(stderr, "[dos-music] no sound_dos/title.xmi (a mod's): its check skipped\n");
    return failures;
}

#endif /* SWOS_SDL_BUILD */
