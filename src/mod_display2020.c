/*
 * mod_display2020.c -- SWOS 2020's result display, as a canonical mod (bin/mods/display2020;
 * Playaveli's for SWOS United, brought into OpenSWOS 2026-11-10 -- the owner's decision -- with its
 * slug and uuid, so SWOS United's copy is this one). SWOS 2020's --new-show-result and
 * --remove-darken-banner, as two settings (OPTIONS -> MODS -> SWOS 2020 RESULT DISPLAY, or
 * OPTIONS -> SETTINGS -> DISPLAY; SWOS.ini [mod.display2020]):
 *
 *   kits = on | off      SWOS 2020's --new-show-result (render.cpp showTeamIconsLarge): on the
 *                        result (half time, full time, a goal) each side's kit at twice the
 *                        scoreboard's size -- under the flags mod's flags, in the same place with
 *                        that mod off (the owner's place, 2026-11-07, not SWOS 2020's beside the
 *                        score) -- and, 400 columns wide or more, the team names 14 columns
 *                        further out (its SR_TEAM_GAP_X).
 *   band = smooth | game | off
 *                        the result's band: "smooth" darkened evenly across the width (SWOS 2020's
 *                        drawDarkenRect over the wide screen's sides, render.cpp:4183), in place
 *                        of the Amiga's checker baked into its grid sprites; "game" the game's own
 *                        grid; "off" no band at all -- SWOS 2020's --remove-darken-banner, which
 *                        turns its darkening off (render.cpp:4180), with the grid gone too so
 *                        nothing darkens the pitch under the result.
 *
 * SWOS 2020's scoreboard (--new-scoreboard; SWOS United's first display2020 drew its top-left
 * corner) is the scoreboard mod's (src/mod_scoreboard.c, every corner): not drawn here twice.
 *
 * How: the engine's hooks (include/mod_api.h). spriteDraw leaves the grid sprites out of the
 * picture (band smooth or off), notes the band's rows (and the frame buffer's pixels there before
 * the result's text) and moves the names; frameUnder (under the other mods: the flags go over it)
 * draws the band's darkening and the kits into an RGBA layer the picture's size, over the finished
 * picture through gfx.h -- either presenter. Halving a colour is black at half alpha over it; the
 * band's text (what changed in the frame buffer since the snapshot, or what the HUD layer holds
 * with the sub-pixel scroll on) is left clear, so it stays bright. Drawing only: no sprite, no match
 * state changes. SDL builds.
 */
#include "mod_api.h"

#ifdef SWOS_SDL_BUILD

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game_types.h"
#include "display.h"            /* _screen_width, _screen_height, FRONTBUFFER_ROW_STRIDE_BYTES */
#include "gfx.h"
#include "platform_sdl.h"       /* Platform_FrameShown, Platform_HudLayerBuffer, Platform_PaletteRgb */

#define SLUG "display2020"

extern TeamGame  _topTeamInGame, _bottomTeamInGame;
extern uint16_t  _trainingGame, _replayState;
extern uint16_t  _playGame;
extern volatile uintptr_t _frontbufferBasePtr;
extern Sprite    _team1NameSprite, _team2NameSprite;
extern Sprite    _gridUpperRowLeftSprite, _gridUpperRowRightSprite, _gridMiddleRowLeftSprite,
                 _gridMiddleRowRightSprite, _gridBottomRowLeftSprite, _gridBottomRowRightSprite,
                 _gridUpperRowExtraSprite, _gridMiddleRowExtraSprite, _gridBottomRowExtraSprite;

enum { BAND_SMOOTH, BAND_GAME, BAND_OFF };

static int32_t s_test, s_testBand, s_testKits;     /* SWOS_DISPLAY2020_TEST (below) */

static int32_t kits_on(void) { return ModApi_SettingInt(SLUG, "kits", 1); }

static int32_t band_mode(void)
{
    const char *b = ModApi_Setting(SLUG, "band", "smooth");
    return strcmp(b, "game") == 0 ? BAND_GAME : strcmp(b, "off") == 0 ? BAND_OFF : BAND_SMOOTH;
}

