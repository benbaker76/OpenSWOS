/*
 * slang.h -- RetroArch's slang shaders: presets (.slangp) and passes (.slang), compiled on the
 * player's machine for whichever backend is drawing (src/slang.c; docs/RENDERING.md, "Shaders
 * from files"). RetroArch's own way: glslang compiles a pass's Vulkan GLSL to SPIR-V, and
 * SPIRV-Cross translates that into GLSL 410, GLSL ES 300, Metal or HLSL -- one file for every
 * backend. SDL builds with SWOS_SHADERCC (the Makefile's SHADERCC) only.
 *
 * This file knows nothing of sokol: it hands the renderer each pass's source for its backend and
 * a description of what to feed it -- the uniforms by meaning (MVP, SourceSize, FrameCount, a
 * parameter ...) at their offsets, and the textures by meaning (Original, Source, a pass's output,
 * a lookup texture ...) at their slots. src/gpu_present.c turns that into sokol shaders and runs
 * the passes. The slot conventions every backend shares are here (SLANG_SLOT_*).
 */
#ifndef SWOS_SLANG_H
#define SWOS_SLANG_H

#include <stdint.h>

#define SLANG_MAX_PASSES    26
#define SLANG_MAX_LUTS      16
#define SLANG_MAX_PARAMS    160
#define SLANG_MAX_TEXTURES  16      /* a pass's textures (sokol's view / sampler slots) */
#define SLANG_MAX_UNIFORMS  96      /* a block's members */
#define SLANG_MAX_HISTORY   16      /* OriginalHistory1..15 */

typedef enum {
    SLANG_TARGET_GLSL410,           /* desktop OpenGL 4.1 core */
    SLANG_TARGET_GLES300,           /* OpenGL ES 3.0 / WebGL2 */
    SLANG_TARGET_MSL_MACOS,         /* Metal */
    SLANG_TARGET_MSL_IOS,
    SLANG_TARGET_HLSL50,            /* Direct3D 11 */
} SlangTarget;

/* The uniform blocks' sokol slots: the UBO and the push constants, each once for the vertex
 * stage and once for the fragment stage (sokol's blocks are one stage each); the same slot is
 * the MSL [[buffer(n)]] and the HLSL register(bn) of that stage. Textures take view/sampler slot i
 * (MSL texture/sampler i, HLSL t i / s i). */
enum { SLANG_SLOT_UBO_VS = 0, SLANG_SLOT_UBO_FS = 1, SLANG_SLOT_PUSH_VS = 2, SLANG_SLOT_PUSH_FS = 3 };

typedef enum { SLANG_SCALE_DEFAULT, SLANG_SCALE_SOURCE, SLANG_SCALE_VIEWPORT, SLANG_SCALE_ABSOLUTE } SlangScaleType;
typedef enum { SLANG_WRAP_BORDER, SLANG_WRAP_EDGE, SLANG_WRAP_REPEAT, SLANG_WRAP_MIRRORED } SlangWrap;
typedef enum {
    SLANG_FORMAT_DEFAULT,           /* RGBA8 (or the preset's float / sRGB framebuffer) */
    SLANG_FORMAT_RGBA8, SLANG_FORMAT_SRGBA8, SLANG_FORMAT_RGBA16F, SLANG_FORMAT_RGBA32F,
    SLANG_FORMAT_RGB10A2, SLANG_FORMAT_R8, SLANG_FORMAT_RG8, SLANG_FORMAT_R16F, SLANG_FORMAT_RG16F,
    SLANG_FORMAT_R32F, SLANG_FORMAT_RG32F,
} SlangFormat;

typedef struct {
    char        path[512];          /* the .slang, resolved */
    char        alias[64];          /* the preset's aliasN, else the pass's #pragma name */
    int8_t      filter;             /* -1 the default (linear), 0 nearest, 1 linear */
    uint8_t     scaleTypeX, scaleTypeY;     /* SlangScaleType */
    float       scaleX, scaleY;     /* a factor (source / viewport) or pixels (absolute) */
    uint8_t     wrap;               /* SlangWrap */
    uint8_t     mipmap, floatFb, srgbFb;
    uint32_t    frameCountMod;      /* 0 none */
} SlangPassDesc;

