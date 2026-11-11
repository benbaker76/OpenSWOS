/*
 * world_finance.c -- a world's club money (include/world_finance.h; docs/WORLD_2026_27.md, "The
 * Mod 4.1's money").
 *
 * Ported from the SWOS 96/97 Mod 4.1 (github.com/aminta/swos-9697-mod): tools/finance.py (the TV
 * and commercial income, the gate cap) and tools/uprize.c / uprize2.c / uprize.py (the UEFA prize
 * money). Copyright (c) 2026 Davide Lorigliola, MIT licence:
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 *   and associated documentation files (the "Software"), to deal in the Software without
 *   restriction, including without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
 *   Software is furnished to do so, subject to the following conditions: The above copyright
 *   notice and this permission notice shall be included in all copies or substantial portions of
 *   the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 *
 * The Mod pays both in every one of its worlds, 1996-97 included; OpenSWOS only in a world other
 * than 96/97, whose money stays the original game's (docs/WORLD_2026_27.md §1).
 */
#include "world_finance.h"
#include "world.h"
#include "league_groups.h"
#include "uefa_coef.h"
#include "string_utils.h"

extern int32_t  _newBalance, _old_balance, _competitionBonuses, _weeklyPlayerWages;
extern int32_t  _sensisoftPlcSponsorship, _startingBalance;
extern uint16_t _wageScalingDivisor, _currentDivisionIndex, _selTeamNumber, _gameType;
extern uint16_t _stayingInOldClub;
extern uint8_t  _seasonPlaying;
extern int32_t  ModApi_SettingInt(const char *slug, const char *key, int32_t fallback);

/* The world mod's settings (mod.json "settings", OPTIONS -> MODS -> its page; docs/MODS.md §17):
 * what a new career starts with. */
#define WORLD_MOD "world2627"

#define GAME_TYPE_CAREER 4
#define MAX_BALANCE 100000000                   /* (ClampNewBalanceAndUpdateClubStatus's) */

static int32_t world(void)
{
    return SWOS_WORLDS && World_Current() != WORLD_9697;
}

static void credit(int32_t a)
{
    _newBalance = _newBalance > MAX_BALANCE - a ? MAX_BALANCE : _newBalance + a;
}

/* =====================================================================
 * TV and commercial income (finance.py)
 * ===================================================================== */

static const int32_t kTvBase[4] = { 3000000, 1200000, 500000, 200000 };    /* a season's, by level */

static int32_t s_tvPeriod;                      /* (FI_PER: this statement period's share) */
static uint8_t s_tvPeriodKnown;                 /* (0: the period began before a load -- not saved) */

int32_t WorldFinance_On(void)
{
    return world() && _gameType == GAME_TYPE_CAREER;
}

/* The career club's division as a level of its league: a level of parallel groups is one level
 * (include/league_groups.h), a league without groups its division. */
static int32_t tv_level(void)
{
    const LeagueGroups *lg = LeagueGroups_ForCountry(World_FileCountry((uint8_t)(_selTeamNumber >> 8)));
    const int32_t div = _currentDivisionIndex;
    if (lg)
        for (int32_t l = 0; l < lg->levels; l++)
            if (div >= lg->level[l].first && div < lg->level[l].first + lg->level[l].groups)
                return l;
    return div;
}

void WorldFinance_Charge(void)
{
    if (!WorldFinance_On())
        return;
    int32_t level = tv_level();
    if (level > 3)
        level = 3;
    const int32_t base = _wageScalingDivisor ? kTvBase[level] / _wageScalingDivisor : 0;
    const int32_t wages = _weeklyPlayerWages > 0 ? _weeklyPlayerWages : 0;
    const int32_t tv = base + (int32_t)((uint32_t)wages / 10u * 11u + (uint32_t)wages % 10u * 11u / 10u);
    s_tvPeriod += tv;
    g_ffp.inc += base;                  /* (FFP's income: the base, not the share paid for the wages) */
    credit(tv);
}

void WorldFinance_PeriodStart(void)
{
    s_tvPeriod = 0;
    s_tvPeriodKnown = 1;
}

void WorldFinance_Loaded(void)
{
    s_tvPeriod = 0;
    s_tvPeriodKnown = 0;
}

