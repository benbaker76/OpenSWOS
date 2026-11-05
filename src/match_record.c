/*
 * match_record.c -- stream a live match to a recorder. See include/match_record.h
 * and include/record_proto.h.
 *
 * One TCP connection per live match, non-blocking from start to end. Everything
 * the recorder has not yet taken waits in one growing byte buffer; a recorder that
 * falls far behind (or never answers) loses its recording at RECORD_HOLD_MAX, and
 * the match never notices. The only wait is at the END of a match, to deliver the
 * last frames and the END message -- bounded, and the match is over by then.
 */
#include "match_record.h"

#ifdef SWOS_SDL_BUILD

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char s_configured[80];
static int32_t s_arrangedByServer;

/* [netplay] live_cast (LIVE CAST, src/live_cast.c): 0 keeps this player's matches from viewers. */
static int32_t s_noWatch;
void MatchRecord_SetLiveCast(int32_t on) { s_noWatch = !on; }

void MatchRecord_SetConfigured(const char *spec)
{
    snprintf(s_configured, sizeof s_configured, "%s", spec ? spec : "");
}

void MatchRecord_SetArrangedByServer(int32_t on)
{
    s_arrangedByServer = on != 0;
}

#ifdef SWOS_NETPLAY

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <SDL.h>

#include "build_info.h"
#include "mm_client.h"
#include "record_proto.h"
#include "replay_v2.h"      /* ReplayRec_Content: the .SRP being recorded */
#include "srp_writer.h"
#include "netplay_ghost.h"  /* Ghost_PredByte / Ghost_SeenRecord: RECORD_REPLAY_PRED */

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0          /* macOS: SO_NOSIGPIPE on the socket instead */
#endif

enum {
    RECORD_HOLD_MAX  = 8 << 20,     /* bytes waiting for a recorder before it is dropped */
    RECORD_BATCH     = 32,          /* frames per RECORD_FRAMES: about half a second */
    RECORD_END_WAIT  = 1000,        /* ms the end of a match may spend on the tail */
    RECORD_PORT      = 19602,       /* MM_DEFAULT_PORT: the recorder is the matchmaker */
};

static struct {
    int32_t  active;                /* a match is being recorded */
    int32_t  sock;                  /* -1 when none */
    int32_t  connected;             /* the non-blocking connect has completed */
    uint8_t *out;                   /* bytes the recorder has not taken yet */
    uint32_t outLen, outCap;
    uint64_t batch[RECORD_BATCH];
    uint32_t batchFirst;
    int32_t  batchCount;
    uint32_t nextFrame;             /* the next frame expected */
    uint32_t frames;                /* how many were streamed */
    char     target[80];
    /* A FULL recording: the .SRP the game is writing, as it is confirmed. */
    int32_t  full;
    int32_t  metaSent;
    int32_t  resumeSent;
    uint32_t replayNext;            /* the next replay frame owed */
    uint32_t audioNext, chapterNext;/* the next sound / chapter owed, as indices */
    uint32_t replayFrames;          /* how many were streamed */
} s_rec = { 0, -1 };

static char s_targetText[80];

/* host:port for "server" or a spec, "" for off. */
static int32_t resolve_target(char *host, size_t hostCap, uint16_t *port)
{
    const char *spec = getenv("SWOS_RECORD");
    const char *colon;
    size_t n;

    if (!spec || !spec[0])
        spec = s_configured;
    if (!spec[0] || SDL_strcasecmp(spec, "off") == 0 || SDL_strcasecmp(spec, "false") == 0)
        return 1;
    if (SDL_strcasecmp(spec, "server") == 0) {
        if (!s_arrangedByServer)
            return 1;               /* not the matchmaker's match: see MatchRecord_SetArrangedByServer */
        snprintf(host, hostCap, "%s", MmClient_DefaultHost());
        *port = MmClient_DefaultPort();
        return 0;
    }
    colon = strrchr(spec, ':');
    n = colon ? (size_t)(colon - spec) : strlen(spec);
    if (n == 0 || n >= hostCap)
        return 1;
    memcpy(host, spec, n);
    host[n] = '\0';
    *port = RECORD_PORT;
    if (colon) {
        int v = atoi(colon + 1);
        if (v <= 0 || v > 65535)
            return 1;
        *port = (uint16_t)v;
    }
    return 0;
}

