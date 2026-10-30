/*
 * uefa_coef.c -- a world's UEFA coefficients and its qualifying rounds' season end
 * (include/uefa_coef.h; docs/WORLD_2026_27.md, phase 5). The SWOS 96/97 Mod's
 * tools/uefacoef.py (uc_*) and tools/uefaq.py (uq_country / uq_fill / uq_cpts), MIT, Davide
 * Lorigliola, as its assembly does them; the qualifying rounds themselves are its uefaq.c
 * (src/uefa_access.c).
 *
 * The Mod's club ids are its team words, file | ordinal << 8 (uefa_access.c compares and hashes
 * them); ours are BE {country, ordinal}: modId() / ourId() convert.
 */
#include "uefa_coef.h"
#include "uefa_access.h"
#include "game_types.h"
#include "endian_helpers.h"
#include "league_phase.h"
#include "world.h"
#include "league_playoffs.h"
#ifdef SWOS_SDL_BUILD
#include <stdlib.h>              /* (getenv: SWOS_UEFAQ_FORCE) */
#endif

UefaCoefState g_uefaCoef;
struct uq_state g_uqState;              /* (the season's bracket: its points at the next season end) */

extern uint16_t _euroCupTitleHolderTeamId, _cupWinnersCupTitleHolderTeamId, _uefaCupTitleHolderTeamId;
extern uint8_t _primaryQualifiedTeamGrids[];
extern uint16_t _selTeamNumber, _isNationalSelector;
extern uint8_t _euroCupCopy[], _cupWinnersCupCopy[], _uefaCupCopy[];
extern uint8_t _teamSelectionKeyCache[];
extern uint16_t _teamSelectionKeyCacheCount, _teamSelectionKeyCacheIndex, _teamSelectionKeyCacheOffset;
extern uint16_t _competitionSlotUpdatedFlag;
extern DIYCompetitionEx _diyCompetitionStart;
uint16_t Career_SelectEuroCupSlotIfTeamParticipates(uint8_t *cupCopy, uint16_t euroCupIndex);
void Career_AppendTeamToTmdTeamPool(TeamFileEx *team);
void Career_RetireSlotCalendarEntries(uint8_t slot);
void Career_AppendTeamToEuroCupListN(int32_t cup, TeamFileEx *record);    /* (career_eurocups.c) */

static uint16_t modId(uint16_t team) { return (uint16_t)((team >> 8) | (team << 8)); }
static uint16_t ourId(uint16_t id) { return (uint16_t)((id >> 8) | (id << 8)); }

const WorldUefaSeeds *World_UefaSeeds(void);

int32_t UefaCoef_On(void)
{
    const uint8_t (*places)[4];
    int32_t n;
    const WorldUefaSeeds *s = World_UefaSeeds();
    return s && s->nCountries && World_LeaguePhaseCups(&places, &n) != 0;
}

void UefaCoef_Reset(void)
{
    g_uefaCoef.init = 0;
    g_uqState.mark = 0;
    g_uqState.nbr = 0;
}

static UefaClubCoef *club(uint16_t team, int32_t make)
{
    UefaCoefState *c = &g_uefaCoef;
    for (int32_t i = 0; i < c->nClubs; i++)
        if (c->club[i].team == team)
            return &c->club[i];
    if (!make || c->nClubs >= UEFA_COEF_CLUBS)    /* (full: as the Mod past its global numbers) */
        return 0;
    UefaClubCoef *k = &c->club[c->nClubs++];
    k->team = team;
    k->pts = 0;
    for (int32_t s = 0; s < 5; s++)
        k->hist[s] = 0;
    return k;
}

/* uc_init: zero, the seeds (5 seasons of the associations, a season's units of the clubs). */
static void init(void)
{
    UefaCoefState *c = &g_uefaCoef;
    const WorldUefaSeeds *s = World_UefaSeeds();
    uint8_t *p = (uint8_t *)c;
    for (uint32_t i = 0; i < sizeof *c; i++)
        p[i] = 0;
    c->init = 1;
    for (int32_t i = 0; s && i < s->nCountries; i++)
        for (int32_t k = 0; k < 5; k++)
            c->hist[k][s->countries[i].country] = s->countries[i].seasons[k];
    for (int32_t i = 0; s && i < s->nClubs; i++) {
        UefaClubCoef *k = club((uint16_t)(s->clubs[i].country << 8 | s->clubs[i].ordinal), 1);
        for (int32_t j = 0; k && j < 5; j++)
            k->hist[j] = s->clubs[i].units;
    }
}

