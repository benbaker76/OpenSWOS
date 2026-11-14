/*
 * long_bench.c -- benches of up to 15 substitutes in a world's matches (include/long_bench.h).
 *
 * Ported from the SWOS 96/97 Mod's tools/benchx.py (4.0.5 / 4.1) (github.com/aminta/swos-9697-mod).
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
 * What the Mod patches into the DOS exe, done here in the game's own routines' C:
 *   bx_max / bx_min / bx_look / bx_frset -> LongBench_MatchStart (the bench and the changes)
 *   bx_next     -> InitInGameTeamStructure's loop to 11 + the bench (LongBench_InitPlace)
 *   bx_xnamed   -> a club's places 16.. = its players 17.. still there (squad_extras.c)
 *   bx_table    -> ingameTeamPlayerOffsets for 26 places (swos_amiga_data_imports.c)
 *   bx_cmp_last -> the bench cursor's last place (LongBench_Last, bench_subs.c)
 *   bx_win_end  -> the dugout's window of five (bench_subs.c, the Mod's 4.0.5 scroll)
 *   bx_make / bx_name1 / bx_name2 -> the names of places 17.. (LongBench_MakeNameSprites, _NameSprite)
 *   bx_wcount   -> the after-match write-backs over every place (game_init.c, LongBench_Record)
 *   the game's `cmp [gameMaxSubstitutes], 5` branches -> never taken (bench_subs.c: LB_GK_FIRST)
 */
#include "long_bench.h"
#include "squad_extras.h"
#include "custom_team.h"
#include "world.h"
#include "endian_helpers.h"

#include <string.h>
#ifdef SWOS_SDL_BUILD
#include <stdio.h>
#include <stdlib.h>
#endif

extern TeamGame   _topTeamInGame, _bottomTeamInGame;
extern TeamFileEx _careerTeam;
extern uint16_t   _careerNumPlayers, _selTeamNumber, _gameType, _trainingGame, _isNationalSelector;
extern uint16_t   _cGetPlayerAtIndexFileIdx, _gameMaxSubstitutes;
extern uint8_t    _gameName[];
extern DIYCompetitionEx _diyCompetitionStart;
extern uint8_t   *GetPlayer(uint16_t teamNumber, uint16_t playerFileIndex);
extern uint8_t   *GetPlayerAtIndex(uint16_t teamNumber, uint16_t ordinal);
extern void       InitInGamePlayer(uint16_t playerIndex, uint16_t playerSlot, uint8_t *playerBase, PlayerInfo *pi);
extern void       SetPlayerNameAndFace(uint8_t *playerBase, PlayerInfo *pi);

#define GAME_TYPE_NO_GAME  0
#define GAME_TYPE_DIY      1
#define GAME_TYPE_CAREER   4
#define PI_BYTES           ((uint32_t)sizeof(PlayerInfo))      /* 61 */
#define PF_OFF             ((uint32_t)sizeof(TeamFile))        /* 76 */

typedef char _lb_pi_check[(sizeof(PlayerInfo) == 61) ? 1 : -1];
typedef char _lb_rp_check[(sizeof(PlayerInfo) * 2 * LB_EXTRA_PLACES + 2 == LB_REPLAY_BYTES) ? 1 : -1];

static uint16_t s_places = LB_GAME_PLACES;
static uint16_t s_frActive[2] = { 0xFFFF, 0xFFFF };    /* (a friendly club's own order: below) */
static int32_t  s_quiet;
static uint8_t  s_named, s_et;
static uint8_t  s_kickoff[2][LB_EXTRA_PLACES * 61];

uint8_t g_lbOrdinals[2][LB_EXTRA_PLACES];
uint8_t g_lbNameLenCopy[2][LB_EXTRA_PLACES];
static uint8_t s_nameLen[2][LB_EXTRA_PLACES];

uint16_t LongBench_Places(void) { return s_places; }
int32_t  LongBench_On(void) { return s_places > LB_GAME_PLACES; }

uint16_t LongBench_Last(void)
{
    return (uint16_t)(s_places > LB_GAME_PLACES && !_trainingGame ? s_places - 11 : 5);
}

uint16_t LongBench_SubsAllowed(void)
{
    extern uint16_t _maxSubsPerTeam;
    extern uint16_t _extraTimeState;
    /* (the competition's extra-time substitute: once extra time is on, -1 from StartFirstExtraTime;
     *  s_et is the match's, decided at the kick-off on both games alike) */
    return (uint16_t)(_maxSubsPerTeam + (s_et && (int16_t)_extraTimeState < 0 ? 1 : 0));
}

PlayerInfo g_lbExtra[2][LB_EXTRA_PLACES];

PlayerInfo *LongBench_Player(TeamGame *tg, uint16_t place)
{
    if (place < LB_GAME_PLACES)
        return &tg->players[place];
    /* (places 16.. of the two in-match teams; any other TeamGame has the game's 16) */
    return &g_lbExtra[tg == &_bottomTeamInGame][place - LB_GAME_PLACES];
}

