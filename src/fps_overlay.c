/*
 * fps_overlay.c -- SWOS 2020's --show-fps: the frames shown a second, small, in the window's top
 * right corner. [video] show_fps = true | false (VIDEO OPTIONS' SHOW FPS). SDL builds.
 *
 * Counted where the frames are presented (platform_sdl.c draw_overlays, on either presenter): what
 * reaches the window, menus and matches alike. Drawn over the picture with gfx.h in the window's
 * own pixels -- a 3 x 5 digit font of filled squares on a dark box -- never into the game's 8bpp
 * frame, so recordings, screenshots of the frame and every match are as they were.
 */
#ifdef SWOS_SDL_BUILD

#include "gfx.h"

#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int32_t  s_on;
static uint32_t s_since, s_frames;
static int32_t  s_fps = -1;             /* the last whole second's, -1 none yet */

void FpsOverlay_Set(int32_t on)
{
    s_on = on != 0;
    s_since = 0;
    s_frames = 0;
    s_fps = -1;
}

int32_t FpsOverlay_On(void) { return s_on; }

/* The last whole second's count (-1 none yet): SWOS_FPS_TEST. */
int32_t FpsOverlay_Value(void) { return s_fps; }

/* 3 x 5: a row a byte, bit 2 the left column. */
static const uint8_t k_glyph[][5] = {
    { 7, 5, 5, 5, 7 }, { 2, 6, 2, 2, 7 }, { 7, 1, 7, 4, 7 }, { 7, 1, 3, 1, 7 }, { 5, 5, 7, 1, 1 },
    { 7, 4, 7, 1, 7 }, { 7, 4, 7, 5, 7 }, { 7, 1, 1, 2, 2 }, { 7, 5, 7, 5, 7 }, { 7, 5, 7, 1, 7 },
    { 7, 4, 6, 4, 4 },   /* F */
    { 7, 5, 7, 4, 4 },   /* P */
    { 7, 4, 7, 1, 7 },   /* S */
};

static int32_t glyph_of(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c == 'F' ? 10 : c == 'P' ? 11 : c == 'S' ? 12 : -1;
}

/* Each frame presented, between the presenter's frame and its present (Gfx_InFrame). */
void FpsOverlay_Draw(void)
{
    const uint32_t now = SDL_GetTicks();
    char text[16];
    int ow = 0, oh = 0;
    if (!s_on)
        return;
    if (!s_since)
        s_since = now;
    s_frames++;
    if (now - s_since >= 1000) {
        s_fps = (int32_t)((s_frames * 1000u + (now - s_since) / 2) / (now - s_since));
        s_frames = 0;
        s_since = now;
    }
    if (s_fps < 0)
        return;
    {   /* SWOS_FPS_TEST=1: the counter counted (once). PASS / FAIL on stdout. */
        static int32_t told;
        if (!told && getenv("SWOS_FPS_TEST")) {
            told = 1;
            printf("[fps-test] %s: %d frames shown a second\n", s_fps > 0 ? "PASS" : "FAIL", (int)s_fps);
            fflush(stdout);
        }
    }
    snprintf(text, sizeof text, "%d FPS", (int)s_fps);

    {
        const GfxSpace space = Gfx_Space();
        int32_t px, n = (int32_t)strlen(text), w, h, x0, y0;
        Gfx_SetSpace(GFX_SPACE_OUTPUT);
        Gfx_OutputSize(&ow, &oh);
        px = oh / 270;                          /* (one pixel of a 272-line picture) */
        if (px < 2)
            px = 2;
        w = (n * 4 + 1) * px;
        h = 7 * px;
        x0 = ow - w - px * 2;
        y0 = px * 2;
        {
            const SDL_FRect box = { (float)x0, (float)y0, (float)w, (float)h };
            const SDL_Color bg = { 0, 0, 0, 160 };
            Gfx_FillRect(&box, bg);
        }
        for (int32_t i = 0; i < n; i++) {
            const int32_t g = glyph_of(text[i]);
            const SDL_Color fg = { 255, 255, 96, 255 };
            if (g < 0)
                continue;
            for (int32_t r = 0; r < 5; r++)
                for (int32_t c = 0; c < 3; c++)
                    if (k_glyph[g][r] & (4 >> c)) {
                        const SDL_FRect d = { (float)(x0 + (1 + i * 4 + c) * px), (float)(y0 + (1 + r) * px),
                                              (float)px, (float)px };
                        Gfx_FillRect(&d, fg);
                    }
        }
        Gfx_SetSpace(space);
    }
}

#endif /* SWOS_SDL_BUILD */
