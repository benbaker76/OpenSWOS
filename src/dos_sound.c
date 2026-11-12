/*
 * dos_sound.c -- the PC game's sounds. See include/dos_sound.h and docs/DOS_SOUND.md.
 *
 * Ported from the PC game's disassembly (swos-port's swos.asm): PlayEnqueuedSamples,
 * PlayCommentarySample, PlaySampleFromTable, the Play*Comment routines,
 * LoadCrowdChantSample, LoadAndPlayEndGameComment and the Play*Sample effects, with
 * their volumes and their tables (src/dos_sound_tables.h, generated).
 *
 * Three things are not the PC game's:
 *   - the random numbers. The PC game draws its commentary lines from the game's own
 *     generator; here they come from a private one, so that the sound cannot change
 *     a match (and two players need not hear the same line);
 *   - the delays count frames this player was SHOWN, not frames simulated: under a
 *     rollback a frame is simulated more than once;
 *   - the crowd chants keep the Amiga's rhythm. Their state machine calls the game's
 *     random generator in both releases, with different numbers in each, and the
 *     match depends on those calls: a PC chant is started when the Amiga's machine
 *     starts one, and from there has the PC game's own fade and length.
 */
#include "dos_sound.h"

#ifdef SWOS_SDL_BUILD

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#ifndef SWOS_SWITCH_BUILD
#include <dirent.h>     /* the Switch has no folder to put a pack in: custom_scan is empty there */
#endif

#include "swos_io.h"
#include "dos_sound_tables.h"
#define DR_MP3_NO_STDIO                     /* as src/music_stream.c, which compiles it */
#include "../third_party/dr_mp3/dr_mp3.h"
#define STB_VORBIS_HEADER_ONLY              /* (src/stb_vorbis_impl.c compiles it) */
#include "../third_party/stb_vorbis/stb_vorbis.c"
#include "mods.h"                           /* a mod's effect in another format (file_load) */

extern void AudioSDL_PcmPlay(int voice, const uint8_t *data, uint32_t len, int rate, int volume, int loops);
extern void AudioSDL_PcmPlay16(int voice, const int16_t *data, uint32_t len, int rate, int volume, int loops);
extern void AudioSDL_PcmVolume(int voice, int volume);
extern void AudioSDL_PcmStop(int voice);
extern int  AudioSDL_PcmPlaying(int voice);

#define RATE            22050

/* Voices of the mixer (audio_sdl.c has eight). */
#define VOICE_CROWD     0       /* the background crowd, looping */
#define VOICE_CHANT     1
#define VOICE_COMMENT   2
#define VOICE_REACTION  3       /* goal cheer, the sigh, the end-of-game crowd */
#define VOICE_FX_FIRST  4
#define VOICE_FX_COUNT  4

/* soundEffectsTable's order. */
enum { FX_CROWD, FX_BOUNCE, FX_HOMEGOAL, FX_KICK, FX_WHISTLE, FX_MISSGOAL, FX_ENDGAME, FX_FOUL,
       FX_CHANT4L, FX_CHANT10L, FX_CHANT8L };

static int32_t s_style;                 /* 1: the PC's sound */
static int32_t s_commentary = 2;        /* 0 off, 1 on, 2 with the PC's sound only */
static int32_t s_live;                  /* a match is on with something of the PC's to play */
static int32_t s_full;                  /* ... its effects, crowd and chants: the Amiga's are not started */
static int32_t s_training;
static int32_t s_muteCommentary;        /* the PC game's F8 */

/* A file of the table: the PC game's .raw (8-bit, RATE), or a mod's .ogg / .wav / .mp3 decoded to
 * 16-bit mono at its own rate (is16). */
static struct { uint8_t *volatile data; uint32_t len; int32_t rate; int8_t is16, tried; } s_file[DOS_SOUND_FILES];
static SDL_Thread  *s_loader;           /* reads the files while the game starts */
static volatile int s_loadDone, s_loadGot;

/* What the PC game keeps between its sound calls. */
static struct {
    int16_t yellowTimer, redTimer, goodPassTimer, deletedTimer, throwInTimer, cornerTimer,
            substituteTimer, tacticsTimer;      /* -1: none waiting */
    int16_t goodPassCount;
    int32_t performingPenalty;
    int16_t last[16];                           /* the last line of each (offset, size) table */
    int32_t introChant;                         /* a file, or -1: chant4l */
    int32_t resultChant;                        /* a file, or -1 */
    int32_t interesting;                        /* itsAnInterestingGame */
    int32_t fxNext;
    /* The chant being played: the PC game's fade in, hold and fade out, in its ticks. */
    int32_t chantPhase;                         /* 0 none, 1 in, 2 hold, 3 out */
    int32_t chantTimer, chantAcc;
    uint32_t rnd;
} s;

static uint32_t rnd(void)
{
    s.rnd = s.rnd * 1664525u + 1013904223u;
    return s.rnd >> 16;
}

/* ---- your own commentary ----
 *
 * SWOS 2020's arrangement: a folder per category under sound_dos/commentary/ (or
 * [audio] commentary_dir), any number of files in each, of any length -- .raw (the PC
 * game's own format: unsigned 8-bit mono, 22,050 Hz), .wav, .mp3, .ogg or .aiff. A category with
 * files in its folder is spoken from them; one without keeps the PC game's lines.
 * "_x3" or "-x3" at the end of a name makes that file three times as likely.
 * The names are SWOS 2020's, so a pack made for it drops in. */
enum { C_CORNER, C_DIRTY_TACKLE, C_END_GAME_ROUT, C_END_GAME_SENSATIONAL, C_END_GAME_SO_CLOSE,
       C_FREE_KICK, C_GOAL, C_GOOD_PLAY, C_GOOD_TACKLE, C_HEADER, C_HIT_BAR, C_HIT_POST, C_INJURY,
       C_KEEPER_CLAIMED, C_KEEPER_SAVED, C_NEAR_MISS, C_OWN_GOAL, C_PENALTY, C_PENALTY_MISSED,
       C_PENALTY_SAVED, C_PENALTY_SCORED, C_RED_CARD, C_SUBSTITUTION, C_TACTIC_CHANGED, C_THROW_IN,
       C_YELLOW_CARD, CATEGORIES };

