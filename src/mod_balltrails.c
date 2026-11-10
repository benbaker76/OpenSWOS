/*
 * mod_balltrails.c -- the ball trails mod (canonical, bin/mods/balltrails; docs/MODS.md §35): SWOS
 * 2020's --ball-trails=1 (game.cpp drawSprites: the trail drawn just before the ball). SDL builds.
 *
 * While the ball is loose and moving, five discs behind it along its way -- the last frames'
 * steps (its delta) back from where it is drawn, the nearest 2 pixels across, then 1, then single
 * pixels -- in SWOS 2020's colour 121. Drawn under the ball (spriteDraw: the ball's turn), not in a
 * replay. "changes": "cosmetic": the picture only.
 */
#ifdef SWOS_SDL_BUILD
#include "mod_api.h"
#include "mod_draw8.h"

#include <stdio.h>
#include <stdlib.h>

static int32_t s_test, s_drawn;         /* SWOS_BALLTRAILS_TEST (below): trails drawn */

static int32_t trails_sprite(const struct Sprite *sprite, const struct SpriteGraphics *gfx, int16_t *x, int16_t *y)
{
    ModBall b;
    D8 d;
    (void)gfx;
    if (ModApi_SpriteKind(sprite) != MODSPRITE_BALL || !ModApi_InMatch() || ModApi_InReplay() ||
        !ModApi_Ball(&b) || !b.free || b.speed == 0 || !D8_Begin(&d))
        return 0;
    const float kLength = 5.0f, kSize = 2.0f;
    for (int32_t k = 1; k <= (int32_t)kLength; k++) {
        const int32_t r = (int32_t)(-kSize / (kLength - 1.0f) * (float)(k - 1) + kSize);
        D8_Disc(&d, *x + 1 - b.deltaX * k, *y + 1 - b.deltaY * k, r, 121);
    }
    s_drawn++;
    return 0;
}

/* SWOS_BALLTRAILS_TEST=1: in the first match a minute of play (SWOS_MOD_MATCH_TEST's match is one),
 * PASS when trails were drawn on some of its frames. */
static void trails_frame(void)
{
    if (s_test && ModApi_MatchFrames() == 1500) {
        fprintf(stdout, "BALLTRAILS TEST %s: trails on %d frames\n", s_drawn > 0 ? "PASS" : "FAIL", (int)s_drawn);
        exit(s_drawn > 0 ? 0 : 1);
    }
}

static void trails_init(void) { s_test = getenv("SWOS_BALLTRAILS_TEST") != NULL; }

const ModDef ModDef_balltrails = {
    .slug = "balltrails",
    .init = trails_init,
    .spriteDraw = trails_sprite,
    .matchFrame = trails_frame,
};
#endif
