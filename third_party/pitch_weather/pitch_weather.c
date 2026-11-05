/* pitch-weather: rain, drizzle and snow drawn over a 2D football game with SDL2.
 * Copyright (c) 2026 C4ppy77. MIT licence, see LICENSE.
 *
 * Every drop is a thin slanted quad fading towards its tail, every snowflake a tiny diamond, and
 * they all go to the GPU in one SDL_RenderGeometry call. A drop's position comes from a fixed
 * pseudo-random start plus the clock, so nothing is stored or updated per drop.
 *
 * OpenSWOS's C port of src/pitchWeather.cpp (see OPENSWOS.md): the same constants and arithmetic;
 * std::vector becomes fixed arrays sized for the most drops any weather draws. */
#include "pitch_weather.h"

#include <math.h>

/* how rain looks; lengths and widths in points on a 390 point tall screen, scaled to the real one */
typedef struct {
    float drops;            /* on screen at once at full intensity */
    float length;
    float width;
    float alpha;
    float speed;            /* screen heights a second */
} RainStyle;

#define RAIN_SLANT 0.22f
#define REFERENCE_SCREEN_HEIGHT 390.0f
#define MAX_QUADS 210       /* the heavy rain's drops at full intensity (snow: 120) */

PitchWeather PitchWeather_ForPitchType(int pitchType, float minute, int seed)
{
    PitchWeather weather = { PITCH_WEATHER_NONE, 0.0f };
    float low, high;
    switch (pitchType) {
    case 0: weather.kind = PITCH_WEATHER_SNOW; low = 0.55f; high = 1; break;
    case 1: weather.kind = PITCH_WEATHER_HEAVY_RAIN; low = 0.7f; high = 1; break;
    case 2: weather.kind = PITCH_WEATHER_RAIN; low = 0.5f; high = 0.9f; break;
    case 3: weather.kind = PITCH_WEATHER_DRIZZLE; low = 0.4f; high = 0.8f; break;
    default: return weather;
    }

    const double kPi = 3.14159265358979;
    double t = (minute > 0.0f ? minute : 0.0f) / 90.0 * 2 * kPi;
    double phase = seed * kPi / 180;
    double wave = (sin(1.3 * t + phase) + 0.5 * sin(3.1 * t + 2 * phase)) / 1.5;
    double intensity = low + (high - low) * (wave + 1) / 2;
    weather.intensity = (float)(round(intensity * 10) / 10);
    return weather;
}

static RainStyle rain_style(PitchWeatherKind kind)
{
    RainStyle drizzle = { 70, 9, 0.8f, 0.22f, 1.1f }, rain = { 130, 14, 1, 0.28f, 1.5f },
              heavy = { 210, 20, 1.2f, 0.32f, 1.9f };
    return kind == PITCH_WEATHER_DRIZZLE ? drizzle : kind == PITCH_WEATHER_RAIN ? rain : heavy;
}

/* a fixed pseudo-random 0..1 for drop i; salt picks a different number for each property */
static float random01(int i, int salt)
{
    double value = sin(i * 12.9898 + salt * 78.233) * 43758.5453;
    return (float)(value - floor(value));
}

static SDL_Color color(Uint8 r, Uint8 g, Uint8 b, float alpha)
{
    const float a = alpha < 0.0f ? 0.0f : alpha > 1.0f ? 1.0f : alpha;
    SDL_Color c = { r, g, b, (Uint8)(a * 255) };
    return c;
}

static SDL_Vertex vertex(float x, float y, SDL_Color c)
{
    SDL_Vertex v;
    v.position.x = x;
    v.position.y = y;
    v.color = c;
    v.tex_coord.x = v.tex_coord.y = 0.0f;
    return v;
}

