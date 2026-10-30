/*
 * replay_v2.c -- whole-match replays in the SWRP format: recorder, file, player.
 * See include/replay_v2.h; docs/REPLAY_FORMAT_V2.md is the specification.
 *
 * What a .RPL / .HIL records is what a 336-wide screen drew: sprite positions relative to
 * the camera, packed into 10 bits, no sound. A .SRP records the match instead:
 *
 *   - every WORLD sprite in the draw list, drawn or not, at its world position -- so a
 *     replay recorded on a 4:3 screen fills a 16:9 one, and the other way round;
 *   - the SIMULATION camera (_cameraX), which is the same on every display, so playback
 *     maps it onto whatever display it is shown on exactly as a live match does
 *     (Render_CameraX) instead of guessing a camera from the ball;
 *   - every sound effect and channel volume the match set, so the kicks, whistles, goal
 *     roars and crowd chants of the match are the ones heard.
 *
 * SDL-only: the Amiga build has no file for it and no use for widescreen.
 */
#ifdef SWOS_SDL_BUILD

#include "replay_v2.h"
#include <SDL.h>            /* the checkpoint's thread */
#include "dos_sound.h"
#include "audio_sfx_filter.h"
#include "build_info.h"
#include "display.h"
#include "endian_helpers.h"
#include "game_types.h"
#include "sprites.h"            /* Sprite_PastWalkOffEdge */
#include "miniz_swos.h"
#include "srp_writer.h"     /* the file's layout, shared with the recorder */
#include "swos_io.h"
#include "image_cmd.h"      /* ProcessImageCommandStream */
#include "platform_sdl.h"   /* Platform_NearestColour: the prediction overlay */
#include "dos_mode.h"       /* _gDosView: the view a recording was played in */
#ifdef SWOS_NETPLAY
#include "netplay_ghost.h"  /* Ghost_Prediction: this player's PRED and SEEN */
#include "match_prediction.h"   /* the other player's, over the lobby's link */
#endif
#ifdef SWOS_WASM_BUILD
#include <emscripten.h>     /* EMSCRIPTEN_KEEPALIVE: the playback page's controls */
#else
#define EMSCRIPTEN_KEEPALIVE
#endif
#include "match_sim.h"      /* InitTeams */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- the file ---------------------------------------------------------------------- */

/* The layout's constants (SWRP_*, CHUNK_*, FRAME_HEAD_BYTES, SPRITE_BYTES, ...) are
 * include/srp_writer.h's: the recorder writes these files too. */

#define TEAM_BYTES          1018    /* a TeamGame as the HIL header holds it */
#define TEAM_HDR_INT16S     11
#define META_SCORE_OFF      SRP_META_SCORE_OFF          /* the final score inside META */
#define META_BYTES          (2 + 2 + 2 + 2 + 4 + 32 + 2 + 2 + 2 + 2 + 2 + 40 + 40 + 2 * TEAM_BYTES)
#define MAX_FRAME_SPRITES   78      /* _sortedSprites */

/* Sprites at z >= this are the screen-pinned HUD (sprites.c HUD_LAYER_Z_MIN): placed
 * for this display, and drawn by the viewer's own overlay instead. */
#define HUD_Z_MIN           8000

enum { CHAPTER_GOAL = 1, CHAPTER_HALF = 2 };

typedef SrpAudioEvent AudioEvent;
typedef SrpChapter    Chapter;

/* ---- game state it reads and writes ------------------------------------------------ */

extern Sprite     *_sortedSprites[];
extern uint16_t    _numSpritesToRender;
extern uint8_t    *_spriteGraphicsPtr;
extern FixedPoint  _cameraX, _cameraY;
extern uint16_t    _team1TotalGoals, _team2TotalGoals;
extern uint16_t    _animPatternsState;
extern uint8_t     _gameTime[4];
extern uint16_t    _halfNumber;
extern uint16_t    _statsTeam1Goals, _statsTeam2Goals;
extern uint16_t    _pitchNumberAndType, _gameMaxSubstitutes;
extern uint8_t     _gameName[], _gameRound[];
extern TeamGame    _topTeamInGame, _bottomTeamInGame;
extern uint16_t    _trainingGame;
extern int         _gArcadeMode, _gDosGameplay, _gDosGraphics, _gOriginalFireHoldLockout;
extern int         _screen_width;

extern uint8_t     _hilFileBuffer[];
extern uint16_t    _hilNumGoals, _hilTeam1Goals, _hilTeam2Goals;
extern uint16_t    _hilPitchNumber, _hilMaxSubstitutes;
extern uint8_t     _hilGameName[], _hilGameRound[];
extern uint16_t    _hilLeftTeamGoals, _hilRightTeamGoals;
extern volatile uint16_t _numScenesInHilFile;
extern uint16_t    _replayState;
extern int16_t     _savedCameraX, _savedCameraY;
extern int         _replayPlaybackFps, _replayRecordingIsWide;

extern int         DosGameplay_TargetFps(void);
extern int         ReplayTest_Armed(void);
extern void        DosReplay_ClearRpl(void);
extern void        DarkenRectangle(uint16_t sprIdx, int16_t x, int16_t y);
extern void        MoveView(int16_t newScreenX, int16_t newScreenY);
extern void        DrawAnimatedPatterns(void);
extern void        ResetAnimatedPatternsForBothTeams(void);
extern void        SetCameraCoordinates(int16_t targetX, int16_t targetY);
extern void        InitCameraMoveView(int16_t targetX, int16_t targetY);
extern void        SwapBuffersAndCameraViewState(void);
extern void        MirrorFrontbuffer(void);
extern void        AudioApplyPresetToChannels(uint16_t presetId);
extern void        AudioChannelQueueNextWaveIfEnabled(uint16_t channel, uint16_t waveId);
extern void        AudioSetChannelVolume(uint16_t channelOrAll, uint16_t volume);

static SpriteGraphics *graphics(uint16_t imageIndex)
{
    return ((SpriteGraphics **)(void *)_spriteGraphicsPtr)[imageIndex];
}

/* ---- growable buffers --------------------------------------------------------------- */

static int32_t grow(void **buf, uint32_t *cap, uint32_t need, uint32_t elem)
{
    uint32_t n;
    void *p;
    if (need <= *cap)
        return 0;
    n = *cap ? *cap : 4096;
    while (n < need)
        n *= 2;
    p = realloc(*buf, (size_t)n * elem);
    if (!p)
        return 1;
    *buf = p;
    *cap = n;
    return 0;
}

/* ---- the trace (tests) --------------------------------------------------------------- */

/* SWOS_REPLAY_TRACE=<file>: one line per frame, "frame F sprites N hash H", and one per
 * sound, "frame F sound K C I" -- written the same way while recording (the sprites the
 * live match drew) and while playing (the sprites the player drew), so a test can require
 * the two to be identical at the same screen width. */
static FILE    *s_trace;
static int32_t  s_traceClamped;     /* playback held this frame's camera (shown_camera_y) */
static int32_t  s_traceTried;
static uint32_t s_traceCount;
static uint64_t s_traceHash;

static FILE *trace_file(void)
{
    if (!s_traceTried) {
        const char *path = getenv("SWOS_REPLAY_TRACE");
        s_traceTried = 1;
        if (path && path[0])
            s_trace = fopen(path, "w");
    }
    return s_trace;
}

static void trace_sprite(int16_t ordinal, int16_t x, int16_t y)
{
    /* SWOS_REPLAY_TRACE_SPRITES=1: every sprite too, for finding the one that differs. */
    static int32_t verbose = -1;
    if (verbose < 0)
        verbose = getenv("SWOS_REPLAY_TRACE_SPRITES") != NULL;
    if (verbose && s_trace)
        fprintf(s_trace, "  sprite %d %d %d\n", ordinal, x, y);
    uint64_t h = (uint64_t)(uint16_t)ordinal * 0x9E3779B97F4A7C15ull;
    h ^= (uint64_t)(uint16_t)x * 0xC2B2AE3D27D4EB4Full;
    h ^= (uint64_t)(uint16_t)y * 0x165667B19E3779F9ull;
    h ^= h >> 29;
    /* Mixed in ORDER, not summed: index 0 is drawn first, so two frames with the same
     * sprites in a different order overlap differently and must not hash alike. */
    s_traceHash = (s_traceHash * 0xBF58476D1CE4E5B9ull) ^ h;
    s_traceCount++;
}

static void trace_frame(uint32_t frame)
{
    extern int Render_ScrollSubpixel(uint16_t *fx, uint16_t *fy);
    uint16_t fx, fy;
    Render_ScrollSubpixel(&fx, &fy);
    /* "view": the pitch's draw origin and the presenter's sub-pixel shift, as published
     * by the last ScrollToCurrent -- the picture under the sprites. */
    if (trace_file())
        fprintf(s_trace, "frame %u sprites %u hash %016llx view %d %d %u %u%s\n", frame,
                s_traceCount, (unsigned long long)s_traceHash, Render_CameraX(), Render_CameraY(),
                fx, fy, s_traceClamped ? " clamped" : "");
    s_traceClamped = 0;
    s_traceCount = 0;
    s_traceHash = 0;
}

static void trace_sound(uint32_t frame, uint8_t kind, uint8_t channel, uint16_t id)
{
    if (trace_file())
        fprintf(s_trace, "frame %u sound %u %u %u\n", frame, kind, channel, id);
}

/* The same cull the live renderer applies before it queues a sprite
 * (BuildSpriteBandAndGeneralRenderStacks), so the trace names the sprites on screen. */
static int32_t in_render_view(const SpriteGraphics *g, int16_t sx, int16_t sy)
{
    return !(sx >= _screen_width || sy >= SCREEN_HEIGHT || sx <= -g->pixWidth || sy <= -g->nlines);
}

/* ======================================================================================
 * Recording
 * ====================================================================================== */

static struct {
    const char *path;           /* --record-replay, or NULL for LATEST.SRP */
    int32_t     autoSave;

    int32_t     on;             /* recording this match */
    int32_t     open;           /* a frame is being recorded */
    int32_t     headDone;       /* its head holds the state it was drawn with */
    uint32_t    frame;          /* the frame open, or the next one */
    uint32_t    frameStart;     /* its offset in frms */
    uint16_t    sprites;        /* its sprites so far */

    uint8_t    *frms;           /* the frame stream, big-endian, as it goes in the file */
    uint32_t    frmsLen, frmsCap;
    uint32_t   *offsets;        /* each committed frame's offset in frms */
    uint32_t    frameCount, offsetsCap;

    AudioEvent *audio;
    uint32_t    audioCount, audioCap;
    uint32_t    audioKeep;      /* sounds from before the first frame: a rewind to frame 0 keeps them */
    int32_t     audioKeepSet;
    Chapter    *chapters;
    uint32_t    chapterCount, chapterCap;

    uint8_t    *meta;           /* the match's identity, taken at kick-off */
    int32_t     suppressed;     /* ReplayRec_Suppress: nothing is recorded */
} s_rec;

void ReplayRec_Configure(const char *path, int32_t autoSave)
{
    s_rec.path = (path && path[0]) ? path : NULL;
    s_rec.autoSave = autoSave;
}

int32_t ReplayRec_Recording(void)
{
    return s_rec.on;
}

void ReplayRec_Suppress(int32_t on)
{
    s_rec.suppressed = on;
}

static void rec_on_sound(uint8_t kind, uint8_t channel, uint16_t id)
{
    AudioEvent *e;
    if (!s_rec.on)
        return;
    if (grow((void **)&s_rec.audio, &s_rec.audioCap, s_rec.audioCount + 1, sizeof *s_rec.audio))
        return;
    e = &s_rec.audio[s_rec.audioCount++];
    e->frame = s_rec.frame;             /* the frame open, or before the first: frame 0 */
    e->kind = kind;
    e->channel = channel;
    e->id = id;
    trace_sound(s_rec.frame, kind, channel, id);
}

