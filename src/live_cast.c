/*
 * live_cast.c -- LIVE CAST: watching a live online match (SWOS 2020's "watch live matches";
 * include/record_proto.h RECORD_WATCH, docs/NETPLAY.md §7 "Watching a match live").
 *
 * The recorder (the matchmaking server run with --record) already has the match as a player's
 * game streams it -- what was DRAWN, frame by frame, with its sounds: the .SRP it keeps. A viewer
 * asks it for that stream and plays it in the replay viewer as it arrives, a few seconds behind
 * the players. Nothing is simulated, so the viewer's build and screen need not be the players':
 * a recording plays the same on any of them (resolution independent, docs/REPLAY_FORMAT_V2.md).
 *
 *   --watch=<host[:port]>           the match recorded the longest
 *   --watch=<host[:port]>,<id>      that match (its hex id, as --watch-list prints it)
 *   --watch-list=<host[:port]>      the live matches, then exits
 *
 * A thread reads the stream; the viewer's frame loop takes what has arrived (LiveCast_Pump).
 * SDL builds with netplay (the sockets); not the web, which has no TCP.
 */
#include "live_cast.h"

#if defined(SWOS_SDL_BUILD) && defined(SWOS_NETPLAY) && !defined(SWOS_WASM_BUILD)

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/time.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <SDL.h>

#include "mm_client.h"
#include "mod_api.h"         /* ModLiveMatch */
#include "record_proto.h"
#include "replay_v2.h"
#include "srp_writer.h"
#include "swos_io.h"

#define LIVE_FILE   "LIVE.SRP"
enum { kFrameHead = 20, kSpriteBytes = 6, kLag = 3 };   /* (FRMS; seconds behind the players) */

static char     s_host[256];
static uint16_t s_port;
static uint64_t s_id;
static int32_t  s_armed, s_list;

static SDL_mutex  *s_lock;
static SDL_Thread *s_thread;
static int        s_sock = -1;
static volatile int32_t s_done;         /* the stream ended (or failed) */

/* What has arrived (under s_lock). */
static uint8_t       *s_meta;
static uint32_t       s_metaLen;
static uint16_t       s_tick, s_quirks;
static uint8_t       *s_frms;
static uint32_t       s_frmsLen, s_frmsCap;
static uint32_t      *s_offsets;
static uint32_t       s_frames, s_offsetsCap;
static SrpAudioEvent *s_audio;
static uint32_t       s_audioCount, s_audioCap;
static SrpChapter    *s_chap;
static uint32_t       s_chapCount, s_chapCap;
static uint16_t       s_score[2];
static int32_t        s_haveScore, s_outcome = -1;

/* What the viewer has been given. */
static uint32_t s_given, s_givenAudio;
static int32_t  s_seeked, s_ended, s_viewing;

int32_t LiveCast_Request(const char *spec, int32_t list)
{
    const char *comma, *colon;
    size_t n;
    if (!spec || !spec[0])
        return 1;
    comma = strchr(spec, ',');
    n = comma ? (size_t)(comma - spec) : strlen(spec);
    if (n >= sizeof s_host)
        return 1;
    memcpy(s_host, spec, n);
    s_host[n] = 0;
    colon = strrchr(s_host, ':');
    s_port = MmClient_DefaultPort();
    if (colon) {
        s_port = (uint16_t)atoi(colon + 1);
        s_host[colon - s_host] = 0;
    }
    s_id = comma ? strtoull(comma + 1, NULL, 16) : 0;
    s_armed = 1;
    s_list = list;
    return 0;
}

int32_t LiveCast_Armed(void) { return s_armed; }

/* A connection to s_host:s_port within ~2 s (a menu waits on it), blocking after. */
static int open_socket(void)
{
    struct addrinfo hints, *res = NULL;
    char port[8];
    int s;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port, sizeof port, "%u", (unsigned)s_port);
    if (getaddrinfo(s_host, port, &hints, &res) != 0 || !res) {
        fprintf(stderr, "[live] %s: cannot resolve it\n", s_host);
        return -1;
    }
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s >= 0) {
        const int fl = fcntl(s, F_GETFL, 0);
        struct pollfd pf;
        int err = 0;
        socklen_t el = sizeof err;
        fcntl(s, F_SETFL, fl | O_NONBLOCK);
        if (connect(s, res->ai_addr, res->ai_addrlen) != 0 && errno != EINPROGRESS)
            err = errno;
        pf.fd = s;
        pf.events = POLLOUT;
        if (!err && (poll(&pf, 1, 2000) != 1 || getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &el) != 0))
            err = ETIMEDOUT;
        fcntl(s, F_SETFL, fl);
        if (err) {
            fprintf(stderr, "[live] %s:%u: %s\n", s_host, (unsigned)s_port, strerror(err));
            close(s);
            s = -1;
        } else {
            struct timeval tv = { 3, 0 };   /* (a list's answer; a watch's reader is unbounded) */
            setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        }
    }
    freeaddrinfo(res);
    return s;
}

