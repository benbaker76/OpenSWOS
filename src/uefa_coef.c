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

UefaCoefState g_uefaCoef;
struct uq_state g_uqState;              /* (the season's bracket: its points at the next season end) */

extern uint16_t _euroCupTitleHolderTeamId, _cupWinnersCupTitleHolderTeamId, _uefaCupTitleHolderTeamId;
extern uint8_t _primaryQualifiedTeamGrids[];
extern uint16_t _selTeamNumber;
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
static uint8_t s_on, s_keepUecl;
static uint16_t s_listed;

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
    /* (a league cup winner's place -- pack season.league_cup_europe -- and a country's European
     * play-offs: none in World 2026-27 yet; the access list then takes the next league club) */
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
    UQ_IN.manager = 0;                  /* (the career club's ties are simulated: step 4c plays them) */
    UQ_IN.force = 0;
    UQ_S = &g_uqState;
    uq_season();
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

int32_t UefaQual_KeepsUeclHolder(void) { return s_keepUecl; }
uint16_t UefaQual_Listed(void) { return s_listed; }
