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

static void board_drawn(void)
{
    D8 d;
    ModResultBanner banner;
    if (!ModApi_InMatch() || ModApi_InReplay() || !D8_Begin(&d))
        return;
    const int32_t mode = corner();
    if ((mode == BOTTOM_LEFT || mode == BOTTOM_RIGHT) && ModApi_ResultBanner(&banner))
        return;
    int32_t ox, oy;                     /* (the clock's and score's) */
    switch (mode) {
    case BOTTOM_LEFT:  ox = 10;        oy = d.h - 13; break;
    case BOTTOM_RIGHT: ox = d.w - 80;  oy = d.h - 13; break;
    case TOP_RIGHT:    ox = d.w - 80;  oy = 5;        break;
    default:           ox = 10;        oy = 5 + 1;    break;
    }
    /* showTeamIcons: the box (its own ox 2 to the left) and the kits */
    const int32_t bx = ox - 2;
    D8_Darken(&d, bx, oy, 70 + 2, 8);
    D8_Kit(&d, 0, bx + 23, oy + 1, 1);
    D8_Kit(&d, 1, bx + 63, oy + 1, 1);
    /* the clock, right-aligned at ox + 20; the score centred at ox + 47 */
    char buf[16];
    snprintf(buf, sizeof buf, "%d'", (int)ModApi_MatchMinute());
    ModApi_DrawText(ox + 20 - ModApi_TextWidth(buf, 1), oy + 2, 2 /* kWhiteText2 */, buf, 1);
    int16_t goals[2];
    ModApi_Score(goals);
    snprintf(buf, sizeof buf, "%d-%d", (int)goals[0], (int)goals[1]);
    ModApi_DrawText(ox + 47 - ModApi_TextWidth(buf, 1) / 2, oy + 2, 2, buf, 1);
    s_testDrawn++;
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
};
#endif
