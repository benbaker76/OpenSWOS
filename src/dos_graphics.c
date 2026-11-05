/*
 * dos_graphics.c — DOS SWOS graphics loaders → our 8bpp chunky internal format.
 * PC-only optional feature (gDosGraphics). See docs/DOS_GRAPHICS.md.
 *
 * Palette + decode tables transcribed from the working swos-port-c reference
 * (vm_stubs.cpp, ultimately DosPalette.cs). DOS files are little-endian.
 */
#include "team_colors.h"
#include "dos_graphics.h"
#include "display.h"
#include "dos_mode.h"
#include <string.h>
#ifdef SWOS_SDL_BUILD
#include "pitch_map.h"
#include <stdlib.h>
#include <stdio.h>
#endif

/* DOS-mode flags (declared in dos_mode.h). Defined here — a SYNCED file — so the
 * Amiga build links even when a synced hook (pitch.c, asset_loader.c, …)
 * references them. main.c, which sets them from the CLI/INI, is SDL-only.
 * Default 0 = the normal Amiga graphics/gameplay. */
int _gDosGraphics = 0;
int _gDosGameplay = 0;
/* 1 = play matches in the DOS release's 320x200 view (display.h, DOS_VIEW_*).
 * Menus keep their own 336x272 -- they are Amiga artwork and do not fit 200
 * lines, which is what cut the main menu in half when this was applied at
 * startup rather than per scene. */
int _gDosView = 0;
/* What a DOS-view match is RENDERED at: 320 wide, or more on a wide display.
 * The simulation is pinned to 320x200 regardless, so the extra columns are
 * margin the game does not know about -- the same bargain the Amiga view's
 * widescreen makes (display.h, "the SIMULATION view"). */
int _dos_view_width = DOS_VIEW_WIDTH;
/* The DOS view's PICTURE height: 200, or 272 with widescreen on (main.c). The match is
 * simulated at 320x200 either way; a taller picture shows more pitch around it
 * (camera.c Render_CameraY, docs/WIDESCREEN.md "The DOS view"). */
int _dos_view_height = DOS_VIEW_HEIGHT;
int _gWidescreen  = 0;   /* 1 = wider 16:9 pitch view. SDL main sets from opts; Amiga stays 0 */
char _gDosGraphicsDir[256] = "grafs_dos";   /* asset dir; see dos_mode.h */