static uint8_t *build_meta(void);

static void rec_free(void)
{
    free(s_rec.meta);
    s_rec.meta = NULL;
    free(s_rec.frms);
    free(s_rec.offsets);
    free(s_rec.audio);
    free(s_rec.chapters);
    s_rec.frms = NULL;
    s_rec.offsets = NULL;
    s_rec.audio = NULL;
    s_rec.chapters = NULL;
    s_rec.frmsLen = s_rec.frmsCap = 0;
    s_rec.frameCount = s_rec.offsetsCap = 0;
    s_rec.audioCount = s_rec.audioCap = 0;
    s_rec.audioKeep = 0;
    s_rec.audioKeepSet = 0;
    s_rec.chapterCount = s_rec.chapterCap = 0;
}

/* ---- a RESUMABLE recording --------------------------------------------------------
 * What the match was set up from, the state it kicked off in, and every frame's input and
 * hash (include/srp_writer.h: SIMV, SETP, STAT, INPT, HASH): enough to play the match
 * again exactly -- to re-simulate it on another machine, or to play on from any frame of
 * it -- not only to watch it. Only a netplay match gives them (the lobby's setup, GGPO's
 * frame-0 save, the confirmed inputs); a match played any other way leaves them empty and
 * its .SRP is exactly what it was.
 *
 * The setup arrives BEFORE ReplayRec_MatchStart (the lobby applies it, then the match
 * starts), so it waits as `pending` and MatchStart takes it. Inputs and hashes are kept by
 * frame number: a frame GGPO plays again replaces its own, and once confirmed it is the
 * frame as finally played. */
static struct {
    uint8_t  *pendingSetup;
    uint32_t  pendingSetupLen;
    uint8_t  *setup;
    uint32_t  setupLen;
    uint8_t  *kickoff;
    uint32_t  kickoffLen;
    uint64_t  layout;
    uint8_t   rules;
    uint16_t  simWidth;
    uint8_t  *inputs;           /* SRP_INPUT_BYTES a frame */
    uint32_t *hashes;
    uint32_t  cap;              /* frames both arrays hold */
    uint32_t  known;            /* frames with an input AND a hash, from 0 */
} s_resume;

static void resume_clear(void)
{
    free(s_resume.setup);
    free(s_resume.kickoff);
    free(s_resume.inputs);
    free(s_resume.hashes);
    s_resume.setup = s_resume.kickoff = s_resume.inputs = NULL;
    s_resume.hashes = NULL;
    s_resume.setupLen = s_resume.kickoffLen = s_resume.cap = s_resume.known = 0;
}

void ReplayRec_SetSetup(const uint8_t *setup, uint32_t len)
{
    free(s_resume.pendingSetup);
    s_resume.pendingSetup = len ? (uint8_t *)malloc(len) : NULL;
    s_resume.pendingSetupLen = s_resume.pendingSetup ? len : 0;
    if (s_resume.pendingSetup)
        memcpy(s_resume.pendingSetup, setup, len);
}

void ReplayRec_SetKickoff(const uint8_t *packet, uint32_t len, uint64_t layout, uint8_t rules,
                          uint16_t simWidth)
{
    free(s_resume.kickoff);
    s_resume.kickoff = len ? (uint8_t *)malloc(len) : NULL;
    s_resume.kickoffLen = s_resume.kickoff ? len : 0;
    if (s_resume.kickoff)
        memcpy(s_resume.kickoff, packet, len);
    s_resume.layout = layout;
    s_resume.rules = rules;
    s_resume.simWidth = simWidth;
}

static int32_t resume_grow(uint32_t frame)
{
    uint32_t cap;
    uint8_t *in;
    uint32_t *h;

    if (frame < s_resume.cap)
        return 0;
    cap = s_resume.cap ? s_resume.cap : 4096;
    while (cap <= frame)
        cap *= 2;
    /* The capacity moves only once BOTH have grown, so a failure leaves them agreeing. */
    in = (uint8_t *)realloc(s_resume.inputs, (size_t)cap * SRP_INPUT_BYTES);
    if (!in)
        return 1;
    s_resume.inputs = in;
    h = (uint32_t *)realloc(s_resume.hashes, (size_t)cap * sizeof(uint32_t));
    if (!h)
        return 1;
    s_resume.hashes = h;
    s_resume.cap = cap;
    return 0;
}

void ReplayRec_SetInput(uint32_t frame, const uint8_t *input)
{
    if (!s_resume.setup || resume_grow(frame))
        return;
    memcpy(s_resume.inputs + (size_t)frame * SRP_INPUT_BYTES, input, SRP_INPUT_BYTES);
}

void ReplayRec_SetHash(uint32_t frame, uint64_t hash)
{
    if (!s_resume.setup || resume_grow(frame))
        return;
    s_resume.hashes[frame] = (uint32_t)hash;
    if (frame >= s_resume.known)
        s_resume.known = frame + 1;
}

static void ckpt_join(void);    /* ReplayRec_Checkpoint's thread, below */

void ReplayRec_MatchStart(void)
{
    ckpt_join();
    ReplayRec_MatchEnd();       /* a match that never reached its end still gets written */

    /* This match's setup, if the lobby gave one; a previous match's is gone either way. */
    resume_clear();
    s_resume.setup = s_resume.pendingSetup;
    s_resume.setupLen = s_resume.pendingSetupLen;
    s_resume.pendingSetup = NULL;
    s_resume.pendingSetupLen = 0;

    /* A loaded replay is not this match's: before this, watching a .RPL and then playing a
     * friendly left the .RPL behind for the in-match highlights (H) to play. */
    ReplayV2_Unload();
    DosReplay_ClearRpl();

    /* Not training, not the arcade cabinet, and not the replay rig's corpus runs -- a
     * test that wants a recording says so (--record-replay). */
    if (_trainingGame != 0 || _gArcadeMode || (ReplayTest_Armed() && !s_rec.path) || s_rec.suppressed)
        return;

    rec_free();
    s_rec.on = 1;
    s_rec.open = 0;
    s_rec.frame = 0;
    /* The teams, competition and PITCH as the match was set up. At the end they are not
     * the match's any more -- SetPitchTypeAndNumber runs again for what comes next, and a
     * replay written with that value played on a different pitch than the match did. */
    s_rec.meta = build_meta();
    Audio_SetSfxObserver(rec_on_sound);
}

void ReplayRec_SetFrame(uint32_t frame)
{
    uint32_t i;
    if (!s_rec.on)
        return;
    s_rec.open = 0;
    /* What the match started before its first frame (the PC sounds' MATCH_START,
     * dos_sound.h) is not started again when frame 0 is played again. */
    if (!s_rec.audioKeepSet) {
        s_rec.audioKeepSet = 1;
        s_rec.audioKeep = s_rec.audioCount;
    }
    if (frame < s_rec.frameCount) {
        /* Played again: this frame and everything after it are re-recorded. */
        s_rec.frmsLen = s_rec.offsets[frame];
        s_rec.frameCount = frame;
        while (s_rec.audioCount > s_rec.audioKeep && s_rec.audio[s_rec.audioCount - 1].frame >= frame)
            s_rec.audioCount--;
        for (i = s_rec.chapterCount; i > 0 && s_rec.chapters[i - 1].frame >= frame; i--)
            ;
        s_rec.chapterCount = i;
    }
    /* A step that played no recorded frame (the statistics overlay held one) leaves a gap:
     * fill it with the last frame, which is what the screen showed, so a frame's number
     * stays its index -- the rewind above depends on it. */
    while (s_rec.frameCount > 0 && s_rec.frameCount < frame) {
        uint32_t from = s_rec.offsets[s_rec.frameCount - 1];
        uint32_t len = s_rec.frmsLen - from;
        if (grow((void **)&s_rec.frms, &s_rec.frmsCap, s_rec.frmsLen + len, 1) ||
            grow((void **)&s_rec.offsets, &s_rec.offsetsCap, s_rec.frameCount + 1, sizeof(uint32_t)))
            break;
        memcpy(s_rec.frms + s_rec.frmsLen, s_rec.frms + from, len);
        s_rec.offsets[s_rec.frameCount++] = s_rec.frmsLen;
        s_rec.frmsLen += len;
    }
    s_rec.frame = frame;
}

void ReplayRec_FrameBegin(void)
{
    if (!s_rec.on)
        return;
    if (s_rec.open)
        s_rec.frmsLen = s_rec.frameStart;       /* a frame that never finished */
    if (grow((void **)&s_rec.frms, &s_rec.frmsCap,
             s_rec.frmsLen + FRAME_HEAD_BYTES + MAX_FRAME_SPRITES * SPRITE_BYTES, 1)) {
        s_rec.on = 0;
        Audio_SetSfxObserver(NULL);
        fprintf(stderr, "[replay] out of memory: this match will not be recorded\n");
        return;
    }
    s_rec.frameStart = s_rec.frmsLen;
    memset(s_rec.frms + s_rec.frmsLen, 0, FRAME_HEAD_BYTES);    /* filled by write_head */
    s_rec.frmsLen += FRAME_HEAD_BYTES;
    s_rec.sprites = 0;
    s_rec.open = 1;
    s_rec.headDone = 0;
}

/* The frame's state AS DRAWN, so taken with its sprites rather than at FrameBegin (where
 * the legacy recorder takes it): the camera can move in between -- the end-of-match cut
 * does -- and DrawAnimatedPatterns sets this frame's crowd state, so both would be a
 * frame stale. */
static void write_head(void)
{
    uint8_t *h = s_rec.frms + s_rec.frameStart;
    if (s_rec.headDone)
        return;
    /* The camera in full 16.16. A wide display's draw origin scales the fraction too
     * (Render_CameraX16), so the integer part alone put playback a pixel out at 484 wide
     * on some frames. */
    BE_WRITE_U32(h + 2, (uint32_t)_cameraX.raw);
    BE_WRITE_U32(h + 6, (uint32_t)_cameraY.raw);
    BE_WRITE_U16(h + 10, _team1TotalGoals);
    BE_WRITE_U16(h + 12, _team2TotalGoals);
    BE_WRITE_U16(h + 14, _animPatternsState);
    BE_WRITE_U16(h + 16, (uint16_t)(_gameTime[1] * 100 + _gameTime[2] * 10 + _gameTime[3]));
    /* The animated ad's line, plus one: 0 is a recording from before it was kept. The
     * ad is a change to the sprite GRAPHICS, not a position (Adverts_ApplyStripLine). */
    {
        extern uint16_t _currentAnimatedAdLine;
        BE_WRITE_U16(h + 18, (uint16_t)(_currentAnimatedAdLine + 1));
    }
    s_rec.headDone = 1;
}

void ReplayRec_CaptureSprites(void)
{
    uint16_t i, n;
    if (!s_rec.on || !s_rec.open)
        return;
    write_head();
    n = _numSpritesToRender;
    if (n > MAX_FRAME_SPRITES)
        n = MAX_FRAME_SPRITES;
    /* In the order they are drawn -- index 0 first, at the back -- and every one, on
     * screen or not: a wider display than this one shows more of them. */
    for (i = 0; i < n; i++) {
        const Sprite *s = _sortedSprites[i];
        const SpriteGraphics *g;
        uint8_t *p;
        if (!s || s->imageIndex < 0 || s->z.s.i >= HUD_Z_MIN)
            continue;
        g = graphics((uint16_t)s->imageIndex);
        if (Sprite_PastWalkOffEdge(s, g))
            continue;               /* (walked off past the original's screen: not drawn either) */
        p = s_rec.frms + s_rec.frmsLen;
        BE_WRITE_U16(p, (uint16_t)s->imageIndex);
        BE_WRITE_U16(p + 2, (uint16_t)(s->x.s.i - g->centerX));
        BE_WRITE_U16(p + 4, (uint16_t)(s->y.s.i - g->centerY - s->z.s.i));
        s_rec.frmsLen += SPRITE_BYTES;
        s_rec.sprites++;
    }
}

