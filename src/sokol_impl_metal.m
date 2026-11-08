/*
 * sokol_impl_metal.m -- sokol_gfx's implementation for the Metal backend (macOS, iOS), and what the
 * GPU presenter needs of Metal around it (src/gpu_present.c; docs/RENDERING.md). Objective-C with
 * ARC, as sokol's Metal backend wants; the other backends compile sokol in src/sokol_impl.c.
 *
 *   Mtl_Init            the device, and SDL's Metal view and layer on the window (BGRA8; vsync
 *                       is the layer's displaySyncEnabled on macOS -- iOS always syncs)
 *   Mtl_FrameBegin/End  an autorelease pool around a frame (the drawable and sokol's encoders are
 *                       autoreleased objects)
 *   Mtl_NextDrawable    the layer's next drawable, for the screen pass
 *   Mtl_Read*           the frame's pixels for a capture: the drawable copied into a buffer on the
 *                       frame's own command buffer before its pass ends (and presents it), read
 *                       when the frame is done
 */
#include "sokol_config.h"

#if defined(SWOS_SDL_BUILD) && SWOS_GPU && defined(SOKOL_METAL)

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <SDL.h>
#include <SDL_metal.h>

#define SOKOL_IMPL
#include "../third_party/sokol/sokol_gfx.h"

#define SOKOL_SHDC_IMPL
#include "shaders/present.glsl.h"

extern void *objc_autoreleasePoolPush(void);
extern void  objc_autoreleasePoolPop(void *pool);

static SDL_MetalView         s_view;
static CAMetalLayer         *s_layer;
static id<MTLDevice>         s_device;
static id<CAMetalDrawable>   s_drawable;
static void                 *s_pool;
static id<MTLBuffer>         s_readBuf;
static id<MTLCommandBuffer>  s_readCb;
static int                   s_readW, s_readH;

const void *Mtl_Init(SDL_Window *window, int vsync, int *vsyncOut)
{
    s_device = MTLCreateSystemDefaultDevice();
    if (!s_device)
        return NULL;
    s_view = SDL_Metal_CreateView(window);
    if (!s_view) {
        s_device = nil;
        return NULL;
    }
    s_layer = (__bridge CAMetalLayer *)SDL_Metal_GetLayer(s_view);
    s_layer.device = s_device;
    s_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    s_layer.framebufferOnly = NO;           /* (a capture reads the drawable back) */
#if !TARGET_OS_IPHONE
    s_layer.displaySyncEnabled = vsync ? YES : NO;
    *vsyncOut = vsync != 0;
#else
    (void)vsync;
    *vsyncOut = 1;
#endif
    return (__bridge const void *)s_device;
}

const char *Mtl_Renderer(void)
{
    return s_device ? s_device.name.UTF8String : "";
}

void Mtl_Shutdown(void)
{
    s_drawable = nil;
    s_readBuf = nil;
    s_readCb = nil;
    s_layer = nil;
    if (s_view)
        SDL_Metal_DestroyView(s_view);
    s_view = NULL;
    s_device = nil;
}

void Mtl_FrameBegin(void)
{
    s_pool = objc_autoreleasePoolPush();
}

const void *Mtl_NextDrawable(void)
{
    s_drawable = [s_layer nextDrawable];
    return (__bridge const void *)s_drawable;
}

void Mtl_FrameEnd(void)
{
    s_drawable = nil;
    if (s_pool)
        objc_autoreleasePoolPop(s_pool);
    s_pool = NULL;
}

/* Before the screen pass ends: its encoder closed here (sokol's end_pass then only presents),
 * and the drawable copied into a buffer the CPU can read. 0 when there is nothing to read. */
int Mtl_ReadBeforeEndPass(int w, int h)
{
    id<MTLBlitCommandEncoder> blit;
    if (!s_drawable || !_sg.mtl.cmd_buffer || !_sg.mtl.render_cmd_encoder ||
        (int)s_drawable.texture.width != w || (int)s_drawable.texture.height != h)
        return 0;
    [_sg.mtl.render_cmd_encoder endEncoding];
    _sg.mtl.render_cmd_encoder = nil;
    if (!s_readBuf || s_readW != w || s_readH != h) {
        s_readBuf = [s_device newBufferWithLength:(NSUInteger)w * (NSUInteger)h * 4u options:MTLResourceStorageModeShared];
        s_readW = w;
        s_readH = h;
    }
    blit = [_sg.mtl.cmd_buffer blitCommandEncoder];
    [blit copyFromTexture:s_drawable.texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake((NSUInteger)w, (NSUInteger)h, 1) toBuffer:s_readBuf destinationOffset:0
      destinationBytesPerRow:(NSUInteger)w * 4u destinationBytesPerImage:(NSUInteger)w * (NSUInteger)h * 4u];
    [blit endEncoding];
    s_readCb = _sg.mtl.cmd_buffer;
    return 1;
}

/* After sg_commit: the frame done, its pixels as RGBA, top row first. */
void Mtl_ReadAfterCommit(uint8_t *rgba)
{
    const uint8_t *src;
    if (!s_readCb || !s_readBuf)
        return;
    [s_readCb waitUntilCompleted];
    src = (const uint8_t *)s_readBuf.contents;
    for (int i = 0; i < s_readW * s_readH; i++) {      /* BGRA to RGBA */
        rgba[i * 4 + 0] = src[i * 4 + 2];
        rgba[i * 4 + 1] = src[i * 4 + 1];
        rgba[i * 4 + 2] = src[i * 4 + 0];
        rgba[i * 4 + 3] = src[i * 4 + 3];
    }
    s_readCb = nil;
}

#endif