/* ---- base game palette: 256 RGB triples, 6-bit VGA (0..63) ---- */
static const uint8_t kDosGamePalette[768] = {
     28,20, 0, 38,38,38, 63,63,63,  0, 0, 0, 25, 8, 0, 46,17, 0, 63,25, 0, 24,20, 0,
      0, 8, 0, 20,20, 0, 63, 0, 0,  0, 0,63, 25, 0, 8, 38,38,63,  0,55, 0, 63,63, 0,
     39,50, 0, 43,43,43, 63,63,63,  6, 6, 6, 31, 9, 0, 52,18, 0, 63,32,13, 33,50, 0,
      0,14, 0, 50,49, 0, 63,27,27, 30,31,63, 31, 0,10, 47,47,63,  0,60, 0, 63,63,47,
     27,34, 0, 29,29,29, 52,52,52,  0, 0, 0, 18, 5, 0, 30, 9, 0, 46,16, 0, 22,33, 0,
      0, 1, 0, 33,32, 0, 51, 0, 0,  0, 0,51, 15, 0, 5, 30,31,51,  0,43, 0, 51,50, 0,
      0, 0, 0,  5, 5, 5, 11,11,11, 17,17,17, 23,23,23, 29,29,29, 34,34,34, 40,40,40,
     46,46,46, 52,52,52, 57,57,57, 63,63,63, 63,54,47, 63,44,32, 63,35,16, 63,25, 0,
     57,22, 0, 50,19, 0, 44,16, 0, 38,13, 0, 32,11, 0, 25, 8, 0,  0, 0, 0,  0, 0,28,
      0, 0,32,  0, 0,37,  0, 0,42, 11,11,49, 24,26,56, 37,38,57, 50,46,41, 44,30, 0,
     17,11, 0, 23,14, 0, 58,58,58, 56,53,53, 55,49,49, 54,46,46, 53,42,42, 52,39,39,
     51,35,35, 44,30,30, 36,25,25, 29,20,20, 22,15,15, 14,10,10,  7, 5, 5,  0, 0, 0,
     63,58,52, 58,52,44, 53,47,38, 49,42,32, 44,37,27, 37,31,23, 31,25,19, 24,20,14,
     21,17,13,  9, 7, 5, 21,15, 2, 46,39,30,  0, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, 0,
      0,38,63, 20,20,20, 28,28,28,  8, 8, 8, 60, 0, 0, 60,28,28, 32,32,32, 36,36,36,
     40,40,40, 44,44,44, 60,44,44, 48,48,48, 52,52,52, 56,56,56, 60,60,60, 63,19,19,
     13, 9, 0, 23,23,23, 48,48,48,  0, 0, 0, 10, 3, 0, 31,11, 0, 48,18, 0,  9, 7, 0,
      0, 0, 0,  5, 5, 0, 48, 0, 0,  0, 0,48,  0, 0, 0, 29,29,48,  0,40, 0, 48,48, 0,
     27,35, 0, 28,28,28, 48,48,48,  0, 0, 0, 16, 4, 0, 37,12, 0, 48,24,10, 23,35, 0,
      0, 0, 0, 35,34, 0, 48,20,20, 23,23,48,  0, 0, 0, 36,36,48,  0,45, 0, 48,48,36,
     15,19, 0, 14,14,14, 37,37,37,  0, 0, 0,  3, 0, 0, 15, 4, 0, 31,10, 0, 11,18, 0,
      0, 0, 0, 18,17, 0, 36, 0, 0,  0, 0,36,  0, 0, 0, 21,21,36,  0,28, 0, 36,35, 0,
      0, 0, 0,  0, 0, 0,  0, 0, 0,  2, 2, 2,  8, 8, 8, 14,14,14, 19,19,19, 25,25,25,
     31,31,31, 37,37,37, 42,42,42, 48,48,48, 48,41,36, 48,33,24, 48,26,12, 48,18, 0,
     42,15, 0, 35,13, 0, 29,10, 0, 23, 7, 0, 17, 5, 0, 10, 3, 0,  0, 0, 0,  0, 0,13,
      0, 0,17,  0, 0,22,  0, 0,27,  7, 7,34, 17,19,41, 27,28,42, 35,32,28, 29,19, 0,
      2, 1, 0,  8, 4, 0, 43,43,43, 41,38,38, 40,36,36, 39,33,33, 38,30,30, 37,27,27,
     36,24,24, 29,20,20, 21,14,14, 14, 9, 9,  7, 4, 4,  0, 0, 0,  0, 0, 0,  0, 0, 0,
     48,44,39, 43,38,32, 38,33,27, 34,29,22, 29,24,17, 22,18,13, 16,12, 9,  9, 7, 5,
      6, 4, 3,  0, 0, 0,  6, 4, 0, 31,26,20,  0, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, 0,
      0,28,48,  5, 5, 5, 13,13,13,  0, 0, 0, 45, 0, 0, 45,21,21, 17,17,17, 21,21,21,
     25,25,25, 29,29,29, 45,33,33, 33,33,33, 37,37,37, 41,41,41, 45,45,45, 48,14,14,
};

/* ---- pitch-condition overlay: 9 palette entries replaced per pitch type ---- */
static const uint8_t kPitchPaletteIndices[9] = { 0, 7, 9, 78, 79, 80, 81, 106, 107 };

