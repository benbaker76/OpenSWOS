/*
 * gpu_slang.c -- a slang preset's passes on the GPU presenter (include/gpu_slang.h; src/slang.c
 * compiles them; docs/RENDERING.md, "Shaders from files").
 *
 * Each frame: the presenter draws the game's picture -- with its HUD and the mods' picture-space
 * overlays -- into Original at the game's own size (GpuSlang_BeginOriginal); GpuSlang_Run then
 * draws every pass into its own image (sized by its scale type), the last onto the screen in the
 * picture's rectangle. A pass is fed what it reads -- Original, Source, earlier passes' output,
 * last frame's (feedback), past frames (history), the preset's textures -- and its uniforms by
 * meaning: MVP, the sizes, FrameCount, the parameters.
 *
 * ROWS: a slang shader takes an image's first row to be its top (Vulkan's, Metal's and D3D's
 * way). GL keeps a render target bottom up, so on GL every pass into an image is drawn upside
 * down (its MVP flips y), and so is Original (gpu_present.c); only the last, onto the screen, is
 * drawn upright. GL's uniform blocks are the renderer's own buffers (SokolGl_*: slang.h).
 */
#include "gpu_slang.h"

#if defined(SWOS_SDL_BUILD) && defined(SWOS_SHADERCC)
#include "sokol_config.h"
#endif

#if defined(SWOS_SDL_BUILD) && defined(SWOS_SHADERCC) && SWOS_GPU

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slang.h"
#include "../third_party/stb_image/stb_image.h"
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
#define SLANG_GL 1
extern uint32_t SokolGl_NewUniformBuffer(void);
extern void     SokolGl_DeleteUniformBuffer(uint32_t b);
extern void     SokolGl_BindUniformBlocks(uint32_t prog, const char *ubo, const char *push);
extern void     SokolGl_SetUniformBuffer(uint32_t point, uint32_t b, const void *data, uint32_t size);
extern void     SokolGl_FramebufferSrgb(int on);
extern void     SokolGl_GenerateMipmaps(uint32_t tex);
#else
#define SLANG_GL 0
#include "gpu_native.h"         /* (GpuNative_GenerateMipmaps) */
#endif

typedef struct {
    sg_image        img;
    sg_view         tex, att;
    int             w, h, mips;
    sg_pixel_format fmt;
} Rt;

static int mip_count(int w, int h)
{
    int n = 1;
    while ((w > 1 || h > 1) && n < 16) {
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
        n++;
    }
    return n;
}

typedef struct {
    SlangPassCode code;
    sg_shader     shd;
    sg_pipeline   pip, pipScreen;   /* into its image / onto the screen */
    sg_pixel_format fmt;
    Rt            out[2];           /* [frame & 1] with feedback, else [0] */
    int           feedback;         /* a pass reads this one's last frame */
    uint32_t      ubo, push;        /* GL: the blocks' buffers */
    uint8_t       uboData[4096], pushData[4096];
} Pass;

/* Parameter values a mod set (GpuSlang_SetParameter), kept across loads: applied over the
 * preset's own when one is loaded, and at once to the one loaded. */
static struct { char name[64]; float value; } s_over[32];
static int s_nover;

static void apply_overrides(void);

static struct {
    int         on;
    SlangPreset preset;
    Pass        pass[SLANG_MAX_PASSES];
    int         npasses;
    Rt          orig[SLANG_MAX_HISTORY];    /* [frame % history]: Original, then its history */
    int         history;                    /* frames kept: 1 + the deepest OriginalHistoryN */
    sg_image    lut[SLANG_MAX_LUTS];
    sg_view     lutView[SLANG_MAX_LUTS];
    int         lutW[SLANG_MAX_LUTS], lutH[SLANG_MAX_LUTS];
    sg_buffer   quad;
    sg_sampler  smp[2][4][2];               /* [linear][SlangWrap][mipmapped] */
    uint32_t    frame;
    int         origW, origH;
} S;

