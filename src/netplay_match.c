/*
 * netplay_match.c -- a match played through the netplay frame step and GGPO.
 * See include/netplay_match.h and docs/NETPLAY.md, "Playing a match through GGPO".
 */
#ifdef SWOS_SDL_BUILD

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>        /* SDL_GetTicks, SDL_Delay */

#include "netplay_match.h"
#include "audio_sfx_filter.h"
#include "dos_gameplay.h"   /* DosGameplay_TargetFps: the match's frame rate */
#include "contest_log.h"    /* --contest-log, while a recording is re-simulated */
#include "display.h"        /* _sim_draw_width, _screen_width */
#include "game_loop.h"
#include "match_step.h"
#include "match_record.h"   /* the recorder: SWOS_RECORD, [netplay] record */
#include "match_report.h"   /* both players' logs, after the match */
#include "match_prediction.h"   /* both players' PRED and SEEN, after the match */
#include "netplay_ghost.h"  /* what this peer drew against what finally happened */
#include "netplay_lobby.h"
#include "p2p_link.h"
#include "record_proto.h"
#include "replay_v2.h"     /* the resumable recording: kick-off state, inputs, hashes */
#include "state_wire.h"
#include "srp_writer.h"      /* SRP_INPUT_BYTES */
#include "netplay_netem.h"
#include "netplay.h"
#include "replay_test.h"
#include "sim_state.h"
#include "test_rules.h"      /* CARDS/INJURIES: the rule a match is played by */

extern int      Platform_Present(void);
extern void     Platform_DelayNextFrame(uint32_t us);
extern void     Platform_QuiesceTimerThread(void);
extern void     Platform_ResumeTimerThread(void);
extern void     _Int3TimerHandler(void);
extern uint16_t _playGame[1];

typedef enum {
    NETPLAY_MATCH_OFF, NETPLAY_MATCH_DIRECT, NETPLAY_MATCH_SYNCTEST, NETPLAY_MATCH_P2P,
    NETPLAY_MATCH_RESIM
} NetplayMatchMode;

static const char *const k_modeName[] = { "off", "direct", "synctest", "p2p", "resim" };

#define INPUTS_SIZE         (MATCH_INPUT_SIZE * MATCH_PLAYERS)
#define MAX_PREDICTION      8           /* frames a peer may play ahead of confirmed input */

#define DEFAULT_TIMEOUT_MS  30000
/* A live match: how long a silent peer freezes the game before the match ends and both go
 * back to the lobby. Long enough for a link that drops out for a moment (the netem
 * profiles' worst blackout is 400 ms); short enough that a player whose opponent quit is
 * not left staring at a frozen pitch. SWOS 2020 uses 5 s. */
#define LIVE_TIMEOUT_MS     10000
#define LINGER_MS           3000        /* after the end: how long to wait for the peer's acks */
#define LIVE_DUMP_FRAMES_MAX 8          /* SWOS_LIVE_DUMP_FRAME takes a list; this is how long */

static void streamNewMatch(void);   /* SWOS_LIVE_DUMP_STREAM: a file per match, below */
static void streamState(int32_t frame);
static void ringNewMatch(void);     /* SWOS_LIVE_DESYNC_DUMP: the last frames, kept in memory */
static void ringRecord(int32_t frame, const uint8_t *inputs);
static void ringDump(int32_t desyncFrame);

static struct {
    /* options */
    NetplayMatchMode mode;
    int32_t          checkDistance;       /* synctest */
    int32_t          player;              /* p2p: the local player, 1 or 2 */
    int32_t          port;                /* p2p: the local UDP port */
    char             peerAddress[32];     /* p2p */
    int32_t          peerPort;
    int32_t          delay;               /* p2p: input delay in frames */
    int32_t          stallMs;             /* p2p: the longest pause of a busy machine */
    int32_t          live;                /* p2p: input from this machine's pad, not a recording */
    int32_t          liveFrameLimit;      /* live: stop here (SWOS_LIVE_FRAMES), 0 = play it out */
    int32_t          timeoutMs;
    const char      *tracePath;
    const char      *soundTracePath;      /* the effects played, per frame (--netplay-sound-trace) */

    /* the session */
    NetplaySession  *session;
    uint32_t         stateSize;
    int32_t          running;             /* GGPO reports the session running */
    int32_t          prepared;            /* p2p: the recording's kick-off state is applied */
    int32_t          peerGone;            /* p2p: the peer left after the end was known */
    uint32_t         frames;              /* frames played for the first time */
    uint32_t         resimulated;         /* frames GGPO played again after loading a state */
    /* PRED (netplay_ghost.h Ghost_Lag): the other player's input as each frame was played,
     * by frame, to check the guess GGPO made was the last input it had. */
    uint8_t          remoteIn[64][MATCH_INPUT_SIZE];
    int32_t          stalledPending;      /* held at the prediction limit since the last frame */
    uint32_t         guessedFrames;       /* frames first played on a guess */
    uint32_t         guessNotRepeat;      /* ... whose guess was not the last input known */
    uint32_t         saves, loads;
    uint32_t         unidentified;        /* checksums that met a pointer with no identity */
    int32_t          lastConfirmed;       /* p2p */
    int32_t          resultFrame;         /* live: the frame a whistle went at, until it is confirmed; -1 */
    int32_t          checkpointed;        /* live: the match so far was saved at 1 half time, 2 the final whistle */
    uint32_t         lastProgressMs;      /* p2p: when a frame was last played or confirmed */
    int32_t          endFrame;            /* p2p: the frame the match ends at, once final; -1 */
    uint32_t         endScanFrom;         /* p2p: every frame below this is final, not over */
    uint32_t         lingerStartMs;       /* p2p: 0 until the end is known */
    uint32_t         stallRng;            /* p2p: when to pause -- never the game's generator */
    uint32_t         randomInput;         /* live: SWOS_LIVE_RANDOM_INPUT's generator, 0 = the pad */
    uint32_t         chaseInput;          /* live: SWOS_LIVE_CHASE_INPUT's generator, 0 = off */
    int32_t          paceUsPerFrame;      /* live: extra wait per frame GGPO's time sync asks for */
    int32_t          paceFramesLeft;      /* ... for this many more frames */
    uint8_t          paceKind;            /* whose wait that is: SRP_READ_BALANCE or SRP_READ_TIMESYNC */
    int64_t          paceBiasUs;          /* ... and the standing slowdown it has learned */
    int64_t          pacedUs;             /* live: all of it, for the report */
    uint32_t         timeSyncs;           /* p2p: TIMESYNC events that asked for a wait */
    uint32_t         predictionWaits;     /* p2p: passes held at the prediction limit */
    /* live: the connection, for the log (logStats). Nothing here reaches the match. */
    int32_t          linkPing, linkJitter;            /* the link's, when the delay was agreed */
    uint32_t         statStartMs, statNextSampleMs, statNextReportMs;
    int32_t          statLastPing;                    /* the previous sample, for jitter */
    int32_t          statN, statSum, statMin, statMax, statJitSum;   /* this 30 s */
    int32_t          statAllN, statAllSum, statAllMin, statAllMax, statAllJitSum;
    uint32_t         statLoads, statWaits;            /* rollbacks / holds at the last report */
    int32_t          slowPct;             /* live: SWOS_LIVE_SLOW_PCT, a slow clock for a test */
    int32_t          slowCount;
    int32_t          noPacing;            /* live: SWOS_LIVE_NO_PACING, to measure what pacing does */
    int32_t          hashEvery;           /* live: SWOS_LIVE_HASH_EVERY, frames between hash lines */
    int32_t          dumpFrames[LIVE_DUMP_FRAMES_MAX];  /* live: SWOS_LIVE_DUMP_FRAME, dump the state at these */
    int32_t          dumpFrameCount;
    int32_t          desyncFrame;         /* live: SWOS_LIVE_DESYNC_FRAME, go out of step on purpose */
    const char      *abortWhy;            /* live: a reason to end the match early, for the player */

    /* per frame, from its last save: the state hashes, and whether the match is over */
    uint64_t        *traceSim, *traceView, *traceObs;
    uint8_t         *over;
    uint32_t         traceLen, traceCap;
    uint32_t         recordNext;          /* live: the next frame the recorder is owed */
    int32_t          kickoffTaken;        /* live: frame 0's state went to the recording */
} s_np;

static void fail(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "[netplay] FAIL: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
    exit(2);
}

/* ---- options ---- */

static const char *after(const char *arg, const char *prefix)
{
    size_t n = strlen(prefix);
    return strncmp(arg, prefix, n) == 0 ? arg + n : NULL;
}

static int32_t parseInt(const char *v, long lo, long hi, const char *what, int32_t *out)
{
    char *end;
    long n = strtol(v, &end, 10);
    if (end == v || *end != 0 || n < lo || n > hi) {
        fprintf(stderr, "%s: expected %ld..%ld, got '%s'\n", what, lo, hi, v);
        return -1;
    }
    *out = (int32_t)n;
    return 1;
}

int32_t NetplayMatch_ParseArg(const char *arg)
{
    const char *v;

    if ((v = after(arg, "--netplay-match="))) {
        if (strcmp(v, "direct") == 0) {
            s_np.mode = NETPLAY_MATCH_DIRECT;
            return 1;
        }
        if (strcmp(v, "p2p") == 0) {
            s_np.mode = NETPLAY_MATCH_P2P;
            return 1;
        }
        if (strncmp(v, "synctest", 8) == 0 && (v[8] == 0 || v[8] == ':')) {
            s_np.checkDistance = 1;
            if (v[8] == ':' && parseInt(v + 9, 1, MAX_PREDICTION, "--netplay-match=synctest:<N>",
                                        &s_np.checkDistance) != 1)
                return -1;
            s_np.mode = NETPLAY_MATCH_SYNCTEST;
            return 1;
        }
        fprintf(stderr, "--netplay-match: expected direct, synctest[:N] or p2p, got '%s'\n", v);
        return -1;
    }
    if ((v = after(arg, "--netplay-player=")))
        return parseInt(v, 1, MATCH_PLAYERS, "--netplay-player", &s_np.player);
    if ((v = after(arg, "--netplay-port=")))
        return parseInt(v, 1, 65535, "--netplay-port", &s_np.port);
    if ((v = after(arg, "--netplay-peer="))) {
        const char *colon = strrchr(v, ':');
        size_t n = colon ? (size_t)(colon - v) : 0;
        if (n == 0 || n >= sizeof s_np.peerAddress) {
            fprintf(stderr, "--netplay-peer: expected <IPv4>:<port>, got '%s'\n", v);
            return -1;
        }
        memcpy(s_np.peerAddress, v, n);
        s_np.peerAddress[n] = 0;
        return parseInt(colon + 1, 1, 65535, "--netplay-peer's port", &s_np.peerPort);
    }
    if ((v = after(arg, "--netplay-delay=")))
        return parseInt(v, 0, MAX_PREDICTION - 1, "--netplay-delay", &s_np.delay);
    if ((v = after(arg, "--netplay-stall=")))
        return parseInt(v, 0, 1000, "--netplay-stall", &s_np.stallMs);
    if ((v = after(arg, "--netplay-timeout="))) {
        int32_t seconds;
        if (parseInt(v, 1, 3600, "--netplay-timeout", &seconds) != 1)
            return -1;
        s_np.timeoutMs = seconds * 1000;
        return 1;
    }
    if ((v = after(arg, "--netplay-trace="))) {
        s_np.tracePath = v;
        return 1;
    }
    if ((v = after(arg, "--netplay-sound-trace="))) {
        s_np.soundTracePath = v;
        return 1;
    }
    fprintf(stderr, "unknown option %s\n", arg);
    return -1;
}

const char *NetplayMatch_ArgError(void)
{
    int32_t p2pOptions = s_np.player || s_np.port || s_np.peerPort || s_np.delay || s_np.stallMs;

    if (s_np.mode == NETPLAY_MATCH_OFF &&
        (p2pOptions || s_np.tracePath || s_np.soundTracePath || s_np.timeoutMs))
        return "the --netplay-* options need --netplay-match";
    if (s_np.mode != NETPLAY_MATCH_P2P && p2pOptions)
        return "--netplay-player, -port, -peer, -delay and -stall are for --netplay-match=p2p";
    if (s_np.mode == NETPLAY_MATCH_P2P && (!s_np.player || !s_np.port || !s_np.peerPort))
        return "--netplay-match=p2p needs --netplay-player=1|2, --netplay-port=<local UDP port> "
               "and --netplay-peer=<IPv4>:<port>";
    return NULL;
}

int32_t NetplayMatch_Enabled(void)
{
    return s_np.mode != NETPLAY_MATCH_OFF;
}

/* ONE TICK A FRAME. The step advances both clocks (MatchStep_AdvanceClock) and then
 * runs the VBlank interrupt's work (_Int3TimerHandler), which advanced them AGAIN
 * unless the replay rig owned them -- and the rig is not running in a live match. So
 * every online match until 2026-09-29 counted two ticks a frame, while the Amiga (and
 * an offline match, and every netplay test, which run under the rig) count one: frame
 * 5000 of a live match was tick 10000. Both clocks are read by the rules --
 * `btst #1,currentTick` sets how often the ball runs away from the man dribbling it
 * (SWOS2.s CalculateIfPlayerWinsBall), and currentGameTick & mask paces dozens of AI
 * and physics decisions -- so online SWOS played to a different beat (ReportBot 0014:
 * "the ball doesn't stick to the feet as it should"). */
static int32_t s_legacyDoubleClock;
/* Only THAT call: set around the step's _Int3TimerHandler. Anywhere else the interrupt
 * is the clock -- the timer thread through the screens before and after a match, whose
 * `while (_currentTick == d0)` waits would otherwise wait forever (it hung the netplay
 * corpus test, which is in netplay mode from its command line on). */
static int32_t s_stepRunsInterrupt;

int32_t NetplayMatch_OwnsClock(void)
{
    return s_stepRunsInterrupt && !s_legacyDoubleClock;
}

void NetplayMatch_SetLegacyDoubleClock(int32_t on)
{
    s_legacyDoubleClock = on != 0;
}

/* ESC during a live match (input_sdl.c): end it cleanly at the next frame. */
static volatile int32_t s_leaveRequested;
void NetplayMatch_RequestLeave(void)
{
    s_leaveRequested = 1;
}

/* G or the pad's BACK during a live match (input_sdl.c): the player saw a ghost. */

/* Where a live match's LOCAL input comes from. NULL = this machine's pad, which
 * is what a real match uses. A test installs its own so the peer has something
 * it cannot predict -- with both sides sending nothing, "same as last frame" is
 * always right, no prediction ever misses and no rollback ever happens, so a
 * lossy-network test proves nothing. */
static uint8_t (*s_inputSource)(void);

/* Called when a LIVE session has finished and every frame is confirmed -- the
 * one moment the state is final and two peers can be compared. A test uses it
 * to report and exit; leaving the match loop instead lands in the post-match
 * screens, which wait for input a headless peer never gives. */
static void (*s_liveFinished)(void);

/* How the last live match ended, until NetplayMatch_Finish collects it (see liveEnd). */
static int32_t s_liveOutcome;           /* NetplayLiveOutcome */
static char    s_liveWhy[64];

void NetplayMatch_SetFinishedCallback(void (*fn)(void))
{
    s_liveFinished = fn;
}

void NetplayMatch_SetInputSource(uint8_t (*fn)(void))
{
    s_inputSource = fn;
}

/* Outside s_np: StartLive clears that, and the key is set before it. */
static uint8_t s_packetKey[16];
static int32_t s_hasPacketKey;

void NetplayMatch_SetPacketKey(const uint8_t *key)
{
    s_hasPacketKey = key != NULL;
    if (key)
        memcpy(s_packetKey, key, sizeof s_packetKey);
}

