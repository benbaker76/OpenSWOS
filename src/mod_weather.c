/*
 * mod_weather.c -- the weather mod (canonical, bin/mods/weather; docs/MODS.md §16): rain, drizzle
 * and snow over a match, from its pitch type -- frozen snow, muddy heavy rain, wet rain, soft
 * drizzle -- its intensity drifting over the 90 minutes. The effect is C4ppy77's pitch-weather
 * (third_party/pitch_weather, MIT).
 *
 * On a muddy, wet or soft pitch every player leaves a trail of footprints, each fading out after a
 * while (heavier and longer-lived the wetter the pitch) so the pitch never gets too messed up; and
 * on a rain pitch the drops splash on the grass. Both are on the pitch, in its own pixels: under
 * the players and moving with it. A replay (the one after a goal, or VIEW's) has its own trail,
 * made from the players it shows.
 *
 * Cosmetic: the weather is drawn over the finished frame, its clock and its per-match pattern
 * real time and a random seed, which no match state sees. The footprints and splashes are stamped
 * into the frame being drawn just after the pitch and before the sprites; the frame is not match
 * state (src/sim_state.c), and the prints are added and aged only when a frame is presented,
 * which a re-simulated frame never is. Through include/mod_api.h only. The weather needs an SDL
 * with SDL_RenderGeometry (2.0.18); the rest does not.
 */
#include "mod_hooks.h"

#ifdef SWOS_SDL_BUILD

#include "mod_api.h"

#include <stdlib.h>      /* abs */
#include <string.h>

/* ---- mud ---- */

enum {
    kMaxPrints  = 4096,
    kLevels     = 3,                /* how dark a print is: fresh, drying, nearly gone */
    kStride     = 5,                /* pixels a player moves between two prints */
    kMaxWalkers = 32,
};

typedef struct {
    int16_t  x, y;                  /* world */
    uint16_t age;                   /* presented frames */
    uint8_t  shape;                 /* which pixels of the 2x2 (the foot) */
    uint8_t  used;
} Print;

typedef struct {
    Print   prints[kMaxPrints];
    int32_t next;
} Pool;

/* someone walking: where his last print is, which foot is next */
typedef struct {
    int16_t x, y, lastX, lastY;
    uint8_t foot, have, seen;
} Walker;

static Pool     s_live, s_replay;   /* the match's trail, a replay's */
static Walker   s_liveWalkers[22];
static Walker   s_replayWalkers[kMaxWalkers];
static int32_t  s_replayWalkerCount;
static int16_t  s_collected[kMaxWalkers][2];   /* the players a replay frame showed, their feet */
static int32_t  s_collectedCount;

static uint8_t  s_lut[kLevels][256];        /* palette entry -> its muddied entry, per level */
static uint8_t  s_splash[256];              /* palette entry -> where a raindrop lands on it */
static uint32_t s_lutAge = 0xFFFFFFFFu;

/* How long a print lasts (presented frames) and how dark it starts, per pitch type: 0 none. */
static void mud_for(int pitch, int32_t *life, int32_t *level)
{
    *life = 0;
    *level = 0;
    switch (pitch) {
    case 1: *life = 50 * 14; *level = 0; break;     /* muddy */
    case 2: *life = 50 * 8;  *level = 0; break;     /* wet */
    case 3: *life = 50 * 5;  *level = 1; break;     /* soft */
    default: break;
    }
}

/* The palette entries a pixel becomes under mud, nearest in the palette shown: browner and
 * darker, less so each level; and under a splash, lighter and greyer. Rebuilt every second or so,
 * since a match's palette can fade. */
static void build_luts(void)
{
    static const float kDark[kLevels] = { 0.50f, 0.62f, 0.76f };
    int l, i;
    for (l = 0; l < kLevels; l++)
        for (i = 0; i < 256; i++) {
            uint8_t r, g, b;
            ModApi_PaletteColour((uint8_t)i, &r, &g, &b);
            const float k = kDark[l], m = 1.0f - k;
            /* toward a dark earth brown */
            s_lut[l][i] = ModApi_NearestColour((uint8_t)(r * k + 0x5a * m * 0.9f), (uint8_t)(g * k + 0x40 * m * 0.8f),
                                               (uint8_t)(b * k + 0x22 * m * 0.6f));
        }
    for (i = 0; i < 256; i++) {
        uint8_t r, g, b;
        ModApi_PaletteColour((uint8_t)i, &r, &g, &b);
        s_splash[i] = ModApi_NearestColour((uint8_t)(r * 0.35f + 0xd8 * 0.65f), (uint8_t)(g * 0.35f + 0xe0 * 0.65f),
                                           (uint8_t)(b * 0.35f + 0xe8 * 0.65f));
    }
}