static int32_t side_of(const TeamGame *tg)
{
    return tg == &_bottomTeamInGame;
}

/* ---- the bench of a match ---- */

#if defined(SWOS_SDL_BUILD) && SWOS_WORLDS

static int32_t club_bench(const TeamFile *t, uint8_t *et);

static int32_t world_benches(void)
{
    return World_Current() == WORLD_2627;
}

/* A club's (country, division) bench: its division's, else its country's own; 0: none -- and none
 * for a national team, the game's custom teams or a team made from a club (MY CUSTOM TEAM): they
 * keep the game's bench, as the Mod's. */
static int32_t division_bench(const TeamFile *t, uint8_t *et)
{
    if (t->countryNumber == CN_CUSTOM || (t->countryNumber >= CN_EUROPE && t->countryNumber <= CN_OCEANIA))
        return 0;
    if (CustomTeam_IsEdited((const TeamFileEx *)t))
        return 0;
    return club_bench(t, et);
}

static int32_t club_bench(const TeamFile *t, uint8_t *et)
{
    const uint8_t country = World_FileCountry(t->countryNumber);
    const LongBenchDiv *own = NULL;
    for (int32_t i = 0; i < g_worldBenchDivCount; i++) {
        const LongBenchDiv *d = &g_worldBenchDivs[i];
        if (d->country != country)
            continue;
        if (d->division == t->league) {
            *et = d->et;
            return d->named;
        }
        if (d->division == 0xFF)
            own = d;
    }
    if (!own)
        return 0;
    *et = own->et;
    return own->named;
}

/* A country's own bench (its top league's, else its confederation's); 0: none. */
static int32_t country_bench(uint8_t country, uint8_t *et)
{
    for (int32_t i = 0; i < g_worldBenchDivCount; i++)
        if (g_worldBenchDivs[i].country == country && g_worldBenchDivs[i].division == 0xFF) {
            *et = g_worldBenchDivs[i].et;
            return g_worldBenchDivs[i].named;
        }
    return 0;
}

/* A contest of the country (or a continental one) by its name; 0: none. */
static int32_t title_bench(uint8_t country, const char *name, uint8_t *et)
{
    if (!name || !name[0])
        return 0;
    for (int32_t i = 0; i < g_worldBenchTitleCount; i++) {
        const LongBenchTitle *e = &g_worldBenchTitles[i];
        if ((e->country == 0 || e->country == country) && strcmp(e->name, name) == 0) {
            *et = e->et;
            return e->named;
        }
    }
    return 0;
}

static int32_t league_match;            /* LongBench_LeagueMatch: the next kick-off is a league's */

/* The game's bench for this match, or 0 (the game's rule). */
static int32_t match_bench(const TeamFile *t1, const TeamFile *t2, uint8_t *et)
{
    *et = 0;
    if (!world_benches() || _trainingGame || _isNationalSelector)
        return 0;
    if (_gameType == GAME_TYPE_CAREER) {
        /* the career club's country: its contests by name, its league by division, else its own */
        const TeamFile *club = (const TeamFile *)&_careerTeam;
        if ((const void *)t2 == (const void *)&_careerTeam)
            club = t2;
        else if ((const void *)t1 == (const void *)&_careerTeam)
            club = t1;
        const uint8_t country = World_FileCountry(club->countryNumber);
        /* (the contest's title: the competition slot's name, the Mod's DIY_competitionStart + 4 --
         *  already there for the pre-match screen, as at the kick-off) */
        int32_t n = title_bench(country, (const char *)_diyCompetitionStart.base.competitionName, et);
        if (!n)
            n = league_match ? club_bench(club, et) : country_bench(country, et);
        return n;
    }
    if (_gameType == GAME_TYPE_NO_GAME || _gameType == GAME_TYPE_DIY) {
        /* a friendly / DIY match: the longer of the two clubs' benches (a national or custom team
         * has none: the other's) */
        uint8_t e1 = 0, e2 = 0;
        const int32_t a = division_bench(t1, &e1), b = division_bench(t2, &e2);
        *et = a >= b ? e1 : e2;
        return a >= b ? a : b;
    }
    return 0;
}

#else
static int32_t league_match;
static int32_t match_bench(const TeamFile *t1, const TeamFile *t2, uint8_t *et)
{
    (void)t1; (void)t2;
    *et = 0;
    return 0;
}
#endif

uint16_t LongBench_PreMatch(const TeamFile *team1, const TeamFile *team2)
{
    uint8_t et = 0;
    const int32_t n = match_bench(team1, team2, &et);
    LongBench_FrActivate(team1, team2, n > 5);
    return (uint16_t)(n > 5 ? (n > LB_MAX_SUBS ? LB_MAX_SUBS : n) : 0);
}

void LongBench_LeagueMatch(int32_t on)
{
    league_match = on;
}

void LongBench_Prepare(const TeamFile *team1, const TeamFile *team2)
{
    int16_t mn = 0, mx = 0;
    const int32_t quiet = s_quiet;
    s_quiet = 1;
    LongBench_MatchStart(team1, team2, &mn, &mx);
    s_quiet = quiet;
}

