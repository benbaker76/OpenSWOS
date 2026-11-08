/*
 * slang.c -- RetroArch's slang shaders, compiled here (include/slang.h; docs/RENDERING.md,
 * "Shaders from files"). A preset (.slangp) names its passes (.slang); each pass is Vulkan GLSL
 * with "#pragma stage vertex|fragment" sections, compiled by glslang to SPIR-V, reflected by
 * SPIRV-Cross for its uniforms and textures, and translated by SPIRV-Cross to the backend's
 * language. Only with SWOS_SHADERCC (third_party/glslang, third_party/SPIRV-Cross).
 */
#include "slang.h"

#if defined(SWOS_SDL_BUILD) && defined(SWOS_SHADERCC)

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>
#include <spirv_cross_c.h>

/* ---- small things ---- */

static void set_err(char *err, int32_t cap, const char *fmt, const char *a, const char *b)
{
    if (err && cap > 0)
        snprintf(err, (size_t)cap, fmt, a ? a : "", b ? b : "");
}

static char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    char *text;
    long n;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    text = (char *)malloc((size_t)n + 1);
    if (text && fread(text, 1, (size_t)n, f) != (size_t)n) {
        free(text);
        text = NULL;
    }
    fclose(f);
    if (text) {
        text[n] = '\0';
        if (len)
            *len = (size_t)n;
    }
    return text;
}

/* `rel` against the folder `base` (a file's path) is in: absolute paths kept. */
static void resolve(char *out, size_t cap, const char *base, const char *rel)
{
    const char *slash = NULL;
    if (rel[0] == '/' || (rel[0] && rel[1] == ':')) {
        snprintf(out, cap, "%s", rel);
        return;
    }
    for (const char *p = base; *p; p++)
        if (*p == '/' || *p == '\\')
            slash = p;
    if (slash)
        snprintf(out, cap, "%.*s/%s", (int)(slash - base), base, rel);
    else
        snprintf(out, cap, "%s", rel);
    for (char *p = out; *p; p++)
        if (*p == '\\')
            *p = '/';
}

typedef struct { char *s; size_t n, cap; } Buf;

static void buf_add(Buf *b, const char *s, size_t n)
{
    if (b->n + n + 1 > b->cap) {
        size_t c = b->cap ? b->cap * 2 : 4096;
        while (c < b->n + n + 1)
            c *= 2;
        char *t = (char *)realloc(b->s, c);
        if (!t)
            return;
        b->s = t;
        b->cap = c;
    }
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = '\0';
}

static int starts(const char *s, const char *word)
{
    while (*s == ' ' || *s == '\t')
        s++;
    return strncmp(s, word, strlen(word)) == 0;
}

/* ---- the preset ---- */

/* A value: a quoted one up to its closing quote, else up to a comment ("0.0625" # why, 2 # why). */
static void unquote(char *v)
{
    size_t n;
    if (v[0] == '"') {
        char *close = strchr(v + 1, '"');
        if (close)
            close[1] = '\0';
    } else {
        char *hash = strchr(v, '#');
        if (hash)
            *hash = '\0';
    }
    n = strlen(v);
    while (n && (v[n - 1] == ' ' || v[n - 1] == '\t' || v[n - 1] == '\r'))
        v[--n] = '\0';
    if (n >= 2 && v[0] == '"' && v[n - 1] == '"') {
        memmove(v, v + 1, n - 2);
        v[n - 2] = '\0';
    }
}

static int truthy(const char *v) { return !strcmp(v, "true") || !strcmp(v, "1"); }

static uint8_t scale_type(const char *v)
{
    return !strcmp(v, "source") ? SLANG_SCALE_SOURCE : !strcmp(v, "viewport") ? SLANG_SCALE_VIEWPORT :
           !strcmp(v, "absolute") ? SLANG_SCALE_ABSOLUTE : SLANG_SCALE_DEFAULT;
}

static uint8_t wrap_mode(const char *v)
{
    return !strcmp(v, "clamp_to_edge") ? SLANG_WRAP_EDGE : !strcmp(v, "repeat") ? SLANG_WRAP_REPEAT :
           !strcmp(v, "mirrored_repeat") ? SLANG_WRAP_MIRRORED : SLANG_WRAP_BORDER;
}

/* key = value pairs of one preset file, after the presets it #references (a later value wins);
 * each remembers the file it came from, for the paths in it. */
typedef struct { char key[96], value[512], file[512]; } KV;