/* uc_ok: a cup of a league-phase world (the block made at its first use). */
static int32_t ok(uint8_t compId)
{
    if (compId > 2 || !UefaCoef_On())
        return 0;
    if (!g_uefaCoef.init)
        init();
    return 1;
}

/* uc_add: a club's units this season, and its association's. */
static void add(uint16_t team, uint32_t units)
{
    g_uefaCoef.pts[team >> 8] += units;
    UefaClubCoef *k = club(team, 1);
    if (k)
        k->pts = (uint16_t)(k->pts + units);
}

void UefaCoef_StageStart(uint8_t compId, const uint16_t *teams, int32_t n)
{
    if (!ok(compId))
        return;
    UefaCoefState *c = &g_uefaCoef;
    c->prevN[compId] = 0;
    c->phaseEnded[compId] = 0;
    c->done = 0;                        /* (a season is running: its end will be counted) */
    for (int32_t i = 0; i < n; i++) {
        c->teams[teams[i] >> 8]++;
        if (compId == 0)
            add(teams[i], 48);          /* (Champions League league phase: 6 points) */
    }
}

void UefaCoef_Result(uint8_t compId, uint16_t home, uint16_t away, uint16_t g1, uint16_t g2)
{
    if (!ok(compId))
        return;
    if (g1 == g2) {
        add(home, 8);
        add(away, 8);
    } else {
        add(g1 > g2 ? home : away, 16);
    }
}

void UefaCoef_PhaseEnd(uint8_t compId, const uint16_t *order)
{
    if (!ok(compId) || g_uefaCoef.phaseEnded[compId])
        return;
    g_uefaCoef.phaseEnded[compId] = 1;
    for (int32_t p = 0; p < 24; p++) {
        int32_t u = 2 * (24 - p);       /* (0.25 a place) */
        if (compId == 2 && p < 8)       /* (Conference League: 0.125 for places 9-24, then 0.25) */
            u = 2 * (8 - p) + 16;
        else if (compId == 2)
            u = 24 - p;
        add(order[p], (uint32_t)u);
    }
}

/* uc_draw: a knockout round drawn -- the previous round's ties (its clubs still in: 3 points,
 * out: 1), the bonus for reaching a round from the round of 16, the final's two clubs. */
void UefaCoef_Draw(uint8_t compId, int32_t stage, const uint16_t *teams, int32_t n)
{
    if (n > 16 || !ok(compId))
        return;
    UefaCoefState *c = &g_uefaCoef;
    static const uint8_t reach[3] = { 12, 8, 4 };
    for (int32_t i = 0; i < c->prevN[compId]; i++) {
        uint16_t t = c->prev[compId][i];
        uint32_t u = 8;
        for (int32_t j = 0; j < n; j++)
            if (teams[j] == t)
                u = 24;
        add(t, u);
    }
    c->prevN[compId] = (uint8_t)n;
    for (int32_t i = 0; i < n; i++) {
        c->prev[compId][i] = teams[i];
        if (stage >= 2)
            add(teams[i], reach[compId]);
    }
    if (n == 2) {
        c->fin[compId][0] = teams[0];
        c->fin[compId][1] = teams[1];
    }
}

/* uc_kc: its points of the 5 seasons, or a fifth of its association's coefficient. */
uint32_t UefaCoef_Club(uint16_t team)
{
    const UefaCoefState *c = &g_uefaCoef;
    const UefaClubCoef *k = club(team, 0);
    uint32_t own = 0, a = 0;
    for (int32_t s = 0; s < 5; s++) {
        if (k)
            own += k->hist[s];
        a += c->hist[s][team >> 8];
    }
    a /= 625;                           /* (thousandths of points / 5 / 1000 * 8) */
    return a > own ? a : own;
}

/* uc_str: the pots' key -- the club coefficient, the squad value breaking ties; the Champions
 * League holder first. */