static sg_pixel_format format_of(const SlangPassDesc *d, uint8_t pragma)
{
    if (d->floatFb)
        return SG_PIXELFORMAT_RGBA16F;
    if (d->srgbFb)
        return SG_PIXELFORMAT_SRGB8A8;
    switch (pragma) {
    case SLANG_FORMAT_SRGBA8:  return SG_PIXELFORMAT_SRGB8A8;
    case SLANG_FORMAT_RGBA16F: return SG_PIXELFORMAT_RGBA16F;
    case SLANG_FORMAT_RGBA32F: return SG_PIXELFORMAT_RGBA32F;
    case SLANG_FORMAT_RGB10A2: return SG_PIXELFORMAT_RGB10A2;
    case SLANG_FORMAT_R8:      return SG_PIXELFORMAT_R8;
    case SLANG_FORMAT_RG8:     return SG_PIXELFORMAT_RG8;
    case SLANG_FORMAT_R16F:    return SG_PIXELFORMAT_R16F;
    case SLANG_FORMAT_RG16F:   return SG_PIXELFORMAT_RG16F;
    case SLANG_FORMAT_R32F:    return SG_PIXELFORMAT_R32F;
    case SLANG_FORMAT_RG32F:   return SG_PIXELFORMAT_RG32F;
    default:                   return SG_PIXELFORMAT_RGBA8;
    }
}

static void rt_free(Rt *r)
{
    if (r->img.id) {
        sg_destroy_view(r->tex);
        sg_destroy_view(r->att);
        sg_destroy_image(r->img);
    }
    memset(r, 0, sizeof *r);
}

static int rt_ensure(Rt *r, int w, int h, sg_pixel_format fmt, int mipmapped)
{
    sg_image_desc d;
    sg_view_desc v;
    const int mips = mipmapped ? mip_count(w, h) : 1;
    if (r->img.id && r->w == w && r->h == h && r->fmt == fmt && r->mips == mips)
        return 1;
    rt_free(r);
    memset(&d, 0, sizeof d);
    d.width = w;
    d.height = h;
    d.pixel_format = fmt;
    d.num_mipmaps = mips;
    d.usage.color_attachment = true;
    d.label = "slang-pass";
    r->img = sg_make_image(&d);
    memset(&v, 0, sizeof v);
    v.texture.image = r->img;
    r->tex = sg_make_view(&v);
    memset(&v, 0, sizeof v);
    v.color_attachment.image = r->img;
    r->att = sg_make_view(&v);
    r->w = w;
    r->h = h;
    r->mips = mips;
    r->fmt = fmt;
    return sg_query_view_state(r->att) == SG_RESOURCESTATE_VALID;
}

static sg_sampler sampler(int linear, int wrap, int mipmapped)
{
    sg_sampler *s = &S.smp[linear ? 1 : 0][wrap & 3][mipmapped ? 1 : 0];
    if (!s->id) {
        static const sg_wrap k_wrap[4] = { SG_WRAP_CLAMP_TO_BORDER, SG_WRAP_CLAMP_TO_EDGE, SG_WRAP_REPEAT,
                                           SG_WRAP_MIRRORED_REPEAT };
        sg_sampler_desc d;
        memset(&d, 0, sizeof d);
        d.min_filter = d.mag_filter = linear ? SG_FILTER_LINEAR : SG_FILTER_NEAREST;
        d.wrap_u = d.wrap_v = k_wrap[wrap & 3];
        if (mipmapped)
            d.mipmap_filter = linear ? SG_FILTER_LINEAR : SG_FILTER_NEAREST;
        d.border_color = SG_BORDERCOLOR_TRANSPARENT_BLACK;
        *s = sg_make_sampler(&d);
    }
    return *s;
}

static sg_pipeline make_pipeline(sg_shader shd, sg_pixel_format fmt, int screen)
{
    sg_pipeline_desc d;
    memset(&d, 0, sizeof d);
    d.shader = shd;
    d.primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP;
    d.cull_mode = SG_CULLMODE_NONE;
    d.layout.buffers[0].stride = 24;
    d.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT4;
    d.layout.attrs[0].offset = 0;
    d.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
    d.layout.attrs[1].offset = 16;
    d.depth.pixel_format = SG_PIXELFORMAT_NONE;
    if (!screen)
        d.colors[0].pixel_format = fmt;
    d.label = "slang-pass";
    return sg_make_pipeline(&d);
}

