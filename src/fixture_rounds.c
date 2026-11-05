/*
 * fixture_rounds.c -- real match days for leagues of an odd number of clubs
 * (include/fixture_rounds.h).
 *
 * Ported from the SWOS 96/97 Mod's tools/oddtables.py (rounds), which carries this notice:
 *
 *   Copyright (c) 2026 Davide Lorigliola
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of this
 *   software and associated documentation files (the "Software"), to deal in the Software
 *   without restriction, including without limitation the rights to use, copy, modify,
 *   merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 *   permit persons to whom the Software is furnished to do so, subject to the following
 *   conditions:
 *
 *   The above copyright notice and this permission notice shall be included in all copies
 *   or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 *   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
 *   PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 *   HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 *   CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
 *   THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Berger tables by the circle method, a dummy club making n even: whoever meets it rests.
 * Every club once per round at rest, (n - 1) / 2 +- 1 home games each in a cycle (the
 * engine plays the second cycle the other way round), at most 3 home or away running. The
 * pairs are the original table's, in another order (the Mod checked it for every n).
 */
#include "fixture_rounds.h"

#define FIRST_ODD 5
#define LAST_ODD  23

extern const uint8_t *_fixtureSchedulePtrTable[];

/* n(n - 1) bytes for each odd n: 2150. */
static uint8_t s_tables[2150];
static uint8_t s_installed;

/* One cycle of n clubs (odd) as pairs, round by round, into out. */
static uint8_t *rounds(int32_t n, uint8_t *out)
{
    const int32_t k = n + 1;
    uint8_t lst[LAST_ODD + 1];
    for (int32_t i = 0; i < k; i++)
        lst[i] = (uint8_t)i;
    for (int32_t r = 0; r < k - 1; r++) {
        for (int32_t i = 0; i < k / 2; i++) {
            uint8_t a = lst[i], b = lst[k - 1 - i];
            /* The fixed club alternates, the others by their place. */
            if (i == 0 ? (r & 1) : (i & 1)) {
                const uint8_t t = a;
                a = b;
                b = t;
            }
            if (a < n && b < n) {
                *out++ = a;
                *out++ = b;
            }
        }
        /* [first] + [last] + the rest */
        const uint8_t last = lst[k - 1];
        for (int32_t i = k - 1; i > 1; i--)
            lst[i] = lst[i - 1];
        lst[1] = last;
    }
    return out;
}

void FixtureRounds_Install(void)
{
    uint8_t *p = s_tables;
    for (int32_t n = FIRST_ODD; n <= LAST_ODD; n += 2) {
        _fixtureSchedulePtrTable[n - 2] = p;
        p = rounds(n, p);
    }
    s_installed = 1;
}

int32_t FixtureRounds_Real(int32_t n)
{
    return !(n & 1) || (s_installed && n >= FIRST_ODD && n <= LAST_ODD);
}
