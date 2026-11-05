/* pitch-weather: rain, drizzle and snow drawn over a 2D football game with SDL2.
 * Copyright (c) 2026 C4ppy77. MIT licence, see LICENSE.
 *
 * OpenSWOS's C port of github.com/C4ppy77/pitch-weather-SWOS (src/pitchWeather.h/.cpp at
 * 9e67344, 2026-10-19): the same weather, the same numbers, in C so it builds wherever the game's
 * SDL build does. See OPENSWOS.md. */
#ifndef PITCH_WEATHER_H
#define PITCH_WEATHER_H

#include <SDL.h>

typedef enum { PITCH_WEATHER_NONE, PITCH_WEATHER_DRIZZLE, PITCH_WEATHER_RAIN, PITCH_WEATHER_HEAVY_RAIN,
               PITCH_WEATHER_SNOW } PitchWeatherKind;

typedef struct {
    PitchWeatherKind kind;
    float intensity;        /* 0..1, how much is falling right now */
} PitchWeather;

/* The weather for a SWOS-style pitch type: 0 frozen (snow), 1 muddy (heavy rain), 2 wet (rain),
 * 3 soft (drizzle), anything else none. Its intensity drifts over the match in two slow waves, in
 * steps of 0.1; seed (0..359) gives each match its own pattern, minute is the match minute (0..90). */
PitchWeather PitchWeather_ForPitchType(int pitchType, float minute, int seed);

/* Draws the weather over area (usually the whole game screen), with a slight tint for the gloom or
 * the cold. Call it once a frame after drawing the game and before presenting; seconds is any
 * steadily rising clock. Needs SDL 2.0.18 or later (SDL_RenderGeometry). */
void PitchWeather_Draw(SDL_Renderer *renderer, const SDL_Rect *area, const PitchWeather *weather, double seconds);

#endif