/* When the match last drew its sprites. `_playGame` is match state and stays set when a live match
 * ends early and goes back to the lobby, so the band is drawn only while the match is still drawing
 * (or paused: frameKept) -- the HUD layer's 100 ms rule (platform_sdl.c). */
static uint64_t s_drawnMs;

static int32_t fresh(void)
{
    return s_drawnMs && Platform_NowMs() - s_drawnMs <= 100;
}

static void frame_kept(void)
{
    if (s_drawnMs)
        s_drawnMs = Platform_NowMs();
}

/* ---- the layer: RGBA, the picture's size, drawn over it ---- */

static uint32_t   *s_layer;                     /* 0xAARRGGBB, _screen_width a row */
static int32_t     s_layerW, s_layerH;
static GfxTexture *s_tex;
static int32_t     s_texW, s_texH;
static uint8_t    *s_rgba;                      /* (the texture's bytes: R, G, B, A) */
static int32_t     s_dirty;                     /* the layer holds something this frame */

static int32_t layer_ready(void)
{
    if (s_layer && s_layerW == _screen_width && s_layerH == _screen_height)
        return 1;
    free(s_layer);
    free(s_rgba);
    s_layerW = _screen_width;
    s_layerH = _screen_height;
    s_layer = (uint32_t *)calloc((size_t)s_layerW * s_layerH, 4);
    s_rgba = (uint8_t *)malloc((size_t)s_layerW * s_layerH * 4);
    return s_layer && s_rgba;
}

