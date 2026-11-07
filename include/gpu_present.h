/*
 * gpu_present.h -- the GPU presenter (src/gpu_present.c, sokol_gfx; docs/RENDERING.md).
 *
 * The game draws its picture as it always has: 8bpp palette indices in the frontbuffer, the HUD
 * in its own 8bpp layer. This presents it: both go to the GPU as R8 textures with the palette as
 * a 256x1 one, and a shader colours and scales them -- in place of SDL's renderer, which needs the
 * frame converted to RGBA on the CPU first (platform_sdl.c chunky_to_rgba). The simulation, the
 * rollback state, replays and recordings only ever see the 8bpp frame: none of them changes.
 *
 * [video] renderer = gpu (the default), accel (SDL's renderer, hardware), software. A machine that
 * cannot have the GPU presenter -- no OpenGL 4.1 / GLES 3 context, a function missing -- runs on
 * SDL's renderer instead, said in the log.
 */
#ifndef SWOS_GPU_PRESENT_H
#define SWOS_GPU_PRESENT_H

#include <SDL.h>
#include <stdint.h>

/* Compiled into this build (sokol_config.h SWOS_GPU). */
int32_t GpuPresent_Compiled(void);

/* Before SDL_CreateWindow: the context's attributes; the window's flags to add. */
void    GpuPresent_ConfigureWindow(void);
uint32_t GpuPresent_WindowFlags(void);

/* The context and sokol on `window`; 0 when it is up (else the caller falls back). */
int32_t GpuPresent_Init(SDL_Window *window, int vsync);
void    GpuPresent_Shutdown(void);
int32_t GpuPresent_Active(void);
int32_t GpuPresent_Vsync(void);                 /* what the driver gave */

/* One frame of the picture. */
typedef struct {
    const uint8_t  *index;      /* the 8bpp frame (w x h, `indexPitch` bytes a row), or NULL */
    int             indexPitch;
    const uint32_t *argb;       /* or an ARGB8888 frame (the arcade's own screens), w a row */
    int             w, h;
    const SDL_Color *palette;   /* 256 */
    const uint8_t  *hud;        /* the HUD layer (w x h, `hudPitch` a row), or NULL: none */
    int             hudPitch;
    uint16_t        fracX, fracY;   /* the camera's sub-pixel remainder, 16.16 (0: none) */
    int             subpixel;   /* [video] subpixel_scroll */
    int             smooth;     /* [video] scaling = smooth */
    int             stretch;    /* the picture fills the window (widescreen = stretch) */
} GpuFrame;

/* Draws the picture; the overlays (gfx.h) can go on after it, until GpuPresent_End. A frame with
 * no picture (`f` NULL) is cleared to `clear` (r, g, b) -- the loading bar's. 0 when there is a
 * frame to draw into (a minimised window has none: then nothing else is to be drawn). */
int32_t GpuPresent_Begin(const GpuFrame *f, const uint8_t clear[3]);
void    GpuPresent_End(void);                   /* commits and swaps */

/* The drawable's size; where the picture lands on it (pixels), and its own size. */
void    GpuPresent_OutputSize(int *w, int *h);
void    GpuPresent_PictureRect(float *x, float *y, float *w, float *h);
/* The frame just drawn, as RGBA top row first (before GpuPresent_End); 0 when read. */
int32_t GpuPresent_ReadPixels(uint8_t *rgba, int w, int h);

/* gfx.h's GPU side (src/gfx.c calls these when Gfx_IsGpu). */
#include "gfx.h"
typedef struct GpuTex GpuTex;
void    GpuGfx_SetState(GpuTex *tex, GfxBlend blend, GfxSpace space);
void    GpuGfx_Push(const GfxVertex *v, int n);          /* triangles, in threes */
void    GpuGfx_SpaceSize(GfxSpace space, int *w, int *h);
GpuTex *GpuGfx_CreateTexture(int w, int h, int linear);
void    GpuGfx_UpdateTexture(GpuTex *t, const void *rgba, int pitch);
void    GpuGfx_DestroyTexture(GpuTex *t);
void    GpuPresent_EndPass(void);                        /* (before a read of the pixels) */

#endif