static int read_preset_kv(const char *path, KV *kv, int *n, int max, int depth, char *err, int32_t errCap)
{
    char *text = read_file(path, NULL), *line, *save = NULL;
    if (!text) {
        set_err(err, errCap, "%s: cannot read it%s", path, NULL);
        return 0;
    }
    for (line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *eq, *k, *v, val[512];
        int i;
        while (*line == ' ' || *line == '\t')
            line++;
        if (starts(line, "#reference")) {
            char ref[512], full[1024];
            for (k = line + 10; *k == ' ' || *k == '\t'; k++) {}
            snprintf(ref, sizeof ref, "%s", k);
            unquote(ref);
            resolve(full, sizeof full, path, ref);
            if (depth > 8 || !read_preset_kv(full, kv, n, max, depth + 1, err, errCap)) {
                free(text);
                return 0;
            }
            continue;
        }
        if (line[0] == '#' || !(eq = strchr(line, '=')))
            continue;
        *eq = '\0';
        k = line;
        for (size_t kl = strlen(k); kl && (k[kl - 1] == ' ' || k[kl - 1] == '\t'); kl--)
            k[kl - 1] = '\0';
        for (v = eq + 1; *v == ' ' || *v == '\t'; v++) {}
        snprintf(val, sizeof val, "%s", v);
        unquote(val);
        for (i = 0; i < *n; i++)
            if (!strcmp(kv[i].key, k))
                break;
        if (i == *n) {
            if (*n == max)
                continue;
            (*n)++;
        }
        snprintf(kv[i].key, sizeof kv[i].key, "%s", k);
        snprintf(kv[i].value, sizeof kv[i].value, "%s", val);
        snprintf(kv[i].file, sizeof kv[i].file, "%s", path);
    }
    free(text);
    return 1;
}

static const KV *kv_find(const KV *kv, int n, const char *key)
{
    for (int i = 0; i < n; i++)
        if (!strcmp(kv[i].key, key))
            return &kv[i];
    return NULL;
}

static const char *kv_get(const KV *kv, int n, const char *key)
{
    const KV *e = kv_find(kv, n, key);
    return e ? e->value : NULL;
}

/* A .slang with its #includes in place. */
static int load_slang(const char *path, Buf *out, int depth, char *err, int32_t errCap)
{
    char *text = read_file(path, NULL);
    const char *p;
    if (!text) {
        set_err(err, errCap, "%s: cannot read it%s", path, NULL);
        return 0;
    }
    for (p = text; *p;) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) + 1 : strlen(p);
        if (starts(p, "#include")) {
            const char *q = strchr(p, '"'), *r = q ? strchr(q + 1, '"') : NULL;
            if (q && r && r < p + n) {
                char rel[512], full[1024];
                snprintf(rel, sizeof rel, "%.*s", (int)(r - q - 1), q + 1);
                resolve(full, sizeof full, path, rel);
                if (depth > 16 || !load_slang(full, out, depth + 1, err, errCap)) {
                    free(text);
                    return 0;
                }
                buf_add(out, "\n", 1);
                p += n;
                continue;
            }
        }
        buf_add(out, p, n);
        p += n;
    }
    free(text);
    return 1;
}

/* A pass's #pragma parameter lines into the preset's list (a name once). */
static void gather_params(SlangPreset *pr, const char *src)
{
    for (const char *p = src; p && *p;) {
        const char *e = strchr(p, '\n');
        if (starts(p, "#pragma parameter") && pr->nparams < SLANG_MAX_PARAMS) {
            SlangParam prm;
            char line[512], *q;
            memset(&prm, 0, sizeof prm);
            snprintf(line, sizeof line, "%.*s", (int)(e ? e - p : (long)strlen(p)), p);
            q = strstr(line, "parameter") + 9;
            while (*q == ' ' || *q == '\t')
                q++;
            {
                int n = 0;
                while (q[n] && q[n] != ' ' && q[n] != '\t' && n < (int)sizeof prm.name - 1)
                    n++;
                snprintf(prm.name, sizeof prm.name, "%.*s", n, q);
                q += n;
            }
            {
                char *a = strchr(q, '"'), *b = a ? strchr(a + 1, '"') : NULL;
                if (a && b) {
                    snprintf(prm.desc, sizeof prm.desc, "%.*s", (int)(b - a - 1), a + 1);
                    q = b + 1;
                }
            }
            sscanf(q, "%f %f %f %f", &prm.def, &prm.min, &prm.max, &prm.step);
            prm.value = prm.def;
            int i;
            for (i = 0; i < pr->nparams; i++)
                if (!strcmp(pr->param[i].name, prm.name))
                    break;
            if (i == pr->nparams)
                pr->param[pr->nparams++] = prm;
        }
        p = e ? e + 1 : NULL;
    }
}