/* A match played between two machines -- or one of those played again from its
 * recording (--srp-resim), which has to be played the same way: controls assigned
 * without the pre-match menu, no goal replay. */
int32_t NetplayMatch_IsLive(void)
{
    return (s_np.mode == NETPLAY_MATCH_P2P && s_np.live) || s_np.mode == NETPLAY_MATCH_RESIM;
}

int32_t NetplayMatch_IsP2P(void)
{
    return s_np.mode == NETPLAY_MATCH_P2P;
}

int32_t NetplayMatch_LiveOutcome(const char **why)
{
    if (why)
        *why = s_liveWhy;
    return s_liveOutcome;
}

/* ---- the state, hashed ---- */

/* Over the portable encoding, so a peer on any platform computes the same. */
static uint64_t stateHash(uint32_t tiers)
{
    uint64_t h = SimState_Hash(tiers);
    if (SimState_HashUnidentified())
        s_np.unidentified++;
    return h;
}

/* The frame's checksum, the one GGPO-X compares with the peer's once the frame is
 * confirmed. (Synctest compares the checksums of its saves instead.) */
static uint16_t frameChecksum(void)
{
    uint64_t h = stateHash(SIM_TIER_SIM);
    return (uint16_t)(h ^ (h >> 16) ^ (h >> 32) ^ (h >> 48));
}

static void traceGrow(uint32_t frame)
{
    uint32_t cap;

    if (frame < s_np.traceCap)
        return;
    cap = s_np.traceCap ? s_np.traceCap : 65536;
    while (cap <= frame)
        cap *= 2;
    s_np.traceSim = (uint64_t *)realloc(s_np.traceSim, cap * sizeof(uint64_t));
    s_np.traceView = (uint64_t *)realloc(s_np.traceView, cap * sizeof(uint64_t));
    s_np.traceObs = (uint64_t *)realloc(s_np.traceObs, cap * sizeof(uint64_t));
    s_np.over = (uint8_t *)realloc(s_np.over, cap);
    if (!s_np.traceSim || !s_np.traceView || !s_np.traceObs || !s_np.over)
        fail("out of memory for the frame trace");
    memset(s_np.over + s_np.traceCap, 0, cap - s_np.traceCap);
    s_np.traceCap = cap;
}

/* Frame `frame` as it stands now: its hashes, and whether the match is over at it. A later
 * save of the same frame overwrites them, so once every frame is confirmed each entry is
 * the frame as finally played. */
static void traceRecord(uint32_t frame, uint64_t sim, uint64_t view)
{
    if (s_np.live)
        ReplayRec_SetHash(frame, sim);      /* the resumable recording's HASH */
    traceGrow(frame);
    s_np.traceSim[frame] = sim;
    s_np.traceView[frame] = view;
    s_np.traceObs[frame] = s_np.tracePath ? ReplayTest_ObservableHash() : 0;
    s_np.over[frame] = _playGame[0] == 0;
    if (frame >= s_np.traceLen)
        s_np.traceLen = frame + 1;
}

static void traceRecordNow(uint32_t frame)
{
    uint64_t sim = stateHash(SIM_TIER_SIM);
    traceRecord(frame, sim, stateHash(SIM_TIER_VIEW));
}

/* The rollback rig's trace format (Rollback_Finish), so the two compare byte for byte. */
static int32_t writeTrace(uint32_t frames)
{
    FILE *f;
    uint32_t i;

    if (!s_np.tracePath)
        return 0;
    f = fopen(s_np.tracePath, "w");
    if (!f) {
        fprintf(stderr, "[netplay] cannot write %s\n", s_np.tracePath);
        return 2;
    }
    for (i = 0; i < frames && i < s_np.traceLen; i++)
        fprintf(f, "%u %016llx %016llx %016llx\n", i, (unsigned long long)s_np.traceSim[i],
                (unsigned long long)s_np.traceView[i], (unsigned long long)s_np.traceObs[i]);
    fclose(f);
    fprintf(stderr, "[netplay] wrote %s (%u frames)\n", s_np.tracePath, i);
    return 0;
}

/* Synctest: each frame's state as first played. GGPO compares only checksums and aborts;
 * with these the report names what came out different. */
#define FIRST_RUN_FRAMES 16     /* more than the 8 frames a synctest goes back */

typedef struct {
    int32_t  frame;             /* -1: empty */
    int32_t  checksum;
    uint8_t *state;
} FirstRun;

static FirstRun s_firstRun[FIRST_RUN_FRAMES];

static void reportDifference(int32_t frame, const uint8_t *first, const uint8_t *again)
{
    uint32_t offset = 0, differing = 0;
    int32_t i;

    for (i = 0; i < g_simStateRegionCount; i++) {
        const SimStateRegion *reg = &g_simStateRegions[i];
        if (!SimState_RegionActive(i, SIM_TIER_ALL))
            continue;
        if (memcmp(first + offset, again + offset, reg->size) != 0) {
            uint32_t b = 0;
            while (first[offset + b] == again[offset + b])
                b++;
            if (differing++ < 12)
                fprintf(stderr, "[netplay]   %-36s byte %u of %u: first played %02X, re-simulated %02X\n",
                        reg->name, b, reg->size, first[offset + b], again[offset + b]);
        }
        offset += reg->size;
    }
    fail("frame %d, played again from a loaded state, came out different: %u registry "
         "entries differ", frame, differing);
}

static void checkFirstRun(int32_t frame, int32_t checksum, const uint8_t *state)
{
    FirstRun *fr = &s_firstRun[(uint32_t)frame % FIRST_RUN_FRAMES];

    if (fr->frame != frame) {
        if (!fr->state && !(fr->state = (uint8_t *)malloc(s_np.stateSize)))
            fail("out of memory");
        memcpy(fr->state, state, s_np.stateSize);
        fr->frame = frame;
        fr->checksum = checksum;
    } else if (checksum != fr->checksum) {
        reportDifference(frame, fr->state, state);
    }
}

/* ---- the step ---- */

/* (GGPO-X's prediction balance, docs/NETPLAY_BALANCE.md, is not used: the host's BALANCE
 * switch and [netplay] balance were removed 2026-10-30 -- it caused netplay problems.) */

/* The host's switches (netplay_match.h), set by the lobby at kick-off on both machines. */
static int32_t s_hostSwitches = -1;

void NetplayMatch_SetHostSwitches(int32_t switches)
{
    s_hostSwitches = switches < 0 ? -1 : (switches & NETPLAY_SW_ALL);
}

int32_t NetplayMatch_HostSwitches(void) { return s_hostSwitches; }

/* Whether switch `sw` is ON for the match: the host's, or as this build plays. */
static int32_t switchOn(int32_t sw)
{
    if (s_hostSwitches >= 0)
        return (s_hostSwitches & sw) != 0;
    return (NETPLAY_SW_DEFAULT & sw) != 0;
}

/* ---- sound during a rollback ----
 *
 * A frame GGPO plays again already played once, and the effects it started were heard
 * then: a re-simulation that started them again would repeat every kick, whistle and
 * crowd chant in the rolled-back frames, up to eight frames late and once per rollback.
 *
 * So every effect a frame starts is written down against that frame, and a frame played
 * again starts only what is not written down yet -- an effect that happens only once the
 * corrected input is known, which then plays a few frames late rather than never. What
 * the mispredicted frames started and the corrected ones do not cannot be taken back;
 * those effects are short. Count matters, not just kind: a frame that started the same
 * effect twice starts it twice.
 *
 * The filter decides only what reaches the speakers. The match reads nothing back from
 * the mixer (the one gate that looks, GateKickSampleAndCrowd's _audioDbgFlagA bit 7, is
 * never set on SDL), so the state both peers compare is the same with sound on or off. */
#define SOUND_FRAMES    32      /* a power of two, well past MAX_PREDICTION */
#define SOUND_EVENTS    16      /* effects one frame starts; more are played, not written down */

typedef struct {
    int32_t  frame;             /* -1: empty */
    uint32_t count;
    uint32_t event[SOUND_EVENTS];
    uint32_t matched;           /* this pass over the frame: entries already accounted for */
} SoundFrame;

static SoundFrame s_sound[SOUND_FRAMES];
static int32_t    s_soundFrame = -1;    /* the frame being stepped; -1 outside a step */
static int32_t    s_soundAgain;         /* ... and it is a re-simulation */
static uint32_t   s_soundPlayed, s_soundLate, s_soundRepeats;
static FILE      *s_soundTrace;

static void soundReset(void)
{
    uint32_t i;
    for (i = 0; i < SOUND_FRAMES; i++)
        s_sound[i].frame = -1;
    s_soundFrame = -1;
    s_soundAgain = 0;
    s_soundPlayed = s_soundLate = s_soundRepeats = 0;
}

static int32_t soundFilter(uint8_t kind, uint8_t channel, uint16_t id)
{
    SoundFrame *f;
    uint32_t ev = (uint32_t)kind << 24 | (uint32_t)channel << 16 | id, i;

    if (s_soundFrame < 0)
        return 1;               /* not a frame of the match: the pre-match screens */
    f = &s_sound[(uint32_t)s_soundFrame & (SOUND_FRAMES - 1)];
    for (i = 0; i < f->count; i++) {
        if (f->event[i] == ev && !(f->matched & (1u << i))) {
            f->matched |= 1u << i;
            s_soundRepeats++;
            return 0;           /* heard when this frame was first played */
        }
    }
    if (f->count < SOUND_EVENTS) {
        f->matched |= 1u << f->count;
        f->event[f->count++] = ev;
    }
    s_soundPlayed++;
    if (s_soundAgain)
        s_soundLate++;
    if (s_soundTrace)
        fprintf(s_soundTrace, "%d %u %u %u%s\n", s_soundFrame, kind, channel, id,
                s_soundAgain ? " late" : "");
    return 1;
}

/* Around every step: which frame it is, and whether it has been played before. */
static void soundFrameBegin(int32_t frame, int32_t again)
{
    /* The replay recorder numbers frames the same way, so a re-simulated frame replaces
     * what was recorded for it (src/replay_v2.c). */
    { extern void ReplayRec_SetFrame(uint32_t frame); ReplayRec_SetFrame((uint32_t)frame); }
    SoundFrame *f = &s_sound[(uint32_t)frame & (SOUND_FRAMES - 1)];
    if (f->frame != frame) {
        f->frame = frame;
        f->count = 0;
    }
    f->matched = 0;
    s_soundFrame = frame;
    s_soundAgain = again;
}

static void soundFrameEnd(void)
{
    s_soundFrame = -1;
    s_soundAgain = 0;
}

/* One frame: the players' inputs into the game, the frame, its ticks, its buffer swap and
 * the VBlank interrupt's work -- everything a frame does but showing it. GGPO calls it
 * again for every frame it re-simulates. `logFrame` numbers the input's log lines (-1
 * keeps them quiet); `compare` compares the frame with the recording, for a frame played
 * once and never rolled back. */
static GameFrameResult step(const uint8_t *inputs, int32_t logFrame, int32_t compare)
{
    MatchPlayerInput in[MATCH_PLAYERS];
    GameFrameResult r;
    int32_t p;

    /* Between peers a frame past the end of the match gets played: predicted, or
     * re-simulated after a correction that ended the match sooner. The match is over at
     * it, and the frame changes nothing. */
    if (s_np.mode == NETPLAY_MATCH_P2P && _playGame[0] == 0)
        return GAME_FRAME_MATCH_OVER;

    for (p = 0; p < MATCH_PLAYERS; p++)
        MatchStep_UnpackInput(&in[p], inputs + p * MATCH_INPUT_SIZE);
    MatchStep_ApplyInput(in, logFrame);
    /* SWOS_LIVE_DESYNC_FRAME=N on ONE peer: simulate frame N differently, on purpose, so
     * what happens when two peers go out of step can be watched instead of waited for.
     * One bit of _seed is a real divergence: _seed is a COUNTER, so the two Rand()
     * streams stay one step apart for the rest of the match, and it is in the checksummed
     * SIM tier.
     *
     * INSIDE the frame, and re-applied every time GGPO simulates that frame -- which is
     * what a real divergence is, and what makes it survive. Applied after the step
     * instead, it was undone by the first rollback (the state GGPO saved for that frame
     * predates it), and the peers quietly re-converged: satellite "detected" 0 of 5
     * injected desyncs because there was no longer one to find. */
    if (s_np.live && s_np.desyncFrame > 0) {
        int32_t dsFrame = 0;
        Netplay_CurrentFrame(s_np.session, &dsFrame);
        if (dsFrame == s_np.desyncFrame) {
            extern uint8_t _seed;
            _seed ^= 0x01;
            fprintf(stderr, "[netplay] TEST: simulating frame %d out of step on purpose "
                            "(SWOS_LIVE_DESYNC_FRAME)\n", dsFrame);
        }
    }
    r = GameLoop_Frame();
    if (r != GAME_FRAME_DONE && r != GAME_FRAME_HELD)
        return r;
    if (compare)
        ReplayTest_FrameCheck();
    MatchStep_AdvanceClock();
    GameLoop_EndFrame(0);
    /* The VBlank interrupt's work -- the fade ticks among it -- is part of the frame: on the
     * Amiga it runs once after every frame, so it runs once in every step, and from nowhere
     * else while a netplay match plays (NetplayMatch_Start). It does not advance the
     * clocks: MatchStep_AdvanceClock just did (NetplayMatch_OwnsClock). */
    s_stepRunsInterrupt = 1;
    _Int3TimerHandler();
    s_stepRunsInterrupt = 0;
    return GAME_FRAME_DONE;     /* a frame the statistics overlay held is a frame too */
}

/* ---- GGPO's callbacks ---- */

static int32_t cbSave(void *ctx, uint8_t **state, int32_t *len, int32_t *checksum, int32_t frame)
{
    uint8_t *buf = (uint8_t *)malloc(s_np.stateSize);
    uint64_t sim, view, both;

    (void)ctx;
    if (!buf)
        return 0;
    SimState_Save(buf, SIM_TIER_ALL);
    sim = stateHash(SIM_TIER_SIM);
    view = stateHash(SIM_TIER_VIEW);
    both = sim ^ ((view << 1) | (view >> 63));
    *state = buf;
    *len = (int32_t)s_np.stateSize;
    *checksum = (int32_t)(uint32_t)(both ^ (both >> 32));
    s_np.saves++;
    /* The state the match kicked off in, for the resumable recording (replay_v2.h): with
     * the setup and every frame's input it plays the match again exactly. Frame 0's first
     * save is the state before frame 0 is played -- the point a re-simulation starts from. */
    if (frame == 0 && s_np.live && !s_np.kickoffTaken) {
        uint32_t cap = StateWire_MaxBytes(SIM_TIER_ALL);
        uint8_t *packet = (uint8_t *)malloc(cap);
        int32_t n = packet ? StateWire_Encode(packet, cap, SIM_TIER_ALL, 0, NULL, 0) : -1;
        s_np.kickoffTaken = 1;
        if (n > 0)
            /* With the host's CARDS/INJURIES off the match is played by the old tester
             * builds' rule, and says so the way they did: --srp-resim replays it by it. */
            ReplayRec_SetKickoff(packet, (uint32_t)n, SimState_LayoutHash(),
                                 (uint8_t)(P2PLink_GameplayFlags() |
                                           (TestRules_NoInjuriesCards() ? P2P_RULES_BUILD : 0)),
                                 (uint16_t)_sim_draw_width);
        else
            fprintf(stderr, "[netplay] the kick-off state could not be recorded (%s): the "
                            "replay will not be resumable\n", n < 0 ? StateWire_ErrorName(n) : "no memory");
        free(packet);
    }
    if (frame >= 0)
        traceRecord((uint32_t)frame, sim, view);
    if (s_np.mode == NETPLAY_MATCH_SYNCTEST)
        checkFirstRun(frame, *checksum, buf);
    return 1;
}

