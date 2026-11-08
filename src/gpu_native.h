/*
 * gpu_native.h -- what the GPU presenter (src/gpu_present.c) needs of a backend that is not
 * OpenGL: its device, its swapchain, the frame's target, a capture, mipmaps (docs/RENDERING.md).
 * One interface, two files: src/sokol_impl_metal.m (Metal: macOS, iOS) and
 * src/sokol_impl_d3d11.c (Direct3D 11: Windows). Each also compiles sokol's implementation for
 * its backend. OpenGL needs none of this: SDL's GL context is the device, and the window's
 * framebuffer 0 the target (src/sokol_impl.c).
 */
#ifndef SWOS_GPU_NATIVE_H
#define SWOS_GPU_NATIVE_H

#include "sokol_config.h"

#if SWOS_GPU && (defined(SOKOL_METAL) || defined(SOKOL_D3D11))
#define GPU_NATIVE 1

#include <stdint.h>
#include <SDL.h>
#ifndef SOKOL_GFX_INCLUDED              /* (an implementation file has it, SOKOL_IMPL and all) */
#include "../third_party/sokol/sokol_gfx.h"
#endif

/* The device on the window: env gets what sg_setup needs. 0 on success; *vsyncOut what it got. */
int32_t      GpuNative_Init(SDL_Window *window, int32_t vsync, int32_t *vsyncOut, sg_environment *env);
void         GpuNative_Shutdown(void);
const char  *GpuNative_Renderer(void);
/* The window's drawable in pixels. */
void         GpuNative_DrawableSize(int *w, int *h);
/* The swapchain's pixel format (the presenter's default colour format). */
sg_pixel_format GpuNative_SwapFormat(void);

/* A frame: Begin before its first pass, Swapchain for the screen pass (once a frame), End after
 * sg_commit -- it presents. */
void         GpuNative_FrameBegin(void);
void         GpuNative_Swapchain(sg_swapchain *sc);
void         GpuNative_FrameEnd(void);

/* A capture of the screen pass: Before while its pass is still open (0: nothing to read), then
 * sg_end_pass + sg_commit, then After -- the pixels as RGBA, top row first. */
int32_t      GpuNative_ReadBeforeEndPass(int w, int h);
void         GpuNative_ReadAfterCommit(uint8_t *rgba);

/* An image's mip levels from its level 0 (a render target the next slang pass reads mipmapped),
 * outside any pass. */
void         GpuNative_GenerateMipmaps(sg_image img);

#endif
#endif
