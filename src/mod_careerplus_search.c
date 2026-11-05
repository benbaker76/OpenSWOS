/*
 * mod_careerplus_search.c -- Career+'s player searches (CAREER_PLUS.md F4, F5): every club's
 * players as the career has them, ten a page.
 *
 *  - WORLD PLAYER SEARCH, from BUY PLAYER (MODPOINT_BUY_PLAYER): any club's player but yours,
 *    by position and a value band (MIN, MAX), the dearest first; fire on one makes an offer
 *    for him (ModApi_OfferForPlayer: the game's MAKE OFFER, and BUY PLAYER closes when he
 *    is bought).
 *  - SEARCH NATIONAL PLAYERS, from a national team's ADD PLAYER (MODPOINT_NATIONAL_ADD_PLAYER):
 *    the players of its nationality at every club, by position, the dearest, the cheapest or by
 *    name first; fire on one calls him up (ModApi_CallUpPlayer: ADD PLAYER adds him as HOME /
 *    ABROAD would).
 *
 * The clubs are the mod's copy (mod_careerplus_world.c), read again only when the career has
 * changed them since (CP_Search_Stale). The fork's world_player_search.c and
 * national_player_search.c.
 */
#include "mod_api.h"
#include "mod_careerplus.h"
#include "game_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SWOS_SDL_BUILD

enum {
    kTitle = 0, kFilter = 2, kSecond = 3, kThird = 4, kFirstRow = 5, kRows = 10, kPager = 15,
    kPositions = 9          /* ALL, then the eight */
};

typedef struct { int32_t club, slot, value; } Hit;

static int32_t s_national;      /* which search */
static int32_t s_stale = 1;     /* the copy of the clubs is older than the career's */
static Hit    *s_hits;
static int32_t s_hitCount, s_hitCap, s_page;
static int32_t s_filter, s_min, s_max = 10, s_sort;
static char    s_rows[kRows][64], s_filterText[24], s_secondText[24], s_thirdText[24], s_pageText[48];

static const char *const kPos[kPositions] = { "ALL", "GK", "RB", "LB", "D", "RW", "LW", "M", "ATT" };
static const int32_t kBands[] = { 0, 50, 100, 250, 500, 1000, 2000, 5000, 10000, 30000, 65535 };

void CP_Search_Stale(void) { s_stale = 1; }
void CP_Search_Fresh(void) { s_stale = 0; }

static void value_text(int32_t k, char *out, size_t cap)
{
    if (k >= 1000)
        snprintf(out, cap, "%d.%dM", (int)(k / 1000), (int)(k % 1000 / 100));
    else
        snprintf(out, cap, "%dK", (int)k);
}

static int32_t by_value(const void *a, const void *b)
{
    const Hit *x = (const Hit *)a, *y = (const Hit *)b;
    if (x->value != y->value)
        return s_sort == 1 ? x->value - y->value : y->value - x->value;
    return strcmp((const char *)CP_World_Club(x->club)->players[x->slot].playerName,
                  (const char *)CP_World_Club(y->club)->players[y->slot].playerName);
}

static int32_t by_name(const void *a, const void *b)
{
    const Hit *x = (const Hit *)a, *y = (const Hit *)b;
    return strcmp((const char *)CP_World_Club(x->club)->players[x->slot].playerName,
                  (const char *)CP_World_Club(y->club)->players[y->slot].playerName);
}

static void find(void)
{
    extern uint16_t _selTeamNumber;
    extern uint8_t _nationalTeamNationality;    /* (the national team being picked for) */
    s_hitCount = 0;
    for (int32_t c = 0; c < CP_World_Count(); c++) {
        const CP_Club *club = CP_World_Club(c);
        if (!s_national && club->team == _selTeamNumber)
            continue;
        for (int32_t s = 0; s < 16; s++) {
            const PlayerFile *p = &club->players[s];
            if (!CP_PlayerValid(p) || (s_filter && CP_Position(p) != s_filter - 1))
                continue;
            if (s_national && (uint8_t)p->nationality != _nationalTeamNationality)
                continue;
            const int32_t v = ModApi_PlayerValue(p);
            if (!s_national && (v < kBands[s_min] || v > kBands[s_max]))
                continue;
            if (s_hitCount == s_hitCap) {
                const int32_t cap = s_hitCap ? s_hitCap * 2 : 4096;
                Hit *h = (Hit *)realloc(s_hits, (size_t)cap * sizeof *h);
                if (!h)
                    return;
                s_hits = h, s_hitCap = cap;
            }
            s_hits[s_hitCount++] = (Hit){ c, s, v };
        }
    }
    qsort(s_hits, (size_t)s_hitCount, sizeof *s_hits, s_national && s_sort == 2 ? by_name : by_value);
    if (s_page * kRows >= s_hitCount)
        s_page = 0;
}