int32_t Slang_LoadPreset(const char *path, SlangPreset *out, char *err, int32_t errCap)
{
    static KV kv[1024];
    int n = 0;
    char key[64];
    const char *v;

    memset(out, 0, sizeof *out);
    snprintf(out->path, sizeof out->path, "%s", path);
    if (!read_preset_kv(path, kv, &n, (int)(sizeof kv / sizeof kv[0]), 0, err, errCap))
        return 0;
    v = kv_get(kv, n, "shaders");
    out->npasses = v ? atoi(v) : 0;
    if (out->npasses <= 0 || out->npasses > SLANG_MAX_PASSES) {
        set_err(err, errCap, "%s: \"shaders\" is missing or not 1..26%s", path, NULL);
        return 0;
    }
    for (int i = 0; i < out->npasses; i++) {
        SlangPassDesc *d = &out->pass[i];
        d->filter = -1;
        d->scaleX = d->scaleY = 1.0f;
        snprintf(key, sizeof key, "shader%d", i);
        {
            const KV *e = kv_find(kv, n, key);
            if (!e) {
                set_err(err, errCap, "%s: no %s", path, key);
                return 0;
            }
            resolve(d->path, sizeof d->path, e->file, e->value);   /* (the file that names it) */
        }
#define GET(fmt) (snprintf(key, sizeof key, fmt, i), kv_get(kv, n, key))
        if ((v = GET("filter_linear%d")))   d->filter = (int8_t)truthy(v);
        if ((v = GET("alias%d")))           snprintf(d->alias, sizeof d->alias, "%s", v);
        if ((v = GET("scale_type%d")))      d->scaleTypeX = d->scaleTypeY = scale_type(v);
        if ((v = GET("scale_type_x%d")))    d->scaleTypeX = scale_type(v);
        if ((v = GET("scale_type_y%d")))    d->scaleTypeY = scale_type(v);
        if ((v = GET("scale%d")))           d->scaleX = d->scaleY = (float)atof(v);
        if ((v = GET("scale_x%d")))         d->scaleX = (float)atof(v);
        if ((v = GET("scale_y%d")))         d->scaleY = (float)atof(v);
        if ((v = GET("wrap_mode%d")))       d->wrap = wrap_mode(v);
        if ((v = GET("mipmap_input%d")))    d->mipmap = (uint8_t)truthy(v);
        if ((v = GET("float_framebuffer%d"))) d->floatFb = (uint8_t)truthy(v);
        if ((v = GET("srgb_framebuffer%d")))  d->srgbFb = (uint8_t)truthy(v);
        if ((v = GET("frame_count_mod%d"))) d->frameCountMod = (uint32_t)atoi(v);
#undef GET
    }
    if ((v = kv_get(kv, n, "textures"))) {
        char list[512], *t, *s2 = NULL;
        snprintf(list, sizeof list, "%s", v);
        for (t = strtok_r(list, ";", &s2); t && out->nluts < SLANG_MAX_LUTS; t = strtok_r(NULL, ";", &s2)) {
            SlangLut *l = &out->lut[out->nluts];
            const KV *e = kv_find(kv, n, t);
            if (!e)
                continue;
            snprintf(l->name, sizeof l->name, "%s", t);
            resolve(l->path, sizeof l->path, e->file, e->value);
            snprintf(key, sizeof key, "%s_linear", t);
            l->linear = (v = kv_get(kv, n, key)) ? (uint8_t)truthy(v) : 1;
            snprintf(key, sizeof key, "%s_mipmap", t);
            l->mipmap = (v = kv_get(kv, n, key)) ? (uint8_t)truthy(v) : 0;
            snprintf(key, sizeof key, "%s_wrap_mode", t);
            l->wrap = (v = kv_get(kv, n, key)) ? wrap_mode(v) : SLANG_WRAP_BORDER;
            out->nluts++;
        }
    }
    /* every pass's parameters, then the preset's values for them */
    for (int i = 0; i < out->npasses; i++) {
        Buf b = { 0 };
        if (!load_slang(out->pass[i].path, &b, 0, err, errCap)) {
            free(b.s);
            return 0;
        }
        gather_params(out, b.s);
        /* the pass's #pragma name, when the preset gives no alias */
        if (!out->pass[i].alias[0])
            for (const char *p = b.s; p && *p;) {
                const char *e = strchr(p, '\n');
                if (starts(p, "#pragma name")) {
                    const char *q = strstr(p, "name") + 4;
                    int k = 0;
                    while (*q == ' ' || *q == '\t')
                        q++;
                    while (q[k] && q[k] != '\n' && q[k] != '\r' && q[k] != ' ' && k < 63)
                        k++;
                    snprintf(out->pass[i].alias, sizeof out->pass[i].alias, "%.*s", k, q);
                }
                p = e ? e + 1 : NULL;
            }
        free(b.s);
    }
    for (int i = 0; i < out->nparams; i++)
        if ((v = kv_get(kv, n, out->param[i].name)))
            out->param[i].value = (float)atof(v);
    return 1;
}

/* ---- a pass ---- */

/* The pass's text split into its two stages: what is outside "#pragma stage" goes to both. The
 * pragmas glslang does not know are left out (parameter / name / format / stage), the format
 * kept. */
static void split_stages(const char *src, Buf *vs, Buf *fs, uint8_t *format)
{
    int stage = 0;                  /* 0 both, 1 vertex, 2 fragment */
    for (const char *p = src; *p;) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) + 1 : strlen(p);
        if (starts(p, "#pragma stage")) {
            stage = strstr(p, "vertex") && strstr(p, "vertex") < p + n ? 1 : 2;
            buf_add(vs, "\n", 1);
            buf_add(fs, "\n", 1);
        } else if (starts(p, "#pragma format")) {
            static const struct { const char *name; uint8_t f; } k_fmt[] = {
                { "R8G8B8A8_UNORM", SLANG_FORMAT_RGBA8 }, { "R8G8B8A8_SRGB", SLANG_FORMAT_SRGBA8 },
                { "R16G16B16A16_SFLOAT", SLANG_FORMAT_RGBA16F }, { "R32G32B32A32_SFLOAT", SLANG_FORMAT_RGBA32F },
                { "A2B10G10R10_UNORM_PACK32", SLANG_FORMAT_RGB10A2 }, { "R8_UNORM", SLANG_FORMAT_R8 },
                { "R8G8_UNORM", SLANG_FORMAT_RG8 }, { "R16_SFLOAT", SLANG_FORMAT_R16F },
                { "R16G16_SFLOAT", SLANG_FORMAT_RG16F }, { "R32_SFLOAT", SLANG_FORMAT_R32F },
                { "R32G32_SFLOAT", SLANG_FORMAT_RG32F },
            };
            for (size_t i = 0; i < sizeof k_fmt / sizeof k_fmt[0]; i++) {
                const char *f = strstr(p, k_fmt[i].name);
                if (f && f < p + n)
                    *format = k_fmt[i].f;
            }
            buf_add(vs, "\n", 1);
            buf_add(fs, "\n", 1);
        } else if (starts(p, "#pragma parameter") || starts(p, "#pragma name")) {
            buf_add(vs, "\n", 1);       /* (a line kept, so glslang's line numbers are the file's) */
            buf_add(fs, "\n", 1);
        } else {
            if (stage != 2) buf_add(vs, p, n); else buf_add(vs, "\n", 1);
            if (stage != 1) buf_add(fs, p, n); else buf_add(fs, "\n", 1);
        }
        p += n;
    }
}

