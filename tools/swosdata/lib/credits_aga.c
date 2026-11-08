/*
 * credits_aga.c -- the Amiga AGA CREDITS screen's backdrop; see credits_aga.h.
 * Ported from scripts/make_credits_aga.py (keep the constants in step with it).
 */
#include "credits_aga.h"
#include "amiga_raw.h"

#include <stdio.h>
#include <string.h>

#define CA_W        352     /* a full-screen overlay */
#define CA_H        272
#define CA_PHOTO    40      /* px, square */
#define CA_STEP     54      /* photo + 14 px: six across the picture */
#define CA_STRIP    7       /* photos in credits_icons.bmp */
#define CA_COUNT    6
#define CA_PHOTO_X  (176 - ((CA_COUNT - 1) * CA_STEP + CA_PHOTO) / 2)  /* centred on text x 176 */
#define CA_PHOTO_Y  129
#define CA_FG_SHIFT 16      /* menu x -> picture x on AGA */
#define CA_PAL_BASE 128
#define CA_SHADOW   3       /* the menu palette's black */

/* The photos shown, in the order drawn, by their place in the strip:
 * BENBAKER76, ZKZ, PLAYAVELI, STARWINDZ, THD79, REDHAIR (menus.c k_creditsAgaNames). */
static const int32_t k_photos[CA_COUNT] = { 1, 3, 0, 2, 4, 5 };

int32_t Sd_MakeCreditsAga(const SgImage *bg, const SgImage *strip, SgBuf *outRaw, char *err)
{
    outRaw->data = NULL;
    outRaw->size = 0;
    if (bg->w != CA_W || bg->h != CA_H) {
        snprintf(err, 256, "credits: MENUBG is %dx%d, not %dx%d", (int)bg->w, (int)bg->h, CA_W, CA_H);
        return -1;
    }
    if (strip->w != CA_PHOTO * CA_STRIP || strip->h != CA_PHOTO) {
        snprintf(err, 256, "credits: credits_icons is %dx%d, not a %dx%d strip",
                 (int)strip->w, (int)strip->h, CA_PHOTO * CA_STRIP, CA_PHOTO);
        return -1;
    }
    uint32_t nbg = (uint32_t)bg->w * (uint32_t)bg->h, nst = (uint32_t)strip->w * (uint32_t)strip->h;
    uint8_t bgMax = 0, stMin = 255;
    for (uint32_t i = 0; i < nbg; i++)
        if (bg->px[i] > bgMax) bgMax = bg->px[i];
    for (uint32_t i = 0; i < nst; i++)
        if (strip->px[i] < stMin) stMin = strip->px[i];
    if (bgMax >= CA_PAL_BASE || stMin < CA_PAL_BASE) {
        snprintf(err, 256, "credits: the backdrop and the photos share palette entries");
        return -1;
    }

    SgImage out;
    if (SgImage_Init(&out, CA_W, CA_H)) {
        snprintf(err, 256, "credits: out of memory");
        return -1;
    }
    memcpy(out.px, bg->px, nbg);
    for (int32_t p = 0; p < CA_COUNT; p++) {
        int32_t x0 = CA_FG_SHIFT + CA_PHOTO_X + p * CA_STEP;
        for (int32_t y = 0; y < CA_PHOTO; y++)
            for (int32_t x = 0; x < CA_PHOTO; x++)
                out.px[(CA_PHOTO_Y + y + 1) * CA_W + x0 + x + 1] = CA_SHADOW;
        for (int32_t y = 0; y < CA_PHOTO; y++)
            for (int32_t x = 0; x < CA_PHOTO; x++)
                out.px[(CA_PHOTO_Y + y) * CA_W + x0 + x] =
                    strip->px[y * strip->w + k_photos[p] * CA_PHOTO + x];
    }
    int32_t rc = Sg_AmigaRawEncode(&out, 8, outRaw, err);
    SgImage_Free(&out);
    return rc;
}
