/*
 * netplay_ghost.c -- what a netplay peer drew against the match as finally played.
 * include/netplay_ghost.h has why.
 */
#include "netplay_ghost.h"
#include "project_identity.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game_types.h"
#include "sprite_data.h"
#include "dos_gameplay.h"       /* DosGameplay_TargetFps: frames a second, for match times */
#include "endian_helpers.h"
#include "srp_writer.h"         /* SRP_PRED_* / SRP_SEEN_*: what a .SRP keeps of all this */

extern TeamGeneralInfo _topTeamData, _bottomTeamData;
extern Sprite *_teamSpritesTable[];

enum {
    GHOST_PLAYERS = 22,         /* _teamSpritesTable: team 1's eleven, then team 2's */
    TOUCH_PX      = 5,          /* two players this close are drawn on top of each other */
    APART_PX      = 12,         /* ... and this far apart in the other version, it never happened */
    NEAR_BALL_PX  = 16,         /* only overlaps where the play is */
    CARRIER_PX    = 6,          /* the player nearest the ball, this close, has it at his feet */
    WALK_FRAMES   = 4,          /* on top of the carrier this long without winning it: a walk-through */
    KEPT_FRAMES   = 8,          /* ... and the carrier still has it this long after */
    MAX_PRINTED   = 12,         /* events printed, longest first */
    CLIP_S        = 3,          /* a saved window: this many seconds either side */
    CLIP_EVENTS   = 3,          /* ... around the worst events */
    CLIP_JUMPS    = 2,          /* ... and around the biggest jumps */
    JUMP_PX       = 24,         /* a player drawn this far from where he was: a jump */
    CLIP_MAX      = 8,          /* windows kept at most */
};

typedef enum {
    EV_DRAWN_ONLY,              /* drawn touching near the ball, apart in the final game */
    EV_FINAL_ONLY,              /* touching in the final game, drawn apart */
    EV_WALK_THROUGH,            /* on top of the ball carrier in the final game, won nothing */
    EV_JUMP,                    /* a player drawn JUMP_PX+ from where he was: he jumps when corrected */
    EV_KINDS
} GhostKind;

static const char *const k_kindText[EV_KINDS] = {
    "drawn touching, never did",
    "touched, never drawn",
    "walked through the man on the ball, won nothing",
    "drawn far from where he was",
};

typedef struct {
    int16_t x[GHOST_PLAYERS], y[GHOST_PLAYERS];
    int16_t bx, by, bz;
    uint8_t owner;              /* 1 top team has the ball, 2 bottom, 0 nobody */
    uint8_t have;
} GhostFrame;

typedef struct {
    int32_t   frame, frames;    /* first frame and length */
    int32_t   a, b;             /* the two players (indices into the table) */
    int32_t   drawnPx, truePx;  /* their distance drawn, and in the final game, at the first frame */
    GhostKind kind;
} GhostEvent;

static struct {
    int32_t     player;
    GhostFrame *drawn, *final;
    uint32_t    cap;
    uint32_t    corrections, changed;   /* re-simulated plays, and those that moved anything */
    int32_t     reported;               /* Ghost_Report has run for this match */
    GhostEvent *events;
    uint32_t    eventCount, eventCap;
    char       *clips;              /* Ghost_Clips: the windows, as CSV */
    uint32_t    clipsLen, clipsCap;
    uint8_t    *pred;               /* PRED: each frame's lag and flags (SRP_PRED_*) */
    uint8_t    *read;               /* READ: SRP_READ_BYTES a frame -- the controls as read, ms since the last */
    uint32_t    readLastMs;
    int32_t     readAny;            /* a frame's read has been noted this match */
    uint16_t    delay;              /* the input delay the reads were made under */
    uint8_t     balance;            /* this player has the prediction balance on */
    uint8_t    *predOut, *seenOut, *readOut;  /* Ghost_Prediction's */
    uint32_t    seenOutLen;
} s_g;