static int make_shader(Pass *p)
{
    sg_shader_desc d;
    memset(&d, 0, sizeof d);
    d.vertex_func.source = p->code.vs;
    d.vertex_func.entry = p->code.vsEntry;
    d.fragment_func.source = p->code.fs;
    d.fragment_func.entry = p->code.fsEntry;
    if (p->code.attrName[0][0])
        d.attrs[0].glsl_name = p->code.attrName[0];
    if (p->code.attrName[1][0])
        d.attrs[1].glsl_name = p->code.attrName[1];
    d.attrs[0].hlsl_sem_name = d.attrs[1].hlsl_sem_name = "TEXCOORD";
    d.attrs[0].hlsl_sem_index = 0;
    d.attrs[1].hlsl_sem_index = 1;
#if !SLANG_GL
    /* Metal and D3D take the blocks through sokol, as raw bytes at their slots (slang.h) */
    if (p->code.ubo.size) {
        if (p->code.ubo.stages & 1) {
            d.uniform_blocks[SLANG_SLOT_UBO_VS].stage = SG_SHADERSTAGE_VERTEX;
            d.uniform_blocks[SLANG_SLOT_UBO_VS].size = p->code.ubo.size;
            d.uniform_blocks[SLANG_SLOT_UBO_VS].msl_buffer_n = SLANG_SLOT_UBO_VS;
            d.uniform_blocks[SLANG_SLOT_UBO_VS].hlsl_register_b_n = SLANG_SLOT_UBO_VS;
        }
        if (p->code.ubo.stages & 2) {
            d.uniform_blocks[SLANG_SLOT_UBO_FS].stage = SG_SHADERSTAGE_FRAGMENT;
            d.uniform_blocks[SLANG_SLOT_UBO_FS].size = p->code.ubo.size;
            d.uniform_blocks[SLANG_SLOT_UBO_FS].msl_buffer_n = SLANG_SLOT_UBO_FS;
            d.uniform_blocks[SLANG_SLOT_UBO_FS].hlsl_register_b_n = SLANG_SLOT_UBO_FS;
        }
    }
    if (p->code.push.size) {
        if (p->code.push.stages & 1) {
            d.uniform_blocks[SLANG_SLOT_PUSH_VS].stage = SG_SHADERSTAGE_VERTEX;
            d.uniform_blocks[SLANG_SLOT_PUSH_VS].size = p->code.push.size;
            d.uniform_blocks[SLANG_SLOT_PUSH_VS].msl_buffer_n = SLANG_SLOT_PUSH_VS;
            d.uniform_blocks[SLANG_SLOT_PUSH_VS].hlsl_register_b_n = SLANG_SLOT_PUSH_VS;
        }
        if (p->code.push.stages & 2) {
            d.uniform_blocks[SLANG_SLOT_PUSH_FS].stage = SG_SHADERSTAGE_FRAGMENT;
            d.uniform_blocks[SLANG_SLOT_PUSH_FS].size = p->code.push.size;
            d.uniform_blocks[SLANG_SLOT_PUSH_FS].msl_buffer_n = SLANG_SLOT_PUSH_FS;
            d.uniform_blocks[SLANG_SLOT_PUSH_FS].hlsl_register_b_n = SLANG_SLOT_PUSH_FS;
        }
    }
#endif
    for (int i = 0; i < p->code.ntex; i++) {
        const sg_shader_stage st = (p->code.tex[i].stages & 2) ? SG_SHADERSTAGE_FRAGMENT : SG_SHADERSTAGE_VERTEX;
        d.views[i].texture.stage = st;
        d.views[i].texture.image_type = SG_IMAGETYPE_2D;
        d.views[i].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
        d.views[i].texture.msl_texture_n = (uint8_t)i;
        d.views[i].texture.hlsl_register_t_n = (uint8_t)i;
        d.samplers[i].stage = st;
        d.samplers[i].sampler_type = SG_SAMPLERTYPE_FILTERING;
        d.samplers[i].msl_sampler_n = (uint8_t)i;
        d.samplers[i].hlsl_register_s_n = (uint8_t)i;
        d.texture_sampler_pairs[i].stage = st;
        d.texture_sampler_pairs[i].view_slot = (uint8_t)i;
        d.texture_sampler_pairs[i].sampler_slot = (uint8_t)i;
        d.texture_sampler_pairs[i].glsl_name = p->code.tex[i].name;
    }
    d.label = "slang-pass";
    p->shd = sg_make_shader(&d);
    if (sg_query_shader_state(p->shd) != SG_RESOURCESTATE_VALID)
        return 0;
#if SLANG_GL
    SokolGl_BindUniformBlocks(sg_gl_query_shader_info(p->shd).prog, p->code.ubo.size ? p->code.ubo.glslName : NULL,
                              p->code.push.size ? p->code.push.glslName : NULL);
    p->ubo = p->code.ubo.size ? SokolGl_NewUniformBuffer() : 0;
    p->push = p->code.push.size ? SokolGl_NewUniformBuffer() : 0;
#endif
    return 1;
}

