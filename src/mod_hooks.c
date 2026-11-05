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

#include "game_types.h"

extern void ModWeather_FrameDrawn(SDL_Renderer *renderer);    /* src/mod_weather.c */
extern void ModWeather_PitchDrawn(void);
extern void ModWeather_ReplayFrameBegin(int32_t first, int32_t fresh);
extern void ModWeather_ReplaySprite(uint16_t image, int16_t left, int16_t top);
extern void ModHair_PlayerSetsRaw(void);                        /* src/mod_hair.c */
extern void ModHair_PlayerDrawn(const Sprite *sprite, const SpriteGraphics *gfx, int16_t x, int16_t y);

typedef struct {
    const char *slug;
    void (*frameDrawn)(SDL_Renderer *renderer);
    void (*pitchDrawn)(void);
    void (*playerSetsRaw)(void);
    void (*playerDrawn)(const Sprite *sprite, const SpriteGraphics *gfx, int16_t x, int16_t y);
    void (*replayFrameBegin)(int32_t first, int32_t fresh);
    void (*replaySprite)(uint16_t image, int16_t left, int16_t top);
} BuiltinMod;

static const BuiltinMod k_builtin[] = {
    { "weather", ModWeather_FrameDrawn, ModWeather_PitchDrawn, NULL, NULL, ModWeather_ReplayFrameBegin,
      ModWeather_ReplaySprite },
    { "hair", NULL, NULL, ModHair_PlayerSetsRaw, ModHair_PlayerDrawn, NULL, NULL },
};

void ModHooks_ReplayFrameBegin(int32_t first, int32_t fresh)
{
    size_t i;
    ModApi_NoteReplayFrame();
    for (i = 0; i < sizeof k_builtin / sizeof k_builtin[0]; i++)
        if (k_builtin[i].replayFrameBegin && Mods_IsOn(k_builtin[i].slug))
            k_builtin[i].replayFrameBegin(first, fresh);
}

void ModHooks_ReplaySprite(uint16_t image, int16_t left, int16_t top)
{
    size_t i;
    for (i = 0; i < sizeof k_builtin / sizeof k_builtin[0]; i++)
        if (k_builtin[i].replaySprite && Mods_IsOn(k_builtin[i].slug))
            k_builtin[i].replaySprite(image, left, top);
}

void ModHooks_PlayerSetsRaw(void)
{
    size_t i;
    for (i = 0; i < sizeof k_builtin / sizeof k_builtin[0]; i++)
        if (k_builtin[i].playerSetsRaw && Mods_IsOn(k_builtin[i].slug))
            k_builtin[i].playerSetsRaw();
}

void ModHooks_PlayerDrawn(const Sprite *sprite, const SpriteGraphics *gfx, int16_t x, int16_t y)
{
    size_t i;
    for (i = 0; i < sizeof k_builtin / sizeof k_builtin[0]; i++)
        if (k_builtin[i].playerDrawn && Mods_IsOn(k_builtin[i].slug))
            k_builtin[i].playerDrawn(sprite, gfx, x, y);
}

void ModHooks_PitchDrawn(void)
{
    size_t i;
    for (i = 0; i < sizeof k_builtin / sizeof k_builtin[0]; i++)
        if (k_builtin[i].pitchDrawn && Mods_IsOn(k_builtin[i].slug))
            k_builtin[i].pitchDrawn();
}

extern void ModApi_NotePresent(void);       /* src/mod_api.c: what ModApi_InReplay counts by */
extern void ModApi_NoteReplayFrame(void);

void ModHooks_FrameDrawn(SDL_Renderer *renderer)
{
    size_t i;
    ModApi_NotePresent();
    for (i = 0; i < sizeof k_builtin / sizeof k_builtin[0]; i++)
        if (k_builtin[i].frameDrawn && Mods_IsOn(k_builtin[i].slug))
            k_builtin[i].frameDrawn(renderer);
}

#endif /* SWOS_SDL_BUILD */