int32_t WorldFinance_PeriodTv(int32_t rowsTotal, int32_t *show)
{
    *show = WorldFinance_On();
    if (!*show)
        return 0;
    if (s_tvPeriodKnown)
        return s_tvPeriod;
    return _newBalance - _old_balance - rowsTotal;      /* (a period begun before a load) */
}

/* =====================================================================
 * UEFA prize money (uprize.c, uprize2.c)
 * ===================================================================== */

UefaPrizeState g_uefaPrize;

enum { P_PART, P_WIN, P_DRAW, P_PO, P_R16, P_QF, P_SF, P_FIN, P_WON, P_SHARE, P_TOP8, P_TOP16, P_COUNT };
enum { F_PART = 1, F_WON = 2, F_RANK = 4 };

/* UEFA circular No. 48/2026 (13 July 2026), the 2026/27 distribution in euros (uprize.py EUROS,
 * RANKING, VALUE): participation, win, draw, the knockout play-offs, round of 16, quarter-finals,
 * semi-finals, final, the winner's extra; the ranking pillar's share, places 1-8, places 9-16. */
static const uint32_t kEuros[3][P_COUNT] = {
    { 18620000, 2100000, 700000, 1000000, 11000000, 12500000, 15000000, 18500000, 6500000,
      275000, 2000000, 1000000 },               /* Champions League */
    { 4310000, 450000, 150000, 300000, 1750000, 2500000, 4200000, 7000000, 6000000,
      75000, 600000, 300000 },                  /* Europa League */
    { 3170000, 400000, 133000, 200000, 800000, 1300000, 2500000, 4000000, 3000000,
      28000, 400000, 200000 },                  /* Conference League */
};
/* The value pillar: one share of the European part, of the non-European part. */
static const uint32_t kValueEuros[3][2] = { { 935000, 346000 }, { 217000, 80000 }, { 63000, 23000 } };
#define SCALE_DIV 20                            /* the game's money = 1/20 of the euros (uprize.py K) */

/* The European part's media-market order: UEFA does not publish it; the Mod's (Davide, 09/11)
 * puts the five biggest first, then every other association in the world's coefficient order. */
static const uint8_t kMarket[5] = { 8, 35, 14, 20, 13 };   /* ENG ESP GER ITA FRA (our country numbers) */

/* A payment of the table: every one rounded to thousands, the ranking's share before it is
 * multiplied (uprize.py amounts()). */
static uint32_t amount(int32_t cup, int32_t k)
{
    const uint32_t a = kEuros[cup][k] / SCALE_DIV;
    return k == P_SHARE ? a : (a + 500u) / 1000u * 1000u;
}

static void pay_raw(uint32_t a)                 /* (up_credit) */
{
    a = (a + 500u) / 1000u * 1000u;
    if (!a)
        return;
    _competitionBonuses += (int32_t)a;
    credit((int32_t)a);
}

static int32_t s_cup;                           /* the cup of this event */
static void pay(int32_t k, uint32_t n) { pay_raw(amount(s_cup, k) * n); }

int32_t UefaPrize_On(void)
{
    return world() && _gameType == GAME_TYPE_CAREER && UefaCoef_On() && !g_uefaPrize.off;
}

void UefaPrize_NewCareer(void)
{
    UefaPrizeState z = { 0 };
    g_uefaPrize = z;
    g_uefaPrize.ok = 1;
    g_uefaPrize.off = !ModApi_SettingInt(WORLD_MOD, "uefa_prizes", 1);
}

void UefaPrize_LoadedWithout(void)
{
    UefaPrizeState z = { 0 };
    g_uefaPrize = z;                            /* (ok 0: an older save) */
}

/* The state for this cup and season; another one (or none yet) starts at zero -- an older save's
 * (ok 0) at what its club has reached now (counted as paid). */
static UefaPrizeState *cur(uint32_t part, uint32_t w, uint32_t d, uint32_t st, uint32_t won, uint32_t rank)
{
    UefaPrizeState *u = &g_uefaPrize;
    const uint8_t stamp = (uint8_t)(_seasonPlaying + 1u);
    if (u->ok && u->cup == s_cup && u->season == stamp)
        return u;
    if (u->ok)
        part = w = d = st = won = rank = 0;
    u->ok = 1;
    u->cup = (uint8_t)s_cup;
    u->season = stamp;
    u->flags = (uint8_t)((part ? F_PART : 0) | (won ? F_WON : 0) | (rank ? F_RANK : 0));
    u->wins = (uint8_t)w;
    u->draws = (uint8_t)d;
    u->stage = (uint8_t)st;
    return u;
}

