/*
 * bteam.c -- the B-team call-ups of a world's career (include/bteam.h).
 *
 * Ported from the SWOS 96/97 Mod 4.1's tools/bteam.py (4.0.6, github.com/aminta/swos-9697-mod).
 * Copyright (c) 2026 Davide Lorigliola, MIT licence:
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 *   and associated documentation files (the "Software"), to deal in the Software without
 *   restriction, including without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
 *   Software is furnished to do so, subject to the following conditions: The above copyright
 *   notice and this permission notice shall be included in all copies or substantial portions of
 *   the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 *
 * The Mod's routines and ours:
 *   bt_can / bt_add -> BTeam_Can / BTeam_Add (the same checks in the same order, the same codes)
 *   bt_back / bt_ret -> back(), BTeam_ReturnMatch (the calendar's step: its bt_adv),
 *                       BTeam_ReturnAll (ContinueCareerNextSeason, before the season's update:
 *                       its bt_end)
 *   bt_wst  -> BTeam_CalledKind in Career_UpdatePlayerWeeklyWageCache (a one-match call-up: 0)
 *   bt_gen  -> BTeam_CalledKind in the surplus fillers' removal (transfer_market.c)
 *   bt_readto -> BTeam_Holds in squad_extras.c (a called-up extra is not on his club's page 2)
 *   the squad screen's SQ. B and the call-up page -> BTeam_SquadMenuInit, BTeamSquadButton (the B
 *     squad in BUY OTHER PLAYERS' screen, its page 2 the extras) and a dialog (menu_dialog.c)
 * His careerTeam slot is found by his name (the Mod: a hash of it), as slots do not move but what
 * a slot holds may.
 */
#include "bteam.h"
#include "squad_extras.h"
#include "world.h"
#include "menu_system.h"

#include <string.h>
#ifdef SWOS_SDL_BUILD
#include <stdio.h>
#include <stdlib.h>
#endif

BTeamState g_bteam;

extern uint16_t   _gameType, _isNationalSelector, _selTeamNumber, _careerNumPlayers;
extern uint8_t    _currentMatchPlayers[];
extern TeamFileEx _careerTeam;
extern uint8_t    _seasonPlaying;
extern MenuEntry  _menuEntries[];
extern uint8_t    _currentMenu[];

#define GAME_TYPE_CAREER 4
#define PF_OFF           76

void BTeam_Reset(void)
{
    memset(&g_bteam, 0, sizeof g_bteam);
    SquadExtras_Changed();
}

static int32_t same_name(const int8_t *a, const int8_t *b)
{
    for (int32_t i = 0; i < 23; i++) {
        if (a[i] != b[i])
            return 0;
        if (!a[i])
            return 1;
    }
    return 1;
}

int32_t BTeam_Holds(uint16_t team, uint16_t index)
{
    for (int32_t i = 0; i < g_bteam.n; i++)
        if (g_bteam.call[i].team == team && g_bteam.call[i].index == index)
            return 1;
    return 0;
}

int32_t BTeam_CalledKind(const PlayerFile *p)
{
    if (!g_bteam.n || !p->playerName[0])
        return 0;
    for (int32_t i = 0; i < g_bteam.n; i++)
        if (same_name(g_bteam.call[i].name, p->playerName))
            return g_bteam.call[i].kind & 3;
    return 0;
}

const char *BTeam_CodeText(int32_t code)
{
    static const char *const k[] = {
        "", "AT MOST 4 A MATCH", "TOO MANY OVER-AGE PLAYERS", "TRANSFER WINDOW CLOSED", "SQUAD FULL (30)",
        "AT MOST 3 A WINDOW", "ALREADY CALLED UP", "NO B TEAM", "NOT A VALID PLAYER", "TOO MANY CALLED UP",
    };
    return code >= 0 && code < (int32_t)(sizeof k / sizeof k[0]) ? k[code] : "";
}

#if defined(SWOS_SDL_BUILD) && SWOS_WORLDS

#include "mjson.h"
#include "swos_io.h"
#include "menu_dialog.h"

