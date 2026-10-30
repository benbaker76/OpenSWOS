/*
 * match_report.c -- after an online match, one zip of both players' logs and ratings.
 * include/match_report.h says what it does, include/report_proto.h what crosses the
 * wire, docs/NETPLAY_REPORT.md why.
 *
 * Nothing here runs during play: a report starts when HOW WAS THE MATCH? has been
 * answered, travels over the lobby's link while the players are back in the lobby, and
 * is uploaded by a thread of its own, so a slow or absent server costs nobody a frame.
 */
#include "match_report.h"
#include "project_identity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int32_t s_enabled;

void MatchReport_SetEnabled(int32_t on) { s_enabled = on != 0; }
int32_t MatchReport_Enabled(void)       { return s_enabled; }

/* Whether this match's report leaves the machine: the setting, unless a test says
 * where to send it (SWOS_REPORTS=host[:port] or server), or off. */
static int32_t sending(void)
{
    const char *spec = getenv("SWOS_REPORTS");
    if (spec && spec[0])
        return strcmp(spec, "off") != 0 && strcmp(spec, "false") != 0;
    return s_enabled;
}

#ifdef SWOS_NETPLAY

#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <SDL.h>

#include "build_info.h"
#include "miniz_swos.h"
#include "mm_client.h"
#include "p2p_link.h"
#include "record_proto.h"   /* Record_Put / Record_Get */
#include "report_proto.h"
#include "swos_io.h"
#include "netplay_ghost.h"    /* Ghost_Clips: the windows the zip carries */

#ifndef __EMSCRIPTEN__
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0          /* macOS: SO_NOSIGPIPE on the socket instead */
#endif
#endif

enum {
    CH_REPORT        = 3,           /* the lobby has 0..2 (src/netplay_lobby.c) */
    REPORT_KEEP      = 20,          /* zips kept in save/netplay */
    REPORT_WAIT_MS   = 120000,      /* how long the other player may take to answer */
    REPORT_LOG_MAX   = 2 << 20,     /* the most of SWOS.log taken, from its end */
    REPORT_HALF_MAX  = 120 << 10,   /* under the link's 128 KB a message */
    REPORT_PORT      = 19602,       /* MM_DEFAULT_PORT */
};

typedef struct {
    int32_t  have;
    int32_t  rating;
    int32_t  hasLog;
    int32_t  off;                   /* SEND MATCH REPORTS off: nothing else was sent */
    char     name[32], build[24], platform[24], result[64], comment[64];
    char    *log;                   /* the log, as text; NULL without one */
    uint32_t logLen;
    char    *ghost;                 /* the ghost windows, as CSV (netplay_ghost.h); NULL without */
    uint32_t ghostLen;
} Half;

static struct {
    int32_t  active;                /* a match began and its report is not written yet */
    int32_t  submitted;             /* this player has answered */
    int32_t  sent;                  /* ... and the link took the half */
    uint32_t submittedAt;
    uint64_t matchId, relayToken;
    int32_t  player;                /* 1 or 2 */
    int32_t  delay, simWidth;
    char     team[2][24];
    time_t   kickoff;
    Half     half[2];               /* [0] player 1's, [1] player 2's */
    uint8_t *msg;                   /* this player's REPORT_HALF */
    uint32_t msgLen;
} s_rep;

static void copy_text(char *out, size_t cap, const char *s)
{
    snprintf(out, cap, "%s", s ? s : "");
}

static void half_free(Half *h)
{
    free(h->log);
    free(h->ghost);
    memset(h, 0, sizeof *h);
}

static void reset(void)
{
    half_free(&s_rep.half[0]);
    half_free(&s_rep.half[1]);
    free(s_rep.msg);
    memset(&s_rep, 0, sizeof s_rep);
}

void MatchReport_Begin(const MatchRecordInfo *info)
{
    if (s_rep.active)
        MatchReport_Finish();
    reset();
    s_rep.active     = 1;
    s_rep.matchId    = info->matchId;
    s_rep.relayToken = info->relayToken;
    s_rep.player     = info->player == 2 ? 2 : 1;
    s_rep.delay      = info->delay;
    s_rep.simWidth   = info->simWidth;
    s_rep.kickoff    = time(NULL);
    copy_text(s_rep.team[0], sizeof s_rep.team[0], info->team1);
    copy_text(s_rep.team[1], sizeof s_rep.team[1], info->team2);
    copy_text(s_rep.half[s_rep.player - 1].name, sizeof s_rep.half[0].name, info->myName);
    copy_text(s_rep.half[2 - s_rep.player].name, sizeof s_rep.half[0].name, info->peerName);
}

