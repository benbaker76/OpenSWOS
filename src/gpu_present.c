/*
 * gpu_present.c -- the GPU presenter: the game's 8bpp frame coloured and scaled by a shader
 * (gpu_present.h, docs/RENDERING.md), and the GPU side of gfx.h's overlays. SDL builds.
 *
 * What it reproduces from SDL's renderer path (platform_sdl.c render_frame_impl), so the two look
 * the same:
 *   - the letterbox: SDL2's logical size (the picture fitted to the window, its aspect kept,
 *     centred), or the whole window when the picture is stretched;
 *   - "smooth" scaling: nearest to a whole factor n, then linear -- n chosen as
 *     scale_pass_factor chooses it; at a whole scale (every windowed one) nearest alone;
 *   - the sub-pixel scroll: the source moved by the camera's remainder, quantised as
 *     present_frame_copy does, its last row and column clamped;
 *   - the HUD layer over it, unshifted, index 0 transparent.
 * Then the overlays (gfx.h) and the present.
 */
#ifdef SWOS_SDL_BUILD
#include "gpu_present.h"
#include "gfx.h"
#include "sokol_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if SWOS_GPU
#include "shaders/present.glsl.h"

extern int  SokolGl_Load(void);                          /* sokol_impl.c */
extern void SokolGl_ReadPixels(int w, int h, void *rgba);
extern const char *SokolGl_Renderer(void);

#define BATCH_VERTS (64 * 1024)

typedef struct GpuTex GpuTex;
struct GpuTex {
    sg_image image;
    sg_view  view;
    int      w, h, linear;
};

static struct {
    int           up;
    SDL_Window   *window;
    SDL_GLContext ctx;
    int           vsync;
    sg_pipeline   world, hud, batch[3];
    sg_sampler    nearest, linear;
    sg_image      index, argb, hudImage, palette;
    sg_view       indexView, argbView, hudView, paletteView;
    int           imgW, imgH;
    uint8_t      *stage;                /* the packed frame / HUD (sokol takes whole images) */
    uint32_t     *palStage;
    GpuTex   *white;
    sg_buffer     vbuf;
    /* the frame */
    int           inPass, outW, outH;
    float         picX, picY, picW, picH;
    int           srcW, srcH;
    /* the batch */
    GfxVertex     verts[BATCH_VERTS];
    int           nverts;
    GpuTex   *batchTex;
    GfxBlend      batchBlend;
    GfxSpace      batchSpace;
    int           batchFull;            /* the buffer's room for this frame is spent */
    /* the CRT pass ([video] crt_curvature, crt_ntsc): the picture's space drawn into `off`, then
     * through the crt program onto the screen (resolve) before anything in the window's space */
    float         crtCurve, crtNtsc;    /* 0..1 */
    sg_pipeline   crt;
    sg_image      off;
    sg_view       offTex, offAtt;
    int           offW, offH;
    int           toOff;                /* this frame's picture space is going into `off` */
    GpuTex       *backdrop;             /* the menus' true-colour backdrop (GpuFrame.backdrop) */
    uint32_t      backdropSerial;
    uint32_t      frames;
} G;

void GpuPresent_SetCrt(int curvature, int ntsc)
{
    G.crtCurve = (float)(curvature < 0 ? 0 : curvature > 100 ? 100 : curvature) / 100.0f;
    G.crtNtsc = (float)(ntsc < 0 ? 0 : ntsc > 100 ? 100 : ntsc) / 100.0f;
}

static void sokol_log(const char *tag, uint32_t level, uint32_t item, const char *msg, uint32_t line,
                      const char *file, void *user)
{
    (void)tag; (void)file; (void)user;
    if (level <= 2)                                    /* panic, error, warning */
        fprintf(stderr, "[gpu] sokol %s %u: %s (line %u)\n", level <= 1 ? "error" : "warning",
                (unsigned)item, msg ? msg : "", (unsigned)line);
}

int32_t GpuPresent_Compiled(void) { return 1; }
int32_t GpuPresent_Active(void) { return G.up; }
int32_t GpuPresent_Vsync(void) { return G.vsync; }

void GpuPresent_ConfigureWindow(void)
{
#if defined(SOKOL_GLES3)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);      /* sokol-shdc's GLSL 410 */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
}

uint32_t GpuPresent_WindowFlags(void) { return SDL_WINDOW_OPENGL; }