static int32_t cbLoad(void *ctx, const uint8_t *state, int32_t len, int32_t framesToRollback)
{
    (void)ctx;
    (void)framesToRollback;
    if ((uint32_t)len != s_np.stateSize)
        return 0;
    SimState_Load(state, SIM_TIER_ALL);
    s_np.loads++;
    return 1;
}

static void cbFree(void *ctx, uint8_t *state)
{
    (void)ctx;
    free(state);
}

/* A frame GGPO re-simulates, after loading an earlier state; it supplies the inputs. */
static int32_t cbAdvance(void *ctx)
{
    uint8_t inputs[INPUTS_SIZE];
    int32_t disconnect = 0, rc;
    GameFrameResult r;

    (void)ctx;
    rc = Netplay_SyncInput(s_np.session, inputs, &disconnect);
    if (rc != NETPLAY_OK)
        fail("re-simulating a frame: synchronize input: %s", Netplay_ErrorName(rc));
    int32_t resimFrame = 0;
    Netplay_CurrentFrame(s_np.session, &resimFrame);
    soundFrameBegin(resimFrame, 1);
    r = step(inputs, -1, 0);
    soundFrameEnd();
    if (s_np.mode == NETPLAY_MATCH_P2P) {
        Ghost_Played(resimFrame, 0);        /* the correction replaces what was drawn */
        if (resimFrame >= 0)
            memcpy(s_np.remoteIn[resimFrame & 63], inputs + (2 - s_np.player) * MATCH_INPUT_SIZE,
                   MATCH_INPUT_SIZE);
    }
    if (r != GAME_FRAME_DONE && !(s_np.mode == NETPLAY_MATCH_P2P && r == GAME_FRAME_MATCH_OVER))
        fail("a re-simulated frame left the match loop (GameFrameResult %d)", (int)r);
    s_np.resimulated++;
    /* Record the CORRECTED frame too, or the record is a lie on any peer that predicted:
     * first play is what this peer guessed, and the frame GGPO finally kept is this one.
     * Both writers key on the frame number, so this replaces the earlier pass rather than
     * adding to it. Without it a local pair test "parts" at frame 2 on the other player's
     * pad state, which is a prediction being compared with a correction and nothing else. */
    if (s_np.live) {
        int32_t done = 0;
        Netplay_CurrentFrame(s_np.session, &done);
        streamState(done);
        ringRecord(done, inputs);
        if (done >= 0)
            ReplayRec_SetInput((uint32_t)done, inputs);     /* replaces the predicted one */
    }
    Netplay_AdvanceFrame(s_np.session, frameChecksum());
    return 1;
}

static void cbEvent(void *ctx, const NetplayEvent *ev)
{
    (void)ctx;
    switch (ev->code) {
    case NETPLAY_EVENT_RUNNING:
        if (!s_np.running) {
            fprintf(stderr, "[netplay] synchronized with the peer\n");
            if (s_np.live) {
                /* What the link measured before the match -- what the delay was
                 * agreed from (p2p_link.c propose_delay). */
                fprintf(stderr, "[netplay] kick-off: ping %d ms, jitter %d ms (measured by the link), "
                                "input delay %d\n", s_np.linkPing, s_np.linkJitter, s_np.delay);
            }
        }
        s_np.running = 1;
        break;
    /* A corpus run fails on any of these. A live match is somebody's game: it ends,
     * and they go back to the lobby told why (liveEnd). The event arrives from inside a
     * GGPO call, which is no place to close the session, so p2pFrame ends it. */
    case NETPLAY_EVENT_DESYNC:
        if (!s_np.live)
            fail("GGPO reports a desync at frame %d (local checksum %04X, remote %04X)",
                 ev->desyncFrame, ev->localChecksum, ev->remoteChecksum);
        /* Two frame numbers, because they are never the same one and the gap is the
         * interesting half: the checksums for a frame are only compared once both peers
         * have CONFIRMED it, so a slow link is found out later, not differently. */
        fprintf(stderr, "[netplay] desync at frame %d, noticed at frame %u (local checksum "
                        "%04X, remote %04X)\n",
                ev->desyncFrame, s_np.frames, ev->localChecksum, ev->remoteChecksum);
        /* Write what we kept, on BOTH peers, before the match ends and the frames go. */
        ringDump(ev->desyncFrame);
        if (!s_np.abortWhy)
            s_np.abortWhy = "THE TWO GAMES WENT OUT OF STEP";
        break;
    case NETPLAY_EVENT_DISCONNECTED:
        s_np.peerGone = 1;
        if (s_np.endFrame >= 0)
            break;
        if (!s_np.live)
            fail("the peer (player %d) disconnected", ev->player);
        fprintf(stderr, "[netplay] the peer (player %d) disconnected\n", ev->player);
        if (!s_np.abortWhy)
            s_np.abortWhy = "THE OTHER PLAYER LEFT";
        break;
    case NETPLAY_EVENT_TIMESYNC:
        /* Frame pacing. Two machines never run at exactly the same rate, and nothing
         * else makes the faster one wait: it plays further and further ahead of the
         * input it has, until it reaches the prediction limit (8 frames) and from then
         * on stalls a frame at a time and rolls back up to 8 frames whenever the other
         * player's input changes -- players see the opponent jump and slide. GGPO
         * measures the lead every timeSyncPeriod frames and says how many frames this
         * peer is ahead; the peer that is ahead waits that long, spread thinly over
         * the frames until the next measurement so no single frame visibly hitches.
         * The peer that is behind does nothing.
         *
         * Two parts. The first closes the lead GGPO reports over the next period. On its
         * own it leaves the faster peer a steady ~2.5 frames ahead at a 2% clock
         * difference, because it only acts once there is a lead to measure -- still
         * predicting, and rolling back, all match long. So the second part LEARNS the
         * difference: a standing slowdown nudged up by half the first part each time
         * this peer is found ahead, and down when behind, never below zero or above a
         * tenth of a frame. Two machines' clocks differ by a steady rate, which is what
         * it converges to, and the lead goes to nothing. GGPO's figure is already half
         * the gap (each side is meant to close half). The simulation never sees any of
         * it: only wall time. */
        if (s_np.live && getenv("SWOS_LIVE_PACING_LOG"))
            fprintf(stderr, "[netplay] time sync at frame %u: %d.%03d frames ahead, period %d\n",
                    s_np.frames, ev->framesAheadMilli / 1000, abs(ev->framesAheadMilli % 1000),
                    ev->timeSyncPeriod);
        s_np.paceUsPerFrame = 0;
        s_np.paceFramesLeft = 0;
        if (s_np.live && !s_np.noPacing && ev->timeSyncPeriod > 0 &&
            (ev->framesAheadMilli > 250 || ev->framesAheadMilli < -250)) {
            int64_t frameUs = 1000000 / DosGameplay_TargetFps();
            int64_t close = frameUs * ev->framesAheadMilli / 1000 / ev->timeSyncPeriod;
            s_np.paceBiasUs += close / 2;
            if (s_np.paceBiasUs < 0)
                s_np.paceBiasUs = 0;
            if (s_np.paceBiasUs > frameUs / 10)
                s_np.paceBiasUs = frameUs / 10;
            if (close > 0) {
                s_np.paceUsPerFrame = (int32_t)close;
                s_np.paceFramesLeft = ev->timeSyncPeriod;
                s_np.paceKind = SRP_READ_TIMESYNC;
                s_np.timeSyncs++;
            }
        }
        break;
    case NETPLAY_EVENT_NETWORK_ERROR:
        if (!s_np.live)
            fail("network error %d", ev->errorCode);
        fprintf(stderr, "[netplay] network error %d\n", ev->errorCode);
        if (!s_np.abortWhy)
            s_np.abortWhy = "A NETWORK ERROR ENDED THE MATCH";
        break;
    default:
        break;
    }
}

/* ---- the session ---- */

int32_t NetplayMatch_Start(void)
{
    NetplayCallbacks cb;
    int32_t rc, i;

    if (s_np.mode == NETPLAY_MATCH_OFF)
        return 0;
    /* Only the frame step may write game memory: no timer thread running the VBlank
     * interrupt behind its back (docs/ROLLBACK.md, "How the rig plays a match"). */
    Platform_QuiesceTimerThread();
    _gFrameStepInput = 1;      /* input a frame at a time (game_loop.h) */
    s_np.stateSize = SimState_Size(SIM_TIER_ALL);
    s_np.endFrame = -1;
    s_np.resultFrame = -1;
    s_np.lastConfirmed = -1;
    Ghost_Begin(s_np.player);
    if (!s_np.timeoutMs)
        s_np.timeoutMs = DEFAULT_TIMEOUT_MS;
    for (i = 0; i < FIRST_RUN_FRAMES; i++)
        s_firstRun[i].frame = -1;
    soundReset();
    if (s_np.soundTracePath && !(s_soundTrace = fopen(s_np.soundTracePath, "w"))) {
        fprintf(stderr, "[netplay] cannot write %s\n", s_np.soundTracePath);
        return 1;
    }

    memset(&cb, 0, sizeof cb);
    cb.saveState = cbSave;
    cb.loadState = cbLoad;
    cb.freeState = cbFree;
    cb.advanceFrame = cbAdvance;
    cb.onEvent = cbEvent;
    if (s_np.mode != NETPLAY_MATCH_DIRECT && !Netplay_Available()) {
        fprintf(stderr, "[netplay] this build has no netplay (make NETPLAY=1)\n");
        return 1;
    }
    if (s_np.mode == NETPLAY_MATCH_SYNCTEST) {
        rc = Netplay_StartSyncTest(&s_np.session, MATCH_INPUT_SIZE, s_np.checkDistance, &cb);
        if (rc != NETPLAY_OK) {
            fprintf(stderr, "[netplay] starting the synctest session: %s\n", Netplay_ErrorName(rc));
            return 1;
        }
    } else if (s_np.mode == NETPLAY_MATCH_P2P) {
        NetplayConfig cfg;
        memset(&cfg, 0, sizeof cfg);
        cfg.localPort = (uint16_t)s_np.port;
        cfg.localPlayer = s_np.player;
        snprintf(cfg.remoteAddress, sizeof cfg.remoteAddress, "%s", s_np.peerAddress);
        cfg.remotePort = (uint16_t)s_np.peerPort;
        cfg.inputSize = MATCH_INPUT_SIZE;
        cfg.frameDelay = s_np.delay;
        cfg.maxPrediction = MAX_PREDICTION;
        cfg.fps = DosGameplay_TargetFps();  /* 50 or 70: the rate the match is played at */
        cfg.disconnectTimeoutMs = s_np.timeoutMs;
        cfg.disconnectNotifyMs = 1000;
        cfg.hasPacketKey = s_hasPacketKey;
        memcpy(cfg.packetKey, s_packetKey, sizeof cfg.packetKey);
        s_np.stallRng = 0x9E3779B9u ^ ((uint32_t)s_np.player * 0x85EBCA6Bu);
        rc = Netplay_StartP2P(&s_np.session, &cfg, &cb);
        if (rc != NETPLAY_OK) {
            fprintf(stderr, "[netplay] starting the p2p session: %s\n", Netplay_ErrorName(rc));
            return 1;
        }
    }
    /* KICK-OFF SYNC: before the first input, as ggpo_set_kickoff_prediction wants. */
    Netplay_SetKickoffPrediction(s_np.session, switchOn(NETPLAY_SW_KICKOFF_SYNC));
    if (s_hostSwitches >= 0)
        fprintf(stderr, "[netplay] the host's switches: short presses %s, kick-off sync %s, cards/injuries %s\n",
                switchOn(NETPLAY_SW_SHORT_PRESSES) ? "ON" : "OFF", switchOn(NETPLAY_SW_KICKOFF_SYNC) ? "ON" : "OFF",
                switchOn(NETPLAY_SW_CARDS_INJURIES) ? "ON" : "OFF");

    Audio_SetSfxFilter(soundFilter);      /* sound during a rollback, above */
    if (Netem_Enabled())
        fprintf(stderr, "[netplay] network emulation: %s\n", Netem_Describe());
    fprintf(stderr, "[netplay] %s match: %d bytes of input per player, %u bytes of state per save",
            k_modeName[s_np.mode], MATCH_INPUT_SIZE, s_np.stateSize);
    if (s_np.mode == NETPLAY_MATCH_SYNCTEST)
        fprintf(stderr, ", check distance %d", s_np.checkDistance);
    if (s_np.mode == NETPLAY_MATCH_P2P)
        fprintf(stderr, "; player %d on UDP port %d, peer %s:%d, input delay %d, pauses up to %d ms",
                s_np.player, s_np.port, s_np.peerAddress, s_np.peerPort, s_np.delay, s_np.stallMs);
    fputc('\n', stderr);
    return 0;
}

/* Start a LIVE match against the peer the matchmaking menu found (or the
 * direct-IP menu connected to). Everything the corpus p2p mode does, with this
 * machine's pad as the local input.
 *
 * `delay` MUST be the delay BOTH peers agreed (P2PLink_AgreedDelay), not one
 * this machine picked: an asymmetric delay is what makes one player feel they
 * are chasing the other, and in a close contest it decides the ball before the
 * game's own roll is reached.
 *
 * The p2p link has to be CLOSED before this is called. GGPO opens its own
 * socket, and on the same local port -- which is also what keeps a NAT mapping
 * open across the handover. */
/* The recorder (include/match_record.h): who is playing what, then every confirmed
 * frame's SIM-tier hash, then how it ended. Off unless SWOS_RECORD or [netplay] record
 * asks for it; it only ever READS the trace, so it cannot touch the match. */
static void team_name(char *out, size_t cap, const TeamFileEx *t)
{
    size_t i;
    for (i = 0; i + 1 < cap && i < sizeof t->team.teamName && t->team.teamName[i]; i++)
        out[i] = (char)t->team.teamName[i];
    out[i] = '\0';
}

static void recordBegin(int32_t localPlayer, int32_t delay, int32_t simWidth)
{
    extern TeamFileEx _selectedTeams[SELECTED_TEAMS_MAX];
    const LobbyPlayer *me = NetplayLobby_Player(localPlayer);
    const LobbyPlayer *them = NetplayLobby_Player(3 - localPlayer);
    char team1[20], team2[20];
    MatchRecordInfo info;

    team_name(team1, sizeof team1, &_selectedTeams[0]);
    team_name(team2, sizeof team2, &_selectedTeams[1]);
    memset(&info, 0, sizeof info);
    info.matchId    = s_hasPacketKey ? Record_MatchId(s_packetKey) : 0;
    info.relayToken = P2PLink_RelayToken();
    info.player     = localPlayer;
    info.gameplay   = P2PLink_GameplayFlags();      /* the handshake's rules byte */
    info.simWidth   = simWidth;
    info.delay      = delay;
    info.myName     = me ? me->name : "";
    info.peerName   = them ? them->name : "";
    info.team1      = team1;
    info.team2      = team2;
    /* Who played, into the match's .SRP from kick-off (PLYR): the host (player 1) first. */
    ReplayRec_SetPlayers(localPlayer == 1 ? info.myName : info.peerName,
                         localPlayer == 1 ? info.peerName : info.myName);
    MatchRecord_Begin(&info);
    MatchReport_Begin(&info);           /* both players' logs, zipped, after the match */
    MatchPrediction_Begin();            /* both players' PRED and SEEN, after it */
}

