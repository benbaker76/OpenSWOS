/*
 * cal_label.c -- a competition's short name fitted to the career fixture list's name column.
 *
 * The column (CareerGameListCommon, menus.c) has 70 px of the small font before a
 * home-and-away row's "(H)" / "(A)". 96/97's short names were written to fit it; World
 * 2026-27's come from the SWOS 96/97 Mod's packs, and 133 of its 322 did not: they ran
 * into the marker (docs/MENUS.md section 11). The Mod's own DOS game has the same column
 * and fits every short name when it builds (tools/complist.py, fit_short: a hand-picked
 * label, else standard abbreviations last word first, else the widest words cut). This is
 * that function, so a label reads the same in both games, applied where the column is
 * drawn: every source of names (packs, continental and European cups, future worlds) and
 * only a name that does not fit.
 *
 * Ported from the SWOS 96/97 Mod, tools/complist.py (fit_short, FIT_SHORT, ABBR) at
 * 77b3767, under its MIT licence:
 *
 *   Copyright (c) 2026 Davide Lorigliola
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of this
 *   software and associated documentation files (the "Software"), to deal in the Software
 *   without restriction, including without limitation the rights to use, copy, modify,
 *   merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 *   permit persons to whom the Software is furnished to do so, subject to the following
 *   conditions: The above copyright notice and this permission notice shall be included in
 *   all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS",
 *   WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 *
 * Widths are the game's own (GetTextWidth on the small font), which the Mod's SMALL_W
 * table copies.
 */
#include "cal_label.h"
#include "text_render.h"    /* GetTextWidth */

#include <string.h>

/* The Amiga m68k <string.h> is partial (memcpy, not strlen / strcmp): their prototypes, as
 * mjson.c and save_format_json.c give them; the symbols are src/swos_io.c's. */
#if defined(__m68k__) || defined(__amigaos__) || defined(__amigaos4__)
extern size_t strlen(const char *s);
extern int    strcmp(const char *a, const char *b);
#endif

extern void *_charTables[];     /* [0]: the small font */

/* The Amiga build has strlen / strcmp / memcpy but no strcpy / strcat (src/swos_io.c). */
static void cpy(char *d, const char *s)
{
    while ((*d++ = *s++) != 0)
        ;
}
static void cat(char *d, const char *s)
{
    cpy(d + strlen(d), s);
}

static const char *const kFitShort[][2] = {
    { "AUSTRALIA CUP", "AUST. CUP" },
    { "BULGARIAN CUP", "BULG. CUP" },
    { "EMPEROR'S CUP", "EMPEROR CUP" },
    { "PUCHAR POLSKI", "PUCHAR POL." },
    { "SCHWEIZER CUP", "SCHWEIZ. CUP" },
    { "RELEGATION 1/2", "RELEG. 1/2" },
    { "RELEGATION 2/3", "RELEG. 2/3" },
    { "SEGUNDA FINAL", "SEG. FINAL" },
    { "LANDESMEISTER", "LANDESMEIST." },
    { "CAMPIONATO SMR", "CAMP. SMR" },
    { "BEKER VAN BELGIE", "BEKER BELGIE" },
    { "COPA COSTA RICA", "COPA C. RICA" },
    { "CONCACAF CHAMP.", "CONCACAF CC" },
    { "AFC CHALLENGE L.", "AFC CHALL. L." },
    { "CAF CHAMPIONS L.", "CAF CHAMP. L." },
    { "CAF CONFED. CUP", "CAF CONF. CUP" },
    { "CAF SUPER CUP", "CAF SUPERCUP" },
    { "CHAMPIONS LEAGUE", "CHAMPIONS L." },
    { "EUROPA LEAGUE", "EUROPA L." },
    { "CUP WINNERS CUP", "CUP W. CUP" },
    { "GHANA PREM. LGE", "GHANA PREM." },
    { "EGYPT LEAGUE CUP", "EGY LGE CUP" },
    { "LIGA DE PRIMERA", "LIGA PRIMERA" },
    { "COPA DE LA LIGA", "COPA LIGA" },
    { "P.NACIONAL FINAL", "P. NAC. FINAL" },
    { "CUPA MOLDOVEI", "CUPA MOLDOVA" },
    { "MALTA PREMIER", "MALTA PREM." },
    { "COPA COLOMBIA", "COPA COL." },
    { "CYMRU PREMIER", "CYMRU PREM." },
    { "LIGA PORTUGAL", "LIGA PORT." },
};