void LongBench_MatchEnd(void)
{
    s_places = LB_GAME_PLACES;      /* (between matches the state is the game's: a lobby's too) */
    s_named = s_et = 0;
    s_frActive[0] = s_frActive[1] = 0xFFFF;
}

void LongBench_MatchStart(const TeamFile *team1, const TeamFile *team2, int16_t *minSubs, int16_t *maxSubs)
{
    uint8_t et = 0;
    int32_t named = match_bench(team1, team2, &et);
    s_places = LB_GAME_PLACES;
    s_named = s_et = 0;
    LongBench_FrActivate(team1, team2, named > 5);
#ifdef SWOS_SDL_BUILD
    {   /* (test: SWOS_TEST_FRSWAP=a:b -- the top club's places a and b swapped, as the pre-match
         *  screen would, before the in-match teams are built) */
        static int32_t done;
        const char *e = getenv("SWOS_TEST_FRSWAP");
        int a = -1, b = -1;
        if (e && !done && sscanf(e, "%d:%d", &a, &b) == 2 && team1) {
            const uint16_t t = PACK_BE_U16(team1->countryNumber, team1->teamOrdinal);
            done = 1;
            fprintf(stderr, "[bench] test: places %d and %d of %.16s swapped: %s\n", a + 1, b + 1,
                    (const char *)team1->teamName, LongBench_FrSwap(t, (int16_t)a, (int16_t)b) ? "done" : "NO ORDER");
        }
    }
#endif
    if (named > LB_MAX_SUBS)
        named = LB_MAX_SUBS;
    if (named > 5) {
        s_named = (uint8_t)named;
        s_et = et;
        s_places = (uint16_t)(11 + named);
        *maxSubs = (int16_t)named;
        *minSubs = LB_USED;
    } else if (named > 0) {
        /* (a real bench of 5 or fewer: that many, the changes as the Mod's rule) */
        *maxSubs = (int16_t)named;
        *minSubs = (int16_t)(named < LB_USED ? named : LB_USED);
    }
#ifdef SWOS_SDL_BUILD
    if (named > 0 && !s_quiet)
        fprintf(stderr, "[bench] %s: %d named substitutes, %d changes%s (%u places)\n", (const char *)_gameName,
                (int)*maxSubs, (int)*minSubs, s_et ? ", one more in extra time" : "", (unsigned)s_places);
#endif
}

/* ---- the in-match teams' places 16.. ---- */

static void empty_place(PlayerInfo *pi, uint16_t place)
{
    memset(pi, 0, PI_BYTES);
    pi->playerIndex = (int8_t)place;
    pi->position = (int8_t)0xFF;        /* (as a substituted player: every bench routine skips him) */
    pi->skip = 1;
}

void LongBench_InitPlace(TeamFileEx *team, TeamGame *tg, uint16_t place)
{
    PlayerInfo *pi = LongBench_Player(tg, place);
    const uint16_t teamNum = PACK_BE_U16(team->team.countryNumber, team->team.teamOrdinal);
    uint8_t *base = NULL;
    uint16_t index = place;

    if (team == &_careerTeam) {
        /* the managed club: its squad order's place (careerTeam's 30) */
        if (place < _careerNumPlayers) {
            base = GetPlayerAtIndex(teamNum, place);
            index = _cGetPlayerAtIndexFileIdx;
        }
    } else {
        /* any other club: its players 17.. still there, in order */
        uint16_t at = 0, n = 0;
        const uint8_t *order = LongBench_FrOrder(teamNum, &n);
        PlayerFile *p = NULL;
        if (order) {                    /* (a friendly club's own order: the pre-match screen's) */
            if (place < n) {
                at = order[place];
                if (at < LB_GAME_PLACES) {
                    base = GetPlayer(teamNum, at);
                    index = at;
                } else
                    p = SquadExtras_BenchRecord(teamNum, at);
            }
        } else
            p = SquadExtras_BenchPlayer(teamNum, (int32_t)(place - LB_GAME_PLACES), &at);
        if (p) {
            base = (uint8_t *)p - PF_OFF;
            index = at;
        }
    }
    if (!base) {
        empty_place(pi, place);
        return;
    }
    InitInGamePlayer(index, place, base, pi);
    SetPlayerNameAndFace(base, pi);
    pi->passing = pi->shooting = pi->heading = pi->tackling = 0;
    pi->ballControl = pi->speed = pi->finishing = pi->goalieSkill = 0;
    /* (the Mod 4.0.6: with a long bench an injured or suspended substitute is left out -- an
     *  empty seat; InitInGamePlayer marked him substituted already) */
    if (pi->skip)
        empty_place(pi, place);
}

uint8_t *LongBench_Record(const TeamFileEx *team, int8_t playerIndex)
{
    const uint16_t teamNum = PACK_BE_U16(team->team.countryNumber, team->team.teamOrdinal);
    if (playerIndex < 0)
        return NULL;
    if (playerIndex < LB_GAME_PLACES || team == &_careerTeam)
        return GetPlayer(teamNum, (uint16_t)playerIndex);
    {
        PlayerFile *p = SquadExtras_BenchRecord(teamNum, (uint16_t)playerIndex);
        return p ? (uint8_t *)p - PF_OFF : NULL;
    }
}

