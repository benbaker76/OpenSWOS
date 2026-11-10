/*
 * mod_netstats.c -- the network quality overlay mod (canonical, bin/mods/netstats; docs/MODS.md
 * §35): SWOS 2020's frame-fairness strip along the top of an online match (mpgame.cpp drawImGui,
 * its ConnectionQualityTracker; F7 in game.cpp cycling g_JitterGraphMode). SDL builds.
 *
 * Centred along the top edge, in the match's own pixels:
 *   [ROLLBACK TIMELINE] [BALANCE BAR] [+N.N] [JITTER: Nms] [why]
 * the timeline the last 240 frames' rollbacks (each column the worst in it: its height and colour,
 * green to red, by how many frames were rolled back), the bar who runs ahead (this game's frames
 * behind the other minus the other's behind this one: right, "+", the other player guesses more),
 * the jitter the spread of the ping over the last 5-10 s, and why it warns: Jitter (that spread
 * over 3.5 frames), Unfair (3 frames apart for a second) or Rollbacks (22 of the last 100 three
 * frames or more), each held a second. F7 (a hotkey: [mod.netstats] hotkey) cycles WHEN POOR (it
 * pops up while a warning is held; SWOS 2020's default), ALWAYS and NEVER, kept in SWOS.ini.
 *
 * Draw-only: it reads the connection (ModApi_NetStats) and draws, never touching the match, and
 * only on frames shown (not GGPO's re-simulations). "changes": "cosmetic".
 */
#ifdef SWOS_SDL_BUILD
#include "mod_api.h"
#include "mod_draw8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SHOW_POOR, SHOW_ALWAYS, SHOW_NEVER };
static const char *const kShow[] = { "when poor", "always", "never" };

enum { TIMELINE = 240, RB_WINDOW = 100, LARGE_RB = 3, LARGE_SHARE = 22, HOLD_MS = 1000 };

static struct {
    uint8_t  timeline[TIMELINE];        /* per frame shown: the largest rollback since the last, 0 none */
    int32_t  head;
    uint8_t  rbRing[RB_WINDOW];         /* the last 100 rollbacks: 1 large */
    int32_t  rbHead, rbCount, rbLarge;
    uint32_t prevBySize[8];
    uint8_t  snapped;
    int32_t  pingMinCur, pingMinPrev, pingMaxCur, pingMaxPrev, rttRange;
    uint32_t pingBucket, pingFirst, lastFrame;
    int32_t  unfairFrames;
    uint32_t holdUntil[3];              /* jitter, unfair, rollbacks */
    uint8_t  warn[3], poor;
} T;

static int32_t s_test, s_testDrawn;     /* SWOS_NETSTATS_TEST (below) */

static int32_t show_mode(void)
{
    const char *v = ModApi_Setting("netstats", "show", kShow[SHOW_POOR]);
    for (int32_t i = 0; i < 3; i++)
        if (strcmp(v, kShow[i]) == 0)
            return i;
    return SHOW_POOR;
}

static void netstats_key(void)          /* F7 */
{
    ModApi_SetSetting("netstats", "show", kShow[(show_mode() + 1) % 3]);
}

static void tracker_reset(void)
{
    memset(&T, 0, sizeof T);
    T.pingMinCur = T.pingMinPrev = 1 << 30;
}

