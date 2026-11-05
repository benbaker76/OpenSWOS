/*
 * mod_ticker.c -- the news ticker (SWOS 2020's, render.cpp showNewsStr; canonical, bin/mods/ticker;
 * docs/MODS.md §24). SDL builds.
 *
 * A line of news scrolling right to left through a menu's ticker box -- the box its JSON names
 * ("ticker_entry", docs/MENUS.md §13; SWOS United's menus2020 has one) -- one item a pass, the next
 * picked at random, as SWOS 2020's. The news is OpenSWOS JSON, "openswos-news":
 *
 *   { "format": "openswos-news", "version": 1,
 *     "items": [ { "text": "...", "link": "https://..." }, ... ] }
 *
 * read from the player's save/mods/ticker/news.json first (where a feed's download is kept), else
 * the mod's own data_json/ticker/news.json. SWOS 2020 downloaded news.txt / tips.txt from its server;
 * a feed here is the next step and writes the same file. Setting "speed": slow / normal / fast.
 */
#include "mod_api.h"

#ifdef SWOS_SDL_BUILD

#include "mjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLUG "ticker"

enum { kMaxItems = 64, kTextLen = 160 };

static char    s_text[kMaxItems][kTextLen];
static int32_t s_count, s_loaded, s_item = -1;
static int32_t s_offset;                /* the text's left edge, from the box's right edge */
static uint32_t s_lastTick, s_seed = 0x2545F491u;

static uint32_t next_random(void)       /* (its own: the game's numbers are the match's) */
{
    s_seed ^= s_seed << 13;
    s_seed ^= s_seed >> 17;
    s_seed ^= s_seed << 5;
    return s_seed;
}

static void parse(const char *json, int len)
{
    char fmt[32] = "";
    mjson_get_string(json, len, "$.format", fmt, (int)sizeof fmt);
    if (strcmp(fmt, "openswos-news") != 0)
        return;
    for (int32_t i = 0; s_count < kMaxItems; i++) {
        char q[48];
        snprintf(q, sizeof q, "$.items[%d].text", (int)i);
        if (mjson_get_string(json, len, q, s_text[s_count], kTextLen) < 0)
            break;
        for (char *c = s_text[s_count]; *c; c++)    /* (the menu fonts are capitals) */
            if (*c >= 'a' && *c <= 'z')
                *c = (char)(*c - 32);
        if (s_text[s_count][0])
            s_count++;
    }
}

static void load(void)
{
    uint32_t size = 0;
    char *json;
    s_loaded = 1;
    json = (char *)ModApi_LoadFile(SLUG, "news.json", &size);
    if (json) {
        parse(json, (int)size);
        free(json);
    }
    if (!s_count) {
        char path[1200];
        const char *dir = ModApi_ModDir(SLUG);
        FILE *f;
        snprintf(path, sizeof path, "%s/data_json/ticker/news.json", dir ? dir : ".");
        f = fopen(path, "rb");
        if (f) {
            static char buf[16384];
            const size_t n = fread(buf, 1, sizeof buf - 1, f);
            fclose(f);
            buf[n] = 0;
            parse(buf, (int)n);
        }
    }
    s_seed ^= ModApi_Ticks();
    fprintf(stderr, "[ticker] %d news items\n", (int)s_count);
}

/* Each menu frame: the box painted back, the line drawn where it has scrolled to, clipped to the
 * box a character at a time (the menu text has no clipping of its own). */
static void ticker_menu_drawn(void)
{
    const int32_t entry = ModApi_MenuTickerEntry();
    int32_t bx, by, bw, bh;
    if (entry < 0)
        return;
    if (!s_loaded)
        load();
    if (!s_count)
        return;
    ModApi_MenuEntryRect(entry, &bx, &by, &bw, &bh);
    if (bw <= 0 || bh <= 0)
        return;

    const char *speed = ModApi_Setting(SLUG, "speed", "normal");
    const int32_t pxPerSec = !strcmp(speed, "slow") ? 30 : !strcmp(speed, "fast") ? 90 : 50;
    const uint32_t now = ModApi_Ticks();
    if (s_item < 0 || !s_lastTick) {
        s_item = (int32_t)(next_random() % (uint32_t)s_count);
        s_offset = 0;
        s_lastTick = now;
    }
    s_offset += (int32_t)((now - s_lastTick) * (uint32_t)pxPerSec / 1000u);
    if ((now - s_lastTick) * (uint32_t)pxPerSec >= 1000u)
        s_lastTick = now;

    const char *text = s_text[s_item];
    const int32_t width = ModApi_TextWidth(text, 1);
    if (s_offset > bw + width) {            /* gone off the left: the next one */
        s_item = (int32_t)(next_random() % (uint32_t)s_count);
        s_offset = 0;
    }

    ModApi_RestoreMenuBackground(bx, by, bw, bh);
    const int32_t y = by + (bh - 6) / 2;
    int32_t x = bx + bw - s_offset;
    for (const char *c = text; *c; c++) {
        char one[2] = { *c, 0 };
        const int32_t cw = ModApi_TextWidth(one, 1);
        if (x >= bx && x + cw <= bx + bw && *c != ' ')
            ModApi_DrawText(x, y, 2, one, 1);
        x += cw;
        if (x >= bx + bw)
            break;
    }
}

const ModDef ModDef_ticker = {
    .slug = SLUG,
    .menuDrawn = ticker_menu_drawn,
};

#endif /* SWOS_SDL_BUILD */