uint32_t UefaCoef_PotKey(uint8_t compId, uint16_t team, uint32_t value)
{
    if (!ok(compId))
        return value;
    if (compId == 0 && team == _euroCupTitleHolderTeamId)
        return 0xFFFFFFFFu;
    return (UefaCoef_Club(team) << 10) + (value > 1023 ? 1023 : value);
}

/* ---- the season end ---- */

static uint8_t s_rankC[64];             /* UC_C: the world's UEFA associations, best first */
static int32_t s_rankN;

/* uc_rank: by their 5 ring seasons (pack order on a tie). */
static void rank(void)
{
    const WorldUefaSeeds *s = World_UefaSeeds();
    uint32_t t[64];
    s_rankN = 0;
    for (int32_t i = 0; i < s->nCountries && s_rankN < 64; i++)
        s_rankC[s_rankN++] = s->countries[i].country;
    for (int32_t i = 0; i < s_rankN; i++) {
        t[i] = 0;
        for (int32_t k = 0; k < 5; k++)
            t[i] += g_uefaCoef.hist[k][s_rankC[i]];
    }
    for (int32_t i = 1; i < s_rankN; i++) {
        uint8_t cc = s_rankC[i];
        uint32_t v = t[i];
        int32_t j = i;
        for (; j > 0 && v > t[j - 1]; j--) {
            t[j] = t[j - 1];
            s_rankC[j] = s_rankC[j - 1];
        }
        s_rankC[j] = cc;
        t[j] = v;
    }
}

/* uc_final: the finals' winners, the qualifying ties' points, the ranking (before this season
 * enters the ring: the access list uses a year-old one), the ring. */
static void close_season(void)
{
    UefaCoefState *c = &g_uefaCoef;
    if (!ok(0) || c->done)
        return;
    c->done = 1;
    const uint16_t hold[3] = { _euroCupTitleHolderTeamId, _cupWinnersCupTitleHolderTeamId, _uefaCupTitleHolderTeamId };
    for (int32_t k = 0; k < 3; k++) {
        if ((c->fin[k][0] || c->fin[k][1]) && (c->fin[k][0] == hold[k] || c->fin[k][1] == hold[k]))
            add(hold[k], 16);
        c->fin[k][0] = c->fin[k][1] = 0;
    }
    UQ_S = &g_uqState;                  /* (uq_cpts: last season's bracket) */
    uq_coef();
    for (u32 i = 0; i < UQ_NCP; i++) {
        uint16_t t = ourId(UQ_CP[i].team);
        add(t, UQ_CP[i].units);
        if (UQ_CP[i].count)
            c->teams[t >> 8]++;         /* (a club of its association's divisor: no league phase) */
    }
    rank();
    const int32_t r = c->ring;
    for (int32_t a = 0; a < 256; a++) {
        c->hist[r][a] = c->teams[a] ? c->pts[a] * 125u / c->teams[a] : 0;
        c->pts[a] = 0;
        c->teams[a] = 0;
    }
    int32_t n = 0;
    for (int32_t i = 0; i < c->nClubs; i++) {   /* (a club with no points left in the ring: dropped) */
        UefaClubCoef k = c->club[i];
        k.hist[r] = k.pts;
        k.pts = 0;
        if (k.hist[0] | k.hist[1] | k.hist[2] | k.hist[3] | k.hist[4])
            c->club[n++] = k;
    }
    c->nClubs = (uint16_t)n;
    c->ring = (uint8_t)(r + 1 < 5 ? r + 1 : 0);
}

#if SWOS_WORLDS
static TeamFileEx s_rec[550];           /* (UQREC: the clubs seen, their records) */
#else
static TeamFileEx s_rec[1];
#endif
static uint8_t s_on, s_keepUecl, s_pend;
static uint16_t s_listed;

static void clear_tie(int32_t c);
static void make_tie(u8 r, u16 opp);

static u16 seen(uint16_t team, int32_t idx, int32_t rk)
{
    const TeamFileEx *t = &s_rec[idx];
    uint32_t v = 0;
    for (int32_t k = 0; k < 16; k++)
        v += (uint8_t)t->players[k].price;
    UQ_IN.club[idx].id = modId(team);
    UQ_IN.club[idx].rank = (u8)rk;
    UQ_IN.club[idx].str = v;
    UQ_IN.club[idx].coef = UefaCoef_Club(team);
    return (u16)(idx + 1);
}

