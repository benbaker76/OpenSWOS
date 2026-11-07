/*
 * crt_effects.c — see include/crt_effects.h.
 *
 * Everything lands in one RGB texture that is multiplied over the finished
 * frame (SDL_BLENDMODE_MOD: dst = dst * src). White leaves a pixel alone, grey
 * dims it, black kills it — so a single texture can carry the grille, the
 * vignette and the corner shape at once.
 *
 * Adapted from CannonBall-SE's overlay path (src/main/sdl2/rendersurface.cpp
 * init_overlay), which builds the same combined mask on the CPU rather than
 * paying for a per-pixel shader. The maths here is our own: CannonBall models
 * true barrel curvature by intersecting two large circles, which it needs
 * because it also warps the image; we only draw the rounded glass edge, so a
 * rounded-rectangle distance field gives the same look for far less work.
 */
#include "crt_effects.h"

#ifdef SWOS_SDL_BUILD

#include "gfx.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static CrtEffectsConfig s_cfg;          /* current settings */
static CrtEffectsConfig s_builtCfg;     /* what s_tex was built from */
static GfxTexture      *s_tex;
static int              s_builtW, s_builtH;
static int              s_failed;       /* creation failed once — stop retrying */

void CrtEffects_Configure(const CrtEffectsConfig *cfg)
{
    if (cfg) s_cfg = *cfg;
}

int CrtEffects_Active(void)
{
    return !s_failed &&
           (s_cfg.mask != CRT_MASK_OFF || s_cfg.vignette > 0 || s_cfg.corners > 0);
}

void CrtEffects_Shutdown(void)
{
    if (s_tex) { Gfx_DestroyTexture(s_tex); s_tex = NULL; }
    s_builtW = s_builtH = 0;
    s_failed = 0;
}

static int cfg_same(const CrtEffectsConfig *a, const CrtEffectsConfig *b)
{
    return a->mask == b->mask && a->mask_strength == b->mask_strength &&
           a->vignette == b->vignette && a->corners == b->corners;
}

/* Grille. Returns the per-channel multiplier for one output pixel.
 *
 * An aperture grille dims two of the three channels on each column, so a white
 * pixel becomes an R,G,B triad and mid-tones pick up the tube's texture while
 * black stays black — the reason this reads as "CRT" rather than "dark". The
 * slot mask staggers alternate row-pairs by half a triad, which is what a real
 * shadow mask does. Scanlines dim whole rows instead.
 *
 * `dim` is the off-phase level: 255 = invisible, 0 = fully black. */
static void mask_rgb(int x, int y, int dim, CrtMaskKind kind,
                     uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = *g = *b = 255;
    switch (kind) {
    case CRT_MASK_APERTURE:
    case CRT_MASK_SLOT: {
        int col = x % 3;
        if (kind == CRT_MASK_SLOT) {
            /* Stagger every other pair of rows by half a triad. */
            if (((y >> 1) & 1) != 0) col = (col + 2) % 3;
        }
        if (col != 0) *r = (uint8_t)dim;
        if (col != 1) *g = (uint8_t)dim;
        if (col != 2) *b = (uint8_t)dim;
        /* Slot masks also have a horizontal gap between the phosphor slots. */
        if (kind == CRT_MASK_SLOT && (y % 3) == 2) {
            *r = (uint8_t)((*r * dim) / 255);
            *g = (uint8_t)((*g * dim) / 255);
            *b = (uint8_t)((*b * dim) / 255);
        }
        break;
    }
    case CRT_MASK_SCANLINES:
        if ((y & 1) != 0) *r = *g = *b = (uint8_t)dim;
        break;
    case CRT_MASK_OFF:
    default:
        break;
    }
}