/* Every association's coefficient rank (5 ring seasons, the seeds' order on a tie), for the
 * media-market order after the five. */
static int32_t assoc_rank(uint8_t country)
{
    const WorldUefaSeeds *s = World_UefaSeeds();
    uint32_t mine = 0;
    int32_t found = 0, better = 0, idx = 0;
    if (!s)
        return 255;
    for (int32_t i = 0; i < s->nCountries; i++)
        if (s->countries[i].country == country) {
            found = 1;
            idx = i;
            for (int32_t k = 0; k < 5; k++)
                mine += g_uefaCoef.hist[k][country];
        }
    if (!found)
        return 255;
    for (int32_t i = 0; i < s->nCountries; i++) {
        uint32_t t = 0;
        for (int32_t k = 0; k < 5; k++)
            t += g_uefaCoef.hist[k][s->countries[i].country];
        better += t > mine || (t == mine && i < idx);
    }
    return better;
}

static uint32_t market(uint16_t word)
{
    const uint8_t c = World_FileCountry((uint8_t)(word >> 8));
    for (int32_t k = 0; k < 5; k++)
        if (kMarket[k] == c)
            return (uint32_t)k;
    return 5u + (uint32_t)assoc_rank(c);
}

/* The value pillar (uprize2.c up_value), paid with the participation: the 36 ranked by market
 * then coefficient and by coefficient; the European rank the sum of the two (the coefficient
 * rank on a tie), 36 - rank shares; the non-European part 36 - the coefficient rank. */
static void value_pillar(int32_t me, const uint16_t *words)
{
    uint32_t c[36], m[36], a[36], b[36];
    for (int32_t i = 0; i < 36; i++) {
        c[i] = UefaCoef_Club(words[i]);
        m[i] = market(words[i]);
    }
    for (int32_t i = 0; i < 36; i++) {
        a[i] = b[i] = 0;
        for (int32_t j = 0; j < 36; j++) {
            const uint32_t cb = c[j] > c[i] || (c[j] == c[i] && j < i);
            b[i] += cb;
            a[i] += m[j] < m[i] || (m[j] == m[i] && cb);
        }
    }
    uint32_t e = 0;
    for (int32_t j = 0; j < 36; j++)
        e += a[j] + b[j] < a[me] + b[me] || (a[j] + b[j] == a[me] + b[me] && b[j] < b[me]);
    pay_raw(kValueEuros[s_cup][0] / SCALE_DIV * (36u - e) + kValueEuros[s_cup][1] / SCALE_DIV * (36u - b[me]));
}

void UefaPrize_Result(uint8_t compId, int32_t row, uint16_t played, uint16_t wins, uint16_t draws,
                      int32_t won, int32_t drawn, const uint16_t *words)
{
    if (compId > 2 || !UefaPrize_On() || row < 0 || row >= 36)
        return;
    s_cup = compId;
    /* (the result is in the row: an older save counts what came before it as paid) */
    UefaPrizeState *u = cur(played > 1, (uint32_t)wins - (won != 0), (uint32_t)draws - (drawn != 0), 0, 0, 0);
    const uint32_t w = wins, d = draws;
    if (!(u->flags & F_PART)) {
        u->flags |= F_PART;
        pay(P_PART, 1);
        value_pillar(row, words);
    }
    if (w > u->wins) {
        pay(P_WIN, w - u->wins);
        u->wins = (uint8_t)w;
    }
    if (d > u->draws) {
        pay(P_DRAW, d - u->draws);
        u->draws = (uint8_t)d;
    }
}

void UefaPrize_PhaseEnd(uint8_t compId, const uint16_t *finalOrder)
{
    if (compId > 2 || !UefaPrize_On())
        return;
    int32_t k = 0;
    while (k < 36 && finalOrder[k] != _selTeamNumber)
        k++;
    if (k == 36)
        return;
    s_cup = compId;
    UefaPrizeState *u = cur(1, 0, 0, 0, 0, 0);
    if (u->flags & F_RANK)
        return;
    u->flags |= F_RANK;
    pay(P_SHARE, (uint32_t)(36 - k));
    if (k < 16)
        pay(k < 8 ? P_TOP8 : P_TOP16, 1);
}