void LongBench_SaveKickOff(void)
{
    memcpy(s_kickoff, g_lbExtra, sizeof s_kickoff);
}

void LongBench_RestoreKickOff(void)
{
    memcpy(g_lbExtra, s_kickoff, sizeof s_kickoff);
}

/* ---- a friendly club's own order of its places (the Mod's fr_find, fr_ga, fr_sw_pm) ----
 * In a World 2026-27 friendly or DIY match with a long bench (offline: online the lobby has no
 * pre-match screen), a club's places are 16 + its players 17..: 0..15 its record's order
 * (playerNumbers), then its players 17.. in their data order. The pre-match screen lists them all
 * (row 17 and the box rotate the places from 17 on), swaps any two, and the kick-off builds the
 * in-match team in that order. The record keeps its 16 in their new relative order (no index past
 * 15 in it: a player 17.. among the starters moves the record's next ones up). Four clubs' orders
 * kept; one whose record was reordered elsewhere starts again from it. */
#define FR_CLUBS 4
typedef struct {
    uint16_t team;
    uint8_t  valid, n;                  /* n: places (16 + its players 17..) */
    uint8_t  order[16 + SQX_MAX];       /* place -> file index (0..15 its record's, 16.. its 17..) */
    uint8_t  rec[16];                   /* the record's playerNumbers as last written */
} FrOrder;
static FrOrder  s_fr[FR_CLUBS];
static uint8_t  s_frNext;

static TeamFileEx *fr_team(uint16_t team)
{
    extern TeamFileEx *GetTeamPointer(uint16_t teamId);
    return GetTeamPointer(team);
}

static FrOrder *fr_find(uint16_t team)
{
    if (team == 0xFFFF || (team != s_frActive[0] && team != s_frActive[1]))
        return NULL;
    TeamFileEx *t = fr_team(team);
    if (!t)
        return NULL;
    FrOrder *f = NULL;
    for (int32_t i = 0; i < FR_CLUBS && !f; i++)
        if (s_fr[i].valid && s_fr[i].team == team)
            f = &s_fr[i];
    if (f && memcmp(f->rec, t->team.playerNumbers, 16) != 0)
        f->valid = 0;                   /* (reordered elsewhere: from its record again) */
    if (!f || !f->valid) {
        if (!f) {
            f = &s_fr[s_frNext];
            s_frNext = (uint8_t)((s_frNext + 1) % FR_CLUBS);
        }
        memset(f, 0, sizeof *f);
        f->team = team;
        memcpy(f->order, t->team.playerNumbers, 16);
        memcpy(f->rec, t->team.playerNumbers, 16);
        f->n = 16;
        uint16_t at;
        for (int32_t k = 0; f->n < 16 + SQX_MAX && SquadExtras_BenchPlayer(team, k, &at); k++)
            f->order[f->n++] = (uint8_t)at;
        f->valid = 1;
    }
    return f;
}

static int32_t fr_eligible(const TeamFile *t)
{
#if defined(SWOS_SDL_BUILD) && SWOS_WORLDS
    extern int32_t NetplayMatch_Enabled(void);
    if (!t || World_Current() != WORLD_2627 || NetplayMatch_Enabled() ||
        (_gameType != GAME_TYPE_NO_GAME && _gameType != GAME_TYPE_DIY) || _trainingGame)
        return 0;
    if (t->countryNumber == CN_CUSTOM || (t->countryNumber >= CN_EUROPE && t->countryNumber <= CN_OCEANIA) ||
        (const void *)t == (const void *)&_careerTeam || CustomTeam_IsEdited((const TeamFileEx *)t))
        return 0;
    uint16_t at;
    return SquadExtras_BenchPlayer(PACK_BE_U16(t->countryNumber, t->teamOrdinal), 0, &at) != NULL;
#else
    (void)t;
    return 0;
#endif
}

void LongBench_FrActivate(const TeamFile *team1, const TeamFile *team2, int32_t on)
{
    const TeamFile *t[2] = { team1, team2 };
    for (int32_t i = 0; i < 2; i++)
        s_frActive[i] = on && fr_eligible(t[i]) ? PACK_BE_U16(t[i]->countryNumber, t[i]->teamOrdinal) : 0xFFFF;
#ifdef SWOS_SDL_BUILD
    if (getenv("SWOS_TEST_FRIENDLY"))
        fprintf(stderr, "[bench] test: own orders %04x %04x (long bench %d)\n", (unsigned)s_frActive[0],
                (unsigned)s_frActive[1], (int)on);
#endif
}

const uint8_t *LongBench_FrOrder(uint16_t team, uint16_t *count)
{
    const FrOrder *f = fr_find(team);
    if (!f)
        return NULL;
    *count = f->n;
    return f->order;
}