static const uint8_t kPitchPalettes[7][27] = {
    /* 0 Frozen */ {36,36,24, 32,36,24, 28,36,24, 47,47,40, 38,38,33, 15,15,13, 22,22,19, 34,29,21, 40,39,34},
    /* 1 Muddy  */ {28,20, 0, 24,20, 0, 20,20, 0, 50,46,41, 44,30, 0, 17,11, 0, 23,14, 0, 21,15, 2, 46,39,30},
    /* 2 Wet    */ {12,36, 0, 28,36, 0, 12,36, 0, 46,48,40, 33,43,12, 15,22, 0, 18,27, 0, 25,31, 0, 45,45,37},
    /* 3 Soft   */ {12,28, 0, 28,28, 0, 12,28, 0, 45,48,38, 21,37, 9, 11,17, 0, 15,22, 0, 23,22, 0, 41,41,32},
    /* 4 Normal */ {28,36, 0, 24,36, 0, 36,36, 0, 45,48,38, 32,42,11, 15,22, 0, 18,27, 0, 31,32, 0, 48,46,34},
    /* 5 Dry    */ {36,36, 0, 32,36, 0, 28,36, 0, 44,46,37, 33,44,12, 15,22, 0, 18,27, 0, 33,30, 0, 47,41,34},
    /* 6 Hard   */ {36,28, 0, 32,28, 0, 28,28, 0, 50,46,37, 45,34, 0, 19,15, 0, 26,19, 0, 20,25, 0, 44,38,28},
};

/* ---- arcade service-menu palette: 16 TRUE 8-bit RGB entries ----
 * From SVCFONT.bmp's embedded palette (== DosPalette.Service). These are NOT
 * 6-bit-scaled values (e.g. 0xAD = 173 is not a multiple of 4), so store the
 * 8-bit RGB directly — do NOT run them through DosGfx_6to8. */
static const uint8_t kDosServicePalette[48] = {
    0x00,0x00,0x00,  0xAD,0xAD,0xAD,  0xF7,0xF7,0xF7,  0xF7,0x00,0x00,
    0xEF,0x94,0x00,  0xEF,0xEF,0x00,  0x00,0xF7,0x00,  0x00,0x00,0xF7,
    0x73,0xA5,0x73,  0x00,0xD6,0xF7,  0x6B,0x6B,0x6B,  0xDE,0xC6,0x00,
    0x00,0xCE,0xFF,  0xF7,0x7B,0xBD,  0xF7,0x00,0xF7,  0xCE,0xDE,0xFF,
};

void DosGfx_BuildServicePalette(uint8_t *rgb888Out)
{
    memcpy(rgb888Out, kDosServicePalette, sizeof kDosServicePalette);
}

/* Colours another tier has placed here (DosGfx_SetExtraColour). */
static uint8_t s_extra[768];
static uint8_t s_extraSet[256];

void DosGfx_SetExtraColour(int entry, uint8_t r8, uint8_t g8, uint8_t b8)
{
    if (entry < 0 || entry > 255)
        return;
    s_extra[entry * 3 + 0] = r8;
    s_extra[entry * 3 + 1] = g8;
    s_extra[entry * 3 + 2] = b8;
    s_extraSet[entry] = 1;
}