static int glslang_ready;

/* Vulkan GLSL to SPIR-V. NULL and err on failure; free() the words. */
static uint32_t *to_spirv(const char *src, glslang_stage_t stage, size_t *nwords, const char *what,
                          char *err, int32_t errCap)
{
    glslang_input_t in;
    glslang_shader_t *sh;
    glslang_program_t *pr;
    uint32_t *words = NULL;

    if (!glslang_ready) {
        glslang_initialize_process();
        glslang_ready = 1;
    }
    memset(&in, 0, sizeof in);
    in.language = GLSLANG_SOURCE_GLSL;
    in.stage = stage;
    in.client = GLSLANG_CLIENT_VULKAN;
    in.client_version = GLSLANG_TARGET_VULKAN_1_0;
    in.target_language = GLSLANG_TARGET_SPV;
    in.target_language_version = GLSLANG_TARGET_SPV_1_0;
    in.code = src;
    in.default_version = 450;
    in.default_profile = GLSLANG_NO_PROFILE;
    in.messages = GLSLANG_MSG_DEFAULT_BIT;
    in.resource = glslang_default_resource();
    sh = glslang_shader_create(&in);
    if (!glslang_shader_preprocess(sh, &in) || !glslang_shader_parse(sh, &in)) {
        set_err(err, errCap, "%s: %s", what, glslang_shader_get_info_log(sh));
        glslang_shader_delete(sh);
        return NULL;
    }
    pr = glslang_program_create();
    glslang_program_add_shader(pr, sh);
    if (!glslang_program_link(pr, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT)) {
        set_err(err, errCap, "%s: %s", what, glslang_program_get_info_log(pr));
    } else {
        glslang_program_SPIRV_generate(pr, stage);
        *nwords = glslang_program_SPIRV_get_size(pr);
        words = (uint32_t *)malloc(*nwords * 4);
        if (words)
            glslang_program_SPIRV_get(pr, words);
    }
    glslang_program_delete(pr);
    glslang_shader_delete(sh);
    return words;
}

/* A texture's name to what it is ("PassOutput2", an alias, "<alias>Feedback", a LUT ...); a size
 * uniform is the same name with "Size" after it. 0 when it is none of them. */
static int texture_of(const SlangPreset *pr, int pass, const char *name, uint8_t *kind, int16_t *index)
{
    int i;
    if (!strcmp(name, "Original")) { *kind = SLANG_TEX_ORIGINAL; *index = 0; return 1; }
    if (!strcmp(name, "Source"))   { *kind = SLANG_TEX_SOURCE; *index = 0; return 1; }
    if (!strncmp(name, "OriginalHistory", 15) && isdigit((unsigned char)name[15])) {
        *kind = SLANG_TEX_ORIGINAL_HISTORY; *index = (int16_t)atoi(name + 15);
        if (*index == 0)                /* (slang's spec: OriginalHistory0 is Original) */
            *kind = SLANG_TEX_ORIGINAL;
        return *index >= 0 && *index < SLANG_MAX_HISTORY;
    }
    if (!strncmp(name, "PassOutput", 10) && isdigit((unsigned char)name[10])) {
        *kind = SLANG_TEX_PASS_OUTPUT; *index = (int16_t)atoi(name + 10);
        return *index < pass;
    }
    if (!strncmp(name, "PassFeedback", 12) && isdigit((unsigned char)name[12])) {
        *kind = SLANG_TEX_PASS_FEEDBACK; *index = (int16_t)atoi(name + 12);
        return *index < pr->npasses;
    }
    for (i = 0; i < pr->npasses; i++) {
        size_t n = strlen(pr->pass[i].alias);
        if (!n)
            continue;
        if (!strcmp(name, pr->pass[i].alias) && i < pass) {
            *kind = SLANG_TEX_PASS_OUTPUT; *index = (int16_t)i; return 1;
        }
        if (!strncmp(name, pr->pass[i].alias, n) && !strcmp(name + n, "Feedback")) {
            *kind = SLANG_TEX_PASS_FEEDBACK; *index = (int16_t)i; return 1;
        }
    }
    for (i = 0; i < pr->nluts; i++)
        if (!strcmp(name, pr->lut[i].name)) {
            *kind = SLANG_TEX_LUT; *index = (int16_t)i; return 1;
        }
    return 0;
}

