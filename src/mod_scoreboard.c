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
#include "mod_hudbox.h"

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
    if (kind == MODSPRITE_PLAYER_NAME && corner() == TOP_LEFT && !ModApi_HudHidden(MODHUD_SCOREBOARD))
        *y = (int16_t)(*y + 7);
    return 0;
}

/* The board is drawn at the present, over the finished picture (mod_hudbox.h: SWOS 2020 drew it
 * over its output, after the scroll -- in the frame buffer it went with the pitch and flickered as
 * the camera moved), the picture under it shaded as SWOS 2020's index + 128 shades it, the kits
 * and the text over that (Playa, 2026-11-11/12). */
enum { kBoxW = 73, kBoxH = 9 };

static HudBox s_box;

/* The board's contents at the box's own (0, 0). */
static void board_contents(void *ctx)
{
    (void)ctx;
    int32_t sw, sh;
    uint8_t *fb;
    int32_t stride;
    fb = ModApi_Frame(&stride);         /* (the scratch buffer, while HudBox_Draw draws) */
    ModApi_ScreenSize(&sw, &sh);
    D8 d = { fb, stride, kBoxW, kBoxH };
    /* showTeamIcons put the kits at 23 and 63 of the box; the first is drawn at 27, so the two sit
     * alike about the score (centred at 49): at 23 it was 4 nearer the clock (Playa, 2026-11-12) */
    D8_Kit(&d, 0, 27, 1, 1);
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
}

static void board_drawn(void)
{
    ModResultBanner banner;
    int32_t sw, sh;
    if (!ModApi_InMatch() || ModApi_InReplay() || ModApi_HudHidden(MODHUD_SCOREBOARD)) {
        HudBox_Off(&s_box);             /* (MODHUD_SCOREBOARD: TEAM TRAINING has none, as SWOS 2020's) */
        return;
    }
    const int32_t mode = corner();
    if ((mode == BOTTOM_LEFT || mode == BOTTOM_RIGHT) && ModApi_ResultBanner(&banner)) {
        HudBox_Off(&s_box);
        return;
    }
    ModApi_ScreenSize(&sw, &sh);
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
    HudBox_Draw(&s_box, ox - 2, oy, kBoxW, kBoxH, HUDBOX_SHADE, 0, 0, 0, board_contents, NULL);
    s_testDrawn++;
}

static void board_kept(void) { HudBox_Kept(&s_box); }

static void board_present(void)
{
    if (ModApi_InMatch() && !ModApi_InReplay())
        HudBox_Present(&s_box);
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