static int send_all(int s, const uint8_t *p, size_t n)
{
    while (n) {
        const ssize_t k = send(s, p, n, MSG_NOSIGNAL);
        if (k <= 0)
            return -1;
        p += k;
        n -= (size_t)k;
    }
    return 0;
}

static int ask(int s, uint8_t type, uint64_t id)
{
    uint8_t m[RECORD_HEADER_LEN + 13];
    memcpy(m + RECORD_HEADER_LEN, RECORD_VIEWER_MAGIC, 4);
    m[RECORD_HEADER_LEN + 4] = RECORD_PROTOCOL;
    Record_Put64(m + RECORD_HEADER_LEN + 5, id);
    Record_PutHeader(m, type, type == RECORD_WATCH ? 13 : 5);
    return send_all(s, m, RECORD_HEADER_LEN + (type == RECORD_WATCH ? 13 : 5));
}

/* One message: 0 read, -1 the stream is over. `buf` grows as needed. */
static int read_message(int s, uint8_t *type, uint8_t **buf, uint32_t *cap, uint32_t *len)
{
    uint8_t h[RECORD_HEADER_LEN];
    uint32_t got = 0;
    while (got < RECORD_HEADER_LEN) {
        const ssize_t k = recv(s, h + got, RECORD_HEADER_LEN - got, 0);
        if (k <= 0)
            return -1;
        got += (uint32_t)k;
    }
    *type = h[0];
    *len = ((uint32_t)h[1] << 16) | ((uint32_t)h[2] << 8) | h[3];
    if (*len > RECORD_MAX_PAYLOAD)
        return -1;
    if (*len + 1 > *cap) {
        uint8_t *nb = (uint8_t *)realloc(*buf, *len + 1);
        if (!nb)
            return -1;
        *buf = nb;
        *cap = *len + 1;
    }
    for (got = 0; got < *len;) {
        const ssize_t k = recv(s, *buf + got, *len - got, 0);
        if (k <= 0)
            return -1;
        got += (uint32_t)k;
    }
    return 0;
}

static int grow(void **p, uint32_t *cap, uint32_t need, size_t item)
{
    if (need <= *cap)
        return 0;
    uint32_t n = *cap ? *cap : 1024;
    while (n < need)
        n *= 2;
    void *np = realloc(*p, (size_t)n * item);
    if (!np)
        return -1;
    *p = np;
    *cap = n;
    return 0;
}

/* RECORD_REPLAY_FRAMES as the recorder sends a viewer one (record_proto.h). */
static int take_frames(const uint8_t *p, uint32_t len)
{
    uint32_t first, count, at = 6, k;
    if (len < 6)
        return -1;
    first = Record_Get32(p);
    count = Record_Get16(p + 4);
    if (first != s_frames)
        return -1;                          /* (in order, each once) */
    for (k = 0; k < count; k++) {
        uint32_t n;
        if (at + kFrameHead > len)
            return -1;
        n = kFrameHead + kSpriteBytes * Record_Get16(p + at);
        if (at + n > len || grow((void **)&s_frms, &s_frmsCap, s_frmsLen + n, 1) ||
            grow((void **)&s_offsets, &s_offsetsCap, s_frames + 1, sizeof *s_offsets))
            return -1;
        s_offsets[s_frames++] = s_frmsLen;
        memcpy(s_frms + s_frmsLen, p + at, n);
        s_frmsLen += n;
        at += n;
    }
    if (at + 2 > len)
        return -1;
    count = Record_Get16(p + at);
    at += 2;
    if (at + count * 8 + 2 > len || grow((void **)&s_audio, &s_audioCap, s_audioCount + count, sizeof *s_audio))
        return -1;
    for (k = 0; k < count; k++, at += 8) {
        SrpAudioEvent *e = &s_audio[s_audioCount++];
        e->frame = Record_Get32(p + at);
        e->kind = p[at + 4];
        e->channel = p[at + 5];
        e->id = (uint16_t)Record_Get16(p + at + 6);
    }
    count = Record_Get16(p + at);
    at += 2;
    if (at + count * 8 > len || grow((void **)&s_chap, &s_chapCap, s_chapCount + count, sizeof *s_chap))
        return -1;
    for (k = 0; k < count; k++, at += 8) {
        SrpChapter *c = &s_chap[s_chapCount++];
        c->frame = Record_Get32(p + at);
        c->kind = p[at + 4];
        c->team = p[at + 5];
        c->value = (uint16_t)Record_Get16(p + at + 6);
    }
    return 0;
}