/* Every frame up to `confirmed` whose start has been saved. Confirmed is set after
 * any rollback its inputs called for has been played (the GGPO-X fork's
 * ConfirmedFrame), so these hashes are the frames as finally played. */
static void recordConfirmed(int32_t confirmed)
{
    /* Nothing past the end of the match, once it is known (liveEnd says why). */
    if (s_np.endFrame >= 0 && confirmed > s_np.endFrame - 1)
        confirmed = s_np.endFrame - 1;
    while ((int64_t)s_np.recordNext <= (int64_t)confirmed && s_np.recordNext < s_np.traceLen) {
        MatchRecord_Frame(s_np.recordNext, s_np.traceSim[s_np.recordNext]);
        s_np.recordNext++;
    }
    if (confirmed >= 0)
        MatchRecord_Replay((uint32_t)confirmed);
    MatchRecord_Pump();
}

int32_t NetplayMatch_StartLive(int32_t localPlayer, const char *peerIp, uint16_t peerPort,
                               uint16_t localPort, int32_t delay, int32_t simWidth)
{
    int32_t rc;

    if (localPlayer != 1 && localPlayer != 2)
        return 1;
    if (!peerIp || !peerIp[0])
        return 1;
    if (delay < 0)
        delay = 0;
    if (delay > MAX_PREDICTION - 1)
        delay = MAX_PREDICTION - 1;

    /* Both peers must start the match from the same clock.
     *
     * The tick counters have been running since each process booted, so they
     * hold whatever the wall clock had reached -- a different number on each
     * machine, and the state hash covers them, so GGPO sees a desync on frame 0
     * before a single input has been exchanged. Measured: of 355 registry
     * regions exactly these two differed between two peers.
     *
     * _lastGameTick starts one BEHIND the tick, not equal to it: ReadTimerDelta
     * turns the difference into _lastFrameTicks, and leaving them equal gives
     * frame 0 a delta of 0, which lands every timer-driven transition in the
     * match one frame late. The replay rig documents the same values and the
     * same reason (applyFirstFrameSetup in src/replay_test.c). */
    {
        extern volatile uint16_t _currentGameTick;
        extern volatile uint16_t _currentTick;
        extern uint16_t          _lastGameTick;
        _currentGameTick = 0;
        _currentTick     = 0;
        _lastGameTick    = 0xFFFF;
        MatchStep_Reset();          /* no button held, no bench call pending */
    }

    /* And from released controls. From here InputSDL_PostFrame leaves the joystick
     * and fire registers to the session, so the pre-match screens would otherwise
     * keep reading whatever was held when the match started -- including the fire
     * press on START, which hung the stadium screen's wait for a release
     * (InputSDL_ReleaseAll). */
    {
        extern void InputSDL_ReleaseAll(void);
        InputSDL_ReleaseAll();
    }

    memset(&s_np, 0, sizeof s_np);
    s_liveOutcome    = NETPLAY_LIVE_NONE;
    s_liveWhy[0]     = '\0';
    s_np.mode        = NETPLAY_MATCH_P2P;
    s_np.live        = 1;
    s_np.player      = localPlayer;
    s_np.port        = localPort;
    s_np.peerPort    = peerPort;
    s_np.delay       = delay;
    {   /* The link's measurement, taken now: by the time GGPO synchronizes it owns the
         * socket and the link has let go of it. */
        extern int32_t P2PLink_ProposedPingMs(void), P2PLink_ProposedJitterMs(void);
        s_np.linkPing   = P2PLink_ProposedPingMs();
        s_np.linkJitter = P2PLink_ProposedJitterMs();
    }
    s_np.timeoutMs   = LIVE_TIMEOUT_MS;
    {
        const char *lim = getenv("SWOS_LIVE_FRAMES");
        const char *rnd = getenv("SWOS_LIVE_RANDOM_INPUT");
        const char *slow = getenv("SWOS_LIVE_SLOW_PCT");
        s_np.slowPct = (slow && slow[0]) ? atoi(slow) : 0;
        s_np.noPacing = getenv("SWOS_LIVE_NO_PACING") != NULL;
        streamNewMatch();
        ringNewMatch();
        {
            const char *he = getenv("SWOS_LIVE_HASH_EVERY");
            const char *df = getenv("SWOS_LIVE_DUMP_FRAME");
            s_np.hashEvery = (he && he[0]) ? atoi(he) : 100;
            /* A COMMA-SEPARATED LIST, because the frame a desync is found at is only
             * interesting beside the frame before it: the first one that differs says
             * what parted, its predecessor says what they still agreed on. Dumping both
             * in one run is one cross-machine match instead of two. */
            /* SWOS_LIVE_DESYNC_FRAME=N: a desync ON PURPOSE, so the thing that happens
             * when two peers go out of step can be watched rather than waited for. */
            { const char *dsf = getenv("SWOS_LIVE_DESYNC_FRAME");
              s_np.desyncFrame = (dsf && dsf[0]) ? atoi(dsf) : 0; }
            s_np.dumpFrameCount = 0;
            while (df && *df && s_np.dumpFrameCount < LIVE_DUMP_FRAMES_MAX) {
                char *end;
                int32_t f = (int32_t)strtol(df, &end, 10);
                if (end == df)
                    break;
                if (f > 0)
                    s_np.dumpFrames[s_np.dumpFrameCount++] = f;
                df = (*end == ',') ? end + 1 : end;
            }
        }
        s_np.liveFrameLimit = (lim && lim[0]) ? atoi(lim) : 0;
        s_np.randomInput = (rnd && rnd[0]) ? (uint32_t)strtoul(rnd, NULL, 0) | 1u : 0;
        {
            const char *ch = getenv("SWOS_LIVE_CHASE_INPUT");
            s_np.chaseInput = (ch && ch[0]) ? (uint32_t)strtoul(ch, NULL, 0) | 1u : 0;
        }
    }
    snprintf(s_np.peerAddress, sizeof s_np.peerAddress, "%s", peerIp);

    rc = NetplayMatch_Start();
    if (rc == 0)
        recordBegin(localPlayer, delay, simWidth);
    if (rc == 0) {
        /* One simulation draw width on both machines (display.h): what margin animates
         * and which crowd tiles toggle are decided for it, whatever each display is.
         * From here, so the pre-match screens that follow run at it too. */
        if (simWidth < SCREEN_WIDTH)
            simWidth = SCREEN_WIDTH;
        if (simWidth > PITCH_VIEW_MAX_WIDTH)
            simWidth = PITCH_VIEW_MAX_WIDTH;
        _sim_draw_width = simWidth;
        fprintf(stderr, "[netplay] simulation draw width %d (this display %d)\n",
                (int)_sim_draw_width, (int)_screen_width);
    }
    return rc;
}

/* SWOS_LIVE_RANDOM_INPUT=<seed>: a live match's local input from a generator instead of
 * the pad, for a test through the menus (scripts/netplay_lobby_test.sh), where nobody
 * holds one. A direction or none, fire about a fifth of the time, held 3..14 frames --
 * the other peer's "same as last frame" prediction is wrong often enough to roll back.
 * Seed the two peers differently. Its own generator, never the game's. */
typedef struct {
    uint32_t state;
    uint8_t  held;
    int32_t  framesLeft;
} RandomPad;

static uint8_t randomPad(RandomPad *pad)
{
    static const uint8_t k_dirs[9] = { 0x01, 0x02, 0x04, 0x08, 0x05, 0x09, 0x06, 0x0A, 0 };
    uint32_t x;

    if (pad->framesLeft-- > 0)
        return pad->held;
    x = pad->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    pad->state = x ? x : 0x9E3779B9u;
    pad->held = k_dirs[x % 9u];
    if (((x >> 8) % 5u) == 0)
        pad->held |= 0x20;
    pad->framesLeft = (int32_t)((x >> 16) % 12u) + 3;
    return pad->held;
}

static uint8_t randomInput(void)
{
    static RandomPad pad;
    pad.state = s_np.randomInput;
    {
        uint8_t v = randomPad(&pad);
        s_np.randomInput = pad.state;
        return v;
    }
}

/* SWOS_LIVE_CHASE_INPUT=<seed>: a live match's local input from a player who chases the
 * ball, for reproducing what two humans do to each other under rollback (netplay_ghost.h).
 * A random pad seldom reaches the ball, and two of them seldom meet there; this one plays
 * the way people do: runs its controlled player at the ball, re-aiming every few frames,
 * slides in when an opponent has it close by, weaves at the other goal when it has it
 * and now and then shoots, with a random re-aim among the rest.
 *
 * It reads what THIS peer has on screen -- the frame just played, which under rollback may
 * be a guess -- exactly as a person reacts to what they see. Only this peer's input
 * depends on it, and GGPO carries that input to the other side, so it cannot desync. */
extern TeamGeneralInfo _topTeamData, _bottomTeamData;
extern Sprite _ballSprite;

static uint8_t chaseInput(void)
{
    static uint8_t held;
    static int32_t framesLeft;
    TeamGeneralInfo *me, *them;
    const Sprite *pl, *keeper;
    uint32_t x;
    int32_t dx, dy;
    uint8_t v = 0;

    if (framesLeft-- > 0)
        return held;
    x = s_np.chaseInput;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_np.chaseInput = x ? x : 0x9E3779B9u;

    me = _topTeamData.playerNumber == s_np.player ? &_topTeamData
       : _bottomTeamData.playerNumber == s_np.player ? &_bottomTeamData
       : s_np.player == 1 ? &_topTeamData : &_bottomTeamData;
    them = me == &_topTeamData ? &_bottomTeamData : &_topTeamData;
    pl = me->controlledPlayer;
    framesLeft = (int32_t)((x >> 16) % 5u) + 2;         /* a human re-aims every 2..6 frames */

    if (!pl || (x % 100u) < 12) {                       /* nobody to steer, or a stray push */
        static const uint8_t k_dirs[8] = { 0x01, 0x02, 0x04, 0x08, 0x05, 0x09, 0x06, 0x0A };
        held = k_dirs[(x >> 8) % 8u];
        return held;
    }
    if (me->playerHasBall) {
        /* Away from our own keeper, weaving. */
        keeper = me->spritesTable ? me->spritesTable[0] : NULL;
        dy = keeper && keeper->y.s.i > pl->y.s.i ? -1 : 1;
        dx = (int32_t)((x >> 4) % 3u) - 1;
        if ((x >> 12) % 100u < 3)
            v |= 0x20;                                  /* have a shot */
    } else {
        dx = _ballSprite.x.s.i - pl->x.s.i;
        dy = _ballSprite.y.s.i - pl->y.s.i;
        if (them->playerHasBall && dx > -10 && dx < 10 && dy > -10 && dy < 10 &&
            (x >> 12) % 100u < 30)
            v |= 0x20;                                  /* slide in */
        else if (!them->playerHasBall && dx > -6 && dx < 6 && dy > -6 && dy < 6 &&
                 (x >> 12) % 100u < 25)
            v |= 0x20;                                  /* a loose ball at our feet: kick it
                                                         * (the kick-off waits for this) */
        /* A diagonal only when both axes matter, as a thumb on a pad does. */
        if (dx > -3 && dx < 3) dx = 0;
        if (dy > -3 && dy < 3) dy = 0;
    }
    if (dy < 0) v |= 0x01;
    if (dy > 0) v |= 0x02;
    if (dx < 0) v |= 0x04;
    if (dx > 0) v |= 0x08;
    held = v;
    if (v & 0x20)
        framesLeft = 1;                                 /* a tap, not a hold */
    return held;
}

/* SWOS_SYNCTEST_RANDOM_INPUT=<seed>, direct and synctest only: both players' input from
 * the live match's generator instead of the recording, which then only sets the match
 * up. The corpus's own inputs never reached what a random pad does -- a live desync
 * under heavy rollback (2026-09-19, Mac v Windows, the relay's awful profile) parted on
 * a booking with player 2's bench controls in play -- and synctest is that rollback,
 * deterministic, in one process: every frame played again from a loaded state, and any
 * state a load does not put back named. The match no longer follows its recording, so
 * the rig's per-frame comparison with it is off. */
static RandomPad s_testPad[MATCH_PLAYERS];
static int32_t   s_testRandom = -1;         /* -1: not read yet */

static int32_t testRandomInput(void)
{
    if (s_testRandom < 0) {
        const char *v = getenv("SWOS_SYNCTEST_RANDOM_INPUT");
        uint32_t seed = (v && v[0]) ? (uint32_t)strtoul(v, NULL, 0) : 0;
        s_testRandom = seed != 0;
        s_testPad[0].state = seed | 1u;
        s_testPad[1].state = (seed * 2654435761u) | 1u;
        if (s_testRandom)
            fprintf(stderr, "[netplay] both players' input from a generator, seed 0x%08X "
                            "(SWOS_SYNCTEST_RANDOM_INPUT)\n", seed);
    }
    return s_testRandom;
}

/* direct and synctest: one process, both players' inputs, one frame per pass. */
static int32_t singleProcessFrame(void)
{
    MatchPlayerInput in[MATCH_PLAYERS];
    uint8_t local[INPUTS_SIZE], synced[INPUTS_SIZE];
    uint32_t frame;
    GameFrameResult r;
    int32_t p, rc, disconnect = 0;

    /* The frame boundary: the rollback rig's trace, and this frame's record. */
    ReplayTest_FrameStart();
    frame = ReplayTest_FrameIndex();
    ReplayTest_RecordedInput(frame, in);
    if (testRandomInput()) {
        for (p = 0; p < MATCH_PLAYERS; p++) {
            in[p].controls = randomPad(&s_testPad[p]);
            in[p].key = 0;
        }
    }
    for (p = 0; p < MATCH_PLAYERS; p++)
        MatchStep_PackInput(&in[p], local + p * MATCH_INPUT_SIZE);

    if (s_np.mode == NETPLAY_MATCH_DIRECT) {
        if (frame == 0 && s_np.tracePath)
            traceRecordNow(0);
        soundFrameBegin((int32_t)frame, 0);
        r = step(local, (int32_t)frame, !testRandomInput());
        soundFrameEnd();
        if (r == GAME_FRAME_DONE && s_np.tracePath)
            traceRecordNow(frame + 1);
    } else {
        Netplay_Idle(s_np.session);
        for (p = 0; p < MATCH_PLAYERS; p++) {
            rc = Netplay_AddLocalInput(s_np.session, p + 1, local + p * MATCH_INPUT_SIZE);
            if (rc != NETPLAY_OK)
                fail("frame %u: adding player %d's input: %s", frame, p + 1, Netplay_ErrorName(rc));
        }
        rc = Netplay_SyncInput(s_np.session, synced, &disconnect);
        if (rc != NETPLAY_OK)
            fail("frame %u: synchronize input: %s", frame, Netplay_ErrorName(rc));
        /* Both players are local and there is no input delay: GGPO hands back what was added. */
        if (memcmp(synced, local, sizeof local) != 0)
            fail("frame %u: GGPO returned other inputs than the frame's own", frame);
        soundFrameBegin((int32_t)frame, 0);
        r = step(synced, (int32_t)frame, !testRandomInput());
        soundFrameEnd();
        /* Where synctest rolls back and re-simulates, every checkDistance frames. */
        if (r == GAME_FRAME_DONE)
            Netplay_AdvanceFrame(s_np.session, frameChecksum());
    }

    if (r == GAME_FRAME_DONE) {
        s_np.frames++;
        ReplayTest_FrameDone();
        Platform_Present();
    } else if (r == GAME_FRAME_DETOUR) {
        /* The highlights and instant-replay loops run frames of their own, which nothing
         * can roll back. Ended like any other frame, it is reported at the next frame
         * boundary, as the replay does without netplay. */
        GameLoop_EndFrame(1);
    }
    return r;
}

