/*
 * pixel_filter.h -- SWOS 2020's picture filters, the ones that are ours to write (src/pixel_filter.c;
 * launcher options, agent B; docs/RENDERING.md "Pixel filters").
 *
 * [video] pixel_filter = none | simple2x | scale2x | scale3x. The frame, as ARGB, made 2 or 3
 * times bigger before the presenter scales it to the window: simple2x doubles every pixel (SWOS
 * 2020's Simple2x), scale2x / scale3x are Andrea Mazzoleni's published Scale2x / Scale3x rules
 * (AdvMAME2x / 3x), written here from the algorithm's description. SWOS 2020's LQ2x/3x, HQ2x/3x
 * (hqx: LGPL) and 2xBRZ/3xBRZ (xBRZ: GPL 3) are not here: their only implementations are under
 * licences OpenSWOS does not take.
 *
 * Drawing only: the frame the game drew is read, never changed -- the simulation, the rollback
 * state, replays and recordings never see a filter.
 */
#ifndef SWOS_PIXEL_FILTER_H
#define SWOS_PIXEL_FILTER_H

#include <stdint.h>

enum {
    PIXEL_FILTER_NONE,
    PIXEL_FILTER_SIMPLE2X,
    PIXEL_FILTER_SCALE2X,
    PIXEL_FILTER_SCALE3X,
    PIXEL_FILTER_COUNT
};

int32_t     PixelFilter_Parse(const char *name);        /* PIXEL_FILTER_*, -1 not one */
const char *PixelFilter_Name(int32_t filter);           /* "none", "simple2x", ... */
const char *PixelFilter_Label(int32_t filter);          /* "NONE", "SIMPLE 2X", ... (the menu's) */
int32_t     PixelFilter_Factor(int32_t filter);         /* 1, 2, 2, 3 */

/* `src` w x h ARGB (`srcPitch` pixels a row) into `dst`, (w * factor) x (h * factor), tightly
 * packed. A filter of factor 1 copies. */
void        PixelFilter_Run(int32_t filter, const uint32_t *src, int32_t w, int32_t h, int32_t srcPitch,
                            uint32_t *dst);

/* --pixel-filter-selftest: each filter on small pictures with known answers. Failures. */
int32_t     PixelFilter_SelfTest(void);

#endif