static void semantic_of(const SlangPreset *pr, int pass, const char *name, SlangUniform *u)
{
    size_t n = strlen(name);
    u->sem = SLANG_SEM_NONE;
    if (!strcmp(name, "MVP")) { u->sem = SLANG_SEM_MVP; return; }
    if (!strcmp(name, "OutputSize")) { u->sem = SLANG_SEM_OUTPUT_SIZE; return; }
    if (!strcmp(name, "FinalViewportSize")) { u->sem = SLANG_SEM_FINAL_VIEWPORT_SIZE; return; }
    if (!strcmp(name, "FrameCount")) { u->sem = SLANG_SEM_FRAME_COUNT; return; }
    if (!strcmp(name, "FrameDirection")) { u->sem = SLANG_SEM_FRAME_DIRECTION; return; }
    if (!strcmp(name, "Rotation")) { u->sem = SLANG_SEM_ROTATION; return; }
    if (!strcmp(name, "OriginalAspect") || !strcmp(name, "OriginalAspectRotated")) { u->sem = SLANG_SEM_ORIGINAL_ASPECT; return; }
    if (!strcmp(name, "TotalSubFrames") || !strcmp(name, "CurrentSubFrame")) { u->sem = SLANG_SEM_ONE; return; }
    if (n > 4 && !strcmp(name + n - 4, "Size")) {
        char tex[96];
        snprintf(tex, sizeof tex, "%.*s", (int)(n - 4), name);
        if (texture_of(pr, pass, tex, &u->kind, &u->index)) {
            u->sem = SLANG_SEM_TEXTURE_SIZE;
            return;
        }
    }
    for (int i = 0; i < pr->nparams; i++)
        if (!strcmp(name, pr->param[i].name)) {
            u->sem = SLANG_SEM_PARAMETER;
            u->index = (int16_t)i;
            return;
        }
}

/* A block's members, by meaning. */
static void reflect_block(spvc_compiler comp, const spvc_reflected_resource *r, const SlangPreset *pr, int pass,
                          SlangBlock *blk, uint8_t stage)
{
    spvc_type type = spvc_compiler_get_type_handle(comp, r->base_type_id);
    unsigned nm = spvc_type_get_num_member_types(type);
    size_t size = 0;
    spvc_compiler_get_declared_struct_size(comp, type, &size);
    if ((uint32_t)size > blk->size)
        blk->size = (uint32_t)size;
    blk->stages |= stage;
    for (unsigned i = 0; i < nm; i++) {
        const char *name = spvc_compiler_get_member_name(comp, r->base_type_id, i);
        unsigned off = 0;
        size_t msize = 0;
        SlangUniform u;
        int j;
        spvc_compiler_type_struct_member_offset(comp, type, i, &off);
        spvc_compiler_get_declared_struct_member_size(comp, type, i, &msize);
        memset(&u, 0, sizeof u);
        u.offset = off;
        u.size = (uint32_t)msize;
        semantic_of(pr, pass, name ? name : "", &u);
        if (u.sem == SLANG_SEM_NONE)
            continue;
        for (j = 0; j < blk->nu; j++)
            if (blk->u[j].offset == u.offset)
                break;
        if (j == blk->nu && blk->nu < SLANG_MAX_UNIFORMS)
            blk->u[blk->nu++] = u;
    }
}

typedef struct {
    spvc_context  ctx;
    spvc_compiler comp;
    spvc_resources res;
} Cross;

static int cross_open(Cross *x, const uint32_t *words, size_t n, spvc_backend be)
{
    spvc_parsed_ir ir;
    memset(x, 0, sizeof *x);
    if (spvc_context_create(&x->ctx) != SPVC_SUCCESS)
        return 0;
    if (spvc_context_parse_spirv(x->ctx, words, n, &ir) != SPVC_SUCCESS ||
        spvc_context_create_compiler(x->ctx, be, ir, SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &x->comp) != SPVC_SUCCESS ||
        spvc_compiler_create_shader_resources(x->comp, &x->res) != SPVC_SUCCESS)
        return 0;
    return 1;
}

static void list(Cross *x, spvc_resource_type t, const spvc_reflected_resource **r, size_t *n)
{
    *r = NULL;
    *n = 0;
    spvc_resources_get_resource_list_for_type(x->res, t, r, n);
}

/* What a stage reads, into the pass's description (both stages share it). */
static void reflect_stage(Cross *x, const SlangPreset *pr, int pass, SlangPassCode *out, uint8_t stage)
{
    const spvc_reflected_resource *r;
    size_t n;
    list(x, SPVC_RESOURCE_TYPE_UNIFORM_BUFFER, &r, &n);
    for (size_t i = 0; i < n; i++)
        reflect_block(x->comp, &r[i], pr, pass, &out->ubo, stage);
    list(x, SPVC_RESOURCE_TYPE_PUSH_CONSTANT, &r, &n);
    for (size_t i = 0; i < n; i++)
        reflect_block(x->comp, &r[i], pr, pass, &out->push, stage);
    list(x, SPVC_RESOURCE_TYPE_SAMPLED_IMAGE, &r, &n);
    for (size_t i = 0; i < n; i++) {
        const char *name = spvc_compiler_get_name(x->comp, r[i].id);
        uint8_t kind;
        int16_t index;
        int j;
        if (!name || !texture_of(pr, pass, name, &kind, &index))
            continue;
        for (j = 0; j < out->ntex; j++)
            if (!strcmp(out->tex[j].name, name))
                break;
        if (j == out->ntex) {
            if (out->ntex == SLANG_MAX_TEXTURES)
                continue;
            out->ntex++;
            snprintf(out->tex[j].name, sizeof out->tex[j].name, "%s", name);
            out->tex[j].kind = kind;
            out->tex[j].index = index;
        }
        out->tex[j].stages |= stage;
    }
}