const char *MatchRecord_Target(void)
{
    char host[72];
    uint16_t port;
    if (resolve_target(host, sizeof host, &port) != 0)
        return "";
    snprintf(s_targetText, sizeof s_targetText, "%s:%u", host, (unsigned)port);
    return s_targetText;
}

static void drop(const char *why)
{
    if (s_rec.sock >= 0) {
        fprintf(stderr, "[record] %s: %s -- this match is not being recorded any more\n",
                s_rec.target, why);
        close(s_rec.sock);
    }
    s_rec.sock = -1;
    s_rec.connected = 0;
    s_rec.outLen = 0;
}

static void hold(const uint8_t *data, uint32_t len)
{
    if (s_rec.sock < 0)
        return;
    if (s_rec.outLen + len > RECORD_HOLD_MAX) {
        drop("the recorder has fallen too far behind");
        return;
    }
    if (s_rec.outLen + len > s_rec.outCap) {
        uint32_t cap = s_rec.outCap ? s_rec.outCap : 4096;
        uint8_t *grown;
        while (cap < s_rec.outLen + len)
            cap *= 2;
        grown = (uint8_t *)realloc(s_rec.out, cap);
        if (!grown) {
            drop("out of memory");
            return;
        }
        s_rec.out = grown;
        s_rec.outCap = cap;
    }
    memcpy(s_rec.out + s_rec.outLen, data, len);
    s_rec.outLen += len;
}

static void hold_message(uint8_t type, const uint8_t *payload, uint32_t len)
{
    uint8_t h[RECORD_HEADER_LEN];
    Record_PutHeader(h, type, len);
    hold(h, sizeof h);
    hold(payload, len);
}

static uint32_t put_string(uint8_t *p, const char *s)
{
    size_t n = s ? strlen(s) : 0;
    if (n > 255)
        n = 255;
    p[0] = (uint8_t)n;
    if (n)
        memcpy(p + 1, s, n);
    return (uint32_t)n + 1;
}

/* Send whatever the socket takes now. Never blocks. */
static void flush_now(void)
{
    if (s_rec.sock < 0)
        return;
    if (!s_rec.connected) {
        struct pollfd pfd;
        int err = 0;
        socklen_t el = (socklen_t)sizeof err;
        pfd.fd = s_rec.sock;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        if (poll(&pfd, 1, 0) <= 0)
            return;                     /* still connecting */
        if (getsockopt(s_rec.sock, SOL_SOCKET, SO_ERROR, (char *)&err, &el) != 0 || err != 0) {
            drop(err ? strerror(err) : "cannot connect");
            return;
        }
        s_rec.connected = 1;
        fprintf(stderr, "[record] connected to %s\n", s_rec.target);
    }
    while (s_rec.outLen > 0) {
        ssize_t n = send(s_rec.sock, (const char *)s_rec.out, s_rec.outLen, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                return;
            drop(strerror(errno));
            return;
        }
        memmove(s_rec.out, s_rec.out + n, s_rec.outLen - (uint32_t)n);
        s_rec.outLen -= (uint32_t)n;
    }
}

static void flush_batch(void)
{
    uint8_t buf[6 + RECORD_BATCH * 8];
    int32_t i;
    if (s_rec.batchCount == 0)
        return;
    Record_Put32(buf, s_rec.batchFirst);
    Record_Put16(buf + 4, (uint32_t)s_rec.batchCount);
    for (i = 0; i < s_rec.batchCount; i++)
        Record_Put64(buf + 6 + 8 * i, s_rec.batch[i]);
    hold_message(RECORD_FRAMES, buf, 6 + 8 * (uint32_t)s_rec.batchCount);
    s_rec.batchCount = 0;
}