static int32_t reserve(uint32_t frame)
{
    uint32_t cap;
    GhostFrame *d, *f;
    uint8_t *p, *r;
    if (frame < s_g.cap)
        return 0;
    cap = s_g.cap ? s_g.cap : 4096;
    while (cap <= frame)
        cap *= 2;
    d = (GhostFrame *)realloc(s_g.drawn, cap * sizeof *d);
    if (d)
        s_g.drawn = d;
    f = (GhostFrame *)realloc(s_g.final, cap * sizeof *f);
    if (f)
        s_g.final = f;
    p = (uint8_t *)realloc(s_g.pred, cap);
    if (p)
        s_g.pred = p;
    r = (uint8_t *)realloc(s_g.read, (size_t)cap * SRP_READ_BYTES);
    if (r)
        s_g.read = r;
    if (!d || !f || !p || !r)
        return 1;
    memset(s_g.read + (size_t)s_g.cap * SRP_READ_BYTES, SRP_READ_UNKNOWN, (size_t)(cap - s_g.cap) * SRP_READ_BYTES);
    memset(s_g.drawn + s_g.cap, 0, (cap - s_g.cap) * sizeof *d);
    memset(s_g.final + s_g.cap, 0, (cap - s_g.cap) * sizeof *f);
    memset(s_g.pred + s_g.cap, SRP_PRED_UNKNOWN, cap - s_g.cap);
    s_g.cap = cap;
    return 0;
}

void Ghost_Begin(int32_t player)
{
    s_g.player = player;
    s_g.corrections = s_g.changed = 0;
    s_g.reported = 0;
    s_g.eventCount = 0;
    s_g.clipsLen = 0;
    if (s_g.cap) {
        memset(s_g.drawn, 0, s_g.cap * sizeof *s_g.drawn);
        memset(s_g.final, 0, s_g.cap * sizeof *s_g.final);
        memset(s_g.pred, SRP_PRED_UNKNOWN, s_g.cap);
        memset(s_g.read, SRP_READ_UNKNOWN, (size_t)s_g.cap * SRP_READ_BYTES);
    }
    s_g.readAny = 0;
    s_g.delay = 0;
    s_g.balance = 0;
}

static void capture(GhostFrame *g)
{
    int32_t i;
    for (i = 0; i < GHOST_PLAYERS; i++) {
        const Sprite *s = _teamSpritesTable[i];
        g->x[i] = s ? s->x.s.i : 0;
        g->y[i] = s ? s->y.s.i : 0;
    }
    g->bx = _ballSprite.x.s.i;
    g->by = _ballSprite.y.s.i;
    g->bz = _ballSprite.z.s.i;
    g->owner = _topTeamData.playerHasBall ? 1 : _bottomTeamData.playerHasBall ? 2 : 0;
    g->have = 1;
}

void Ghost_Played(int32_t frame, int32_t first)
{
    if (frame < 0 || reserve((uint32_t)frame))
        return;
    if (first) {
        capture(&s_g.final[frame]);         /* the last play of a frame is the one kept */
        s_g.drawn[frame] = s_g.final[frame];
    } else {
        GhostFrame before = s_g.final[frame];
        capture(&s_g.final[frame]);
        s_g.corrections++;
        s_g.changed += memcmp(&before, &s_g.final[frame], sizeof before) != 0;
        if (s_g.pred[frame] != SRP_PRED_UNKNOWN)
            s_g.pred[frame] |= SRP_PRED_REPLAYED;
    }
}

void Ghost_Lag(int32_t frame, int32_t lag, int32_t stalled)
{
    if (frame < 0 || reserve((uint32_t)frame))
        return;
    if (lag < 0)
        lag = 0;
    if (lag > (int32_t)SRP_PRED_LAG)
        lag = SRP_PRED_LAG;
    s_g.pred[frame] = (uint8_t)(lag | (stalled ? SRP_PRED_STALLED : 0));
}

/* READ (ReportBot 0025): the controls this player's game read for `frame`, taken when
 * GGPO accepted them -- they are applied `delay` frames later -- and the wall clock then.
 * Two bytes kept in memory; nothing is written or logged during play. */
void Ghost_Read(int32_t frame, uint8_t controls, uint32_t nowMs, int32_t delay, uint8_t flags,
                int32_t balanceOn)
{
    uint32_t dt;
    if (frame < 0 || reserve((uint32_t)frame))
        return;
    dt = s_g.readAny ? nowMs - s_g.readLastMs : 0;
    s_g.readLastMs = nowMs;
    s_g.readAny = 1;
    s_g.delay = (uint16_t)(delay < 0 ? 0 : delay > 255 ? 255 : delay);
    s_g.balance = balanceOn != 0;
    s_g.read[(size_t)frame * SRP_READ_BYTES] = controls;
    s_g.read[(size_t)frame * SRP_READ_BYTES + 1] = (uint8_t)(dt > 255 ? 255 : dt);
    s_g.read[(size_t)frame * SRP_READ_BYTES + 2] = flags;
}