/* updateConnectionQualityTracking, once a frame shown. */
static void tracker_tick(const ModNetStats *s)
{
    const uint32_t now = ModApi_Ticks();
    const int32_t fps = ModApi_FrameRate() > 0 ? ModApi_FrameRate() : 50;
    if (s->pingMs > 0) {
        if (!T.pingBucket) T.pingBucket = T.pingFirst = now ? now : 1;
        if (now - T.pingBucket >= 5000) {
            T.pingMinPrev = T.pingMinCur;
            T.pingMaxPrev = T.pingMaxCur;
            T.pingMinCur = 1 << 30;
            T.pingMaxCur = 0;
            T.pingBucket = now;
        }
        if (s->pingMs < T.pingMinCur) T.pingMinCur = s->pingMs;
        if (s->pingMs > T.pingMaxCur) T.pingMaxCur = s->pingMs;
    }
    const int32_t lo = T.pingMinCur < T.pingMinPrev ? T.pingMinCur : T.pingMinPrev;
    const int32_t hi = T.pingMaxCur > T.pingMaxPrev ? T.pingMaxCur : T.pingMaxPrev;
    T.rttRange = lo < (1 << 30) && hi >= lo ? hi - lo : 0;

    uint8_t worst = 0;
    if (!T.snapped) {
        memcpy(T.prevBySize, s->rollbacksBySize, sizeof T.prevBySize);
        T.snapped = 1;
    } else {
        for (int32_t k = 1; k <= 7; k++) {
            uint32_t d = s->rollbacksBySize[k] - T.prevBySize[k];
            T.prevBySize[k] = s->rollbacksBySize[k];
            if (d == 0 || d > 1000)
                continue;
            worst = (uint8_t)k;
            if (d > RB_WINDOW) d = RB_WINDOW;
            const uint8_t large = k >= LARGE_RB;
            for (uint32_t i = 0; i < d; i++) {
                if (T.rbCount < RB_WINDOW) T.rbCount++;
                else T.rbLarge -= T.rbRing[T.rbHead];
                T.rbRing[T.rbHead] = large;
                T.rbLarge += large;
                T.rbHead = (T.rbHead + 1) % RB_WINDOW;
            }
        }
    }
    T.timeline[T.head] = worst;
    T.head = (T.head + 1) % TIMELINE;

    const int32_t rollbackBad = T.rbCount >= RB_WINDOW && T.rbLarge * 100 >= LARGE_SHARE * T.rbCount;
    const int32_t imbalance = abs(s->localBehindX10 - s->remoteBehindX10);
    T.unfairFrames = imbalance >= 30 ? T.unfairFrames + 1 : 0;
    const int32_t unfairBad = T.unfairFrames > fps;
    const int32_t jitterBad = T.pingFirst && now - T.pingFirst >= 3000 && T.rttRange * 2 * fps > 7 * 1000;
    const int32_t bad[3] = { jitterBad, unfairBad, rollbackBad };
    T.poor = 0;
    for (int32_t i = 0; i < 3; i++) {
        if (bad[i]) T.holdUntil[i] = now + HOLD_MS;
        T.warn[i] = (uint8_t)((int32_t)(T.holdUntil[i] - now) > 0);
        T.poor |= T.warn[i];
    }
}

/* rollbackSizeColor: 0 black, 1 green .. 5+ red */
static uint8_t size_colour(uint8_t size)
{
    if (!size)
        return ModApi_NearestColour(0, 0, 0);
    const int32_t k = size >= 5 ? 4 : size - 1;     /* 0..4 */
    if (k <= 2)
        return ModApi_NearestColour((uint8_t)(60 + 85 * k), 210, (uint8_t)(60 - 10 * k));
    return ModApi_NearestColour(230, (uint8_t)(210 - 75 * (k - 2)), 50);
}

static void text_at(int32_t x, int32_t y, const char *t) { ModApi_DrawText(x, y, 2 /* kWhiteText2 */, t, 1); }