static const char *const k_category[CATEGORIES] = {
    "corner", "dirty_tackle", "end_game_rout", "end_game_sensational", "end_game_so_close",
    "free_kick", "goal", "good_play", "good_tackle", "header", "hit_bar", "hit_post", "injury",
    "keeper_claimed", "keeper_saved", "near_miss", "own_goal", "penalty", "penalty_missed",
    "penalty_saved", "penalty_scored", "red_card", "substitution", "tactic_changed", "throw_in",
    "yellow_card",
};

#define CUSTOM_PER_CATEGORY 64
#define CUSTOM_MAX_BYTES    (96u * 1024u * 1024u)   /* of decoded sound, all categories together */

typedef struct CustomLine {
    void    *data;              /* uint8_t (.raw) or int16_t samples, mono */
    uint32_t frames;
    int32_t  rate;
    int32_t  is16;
    int32_t  chance;
} CustomLine;

static struct {
    CustomLine   line[CUSTOM_PER_CATEGORY];
    volatile int count;         /* set last: the game reads it while the loader works */
    int32_t      total;         /* of the chances */
    int32_t      last;
} s_custom[CATEGORIES];
static char     s_customDir[512];
static uint32_t s_customBytes;

/* ---- files ---- */

static int32_t file_load_mod(int32_t id);   /* (below custom_load: a mod's, any format) */

static int32_t file_load(int32_t id)
{
    char rel[96], anchored[1024];
    FILE *f;
    int32_t size;

    if (id < 0 || id >= DOS_SOUND_FILES)
        return 0;
    if (s_file[id].tried)
        return s_file[id].data != NULL;
    s_file[id].tried = 1;
    if (file_load_mod(id))
        return 1;
    snprintf(rel, sizeof rel, "sound_dos/%s", k_dosSoundFiles[id]);
    f = fopen(swos_data_path(rel, anchored, (uint32_t)sizeof anchored), "rb");
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    size = (int32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size > 0 && size < 4 * 1024 * 1024) {
        uint8_t *data = (uint8_t *)malloc((size_t)size);
        if (data && fread(data, 1, (size_t)size, f) == (size_t)size) {
            s_file[id].len = (uint32_t)size;
            s_file[id].rate = RATE;
            s_file[id].is16 = 0;
            s_file[id].data = data;         /* last: the game plays what has a pointer */
        } else {
            free(data);
        }
    }
    fclose(f);
    return s_file[id].data != NULL;
}