/* ---- this player's log ----------------------------------------------------- */

/* SWOS.log from its end, at most REPORT_LOG_MAX, with the game folder's path -- which
 * names the player's account on their machine -- taken out. */
static char *read_log(uint32_t *lenOut)
{
    char path[1100], *buf, *out;
    const char *dir = swos_user_dir();
    size_t dirLen = strlen(dir), n, i, o;
    long size;
    FILE *f;

    *lenOut = 0;
    snprintf(path, sizeof path, "%s/SWOS.log", dir);
    if (getenv("SWOS_NO_LOG") || !(f = fopen(path, "rb")))
        return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    if (size <= 0) {
        fclose(f);
        return NULL;
    }
    n = (size_t)size > REPORT_LOG_MAX ? REPORT_LOG_MAX : (size_t)size;
    fseek(f, size - (long)n, SEEK_SET);
    buf = (char *)malloc(n + 1);
    out = (char *)malloc(n + 1);
    if (!buf || !out || fread(buf, 1, n, f) != n) {
        fclose(f);
        free(buf);
        free(out);
        return NULL;
    }
    fclose(f);
    buf[n] = '\0';

    /* "<SWOS folder>" (13) is shorter than any real path, so `out` cannot overflow. */
    for (i = 0, o = 0; i < n; ) {
        if (dirLen >= 13 && i + dirLen <= n && memcmp(buf + i, dir, dirLen) == 0) {
            memcpy(out + o, "<SWOS folder>", 13);
            o += 13;
            i += dirLen;
        } else {
            out[o++] = buf[i++];
        }
    }
    out[o] = '\0';
    free(buf);
    *lenOut = (uint32_t)o;
    return out;
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

static int32_t get_string(const uint8_t **p, const uint8_t *end, char *out, size_t cap)
{
    size_t n;
    if (*p >= end)
        return 1;
    n = **p;
    if ((size_t)(end - *p) < n + 1)
        return 1;
    snprintf(out, cap, "%.*s", (int)n, (const char *)(*p + 1));
    *p += n + 1;
    return 0;
}

/* The half as REPORT_HALF: the log deflated, cut from the front until it fits. */
/* Deflate `text` into at most `budget` bytes: whole if it fits, else cut -- from the
 * front (`keepEnd`: a log, whose newest lines matter) or from the back at a line
 * (the ghost windows, whose header comes first). *used is how much of it went in. */
static uint8_t *pack_within(const char *text, uint32_t len, size_t budget, int32_t keepEnd,
                            size_t *packedLen, uint32_t *from, uint32_t *used)
{
    uint32_t start = 0, n = len;
    for (;;) {
        uint8_t *packed = (uint8_t *)tdefl_compress_mem_to_heap(text + start, n, packedLen,
                                                               TDEFL_DEFAULT_MAX_PROBES);
        if (!packed || *packedLen <= budget || n < 4096) {
            *from = start;
            *used = packed ? n : 0;
            return packed;
        }
        free(packed);
        if (keepEnd) {
            start += n / 2;
            n = len - start;
        } else {
            n /= 2;
            while (n > 0 && text[n - 1] != '\n')
                n--;
        }
    }
}

/* The half as REPORT_HALF: the log and the ghost windows deflated, cut until they fit. */
static void build_message(const Half *h)
{
    uint8_t *packed = NULL, *ghost = NULL, *m;
    size_t packedLen = 0, ghostLen = 0;
    uint32_t from = 0, logUsed = 0, gFrom = 0, gUsed = 0, fixed;

    if (h->ghost)
        ghost = pack_within(h->ghost, h->ghostLen, REPORT_GHOST_MAX, 0, &ghostLen, &gFrom, &gUsed);
    if (!ghost)
        ghostLen = 0;
    if (h->log)
        packed = pack_within(h->log, h->logLen, REPORT_HALF_MAX - 2048 - ghostLen, 1,
                             &packedLen, &from, &logUsed);
    if (!packed)
        packedLen = 0;

    m = (uint8_t *)malloc(REPORT_HALF_FIXED + 5 * 256 + packedLen + 8 + ghostLen);
    if (!m) {
        free(packed);
        free(ghost);
        return;
    }
    memcpy(m, REPORT_HALF_MAGIC, 4);
    m[4] = REPORT_PROTOCOL;
    m[5] = (uint8_t)s_rep.player;
    m[6] = (uint8_t)h->rating;
    m[7] = (uint8_t)((packed ? REPORT_HALF_LOG : 0) | (h->off ? REPORT_HALF_OFF : 0) |
                     (ghost ? REPORT_HALF_GHOST : 0));
    Record_Put64(m + 8, s_rep.matchId);
    Record_Put32(m + 16, packed ? logUsed : 0);
    Record_Put32(m + 20, (uint32_t)packedLen);
    fixed = REPORT_HALF_FIXED;
    fixed += put_string(m + fixed, h->name);
    fixed += put_string(m + fixed, h->build);
    fixed += put_string(m + fixed, h->platform);
    fixed += put_string(m + fixed, h->result);
    fixed += put_string(m + fixed, h->comment);
    if (packed)
        memcpy(m + fixed, packed, packedLen);
    fixed += (uint32_t)packedLen;
    if (ghost) {
        Record_Put32(m + fixed, gUsed);
        Record_Put32(m + fixed + 4, (uint32_t)ghostLen);
        memcpy(m + fixed + 8, ghost, ghostLen);
        fixed += 8 + (uint32_t)ghostLen;
    }
    (void)from; (void)gFrom;
    free(packed);
    free(ghost);
    s_rep.msg = m;
    s_rep.msgLen = fixed;
}

void MatchReport_Submit(int32_t rating, const char *comment, const char *result)
{
    Half *h;
    if (!s_rep.active || s_rep.submitted)
        return;
    h = &s_rep.half[s_rep.player - 1];
    h->have = 1;
    h->rating = rating >= 1 && rating <= 5 ? rating : 0;
    copy_text(h->build, sizeof h->build, Build_Id());
    copy_text(h->platform, sizeof h->platform, Build_Platform());
    copy_text(h->result, sizeof h->result, result);
    copy_text(h->comment, sizeof h->comment, comment);
    h->log = read_log(&h->logLen);
    h->hasLog = h->log != NULL;
    {
        uint32_t n = 0;
        const char *clips = Ghost_Clips(&n);
        if (clips && n && (h->ghost = (char *)malloc(n + 1)) != NULL) {
            memcpy(h->ghost, clips, n);
            h->ghost[n] = '\0';
            h->ghostLen = n;
        }
    }
    /* Off: this player's own zip still has everything of theirs, but nothing about
     * them leaves the machine -- the other player is told only that. */
    {
        Half shared = *h;
        if (!sending()) {
            memset(&shared, 0, sizeof shared);
            copy_text(shared.name, sizeof shared.name, h->name);
            shared.off = 1;
        }
        build_message(&shared);
    }
    s_rep.submitted = 1;
    s_rep.submittedAt = SDL_GetTicks();
}

/* ---- the other player's half ------------------------------------------------ */

static void take_peer_half(const uint8_t *p, uint32_t len)
{
    const uint8_t *end = p + len, *q;
    Half *h;
    uint32_t logLen, packedLen;
    int32_t player;

    if (len < REPORT_HALF_FIXED || memcmp(p, REPORT_HALF_MAGIC, 4) != 0 || p[4] != REPORT_PROTOCOL)
        return;
    player = p[5];
    if (player != 3 - s_rep.player || Record_Get64(p + 8) != s_rep.matchId)
        return;                                 /* not this match's other half */
    h = &s_rep.half[player - 1];
    half_free(h);
    h->off    = (p[7] & REPORT_HALF_OFF) != 0;
    h->rating = p[6] <= 5 ? p[6] : 0;
    logLen    = Record_Get32(p + 16);
    packedLen = Record_Get32(p + 20);
    q = p + REPORT_HALF_FIXED;
    if (get_string(&q, end, h->name, sizeof h->name) || get_string(&q, end, h->build, sizeof h->build) ||
        get_string(&q, end, h->platform, sizeof h->platform) ||
        get_string(&q, end, h->result, sizeof h->result) ||
        get_string(&q, end, h->comment, sizeof h->comment))
        return;
    if ((p[7] & REPORT_HALF_LOG) && packedLen <= (uint32_t)(end - q) && logLen <= REPORT_LOG_MAX) {
        char *log = (char *)malloc(logLen + 1);
        if (log && tinfl_decompress_mem_to_mem(log, logLen, q, packedLen,
                                               TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF) == logLen) {
            log[logLen] = '\0';
            h->log = log;
            h->logLen = logLen;
            h->hasLog = 1;
        } else {
            free(log);
        }
    }
    if (p[7] & REPORT_HALF_LOG)
        q += packedLen;
    if ((p[7] & REPORT_HALF_GHOST) && q + 8 <= end) {
        uint32_t rawLen = Record_Get32(q), zLen = Record_Get32(q + 4);
        q += 8;
        if (zLen <= (uint32_t)(end - q) && rawLen <= 4 * REPORT_LOG_MAX) {
            char *g = (char *)malloc(rawLen + 1);
            if (g && tinfl_decompress_mem_to_mem(g, rawLen, q, zLen,
                                                 TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF) == rawLen) {
                g[rawLen] = '\0';
                h->ghost = g;
                h->ghostLen = rawLen;
            } else {
                free(g);
            }
        }
    }
    h->have = 1;
}

/* ---- the zip ----------------------------------------------------------------- */

static void safe_name(char *out, size_t cap, const char *name)
{
    size_t o = 0;
    const char *c;
    for (c = name ? name : ""; *c && o + 1 < cap; c++) {
        char ch = *c;
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-')
            out[o++] = ch;
        else if (ch >= 'a' && ch <= 'z')
            out[o++] = (char)(ch - 'a' + 'A');
        else if (o && out[o - 1] != '_')
            out[o++] = '_';
    }
    while (o && out[o - 1] == '_')
        o--;
    if (!o)
        o = (size_t)snprintf(out, cap, "PLAYER");
    out[o] = '\0';
}

static const char *stars(int32_t rating)
{
    static const char *const text[6] = { "no rating", "1 of 5", "2 of 5", "3 of 5", "4 of 5", "5 of 5" };
    return text[rating >= 0 && rating <= 5 ? rating : 0];
}

static uint32_t summary(char *out, size_t cap)
{
    char when[32];
    size_t n;
    int32_t i;
    struct tm *tm = localtime(&s_rep.kickoff);

    if (!tm || !strftime(when, sizeof when, "%Y-%m-%d %H:%M", tm))
        copy_text(when, sizeof when, "?");
    n = (size_t)snprintf(out, cap,
        PROJECT_NAME " -- netplay match report\r\n"
        "\r\n"
        "match id     %016llx\r\n"
        "kick-off     %s (this machine's clock)\r\n"
        "teams        %s v %s\r\n"
        "played       %s, input delay %d, width %d\r\n"
        "written by   player %d\r\n",
        (unsigned long long)s_rep.matchId, when, s_rep.team[0], s_rep.team[1],
        s_rep.relayToken ? "through the server's relay" : "peer to peer",
        (int)s_rep.delay, (int)s_rep.simWidth, (int)s_rep.player);
    for (i = 0; i < 2 && n < cap; i++) {
        const Half *h = &s_rep.half[i];
        n += (size_t)snprintf(out + n, cap - n, "\r\nplayer %d     %s%s\r\n", (int)(i + 1),
                              h->name[0] ? h->name : "?", i == 0 ? " (host)" : "");
        if (n >= cap)
            break;
        if (!h->have)
            n += (size_t)snprintf(out + n, cap - n, "             sent nothing: left, or did not answer in time\r\n");
        else if (h->off)
            n += (size_t)snprintf(out + n, cap - n, "             has SEND MATCH REPORTS off\r\n");
        else
            n += (size_t)snprintf(out + n, cap - n,
                "build        %s, %s\r\n"
                "saw          %s\r\n"
                "rating       %s\r\n"
                "comment      %s\r\n"
                "log          %s\r\n"
                "ghosts       %s\r\n",
                h->build, h->platform, h->result[0] ? h->result : "-", stars(h->rating),
                h->comment[0] ? h->comment : "-",
                h->hasLog ? (i == 0 ? "SWOS_player1.log" : "SWOS_player2.log") : "not sent",
                h->ghost ? (i == 0 ? "ghost_player1.csv (scripts/ghost_view.py)"
                                   : "ghost_player2.csv (scripts/ghost_view.py)")
                         : "none: no G-mark and nothing detected");
    }
    return n < cap ? (uint32_t)n : (uint32_t)cap - 1;
}

static void make_dir(const char *path)
{
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

static int compare_names(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Keep the newest REPORT_KEEP: the names start with the date, so they sort by age. */
static void prune(const char *dir)
{
    char *names[256];
    int32_t count = 0, i;
    struct dirent *e;
    DIR *d = opendir(dir);
    if (!d)
        return;
    while ((e = readdir(d)) != NULL && count < 256) {
        size_t n = strlen(e->d_name);
        if (n > 4 && strcmp(e->d_name + n - 4, ".zip") == 0 && (names[count] = strdup(e->d_name)) != NULL)
            count++;
    }
    closedir(d);
    qsort(names, (size_t)count, sizeof names[0], compare_names);
    for (i = 0; i < count; i++) {
        if (i < count - REPORT_KEEP) {
            char path[1200];
            snprintf(path, sizeof path, "%s/%s", dir, names[i]);
            remove(path);
        }
        free(names[i]);
    }
}

/* ---- the upload ----------------------------------------------------------- */

static SDL_atomic_t s_uploading;         /* upload threads still running */

typedef struct {
    char     host[72];
    uint16_t port;
    uint8_t *data;
    uint32_t len;
} Upload;

/* Where: SWOS_REPORTS, else the server this build logs in to. "" = nowhere. */
static int32_t upload_target(char *host, size_t cap, uint16_t *port)
{
    const char *spec = getenv("SWOS_REPORTS");
    const char *colon;
    size_t n;

#ifdef __EMSCRIPTEN__
    (void)spec; (void)colon; (void)n; (void)host; (void)cap; (void)port;
    return 1;                           /* a browser has no TCP */
#else
    if (!spec || !spec[0]) {
        /* The automated tests play match after match against a local server, or
         * none: they upload only where SWOS_REPORTS says. */
        if (getenv("SWOS_LOBBY_READY") || getenv("SWOS_LIVE_FRAMES") || getenv("SWOS_MM_AUTO"))
            return 1;
        spec = "server";
    }
    if (SDL_strcasecmp(spec, "off") == 0 || SDL_strcasecmp(spec, "false") == 0)
        return 1;
    if (SDL_strcasecmp(spec, "server") == 0) {
        snprintf(host, cap, "%s", MmClient_DefaultHost());
        *port = MmClient_DefaultPort();
        return host[0] ? 0 : 1;
    }
    colon = strrchr(spec, ':');
    n = colon ? (size_t)(colon - spec) : strlen(spec);
    if (n == 0 || n >= cap)
        return 1;
    memcpy(host, spec, n);
    host[n] = '\0';
    *port = REPORT_PORT;
    if (colon) {
        int v = atoi(colon + 1);
        if (v <= 0 || v > 65535)
            return 1;
        *port = (uint16_t)v;
    }
    return 0;
#endif
}

#ifndef __EMSCRIPTEN__
/* Wait until `fd` can do `events`, at most `ms`. */
static int32_t wait_for(int fd, short events, int ms)
{
    struct pollfd pfd;
    int r;
    pfd.fd = fd;
    pfd.events = events;
    pfd.revents = 0;
    do {
        r = poll(&pfd, 1, ms);
    } while (r < 0 && errno == EINTR);
    return r > 0 ? 0 : 1;
}

static const char *send_report(const Upload *u)
{
    char portText[8];
    struct addrinfo hints, *res = NULL;
    uint32_t done = 0;
    uint8_t answer[5];
    uint32_t got = 0;
    int fd, err = 0;
    socklen_t el = (socklen_t)sizeof err;

    snprintf(portText, sizeof portText, "%u", (unsigned)u->port);
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(u->host, portText, &hints, &res) != 0 || !res)
        return "cannot find the server";
    fd = (int)socket(res->ai_family, SOCK_STREAM, 0);
    if (fd < 0) {
        freeaddrinfo(res);
        return "no socket";
    }
#ifdef SO_NOSIGPIPE
    {
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
    }
#endif
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0 && errno != EINPROGRESS) {
        freeaddrinfo(res);
        close(fd);
        return "cannot connect";
    }
    freeaddrinfo(res);
    if (wait_for(fd, POLLOUT, 8000) ||
        getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&err, &el) != 0 || err != 0) {
        close(fd);
        return "cannot connect";
    }
    while (done < u->len) {
        ssize_t n = send(fd, (const char *)u->data + done, u->len - done, MSG_NOSIGNAL);
        if (n > 0) {
            done += (uint32_t)n;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            if (wait_for(fd, POLLOUT, 15000)) {
                close(fd);
                return "the server stopped taking it";
            }
        } else {
            close(fd);
            return "the connection broke";
        }
    }
    while (got < sizeof answer) {
        ssize_t n;
        if (wait_for(fd, POLLIN, 15000)) {
            close(fd);
            return "no answer from the server";
        }
        n = recv(fd, (char *)answer + got, sizeof answer - got, 0);
        if (n > 0)
            got += (uint32_t)n;
        else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            close(fd);
            return "the server does not take reports (no --reports)";
        }
    }
    close(fd);
    if (memcmp(answer, REPORT_UPLOAD_MAGIC, 4) != 0)
        return "an answer that is not a report server's";
    return answer[4] == 0 ? NULL : "the server refused it";
}