static int tex_slot(const SlangPassCode *code, const char *name)
{
    for (int i = 0; i < code->ntex; i++)
        if (!strcmp(code->tex[i].name, name))
            return i;
    return -1;
}

/* One stage translated for the target, the slots set as slang.h says. */
static char *translate(Cross *x, SlangTarget target, SlangPassCode *code, uint8_t stage, char *err, int32_t errCap)
{
    const SpvExecutionModel model = stage == 1 ? SpvExecutionModelVertex : SpvExecutionModelFragment;
    spvc_compiler_options opt;
    const spvc_reflected_resource *r;
    size_t n;
    const char *src = NULL;
    char *copy;

    spvc_compiler_create_compiler_options(x->comp, &opt);
    if (target == SLANG_TARGET_GLSL410 || target == SLANG_TARGET_GLES300) {
        const int es = target == SLANG_TARGET_GLES300;
        spvc_compiler_options_set_uint(opt, SPVC_COMPILER_OPTION_GLSL_VERSION, es ? 300 : 410);
        spvc_compiler_options_set_bool(opt, SPVC_COMPILER_OPTION_GLSL_ES, es);
        spvc_compiler_options_set_bool(opt, SPVC_COMPILER_OPTION_GLSL_EMIT_PUSH_CONSTANT_AS_UNIFORM_BUFFER, 1);
        spvc_compiler_options_set_bool(opt, SPVC_COMPILER_OPTION_GLSL_ES_DEFAULT_FLOAT_PRECISION_HIGHP, 1);
        spvc_compiler_options_set_bool(opt, SPVC_COMPILER_OPTION_GLSL_ES_DEFAULT_INT_PRECISION_HIGHP, 1);
        spvc_compiler_options_set_bool(opt, SPVC_COMPILER_OPTION_GLSL_ENABLE_420PACK_EXTENSION, 0);
        /* the blocks stay uniform blocks (GL 4.1, GLES 3 and WebGL2 all have them: sokol's own
         * uniforms take one type a block, and a slang block mixes uint and float), named for the
         * renderer, which binds its own buffers to them (slang.h: glslName) */
        list(x, SPVC_RESOURCE_TYPE_UNIFORM_BUFFER, &r, &n);
        for (size_t i = 0; i < n; i++)
            spvc_compiler_set_name(x->comp, r[i].base_type_id, "swos_UBO");
        list(x, SPVC_RESOURCE_TYPE_PUSH_CONSTANT, &r, &n);
        for (size_t i = 0; i < n; i++)
            spvc_compiler_set_name(x->comp, r[i].base_type_id, "swos_Push");
        snprintf(code->ubo.glslName, sizeof code->ubo.glslName, "swos_UBO");
        snprintf(code->push.glslName, sizeof code->push.glslName, "swos_Push");
        /* the varyings matched by location, whatever each stage calls them */
        list(x, stage == 1 ? SPVC_RESOURCE_TYPE_STAGE_OUTPUT : SPVC_RESOURCE_TYPE_STAGE_INPUT, &r, &n);
        for (size_t i = 0; i < n; i++) {
            char vname[32];
            snprintf(vname, sizeof vname, "swos_v%u", spvc_compiler_get_decoration(x->comp, r[i].id, SpvDecorationLocation));
            spvc_compiler_set_name(x->comp, r[i].id, vname);
        }
        if (stage == 1) {
            list(x, SPVC_RESOURCE_TYPE_STAGE_INPUT, &r, &n);
            for (size_t i = 0; i < n; i++) {
                const unsigned loc = spvc_compiler_get_decoration(x->comp, r[i].id, SpvDecorationLocation);
                if (loc < 2)
                    snprintf(code->attrName[loc], sizeof code->attrName[loc], "%s",
                             spvc_compiler_get_name(x->comp, r[i].id));
            }
        }
    } else if (target == SLANG_TARGET_MSL_MACOS || target == SLANG_TARGET_MSL_IOS) {
        spvc_msl_resource_binding b;
        spvc_compiler_options_set_uint(opt, SPVC_COMPILER_OPTION_MSL_PLATFORM,
                                       target == SLANG_TARGET_MSL_IOS ? SPVC_MSL_PLATFORM_IOS : SPVC_MSL_PLATFORM_MACOS);
        spvc_compiler_options_set_uint(opt, SPVC_COMPILER_OPTION_MSL_VERSION, 20000);
        list(x, SPVC_RESOURCE_TYPE_UNIFORM_BUFFER, &r, &n);
        for (size_t i = 0; i < n; i++) {
            spvc_msl_resource_binding_init(&b);
            b.stage = model;
            b.desc_set = spvc_compiler_get_decoration(x->comp, r[i].id, SpvDecorationDescriptorSet);
            b.binding = spvc_compiler_get_decoration(x->comp, r[i].id, SpvDecorationBinding);
            b.msl_buffer = stage == 1 ? SLANG_SLOT_UBO_VS : SLANG_SLOT_UBO_FS;
            spvc_compiler_msl_add_resource_binding(x->comp, &b);
        }
        spvc_msl_resource_binding_init(&b);
        b.stage = model;
        b.desc_set = SPVC_MSL_PUSH_CONSTANT_DESC_SET;
        b.binding = SPVC_MSL_PUSH_CONSTANT_BINDING;
        b.msl_buffer = stage == 1 ? SLANG_SLOT_PUSH_VS : SLANG_SLOT_PUSH_FS;
        spvc_compiler_msl_add_resource_binding(x->comp, &b);
        list(x, SPVC_RESOURCE_TYPE_SAMPLED_IMAGE, &r, &n);
        for (size_t i = 0, unknown = 0; i < n; i++) {
            int slot = tex_slot(code, spvc_compiler_get_name(x->comp, r[i].id));
            if (slot < 0)                   /* (one we do not feed: left unbound, but at a slot of its own --
                                             * SPIRV-Cross's default would be 0, which a texture we feed has) */
                slot = SLANG_MAX_TEXTURES + (int)unknown++;
            spvc_msl_resource_binding_init(&b);
            b.stage = model;
            b.desc_set = spvc_compiler_get_decoration(x->comp, r[i].id, SpvDecorationDescriptorSet);
            b.binding = spvc_compiler_get_decoration(x->comp, r[i].id, SpvDecorationBinding);
            b.msl_texture = b.msl_sampler = (unsigned)slot;
            spvc_compiler_msl_add_resource_binding(x->comp, &b);
        }
    } else {
        spvc_hlsl_resource_binding b;
        spvc_compiler_options_set_uint(opt, SPVC_COMPILER_OPTION_HLSL_SHADER_MODEL, 50);
        list(x, SPVC_RESOURCE_TYPE_UNIFORM_BUFFER, &r, &n);
        for (size_t i = 0; i < n; i++) {
            spvc_hlsl_resource_binding_init(&b);
            b.stage = model;
            b.desc_set = spvc_compiler_get_decoration(x->comp, r[i].id, SpvDecorationDescriptorSet);
            b.binding = spvc_compiler_get_decoration(x->comp, r[i].id, SpvDecorationBinding);
            b.cbv.register_binding = stage == 1 ? SLANG_SLOT_UBO_VS : SLANG_SLOT_UBO_FS;
            spvc_compiler_hlsl_add_resource_binding(x->comp, &b);
        }
        spvc_hlsl_resource_binding_init(&b);
        b.stage = model;
        b.desc_set = SPVC_HLSL_PUSH_CONSTANT_DESC_SET;
        b.binding = SPVC_HLSL_PUSH_CONSTANT_BINDING;
        b.cbv.register_binding = stage == 1 ? SLANG_SLOT_PUSH_VS : SLANG_SLOT_PUSH_FS;
        spvc_compiler_hlsl_add_resource_binding(x->comp, &b);
        list(x, SPVC_RESOURCE_TYPE_SAMPLED_IMAGE, &r, &n);
        for (size_t i = 0, unknown = 0; i < n; i++) {
            int slot = tex_slot(code, spvc_compiler_get_name(x->comp, r[i].id));
            if (slot < 0)                   /* (as for Metal) */
                slot = SLANG_MAX_TEXTURES + (int)unknown++;
            spvc_hlsl_resource_binding_init(&b);
            b.stage = model;
            b.desc_set = spvc_compiler_get_decoration(x->comp, r[i].id, SpvDecorationDescriptorSet);
            b.binding = spvc_compiler_get_decoration(x->comp, r[i].id, SpvDecorationBinding);
            b.srv.register_binding = b.sampler.register_binding = (unsigned)slot;
            spvc_compiler_hlsl_add_resource_binding(x->comp, &b);
        }
    }
    spvc_compiler_install_compiler_options(x->comp, opt);
    if (spvc_compiler_compile(x->comp, &src) != SPVC_SUCCESS || !src) {
        set_err(err, errCap, "SPIRV-Cross: %s%s", spvc_context_get_last_error_string(x->ctx), NULL);
        return NULL;
    }
    {
        const char *entry = spvc_compiler_get_cleansed_entry_point_name(x->comp, "main", model);
        snprintf(stage == 1 ? code->vsEntry : code->fsEntry, sizeof code->vsEntry, "%s", entry ? entry : "main");
    }
    copy = (char *)malloc(strlen(src) + 1);
    if (copy)
        strcpy(copy, src);
    return copy;
}

