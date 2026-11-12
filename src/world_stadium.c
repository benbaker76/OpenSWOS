/*
 * world_stadium.c -- a world's stadium names on the stadium screen (include/world_stadium.h;
 * docs/WORLD_2026_27.md, "Stadium names").
 *
 * The SWOS 96/97 Mod 4.1 shows the home club's ground under the two team bars before a match
 * (its tools/stadium.py: DATA\STADTMW5.DAT, made from tools/stadium_tmw5_clubs.csv and
 * stadium_tmw5_venues.csv -- full official names up to 48 letters, a neutral final's venue
 * first). The names are DATA (the Mod's licence §2): they come with the world's package, as
 *
 *     data_json/worlds/<tag>/competitions/stadiums.json
 *     { "clubs":  [ [file, ordinal, "NAME"], ... ],
 *       "venues": [ { "competition": "FA CUP", "round": "FINAL", "name": "WEMBLEY STADIUM",
 *                     "season": 1 }, ... ] }                 (season 0: every season)
 *
 * which scripts/world_import.py writes from the Mod's two CSVs when they are in its folder.
 * Without the file (today's package) nothing is shown: the screen is the game's.
 *
 * SDL only (the Amiga reads no JSON; a baked table is the way there if Davide's names are
 * shipped with the drawer's data).
 */
#include "world_stadium.h"
#include "world.h"

#if defined(SWOS_SDL_BUILD) && SWOS_WORLDS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mjson.h"
#include "swos_io.h"

static char   *s_json;                  /* the world's stadiums.json, read once */
static int32_t s_len;
static int32_t s_read = -1;             /* the world it was read for (-1: none yet) */

static void load(void)
{
    if (s_read == (int32_t)World_Current())
        return;
    free(s_json);
    s_json = NULL;
    s_len = 0;
    s_read = (int32_t)World_Current();
    if (World_Current() == WORLD_9697)
        return;
    char rel[96], path[1024];
    snprintf(rel, sizeof rel, "data_json/worlds/%s/competitions/stadiums.json", World_Tag());
    FILE *f = fopen(swos_data_path(rel, path, (uint32_t)sizeof path), "rb");
    if (!f)
        return;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n > 0 && n < 8 * 1024 * 1024 && (s_json = (char *)malloc((size_t)n + 1)) != NULL) {
        s_len = (int32_t)fread(s_json, 1, (size_t)n, f);
        s_json[s_len] = 0;
        fprintf(stderr, "[world] stadium names: %s (%d bytes)\n", rel, (int)s_len);
    }
    fclose(f);
}

/* The game's strings end at their NUL or at the '*' that pads them. */
static int32_t same_game_string(const char *game, const char *row)
{
    while (*row && *game && *game != '*' && *game == *row)
        game++, row++;
    return !*row && (!*game || *game == '*');
}

const char *WorldStadium_Name(uint8_t file, uint8_t ordinal, const char *gameName,
                              const char *gameRound, int32_t season)
{
    static char name[WORLD_STADIUM_NAME_MAX + 1];
    load();
    if (!s_json)
        return NULL;
    const char *arr;
    int32_t alen, koff, klen, voff, vlen, vtype, off = 0;
    /* a neutral final's venue first */
    if (gameName && mjson_find(s_json, s_len, "$.venues", &arr, &alen) == MJSON_TOK_ARRAY)
        while ((off = mjson_next(arr, alen, off, &koff, &klen, &voff, &vlen, &vtype)) != 0) {
            char comp[64], round[64];
            double ses = 0;
            const char *v = arr + voff;
            if (vtype != MJSON_TOK_OBJECT ||
                mjson_get_string(v, vlen, "$.competition", comp, sizeof comp) <= 0 ||
                mjson_get_string(v, vlen, "$.round", round, sizeof round) < 0 ||
                mjson_get_string(v, vlen, "$.name", name, sizeof name) <= 0)
                continue;
            mjson_get_number(v, vlen, "$.season", &ses);
            if (same_game_string(gameName, comp) && same_game_string(gameRound ? gameRound : "", round) &&
                (ses == 0 || (int32_t)ses == season))
                return name;
        }
    off = 0;
    if (mjson_find(s_json, s_len, "$.clubs", &arr, &alen) == MJSON_TOK_ARRAY)
        while ((off = mjson_next(arr, alen, off, &koff, &klen, &voff, &vlen, &vtype)) != 0) {
            double f = -1, o = -1;
            const char *v = arr + voff;
            if (vtype != MJSON_TOK_ARRAY || mjson_get_number(v, vlen, "$[0]", &f) <= 0 ||
                mjson_get_number(v, vlen, "$[1]", &o) <= 0)
                continue;
            if ((int32_t)f == file && (int32_t)o == ordinal)
                return mjson_get_string(v, vlen, "$[2]", name, sizeof name) > 0 ? name : NULL;
        }
    return NULL;
}

#else

const char *WorldStadium_Name(uint8_t file, uint8_t ordinal, const char *gameName,
                              const char *gameRound, int32_t season)
{
    (void)file; (void)ordinal; (void)gameName; (void)gameRound; (void)season;
    return 0;
}

#endif