extern uint8_t *GetPlayer(uint16_t teamNumber, uint16_t playerFileIndex);
extern void     Career_RemovePlayerFromTeamInternal(uint16_t teamNumber, uint16_t playerOrd);
extern void     Career_ConvertPlayerToTrialistFromName(uint16_t sourceTeam, uint16_t playerOrd);
extern void     Career_RemovePlayerFromSelectedTeamRoster(uint16_t index);
extern int32_t  Career_WriteTeamPlayer(uint16_t teamId, uint16_t slot, const PlayerFile *player);
extern uint16_t Career_NewPlayerSquadPos(void);
extern void     TrainingMatch_SaveSelectedPlayerId(uint16_t value);
extern void     SetBenchPlayersNumbers(TeamFileEx *team);
extern void     SquadNumbers_Free(uint16_t slot);
extern void     Career_UpdatePlayerWeeklyWageCache(uint16_t playerIndex);
extern int32_t  DataJson_LoadTeamBorn(int32_t file, int32_t ordinal, uint16_t *born, int32_t max);
extern uint8_t  _teamSelectionKeyCache[];
extern uint16_t _teamSelectionKeyCacheOffset, _teamSelectionKeyCacheCount;

/* ---- the world's B teams (competitions/bteams.json, from the Mod's DATA\BAGE.nnn) ---- */

#define MAX_TEAMS 64
#define MAX_RULES 16
#define MAX_WINDOWS 4
static struct {
    int32_t  world;                     /* the world read for, -1 none yet */
    int32_t  nTeams, nRules;
    struct { uint16_t b, parent; uint8_t rules; } team[MAX_TEAMS];
    struct { uint8_t cap, u23, gkAnyAge, nWin; uint8_t win[MAX_WINDOWS][2]; } rule[MAX_RULES];
} B = { -1, 0, 0, { { 0, 0, 0 } }, { { 0, 0, 0, 0, { { 0, 0 } } } } };

static void load(void)
{
    if (B.world == (int32_t)World_Current())
        return;
    B.world = (int32_t)World_Current();
    B.nTeams = B.nRules = 0;
    if (World_Current() == WORLD_9697)
        return;
    char rel[96], path[1024];
    snprintf(rel, sizeof rel, "data_json/worlds/%s/competitions/bteams.json", World_Tag());
    FILE *f = fopen(swos_data_path(rel, path, (uint32_t)sizeof path), "rb");
    if (!f)
        return;
    fseek(f, 0, SEEK_END);
    const long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *js = len > 0 && len < 1024 * 1024 ? (char *)malloc((size_t)len + 1) : NULL;
    const int32_t n = js ? (int32_t)fread(js, 1, (size_t)len, f) : 0;
    fclose(f);
    if (!js)
        return;
    js[n] = 0;
    const char *arr;
    int alen, koff, klen, voff, vlen, vt, off = 0;
    if (mjson_find(js, n, "$.rules", &arr, &alen) == MJSON_TOK_ARRAY)
        while (B.nRules < MAX_RULES && (off = mjson_next(arr, alen, off, &koff, &klen, &voff, &vlen, &vt)) != 0) {
            const char *v = arr + voff, *w;
            double d = 0;
            int wl, wo = 0, k2, kl2, v2, vl2, t2;
            int gk = 0;
            if (vt != MJSON_TOK_OBJECT)
                continue;
            B.rule[B.nRules].cap = mjson_get_number(v, vlen, "$.over_age_cap", &d) ? (uint8_t)d : 0xFF;
            B.rule[B.nRules].u23 = mjson_get_number(v, vlen, "$.u23", &d) ? (uint8_t)d : 23;
            B.rule[B.nRules].gkAnyAge = (uint8_t)(mjson_get_bool(v, vlen, "$.gk_any_age", &gk) && gk);
            B.rule[B.nRules].nWin = 0;
            if (mjson_find(v, vlen, "$.windows", &w, &wl) == MJSON_TOK_ARRAY)
                while (B.rule[B.nRules].nWin < MAX_WINDOWS &&
                       (wo = mjson_next(w, wl, wo, &k2, &kl2, &v2, &vl2, &t2)) != 0) {
                    double a = 0, z = 0;
                    if (t2 == MJSON_TOK_ARRAY && mjson_get_number(w + v2, vl2, "$[0]", &a) &&
                        mjson_get_number(w + v2, vl2, "$[1]", &z)) {
                        B.rule[B.nRules].win[B.rule[B.nRules].nWin][0] = (uint8_t)a;
                        B.rule[B.nRules].win[B.rule[B.nRules].nWin++][1] = (uint8_t)z;
                    }
                }
            B.nRules++;
        }
    off = 0;
    if (mjson_find(js, n, "$.teams", &arr, &alen) == MJSON_TOK_ARRAY)
        while (B.nTeams < MAX_TEAMS && (off = mjson_next(arr, alen, off, &koff, &klen, &voff, &vlen, &vt)) != 0) {
            const char *v = arr + voff;
            double b = -1, p = -1, r = -1;
            if (vt == MJSON_TOK_OBJECT && mjson_get_number(v, vlen, "$.b", &b) &&
                mjson_get_number(v, vlen, "$.parent", &p) && mjson_get_number(v, vlen, "$.rules", &r) &&
                r >= 0 && r < B.nRules) {
                B.team[B.nTeams].b = (uint16_t)b;
                B.team[B.nTeams].parent = (uint16_t)p;
                B.team[B.nTeams++].rules = (uint8_t)r;
            }
        }
    free(js);
    fprintf(stderr, "[bteam] %s: %d B teams, %d rules\n", rel, (int)B.nTeams, (int)B.nRules);
}