static int SDLCALL upload_thread(void *arg)
{
    Upload *u = (Upload *)arg;
    const char *why = send_report(u);
    if (why)
        fprintf(stderr, "[report] not sent to %s:%u: %s\n", u->host, (unsigned)u->port, why);
    else
        fprintf(stderr, "[report] sent to %s:%u (%u bytes)\n", u->host, (unsigned)u->port, (unsigned)u->len);
    free(u->data);
    free(u);
    SDL_AtomicAdd(&s_uploading, -1);
    return 0;
}
#endif

static void upload(const uint8_t *zip, uint32_t zipLen)
{
    const Half *me = &s_rep.half[s_rep.player - 1];
    const Half *them = &s_rep.half[2 - s_rep.player];
    Upload *u;
    uint32_t n;

    if (!sending() || zipLen > REPORT_ZIP_MAX)
        return;
    u = (Upload *)calloc(1, sizeof *u);
    if (!u)
        return;
    if (upload_target(u->host, sizeof u->host, &u->port) != 0 ||
        !(u->data = (uint8_t *)malloc(REPORT_UPLOAD_FIXED + 4 * 256 + zipLen))) {
        free(u);
        return;
    }
    memcpy(u->data, REPORT_UPLOAD_MAGIC, 4);
    u->data[4] = REPORT_PROTOCOL;
    u->data[5] = (uint8_t)s_rep.player;
    u->data[6] = (uint8_t)me->rating;
    u->data[7] = 0;
    Record_Put64(u->data + 8, s_rep.matchId);
    Record_Put32(u->data + 16, zipLen);
    n = REPORT_UPLOAD_FIXED;
    n += put_string(u->data + n, me->name);
    n += put_string(u->data + n, them->name);
    n += put_string(u->data + n, me->build);
    n += put_string(u->data + n, me->comment);
    memcpy(u->data + n, zip, zipLen);
    u->len = n + zipLen;
#ifndef __EMSCRIPTEN__
    {
        SDL_Thread *t;
        SDL_AtomicAdd(&s_uploading, 1);
        t = SDL_CreateThread(upload_thread, "match report", u);
        if (t) {
            SDL_DetachThread(t);
            return;
        }
        SDL_AtomicAdd(&s_uploading, -1);
    }
#endif
    free(u->data);
    free(u);
}