static void copyRecord(int32_t idx, const uint8_t *grid)
{
#if SWOS_WORLDS
    const uint8_t *s = grid;
    uint8_t *d = (uint8_t *)&s_rec[idx];
    for (uint32_t i = 0; i < sizeof(TeamFileEx); i++)
        d[i] = s[i];
#else
    (void)idx;
    (void)grid;
#endif
}

/* uq_country: a country's top 9 (the standings' records) and its cup winner into the input;
 * the first one of a season end closes the season's coefficients and starts a fresh input. */
void UefaQual_Country(void)
{
    if (!UefaCoef_On())
        return;
    if (!s_on) {
        s_on = 1;
        close_season();
        if (!s_rankN)
            rank();                     /* (a season end already counted: its ranking again) */
        uint8_t *p = (uint8_t *)&UQ_IN;
        for (uint32_t i = 0; i < sizeof UQ_IN; i++)
            p[i] = 0;
    }
    const uint8_t *g0 = _primaryQualifiedTeamGrids;
    int32_t first = -1;
    for (int32_t i = 0; i < 9 && first < 0; i++) {
        uint16_t t = BE_READ_U16(g0 + (uint32_t)i * sizeof(TeamFileEx));
        if (t && t != 0xFFFF)
            first = t >> 8;
    }
    int32_t rk = 0;
    for (int32_t i = 0; i < s_rankN && !rk; i++)
        if (s_rankC[i] == first)
            rk = i + 1;
    if (!rk || rk > 55)
        return;                         /* (not one of the world's UEFA associations) */
    UQ_IN.present[rk] = 1;
    int32_t n = 0;
    for (int32_t i = 0; i < 9; i++) {
        const uint8_t *g = g0 + (uint32_t)i * sizeof(TeamFileEx);
        uint16_t t = BE_READ_U16(g);
        if (!t || t == 0xFFFF)
            continue;
        int32_t idx = (rk - 1) * 10 + n;
        copyRecord(idx, g);
        UQ_IN.league[rk][n++] = seen(t, idx, rk);
    }
    const uint8_t *cg = g0 + 9 * sizeof(TeamFileEx);    /* (the cup winner) */
    uint16_t cw = BE_READ_U16(cg);
    if (cw && cw != 0xFFFF) {
        int32_t j = 0;
        while (j < n && UQ_IN.club[(rk - 1) * 10 + j].id != modId(cw))
            j++;
        if (j < n) {
            UQ_IN.cup[rk] = (u16)((rk - 1) * 10 + j + 1);
        } else {
            int32_t idx = (rk - 1) * 10 + 9;
            copyRecord(idx, cg);
            UQ_IN.cup[rk] = seen(cw, idx, rk);
        }
    }
    /* Its knockout play-offs (the Mod's UQ_EPP, gplay's bytes by its league file): a 'europe'
     * one gives its last Conference League place (Austria, Bulgaria, Denmark, the Netherlands,
     * Northern Ireland, Romania, San Marino, Wales), run by uq_season (ep_run). (A league cup
     * winner's place -- pack season.league_cup_europe -- no pack of World 2026-27 has.) */
    int32_t np = 0;
    const LeaguePlayoffs *po = World_LeaguePlayoffs(&np);
    for (int32_t i = 0; i < np && !UQ_IN.eplay[rk]; i++)
        if (po[i].country == first && po[i].spec)
            UQ_IN.eplay[rk] = po[i].spec;       /* (the first of its file, as the Mod's table) */
}