static int32_t row_of(uint16_t parent)
{
    load();
    for (int32_t i = 0; i < B.nTeams; i++)
        if (B.team[i].parent == parent)
            return i;
    return -1;
}

uint16_t BTeam_Of(uint16_t parent)
{
    const int32_t r = row_of(parent);
    return r < 0 ? 0xFFFF : B.team[r].b;
}

int32_t BTeam_Shown(void)
{
    return World_Current() != WORLD_9697 && _gameType == GAME_TYPE_CAREER && !_isNationalSelector &&
           BTeam_Of(_selTeamNumber) != 0xFFFF;
}

/* A B team's players' years (the team file's "born"), cached for the last team asked. */
uint16_t BTeam_Born(uint16_t team, uint16_t index)
{
    static uint16_t cacheTeam = 0xFFFF;
    static int32_t cacheWorld = -1;
    static uint16_t born[16 + SQX_MAX];
    if (team != cacheTeam || cacheWorld != (int32_t)World_Current()) {
        cacheTeam = team;
        cacheWorld = (int32_t)World_Current();
        if (DataJson_LoadTeamBorn(team >> 8, team & 0xFF, born, 16 + SQX_MAX) <= 0)
            memset(born, 0, sizeof born);
    }
    return index < 16 + SQX_MAX ? born[index] : 0;
}

/* The player at the B team's place `index` (its record, the career's changes applied; or its
 * extras', not gone), or NULL (bt_src). */
static PlayerFile *source(uint16_t team, uint16_t index)
{
    PlayerFile *p = index < 16 ? (PlayerFile *)(GetPlayer(team, index) + PF_OFF) : SquadExtras_Player(team, index);
    return p && p->playerName[0] ? p : NULL;
}

/* The career calendar's week now (the Mod's split.week_of scale: 8 a month from January of the
 * season's first year), 0xFF none. */
static uint8_t week_now(void)
{
    if (!_teamSelectionKeyCacheCount)
        return 0xFF;
    return ((const TeamSelectionKeyCacheEntry *)(_teamSelectionKeyCache + _teamSelectionKeyCacheOffset))->sortKey;
}

static uint8_t s_over, s_window;        /* BTeam_Can's findings for BTeam_Add */