/* Build the combined texture at the current output size (on whichever presenter: gfx.h). */
static GfxTexture *build(int w, int h)
{
    if (w <= 0 || h <= 0) return NULL;

    GfxTexture *t = Gfx_CreateTexture(w, h, 0);
    uint8_t *px = (uint8_t *)malloc((size_t)w * h * 4);
    if (!t || !px) {
        fprintf(stderr, "[crt] %dx%d overlay texture failed (%s) — effects off\n",
                w, h, SDL_GetError());
        Gfx_DestroyTexture(t);
        free(px);
        s_failed = 1;
        return NULL;
    }
    const int pitch = w * 4;

    /* Vignette geometry: measured from the centre, in units of the half-
     * diagonal, so it looks the same at any resolution or aspect. Nothing
     * dims inside `inner`; beyond it the falloff is squared so there is no
     * visible ring where it starts. */
    const double midx   = w * 0.5;
    const double midy   = h * 0.5;
    const double dia    = sqrt(midx * midx + midy * midy);
    const double inner  = dia * 0.30;
    const double outer  = dia * 1.00;
    const double span   = (outer - inner) * (outer - inner);
    const double vstr   = s_cfg.vignette / 100.0;

    /* Rounded-glass corner: a rounded-rect distance field. `edge` is the soft
     * band that fades to black so the corner is not a hard cut. */
    const double radius = (s_cfg.corners / 100.0) * (dia * 0.18);
    const double edge   = dia * 0.010;

    const int dim = 255 - (s_cfg.mask_strength * 255) / 100;

    for (int y = 0; y < h; y++) {
        uint8_t *row = px + (size_t)y * pitch;           /* R, G, B, A */
        for (int x = 0; x < w; x++) {
            uint8_t r, g, b;
            mask_rgb(x, y, dim, s_cfg.mask, &r, &g, &b);

            double shade = 1.0;

            if (s_cfg.vignette > 0) {
                double dx = midx - (x + 0.5), dy = midy - (y + 0.5);
                double d2 = dx * dx + dy * dy;
                if (d2 > inner * inner) {
                    double d = sqrt(d2);
                    double f = ((d - inner) * (d - inner)) / span;
                    if (f > 1.0) f = 1.0;
                    shade *= 1.0 - vstr * f;
                }
            }

            if (radius > 0.5) {
                /* Distance OUTSIDE the rounded rectangle inset by `radius`. */
                double qx = fabs(x + 0.5 - midx) - (midx - radius);
                double qy = fabs(y + 0.5 - midy) - (midy - radius);
                if (qx < 0.0) qx = 0.0;
                if (qy < 0.0) qy = 0.0;
                double d = sqrt(qx * qx + qy * qy) - radius;
                if (d > 0.0) {
                    double f = 1.0 - d / edge;          /* 1 at the edge -> 0 outside */
                    if (f < 0.0) f = 0.0;
                    if (f > 1.0) f = 1.0;
                    shade *= f;
                }
            }

            if (shade < 0.0) shade = 0.0;
            r = (uint8_t)(r * shade);
            g = (uint8_t)(g * shade);
            b = (uint8_t)(b * shade);
            row[x * 4 + 0] = r;
            row[x * 4 + 1] = g;
            row[x * 4 + 2] = b;
            row[x * 4 + 3] = 0xFF;
        }
    }
    Gfx_UpdateTexture(t, px, pitch);
    free(px);
    return t;
}

void CrtEffects_Apply(void)
{
    if (!CrtEffects_Active()) return;

    int outW = 0, outH = 0;
    Gfx_OutputSize(&outW, &outH);
    if (outW <= 0 || outH <= 0) return;

    if (!s_tex || outW != s_builtW || outH != s_builtH || !cfg_same(&s_cfg, &s_builtCfg)) {
        if (s_tex) { Gfx_DestroyTexture(s_tex); s_tex = NULL; }
        s_tex = build(outW, outH);
        if (!s_tex) return;
        s_builtW = outW; s_builtH = outH; s_builtCfg = s_cfg;
        fprintf(stderr, "[crt] overlay %dx%d: mask=%d strength=%d vignette=%d corners=%d\n",
                outW, outH, (int)s_cfg.mask, s_cfg.mask_strength,
                s_cfg.vignette, s_cfg.corners);
    }

    /* Draw in OUTPUT pixels: through the picture's letterboxed mapping the grille would be
     * rescaled and alias to mush. */
    const GfxSpace space = Gfx_Space();
    const GfxBlend blend = Gfx_Blend();
    Gfx_SetSpace(GFX_SPACE_OUTPUT);
    Gfx_SetBlend(GFX_BLEND_MOD);
    Gfx_DrawTexture(s_tex, NULL, NULL);
    Gfx_SetBlend(blend);
    Gfx_SetSpace(space);
}

#endif /* SWOS_SDL_BUILD */