static uint8_t *slurp(const char *path, uint32_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *data = NULL;
    int32_t size;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    size = (int32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size > 0 && size < 32 * 1024 * 1024) {
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

static int32_t ends_with(const char *name, const char *ext)
{
    size_t n = strlen(name), e = strlen(ext);
    return n > e && SDL_strcasecmp(name + n - e, ext) == 0;
}

/* SWOS 2020's rule: "..._x3.wav" or "...-x3.wav" is three times as likely as a plain name. */
static int32_t chance_of(const char *name)
{
    const char *dot = strrchr(name, '.'), *p;
    int32_t value = 0, power = 1;

    if (!dot || dot - name < 4)
        return 1;
    for (p = dot - 1; p > name && *p >= '0' && *p <= '9'; p--) {
        value += (*p - '0') * power;
        power *= 10;
    }
    if (p == dot - 1 || p - name < 2 || *p != 'x' || (p[-1] != '_' && p[-1] != '-'))
        return 1;
    return value < 1 ? 1 : value > 100 ? 100 : value;
}

/* An AIFF file (FORM / AIFF, uncompressed PCM, 8 or 16 bits, big-endian; one of SWOS 2020's
 * packs has one): its samples mixed to 16-bit mono into *out. 0 when it is not that. */
static int32_t aiff_decode(const uint8_t *b, uint32_t len, int16_t **out, uint32_t *frames, int32_t *rate)
{
    uint32_t at = 12, nframes = 0, ssnd = 0, ssndLen = 0;
    int32_t channels = 0, bits = 0, hz = 0;
    if (len < 12 || memcmp(b, "FORM", 4) != 0 || memcmp(b + 8, "AIFF", 4) != 0)
        return 0;
    while (at + 8 <= len) {
        const uint32_t size = (uint32_t)b[at + 4] << 24 | (uint32_t)b[at + 5] << 16 | (uint32_t)b[at + 6] << 8 | b[at + 7];
        const uint8_t *c = b + at + 8;
        if (size > len - at - 8)
            break;
        if (memcmp(b + at, "COMM", 4) == 0 && size >= 18) {
            channels = c[0] << 8 | c[1];
            nframes = (uint32_t)c[2] << 24 | (uint32_t)c[3] << 16 | (uint32_t)c[4] << 8 | c[5];
            bits = c[6] << 8 | c[7];
            {   /* the rate: an 80-bit extended float (exponent, then the mantissa's top 32 bits do) */
                const int32_t e = ((c[8] & 0x7F) << 8 | c[9]) - 16383 - 31;
                const uint32_t m = (uint32_t)c[10] << 24 | (uint32_t)c[11] << 16 | (uint32_t)c[12] << 8 | c[13];
                hz = e >= 0 ? (int32_t)(m << e) : (int32_t)(m >> -e);
            }
        } else if (memcmp(b + at, "SSND", 4) == 0 && size >= 8) {
            const uint32_t off = (uint32_t)c[0] << 24 | (uint32_t)c[1] << 16 | (uint32_t)c[2] << 8 | c[3];
            ssnd = at + 16 + off;
            ssndLen = size - 8 - off;
        }
        at += 8 + size + (size & 1);
    }
    const int32_t bytes = bits <= 8 ? 1 : 2;
    if (!ssnd || channels < 1 || (bits != 8 && bits != 16) || hz < 4000 || ssnd > len ||
        (uint64_t)nframes * channels * bytes > ssndLen || ssndLen > len - ssnd)
        return 0;
    int16_t *mono = (int16_t *)malloc((size_t)nframes * 2 + 2);
    if (!mono)
        return 0;
    for (uint32_t i = 0; i < nframes; i++) {
        int32_t sum = 0;
        for (int32_t ch = 0; ch < channels; ch++) {
            const uint8_t *p = b + ssnd + ((size_t)i * channels + ch) * bytes;
            sum += bytes == 1 ? (int8_t)p[0] * 256 : (int16_t)(p[0] << 8 | p[1]);
        }
        mono[i] = (int16_t)(sum / channels);
    }
    *out = mono;
    *frames = nframes;
    *rate = hz;
    return 1;
}

/* One file of a category's folder, decoded to mono. 0 when it is not sound we can play. */
static int32_t custom_load(const char *path, CustomLine *out)
{
    uint32_t len = 0;

    memset(out, 0, sizeof *out);
    if (ends_with(path, ".raw")) {
        out->data = slurp(path, &len);
        out->frames = len;
        out->rate = RATE;
    } else if (ends_with(path, ".wav")) {
        SDL_AudioSpec spec;
        Uint8 *wav = NULL;
        Uint32 wavLen = 0;
        SDL_AudioCVT cvt;
        if (!SDL_LoadWAV(path, &spec, &wav, &wavLen))
            return 0;
        if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, 1, spec.freq) >= 0) {
            cvt.len = (int)wavLen;
            cvt.buf = (Uint8 *)malloc((size_t)wavLen * (size_t)(cvt.len_mult > 0 ? cvt.len_mult : 1));
            if (cvt.buf) {
                memcpy(cvt.buf, wav, wavLen);
                if (!cvt.needed || SDL_ConvertAudio(&cvt) == 0) {
                    out->data = cvt.buf;
                    out->frames = (uint32_t)(cvt.needed ? cvt.len_cvt : cvt.len) / 2;
                    out->rate = spec.freq;
                    out->is16 = 1;
                } else {
                    free(cvt.buf);
                }
            }
        }
        SDL_FreeWAV(wav);
    } else if (ends_with(path, ".mp3")) {
        uint8_t *file = slurp(path, &len);
        drmp3_config cfg;
        drmp3_uint64 frames = 0;
        drmp3_int16 *pcm;
        if (!file)
            return 0;
        pcm = drmp3_open_memory_and_read_pcm_frames_s16(file, len, &cfg, &frames, NULL);
        free(file);
        if (pcm && frames && cfg.channels >= 1 && cfg.channels <= 2) {
            int16_t *mono = (int16_t *)malloc((size_t)frames * 2);
            if (mono) {
                drmp3_uint64 i;
                for (i = 0; i < frames; i++)
                    mono[i] = cfg.channels == 2 ? (int16_t)((pcm[i * 2] + pcm[i * 2 + 1]) / 2) : pcm[i];
                out->data = mono;
                out->frames = (uint32_t)frames;
                out->rate = (int32_t)cfg.sampleRate;
                out->is16 = 1;
            }
        }
        if (pcm)
            drmp3_free(pcm, NULL);
    } else if (ends_with(path, ".aiff") || ends_with(path, ".aif")) {
        uint8_t *file = slurp(path, &len);
        int16_t *mono = NULL;
        if (!file)
            return 0;
        if (aiff_decode(file, len, &mono, &out->frames, &out->rate)) {
            out->data = mono;
            out->is16 = 1;
        }
        free(file);
    } else if (ends_with(path, ".ogg")) {  /* (SWOS 2020's packs: stb_vorbis, docs/MODS.md §10.3) */
        uint8_t *file = slurp(path, &len);
        int channels = 0, rate = 0;
        short *pcm = NULL;
        int frames;
        if (!file)
            return 0;
        frames = stb_vorbis_decode_memory(file, (int)len, &channels, &rate, &pcm);
        free(file);
        if (pcm && frames > 0 && channels >= 1) {
            int16_t *mono = (int16_t *)malloc((size_t)frames * 2);
            if (mono) {
                for (int i = 0; i < frames; i++) {
                    int32_t sum = 0;
                    for (int c = 0; c < channels; c++)
                        sum += pcm[i * channels + c];
                    mono[i] = (int16_t)(sum / channels);
                }
                out->data = mono;
                out->frames = (uint32_t)frames;
                out->rate = rate;
                out->is16 = 1;
            }
        }
        free(pcm);
    }
    if (!out->data || !out->frames || out->rate < 4000) {
        free(out->data);
        out->data = NULL;
        return 0;
    }
    out->chance = chance_of(path);
    return 1;
}

static int32_t trace_on(void);

/* A mod's own copy of a table file in any format SWOS 2020's packs use -- <name>.raw, .ogg, .wav
 * or .mp3 under the mod's sound_dos/ -- the first enabled mod with one; the PC game's .raw when no
 * mod has it (docs/MODS.md §10.3). 1 when a mod's was loaded. */