void UefaPrize_Stage(uint8_t compId, int32_t stage, int32_t numTeams, const uint16_t *teams)
{
    if (compId > 2 || stage < 1 || stage > 5 || !UefaPrize_On())
        return;
    int32_t i = 0;
    while (i < numTeams && teams[i] != _selTeamNumber)
        i++;
    if (i == numTeams)
        return;
    const int32_t k = stage == 1 ? P_PO : numTeams == 16 ? P_R16 : numTeams == 8 ? P_QF
                    : numTeams == 4 ? P_SF : numTeams == 2 ? P_FIN : -1;
    if (k < 0)
        return;
    s_cup = compId;
    UefaPrizeState *u = cur(1, 0, 0, (uint32_t)stage, 0, 1);
    if (stage > u->stage) {
        u->stage = (uint8_t)stage;
        pay(k, 1);
    }
}

void UefaPrize_CupWon(uint8_t compId, uint16_t winner)
{
    if (compId > 2 || winner != _selTeamNumber || !UefaPrize_On())
        return;
    s_cup = compId;
    UefaPrizeState *u = cur(1, 0, 0, 0, 1, 1);
    if (!(u->flags & F_WON)) {
        u->flags |= F_WON;
        pay(P_WON, 1);
    }
}

int32_t UefaPrize_SkipSeasonStartTerm(uint16_t euroCupSlotIndex)
{
    /* (the three UEFA cups, or a world's qualifying tie into one; the Mod skips the term for any
     * club of a league-phase world -- another confederation's too, which no UEFA prize pays) */
    return UefaPrize_On() && (euroCupSlotIndex <= 2 || (euroCupSlotIndex >= 4 && euroCupSlotIndex <= 6));
}

int32_t UefaPrize_ShowRow(void)
{
    return _competitionBonuses != 0 && UefaPrize_On();
}

int32_t UefaPrize_Shown(void)
{
    return world() && _gameType == GAME_TYPE_CAREER && UefaCoef_On();
}

/* =====================================================================
 * Financial fair play (ffp.py)
 * ===================================================================== */

FfpState g_ffp;

#define FFP_LIMIT 40                            /* per cent of last season's income (Davide 05/11) */

static int32_t s_ffpWagesEnd;                   /* (FF_WEND: between the season start's two halves) */
static int32_t s_ffpSponsorship;                /* (the ending season's sponsorship: the call resets it) */

void Ffp_NewCareer(void)
{
    FfpState z = { 0 };
    g_ffp = z;
    g_ffp.on = ModApi_SettingInt(WORLD_MOD, "financial_fair_play", 1) != 0;
    g_ffp.valid = 1;                            /* (counted from its first season) */
}

void Ffp_LoadedWithout(void)
{
    FfpState z = { 0 };
    g_ffp = z;                                  /* (an older save: off, its season not counted) */
}

void Ffp_Gate(int32_t credited)
{
    if (WorldFinance_On())
        g_ffp.inc += credited;
}

/* (32-bit arithmetic throughout: the Amiga's libgcc has no 64-bit division) */
static int32_t wage_bill(uint16_t charges)      /* the wages per charge x the charges, saturated */
{
    const int32_t w = _weeklyPlayerWages > 0 ? _weeklyPlayerWages : 0;
    if (charges && w > INT32_MAX / charges)
        return INT32_MAX;
    return w * charges;
}

static int32_t wages_now(void)
{
    return wage_bill(_wageScalingDivisor);
}

/* W - the limit (> 0: over it); last season's income I is never negative here (none: no limit). */
static int32_t over_by(int32_t w)
{
    const int32_t i = g_ffp.iprev;
    const int32_t limit = i / 100 * FFP_LIMIT + i % 100 * FFP_LIMIT / 100;      /* (I x 40 / 100) */
    return w - limit;
}

void Ffp_SeasonStartBefore(void)
{
    if (!WorldFinance_On())
        return;
    /* the wage bill at the season's end, before the squad is priced again: its wages per charge x
     * ITS charges (an older save: none kept -- the new season's) */
    s_ffpWagesEnd = wage_bill(g_ffp.nch ? g_ffp.nch : _wageScalingDivisor);
    s_ffpSponsorship = _sensisoftPlcSponsorship;
}