int32_t Slang_CompilePass(const SlangPreset *preset, int32_t pass, SlangTarget target, SlangPassCode *out,
                          char *err, int32_t errCap)
{
    Buf src = { 0 }, vsb = { 0 }, fsb = { 0 };
    uint32_t *vw = NULL, *fw = NULL;
    size_t vn = 0, fn = 0;
    Cross vx, fx;
    spvc_backend be = target == SLANG_TARGET_HLSL50 ? SPVC_BACKEND_HLSL :
                      (target == SLANG_TARGET_MSL_MACOS || target == SLANG_TARGET_MSL_IOS) ? SPVC_BACKEND_MSL :
                      SPVC_BACKEND_GLSL;
    int ok = 0;
    char what[600];

    memset(out, 0, sizeof *out);
    memset(&vx, 0, sizeof vx);
    memset(&fx, 0, sizeof fx);
    if (pass < 0 || pass >= preset->npasses)
        return 0;
    if (!load_slang(preset->pass[pass].path, &src, 0, err, errCap))
        goto done;
    split_stages(src.s, &vsb, &fsb, &out->format);
    snprintf(what, sizeof what, "%s (vertex)", preset->pass[pass].path);
    if (!(vw = to_spirv(vsb.s, GLSLANG_STAGE_VERTEX, &vn, what, err, errCap)))
        goto done;
    snprintf(what, sizeof what, "%s (fragment)", preset->pass[pass].path);
    if (!(fw = to_spirv(fsb.s, GLSLANG_STAGE_FRAGMENT, &fn, what, err, errCap)))
        goto done;
    if (!cross_open(&vx, vw, vn, be) || !cross_open(&fx, fw, fn, be)) {
        set_err(err, errCap, "%s: SPIRV-Cross could not read the SPIR-V%s", preset->pass[pass].path, NULL);
        goto done;
    }
    reflect_stage(&vx, preset, pass, out, 1);
    reflect_stage(&fx, preset, pass, out, 2);
    out->ubo.size = (out->ubo.size + 15u) & ~15u;
    out->push.size = (out->push.size + 15u) & ~15u;
    if (!(out->vs = translate(&vx, target, out, 1, err, errCap)) ||
        !(out->fs = translate(&fx, target, out, 2, err, errCap)))
        goto done;
    ok = 1;
done:
    if (vx.ctx) spvc_context_destroy(vx.ctx);
    if (fx.ctx) spvc_context_destroy(fx.ctx);
    free(vw);
    free(fw);
    free(src.s);
    free(vsb.s);
    free(fsb.s);
    if (!ok)
        Slang_FreePass(out);
    return ok;
}