int32_t Ghost_ReadDelay(void) { return s_g.delay; }
int32_t Ghost_ReadBalance(void) { return s_g.balance; }

void Ghost_ReadBytes(uint32_t frame, uint8_t *out)
{
    uint32_t b;
    for (b = 0; b < SRP_READ_BYTES; b++)
        out[b] = frame < s_g.cap ? s_g.read[(size_t)frame * SRP_READ_BYTES + b] : SRP_READ_UNKNOWN;
}

int32_t Ghost_Player(void)
{
    return s_g.player;
}

uint8_t Ghost_PredByte(uint32_t frame)
{
    return frame < s_g.cap ? s_g.pred[frame] : SRP_PRED_UNKNOWN;
}

/* An object's position as the SEEN record keeps it: players where they stand, the ball
 * where it is seen (its height taken off). */
static void seen_pos(const GhostFrame *g, int32_t i, int16_t *x, int16_t *y)
{
    if (i == SRP_SEEN_BALL) {
        *x = g->bx;
        *y = (int16_t)(g->by - g->bz);
    } else {
        *x = g->x[i];
        *y = g->y[i];
    }
}

uint32_t Ghost_SeenRecord(uint32_t frame, uint8_t *out)
{
    const GhostFrame *d, *f;
    uint32_t n = 0, i;
    if (frame >= s_g.cap || !s_g.drawn[frame].have || !s_g.final[frame].have)
        return 0;
    d = &s_g.drawn[frame];
    f = &s_g.final[frame];
    for (i = 0; i < SRP_SEEN_OBJECTS; i++) {
        int16_t dx, dy, fx, fy;
        uint8_t *p = out + SRP_SEEN_REC_HEAD + n * SRP_SEEN_OBJ_BYTES;
        seen_pos(d, (int32_t)i, &dx, &dy);
        seen_pos(f, (int32_t)i, &fx, &fy);
        if (dx == fx && dy == fy)
            continue;
        p[0] = (uint8_t)i;
        BE_WRITE_U16(p + 1, (uint16_t)dx);
        BE_WRITE_U16(p + 3, (uint16_t)dy);
        BE_WRITE_U16(p + 5, (uint16_t)fx);
        BE_WRITE_U16(p + 7, (uint16_t)fy);
        n++;
    }
    if (n == 0)
        return 0;
    BE_WRITE_U32(out, frame);
    out[4] = (uint8_t)s_g.player;
    out[5] = (uint8_t)n;
    return SRP_SEEN_REC_HEAD + n * SRP_SEEN_OBJ_BYTES;
}

int32_t Ghost_Prediction(uint32_t frames, SrpPrediction *out)
{
    uint32_t f, n = 0;
    uint8_t *seen;

    memset(out, 0, sizeof *out);
    free(s_g.predOut);
    free(s_g.seenOut);
    free(s_g.readOut);
    s_g.readOut = (uint8_t *)malloc((size_t)frames * SRP_READ_BYTES + 1);
    s_g.predOut = (uint8_t *)malloc(frames + 1);
    seen = (uint8_t *)malloc((size_t)frames * 8 + 256);     /* grown below as needed */
    s_g.seenOut = seen;
    s_g.seenOutLen = 0;
    if (!s_g.predOut || !seen || !s_g.readOut)
        return 1;
    for (f = 0; f < frames; f++)
        Ghost_ReadBytes(f, s_g.readOut + (size_t)f * SRP_READ_BYTES);
    {
        uint32_t cap = frames * 8 + 256;
        for (f = 0; f < frames; f++) {
            uint8_t rec[SRP_SEEN_REC_HEAD + SRP_SEEN_OBJECTS * SRP_SEEN_OBJ_BYTES];
            uint32_t len;
            s_g.predOut[f] = Ghost_PredByte(f);
            len = Ghost_SeenRecord(f, rec);
            if (!len)
                continue;
            if (n + len > cap) {
                uint8_t *grown = (uint8_t *)realloc(s_g.seenOut, cap * 2 + len);
                if (!grown)
                    return 1;
                s_g.seenOut = grown;
                cap = cap * 2 + len;
            }
            memcpy(s_g.seenOut + n, rec, len);
            n += len;
        }
    }
    s_g.seenOutLen = n;
    out->pred = s_g.predOut;
    out->predFrames = frames;
    out->seen = s_g.seenOut;
    out->seenLen = n;
    if (s_g.readAny) {
        out->read = s_g.readOut;
        out->readFrames = frames;
        out->delay = s_g.delay;
        out->balance = s_g.balance;
    }
    return 0;
}

