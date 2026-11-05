/*
 * mod_careerplus_winners.c -- Career+'s PAST WINNERS (CAREER_PLUS.md F3): every title of a
 * career, kept from competitionFinished, and a screen of them from a competition's STATISTICS
 * (MODPOINT_COMPETITION_STATS): the viewed competition's winners, newest first, ten a page.
 * The base game's WINNERS screen is untouched (the owner's decision, 2026-11-05).
 *
 * Kept in the mod's section of the career save as "winners": [[season, competition, league,
 * division, team], ...] -- the team its id (file << 8 | ordinal), its name read when shown.
 */
#include "mod_api.h"
#include "mod_careerplus.h"
#include "mjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SWOS_SDL_BUILD

typedef struct {
    uint16_t season;
    uint8_t  competition;   /* comp_id: as competitionFinished and ModApi_ViewedCompetition say */
    uint8_t  league;        /* 1 a league's division, 0 a cup */
    uint8_t  division;      /* (a league's: 0 the top) */
    uint16_t team;
} Winner;

static Winner *s_winners;
static int32_t s_count, s_cap;

void CP_Winners_Clear(void)
{
    s_count = 0;
}

static void add(Winner w)
{
    for (int32_t i = 0; i < s_count; i++) {
        Winner *r = &s_winners[i];
        if (r->season == w.season && r->competition == w.competition && r->league == w.league &&
            r->division == w.division) {
            r->team = w.team;                   /* (decided again: the last word) */
            return;
        }
    }
    if (s_count == s_cap) {
        const int32_t cap = s_cap ? s_cap * 2 : 512;
        Winner *p = (Winner *)realloc(s_winners, (size_t)cap * sizeof *p);
        if (!p) {
            fprintf(stderr, "[careerplus] no memory for the winners: one not kept\n");
            return;
        }
        s_winners = p;
        s_cap = cap;
    }
    s_winners[s_count++] = w;
}

void CP_Winners_Record(const ModCompetitionResult *r)
{
    Winner w = { r->season, r->competitionId, (uint8_t)(r->kind == MODCOMP_LEAGUE),
                 (uint8_t)(r->kind == MODCOMP_LEAGUE ? r->division : 0), r->winner };
    add(w);
}

void CP_Winners_Save(ModJsonWriter *out)
{
    ModJson_Printf(out, ",\"winners\":[");
    for (int32_t i = 0; i < s_count; i++) {
        const Winner *w = &s_winners[i];
        ModJson_Printf(out, "%s[%u,%u,%u,%u,%u]", i ? "," : "", (unsigned)w->season,
                       (unsigned)w->competition, (unsigned)w->league, (unsigned)w->division,
                       (unsigned)w->team);
    }
    ModJson_Printf(out, "]");
}

void CP_Winners_Load(const char *json, int32_t len)
{
    const char *arr;
    int arrLen;
    s_count = 0;
    if (!json || mjson_find(json, len, "$.winners", &arr, &arrLen) != MJSON_TOK_ARRAY)
        return;
    int32_t *v = (int32_t *)malloc((size_t)arrLen / 2 * sizeof *v + sizeof *v);
    const int32_t count = v ? CP_Numbers(arr, arrLen, v, arrLen / 2) : 0;
    for (int32_t k = 0; k + 5 <= count; k += 5) {     /* [season, competition, league, division, team] */
        if (v[k] < 0 || v[k + 1] < 0 || v[k + 1] > 255 || v[k + 3] < 0 || v[k + 3] > 255 ||
            v[k + 4] < 0 || v[k + 4] > 65535)
            continue;
        Winner w = { (uint16_t)v[k], (uint8_t)v[k + 1], (uint8_t)(v[k + 2] != 0), (uint8_t)v[k + 3],
                     (uint16_t)v[k + 4] };
        add(w);
    }
    free(v);
}

/* ---- the screen (data_json/menus/careerplus/winnersMenu.json): entry 0 the title, 1-10 the
 *      rows, 11 PREVIOUS, 12 NEXT, 13 EXIT ---- */

enum { kTitle = 0, kFirstRow = 1, kRows = 10 };

static uint8_t s_competition, s_division;
static int32_t s_league, s_page;
static char s_title[40], s_text[kRows][48];

static int32_t matches(const Winner *w)
{
    return w->competition == s_competition && w->league == (uint8_t)s_league &&
           (!s_league || w->division == s_division);
}

static int32_t matching(void)
{
    int32_t n = 0;
    for (int32_t i = 0; i < s_count; i++)
        n += matches(&s_winners[i]);
    return n;
}

/* The k-th match, newest season first (each season's titles in the order they were decided). */
static const Winner *nth(int32_t k)
{
    int32_t top = 0;
    for (int32_t i = 0; i < s_count; i++)
        if (s_winners[i].season > top)
            top = s_winners[i].season;
    for (int32_t season = top; season >= 0; season--)
        for (int32_t i = 0; i < s_count; i++)
            if (s_winners[i].season == season && matches(&s_winners[i]) && k-- == 0)
                return &s_winners[i];
    return NULL;
}

static void fill(void)
{
    snprintf(s_title, sizeof s_title, "PAST WINNERS - PAGE %d", (int)s_page + 1);
    ModApi_MenuText(kTitle, s_title);
    for (int32_t r = 0; r < kRows; r++) {
        const Winner *w = nth(s_page * kRows + r);
        char name[24];
        if (!w)
            s_text[r][0] = 0;
        else if (ModApi_CareerTeamName(w->team, name, (int32_t)sizeof name) == 0)
            snprintf(s_text[r], sizeof s_text[r], "SEASON %02u   %s", (unsigned)w->season + 1, name);
        else
            snprintf(s_text[r], sizeof s_text[r], "SEASON %02u   -", (unsigned)w->season + 1);
        if (r == 0 && !w && s_page == 0)
            snprintf(s_text[r], sizeof s_text[r], "NO WINNERS YET IN THIS CAREER");
        ModApi_MenuText(kFirstRow + r, s_text[r]);
    }
}

static void redraw(void)
{
    fill();
    ModApi_MenuRedraw(kTitle);
    for (int32_t r = 0; r < kRows; r++)
        ModApi_MenuRedraw(kFirstRow + r);
}

static void winnersInit(void) { fill(); }

static void winnersPrevious(void)
{
    if (s_page > 0) {
        s_page--;
        redraw();
    }
}

static void winnersNext(void)
{
    if ((s_page + 1) * kRows < matching()) {
        s_page++;
        redraw();
    }
}

static void winnersOpen(void)
{
    uint8_t division = 0;
    int32_t league = 0;
    if (ModApi_ViewedCompetition(&s_competition, &league, &division) != 0) {
        ModApi_ShowError("NO COMPETITION TO SHOW");
        return;
    }
    s_league = league;
    s_division = division;
    s_page = 0;
    ModApi_ShowMenu("careerplus/winnersMenu");
}

void CP_Winners_Init(void)
{
    static const ModHandler kHandlers[] = {
        { "winnersInit", winnersInit },
        { "winnersPrevious", winnersPrevious },
        { "winnersNext", winnersNext },
    };
    ModApi_RegisterHandlers(CP_SLUG, kHandlers, (int32_t)(sizeof kHandlers / sizeof kHandlers[0]));
    ModApi_AddEntry(MODPOINT_COMPETITION_STATS, CP_SLUG, "PAST WINNERS", winnersOpen);
}

#endif /* SWOS_SDL_BUILD */
