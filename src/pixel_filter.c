/*
 * pixel_filter.c -- SWOS 2020's Simple2x, Scale2x and Scale3x picture filters (include/pixel_filter.h;
 * launcher options, agent B). SDL builds.
 *
 * Scale2x and Scale3x are Andrea Mazzoleni's rules (scale2x.it, "the algorithm"), written here from
 * their published description, not from any implementation: each source pixel E becomes a 2x2 or
 * 3x3 block, a corner of which takes a neighbour's colour where two neighbours meet across it and
 * the other two differ -- the edges of pixel art come out rounded, its flat areas unchanged. The
 * picture's border pixels see themselves beyond the edge. Pixels are compared whole (ARGB), so a
 * transparent key (alpha 0) is a colour like any other.
 */
#include "pixel_filter.h"

#ifdef SWOS_SDL_BUILD

#include <stdio.h>
#include <string.h>

static const char *const k_names[PIXEL_FILTER_COUNT]  = { "none", "simple2x", "scale2x", "scale3x" };
static const char *const k_labels[PIXEL_FILTER_COUNT] = { "NONE", "SIMPLE 2X", "SCALE 2X", "SCALE 3X" };

int32_t PixelFilter_Parse(const char *name)
{
    for (int32_t i = 0; name && i < PIXEL_FILTER_COUNT; i++) {
        const char *a = name, *b = k_names[i];
        while (*a && *b && (*a | 0x20) == *b) { a++; b++; }
        if (!*a && !*b)
            return i;
    }
    return -1;
}

const char *PixelFilter_Name(int32_t f)  { return f > 0 && f < PIXEL_FILTER_COUNT ? k_names[f] : k_names[0]; }
const char *PixelFilter_Label(int32_t f) { return f > 0 && f < PIXEL_FILTER_COUNT ? k_labels[f] : k_labels[0]; }

int32_t PixelFilter_Factor(int32_t f)
{
    return f == PIXEL_FILTER_SCALE3X ? 3 : (f == PIXEL_FILTER_SIMPLE2X || f == PIXEL_FILTER_SCALE2X) ? 2 : 1;
}

static void simple(const uint32_t *src, int32_t w, int32_t h, int32_t pitch, uint32_t *dst, int32_t k)
{
    const int32_t dw = w * k;
    for (int32_t y = 0; y < h; y++) {
        uint32_t *row = dst + (size_t)y * k * dw;
        const uint32_t *s = src + (size_t)y * pitch;
        for (int32_t x = 0; x < w; x++)
            for (int32_t i = 0; i < k; i++)
                row[x * k + i] = s[x];
        for (int32_t i = 1; i < k; i++)
            memcpy(row + (size_t)i * dw, row, (size_t)dw * 4);
    }
}

/*   A B C
 *   D E F     E's neighbours (clamped at the picture's edge)
 *   G H I                                                      */
static void scale2x(const uint32_t *src, int32_t w, int32_t h, int32_t pitch, uint32_t *dst)
{
    const int32_t dw = w * 2;
    for (int32_t y = 0; y < h; y++) {
        const uint32_t *up = src + (size_t)(y > 0 ? y - 1 : y) * pitch;
        const uint32_t *row = src + (size_t)y * pitch;
        const uint32_t *dn = src + (size_t)(y + 1 < h ? y + 1 : y) * pitch;
        uint32_t *o0 = dst + (size_t)y * 2 * dw, *o1 = o0 + dw;
        for (int32_t x = 0; x < w; x++) {
            const int32_t xl = x > 0 ? x - 1 : x, xr = x + 1 < w ? x + 1 : x;
            const uint32_t B = up[x], D = row[xl], E = row[x], F = row[xr], H = dn[x];
            if (B != H && D != F) {
                o0[2 * x]     = D == B ? D : E;
                o0[2 * x + 1] = B == F ? F : E;
                o1[2 * x]     = D == H ? D : E;
                o1[2 * x + 1] = H == F ? F : E;
            } else {
                o0[2 * x] = o0[2 * x + 1] = o1[2 * x] = o1[2 * x + 1] = E;
            }
        }
    }
}

static void scale3x(const uint32_t *src, int32_t w, int32_t h, int32_t pitch, uint32_t *dst)
{
    const int32_t dw = w * 3;
    for (int32_t y = 0; y < h; y++) {
        const uint32_t *up = src + (size_t)(y > 0 ? y - 1 : y) * pitch;
        const uint32_t *row = src + (size_t)y * pitch;
        const uint32_t *dn = src + (size_t)(y + 1 < h ? y + 1 : y) * pitch;
        uint32_t *o0 = dst + (size_t)y * 3 * dw, *o1 = o0 + dw, *o2 = o1 + dw;
        for (int32_t x = 0; x < w; x++) {
            const int32_t xl = x > 0 ? x - 1 : x, xr = x + 1 < w ? x + 1 : x;
            const uint32_t A = up[xl], B = up[x], C = up[xr];
            const uint32_t D = row[xl], E = row[x], F = row[xr];
            const uint32_t G = dn[xl], H = dn[x], I = dn[xr];
            uint32_t *p0 = o0 + 3 * x, *p1 = o1 + 3 * x, *p2 = o2 + 3 * x;
            if (B != H && D != F) {
                p0[0] = D == B ? D : E;
                p0[1] = (D == B && E != C) || (B == F && E != A) ? B : E;
                p0[2] = B == F ? F : E;
                p1[0] = (D == B && E != G) || (D == H && E != A) ? D : E;
                p1[1] = E;
                p1[2] = (B == F && E != I) || (H == F && E != C) ? F : E;
                p2[0] = D == H ? D : E;
                p2[1] = (D == H && E != I) || (H == F && E != G) ? H : E;
                p2[2] = H == F ? F : E;
            } else {
                p0[0] = p0[1] = p0[2] = p1[0] = p1[1] = p1[2] = p2[0] = p2[1] = p2[2] = E;
            }
        }
    }
}