static int32_t file_load_mod(int32_t id)
{
    static const char *const ext[] = { ".raw", ".ogg", ".wav", ".mp3", ".aiff" };
    char base[96], path[1300];
    const char *dot = strrchr(k_dosSoundFiles[id], '.');
    snprintf(base, sizeof base, "%.*s", dot ? (int)(dot - k_dosSoundFiles[id]) : 95, k_dosSoundFiles[id]);
    for (int32_t m = 0; m < Mods_Count(); m++)
        for (int32_t e = 0; e < 5; e++) {
            CustomLine line;
            FILE *f;
            snprintf(path, sizeof path, "%s/sound_dos/%s%s", Mods_Dir(m), base, ext[e]);
            if (!(f = fopen(path, "rb")))
                continue;
            fclose(f);
            if (!custom_load(path, &line)) {
                fprintf(stderr, "[dos-sound] %s: not sound we can play -- the game's own\n", path);
                return 0;
            }
            s_file[id].len = line.frames;
            s_file[id].rate = line.rate;
            s_file[id].is16 = (int8_t)line.is16;
            s_file[id].data = (uint8_t *)line.data;     /* last: the game plays what has a pointer */
            if (trace_on())
                fprintf(stderr, "[dos-sound] %s from %s (%u frames at %d Hz)\n", k_dosSoundFiles[id], path,
                        (unsigned)line.frames, (int)line.rate);
            return 1;
        }
    return 0;
}


/* Every category's folder, once, after the PC game's own files. */
static void custom_scan(void)
{
#ifndef SWOS_SWITCH_BUILD
    char base[1024], dir[1280], path[1600];
    int32_t c, lines = 0, cats = 0;

    if (s_customDir[0] == '/' || s_customDir[0] == '\\' || (s_customDir[0] && s_customDir[1] == ':'))
        snprintf(base, sizeof base, "%s", s_customDir);                 /* absolute */
    else
        swos_data_path(s_customDir[0] ? s_customDir : "sound_dos/commentary", base, (uint32_t)sizeof base);
    for (c = 0; c < CATEGORIES; c++) {
        DIR *d;
        struct dirent *e;
        int32_t n = 0, total = 0;

        snprintf(dir, sizeof dir, "%s/%s", base, k_category[c]);
        d = opendir(dir);
        if (!d)
            continue;
        while ((e = readdir(d)) != NULL && n < CUSTOM_PER_CATEGORY) {
            CustomLine line;
            if (e->d_name[0] == '.')
                continue;
            snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
            if (!ends_with(path, ".raw") && !ends_with(path, ".wav") && !ends_with(path, ".mp3") &&
                !ends_with(path, ".ogg") && !ends_with(path, ".aiff") && !ends_with(path, ".aif")) {
                if (!ends_with(path, ".txt"))
                    fprintf(stderr, "[sound] commentary/%s/%s: not .raw, .wav, .mp3, .ogg or .aiff -- left out\n",
                            k_category[c], e->d_name);
                continue;
            }
            if (!custom_load(path, &line)) {
                fprintf(stderr, "[sound] commentary/%s/%s could not be read\n", k_category[c], e->d_name);
                continue;
            }
            s_customBytes += line.frames * (line.is16 ? 2u : 1u);
            if (s_customBytes > CUSTOM_MAX_BYTES) {
                fprintf(stderr, "[sound] commentary: more than %u MB of sound; the rest is left out\n",
                        CUSTOM_MAX_BYTES >> 20);
                free(line.data);
                closedir(d);
                goto done;
            }
            s_custom[c].line[n++] = line;
            total += line.chance;
        }
        closedir(d);
        s_custom[c].total = total;
        s_custom[c].last = -1;
        s_custom[c].count = n;
        if (n) {
            lines += n;
            cats++;
        }
    }
done:
    if (lines)
        fprintf(stderr, "[sound] your commentary: %d files in %d of %d categories (%s)\n", lines, cats,
                CATEGORIES, base);
#endif
}

/* All 156 files, 9 MB, read on a thread of their own from the moment the PC sounds are
 * asked for -- never at kick-off. Read there, they held the first frame of a match back
 * by as long as the disk took: an online match timed out before it began, and the
 * replay rig's first frame came out different (its clock runs in real time). A sound
 * whose file has not arrived yet is not played. */
static void custom_scan(void);

static int load_all(void *unused)
{
    int32_t i, got = 0;
    (void)unused;
    for (i = 0; i < DOS_SOUND_FILES; i++)
        got += file_load(i);
    s_loadGot = got;
    s_loadDone = 1;
    custom_scan();
    return 0;
}

static void preload(void)
{
    if (s_loader || s_loadDone)
        return;
#ifdef SWOS_WASM_BUILD
    load_all(NULL);         /* no threads, and no sound_dos/ either: the first fopen fails */
#else
    s_loader = SDL_CreateThread(load_all, "dos-sound", NULL);
    if (!s_loader)
        load_all(NULL);
#endif
}

/* SWOS_DOS_SOUND_TRACE=1: every sample started, on stderr. */
static int32_t trace_on(void)
{
    static int32_t on = -1;
    if (on < 0)
        on = getenv("SWOS_DOS_SOUND_TRACE") != NULL;
    return on;
}

static void play_at(int32_t voice, int32_t id, int32_t volume, int32_t loops, int32_t rate)
{
    if (id == DOS_SOUND_NONE || id < 0 || id >= DOS_SOUND_FILES || !s_file[id].data)
        return;
    if (trace_on())
        fprintf(stderr, "[dos-sound] voice %d: %s, volume %d%s%s\n", voice, k_dosSoundFiles[id], volume,
                loops ? ", looped" : "", rate != RATE ? ", at half rate" : "");
    if (s_file[id].is16)                /* (a mod's, at its own rate: the game's `rate` relative to RATE) */
        AudioSDL_PcmPlay16(voice, (const int16_t *)s_file[id].data, s_file[id].len,
                           (int)((int64_t)s_file[id].rate * rate / RATE), volume, loops);
    else
        AudioSDL_PcmPlay(voice, s_file[id].data, s_file[id].len, rate, volume, loops);
}

static void play(int32_t voice, int32_t id, int32_t volume, int32_t loops)
{
    play_at(voice, id, volume, loops, RATE);
}

static void play_fx(int32_t fx, int32_t volume)
{
    play(VOICE_FX_FIRST + s.fxNext, k_dosEffects[fx], volume, 0);
    s.fxNext = (s.fxNext + 1) % VOICE_FX_COUNT;
}

