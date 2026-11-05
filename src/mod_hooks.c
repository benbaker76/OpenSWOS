/*
 * mod_hooks.c -- the built-in mods' hooks (include/mod_hooks.h, docs/MODS.md §16).
 *
 * Each built-in mod is a row: its slug (its canonical folder bin/mods/<slug>) and its hooks. A
 * hook runs only while its mod is on. New hooks are added here as mods need them, each a fixed
 * point of the engine.
 */
#include "mod_hooks.h"

#ifdef SWOS_SDL_BUILD

#include "mods.h"

#include <stddef.h>

extern void ModWeather_FrameDrawn(SDL_Renderer *renderer);    /* src/mod_weather.c */
extern void ModWeather_PitchDrawn(void);

typedef struct {
    const char *slug;
    void (*frameDrawn)(SDL_Renderer *renderer);
    void (*pitchDrawn)(void);
} BuiltinMod;

static const BuiltinMod k_builtin[] = {
    { "weather", ModWeather_FrameDrawn, ModWeather_PitchDrawn },
};

void ModHooks_PitchDrawn(void)
{
    size_t i;
    for (i = 0; i < sizeof k_builtin / sizeof k_builtin[0]; i++)
        if (k_builtin[i].pitchDrawn && Mods_IsOn(k_builtin[i].slug))
            k_builtin[i].pitchDrawn();
}

void ModHooks_FrameDrawn(SDL_Renderer *renderer)
{
    size_t i;
    for (i = 0; i < sizeof k_builtin / sizeof k_builtin[0]; i++)
        if (k_builtin[i].frameDrawn && Mods_IsOn(k_builtin[i].slug))
            k_builtin[i].frameDrawn(renderer);
}

#endif /* SWOS_SDL_BUILD */