static int load_lut(int i)
{
    int w = 0, h = 0, n = 0;
    uint8_t *px = stbi_load(S.preset.lut[i].path, &w, &h, &n, 4);
    sg_image_desc d;
    sg_view_desc v;
    if (!px) {
        fprintf(stderr, "[shaders] %s: could not be read (%s)\n", S.preset.lut[i].path, stbi_failure_reason());
        return 0;
    }
    memset(&d, 0, sizeof d);
    d.width = w;
    d.height = h;
    d.pixel_format = SG_PIXELFORMAT_RGBA8;
    d.data.mip_levels[0].ptr = px;
    d.data.mip_levels[0].size = (size_t)w * h * 4;
    d.label = "slang-lut";
    {
        /* its levels, when the preset asks (<name>_mipmap): each a 2x2 box of the one before */
        uint8_t *lv[16] = { 0 };
        int n = 1, lw = w, lh = h;
        const uint8_t *prev = px;
        while (S.preset.lut[i].mipmap && (lw > 1 || lh > 1) && n < 16) {
            const int nw = lw > 1 ? lw / 2 : 1, nh = lh > 1 ? lh / 2 : 1;
            uint8_t *q = (uint8_t *)malloc((size_t)nw * nh * 4);
            if (!q)
                break;
            for (int y = 0; y < nh; y++)
                for (int x = 0; x < nw; x++)
                    for (int c = 0; c < 4; c++) {
                        const int x0 = x * 2 < lw ? x * 2 : lw - 1, x1 = x * 2 + 1 < lw ? x * 2 + 1 : lw - 1;
                        const int y0 = y * 2 < lh ? y * 2 : lh - 1, y1 = y * 2 + 1 < lh ? y * 2 + 1 : lh - 1;
                        q[((size_t)y * nw + x) * 4 + c] = (uint8_t)((prev[((size_t)y0 * lw + x0) * 4 + c] +
                            prev[((size_t)y0 * lw + x1) * 4 + c] + prev[((size_t)y1 * lw + x0) * 4 + c] +
                            prev[((size_t)y1 * lw + x1) * 4 + c] + 2) / 4);
                    }
            lv[n] = q;
            d.data.mip_levels[n].ptr = q;
            d.data.mip_levels[n].size = (size_t)nw * nh * 4;
            prev = q;
            lw = nw;
            lh = nh;
            n++;
        }
        d.num_mipmaps = n;
        S.lut[i] = sg_make_image(&d);
        for (int k = 1; k < n; k++)
            free(lv[k]);
    }
    stbi_image_free(px);
    memset(&v, 0, sizeof v);
    v.texture.image = S.lut[i];
    S.lutView[i] = sg_make_view(&v);
    S.lutW[i] = w;
    S.lutH[i] = h;
    return 1;
}

void GpuSlang_Unload(void)
{
    for (int i = 0; i < S.npasses; i++) {
        Pass *p = &S.pass[i];
        rt_free(&p->out[0]);
        rt_free(&p->out[1]);
        if (p->pip.id) sg_destroy_pipeline(p->pip);
        if (p->pipScreen.id) sg_destroy_pipeline(p->pipScreen);
        if (p->shd.id) sg_destroy_shader(p->shd);
#if SLANG_GL
        SokolGl_DeleteUniformBuffer(p->ubo);
        SokolGl_DeleteUniformBuffer(p->push);
#endif
        Slang_FreePass(&p->code);
    }
    for (int i = 0; i < SLANG_MAX_HISTORY; i++)
        rt_free(&S.orig[i]);
    for (int i = 0; i < S.preset.nluts; i++)
        if (S.lut[i].id) {
            sg_destroy_view(S.lutView[i]);
            sg_destroy_image(S.lut[i]);
        }
    if (S.quad.id)
        sg_destroy_buffer(S.quad);
    for (int a = 0; a < 2; a++)
        for (int b = 0; b < 4; b++)
            for (int c = 0; c < 2; c++)
                if (S.smp[a][b][c].id)
                    sg_destroy_sampler(S.smp[a][b][c]);
    memset(&S, 0, sizeof S);
}