static int SDLCALL reader(void *unused)
{
    uint8_t *buf = NULL, type;
    uint32_t cap = 0, len;
    (void)unused;
    while (read_message(s_sock, &type, &buf, &cap, &len) == 0) {
        SDL_LockMutex(s_lock);
        if (type == RECORD_REPLAY_META && len >= 4 && !s_meta) {
            s_tick = (uint16_t)Record_Get16(buf);
            s_quirks = (uint16_t)Record_Get16(buf + 2);
            s_meta = (uint8_t *)malloc(len - 4 + 1);
            if (s_meta) {
                memcpy(s_meta, buf + 4, len - 4);
                s_metaLen = len - 4;
            }
        } else if (type == RECORD_REPLAY_FRAMES) {
            if (take_frames(buf, len) != 0) {
                SDL_UnlockMutex(s_lock);
                fprintf(stderr, "[live] a malformed stream: stopped\n");
                break;
            }
        } else if (type == RECORD_REPLAY_SCORE && len >= 4) {
            s_score[0] = (uint16_t)Record_Get16(buf);
            s_score[1] = (uint16_t)Record_Get16(buf + 2);
            s_haveScore = 1;
        } else if (type == RECORD_END && len >= 1) {
            s_outcome = buf[0];
            SDL_UnlockMutex(s_lock);
            break;
        }
        SDL_UnlockMutex(s_lock);
    }
    free(buf);
    s_done = 1;
    return 0;
}

/* The recording as it stands, written where the viewer reads it. */
static int write_snapshot(void)
{
    SrpContent c;
    uint32_t n = 0;
    uint8_t *file;
    char path[1280];
    FILE *f;
    memset(&c, 0, sizeof c);
    c.meta = s_meta;
    c.metaLen = s_metaLen;
    c.frms = s_frms;
    c.frmsLen = s_frmsLen;
    c.offsets = s_offsets;
    c.frameCount = s_frames;
    c.audio = s_audio;
    c.audioCount = s_audioCount;
    c.chapters = s_chap;
    c.chapterCount = s_chapCount;
    c.tickRate = s_tick;
    c.quirks = s_quirks;
    file = Srp_Build(&c, &n);
    if (!file)
        return -1;
    snprintf(path, sizeof path, "%s/%s", swos_save_dir(), LIVE_FILE);
    f = fopen(path, "wb");
    if (!f || fwrite(file, 1, n, f) != n) {
        if (f)
            fclose(f);
        free(file);
        return -1;
    }
    fclose(f);
    free(file);
    s_given = s_frames;
    s_givenAudio = s_audioCount;
    return 0;
}

/* The live matches, as data: the count, or -1 when the recorder cannot be reached. */
static int32_t list_data(ModLiveMatch *out, int32_t cap)
{
    uint8_t *buf = NULL, type;
    uint32_t bcap = 0, len, at, k, count;
    const int sock = open_socket();
    if (sock < 0)
        return -1;
    {
        struct timeval tv = { 2, 0 };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    }
    if (ask(sock, RECORD_WATCH_LIST, 0) != 0 || read_message(sock, &type, &buf, &bcap, &len) != 0 ||
        type != RECORD_WATCH_MATCHES || len < 2) {
        close(sock);
        free(buf);
        return -1;
    }
    close(sock);
    count = Record_Get16(buf);
    for (k = 0, at = 2; k < count && at + 12 <= len; k++) {
        ModLiveMatch m;
        char *str[4];
        memset(&m, 0, sizeof m);
        m.id = Record_Get64(buf + at);
        m.frames = Record_Get32(buf + at + 8);
        at += 12;
        str[0] = m.players[0]; str[1] = m.players[1]; str[2] = m.teams[0]; str[3] = m.teams[1];
        for (int i = 0; i < 4; i++) {
            const uint32_t sl = at < len ? buf[at++] : 0;
            const uint32_t take = sl < 23 ? sl : 23;
            if (at + take <= len)
                memcpy(str[i], buf + at, take);
            at += sl;
        }
        if (out && (int32_t)k < cap)
            out[k] = m;
    }
    free(buf);
    return (int32_t)(count < (uint32_t)cap || !out ? count : (uint32_t)cap);
}