/* The two long whistles -- the foul and the end of a half -- are recordings at HALF the
 * rate of everything else. The PC game plays them through a routine of their own, which
 * the disassembly names PlayRefereeWhistleSample22Khz and which sets 11,025 Hz; played
 * at 22,050 they were an octave high and half as long (ReportBot 0033). The kick-off
 * whistle is a 22,050 Hz file like the rest. */
static void play_long_whistle(int32_t fx)
{
    play_at(VOICE_FX_FIRST + s.fxNext, k_dosEffects[fx], 42, 0, RATE / 2);
    s.fxNext = (s.fxNext + 1) % VOICE_FX_COUNT;
}

/* ---- commentary ---- */

static int32_t commentary_wanted(void)
{
    return s_commentary == 1 || (s_commentary == 2 && s_style);
}

static int32_t commentary_on(void)
{
    return commentary_wanted() && !s_muteCommentary && !s_training;
}

/* PlayCommentarySample: a line of an (offset, size) table, never the last one played
 * from it. It replaces the line being spoken. */
/* A line of the category's folder, by its files' chances, never the one just played
 * from it. 0 when the folder has none: the PC game's lines are used. */
static int32_t custom(int32_t cat)
{
    int32_t n = s_custom[cat].count, roll, i;
    const CustomLine *line;

    if (n <= 0)
        return 0;
    roll = (int32_t)(rnd() % (uint32_t)s_custom[cat].total);
    for (i = 0; i < n - 1; i++) {
        roll -= s_custom[cat].line[i].chance;
        if (roll < 0)
            break;
    }
    if (i == s_custom[cat].last && n > 1)
        i = (i + 1) % n;
    s_custom[cat].last = i;
    line = &s_custom[cat].line[i];
    if (trace_on())
        fprintf(stderr, "[dos-sound] voice %d: commentary/%s, file %d of %d\n", VOICE_COMMENT, k_category[cat], i + 1, n);
    if (line->is16)
        AudioSDL_PcmPlay16(VOICE_COMMENT, (const int16_t *)line->data, line->frames, line->rate, 127, 0);
    else
        AudioSDL_PcmPlay(VOICE_COMMENT, (const uint8_t *)line->data, line->frames, line->rate, 127, 0);
    return 1;
}

/* A category that has only a folder: SWOS 2020's header and injury lines, and the
 * "so close" line the PC game never reaches. */
static void comment_custom(int32_t cat)
{
    if (commentary_on())
        custom(cat);
}

static void comment(int32_t cat, const uint8_t *table, int32_t mask, int32_t slot)
{
    int32_t pick;
    if (!commentary_on() || custom(cat))
        return;
    pick = (int32_t)rnd() & mask;
    if (pick == s.last[slot])
        pick = (pick + 1) & mask;
    s.last[slot] = (int16_t)pick;
    play(VOICE_COMMENT, table[pick], 127, 0);
}

/* PlaySampleFromTable and its two relatives: a file of a table of names. */
static void comment_file(int32_t cat, const uint8_t *table, int32_t mask)
{
    if (!commentary_on() || custom(cat))
        return;
    play(VOICE_COMMENT, table[(int32_t)rnd() & mask], 127, 0);
}

enum { L_GOAL, L_SAVED, L_OWN_GOAL, L_NEAR_MISS, L_POST, L_BAR, L_CLAIMED, L_TACKLE, L_PASS,
       L_FOUL, L_DANGEROUS, L_PENALTY, L_PEN_SAVED, L_PEN_MISSED, L_PEN_GOAL };

static void penalty_goal(void)
{
    s.performingPenalty = 0;
    comment(C_PENALTY_SCORED, k_dos_penaltyGoalSamples, 3, L_PEN_GOAL);
}

static void penalty_missed(void)
{
    s.performingPenalty = 0;
    comment(C_PENALTY_MISSED, k_dos_missedPenaltySamples, 3, L_PEN_MISSED);
}

static void penalty_saved(void)
{
    s.performingPenalty = 0;
    comment(C_PENALTY_SAVED, k_dos_savedPenaltySamples, 3, L_PEN_SAVED);
}

/* ---- chants ---- */

/* LoadCrowdChantSample: after a goal, the chant about the score, or none. */
static void result_chant(int32_t goals1, int32_t goals2)
{
    int32_t pick;

    s.interesting = 0;
    s.resultChant = -1;
    if (!s_full || s_training)
        return;
    if (goals1 != 0 && goals2 != 0) {
        if (goals1 != goals2)
            return;
        pick = 8;                           /* the equaliser */
    } else {
        if (goals1 == goals2)
            return;
        pick = goals1 ? goals1 : goals2;
        if (pick > 6)
            return;
        if (!(rnd() & 1))
            pick = 7;                       /* cheer the losing team up */
    }
    s.resultChant = k_dosResultChants[pick - 1];
    s.interesting = 1;
    AudioSDL_PcmStop(VOICE_CHANT);          /* StopChantsSample */
    s.chantPhase = 0;
}

void DosSound_ChantStart(int32_t slot)
{
    int32_t id;

    if (!s_full || s_training)
        return;
    /* playIntroChantSampleOffset, 8l, 10l (the result chant in its place), 4l -- and
     * an interesting game is given the result chant whatever was picked. */
    if (s.interesting && s.resultChant >= 0)
        id = s.resultChant;
    else if (slot == 0)
        id = s.introChant >= 0 ? s.introChant : k_dosEffects[FX_CHANT4L];
    else if (slot == 1)
        id = k_dosEffects[FX_CHANT8L];
    else if (slot == 2)
        id = k_dosEffects[FX_CHANT10L];
    else
        id = k_dosEffects[FX_CHANT4L];
    /* PlayChantSample22Khz: volume 0 and repeating until stopped (it hands AIL a loop
     * count of 0 whatever its caller asked for). The fade brings it in. */
    play(VOICE_CHANT, id, 0, -1);
    s.chantPhase = 1;
    s.chantTimer = 128;                     /* fadeInChantTimer */
    s.chantAcc = 0;
}