static const char *const kAbbr[][2] = {
    { "LEAGUE", "LGE" },
    { "DIVISION", "DIV." },
    { "DIVISIO", "DIV." },
    { "DIVISAO", "DIV." },
    { "PREMIER", "PREM." },
    { "PROFESIONAL", "PROF." },
    { "CHAMPIONS", "CHAMP." },
    { "CHAMPIONNAT", "CHAMP." },
    { "CAMPIONI", "CAMP." },
    { "CAMPEONATO", "CAMP." },
    { "GIRONE", "GIR." },
    { "GRUPO", "GR." },
    { "NATIONAL", "NAT." },
    { "NACIONAL", "NAC." },
    { "NATIONALLIGA", "NATIONALL." },
    { "PLAY-OFFS", "P-OFFS" },
    { "PLAY-OFF", "P-OFF" },
    { "COUPE", "C." },
    { "COPPA", "C." },
    { "PRESIDENT", "PRES." },
    { "PRESIDENT'S", "PRES." },
    { "CONSTITUCIO", "CONST." },
    { "SUPERIORE", "SUP." },
    { "KNOCKOUT", "K.O." },
    { "NORTHERN", "NORTH." },
    { "SOUTHERN", "SOUTH." },
    { "CENTRAL", "CENT." },
    { "OCCIDENTE", "OCC." },
    { "OCCIDENT", "OCC." },
    { "ORIENTE", "OR." },
    { "EXPANSION", "EXP." },
    { "QUEENSLAND", "QLD" },
    { "LUXEMBOURG", "LUX." },
    { "LUXEMB.", "LUX." },
    { "PORTUGAL", "PORT." },
    { "URUGUAY", "URU." },
    { "VENEZUELA", "VEN." },
    { "COLOMBIA", "COL." },
    { "PARAGUAY", "PAR." },
    { "ARGENTINA", "ARG." },
    { "BRASIL", "BRA." },
    { "TUNISIE", "TUN." },
    { "D'ALGERIE", "D'ALG." },
    { "SHQIPERISE", "SHQ." },
    { "SHQIPERIS", "SHQ." },
    { "SVENSKA", "SV." },
    { "SCOTTISH", "SCOT." },
    { "IRISH", "IR." },
    { "WELSH", "WELSH" },
    { "BELARUSIAN", "BLR" },
    { "MACEDONIAN", "MKD" },
    { "AZERBAIJAN", "AZE" },
    { "KAZAKHSTAN", "KAZ" },
    { "CANADIAN", "CAN." },
    { "CHAMP.", "CH." },
};
enum { kWords = 12, kWordLen = 32, kLine = 64 };

static int32_t px(const char *s)
{
    return GetTextWidth((uint8_t *)s, (uint8_t *)_charTables[0]);
}

/* words[0..n) joined by spaces into out (kLine). */
static void join(char words[][kWordLen], int32_t n, char *out)
{
    int32_t i;
    out[0] = 0;
    for (i = 0; i < n; i++) {
        if (i)
            cat(out, " ");
        cat(out, words[i]);
    }
}

/* w without its trailing dots, its length. */
static int32_t stem_len(const char *w)
{
    int32_t n = (int32_t)strlen(w);
    while (n > 0 && w[n - 1] == '.')
        n--;
    return n;
}

static const uint8_t *put(const char *s, uint8_t *out, int32_t size)
{
    int32_t i;
    for (i = 0; i < size - 1 && s[i]; i++)
        out[i] = (uint8_t)s[i];
    out[i] = 0;
    return out;
}

const uint8_t *CalLabel_Fit(const uint8_t *name, uint8_t *out, int32_t size)
{
    char words[kWords][kWordLen], line[kLine], trial[kLine], cut[kLine];
    int32_t n = 0, i, k, pass;
    const char *p = (const char *)name;

    if (!name || !_charTables[0] || px((const char *)name) <= CAL_LABEL_PX)
        return name;
    for (i = 0; i < (int32_t)(sizeof kFitShort / sizeof kFitShort[0]); i++)
        if (strcmp(kFitShort[i][0], (const char *)name) == 0)
            return put(kFitShort[i][1], out, size);

    while (*p && n < kWords) {                      /* the words */
        int32_t len = 0;
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        while (*p && *p != ' ' && len < kWordLen - 1)
            words[n][len++] = *p++;
        while (*p && *p != ' ')
            p++;
        words[n++][len] = 0;
    }

    for (i = n - 1; i >= 0; i--) {                  /* the standard abbreviations, last word first */
        join(words, n, line);
        if (px(line) <= CAL_LABEL_PX)
            return put(line, out, size);
        for (k = 0; k < (int32_t)(sizeof kAbbr / sizeof kAbbr[0]); k++)
            if (strcmp(words[i], kAbbr[k][0]) == 0) {
                cpy(words[i], kAbbr[k][1]);
                break;
            }
    }
    for (pass = 0; pass < 12; pass++) {             /* the widest word over 4 letters cut */
        int32_t best = -1, bestPx = -1, stem, cutAt;
        join(words, n, line);
        if (px(line) <= CAL_LABEL_PX)
            return put(line, out, size);
        for (k = 0; k < n; k++)
            if (stem_len(words[k]) > 4 && !(words[k][0] >= '0' && words[k][0] <= '9') && px(words[k]) > bestPx) {
                best = k;
                bestPx = px(words[k]);
            }
        if (best < 0)
            break;
        stem = stem_len(words[best]);
        cutAt = stem - 1;
        for (;;) {
            char saved[kWordLen];
            cpy(saved, words[best]);
            memcpy(words[best], saved, (size_t)cutAt);
            cpy(words[best] + cutAt, ".");
            join(words, n, trial);
            cpy(words[best], saved);
            if (!(cutAt > 3 && px(trial) > CAL_LABEL_PX))
                break;
            cutAt--;
        }
        if (cutAt < 3)
            cutAt = 3;
        words[best][cutAt] = 0;
        cat(words[best], ".");
    }

    join(words, n, line);                           /* else cut, a dot if it fits */
    cut[0] = 0;
    for (i = 0; line[i]; i++) {
        int32_t len = (int32_t)strlen(cut);
        cut[len] = line[i];
        cut[len + 1] = 0;
        if (px(cut) > CAL_LABEL_PX) {
            cut[len] = 0;
            break;
        }
    }
    {
        int32_t full = strcmp(cut, line) == 0, len = (int32_t)strlen(cut);
        while (len > 0 && (cut[len - 1] == ' ' || cut[len - 1] == '.'))
            cut[--len] = 0;
        if (!full) {
            cpy(trial, cut);
            cat(trial, ".");
            if (px(trial) <= CAL_LABEL_PX)
                cpy(cut, trial);
        }
    }
    return put(cut, out, size);
}
