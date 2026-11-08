/*
 * gfx.h -- drawing over the game's picture, on either presenter (SDL builds).
 *
 * The picture itself is the presenter's (platform_sdl.c): SDL's renderer, or the GPU presenter
 * (src/gpu_present.c, sokol_gfx, docs/RENDERING.md). What is drawn OVER it -- the weather and the
 * flags (mods), the CRT mask, the phones' touch controls, the loading bar -- goes through here,
 * so it looks the same on both: textures, triangles, rectangles and lines, in one of two spaces,
 *
 *   GFX_SPACE_PICTURE  the picture's own pixels (_screen_width x _screen_height), letterboxed or
 *                      stretched to the window as the picture is;
 *   GFX_SPACE_OUTPUT   the window's pixels (the drawable's, on a high-DPI screen).
 *
 * Only between the presenter's frame and its present (Gfx_InFrame): ModHooks_FrameDrawn,
 * CrtEffects_Apply, TouchControls_Draw and the progress bar are called there.
 */
#ifndef SWOS_GFX_H
#define SWOS_GFX_H

#include <SDL.h>
#include <stdint.h>

typedef struct GfxTexture GfxTexture;

typedef enum { GFX_SPACE_PICTURE, GFX_SPACE_OUTPUT } GfxSpace;
typedef enum { GFX_BLEND_NONE, GFX_BLEND_ALPHA, GFX_BLEND_MOD } GfxBlend;

/* A vertex: exactly SDL_Vertex's layout (position, colour, texture coordinate), so a caller of
 * SDL_RenderGeometry passes its array as it is. */
typedef SDL_Vertex GfxVertex;

int32_t Gfx_IsGpu(void);                        /* the GPU presenter is the one drawing */
void    Gfx_SetSpace(GfxSpace space);           /* for the draws that follow (PICTURE at a frame's start) */
GfxSpace Gfx_Space(void);
void    Gfx_SpaceSize(int *w, int *h);          /* the current space's size, in its own pixels */
void    Gfx_OutputSize(int *w, int *h);         /* the window's drawable, in pixels */
/* Where the picture is on the window, in OUTPUT pixels (the letterbox or the stretch): to draw
 * at the window's own resolution over something the picture has (a menu entry's box). */
void    Gfx_PictureRect(float *x, float *y, float *w, float *h);

/* A texture of RGBA8 (bytes R, G, B, A in memory), `linear` to filter when scaled, else nearest. */
GfxTexture *Gfx_CreateTexture(int w, int h, int linear);
void    Gfx_UpdateTexture(GfxTexture *t, const void *rgba, int pitch);   /* the whole of it */
void    Gfx_DestroyTexture(GfxTexture *t);

void    Gfx_SetBlend(GfxBlend blend);           /* for the draws that follow (ALPHA at a frame's start) */
GfxBlend Gfx_Blend(void);

/* `src` NULL: the whole texture; `dst` NULL: the whole space. */
void    Gfx_DrawTexture(GfxTexture *t, const SDL_Rect *src, const SDL_FRect *dst);
/* Triangles; `tex` NULL draws the colours alone; `indices` NULL takes the vertices in threes. */
void    Gfx_Geometry(GfxTexture *tex, const GfxVertex *v, int nv, const int *indices, int ni);
void    Gfx_FillRect(const SDL_FRect *r, SDL_Color c);
void    Gfx_DrawRect(const SDL_FRect *r, SDL_Color c);      /* its outline, a pixel wide */
void    Gfx_DrawLine(float x1, float y1, float x2, float y2, SDL_Color c);

/* ---- the presenters' side (platform_sdl.c, gpu_present.c) ---- */
void    Gfx_BindSdl(SDL_Renderer *r, SDL_Window *w);   /* SDL's renderer when it presents (else NULL) */
void    Gfx_BeginFrame(void);                   /* PICTURE space, ALPHA blend */
void    Gfx_EndFrame(void);
int32_t Gfx_InFrame(void);

#endif
