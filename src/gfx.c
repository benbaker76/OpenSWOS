/*
 * gfx.c -- gfx.h on either presenter: SDL's renderer, or the GPU presenter's batch
 * (gpu_present.c). SDL builds.
 */
#ifdef SWOS_SDL_BUILD
#include "gfx.h"
#include "gpu_present.h"

#include <stdlib.h>
#include <string.h>

extern int _screen_width, _screen_height;

struct GfxTexture {
    SDL_Texture *sdl;
    GpuTex      *gpu;
    int          w, h;
};

static SDL_Renderer *s_sdl;             /* SDL's renderer, when it is the presenter */
static GfxSpace s_space = GFX_SPACE_PICTURE;
static GfxBlend s_blend = GFX_BLEND_ALPHA;
static int      s_inFrame;
/* SDL's state round an OUTPUT-space draw, put back for PICTURE */
static int      s_savedLogW, s_savedLogH;
static float    s_savedSx = 1.0f, s_savedSy = 1.0f;
static SDL_Rect s_savedVp;
static int      s_outputSet;

static SDL_Window *s_win;               /* its window (SDL_RenderGetWindow is 2.0.22's) */

void Gfx_BindSdl(SDL_Renderer *r, SDL_Window *w) { s_sdl = r; s_win = w; }

int32_t Gfx_IsGpu(void) { return GpuPresent_Active(); }
int32_t Gfx_InFrame(void) { return s_inFrame; }
GfxSpace Gfx_Space(void) { return s_space; }
GfxBlend Gfx_Blend(void) { return s_blend; }

static SDL_BlendMode sdl_blend(GfxBlend b)
{
    return b == GFX_BLEND_NONE ? SDL_BLENDMODE_NONE : b == GFX_BLEND_MOD ? SDL_BLENDMODE_MOD : SDL_BLENDMODE_BLEND;
}

void Gfx_SetSpace(GfxSpace space)
{
    if (!Gfx_IsGpu() && s_sdl && space != s_space) {
        if (space == GFX_SPACE_OUTPUT) {
            SDL_RenderGetLogicalSize(s_sdl, &s_savedLogW, &s_savedLogH);
            SDL_RenderGetScale(s_sdl, &s_savedSx, &s_savedSy);
            SDL_RenderGetViewport(s_sdl, &s_savedVp);
            if (s_savedLogW) {
                SDL_RenderSetLogicalSize(s_sdl, 0, 0);      /* (as the CRT mask always did) */
            } else {
                SDL_RenderSetScale(s_sdl, 1.0f, 1.0f);
                SDL_RenderSetViewport(s_sdl, NULL);
            }
            s_outputSet = 1;
        } else if (s_outputSet) {
            /* (A logical size sets its own viewport and scale -- the letterbox -- so only a
             * renderer without one gets its scale and viewport back by hand.) */
            if (s_savedLogW) {
                SDL_RenderSetLogicalSize(s_sdl, s_savedLogW, s_savedLogH);
            } else {
                SDL_RenderSetScale(s_sdl, s_savedSx, s_savedSy);
                SDL_RenderSetViewport(s_sdl, &s_savedVp);
            }
            s_outputSet = 0;
        }
    }
    s_space = space;
}

void Gfx_OutputSize(int *w, int *h)
{
    *w = *h = 0;
    if (Gfx_IsGpu())
        GpuPresent_OutputSize(w, h);
    else if (s_sdl)
        SDL_GetRendererOutputSize(s_sdl, w, h);
}

void Gfx_PictureRect(float *x, float *y, float *w, float *h)
{
    int ow = 0, oh = 0;
    *x = *y = 0.0f;
    Gfx_OutputSize(&ow, &oh);
    *w = (float)ow;
    *h = (float)oh;
    if (Gfx_IsGpu()) {
        GpuPresent_PictureRect(x, y, w, h);
        return;
    }
#if SDL_VERSION_ATLEAST(2, 0, 18)
    if (!s_sdl)
        return;
    int lw = 0, lh = 0, ww = 0, wh = 0;
    SDL_RenderGetLogicalSize(s_sdl, &lw, &lh);
    SDL_Window *win = s_win;
    if (!lw || !lh || !win)
        return;                                 /* (stretched: the picture is the whole window) */
    SDL_GetWindowSize(win, &ww, &wh);
    if (ww <= 0 || wh <= 0)
        return;
    int x0, y0, x1, y1;
    SDL_RenderLogicalToWindow(s_sdl, 0.0f, 0.0f, &x0, &y0);
    SDL_RenderLogicalToWindow(s_sdl, (float)lw, (float)lh, &x1, &y1);
    const float sx = (float)ow / ww, sy = (float)oh / wh;    /* window points -> drawable pixels */
    *x = x0 * sx;
    *y = y0 * sy;
    *w = (x1 - x0) * sx;
    *h = (y1 - y0) * sy;
#endif
}