static void keep_luts(void)
{
    if (++s_lutAge >= 50) {
        build_luts();
        s_lutAge = 0;
    }
}

/* the next print's shape: its own generator, so no random number the game draws is taken */
static uint8_t shape_next(void)
{
    static uint32_t state = 0x9e3779b9u;
    state = state * 1664525u + 1013904223u;
    return (uint8_t)(1 + ((state >> 24) & 7));
}

static void add_print(Pool *pool, int16_t x, int16_t y)
{
    Print *p = &pool->prints[pool->next];
    pool->next = (pool->next + 1) % kMaxPrints;
    p->x = x;
    p->y = y;
    p->age = 0;
    p->shape = shape_next();
    p->used = 1;
}

static void age_pool(Pool *pool, int32_t life)
{
    for (int32_t i = 0; i < kMaxPrints; i++)
        if (pool->prints[i].used && ++pool->prints[i].age >= life)
            pool->prints[i].used = 0;
}

/* He is at x, y now: a print when he has gone a stride from his last one. */
static void walk(Pool *pool, Walker *w, int16_t x, int16_t y)
{
    w->x = x;
    w->y = y;
    if (!w->have) {
        w->lastX = x;
        w->lastY = y;
        w->have = 1;
        return;
    }
    const int32_t dx = x - w->lastX, dy = y - w->lastY;
    if (dx * dx + dy * dy < kStride * kStride)
        return;
    if (dx * dx + dy * dy <= 40 * 40) {             /* (more is a jump, a restart: no trail across it) */
        /* left foot and right foot either side of the way he is going */
        const int side = w->foot ? 1 : -1;
        const int ox = abs(dy) > abs(dx) ? side : 0, oy = abs(dy) > abs(dx) ? 0 : side;
        w->foot ^= 1;
        add_print(pool, (int16_t)(x + ox), (int16_t)(y + oy));
    }
    w->lastX = x;
    w->lastY = y;
}

/* A presented frame of a match (not a replay): new prints where the players have walked. */
static void mud_update_live(void)
{
    static int wasInMatch;
    const int inMatch = ModApi_InMatch() != 0;
    int32_t life, level;
    if (inMatch && !wasInMatch) {                   /* a new match: a clean pitch */
        memset(&s_live, 0, sizeof s_live);
        memset(s_liveWalkers, 0, sizeof s_liveWalkers);
        s_lutAge = 0xFFFFFFFFu;
    }
    wasInMatch = inMatch;
    mud_for(ModApi_PitchType(), &life, &level);
    if (!inMatch || !life || ModApi_InReplay())     /* (a replay: the match's trail waits for it) */
        return;
    keep_luts();
    age_pool(&s_live, life);
    ModPlayer players[22];
    const int32_t n = ModApi_Players(players, 22);
    for (int32_t i = 0; i < n; i++) {
        Walker *w = &s_liveWalkers[players[i].team * 11 + players[i].index];
        if (!players[i].onPitch || players[i].z > 2) {  /* (off the pitch, or in the air) */
            w->have = 0;
            continue;
        }
        walk(&s_live, w, players[i].x, players[i].y);
    }
}

/* A replay frame: its players (no names, only where they are) followed by the nearest of where
 * they were. */
static void mud_update_replay(void)
{
    int32_t life, level;
    mud_for(ModApi_PitchType(), &life, &level);
    if (!life)
        return;
    keep_luts();
    age_pool(&s_replay, life);
    for (int32_t i = 0; i < s_replayWalkerCount; i++)
        s_replayWalkers[i].seen = 0;
    for (int32_t c = 0; c < s_collectedCount; c++) {
        const int16_t x = s_collected[c][0], y = s_collected[c][1];
        int32_t best = -1, bestD = 12 * 12 + 1;
        for (int32_t i = 0; i < s_replayWalkerCount; i++) {
            const int32_t dx = x - s_replayWalkers[i].x, dy = y - s_replayWalkers[i].y, d = dx * dx + dy * dy;
            if (!s_replayWalkers[i].seen && d < bestD) {
                bestD = d;
                best = i;
            }
        }
        if (best < 0) {
            if (s_replayWalkerCount == kMaxWalkers)
                continue;
            best = s_replayWalkerCount++;
            memset(&s_replayWalkers[best], 0, sizeof s_replayWalkers[best]);
        }
        s_replayWalkers[best].seen = 1;
        walk(&s_replay, &s_replayWalkers[best], x, y);
    }
    /* who was not seen has gone */
    for (int32_t i = 0; i < s_replayWalkerCount;)
        if (!s_replayWalkers[i].seen)
            s_replayWalkers[i] = s_replayWalkers[--s_replayWalkerCount];
        else
            i++;
    s_collectedCount = 0;
}