static void fr_write_back(FrOrder *f)
{
    TeamFileEx *t = fr_team(f->team);
    int32_t k = 0;
    for (int32_t i = 0; i < f->n && k < 16; i++)
        if (f->order[i] < 16)
            t->team.playerNumbers[k++] = f->order[i];
    memcpy(f->rec, t->team.playerNumbers, 16);
}

int32_t LongBench_FrSwap(uint16_t team, int16_t a, int16_t b)
{
    FrOrder *f = fr_find(team);
    if (!f || a < 0 || b < 0 || a >= f->n || b >= f->n)
        return 0;
    const uint8_t t = f->order[a];
    f->order[a] = f->order[b];
    f->order[b] = t;
    fr_write_back(f);
    return 1;
}

int32_t LongBench_FrRotate(uint16_t team, uint16_t from)
{
    FrOrder *f = fr_find(team);
    if (!f || from + 1 >= f->n)
        return f != NULL;
    const uint8_t first = f->order[from];
    memmove(&f->order[from], &f->order[from + 1], (size_t)(f->n - from - 1));
    f->order[f->n - 1] = first;
    fr_write_back(f);
    return 1;
}

/* ---- the two-row dugouts (the Mod's dugout.py, 4.0.6 / 4.1) ----
 * The dugouts are pitch tiles in columns 0-1: the top one rows 23-27 (the roof cap, two middle rows,
 * the seat end, the steps), the bottom one 29-33, the halfway line between them. With a long bench
 * the top one grows a row upwards (cap 22) and the bottom one a row downwards (steps 34), and both a
 * column wider (column 2: column 1's last pixel column repeated, the floor going on). Every tile comes
 * from the original dugouts in the same grass-stripe phase; one missing in a phase is made from one
 * that exists, its grass pixels swapped for that phase's grass. Done on the loaded tile map (any of
 * the game's pitch sets), so the files stay as they are; a pitch whose dugouts are not where the
 * game's are is left alone. Drawing only: the match never reads a tile. */
int32_t LongBench_TwoRows(void)
{
    return LongBench_On() && !_trainingGame;
}

#ifdef SWOS_SDL_BUILD
#define DG_COLS      42
#define DG_ADD       1
#define DG_TILES     96                 /* new tiles at most (the Mod's PITCH1-5 make 10-20) */
static const int8_t k_top[5] = { 23, 24, 25, 26, 27 }, k_bot[5] = { 29, 30, 31, 32, 33 };
static uint8_t  s_dgPool[DG_TILES][256];
static int32_t  s_dgUsed;

enum { DG_CAP, DG_MID, DG_END, DG_STEPS, DG_NONE };

static int32_t dg_kind_new(int32_t r)   /* the long dugouts' row r */
{
    if (r == 22 || r == 29) return DG_CAP;
    if ((r >= 23 && r <= 25) || (r >= 30 && r <= 32)) return DG_MID;
    if (r == 26 || r == 33) return DG_END;
    if (r == 27 || r == 34) return DG_STEPS;
    return DG_NONE;
}

static int32_t dg_kind_old(int32_t r)   /* the game's dugouts' row r */
{
    static const int8_t k[5] = { DG_CAP, DG_MID, DG_MID, DG_END, DG_STEPS };
    for (int32_t i = 0; i < 5; i++)
        if (r == k_top[i] || r == k_bot[i])
            return k[i];
    return DG_NONE;
}

static const uint8_t *grass_tile(uint8_t *const *o, const int32_t *grass, int32_t ng, int32_t per, int32_t r,
                                 int32_t c)
{
    for (int32_t i = 0; i < ng; i++)
        if (((grass[i] - r) % per + per) % per == 0 && dg_kind_new(grass[i]) == DG_NONE)
            return o[grass[i] * DG_COLS + c];
    return o[grass[0] * DG_COLS + c];
}

static uint8_t *dg_add(const uint8_t *px)
{
    if (s_dgUsed >= DG_TILES)
        return NULL;
    memcpy(s_dgPool[s_dgUsed], px, 256);
    return s_dgPool[s_dgUsed++];
}