static sg_pipeline make_pipeline(sg_shader shd, int strip, GfxBlend blend, int premultiplied, int vertices,
                                 const char *label)
{
    sg_pipeline_desc d;
    memset(&d, 0, sizeof d);
    d.shader = shd;
    d.primitive_type = strip ? SG_PRIMITIVETYPE_TRIANGLE_STRIP : SG_PRIMITIVETYPE_TRIANGLES;
    d.cull_mode = SG_CULLMODE_NONE;
    d.depth.pixel_format = SG_PIXELFORMAT_NONE;
    d.label = label;
    if (vertices) {
        d.layout.buffers[0].stride = (int)sizeof(GfxVertex);
        d.layout.attrs[ATTR_batch_pos].format = SG_VERTEXFORMAT_FLOAT2;
        d.layout.attrs[ATTR_batch_pos].offset = (int)offsetof(GfxVertex, position);
        d.layout.attrs[ATTR_batch_color0].format = SG_VERTEXFORMAT_UBYTE4N;
        d.layout.attrs[ATTR_batch_color0].offset = (int)offsetof(GfxVertex, color);
        d.layout.attrs[ATTR_batch_texcoord0].format = SG_VERTEXFORMAT_FLOAT2;
        d.layout.attrs[ATTR_batch_texcoord0].offset = (int)offsetof(GfxVertex, tex_coord);
    }
    sg_blend_state *b = &d.colors[0].blend;
    if (premultiplied) {                                /* the HUD: colour already times alpha */
        b->enabled = true;
        b->src_factor_rgb = SG_BLENDFACTOR_ONE;
        b->dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        b->src_factor_alpha = SG_BLENDFACTOR_ONE;
        b->dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    } else if (blend == GFX_BLEND_ALPHA) {              /* SDL_BLENDMODE_BLEND */
        b->enabled = true;
        b->src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        b->dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        b->src_factor_alpha = SG_BLENDFACTOR_ONE;
        b->dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    } else if (blend == GFX_BLEND_MOD) {                /* SDL_BLENDMODE_MOD: dst = src * dst */
        b->enabled = true;
        b->src_factor_rgb = SG_BLENDFACTOR_DST_COLOR;
        b->dst_factor_rgb = SG_BLENDFACTOR_ZERO;
        b->src_factor_alpha = SG_BLENDFACTOR_ZERO;
        b->dst_factor_alpha = SG_BLENDFACTOR_ONE;
    }
    return sg_make_pipeline(&d);
}

static sg_image make_image(int w, int h, sg_pixel_format fmt, const char *label)
{
    sg_image_desc d;
    memset(&d, 0, sizeof d);
    d.width = w;
    d.height = h;
    d.pixel_format = fmt;
    d.usage.stream_update = true;
    d.label = label;
    return sg_make_image(&d);
}

static sg_view make_view(sg_image img)
{
    sg_view_desc d;
    memset(&d, 0, sizeof d);
    d.texture.image = img;
    return sg_make_view(&d);
}

GpuTex *GpuGfx_CreateTexture(int w, int h, int linear);
void    GpuGfx_UpdateTexture(GpuTex *t, const void *rgba, int pitch);