static void stamp_prints(const Pool *pool, uint8_t *fb, int32_t stride, int32_t w, int32_t h)
{
    int32_t life, level;
    int16_t cx, cy;
    mud_for(ModApi_PitchType(), &life, &level);
    if (!life)
        return;
    ModApi_Camera(&cx, &cy);
    static const int8_t kDx[4] = { 0, 1, 0, 1 }, kDy[4] = { 0, 0, 1, 1 };
    for (int32_t i = 0; i < kMaxPrints; i++) {
        const Print *p = &pool->prints[i];
        if (!p->used)
            continue;
        const int sx = p->x - cx, sy = p->y - cy;
        if (sx < 0 || sy < 0 || sx + 1 >= w || sy + 1 >= h)
            continue;
        /* drier and fainter with age, and in its last quarter its pixels go one by one */
        int lv = level + p->age * (kLevels - level) / life;
        if (lv >= kLevels)
            lv = kLevels - 1;
        const int fading = p->age > life * 3 / 4;
        const int keep = fading ? 4 - (p->age - life * 3 / 4) * 4 / (life - life * 3 / 4 + 1) : 4;
        for (int k = 0, n = 0; k < 4; k++) {
            /* shape: the pixels of the 2x2 left out -- a foot is never a solid square */
            if (k == (p->shape & 3) && (p->shape & 4))
                continue;
            if (++n > keep)
                break;
            uint8_t *px = fb + (sy + kDy[k]) * stride + sx + kDx[k];
            *px = s_lut[lv][*px];
        }
    }
}

/* ---- splashes: the drops hitting the pitch ---- */

static float s_rainIntensity;               /* the weather's, as last drawn (0: no rain) */
static int   s_rainKind;                    /* 1 heavy, 2 rain, 3 drizzle (the pitch type's) */

static uint32_t mix32(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u + (a << 6) + (a >> 2));
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    return h;
}

/* The pitch is cut into cells of 32 pixels, each with a few splash slots; a slot lands a drop
 * every so often somewhere new in its cell -- a dot, a crown, a ring going -- so the splashes are
 * where they hit the pitch, and stay there as the camera moves. Real time (ModApi_Ticks): what is
 * drawn depends on nothing the match keeps. */
static void draw_splashes(uint8_t *fb, int32_t stride, int32_t w, int32_t h)
{
    static const int kPerScreen[4] = { 0, 150, 90, 35 };    /* by pitch type 1..3, at full intensity */
    static const int8_t kStage[3][5][2] = {
        { { 0, 0 }, { 99, 99 } },                                   /* the drop lands */
        { { -1, -1 }, { 1, -1 }, { 0, 0 }, { 99, 99 } },            /* its crown */
        { { -2, -1 }, { 2, -1 }, { -1, 0 }, { 1, 0 }, { 99, 99 } }, /* the ring, going */
    };
    enum { kCell = 32, kSlotsPerCell = 4, kPeriod = 360 };          /* ms from a slot's drop to its next */
    if (s_rainKind < 1 || s_rainKind > 3 || s_rainIntensity <= 0)
        return;
    /* of a cell's slots, the share that rains: the screen's drops over its cells' slots */
    const float cells = (float)(w * h) / (kCell * kCell);
    const uint32_t share = (uint32_t)(1000.0f * kPerScreen[s_rainKind] * s_rainIntensity / (cells * kSlotsPerCell));
    int16_t camX, camY;
    ModApi_Camera(&camX, &camY);
    const uint32_t ms = ModApi_Ticks();
    const int cx0 = (camX >= 0 ? camX : camX - kCell + 1) / kCell, cy0 = (camY >= 0 ? camY : camY - kCell + 1) / kCell;
    for (int cy = cy0; cy * kCell < camY + h; cy++)
        for (int cx = cx0; cx * kCell < camX + w; cx++) {
            const uint32_t cell = (uint32_t)(cx * 7919 + cy * 104729);
            for (uint32_t s = 0; s < kSlotsPerCell; s++) {
                const uint32_t slot = mix32(cell, s);
                if (slot % 1000 >= share)
                    continue;
                const uint32_t t = ms + (slot >> 10) % kPeriod, cycle = t / kPeriod, age = t % kPeriod;
                const int stage = (int)(age / 60);  /* 60 ms a stage, then nothing till the next */
                if (stage > 2)
                    continue;
                const uint32_t where = mix32(slot, cycle);
                const int x = cx * kCell + (int)(where % kCell) - camX, y = cy * kCell + (int)((where >> 8) % kCell) - camY;
                for (int k = 0; kStage[stage][k][0] != 99; k++) {
                    const int px = x + kStage[stage][k][0], py = y + kStage[stage][k][1];
                    if (px >= 0 && py >= 0 && px < w && py < h) {
                        uint8_t *p = fb + py * stride + px;
                        *p = s_splash[*p];
                    }
                }
            }
        }
}

