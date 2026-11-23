/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#ifndef INPUT_H
#define INPUT_H

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef short int16_t;

void Player1StatusProc(void);
void Player2StatusProc(void);
/* The Amiga only: the second fire buttons held (pin 9; bit 0 player 1, bit 1 player 2). */
uint16_t Input_SecondButtons(void);

/* SDL-only — set when SPACE (keyboard) or the second button on a
 * controller / raw joystick is pressed. Auto-advance pause loops can
 * test this to bail out of dwell waits early. Not present on the
 * Amiga build; gate any use behind a stub or `#ifdef` if back-porting
 * a routine that reads it. */
extern volatile uint16_t _pauseSkipRequested;

#endif