static int run_list(void)
{
    ModLiveMatch m[64];
    const int32_t n = list_data(m, 64);
    if (n < 0) {
        fprintf(stdout, "LIVE: no answer from %s:%u\n", s_host, (unsigned)s_port);
        return 1;
    }
    fprintf(stdout, "LIVE: %d match(es) on %s:%u\n", (int)n, s_host, (unsigned)s_port);
    for (int32_t k = 0; k < n; k++)
        fprintf(stdout, "  %016llx  %s v %s  (%s v %s)  %u frames\n", (unsigned long long)m[k].id, m[k].players[0],
                m[k].players[1], m[k].teams[0], m[k].teams[1], (unsigned)m[k].frames);
    return 0;
}

/* Connects, asks for match `s_id`, and waits for its look and first frames, then writes them as
 * the .SRP the viewer opens. 0 = ready to view. */
static int32_t start(void)
{
    s_done = 0;
    s_outcome = -1;
    s_sock = open_socket();
    if (s_sock < 0)
        return 1;
    if (!s_lock)
        s_lock = SDL_CreateMutex();
    if (!s_lock || ask(s_sock, RECORD_WATCH, s_id) != 0)
        return 1;
    s_thread = SDL_CreateThread(reader, "live-cast", NULL);
    if (!s_thread)
        return 1;
    for (uint32_t waited = 0; waited < 15000; waited += 50) {
        SDL_LockMutex(s_lock);
        const int ready = s_meta && s_frames >= 50;
        SDL_UnlockMutex(s_lock);
        if (ready || s_done)
            break;
        SDL_Delay(50);
    }
    SDL_LockMutex(s_lock);
    const int ok = s_meta && s_frames > 0 && write_snapshot() == 0;
    SDL_UnlockMutex(s_lock);
    if (!ok) {
        fprintf(stderr, "[live] %s:%u: %s\n", s_host, (unsigned)s_port,
                s_outcome == 2 ? "no such match live" : "no match to watch");
        return 1;
    }
    s_seeked = 0;
    s_ended = 0;
    s_viewing = 1;
    fprintf(stderr, "[live] watching %s:%u from frame %u\n", s_host, (unsigned)s_port, (unsigned)s_given);
    return 0;
}

/* The connection closed, the reader joined, every buffer freed: ready for the next match. */
static void stop(void)
{
    if (s_sock >= 0) {
        shutdown(s_sock, SHUT_RDWR);
        close(s_sock);
        s_sock = -1;
    }
    if (s_thread) {
        SDL_WaitThread(s_thread, NULL);
        s_thread = NULL;
    }
    free(s_meta); free(s_frms); free(s_offsets); free(s_audio); free(s_chap);
    s_meta = s_frms = NULL;
    s_offsets = NULL;
    s_audio = NULL;
    s_chap = NULL;
    s_metaLen = s_frmsLen = s_frmsCap = s_frames = s_offsetsCap = 0;
    s_audioCount = s_audioCap = s_chapCount = s_chapCap = 0;
    s_given = s_givenAudio = 0;
    s_haveScore = 0;
    s_viewing = 0;
}

int32_t LiveCast_Run(char *viewPath, uint32_t cap)
{
    if (s_list) {
        const int r = run_list();
        fflush(stdout);
        exit(r);
    }
    if (start() != 0) {
        fprintf(stdout, "LIVE: %s\n", s_outcome == 2 ? "no such match live" : "nothing to watch");
        fflush(stdout);
        return 1;
    }
    snprintf(viewPath, cap, "%s", LIVE_FILE);
    return 0;
}

/* Each frame the viewer draws: the frames that arrived since, kLag seconds behind the latest;
 * and when the match is over and its last frame has been shown, the viewer is left. */