void Gfx_SpaceSize(int *w, int *h)
{
    if (Gfx_IsGpu()) {
        GpuGfx_SpaceSize(s_space, w, h);
    } else if (s_space == GFX_SPACE_OUTPUT) {
        Gfx_OutputSize(w, h);
    } else {
        *w = _screen_width;
        *h = _screen_height;
    }
}

void Gfx_SetBlend(GfxBlend blend) { s_blend = blend; }

void Gfx_BeginFrame(void)
{
    s_inFrame = 1;
    s_space = GFX_SPACE_PICTURE;
    s_blend = GFX_BLEND_ALPHA;
    s_outputSet = 0;
}

void Gfx_EndFrame(void)
{
    Gfx_SetSpace(GFX_SPACE_PICTURE);
    s_inFrame = 0;
}

/* ---- textures ---- */

GfxTexture *Gfx_CreateTexture(int w, int h, int linear)
{
    GfxTexture *t = (GfxTexture *)calloc(1, sizeof *t);
    if (!t)
        return NULL;
    t->w = w;
    t->h = h;
    if (Gfx_IsGpu()) {
        t->gpu = GpuGfx_CreateTexture(w, h, linear);
    } else if (s_sdl) {
        t->sdl = SDL_CreateTexture(s_sdl, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
        if (t->sdl)
            SDL_SetTextureScaleMode(t->sdl, linear ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    }
    if (!t->gpu && !t->sdl) {
        free(t);
        return NULL;
    }
    return t;
}

void Gfx_UpdateTexture(GfxTexture *t, const void *rgba, int pitch)
{
    if (!t)
        return;
    if (t->gpu)
        GpuGfx_UpdateTexture(t->gpu, rgba, pitch);
    else if (t->sdl)
        SDL_UpdateTexture(t->sdl, NULL, rgba, pitch);
}

void Gfx_DestroyTexture(GfxTexture *t)
{
    if (!t)
        return;
    if (t->gpu)
        GpuGfx_DestroyTexture(t->gpu);
    if (t->sdl)
        SDL_DestroyTexture(t->sdl);
    free(t);
}

/* ---- drawing ---- */

static void gpu_quad(GpuTex *tex, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                     SDL_Color c)
{
    GfxVertex v[6];
    const float px[6] = { x0, x1, x0, x1, x1, x0 }, py[6] = { y0, y0, y1, y0, y1, y1 };
    const float pu[6] = { u0, u1, u0, u1, u1, u0 }, pv[6] = { v0, v0, v1, v0, v1, v1 };
    for (int i = 0; i < 6; i++) {
        v[i].position.x = px[i];
        v[i].position.y = py[i];
        v[i].color = c;
        v[i].tex_coord.x = pu[i];
        v[i].tex_coord.y = pv[i];
    }
    GpuGfx_SetState(tex, s_blend, s_space);
    GpuGfx_Push(v, 6);
}

void Gfx_DrawTexture(GfxTexture *t, const SDL_Rect *src, const SDL_FRect *dst)
{
    if (!t)
        return;
    int sw, sh;
    Gfx_SpaceSize(&sw, &sh);
    const SDL_FRect d = dst ? *dst : (SDL_FRect){ 0.0f, 0.0f, (float)sw, (float)sh };
    if (t->gpu) {
        const SDL_Rect s = src ? *src : (SDL_Rect){ 0, 0, t->w, t->h };
        const SDL_Color white = { 255, 255, 255, 255 };
        gpu_quad(t->gpu, d.x, d.y, d.x + d.w, d.y + d.h, (float)s.x / t->w, (float)s.y / t->h,
                 (float)(s.x + s.w) / t->w, (float)(s.y + s.h) / t->h, white);
    } else if (t->sdl && s_sdl) {
        SDL_SetTextureBlendMode(t->sdl, sdl_blend(s_blend));
        SDL_RenderCopyF(s_sdl, t->sdl, src, &d);
    }
}

void Gfx_Geometry(GfxTexture *tex, const GfxVertex *v, int nv, const int *indices, int ni)
{
    if (!v || nv <= 0)
        return;
    if (Gfx_IsGpu()) {
        GpuGfx_SetState(tex ? tex->gpu : NULL, s_blend, s_space);
        if (!indices) {
            GpuGfx_Push(v, nv - nv % 3);
            return;
        }
        GfxVertex tri[3];
        for (int i = 0; i + 2 < ni; i += 3) {
            int ok = 1;
            for (int k = 0; k < 3; k++) {
                const int j = indices[i + k];
                if (j < 0 || j >= nv)
                    ok = 0;
                else
                    tri[k] = v[j];
            }
            if (ok)
                GpuGfx_Push(tri, 3);
        }
        return;
    }
#if SDL_VERSION_ATLEAST(2, 0, 18)
    if (!s_sdl)
        return;
    if (tex && tex->sdl)
        SDL_SetTextureBlendMode(tex->sdl, sdl_blend(s_blend));
    SDL_SetRenderDrawBlendMode(s_sdl, sdl_blend(s_blend));
    SDL_RenderGeometry(s_sdl, tex ? tex->sdl : NULL, v, nv, indices, ni);
#else
    (void)tex; (void)indices; (void)ni;
#endif
}

void Gfx_FillRect(const SDL_FRect *r, SDL_Color c)
{
    if (!r)
        return;
    if (Gfx_IsGpu()) {
        gpu_quad(NULL, r->x, r->y, r->x + r->w, r->y + r->h, 0, 0, 1, 1, c);
    } else if (s_sdl) {
        SDL_SetRenderDrawBlendMode(s_sdl, sdl_blend(s_blend));
        SDL_SetRenderDrawColor(s_sdl, c.r, c.g, c.b, c.a);
        SDL_RenderFillRectF(s_sdl, r);
    }
}

void Gfx_DrawRect(const SDL_FRect *r, SDL_Color c)
{
    if (!r)
        return;
    if (Gfx_IsGpu()) {
        const SDL_FRect e[4] = {
            { r->x, r->y, r->w, 1.0f }, { r->x, r->y + r->h - 1.0f, r->w, 1.0f },
            { r->x, r->y + 1.0f, 1.0f, r->h - 2.0f }, { r->x + r->w - 1.0f, r->y + 1.0f, 1.0f, r->h - 2.0f } };
        for (int i = 0; i < 4; i++)
            Gfx_FillRect(&e[i], c);
    } else if (s_sdl) {
        SDL_SetRenderDrawBlendMode(s_sdl, sdl_blend(s_blend));
        SDL_SetRenderDrawColor(s_sdl, c.r, c.g, c.b, c.a);
        SDL_RenderDrawRectF(s_sdl, r);
    }
}

void Gfx_DrawLine(float x1, float y1, float x2, float y2, SDL_Color c)
{
    if (Gfx_IsGpu()) {
        /* A pixel-wide quad along it, as SDL draws a line. */
        float dx = x2 - x1, dy = y2 - y1;
        float len = SDL_sqrtf(dx * dx + dy * dy);
        if (len <= 0.0f) {
            const SDL_FRect dot = { x1, y1, 1.0f, 1.0f };
            Gfx_FillRect(&dot, c);
            return;
        }
        const float nx = -dy / len * 0.5f, ny = dx / len * 0.5f;
        GfxVertex v[6];
        const float px[6] = { x1 + nx, x2 + nx, x1 - nx, x2 + nx, x2 - nx, x1 - nx };
        const float py[6] = { y1 + ny, y2 + ny, y1 - ny, y2 + ny, y2 - ny, y1 - ny };
        for (int i = 0; i < 6; i++) {
            v[i].position.x = px[i] + 0.5f;
            v[i].position.y = py[i] + 0.5f;
            v[i].color = c;
            v[i].tex_coord.x = v[i].tex_coord.y = 0.0f;
        }
        GpuGfx_SetState(NULL, s_blend, s_space);
        GpuGfx_Push(v, 6);
    } else if (s_sdl) {
        SDL_SetRenderDrawBlendMode(s_sdl, sdl_blend(s_blend));
        SDL_SetRenderDrawColor(s_sdl, c.r, c.g, c.b, c.a);
        SDL_RenderDrawLineF(s_sdl, x1, y1, x2, y2);
    }
}
#endif /* SWOS_SDL_BUILD */