void MatchReport_Finish(void)
{
    mz_zip_archive zip;
    void *zipData = NULL;
    size_t zipLen = 0;
    char text[4096], dir[1100], path[1300], when[24], p1[24], p2[24];
    struct tm *tm;
    int32_t i, ok;
    FILE *f;

    if (!s_rep.active)
        return;
    if (!s_rep.submitted) {             /* never answered: the match ended the app, say */
        reset();
        return;
    }

    memset(&zip, 0, sizeof zip);
    ok = mz_zip_writer_init_heap(&zip, 0, 64 << 10);
    for (i = 0; ok && i < 2; i++) {
        const Half *h = &s_rep.half[i];
        if (h->log)
            ok = mz_zip_writer_add_mem(&zip, i == 0 ? "SWOS_player1.log" : "SWOS_player2.log",
                                       h->log, h->logLen, MZ_DEFAULT_COMPRESSION);
        if (ok && h->ghost)
            ok = mz_zip_writer_add_mem(&zip, i == 0 ? "ghost_player1.csv" : "ghost_player2.csv",
                                       h->ghost, h->ghostLen, MZ_DEFAULT_COMPRESSION);
    }
    if (ok) {
        uint32_t n = summary(text, sizeof text);
        ok = mz_zip_writer_add_mem(&zip, "rating.txt", text, n, MZ_DEFAULT_COMPRESSION);
    }
    if (ok)
        ok = mz_zip_writer_finalize_heap_archive(&zip, &zipData, &zipLen);
    mz_zip_writer_end(&zip);
    if (!ok) {
        fprintf(stderr, "[report] could not build the zip\n");
        free(zipData);
        reset();
        return;
    }

    tm = localtime(&s_rep.kickoff);
    if (!tm || !strftime(when, sizeof when, "%Y-%m-%d_%H%M%S", tm))
        copy_text(when, sizeof when, "match");
    safe_name(p1, sizeof p1, s_rep.half[0].name);
    safe_name(p2, sizeof p2, s_rep.half[1].name);
    snprintf(dir, sizeof dir, "%s/netplay", swos_save_dir());
    make_dir(dir);
    snprintf(path, sizeof path, "%s/%s_%s_vs_%s.zip", dir, when, p1, p2);
    f = fopen(path, "wb");
    if (f && fwrite(zipData, 1, zipLen, f) == zipLen) {
        fclose(f);
        prune(dir);
        swos_save_commit();
        fprintf(stderr, "[report] match %016llx: wrote %s (%u bytes; player 1 %s, player 2 %s)\n",
                (unsigned long long)s_rep.matchId, path, (unsigned)zipLen,
                s_rep.half[0].have ? stars(s_rep.half[0].rating) : "sent nothing",
                s_rep.half[1].have ? stars(s_rep.half[1].rating) : "sent nothing");
    } else {
        if (f)
            fclose(f);
        fprintf(stderr, "[report] cannot write %s\n", path);
    }
    upload((const uint8_t *)zipData, (uint32_t)zipLen);
    mz_free(zipData);
    reset();
}