static void netstats_drawn(void)
{
    ModNetStats s;
    D8 d;
    if (!ModApi_NetStats(&s) || s.resimulating)
        return;
    if (s.frame != T.lastFrame || !T.snapped) {
        if (s.frame < T.lastFrame)      /* (a new match on the same link: a rematch) */
            tracker_reset();
        T.lastFrame = s.frame;
        tracker_tick(&s);
    }
    const int32_t mode = show_mode();
    if (mode == SHOW_NEVER || (mode == SHOW_POOR && !T.poor) || !D8_Begin(&d))
        return;
    s_testDrawn++;

    const int32_t stripH = 8, textY = 1, pad = 2, gap = 1;
    const int32_t advW = ModApi_TextWidth("+9.9", 1) + 2 * pad;
    const int32_t jitW = ModApi_TextWidth("JITTER: 999MS", 1) + 2 * pad;
    const int32_t warnW = ModApi_TextWidth("JITTER|UNFAIR|ROLLBACKS", 1) + 2 * pad;
    const int32_t sideW = advW + gap + jitW;
    const int32_t segs = 11, segW = 2, segGap = 1, barW = segs * segW + (segs - 1) * segGap, barH = 5;
    const int32_t cx = d.w / 2, barLeft = cx - barW / 2, barTop = (stripH - barH) / 2;

    /* the bar: who is ahead (local - remote behind: + this game the further behind) */
    const int32_t adv = s.localBehindX10 - s.remoteBehindX10;
    int32_t t1000 = adv * 1000 / 60;                 /* (kMaxRange 6 frames) */
    if (t1000 > 1000) t1000 = 1000;
    if (t1000 < -1000) t1000 = -1000;
    const int32_t sev = abs(t1000);                  /* 0..1000 */
    const uint8_t knob = sev < 500 ? ModApi_NearestColour((uint8_t)(60 + 170 * sev / 500), 210, (uint8_t)(60 - 20 * sev / 500))
                                   : ModApi_NearestColour(230, (uint8_t)(210 - 150 * (sev - 500) / 500), (uint8_t)(40 + 20 * (sev - 500) / 500));
    D8_Darken(&d, barLeft - pad, 0, barW + 2 * pad - 1, stripH - 1);
    const uint8_t segBright = ModApi_NearestColour(200, 200, 200), segDim = ModApi_NearestColour(110, 110, 110);
    for (int32_t i = 0; i < segs; i++) {
        const int32_t centre = i == segs / 2, h = centre ? barH : 3;
        D8_Fill(&d, barLeft + i * (segW + segGap), barTop + (barH - h) / 2, segW, h, centre ? segBright : segDim);
    }
    const int32_t r = 2;
    D8_Disc(&d, barLeft + r + (barW - 2 * r) * (1000 + t1000) / 2000, barTop + barH / 2, r, knob);

    /* left: the rollback timeline, a column a pixel */
    const int32_t rbRight = barLeft - pad - gap, rbLeft = rbRight - sideW;
    D8_Darken(&d, rbLeft, 0, sideW - 1, stripH - 1);
    {
        const int32_t w = sideW - 2 * pad, x0 = rbLeft + pad, h = stripH;
        for (int32_t c = 0; c < w; c++) {
            const int32_t lo = c * TIMELINE / w;
            int32_t hiI = (c + 1) * TIMELINE / w;
            if (hiI <= lo) hiI = lo + 1;
            uint8_t worst = 0;
            for (int32_t i = lo; i < hiI; i++) {
                const uint8_t v = T.timeline[(T.head + i) % TIMELINE];
                if (v > worst) worst = v;
            }
            if (!worst)
                continue;
            const int32_t lin = worst >= 5 ? 5 : worst;
            int32_t bh = h * lin * lin / 25;
            if (bh < 1) bh = 1;
            D8_Fill(&d, x0 + c, h - bh, 1, bh, size_colour(worst));
        }
    }

    /* right: the advantage, the jitter, why */
    char buf[48];
    const int32_t advLeft = barLeft + barW + pad + gap;
    D8_Darken(&d, advLeft, 0, advW - 1, stripH - 1);
    {
        int32_t a = adv > 99 ? 99 : adv < -99 ? -99 : adv;
        snprintf(buf, sizeof buf, "%c%d.%d", a < 0 ? '-' : '+', abs(a) / 10, abs(a) % 10);
        text_at(advLeft + advW - pad - ModApi_TextWidth(buf, 1), textY, buf);
    }
    const int32_t jitLeft = advLeft + advW + gap;
    D8_Darken(&d, jitLeft, 0, jitW - 1, stripH - 1);
    snprintf(buf, sizeof buf, "JITTER: %dMS", (int)T.rttRange);
    text_at(jitLeft + (jitW - ModApi_TextWidth(buf, 1)) / 2, textY, buf);
    if (T.poor) {
        static const char *const kWhy[3] = { "JITTER", "UNFAIR", "ROLLBACKS" };
        buf[0] = '\0';
        for (int32_t i = 0; i < 3; i++)
            if (T.warn[i]) {
                if (buf[0]) strncat(buf, "|", sizeof buf - strlen(buf) - 1);
                strncat(buf, kWhy[i], sizeof buf - strlen(buf) - 1);
            }
        const int32_t warnLeft = jitLeft + jitW + gap;
        D8_Darken(&d, warnLeft, 0, warnW - 1, stripH - 1);
        text_at(warnLeft + (warnW - ModApi_TextWidth(buf, 1)) / 2, textY, buf);
    }
}

static void netstats_start(void) { tracker_reset(); }

/* SWOS_NETSTATS_TEST=1 with a live match (scripts/netplay_lobby_test.sh's, [mod.netstats] show =
 * always): logs how often the strip was drawn at the final whistle. */
static void netstats_ended(const ModMatchResult *r)
{
    (void)r;
    if (s_test)
        fprintf(stdout, "NETSTATS TEST %s: drawn %d times, %d rollbacks seen\n", s_testDrawn > 0 ? "PASS" : "FAIL",
                (int)s_testDrawn, (int)T.rbCount);
}

static void netstats_init(void)
{
    tracker_reset();
    s_test = getenv("SWOS_NETSTATS_TEST") != NULL;
    ModApi_AddHotkey("netstats", "F7", "the network quality strip: when poor / always / never", netstats_key);
}

const ModDef ModDef_netstats = {
    .slug = "netstats",
    .init = netstats_init,
    .matchStart = netstats_start,
    .matchDrawn = netstats_drawn,
    .matchEnded = netstats_ended,
};
#endif