void MatchRecord_Begin(const MatchRecordInfo *info)
{
    char host[72], portText[8];
    uint16_t port;
    struct addrinfo hints, *res = NULL;
    uint8_t hello[RECORD_HELLO_FIXED + 6 * 256];
    uint32_t n;
    int32_t s, flags;

    if (s_rec.active)                   /* a match that never ended properly */
        drop("a new match started");
    s_rec.active = 0;
    s_rec.batchCount = 0;
    s_rec.nextFrame = 0;
    s_rec.frames = 0;
    s_rec.outLen = 0;
    s_rec.metaSent = 0;
    s_rec.resumeSent = 0;
    s_rec.replayNext = s_rec.audioNext = s_rec.chapterNext = 0;
    s_rec.replayFrames = 0;
    {
        /* Full unless asked for hashes only (SWOS_RECORD_MODE=hashes): the hashes find a
         * desync, the frames keep the match. */
        const char *mode = getenv("SWOS_RECORD_MODE");
        s_rec.full = !(mode && SDL_strcasecmp(mode, "hashes") == 0);
    }

    if (resolve_target(host, sizeof host, &port) != 0)
        return;
    snprintf(s_rec.target, sizeof s_rec.target, "%s:%u", host, (unsigned)port);

    /* Resolving is the one step that can block (a DNS name); it happens here, at
     * kick-off, before the first frame is played. */
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portText, sizeof portText, "%u", (unsigned)port);
    if (getaddrinfo(host, portText, &hints, &res) != 0 || !res) {
        fprintf(stderr, "[record] %s: cannot resolve it -- not recording\n", s_rec.target);
        return;
    }
    s = (int32_t)socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        freeaddrinfo(res);
        return;
    }
    flags = fcntl(s, F_GETFL, 0);
    if (flags < 0 || fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(s);
        freeaddrinfo(res);
        return;
    }
    {
        int one = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
#ifdef SO_NOSIGPIPE
        /* macOS has no MSG_NOSIGNAL: a recorder that hangs up must not SIGPIPE us. */
        setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&one, sizeof one);
#endif
    }
    if (connect(s, res->ai_addr, res->ai_addrlen) != 0 && errno != EINPROGRESS) {
        fprintf(stderr, "[record] %s: %s -- not recording\n", s_rec.target, strerror(errno));
        close(s);
        freeaddrinfo(res);
        return;
    }
    freeaddrinfo(res);
    s_rec.sock = s;
    s_rec.connected = 0;
    s_rec.active = 1;

    memset(hello, 0, RECORD_HELLO_FIXED);
    memcpy(hello, RECORD_MAGIC, 4);
    hello[4] = RECORD_PROTOCOL;
    hello[5] = (uint8_t)info->player;
    hello[6] = s_rec.full ? RECORD_MODE_FULL : RECORD_MODE_HASHES;
    hello[7] = (uint8_t)info->gameplay;
    Record_Put64(hello + 8, info->matchId);
    Record_Put64(hello + 16, info->relayToken);
    Record_Put16(hello + 24, (uint32_t)info->simWidth);
    hello[26] = (uint8_t)info->delay;
    hello[27] = s_noWatch ? RECORD_HELLO_NO_WATCH : 0;  /* ([netplay] live_cast = off) */
    n = RECORD_HELLO_FIXED;
    n += put_string(hello + n, Build_Id());
    n += put_string(hello + n, Build_Platform());
    n += put_string(hello + n, info->myName);
    n += put_string(hello + n, info->peerName);
    n += put_string(hello + n, info->team1);
    n += put_string(hello + n, info->team2);
    hold_message(RECORD_HELLO, hello, n);
    fprintf(stderr, "[record] recording match %016llx to %s\n",
            (unsigned long long)info->matchId, s_rec.target);
    flush_now();
}