/* The joystick history the game keeps between frames -- the registry's "controls"
 * block (src/sim_state.c) -- back to its boot state, all zero.
 *
 * Player1StatusProc/Player2StatusProc carry a fire counter, a last-fired flag and the
 * was/press edge detectors from one call to the next, and they run in the MENUS too. A
 * player who pressed START left their counter mid-count: two peers came out of the
 * lobby with _pl1FireCounter 2 against 0, and desynced on frame 1. The directions
 * themselves are recomputed from the registers on every call; they are cleared only
 * so the whole block starts the same. The live test never met this -- it skips the
 * front end, so its controls were still at boot. */
static void resetControlHistory(void)
{
    extern uint16_t _pl1Direction[1], _pl2Direction[1];
    extern uint8_t  _pl1Fire, _pl2Fire, _pl1ShortFire, _pl2ShortFire;
    extern uint8_t  _pl1Up, _pl1Down, _pl1Left, _pl1Right, _pl2Up, _pl2Down, _pl2Left, _pl2Right;
    extern uint8_t  _pl1UpRight, _pl1UpLeft, _pl1DownLeft, _pl1DownRight;
    extern uint8_t  _pl2UpRight, _pl2UpLeft, _pl2DownLeft, _pl2DownRight;
    extern uint8_t  _pl1WasUp, _pl1WasDown, _pl1WasLeft, _pl1WasRight;
    extern uint8_t  _pl2WasUp, _pl2WasDown, _pl2WasLeft, _pl2WasRight;
    extern uint8_t  _pl1UpPress, _pl1DownPress, _pl1LeftPress, _pl1RightPress;
    extern uint8_t  _pl2UpPress, _pl2DownPress, _pl2LeftPress, _pl2RightPress;
    extern uint16_t _pl1FireCounter[1], _pl2FireCounter[1];
    extern uint8_t  _pl1LastFired, _pl2LastFired, _subsFire;

    _pl1Direction[0] = _pl2Direction[0] = 0;
    _pl1Fire = _pl2Fire = _pl1ShortFire = _pl2ShortFire = 0;
    _pl1Up = _pl1Down = _pl1Left = _pl1Right = _pl2Up = _pl2Down = _pl2Left = _pl2Right = 0;
    _pl1UpRight = _pl1UpLeft = _pl1DownLeft = _pl1DownRight = 0;
    _pl2UpRight = _pl2UpLeft = _pl2DownLeft = _pl2DownRight = 0;
    _pl1WasUp = _pl1WasDown = _pl1WasLeft = _pl1WasRight = 0;
    _pl2WasUp = _pl2WasDown = _pl2WasLeft = _pl2WasRight = 0;
    _pl1UpPress = _pl1DownPress = _pl1LeftPress = _pl1RightPress = 0;
    _pl2UpPress = _pl2DownPress = _pl2LeftPress = _pl2RightPress = 0;
    _pl1FireCounter[0] = _pl2FireCounter[0] = 0;
    _pl1LastFired = _pl2LastFired = _subsFire = 0;
}


/* p2p: a pass that plays no frame still shows one, and yields to the peer's process. */
static void waitForPeer(void)
{
    Platform_Present();
    SDL_Delay(1);
}

/* The frame the match ends at, once nothing can change it: the first frame at which the
 * match is over, or the end of the recording. Only frames that are final count -- played,
 * and with every input before them confirmed, so that no rollback saves them again. */
static void findEnd(int32_t frame, int32_t confirmed)
{
    int64_t last = (int64_t)confirmed + 1;
    uint32_t f;

    if (last > frame)
        last = frame;
    for (f = s_np.endScanFrom; (int64_t)f <= last && f < s_np.traceLen; f++) {
        if (s_np.over[f]) {
            s_np.endFrame = (int32_t)f;
            return;
        }
    }
    s_np.endScanFrom = f;
    /* A corpus match also ends when the RECORDING does. A live match has no
     * recording -- ReplayTest_FrameCount() is 0 -- so this test would declare
     * the match over at frame 0, which is exactly what it did: both peers
     * synchronized, played one frame, and stopped with "frame 0 is confirmed:
     * the recording ends there". A live match ends only when the MATCH does,
     * which is the s_np.over[] scan above. */
    if (s_np.live)
        return;
    if (last >= (int64_t)ReplayTest_FrameCount() && f > ReplayTest_FrameCount())
        s_np.endFrame = (int32_t)ReplayTest_FrameCount();
}

/* A live match is over -- finished, every frame confirmed, or ended early -- and the
 * machine goes back to the menus: the session closed, the timer thread and the pad
 * back, one frame of input at a time no more. The mode goes OFF here, not in
 * NetplayMatch_Finish, because the game plays on after the last frame: the fade, and
 * everything InitAndPlayGame does on the way back to the lobby reads input and the
 * clock the ordinary way. (Before this, they ran with the session still claiming both,
 * and the first live match that reached its end through the menus waited for ever on
 * the friendly REPLAY / EXIT screen for a press the pad could no longer deliver.) */
/* The connection during a live match, in the log: GGPO's ping sampled once a second, and
 * every 30 s a line with its average, range and jitter (the mean change from one sample to
 * the next), the rollbacks and holds since the last line, and how far this peer runs
 * ahead. A lag report is then answerable from the two players' logs alone (docs/
 * NETPLAY_SETUP.md section 12). Wall-clock driven and read-only: none of it touches the match. */
static void logStatsLine(const char *what, int32_t n, int32_t sum, int32_t mn, int32_t mx,
                         int32_t jit)
{
    if (n <= 0) {
        fprintf(stderr, "[netplay] %s: no ping samples\n", what);
        return;
    }
    fprintf(stderr, "[netplay] %s: ping %d ms (%d..%d), jitter %d ms", what, sum / n, mn, mx,
            n > 1 ? jit / (n - 1) : 0);
}

static void logStats(uint32_t now, int32_t force)
{
    int32_t ping = -1, aheadX10 = 0;
    if (!s_np.live || !s_np.running || !s_np.session)
        return;
    if (!s_np.statStartMs) {                /* a new match, rematches included */
        s_np.statStartMs = now ? now : 1;
        s_np.statNextSampleMs = now;
        s_np.statNextReportMs = now + 30000;
        s_np.statLastPing = -1;
        s_np.statN = s_np.statSum = s_np.statJitSum = s_np.statMax = 0;
        s_np.statAllN = s_np.statAllSum = s_np.statAllJitSum = s_np.statAllMax = 0;
        s_np.statMin = s_np.statAllMin = 1 << 30;
        s_np.statLoads = s_np.loads;
        s_np.statWaits = s_np.predictionWaits;
    }
    if (now >= s_np.statNextSampleMs) {
        s_np.statNextSampleMs = now + 1000;
        Netplay_NetStats(s_np.session, 3 - s_np.player, &ping, &aheadX10);
        if (ping > 0) {
            int32_t d = s_np.statLastPing > 0 ? (ping > s_np.statLastPing ? ping - s_np.statLastPing
                                                                          : s_np.statLastPing - ping) : 0;
            s_np.statLastPing = ping;
            s_np.statN++;    s_np.statSum += ping;    s_np.statJitSum += d;
            s_np.statAllN++; s_np.statAllSum += ping; s_np.statAllJitSum += d;
            if (ping < s_np.statMin) s_np.statMin = ping;
            if (ping > s_np.statMax) s_np.statMax = ping;
            if (ping < s_np.statAllMin) s_np.statAllMin = ping;
            if (ping > s_np.statAllMax) s_np.statAllMax = ping;
        }
    }
    if (force || now >= s_np.statNextReportMs) {
        char what[48];
        uint32_t secs = (now - s_np.statStartMs) / 1000;
        s_np.statNextReportMs = now + 30000;
        Netplay_NetStats(s_np.session, 3 - s_np.player, &ping, &aheadX10);
        snprintf(what, sizeof what, "%u:%02u, frame %u", secs / 60, secs % 60, s_np.frames);
        logStatsLine(what, s_np.statN, s_np.statSum, s_np.statMin, s_np.statMax, s_np.statJitSum);
        if (s_np.statN > 0)
            fprintf(stderr, "; %u rollbacks, %u holds since the last line; %s%d.%d frames ahead\n",
                    s_np.loads - s_np.statLoads, s_np.predictionWaits - s_np.statWaits,
                    aheadX10 < 0 ? "-" : "", (aheadX10 < 0 ? -aheadX10 : aheadX10) / 10,
                    (aheadX10 < 0 ? -aheadX10 : aheadX10) % 10);
        s_np.statLoads = s_np.loads;
        s_np.statWaits = s_np.predictionWaits;
        s_np.statN = s_np.statSum = s_np.statJitSum = s_np.statMax = 0;
        s_np.statMin = 1 << 30;
    }
}

static void liveEnd(int32_t outcome, const char *why)
{
    /* A match that ends EARLY has its frames written too, whatever ended it. The peer
     * that notices the desync first ends the match, and the other one then sees the
     * disconnect before its own desync event and would never write its half -- and half a
     * pair proves nothing, because the whole method is diffing the two. */
    if (outcome != NETPLAY_LIVE_FINISHED)
        ringDump(-1);
    /* What this player drew against what happened -- an early end too, which is when
     * it is most wanted: frames up to the last confirmed one are final. */
    Ghost_Report(outcome == NETPLAY_LIVE_FINISHED && s_np.endFrame >= 0 ? s_np.endFrame
                                                                        : s_np.lastConfirmed);
    if (s_np.live && s_np.statStartMs) {
        logStatsLine("whole match", s_np.statAllN, s_np.statAllSum, s_np.statAllMin,
                     s_np.statAllMax, s_np.statAllJitSum);
        if (s_np.statAllN > 0)
            fputc('\n', stderr);
        s_np.statStartMs = 0;
    }
    fprintf(stderr, "[netplay] live match %s at frame %d%s%s: %u frames played, %u re-simulated "
                    "after %u rollbacks; held at the prediction limit %u times; time sync asked "
                    "for %u waits, %lld ms in all, ending %lld us a frame slower\n",
            outcome == NETPLAY_LIVE_FINISHED ? "finished" : "ended early",
            s_np.endFrame >= 0 ? s_np.endFrame : (int32_t)s_np.frames, why ? ": " : "", why ? why : "",
            s_np.frames, s_np.resimulated, s_np.loads, s_np.predictionWaits, s_np.timeSyncs,
            (long long)(s_np.pacedUs / 1000), (long long)s_np.paceBiasUs);
    /* PRED's premise, measured: every guessed frame's guess was the last input known. */
    fprintf(stderr, "[pred] %u frames first played on a guess of the other player's input; "
                    "%u of the guesses were not their last known input%s\n",
            s_np.guessedFrames, s_np.guessNotRepeat,
            s_np.guessNotRepeat ? " -- PRED's guesses cannot be rebuilt from INPT" : "");
    printf("[live] final state hash %016llx\n", (unsigned long long)SimState_Hash(SIM_TIER_ALL));
    /* The SIM tier alone is what two peers must agree on. The VIEW tier holds the
     * highlight ring, recorded in drawn coordinates, so it differs between peers on
     * different screen widths without the match doing so. */
    printf("[live] final sim hash %016llx\n", (unsigned long long)SimState_Hash(SIM_TIER_SIM));
    {
        /* One tick a frame, as the Amiga (NetplayMatch_OwnsClock): the clocks start at 0
         * with the match, so they end at its frame. relay_test.sh holds it to that. */
        extern volatile uint16_t _currentTick;
        printf("[live] clock: tick %u at frame %d\n", (unsigned)_currentTick,
               s_np.endFrame >= 0 ? s_np.endFrame : (int32_t)s_np.frames);
    }
    fflush(stdout);
    /* A match that FINISHED is its frames up to the end and no more. A player whose game
     * ran ahead of the other's at the end played frames past it on a guess; they were in
     * this player's file and not the recorder's ("the match ended after 25556 of 25559
     * frames", a recording of 2026-10-08), and the two files differed. */
    if (outcome == NETPLAY_LIVE_FINISHED && s_np.endFrame >= 0) {
        extern void ReplayRec_SetFrame(uint32_t frame);
        ReplayRec_SetFrame((uint32_t)s_np.endFrame);
    }
    recordConfirmed(s_np.lastConfirmed);
    MatchRecord_End(outcome == NETPLAY_LIVE_FINISHED,
                    s_np.endFrame >= 0 ? s_np.endFrame : (int32_t)s_np.frames,
                    SimState_Hash(SIM_TIER_SIM), why);
    NetplayMatch_Finish();              /* reports, closes the session, drops the sound filter */
    s_liveOutcome = outcome;
    snprintf(s_liveWhy, sizeof s_liveWhy, "%s", why ? why : "");
    s_np.mode = NETPLAY_MATCH_OFF;
    s_np.live = 0;
    _gFrameStepInput = 0;
    _sim_draw_width = _screen_width;    /* this display's again (display.h) */
    Platform_ResumeTimerThread();
}

static void p2pFinish(void)
{
    ReplayDriverState st;

    /* Every frame is final now. Before the live callback: a test exits there, and
     * liveEnd -- which reports a match that ends early -- would never run. */
    Ghost_Report(s_np.endFrame);
    fprintf(stderr, "[netplay] frame %d is confirmed: %s\n", s_np.endFrame,
            s_np.live ? "the session stops there"
                      : (s_np.endFrame < (int32_t)ReplayTest_FrameCount()
                             ? "the match is over there" : "the recording ends there"));
    /* A corpus match hands back to the replay rig, whose finish() reports the
     * comparison and then EXITS THE PROCESS. A live match has no rig and must
     * not exit here: the caller still has to leave the match loop, hash the
     * final state and report it. Returning GAME_FRAME_MATCH_OVER (p2pFrame)
     * ends the match instead, and the process ends on its own terms. */
    if (s_np.live) {
        /* The callback first: a test hashes the final state there and exits. */
        if (s_liveFinished)
            s_liveFinished();
        liveEnd(NETPLAY_LIVE_FINISHED, NULL);
        return;
    }

    st.frame = (uint32_t)s_np.endFrame;
    ReplayTest_SetDriverState(&st);
    ReplayTest_Finish(0);
}

/* SWOS_LIVE_DESYNC_DUMP=<dir>: the last frames, in memory, written only if the peers part.
 *
 * SWOS_LIVE_DUMP_STREAM writes every frame and costs ~34 KB a frame, which is fine for a
 * 600-frame test and hopeless for the thing actually needed here: leaving two machines
 * playing for hours waiting for a desync that happens once in dozens of matches. So keep
 * the last frames in a ring instead and spend the disk only when GGPO says the two have
 * parted. The ring is longer than the detection lag (measured 16-23 frames,
 * docs/NETPLAY.md), so the frame that parted, and the frames either side of it, are still
 * in hand when the event arrives.
 *
 * The inputs go with them, for the whole match, not just the ring: 4 bytes a frame, so a
 * 20-minute match is 336 KB. They are what makes a caught desync REPRODUCIBLE -- a state
 * to start from and the inputs that were played, on any machine, offline, under whatever
 * sanitizer we like. Without them a desync is a field name and a shrug.
 *
 * The states are written in the same SWSP stream format the per-frame dump uses, so
 * --netplay-state-stream-diff reads them unchanged. */
#define DESYNC_RING_FRAMES 96

static struct {
    uint8_t  *state[DESYNC_RING_FRAMES];
    int32_t   frame[DESYNC_RING_FRAMES];
    uint32_t  size;                 /* portable state bytes, 0 until armed */
    int32_t   next;                 /* ring cursor */
    int32_t   matchNo;
    int32_t   armed;                /* -1 gave up, 0 untried, 1 running */
    int32_t   dumped;               /* one dump per match is enough */
    const char *dir;
    uint8_t  *inputs;               /* 4 bytes a frame from frame 0 */
    int32_t   inputCap, inputMax;   /* frames allocated, highest frame seen */
} s_ring;