void LiveCast_Pump(void)
{
    if (!s_lock || !s_viewing)
        return;
    SDL_LockMutex(s_lock);
    if (s_frames > s_given) {
        uint32_t a1 = s_givenAudio;
        while (a1 < s_audioCount && s_audio[a1].frame < s_frames)
            a1++;
        const uint32_t from = s_offsets[s_given];
        if (ReplayV2_LiveAppend(s_frms + from, s_frmsLen - from, s_frames - s_given, s_audio + s_givenAudio,
                                a1 - s_givenAudio) == 0) {
            s_given = s_frames;
            s_givenAudio = a1;
        }
    }
    const int over = s_done && s_given == s_frames;
    SDL_UnlockMutex(s_lock);
    if (!s_seeked) {                        /* (a match joined late: to near its live edge) */
        const int32_t back = (int32_t)(s_tick ? s_tick : 50) * kLag;
        s_seeked = 1;
        if ((int32_t)s_given > back * 2)
            ReplayV2_PlayerSeek((int32_t)s_given - back);
    }
    if (over && !s_ended && ReplayV2_PlayerFrame() >= (int32_t)s_given - 1) {
        extern volatile uint16_t _playHighlightFlag;
        s_ended = 1;
        ReplayV2_PlayerMode(0);             /* (the recording then ends as any does: its loop returns) */
        _playHighlightFlag = 0;             /* (... and the viewer, as on ESC) */
    }
}

/* ---- the mods' calls (include/mod_api.h, "LIVE CAST") ---- */

static void use_default_server(void)
{
    snprintf(s_host, sizeof s_host, "%s", MmClient_DefaultHost());
    s_port = MmClient_DefaultPort();
}

int32_t ModApi_LiveList(ModLiveMatch *out, int32_t cap)
{
    if (s_viewing)
        return -1;
    use_default_server();
    return list_data(out, cap);
}

/* ModApi_LiveAny: the last answer, asked again in the background at most every 20 s. */
static SDL_atomic_t s_anyAnswer = { -1 };
static SDL_atomic_t s_anyBusy;
static uint32_t     s_anyAsked;

static int SDLCALL any_thread(void *unused)
{
    (void)unused;
    SDL_AtomicSet(&s_anyAnswer, ModApi_LiveList(NULL, 0) > 0 ? 1 : SDL_AtomicGet(&s_anyAnswer) == -2 ? -1 : 0);
    SDL_AtomicSet(&s_anyBusy, 0);
    return 0;
}

int32_t ModApi_LiveAny(void)
{
    const uint32_t now = SDL_GetTicks();
    if (!s_viewing && (!s_anyAsked || now - s_anyAsked >= 20000) && SDL_AtomicCAS(&s_anyBusy, 0, 1)) {
        SDL_Thread *t;
        s_anyAsked = now ? now : 1;
        t = SDL_CreateThread(any_thread, "live-any", NULL);
        if (t)
            SDL_DetachThread(t);
        else
            SDL_AtomicSet(&s_anyBusy, 0);
    }
    return SDL_AtomicGet(&s_anyAnswer);
}

int32_t ModApi_LiveWatch(uint64_t id)
{
    extern void ShowHighlights(void);
    extern void ReplayV2_Unload(void);
    extern int32_t ReplayV2_Load(const uint8_t *name);
    int32_t rc;
    if (s_viewing)
        return -1;
    use_default_server();
    s_id = id;
    if (start() != 0) {
        stop();
        return -1;
    }
    ReplayV2_Unload();
    if (ReplayV2_Load((const uint8_t *)LIVE_FILE) != 0) {
        stop();
        return -1;
    }
    ReplayV2_PlayerMode(1);
    ShowHighlights();                       /* (the stadium, the viewer, back to the menu) */
    ReplayV2_PlayerMode(0);
    rc = s_ended ? 0 : 1;                   /* the match's end, or the viewer's ESC */
    ReplayV2_Unload();
    stop();
    return rc;
}

#else

int32_t LiveCast_Request(const char *spec, int32_t list) { (void)spec; (void)list; return 1; }
int32_t LiveCast_Armed(void) { return 0; }
int32_t LiveCast_Run(char *viewPath, uint32_t cap) { (void)viewPath; (void)cap; return 1; }
void    LiveCast_Pump(void) {}
#ifdef SWOS_SDL_BUILD
#include "mod_api.h"
int32_t ModApi_LiveList(ModLiveMatch *out, int32_t cap) { (void)out; (void)cap; return -1; }
int32_t ModApi_LiveAny(void) { return -1; }
int32_t ModApi_LiveWatch(uint64_t id) { (void)id; return -1; }
#endif

#endif