/* The half the last half chapter named -- read back from the chapters, so a netplay
 * rewind that dropped some cannot leave it stale. */
static uint16_t last_half(void)
{
    uint32_t i;
    for (i = s_rec.chapterCount; i > 0; i--)
        if (s_rec.chapters[i - 1].kind == CHAPTER_HALF)
            return s_rec.chapters[i - 1].value;
    return 0xFFFF;
}

static void add_chapter(uint8_t kind, uint8_t team, uint16_t value)
{
    Chapter *c;
    if (grow((void **)&s_rec.chapters, &s_rec.chapterCap, s_rec.chapterCount + 1, sizeof *c))
        return;
    c = &s_rec.chapters[s_rec.chapterCount++];
    c->frame = s_rec.frame;
    c->kind = kind;
    c->team = team;
    c->value = value;
}

void ReplayRec_FrameEnd(void)
{
    const uint8_t *h;
    if (!s_rec.on || !s_rec.open)
        return;
    if (grow((void **)&s_rec.offsets, &s_rec.offsetsCap, s_rec.frameCount + 1, sizeof(uint32_t))) {
        s_rec.frmsLen = s_rec.frameStart;
        s_rec.open = 0;
        return;
    }
    write_head();
    BE_WRITE_U16(s_rec.frms + s_rec.frameStart, s_rec.sprites);
    h = s_rec.frms + s_rec.frameStart;

    /* Chapters: the start of each half (and of extra time), and every goal. */
    if (s_rec.frameCount == 0 || _halfNumber != last_half()) {
        add_chapter(CHAPTER_HALF, 0, _halfNumber);
    }
    if (s_rec.frameCount > 0) {
        const uint8_t *prev = s_rec.frms + s_rec.offsets[s_rec.frameCount - 1];
        if (BE_READ_U16(h + 10) > BE_READ_U16(prev + 10))
            add_chapter(CHAPTER_GOAL, 1, BE_READ_U16(h + 10));
        if (BE_READ_U16(h + 12) > BE_READ_U16(prev + 12))
            add_chapter(CHAPTER_GOAL, 2, BE_READ_U16(h + 12));
    }

    s_rec.offsets[s_rec.frameCount++] = s_rec.frameStart;
    s_rec.open = 0;
    trace_frame(s_rec.frame);
    s_rec.frame++;
}

/* sprites.c: a world sprite the live match actually drew (the trace only). */
void ReplayTrace_LiveSprite(int16_t ordinal, int16_t x, int16_t y)
{
    if (s_rec.on && s_rec.open && trace_file())
        trace_sprite(ordinal, x, y);
}

/* ---- writing ----------------------------------------------------------------------------- */

static void put_team(uint8_t *out, const TeamGame *team)
{
    const uint8_t *src = (const uint8_t *)team;
    int32_t i;
    for (i = 0; i < TEAM_HDR_INT16S; i++) {
        int16_t v;
        memcpy(&v, src + 2 * i, 2);
        BE_WRITE_U16(out + 2 * i, (uint16_t)v);
    }
    memcpy(out + 2 * TEAM_HDR_INT16S, src + 2 * TEAM_HDR_INT16S, TEAM_BYTES - 2 * TEAM_HDR_INT16S);
}

static uint8_t *build_meta(void)
{
    uint8_t *m = (uint8_t *)calloc(1, META_BYTES);
    uint8_t *q = m;
    if (!m)
        return NULL;
    BE_WRITE_U16(q, 1);                                           q += 2;  /* meta version */
    BE_WRITE_U16(q, (uint16_t)_screen_width);                     q += 2;
    /* 8 the DOS view (320x200), 16 "the view is recorded" -- a recording made before the
     * view was kept says neither, and the player works it out (recorded_view). */
    BE_WRITE_U16(q, (uint16_t)((_gDosGameplay ? 1 : 0) | (_gDosGraphics ? 2 : 0) |
                               (_gArcadeMode ? 4 : 0) | (_gDosView ? 8 : 0) | 16));   q += 2;
    /* The height the match was SIMULATED in (display.h _sim_view_height): 200 in the DOS
     * view, 272 in the Amiga's, whatever the picture. Playback maps the recorded camera
     * onto its own picture from it, exactly as the live match did (camera.c
     * viewOriginY16). 0 in a file from before it was kept. */
    BE_WRITE_U16(q, (uint16_t)_sim_view_height);                  q += 2;
    BE_WRITE_U32(q, (uint32_t)time(NULL));                        q += 4;
    snprintf((char *)q, 32, "%s", Build_Version());               q += 32;
    q += 4;     /* the final score, patched in at the end of the match */
    BE_WRITE_U16(q, _pitchNumberAndType);                         q += 2;
    BE_WRITE_U16(q, _gameMaxSubstitutes);                         q += 2;
    BE_WRITE_U16(q, 0);                                           q += 2;
    memcpy(q, _gameName, 40);                                     q += 40;
    memcpy(q, _gameRound, 40);                                    q += 40;
    put_team(q, &_topTeamInGame);                                 q += TEAM_BYTES;
    put_team(q, &_bottomTeamInGame);
    return m;
}

/* A name with a directory in it is used as it is; a bare name goes to the save folder,
 * where the highlights menu lists it. */
static void file_path(const char *name, char *out, size_t cap)
{
    if (strchr(name, '/') || strchr(name, '\\'))
        snprintf(out, cap, "%s", name);
    else
        snprintf(out, cap, "%s/%s", swos_save_dir(), name);
}

static int32_t write_file(const char *name, const uint8_t *buf, uint32_t len)
{
    char path[1024];
    FILE *f;
    size_t put;
    file_path(name, path, sizeof path);
    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "[replay] cannot write %s\n", path);
        return 1;
    }
    put = fwrite(buf, 1, len, f);
    fclose(f);
    if (put != len) {
        fprintf(stderr, "[replay] short write to %s\n", path);
        return 1;
    }
    swos_save_commit();          /* the save mount, on the Switch (swos_io.h) */
    fprintf(stderr, "[replay] wrote %s (%u bytes)\n", path, len);
    return 0;
}

static char s_playerNames[2][64];  /* who played: ReplayRec_SetPlayers, below */

/* The recording so far as the parts of a file (include/srp_writer.h). */
static void content(SrpContent *c)
{
    memset(c, 0, sizeof *c);
    c->meta         = s_rec.meta;
    c->metaLen      = s_rec.meta ? META_BYTES : 0;
    c->frms         = s_rec.frms;
    c->frmsLen      = s_rec.frmsLen;
    c->offsets      = s_rec.offsets;
    c->frameCount   = s_rec.frameCount;
    c->audio        = s_rec.audio;
    c->audioCount   = s_rec.audioCount;
    c->chapters     = s_rec.chapters;
    c->chapterCount = s_rec.chapterCount;
    c->tickRate     = (uint16_t)DosGameplay_TargetFps();
    if (s_playerNames[0][0] || s_playerNames[1][0]) {
        c->players[0] = s_playerNames[0];
        c->players[1] = s_playerNames[1];
    }
    c->quirks       = _gOriginalFireHoldLockout ? SWRP_QUIRK_ORIGINAL_FIRE_HOLD_LOCKOUT : 0;
    /* The resume parts whenever there are any (the recorder's stream sends them as they
     * come); write_recording keeps them only when every frame has its input and hash. */
    if (s_resume.setup && s_resume.kickoff) {
        c->setup      = s_resume.setup;
        c->setupLen   = s_resume.setupLen;
        c->kickoff    = s_resume.kickoff;
        c->kickoffLen = s_resume.kickoffLen;
        c->inputs     = s_resume.inputs;
        c->hashes     = s_resume.hashes;
        c->layout     = s_resume.layout;
        c->rules      = s_resume.rules;
        c->simWidth   = s_resume.simWidth;
    }
}

int32_t ReplayRec_Content(SrpContent *out)
{
    if (!s_rec.on || !s_rec.meta)
        return 1;
    content(out);
    return 0;
}

/* ---- what each player SAW: PRED and SEEN (include/srp_writer.h) ------------------------
 * A live match's .SRP keeps this player's half as it is written, from the ghost recorder
 * (netplay_ghost.h), and the other player's when it arrives over the lobby's link after
 * the match (src/match_prediction.c) -- then the files are written again with both, and
 * are the recorder's copy byte for byte. [netplay] prediction = false keeps neither. */
/* Who played (PLYR): an online match's two players, player 1 (the host) first, from the
 * lobby -- in the file from kick-off, so a recording names its players however the match
 * ends. Kept until the match that set them is written. */
void ReplayRec_SetPlayers(const char *player1, const char *player2)
{
    snprintf(s_playerNames[0], sizeof s_playerNames[0], "%s", player1 ? player1 : "");
    snprintf(s_playerNames[1], sizeof s_playerNames[1], "%s", player2 ? player2 : "");
}

static int32_t s_predKept = 1;
static struct {
    int32_t       player;           /* this machine's half: 1 or 2; 0 when there is none */
    SrpPrediction own;
    uint8_t      *ownBuf;           /* own.pred, own.seen, then own.read: one allocation */
    char          written[2][64];   /* the files this match went to */
    int32_t       nWritten;
} s_pred;

void ReplayRec_SetPredictionKept(int32_t on) { s_predKept = on != 0; }

int32_t ReplayRec_PredictionKept(void)
{
    const char *e = getenv("SWOS_RECORD_PREDICTION");
    if (e && e[0])
        return !(e[0] == '0' || strcmp(e, "off") == 0 || strcmp(e, "false") == 0);
    return s_predKept;
}

static void pred_clear(void)
{
    free(s_pred.ownBuf);
    memset(&s_pred, 0, sizeof s_pred);
}

/* This player's half, kept (copied) for the rewrite once the other half arrives. */
static const SrpPrediction *pred_own(uint32_t frames)
{
#ifdef SWOS_NETPLAY
    SrpPrediction g;
    int32_t player = Ghost_Player();
    pred_clear();
    if (!s_resume.kickoff || !ReplayRec_PredictionKept() || player < 1 || player > 2 ||
        Ghost_Prediction(frames, &g) != 0)
        return NULL;
    s_pred.ownBuf = (uint8_t *)malloc((size_t)g.predFrames + g.seenLen +
                                      (size_t)g.readFrames * SRP_READ_BYTES + 1);
    if (!s_pred.ownBuf)
        return NULL;
    memcpy(s_pred.ownBuf, g.pred, g.predFrames);
    memcpy(s_pred.ownBuf + g.predFrames, g.seen, g.seenLen);
    s_pred.own.pred = s_pred.ownBuf;
    s_pred.own.predFrames = g.predFrames;
    s_pred.own.seen = s_pred.ownBuf + g.predFrames;
    s_pred.own.seenLen = g.seenLen;
    if (g.read) {
        uint8_t *at = s_pred.ownBuf + g.predFrames + g.seenLen;
        memcpy(at, g.read, (size_t)g.readFrames * SRP_READ_BYTES);
        s_pred.own.read = at;
        s_pred.own.readFrames = g.readFrames;
        s_pred.own.delay = g.delay;
        s_pred.own.balance = g.balance;
    }
    s_pred.player = player;
    return &s_pred.own;
#else
    (void)frames;
    return NULL;
#endif
}