/* The Amiga's chant channel volume. Not followed: the Amiga's machine says WHEN a chant
 * starts, and chant_tick gives it the PC game's own shape -- in at 128 ticks to volume
 * 64 (half), held 500 to 1010 ticks (7 to 14 seconds), out over 128.
 *
 * The PC game has TWO copies of this routine, and until 2026-10-07 this was the one
 * nothing calls (swos.asm cseg_18236: to volume 127, held 250 to 760): chants were twice
 * as loud as the PC game's and half as long. PlayCrowdChants, which PlayEnqueuedSamples
 * calls, is the one below. */
void DosSound_ChantVolume(int32_t volume)
{
    (void)volume;
}

/* One frame shown. The PC game's PlayCrowdChants runs once a tick of its 70 a second. */
static void chant_tick(void)
{
    extern int DosGameplay_TargetFps(void);
    int32_t fps = DosGameplay_TargetFps();

    if (!s.chantPhase)
        return;
    for (s.chantAcc += 70; s.chantAcc >= fps && s.chantPhase; s.chantAcc -= fps) {
        if (s.chantPhase == 1) {            /* crowdChantVolume = (128 - fadeInChantTimer) / 2 */
            int32_t volume = (128 - --s.chantTimer) >> 1;
            if (volume)                     /* "don't stop it when it's zero" */
                AudioSDL_PcmVolume(VOICE_CHANT, volume);
            if (s.chantTimer == 0) {
                s.chantPhase = 2;
                s.chantTimer = (int32_t)(rnd() & 0xFF) * 2 + 500;      /* nextChantTimer */
            }
        } else if (s.chantPhase == 2) {
            if (--s.chantTimer == 0) {
                s.chantPhase = 3;
                s.chantTimer = 128;         /* fadeOutChantTimer */
            }
        } else {                            /* crowdChantVolume = fadeOutChantTimer / 2; 0 stops it */
            int32_t volume = --s.chantTimer >> 1;
            if (volume == 0) {
                AudioSDL_PcmStop(VOICE_CHANT);
                s.chantPhase = 0;
            } else {
                AudioSDL_PcmVolume(VOICE_CHANT, volume);
            }
        }
    }
}

/* ---- the match ---- */

static void match_start(uint16_t arg)
{
    int32_t i, colour = arg & 0x0F;

    DosSound_Stop();
    if (!s_style && !commentary_wanted())
        return;
    preload();
    if (s_loadDone && s_loadGot != DOS_SOUND_FILES) {
        fprintf(stderr, "[sound] the PC sounds are incomplete (%d of %d files in sound_dos/): "
                "the Amiga's are used\n", (int)s_loadGot, DOS_SOUND_FILES);
        return;
    }
    memset(&s, 0, sizeof s);
    s.yellowTimer = s.redTimer = s.goodPassTimer = s.deletedTimer = -1;
    s.throwInTimer = s.cornerTimer = s.substituteTimer = s.tacticsTimer = -1;
    for (i = 0; i < 16; i++)
        s.last[i] = -1;
    s.resultChant = -1;
    s.rnd = (uint32_t)SDL_GetTicks() * 2654435761u + 1;
    /* LoadIntroChant: a chant for the home side's shirt colour, where there is one. */
    s.introChant = k_dosIntroChantByColour[colour] >= 0
                 ? k_dosIntroChants[k_dosIntroChantByColour[colour]] : -1;
    s_training = (arg & 0x100) != 0;
    s_muteCommentary = 0;
    s_live = 1;
    s_full = s_style;
    if (s_full) {
        /* The Amiga's own crowd is already running: the match starts it (presets 1 and
         * 6, PlaySample1IfNotTraining and its neighbours) before this event, while the
         * PC sounds are not yet "live" and nothing holds it back -- and it loops for the
         * whole match, at about five times the PC crowd's level, under everything here.
         * Heard as a background as loud as the commentary (ReportBot 0027). A recording's
         * playback never showed it: it silences the effects when it starts. */
        extern void AudioSDL_SilenceSfx(void);
        AudioSDL_SilenceSfx();
    }
    if (s_full && !s_training)
        play(VOICE_CROWD, k_dosEffects[FX_CROWD], 100, -1);     /* PlayCrowdNoiseSample */
    fprintf(stderr, "[sound] %s%s\n", s_full ? "PC sounds" : "Amiga sounds",
            commentary_wanted() ? " with the PC commentary" : ", no commentary");
}

/* PlayEnqueuedSamples: at most one waiting line a frame, in the PC game's order. */
static int32_t due(int16_t *timer)
{
    if (*timer < 0)
        return 0;
    if (--*timer != 0)
        return 0;
    *timer = -1;
    return 1;
}

static void tick(uint16_t arg)
{
    chant_tick();
    if (due(&s.yellowTimer))
        comment_file(C_YELLOW_CARD, k_dos_yellowCardSamplesTable, 15);
    else if (due(&s.redTimer))
        comment_file(C_RED_CARD, k_dos_redCardSamplesTable, 15);
    else if (due(&s.goodPassTimer)) {
        if (!AudioSDL_PcmPlaying(VOICE_COMMENT))        /* never over another line */
            comment(C_GOOD_PLAY, k_dos_goodPassSamples, 3, L_PASS);
    } else if (due(&s.deletedTimer))
        ;
    else if (due(&s.throwInTimer))
        comment_file(C_THROW_IN, k_dos_throwInSamplesTable, 7);
    else if (due(&s.cornerTimer)) {
        if (!(arg & 1))                                 /* not in a shoot-out */
            comment_file(C_CORNER, k_dos_cornerSamplesTable, 7);
    } else if (due(&s.substituteTimer))
        comment_file(C_SUBSTITUTION, k_dos_playerSubstitutedSamplesTable, 7);
    else if (due(&s.tacticsTimer))
        comment_file(C_TACTIC_CHANGED, k_dos_tacticsChangeSamplesTable, 7);
}