void PixelFilter_Run(int32_t filter, const uint32_t *src, int32_t w, int32_t h, int32_t pitch, uint32_t *dst)
{
    switch (filter) {
    case PIXEL_FILTER_SIMPLE2X: simple(src, w, h, pitch, dst, 2); break;
    case PIXEL_FILTER_SCALE2X:  scale2x(src, w, h, pitch, dst);   break;
    case PIXEL_FILTER_SCALE3X:  scale3x(src, w, h, pitch, dst);   break;
    default:                    simple(src, w, h, pitch, dst, 1); break;
    }
}

/* ---- --pixel-filter-selftest ---- */

static int32_t expect(const char *what, const uint32_t *got, const uint32_t *want, int32_t n)
{
    if (memcmp(got, want, (size_t)n * 4) == 0)
        return 0;
    fprintf(stderr, "[pixel-filter-selftest] FAIL: %s\n", what);
    for (int32_t i = 0; i < n; i++)
        fprintf(stderr, "  [%d] got %08x want %08x\n", (int)i, (unsigned)got[i], (unsigned)want[i]);
    return 1;
}

int32_t PixelFilter_SelfTest(void)
{
    enum { A = 0xFF102030u, B = 0xFFC0B0A0u };
    int32_t fails = 0;
    uint32_t out[128];      /* (the largest: 3 x 3 at 3x, 81) */
    {   /* names */
        for (int32_t f = 0; f < PIXEL_FILTER_COUNT; f++)
            if (PixelFilter_Parse(PixelFilter_Name(f)) != f) { fails++; fprintf(stderr, "[pixel-filter-selftest] FAIL: name %d\n", (int)f); }
        if (PixelFilter_Parse("hq2x") != -1 || PixelFilter_Parse("SCALE2X") != PIXEL_FILTER_SCALE2X) {
            fails++;
            fprintf(stderr, "[pixel-filter-selftest] FAIL: parse\n");
        }
    }
    {   /* simple2x: every pixel a 2x2 block */
        const uint32_t src[2] = { A, B };
        const uint32_t want[8] = { A, A, B, B, A, A, B, B };
        PixelFilter_Run(PIXEL_FILTER_SIMPLE2X, src, 2, 1, 2, out);
        fails += expect("simple2x", out, want, 8);
    }
    {   /* scale2x: B's corner rounded where the two A's meet across it; A's left square */
        const uint32_t src[4] = { A, A, A, B };
        const uint32_t want[16] = { A, A, A, A,
                                    A, A, A, A,
                                    A, A, A, B,
                                    A, A, B, B };
        PixelFilter_Run(PIXEL_FILTER_SCALE2X, src, 2, 2, 2, out);
        fails += expect("scale2x corner", out, want, 16);
    }
    {   /* scale3x: the same corner, three wide */
        const uint32_t src[4] = { A, A, A, B };
        const uint32_t want[36] = { A, A, A, A, A, A,
                                    A, A, A, A, A, A,
                                    A, A, A, A, A, A,
                                    A, A, A, A, A, B,
                                    A, A, A, A, B, B,
                                    A, A, A, B, B, B };
        PixelFilter_Run(PIXEL_FILTER_SCALE3X, src, 2, 2, 2, out);
        fails += expect("scale3x corner", out, want, 36);
    }
    {   /* a flat picture stays flat, and the source's pitch is honoured */
        uint32_t src[3 * 4];
        for (int32_t i = 0; i < 12; i++) src[i] = (i % 4) == 3 ? B : A;   /* (column 3 is past w) */
        for (int32_t f = 1; f < PIXEL_FILTER_COUNT; f++) {
            const int32_t k = PixelFilter_Factor(f), n = 3 * k * 3 * k;
            PixelFilter_Run(f, src, 3, 3, 4, out);
            for (int32_t i = 0; i < n; i++)
                if (out[i] != A) { fails++; fprintf(stderr, "[pixel-filter-selftest] FAIL: %s flat\n", PixelFilter_Name(f)); break; }
        }
    }
    fprintf(stderr, "[pixel-filter-selftest] %s\n", fails ? "FAILED" : "ok");
    return fails;
}

#else
typedef int pixel_filter_amiga_has_none;     /* (ISO C: a translation unit is not empty) */
#endif /* SWOS_SDL_BUILD */