/* uq_fill: the holders, the 2 European Performance Spots, the seed, the rounds, the lists. */
void UefaQual_Fill(void)
{
    s_listed = 0;
    s_keepUecl = 0;
    if (!s_on)
        return;
    s_on = 0;
    const WorldUefaSeeds *sd = World_UefaSeeds();
    UQ_CUPONLY_FILE[0] = sd->cupOnly;
    UQ_IN.nclub = 553;
    const uint16_t hold[3] = { _euroCupTitleHolderTeamId, _cupWinnersCupTitleHolderTeamId, _uefaCupTitleHolderTeamId };
    for (int32_t k = 0; k < 3; k++) {
        if (!hold[k] || hold[k] == 0xFFFF)
            continue;
        int32_t i = 0;
        while (i < 550 && UQ_IN.club[i].id != modId(hold[k]))
            i++;
        if (i == 550) {                 /* (not seen: its own index, no record) */
            i = 550 + k;
            UQ_IN.club[i].id = modId(hold[k]);
            for (int32_t r = 0; r < s_rankN; r++)
                if (s_rankC[r] == hold[k] >> 8)
                    UQ_IN.club[i].rank = (u8)(r + 1);
        }
        UQ_IN.hold[k] = (u16)(i + 1);
    }
    const UefaCoefState *c = &g_uefaCoef;   /* (EPS: the season just ended, the ring's last) */
    const int32_t last = c->ring ? c->ring - 1 : 4;
    int32_t v0 = -1, v1 = -1;
    for (int32_t i = 0; i < s_rankN && i < 55; i++) {
        int32_t v = (int32_t)c->hist[last][s_rankC[i]];
        if (v > v0) {
            v1 = v0;
            UQ_IN.eps[1] = UQ_IN.eps[0];
            v0 = v;
            UQ_IN.eps[0] = (u8)(i + 1);
        } else if (v > v1) {
            v1 = v;
            UQ_IN.eps[1] = (u8)(i + 1);
        }
    }
    u32 seed = 0;                       /* (the clubs seen: a save gives the same draws) */
    for (int32_t i = 0; i < 553; i++)
        seed = seed * 31u + UQ_IN.club[i].id + UQ_IN.club[i].str;
    UQ_IN.seed = seed;
    UQ_IN.manager = 0;                  /* (the career club: its ties are its own to play) */
    if (!_isNationalSelector)
        for (int32_t i = 0; i < 550; i++)
            if (UQ_IN.club[i].id && UQ_IN.club[i].id == modId(_selTeamNumber))
                UQ_IN.manager = (u16)(i + 1);
    UQ_IN.force = 0;
#ifdef SWOS_SDL_BUILD
    {                                   /* (tests, as the Mod's UEFAQ_FORCE: the career club's first round) */
        const char *f = getenv("SWOS_UEFAQ_FORCE");
        if (f && UQ_IN.manager)
            UQ_IN.force = (u8)atoi(f);
    }
#endif
    UQ_S = &g_uqState;
    uq_season();
    for (int32_t k = 0; k < 3; k++)     /* (the tie contests: none until the career club has one) */
        clear_tie(k);
    s_pend = 0;
    if (UQ_OUT.mt_n) {                  /* its first tie: slot 3 at the season start */
        make_tie(UQ_OUT.mt_round[0], UQ_IN.club[UQ_OUT.mt_opp[0] - 1].id);
        s_pend = 1;
    }
    for (int32_t k = 0; k < 3; k++)
        for (int32_t j = 0; j < UQ_OUT.gn[k]; j++) {
            int32_t idx = UQ_OUT.gl[k][j] - 1;
            if (idx >= 550)
                continue;               /* (a holder not seen: it joins with the holders) */
            uint16_t t = ourId(UQ_IN.club[idx].id);
            if (t == hold[0] || t == hold[1] || (t == hold[2] && !UQ_OUT.hkeep[2]))
                continue;               /* (the holders join after) */
            Career_AppendTeamToEuroCupListN(k, &s_rec[idx]);
            s_listed++;
        }
    s_keepUecl = UQ_OUT.hkeep[2];
}

/* ---- the career club's qualifying ties (uq_resolve; the Mod's uq_daily) ----
 *
 * A tie is a cup of one round of two legs, extra time and penalties, no away goals, on its
 * round's weeks (the Mod's: Q1 early July, Q2 late July, Q3 early August, the play-off late
 * August; the second leg two eighths of a month later), in slot 3 as a European cup is. When
 * it is decided the club's next tie takes slot 3, or, its ties over, the league phases' lists
 * are rewritten with its results (uq_resolve: the stand-in back to the club, every result
 * unlike the simulated one swapping the two clubs from there on) and the league phase it
 * reached takes slot 3. */
#define UEFA_TIE_ID0 229                /* (free competition ids: 229 CL, 230 EL, 231 UECL ties) */
static CompetitionCup s_tie[3];
static char kTieName[3][2][24] = {
    { "QUAL. CHAMPIONS LEAGUE", "QUAL. CL" },
    { "QUAL. EUROPA LEAGUE", "QUAL. EL" },
    { "QUAL. CONFERENCE LEAGUE", "QUAL. UECL" },
};

