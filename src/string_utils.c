/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "string_utils.h"
#include "menu_system.h"   /* Int2Ascii */

/* Prevent GCC from converting byte loops to memcpy/memset (no libc) */
#pragma GCC optimize("no-tree-loop-distribute-patterns")

#include "world.h"   /* a euro world's money sign (Currency_Apply) */

/* ASM externs for number formatting */
extern uint8_t _aPound[];       /* "£" (0x9C, 0x00) */
extern uint8_t _nullString_0[]; /* "" (0x00) */

/* Money symbol. The pound (byte 0x9C) is hard-coded at the start of every money
 * format string: aPound (used by Int2Pounds for balances/wages/transfers) and
 * the K/M player-value strings a1k/a0m/a01m. SWOS.ini [general] currency= patches that
 * leading byte in all of them: pound -> 0x9C (stock), dollar -> '$' 0x24, euro -> '€' 0x9D.
 * The '$' and '€' glyphs live in the font extension (see docs/FONTS.md); the pound glyph is
 * the stock 0x9C. */
static uint8_t s_currencyChosen = 0x9C;                       /* the player's (SWOS.ini) */

/* A world whose seasons are the euro's (its first season 2002 or later: World 2026-27) shows the
 * pound as the euro -- the amounts as they are, the sign alone (the SWOS 96/97 Mod 4.1's
 * tools/euro.py, option A) -- unless the world mod's setting "euro" is off. A player who chose
 * the dollar keeps it. 96/97 (and a SWOS_WORLDS=0 build) keeps the pound. */
static uint8_t currency_for_world(void)
{
#if SWOS_WORLDS
    if (s_currencyChosen == 0x9C && World_FirstYear() >= 2002) {
        extern int32_t ModApi_SettingInt(const char *slug, const char *key, int32_t fallback);
        char slug[16] = "world";
        const char *t = World_Tag();
        int32_t k = 5;
        while (*t && k < (int32_t)sizeof slug - 1)
            slug[k++] = *t++;
        slug[k] = 0;
        if (ModApi_SettingInt(slug, "euro", 1))
            return 0x9D;
    }
#endif
    return s_currencyChosen;
}

void Currency_Apply(void)
{
    extern char _a1k[], _a0m[], _a01m[];
    const uint8_t sym = currency_for_world();
    _aPound[0] = sym;
    _a1k[0] = _a0m[0] = _a01m[0] = (char)sym;
}

void Currency_Set(const char *name)
{
    uint8_t sym = 0x9C;                                       /* pound (default/stock) */
    if (name) {
        if (name[0] == 'd' || name[0] == 'D') sym = 0x24;     /* dollar '$' */
        else if (name[0] == 'e' || name[0] == 'E') sym = 0x9D; /* euro '€' */
    }
    s_currencyChosen = sym;
    Currency_Apply();
}

/* Back-compat shim for any caller still passing a bool. */
void Currency_SetDollar(int dollar) { Currency_Set(dollar ? "dollar" : "pound"); }
extern uint8_t _aBank[];        /* "BANK" */
extern int32_t _newBalance;

/* ================================================================
 * GetStringLength  (SWOS2.s line 40972)
 *
 * Returns the length of a null-terminated string.
 *
 * Asm register convention:
 *   in:  a0 = string pointer
 *   out: d0.w = length, a0 = pointer to null terminator
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t GetStringLength(const uint8_t *str)
{
    uint16_t len = 0;
    while (str[len] != 0)
        len++;
    return len;
}

/* ================================================================
 * StringEquals  (SWOS2.s line 40997)
 *
 * Compares two null-terminated strings.
 * Returns 0 if equal (matching asm Z flag set convention),
 * non-zero if different.
 *
 * Asm register convention:
 *   in:  a0 = s1, a1 = s2
 *   out: Z flag (set if equal)
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t StringEquals(const uint8_t *s1, const uint8_t *s2)
{
    uint8_t c;
    for (;;) {
        c = *s1++;
        if (c == 0) {
            /* s1 ended — equal only if s2 also at null */
            return (int16_t)*s2;
        }
        if (c != *s2++)
            return 1; /* not equal */
    }
}

/* ================================================================
 * StringCopy  (SWOS2.s line 41015)
 *
 * Copies src to dst including null terminator.
 * Returns pointer to the null terminator in dst (allows chaining).
 *
 * Asm register convention:
 *   in:  a0 = src, a1 = dst
 *   out: a1 = pointer to null terminator in dst
 *        a0 = one byte past the null terminator in src
 * ================================================================ */
__attribute__((used, externally_visible))
uint8_t *StringCopy(const uint8_t *src, uint8_t *dst)
{
#ifdef SWOS_SDL_BUILD
    /* Guard against NULL `src`, which can happen when a zero-stubbed
     * pointer table hands out a null entry (e.g. defaultNamesTable
     * before we populate it). The original Amiga code assumed SWOS2.s
     * always supplied non-null strings. */
    if (!src) { *dst = 0; return dst; }
#endif
    while ((*dst = *src) != 0) {
        src++;
        dst++;
    }
    return dst; /* points to null terminator */
}

/* ================================================================
 * StringCopyUntilChar  (SWOS2.s line 41028)
 *
 * Copies src to dst until stopChar or end of string.
 * Returns pointer to null terminator in dst.
 *
 * Asm register convention:
 *   in:  a0 = src, a1 = dst, d0.b = stop char
 *   out: a1 = pointer to null terminator in dst
 *        a0 = if stop char found: one before the stop char
 *             if null found: one past the null
 *
 * Note: the asm stub handles the a0 update via the second
 * return value in d1 (srcEnd position).
 * ================================================================ */
