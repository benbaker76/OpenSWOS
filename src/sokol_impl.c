/*
 * sokol_impl.c -- the one file that compiles sokol_gfx's implementation, its shaders and, for
 * desktop OpenGL, the entry points it calls (sokol_config.h, sokol_gl_loader.h; SDL builds).
 */
#ifdef SWOS_SDL_BUILD
#include "sokol_config.h"

#if SWOS_GPU && (defined(SOKOL_GLCORE) || defined(SOKOL_GLES3))   /* (Metal, D3D11: src/gpu_native.h) */
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
/* The slang passes' uniform blocks on GL (src/gpu_slang.c): sokol's GL uniforms take one type a
 * block and no uint, and a slang block mixes them -- so a pass's two blocks are real uniform
 * blocks, bound to buffers of ours at binding points 0 (the UBO) and 1 (the push constants).
 * sokol never touches GL_UNIFORM_BUFFER bindings. */
uint32_t SokolGl_NewUniformBuffer(void)
{
    GLuint b = 0;
    glGenBuffers(1, &b);
    return b;
}

void SokolGl_DeleteUniformBuffer(uint32_t b)
{
    GLuint id = b;
    if (id)
        glDeleteBuffers(1, &id);
}

void SokolGl_BindUniformBlocks(uint32_t prog, const char *ubo, const char *push)
{
    GLuint i;
    if (ubo && (i = glGetUniformBlockIndex(prog, ubo)) != GL_INVALID_INDEX)
        glUniformBlockBinding(prog, i, 0);
    if (push && (i = glGetUniformBlockIndex(prog, push)) != GL_INVALID_INDEX)
        glUniformBlockBinding(prog, i, 1);
}

/* A pass into an sRGB image: desktop GL encodes on write only with GL_FRAMEBUFFER_SRGB on (GLES
 * always does), and sokol leaves it off -- so the slang passes switch it around theirs. */
void SokolGl_FramebufferSrgb(int on)
{
#if defined(SOKOL_GLCORE)
    if (on)
        glEnable(0x8DB9 /* GL_FRAMEBUFFER_SRGB */);
    else
        glDisable(0x8DB9);
#else
    (void)on;
#endif
}

/* A slang pass's output that the next pass reads mipmapped (mipmap_input): its levels, after the
 * pass drew level 0. sokol makes no mipmaps of a render target. */
void SokolGl_GenerateMipmaps(uint32_t tex)
{
    glBindTexture(GL_TEXTURE_2D, tex);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void SokolGl_SetUniformBuffer(uint32_t point, uint32_t b, const void *data, uint32_t size)
{
    glBindBuffer(GL_UNIFORM_BUFFER, b);
    glBufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)size, data, GL_STREAM_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, point, b);
}

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