int32_t GpuPresent_Init(SDL_Window *window, int vsync)
{
    memset(&G, 0, sizeof G);
    G.window = window;
    G.ctx = SDL_GL_CreateContext(window);
    if (!G.ctx) {
        fprintf(stderr, "[gpu] no OpenGL context (%s): SDL's renderer instead\n", SDL_GetError());
        return -1;
    }
    SDL_GL_MakeCurrent(window, G.ctx);
    if (SokolGl_Load() != 0) {
        fprintf(stderr, "[gpu] the driver lacks what the presenter needs: SDL's renderer instead\n");
        SDL_GL_DeleteContext(G.ctx);
        G.ctx = NULL;
        return -1;
    }
    if (vsync) {
        if (SDL_GL_SetSwapInterval(1) != 0)
            fprintf(stderr, "[gpu] no vsync: %s\n", SDL_GetError());
    } else {
        SDL_GL_SetSwapInterval(0);
    }
    G.vsync = SDL_GL_GetSwapInterval() != 0;

    sg_desc desc;
    memset(&desc, 0, sizeof desc);
    desc.logger.func = sokol_log;
    desc.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    desc.environment.defaults.depth_format = SG_PIXELFORMAT_NONE;
    desc.environment.defaults.sample_count = 1;
    desc.buffer_pool_size = 16;
    desc.image_pool_size = 256;             /* (a flag a team: mod_flags.c) */
    desc.view_pool_size = 256;
    sg_setup(&desc);
    if (!sg_isvalid()) {
        fprintf(stderr, "[gpu] sokol did not start: SDL's renderer instead\n");
        SDL_GL_DeleteContext(G.ctx);
        G.ctx = NULL;
        return -1;
    }

    const sg_backend be = sg_query_backend();
    const sg_shader present = sg_make_shader(present_shader_desc(be));
    const sg_shader batch = sg_make_shader(batch_shader_desc(be));
    G.crt = make_pipeline(sg_make_shader(crt_shader_desc(be)), 1, GFX_BLEND_NONE, 0, 0, "crt");
    G.world = make_pipeline(present, 1, GFX_BLEND_NONE, 0, 0, "present-world");
    G.hud = make_pipeline(present, 1, GFX_BLEND_NONE, 1, 0, "present-hud");
    for (int b = 0; b < 3; b++)
        G.batch[b] = make_pipeline(batch, 0, (GfxBlend)b, 0, 1, "batch");

    sg_sampler_desc s;
    memset(&s, 0, sizeof s);
    s.min_filter = s.mag_filter = SG_FILTER_NEAREST;
    s.wrap_u = s.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    G.nearest = sg_make_sampler(&s);
    s.min_filter = s.mag_filter = SG_FILTER_LINEAR;
    G.linear = sg_make_sampler(&s);

    G.palette = make_image(256, 1, SG_PIXELFORMAT_RGBA8, "palette");
    G.paletteView = make_view(G.palette);
    G.palStage = (uint32_t *)calloc(256, 4);

    sg_buffer_desc bd;
    memset(&bd, 0, sizeof bd);
    bd.size = sizeof G.verts;
    bd.usage.vertex_buffer = true;
    bd.usage.stream_update = true;
    bd.label = "batch";
    G.vbuf = sg_make_buffer(&bd);

    int ok = sg_query_pipeline_state(G.world) == SG_RESOURCESTATE_VALID &&
             sg_query_pipeline_state(G.hud) == SG_RESOURCESTATE_VALID &&
             sg_query_pipeline_state(G.batch[1]) == SG_RESOURCESTATE_VALID &&
             sg_query_view_state(G.paletteView) == SG_RESOURCESTATE_VALID && G.palStage;
    if (!ok) {
        fprintf(stderr, "[gpu] the presenter's shaders did not build: SDL's renderer instead\n");
        sg_shutdown();
        SDL_GL_DeleteContext(G.ctx);
        G.ctx = NULL;
        return -1;
    }
    G.up = 1;
    static const uint8_t kWhite[4] = { 255, 255, 255, 255 };
    G.white = GpuGfx_CreateTexture(1, 1, 0);
    G.batchBlend = GFX_BLEND_ALPHA;
    G.batchSpace = GFX_SPACE_PICTURE;
    fprintf(stderr, "[gpu] presenter up: %s, %s, vsync %s\n",
#if defined(SOKOL_GLES3)
            "OpenGL ES 3 / WebGL2",
#else
            "OpenGL 4.1 core",
#endif
            SokolGl_Renderer(), G.vsync ? "on" : "off");
    GpuGfx_UpdateTexture(G.white, kWhite, 4);
    return 0;
}

void GpuPresent_Shutdown(void)
{
    if (!G.up)
        return;
    sg_shutdown();
    if (G.ctx)
        SDL_GL_DeleteContext(G.ctx);
    free(G.stage);
    free(G.palStage);
    G.up = 0;
}

void GpuPresent_OutputSize(int *w, int *h)
{
    SDL_GL_GetDrawableSize(G.window, w, h);
}

void GpuPresent_PictureRect(float *x, float *y, float *w, float *h)
{
    *x = G.picX; *y = G.picY; *w = G.picW; *h = G.picH;
}