/* LoadAndPlayEndGameComment. */
static void game_over(int32_t goals1, int32_t goals2)
{
    int32_t diff = goals1 > goals2 ? goals1 - goals2 : goals2 - goals1;

    if (!s_training && s_full)
        play(VOICE_REACTION, k_dosEndGameCrowd[goals1 == goals2 ? 2 : goals2 < goals1 ? 0 : 1], 127, 0);
    if (diff >= 3)
        comment_file(C_END_GAME_ROUT, k_dos_completeRoutSamplesTable, 7);
    /* The PC game means to say "so close" after a draw, and never does: its test
     * for a difference of 0 follows a jump taken for every difference of 0 or more
     * (swos.asm, LoadAndPlayEndGameComment, "this jump is never taken"). Kept for
     * its own lines; a commentary folder's end_game_so_close is spoken, as SWOS 2020's is. */
    else if (diff == 0 && s_custom[C_END_GAME_SO_CLOSE].count > 0)
        comment_custom(C_END_GAME_SO_CLOSE);
    else if (goals1 + goals2 >= 4)
        comment_file(C_END_GAME_SENSATIONAL, k_dos_sensationalGameSamplesTable, 7);
}

void DosSound_Event(uint8_t event, uint16_t arg)
{
    /* The arcade cabinet's attract reels play under its title song (arcade.c). */
    extern int _gArcadeSfxSuppressed;
    if (_gArcadeSfxSuppressed)
        return;
    if (event == DOS_EV_MATCH_START) {
        match_start(arg);
        return;
    }
    if (!s_live)
        return;
    if (trace_on() && event != DOS_EV_TICK)
        fprintf(stderr, "[dos-sound] event %u arg %u\n", event, arg);

    switch (event) {
    case DOS_EV_MATCH_END:      DosSound_Stop(); break;
    case DOS_EV_TICK:           tick(arg); break;

    case DOS_EV_KICK:           if (s_full) play_fx(FX_KICK, 25); break;
    case DOS_EV_BOUNCE:         if (s_full) play_fx(FX_BOUNCE, 42); break;
    case DOS_EV_WHISTLE:        if (s_full) play_fx(FX_WHISTLE, 42); break;
    case DOS_EV_FOUL_WHISTLE:   if (s_full) play_long_whistle(FX_FOUL); break;
    case DOS_EV_END_WHISTLE:    if (s_full) play_long_whistle(FX_ENDGAME); break;
    case DOS_EV_GOAL_CROWD:
        if (s_full && !s_training)
            play(VOICE_REACTION, k_dosEffects[FX_HOMEGOAL], 127, 0);
        break;
    case DOS_EV_MISS_CROWD:
        if (s_full && !s_training)
            play(VOICE_REACTION, k_dosEffects[FX_MISSGOAL], 127, 0);
        break;

    case DOS_EV_GOAL:
        if (s.performingPenalty || (arg & 1))
            penalty_goal();
        else
            comment(C_GOAL, k_dos_goalSamples, 7, L_GOAL);
        break;
    case DOS_EV_OWN_GOAL:       comment(C_OWN_GOAL, k_dos_ownGoalSamples, 7, L_OWN_GOAL); break;
    case DOS_EV_NEAR_MISS:
        if (s.performingPenalty || (arg & 1))
            penalty_missed();
        else
            comment(C_NEAR_MISS, k_dos_nearMissesSamples, 3, L_NEAR_MISS);
        break;
    case DOS_EV_KEEPER_SAVED:
        if (s.performingPenalty || (arg & 1))
            penalty_saved();
        else
            comment(C_KEEPER_SAVED, k_dos_goalkeeperSavedSamples, 7, L_SAVED);
        break;
    case DOS_EV_KEEPER_CLAIMED: comment(C_KEEPER_CLAIMED, k_dos_keeperClaimedSamples, 7, L_CLAIMED); break;
    case DOS_EV_WOODWORK:
        /* UpdateBall: half the time the post line; the other half the bar line,
         * unless the game's flag says post. */
        if (!(rnd() & 1) || arg)
            comment(C_HIT_POST, k_dos_postHitSamples, 7, L_POST);
        else
            comment(C_HIT_BAR, k_dos_barHitSamples, 1, L_BAR);
        break;
    case DOS_EV_PENALTY:
        s.performingPenalty = 1;
        comment(C_PENALTY, k_dos_itsPenaltySamples, 3, L_PENALTY);
        break;
    case DOS_EV_FOUL:           comment(C_FREE_KICK, k_dos_foulSamples, 7, L_FOUL); break;
    case DOS_EV_DANGEROUS_PLAY: comment(C_DIRTY_TACKLE, k_dos_dangerousPlaySamples, 3, L_DANGEROUS); break;
    case DOS_EV_GOOD_TACKLE:
        if (!AudioSDL_PcmPlaying(VOICE_COMMENT))
            comment(C_GOOD_TACKLE, k_dos_goodTackleSamples, 3, L_TACKLE);
        break;
    case DOS_EV_PASS:
        /* EnqueuPlayingGoodPassSample: any pass drops a waiting line; the fifth asks for one. */
        s.goodPassTimer = -1;
        if (++s.goodPassCount == 5) {
            s.goodPassCount = 0;
            s.goodPassTimer = 10;
        }
        break;
    case DOS_EV_CORNER:         s.cornerTimer = 100; break;
    case DOS_EV_THROW_IN:       s.throwInTimer = 70; break;
    case DOS_EV_YELLOW_CARD:    s.yellowTimer = 100; break;
    case DOS_EV_RED_CARD:       s.redTimer = 100; break;
    case DOS_EV_SUBSTITUTION:   s.substituteTimer = 70; break;
    case DOS_EV_TACTICS:        s.tacticsTimer = 70; break;
    case DOS_EV_HEADER:
        /* SWOS 2020: a header by a human's player, and never over another line. */
        if (arg && !AudioSDL_PcmPlaying(VOICE_COMMENT))
            comment_custom(C_HEADER);
        break;
    case DOS_EV_INJURY:
        if (arg)
            comment_custom(C_INJURY);
        break;
    case DOS_EV_SCORE:          result_chant(arg >> 8, arg & 0xFF); break;
    case DOS_EV_GAME_OVER:      game_over(arg >> 8, arg & 0xFF); break;
    default: break;
    }
}