int32_t BTeam_Can(int32_t kind, uint16_t index)
{
    s_over = s_window = 0;
    if (_isNationalSelector || _gameType != GAME_TYPE_CAREER)
        return BTEAM_NO_BTEAM;
    const int32_t r = row_of(_selTeamNumber);
    if (r < 0)
        return BTEAM_NO_BTEAM;
    const uint16_t bt = B.team[r].b;
    if (index >= 16 + SquadExtras_Count(bt))
        return BTEAM_NO_PLAYER;
    if (BTeam_Holds(bt, index))
        return BTEAM_ALREADY;
    const PlayerFile *src = source(bt, index);
    if (!src)
        return BTEAM_NO_PLAYER;
    if (_careerNumPlayers >= 30)
        return BTEAM_SQUAD_FULL;
    if (g_bteam.n >= BTEAM_MAX)
        return BTEAM_LIST_FULL;
    const int32_t ru = B.team[r].rules;
    const uint16_t born = BTeam_Born(bt, index);
    const int32_t keeper = (src->positionAndFace & PF_POSITION_MASK) == 0;
    if (born && !(B.rule[ru].gkAnyAge && keeper)) {
        const int32_t year = (int32_t)World_SeasonYear(_seasonPlaying);
        if ((int32_t)born < year - (int32_t)B.rule[ru].u23)      /* (born before: 23 or older this season) */
            s_over = 1;
    }
    if (kind == 1) {
        int32_t calls = 0, over = 0;
        for (int32_t i = 0; i < g_bteam.n; i++)
            if ((g_bteam.call[i].kind & 3) == 1) {
                calls++;
                over += (g_bteam.call[i].kind >> 3) & 1;
            }
        if (calls >= BTEAM_MAX_MATCH)
            return BTEAM_MAX4_MATCH;
        if (s_over && over >= B.rule[ru].cap)
            return BTEAM_OVER_AGE;
        return BTEAM_OK;
    }
    const uint8_t now = week_now();
    for (int32_t w = 0; w < B.rule[ru].nWin; w++)
        if (now >= B.rule[ru].win[w][0] && now <= B.rule[ru].win[w][1]) {
            s_window = (uint8_t)(w + 1);
            break;
        }
    if (!s_window)
        return BTEAM_WINDOW_CLOSED;
    int32_t inWindow = 0;
    for (int32_t i = 0; i < g_bteam.n; i++)
        if ((g_bteam.call[i].kind & 0x33) == (uint8_t)(s_window << 4 | 2))
            inWindow++;
    return inWindow >= BTEAM_MAX_WINDOW ? BTEAM_MAX3_WINDOW : BTEAM_OK;
}

int32_t BTeam_Add(int32_t kind, uint16_t index)
{
    const int32_t code = BTeam_Can(kind, index);
    if (code != BTEAM_OK)
        return code;
    const uint16_t bt = BTeam_Of(_selTeamNumber);
    PlayerFile tmp = *source(bt, index);
    /* the lowest slot not in the squad order (a purchase's way) */
    uint16_t slot = 0;
    for (int32_t again = 1; again;) {
        again = 0;
        for (uint16_t i = 0; i < _careerNumPlayers; i++)
            if (_currentMatchPlayers[i] == (uint8_t)slot) {
                slot++;
                again = 1;
                break;
            }
    }
    TrainingMatch_SaveSelectedPlayerId(slot);
    tmp.positionAndFace |= PF_LOAN;     /* on loan: never listed, never bid for; his value LOAN */
    tmp.goalStats[0] = tmp.goalStats[1] = tmp.goalStats[2] = tmp.goalStats[3] = 0;
    *(PlayerFile *)((uint8_t *)&_careerTeam + PF_OFF + slot * sizeof(PlayerFile)) = tmp;
    /* after the match's 11 and bench, in the squad's order */
    const uint16_t at = Career_NewPlayerSquadPos();
    for (uint16_t i = _careerNumPlayers; i > at; i--)
        _currentMatchPlayers[i] = _currentMatchPlayers[i - 1];
    _currentMatchPlayers[at] = (uint8_t)slot;
    BTeamCall *c = &g_bteam.call[g_bteam.n++];      /* (before the wage: a one-match call-up is paid 0) */
    c->kind = (uint8_t)(kind | s_window << 4 | s_over << 3);
    c->index = (uint8_t)index;
    c->team = bt;
    memset(c->name, 0, sizeof c->name);
    memcpy(c->name, tmp.playerName, 22);
    SetBenchPlayersNumbers(&_careerTeam);
    _careerNumPlayers++;
    SquadNumbers_Free(slot);
    Career_UpdatePlayerWeeklyWageCache(slot);
    Career_RemovePlayerFromTeamInternal(bt, index);         /* (off the markets) */
    if (kind == 2 && index < 16)
        Career_ConvertPlayerToTrialistFromName(bt, index);  /* (the B team: the game's generated player) */
    _careerTeam.team.playerValuesDirty = TEAM_VALUES_DIRTY;
    SquadExtras_Changed();
    fprintf(stderr, "[bteam] %.22s (%04x place %u) called up %s%s, careerTeam slot %u\n",
            (const char *)c->name, (unsigned)bt, (unsigned)index + 1, kind == 1 ? "for the next match" : "for the season",
            s_over ? " (over-age)" : "", (unsigned)slot);
    return BTEAM_OK;
}