static void ringNewMatch(void)
{
    int32_t i;
    for (i = 0; i < DESYNC_RING_FRAMES; i++)
        s_ring.frame[i] = -1;
    s_ring.next = 0;
    s_ring.dumped = 0;
    s_ring.inputMax = -1;
    s_ring.matchNo++;
    s_ring.armed = 0;
}

static void ringRecord(int32_t frame, const uint8_t *inputs)
{
    int32_t slot;

    if (s_ring.armed < 0 || frame < 0)
        return;
    if (s_ring.armed == 0) {
        s_ring.armed = -1;
        s_ring.dir = getenv("SWOS_LIVE_DESYNC_DUMP");
        if (!s_ring.dir || !s_ring.dir[0])
            return;
        s_ring.size = SimState_PortableSize(SIM_TIER_ALL);
        for (slot = 0; slot < DESYNC_RING_FRAMES; slot++) {
            if (!s_ring.state[slot] && !(s_ring.state[slot] = (uint8_t *)malloc(s_ring.size))) {
                fprintf(stderr, "[netplay] no memory for the desync ring\n");
                return;
            }
        }
        s_ring.armed = 1;
        fprintf(stderr, "[netplay] keeping the last %d frames for a desync dump (%u bytes a "
                        "frame, %s)\n", DESYNC_RING_FRAMES, s_ring.size, s_ring.dir);
    }

    /* A frame GGPO re-simulates overwrites its own slot rather than taking a new one, so
     * the ring holds FRAMES, not passes, and always reaches back the same distance. */
    for (slot = 0; slot < DESYNC_RING_FRAMES; slot++)
        if (s_ring.frame[slot] == frame)
            break;
    if (slot == DESYNC_RING_FRAMES) {
        slot = s_ring.next;
        s_ring.next = (s_ring.next + 1) % DESYNC_RING_FRAMES;
    }
    s_ring.frame[slot] = frame;
    SimState_SavePortable(s_ring.state[slot], SIM_TIER_ALL);

    if (frame >= s_ring.inputCap) {
        int32_t want = frame + 4096;
        uint8_t *grown = (uint8_t *)realloc(s_ring.inputs, (size_t)want * 4);
        if (!grown)
            return;
        memset(grown + (size_t)s_ring.inputCap * 4, 0, (size_t)(want - s_ring.inputCap) * 4);
        s_ring.inputs = grown;
        s_ring.inputCap = want;
    }
    memcpy(s_ring.inputs + (size_t)frame * 4, inputs, MATCH_PLAYERS * MATCH_INPUT_SIZE);
    if (frame > s_ring.inputMax)
        s_ring.inputMax = frame;
}

static void ringDump(int32_t desyncFrame)
{
    char path[512];
    uint8_t hdr[16], head[4];
    uint64_t layout;
    FILE *f;
    int32_t i, lo, hi, frame, written = 0;

    if (s_ring.armed != 1 || s_ring.dumped)
        return;
    s_ring.dumped = 1;

    lo = hi = -1;
    for (i = 0; i < DESYNC_RING_FRAMES; i++) {
        if (s_ring.frame[i] < 0)
            continue;
        if (lo < 0 || s_ring.frame[i] < lo) lo = s_ring.frame[i];
        if (s_ring.frame[i] > hi)           hi = s_ring.frame[i];
    }
    if (lo < 0)
        return;

    /* Named by MATCH and peer, not by the desync frame: the two peers notice at slightly
     * different frames (200 and 201 in a test), and files that do not pair up by name are
     * files nobody diffs. */
    snprintf(path, sizeof path, "%s/desync-m%d-p%d.bin", s_ring.dir, s_ring.matchNo,
             s_np.player);
    if ((f = fopen(path, "wb")) == NULL) {
        fprintf(stderr, "[netplay] cannot write %s\n", path);
        return;
    }
    memcpy(hdr, "SWSP", 4);
    hdr[4] = (uint8_t)(s_ring.size);       hdr[5] = (uint8_t)(s_ring.size >> 8);
    hdr[6] = (uint8_t)(s_ring.size >> 16); hdr[7] = (uint8_t)(s_ring.size >> 24);
    layout = SimState_LayoutHash();
    for (i = 0; i < 8; i++)
        hdr[8 + i] = (uint8_t)(layout >> (8 * i));
    fwrite(hdr, 1, sizeof hdr, f);

    for (frame = lo; frame <= hi; frame++) {
        for (i = 0; i < DESYNC_RING_FRAMES; i++) {
            if (s_ring.frame[i] != frame)
                continue;
            head[0] = (uint8_t)(frame);       head[1] = (uint8_t)(frame >> 8);
            head[2] = (uint8_t)(frame >> 16); head[3] = (uint8_t)(frame >> 24);
            fwrite(head, 1, sizeof head, f);
            fwrite(s_ring.state[i], 1, s_ring.size, f);
            written++;
            break;
        }
    }
    fclose(f);

    /* The inputs, from frame 0: <frame:4 LE><p1:2><p2:2> each, so the match can be
     * played again offline instead of waited for again. */
    snprintf(path, sizeof path, "%s/desync-m%d-p%d.inputs", s_ring.dir, s_ring.matchNo,
             s_np.player);
    if ((f = fopen(path, "wb")) != NULL) {
        for (frame = 0; frame <= s_ring.inputMax; frame++) {
            head[0] = (uint8_t)(frame);       head[1] = (uint8_t)(frame >> 8);
            head[2] = (uint8_t)(frame >> 16); head[3] = (uint8_t)(frame >> 24);
            fwrite(head, 1, sizeof head, f);
            fwrite(s_ring.inputs + (size_t)frame * 4, 1, MATCH_PLAYERS * MATCH_INPUT_SIZE, f);
        }
        fclose(f);
    }
    fprintf(stderr, "[netplay] desync at frame %d: dumped frames %d..%d (%d) and %d frames of "
                    "input to %s\n", (int)desyncFrame, lo, hi, written, s_ring.inputMax + 1,
            s_ring.dir);
    fflush(stderr);
}

/* SWOS_LIVE_DUMP_STREAM=<path>: EVERY frame's state, appended as it is played.
 *
 * SWOS_LIVE_DUMP_FRAME asks which frame to dump, and between two machines that
 * question cannot be answered in advance: the frame two peers part on is only known
 * once both logs are side by side, and by then the match is over. A live match does
 * not repeat either -- each run starts from its own clock and plays a different match
 * -- so "run it again and dump frame F" narrows nothing. Write them all instead: one
 * open, one buffered write per frame, ~21 MB for 600 frames, and
 * --netplay-state-stream-diff finds the first differing frame afterwards, with the
 * frame before it still in hand.
 *
 * Records carry GGPO's frame number, not the step count, so a re-simulated frame is a
 * second record for the same number (last one wins) rather than a shift of everything
 * after it. <path> gets the match ordinal appended, because the lobby plays a rematch
 * and the second match must not overwrite the first.
 *
 * The PORTABLE encoding, not SimState_Save: two peers are two processes, and their raw
 * saves differ in every pointer region before the match has done anything at all --
 * a local pair test comes out 34 regions apart on frame 0 and still ends on the same
 * hash. The portable encoding is what SimState_Hash covers (pointers as identities,
 * typed fields big-endian), so a stream difference means what a hash difference means. */
static struct {
    FILE    *out;
    uint8_t *buf;
    uint32_t size;
    int32_t  matchNo;
    int32_t  state;      /* 0 = not opened for this match, 1 = open, -1 = gave up */
    int32_t  warned;
} s_stream;

static void streamNewMatch(void)
{
    if (s_stream.out) {
        fclose(s_stream.out);
        s_stream.out = NULL;
    }
    s_stream.matchNo++;
    s_stream.state = 0;
}

static void streamState(int32_t frame)
{
    uint8_t head[4];
    int32_t i;

    if (s_stream.state < 0)
        return;
    if (s_stream.state == 0) {
        const char *path = getenv("SWOS_LIVE_DUMP_STREAM");
        char full[512];
        uint8_t hdr[16];
        uint64_t layout;

        s_stream.state = -1;
        if (!path || !path[0])
            return;
        s_stream.size = SimState_PortableSize(SIM_TIER_ALL);
        snprintf(full, sizeof full, "%s.m%d-p%d.bin", path, s_stream.matchNo, s_np.player);
        if ((s_stream.out = fopen(full, "wb")) == NULL) {
            fprintf(stderr, "[netplay] cannot write the state stream %s\n", full);
            return;
        }
        if (!s_stream.buf && (s_stream.buf = (uint8_t *)malloc(s_stream.size)) == NULL) {
            fclose(s_stream.out);
            s_stream.out = NULL;
            return;
        }
        /* The layout hash rides along so a diff can refuse two peers whose registries
         * are not the same registry -- different builds name different bytes. */
        memcpy(hdr, "SWSP", 4);
        hdr[4] = (uint8_t)(s_stream.size);
        hdr[5] = (uint8_t)(s_stream.size >> 8);
        hdr[6] = (uint8_t)(s_stream.size >> 16);
        hdr[7] = (uint8_t)(s_stream.size >> 24);
        layout = SimState_LayoutHash();
        for (i = 0; i < 8; i++)
            hdr[8 + i] = (uint8_t)(layout >> (8 * i));
        fwrite(hdr, 1, sizeof hdr, s_stream.out);
        s_stream.state = 1;
        fprintf(stderr, "[netplay] streaming every frame's state to %s (%u bytes a frame)\n",
                full, s_stream.size);
    }
    head[0] = (uint8_t)(frame);
    head[1] = (uint8_t)(frame >> 8);
    head[2] = (uint8_t)(frame >> 16);
    head[3] = (uint8_t)(frame >> 24);
    if (SimState_SavePortable(s_stream.buf, SIM_TIER_ALL) != 0 && !s_stream.warned) {
        /* A pointer with no identity is written as a value no load accepts, so the
         * frame would compare as different for a reason that is not the desync. */
        fprintf(stderr, "[netplay] the state stream holds pointers with no identity -- "
                        "frame %d onwards cannot be compared between peers\n", frame);
        s_stream.warned = 1;
    }
    fwrite(head, 1, sizeof head, s_stream.out);
    fwrite(s_stream.buf, 1, s_stream.size, s_stream.out);
    /* Flushed every frame: a match that ends in a hang, a kill or a disconnect is
     * exactly the one whose stream is worth having. */
    fflush(s_stream.out);
}

/* p2p: this peer's player's recorded input, GGPO's prediction of the other's, and a frame
 * whenever GGPO allows one. */