static uint8_t *read_file(const char *name, uint32_t *len);

int32_t ReplayRec_AddPeerPrediction(const SrpPrediction *peer)
{
    const SrpPrediction *half[2];
    int32_t i, rc = 0;
    if (!s_pred.player || !s_pred.nWritten || !peer)
        return 1;
    half[s_pred.player - 1] = &s_pred.own;
    half[2 - s_pred.player] = peer;
    for (i = 0; i < s_pred.nWritten; i++) {
        uint32_t len = 0, outLen = 0;
        uint8_t *file = read_file(s_pred.written[i], &len), *out;
        out = file ? Srp_WithPrediction(file, len, half[0], half[1], &outLen) : NULL;
        if (!out || write_file(s_pred.written[i], out, outLen) != 0)
            rc = 1;
        free(file);
        free(out);
    }
    if (!rc)
        fprintf(stderr, "[replay] %s now has both players' prediction data\n", s_pred.written[0]);
    s_pred.nWritten = 0;            /* once */
    return rc;
}

static int32_t write_recording(void)
{
    SrpContent c;
    uint8_t *file;
    uint32_t total = 0;
    int32_t rc;
    const SrpPrediction *own;

    if (!s_rec.meta) {
        fprintf(stderr, "[replay] out of memory: the match could not be saved\n");
        return 1;
    }
    content(&c);
    if (c.setup && s_resume.known < s_rec.frameCount) {
        fprintf(stderr, "[replay] %u of %u frames have their input: this replay will not be "
                        "resumable\n", s_resume.known, s_rec.frameCount);
        c.setup = NULL;
    }
    own = pred_own(c.frameCount);
    if (own) {
        c.prediction[s_pred.player - 1] = own;
#ifdef SWOS_NETPLAY
        c.prediction[2 - s_pred.player] = MatchPrediction_Peer();   /* here already, or NULL */
#endif
    }
    file = Srp_Build(&c, &total);
    if (!file) {
        fprintf(stderr, "[replay] out of memory: the match could not be saved\n");
        return 1;
    }
    rc = write_file(s_rec.path ? s_rec.path : REPLAY_V2_LATEST, file, total);
    if (!rc && own)
        snprintf(s_pred.written[s_pred.nWritten++], sizeof s_pred.written[0], "%s",
                 s_rec.path ? s_rec.path : REPLAY_V2_LATEST);
    if (!rc && s_rec.autoSave && !s_rec.path) {
        /* A dated copy: MMDDHHMM.SRP, eight characters as the file menus want. */
        char name[32];
        time_t t = time(NULL);
        struct tm *tm = localtime(&t);
        if (tm) {
            snprintf(name, sizeof name, "%02d%02d%02d%02d.SRP", tm->tm_mon + 1,
                     tm->tm_mday, tm->tm_hour, tm->tm_min);
            if (write_file(name, file, total) == 0 && own)
                snprintf(s_pred.written[s_pred.nWritten++], sizeof s_pred.written[0], "%s", name);
        }
    }
#ifdef SWOS_NETPLAY
    /* Ours to the other player; theirs, when it comes, is added to these files -- unless
     * it was here first and they have it already. */
    if (!rc && own) {
        MatchPrediction_Offer(s_pred.player, own);
        if (c.prediction[2 - s_pred.player])
            s_pred.nWritten = 0;
    }
#endif
    free(file);
    if (rc)
        fprintf(stderr, "[replay] the match could not be saved\n");
    return rc;
}

/* A copy of the match SO FAR, written while it is still being played: the final whistle
 * is where players close the game, and until 2026-10-12 a game that went without ending
 * the match -- closed, crashed, the power -- had saved nothing: LATEST.SRP was still the
 * match before. netplay_match.c calls this once, when the final whistle has gone and every
 * frame up to it is confirmed. Only the frames that are final go in (their input and hash
 * known), so the file plays again exactly; the real end writes over it, with the other
 * player's half of PRED/SEEN and the score.
 *
 * The file is built on a thread, from a COPY of everything it needs. Building it in the
 * frame took 155 ms on a 20,000-frame match: that player's game stood still for eight
 * frames while the other's ran on -- past the end of the match, when it happened there,
 * so the host's file had three frames the recorder's did not. Nothing here touches the
 * match or the recording being kept. */
typedef struct {
    SrpContent    c;
    SrpPrediction own;
    char          names[2][64];
    char          path[512];
    void         *parts[12];
    int32_t       nParts;
} Checkpoint;

static SDL_Thread *s_ckptThread;
static SDL_atomic_t s_ckptDone;      /* that thread has finished: joining it costs nothing */

static void *ckpt_dup(Checkpoint *k, const void *p, size_t n)
{
    void *q;
    if (!p || !n || k->nParts >= (int32_t)(sizeof k->parts / sizeof k->parts[0]))
        return NULL;
    q = malloc(n);
    if (q) {
        memcpy(q, p, n);
        k->parts[k->nParts++] = q;
    }
    return q;
}

static void ckpt_free(Checkpoint *k)
{
    int32_t i;
    for (i = 0; i < k->nParts; i++)
        free(k->parts[i]);
    free(k);
}

static int ckpt_run(void *arg)
{
    Checkpoint *k = (Checkpoint *)arg;
    uint32_t total = 0;
    uint8_t *file = Srp_Build(&k->c, &total);
    if (file) {
        if (write_file(k->path, file, total) == 0)
            fprintf(stderr, "[replay] the match so far is saved (%u frames, %u bytes): closing "
                            "the game now loses nothing\n", k->c.frameCount, total);
        free(file);
    }
    ckpt_free(k);
    SDL_AtomicSet(&s_ckptDone, 1);
    return 0;
}

/* Before the recording is written for good, or a new one starts: the copy first. */
static void ckpt_join(void)
{
    if (s_ckptThread) {
        SDL_WaitThread(s_ckptThread, NULL);
        s_ckptThread = NULL;
    }
}

void ReplayRec_Checkpoint(void)
{
    SrpContent c;
    Checkpoint *k;
    const SrpPrediction *own;
    uint32_t keep;

    if (!s_rec.on || !s_rec.meta || s_rec.frameCount == 0)
        return;
    /* The copy before this one (half time's, at the final whistle): done long ago, so
     * this join is free. Still writing -- two whistles a breath apart -- and this one is
     * skipped rather than waited for: a wait is the stall the thread exists to avoid. */
    if (s_ckptThread) {
        if (!SDL_AtomicGet(&s_ckptDone))
            return;
        ckpt_join();
    }
    content(&c);
    keep = c.frameCount;
    if (c.setup && s_resume.known < keep)
        keep = s_resume.known;
    if (keep == 0)
        return;
    k = (Checkpoint *)calloc(1, sizeof *k);
    if (!k)
        return;
    k->c = c;
    k->c.frameCount = keep;
    k->c.frmsLen = keep < c.frameCount ? c.offsets[keep] : c.frmsLen;
    while (k->c.audioCount > 0 && c.audio[k->c.audioCount - 1].frame >= keep)
        k->c.audioCount--;
    while (k->c.chapterCount > 0 && c.chapters[k->c.chapterCount - 1].frame >= keep)
        k->c.chapterCount--;
    k->c.meta = (const uint8_t *)ckpt_dup(k, c.meta, c.metaLen);
    k->c.frms = (const uint8_t *)ckpt_dup(k, c.frms, k->c.frmsLen);
    k->c.offsets = (const uint32_t *)ckpt_dup(k, c.offsets, (size_t)keep * sizeof(uint32_t));
    k->c.audio = (const SrpAudioEvent *)ckpt_dup(k, c.audio, (size_t)k->c.audioCount * sizeof *c.audio);
    k->c.chapters = (const SrpChapter *)ckpt_dup(k, c.chapters, (size_t)k->c.chapterCount * sizeof *c.chapters);
    if (!k->c.meta || !k->c.frms || !k->c.offsets || (k->c.audioCount && !k->c.audio) ||
        (k->c.chapterCount && !k->c.chapters)) {
        ckpt_free(k);
        return;
    }
    /* The score so far, in the copy only. */
    BE_WRITE_U16((uint8_t *)k->c.meta + META_SCORE_OFF, _statsTeam1Goals);
    BE_WRITE_U16((uint8_t *)k->c.meta + META_SCORE_OFF + 2, _statsTeam2Goals);
    if (c.setup) {
        k->c.setup = (const uint8_t *)ckpt_dup(k, c.setup, c.setupLen);
        k->c.kickoff = (const uint8_t *)ckpt_dup(k, c.kickoff, c.kickoffLen);
        k->c.inputs = (const uint8_t *)ckpt_dup(k, c.inputs, (size_t)keep * SRP_INPUT_BYTES);
        k->c.hashes = (const uint32_t *)ckpt_dup(k, c.hashes, (size_t)keep * sizeof(uint32_t));
        if (!k->c.setup || !k->c.kickoff || !k->c.inputs || !k->c.hashes)
            k->c.setup = k->c.kickoff = NULL, k->c.inputs = NULL, k->c.hashes = NULL;   /* watchable, then */
    }
    if (c.players[0] || c.players[1]) {
        snprintf(k->names[0], sizeof k->names[0], "%s", c.players[0] ? c.players[0] : "");
        snprintf(k->names[1], sizeof k->names[1], "%s", c.players[1] ? c.players[1] : "");
        k->c.players[0] = k->names[0];
        k->c.players[1] = k->names[1];
    }
    own = pred_own(keep);
    if (own) {
        k->own = *own;
        k->own.pred = (const uint8_t *)ckpt_dup(k, own->pred, own->predFrames);
        k->own.seen = (const uint8_t *)ckpt_dup(k, own->seen, own->seenLen);
        k->own.read = own->read ? (const uint8_t *)ckpt_dup(k, own->read, (size_t)own->readFrames * SRP_READ_BYTES)
                                : NULL;
        if (!k->own.seen)
            k->own.seenLen = 0;
        if (!k->own.read)
            k->own.readFrames = 0;
        if (k->own.pred)
            k->c.prediction[s_pred.player - 1] = &k->own;
    }
    snprintf(k->path, sizeof k->path, "%s", s_rec.path ? s_rec.path : REPLAY_V2_LATEST);
    SDL_AtomicSet(&s_ckptDone, 0);
    s_ckptThread = SDL_CreateThread(ckpt_run, "srp-checkpoint", k);
    if (!s_ckptThread)
        ckpt_free(k);
}

void ReplayRec_MatchEnd(void)
{
    ckpt_join();                /* the copy of the match so far, if it is still being written */
    if (!s_rec.on)
        return;
    s_rec.on = 0;
    s_rec.open = 0;
    Audio_SetSfxObserver(NULL);
    if (s_rec.meta) {
        BE_WRITE_U16(s_rec.meta + META_SCORE_OFF, _statsTeam1Goals);
        BE_WRITE_U16(s_rec.meta + META_SCORE_OFF + 2, _statsTeam2Goals);
    }
    if (s_rec.frameCount > 0) {
        fprintf(stderr, "[replay] recorded %u frames, %u sounds, %u chapters\n",
                s_rec.frameCount, s_rec.audioCount, s_rec.chapterCount);
        write_recording();
    }
    rec_free();
    resume_clear();
    s_playerNames[0][0] = s_playerNames[1][0] = '\0';     /* this match's, now written */
    if (s_trace)
        fflush(s_trace);
}

/* ======================================================================================
 * Playback
 * ====================================================================================== */