void LongBench_PitchDugouts(uint8_t **tiles)
{
    uint8_t *o[55 * DG_COLS];           /* the original map */
    uint8_t *have[DG_NONE][2][8];       /* (kind, column, stripe phase) -> tile */
    int32_t grass[55], ng = 0, per = 0;
    if (!LongBench_TwoRows() || !tiles)
        return;
    memcpy(o, tiles, sizeof o);
    s_dgUsed = 0;
    memset(have, 0, sizeof have);
    for (int32_t r = 10; r < 47; r++)
        if (dg_kind_old(r) == DG_NONE)
            grass[ng++] = r;
#define T(r, c) o[(r) * DG_COLS + (c)]
    for (int32_t p = 1; p <= 8 && !per; p++) {
        int32_t ok = 1;
        for (int32_t i = 0; i < ng && ok; i++)
            for (int32_t j = 0; j < ng && ok; j++)
                if ((grass[j] - grass[i]) % p == 0)
                    for (int32_t c = 0; c < 2 && ok; c++)
                        ok = T(grass[i], c) == T(grass[j], c);
        if (ok)
            per = p;
    }
    if (!per) {
        fprintf(stderr, "[bench] the pitch's grass has no stripe period: the game's dugouts kept\n");
        return;
    }
    for (int32_t i = 0; i < 10; i++) {
        const int32_t r = i < 5 ? k_top[i] : k_bot[i - 5];
        for (int32_t c = 0; c < 2; c++)
            if (!have[dg_kind_old(r)][c][r % per])
                have[dg_kind_old(r)][c][r % per] = T(r, c);
    }
    /* the grass tile of row r's stripe phase, column c (a row that stays grass) */
    #define G(r, c) grass_tile(o, grass, ng, per, (r), (c))
    {
        /* (the game's dugouts must be where the Mod's are: ink from row 23 to row 33's end) */
        if (T(23, 0) == G(23, 0) && T(23, 1) == G(23, 1)) {
            fprintf(stderr, "[bench] the pitch's dugouts are not where the game's are: kept\n");
            return;
        }
        for (int32_t c = 0; c < 3; c++)
            if (T(22, c) != G(22, c) || T(34, c) != G(34, c)) {
                fprintf(stderr, "[bench] the rows the dugouts grow into are not grass: the game's dugouts kept\n");
                return;
            }
    }
    for (int32_t r = 22; r <= 34; r++) {
        const int32_t k = dg_kind_new(r);
        if (k == DG_NONE)
            continue;
        for (int32_t c = 0; c < 2; c++) {
            uint8_t *t = have[k][c][r % per];
            if (!t) {                   /* a phase with no such tile: from one that has it, grass swapped */
                int32_t ph = -1;
                for (int32_t p = 0; p < per && ph < 0; p++)
                    if (have[k][c][p])
                        ph = p;
                if (ph < 0)
                    return;
                const uint8_t *src = have[k][c][ph], *g0 = G(ph, c), *g1 = G(r, c);
                uint8_t px[256];
                for (int32_t i = 0; i < 256; i++)
                    px[i] = src[i] == g0[i] ? g1[i] : src[i];
                t = have[k][c][r % per] = dg_add(px);
                if (!t)
                    return;
            }
            tiles[r * DG_COLS + c] = t;
        }
    }
    for (int32_t r = 22; r <= 34; r++) {   /* the 3rd column: the floor goes on */
        if (dg_kind_new(r) == DG_NONE)
            continue;
        const uint8_t *t1 = tiles[r * DG_COLS + 1], *g1 = G(r, 1), *g2 = G(r, 2);
        uint8_t px[256];
        for (int32_t y = 0; y < 16; y++)
            for (int32_t x = 0; x < 16; x++)
                px[y * 16 + x] = t1[y * 16 + 15] == g1[y * 16 + 15] ? g2[y * 16 + x] : t1[y * 16 + 15];
        uint8_t *t = dg_add(px);
        if (t)
            tiles[r * DG_COLS + 2] = t;
    }
#undef T
#undef G
    fprintf(stderr, "[bench] two-row dugouts: stripe period %d, %d new tiles\n", (int)per, (int)s_dgUsed);
}
#else
void LongBench_PitchDugouts(uint8_t **tiles) { (void)tiles; }
#endif

/* The game's selection arrow mirrored (the front row's, pointing left): a sprite of its own past the
 * names' (SDL), made once from the arrow; the game's own index elsewhere. */
int32_t LongBench_MirroredArrow(int32_t arrow)
{
#ifdef SWOS_SDL_BUILD
    extern uint8_t *_spriteGraphicsPtr;
    static SpriteGraphics hdr;
    static uint8_t *pix;
    static const SpriteGraphics *made;
    SpriteGraphics **table = (SpriteGraphics **)(uintptr_t)_spriteGraphicsPtr;
    const SpriteGraphics *a = table ? table[arrow] : NULL;
    if (!a || !a->sprData || a->maskPtr)
        return arrow;
    if (made != a) {
        const uint32_t stride = (uint32_t)a->wquads * 16u, bytes = stride * (uint32_t)a->nlines;
        free(pix);
        pix = (uint8_t *)calloc(1, bytes);
        if (!pix)
            return arrow;
        hdr = *a;
        for (int32_t y = 0; y < a->nlines; y++)
            for (int32_t x = 0; x < a->pixWidth; x++)
                pix[y * stride + (a->pixWidth - 1 - x)] = a->sprData[y * stride + x];
        hdr.sprData = pix;
        hdr.centerX = (int16_t)(a->pixWidth - 1 - a->centerX);
        hdr.ordinal = (int16_t)LB_ARROW_SPRITE;
        made = a;
    }
    table[LB_ARROW_SPRITE] = &hdr;
    return LB_ARROW_SPRITE;
#else
    return arrow;
#endif
}

/* ---- the names of places 17.. ---- */