void Slang_FreePass(SlangPassCode *code)
{
    free(code->vs);
    free(code->fs);
    code->vs = code->fs = NULL;
}

/* ---- --slang-selftest ---- */

int32_t Slang_SelfTest(const char *path)
{
    static SlangPreset pr;
    static const struct { SlangTarget t; const char *name; } k_targets[] = {
        { SLANG_TARGET_GLSL410, "GLSL 410" }, { SLANG_TARGET_GLES300, "GLSL ES 300" },
        { SLANG_TARGET_MSL_MACOS, "Metal" }, { SLANG_TARGET_HLSL50, "HLSL 5" },
    };
    char err[2048];
    int32_t failed = 0;

    if (!Slang_LoadPreset(path, &pr, err, (int32_t)sizeof err)) {
        printf("[slang] %s\n", err);
        return 0;
    }
    printf("[slang] %s: %d passes, %d textures, %d parameters\n", path, (int)pr.npasses, (int)pr.nluts, (int)pr.nparams);
    for (int p = 0; p < pr.npasses; p++)
        for (size_t t = 0; t < sizeof k_targets / sizeof k_targets[0]; t++) {
            SlangPassCode code;
            if (!Slang_CompilePass(&pr, p, k_targets[t].t, &code, err, (int32_t)sizeof err)) {
                printf("[slang]   pass %d %s: FAILED\n%s\n", p, k_targets[t].name, err);
                failed++;
                continue;
            }
            if (t == 0) {
                printf("[slang]   pass %d (%s): ubo %u B (%d known), push %u B (%d known), textures:", p,
                       pr.pass[p].alias[0] ? pr.pass[p].alias : "-", (unsigned)code.ubo.size, (int)code.ubo.nu,
                       (unsigned)code.push.size, (int)code.push.nu);
                for (int i = 0; i < code.ntex; i++)
                    printf(" %s", code.tex[i].name);
                printf("\n");
            }
            printf("[slang]   pass %d %s: %zu + %zu bytes\n", p, k_targets[t].name, strlen(code.vs), strlen(code.fs));
            Slang_FreePass(&code);
        }
    printf("[slang] %s\n", failed ? "FAILED" : "all compiled");
    return failed == 0;
}

#else   /* no shader compiler in this build */

int32_t Slang_LoadPreset(const char *path, SlangPreset *out, char *err, int32_t errCap)
{
    (void)path; (void)out;
    if (err && errCap > 0)
        snprintf(err, (size_t)errCap, "this build has no shader compiler (SHADERCC=0)");
    return 0;
}
int32_t Slang_CompilePass(const SlangPreset *preset, int32_t pass, SlangTarget target, SlangPassCode *out,
                          char *err, int32_t errCap)
{
    (void)preset; (void)pass; (void)target; (void)out; (void)err; (void)errCap;
    return 0;
}
void    Slang_FreePass(SlangPassCode *code) { (void)code; }
int32_t Slang_SelfTest(const char *preset) { (void)preset; return 0; }

#endif