void MatchRecord_Frame(uint32_t frame, uint64_t simHash)
{
    if (!s_rec.active || s_rec.sock < 0)
        return;
    if (frame != s_rec.nextFrame)
        return;                         /* a caller bug: frames arrive in order, once */
    if (s_rec.batchCount == 0)
        s_rec.batchFirst = frame;
    s_rec.batch[s_rec.batchCount++] = simHash;
    s_rec.nextFrame = frame + 1;
    s_rec.frames++;
    if (s_rec.batchCount == RECORD_BATCH)
        flush_batch();
}

/* The replay frames `from`..`to` with the sounds and chapters they played. */
static void send_replay(const SrpContent *c, uint32_t from, uint32_t to)
{
    uint32_t f, len = 6, i, nAudio = 0, nChap = 0, audioEnd, chapEnd;
    uint8_t *buf, *p;

    for (f = from; f <= to; f++)
        len += FRAME_HEAD_BYTES + SPRITE_BYTES * Record_Get16(c->frms + c->offsets[f]);
    for (audioEnd = s_rec.audioNext; audioEnd < c->audioCount && c->audio[audioEnd].frame <= to; audioEnd++)
        nAudio++;
    for (chapEnd = s_rec.chapterNext; chapEnd < c->chapterCount && c->chapters[chapEnd].frame <= to; chapEnd++)
        nChap++;
    len += 2 + nAudio * AUDIO_EVENT_BYTES + 2 + nChap * CHAPTER_BYTES;
    len += 2 + (c->inputs ? (to - from + 1) * SRP_INPUT_BYTES : 0);
    buf = (uint8_t *)malloc(len);
    if (!buf) {
        drop("out of memory");
        return;
    }
    Record_Put32(buf, from);
    Record_Put16(buf + 4, to - from + 1);
    p = buf + 6;
    for (f = from; f <= to; f++) {
        uint32_t n = FRAME_HEAD_BYTES + SPRITE_BYTES * Record_Get16(c->frms + c->offsets[f]);
        memcpy(p, c->frms + c->offsets[f], n);
        p += n;
    }
    Record_Put16(p, nAudio);
    p += 2;
    for (i = s_rec.audioNext; i < audioEnd; i++, p += AUDIO_EVENT_BYTES) {
        Record_Put32(p, c->audio[i].frame);
        p[4] = c->audio[i].kind;
        p[5] = c->audio[i].channel;
        Record_Put16(p + 6, c->audio[i].id);
    }
    Record_Put16(p, nChap);
    p += 2;
    for (i = s_rec.chapterNext; i < chapEnd; i++, p += CHAPTER_BYTES) {
        Record_Put32(p, c->chapters[i].frame);
        p[4] = c->chapters[i].kind;
        p[5] = c->chapters[i].team;
        Record_Put16(p + 6, c->chapters[i].value);
    }
    /* Each frame's input, for a resumable recording: confirmed, so final. */
    Record_Put16(p, c->inputs ? to - from + 1 : 0);
    p += 2;
    if (c->inputs) {
        memcpy(p, c->inputs + (size_t)from * SRP_INPUT_BYTES, (size_t)(to - from + 1) * SRP_INPUT_BYTES);
        p += (to - from + 1) * SRP_INPUT_BYTES;
    }
    hold_message(RECORD_REPLAY_FRAMES, buf, len);
    free(buf);
    s_rec.audioNext = audioEnd;
    s_rec.chapterNext = chapEnd;
    s_rec.replayNext = to + 1;
    s_rec.replayFrames += to - from + 1;
}

/* This player's READ bytes for frames `from`..`to`: what it read, and when. */
static void send_read(uint32_t from, uint32_t to)
{
    uint32_t count = to - from + 1, f;
    uint8_t *buf = (uint8_t *)malloc(8 + (size_t)count * SRP_READ_BYTES);

    if (!buf) {
        drop("out of memory");
        return;
    }
    Record_Put32(buf, from);
    Record_Put16(buf + 4, count);
    buf[6] = (uint8_t)Ghost_ReadDelay();
    buf[7] = (uint8_t)Ghost_ReadBalance();
    for (f = from; f <= to; f++)
        Ghost_ReadBytes(f, buf + 8 + (size_t)(f - from) * SRP_READ_BYTES);
    hold_message(RECORD_REPLAY_READ, buf, 8 + count * SRP_READ_BYTES);
    free(buf);
}

