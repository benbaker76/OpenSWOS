#ifndef KIT_PALETTE_H
#define KIT_PALETTE_H

/* kit_palette.c -- palette entries for the kit colour variants a match wears (SDL). */

#include <stdint.h>

/* 1 when this match draws the kit variants (the EXTENDED rule, not the Jaguar sprites). */
int32_t  KitPalette_Active(void);
/* A pitch's pixels, as it loads: off the entries the variants may take, onto their twins. */
int32_t  KitPalette_RemapPitch(uint8_t *pixels, uint32_t count);
/* Before a match's sprites are coloured: its variants take entries afresh. */
void     KitPalette_NewMatch(void);
/* The entry a kit colour byte is drawn in this match; 0 = the base colour's own. */
uint16_t KitPalette_Entry(uint8_t colour);
/* The pitch condition's colours are in: a variant too like the grass is drawn a little lighter or darker. */
void     KitPalette_PitchApplied(void);
int32_t  KitPalette_SelfTest(void);

#endif