static struct {
    int32_t     loaded;
    uint16_t    tickRate, quirks, flags;
    uint32_t    frameCount;
    uint8_t    *frms;           /* inflated frame stream */
    uint32_t    frmsLen;
    uint32_t   *offsets;
    AudioEvent *audio;
    uint32_t    audioCount;
    uint32_t    cursor, audioCursor;
    /* What each player saw (PRED, SEEN), when the recording has it. */
    uint8_t    *pred;           /* 2 a frame, SRP_PRED_* plus the PLAY_PRED_* found on load */
    uint8_t    *read;           /* READ's frames: 2 * SRP_READ_BYTES a frame; NULL without the chunk */
    uint8_t     readDelay, readHead;    /* its input delay and SRP_READ_HEAD_* */
    uint8_t    *seen;           /* SEEN's records */
    uint32_t    seenLen;
    uint32_t   *seenAt;         /* frameCount + 1: where frame f's records start in `seen` */
    int32_t     dosView;        /* the view it was played in: 1 DOS, 0 Amiga, -1 not known */
    char        names[2][64];   /* PLYR: who played, "" when the file does not say */
    uint16_t    simHeight;      /* META: the view height it was simulated in, 0 not known */
} s_play;

/* Worked out on load, never in a file: this frame was DRAWN wrong for that player -- one
 * of the guesses it was drawn on was not the input that came, or SEEN has it somewhere
 * else. */
#define PLAY_PRED_WRONG     0x40u
#define PLAY_PRED_SEEN      0x80u

void ReplayV2_Unload(void)
{
    Audio_SetSfxObserver(NULL);         /* the playback trace's, if it was watching */
    free(s_play.frms);
    free(s_play.offsets);
    free(s_play.audio);
    free(s_play.pred);
    free(s_play.read);
    free(s_play.seen);
    free(s_play.seenAt);
    memset(&s_play, 0, sizeof s_play);
}

int32_t ReplayV2_Loaded(void)   { return s_play.loaded; }
int32_t ReplayV2_HasAudio(void) { return s_play.loaded && (s_play.flags & SWRP_FLAG_AUDIO); }

static uint8_t *read_file(const char *name, uint32_t *len)
{
    char path[1024];
    FILE *f;
    long n;
    uint8_t *buf;
    file_path(name, path, sizeof path);
    f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= SWRP_HEADER_BYTES || n > 64L * 1024 * 1024) {
        fclose(f);
        return NULL;
    }
    buf = (uint8_t *)malloc((size_t)n);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *len = (uint32_t)n;
    return buf;
}

/* The chunk's bytes, inflated if they were deflated. NULL if absent or broken. */
static uint8_t *chunk_get(const uint8_t *file, uint32_t fileLen, uint32_t fourcc, uint32_t *outLen)
{
    uint16_t n = BE_READ_U16(file + 20), i;
    if (SWRP_HEADER_BYTES + (uint32_t)n * SWRP_DIR_BYTES > fileLen)
        return NULL;
    for (i = 0; i < n; i++) {
        const uint8_t *d = file + SWRP_HEADER_BYTES + i * SWRP_DIR_BYTES;
        uint32_t off = BE_READ_U32(d + 4), stored = BE_READ_U32(d + 8), raw = BE_READ_U32(d + 12);
        uint8_t *out;
        if (BE_READ_U32(d) != fourcc)
            continue;
        if (off > fileLen || stored > fileLen - off || raw > 256u * 1024 * 1024)
            return NULL;
        out = (uint8_t *)malloc(raw ? raw : 1);
        if (!out)
            return NULL;
        if (stored == raw) {
            memcpy(out, file + off, raw);
        } else if (tinfl_decompress_mem_to_mem(out, raw, file + off, stored, 0) != raw) {
            free(out);
            return NULL;
        }
        *outLen = raw;
        return out;
    }
    return NULL;
}

/* A resumable recording's parts (replay_v2.h ReplayResume). 0, or 1 no such .SRP, 2 a
 * replay that was only recorded to be watched, 3 parts that do not add up. */
int32_t ReplayV2_LoadResume(const char *name, ReplayResume *out)
{
    uint32_t len = 0, simvLen = 0, inLen = 0, hashLen = 0, f;
    uint8_t *file, *simv = NULL, *hash = NULL;
    int32_t rc = 0;

    memset(out, 0, sizeof *out);
    file = read_file(name, &len);
    if (!file || memcmp(file, "SWRP", 4) != 0) {
        free(file);
        return 1;
    }
    simv = chunk_get(file, len, CHUNK_SIMV, &simvLen);
    out->setup = chunk_get(file, len, CHUNK_SETP, &out->setupLen);
    out->kickoff = chunk_get(file, len, CHUNK_STAT, &out->kickoffLen);
    out->inputs = chunk_get(file, len, CHUNK_INPT, &inLen);
    hash = chunk_get(file, len, CHUNK_HASH, &hashLen);
    out->frames = BE_READ_U32(file + 16);
    free(file);
    if (!simv || !out->setup || !out->kickoff || !out->inputs || !hash) {
        rc = 2;
    } else if (simvLen < SRP_SIMV_BYTES || BE_READ_U16(simv) != SRP_SIMV_VERSION ||
               out->frames == 0 || out->frames > 0x1000000u ||   /* the sizes below cannot wrap */
               inLen != out->frames * SRP_INPUT_BYTES || hashLen != out->frames * SRP_HASH_BYTES) {
        rc = 3;
    } else {
        out->rules = simv[2];
        out->layout = ((uint64_t)BE_READ_U32(simv + 4) << 32) | BE_READ_U32(simv + 8);
        out->simWidth = BE_READ_U16(simv + 12);
        out->hashes = (uint32_t *)malloc((size_t)out->frames * sizeof(uint32_t) + 1);
        if (!out->hashes)
            rc = 3;
        else
            for (f = 0; f < out->frames; f++)
                out->hashes[f] = BE_READ_U32(hash + f * SRP_HASH_BYTES);
    }
    free(simv);
    free(hash);
    if (rc)
        ReplayV2_FreeResume(out);
    return rc;
}

/* What each player's game knew frame by frame, for a re-simulation of what each SAW
 * (NetplayMatch_SetResimViews): PRED's two bytes a frame, and SEEN whole. 0 when the
 * recording has PRED for `frames` frames; the caller frees both. */
int32_t ReplayV2_LoadPrediction(const char *name, uint32_t frames, uint8_t **pred, uint8_t **seen,
                                uint32_t *seenLen)
{
    uint32_t len = 0, predLen = 0;
    uint8_t *file, *chunk;

    *pred = *seen = NULL;
    *seenLen = 0;
    file = read_file(name, &len);
    if (!file || memcmp(file, "SWRP", 4) != 0) {
        free(file);
        return 1;
    }
    chunk = chunk_get(file, len, CHUNK_PRED, &predLen);
    if (chunk && predLen >= SRP_PRED_HEAD + 2 * frames && BE_READ_U16(chunk) == SRP_PRED_VERSION &&
        BE_READ_U32(chunk + 4) == frames && (*pred = (uint8_t *)malloc(2 * (size_t)frames + 1)) != NULL)
        memcpy(*pred, chunk + SRP_PRED_HEAD, 2 * (size_t)frames);
    free(chunk);
    *seen = chunk_get(file, len, CHUNK_SEEN, seenLen);
    if (*seen && (*seenLen < SRP_SEEN_HEAD || BE_READ_U16(*seen) != SRP_SEEN_VERSION)) {
        free(*seen);
        *seen = NULL;
        *seenLen = 0;
    }
    free(file);
    return *pred ? 0 : 2;
}

void ReplayV2_FreeResume(ReplayResume *r)
{
    free(r->setup);
    free(r->kickoff);
    free(r->inputs);
    free(r->hashes);
    memset(r, 0, sizeof *r);
}

/* Into the HIL header the highlights viewer reads (CopyTeamsAndHeader), as a .RPL load
 * leaves it: kit words big-endian, the rest of each team byte for byte. */
static void apply_meta(const uint8_t *m)
{
    const uint8_t *q = m + 2 + 2 + 2 + 2 + 4 + 32;
    _hilTeam1Goals     = BE_READ_U16(q);        q += 2;
    _hilTeam2Goals     = BE_READ_U16(q);        q += 2;
    _hilPitchNumber    = BE_READ_U16(q);        q += 2;
    _hilMaxSubstitutes = BE_READ_U16(q);        q += 2;
    q += 2;
    memcpy(_hilGameName, q, 40);                q += 40;
    memcpy(_hilGameRound, q, 40);               q += 40;
    BE_WRITE_U32(_hilFileBuffer, 0x0000000Cu);
    BE_WRITE_U32(_hilFileBuffer + 4, 192254u);
    memcpy(_hilFileBuffer + 8, q, TEAM_BYTES);
    memcpy(_hilFileBuffer + 8 + TEAM_BYTES, q + TEAM_BYTES, TEAM_BYTES);
    _hilNumGoals = 1;
}

/* The view the match was played in, which the replay is shown in (ReplayV2_UseRecordedView):
 * the DOS view draws 200 lines, and a DOS-view match shown 272 lines tall shows what that
 * player never saw -- players below the picture, and past the bottom of the stadium at
 * the bottom goal (20260929-205209-f526dc63.SRP, frame 7056). META says, from now on;
 * a netplay recording's SIMV said before that; older files are read from the camera,
 * which goes below Y 608 (880 - 272) only when the match was simulated 200 lines tall. */
static int32_t recorded_view(const uint8_t *meta, const uint8_t *file, uint32_t fileLen)
{
    uint16_t flags = BE_READ_U16(meta + 4);
    uint32_t simvLen = 0, f;
    uint8_t *simv;
    if (flags & 16)
        return (flags & 8) ? 1 : 0;
    simv = chunk_get(file, fileLen, CHUNK_SIMV, &simvLen);
    if (simv && simvLen >= SRP_SIMV_BYTES && (simv[2] & 0x40)) {     /* P2P_RULE_VIEW_KNOWN */
        int32_t dos = (simv[2] & 0x20) != 0;                         /* P2P_RULE_DOS_VIEW */
        free(simv);
        return dos;
    }
    free(simv);
    for (f = 0; f < s_play.frameCount; f++)
        if ((int16_t)(BE_READ_U32(s_play.frms + s_play.offsets[f] + 6) >> 16) > 880 - SCREEN_HEIGHT)
            return 1;
    return -1;
}

static int32_t s_ownDosView = -1;       /* the player's own, while a recording's is on */
static int32_t s_ownSimHeight = -1;     /* the simulation view height, while the recording's is on */
static int32_t s_setSimHeight;

/* After the viewer's MatchViewEnter: the camera is mapped onto this picture from the
 * height the match was simulated in, which is the recording's -- a replay rig match
 * simulated in the DOS view's 200 lines while drawing 272 of them, and playback must map
 * it the same way to draw what the live match drew. */
void ReplayV2_ApplyRecordedSimView(void)
{
    if (!s_play.loaded || !s_play.simHeight || s_play.simHeight > 272 ||
        s_play.simHeight == (uint16_t)_sim_view_height)
        return;
    s_ownSimHeight = _sim_view_height;
    _sim_view_height = (int16_t)s_play.simHeight;
    s_setSimHeight = _sim_view_height;
}

void ReplayV2_UseRecordedView(void)
{
    if (!s_play.loaded || s_play.dosView < 0 || s_play.dosView == (_gDosView != 0))
        return;
    s_ownDosView = _gDosView;
    _gDosView = s_play.dosView;
    fprintf(stderr, "[replay] shown in the %s view it was played in\n", _gDosView ? "DOS" : "Amiga");
}

void ReplayV2_RestoreView(void)
{
    /* Only if it is still ours: leaving the DOS view has already reset it otherwise. */
    if (s_ownSimHeight >= 0) {
        if (_sim_view_height == s_setSimHeight)
            _sim_view_height = (int16_t)s_ownSimHeight;
        s_ownSimHeight = -1;
    }
    if (s_ownDosView < 0)
        return;
    _gDosView = s_ownDosView;
    s_ownDosView = -1;
}