/* Where the picture lands: SDL2's logical size, or the whole window (stretch). */
static void picture_rect(int stretch, int lw, int lh)
{
    if (stretch || lw <= 0 || lh <= 0) {
        G.picX = G.picY = 0.0f;
        G.picW = (float)G.outW;
        G.picH = (float)G.outH;
        return;
    }
    const float want = (float)lw / (float)lh, real = (float)G.outW / (float)G.outH;
    if (fabsf(want - real) < 0.0001f) {
        G.picX = G.picY = 0.0f;
        G.picW = (float)G.outW;
        G.picH = (float)G.outH;
    } else if (want > real) {               /* wider than the window: bars above and below */
        const float s = (float)G.outW / (float)lw;
        G.picW = (float)G.outW;
        G.picH = floorf((float)lh * s);
        G.picX = 0.0f;
        G.picY = floorf(((float)G.outH - G.picH) / 2.0f);
    } else {                                /* narrower: bars at the sides */
        const float s = (float)G.outH / (float)lh;
        G.picH = (float)G.outH;
        G.picW = floorf((float)lw * s);
        G.picY = 0.0f;
        G.picX = floorf(((float)G.outW - G.picW) / 2.0f);
    }
}

/* The frame's images, (re)made at its size. */
static int ensure_images(int w, int h)
{
    if (G.imgW == w && G.imgH == h && G.index.id)
        return 1;
    if (G.index.id) {
        sg_destroy_view(G.indexView); sg_destroy_image(G.index);
        sg_destroy_view(G.argbView);  sg_destroy_image(G.argb);
        sg_destroy_view(G.hudView);   sg_destroy_image(G.hudImage);
    }
    free(G.stage);
    G.stage = (uint8_t *)malloc((size_t)w * h * 4);
    G.index = make_image(w, h, SG_PIXELFORMAT_R8, "frame");
    G.argb = make_image(w, h, SG_PIXELFORMAT_RGBA8, "frame-rgba");
    G.hudImage = make_image(w, h, SG_PIXELFORMAT_R8, "hud");
    G.indexView = make_view(G.index);
    G.argbView = make_view(G.argb);
    G.hudView = make_view(G.hudImage);
    G.imgW = w;
    G.imgH = h;
    return G.stage != NULL;
}

static void upload_r8(sg_image img, const uint8_t *src, int pitch, int w, int h)
{
    for (int y = 0; y < h; y++)
        memcpy(G.stage + (size_t)y * w, src + (size_t)y * pitch, (size_t)w);
    sg_image_data d;
    memset(&d, 0, sizeof d);
    d.mip_levels[0].ptr = G.stage;
    d.mip_levels[0].size = (size_t)w * h;
    sg_update_image(img, &d);
}

static float s_key;     /* draw_picture's: the backdrop's key + 1, 0 none */
static void draw_picture(sg_pipeline pip, sg_view tex, const float src[4], float n, int hud, int rgba)
{
    vs_present_params_t vp;
    fs_present_params_t fp;
    vp.dst[0] = -1.0f; vp.dst[1] = 1.0f; vp.dst[2] = 1.0f; vp.dst[3] = -1.0f;
    memcpy(vp.src, src, sizeof vp.src);
    memset(&fp, 0, sizeof fp);
    fp.size[0] = (float)G.srcW;
    fp.size[1] = (float)G.srcH;
    fp.mode[0] = n;
    fp.mode[1] = hud ? 1.0f : 0.0f;
    fp.mode[2] = rgba ? 1.0f : 0.0f;
    fp.mode[3] = hud ? 0.0f : s_key;
    sg_bindings b;
    memset(&b, 0, sizeof b);
    b.views[VIEW_idx_tex] = tex;
    b.views[VIEW_pal_tex] = G.paletteView;
    b.samplers[SMP_idx_smp] = G.nearest;
    b.samplers[SMP_pal_smp] = G.nearest;
    sg_apply_pipeline(pip);
    sg_apply_bindings(&b);
    sg_apply_uniforms(UB_vs_present_params, &(sg_range){ &vp, sizeof vp });
    sg_apply_uniforms(UB_fs_present_params, &(sg_range){ &fp, sizeof fp });
    sg_draw(0, 4, 1);
}

/* The CRT pass's image, the picture's size on screen. */
static int ensure_offscreen(int w, int h)
{
    if (G.off.id && G.offW == w && G.offH == h)
        return 1;
    if (G.off.id) {
        sg_destroy_view(G.offTex);
        sg_destroy_view(G.offAtt);
        sg_destroy_image(G.off);
    }
    sg_image_desc d;
    memset(&d, 0, sizeof d);
    d.width = w;
    d.height = h;
    d.pixel_format = SG_PIXELFORMAT_RGBA8;
    d.usage.color_attachment = true;
    d.label = "crt-picture";
    G.off = sg_make_image(&d);
    sg_view_desc v;
    memset(&v, 0, sizeof v);
    v.texture.image = G.off;
    G.offTex = sg_make_view(&v);
    memset(&v, 0, sizeof v);
    v.color_attachment.image = G.off;
    G.offAtt = sg_make_view(&v);
    G.offW = w;
    G.offH = h;
    return sg_query_view_state(G.offAtt) == SG_RESOURCESTATE_VALID;
}

