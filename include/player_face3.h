/*
 * player_face3.h -- black players with blond hair, face 3 (src/player_face3.c; the SWOS 96/97 Mod's
 * blond.py).
 */
#ifndef PLAYER_FACE3_H
#define PLAYER_FACE3_H

#include "swos_types.h"
#include "shirt_numbers.h"

/* The sprite table's slots: the two teams' face 3 sets (101 each), then the menus' face icon. */
#define FACE3_SPRITE_FIRST   SHIRT_NUMBER_SPRITES_END
#define FACE3_MENU_ICON      (FACE3_SPRITE_FIRST + 2 * 101)
#define FACE3_SPRITES_END    (FACE3_MENU_ICON + 1)

/* ConvertColorsAndTextSprites: before the teams' sets are recoloured, copies of their first
 * (raw) sets, which it recolours after a team's three with face 3 (team 0 / 1). */
void Face3_CopySets(void);
int16_t Face3_Set(int32_t team);   /* (its first sprite, -1 when it could not be made) */

/* A blond outfield player's frame offset (team 0 / 1): its face 3 set (the black one when the
 * copy could not be made). */
int16_t Face3_FrameOffset(int32_t team);

/* The menus' face icon of face 3; of any face (0..3). */
int16_t Face3_MenuIcon(void);
int16_t Face_MenuIcon(uint16_t face);

#endif
