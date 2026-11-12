/*
 * mod_scoreboard.c -- the new scoreboard mod (canonical, bin/mods/scoreboard; docs/MODS.md §35):
 * SWOS 2020's --new-scoreboard=1..4 (game.cpp's clock and score at g_scoresMode's corner,
 * render.cpp showTeamIcons' darkened box and the two kits; UpdateTime_395 / positionTimeSprite
 * taking the game's own clock away, SetCurrentPlayerName_163 the name under the box). SDL builds.
 *
 * Always up in a match, at the corner its setting names (OPTIONS -> MODS -> NEW SCOREBOARD):
 * [ CLOCK' | team 1's kit  SCORE  team 2's kit ] on a darkened box. The game's own clock is not
 * drawn; at the top left the name of the player on the ball goes 7 lower, under the box. The
 * bottom corners give way to the result banner. Not in a replay. "changes": "cosmetic".
 *
 * The setting's corners are SWOS 2020's modes: top-left 4 (its default), bottom-left 1,
 * bottom-right 2, top-right 3.
 */
#ifdef SWOS_SDL_BUILD
#include "mod_api.h"
#include "mod_draw8.h"
#include "gfx.h"
#include "platform_sdl.h"     /* Platform_PaletteRgb, Platform_NowMs */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TOP_LEFT = 4, BOTTOM_LEFT = 1, BOTTOM_RIGHT = 2, TOP_RIGHT = 3 };

static int32_t s_test, s_testDrawn;     /* SWOS_SCOREBOARD_TEST (below) */

static int32_t corner(void)
{
    const char *c = ModApi_Setting("scoreboard", "corner", "top-left");
    return strcmp(c, "bottom-left") == 0 ? BOTTOM_LEFT : strcmp(c, "bottom-right") == 0 ? BOTTOM_RIGHT
         : strcmp(c, "top-right") == 0 ? TOP_RIGHT : TOP_LEFT;
}

static int32_t board_sprite(const struct Sprite *sprite, const struct SpriteGraphics *gfx, int16_t *x, int16_t *y)
{
    (void)gfx; (void)x;
    if (!ModApi_InMatch() || ModApi_InReplay())
        return 0;
    const int32_t kind = ModApi_SpriteKind(sprite);
    if (kind == MODSPRITE_CLOCK)
        return 1;                       /* (positionTimeSprite: 1000 off the screen) */
    if (kind == MODSPRITE_PLAYER_NAME && corner() == TOP_LEFT)
        *y = (int16_t)(*y + 7);
    return 0;
}

/* The board is drawn at the present, over the finished picture (gfx.h, the picture's space):
 * SWOS 2020 drew it over its output, after the scroll. Written into the frame buffer it went
 * with the pitch -- the frame scrolls under the camera by the sub-pixel residual, and the
 * scroll blit keeps rows from the last frame -- so it flickered as the camera moved, and its
 * shading (each pixel's index + 128, SWOS 2020's VGA palette's darker half) found the Amiga
 * palette's upper half, near black (Playa, 2026-11-11). Now: the box a colour halved (black at
 * half alpha, as the result band of display2020), the kits and the text opaque over it. */
enum { kBoxW = 73, kBoxH = 9, kScrH = 12 };

extern volatile uintptr_t _frontbufferBasePtr;
extern int _menu_draw_x_offset;

static uint8_t   *s_scr;                /* kits and text, 8bpp, the frame buffer's stride */
static int32_t    s_scrW;
static uint8_t    s_rgba[kBoxW * kBoxH * 4];
static int32_t    s_boxX, s_boxY;
static uint64_t   s_drawnMs;            /* the last match frame that drew it (the HUD's 100 ms rule) */
static GfxTexture *s_tex;

/* The board's pixels into s_scr, at the box's own (0, 0), `bg` everywhere else. */
static void board_pixels(uint8_t bg)
{
    memset(s_scr, bg, (size_t)s_scrW * kScrH);
    const uintptr_t saved = _frontbufferBasePtr;
    const int savedX = _menu_draw_x_offset;
    _frontbufferBasePtr = (uintptr_t)s_scr;     /* (ModApi_DrawText draws into the frame buffer) */
    _menu_draw_x_offset = 0;
    D8 d = { s_scr, s_scrW, kBoxW, kScrH };
    /* showTeamIcons: the kits at ox + 23 and ox + 63 of its box (ox the box's left) */
    D8_Kit(&d, 0, 23, 1, 1);
    D8_Kit(&d, 1, 63, 1, 1);
    /* the clock, right-aligned at the clock's ox + 20; the score centred at its ox + 47 (that ox
     * 2 in from the box's) */
    char buf[16];
    snprintf(buf, sizeof buf, "%d'", (int)ModApi_MatchMinute());
    ModApi_DrawText(2 + 20 - ModApi_TextWidth(buf, 1), 2, 2 /* kWhiteText2 */, buf, 1);
    int16_t goals[2];
    ModApi_Score(goals);
    snprintf(buf, sizeof buf, "%d-%d", (int)goals[0], (int)goals[1]);
    ModApi_DrawText(2 + 47 - ModApi_TextWidth(buf, 1) / 2, 2, 2, buf, 1);
    _frontbufferBasePtr = saved;
    _menu_draw_x_offset = savedX;
}