static int32_t tie_week(u8 r)           /* (the round's first leg, eighths of a month from January) */
{
    switch (r) {
    case CL_Q1: case EL_Q1: case CO_Q1: return 0x31;
    case CL_Q2: case CL_Q2nc: case EL_Q2: case CO_Q2: case CO_Q2ch: return 0x35;
    case CL_Q3: case CL_Q3nc: case EL_Q3: case EL_Q3ch: case CO_Q3: case CO_Q3ch: return 0x39;
    default: return 0x3C;               /* (the play-offs) */
    }
}

static void clear_tie(int32_t c)
{
    uint8_t *z = (uint8_t *)&s_tie[c];
    for (uint32_t i = 0; i < sizeof s_tie[c]; i++)
        z[i] = 0;
}

/* The tie record of round r: the career club v opp (the Mod's team word). */
static void make_tie(u8 r, u16 opp)
{
    const int32_t c = COMP(r), w = tie_week(r);
    CompetitionCup *t = &s_tie[c];
    const uint16_t a = _selTeamNumber, b = ourId(opp);
    clear_tie(c);
    t->header.comp.compId = (uint8_t)(UEFA_TIE_ID0 + c);
    t->header.comp.type = 1;
    t->header.comp.teamNumber = -1;
    t->header.comp.beginMonth = (int8_t)w;
    t->header.comp.endMonth = (int8_t)(w + 2);
    t->header.comp.reserved1 = 15;      /* (nameTableOffset, teamListOffset) */
    t->header.comp.reserved2 = 21;
    t->header.comp.seedReorderFlag = 1; /* (the club first, as listed) */
    t->header.teams = 2;
    t->header.awayGoalsMode = 0;        /* (no away goals since 2021) */
    t->header.subsAndPointsForWin = 0x35;
    t->header.roundStringBase = 1;
    t->roundRuleFlags[0] = (int8_t)0x94;        /* (two legs, extra time, penalties) */
    t->name[0] = kTieName[c][0];
    t->name[1] = kTieName[c][1];
    t->teamSeedList[0].countryNumber = (uint8_t)(a >> 8);
    t->teamSeedList[0].teamOrdinal = (uint8_t)a;
    t->teamSeedList[1].countryNumber = (uint8_t)(b >> 8);
    t->teamSeedList[1].teamOrdinal = (uint8_t)b;
}

/* uq_post: after the season end's last count check, the records of every club the career club
 * may meet in its ties that no list has (its opponents, whatever its results). */
void UefaQual_Post(void)
{
    if (!s_pend)
        return;
    s_pend = 0;
    for (int32_t p = 0; p < UQ_R.npot; p++) {
        const u16 id = UQ_R.pot[p];
        int32_t listed = 0;
        for (int32_t c = 0; c < 3 && !listed; c++)
            for (int32_t i = 0; i < UQ_OUT.gn[c]; i++)
                if (UQ_IN.club[UQ_OUT.gl[c][i] - 1].id == id)
                    listed = 1;
        for (int32_t i = 0; i < 550 && !listed; i++)
            if (UQ_IN.club[i].id == id) {
                Career_AppendTeamToTmdTeamPool(&s_rec[i]);
                break;
            }
    }
}

/* The slot-3 contest the career club starts a season in: its first tie (0 = none). */
uint8_t *UefaQual_SeasonTie(uint16_t *index)
{
    for (int32_t c = 0; c < 3; c++)
        if (s_tie[c].header.teams == 2 && PACK_BE_U16(s_tie[c].teamSeedList[0].countryNumber,
                                                      s_tie[c].teamSeedList[0].teamOrdinal) == _selTeamNumber) {
            *index = (uint16_t)(4 + c);
            return (uint8_t *)&s_tie[c];
        }
    return 0;
}

/* A loaded career: the tie under way rebuilt from the state (the record is not saved). */
uint8_t *UefaQual_RestoreTie(uint16_t index)
{
    if (index < 4 || index > 6 || g_uqState.mark != 0x5155 || !g_uqState.m)
        return 0;
    UQ_S = &g_uqState;
    uq_resolve();
    if (UQ_R.done)
        return 0;
    for (int32_t c = 0; c < 3; c++)
        clear_tie(c);
    make_tie(UQ_R.r, UQ_R.opp);
    return COMP(UQ_R.r) == index - 4 ? (uint8_t *)&s_tie[index - 4] : 0;
}