static int32_t p2pFrame(void)
{
    MatchPlayerInput in[MATCH_PLAYERS];
    uint8_t local[MATCH_INPUT_SIZE], synced[INPUTS_SIZE];
    int32_t frame = 0, confirmed = -1, rc, disconnect = 0;
    uint32_t now = SDL_GetTicks();
    GameFrameResult r;

    if (!s_np.prepared) {
        int32_t f;
        /* A LIVE match is already standing at kick-off: the menus set the teams
         * up and the match started normally, and BOTH peers did the same thing
         * because the host sent the setup. There is no recording to restore
         * from, and restoring one would throw that away. */
        if (s_np.live) {
            s_np.prepared = 1;
            s_np.lastProgressMs = now;
            /* (A stall's backlog is dropped, as off line: CATCH UP, which played it back to
             * back -- fd911618 -- was removed 2026-10-30; it caused netplay problems.) */
            resetControlHistory();      /* before GGPO saves frame 0 */
            /* And the clock, AGAIN, here. StartLive zeroed it, but the pre-match screens
             * run in between -- the stadium line-up and its waits -- and the SDL timer
             * thread keeps ticking through them by the wall clock. Two peers spend
             * different wall time there, so they reached this frame 0x1B against 0x1D
             * and desynced on frame 1 (reproduced with SWOS_PRESENT_CAPTURE slowing one
             * of them). The replay rig resets at exactly this point for the same reason
             * (applyFirstFrameSetup); these are its values, and its note says why
             * _lastGameTick starts one behind. */
            {
                extern volatile uint16_t _currentGameTick;
                extern volatile uint16_t _currentTick;
                extern uint16_t          _lastGameTick;
                extern uint16_t          _lastFrameTicks;
                _currentGameTick = 0;
                _currentTick     = 0;
                _lastGameTick    = 0xFFFF;
                /* What frame 0's ReadTimerDelta will compute from the two above. It
                 * is set now as well because GGPO saves frame 0 BEFORE that runs, and a
                 * leftover here is the one byte that differed in the kick-off dumps. */
                _lastFrameTicks  = 1;
            }
            /* SWOS_LIVE_DUMP=<dir>: the state this peer kicks off from, for
             * --netplay-state-diff. Taken HERE, at the first frame step, so it is the
             * same point however the match was reached -- the live test or the
             * lobby menu, whose pre-match screens run in between. */
            {
                const char *dir = getenv("SWOS_LIVE_DUMP");
                if (dir && dir[0]) {
                    char path[512];
                    uint32_t n = SimState_Size(SIM_TIER_ALL);
                    uint8_t *buf = (uint8_t *)malloc(n);
                    snprintf(path, sizeof path, "%s/kickoff-p%d.bin", dir, s_np.player);
                    if (buf) {
                        FILE *f;
                        SimState_Save(buf, SIM_TIER_ALL);
                        if ((f = fopen(path, "wb")) != NULL) {
                            fwrite(buf, 1, n, f);
                            fclose(f);
                            fprintf(stderr, "[netplay] kick-off state dumped to %s\n", path);
                        }
                        free(buf);
                    }
                }
            }
            goto prepared;
        }
        /* Before GGPO saves frame 0, which it does when the first input is added. */
        ReplayTest_PrepareFirstFrame();
        s_np.prepared = 1;
        s_np.lastProgressMs = now;
        /* An input delay of d plays the first d frames with no input from this player. */
        for (f = 0; f < s_np.delay; f++) {
            ReplayTest_RecordedInput((uint32_t)f, in);
            if (in[s_np.player - 1].controls || in[s_np.player - 1].key) {
                fprintf(stderr, "[netplay] frame %d of the recording carries player %d's input, "
                                "which an input delay of %d drops: this match cannot equal the "
                                "plain run\n", f, s_np.player, s_np.delay);
                break;
            }
        }
    }
prepared:

    Netplay_Idle(s_np.session);        /* the peer's inputs arrive; a misprediction rolls back */
    if (s_np.live && s_np.abortWhy) {
        liveEnd(NETPLAY_LIVE_ENDED_EARLY, s_np.abortWhy);
        return GAME_FRAME_MATCH_OVER;
    }
    /* The keyboard, emptied before every frame, and ESC is a way out.
     *
     * A live match sends NO key to the other side (InputSDL_LocalMatchControls): R, H,
     * P, S and ESC all leave the match loop, which a session cannot roll back. But that
     * only filtered what was SENT. The event pump still filled this machine's key
     * buffer, so GetKey/MainKeysCheck ran every press here and nowhere else -- Q called
     * the bench, R a replay -- and the two games parted: "pressing F or Q during a
     * match throws you back to the lobby" as THE TWO GAMES WENT OUT OF STEP (ReportBot
     * 0011). The simulation must see the same key buffer on both machines, and the only
     * one both can see is an empty one.
     *
     * ESC was the same desync, which made leaving a match look like one. Now it ends the
     * match here, cleanly -- the other side sees the session close and says THE OTHER
     * PLAYER LEFT. */
    if (s_np.live) {
        extern volatile uint16_t _keyCount;
        extern volatile uint8_t  _keyBuffer[];
        extern uint8_t           _keycodeLut[128];
        int32_t i, leave = s_leaveRequested;
        for (i = 0; i < (int32_t)_keyCount; i++)
            if (_keycodeLut[_keyBuffer[i] & 0x7F] == 0x1B)
                leave = 1;
        _keyCount = 0;
        s_leaveRequested = 0;
        if (leave) {
            liveEnd(NETPLAY_LIVE_ENDED_EARLY, "YOU LEFT THE MATCH");
            return GAME_FRAME_MATCH_OVER;
        }
        /* The window was closed (or the game was asked to quit): the same way out, so
         * the match is saved and the recorder and the other player are told before the
         * game goes. GameLoop_Frame leaves this to us in a live match (game_loop.c). */
        {
            extern uint16_t _quitToDos;
            if (_quitToDos) {
                liveEnd(NETPLAY_LIVE_ENDED_EARLY, "YOU CLOSED THE GAME");
                return GAME_FRAME_MATCH_OVER;
            }
        }
    }
    if (s_np.live)
        logStats(now, 0);
    if (s_np.live) {
        /* Until the two sessions synchronize, no frame runs at all -- and from
         * the outside that is indistinguishable from a match that is simply
         * slow. Say which it is, once a second. */
        static uint32_t lastSay;
        if (!s_np.running && now - lastSay > 1000) {
            lastSay = now;
            printf("[live] waiting for the peer's session...\n");
            fflush(stdout);
        }
    }
    Netplay_CurrentFrame(s_np.session, &frame);
    Netplay_ConfirmedFrame(s_np.session, &confirmed);
    if (confirmed > s_np.lastConfirmed) {
        s_np.lastConfirmed = confirmed;
        s_np.lastProgressMs = now;
    }
    if (s_np.live)
        recordConfirmed(s_np.lastConfirmed);
    /* Half time, and the final whistle: once every frame up to it is confirmed, save the
     * match so far (replay_v2.c ReplayRec_Checkpoint -- on a thread: built here it stalled
     * this player for 155 ms). A game that goes without ending the match -- crashed,
     * killed, the power -- had saved nothing; now it has the match to the last whistle.
     * (The final one is only half a second before the match ends by itself: the half-time
     * one is the net that matters. Closing the window saves everything -- liveEnd.) */
    if (s_np.live) {
        extern uint16_t _gameState;
        int32_t whistle = _gameState == ST_FIRST_HALF_ENDED ? 1 : _gameState == ST_GAME_ENDED ? 2 : 0;
        if (whistle > s_np.checkpointed && s_np.resultFrame < 0)
            s_np.resultFrame = frame;
        if (s_np.resultFrame >= 0 && s_np.lastConfirmed >= s_np.resultFrame) {
            extern void ReplayRec_Checkpoint(void);
            s_np.checkpointed = whistle > s_np.checkpointed ? whistle : s_np.checkpointed + 1;
            fprintf(stderr, "[netplay] frame %d: %s is confirmed; saving the match so far\n", frame,
                    s_np.checkpointed == 1 ? "half time" : "the final whistle");
            ReplayRec_Checkpoint();
            s_np.resultFrame = -1;
        }
    }
    if (now - s_np.lastProgressMs > (uint32_t)s_np.timeoutMs) {
        if (!s_np.live)
            fail("nothing played or confirmed for %d s (%s, frame %d, confirmed %d)",
                 s_np.timeoutMs / 1000, s_np.running ? "running" : "never synchronized with the peer",
                 frame, confirmed);
        fprintf(stderr, "[netplay] nothing played or confirmed for %d s (frame %d, confirmed %d)\n",
                s_np.timeoutMs / 1000, frame, confirmed);
        liveEnd(NETPLAY_LIVE_ENDED_EARLY, s_np.running ? "THE CONNECTION WENT QUIET"
                                                       : "THE OTHER PLAYER NEVER ARRIVED");
        return GAME_FRAME_MATCH_OVER;
    }

    /* A live match normally ends when the match does. SWOS_LIVE_FRAMES=N stops
     * it at a set frame instead, so the pair test finishes in bounded time and
     * both peers report a final hash to compare -- a full match is thousands of
     * frames of wall clock. The limit is the same on both peers (same
     * environment, set by the script), so both stop at the same frame. */
    if (s_np.live && s_np.endFrame < 0 && s_np.running && s_np.liveFrameLimit > 0 &&
        (int32_t)s_np.frames >= s_np.liveFrameLimit) {
        s_np.endFrame = frame;
        printf("[live] stopping at frame %d (SWOS_LIVE_FRAMES)\n", frame);
        fflush(stdout);
    }
    if (s_np.endFrame < 0 && s_np.running)
        findEnd(frame, confirmed);
    if (s_np.endFrame >= 0) {
        /* Stay until the peer has every input of ours, which it needs to reach the same end. */
        int32_t unacked = 1;
        if (!s_np.lingerStartMs)
            s_np.lingerStartMs = now ? now : 1;
        if (Netplay_UnackedInputs(s_np.session, MATCH_PLAYERS + 1 - s_np.player, &unacked) != NETPLAY_OK)
            unacked = 1;
        if (unacked == 0 || s_np.peerGone || now - s_np.lingerStartMs > LINGER_MS) {
            /* Read before p2pFinish: a live match hands the machine back there
             * (liveEnd), which clears the flag. */
            int32_t live = s_np.live;
            p2pFinish();
            /* A live match has to END, not just stop stepping: the session is
             * over, every frame is confirmed, and the caller (InitAndPlayGame)
             * needs to return so the state can be hashed and compared. Without
             * this the game loop keeps going with no session behind it and the
             * final hash is never reached. */
            if (live)
                return GAME_FRAME_MATCH_OVER;
        }
        waitForPeer();
        return GAME_FRAME_DONE;
    }
    /* A corpus match ends when the recording runs out. A LIVE match has no
     * recording to run out of -- it ends when the match does. */
    if (!s_np.running || (!s_np.live && (uint32_t)frame >= ReplayTest_FrameCount())) {
        waitForPeer();
        return GAME_FRAME_DONE;
    }

    /* SWOS_LIVE_SLOW_PCT=P: a machine whose clock runs P percent slow, for a test of the
     * pacing above. A sleep would not do it: the frame limiter keeps deadlines, and takes
     * any sleep back out of the next frame's. So one pass in every 100/P shows a frame
     * without playing one -- fewer frames per second, which is what a slow clock is. */
    if (s_np.slowPct > 0 && ++s_np.slowCount >= 100 / s_np.slowPct) {
        s_np.slowCount = 0;
        Platform_Present();
        return GAME_FRAME_DONE;
    }

    /* Input added now applies `delay` frames later.
     *
     * A LIVE match reads this machine's pad; a corpus match replays a recording.
     * Only the LOCAL player's half is packed either way -- the other player's
     * arrives from GGPO in Netplay_SyncInput below -- so live and recorded
     * differ in exactly one line, and everything after it is the same code the
     * corpus tests exercise. */
    if (s_np.live) {
        extern uint8_t InputSDL_LocalMatchControls(void);
        memset(in, 0, sizeof in);
        in[s_np.player - 1].controls =
            s_np.chaseInput ? chaseInput() :       /* asked for by name: beats a test's own */
            s_inputSource ? s_inputSource() :
            s_np.randomInput ? randomInput() : InputSDL_LocalMatchControls();
        in[s_np.player - 1].key = 0;       /* see InputSDL_LocalMatchControls */
    } else {
        ReplayTest_RecordedInput((uint32_t)(frame + s_np.delay), in);
    }
    MatchStep_PackInput(&in[s_np.player - 1], local);
    rc = Netplay_AddLocalInput(s_np.session, s_np.player, local);
    if (rc != NETPLAY_OK) {
        if (!Netplay_ShouldWait(rc)) {
            if (!s_np.live)
                fail("frame %d: adding player %d's input: %s", frame, s_np.player, Netplay_ErrorName(rc));
            fprintf(stderr, "[netplay] frame %d: adding player %d's input: %s\n", frame, s_np.player,
                    Netplay_ErrorName(rc));
            liveEnd(NETPLAY_LIVE_ENDED_EARLY, "A NETWORK ERROR ENDED THE MATCH");
            return GAME_FRAME_MATCH_OVER;
        }
        /* This pass's read is thrown away and the frame read again: a press it took
         * goes back (input_sdl.c, the press latch). */
        if (s_np.live && !s_np.chaseInput && !s_inputSource && !s_np.randomInput) {
            extern void InputSDL_UnreadMatchControls(void);
            InputSDL_UnreadMatchControls();
        }
        s_np.predictionWaits++;
        s_np.stalledPending = 1;
        waitForPeer();
        return GAME_FRAME_DONE;
    }
    /* READ: what was read for this frame and when, now that GGPO has taken it. */
    if (s_np.mode == NETPLAY_MATCH_P2P && frame >= 0)
    {
        /* ... and what the fire button did since the read before (SRP_READ_FIRE_*): only
         * when the controls really were this machine's. */
        extern uint8_t InputSDL_LastFireEvidence(void);
        uint8_t evidence = (s_np.live && !s_np.chaseInput && !s_inputSource && !s_np.randomInput)
                               ? InputSDL_LastFireEvidence() : 0;
        Ghost_Read(frame, local[0], SDL_GetTicks(), s_np.delay,
                   (uint8_t)((s_np.paceFramesLeft > 0 ? s_np.paceKind : 0) | evidence), 0);
    }
    rc = Netplay_SyncInput(s_np.session, synced, &disconnect);
    if (rc != NETPLAY_OK) {
        if (!s_np.live)
            fail("frame %d: synchronize input: %s", frame, Netplay_ErrorName(rc));
        fprintf(stderr, "[netplay] frame %d: synchronize input: %s\n", frame, Netplay_ErrorName(rc));
        liveEnd(NETPLAY_LIVE_ENDED_EARLY, "A NETWORK ERROR ENDED THE MATCH");
        return GAME_FRAME_MATCH_OVER;
    }
    if (s_np.mode == NETPLAY_MATCH_P2P && frame >= 0) {
        /* How far this frame runs on guesses: frames past the last one whose input from
         * both players is known (GGPO's confirmed frame -- this peer's own is always known
         * `delay` ahead). Taken BEFORE the frame is played, as the guess is. */
        int32_t confirmed = -1, lag;
        const uint8_t *theirs = synced + (2 - s_np.player) * MATCH_INPUT_SIZE;
        Netplay_ConfirmedFrame(s_np.session, &confirmed);
        lag = frame > confirmed ? frame - confirmed : 0;
        Ghost_Lag(frame, lag, s_np.stalledPending);
        s_np.stalledPending = 0;
        if (lag > 0 && lag < 64) {
            /* GGPO-X guesses by repeating the last input it has: the one at `confirmed`,
             * or nothing before the first. Checked, not assumed -- the whole of PRED
             * rests on it (docs/REPLAY_FORMAT_V2.md §4). */
            static const uint8_t none[MATCH_INPUT_SIZE];
            const uint8_t *want = confirmed >= 0 ? s_np.remoteIn[confirmed & 63] : none;
            s_np.guessedFrames++;
            if (memcmp(theirs, want, MATCH_INPUT_SIZE) != 0)
                s_np.guessNotRepeat++;
        }
        memcpy(s_np.remoteIn[frame & 63], theirs, MATCH_INPUT_SIZE);
    }
    soundFrameBegin(frame, 0);
    r = step(synced, frame, 0);
    soundFrameEnd();
    Ghost_Played(frame, 1);             /* what this peer draws */
    if (r != GAME_FRAME_DONE && r != GAME_FRAME_MATCH_OVER)
        fail("frame %d left the match loop (GameFrameResult %d), which a netplay match cannot "
             "roll back", frame, (int)r);
    s_np.frames++;
    s_np.lastProgressMs = now;
    /* A live match has no recording to compare against as it goes, so it says
     * where it is often enough to tell "playing" from "stalled", and prints the
     * state hash so two peers can be compared at the SAME frame without waiting
     * for the end. */
    if (s_np.live && (s_np.frames == 1 ||
                      (s_np.hashEvery > 0 && (s_np.frames % (uint32_t)s_np.hashEvery) == 0))) {
        printf("[live] frame %u  hash %016llx  (rollbacks %u)\n",
               s_np.frames, (unsigned long long)SimState_Hash(SIM_TIER_ALL),
               s_np.resimulated);
        fflush(stdout);
    }
    /* SWOS_LIVE_DUMP_FRAME=N[,N...] with SWOS_LIVE_DUMP=<dir>: the state at those frames,
     * on both peers, for --netplay-state-diff. The kick-off dump answers "did we start the
     * same match"; these answer "what had differed by the time the hashes parted", which is
     * the question a desync actually poses. */
    if (s_np.live && s_np.dumpFrameCount > 0) {
        int32_t i;
        for (i = 0; i < s_np.dumpFrameCount; i++) {
            const char *dir;
            if (s_np.frames != (uint32_t)s_np.dumpFrames[i])
                continue;
            dir = getenv("SWOS_LIVE_DUMP");
            if (dir && dir[0]) {
                char path[512];
                uint32_t n = SimState_Size(SIM_TIER_ALL);
                uint8_t *buf = (uint8_t *)malloc(n);
                snprintf(path, sizeof path, "%s/frame%d-p%d.bin", dir,
                         (int)s_np.dumpFrames[i], s_np.player);
                if (buf) {
                    FILE *f;
                    SimState_Save(buf, SIM_TIER_ALL);
                    if ((f = fopen(path, "wb")) != NULL) {
                        fwrite(buf, 1, n, f);
                        fclose(f);
                        fprintf(stderr, "[netplay] frame %d state dumped to %s\n",
                                (int)s_np.dumpFrames[i], path);
                    }
                    free(buf);
                }
            }
        }
    }
    if (s_np.live) {
        streamState(frame);
        ringRecord(frame, synced);
        if (frame >= 0)
            ReplayRec_SetInput((uint32_t)frame, synced);
    }
    Netplay_AdvanceFrame(s_np.session, frameChecksum());
    if (s_np.live && s_np.abortWhy) {
        liveEnd(NETPLAY_LIVE_ENDED_EARLY, s_np.abortWhy);
        return GAME_FRAME_MATCH_OVER;
    }
    Platform_Present();
    if (s_np.paceFramesLeft > 0 || s_np.paceBiasUs > 0) {
        /* The time sync's wait (above, NETPLAY_EVENT_TIMESYNC): each frame a little
         * longer, not one frame held. */
        int64_t us = s_np.paceBiasUs;
        if (s_np.paceFramesLeft > 0) {
            s_np.paceFramesLeft--;
            us += s_np.paceUsPerFrame;
        }
        s_np.pacedUs += us;
        Platform_DelayNextFrame((uint32_t)us);
    }
    if (s_np.stallMs) {
        /* A busy machine, now and then: the other peer plays on, predicting this one's
         * input, and rolls back when the real input arrives. */
        uint32_t x = s_np.stallRng;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        s_np.stallRng = x;
        if ((x & 15) == 0)
            SDL_Delay((x >> 8) % (uint32_t)s_np.stallMs + 1);
    }
    return GAME_FRAME_DONE;
}

/* ---- playing a recorded match again (--srp-resim, src/srp_resim.c) ----
 *
 * A resumable .SRP (replay_v2.h) holds the setup, the state at frame 0 and every frame's
 * input and SIM hash. This plays it again the way a live match is played -- the same
 * step, the same first-frame reset -- from the recorded state instead of a lobby, and
 * checks every frame's hash against the recording's. On the build and rules it was
 * recorded with, every frame must agree; the first that does not is where this machine
 * plays the match differently from the one that recorded it -- a desync, found offline. */
