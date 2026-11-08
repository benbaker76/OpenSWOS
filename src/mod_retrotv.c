/*
 * mod_retrotv.c -- the retrotv mod (bin/mods/retrotv; canonical, off by default): the picture as a
 * television shows it, through a RetroArch slang preset (docs/RENDERING.md, "Shaders from files";
 * the engine's side is src/slang.c + src/gpu_slang.c). The GPU presenter only: SDL's renderer has
 * no shader stage.
 *
 * [mod.retrotv] preset = a .slangp in the mod's shaders/ or the player's game folder's shaders/
 * (Documents\SWOS\shaders on Windows) -- any of RetroArch's, with its .slang files and textures
 * beside it as RetroArch keeps them -- or off. The default is OpenSWOS's own, openswos-tv.slangp:
 * the tube's glass and a composite signal. curvature and ntsc set its CURVATURE and NTSC
 * parameters (any preset with parameters of those names). Read at start-up: the MODS page's
 * changes apply from the next start, as every mod's.
 */
#ifdef SWOS_SDL_BUILD

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <SDL.h>

#include "mod_api.h"
#include "swos_io.h"            /* swos_user_dir */

#define SLUG "retrotv"

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f)
        fclose(f);
    return f != NULL;
}

static float level(const char *setting, const char *const names[4], const float values[4], float fallback)
{
    for (int i = 0; i < 4; i++)
        if (SDL_strcasecmp(setting, names[i]) == 0)
            return values[i];
    return fallback;
}

static void init(void)
{
    static const char *const k_curve[4] = { "off", "low", "medium", "high" };
    static const float k_curveV[4] = { 0.0f, 0.2f, 0.4f, 0.8f };
    static const char *const k_ntsc[4] = { "off", "low", "medium", "full" };
    static const float k_ntscV[4] = { 0.0f, 0.33f, 0.66f, 1.0f };
    const char *preset = ModApi_Setting(SLUG, "preset", "openswos-tv.slangp");
    char path[1200];

    if (!preset[0] || SDL_strcasecmp(preset, "off") == 0)
        return;
    snprintf(path, sizeof path, "%s/shaders/%s", ModApi_ModDir(SLUG), preset);
    if (!file_exists(path))
        snprintf(path, sizeof path, "%s/shaders/%s", swos_user_dir(), preset);
    if (!file_exists(path)) {
        fprintf(stderr, "[retrotv] %s: not in the mod's shaders/ nor %s/shaders -- no preset\n", preset, swos_user_dir());
        return;
    }
    if (!ModApi_SetShaderPreset(path)) {
        fprintf(stderr, "[retrotv] this build draws no shaders (no shader compiler): the picture as it is\n");
        return;
    }
    ModApi_SetShaderParameter("CURVATURE", level(ModApi_Setting(SLUG, "curvature", "medium"), k_curve, k_curveV, 0.4f));
    ModApi_SetShaderParameter("NTSC", level(ModApi_Setting(SLUG, "ntsc", "full"), k_ntsc, k_ntscV, 1.0f));
    fprintf(stderr, "[retrotv] %s\n", path);
}

const ModDef ModDef_retrotv = {
    .slug = SLUG,
    .init = init,
};

#endif /* SWOS_SDL_BUILD */
