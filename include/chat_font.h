/*
 * chat_font.h -- the chat's typeface, drawn at the window's own pixels (SDL builds).
 *
 * The game's own font is capitals only, one size, drawn into the 8bpp picture and scaled with
 * it. The chat needs small letters and accents and has to be sharp at any window size, so it is
 * drawn OVER the picture, in gfx.h's OUTPUT space: Roboto Condensed (embedded, src/chat_font_data.c)
 * rasterized by stb_truetype at exactly the pixel size asked for, whenever that size changes, into
 * a sheet of glyphs drawn as textured triangles -- the same on either presenter.
 *
 * Text is UTF-8. A character the font does not have is drawn as '?'.
 */
#ifndef SWOS_CHAT_FONT_H
#define SWOS_CHAT_FONT_H

#include <stdint.h>
#include <SDL.h>

/* The glyphs at `px` pixels a line (made when it changes; 0 when ready). Only between a
 * presenter's frame and its present (gfx.h Gfx_InFrame), as any texture. */
int32_t ChatFont_Prepare(int32_t px);
int32_t ChatFont_LineHeight(void);              /* pixels from one baseline to the next */
int32_t ChatFont_Ascent(void);                  /* top of a line to its baseline */

/* The font has this character. */
int32_t ChatFont_Has(uint32_t codepoint);

/* Pixels wide. `bytes` < 0: up to the NUL. */
float   ChatFont_Measure(const char *utf8, int32_t bytes);
/* Drawn with its top-left at x, y, in gfx.h's current space (the caller's: OUTPUT for the chat). */
void    ChatFont_Draw(float x, float y, const char *utf8, int32_t bytes, SDL_Color colour);

/* The next character of a UTF-8 string, and how many bytes it took (at least 1, so a loop always
 * moves on). An invalid sequence is U+FFFD. */
uint32_t ChatFont_NextChar(const char *s, int32_t bytes, int32_t *used);

void    ChatFont_Shutdown(void);

/* Draws a test string into an RGBA buffer on the CPU, as the sheet holds it: --chat-selftest. */
int32_t ChatFont_SelfTest(void);

#endif
