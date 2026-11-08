/*
 * chat_font.c -- the chat's typeface at the window's own pixels (include/chat_font.h).
 * SDL builds.
 */
#ifdef SWOS_SDL_BUILD
#include "chat_font.h"
#include "gfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb_truetype/stb_truetype.h"
#include "stb_image_write.h"                  /* (the self-test's picture) */

extern const uint8_t  g_chatFont[];           /* src/chat_font_data.c */
extern const uint32_t g_chatFontSize;

/* What the chat can show: scripts/make_chat_font.sh subsets the font to exactly these. */
static const struct { uint32_t first, last; } kRanges[] = {
    { 0x0020, 0x007E }, { 0x00A0, 0x017F }, { 0x2013, 0x2014 }, { 0x2018, 0x201E },
    { 0x2022, 0x2022 }, { 0x2026, 0x2026 }, { 0x20AC, 0x20AC },
};
enum { NUM_RANGES = sizeof kRanges / sizeof kRanges[0] };

typedef struct {
    uint16_t x, y, w, h;        /* in the sheet */
    int16_t  xoff, yoff;        /* from the pen on the baseline to the glyph's top-left */
    float    advance;
} Glyph;

static struct {
    int32_t        ready;       /* the font's tables are read */
    stbtt_fontinfo info;
    int32_t        px;          /* the size the sheet is made at (0: none) */
    float          scale;
    int32_t        ascent, lineHeight;
    Glyph         *glyphs;      /* one per character of kRanges, in order */
    int32_t        numGlyphs;
    GfxTexture    *sheet;
    int32_t        sheetW, sheetH;
    GfxVertex     *verts;       /* a string's triangles */
    int32_t        vertCap;
} F;

static int32_t glyph_index(uint32_t cp)
{
    int32_t base = 0;
    for (int32_t r = 0; r < NUM_RANGES; r++) {
        if (cp >= kRanges[r].first && cp <= kRanges[r].last)
            return base + (int32_t)(cp - kRanges[r].first);
        base += (int32_t)(kRanges[r].last - kRanges[r].first + 1);
    }
    return -1;
}

int32_t ChatFont_Has(uint32_t cp) { return glyph_index(cp) >= 0; }