static void board_drawn(void)
{
    ModResultBanner banner;
    int32_t sw, sh;
    if (!ModApi_InMatch() || ModApi_InReplay())
        return;
    const int32_t mode = corner();
    if ((mode == BOTTOM_LEFT || mode == BOTTOM_RIGHT) && ModApi_ResultBanner(&banner))
        return;
    ModApi_ScreenSize(&sw, &sh);
    if (!s_scr || s_scrW != sw) {
        free(s_scr);
        s_scrW = sw;
        s_scr = (uint8_t *)malloc((size_t)sw * kScrH);
        if (!s_scr)
            return;
    }
    int32_t ox, oy;                     /* (the clock's and score's) */
    int32_t left, right;                /* the HUD's box: on an ultrawide the 16:9 part, as the
                                         * game's own clock and the player's name (Playa) */
    ModApi_HudBox(&left, &right);
    if (right <= left || right > sw) {
        left = 0;
        right = sw;
    }
    switch (mode) {
    case BOTTOM_LEFT:  ox = left + 10;   oy = sh - 13; break;
    case BOTTOM_RIGHT: ox = right - 80;  oy = sh - 13; break;
    case TOP_RIGHT:    ox = right - 80;  oy = 5;        break;
    default:           ox = left + 10;   oy = 5 + 1;    break;
    }
    /* What the board drew is what is the same over two backgrounds; the rest is the box. */
    static uint8_t a[kBoxW * kBoxH];
    board_pixels(0x00);
    for (int32_t y = 0; y < kBoxH; y++)
        memcpy(a + y * kBoxW, s_scr + (size_t)y * s_scrW, kBoxW);
    board_pixels(0xFF);
    for (int32_t y = 0; y < kBoxH; y++)
        for (int32_t x = 0; x < kBoxW; x++) {
            const uint8_t c = s_scr[(size_t)y * s_scrW + x];
            uint8_t *p = s_rgba + (y * kBoxW + x) * 4;
            if (c == a[y * kBoxW + x]) {
                Platform_PaletteRgb(c, &p[0], &p[1], &p[2]);
                p[3] = 255;
            } else {
                p[0] = p[1] = p[2] = 0;
                p[3] = 128;
            }
        }
    s_boxX = ox - 2;
    s_boxY = oy;
    s_drawnMs = Platform_NowMs();
    s_testDrawn++;
}

static void board_kept(void)
{
    if (s_drawnMs)
        s_drawnMs = Platform_NowMs();
}

static void board_present(void)
{
    if (!s_drawnMs || Platform_NowMs() - s_drawnMs > 100 || !ModApi_InMatch() || ModApi_InReplay())
        return;
    if (!s_tex && !(s_tex = Gfx_CreateTexture(kBoxW, kBoxH, 0)))
        return;
    Gfx_UpdateTexture(s_tex, s_rgba, kBoxW * 4);
    const GfxSpace space = Gfx_Space();
    const GfxBlend blend = Gfx_Blend();
    const SDL_FRect dst = { (float)s_boxX, (float)s_boxY, (float)kBoxW, (float)kBoxH };
    Gfx_SetSpace(GFX_SPACE_PICTURE);
    Gfx_SetBlend(GFX_BLEND_ALPHA);
    Gfx_DrawTexture(s_tex, NULL, &dst);
    Gfx_SetBlend(blend);
    Gfx_SetSpace(space);
}

/* SWOS_SCOREBOARD_TEST=1: in the first match (SWOS_MOD_MATCH_TEST's is one), PASS when the board
 * was drawn on the frames played. */
static void board_frame(void)
{
    if (s_test && ModApi_MatchFrames() == 600) {
        fprintf(stdout, "SCOREBOARD TEST %s: drawn %d times, corner %d\n", s_testDrawn > 0 ? "PASS" : "FAIL",
                (int)s_testDrawn, (int)corner());
        exit(s_testDrawn > 0 ? 0 : 1);
    }
}

static void board_init(void) { s_test = getenv("SWOS_SCOREBOARD_TEST") != NULL; }

const ModDef ModDef_scoreboard = {
    .slug = "scoreboard",
    .init = board_init,
    .spriteDraw = board_sprite,
    .matchFrame = board_frame,
    .matchDrawn = board_drawn,
    .frameKept  = board_kept,
    .frameUnder = board_present,
};
#endif
