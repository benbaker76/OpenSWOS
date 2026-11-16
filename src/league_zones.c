/*
 * league_zones.c -- a career's league tables in their zones' colours: include/league_zones.h.
 */
#include "league_zones.h"

#ifdef SWOS_SDL_BUILD
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "comp_identity.h"
#include "game_types.h"
#include "mjson.h"
#include "swos_io.h"
#include "uefa_access.h"
#include "uefa_coef.h"
#include "world.h"

extern DIYCompetitionEx _diyCompetitionStart;
extern uint8_t *_currentCompetitionPtr;
extern uint16_t _gameType;

#define ZT_MAX      256
#define ZT_RANGES   8
#define ZT_LEGEND   6
#define EU_PLACES   10

typedef struct {
    uint8_t compId, division, europe, nRanges, nLegend;
    uint8_t range[ZT_RANGES][3];        /* first place, last place, colour */
    uint8_t legCol[ZT_LEGEND];
    char    legLab[ZT_LEGEND][8];
} ZoneTable;

static ZoneTable s_tab[ZT_MAX];
static int32_t   s_n;
static char      s_tag[8];              /* the world whose file is read ("": none yet) */

/* The Mod's colours are the PC menu palette's (zones.py: 0Dh light blue, 0Eh green, 08h dark blue,
 * 0Ch orange, 02h gray); the menus here draw with the Amiga's (swos_globals.c _menuPalette), where
 * 2 is white, 8 dark purple and 12 maroon: each to the Amiga's own of that colour. SWOS United's own
 * (Playa, 2026-11-16): relegation brown, a play-off's places gray -- the file's green (0Eh) is a
 * play-off's; the Europa League's green comes from the access list (`european`) and stays green. */
static uint16_t amiga_colour(uint8_t c, int32_t european)
{
    switch (c) {
    case 0x0D: return 13;               /* light blue (0x88F) */
    case 0x0E: return european ? 14 : 1;    /* green (0x380) / a play-off: gray (0xAAB) */
    case 0x08: return 11;               /* blue (0x00F) */
    case 0x0C: return 6;                /* orange (0xF71) */
    case 0x02: return 4;                /* relegation: brown (0x621) */
    default:   return c;
    }
}

/* the table being drawn */
static const ZoneTable *s_cur;
static uint8_t          s_eu[EU_PLACES];
static int32_t          s_euOn;

static void load(void)
{
    const char *tag = World_Tag();
    if (!strcmp(s_tag, tag))
        return;
    snprintf(s_tag, sizeof s_tag, "%s", tag);
    s_n = 0;
    char rel[64], path[512];
    snprintf(rel, sizeof rel, "data_json/zones/%s.json", tag);
    FILE *f = fopen(swos_data_path(rel, path, (uint32_t)sizeof path), "rb");
    if (!f)
        return;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *j = size > 0 ? (char *)malloc((size_t)size) : NULL;
    if (!j || fread(j, 1, (size_t)size, f) != (size_t)size) {
        free(j);
        fclose(f);
        return;
    }
    fclose(f);
    for (int32_t i = 0; s_n < ZT_MAX; i++) {
        char q[48];
        const char *t;
        int tl;
        snprintf(q, sizeof q, "$.tables[%d]", (int)i);
        if (mjson_find(j, (int)size, q, &t, &tl) != MJSON_TOK_OBJECT)
            break;
        ZoneTable *z = &s_tab[s_n];
        double v;
        memset(z, 0, sizeof *z);
        if (mjson_get_number(t, tl, "$.comp_id", &v) <= 0)
            continue;
        z->compId = (uint8_t)v;
        if (mjson_get_number(t, tl, "$.division", &v) > 0) z->division = (uint8_t)v;
        if (mjson_get_number(t, tl, "$.europe", &v) > 0) z->europe = (uint8_t)v;
        for (int32_t r = 0; r < ZT_RANGES; r++) {
            double a, b, c;
            char p[3][32];
            snprintf(p[0], sizeof p[0], "$.ranges[%d][0]", (int)r);
            snprintf(p[1], sizeof p[1], "$.ranges[%d][1]", (int)r);
            snprintf(p[2], sizeof p[2], "$.ranges[%d][2]", (int)r);
            if (mjson_get_number(t, tl, p[0], &a) <= 0 || mjson_get_number(t, tl, p[1], &b) <= 0 ||
                mjson_get_number(t, tl, p[2], &c) <= 0)
                break;
            z->range[r][0] = (uint8_t)a;
            z->range[r][1] = (uint8_t)b;
            z->range[r][2] = (uint8_t)c;
            z->nRanges++;
        }
        for (int32_t k = 0; k < ZT_LEGEND; k++) {
            double c;
            char p[2][32];
            snprintf(p[0], sizeof p[0], "$.legend[%d][0]", (int)k);
            snprintf(p[1], sizeof p[1], "$.legend[%d][1]", (int)k);
            if (mjson_get_number(t, tl, p[0], &c) <= 0 ||
                mjson_get_string(t, tl, p[1], z->legLab[k], (int)sizeof z->legLab[k]) <= 0)
                break;
            z->legCol[k] = (uint8_t)c;
            z->nLegend++;
        }
        s_n++;
    }
    free(j);
    fprintf(stderr, "[zones] %s: %d league tables\n", rel, (int)s_n);
}