static void ffp_memo(int32_t fine)
{
    extern void *_clubMessagesTable[];
    extern volatile uint16_t _currentMessage;
    extern uintptr_t _plNameForMessage[];
    extern void ShowChairmanMessage(void);
    static char l1[] = "SORRY %a, OUR WAGES ARE OVER 40 PER CENT OF";
    static char l2[] = "INCOME: WE ARE FINED %b AND CANNOT BUY PLAYERS";
    static uint8_t amount[24];
    Int2Pounds((uint32_t)fine, amount);
    void *t0 = _clubMessagesTable[0], *t1 = _clubMessagesTable[1];
    const uintptr_t name = _plNameForMessage[0];
    const uint16_t msg = _currentMessage;
    _clubMessagesTable[0] = l1;                 /* message 1's two lines ours for this one memo */
    _clubMessagesTable[1] = l2;
    _plNameForMessage[0] = (uintptr_t)amount;
    _currentMessage = 1;
    ShowChairmanMessage();
    _clubMessagesTable[0] = t0;
    _clubMessagesTable[1] = t1;
    _plNameForMessage[0] = name;
    _currentMessage = msg;
}

void Ffp_SeasonStartAfter(void)
{
    if (!WorldFinance_On())
        return;
    /* I: the ending season's sponsorship, gates and TV base, the competition bonuses and the
     * chairman's money the season start has just paid */
    int64_t income = (int64_t)s_ffpSponsorship + g_ffp.inc + _competitionBonuses + _startingBalance;
    if (income > INT32_MAX)
        income = INT32_MAX;             /* (a 64-bit sum: no division of it) */
    g_ffp.inc = 0;
    g_ffp.fine = 0;
    g_ffp.block = 0;
    g_ffp.iprev = 0;
    if ((int16_t)_stayingInOldClub > 0 && g_ffp.valid && income > 0) {   /* (a new club -- 0, or 0xFFFF before its first season -- an older save: no season of ours) */
        g_ffp.iprev = (int32_t)income;
        const int32_t x = over_by(s_ffpWagesEnd);
        if (g_ffp.on && x > 0) {
            g_ffp.block = 1;
            g_ffp.fine = x / 2 / 1000 * 1000;
            _newBalance -= g_ffp.fine;          /* (the season-start statement's own row) */
            ffp_memo(g_ffp.fine);
        }
    }
    g_ffp.valid = 1;
    g_ffp.nch = _wageScalingDivisor;            /* (this season's charges: the next season start's W) */
}

int32_t Ffp_PurchasesBlocked(void)
{
    return WorldFinance_On() && g_ffp.on && g_ffp.block && over_by(wages_now()) > 0;
}

int32_t Ffp_Shown(void)
{
    return WorldFinance_On();
}

void Ffp_RatioText(char *out, int32_t size)
{
    char num[16];
    if (g_ffp.iprev > 0) {
        uint32_t w = (uint32_t)wages_now(), i = (uint32_t)g_ffp.iprev;
        while (i > 40000000u) {         /* (the remainder x 10 below stays in 32 bits) */
            w >>= 1;
            i >>= 1;
        }
        uint32_t r = w / i, rem = w % i;
        if (r > 9)
            r = 999;
        else
            for (int32_t d = 0; d < 2; d++) {
                rem *= 10;
                r = r * 10 + rem / i;
                rem %= i;
            }
        int32_t n = 0;
        char rev[8];
        do { rev[n++] = (char)('0' + r % 10); r /= 10; } while (r && n < 7);
        int32_t k = 0;
        while (n)
            num[k++] = rev[--n];
        num[k++] = '%';
        num[k] = 0;
    } else {
        num[0] = '-'; num[1] = '-'; num[2] = 0;
    }
    /* (no snprintf on the Amiga's -nostdlib: by hand) */
    const char *parts[3] = { "WAGES/INCOME ", num, " (MAX 40%)" };
    int32_t o = 0;
    for (int32_t p = 0; p < 3; p++)
        for (const char *c = parts[p]; *c && o < size - 1; c++)
            out[o++] = *c;
    out[o] = 0;
}