static void begin_screen_pass(const float clear[3])
{
    sg_pass pass;
    memset(&pass, 0, sizeof pass);
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value.r = clear[0];
    pass.action.colors[0].clear_value.g = clear[1];
    pass.action.colors[0].clear_value.b = clear[2];
    pass.action.colors[0].clear_value.a = 1.0f;
    pass.swapchain.width = G.outW;
    pass.swapchain.height = G.outH;
    pass.swapchain.sample_count = 1;
    pass.swapchain.color_format = SG_PIXELFORMAT_RGBA8;
    pass.swapchain.depth_format = SG_PIXELFORMAT_NONE;
    pass.swapchain.gl.framebuffer = 0;
    sg_begin_pass(&pass);
}

static float s_clear[3];
static void batch_flush(void);

/* The picture's space is done: onto the screen through the crt program, and from here every
 * draw is the screen's. */
static void crt_resolve(void)
{
    if (!G.toOff)
        return;
    batch_flush();
    sg_end_pass();
    G.toOff = 0;
    begin_screen_pass(s_clear);
    vs_crt_params_t vp;
    fs_crt_params_t fp;
    vp.dst[0] = -1.0f; vp.dst[1] = 1.0f; vp.dst[2] = 1.0f; vp.dst[3] = -1.0f;
    memset(&fp, 0, sizeof fp);
    fp.src[0] = (float)G.srcW;
    fp.src[1] = (float)G.srcH;
    fp.src[2] = (float)(G.frames & 1);
    fp.crt[0] = G.crtCurve;
    fp.crt[1] = G.crtNtsc;
    fp.crt[2] = 1.0f;                   /* (GL: the image's first row is the picture's bottom) */
    sg_bindings b;
    memset(&b, 0, sizeof b);
    b.views[VIEW_pic_tex] = G.offTex;
    b.samplers[SMP_pic_smp] = G.linear;
    sg_apply_viewportf(G.picX, G.picY, G.picW, G.picH, true);
    sg_apply_pipeline(G.crt);
    sg_apply_bindings(&b);
    sg_apply_uniforms(UB_vs_crt_params, &(sg_range){ &vp, sizeof vp });
    sg_apply_uniforms(UB_fs_crt_params, &(sg_range){ &fp, sizeof fp });
    sg_draw(0, 4, 1);
}