/* His careerTeam slot by name, in the squad order, or -1 (bt_slot). */
static int32_t slot_of(const int8_t *name)
{
    for (int32_t k = (int32_t)_careerNumPlayers - 1; k >= 0; k--) {
        const uint8_t s = _currentMatchPlayers[k];
        if (s < 32 && same_name(((const PlayerFile *)((uint8_t *)&_careerTeam + PF_OFF + s * sizeof(PlayerFile)))
                                    ->playerName, name))
            return s;
    }
    return -1;
}

/* The player of entry i back to his B team, out of the career squad (bt_back). */
static void back(int32_t i)
{
    const BTeamCall c = g_bteam.call[i];
    const int32_t slot = slot_of(c.name);
    for (int32_t k = i; k + 1 < g_bteam.n; k++)
        g_bteam.call[k] = g_bteam.call[k + 1];
    g_bteam.n--;
    memset(&g_bteam.call[g_bteam.n], 0, sizeof g_bteam.call[0]);
    if (slot >= 0) {
        if ((c.kind & 3) == 2 && c.index < 16) {
            /* the season: he goes back as he is now (form, value) in place of the generated one */
            PlayerFile p = *(const PlayerFile *)((uint8_t *)&_careerTeam + PF_OFF + slot * sizeof(PlayerFile));
            p.positionAndFace &= (uint8_t)~PF_LOAN;
            Career_WriteTeamPlayer(c.team, c.index, &p);
        }
        Career_RemovePlayerFromSelectedTeamRoster((uint16_t)slot);
        _careerTeam.team.playerValuesDirty = TEAM_VALUES_DIRTY;
    }
    SquadExtras_Changed();
    fprintf(stderr, "[bteam] %.22s back to %04x (place %u)\n", (const char *)c.name, (unsigned)c.team,
            (unsigned)c.index + 1);
}

static void return_kinds(int32_t kinds)
{
    for (int32_t i = g_bteam.n - 1; i >= 0; i--)
        if ((g_bteam.call[i].kind & 3) & kinds)
            back(i);
}

void BTeam_ReturnMatch(void)
{
    if (g_bteam.n)
        return_kinds(1);
}

void BTeam_ReturnAll(void)
{
    if (g_bteam.n)
        return_kinds(3);
}

/* ---- the squad screen's B TEAM (squadMenu entry 64) and the call-up page ---- */

#define SQ_BUTTON 64
#define SQ_GOALS  8
#define SQ_EXIT   9
#define SQ_COACH  5

static uint8_t  s_browsing;             /* BUY OTHER PLAYERS' screen shows the B team (transfer_market.c) */
static uint16_t s_browseTeam;

int32_t BTeam_Browsing(void) { return s_browsing; }
uint16_t BTeam_BrowseTeam(void) { return s_browseTeam; }