/* This player's PRED bytes and SEEN records for frames `from`..`to`, confirmed, so final. */
static void send_pred(uint32_t from, uint32_t to)
{
    uint32_t count = to - from + 1, cap = 10 + count + 1024, len, f;
    uint8_t *buf = (uint8_t *)malloc(cap), *p;
    uint8_t rec[SRP_SEEN_REC_HEAD + SRP_SEEN_OBJECTS * SRP_SEEN_OBJ_BYTES];

    if (!buf) {
        drop("out of memory");
        return;
    }
    Record_Put32(buf, from);
    Record_Put16(buf + 4, count);
    for (f = from; f <= to; f++)
        buf[6 + f - from] = Ghost_PredByte(f);
    len = 10 + count;
    for (f = from; f <= to; f++) {
        uint32_t n = Ghost_SeenRecord(f, rec);
        if (!n)
            continue;
        if (len + n > cap) {
            uint8_t *grown = (uint8_t *)realloc(buf, cap * 2 + n);
            if (!grown) {
                free(buf);
                drop("out of memory");
                return;
            }
            buf = grown;
            cap = cap * 2 + n;
        }
        memcpy(buf + len, rec, n);
        len += n;
    }
    p = buf + 6 + count;
    Record_Put32(p, len - 10 - count);
    hold_message(RECORD_REPLAY_PRED, buf, len);
    free(buf);
}

void MatchRecord_Replay(uint32_t confirmed)
{
    SrpContent c;
    uint32_t last;

    if (!s_rec.active || s_rec.sock < 0 || !s_rec.full)
        return;
    if (ReplayRec_Content(&c) != 0)
        return;                         /* this match is not being recorded by the game */
    if (!s_rec.metaSent) {
        uint8_t *m = (uint8_t *)malloc(4 + c.metaLen);
        if (!m) {
            drop("out of memory");
            return;
        }
        Record_Put16(m, c.tickRate);
        Record_Put16(m + 2, c.quirks);
        memcpy(m + 4, c.meta, c.metaLen);
        hold_message(RECORD_REPLAY_META, m, 4 + c.metaLen);
        free(m);
        s_rec.metaSent = 1;
    }
    /* The parts that make it resumable, once they all exist: the kick-off state comes
     * with GGPO's first save of frame 0, before any frame is confirmed. */
    if (!s_rec.resumeSent && c.setup && c.kickoff) {
        uint32_t n = 16 + c.setupLen + c.kickoffLen;
        uint8_t *m = (uint8_t *)malloc(n);
        if (!m) {
            drop("out of memory");
            return;
        }
        Record_Put64(m, c.layout);
        m[8] = c.rules;
        m[9] = c.style;         /* GAMEPLAY_* (0 from a build before XBOX: m[8] says) */
        Record_Put16(m + 10, c.simWidth);
        Record_Put32(m + 12, c.setupLen);
        memcpy(m + 16, c.setup, c.setupLen);
        memcpy(m + 16 + c.setupLen, c.kickoff, c.kickoffLen);
        hold_message(RECORD_REPLAY_RESUME, m, n);
        free(m);
        s_rec.resumeSent = 1;
    }
    /* Inputs go with the frames only once the recorder has the rest to use them with. */
    if (!s_rec.resumeSent)
        c.inputs = NULL;
    if (c.frameCount == 0)
        return;
    /* Recorded AND confirmed: every rollback the confirmed inputs called for has been
     * played (the GGPO-X fork's ConfirmedFrame), so these frames are final. */
    last = confirmed < c.frameCount - 1 ? confirmed : c.frameCount - 1;
    /* The hashes first, however few: a frame the recorder holds without its hash cannot be
     * played again, and a game that dies here would leave up to RECORD_BATCH of them --
     * the recorder then keeps only the frames that came whole (Recorder.cpp Usable). */
    if (s_rec.replayNext <= last && s_rec.batchCount > 0)
        flush_batch();
    while (s_rec.replayNext <= last && s_rec.sock >= 0) {
        uint32_t to = s_rec.replayNext + 255 < last ? s_rec.replayNext + 255 : last;
        uint32_t from = s_rec.replayNext;
        send_replay(&c, from, to);
        if (s_rec.sock >= 0 && ReplayRec_PredictionKept()) {
            send_pred(from, to);
            if (s_rec.sock >= 0)
                send_read(from, to);
        }
    }
}