__attribute__((used, externally_visible))
uint8_t *StringCopyUntilChar(const uint8_t *src, uint8_t *dst, uint8_t stopChar)
{
    for (;;) {
        if (*src == stopChar) {
            *dst = 0;
            return dst;
        }
        *dst = *src;
        if (*src == 0) {
            return dst;  /* dst points to the null written */
        }
        src++;
        dst++;
    }
}

/* ================================================================
 * Chunk 100: Number formatting & menu utility functions
 * ================================================================ */

/* ================================================================
 * Int2StrWithCommas  (SWOS2.s line 29627)
 *
 * Formats an unsigned 32-bit integer into a string with comma
 * separators (e.g. 1,234,567). Writes into buffer at *dst and
 * returns pointer past the last character written.
 * ================================================================ */
static uint8_t *int2StrWithCommas(uint32_t val, uint8_t *dst)
{
    if (val < 1000) {
        /* Simple case: no commas needed */
        Int2Ascii((uint16_t)val, dst);
        while (*dst) dst++;
        return dst;
    }

    if (val >= 1000000) {
        /* >= 1M: format as X,XXX,XXX */
        uint32_t d1 = val;
        uint32_t high = (val >> 4) / 62500;  /* millions part */
        Int2Ascii((uint16_t)high, dst);
        while (*dst) dst++;
        *dst++ = ',';

        /* Compute remainder: val - (high * 62500 << 4) */
        uint32_t remainder = d1 - (high * 62500 << 4);

        /* Thousands part */
        uint16_t thousands = (uint16_t)(remainder / 1000);
        /* Pad to 3 digits */
        if (thousands < 100) *dst++ = '0';
        if (thousands < 10) *dst++ = '0';
        Int2Ascii(thousands, dst);
        while (*dst) dst++;
        *dst++ = ',';

        /* Units part */
        uint16_t units = (uint16_t)(remainder % 1000);
        if (units < 100) *dst++ = '0';
        if (units < 10) *dst++ = '0';
        Int2Ascii(units, dst);
        while (*dst) dst++;
        return dst;
    }

    /* 1000-999999: format as X,XXX or XX,XXX or XXX,XXX */
    uint16_t high = (uint16_t)(val / 1000);
    Int2Ascii(high, dst);
    while (*dst) dst++;
    *dst++ = ',';

    uint16_t low = (uint16_t)(val % 1000);
    if (low < 100) *dst++ = '0';
    if (low < 10) *dst++ = '0';
    Int2Ascii(low, dst);
    while (*dst) dst++;
    return dst;
}

/* ================================================================
 * Int2Pounds  (SWOS2.s line 29691)
 *
 * Formats an unsigned value as "£123,456".
 * ================================================================ */
__attribute__((used, externally_visible))
void Int2Pounds(uint32_t pounds, uint8_t *dst)
{
    /* nullsub_11 is a no-op */
    /* Copy the currency sign — aPound[0] is patched to '$' by Currency_SetDollar
     * when currency=dollar (default stays the £, byte 0x9C). */
    dst = StringCopy(_aPound, dst);
    /* Format number with commas */
    dst = int2StrWithCommas(pounds, dst);
    /* Copy null string (null terminator) */
    StringCopy(_nullString_0, dst);
}

/* ================================================================
 * Int2PoundsSigned  (SWOS2.s line 29706)
 *
 * Formats a signed value as "£123" or "- £123".
 * ================================================================ */
__attribute__((used, externally_visible))
void Int2PoundsSigned(int32_t pounds, uint8_t *dst)
{
    if (pounds < 0) {
        *dst++ = '-';
        *dst++ = ' ';
        pounds = -pounds;
    }
    Int2Pounds((uint32_t)pounds, dst);
}

/* ================================================================
 * WriteAccountBalance  (SWOS2.s line 29719)
 *
 * Formats a signed value as "+ £123" or "- £123" or just "£0".
 * ================================================================ */
__attribute__((used, externally_visible))
void WriteAccountBalance(int32_t delta, uint8_t *dst)
{
    if (delta > 0) {
        *dst++ = '+';
        *dst++ = ' ';
    } else if (delta < 0) {
        *dst++ = '-';
        *dst++ = ' ';
        delta = -delta;
    }
    Int2Pounds((uint32_t)delta, dst);
}

/* ================================================================
 * GetBankBalance  (SWOS2.s line 29676)
 *
 * Formats the current bank balance as "BANK  £123,456".
 * ================================================================ */
__attribute__((used, externally_visible))
void GetBankBalance(uint8_t *dst)
{
    dst = StringCopy(_aBank, dst);
    *dst++ = ' ';
    *dst++ = ' ';
    Int2PoundsSigned(_newBalance, dst);
}

/* ================================================================
 * PrepareMenuForFadeIn  (SWOS2.s line 25608)
 *
 * Prepares the menu for a fade-in transition.
 * ================================================================ */
extern void DoubleBufferSwapSequence(void);
extern void PrepareMenuBackground(void);
extern void DrawMenu(void);
extern uint16_t _menuFade;

__attribute__((used, externally_visible))
void PrepareMenuForFadeIn(void)
{
    DoubleBufferSwapSequence();
    PrepareMenuBackground();
    DrawMenu();
    _menuFade = 1;
}