/* PRED and SEEN, if the file has them; a file without plays exactly as before. */
static void load_prediction(const uint8_t *file, uint32_t fileLen)
{
    uint32_t predLen = 0, seenLen = 0, inLen = 0, n = s_play.frameCount, f, at, i;
    uint8_t *pred = chunk_get(file, fileLen, CHUNK_PRED, &predLen);
    uint8_t *seen = chunk_get(file, fileLen, CHUNK_SEEN, &seenLen);
    uint8_t *in = chunk_get(file, fileLen, CHUNK_INPT, &inLen);

    if (pred && predLen >= SRP_PRED_HEAD + 2 * n && BE_READ_U16(pred) == SRP_PRED_VERSION &&
        BE_READ_U32(pred + 4) == n && (s_play.pred = (uint8_t *)malloc(2 * n + 1)) != NULL) {
        memcpy(s_play.pred, pred + SRP_PRED_HEAD, 2 * n);
        /* Drawn wrong: GGPO guessed the other player's input at f - lag for every frame
         * after it, and INPT has what it really was. */
        if (in && inLen == n * SRP_INPUT_BYTES) {
            for (f = 0; f < n; f++) {
                for (i = 0; i < 2; i++) {
                    uint8_t *b = &s_play.pred[2 * f + i];
                    uint32_t lag, k, other = (1 - i) * 2;
                    const uint8_t *guess;
                    static const uint8_t none[2];
                    if (*b == SRP_PRED_UNKNOWN)
                        continue;
                    lag = *b & SRP_PRED_LAG;
                    guess = lag <= f ? in + (f - lag) * SRP_INPUT_BYTES + other : none;
                    for (k = lag <= f ? f + 1 - lag : 0; lag && k <= f; k++)
                        if (memcmp(in + k * SRP_INPUT_BYTES + other, guess, 2) != 0) {
                            *b |= PLAY_PRED_WRONG;
                            break;
                        }
                }
            }
        }
    }
    if (seen && seenLen >= SRP_SEEN_HEAD && BE_READ_U16(seen) == SRP_SEEN_VERSION &&
        (s_play.seenAt = (uint32_t *)malloc((size_t)(n + 1) * sizeof(uint32_t))) != NULL) {
        /* Index it by frame, checking every record fits and they are in order. */
        s_play.seen = seen;
        s_play.seenLen = seenLen;
        seen = NULL;
        at = SRP_SEEN_HEAD;
        for (f = 0; f <= n; f++) {
            s_play.seenAt[f] = at;
            while (f < n && at + SRP_SEEN_REC_HEAD <= s_play.seenLen &&
                   BE_READ_U32(s_play.seen + at) == f) {
                uint32_t len = SRP_SEEN_REC_HEAD + (uint32_t)s_play.seen[at + 5] * SRP_SEEN_OBJ_BYTES;
                uint8_t who = s_play.seen[at + 4];
                if (at + len > s_play.seenLen)
                    break;
                if (s_play.pred && (who == 1 || who == 2) &&
                    s_play.pred[2 * f + who - 1] != SRP_PRED_UNKNOWN)
                    s_play.pred[2 * f + who - 1] |= PLAY_PRED_SEEN;
                at += len;
            }
        }
    }
    /* READ: what each player's game read, when, and the waits it was taking. */
    {
        uint32_t readLen = 0;
        uint8_t *read = chunk_get(file, fileLen, CHUNK_READ, &readLen);
        if (read && readLen >= SRP_READ_HEAD + 2 * SRP_READ_BYTES * n &&
            BE_READ_U16(read) == SRP_READ_VERSION && BE_READ_U32(read + 4) == n &&
            (s_play.read = (uint8_t *)malloc(2 * SRP_READ_BYTES * (size_t)n + 1)) != NULL) {
            memcpy(s_play.read, read + SRP_READ_HEAD, 2 * SRP_READ_BYTES * (size_t)n);
            s_play.readDelay = read[2];
            s_play.readHead = read[3];
        }
        free(read);
    }
    if (s_play.pred || s_play.seenAt)
        fprintf(stderr, "[replay] what each player saw: %s%s\n", s_play.pred ? "PRED" : "",
                s_play.seenAt ? (s_play.pred ? " and SEEN" : "SEEN") : "");
    /* SWOS_REPLAY_SCREEN=1|2: start on that player's screen (--view-replay has no page). */
    {
        const char *e = getenv("SWOS_REPLAY_SCREEN");
        if (e && *e)
            ReplayV2_PlayerPerspective(atoi(e));
    }
    free(pred);
    free(seen);
    free(in);
}

int32_t ReplayV2_Load(const uint8_t *name)
{
    uint32_t fileLen = 0, metaLen = 0, audioLen = 0, i, off;
    uint8_t *file = read_file((const char *)name, &fileLen);
    uint8_t *meta = NULL, *audio = NULL;
    const char *why = NULL;

    ReplayV2_Unload();
    if (!file) {
        fprintf(stderr, "[replay] cannot read %s\n", (const char *)name);
        return 1;
    }
    if (memcmp(file, "SWRP", 4) != 0)
        why = "not a SWRP replay";
    else if (BE_READ_U16(file + 4) > SWRP_VERSION)
        why = "made by a newer version";
    else if (BE_READ_U32(file + 24) !=
             (uint32_t)mz_crc32(MZ_CRC32_INIT, file + SWRP_HEADER_BYTES, fileLen - SWRP_HEADER_BYTES))
        why = "damaged (checksum)";
    if (!why) {
        s_play.flags      = BE_READ_U16(file + 6);
        s_play.tickRate   = BE_READ_U16(file + 8);
        s_play.quirks     = BE_READ_U16(file + 10);
        s_play.frameCount = BE_READ_U32(file + 16);
        meta = chunk_get(file, fileLen, CHUNK_META, &metaLen);
        s_play.frms = chunk_get(file, fileLen, CHUNK_FRMS, &s_play.frmsLen);
        if (!meta || metaLen < META_BYTES || !s_play.frms || s_play.frameCount == 0)
            why = "missing or damaged chunks";
    }
    if (!why) {
        /* Index the frames, checking every one fits. */
        s_play.offsets = (uint32_t *)malloc((size_t)s_play.frameCount * sizeof(uint32_t));
        off = 0;
        for (i = 0; s_play.offsets && i < s_play.frameCount; i++) {
            uint16_t sprites;
            if (off + FRAME_HEAD_BYTES > s_play.frmsLen)
                break;
            sprites = BE_READ_U16(s_play.frms + off);
            if (sprites > MAX_FRAME_SPRITES ||
                off + FRAME_HEAD_BYTES + (uint32_t)sprites * SPRITE_BYTES > s_play.frmsLen)
                break;
            s_play.offsets[i] = off;
            off += FRAME_HEAD_BYTES + (uint32_t)sprites * SPRITE_BYTES;
        }
        if (!s_play.offsets || i != s_play.frameCount)
            why = "the frame stream is damaged";
    }
    if (!why && (s_play.flags & SWRP_FLAG_AUDIO)) {
        audio = chunk_get(file, fileLen, CHUNK_AUDI, &audioLen);
        if (!audio && audioLen != 0)
            s_play.flags &= (uint16_t)~SWRP_FLAG_AUDIO;
        s_play.audioCount = audioLen / AUDIO_EVENT_BYTES;
        s_play.audio = (AudioEvent *)malloc((size_t)s_play.audioCount * sizeof(AudioEvent) + 1);
        for (i = 0; s_play.audio && audio && i < s_play.audioCount; i++) {
            const uint8_t *p = audio + i * AUDIO_EVENT_BYTES;
            s_play.audio[i].frame = BE_READ_U32(p);
            s_play.audio[i].kind = p[4];
            s_play.audio[i].channel = p[5];
            s_play.audio[i].id = BE_READ_U16(p + 6);
        }
    }
    free(audio);
    if (!why) {
        uint32_t plyrLen = 0, o = 2, k;
        uint8_t *plyr = chunk_get(file, fileLen, CHUNK_PLYR, &plyrLen);
        for (k = 0; plyr && k < 2 && o < plyrLen; k++) {
            uint32_t n = plyr[o++];
            if (o + n > plyrLen || n >= sizeof s_play.names[k])
                break;
            memcpy(s_play.names[k], plyr + o, n);
            s_play.names[k][n] = '\0';
            o += n;
        }
        free(plyr);
        load_prediction(file, fileLen);
        s_play.dosView = recorded_view(meta, file, fileLen);
        s_play.simHeight = BE_READ_U16(meta + 6);
    }

    if (why) {
        fprintf(stderr, "[replay] %s: %s\n", (const char *)name, why);
        free(meta);
        free(file);
        ReplayV2_Unload();
        return 1;
    }

    DosReplay_ClearRpl();
    apply_meta(meta);
    free(meta);
    free(file);
    _replayPlaybackFps = s_play.tickRate ? s_play.tickRate : 50;
    _replayRecordingIsWide = 0;
    _numScenesInHilFile = 1;
    s_play.loaded = 1;
    fprintf(stderr, "[replay] %s: %u frames at %u fps, %u sounds\n", (const char *)name,
            s_play.frameCount, s_play.tickRate, s_play.audioCount);
    return 0;
}

/* Every sound the game starts while a .SRP plays, traced where the recorder traces what
 * the match started: the two must agree. Watching only the events fired below would not
 * see one the VIEWER adds -- a random crowd chant over the recorded crowd, say. */
static void play_on_sound(uint8_t kind, uint8_t channel, uint16_t id)
{
    trace_sound(s_play.cursor, kind, channel, id);
}

void ReplayV2_StartScene(void)
{
    s_play.cursor = 0;
    s_play.audioCursor = 0;
    _replayState = 1;
    if (s_trace)
        Audio_SetSfxObserver(play_on_sound);
}

static void fire_sound(const AudioEvent *e)
{
    switch (e->kind) {
    case AUDIO_SFX_PRESET:
        AudioApplyPresetToChannels(e->id);
        break;
    case AUDIO_SFX_WAVE:
        AudioChannelQueueNextWaveIfEnabled(e->channel, e->id);
        break;
    case AUDIO_SFX_VOLUME:
        AudioSetChannelVolume(e->channel == 0xFF ? 0xFFFF : e->channel, e->id);
        break;
    case AUDIO_SFX_DOS:
        Audio_DosEvent(e->channel, e->id);
        break;
    default:
        break;
    }
}

/* ---- The player: a recording under a scrub bar (the playback page, wasm/player.html) ----
 *
 * --view-replay plays a recording once and exits. As the PLAYER (ReplayV2_PlayerMode) it
 * holds instead: the page seeks to any frame, pauses and steps, and the last frame stays
 * on screen. Every frame of FRMS has its own offset, so a seek is only the cursor --
 * plus the sound cursor, and the camera put straight where the match had it, as on
 * the first frame (_replayState 1). */
static struct {
    int32_t on, paused, seekTo, step;
    int32_t silenced;               /* the pause's silence is done */
} s_player = { 0, 0, -1, 0, 0 };

void ReplayV2_PlayerMode(int32_t on) { s_player.on = on != 0; }