void MatchRecord_Pump(void)
{
    if (s_rec.active)
        flush_now();
}

void MatchRecord_End(int32_t finished, int32_t endFrame, uint64_t finalSimHash, const char *why)
{
    uint8_t buf[13 + 256];
    uint32_t n, start;

    if (!s_rec.active)
        return;
    if (s_rec.sock < 0) {
        s_rec.active = 0;
        return;
    }
    flush_batch();
    if (s_rec.full && s_rec.metaSent) {
        /* The score META is written with, as ReplayRec_MatchEnd will patch it. */
        extern uint16_t _statsTeam1Goals, _statsTeam2Goals;
        uint8_t score[4];
        Record_Put16(score, _statsTeam1Goals);
        Record_Put16(score + 2, _statsTeam2Goals);
        hold_message(RECORD_REPLAY_SCORE, score, sizeof score);
    }
    s_rec.active = 0;
    buf[0] = (uint8_t)(finished ? 0 : 1);
    Record_Put32(buf + 1, (uint32_t)(endFrame < 0 ? 0 : endFrame));
    Record_Put64(buf + 5, finalSimHash);
    n = 13 + put_string(buf + 13, why);
    hold_message(RECORD_END, buf, n);

    /* The tail: the match is over, so a short wait is affordable here and nowhere
     * else. */
    start = SDL_GetTicks();
    while (s_rec.sock >= 0 && (s_rec.outLen > 0 || !s_rec.connected) &&
           SDL_GetTicks() - start < RECORD_END_WAIT) {
        struct pollfd pfd;
        pfd.fd = s_rec.sock;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        poll(&pfd, 1, 20);
        flush_now();
    }
    if (s_rec.sock >= 0) {
        if (s_rec.outLen > 0)
            fprintf(stderr, "[record] %s: %u bytes never delivered\n", s_rec.target,
                    (unsigned)s_rec.outLen);
        else if (s_rec.full)
            fprintf(stderr, "[record] %u frames and %u replay frames delivered to %s\n",
                    (unsigned)s_rec.frames, (unsigned)s_rec.replayFrames, s_rec.target);
        else
            fprintf(stderr, "[record] %u frames delivered to %s\n", (unsigned)s_rec.frames,
                    s_rec.target);
        shutdown(s_rec.sock, SHUT_WR);
        close(s_rec.sock);
    }
    s_rec.sock = -1;
    s_rec.connected = 0;
    s_rec.outLen = 0;
}

#else  /* !SWOS_NETPLAY -- no live matches to record */

const char *MatchRecord_Target(void) { return ""; }
void MatchRecord_Begin(const MatchRecordInfo *info) { (void)info; }
void MatchRecord_Frame(uint32_t frame, uint64_t simHash) { (void)frame; (void)simHash; }
void MatchRecord_Replay(uint32_t confirmed) { (void)confirmed; }
void MatchRecord_Pump(void) {}
void MatchRecord_End(int32_t finished, int32_t endFrame, uint64_t finalSimHash, const char *why)
{
    (void)finished; (void)endFrame; (void)finalSimHash; (void)why;
}

#endif /* SWOS_NETPLAY */
#endif /* SWOS_SDL_BUILD */
