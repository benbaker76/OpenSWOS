/*
 * head_dump.c -- SWOS_DUMP_HEADS=<folder>: where each raw player frame's head is (SDL builds; a
 * tool for the hair overlays, docs/MODS.md). Called by ConvertColorsAndTextSprites before team 1's
 * raw set (sprites 227..327) is recoloured: for each frame its size, its centre, and the box of its
 * hair pixels (raw colours 9, 12, 13 -- the skin-and-hair tables' hair entries; skin is 4, 5, 6),
 * relative to the centre, into heads.txt; and heads.png, every frame 4x, hair red, skin pink, the
 * rest grey, the centre a blue dot. Once per run.
 */
#ifdef SWOS_SDL_BUILD

#include "game_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint8_t *_spriteGraphicsPtr;
extern int stbi_write_png(char const *filename, int w, int h, int comp, const void *data, int stride);

enum { kFirst = 227, kCount = 101, kScale = 4, kCell = 32, kCols = 16 };

static int is_hair(uint8_t c) { return c == 9 || c == 12 || c == 13; }
static int is_skin(uint8_t c) { return c == 4 || c == 5 || c == 6; }

void HeadDump_Raw(void)
{
    static int done;
    const char *dir = getenv("SWOS_DUMP_HEADS");
    if (done || !dir || !dir[0])
        return;
    done = 1;
    char path[1200];
    snprintf(path, sizeof path, "%s/heads.txt", dir);
    FILE *txt = fopen(path, "w");
    if (!txt)
        return;
    fprintf(txt, "# frame w h centerX centerY  hair: minX minY maxX maxY count (relative to the centre)\n");
    const int rows = (kCount + kCols - 1) / kCols, W = kCols * kCell * kScale, H = rows * kCell * kScale;
    uint8_t *img = (uint8_t *)calloc((size_t)W * H, 3);
    SpriteGraphics **tab = (SpriteGraphics **)_spriteGraphicsPtr;
    for (int f = 0; f < kCount; f++) {
        const SpriteGraphics *s = tab[kFirst + f];
        if (!s || !s->sprData) continue;
        const int pitch = s->wquads * 16;
        int minX = 999, minY = 999, maxX = -999, maxY = -999, n = 0;
        const int ox = (f % kCols) * kCell * kScale, oy = (f / kCols) * kCell * kScale;
        for (int y = 0; y < s->nlines; y++)
            for (int x = 0; x < s->pixWidth; x++) {
                const uint8_t c = s->sprData[y * pitch + x];
                if (is_hair(c)) {
                    const int rx = x - s->centerX, ry = y - s->centerY;
                    if (rx < minX) minX = rx; if (ry < minY) minY = ry;
                    if (rx > maxX) maxX = rx; if (ry > maxY) maxY = ry;
                    n++;
                }
                if (!img || !c || x >= kCell || y >= kCell) continue;
                const uint8_t r = is_hair(c) ? 230 : is_skin(c) ? 240 : 150;
                const uint8_t g = is_hair(c) ? 40  : is_skin(c) ? 180 : 150;
                const uint8_t b = is_hair(c) ? 40  : is_skin(c) ? 170 : 150;
                for (int dy = 0; dy < kScale; dy++)
                    for (int dx = 0; dx < kScale; dx++) {
                        uint8_t *p = img + 3 * ((size_t)(oy + y * kScale + dy) * W + ox + x * kScale + dx);
                        p[0] = r; p[1] = g; p[2] = b;
                    }
            }
        if (img && s->centerX >= 0 && s->centerX < kCell && s->centerY >= 0 && s->centerY < kCell) {
            uint8_t *p = img + 3 * ((size_t)(oy + s->centerY * kScale + 1) * W + ox + s->centerX * kScale + 1);
            p[0] = 40; p[1] = 80; p[2] = 255;
        }
        if (getenv("SWOS_DUMP_HEADS_RAW")) {       /* every frame's raw colours, as hex digits */
            fprintf(txt, "# frame %d\n", f);
            for (int y = 0; y < s->nlines; y++) {
                fputs("#   ", txt);
                for (int x = 0; x < s->pixWidth; x++) {
                    const uint8_t c = s->sprData[y * pitch + x];
                    fputc(c ? "0123456789ABCDEF"[c & 15] : '.', txt);
                }
                fputc('\n', txt);
            }
        }
        if (n)
            fprintf(txt, "%3d %2d %2d %3d %3d  %3d %3d %3d %3d %3d\n", f, s->pixWidth, s->nlines, s->centerX,
                    s->centerY, minX, minY, maxX, maxY, n);
        else
            fprintf(txt, "%3d %2d %2d %3d %3d  -\n", f, s->pixWidth, s->nlines, s->centerX, s->centerY);
    }
    fclose(txt);
    if (img) {
        snprintf(path, sizeof path, "%s/heads.png", dir);
        stbi_write_png(path, W, H, 3, img, W * 3);
        free(img);
    }
    fprintf(stderr, "[heads] %d raw frames from %d into %s\n", kCount, kFirst, dir);
}

#endif /* SWOS_SDL_BUILD */
