/*
 * mod_paletteq.c -- the paletteq mod (bin/mods/paletteq; canonical, off by default): the picture in
 * an old machine's colours, through a RetroArch slang preset of OpenSWOS's own
 * (bin/mods/paletteq/shaders/paletteq.slang; docs/RENDERING.md, "Shaders from files"). The GPU
 * presenter only: SDL's renderer has no shader stage.
 *
 * [mod.paletteq] palette: amiga-ecs (the OCS/ECS colour space, 4,096 colours), amiga-aga (SWOS's
 * own AGA palette), dos-vga (the VGA BIOS's 256), c64 (the VIC-II's 16, Colodore's measurement),
 * and the consoles' (k_palettes below; Wikipedia's List of video game console palettes) -- each a
 * lookup strip in shaders/luts, made with its preset by scripts/make_paletteq.sh. dither: on | off;
 * dither_strength: auto (the palette's own: little for 4,096 colours, much for 4), low, medium,
 * high. Read at start-up,
 * as every mod's settings. One preset draws the picture: with the retrotv mod on too, the one
 * set last wins (this one: it is later in the list).
 */
#ifdef SWOS_SDL_BUILD

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <SDL.h>

#include "mod_api.h"

#define SLUG "paletteq"

/* Each palette and the dither it looks right with ("auto"): little for many colours, much for few. */
static const struct { const char *name; float dither; } k_palettes[] = {
    { "amiga-ecs", 0.06f }, { "amiga-aga", 0.08f }, { "dos-vga", 0.08f }, { "c64", 0.22f },
    { "nes", 0.14f }, { "master-system", 0.18f }, { "mega-drive", 0.10f }, { "game-boy", 0.30f },
    { "virtual-boy", 0.30f }, { "atari-secam", 0.35f }, { "amstrad-cpc", 0.20f }, { "zx-spectrum", 0.30f },
    { "msx", 0.22f }, { "intellivision", 0.22f }, { "super-cassette-vision", 0.25f }, { "odyssey2", 0.25f },
    { "cga", 0.35f }, { "pico-8", 0.20f },
};

static int32_t s_on;                    /* the preset is up (frame keeps its grid on the pitch) */

static void init(void)
{
    static const char *const k_strength[4] = { "auto", "low", "medium", "high" };
    static const float k_strengthV[4] = { -1.0f, 0.06f, 0.12f, 0.25f };
    const char *palette = ModApi_Setting(SLUG, "palette", "c64");
    const char *dither = ModApi_Setting(SLUG, "dither", "on");
    const char *strength = ModApi_Setting(SLUG, "dither_strength", "auto");
    char path[1200];
    int32_t p = -1;

    for (int32_t i = 0; i < (int32_t)(sizeof k_palettes / sizeof k_palettes[0]); i++)
        if (SDL_strcasecmp(palette, k_palettes[i].name) == 0)
            p = i;
    if (p < 0) {
        fprintf(stderr, "[paletteq] %s: no such palette -- the picture as it is\n", palette);
        return;
    }
    snprintf(path, sizeof path, "%s/shaders/paletteq-%s.slangp", ModApi_ModDir(SLUG), k_palettes[p].name);
    if (!ModApi_SetShaderPreset(path)) {
        fprintf(stderr, "[paletteq] this build draws no shaders (SDL's renderer, or no shader compiler): "
                        "the picture as it is\n");
        return;
    }
    float d = k_palettes[p].dither;
    for (int32_t i = 1; i < 4; i++)
        if (SDL_strcasecmp(strength, k_strength[i]) == 0)
            d = k_strengthV[i];
    if (SDL_strcasecmp(dither, "off") == 0)
        d = 0.0f;
    ModApi_SetShaderParameter("DITHER", d);
    s_on = 1;
    fprintf(stderr, "[paletteq] %s, dither %.2f\n", path, (double)d);
}

/* Each frame: the dither's grid where the camera has the pitch (GRID_X / GRID_Y, 0..3), so the
 * pattern stays on the grass as it scrolls -- the world point at the screen's corner, in a match or
 * a replay; the screen's own grid in the menus. */
static void frame(void)
{
    static int32_t lastX = -1, lastY = -1;
    int16_t cx = 0, cy = 0;
    if (!s_on)
        return;
    if (ModApi_InMatch() || ModApi_InReplay())
        ModApi_Camera(&cx, &cy);
    const int32_t gx = cx & 3, gy = cy & 3;
    if (gx != lastX || gy != lastY) {
        ModApi_SetShaderParameter("GRID_X", (float)gx);
        ModApi_SetShaderParameter("GRID_Y", (float)gy);
        lastX = gx;
        lastY = gy;
    }
}

const ModDef ModDef_paletteq = {
    .slug = SLUG,
    .init = init,
    .frameDrawn = frame,
};

#endif /* SWOS_SDL_BUILD */