int32_t GpuSlang_Load(const char *path)
{
    static const float k_quad[4 * 6] = {
        0, 0, 0, 1,  0, 0,
        1, 0, 0, 1,  1, 0,
        0, 1, 0, 1,  0, 1,
        1, 1, 0, 1,  1, 1,
    };
    char err[2048];
#if defined(SOKOL_GLES3)
    const SlangTarget target = SLANG_TARGET_GLES300;
#elif defined(SOKOL_GLCORE)
    const SlangTarget target = SLANG_TARGET_GLSL410;
#elif defined(SOKOL_METAL)
    const SlangTarget target = TARGET_OS_IPHONE ? SLANG_TARGET_MSL_IOS : SLANG_TARGET_MSL_MACOS;
#else
    const SlangTarget target = SLANG_TARGET_HLSL50;
#endif
    GpuSlang_Unload();
    if (!path || !path[0])
        return 0;
    if (!Slang_LoadPreset(path, &S.preset, err, (int32_t)sizeof err)) {
        fprintf(stderr, "[shaders] %s\n", err);
        return 0;
    }
    S.npasses = S.preset.npasses;
    S.history = 1;
    for (int i = 0; i < S.npasses; i++) {
        Pass *p = &S.pass[i];
        if (!Slang_CompilePass(&S.preset, i, target, &p->code, err, (int32_t)sizeof err)) {
            fprintf(stderr, "[shaders] %s: pass %d did not compile:\n%s\n", path, i, err);
            GpuSlang_Unload();
            return 0;
        }
        p->fmt = format_of(&S.preset.pass[i], p->code.format);
        if (!make_shader(p)) {
            fprintf(stderr, "[shaders] %s: pass %d: the driver refused its shader (the log above says why)\n", path, i);
            GpuSlang_Unload();
            return 0;
        }
        p->pip = make_pipeline(p->shd, p->fmt, 0);
        p->pipScreen = make_pipeline(p->shd, p->fmt, 1);
        for (int t = 0; t < p->code.ntex; t++) {
            const SlangTexture *tx = &p->code.tex[t];
            if (tx->kind == SLANG_TEX_PASS_FEEDBACK && tx->index < S.npasses)
                S.pass[tx->index].feedback = 1;
            if (tx->kind == SLANG_TEX_ORIGINAL_HISTORY && tx->index + 1 > S.history)
                S.history = tx->index + 1;
        }
    }
    for (int i = 0; i < S.preset.nluts; i++)
        load_lut(i);
    {
        sg_buffer_desc b;
        memset(&b, 0, sizeof b);
        b.data.ptr = k_quad;
        b.data.size = sizeof k_quad;
        b.usage.vertex_buffer = true;
        b.label = "slang-quad";
        S.quad = sg_make_buffer(&b);
    }
    S.on = 1;
    apply_overrides();
    fprintf(stderr, "[shaders] %s: %d passes, %d textures, %d parameters -- on\n", path, (int)S.npasses,
            (int)S.preset.nluts, (int)S.preset.nparams);
    return 1;
}

int32_t GpuSlang_Active(void) { return S.on; }

static void apply_overrides(void)
{
    for (int i = 0; i < s_nover; i++)
        for (int p = 0; p < S.preset.nparams; p++)
            if (!strcmp(S.preset.param[p].name, s_over[i].name)) {
                float v = s_over[i].value;
                v = v < S.preset.param[p].min ? S.preset.param[p].min : v > S.preset.param[p].max ? S.preset.param[p].max : v;
                S.preset.param[p].value = v;
            }
}

void GpuSlang_SetParameter(const char *name, float value)
{
    int i;
    for (i = 0; i < s_nover; i++)
        if (!strcmp(s_over[i].name, name))
            break;
    if (i == s_nover) {
        if (s_nover == (int)(sizeof s_over / sizeof s_over[0]))
            return;
        s_nover++;
    }
    snprintf(s_over[i].name, sizeof s_over[i].name, "%s", name);
    s_over[i].value = value;
    apply_overrides();
}

