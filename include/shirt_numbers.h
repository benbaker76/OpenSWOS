/*
 * shirt_numbers.h -- the shirt number over a player in a match, 1..99 (src/shirt_numbers.c; the
 * SWOS 96/97 Mod's shirtnum.py).
 */
#ifndef SHIRT_NUMBERS_H
#define SHIRT_NUMBERS_H

#include "swos_types.h"

/* The sprite table's slots: a match's 17..99, then the menus' 12..99 (after the font's,
 * 1220..1303). */
#define SHIRT_NUMBER_SPRITE_17       1310
#define SHIRT_NUMBER_MENU_SPRITE_12  (SHIRT_NUMBER_SPRITE_17 + 83)
#define SHIRT_NUMBER_SPRITES_END     (SHIRT_NUMBER_MENU_SPRITE_12 + 88)

/* After a match's sprites are loaded (ConvertColorsAndTextSprites): 17..99 made from its digits. */
void ShirtNumbers_Build(void);

/* The sprite of a shirt number in a match, -1 for none (0, 100 on). */
int16_t ShirtNumber_Sprite(int8_t number);

/* The little number of the menus' tactics pitches (12..99 made when first needed), -1 for none. */
int16_t ShirtNumber_MenuSprite(int8_t number);

#endif
