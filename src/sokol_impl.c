/*
 * sokol_impl.c -- the one file that compiles sokol_gfx's implementation, its shaders and, for
 * desktop OpenGL, the entry points it calls (sokol_config.h, sokol_gl_loader.h; SDL builds).
 */
#ifdef SWOS_SDL_BUILD
#include "sokol_config.h"

#if SWOS_GPU
#include <SDL.h>
#include <stdio.h>

#ifdef SWOS_GPU_GL_LOADER
#include "sokol_gl_loader.h"
#endif

#define SOKOL_IMPL
#include "../third_party/sokol/sokol_gfx.h"

#define SOKOL_SHDC_IMPL
#include "shaders/present.glsl.h"

#ifdef SWOS_GPU_GL_LOADER
#define X(name, req) __typeof__(swos_##name) swos_##name;
SWOS_GL_FUNCS
#undef X

int SokolGl_Load(void)
{
    int missing = 0;
#define X(name, req)                                                                           \
    swos_##name = (__typeof__(swos_##name))SDL_GL_GetProcAddress(#name);                       \
    if (!swos_##name && (req)) {                                                               \
        fprintf(stderr, "[gpu] the driver has no %s\n", #name);                                \
        missing++;                                                                             \
    }
    SWOS_GL_FUNCS
#undef X
    return missing ? -1 : 0;
}
#else
int SokolGl_Load(void) { return 0; }         /* (the toolchain's GL: linked, nothing to load) */
#endif

/* The drawable's pixels as RGBA, top row first (the presenter's captures: gpu_present.c). */
void SokolGl_ReadPixels(int w, int h, void *rgba)
{
    uint8_t *p = (uint8_t *)rgba;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    for (int y = 0; y < h / 2; y++) {                   /* (GL's rows are bottom first) */
        uint8_t *a = p + (size_t)y * w * 4, *b = p + (size_t)(h - 1 - y) * w * 4;
        for (int i = 0; i < w * 4; i++) {
            const uint8_t t = a[i];
            a[i] = b[i];
            b[i] = t;
        }
    }
}

/* What the driver is (the log's [gpu] line). */
const char *SokolGl_Renderer(void)
{
    const GLubyte *s = glGetString(GL_RENDERER);
    return s ? (const char *)s : "?";
}
#endif /* SWOS_GPU */
#endif /* SWOS_SDL_BUILD */