static void fill(void)
{
    const int32_t pages = s_hitCount ? (s_hitCount + kRows - 1) / kRows : 1;
    snprintf(s_filterText, sizeof s_filterText, "POSITION: %s", kPos[s_filter]);
    ModApi_MenuText(kFilter, s_filterText);
    if (s_national) {
        static const char *const kSort[3] = { "HIGH", "LOW", "NAME" };
        snprintf(s_secondText, sizeof s_secondText, "SORT: %s", kSort[s_sort]);
        ModApi_MenuVisible(kThird, 0);
    } else {
        snprintf(s_secondText, sizeof s_secondText, "MIN: %dK", (int)kBands[s_min]);
        if (s_max == 10)
            snprintf(s_thirdText, sizeof s_thirdText, "MAX: ANY");
        else
            snprintf(s_thirdText, sizeof s_thirdText, "MAX: %dK", (int)kBands[s_max]);
        ModApi_MenuText(kThird, s_thirdText);
    }
    ModApi_MenuText(kSecond, s_secondText);
    snprintf(s_pageText, sizeof s_pageText, "%d MATCHES  %d/%d", (int)s_hitCount, (int)s_page + 1, (int)pages);
    ModApi_MenuText(kPager, s_pageText);
    for (int32_t r = 0; r < kRows; r++) {
        const int32_t at = s_page * kRows + r;
        s_rows[r][0] = 0;
        if (at < s_hitCount) {
            const Hit *h = &s_hits[at];
            const CP_Club *club = CP_World_Club(h->club);
            const PlayerFile *p = &club->players[h->slot];
            char name[24], value[16];
            snprintf(name, sizeof name, "%.13s (%d)", (const char *)p->playerName, (int)CP_Age(club->team, h->slot));
            value_text(h->value, value, sizeof value);
            snprintf(s_rows[r], sizeof s_rows[r], "%-18.18s %-14.14s %-3s %6s", name, club->name,
                     kPos[CP_Position(p) + 1], value);
        }
        ModApi_MenuText(kFirstRow + r, s_rows[r]);
        ModApi_MenuVisible(kFirstRow + r, at < s_hitCount);
    }
}

static void redraw(void)
{
    find();
    fill();
    for (int32_t e = kFilter; e <= kPager; e++)
        ModApi_MenuRedraw(e);
}

static void searchInit(void)
{
    if (s_stale && CP_World_Read() >= 0)
        s_stale = 0;
    find();
    fill();
}

static void searchFilter(void) { s_filter = (s_filter + 1) % kPositions; s_page = 0; redraw(); }

static void searchSecond(void)
{
    if (s_national)
        s_sort = (s_sort + 1) % 3;
    else
        s_min = (s_min + 1) % 10 < s_max ? (s_min + 1) % 10 : 0;
    s_page = 0;
    redraw();
}

static void searchThird(void)
{
    if (s_national)
        return;
    s_max = s_max == 10 ? s_min + 1 : s_max + 1;
    s_page = 0;
    redraw();
}

static void searchPrev(void) { if (s_page > 0) { s_page--; redraw(); } }
static void searchNext(void) { if ((s_page + 1) * kRows < s_hitCount) { s_page++; redraw(); } }

static void pick(int32_t row)
{
    extern void SetExitMenuFlag(void);
    const int32_t at = s_page * kRows + row;
    if (at >= s_hitCount)
        return;
    const Hit *h = &s_hits[at];
    const uint16_t team = CP_World_Club(h->club)->team;
    if (s_national) {
        if (ModApi_CallUpPlayer(team, (uint16_t)h->slot) == 0)
            SetExitMenuFlag();      /* (ADD PLAYER then adds him) */
        else
            ModApi_ShowError("HE CANNOT BE CALLED UP");
        return;
    }
    const int32_t r = ModApi_OfferForPlayer(team, (uint16_t)h->slot);
    if (r == 1) {
        s_stale = 1;
        SetExitMenuFlag();          /* (bought: BUY PLAYER closes, as after the game's own buys) */
    } else if (r < 0)
        ModApi_ShowError("NO OFFER CAN BE MADE NOW");
}

static void searchRow0(void) { pick(0); }  static void searchRow1(void) { pick(1); }
static void searchRow2(void) { pick(2); }  static void searchRow3(void) { pick(3); }
static void searchRow4(void) { pick(4); }  static void searchRow5(void) { pick(5); }
static void searchRow6(void) { pick(6); }  static void searchRow7(void) { pick(7); }
static void searchRow8(void) { pick(8); }  static void searchRow9(void) { pick(9); }

static void open_world(void)
{
    s_national = 0, s_page = 0, s_sort = 0;
    ModApi_ShowMenu("careerplus/worldSearchMenu");
}

static void open_national(void)
{
    s_national = 1, s_page = 0, s_sort = 0;
    ModApi_ShowMenu("careerplus/nationalSearchMenu");
}

void CP_Search_Init(void)
{
    static const ModHandler kHandlers[] = {
        { "searchInit", searchInit }, { "searchFilter", searchFilter }, { "searchSecond", searchSecond },
        { "searchThird", searchThird }, { "searchPrev", searchPrev }, { "searchNext", searchNext },
        { "searchRow0", searchRow0 }, { "searchRow1", searchRow1 }, { "searchRow2", searchRow2 },
        { "searchRow3", searchRow3 }, { "searchRow4", searchRow4 }, { "searchRow5", searchRow5 },
        { "searchRow6", searchRow6 }, { "searchRow7", searchRow7 }, { "searchRow8", searchRow8 },
        { "searchRow9", searchRow9 },
    };
    ModApi_RegisterHandlers(CP_SLUG, kHandlers, (int32_t)(sizeof kHandlers / sizeof kHandlers[0]));
    ModApi_AddEntry(MODPOINT_BUY_PLAYER, CP_SLUG, "WORLD PLAYER SEARCH", open_world);
    ModApi_AddEntry(MODPOINT_NATIONAL_ADD_PLAYER, CP_SLUG, "SEARCH PLAYERS", open_national);
}

#endif /* SWOS_SDL_BUILD */