EMSCRIPTEN_KEEPALIVE int32_t ReplayV2_PlayerFrames(void)   { return s_play.loaded ? (int32_t)s_play.frameCount : 0; }
EMSCRIPTEN_KEEPALIVE int32_t ReplayV2_PlayerTickRate(void) { return s_play.loaded ? (int32_t)s_play.tickRate : 0; }
EMSCRIPTEN_KEEPALIVE int32_t ReplayV2_PlayerPaused(void)   { return s_player.paused; }
/* The frame on screen: the one last drawn. */
EMSCRIPTEN_KEEPALIVE int32_t ReplayV2_PlayerFrame(void)
{
    return s_play.cursor > 0 ? (int32_t)s_play.cursor - 1 : 0;
}
EMSCRIPTEN_KEEPALIVE void ReplayV2_PlayerPause(int32_t paused) { s_player.paused = paused != 0; }
EMSCRIPTEN_KEEPALIVE void ReplayV2_PlayerSeek(int32_t frame)
{
    if (frame < 0)
        frame = 0;
    if (s_play.loaded && frame >= (int32_t)s_play.frameCount)
        frame = (int32_t)s_play.frameCount - 1;
    s_player.seekTo = frame;
}
/* One frame forward or back, then paused there. */
EMSCRIPTEN_KEEPALIVE void ReplayV2_PlayerStep(int32_t delta)
{
    s_player.paused = 1;
    ReplayV2_PlayerSeek(ReplayV2_PlayerFrame() + delta);
}

/* ---- what each player saw, drawn over the recording (PRED and SEEN) ----
 *
 * The overlay shows, for the chosen player(s), every object drawn somewhere other than
 * where the match finally had it: a line from where it really was to where that player's
 * screen showed it, a box on the drawn spot, and the drawn spot's trail over the frames
 * before -- so a rollback reads as a ghost that ran off and snapped back. Player 1 (the
 * host, the top team) in yellow, player 2 in white. Along the bottom-left, a bar per
 * player: its length the frame's lag, red when it drew something that did not happen. */
static int32_t s_overlay = 3;       /* bit 0 player 1's view, bit 1 player 2's */

EMSCRIPTEN_KEEPALIVE int32_t ReplayV2_PlayerPrediction(void)
{
    return (s_play.pred ? 1 : 0) | (s_play.seenAt ? 2 : 0);
}
/* The frame's two bytes -- player 1's in the low byte -- or -1 without PRED. */
EMSCRIPTEN_KEEPALIVE int32_t ReplayV2_PlayerPredAt(int32_t frame)
{
    if (!s_play.pred || frame < 0 || (uint32_t)frame >= s_play.frameCount)
        return -1;
    return s_play.pred[2 * frame] | (s_play.pred[2 * frame + 1] << 8);
}
/* READ (docs/REPLAY_FORMAT_V2.md): -1 without the chunk, else the input delay in the low
 * byte and SRP_READ_HEAD_* (who had the prediction balance on) in the next. */
EMSCRIPTEN_KEEPALIVE int32_t ReplayV2_PlayerReadInfo(void)
{
    return s_play.read ? s_play.readDelay | (s_play.readHead << 8) : -1;
}
/* One player's (1, 2) three READ bytes for a frame, packed: the controls as read, the
 * milliseconds since the frame before << 8, the wait flags (SRP_READ_BALANCE,
 * SRP_READ_TIMESYNC) << 16. -1 without them. */
EMSCRIPTEN_KEEPALIVE int32_t ReplayV2_PlayerReadAt(int32_t frame, int32_t player)
{
    const uint8_t *p;
    if (!s_play.read || frame < 0 || (uint32_t)frame >= s_play.frameCount || player < 1 || player > 2)
        return -1;
    p = s_play.read + ((size_t)frame * 2 + (uint32_t)(player - 1)) * SRP_READ_BYTES;
    if (p[0] == SRP_READ_UNKNOWN && p[1] == SRP_READ_UNKNOWN && p[2] == SRP_READ_UNKNOWN)
        return -1;
    return p[0] | (p[1] << 8) | (p[2] << 16);
}
/* Who played (PLYR): player 1's or 2's name, "" when the recording does not say. */
EMSCRIPTEN_KEEPALIVE const char *ReplayV2_PlayerName(int32_t player)
{
    return s_play.loaded && player >= 1 && player <= 2 ? s_play.names[player - 1] : "";
}
/* All of it, for the page's timeline: 2 bytes a frame, as ReplayV2_PlayerPredAt. */
EMSCRIPTEN_KEEPALIVE uint8_t *ReplayV2_PlayerPredData(void) { return s_play.pred; }
/* Which views the overlay draws (bit 0 player 1, bit 1 player 2); -1 only asks. */
EMSCRIPTEN_KEEPALIVE int32_t ReplayV2_PlayerOverlay(int32_t mode)
{
    if (mode >= 0)
        s_overlay = mode & 3;
    return s_overlay;
}

/* WHOSE SCREEN the picture is: 0 the match as finally played, 1 or 2 that player's
 * (docs/PLAYBACK.md, "Three screens"). A player's screen is the match with every man
 * and the ball that SEEN says he saw elsewhere drawn THERE -- the same sprite, moved.
 * -1 only asks. */
static int32_t s_perspective;
EMSCRIPTEN_KEEPALIVE int32_t ReplayV2_PlayerPerspective(int32_t who)
{
    if (who >= 0)
        s_perspective = who <= 2 ? who : 0;
    return s_perspective;
}

#define SPR_BALL_SHADOW_IMAGE 1069      /* ball_physics.c SPR_BALL_SHADOW */

/* How far each of the frame's sprites moves to show player `who`'s screen. A SEEN object
 * is where a man STANDS (the ball: where it is seen), and a recorded sprite is that less
 * its graphic's centre (ReplayRec_CaptureSprites), so the sprite an object is drawn with
 * is the one whose position plus its centre is the object's final position -- exactly,
 * not nearly. Two things on one spot: the ball is the smaller graphic, and two men who
 * stand on the same pixel (SWOS lets them) are taken in drawing order, so their two
 * pictures may be exchanged. The ball's shadow follows the ball. */
static void perspective_shifts(uint32_t f, int32_t who, const uint8_t *sp, uint16_t sprites,
                               int16_t (*shift)[2])
{
    uint32_t at, end;
    uint8_t used[MAX_FRAME_SPRITES];
    uint16_t i;

    memset(shift, 0, (size_t)sprites * sizeof *shift);
    if (!s_play.seenAt || who < 1 || who > 2 || f >= s_play.frameCount)
        return;
    memset(used, 0, sizeof used);
    at = s_play.seenAt[f];
    end = s_play.seenAt[f + 1];
    while (at < end) {
        const uint8_t *r = s_play.seen + at;
        uint32_t n = r[5], k;
        at += SRP_SEEN_REC_HEAD + n * SRP_SEEN_OBJ_BYTES;
        if (r[4] != who)
            continue;
        for (k = 0; k < n; k++) {
            const uint8_t *o = r + SRP_SEEN_REC_HEAD + k * SRP_SEEN_OBJ_BYTES;
            int16_t dx = (int16_t)BE_READ_U16(o + 1), dy = (int16_t)BE_READ_U16(o + 3);
            int16_t fx = (int16_t)BE_READ_U16(o + 5), fy = (int16_t)BE_READ_U16(o + 7);
            int32_t ball = o[0] == SRP_SEEN_BALL, best = -1, bestArea = 0;
            for (i = 0; i < sprites; i++) {
                const uint8_t *q = sp + (size_t)i * SPRITE_BYTES;
                uint16_t image = BE_READ_U16(q);
                const SpriteGraphics *g = graphics(image);
                int32_t area;
                if (used[i] || image == SPR_BALL_SHADOW_IMAGE || !g)
                    continue;
                if ((int16_t)((int16_t)BE_READ_U16(q + 2) + g->centerX) != fx ||
                    (int16_t)((int16_t)BE_READ_U16(q + 4) + g->centerY) != fy)
                    continue;
                area = (int32_t)g->pixWidth * g->nlines;
                /* The ball is the smallest thing standing there, a man the largest. */
                if (best < 0 || (ball ? area < bestArea : area > bestArea)) {
                    best = i;
                    bestArea = area;
                }
            }
            if (best < 0)
                continue;                   /* not drawn on this frame (off the pitch, hidden) */
            used[best] = 1;
            shift[best][0] = (int16_t)(dx - fx);
            shift[best][1] = (int16_t)(dy - fy);
            if (ball)
                for (i = 0; i < sprites; i++)
                    if (BE_READ_U16(sp + (size_t)i * SPRITE_BYTES) == SPR_BALL_SHADOW_IMAGE) {
                        shift[i][0] = shift[best][0];
                        shift[i][1] = shift[best][1];
                    }
        }
    }
}

static void plot(int32_t x, int32_t y, uint8_t colour)
{
    extern volatile uintptr_t _frontbufferBasePtr;
    uint8_t *fb = (uint8_t *)(uintptr_t)_frontbufferBasePtr;
    if (fb && x >= 0 && y >= 0 && x < _screen_width && y < _screen_height)
        fb[(size_t)y * FRONTBUFFER_ROW_STRIDE_BYTES + x] = colour;
}