sg_view GpuSlang_BeginOriginal(int w, int h)
{
    Rt *r = &S.orig[S.frame % (uint32_t)S.history];
    S.origW = w;
    S.origH = h;
    /* (the presenter's pipelines draw it: the swapchain's format -- BGRA8 on Metal) */
    if (!rt_ensure(r, w, h, sg_query_desc().environment.defaults.color_format, S.preset.pass[0].mipmap))
        return (sg_view){ 0 };
    return r->att;
}

/* ---- running the passes ---- */

typedef struct { sg_view view; int w, h; int linear, wrap, mips; } Tex;

static Rt *pass_out(int i, int lastFrame)
{
    Pass *p = &S.pass[i];
    return p->feedback ? &p->out[(S.frame + (lastFrame ? 1u : 0u)) & 1] : &p->out[0];
}

/* How pass i samples its input: its filter_linear, linear when the preset does not say. */
static int pass_linear(int i) { return i < S.npasses ? S.preset.pass[i].filter != 0 : 1; }

static Tex texture(int pass, const SlangTexture *t)
{
    Tex x;
    memset(&x, 0, sizeof x);
    switch (t->kind) {
    case SLANG_TEX_ORIGINAL: {
        const Rt *r = &S.orig[S.frame % (uint32_t)S.history];
        x.view = r->tex; x.w = r->w; x.h = r->h; x.mips = r->mips;
        x.linear = pass_linear(0); x.wrap = S.preset.pass[0].wrap;
        break;
    }
    case SLANG_TEX_SOURCE:
        if (pass == 0) {
            const Rt *r = &S.orig[S.frame % (uint32_t)S.history];
            x.view = r->tex; x.w = r->w; x.h = r->h; x.mips = r->mips;
        } else {
            const Rt *r = pass_out(pass - 1, 0);
            x.view = r->tex; x.w = r->w; x.h = r->h; x.mips = r->mips;
        }
        x.linear = pass_linear(pass); x.wrap = S.preset.pass[pass].wrap;
        break;
    case SLANG_TEX_ORIGINAL_HISTORY: {
        const Rt *r = &S.orig[(S.frame + (uint32_t)S.history - (uint32_t)t->index) % (uint32_t)S.history];
        if (!r->img.id)
            r = &S.orig[S.frame % (uint32_t)S.history];
        x.view = r->tex; x.w = r->w; x.h = r->h; x.mips = r->mips;
        x.linear = pass_linear(0); x.wrap = S.preset.pass[0].wrap;
        break;
    }
    case SLANG_TEX_PASS_OUTPUT:
    case SLANG_TEX_PASS_FEEDBACK: {
        const Rt *r = pass_out(t->index, t->kind == SLANG_TEX_PASS_FEEDBACK);
        if (!r->img.id)
            r = pass_out(t->index, 0);
        x.view = r->tex; x.w = r->w; x.h = r->h; x.mips = r->mips;
        x.linear = pass_linear(t->index + 1); x.wrap = t->index + 1 < S.npasses ? S.preset.pass[t->index + 1].wrap : 0;
        break;
    }
    case SLANG_TEX_LUT:
        x.view = S.lutView[t->index]; x.w = S.lutW[t->index]; x.h = S.lutH[t->index];
        x.mips = S.preset.lut[t->index].mipmap ? 2 : 1;
        x.linear = S.preset.lut[t->index].linear; x.wrap = S.preset.lut[t->index].wrap;
        break;
    }
    return x;
}

static void put_f(uint8_t *blk, const SlangUniform *u, const float *v, int n)
{
    const uint32_t bytes = (uint32_t)n * 4u < u->size ? (uint32_t)n * 4u : u->size;
    memcpy(blk + u->offset, v, bytes);
}