#ifdef SWOS_SDL_BUILD
extern uint8_t *_spriteGraphicsPtr;
extern uint16_t MakePlayerNameSpriteAt(uint16_t playerNumber, uint8_t *shortName, uint16_t spriteIdx,
                                       uint8_t *nameLenOut);
#define SPR_TEAM1_PLAYER_NAMES_START 0x8D

static SpriteGraphics s_nameHdr[LB_NAME_SPRITES];
static uint8_t       *s_namePix[LB_NAME_SPRITES];
static uint32_t       s_namePixBytes;

/* The extra name sprites, shaped as the game's first name sprite (its buffer, its own pixels). */
static int32_t name_sprites_ready(void)
{
    SpriteGraphics **table = (SpriteGraphics **)(uintptr_t)_spriteGraphicsPtr;
    const SpriteGraphics *model = table ? table[SPR_TEAM1_PLAYER_NAMES_START] : NULL;
    if (!model || !model->sprData)
        return 0;
    const uint32_t bytes = (uint32_t)model->wquads * 16u * (uint32_t)model->nlines;
    for (int32_t k = 0; k < LB_NAME_SPRITES; k++) {
        if (!s_namePix[k] || s_namePixBytes < bytes) {
            free(s_namePix[k]);
            s_namePix[k] = (uint8_t *)calloc(1, bytes);
            if (!s_namePix[k])
                return 0;
        }
        s_nameHdr[k] = *model;
        s_nameHdr[k].sprData = s_namePix[k];
        s_nameHdr[k].maskPtr = NULL;
        s_nameHdr[k].ordinal = (int16_t)(LB_NAME_SPRITE_BASE + k);
        table[LB_NAME_SPRITE_BASE + k] = &s_nameHdr[k];
    }
    s_namePixBytes = bytes;
    return 1;
}
#endif

void LongBench_MakeNameSprites(TeamGame *top, TeamGame *bottom)
{
    memset(s_nameLen, 0, sizeof s_nameLen);
#ifdef SWOS_SDL_BUILD
    if (!LongBench_On() || !name_sprites_ready())
        return;
    TeamGame *tg[2] = { top, bottom };
    for (int32_t t = 0; t < 2; t++)
        for (int32_t k = 0; k < LB_EXTRA_PLACES; k++) {
            PlayerInfo *pi = LongBench_Player(tg[t], (uint16_t)(LB_GAME_PLACES + k));
            MakePlayerNameSpriteAt((uint16_t)(uint8_t)pi->shirtNumber, (uint8_t *)pi->shortName,
                                   (uint16_t)(LB_NAME_SPRITE_BASE + t * LB_EXTRA_PLACES + k), &s_nameLen[t][k]);
        }
#else
    (void)top; (void)bottom;
#endif
}

void LongBench_InitNames(void)
{
    for (int32_t t = 0; t < 2; t++)
        for (int32_t k = 0; k < LB_EXTRA_PLACES; k++) {
            g_lbOrdinals[t][k] = (uint8_t)(LB_GAME_PLACES + k);
            g_lbNameLenCopy[t][k] = s_nameLen[t][k];
        }
}

/* The game's tables (16 a team: _plOrdinalsTable, _plNamesLengthTableCopy) for places 0..15,
 * ours for 16..25. */
extern uint8_t _plOrdinalsTable[], _plNamesLengthTableCopy[];

static uint8_t *ord_at(int32_t team, int16_t place)
{
    return place < LB_GAME_PLACES ? &_plOrdinalsTable[team * 16 + place] : &g_lbOrdinals[team][place - LB_GAME_PLACES];
}

static uint8_t *len_at(int32_t team, int16_t place)
{
    return place < LB_GAME_PLACES ? &_plNamesLengthTableCopy[team * 16 + place]
                                  : &g_lbNameLenCopy[team][place - LB_GAME_PLACES];
}

void LongBench_SwapNames(int32_t team, int16_t enterPlace, int16_t leavePlace)
{
    uint8_t *a = ord_at(team, enterPlace), *b = ord_at(team, leavePlace), t;
    t = *a; *a = *b; *b = t;
    a = len_at(team, enterPlace);
    b = len_at(team, leavePlace);
    t = *a; *a = *b; *b = t;
}

static int32_t test_on(void);

int32_t LongBench_NameSprite(int32_t team, uint8_t ordinal, int16_t *width)
{
    (void)width;
#ifdef SWOS_SDL_BUILD
    {   /* (test: SWOS_TEST_LONGBENCH -- the first time a substitute from place 17 on shows his name) */
        static int32_t shown;
        if (!shown && test_on()) {
            extern void Platform_RequestScreenshot(void);
            shown = 1;
            Platform_RequestScreenshot();
            fprintf(stderr, "[bench] test: the name of place %u's man shown (sprite %d)\n",
                    (unsigned)ordinal + 1, (int)(LB_NAME_SPRITE_BASE + team * LB_EXTRA_PLACES + (ordinal - LB_GAME_PLACES)));
        }
    }
#endif
    return LB_NAME_SPRITE_BASE + team * LB_EXTRA_PLACES + (ordinal - LB_GAME_PLACES);
}