/* "0:34": frames into the match as minutes and seconds of play. */
static const char *clock_text(int32_t frame, char *buf, size_t cap)
{
    int32_t fps = DosGameplay_TargetFps(), s;
    if (fps <= 0)
        fps = 50;
    s = frame / fps;
    snprintf(buf, cap, "%d:%02d", (int)(s / 60), (int)(s % 60));
    return buf;
}

static int32_t dist(const GhostFrame *g, int32_t a, int32_t b)
{
    int32_t dx = g->x[a] - g->x[b], dy = g->y[a] - g->y[b];
    dx = dx < 0 ? -dx : dx;
    dy = dy < 0 ? -dy : dy;
    return dx > dy ? dx : dy;               /* the larger axis: sprites are boxes */
}

static int32_t ballDist(const GhostFrame *g, int32_t a)
{
    int32_t dx = g->x[a] - g->bx, dy = g->y[a] - g->by;
    dx = dx < 0 ? -dx : dx;
    dy = dy < 0 ? -dy : dy;
    return dx > dy ? dx : dy;
}

static int32_t nearBall(const GhostFrame *g, int32_t a)
{
    return ballDist(g, a) < NEAR_BALL_PX;
}

/* One team-1 player against one team-2 player: touching near the ball in `a`, and
 * apart in `b`. */
static int32_t ghostPair(const GhostFrame *a, const GhostFrame *b, int32_t i, int32_t j)
{
    return dist(a, i, j) <= TOUCH_PX && (nearBall(a, i) || nearBall(a, j)) &&
           dist(b, i, j) >= APART_PX;
}

/* The player with the ball at his feet in `g`, or -1: the nearest to a ball on the
 * ground, and close enough to be playing it. */