/* ---- the hooks ---- */

static void stamp(const Pool *pool)
{
    int32_t stride, w, h;
    uint8_t *fb = ModApi_Frame(&stride);
    if (!fb || s_lutAge == 0xFFFFFFFFu)
        return;
    ModApi_ScreenSize(&w, &h);
    /* (its settings: the mod's page, SWOS.ini [mod.weather]) */
    if (SDL_strcasecmp(ModApi_Setting("weather", "splashes", "on"), "off") != 0)
        draw_splashes(fb, stride, w, h);
    if (SDL_strcasecmp(ModApi_Setting("weather", "footprints", "on"), "off") != 0)
        stamp_prints(pool, fb, stride, w, h);
}

/* A match's pitch is in the frame being drawn and the sprites are not yet. */
void ModWeather_PitchDrawn(void)
{
    stamp(&s_live);
}

/* A replay's: `first` its first frame (or one it jumped to), `fresh` one not shown before. */
void ModWeather_ReplayFrameBegin(int32_t first, int32_t fresh)
{
    if (first) {
        memset(&s_replay, 0, sizeof s_replay);
        s_replayWalkerCount = 0;
        s_collectedCount = 0;
        if (s_lutAge == 0xFFFFFFFFu) {              /* (a replay from VIEW: no match before it) */
            build_luts();
            s_lutAge = 0;
        }
    } else if (fresh) {
        mud_update_replay();
    }
    s_collectedCount = fresh ? s_collectedCount : 0;
    stamp(&s_replay);
}

void ModWeather_ReplaySprite(uint16_t image, int16_t left, int16_t top)
{
    int16_t x, y;
    if (s_collectedCount < kMaxWalkers && ModApi_ReplayPlayerFeet(image, left, top, &x, &y)) {
        s_collected[s_collectedCount][0] = x;
        s_collected[s_collectedCount][1] = y;
        s_collectedCount++;
    }
}

/* ---- the weather ---- */

#if SDL_VERSION_ATLEAST(2, 0, 18)
#include "../third_party/pitch_weather/pitch_weather.h"

static void weather_draw(SDL_Renderer *renderer)
{
    static int wasShowing, seed;
    const int showing = ModApi_InMatch() || ModApi_InReplay();
    if (showing && !wasShowing)                 /* a new match (or replay): its own pattern */
        seed = (int)(SDL_GetPerformanceCounter() % 360);
    wasShowing = showing;
    if (!showing) {
        s_rainIntensity = 0;
        return;
    }
    const PitchWeather weather = PitchWeather_ForPitchType(ModApi_PitchType(), (float)ModApi_MatchMinute(), seed);
    s_rainKind = weather.kind == PITCH_WEATHER_SNOW ? 0 : ModApi_PitchType();
    s_rainIntensity = weather.intensity;
    SDL_Rect area = { 0, 0, 0, 0 };
    SDL_RenderGetLogicalSize(renderer, &area.w, &area.h);
    if (area.w <= 0 || area.h <= 0)
        SDL_GetRendererOutputSize(renderer, &area.w, &area.h);
    PitchWeather_Draw(renderer, &area, &weather, ModApi_Ticks() / 1000.0);
}
#else
static void weather_draw(SDL_Renderer *renderer) { (void)renderer; }    /* (an SDL without RenderGeometry) */
#endif

void ModWeather_FrameDrawn(SDL_Renderer *renderer)
{
    mud_update_live();
    weather_draw(renderer);
}

const ModDef ModDef_weather = {
    .slug = "weather",
    .frameDrawn = ModWeather_FrameDrawn,
    .pitchDrawn = ModWeather_PitchDrawn,
    .replayFrameBegin = ModWeather_ReplayFrameBegin,
    .replaySprite = ModWeather_ReplaySprite,
};

#endif /* SWOS_SDL_BUILD */