/* ---- a replay: the places 17.. at the kick-off, for their names (docs/REPLAY_FORMAT_V2.md) ---- */

uint32_t LongBench_ReplayWrite(uint8_t *out)
{
    if (!LongBench_On())
        return 0;
    BE_WRITE_U16(out, s_places);        /* (taken at the kick-off: the places as they kicked off) */
    memcpy(out + 2, g_lbExtra, sizeof g_lbExtra);
    return LB_REPLAY_BYTES;
}

void LongBench_ReplayRead(const uint8_t *in, uint32_t len)
{
    s_places = LB_GAME_PLACES;
    if (!in || len < LB_REPLAY_BYTES)
        return;
    const uint16_t places = BE_READ_U16(in);
    if (places <= LB_GAME_PLACES || places > LB_PLACES_MAX)
        return;
    s_places = places;
    memcpy(g_lbExtra, in + 2, sizeof g_lbExtra);
}

/* ---- SWOS_TEST_LONGBENCH=1: the bench of a long-bench match driven and photographed ----
 * At the first stoppage the top team's bench is called; then, menu frame by frame: the cursor
 * down ten places (the dugout's window scrolls), a screenshot after five and after ten; FIRE on
 * the substitute, a screenshot of the substitutes menu, FIRE again: he comes on. The log says who. */
static int32_t s_test = -1, s_testCalled, s_testFrame;

static int32_t test_on(void)
{
#ifdef SWOS_SDL_BUILD
    if (s_test < 0)
        s_test = getenv("SWOS_TEST_LONGBENCH") != NULL;
#else
    s_test = 0;
#endif
    return s_test && LongBench_On();
}

int32_t LongBench_TestCallBench(void)
{
    if (!test_on() || s_testCalled)
        return 0;
    s_testCalled = 1;
#ifdef SWOS_SDL_BUILD
    fprintf(stderr, "[bench] test: the top team's bench called\n");
#endif
    return 1;
}

void LongBench_TestInput(uint16_t *dir, uint8_t *fire)
{
    if (!test_on() || !s_testCalled || s_testFrame < 0)
        return;
    const int32_t k = s_testFrame++;
    *dir = 0xFFFF;
    *fire = 0;
    if (k >= 80 && k < 160 && (k - 80) % 8 == 0)
        *dir = 4;
    if (k == 180 || k == 240)
        *fire = 1;
#ifdef SWOS_SDL_BUILD
    {
        extern void Platform_RequestScreenshot(void);
        extern uint16_t _benchPlayerIndex, _plToEnterGameIndex, _plToBeSubstitutedPos;
        if (k == 79 || k == 122 || k == 170 || k == 230) {
            Platform_RequestScreenshot();
            fprintf(stderr, "[bench] test: screenshot at menu frame %d, cursor on place %u\n", (int)k,
                    (unsigned)_benchPlayerIndex);
        }
        if (k == 240) {
            const TeamGame *tg = &_topTeamInGame;
            fprintf(stderr, "[bench] test: place %u comes on for place %u\n", (unsigned)_plToEnterGameIndex + 1,
                    (unsigned)_plToBeSubstitutedPos + 1);
            (void)tg;
        }
    }
#endif
    if (k > 260)
        s_testFrame = -1;           /* (done: the controls are the game's again) */
}

/* ---- --long-bench-selftest ---- */

int32_t LongBench_SelfTest(void)
{
#if defined(SWOS_SDL_BUILD) && SWOS_WORLDS
    static const struct { const char *name; uint8_t country; int32_t want; } k[] = {
        { "UEFA CHAMPIONS LEAGUE", 20, 12 }, { "QUALIF. CHAMPIONS LEAGUE", 8, 12 },
        { "FA CUP", 8, 9 }, { "COPPA INTERCONTINENTALE", 0, 14 }, { "NO SUCH CUP", 8, 0 },
    };
    int32_t bad = 0;
    printf("[bench] %d division entries, %d titles\n", (int)g_worldBenchDivCount, (int)g_worldBenchTitleCount);
    for (uint32_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        uint8_t et = 0;
        const int32_t n = title_bench(k[i].country, k[i].name, &et);
        printf("[bench] %-28s country %3u: %2d named%s %s\n", k[i].name, (unsigned)k[i].country, (int)n,
               et ? " (+1 in extra time)" : "", n == k[i].want ? "ok" : "WRONG");
        bad += n != k[i].want;
    }
    {
        TeamFile t;
        memset(&t, 0, sizeof t);
        for (uint8_t d = 0; d < 6; d++) {
            uint8_t et = 0;
            t.countryNumber = 20;           /* (Italy's: Serie A 15, Serie B 12, Serie C 15, ..) */
            t.league = d;
            const int32_t n = club_bench(&t, &et);
            printf("[bench] country 20 division %u: %d named\n", (unsigned)d, (int)n);
        }
    }
    printf("[bench] %s\n", bad ? "FAILED" : "ok");
    return bad != 0;
#else
    return 0;
#endif
}