uint32_t ChatFont_NextChar(const char *s, int32_t bytes, int32_t *used)
{
    const uint8_t *p = (const uint8_t *)s;
    const uint8_t c = p[0];
    int32_t n;
    uint32_t cp;
    if (c < 0x80) { *used = 1; return c; }
    if ((c & 0xE0) == 0xC0)      { n = 2; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { n = 3; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { n = 4; cp = c & 0x07; }
    else { *used = 1; return 0xFFFD; }
    if (bytes >= 0 && n > bytes) { *used = 1; return 0xFFFD; }
    for (int32_t i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) { *used = 1; return 0xFFFD; }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    /* overlong forms and surrogates are not characters */
    if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000) ||
        (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
        *used = 1;
        return 0xFFFD;
    }
    *used = n;
    return cp;
}

static int32_t font_ready(void)
{
    if (F.ready)
        return F.ready > 0;
    F.ready = -1;
    const int32_t off = stbtt_GetFontOffsetForIndex(g_chatFont, 0);
    if (off < 0 || !stbtt_InitFont(&F.info, g_chatFont, off)) {
        fprintf(stderr, "[chat] the chat's font did not load\n");
        return 0;
    }
    F.numGlyphs = 0;
    for (int32_t r = 0; r < NUM_RANGES; r++)
        F.numGlyphs += (int32_t)(kRanges[r].last - kRanges[r].first + 1);
    F.glyphs = (Glyph *)calloc((size_t)F.numGlyphs, sizeof *F.glyphs);
    if (!F.glyphs)
        return 0;
    F.ready = 1;
    return 1;
}

/* The glyphs drawn into an alpha sheet, shelf by shelf. 0 when made. */
static int32_t build_sheet(int32_t px, uint8_t **outAlpha, int32_t *outW, int32_t *outH)
{
    const float scale = stbtt_ScaleForPixelHeight(&F.info, (float)px);
    int32_t ascent, descent, gap;
    stbtt_GetFontVMetrics(&F.info, &ascent, &descent, &gap);
    F.scale = scale;
    F.ascent = (int32_t)(ascent * scale + 0.999f);
    F.lineHeight = (int32_t)((ascent - descent + gap) * scale + 0.999f);

    const int32_t W = px > 40 ? 1024 : 512;
    int32_t H = 64;
    uint8_t *alpha = (uint8_t *)calloc((size_t)W * H, 1);
    if (!alpha)
        return -1;
    int32_t x = 1, y = 1, shelf = 0, g = 0;
    for (int32_t r = 0; r < NUM_RANGES; r++) {
        for (uint32_t cp = kRanges[r].first; cp <= kRanges[r].last; cp++, g++) {
            Glyph *gl = &F.glyphs[g];
            int32_t adv, lsb, x0, y0, x1, y1;
            memset(gl, 0, sizeof *gl);
            stbtt_GetCodepointHMetrics(&F.info, (int)cp, &adv, &lsb);
            gl->advance = adv * scale;
            stbtt_GetCodepointBitmapBox(&F.info, (int)cp, scale, scale, &x0, &y0, &x1, &y1);
            const int32_t w = x1 - x0, h = y1 - y0;
            if (w <= 0 || h <= 0)
                continue;                               /* (a space) */
            if (x + w + 1 > W) {
                x = 1;
                y += shelf + 1;
                shelf = 0;
            }
            while (y + h + 1 > H) {                     /* taller sheet */
                uint8_t *grown = (uint8_t *)realloc(alpha, (size_t)W * H * 2);
                if (!grown) {
                    free(alpha);
                    return -1;
                }
                memset(grown + (size_t)W * H, 0, (size_t)W * H);
                alpha = grown;
                H *= 2;
            }
            stbtt_MakeCodepointBitmap(&F.info, alpha + (size_t)y * W + x, w, h, W, scale, scale, (int)cp);
            gl->x = (uint16_t)x;
            gl->y = (uint16_t)y;
            gl->w = (uint16_t)w;
            gl->h = (uint16_t)h;
            gl->xoff = (int16_t)x0;
            gl->yoff = (int16_t)y0;
            x += w + 1;
            if (h > shelf)
                shelf = h;
        }
    }
    *outAlpha = alpha;
    *outW = W;
    *outH = H;
    return 0;
}

int32_t ChatFont_Prepare(int32_t px)
{
    if (px < 6)
        px = 6;
    if (px > 96)
        px = 96;
    if (!font_ready())
        return -1;
    if (px == F.px && F.sheet)
        return 0;
    uint8_t *alpha = NULL;
    int32_t w, h;
    if (build_sheet(px, &alpha, &w, &h) != 0)
        return -1;
    uint8_t *rgba = (uint8_t *)malloc((size_t)w * h * 4);
    if (!rgba) {
        free(alpha);
        return -1;
    }
    for (int32_t i = 0; i < w * h; i++) {              /* white, the coverage as alpha */
        rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = alpha[i];
    }
    free(alpha);
    if (F.sheet)
        Gfx_DestroyTexture(F.sheet);
    F.sheet = Gfx_CreateTexture(w, h, 0);
    if (F.sheet)
        Gfx_UpdateTexture(F.sheet, rgba, w * 4);
    free(rgba);
    if (!F.sheet) {
        F.px = 0;
        return -1;
    }
    F.px = px;
    F.sheetW = w;
    F.sheetH = h;
    return 0;
}

int32_t ChatFont_LineHeight(void) { return F.lineHeight; }
int32_t ChatFont_Ascent(void) { return F.ascent; }

static const Glyph *glyph_for(uint32_t cp, uint32_t *drawn)
{
    int32_t i = glyph_index(cp);
    if (i < 0) {
        cp = '?';
        i = glyph_index(cp);
    }
    *drawn = cp;
    return &F.glyphs[i];
}

float ChatFont_Measure(const char *s, int32_t bytes)
{
    if (!s || F.px <= 0)
        return 0.0f;
    if (bytes < 0)
        bytes = (int32_t)strlen(s);
    float pen = 0.0f;
    uint32_t prev = 0;
    for (int32_t i = 0; i < bytes;) {
        int32_t used;
        uint32_t cp;
        const Glyph *g = glyph_for(ChatFont_NextChar(s + i, bytes - i, &used), &cp);
        i += used;
        if (prev)
            pen += stbtt_GetCodepointKernAdvance(&F.info, (int)prev, (int)cp) * F.scale;
        pen += g->advance;
        prev = cp;
    }
    return pen;
}

void ChatFont_Draw(float x, float y, const char *s, int32_t bytes, SDL_Color colour)
{
    if (!s || !F.sheet)
        return;
    if (bytes < 0)
        bytes = (int32_t)strlen(s);
    if (F.vertCap < bytes * 6) {
        GfxVertex *v = (GfxVertex *)realloc(F.verts, (size_t)bytes * 6 * sizeof *v);
        if (!v)
            return;
        F.verts = v;
        F.vertCap = bytes * 6;
    }
    const float baseline = y + (float)F.ascent;
    const float iw = 1.0f / (float)F.sheetW, ih = 1.0f / (float)F.sheetH;
    float pen = x;
    uint32_t prev = 0;
    int32_t n = 0;
    for (int32_t i = 0; i < bytes;) {
        int32_t used;
        uint32_t cp;
        const Glyph *g = glyph_for(ChatFont_NextChar(s + i, bytes - i, &used), &cp);
        i += used;
        if (prev)
            pen += stbtt_GetCodepointKernAdvance(&F.info, (int)prev, (int)cp) * F.scale;
        prev = cp;
        if (g->w) {
            /* Whole pixels, so the glyph's pixels land on the screen's one to one -- and a pixel
             * of the sheet's empty gap round it, so no triangle is one pixel thin: SDL's
             * software renderer drops those, and with them every 'i', 'l' and '!'. */
            const float x0 = SDL_floorf(pen + 0.5f) + g->xoff - 1.0f, y0 = SDL_floorf(baseline + 0.5f) + g->yoff - 1.0f;
            const float x1 = x0 + g->w + 2.0f, y1 = y0 + g->h + 2.0f;
            const float u0 = (g->x - 1) * iw, v0 = (g->y - 1) * ih, u1 = (g->x + g->w + 1) * iw, v1 = (g->y + g->h + 1) * ih;
            const float px[6] = { x0, x1, x0, x1, x1, x0 }, py[6] = { y0, y0, y1, y0, y1, y1 };
            const float pu[6] = { u0, u1, u0, u1, u1, u0 }, pv[6] = { v0, v0, v1, v0, v1, v1 };
            for (int32_t k = 0; k < 6; k++, n++) {
                F.verts[n].position.x = px[k];
                F.verts[n].position.y = py[k];
                F.verts[n].color = colour;
                F.verts[n].tex_coord.x = pu[k];
                F.verts[n].tex_coord.y = pv[k];
            }
        }
        pen += g->advance;
    }
    if (n)
        Gfx_Geometry(F.sheet, F.verts, n, NULL, 0);
}

void ChatFont_Shutdown(void)
{
    if (F.sheet)
        Gfx_DestroyTexture(F.sheet);
    F.sheet = NULL;
    F.px = 0;
    free(F.verts);
    F.verts = NULL;
    F.vertCap = 0;
}

/* --chat-selftest: every character of the set has a glyph or is a space, UTF-8 decodes as it
 * should, and a line rasterizes, written to chat_selftest.png for a look. */
int32_t ChatFont_SelfTest(void)
{
    int32_t fails = 0;
    if (!font_ready()) {
        fprintf(stderr, "[chat-selftest] FAIL: the font does not load\n");
        return 1;
    }
    for (int32_t r = 0; r < NUM_RANGES; r++)
        for (uint32_t cp = kRanges[r].first; cp <= kRanges[r].last; cp++)
            if (!stbtt_FindGlyphIndex(&F.info, (int)cp) && cp != 0x20 && cp != 0xA0 && cp != 0xAD) {
                fprintf(stderr, "[chat-selftest] FAIL: no glyph for U+%04X\n", cp);
                fails++;
            }
    {
        static const struct { const char *s; uint32_t cp; int32_t used; } k[] = {
            { "A", 'A', 1 }, { "\xC3\xA9", 0xE9, 2 }, { "\xE2\x82\xAC", 0x20AC, 3 },
            { "\xF0\x9F\x98\x80", 0x1F600, 4 }, { "\xC0\x80", 0xFFFD, 1 }, { "\xE2\x82", 0xFFFD, 1 },
            { "\xFF", 0xFFFD, 1 }, { "\xED\xA0\x80", 0xFFFD, 1 },
        };
        for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
            int32_t used;
            const uint32_t cp = ChatFont_NextChar(k[i].s, (int32_t)strlen(k[i].s), &used);
            if (cp != k[i].cp || used != k[i].used) {
                fprintf(stderr, "[chat-selftest] FAIL: utf-8 case %d gave U+%04X/%d\n", (int)i, cp, used);
                fails++;
            }
        }
    }
    {
        uint8_t *alpha = NULL;
        int32_t w, h;
        if (build_sheet(24, &alpha, &w, &h) != 0) {
            fprintf(stderr, "[chat-selftest] FAIL: no sheet at 24 px\n");
            fails++;
        } else {
            stbi_write_png("chat_selftest.png", w, h, 1, alpha, w);
            fprintf(stderr, "[chat-selftest] sheet %dx%d at 24 px, line %d, ascent %d -> chat_selftest.png\n",
                    w, h, F.lineHeight, F.ascent);
            free(alpha);
        }
    }
    fprintf(stderr, "[chat-selftest] %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
#endif /* SWOS_SDL_BUILD */