static void line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t colour)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy, guard = 2048;
    for (;;) {
        int32_t e2;
        plot(x0, y0, colour);
        if ((x0 == x1 && y0 == y1) || --guard == 0)
            break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* Where object `obj` was drawn on frame `f` for player `who`, from SEEN: 1 found. */
static int32_t seen_drawn(uint32_t f, uint8_t who, uint8_t obj, int16_t *x, int16_t *y)
{
    uint32_t at = s_play.seenAt[f], end = s_play.seenAt[f + 1];
    while (at < end) {
        const uint8_t *r = s_play.seen + at;
        uint32_t n = r[5], i;
        if (r[4] == who)
            for (i = 0; i < n; i++) {
                const uint8_t *o = r + SRP_SEEN_REC_HEAD + i * SRP_SEEN_OBJ_BYTES;
                if (o[0] == obj) {
                    *x = (int16_t)BE_READ_U16(o + 1);
                    *y = (int16_t)BE_READ_U16(o + 3);
                    return 1;
                }
            }
        at += SRP_SEEN_REC_HEAD + n * SRP_SEEN_OBJ_BYTES;
    }
    return 0;
}

static void draw_prediction(uint32_t f, int16_t origin, int16_t camY)
{
    enum { TRAIL = 8 };
    uint8_t colour[2], red;
    uint32_t at, end;
    int32_t who;

    if (!s_player.on || !s_overlay || (!s_play.pred && !s_play.seenAt))
        return;
    colour[0] = Platform_NearestColour(255, 230, 0);
    colour[1] = Platform_NearestColour(255, 255, 255);  /* the palette has no cyan to speak of */
    red = Platform_NearestColour(255, 40, 40);
    if (s_play.seenAt) {
        at = s_play.seenAt[f];
        end = s_play.seenAt[f + 1];
        while (at < end) {
            const uint8_t *r = s_play.seen + at;
            uint32_t n = r[5], i;
            who = r[4];
            at += SRP_SEEN_REC_HEAD + n * SRP_SEEN_OBJ_BYTES;
            if (who < 1 || who > 2 || !(s_overlay & (1 << (who - 1))))
                continue;
            for (i = 0; i < n; i++) {
                const uint8_t *o = r + SRP_SEEN_REC_HEAD + i * SRP_SEEN_OBJ_BYTES;
                int32_t dx = (int16_t)BE_READ_U16(o + 1) - origin, dy = (int16_t)BE_READ_U16(o + 3) - camY;
                int32_t fx = (int16_t)BE_READ_U16(o + 5) - origin, fy = (int16_t)BE_READ_U16(o + 7) - camY;
                uint8_t c = colour[who - 1];
                int32_t k, px = dx, py = dy;
                /* The drawn spot's trail, back through the frames it was also misdrawn. */
                for (k = 1; k < TRAIL && (uint32_t)k <= f; k++) {
                    int16_t tx, ty;
                    if (!seen_drawn(f - (uint32_t)k, (uint8_t)who, o[0], &tx, &ty))
                        break;
                    line(px, py, tx - origin, ty - camY, c);
                    px = tx - origin;
                    py = ty - camY;
                }
                line(fx, fy, dx, dy, c);
                line(dx - 2, dy - 2, dx + 2, dy - 2, c);
                line(dx + 2, dy - 2, dx + 2, dy + 2, c);
                line(dx + 2, dy + 2, dx - 2, dy + 2, c);
                line(dx - 2, dy + 2, dx - 2, dy - 2, c);
                plot(fx, fy, red);
            }
        }
    }
    if (s_play.pred) {
        for (who = 1; who <= 2; who++) {
            uint8_t b = s_play.pred[2 * f + who - 1];
            int32_t y = _screen_height - 10 + (who - 1) * 4, len, x;
            if (!(s_overlay & (1 << (who - 1))) || b == SRP_PRED_UNKNOWN)
                continue;
            len = (b & SRP_PRED_LAG) * 6;
            for (x = 0; x < len; x++) {
                plot(4 + x, y, (b & PLAY_PRED_SEEN) ? red : colour[who - 1]);
                plot(4 + x, y + 1, (b & PLAY_PRED_SEEN) ? red : colour[who - 1]);
            }
            plot(2, y, colour[who - 1]);
            plot(2, y + 1, colour[who - 1]);
        }
    }
}

/* A recorded camera Y as this display can show it. The camera's limit depends on the
 * height of the view it was SIMULATED in (include/display.h: SIM_CAMERA_POS_MAX_Y = 880 -
 * viewH): a match played in the 200-line DOS view goes down to 664, and drawn 272 lines
 * tall that runs past the bottom of the stadium -- rows below the 880-tall pitch, drawn
 * from whatever lies after it (20260929-205209-f526dc63.SRP, frame 7056, and every
 * DOS-view recording at the bottom goal). Held at this screen's own limit it still shows
 * everything that player saw, since a taller picture reaching the same bottom covers it;
 * a recording simulated at this height never reaches it. A recording is normally shown
 * in the view it was played in (recorded_view), so this is for the ones that were not:
 * the replay rig simulates in the corpus's 200-line view while drawing 272, so its live
 * runs went past the pitch on those frames too -- the trace marks them "clamped" and
 * scripts/replay_v2_test.sh leaves them out of the comparison. */
static uint32_t shown_camera_y(uint32_t cy)
{
    /* The SIMULATION range: the picture is mapped onto it (Render_CameraY), so only a
     * recording simulated in a shorter view than this playback's needs holding -- a file
     * from before META kept its simulated height (ReplayV2_ApplyRecordedSimView). */
    int32_t max = 880 - _sim_view_height;
    if (max >= 0 && (int16_t)(cy >> 16) > max) {
        s_traceClamped = 1;
        return (uint32_t)max << 16;
    }
    return cy;
}

void ReplayV2_DrawFrame(void)
{
    const uint8_t *h, *sp;
    uint16_t sprites, i;
    int16_t origin, camY;
    int32_t fresh = 1;              /* a frame not drawn before: its sounds play */
    uint16_t adLine;
    int16_t shift[MAX_FRAME_SPRITES][2];
    extern int32_t Adverts_ApplyStripLine(uint16_t image, uint16_t line);

    if (!s_play.loaded)
        return;
    if (s_player.on) {
        /* No sound carries across a jump or a pause: a seek skips what would have
         * ended a looping effect, which then played on regardless of where the
         * recording is (AudioSDL_SilenceSfx). The recording's next events start
         * what should be heard from here. */
        extern void AudioSDL_SilenceSfx(void);
        if (s_player.seekTo >= 0 || (s_player.paused && !s_player.silenced)) {
            AudioSDL_SilenceSfx();
            s_player.silenced = s_player.paused;
        }
        if (!s_player.paused)
            s_player.silenced = 0;
        if (s_player.seekTo >= 0) {
            s_play.cursor = (uint32_t)s_player.seekTo;
            s_player.seekTo = -1;
            for (s_play.audioCursor = 0; s_play.audioCursor < s_play.audioCount &&
                 s_play.audio[s_play.audioCursor].frame < s_play.cursor; s_play.audioCursor++)
                ;
            _replayState = 1;
            fresh = 0;
        } else if (s_player.paused || s_play.cursor >= s_play.frameCount) {
            /* Hold: the frame on screen, drawn again. */
            if (s_play.cursor > 0)
                s_play.cursor--;
            fresh = 0;
        }
    }
    if (s_play.cursor >= s_play.frameCount) {
        _replayState = 0;
        return;
    }
    h = s_play.frms + s_play.offsets[s_play.cursor];

    if (_replayState == 1) {
        /* The first frame: the camera straight to where the match had it, both buffers. */
        int16_t x = (int16_t)(BE_READ_U32(h + 2) >> 16), y = (int16_t)(shown_camera_y(BE_READ_U32(h + 6)) >> 16);
        _replayState = 2;
        _savedCameraX = _cameraX.s.i;
        _savedCameraY = _cameraY.s.i;
        ResetAnimatedPatternsForBothTeams();
        SetCameraCoordinates(x, y);
        InitCameraMoveView(x, y);
        MirrorFrontbuffer();
        SwapBuffersAndCameraViewState();
        InitCameraMoveView(x, y);
        MirrorFrontbuffer();
    }

    /* The simulation camera, which this display's renderer maps onto its own width
     * exactly as a live match does. */
    {
        /* The fractions first: MoveView pans whole pixels, and its last ScrollToCurrent
         * draws the pitch at -- and publishes the presenter's sub-pixel shift from -- the
         * camera the match drew with. Set after, the pitch sat a pixel off the sprites at
         * a scaled width, and the scroll lost its sub-pixel smoothness. */
        uint32_t cx = BE_READ_U32(h + 2), cy = shown_camera_y(BE_READ_U32(h + 6));
        _cameraX.s.frac = (uint16_t)cx;
        _cameraY.s.frac = (uint16_t)cy;
        MoveView((int16_t)(cx >> 16), (int16_t)(cy >> 16));
    }
    _hilLeftTeamGoals  = BE_READ_U16(h + 10);
    _hilRightTeamGoals = BE_READ_U16(h + 12);
    _animPatternsState = BE_READ_U16(h + 14);
    DrawAnimatedPatterns();

    origin = Render_CameraX();
    camY = Render_CameraY();
    adLine = BE_READ_U16(h + 18);
    adLine = adLine ? (uint16_t)(adLine - 1) : 0;     /* older recordings: the first strip */
    sprites = BE_READ_U16(h);
    if (sprites > MAX_FRAME_SPRITES)
        sprites = MAX_FRAME_SPRITES;
    sp = h + FRAME_HEAD_BYTES;
    if (s_perspective)
        perspective_shifts(s_play.cursor, s_perspective, sp, sprites, shift);
    for (i = 0; i < sprites; i++, sp += SPRITE_BYTES) {
        extern int _menu_draw_x_offset;
        uint16_t image = BE_READ_U16(sp);
        /* Where it lands on screen: the world position through the playback camera, as
         * live drawing does -- moved to where a player's screen had it, when that is
         * the screen being shown. */
        int16_t sx = (int16_t)((int16_t)BE_READ_U16(sp + 2) - origin + (s_perspective ? shift[i][0] : 0));
        int16_t sy = (int16_t)((int16_t)BE_READ_U16(sp + 4) - camY + (s_perspective ? shift[i][1] : 0));
        const SpriteGraphics *g;
        (void)Adverts_ApplyStripLine(image, adLine);   /* the ad's window, as the match cut it */
        g = graphics(image);
        if (!in_render_view(g, sx, sy))
            continue;
        /* The blit adds _menu_draw_x_offset -- WIDE_MARGIN in the highlights viewer, a
         * menu screen -- and the pitch is drawn without it, so it is taken off here, as
         * the .HIL path does (DrawReplayFrame) and drawPatternFromIndex does. Left on,
         * every sprite of a replay wider than 336 sat (W-336)/2 right of the pitch: the
         * goal net drawn a second time beside the goal, every shadow on the wrong
         * ground. The trace records the column the pixels land in -- what the blit is
         * given plus what it adds -- so the replay test compares what is SEEN; it used
         * to record the value before the blit, and could never see this. */
        {
            int16_t blitX = (int16_t)(sx - _menu_draw_x_offset);
            DarkenRectangle(image, blitX, sy);
            if (s_trace)
                trace_sprite(g->ordinal, (int16_t)(blitX + _menu_draw_x_offset), sy);
        }
    }

    draw_prediction(s_play.cursor, origin, camY);

    while (fresh && s_play.audioCursor < s_play.audioCount &&
           s_play.audio[s_play.audioCursor].frame <= s_play.cursor)
        fire_sound(&s_play.audio[s_play.audioCursor++]);

    if (s_trace)
        trace_frame(s_play.cursor);
    s_play.cursor++;
}

/* ======================================================================================
 * The view test (--view-replay)
 * ====================================================================================== */

static const char *s_viewPath;

int32_t ReplayV2_ArmViewTest(const char *path)
{
    if (!path || !path[0])
        return 1;
    s_viewPath = path;
    return 0;
}

int32_t ReplayV2_ViewTestArmed(void)
{
    return s_viewPath != NULL;
}

void ReplayV2_ViewTestRun(void)
{
    extern uint16_t _menuBGImageCmdStream[];
    extern uint8_t *_menuImageStreamBaseEnd;    /* a pointer: the stream's end, not an array */
    extern void     ViewHighlights(void);
    extern uint16_t _hilStarted, _teamsLoaded, _poolplyrLoaded;
    int32_t code;

    trace_file();
    /* The front end was skipped, so the menu sprites the viewer draws were never
     * extracted -- the replay rig and the live netplay test do the same. */
    ProcessImageCommandStream(_menuBGImageCmdStream, _menuImageStreamBaseEnd);
    InitTeams();
    /* A .HIL goes through the highlights menu's own loader (LoadHilFile: our JSON,
     * a legacy Amiga dump, or a DOS / SWOS 2020 file), then ShowHighlights' setup.
     * Like the menu it takes a name in the save folder, not a path. */
    {
        uint32_t n = (uint32_t)strlen(s_viewPath);
        const char *ext = n >= 4 ? s_viewPath + n - 4 : "";
        if (ext[0] == '.' && (ext[1] | 0x20) == 'h' && (ext[2] | 0x20) == 'i' && (ext[3] | 0x20) == 'l') {
            extern int16_t LoadHilFile(uint8_t *filename);
            extern void    CopyTeamsAndHeader(void);
            if (LoadHilFile((uint8_t *)s_viewPath) != 0)
                exit(1);
            _hilStarted = 1;
            _gameMaxSubstitutes = _hilMaxSubstitutes;
            _teamsLoaded = 0;
            _poolplyrLoaded = 0;
            CopyTeamsAndHeader();
            ViewHighlights();
            fprintf(stderr, "[replay] played the highlights in %s\n", s_viewPath);
            fflush(stderr);
            exit(0);
        }
    }
    if (ReplayV2_Load((const uint8_t *)s_viewPath) != 0)
        exit(1);
    _hilStarted = 1;
    _gameMaxSubstitutes = _hilMaxSubstitutes;
    _teamsLoaded = 0;
    _poolplyrLoaded = 0;
    ViewHighlights();
    code = s_play.cursor == s_play.frameCount ? 0 : 2;
    fprintf(stderr, "[replay] played %u of %u frames\n", s_play.cursor, s_play.frameCount);
    if (s_trace)
        fclose(s_trace);
    fflush(stderr);
    exit(code);
}

#endif /* SWOS_SDL_BUILD */