void DosSound_Stop(void)
{
    if (s_live)
        AudioSDL_PcmStop(-1);
    s_live = 0;
    s_full = 0;
}

int32_t DosSound_Live(void)
{
    return s_full;
}

void DosSound_SetStyle(int32_t pc)
{
    s_style = pc != 0;
    if (!s_style)
        DosSound_Stop();
    if (s_style || commentary_wanted())
        preload();
}

int32_t DosSound_Style(void)
{
    return s_style;
}

void DosSound_SetCommentaryDir(const char *dir)
{
    snprintf(s_customDir, sizeof s_customDir, "%s", dir ? dir : "");
}

void DosSound_SetCommentary(int32_t on)
{
    s_commentary = on < 0 || on > 2 ? 2 : on;
    if (commentary_wanted())
        preload();
}

/* ---- self-test ---- */

#define TABLE(t)    { #t, t, (int32_t)sizeof t }

int32_t DosSound_SelfTest(void)
{
    static const struct { const char *name; const uint8_t *ids; int32_t count; } tables[] = {
        TABLE(k_dosEffects), TABLE(k_dosIntroChants), TABLE(k_dosResultChants), TABLE(k_dosEndGameCrowd),
        TABLE(k_dos_goalSamples), TABLE(k_dos_goalkeeperSavedSamples), TABLE(k_dos_ownGoalSamples),
        TABLE(k_dos_nearMissesSamples), TABLE(k_dos_postHitSamples), TABLE(k_dos_barHitSamples),
        TABLE(k_dos_keeperClaimedSamples), TABLE(k_dos_goodTackleSamples), TABLE(k_dos_goodPassSamples),
        TABLE(k_dos_foulSamples), TABLE(k_dos_dangerousPlaySamples), TABLE(k_dos_itsPenaltySamples),
        TABLE(k_dos_savedPenaltySamples), TABLE(k_dos_missedPenaltySamples), TABLE(k_dos_penaltyGoalSamples),
        TABLE(k_dos_cornerSamplesTable), TABLE(k_dos_tacticsChangeSamplesTable),
        TABLE(k_dos_playerSubstitutedSamplesTable), TABLE(k_dos_throwInSamplesTable),
        TABLE(k_dos_redCardSamplesTable), TABLE(k_dos_yellowCardSamplesTable), TABLE(k_dos_drawSamplesTable),
        TABLE(k_dos_completeRoutSamplesTable), TABLE(k_dos_sensationalGameSamplesTable),
    };
    int32_t failures = 0, i, j;
    uint32_t bytes = 0;

    if (getenv("SWOS_MODS_ENABLED") && !Mods_Count())   /* (a sound pack's mods: run before the game loads them) */
        Mods_Init(getenv("SWOS_MODS_ENABLED"), "");
    /* Data made from SWOS 2020 (no PC game: tools/swosdata, ASSETS.ini pc_swos2020) has the PC
     * game's effects (fx/) and 11 of its crowd's sounds, no commentary and no other crowd: there
     * only fx/ must be whole, the rest is said, not failed. */
    extern int32_t AssetsCheck_PcFromSwos2020(void);
    const int32_t from2020 = AssetsCheck_PcFromSwos2020();
    int32_t notIn2020 = 0;
    if (from2020)
        fprintf(stderr, "[dos-sound] the PC data was made from SWOS 2020: its effects must all be there, "
                        "its crowd's and commentary need not\n");
    for (i = 0; i < DOS_SOUND_FILES; i++) {
        if (!file_load(i)) {
            if (from2020 && strncmp(k_dosSoundFiles[i], "fx/", 3) != 0) {
                notIn2020++;
                continue;
            }
            fprintf(stderr, "[dos-sound] FAIL: sound_dos/%s is missing\n", k_dosSoundFiles[i]);
            failures++;
        } else {
            bytes += s_file[i].len * (s_file[i].is16 ? 2u : 1u);
        }
    }
    for (i = 0; i < (int32_t)(sizeof tables / sizeof tables[0]); i++) {
        for (j = 0; j < tables[i].count; j++) {
            uint8_t id = tables[i].ids[j];
            if (id != DOS_SOUND_NONE && id >= DOS_SOUND_FILES) {
                fprintf(stderr, "[dos-sound] FAIL: %s[%d] = %u names no file\n", tables[i].name, j, id);
                failures++;
            }
        }
    }
    {
        int32_t lines = 0, cats = 0, mods = 0;
        for (i = 0; i < DOS_SOUND_FILES; i++)
            mods += s_file[i].is16;
        custom_scan();                  /* (the commentary folders, as a match starts with them) */
        for (i = 0; i < CATEGORIES; i++) {
            lines += s_custom[i].count;
            cats += s_custom[i].count > 0;
        }
        fprintf(stderr, "[dos-sound] %d of the files from a mod in another format; commentary: %d lines in %d "
                        "categories\n", (int)mods, (int)lines, (int)cats);
    }
    if (notIn2020)
        fprintf(stderr, "[dos-sound] %d of the files not in SWOS 2020 (made from it): not there, as expected\n",
                (int)notIn2020);
    fprintf(stderr, "[dos-sound] %d files, %u bytes (%.1f s at %d Hz), %d tables: %s\n",
            DOS_SOUND_FILES, bytes, bytes / (double)RATE, RATE, (int32_t)(sizeof tables / sizeof tables[0]),
            failures ? "FAILED" : "PASSED");
    return failures;
}

#endif /* SWOS_SDL_BUILD */