/* Slot 3 from the season start's own (Career_SelectEuroCupSlotIfTeamParticipates): its
 * calendar's entries so far retired first (Career_RetireSlotCalendarEntries). */
static void slot3(uint8_t *rec, uint16_t index)
{
    Career_RetireSlotCalendarEntries(3);
    uint16_t idx = _teamSelectionKeyCacheIndex, off = _teamSelectionKeyCacheOffset;
    Career_SelectEuroCupSlotIfTeamParticipates(rec, index);
    _teamSelectionKeyCacheIndex = idx;  /* (the new entries all come later: the position stays) */
    _teamSelectionKeyCacheOffset = off;
    _competitionSlotUpdatedFlag = 1;
}

/* uq_daily: the day just played (Career_AdvanceToNextTeamSelectionKeyCacheEntry). */
static uint16_t s_tieWinner;             /* (the career club's tie decided today: its winner) */

/* A two-legged tie decided (diy_competition.c, as CupD_TieDecided): the career club's? */
void UefaQual_TieDecided(uint16_t team1, uint16_t team2, int32_t team2Won)
{
    const uint8_t id = (uint8_t)_diyCompetitionStart.base.field_2E;
    if (id >= UEFA_TIE_ID0 && id <= UEFA_TIE_ID0 + 2 && (team1 == _selTeamNumber || team2 == _selTeamNumber))
        s_tieWinner = team2Won ? team2 : team1;
}

void UefaQual_Daily(void)
{
    struct uq_state *S = &g_uqState;
    if (!s_tieWinner)
        return;
    const uint16_t winner = s_tieWinner;
    s_tieWinner = 0;
    if (S->mark != 0x5155 || !S->m || S->m != modId(_selTeamNumber) || _isNationalSelector)
        return;
    if (winner == _selTeamNumber)
        S->res = (u8)(S->res | 1u << S->k);
    S->k++;
    UQ_S = S;
    uq_resolve();
    for (int32_t c = 0; c < 3; c++)
        clear_tie(c);
    if (!UQ_R.done) {                   /* the next tie */
        make_tie(UQ_R.r, UQ_R.opp);
        slot3((uint8_t *)&s_tie[COMP(UQ_R.r)], (uint16_t)(4 + COMP(UQ_R.r)));
        return;
    }
    S->mark = 0;                        /* (no tie left: the final lists into the season's cups) */
    CompetitionLeaguePhase *cup[3] = { (CompetitionLeaguePhase *)_euroCupCopy,
                                       (CompetitionLeaguePhase *)_cupWinnersCupCopy,
                                       (CompetitionLeaguePhase *)_uefaCupCopy };
    for (int32_t c = 0; c < 3; c++)
        for (int32_t i = 0; i < S->gn[c] && i < 36; i++) {
            const uint16_t t = ourId(UQ_R.gl[c][i]);
            cup[c]->teamSeedList[i].countryNumber = (uint8_t)(t >> 8);
            cup[c]->teamSeedList[i].teamOrdinal = (uint8_t)t;
        }
    if (UQ_R.dest < 3)                  /* (the league phase reached; out of Europe: the tie stays) */
        slot3((uint8_t *)cup[UQ_R.dest], UQ_R.dest);
}

/* A tie's round name (world_cups.c): FIRST .. FOURTH ROUND for Q1, Q2, Q3 and the play-off, by
 * its weeks -- the contest's name says which qualifying (the Mod's uq_tie_round); -1: not a tie. */
int32_t UefaQual_RoundStringIndex(uint8_t compId)
{
    if (compId < UEFA_TIE_ID0 || compId > UEFA_TIE_ID0 + 2)
        return -1;
    const int32_t w = s_tie[compId - UEFA_TIE_ID0].header.comp.beginMonth;
    return w < 0x35 ? 0 : w < 0x39 ? 1 : w < 0x3C ? 2 : 3;
}

int32_t UefaQual_KeepsUeclHolder(void) { return s_keepUecl; }
uint16_t UefaQual_Listed(void) { return s_listed; }