void BTeam_SquadMenuInit(void)
{
    extern uintptr_t _squadTeamFilePtr;
    extern void *_charTables[];
    extern uint16_t GetTextWidth(uint8_t *text, uint8_t *charsTable);
    const Menu *m = (const Menu *)_currentMenu;
    if (m->numEntries <= SQ_BUTTON)
        return;
    _menuEntries[SQ_BUTTON].isInvisible = 1;
    if (_squadTeamFilePtr != (uintptr_t)&_careerTeam || !BTeam_Shown())
        return;
    _menuEntries[SQ_BUTTON].isInvisible = 0;
    _menuEntries[SQ_BUTTON].fg = (uintptr_t)"B TEAM";
    _menuEntries[SQ_GOALS].leftEntry = SQ_BUTTON;
    /* the coach's name, shortened to end before the button (the name itself is kept) */
    static char coach[24];
    const char *name = (const char *)_menuEntries[SQ_COACH].fg;
    const int32_t room = (int32_t)_menuEntries[SQ_BUTTON].x - (int32_t)_menuEntries[SQ_COACH].x - 3;
    if (name && room > 0) {
        int32_t n = 0;
        while (n < 23 && name[n])
            n++;
        memcpy(coach, name, (size_t)n);
        coach[n] = 0;
        while (n > 0 && GetTextWidth((uint8_t *)coach, (uint8_t *)_charTables[0]) > (uint16_t)room)
            coach[--n] = 0;
        _menuEntries[SQ_COACH].fg = (uintptr_t)coach;
    }
}

/* A row picked in the B squad: the call-up page. */
void BTeam_PickPlayer(uint16_t index)
{
    const uint16_t bt = s_browseTeam;
    const PlayerFile *p = source(bt, index);
    if (!p)
        return;
    char name[24], text[256];
    memcpy(name, p->playerName, 22);
    name[22] = 0;
    const uint16_t born = BTeam_Born(bt, index);
    const int32_t year = (int32_t)World_SeasonYear(_seasonPlaying);
    const int32_t r = row_of(_selTeamNumber);
    const int32_t u = r >= 0 ? B.rule[B.team[r].rules].u23 : 23;
    if (born)
        snprintf(text, sizeof text, "%s, BORN %u: %s THIS SEASON.", name, (unsigned)born,
                 (int32_t)born < year - u ? "OVER-AGE" : "UNDER 23");
    else
        snprintf(text, sizeof text, "%s: YEAR OF BIRTH UNKNOWN, COUNTS AS UNDER 23.", name);
    static const char *const buttons[] = { "NEXT MATCH ONLY", "FOR THE SEASON", "EXIT" };
    const int32_t b = MenuDialog_Show("B TEAM CALL-UP", text, buttons, 3, 0);
    if (b != 0 && b != 1)
        return;
    const int32_t code = BTeam_Add(b + 1, index);
    static const char *const ok[] = { "OK" };
    MenuDialog_Show("B TEAM CALL-UP", code == BTEAM_OK ? (b == 0 ? "CALLED UP FOR THE NEXT MATCH."
                                                                  : "CALLED UP FOR THE SEASON.")
                                                      : BTeam_CodeText(code), ok, 1, 0);
}

__attribute__((used, externally_visible))
void BTeamSquadButton(void)
{
    extern uint8_t  _buyOtherPlayersMenu[];
    extern uint8_t *_buyOtherPlayersTitlePtr;
    const uint16_t bt = BTeam_Of(_selTeamNumber);
    if (bt == 0xFFFF)
        return;
    uint8_t *const title = _buyOtherPlayersTitlePtr;
    s_browsing = 1;
    s_browseTeam = bt;
    _buyOtherPlayersTitlePtr = (uint8_t *)"B TEAM: CHOOSE A PLAYER";
    ShowMenu(_buyOtherPlayersMenu);
    s_browsing = 0;
    _buyOtherPlayersTitlePtr = title;
}

/* ---- tests ---- */