static int32_t carrier(const GhostFrame *g)
{
    int32_t i, best = -1, bestD = CARRIER_PX + 1;
    if (!g->have || g->bz > 4)
        return -1;
    for (i = 0; i < GHOST_PLAYERS; i++) {
        int32_t d = ballDist(g, i);
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

static void addEvent(const GhostEvent *e)
{
    if (s_g.eventCount == s_g.eventCap) {
        uint32_t cap = s_g.eventCap ? s_g.eventCap * 2 : 256;
        GhostEvent *n = (GhostEvent *)realloc(s_g.events, cap * sizeof *n);
        if (!n)
            return;
        s_g.events = n;
        s_g.eventCap = cap;
    }
    s_g.events[s_g.eventCount++] = *e;
}

static int compareEvents(const void *p, const void *q)
{
    const GhostEvent *a = (const GhostEvent *)p, *b = (const GhostEvent *)q;
    return b->frames != a->frames ? b->frames - a->frames : a->frame - b->frame;
}

static void printEvent(const GhostEvent *e)
{
    char t[16];
    int32_t a = e->a < 11 ? e->a : e->b, b = e->a < 11 ? e->b : e->a;
    if (e->kind == EV_JUMP) {
        fprintf(stderr, "[ghost]   %s (frame %d), %d frame(s): team %d #%d %s -- up to %d px\n",
                clock_text(e->frame, t, sizeof t), (int)e->frame, (int)e->frames, (int)e->a / 11 + 1,
                (int)e->a % 11 + 1, k_kindText[e->kind], (int)e->drawnPx);
        return;
    }
    fprintf(stderr, "[ghost]   %s (frame %d), %d frame(s): team 1 #%d and team 2 #%d %s -- "
                    "%d px apart drawn, %d px in the final game\n",
            clock_text(e->frame, t, sizeof t), (int)e->frame, (int)e->frames, (int)a + 1,
            (int)b - 10, k_kindText[e->kind], (int)e->drawnPx, (int)e->truePx);
}

static void writeTrace(int32_t lastFrame)
{
    const char *path = getenv("SWOS_GHOST_TRACE");
    FILE *f;
    int32_t fr, i, k;
    if (!path || !path[0] || !(f = fopen(path, "w")))
        return;
    fprintf(f, "frame,version,ball_x,ball_y,ball_z,owner");
    for (i = 0; i < GHOST_PLAYERS; i++)
        fprintf(f, ",t%dp%d_x,t%dp%d_y", i / 11 + 1, i % 11 + 1, i / 11 + 1, i % 11 + 1);
    fputc('\n', f);
    for (fr = 0; fr <= lastFrame && (uint32_t)fr < s_g.cap; fr++)
        for (k = 0; k < 2; k++) {
            const GhostFrame *g = k ? &s_g.final[fr] : &s_g.drawn[fr];
            if (!g->have)
                continue;
            fprintf(f, "%d,%s,%d,%d,%d,%d", fr, k ? "final" : "drawn", g->bx, g->by, g->bz, g->owner);
            for (i = 0; i < GHOST_PLAYERS; i++)
                fprintf(f, ",%d,%d", g->x[i], g->y[i]);
            fputc('\n', f);
        }
    fclose(f);
    fprintf(stderr, "[ghost] every frame, drawn and final, written to %s\n", path);
}

/* Rollback ghosts: a run of frames per team-1/team-2 pair, drawn and final disagreeing. */
static void findRollbackGhosts(int32_t lastFrame)
{
    static GhostEvent cur[2][11][11];
    static int8_t open[2][11][11];
    int32_t fr, i, j, k;

    memset(open, 0, sizeof open);
    for (fr = 0; fr <= lastFrame + 1; fr++) {
        const GhostFrame *d = fr <= lastFrame ? &s_g.drawn[fr] : NULL;
        const GhostFrame *t = fr <= lastFrame ? &s_g.final[fr] : NULL;
        int32_t valid = d && t && d->have && t->have;
        for (k = 0; k < 2; k++)
            for (i = 0; i < 11; i++)
                for (j = 0; j < 11; j++) {
                    int32_t hit = valid && (k == 0 ? ghostPair(d, t, i, 11 + j)
                                                   : ghostPair(t, d, i, 11 + j));
                    GhostEvent *e = &cur[k][i][j];
                    if (hit && !open[k][i][j]) {
                        e->frame = fr;
                        e->frames = 1;
                        e->a = i;
                        e->b = 11 + j;
                        e->drawnPx = dist(d, i, 11 + j);
                        e->truePx = dist(t, i, 11 + j);
                        e->kind = k == 0 ? EV_DRAWN_ONLY : EV_FINAL_ONLY;
                        open[k][i][j] = 1;
                    } else if (hit) {
                        e->frames++;
                    } else if (open[k][i][j]) {
                        addEvent(e);
                        open[k][i][j] = 0;
                    }
                }
    }
}

/* Walk-throughs in the FINAL game: an opponent on top of the man on the ball for
 * WALK_FRAMES and more, and the man still on the ball KEPT_FRAMES later. Plenty of
 * these are ordinary SWOS -- there are no bodies, only the ball -- so the count means
 * most at a moment a player complained of, or against a match played offline. */
static void findWalkThroughs(int32_t lastFrame)
{
    int32_t fr = 0;
    while (fr <= lastFrame) {
        const GhostFrame *t = &s_g.final[fr];
        int32_t c = carrier(t), o = -1, i, lo, hi, end;
        if (c >= 0) {
            lo = c < 11 ? 11 : 0;
            hi = lo + 11;
            for (i = lo; i < hi; i++)
                if (dist(t, c, i) <= TOUCH_PX && (o < 0 || dist(t, c, i) < dist(t, c, o)))
                    o = i;
        }
        if (o < 0) {
            fr++;
            continue;
        }
        for (end = fr + 1; end <= lastFrame; end++) {
            const GhostFrame *n = &s_g.final[end];
            if (carrier(n) != c || dist(n, c, o) > TOUCH_PX)
                break;
        }
        if (end - fr >= WALK_FRAMES) {
            int32_t kept = 1, k;
            for (k = end; k < end + KEPT_FRAMES && k <= lastFrame; k++)
                if (carrier(&s_g.final[k]) >= 0 && carrier(&s_g.final[k]) != c)
                    kept = 0;
            if (kept) {
                GhostEvent e;
                e.frame = fr;
                e.frames = end - fr;
                e.a = c;
                e.b = o;
                e.drawnPx = s_g.drawn[fr].have ? dist(&s_g.drawn[fr], c, o) : -1;
                e.truePx = dist(t, c, o);
                e.kind = EV_WALK_THROUGH;
                addEvent(&e);
            }
        }
        fr = end;
    }
}

/* ---- the windows a match report carries (Ghost_Clips) ---- */

static void clipPrintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void clipPrintf(const char *fmt, ...)
{
    va_list ap;
    int n;
    for (;;) {
        uint32_t room = s_g.clipsCap - s_g.clipsLen;
        va_start(ap, fmt);
        n = vsnprintf(s_g.clips ? s_g.clips + s_g.clipsLen : NULL, room, fmt, ap);
        va_end(ap);
        if (n < 0)
            return;
        if ((uint32_t)n < room) {
            s_g.clipsLen += (uint32_t)n;
            return;
        }
        {
            uint32_t cap = s_g.clipsCap ? s_g.clipsCap * 2 : 65536;
            char *grown;
            while (cap < s_g.clipsLen + (uint32_t)n + 1)
                cap *= 2;
            grown = (char *)realloc(s_g.clips, cap);
            if (!grown)
                return;
            s_g.clips = grown;
            s_g.clipsCap = cap;
        }
    }
}

static void clipRow(int32_t window, const char *why, int32_t fr, const char *version, const GhostFrame *g)
{
    int32_t i;
    clipPrintf("%d,%s,%d,%s,%d,%d,%d,%d", (int)window, why, (int)fr, version, g->bx, g->by, g->bz, g->owner);
    for (i = 0; i < GHOST_PLAYERS; i++)
        clipPrintf(",%d,%d", g->x[i], g->y[i]);
    clipPrintf("\n");
}

/* Windows of CLIP_S either side of the CLIP_EVENTS longest events,
 * overlapping ones merged: every frame as finally played, and as this player drew it
 * wherever that differed -- what the viewer (scripts/ghost_view.py) sets side by side. */
static void buildClips(int32_t lastFrame, int32_t fps, const GhostEvent *longest, int32_t nLongest)
{
    int32_t from[CLIP_MAX + 64], to[CLIP_MAX + 64], n = 0, i, j, fr;
    char why[CLIP_MAX + 64][48];

    s_g.clipsLen = 0;
    for (i = 0, j = 0; i < nLongest && j < CLIP_EVENTS && n < CLIP_MAX; i++) {
        static const char *const k_short[EV_KINDS] = { "drawn-not-real", "real-not-drawn", "walk-through", "jump" };
        if (longest[i].kind == EV_JUMP)
            continue;                   /* by size, below: a jump is short and can be huge */
        from[n] = longest[i].frame - CLIP_S * fps;
        to[n] = longest[i].frame + longest[i].frames + CLIP_S * fps;
        snprintf(why[n], sizeof why[n], "%s", k_short[longest[i].kind]);
        n++;
        j++;
    }
    /* The biggest jumps: the largest drawn error first. */
    {
        int32_t taken[CLIP_JUMPS], t, best;
        for (t = 0; t < CLIP_JUMPS && n < CLIP_MAX; t++) {
            best = -1;
            for (i = 0; i < nLongest; i++) {
                int32_t used = 0, u;
                if (longest[i].kind != EV_JUMP)
                    continue;
                for (u = 0; u < t; u++)
                    used |= taken[u] == i;
                if (!used && (best < 0 || longest[i].drawnPx > longest[best].drawnPx))
                    best = i;
            }
            if (best < 0)
                break;
            taken[t] = best;
            from[n] = longest[best].frame - CLIP_S * fps;
            to[n] = longest[best].frame + longest[best].frames + CLIP_S * fps;
            snprintf(why[n], sizeof why[n], "jump %dpx", (int)longest[best].drawnPx);
            n++;
        }
    }
    if (!n)
        return;
    /* Sorted by start, then merged where they overlap. */
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && from[j] < from[j - 1]; j--) {
            char t[48];
            int32_t a = from[j], b = to[j];
            from[j] = from[j - 1]; to[j] = to[j - 1];
            from[j - 1] = a; to[j - 1] = b;
            memcpy(t, why[j], sizeof t); memcpy(why[j], why[j - 1], sizeof t); memcpy(why[j - 1], t, sizeof t);
        }
    for (i = 0, j = 0; i < n; i++) {
        if (j > 0 && from[i] <= to[j - 1]) {
            if (to[i] > to[j - 1])
                to[j - 1] = to[i];
            if (strlen(why[j - 1]) + strlen(why[i]) + 2 < sizeof why[j - 1]) {
                strcat(why[j - 1], "+");
                strcat(why[j - 1], why[i]);
            }
            continue;
        }
        from[j] = from[i]; to[j] = to[i];
        memcpy(why[j], why[i], sizeof why[j]);
        j++;
    }
    n = j;

    clipPrintf("# " PROJECT_NAME " ghost windows (src/netplay_ghost.c): player %d, %d frames a second.\n"
               "# Every frame as finally played ('final'), and as this player drew it ('drawn') "
               "where that differed.\n# Players 1-11 are team 1, 12-22 team 2; positions in "
               "pitch pixels. scripts/ghost_view.py draws it.\n",
               (int)s_g.player, (int)fps);
    clipPrintf("window,why,frame,version,ball_x,ball_y,ball_z,owner");
    for (i = 0; i < GHOST_PLAYERS; i++)
        clipPrintf(",t%dp%d_x,t%dp%d_y", i / 11 + 1, i % 11 + 1, i / 11 + 1, i % 11 + 1);
    clipPrintf("\n");
    for (i = 0; i < n; i++) {
        if (from[i] < 0)
            from[i] = 0;
        if (to[i] > lastFrame)
            to[i] = lastFrame;
        for (fr = from[i]; fr <= to[i]; fr++) {
            const GhostFrame *d = &s_g.drawn[fr], *t = &s_g.final[fr];
            if (!t->have)
                continue;
            clipRow(i + 1, why[i], fr, "final", t);
            if (d->have && memcmp(d, t, sizeof *d) != 0)
                clipRow(i + 1, why[i], fr, "drawn", d);
        }
    }
}

/* Jumps: runs of frames in which some player was drawn JUMP_PX or more from where he
 * really was -- on the correction he jumps there. The biggest one's player is named. */
static int32_t worstOffset(const GhostFrame *d, const GhostFrame *t, int32_t *who)
{
    int32_t i, worst = 0;
    *who = -1;
    for (i = 0; i < GHOST_PLAYERS; i++) {
        int32_t dx = d->x[i] - t->x[i], dy = d->y[i] - t->y[i], off;
        dx = dx < 0 ? -dx : dx;
        dy = dy < 0 ? -dy : dy;
        off = dx > dy ? dx : dy;
        if (off > worst) {
            worst = off;
            *who = i;
        }
    }
    return worst;
}

static void findJumps(int32_t lastFrame)
{
    GhostEvent e;
    int32_t fr, open = 0;
    for (fr = 0; fr <= lastFrame + 1; fr++) {
        const GhostFrame *d = fr <= lastFrame ? &s_g.drawn[fr] : NULL;
        const GhostFrame *t = fr <= lastFrame ? &s_g.final[fr] : NULL;
        int32_t who = -1, off = d && t && d->have && t->have ? worstOffset(d, t, &who) : 0;
        if (off >= JUMP_PX) {
            if (!open) {
                memset(&e, 0, sizeof e);
                e.frame = fr;
                e.kind = EV_JUMP;
                e.b = -1;
                open = 1;
            }
            e.frames++;
            if (off > e.drawnPx) {
                e.drawnPx = off;
                e.a = who;
            }
        } else if (open) {
            addEvent(&e);
            open = 0;
        }
    }
}

const char *Ghost_Clips(uint32_t *len)
{
    *len = s_g.clipsLen;
    return s_g.clipsLen ? s_g.clips : NULL;
}

void Ghost_Report(int32_t lastFrame)
{
    uint32_t frames = 0, differed = 0, ownerWrong = 0, count[EV_KINDS] = { 0 }, total[EV_KINDS] = { 0 };
    int32_t fr, i, k, m, maxOff = 0, maxOffFrame = -1, maxOffWho = -1, fps = DosGameplay_TargetFps();
    uint64_t sumOff = 0, offSamples = 0;
    GhostEvent *sorted;

    if (!s_g.cap || lastFrame < 0 || s_g.reported)
        return;
    s_g.reported = 1;                   /* once a match: p2pFinish and liveEnd both ask */
    if ((uint32_t)lastFrame >= s_g.cap)
        lastFrame = (int32_t)s_g.cap - 1;
    if (fps <= 0)
        fps = 50;

    for (fr = 0; fr <= lastFrame; fr++) {
        const GhostFrame *d = &s_g.drawn[fr], *t = &s_g.final[fr];
        int32_t same = 1;
        if (!d->have || !t->have)
            continue;
        frames++;
        for (i = 0; i < GHOST_PLAYERS; i++) {
            int32_t dx = d->x[i] - t->x[i], dy = d->y[i] - t->y[i], off;
            dx = dx < 0 ? -dx : dx;
            dy = dy < 0 ? -dy : dy;
            off = dx > dy ? dx : dy;
            if (off) {
                same = 0;
                sumOff += (uint32_t)off;
                offSamples++;
                if (off > maxOff) {
                    maxOff = off;
                    maxOffFrame = fr;
                    maxOffWho = i;
                }
            }
        }
        if (d->bx != t->bx || d->by != t->by || d->bz != t->bz)
            same = 0;
        differed += !same;
        ownerWrong += d->owner != t->owner;
    }
    s_g.eventCount = 0;
    findRollbackGhosts(lastFrame);
    findWalkThroughs(lastFrame);
    findJumps(lastFrame);
    for (i = 0; i < (int32_t)s_g.eventCount; i++) {
        count[s_g.events[i].kind]++;
        total[s_g.events[i].kind] += (uint32_t)s_g.events[i].frames;
    }

    fprintf(stderr, "[ghost] player %d: %u frames, %u drawn differently from the final game (%.1f%%); "
                    "players drawn up to %d px from where they were (mean %.1f px when off); "
                    "possession drawn wrong on %u frames\n",
            (int)s_g.player, frames, differed, frames ? 100.0 * differed / frames : 0.0, (int)maxOff,
            offSamples ? (double)sumOff / (double)offSamples : 0.0, ownerWrong);
    fprintf(stderr, "[ghost] player %d: %u frames re-simulated, %u of them changed what was drawn\n",
            (int)s_g.player, s_g.corrections, s_g.changed);
    fprintf(stderr, "[ghost] player %d: rollback ghosts: %u drawn touching near the ball and %d+ px "
                    "apart in the final game (%u frames); %u touches the final game had and this "
                    "player never saw (%u frames)\n",
            (int)s_g.player, count[EV_DRAWN_ONLY], APART_PX, total[EV_DRAWN_ONLY],
            count[EV_FINAL_ONLY], total[EV_FINAL_ONLY]);
    fprintf(stderr, "[ghost] player %d: %u walk-throughs in the final game -- an opponent on top of "
                    "the man on the ball for %d+ frames who did not win it (%u frames)\n",
            (int)s_g.player, count[EV_WALK_THROUGH], WALK_FRAMES, total[EV_WALK_THROUGH]);
    fprintf(stderr, "[ghost] player %d: %u jumps -- a player drawn %d+ px from where he was (%u frames)\n",
            (int)s_g.player, count[EV_JUMP], JUMP_PX, total[EV_JUMP]);
    if (maxOffFrame >= 0) {
        char t[16];
        fprintf(stderr, "[ghost] player %d: the worst: %s (frame %d), team %d #%d drawn %d px from where he was\n",
                (int)s_g.player, clock_text(maxOffFrame, t, sizeof t), (int)maxOffFrame,
                (int)maxOffWho / 11 + 1, (int)maxOffWho % 11 + 1, (int)maxOff);
    }

    sorted = (GhostEvent *)malloc((s_g.eventCount ? s_g.eventCount : 1) * sizeof *sorted);
    if (sorted && s_g.eventCount) {
        memcpy(sorted, s_g.events, s_g.eventCount * sizeof *sorted);
        qsort(sorted, s_g.eventCount, sizeof *sorted, compareEvents);
        for (k = 0; k < (int32_t)s_g.eventCount && k < MAX_PRINTED; k++)
            printEvent(&sorted[k]);
    }
    /* The windows the match report carries: the longest events. */
    buildClips(lastFrame, fps, sorted, sorted ? (int32_t)s_g.eventCount : 0);
    free(sorted);
    writeTrace(lastFrame);
}