static void fill(uint8_t *blk, const SlangBlock *b, int pass, int outW, int outH, float vw, float vh, int screen)
{
    for (int i = 0; i < b->nu; i++) {
        const SlangUniform *u = &b->u[i];
        if (u->offset + u->size > 4096)
            continue;
        switch (u->sem) {
        case SLANG_SEM_MVP: {
            /* Position 0..1 to the clip space: on GL a pass into an image upside down (the file's head) */
            const int flip = SLANG_GL ? !screen : 0;
            const float sy = flip ? 2.0f : -2.0f, ty = flip ? -1.0f : 1.0f;
            const float m[16] = { 2, 0, 0, 0,  0, sy, 0, 0,  0, 0, 1, 0,  -1, ty, 0, 1 };
            put_f(blk, u, m, 16);
            break;
        }
        case SLANG_SEM_OUTPUT_SIZE: {
            const float v[4] = { (float)outW, (float)outH, 1.0f / (float)outW, 1.0f / (float)outH };
            put_f(blk, u, v, 4);
            break;
        }
        case SLANG_SEM_FINAL_VIEWPORT_SIZE: {
            const float v[4] = { vw, vh, 1.0f / vw, 1.0f / vh };
            put_f(blk, u, v, 4);
            break;
        }
        case SLANG_SEM_FRAME_COUNT: {
            const uint32_t mod = S.preset.pass[pass].frameCountMod;
            const uint32_t c = mod ? S.frame % mod : S.frame;
            memcpy(blk + u->offset, &c, 4);
            break;
        }
        case SLANG_SEM_FRAME_DIRECTION: {
            const int32_t one = 1;
            memcpy(blk + u->offset, &one, 4);
            break;
        }
        case SLANG_SEM_ONE: {
            const uint32_t one = 1;
            memcpy(blk + u->offset, &one, 4);
            break;
        }
        case SLANG_SEM_ROTATION:
            memset(blk + u->offset, 0, 4);
            break;
        case SLANG_SEM_ORIGINAL_ASPECT: {
            const float a = vw / vh;
            memcpy(blk + u->offset, &a, 4);
            break;
        }
        case SLANG_SEM_TEXTURE_SIZE: {
            SlangTexture t;
            Tex x;
            memset(&t, 0, sizeof t);
            t.kind = u->kind;
            t.index = u->index;
            x = texture(pass, &t);
            {
                const float v[4] = { (float)x.w, (float)x.h, x.w ? 1.0f / (float)x.w : 0.0f, x.h ? 1.0f / (float)x.h : 0.0f };
                put_f(blk, u, v, 4);
            }
            break;
        }
        case SLANG_SEM_PARAMETER:
            memcpy(blk + u->offset, &S.preset.param[u->index].value, 4);
            break;
        default:
            break;
        }
    }
}

static int scaled(uint8_t type, float scale, int src, float viewport)
{
    float v = type == SLANG_SCALE_VIEWPORT ? viewport * scale : type == SLANG_SCALE_ABSOLUTE ? scale : (float)src * scale;
    int n = (int)floorf(v + 0.5f);
    return n < 1 ? 1 : n > 8192 ? 8192 : n;
}

static void draw_pass(int i, int outW, int outH, float vw, float vh, int screen)
{
    Pass *p = &S.pass[i];
    sg_bindings b;
    memset(&b, 0, sizeof b);
    b.vertex_buffers[0] = S.quad;
    for (int t = 0; t < p->code.ntex; t++) {
        const Tex x = texture(i, &p->code.tex[t]);
        b.views[t] = x.view;
        b.samplers[t] = sampler(x.linear, x.wrap, x.mips > 1);
    }
    sg_apply_pipeline(screen ? p->pipScreen : p->pip);
    sg_apply_bindings(&b);
    if (p->code.ubo.size) {
        memset(p->uboData, 0, p->code.ubo.size);
        fill(p->uboData, &p->code.ubo, i, outW, outH, vw, vh, screen);
    }
    if (p->code.push.size) {
        memset(p->pushData, 0, p->code.push.size);
        fill(p->pushData, &p->code.push, i, outW, outH, vw, vh, screen);
    }
#if SLANG_GL
    if (p->code.ubo.size)
        SokolGl_SetUniformBuffer(0, p->ubo, p->uboData, p->code.ubo.size);
    if (p->code.push.size)
        SokolGl_SetUniformBuffer(1, p->push, p->pushData, p->code.push.size);
#else
    if (p->code.ubo.size) {
        if (p->code.ubo.stages & 1)
            sg_apply_uniforms(SLANG_SLOT_UBO_VS, &(sg_range){ p->uboData, p->code.ubo.size });
        if (p->code.ubo.stages & 2)
            sg_apply_uniforms(SLANG_SLOT_UBO_FS, &(sg_range){ p->uboData, p->code.ubo.size });
    }
    if (p->code.push.size) {
        if (p->code.push.stages & 1)
            sg_apply_uniforms(SLANG_SLOT_PUSH_VS, &(sg_range){ p->pushData, p->code.push.size });
        if (p->code.push.stages & 2)
            sg_apply_uniforms(SLANG_SLOT_PUSH_FS, &(sg_range){ p->pushData, p->code.push.size });
    }
#endif
    sg_draw(0, 4, 1);
}

