/* The weather effect (third_party/pitch_weather, MIT, C4ppy77), compiled into the SDL builds whose
 * SDL has SDL_RenderGeometry (2.0.18). Its own file in third_party so it stays as it came.
 *
 * It draws through SDL's renderer; its three calls are sent to gfx.h here instead, so it draws
 * over the frame on either presenter (SDL's renderer, or the GPU presenter). Its renderer
 * argument goes unused. */
#ifdef SWOS_SDL_BUILD
#include <SDL.h>
#if SDL_VERSION_ATLEAST(2, 0, 18)
#include "gfx.h"

static int pw_get_blend(SDL_Renderer *r, SDL_BlendMode *m)
{
    const GfxBlend b = Gfx_Blend();
    (void)r;
    *m = b == GFX_BLEND_NONE ? SDL_BLENDMODE_NONE : b == GFX_BLEND_MOD ? SDL_BLENDMODE_MOD : SDL_BLENDMODE_BLEND;
    return 0;
}

static int pw_set_blend(SDL_Renderer *r, SDL_BlendMode m)
{
    (void)r;
    Gfx_SetBlend(m == SDL_BLENDMODE_NONE ? GFX_BLEND_NONE : m == SDL_BLENDMODE_MOD ? GFX_BLEND_MOD : GFX_BLEND_ALPHA);
    return 0;
}

static int pw_geometry(SDL_Renderer *r, SDL_Texture *t, const SDL_Vertex *v, int nv, const int *idx, int ni)
{
    (void)r; (void)t;               /* (it draws no texture) */
    Gfx_Geometry(NULL, v, nv, idx, ni);
    return 0;
}

#define SDL_GetRenderDrawBlendMode pw_get_blend
#define SDL_SetRenderDrawBlendMode pw_set_blend
#define SDL_RenderGeometry pw_geometry
#include "../third_party/pitch_weather/pitch_weather.c"
#endif
#endif