int32_t GpuPresent_Begin(const GpuFrame *f, const uint8_t clear[3])
{
    if (!G.up)
        return 0;
    GpuPresent_OutputSize(&G.outW, &G.outH);
    if (G.outW <= 0 || G.outH <= 0)
        return 0;

    if (f) {
        G.srcW = f->w;
        G.srcH = f->h;
        if (!ensure_images(f->w, f->h))
            return 0;
        /* The images before the pass (one update each a frame, the whole of each). */
        for (int i = 0; i < 256; i++) {
            const SDL_Color c = f->palette[i];
            G.palStage[i] = (uint32_t)c.r | ((uint32_t)c.g << 8) | ((uint32_t)c.b << 16) | 0xFF000000u;
        }
        {
            sg_image_data d;
            memset(&d, 0, sizeof d);
            d.mip_levels[0].ptr = G.palStage;
            d.mip_levels[0].size = 256 * 4;
            sg_update_image(G.palette, &d);
        }
        if (f->argb) {
            sg_image_data d;
            memset(&d, 0, sizeof d);
            d.mip_levels[0].ptr = f->argb;
            d.mip_levels[0].size = (size_t)f->w * f->h * 4;
            sg_update_image(G.argb, &d);
        } else if (f->index) {
            upload_r8(G.index, f->index, f->indexPitch, f->w, f->h);
        }
        if (f->hud)
            upload_r8(G.hudImage, f->hud, f->hudPitch, f->w, f->h);
    }

    s_clear[0] = clear ? clear[0] / 255.0f : 0.0f;
    s_clear[1] = clear ? clear[1] / 255.0f : 0.0f;
    s_clear[2] = clear ? clear[2] / 255.0f : 0.0f;
    picture_rect(f ? f->stretch : 0, f ? f->w : 0, f ? f->h : 0);
    G.frames++;
    G.toOff = f && (G.crtCurve > 0.0f || G.crtNtsc > 0.0f) && G.picW >= 1.0f && G.picH >= 1.0f &&
              ensure_offscreen((int)G.picW, (int)G.picH);
    if (G.toOff) {                      /* the picture's space into `off` (crt_resolve) */
        sg_pass pass;
        memset(&pass, 0, sizeof pass);
        pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        pass.action.colors[0].clear_value.a = 1.0f;
        pass.attachments.colors[0] = G.offAtt;
        sg_begin_pass(&pass);
    } else {
        begin_screen_pass(s_clear);
    }
    G.inPass = 1;
    G.nverts = 0;
    G.batchFull = 0;

    if (!f) {
        G.srcW = G.outW;     /* (a frame with no picture: the overlays' PICTURE space is the window) */
        G.srcH = G.outH;
        return 1;
    }

    /* As scale_pass_factor and present_frame_copy (platform_sdl.c) choose them. */
    const float sx = G.picW / (float)f->w, sy = G.picH / (float)f->h;
    float n = 0.0f;
    if (f->smooth && sx >= 1.5f && sy >= 1.5f &&
        !(fabsf(sx - floorf(sx + 0.5f)) < 0.001f && fabsf(sy - floorf(sy + 0.5f)) < 0.001f)) {
        const float s = sx > sy ? sx : sy;
        n = floorf(s + 0.5f);
        if (n < 2.0f) n = 2.0f;
        if (n > 8.0f) n = 8.0f;
    }
    float qx = 0.0f, qy = 0.0f;
    if (f->subpixel && (f->fracX || f->fracY)) {
        const float fx = (float)f->fracX / 65536.0f, fy = (float)f->fracY / 65536.0f;
        if (n > 0.0f) {
            qx = floorf(fx * n + 0.5f) / n;
            qy = floorf(fy * n + 0.5f) / n;
        } else {
            if (sx >= 2.0f) qx = floorf(fx * sx + 0.5f) / sx;
            if (sy >= 2.0f) qy = floorf(fy * sy + 0.5f) / sy;
        }
    }
    s_key = 0.0f;
    if (f->backdrop && !f->argb) {
        /* The menus' backdrop under the picture: covering the window (or, into the CRT pass's
         * image, the picture), cropped to its aspect, centred. */
        if (!G.backdrop || G.backdropSerial != f->backdropSerial ||
            G.backdrop->w != f->backdropW || G.backdrop->h != f->backdropH) {
            if (G.backdrop)
                GpuGfx_DestroyTexture(G.backdrop);
            G.backdrop = GpuGfx_CreateTexture(f->backdropW, f->backdropH, 1);
            if (G.backdrop)
                GpuGfx_UpdateTexture(G.backdrop, f->backdrop, f->backdropW * 4);
            G.backdropSerial = f->backdropSerial;
        }
        if (G.backdrop) {
            const GfxSpace space = G.toOff ? GFX_SPACE_PICTURE : GFX_SPACE_OUTPUT;
            const float W = G.toOff ? (float)f->w : (float)G.outW, H = G.toOff ? (float)f->h : (float)G.outH;
            const float aw = G.toOff ? G.picW : (float)G.outW, ah = G.toOff ? G.picH : (float)G.outH;
            const float scale = fmaxf(aw / (float)f->backdropW, ah / (float)f->backdropH);
            const float cu = aw / scale / (float)f->backdropW, cv = ah / scale / (float)f->backdropH;
            const float u0 = (1.0f - cu) / 2.0f, v0 = (1.0f - cv) / 2.0f, u1 = u0 + cu, v1 = v0 + cv;
            GfxVertex v[6];
            const float px[6] = { 0, W, 0, W, W, 0 }, py[6] = { 0, 0, H, 0, H, H };
            const float tu[6] = { u0, u1, u0, u1, u1, u0 }, tv[6] = { v0, v0, v1, v0, v1, v1 };
            for (int i = 0; i < 6; i++) {
                memset(&v[i], 0, sizeof v[i]);
                v[i].position.x = px[i];
                v[i].position.y = py[i];
                v[i].color.r = v[i].color.g = v[i].color.b = v[i].color.a = 255;
                v[i].tex_coord.x = tu[i];
                v[i].tex_coord.y = tv[i];
            }
            GpuGfx_SetState(G.backdrop, GFX_BLEND_NONE, space);
            GpuGfx_Push(v, 6);
            batch_flush();
            s_key = (float)f->backdropKey + 1.0f;
        }
    }
    if (G.toOff)
        sg_apply_viewport(0, 0, (int)G.picW, (int)G.picH, true);
    else
        sg_apply_viewportf(G.picX, G.picY, G.picW, G.picH, true);
    const float world[4] = { qx, qy, qx + (float)f->w, qy + (float)f->h };
    /* (over a backdrop the picture blends: its key colour is see-through -- the HUD's pipeline) */
    draw_picture(s_key > 0.0f ? G.hud : G.world, f->argb ? G.argbView : G.indexView, world, n, 0,
                 f->argb != NULL);
    if (f->hud) {
        const float flat[4] = { 0.0f, 0.0f, (float)f->w, (float)f->h };
        draw_picture(G.hud, G.hudView, flat, n, 1, 0);
    }
    return 1;
}