/* An image's mip levels from its level 0, outside any pass (a pass that reads it mipmapped). */
static void gen_mips(const Rt *r)
{
    if (r->mips <= 1)
        return;
#if SLANG_GL
    SokolGl_GenerateMipmaps(sg_gl_query_image_info(r->img).tex[0]);
#else
    GpuNative_GenerateMipmaps(r->img);
#endif
}

/* Pass i into its own image, w x h (its output this frame); 0 when the image could not be made. */
static int draw_offscreen(int i, int w, int h, float vw, float vh)
{
    Rt *r = pass_out(i, 0);
    sg_pass ps;
    const int mipped = i + 1 < S.npasses && S.preset.pass[i + 1].mipmap;  /* (the next reads it so) */
    if (!rt_ensure(r, w, h, S.pass[i].fmt, mipped))
        return 0;
    if (S.pass[i].feedback) {           /* last frame's: there, and black the first time */
        Rt *o = pass_out(i, 1);
        if (!o->img.id || o->w != w || o->h != h || o->fmt != S.pass[i].fmt) {
            rt_ensure(o, w, h, S.pass[i].fmt, mipped);
            memset(&ps, 0, sizeof ps);
            ps.action.colors[0].load_action = SG_LOADACTION_CLEAR;
            ps.attachments.colors[0] = o->att;
            sg_begin_pass(&ps);
            sg_end_pass();
        }
    }
    memset(&ps, 0, sizeof ps);
    ps.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    ps.action.colors[0].clear_value.a = 1.0f;
    ps.attachments.colors[0] = r->att;
    sg_begin_pass(&ps);
#if SLANG_GL
    SokolGl_FramebufferSrgb(S.pass[i].fmt == SG_PIXELFORMAT_SRGB8A8);
#endif
    sg_apply_viewport(0, 0, w, h, true);
    draw_pass(i, w, h, vw, vh, 0);
#if SLANG_GL
    SokolGl_FramebufferSrgb(0);
#endif
    sg_end_pass();
    gen_mips(r);
    return 1;
}

void GpuSlang_Run(float vx, float vy, float vw, float vh, void (*beginScreen)(void))
{
    int srcW = S.origW, srcH = S.origH;
    gen_mips(&S.orig[S.frame % (uint32_t)S.history]);
    /* (SWOS_SHADER_PASSES=k: the chain stops at pass k, drawn onto the screen -- to look at a
     * preset's passes one at a time) */
    const char *stop = getenv("SWOS_SHADER_PASSES");
    const int n = stop && atoi(stop) > 0 && atoi(stop) < S.npasses ? atoi(stop) : S.npasses;
    for (int i = 0; i < n; i++) {
        const SlangPassDesc *d = &S.preset.pass[i];
        if (i == n - 1) {
            /* The last onto the screen. A pass that reads its last frame (PassFeedback of the
             * last pass) gets one: the pass is drawn into its image too, the picture's size --
             * the same shader and inputs, so the same pixels as the screen's. */
            if (S.pass[i].feedback)
                draw_offscreen(i, (int)vw, (int)vh, vw, vh);
            beginScreen();
            sg_apply_viewportf(vx, vy, vw, vh, true);
            draw_pass(i, (int)vw, (int)vh, vw, vh, 1);
            break;
        }
        {
            const uint8_t tx = d->scaleTypeX ? d->scaleTypeX : SLANG_SCALE_SOURCE;
            const uint8_t ty = d->scaleTypeY ? d->scaleTypeY : SLANG_SCALE_SOURCE;
            const int w = scaled(tx, d->scaleX, srcW, vw), h = scaled(ty, d->scaleY, srcH, vh);
            if (!draw_offscreen(i, w, h, vw, vh))
                continue;
            srcW = w;
            srcH = h;
        }
    }
    S.frame++;
}

#else   /* no slang on this build */

int32_t GpuSlang_Load(const char *path) { (void)path; return 0; }
void    GpuSlang_SetParameter(const char *name, float value) { (void)name; (void)value; }
void    GpuSlang_Unload(void) {}
int32_t GpuSlang_Active(void) { return 0; }

#endif