void DosGfx_BuildPalette(int pitchType, uint8_t *rgb888Out)
{
    uint8_t pal6[768];
    memcpy(pal6, kDosGamePalette, sizeof pal6);

    if (pitchType < 0 || pitchType > 6) pitchType = 4;   /* Normal */
    for (int i = 0; i < 9; i++) {
        int dst = kPitchPaletteIndices[i] * 3;
        pal6[dst + 0] = kPitchPalettes[pitchType][i * 3 + 0];
        pal6[dst + 1] = kPitchPalettes[pitchType][i * 3 + 1];
        pal6[dst + 2] = kPitchPalettes[pitchType][i * 3 + 2];
    }
    if (TeamColors_PurpleInMatch()) {   /* purple in maroon's entry (team_colors.c, the Mod's pk_pal) */
        pal6[12 * 3 + 0] = 24; pal6[12 * 3 + 1] = 8; pal6[12 * 3 + 2] = 34;
    }
#ifdef SWOS_SDL_BUILD
    /* A pitch drawn from a map takes the condition's entries from the map's
     * `pitch-<condition>` event (src/pitch_map.c). Through the same six bits as the
     * table above, so the game's own map gives the game's own colours exactly. */
    {
        uint8_t entry[64], colour[64][3];
        int n = PitchMap_Condition(pitchType, entry, colour, 64);
        int differ = 0;
        for (int i = 0; i < n; i++) {
            for (int c = 0; c < 3; c++) {
                uint8_t v = (uint8_t)(colour[i][c] >> 2);
                differ += pal6[entry[i] * 3 + c] != v;
                pal6[entry[i] * 3 + c] = v;
            }
        }
        /* SWOS_PITCH_MAP_CHECK=1: are they the game's own for this condition? */
        {
            static uint8_t told[7];
            if (n && !told[pitchType] && getenv("SWOS_PITCH_MAP_CHECK")) {
                told[pitchType] = 1;
                fprintf(stderr, "[pitch-map] check: condition %d: %d entries from the map, %d "
                                "channels differ from the game's\n", pitchType, n, differ);
            }
        }
    }
#endif
    for (int i = 0; i < 768; i++)
        rgb888Out[i] = DosGfx_6to8(pal6[i]);
    /* After the conversion: these arrive as 8-bit already. */
    for (int i = 0; i < 256; i++) {
        if (!s_extraSet[i])
            continue;
        rgb888Out[i * 3 + 0] = s_extra[i * 3 + 0];
        rgb888Out[i * 3 + 1] = s_extra[i * 3 + 1];
        rgb888Out[i * 3 + 2] = s_extra[i * 3 + 2];
    }
}

int DosGfx_DecodePlanarSprite(const uint8_t *planar, int nlines, int wquads,
                              uint8_t *chunkyOut)
{
    const int lineStride  = wquads * 8;    /* 4 planes * wquads*2 bytes */
    const int planeStride = wquads * 2;    /* one plane, one line */
    const int outStride   = wquads * 16;   /* chunky pixels per line */

    for (int y = 0; y < nlines; y++) {
        const uint8_t *line = planar + (size_t)y * lineStride;
        for (int q = 0; q < wquads; q++) {
            uint8_t pl[4][2];
            for (int p = 0; p < 4; p++) {
                pl[p][0] = line[p * planeStride + q * 2];
                pl[p][1] = line[p * planeStride + q * 2 + 1];
            }
            uint8_t *out = chunkyOut + (size_t)y * outStride + q * 16;
            for (int byteIdx = 0; byteIdx < 2; byteIdx++) {
                for (int bit = 7; bit >= 0; bit--) {
                    int v = ((pl[0][byteIdx] >> bit) & 1)
                          | (((pl[1][byteIdx] >> bit) & 1) << 1)
                          | (((pl[2][byteIdx] >> bit) & 1) << 2)
                          | (((pl[3][byteIdx] >> bit) & 1) << 3);
                    out[byteIdx * 8 + (7 - bit)] = (uint8_t)v;   /* 0..15, MSB=leftmost */
                }
            }
        }
    }
    return nlines * outStride;
}

int DosGfx_ParsePitchMap(const uint8_t *datBuf, uint32_t datSize,
                         uint16_t *tileIndexOut, int maxOut)
{
    int n = (int)(datSize / 4);
    if (n > maxOut) n = maxOut;
    for (int i = 0; i < n; i++) {
        const uint8_t *p = datBuf + (size_t)i * 4;     /* little-endian u32 */
        uint32_t off = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        tileIndexOut[i] = (uint16_t)(off / DOS_TILE_BYTES);
    }
    return n;
}