int32_t BTeam_SelfTest(const char *arg)
{
    load();
    printf("[bteam] world %s: %d B teams, %d rules\n", World_Tag(), (int)B.nTeams, (int)B.nRules);
    for (int32_t r = 0; r < B.nRules; r++)
        printf("  rules %d: over-age cap %d, U%d, keeper any age %d, windows %d (%d-%d, %d-%d)\n", (int)r,
               (int)B.rule[r].cap, (int)B.rule[r].u23, (int)B.rule[r].gkAnyAge, (int)B.rule[r].nWin,
               (int)B.rule[r].win[0][0], (int)B.rule[r].win[0][1], (int)B.rule[r].win[1][0], (int)B.rule[r].win[1][1]);
    const long want = arg && *arg ? strtol(arg, NULL, 16) : -1;
    for (int32_t i = 0; i < B.nTeams; i++) {
        if (want >= 0 && B.team[i].parent != (uint16_t)want)
            continue;
        const uint16_t bt = B.team[i].b;
        uint16_t born[16 + SQX_MAX];
        const int32_t n = DataJson_LoadTeamBorn(bt >> 8, bt & 0xFF, born, 16 + SQX_MAX);
        int32_t known = 0, under = 0;
        const int32_t year = (int32_t)World_FirstYear();
        for (int32_t k = 0; k < n; k++) {
            known += born[k] != 0;
            under += !born[k] || (int32_t)born[k] >= year - (int32_t)B.rule[B.team[i].rules].u23;
        }
        printf("  %04x -> B team %04x, rules %d: %d players, %d with a year, %d under %d in %d\n",
               (unsigned)B.team[i].parent, (unsigned)bt, (int)B.team[i].rules, (int)n, (int)known, (int)under,
               (int)B.rule[B.team[i].rules].u23, (int)year);
    }
    return B.nTeams > 0 ? 0 : 1;
}

/* Test (SWOS_TEST_BTEAM_SHOWN=1, career_autopilot.c's B-team screenshots): the first called-up
 * player swapped into place 16 of the squad order (row 16, whose number is drawn), to see it yellow. */
void BTeam_TestShowCalled(void)
{
    extern uint8_t  _currentMatchPlayers[];
    extern uint16_t _careerNumPlayers;
    if (!getenv("SWOS_TEST_BTEAM_SHOWN"))
        return;
    for (uint16_t k = 16; k < _careerNumPlayers; k++) {
        const PlayerFile *p = (const PlayerFile *)((const uint8_t *)&_careerTeam + PF_OFF +
                                                  _currentMatchPlayers[k] * sizeof(PlayerFile));
        if (BTeam_CalledKind(p)) {
            const uint8_t t = _currentMatchPlayers[15];
            _currentMatchPlayers[15] = _currentMatchPlayers[k];
            _currentMatchPlayers[k] = t;
            fprintf(stderr, "[bteam] test: %.22s to place 16 (his number shown)\n", (const char *)p->playerName);
            return;
        }
    }
}

void BTeam_TestRequests(void)
{
    static int32_t done;
    const char *e = getenv("SWOS_TEST_BTEAM");
    if (done || !e || !*e)
        return;
    done = 1;
    /* kind:index,... (kind 1 next match, 2 season; index 0.. the B team's place - 1) */
    for (const char *s = e; *s;) {
        char *end;
        const long kind = strtol(s, &end, 10);
        if (*end != ':')
            break;
        const long index = strtol(end + 1, &end, 10);
        const int32_t code = BTeam_Add((int32_t)kind, (uint16_t)index);
        fprintf(stderr, "[bteam] test call-up %ld:%ld -> %d %s\n", kind, index, (int)code, BTeam_CodeText(code));
        s = *end == ',' ? end + 1 : end;
    }
}

#else   /* the Amiga, or no worlds */

uint16_t BTeam_Of(uint16_t parent) { (void)parent; return 0xFFFF; }
int32_t BTeam_Shown(void) { return 0; }
uint16_t BTeam_Born(uint16_t team, uint16_t index) { (void)team; (void)index; return 0; }
int32_t BTeam_Can(int32_t kind, uint16_t index) { (void)kind; (void)index; return BTEAM_NO_BTEAM; }
int32_t BTeam_Add(int32_t kind, uint16_t index) { (void)kind; (void)index; return BTEAM_NO_BTEAM; }
void BTeam_ReturnMatch(void) {}
void BTeam_ReturnAll(void) {}
int32_t BTeam_Browsing(void) { return 0; }
uint16_t BTeam_BrowseTeam(void) { return 0xFFFF; }
void BTeam_SquadMenuInit(void) {}   /* (the button is invisible in the menu's data) */
void BTeam_PickPlayer(uint16_t index) { (void)index; }
__attribute__((used, externally_visible))
void BTeamSquadButton(void) {}
int32_t BTeam_SelfTest(const char *arg) { (void)arg; return 1; }
void BTeam_TestRequests(void) {}
void BTeam_TestShowCalled(void) {}

#endif
