/*
 * mod_weather.c -- the weather mod (canonical, bin/mods/weather; docs/MODS.md §16): rain, drizzle
 * and snow over a match, from its pitch type -- frozen snow, muddy heavy rain, wet rain, soft
 * drizzle -- its intensity drifting over the 90 minutes. The effect is C4ppy77's pitch-weather
 * (third_party/pitch_weather, MIT).
 *
 * And mud: on a muddy, wet or soft pitch every player leaves a trail of footprints, each fading
 * out after a while (heavier and longer-lived the wetter the pitch) so the pitch never gets too
 * messed up.
 *
 * Cosmetic: the weather is drawn over the finished frame, its clock and its per-match pattern
 * real time and a random seed, which no match state sees. The footprints are stamped into the
 * frame being drawn just after the pitch and before the sprites, so the players walk over them;
 * the frame is not match state (src/sim_state.c), and the prints are added and aged only when a
 * frame is presented, which a re-simulated frame never is. The weather needs an SDL with
 * SDL_RenderGeometry (2.0.18); the mud does not.
 */
#include "mod_hooks.h"

#ifdef SWOS_SDL_BUILD

#include "display.h"
#include "game_types.h"
#include "platform_sdl.h"

#include <stdlib.h>      /* abs */
#include <string.h>

extern uint16_t _pitchNumberAndType;        /* low byte: the match's pitch type, 0 frozen .. 6 hard */
extern int32_t  GameLoop_Active(void);
extern Sprite **_team1SpritesTable;
extern Sprite **_team2SpritesTable;
extern volatile uintptr_t _frontbufferBasePtr;

/* (SWOS_WEATHER_PITCH=<0..6>: that pitch's weather whatever the match's -- a test's) */
static int pitch_type(void)
{
    const char *force = SDL_getenv("SWOS_WEATHER_PITCH");
    return force && *force ? SDL_atoi(force) : (_pitchNumberAndType & 0xFF);
}

/* ---- mud ---- */

enum {
    kMaxPrints = 4096,
    kLevels    = 3,                 /* how dark a print is: fresh, drying, nearly gone */
    kStride    = 5,                 /* pixels a player moves between two prints */
};

typedef struct {
    int16_t  x, y;                  /* the pitch's pixels */
    uint16_t age;                   /* presented frames */
    uint8_t  shape;                 /* which pixels of the 2x2 (the foot) */
    uint8_t  used;
} Print;

static Print    s_prints[kMaxPrints];
static int32_t  s_next;
static int16_t  s_lastX[22], s_lastY[22];
static uint8_t  s_foot[22], s_have[22];
static uint8_t  s_lut[kLevels][256];        /* palette entry -> its muddied entry, per level */
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
 * darker, less so each level. Rebuilt every second or so, since a match's palette can fade. */
static void build_luts(void)
{
    static const float kDark[kLevels] = { 0.50f, 0.62f, 0.76f };
    int l, i;
    for (l = 0; l < kLevels; l++)
        for (i = 0; i < 256; i++) {
            uint8_t r, g, b;
            Platform_PaletteColour((uint8_t)i, &r, &g, &b);
            const float k = kDark[l];
            /* toward a dark earth brown */
            const float tr = 0x5a, tg = 0x40, tb = 0x22;
            const float m = 1.0f - k;
            s_lut[l][i] = Platform_NearestColour((uint8_t)(r * k + tr * m * 0.9f), (uint8_t)(g * k + tg * m * 0.8f),
                                                 (uint8_t)(b * k + tb * m * 0.6f));
        }
}

/* the next print's shape: its own generator, so no random number the game draws is taken */
static uint8_t shape_next(void)
{
    static uint32_t state = 0x9e3779b9u;
    state = state * 1664525u + 1013904223u;
    return (uint8_t)(1 + ((state >> 24) & 7));
}

static void add_print(int16_t x, int16_t y, uint8_t shape)
{
    Print *p = &s_prints[s_next];
    s_next = (s_next + 1) % kMaxPrints;
    p->x = x;
    p->y = y;
    p->age = 0;
    p->shape = shape;
    p->used = 1;
}