/* ---- the overlays: gfx.h's GPU side ---------------------------------- */

static void batch_flush(void)
{
    if (!G.nverts || !G.inPass)
        return;
    const size_t bytes = (size_t)G.nverts * sizeof(GfxVertex);
    if (G.batchFull || sg_query_buffer_will_overflow(G.vbuf, bytes)) {
        G.batchFull = 1;            /* (more than a frame's room: the rest is not drawn) */
        G.nverts = 0;
        return;
    }
    const int off = sg_append_buffer(G.vbuf, &(sg_range){ G.verts, bytes });
    vs_batch_params_t vp;
    memset(&vp, 0, sizeof vp);
    if (G.batchSpace == GFX_SPACE_OUTPUT && G.toOff) {
        /* the window's space: the picture's is done -- through the CRT first. This batch's
         * vertices are in the buffer already (appended above); the resolve must not flush them. */
        const int n = G.nverts;
        G.nverts = 0;
        crt_resolve();
        G.nverts = n;
    }
    if (G.batchSpace == GFX_SPACE_OUTPUT) {
        sg_apply_viewport(0, 0, G.outW, G.outH, true);
        vp.view[0] = (float)G.outW;
        vp.view[1] = (float)G.outH;
    } else {
        if (G.toOff)
            sg_apply_viewport(0, 0, (int)G.picW, (int)G.picH, true);
        else
            sg_apply_viewportf(G.picX, G.picY, G.picW, G.picH, true);
        vp.view[0] = (float)G.srcW;
        vp.view[1] = (float)G.srcH;
    }
    GpuTex *t = G.batchTex ? G.batchTex : G.white;
    sg_bindings b;
    memset(&b, 0, sizeof b);
    b.vertex_buffers[0] = G.vbuf;
    b.vertex_buffer_offsets[0] = off;
    b.views[VIEW_tex] = t->view;
    b.samplers[SMP_smp] = t->linear ? G.linear : G.nearest;
    sg_apply_pipeline(G.batch[G.batchBlend]);
    sg_apply_bindings(&b);
    sg_apply_uniforms(UB_vs_batch_params, &(sg_range){ &vp, sizeof vp });
    sg_draw(0, G.nverts, 1);
    G.nverts = 0;
}

void GpuGfx_SetState(GpuTex *tex, GfxBlend blend, GfxSpace space)
{
    if (tex != G.batchTex || blend != G.batchBlend || space != G.batchSpace) {
        batch_flush();
        G.batchTex = tex;
        G.batchBlend = blend;
        G.batchSpace = space;
    }
}

void GpuGfx_Push(const GfxVertex *v, int n)
{
    while (n > 0) {
        int room = BATCH_VERTS - G.nverts;
        room -= room % 3;
        if (room <= 0) {
            batch_flush();
            continue;
        }
        const int k = n < room ? n : room;
        memcpy(G.verts + G.nverts, v, (size_t)k * sizeof *v);
        G.nverts += k;
        v += k;
        n -= k;
    }
}

void GpuGfx_SpaceSize(GfxSpace space, int *w, int *h)
{
    if (space == GFX_SPACE_OUTPUT) {
        *w = G.outW;
        *h = G.outH;
    } else {
        *w = G.srcW;
        *h = G.srcH;
    }
}

GpuTex *GpuGfx_CreateTexture(int w, int h, int linear)
{
    GpuTex *t = (GpuTex *)calloc(1, sizeof *t);
    if (!t)
        return NULL;
    sg_image_desc d;
    memset(&d, 0, sizeof d);
    d.width = w;
    d.height = h;
    d.pixel_format = SG_PIXELFORMAT_RGBA8;
    d.usage.dynamic_update = true;
    t->image = sg_make_image(&d);
    t->view = make_view(t->image);
    t->w = w;
    t->h = h;
    t->linear = linear;
    return t;
}