int32_t MatchReport_Pending(void)
{
    return s_rep.active && s_rep.submitted;
}

void MatchReport_Flush(uint32_t waitMs)
{
    uint32_t start = SDL_GetTicks();
    MatchReport_Finish();
    while (SDL_AtomicGet(&s_uploading) > 0 && SDL_GetTicks() - start < waitMs)
        SDL_Delay(20);
}

void MatchReport_Poll(void)
{
    const uint8_t *msg;
    uint32_t len;
    uint16_t gen;
    P2PLinkState st;

    if (!s_rep.active || !s_rep.submitted)
        return;
    st = P2PLink_State();
    if (!s_rep.sent && s_rep.msg && st == P2P_CONNECTED &&
        P2PLink_Send(CH_REPORT, s_rep.msg, s_rep.msgLen) == 0)
        s_rep.sent = 1;
    if (!s_rep.half[2 - s_rep.player].have && st == P2P_CONNECTED &&
        (msg = P2PLink_Received(CH_REPORT, &len, &gen)) != NULL)
        take_peer_half(msg, len);

    /* Both halves in -- and ours delivered, since leaving the lobby closes the link
     * under it -- or the other player gone, or out of time. */
    if ((s_rep.half[2 - s_rep.player].have && s_rep.sent && P2PLink_Delivered(CH_REPORT)) ||
        st == P2P_FAILED || SDL_GetTicks() - s_rep.submittedAt > REPORT_WAIT_MS)
        MatchReport_Finish();
}

#else   /* !SWOS_NETPLAY: no online matches to report */

void MatchReport_Begin(const MatchRecordInfo *info) { (void)info; }
void MatchReport_Submit(int32_t rating, const char *comment, const char *result)
{
    (void)rating; (void)comment; (void)result;
}
void MatchReport_Poll(void)   {}
void MatchReport_Finish(void) {}
int32_t MatchReport_Pending(void) { return 0; }
void MatchReport_Flush(uint32_t waitMs) { (void)waitMs; }

#endif
