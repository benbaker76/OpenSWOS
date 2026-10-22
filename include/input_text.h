/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#ifndef INPUT_TEXT_H
#define INPUT_TEXT_H

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef signed char int8_t;
typedef short int16_t;
typedef int int32_t;

/* Forward declaration */
struct MenuEntry;

/* ---- PrintFormatted ----
 * Format-string printer with %0..%6 decimal substitution and %a/%b/%c string substitution.
 * fmt:  format string
 * out:  output buffer
 * nums: array of 7 uint16_t values (mapped to %0..%6)
 * strA/strB/strC: strings for %a, %b, %c
 */
void PrintFormatted(const uint8_t *fmt, uint8_t *out,
                     uint16_t n0, uint16_t n1, uint16_t n2, uint16_t n3,
                     uint16_t n4, uint16_t n5, uint16_t n6,
                     const uint8_t *strA, const uint8_t *strB, const uint8_t *strC);

/* ---- InputText ----
 * Interactive text editor using the menu system keyboard.
 * maxLenPlus1: max input length + 1 (for nul terminator), must be <= 99
 * entry: current menu entry (string type, fg field = string ptr)
 * Returns: 0 = committed (enter/fire), 1 = cancelled (escape)
 */
uint16_t InputText(uint16_t maxLenPlus1, uint8_t *entry);

#ifdef __ANDROID__
void InputSDL_AndroidTextBegin(void);
void InputSDL_AndroidTextEnd(void);
uint16_t InputSDL_AndroidTextRead(void);
#endif

/* ---- InsertCharacter ----
 * Inserts a character at pos in inMenuKeyboardBuffer, shifting right.
 * pos: pointer within inMenuKeyboardBuffer
 * ch: character to insert
 */
void InsertCharacter(uint8_t *pos, uint8_t ch);

/* ---- InputTextDeleteCharAtCursor ----
 * Deletes the character at pos, shifting left.
 */
void InputTextDeleteCharAtCursor(uint8_t *pos);

/* ---- InputTextFilterKey ----
 * Filters currentKey to allowed charset (A-Z, 0-9, control chars).
 * Returns: the key if allowed, 0 if not allowed.
 */
uint16_t InputTextFilterKey(void);

#endif