void GpuGfx_UpdateTexture(GpuTex *t, const void *rgba, int pitch)
{
    uint8_t *packed = NULL;
    const void *p = rgba;
    if (pitch != t->w * 4) {                        /* (sokol takes rows packed) */
        packed = (uint8_t *)malloc((size_t)t->w * t->h * 4);
        if (!packed)
            return;
        for (int y = 0; y < t->h; y++)
            memcpy(packed + (size_t)y * t->w * 4, (const uint8_t *)rgba + (size_t)y * pitch, (size_t)t->w * 4);
        p = packed;
    }
    sg_image_data d;
    memset(&d, 0, sizeof d);
    d.mip_levels[0].ptr = p;
    d.mip_levels[0].size = (size_t)t->w * t->h * 4;
    sg_update_image(t->image, &d);
    free(packed);
}

void GpuGfx_DestroyTexture(GpuTex *t)
{
    if (!t)
        return;
    if (G.batchTex == t)
        batch_flush(), G.batchTex = NULL;
    sg_destroy_view(t->view);
    sg_destroy_image(t->image);
    free(t);
}

void GpuGfx_TextureSize(const GpuTex *t, int *w, int *h)
{
    *w = t->w;
    *h = t->h;
}

void GpuPresent_EndPass(void)
{
    if (!G.inPass)
        return;
    batch_flush();
    crt_resolve();                      /* (nothing in the window's space this frame) */
    sg_end_pass();
    G.inPass = 0;
}

int32_t GpuPresent_ReadPixels(uint8_t *rgba, int w, int h)
{
    if (!G.up || w != G.outW || h != G.outH)
        return -1;
    GpuPresent_EndPass();
    SokolGl_ReadPixels(w, h, rgba);
    return 0;
}

void GpuPresent_End(void)
{
    if (!G.up)
        return;
    GpuPresent_EndPass();
    sg_commit();
    SDL_GL_SwapWindow(G.window);
}

#else /* !SWOS_GPU */

int32_t GpuPresent_Compiled(void) { return 0; }
void    GpuPresent_ConfigureWindow(void) {}
uint32_t GpuPresent_WindowFlags(void) { return 0; }
int32_t GpuPresent_Init(SDL_Window *window, int vsync) { (void)window; (void)vsync; return -1; }
void    GpuPresent_Shutdown(void) {}
int32_t GpuPresent_Active(void) { return 0; }
int32_t GpuPresent_Vsync(void) { return 0; }
int32_t GpuPresent_Begin(const GpuFrame *f, const uint8_t clear[3]) { (void)f; (void)clear; return 0; }
void    GpuPresent_End(void) {}
void    GpuPresent_EndPass(void) {}
void    GpuPresent_SetCrt(int curvature, int ntsc) { (void)curvature; (void)ntsc; }
void    GpuPresent_OutputSize(int *w, int *h) { *w = *h = 0; }
void    GpuPresent_PictureRect(float *x, float *y, float *w, float *h) { *x = *y = *w = *h = 0; }
int32_t GpuPresent_ReadPixels(uint8_t *rgba, int w, int h) { (void)rgba; (void)w; (void)h; return -1; }
typedef struct GpuTex GpuTex;
struct GpuTex { int unused; };
void GpuGfx_SetState(GpuTex *tex, GfxBlend blend, GfxSpace space) { (void)tex; (void)blend; (void)space; }
void GpuGfx_Push(const GfxVertex *v, int n) { (void)v; (void)n; }
void GpuGfx_SpaceSize(GfxSpace space, int *w, int *h) { (void)space; *w = *h = 0; }
GpuTex *GpuGfx_CreateTexture(int w, int h, int linear) { (void)w; (void)h; (void)linear; return NULL; }
void GpuGfx_UpdateTexture(GpuTex *t, const void *rgba, int pitch) { (void)t; (void)rgba; (void)pitch; }
void GpuGfx_DestroyTexture(GpuTex *t) { (void)t; }
void GpuGfx_TextureSize(const GpuTex *t, int *w, int *h) { (void)t; *w = *h = 0; }

#endif /* SWOS_GPU */
#endif /* SWOS_SDL_BUILD */