/* The division of the career's league whose title the table shows (the title is the division's name
 * after the country's: InitDIYCompetitionFromTableEntry); -1 none. */
static int32_t division_shown(const uint8_t *league)
{
    const Competition *c = (const Competition *)league;
    char **names = comp_name_table((Competition *)c);
    const char *title = (const char *)_diyCompetitionStart.base.competitionName;
    const size_t tl = strlen(title);
    const CompetitionLeague *lg = (const CompetitionLeague *)league;
    int32_t best = -1;
    size_t bestLen = 0;
    for (int32_t d = 0; names && d < lg->header.divisionsCount; d++) {
        const char *n = names[d * 2];
        const size_t nl = n ? strlen(n) : 0;
        if ((const uint8_t *)n == _diyCompetitionStart.base.namePtr)
            return d;
        if (nl && nl <= tl && !strcmp(title + tl - nl, n) && nl > bestLen) {
            best = d;
            bestLen = nl;
        }
    }
    return best;
}

int32_t LeagueZones_Begin(uint16_t mostPlayed, uint16_t perGroup)
{
    s_cur = NULL;
    s_euOn = 0;
    if (_gameType != 4 || World_Current() == WORLD_9697 || !_currentCompetitionPtr)
        return 0;
    load();
    const uint8_t compId = (uint8_t)_diyCompetitionStart.base.field_2E;
    if (!s_n || comp_id(_currentCompetitionPtr) != compId)
        return 0;
    /* from mid-season: the leaders half way through the season's matches */
    const int32_t rounds = _diyCompetitionStart.base.playEachTeam > 0 ? _diyCompetitionStart.base.playEachTeam : 1;
    const int32_t season = (int32_t)(perGroup > 1 ? perGroup - 1 : 1) * rounds;
    if (2 * (int32_t)mostPlayed < season)
        return 0;
    const int32_t d = division_shown(_currentCompetitionPtr);
    for (int32_t i = 0; i < s_n && d >= 0; i++)
        if (s_tab[i].compId == compId && s_tab[i].division == d) {
            s_cur = &s_tab[i];
            break;
        }
    if (!s_cur)
        return 0;
    int32_t rank = 0;
    if (s_cur->europe) {                /* (a league-phase top flight: its European places by its rank) */
        rank = UefaCoef_AssociationRank((uint8_t)((const Competition *)_currentCompetitionPtr)->teamNumber);
        s_euOn = rank > 0 && UefaAccess_PlaceColours(rank, s_eu, EU_PLACES);
    }
    {   /* (once a table: what it shows; SWOS_TEST_ZONES=1 a screenshot of it) */
        static const ZoneTable *told;
        if (told != s_cur) {
            told = s_cur;
            fprintf(stderr, "[zones] comp %u division %u: %u ranges, %u legend boxes, UEFA rank %d%s\n",
                    (unsigned)compId, (unsigned)d, (unsigned)s_cur->nRanges, (unsigned)s_cur->nLegend, (int)rank,
                    s_euOn ? " (its European places)" : "");
            if (getenv("SWOS_TEST_ZONES")) {
                extern void Platform_RequestScreenshot(void);
                Platform_RequestScreenshot();
            }
        }
    }
    return 1;
}

uint16_t LeagueZones_RowColour(int32_t place)
{
    if (!s_cur || place < 1)
        return 0;
    if (s_euOn && place <= EU_PLACES && s_eu[place - 1])
        return amiga_colour(s_eu[place - 1], 1);
    for (int32_t r = 0; r < s_cur->nRanges; r++)
        if (place >= s_cur->range[r][0] && place <= s_cur->range[r][1])
            return amiga_colour(s_cur->range[r][2], 0);
    return 0;
}

int32_t LeagueZones_Legend(uint16_t *colour, const char **label, int32_t max)
{
    int32_t n = 0;
    if (!s_cur)
        return 0;
    if (s_euOn) {                       /* (its European colours first, in place order, one box each) */
        static const char *const kEu[3] = { "CL", "EL", "CONF" };
        uint8_t seen = 0;
        for (int32_t p = 0; p < EU_PLACES && n < max; p++) {
            const int32_t k = s_eu[p] == 0x0D ? 0 : s_eu[p] == 0x0E ? 1 : s_eu[p] == 0x08 ? 2 : -1;
            if (k < 0 || (seen & (1 << k)))
                continue;
            seen |= (uint8_t)(1 << k);
            colour[n] = amiga_colour(s_eu[p], 1);
            label[n++] = kEu[k];
        }
    }
    for (int32_t k = 0; k < s_cur->nLegend && n < max; k++) {
        colour[n] = amiga_colour(s_cur->legCol[k], 0);
        label[n++] = s_cur->legLab[k];
    }
    return n;
}

#else
int32_t  LeagueZones_Begin(uint16_t mostPlayed, uint16_t perGroup) { (void)mostPlayed; (void)perGroup; return 0; }
uint16_t LeagueZones_RowColour(int32_t place) { (void)place; return 0; }
int32_t  LeagueZones_Legend(uint16_t *colour, const char **label, int32_t max)
{
    (void)colour; (void)label; (void)max;
    return 0;
}
#endif