static void draw_tint(SDL_Renderer *renderer, const SDL_Rect *area, const PitchWeather *weather)
{
    const int snow = weather->kind == PITCH_WEATHER_SNOW;
    const float strength = weather->intensity * (weather->kind == PITCH_WEATHER_HEAVY_RAIN ? 1 : 0.7f);
    const SDL_Color top = snow ? color(255, 255, 255, 0.16f * strength) : color(0x1a, 0x22, 0x33, 0.28f * strength);
    const SDL_Color bottom = snow ? color(255, 255, 255, 0.04f * strength) : color(0x1a, 0x22, 0x33, 0.08f * strength);

    const float left = (float)area->x, right = (float)(area->x + area->w);
    const float upper = (float)area->y, lower = (float)(area->y + area->h);
    SDL_Vertex vertices[4];
    vertices[0] = vertex(left, upper, top);
    vertices[1] = vertex(right, upper, top);
    vertices[2] = vertex(right, lower, bottom);
    vertices[3] = vertex(left, lower, bottom);
    static const int kIndices[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(renderer, NULL, vertices, 4, kIndices, 6);
}

static SDL_Vertex s_vertices[MAX_QUADS * 4];
static int s_indices[MAX_QUADS * 6];
static int s_quads;

static void add_quad(float ox, float oy, const SDL_FPoint corners[4], SDL_Color head, SDL_Color tail)
{
    static const int kQuad[6] = { 0, 1, 2, 0, 2, 3 };
    int i;
    if (s_quads >= MAX_QUADS)
        return;
    for (i = 0; i < 4; i++)
        s_vertices[s_quads * 4 + i] = vertex(ox + corners[i].x, oy + corners[i].y, i < 2 ? head : tail);
    for (i = 0; i < 6; i++)
        s_indices[s_quads * 6 + i] = s_quads * 4 + kQuad[i];
    s_quads++;
}

void PitchWeather_Draw(SDL_Renderer *renderer, const SDL_Rect *area, const PitchWeather *weather, double seconds)
{
    if (weather->kind == PITCH_WEATHER_NONE || weather->intensity <= 0 || area->w <= 0 || area->h <= 0)
        return;

    const float width = (float)area->w, height = (float)area->h;
    const float originX = (float)area->x, originY = (float)area->y;
    const float unit = height / REFERENCE_SCREEN_HEIGHT;
    int i;

    SDL_BlendMode oldBlendMode;
    SDL_GetRenderDrawBlendMode(renderer, &oldBlendMode);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    draw_tint(renderer, area, weather);

    s_quads = 0;
    if (weather->kind == PITCH_WEATHER_SNOW) {
        const int count = (int)(120 * weather->intensity);
        for (i = 0; i < count; i++) {
            const float radius = (1.2f + 1.8f * random01(i, 5)) * unit;
            /* bigger flakes are nearer, so they fall faster */
            const double speed = height * (0.07 + 0.08 * random01(i, 5));
            const float y = (float)fmod(random01(i, 1) * height + seconds * speed, height + 6 * unit) - 3 * unit;
            const double sway = sin(seconds * (0.8 + random01(i, 6)) + random01(i, 7) * 6.28) * 10 * unit;
            const float x = (float)fmod(random01(i, 2) * width + seconds * 6 * unit + sway + width, width);
            const SDL_Color flake = color(255, 255, 255, 0.55f + 0.35f * random01(i, 4));
            /* a small diamond reads as a round flake at this size */
            const SDL_FPoint corners[4] = { { x, y - radius }, { x + radius, y }, { x, y + radius }, { x - radius, y } };
            add_quad(originX, originY, corners, flake, flake);
        }
    } else {
        const RainStyle style = rain_style(weather->kind);
        const int count = (int)(style.drops * weather->intensity);
        const float length = style.length * unit, halfWidth = style.width * unit / 2;
        const double fall = style.speed * height;
        /* perpendicular to a drop's slant, for its width */
        const float normalX = 1 / sqrtf(1 + RAIN_SLANT * RAIN_SLANT), normalY = -RAIN_SLANT * normalX;
        for (i = 0; i < count; i++) {
            const double speed = fall * (0.8 + 0.4 * random01(i, 3));
            const float y = (float)fmod(random01(i, 1) * height + seconds * speed, height + length);
            const float x = (float)fmod(random01(i, 2) * width + seconds * speed * RAIN_SLANT, width);
            const float tailX = x - length * RAIN_SLANT, tailY = y - length;
            const float alpha = style.alpha * (0.6f + 0.4f * random01(i, 4));
            const SDL_FPoint corners[4] = {
                { x - normalX * halfWidth, y - normalY * halfWidth }, { x + normalX * halfWidth, y + normalY * halfWidth },
                { tailX + normalX * halfWidth, tailY + normalY * halfWidth }, { tailX - normalX * halfWidth, tailY - normalY * halfWidth },
            };
            /* fading towards the tail */
            add_quad(originX, originY, corners, color(255, 255, 255, alpha), color(255, 255, 255, 0));
        }
    }

    if (s_quads)
        SDL_RenderGeometry(renderer, NULL, s_vertices, s_quads * 4, s_indices, s_quads * 6);
    SDL_SetRenderDrawBlendMode(renderer, oldBlendMode);
}