typedef struct {
    char        name[64], path[512];
    uint8_t     linear, mipmap, wrap;
} SlangLut;

typedef struct {
    char        name[64], desc[96];
    float       def, min, max, step, value;
} SlangParam;

typedef struct {
    char          path[512];
    int32_t       npasses;
    SlangPassDesc pass[SLANG_MAX_PASSES];
    int32_t       nluts;
    SlangLut      lut[SLANG_MAX_LUTS];
    int32_t       nparams;          /* every pass's #pragma parameter, its value the preset's */
    SlangParam    param[SLANG_MAX_PARAMS];
} SlangPreset;

/* What a texture or a size is of. */
typedef enum {
    SLANG_TEX_ORIGINAL,             /* the game's picture, the chain's input */
    SLANG_TEX_SOURCE,               /* the pass before's output (the picture for the first) */
    SLANG_TEX_ORIGINAL_HISTORY,     /* index 1.. frames ago */
    SLANG_TEX_PASS_OUTPUT,          /* index: that pass's output, this frame */
    SLANG_TEX_PASS_FEEDBACK,        /* index: that pass's output, last frame */
    SLANG_TEX_LUT,                  /* index: the preset's texture */
} SlangTexKind;

typedef enum {
    SLANG_SEM_NONE,                 /* not one we know: left 0 */
    SLANG_SEM_MVP,                  /* mat4 */
    SLANG_SEM_OUTPUT_SIZE,          /* vec4 w, h, 1/w, 1/h of this pass's target */
    SLANG_SEM_FINAL_VIEWPORT_SIZE,
    SLANG_SEM_FRAME_COUNT,          /* uint */
    SLANG_SEM_FRAME_DIRECTION,      /* int: 1 */
    SLANG_SEM_ROTATION,             /* uint: 0 */
    SLANG_SEM_TEXTURE_SIZE,         /* vec4 of the texture (kind, index) */
    SLANG_SEM_PARAMETER,            /* float: preset parameter `index` */
    SLANG_SEM_ORIGINAL_ASPECT,      /* float */
    SLANG_SEM_ONE,                  /* uint 1: TotalSubFrames, CurrentSubFrame */
} SlangSemantic;

typedef struct {
    uint8_t  sem, kind;             /* SlangSemantic; for a texture size, SlangTexKind */
    int16_t  index;
    uint32_t offset, size;          /* bytes, in the block */
} SlangUniform;

typedef struct {
    uint32_t     size;              /* bytes (0: the pass has none) */
    uint8_t      stages;            /* 1 vertex, 2 fragment */
    int32_t      nu;
    SlangUniform u[SLANG_MAX_UNIFORMS];
    char         glslName[64];      /* GL: the uniform block's name (std140), bound to the renderer's buffer */
} SlangBlock;

typedef struct {
    uint8_t  kind;                  /* SlangTexKind */
    int16_t  index;
    uint8_t  stages;
    char     name[64];              /* the sampler's name in the source (GL binds by it) */
} SlangTexture;

typedef struct {
    char        *vs, *fs;           /* the backend's source (malloc'd) */
    char         vsEntry[32], fsEntry[32];
    SlangBlock   ubo, push;
    int32_t      ntex;
    SlangTexture tex[SLANG_MAX_TEXTURES];
    char         attrName[2][32];   /* GL: the vertex inputs at locations 0 (Position), 1 (TexCoord) */
    uint8_t      format;            /* the pass's #pragma format, SlangFormat */
} SlangPassCode;

/* A preset and its passes' parameters (their #pragma parameter lines). 0 and err on failure. */
int32_t Slang_LoadPreset(const char *path, SlangPreset *out, char *err, int32_t errCap);
/* One pass for a backend: its source and what it reads. Free with Slang_FreePass. */
int32_t Slang_CompilePass(const SlangPreset *preset, int32_t pass, SlangTarget target, SlangPassCode *out,
                          char *err, int32_t errCap);
void    Slang_FreePass(SlangPassCode *code);

/* --slang-selftest=<preset>: every pass for every backend, what each reads; 1 = all compiled. */
int32_t Slang_SelfTest(const char *preset);

#endif