static uint32_t argb_of(int32_t index)
{
    uint8_t r, g, b;
    Platform_PaletteRgb(index & 0xFF, &r, &g, &b);
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static void put(int32_t x, int32_t y, uint32_t c)
{
    if (x >= 0 && y >= 0 && x < s_layerW && y < s_layerH)
        s_layer[(uint32_t)y * (uint32_t)s_layerW + (uint32_t)x] = c;
}

static const char *const k_uniform[4][8] = {
    { "........", ".12221..", ".12221+.", "..222++.", "..333+..", "..333+..", "...+++..", "........" },
    { "........", ".21112..", ".21112+.", "..111++.", "..333+..", "..333+..", "...+++..", "........" },
    { "........", ".12121..", ".12121+.", "..212++.", "..333+..", "..333+..", "...+++..", "........" },
    { "........", ".11111..", ".12221+.", "..111++.", "..333+..", "..333+..", "...+++..", "........" },
};

/* SWOS 2020's showUniform: `scale` 2 (the result's), its loops running 0..7*scale inclusive. */
static void kit(int32_t x, int32_t y, const TeamGame *t, int32_t scale)
{
    const char *const *art = k_uniform[(uint16_t)t->prShirtType & 3];
    for (int32_t j = 0; j <= 7 * scale; j++)
        for (int32_t i = 0; i <= 7 * scale; i++) {
            const char c = art[j / scale][i / scale];
            const int32_t col = c == '1' ? t->prShirtCol : c == '2' ? t->prStripesCol :
                                c == '3' ? t->prShortsCol : c == '+' ? 19 : -1;
            if (col >= 0)
                put(x + i, y + j, argb_of(col));
        }
}

/* ---- the result's band ----
 * DrawQueuedSprites leaves the grid sprites out and notes their rows, with the frame buffer's
 * pixels there before the result's text; at the present the band is darkened wherever the text did
 * not draw, and the kits go over it. Two copies, one per frame buffer (the game draws into one
 * while the other is presented). */
typedef struct {
    uintptr_t fb;                   /* the frame buffer it was drawn into */
    int32_t   y0, y1;               /* its rows, y1 exclusive; y1 <= y0: none */
    uint8_t  *snap;                 /* the frame buffer's rows before the result's text */
} Band;
static Band s_band[2];

static int32_t wide_layout(void) { return _screen_width >= 400; }

static Band *band_for(uintptr_t fb, int32_t take)
{
    int32_t i;
    for (i = 0; i < 2; i++)
        if (s_band[i].fb == fb)
            return &s_band[i];
    if (!take)
        return NULL;
    i = s_band[0].fb && !s_band[1].fb ? 1 : (s_band[0].fb ? (s_band[0].y1 > s_band[1].y1 ? 1 : 0) : 0);
    s_band[i].fb = fb;
    s_band[i].y0 = s_band[i].y1 = 0;
    return &s_band[i];
}

static int32_t is_grid(const Sprite *spr)
{
    return spr == &_gridUpperRowLeftSprite || spr == &_gridUpperRowRightSprite ||
           spr == &_gridMiddleRowLeftSprite || spr == &_gridMiddleRowRightSprite ||
           spr == &_gridBottomRowLeftSprite || spr == &_gridBottomRowRightSprite ||
           spr == &_gridUpperRowExtraSprite || spr == &_gridMiddleRowExtraSprite ||
           spr == &_gridBottomRowExtraSprite;
}

/* A grid sprite: left out, its rows noted and copied as they are before the result's text. */
static int32_t band_grid(int16_t y, int32_t h)
{
    Band *b = band_for(_frontbufferBasePtr, 1);
    int32_t y0, y1;
    if (!b->snap)
        b->snap = (uint8_t *)malloc((size_t)PITCH_VIEW_MAX_WIDTH * 288);
    if (!b->snap)
        return 0;                                   /* the game's own band, then */
    y0 = y - (wide_layout() ? 0 : 7);
    y1 = y + h;
    if (y0 < 0) y0 = 0;
    if (y1 > _screen_height) y1 = _screen_height;
    if (y1 <= y0)
        return 1;
    for (int32_t r = y0; r < y1; r++)
        memcpy(b->snap + (size_t)r * _screen_width,
               (const uint8_t *)_frontbufferBasePtr + (size_t)r * FRONTBUFFER_ROW_STRIDE_BYTES,
               (size_t)_screen_width);
    if (b->y1 <= b->y0) {
        b->y0 = y0;
        b->y1 = y1;
    } else {
        if (y0 < b->y0) b->y0 = y0;
        if (y1 > b->y1) b->y1 = y1;
    }
    return 1;
}

/* The band of the frame shown: black at half alpha (a colour halved) on every pixel the result's
 * text did not draw -- the frame buffer's, against the snapshot, and the HUD layer's (the text is
 * there with the sub-pixel scroll on). */
static int32_t draw_band(void)
{
    const uint8_t *fb8 = Platform_FrameShown();
    const uint8_t *hud = Platform_HudLayerBuffer();
    const Band *b = fresh() && fb8 ? band_for((uintptr_t)fb8, 0) : NULL;
    if (!b || b->y1 <= b->y0)
        return 0;
    for (int32_t y = b->y0; y < b->y1 && y < s_layerH; y++) {
        const uint8_t *now = fb8 + (size_t)y * FRONTBUFFER_ROW_STRIDE_BYTES;
        const uint8_t *was = b->snap + (size_t)y * _screen_width;
        const uint8_t *h = hud ? hud + (size_t)y * _screen_width : NULL;
        uint32_t *p = s_layer + (size_t)y * s_layerW;
        for (int32_t x = 0; x < s_layerW; x++)
            if (now[x] == was[x] && !(h && h[x]))
                p[x] = 0x80000000u;
    }
    s_testBand++;
    return 1;
}

/* The kits under the flags (the flags mod's: 22 x 16, 6 in from the banner's edge, centred on the
 * names' row) -- in the same place with the flags mod off. */
static int32_t draw_kits(void)
{
    ModResultBanner rb;
    if (!kits_on() || _replayState || _trainingGame || !fresh() || !ModApi_ResultBanner(&rb))
        return 0;
    const int32_t y = rb.nameTop + (rb.nameHeight - 16) / 2 + 16 + 3;
    const int32_t x1 = rb.left + 6 + (22 - 16) / 2;
    const int32_t x2 = rb.right - 6 - 22 + (22 - 16) / 2;
    kit(x1, y, &_topTeamInGame, 2);
    kit(x2, y, &_bottomTeamInGame, 2);
    s_testKits++;
    return 1;
}

/* ---- the hooks ---- */

static void sprites_begin(void)
{
    Band *b = band_for(_frontbufferBasePtr, 1);
    b->y0 = b->y1 = 0;
    s_drawnMs = _playGame ? Platform_NowMs() : 0;
}

static int32_t sprite_draw(const Sprite *spr, const SpriteGraphics *gfx, int16_t *x, int16_t *y)
{
    if (!_playGame)
        return 0;
    if (spr == &_team1NameSprite || spr == &_team2NameSprite) {
        if (wide_layout() && kits_on())
            *x += spr == &_team1NameSprite ? -14 : 14;
        return 0;
    }
    if (gfx && is_grid(spr)) {
        const int32_t mode = band_mode();
        if (mode == BAND_OFF)
            return 1;                               /* (no band: the result's text on the pitch) */
        if (mode == BAND_SMOOTH)
            return band_grid(*y, gfx->nlines);
    }
    return 0;
}

static void frame_under(void)
{
    if (!_playGame || !layer_ready())
        return;
    if (s_dirty) {
        memset(s_layer, 0, (size_t)s_layerW * s_layerH * 4);
        s_dirty = 0;
    }
    s_dirty |= draw_band();
    s_dirty |= draw_kits();
    if (!s_dirty)
        return;
    if (!s_tex || s_texW != s_layerW || s_texH != s_layerH) {
        if (s_tex)
            Gfx_DestroyTexture(s_tex);
        s_tex = Gfx_CreateTexture(s_layerW, s_layerH, 0);
        s_texW = s_layerW;
        s_texH = s_layerH;
        if (!s_tex)
            return;
    }
    for (int32_t i = 0; i < s_layerW * s_layerH; i++) {     /* ARGB -> R, G, B, A bytes */
        const uint32_t v = s_layer[i];
        s_rgba[i * 4 + 0] = (uint8_t)(v >> 16);
        s_rgba[i * 4 + 1] = (uint8_t)(v >> 8);
        s_rgba[i * 4 + 2] = (uint8_t)v;
        s_rgba[i * 4 + 3] = (uint8_t)(v >> 24);
    }
    Gfx_UpdateTexture(s_tex, s_rgba, s_layerW * 4);
    {
        const GfxSpace space = Gfx_Space();
        const GfxBlend blend = Gfx_Blend();
        Gfx_SetSpace(GFX_SPACE_PICTURE);
        Gfx_SetBlend(GFX_BLEND_ALPHA);
        Gfx_DrawTexture(s_tex, NULL, NULL);
        Gfx_SetBlend(blend);
        Gfx_SetSpace(space);
    }
}

/* SWOS_DISPLAY2020_TEST=1 (with SWOS_MOD_MATCH_TEST=1, whose match has the kick-off's result up):
 * at frame 600 PASS when, by its settings, the band and the kits were drawn -- or, band game/off
 * and kits off, were not. */
static void match_frame(void)
{
    if (!s_test || ModApi_MatchFrames() != 600)
        return;
    const int32_t wantBand = band_mode() == BAND_SMOOTH, wantKits = kits_on();
    const int32_t ok = (s_testBand > 0) == wantBand && (s_testKits > 0) == wantKits;
    fprintf(stdout, "DISPLAY2020 TEST %s: band %s drawn %d times, kits %s drawn %d times\n", ok ? "PASS" : "FAIL",
            ModApi_Setting(SLUG, "band", "smooth"), (int)s_testBand, wantKits ? "on" : "off", (int)s_testKits);
    exit(ok ? 0 : 1);
}

static void init(void) { s_test = getenv("SWOS_DISPLAY2020_TEST") != NULL; }

const ModDef ModDef_display2020 = {
    .slug = SLUG,
    .init = init,
    .frameUnder = frame_under,      /* under the flags mod and the rest: the band is shading */
    .spritesBegin = sprites_begin,
    .spriteDraw = sprite_draw,
    .frameKept = frame_kept,
    .matchFrame = match_frame,
};

#endif /* SWOS_SDL_BUILD */