static struct {
    const uint8_t  *kickoff;
    uint32_t        kickoffLen;
    const uint8_t  *inputs;
    const uint32_t *hashes;
    uint32_t        frames, frame, matched;
    int32_t         loaded, firstDiff;
    uint32_t        diffHave, diffWant;
    /* What each player's own game showed (NetplayMatch_SetResimViews). */
    const uint8_t  *pred;           /* 2 bytes a frame, as PRED has them */
    const uint8_t  *seen;           /* SEEN whole */
    uint32_t        seenLen, seenAt;
    uint8_t        *ring[16];       /* the match's state at the start of the last 16 frames */
    uint32_t        viewFrames[2], viewSame[2], viewSkipped[2];
} s_resim;

/* ---- what each player's game SHOWED --------------------------------------------------
 *
 * The match as played is what both machines ended on. What a player WATCHED is his own
 * game the first time it showed each frame: the last frame it had the other player's real
 * input for (this frame less the lag PRED recorded), then every frame since played on a
 * guess -- the other player's input of that frame, repeated, which is what GGPO-X guesses
 * (docs/REPLAY_FORMAT_V2.md, "What each player SAW"). That is replayed here, for each
 * player and each frame his game first played on a guess, with the contest log taking the
 * LAST of those frames: the one he was shown. So the log can say what a player saw
 * happen at the ball before a rollback corrected it (docs/CONTEST_LOG.md, "Views").
 *
 * It checks itself: where SEEN says that player's screen drew a man or the ball, this
 * must put him there, and everywhere else where the match finally had him. */
static uint8_t *s_viewPred, *s_viewSeen;
static uint32_t s_viewSeenLen;

void NetplayMatch_SetResimViews(uint8_t *pred, uint8_t *seen, uint32_t seenLen)
{
    s_viewPred = pred;
    s_viewSeen = seen;
    s_viewSeenLen = seenLen;
}

static void viewPositions(int16_t (*xy)[2])
{
    extern Sprite *_teamSpritesTable[];
    extern Sprite  _ballSprite;
    int32_t i;
    for (i = 0; i < 22; i++) {
        const Sprite *s = _teamSpritesTable[i];
        xy[i][0] = s ? s->x.s.i : 0;
        xy[i][1] = s ? s->y.s.i : 0;
    }
    xy[22][0] = _ballSprite.x.s.i;
    xy[22][1] = (int16_t)(_ballSprite.y.s.i - _ballSprite.z.s.i);
}

/* Player `p`'s game at frame `f`, played from the state at the start of frame f - lag + 1
 * on the other player's input of frame f - lag. `final` is where the match had everything
 * after frame f. */
static void resimView(uint32_t f, int32_t p, uint32_t lag, int16_t (*final)[2])
{
    uint32_t first = f + 1 - lag, k, at;
    const uint8_t *guess;
    static const uint8_t none[MATCH_INPUT_SIZE];
    int16_t want[23][2], got[23][2];
    int32_t i, same = 1;

    guess = first > 0 ? s_resim.inputs + (size_t)(first - 1) * SRP_INPUT_BYTES + (1 - p) * MATCH_INPUT_SIZE
                      : none;
    SimState_Load(s_resim.ring[first & 15], SIM_TIER_ALL);
    for (k = first; k <= f; k++) {
        uint8_t in[SRP_INPUT_BYTES];
        GameFrameResult r;
        memcpy(in + p * MATCH_INPUT_SIZE,
               s_resim.inputs + (size_t)k * SRP_INPUT_BYTES + p * MATCH_INPUT_SIZE, MATCH_INPUT_SIZE);
        memcpy(in + (1 - p) * MATCH_INPUT_SIZE, guess, MATCH_INPUT_SIZE);
        ContestLog_SetFrame(k);
        ContestLog_SetView(p + 1, k != f);
        soundFrameBegin((int32_t)k, 1);
        r = step(in, -1, 0);
        soundFrameEnd();
        if (r != GAME_FRAME_DONE) {         /* a guess that leaves the match loop: not replayed */
            s_resim.viewSkipped[p]++;
            return;
        }
    }
    /* Where the recording says this screen drew everything. */
    memcpy(want, final, sizeof want);
    at = s_resim.seenAt;
    while (s_resim.seen && at + SRP_SEEN_REC_HEAD <= s_resim.seenLen &&
           BE_READ_U32(s_resim.seen + at) <= f) {
        const uint8_t *rec = s_resim.seen + at;
        uint32_t n = rec[5], o;
        if (at + SRP_SEEN_REC_HEAD + n * SRP_SEEN_OBJ_BYTES > s_resim.seenLen)
            break;
        if (BE_READ_U32(rec) == f && rec[4] == p + 1)
            for (o = 0; o < n; o++) {
                const uint8_t *q = rec + SRP_SEEN_REC_HEAD + o * SRP_SEEN_OBJ_BYTES;
                if (q[0] < 23) {
                    want[q[0]][0] = (int16_t)BE_READ_U16(q + 1);
                    want[q[0]][1] = (int16_t)BE_READ_U16(q + 3);
                }
            }
        at += SRP_SEEN_REC_HEAD + n * SRP_SEEN_OBJ_BYTES;
    }
    viewPositions(got);
    for (i = 0; i < 23; i++)
        if (got[i][0] != want[i][0] || got[i][1] != want[i][1])
            same = 0;
    s_resim.viewFrames[p]++;
    s_resim.viewSame[p] += same;
}

static void resimViews(uint32_t f)
{
    int16_t final[23][2];
    int32_t p, any = 0;

    viewPositions(final);
    for (p = 0; p < 2; p++) {
        uint8_t b = s_resim.pred[2 * f + p];
        uint32_t lag = b & SRP_PRED_LAG;
        if (b == SRP_PRED_UNKNOWN || lag == 0 || lag > 15 || lag > f + 1)
            continue;
        resimView(f, p, lag, final);
        any = 1;
    }
    /* SEEN's records are in frame order: step past this frame's. */
    while (s_resim.seen && s_resim.seenAt + SRP_SEEN_REC_HEAD <= s_resim.seenLen &&
           BE_READ_U32(s_resim.seen + s_resim.seenAt) <= f)
        s_resim.seenAt += SRP_SEEN_REC_HEAD + (uint32_t)s_resim.seen[s_resim.seenAt + 5] * SRP_SEEN_OBJ_BYTES;
    if (any) {
        /* Back to the match: the state after frame f, saved for the next frame's start. */
        SimState_Load(s_resim.ring[(f + 1) & 15], SIM_TIER_ALL);
        ContestLog_SetView(0, 0);
    }
}

int32_t NetplayMatch_StartResim(const uint8_t *kickoff, uint32_t kickoffLen, const uint8_t *inputs,
                                const uint32_t *hashes, uint32_t frames, int32_t simWidth)
{
    int32_t rc;

    memset(&s_np, 0, sizeof s_np);
    memset(&s_resim, 0, sizeof s_resim);
    s_np.mode = NETPLAY_MATCH_RESIM;
    s_resim.kickoff = kickoff;
    s_resim.kickoffLen = kickoffLen;
    s_resim.inputs = inputs;
    s_resim.hashes = hashes;
    s_resim.frames = frames;
    s_resim.firstDiff = -1;
    /* Each player's own view too, when the contest log is on and the recording has PRED. */
    if (g_contestLogOn && s_viewPred) {
        uint32_t size = SimState_Size(SIM_TIER_ALL), i;
        s_resim.pred = s_viewPred;
        s_resim.seen = s_viewSeen;
        s_resim.seenLen = s_viewSeenLen;
        s_resim.seenAt = SRP_SEEN_HEAD;
        for (i = 0; i < 16; i++)
            if ((s_resim.ring[i] = (uint8_t *)malloc(size)) == NULL)
                s_resim.pred = NULL;
    }
    /* The clock and the controls as a live match starts them (NetplayMatch_StartLive). */
    {
        extern volatile uint16_t _currentGameTick;
        extern volatile uint16_t _currentTick;
        extern uint16_t          _lastGameTick;
        extern void              InputSDL_ReleaseAll(void);
        _currentGameTick = 0;
        _currentTick     = 0;
        _lastGameTick    = 0xFFFF;
        MatchStep_Reset();
        InputSDL_ReleaseAll();
    }
    rc = NetplayMatch_Start();
    if (rc == 0) {
        if (simWidth < SCREEN_WIDTH)
            simWidth = SCREEN_WIDTH;
        if (simWidth > PITCH_VIEW_MAX_WIDTH)
            simWidth = PITCH_VIEW_MAX_WIDTH;
        _sim_draw_width = simWidth;
    }
    return rc;
}

void NetplayMatch_ResimViewStats(uint32_t frames[2], uint32_t same[2], uint32_t skipped[2])
{
    int32_t p;
    for (p = 0; p < 2; p++) {
        frames[p] = s_resim.viewFrames[p];
        same[p] = s_resim.viewSame[p];
        skipped[p] = s_resim.viewSkipped[p];
    }
}

void NetplayMatch_ResimResult(uint32_t *played, uint32_t *matched, int32_t *firstDiff,
                              uint32_t *have, uint32_t *want)
{
    *played = s_resim.frame;
    *matched = s_resim.matched;
    *firstDiff = s_resim.firstDiff;
    *have = s_resim.diffHave;
    *want = s_resim.diffWant;
}

static int32_t resimFrame(void)
{
    GameFrameResult r;
    uint32_t f = s_resim.frame, h;

    if (!s_resim.loaded) {
        uint32_t size = SimState_Size(SIM_TIER_ALL);
        uint8_t *mine = (uint8_t *)malloc(size);
        int err;
        s_resim.loaded = 1;
        /* The first step of a live match (p2pFrame): released controls, the clock at its
         * kick-off values. The recorded state overwrites what it holds of them. */
        resetControlHistory();
        {
            extern volatile uint16_t _currentGameTick;
            extern volatile uint16_t _currentTick;
            extern uint16_t          _lastGameTick;
            extern uint16_t          _lastFrameTicks;
            _currentGameTick = 0;
            _currentTick     = 0;
            _lastGameTick    = 0xFFFF;
            _lastFrameTicks  = 1;
        }
        /* The match as it stood before frame 0; this display keeps its own LOCAL fields. */
        if (mine)
            SimState_Save(mine, SIM_TIER_ALL);
        err = StateWire_Decode(s_resim.kickoff, s_resim.kickoffLen, NULL, NULL);
        if (err != STATE_WIRE_OK)
            fail("the recording's kick-off state does not load: %s", StateWire_ErrorName(err));
        if (mine)
            SimState_RestoreLocal(mine, SIM_TIER_ALL);
        free(mine);
    }
    /* However it ends -- the recording runs out, a frame parts from it, or the match
     * leaves its loop -- it ends HERE, through the callback: out of the loop lies the
     * post-match screen, waiting for a press nobody headless gives. */
    if (f >= s_resim.frames)
        goto over;                          /* the recording ends here */

    /* The frame's state at its start, as the recording saw it at the same point. */
    h = (uint32_t)stateHash(SIM_TIER_SIM);
    if (h != s_resim.hashes[f]) {
        s_resim.firstDiff = (int32_t)f;
        s_resim.diffHave = h;
        s_resim.diffWant = s_resim.hashes[f];
        goto over;
    }
    s_resim.matched++;

    /* The start of this frame, kept for the views that begin here. */
    if (s_resim.pred && s_resim.ring[f & 15])
        SimState_Save(s_resim.ring[f & 15], SIM_TIER_ALL);
    soundFrameBegin((int32_t)f, 0);
    CONTEST_LOG(ContestLog_SetFrame(f));
    r = step(s_resim.inputs + (size_t)f * SRP_INPUT_BYTES, (int32_t)f, 0);
    soundFrameEnd();
    if (r == GAME_FRAME_DONE && s_resim.pred && s_resim.ring[(f + 1) & 15]) {
        SimState_Save(s_resim.ring[(f + 1) & 15], SIM_TIER_ALL);
        resimViews(f);
    }
    if (r == GAME_FRAME_DONE) {
        s_resim.frame++;
        s_np.frames++;
        Platform_Present();
    } else if (r == GAME_FRAME_DETOUR) {
        GameLoop_EndFrame(1);
    } else {
        goto over;
    }
    return r;
over:
    if (s_liveFinished)
        s_liveFinished();
    return GAME_FRAME_MATCH_OVER;
}

int32_t NetplayMatch_Frame(void)
{
    if (s_np.mode == NETPLAY_MATCH_RESIM)
        return resimFrame();
    return s_np.mode == NETPLAY_MATCH_P2P ? p2pFrame() : singleProcessFrame();
}

int32_t NetplayMatch_Finish(void)
{
    int32_t code = 0, i;
    uint32_t frames = s_np.traceLen;

    switch (s_np.mode) {
    case NETPLAY_MATCH_OFF:
        /* A live match reported and closed itself as it ended (liveEnd); what is left
         * is to forget how it ended, so the next match -- live or not -- starts clean. */
        s_liveOutcome = NETPLAY_LIVE_NONE;
        s_liveWhy[0] = '\0';
        return 0;
    case NETPLAY_MATCH_DIRECT:
        fprintf(stderr, "[netplay] direct: %u frames played\n", s_np.frames);
        break;
    case NETPLAY_MATCH_SYNCTEST:
        fprintf(stderr, "[netplay] synctest: %u frames played, %u re-simulated with matching "
                        "checksums (%u saves, %u loads)\n",
                s_np.frames, s_np.resimulated, s_np.saves, s_np.loads);
        /* A session that never rolled back proved nothing. */
        if (s_np.frames > (uint32_t)s_np.checkDistance && s_np.loads == 0) {
            fprintf(stderr, "[netplay] FAIL: GGPO never rolled the match back\n");
            code = 2;
        }
        break;
    case NETPLAY_MATCH_P2P:
        fprintf(stderr, "[netplay] p2p player %d: %u frames played, %u re-simulated after %u "
                        "rollbacks (%u saves)\n",
                s_np.player, s_np.frames, s_np.resimulated, s_np.loads, s_np.saves);
        frames = s_np.endFrame >= 0 ? (uint32_t)s_np.endFrame + 1 : 0;
        break;
    case NETPLAY_MATCH_RESIM:
        /* src/srp_resim.c reports it (NetplayMatch_ResimResult). */
        Audio_SetSfxFilter(NULL);
        return 0;
    }
    /* What the emulated network actually did, not what it was asked for: a
     * condition configured at 2% may never have fired in a short match, and a
     * run that proved nothing should say so. */
    if (Netem_Enabled())
        fprintf(stderr, "[netplay] network emulation did: %s\n", Netem_Summary());
    if (frames)
        code |= writeTrace(frames);
    Audio_SetSfxFilter(NULL);
    fprintf(stderr, "[netplay] sound: %u effects played (%u of them late, first heard when a "
                    "rollback corrected the input), %u repeats in re-simulated frames kept "
                    "silent\n", s_soundPlayed, s_soundLate, s_soundRepeats);
    if (s_soundTrace) {
        fclose(s_soundTrace);
        s_soundTrace = NULL;
        fprintf(stderr, "[netplay] wrote %s\n", s_np.soundTracePath);
    }
    if (s_np.session) {
        Netplay_Close(s_np.session);
        s_np.session = NULL;
    }
    for (i = 0; i < FIRST_RUN_FRAMES; i++) {
        free(s_firstRun[i].state);
        s_firstRun[i].state = NULL;
    }
    if (s_np.unidentified) {
        fprintf(stderr, "[netplay] FAIL: %u checksums met a pointer with no identity\n",
                s_np.unidentified);
        code = 2;
    }
    return code;
}

#endif /* SWOS_SDL_BUILD */