/* A presented frame of a match: new prints where the players have walked, the old ones aged. */
static void mud_update(void)
{
    static int wasInMatch;
    const int inMatch = GameLoop_Active() != 0;
    int32_t life, level, i;
    if (inMatch && !wasInMatch) {               /* a new match: a clean pitch */
        memset(s_prints, 0, sizeof s_prints);
        memset(s_have, 0, sizeof s_have);
        s_lutAge = 0xFFFFFFFFu;
    }
    wasInMatch = inMatch;
    mud_for(pitch_type(), &life, &level);
    if (!inMatch || !life || !_team1SpritesTable || !_team2SpritesTable)
        return;
    if (++s_lutAge >= 50) {
        build_luts();
        s_lutAge = 0;
    }
    for (i = 0; i < kMaxPrints; i++)
        if (s_prints[i].used && ++s_prints[i].age >= life)
            s_prints[i].used = 0;

    for (i = 0; i < 22; i++) {
        const Sprite *s = i < 11 ? _team1SpritesTable[i] : _team2SpritesTable[i - 11];
        if (!s || !s->visible || s->z.s.i > 2) {     /* (off the pitch, or in the air) */
            s_have[i] = 0;
            continue;
        }
        const int16_t x = s->x.s.i, y = s->y.s.i;
        if (!s_have[i]) {
            s_lastX[i] = x;
            s_lastY[i] = y;
            s_have[i] = 1;
            continue;
        }
        const int32_t dx = x - s_lastX[i], dy = y - s_lastY[i];
        if (dx * dx + dy * dy < kStride * kStride)
            continue;
        if (dx * dx + dy * dy > 40 * 40) {           /* a jump (a restart): no trail across it */
            s_lastX[i] = x;
            s_lastY[i] = y;
            continue;
        }
        /* left foot and right foot either side of the way he is going */
        const int side = s_foot[i] ? 1 : -1;
        const int ox = abs(dy) > abs(dx) ? side : 0, oy = abs(dy) > abs(dx) ? 0 : side;
        s_foot[i] ^= 1;
        add_print((int16_t)(x + ox), (int16_t)(y + oy), shape_next());
        s_lastX[i] = x;
        s_lastY[i] = y;
    }
}

/* The pitch is in the frame being drawn and the sprites are not yet: stamp the prints. */
void ModWeather_PitchDrawn(void)
{
    int32_t life, level, i;
    uint8_t *fb = (uint8_t *)(uintptr_t)_frontbufferBasePtr;
    mud_for(pitch_type(), &life, &level);
    if (!life || !fb || s_lutAge == 0xFFFFFFFFu)
        return;
    const int w = FRONTBUFFER_ROW_STRIDE_BYTES, h = _screen_height;
    const int cx = Render_CameraX(), cy = Render_CameraY();
    for (i = 0; i < kMaxPrints; i++) {
        const Print *p = &s_prints[i];
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
        int k, n = 0;
        static const int8_t kDx[4] = { 0, 1, 0, 1 }, kDy[4] = { 0, 0, 1, 1 };
        for (k = 0; k < 4; k++) {
            /* shape: the pixels of the 2x2 left out -- a foot is never a solid square */
            if (k == (p->shape & 3) && (p->shape & 4))
                continue;
            if (++n > keep)
                break;
            uint8_t *px = fb + (sy + kDy[k]) * w + sx + kDx[k];
            *px = s_lut[lv][*px];
        }
    }
}

/* ---- the weather ---- */

#if SDL_VERSION_ATLEAST(2, 0, 18)
#include "../third_party/pitch_weather/pitch_weather.h"

extern uint32_t _gameTime;                  /* the clock's digits: [1] hundreds, [2] tens, [3] ones */

static void weather_draw(SDL_Renderer *renderer)
{
    static int wasInMatch, seed;
    const int inMatch = GameLoop_Active() != 0;
    if (inMatch && !wasInMatch)                 /* a new match: its own pattern */
        seed = (int)(SDL_GetPerformanceCounter() % 360);
    wasInMatch = inMatch;
    if (!inMatch)
        return;
    const uint8_t *t = (const uint8_t *)&_gameTime;
    const float minute = (float)(t[1] * 100 + t[2] * 10 + t[3]);
    const PitchWeather weather = PitchWeather_ForPitchType(pitch_type(), minute, seed);
    SDL_Rect area = { 0, 0, 0, 0 };
    SDL_RenderGetLogicalSize(renderer, &area.w, &area.h);
    if (area.w <= 0 || area.h <= 0)
        SDL_GetRendererOutputSize(renderer, &area.w, &area.h);
    PitchWeather_Draw(renderer, &area, &weather, SDL_GetTicks() / 1000.0);
}
#else
static void weather_draw(SDL_Renderer *renderer) { (void)renderer; }    /* (an SDL without RenderGeometry) */
#endif

void ModWeather_FrameDrawn(SDL_Renderer *renderer)
{
    mud_update();
    weather_draw(renderer);
}

#endif /* SWOS_SDL_BUILD */
