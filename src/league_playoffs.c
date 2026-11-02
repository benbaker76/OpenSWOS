/*
 * league_playoffs.c -- a world's play-offs and play-outs at the season end
 * (include/league_playoffs.h).
 *
 * The rules are the rules library's (third_party/swos-competition-engine/rules, built here by
 * src/swos_rules_*.c): the SWOS 96/97 Mod's own -- dplay.c's Italian play-offs, gplay.c's pack
 * knockouts, its B-team rules -- one C copy both games link, checked against the Mod's C season
 * after season (its tests/rules/check.sh; scripts/league_playoffs_check.sh here). This file is
 * the game's side of them: the final tables from the DIY league, a league's stage tables kept
 * through the season, the rules of the world's leagues (world_data.c), what the career club
 * played itself (world_playoff.c), and the moves written into the season's tables, the pool
 * (team_pool.c) and the cups' next draws (world_cups.c, world_cupd.c).
 */
#include "league_playoffs.h"
#include "league_groups.h"
#include "team_pool.h"
#include "world_playoff.h"
#include "world_cups.h"
#include "world_cupd.h"
#include "game_types.h"
#include "diy_competition.h"
#include "endian_helpers.h"
#include "world.h"

extern uint16_t _playPromotedAnimation;
extern uint16_t _playRelegatedAnimation;
extern TeamFileEx _careerTeam;
extern DIYCompetitionEx _diyCompetitionStart;
extern uint16_t _teamsCountryNumbers[256];

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;

/* ---- The final tables, as the season end leaves each division ---- */

enum { GT = SR_CLUBS, GD = SR_DIVS };
typedef sr_row lp_row;
static sr_state s_rules;                /* the rules library's state; the final tables are its input */
#define s_in (s_rules.t)

void LeaguePlayoffs_Begin(void)
{
    s_in.nd = 0;
    for (int32_t d = 0; d < GD; d++)
        s_in.n[d] = 0;
}

void LeaguePlayoffs_KeepTable(const uint8_t *competition, uint16_t division)
{
    (void)competition;
    DIYCompetitionEx *ex = &_diyCompetitionStart;
    uint8_t *base = (uint8_t *)&ex->base;
    uint16_t n = (uint16_t)ex->base.numTeams;
    if (division >= GD || ex->base.competitionType != 0)
        return;
    if (n > GT)
        n = GT;
    for (uint16_t k = 0; k < n; k++) {
        const DIYTeamStats *st = (const DIYTeamStats *)(base + ex->matchData.league.teamEntryOffsets[k] +
                                                        DIY_STANDINGS_OFFSET);
        const TeamFileEx *team = (const TeamFileEx *)diy_resolve_team(st->teamSelOffset);
        lp_row *r = &s_in.t[division][k];
        r->team = PACK_BE_U16(team->team.countryNumber, team->team.teamOrdinal);
        r->glob = (u16)team->team.globalTeamNumber;
        r->won = st->wins;
        r->gf = st->goalsFor;
        r->ga = st->goalsAgainst;
        r->pts = st->points;
    }
    s_in.n[division] = (u8)n;
    if (division + 1 > s_in.nd)
        s_in.nd = (u8)(division + 1);
}

/* ---- A league's stage tables (the Mod's GP_SS banks, gp_stage / gp_keep) ---- */

LeagueStageBank g_leagueStages[LEAGUE_STAGE_BANKS];
static LeagueStageBank s_noBank;        /* (no room: nothing kept) */

void LeaguePlayoffs_ResetStages(void)
{
    uint8_t *pr = (uint8_t *)&g_promedio;       /* (a new career: the world's history again) */
    for (uint32_t k = 0; k < sizeof g_promedio; k++)
        pr[k] = 0;
    uint8_t *p = (uint8_t *)g_leagueStages;
    for (uint32_t i = 0; i < sizeof g_leagueStages; i++)
        p[i] = 0;
}

static LeagueStageBank *bank_of(uint8_t country, int32_t make)
{
    for (int32_t i = 0; i < LEAGUE_STAGE_BANKS; i++)
        if (g_leagueStages[i].used && g_leagueStages[i].key == country)
            return &g_leagueStages[i];
    if (!make)
        return 0;
    for (int32_t i = 0; i < LEAGUE_STAGE_BANKS; i++)
        if (!g_leagueStages[i].used) {
            LeagueStageBank *b = &g_leagueStages[i];
            uint8_t *p = (uint8_t *)b;
            for (uint32_t j = 0; j < sizeof *b; j++)
                p[j] = 0;
            b->used = 1;
            b->key = country;
            return b;
        }
    return 0;
}

/* The DIY league's row in table order -> a kept row. */
static void table_row(int32_t place, LeagueStageRow *o)
{
    DIYCompetitionEx *ex = &_diyCompetitionStart;
    const DIYTeamStats *st = (const DIYTeamStats *)((uint8_t *)&ex->base + ex->matchData.league.teamEntryOffsets[place] +
                                                    DIY_STANDINGS_OFFSET);
    const TeamFileEx *team = (const TeamFileEx *)diy_resolve_team(st->teamSelOffset);
    o->team = PACK_BE_U16(team->team.countryNumber, team->team.teamOrdinal);
    o->glob = (u16)team->team.globalTeamNumber;
    o->won = st->wins;
    o->gf = st->goalsFor;
    o->ga = st->goalsAgainst;
    o->pts = st->points;
}

static void row_copy(LeagueStageRow *d, const LeagueStageRow *s)
{
    d->team = s->team; d->glob = s->glob; d->won = s->won; d->gf = s->gf; d->ga = s->ga; d->pts = s->pts;
}

/* gp_stage: stage k's table; annual: r[2] = every other stage table of this season by club,
 * by points, goal difference, goals (the earlier order on a tie). */
void LeaguePlayoffs_StageTable(uint8_t country, uint32_t k, uint32_t annual)
{
    const int32_t n = _diyCompetitionStart.base.numTeams;
    LeagueStageBank *b = bank_of(country, 1);
    if (!b || k >= LEAGUE_STAGE_TABLES || n < 1 || n > GT)
        return;
    for (int32_t i = 0; i < n; i++)
        table_row(i, &b->r[k][i]);
    b->n[k] = (u8)n;
    b->fresh |= (u8)(1 << k);
    if (!annual || k == 0 || b->n[0] != n)
        return;
    LeagueStageRow *a = b->r[2], tmp;
    for (int32_t i = 0; i < n; i++)
        row_copy(&a[i], &b->r[0][i]);
    for (int32_t q = 1; q < LEAGUE_STAGE_TABLES; q++) {
        if (q == 2 || !(b->fresh & 1 << q) || b->n[q] != n)
            continue;
        const LeagueStageRow *o2 = b->r[q];
        for (int32_t i = 0; i < n; i++)
            for (int32_t j = 0; j < n; j++)
                if (o2[j].glob == a[i].glob) {
                    a[i].won += o2[j].won; a[i].gf += o2[j].gf; a[i].ga += o2[j].ga; a[i].pts += o2[j].pts;
                }
    }
    for (int32_t i = 1; i < n; i++)
        for (int32_t j = i; j > 0; j--) {
            const int32_t dp = a[j].pts - a[j - 1].pts, dd = (a[j].gf - a[j].ga) - (a[j - 1].gf - a[j - 1].ga);
            if (dp > 0 || (dp == 0 && (dd > 0 || (dd == 0 && a[j].gf > a[j - 1].gf)))) {
                row_copy(&tmp, &a[j]); row_copy(&a[j], &a[j - 1]); row_copy(&a[j - 1], &tmp);
            } else break;
        }
    b->n[2] = (u8)n;
    b->fresh |= 4;
}

void LeaguePlayoffs_KeepStage(uint8_t country, uint16_t division)
{
    const int32_t n = _diyCompetitionStart.base.numTeams;
    LeagueStageBank *b = bank_of(country, 1);
    if (!b || n < 2 || n > 21)                  /* (the Mod's keep block holds 21) */
        return;
    for (int32_t i = 0; i < n; i++) {
        table_row(i, &b->kept[i]);
        b->kept[i].won = b->kept[i].gf = b->kept[i].ga = 0;     /* (the Mod keeps places and points) */
    }
    b->kept_div = (u8)division;
    b->n_kept = (u8)n;
}

/* ---- The rules library (third_party/swos-competition-engine/rules): a league's rules, the
 * season's inputs, what the career club played ---- */

LeaguePromedio g_promedio;
LeagueWinners g_leagueWinners;
int32_t LeaguePlayoffs_TestHeld;        /* (a test: the play-out held whatever the points) */

static const LeaguePlayoffs *playoffs_of(uint8_t country)
{
    int32_t count = 0;
    const LeaguePlayoffs *all = World_LeaguePlayoffs(&count), *lp = 0;
    for (int32_t i = 0; i < count; i++)
        if (all[i].country == country)
            lp = &all[i];
    return lp;
}

/* A compiled spec's bytes, its closing 0 included (the library checks a spec against them,
 * sr_league.spec_len): walked as the library walks it, play-off by play-off; the world's specs are
 * this build's own data, so a walk is enough -- the library's check is the bound. 0: none. */
static uint32_t spec_bytes(const uint8_t *s)
{
    static const uint8_t *last;
    static uint32_t lastLen;
    uint32_t at = 0;
    if (!s)
        return 0;
    if (s == last)
        return lastLen;
    while (at < 4096 && s[at]) {
        const uint8_t *p = s + at;
        uint32_t r = at + 6 + 4u * p[1];
        if (p[0] != 7)                  /* (the promedio: no entrants, no rounds) */
            for (int32_t k = 0; k < p[2] && r < 4096; k++)
                r += 6 + 2u * s[r];
        at = r;
    }
    last = s;
    lastLen = at + 1;
    return lastLen;
}

/* The world's league -> the library's: its rules, and the promedio's seed as global numbers. */
static sr_promedio_seed s_seed[PROMEDIO_CLUBS];
static void league_of(const LeaguePlayoffs *lp, uint8_t country, sr_league *lg)
{
    int32_t k = 0;
    const WorldPromedio *t = World_Promedio(country, &k);
    lg->divisions = lp->divisions;
    lg->promoted = lp->promoted;
    lg->relegated = lp->relegated;
    lg->italy = lp->italy;
    lg->serie_b = lp->serieB;
    lg->group_first = lp->groupFirst;
    lg->groups = lp->groups;
    lg->spec = lp->spec;
    lg->spec_len = spec_bytes(lp->spec);
    lg->second_team = 0;                /* (the world's rows carry their global numbers) */
    lg->team_file_high = 1;             /* (our team words: PACK_BE_U16(file, ordinal) -- a spec's club, its
                                           (file, ordinal) bytes, made into one; the Mod's are file | ordinal << 8) */
    lg->promedio_seeds = 0;
    for (int32_t i = 0; t && i < k && i < PROMEDIO_CLUBS; i++) {
        sr_promedio_seed *x = &s_seed[lg->promedio_seeds++];
        x->glob = (u16)(_teamsCountryNumbers[country] + t[i].ordinal);
        x->points2 = t[i].points2; x->matches2 = t[i].matches2;
        x->points1 = t[i].points1; x->matches1 = t[i].matches1;
    }
    lg->promedio_seed = s_seed;
    lg->seconds = lp->seconds;
    lg->second = lp->second;
    lg->second_top = lp->secondTop;
    lg->second_flags = lp->secondFlags;
}

/* A level per group level (Serie C's three groups one level) -> the library's levels. */
static uint8_t s_first[LEAGUE_GROUPS_MAX_LEVELS];
static const sr_levels *levels_of(const LeagueGroups *lg, sr_levels *lv)
{
    if (!lg)
        return 0;
    lv->levels = lg->levels;
    for (int32_t l = 0; l < lg->levels && l < LEAGUE_GROUPS_MAX_LEVELS; l++)
        s_first[l] = lg->level[l].first;
    lv->first = s_first;
    return lv;
}

/* What the career club played itself (world_playoff.c), and the log. */
static int32_t host_played_tie(void *ctx, int32_t pidx, uint16_t a, uint16_t b, uint16_t *w)
{
    (void)ctx;
    return WorldPlayoff_PlayedTie(pidx, a, b, w);
}
static int32_t host_played_title(void *ctx, int32_t pidx, uint16_t *w, uint16_t *l)
{
    (void)ctx;
    return WorldPlayoff_PlayedTitle(pidx, w, l);
}
static int32_t host_stage_title(void *ctx, int32_t k, uint16_t team[2], uint16_t glob[2])
{
    const WorldStageTitle *x = WorldPlayoff_StageTitle(k);
    (void)ctx;
    if (!x)
        return 0;
    team[0] = x->team[0]; team[1] = x->team[1];
    glob[0] = x->glob[0]; glob[1] = x->glob[1];
    return 1;
}
#if defined(__m68k__) || defined(__amigaos__)
#define HOST_LOG 0                      /* (SWOS_DBG: nothing on the Amiga) */
#else
#include <stdarg.h>
static void host_log(void *ctx, const char *fmt, ...)
{
    va_list ap;
    (void)ctx;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}
#define HOST_LOG host_log
#endif
static const sr_host s_host = { 0, host_played_tie, host_played_title, host_stage_title, HOST_LOG };

/* The season's inputs as the game has them: the career club (the selected team), its league's
 * stage tables, the promedio, the winners page; its B teams passed over as play-off entrants
 * (the Mod's place_club: the country's global numbers and levels). */
static sr_levels s_seasonLevels;
static void season_of(uint8_t country, sr_season *se)
{
    extern uint16_t _selTeamNumber;
    uint8_t *p = (uint8_t *)se;
    for (uint32_t i = 0; i < sizeof *se; i++)
        p[i] = 0;
    se->test_held = (uint8_t)(LeaguePlayoffs_TestHeld != 0);
    se->mgr = _selTeamNumber;
    se->bank = bank_of(country, 0);
    se->promedio = &g_promedio;
    se->winners = &g_leagueWinners;
    se->host = &s_host;
    se->b_in_ko = 1;
    se->base = _teamsCountryNumbers[country];
    se->levels = levels_of(LeagueGroups_ForCountry(country), &s_seasonLevels);
}

int32_t LeaguePlayoffs_OutHeld(uint16_t pts16, uint16_t pts17)
{
    return sr_out_held(pts16, pts17) || LeaguePlayoffs_TestHeld;
}

int32_t LeaguePlayoffs_PlayoffHeld(uint16_t pts3, uint16_t pts4)
{
    return sr_playoff_held(pts3, pts4) || LeaguePlayoffs_TestHeld;
}

/* The season end's top division of the country just captured: its final table's team words
 * (the other confederations' cups, conti_season.c). */
int32_t LeaguePlayoffs_TopTable(uint16_t *teams, int32_t cap)
{
    int32_t n = s_in.nd ? s_in.n[0] : 0;
    if (n > cap)
        n = cap;
    for (int32_t k = 0; k < n; k++)
        teams[k] = s_in.t[0][k].team;
    return n;
}

/* Play-off pidx of the country's spec (its bytes), 0: none. */
const uint8_t *LeaguePlayoffs_SpecAt(uint8_t country, int32_t pidx)
{
    const LeaguePlayoffs *lp = playoffs_of(country);
    sr_league lg;
    if (!lp)
        return 0;
    league_of(lp, country, &lg);
    return sr_spec_at(&lg, pidx);
}

/* ---- The career club's own play-off (world_playoff.c): the library's ko_build, gp_need ---- */

int32_t LeaguePlayoffs_ContestEntrantsEx(uint8_t country, int32_t nt, const int32_t *divs,
                                         const LeagueStageRow *const *tables, const int32_t *ns, int32_t pidx,
                                         uint16_t *team, uint16_t *glob, uint8_t *prio, uint16_t *pts,
                                         const uint8_t **spec)
{
    const LeaguePlayoffs *lp = playoffs_of(country);
    sr_league lg;
    sr_season se;
    if (!lp)
        return 0;
    league_of(lp, country, &lg);
    season_of(country, &se);
    return sr_contest_entrants(&s_rules, &lg, &se, nt, divs, tables, ns, pidx, team, glob, prio, pts, spec);
}

int32_t LeaguePlayoffs_ContestEntrants(uint8_t country, int32_t d, const LeagueStageRow *table, int32_t n,
                                       int32_t pidx, uint16_t *team, uint16_t *glob, uint8_t *prio,
                                       uint16_t *pts, const uint8_t **spec)
{
    const LeagueStageRow *tables[1] = { table };
    const int32_t divs[1] = { d }, ns[1] = { n };
    return LeaguePlayoffs_ContestEntrantsEx(country, 1, divs, tables, ns, pidx, team, glob, prio, pts, spec);
}

int32_t LeaguePlayoffs_GroupShape(uint8_t country, int32_t pidx, LeagueGroupShape *shape)
{
    return sr_group_shape_of(LeaguePlayoffs_SpecAt(country, pidx), shape);
}

int32_t LeaguePlayoffs_ClubPlayoff(uint8_t country, int32_t d, const LeagueStageRow *table, int32_t n,
                                   uint16_t club, uint8_t *other, int32_t *nother)
{
    const LeaguePlayoffs *lp = playoffs_of(country);
    sr_league lg;
    sr_season se;
    *nother = 0;
    if (!lp)
        return -1;
    league_of(lp, country, &lg);
    season_of(country, &se);
    return sr_club_playoff(&s_rules, &lg, &se, d, table, n, club, other, nother);
}

void LeaguePlayoffs_ContestSeed(int32_t pidx, const uint16_t *team, int32_t n)
{
    sr_contest_seed(&s_rules, pidx, team, n);
}

int32_t LeaguePlayoffs_ContestTie(int32_t a, int32_t b, int32_t legs, int32_t host, int32_t level)
{
    return sr_contest_tie(&s_rules, a, b, legs, host, level);
}

/* =====================================================================
 * The decision: the Mod's final places, written as one-level moves
 * ===================================================================== */

static int32_t sign(int32_t x) { return x < 0 ? -1 : x > 0; }

void LeaguePlayoffs_Decide(uint16_t country, const int8_t *table, int8_t *scratch)
{
    const LeaguePlayoffs *lp = playoffs_of((uint8_t)country);
    sr_league lg;
    sr_season se;
    sr_levels lvs;
    if (!lp || s_in.nd != lp->divisions)
        return;
    for (int32_t d = 0; d < lp->divisions; d++)
        if (!s_in.n[d])
            return;                     /* (a division not played out this season) */
    /* every club's final place, before the moves: the next season's cups (world_cups.c) */
    for (int32_t d = 0; d < lp->divisions; d++)
        for (int32_t k = 0; k < s_in.n[d]; k++) {
            WorldCups_KeepPlace((uint8_t)country, (uint16_t)d, s_in.t[d][k].team, (uint8_t)(k + 1));
            CupD_KeepPlace((uint8_t)country, (uint16_t)d, s_in.t[d][k].team, k, s_in.n[d]);
        }

    league_of(lp, (uint8_t)country, &lg);
    season_of((uint8_t)country, &se);
    if (lp->italy) {                    /* the career club's play-off / play-out, played (world_playoff.c) */
        se.cup_winner = WorldCups_LeagueCupWinner((uint8_t)country);
        if (g_worldPlayoff.kind == WORLD_PLAYOFF_C_PLAYOFF)
            se.cpo_win = g_worldPlayoff.winner;
        if (g_worldPlayoff.kind == WORLD_PLAYOFF_B_PLAYOUT)
            se.out_win = g_worldPlayoff.winner;
        if (g_worldPlayoff.kind == WORLD_PLAYOFF_B_PLAYOFF)
            se.po_win = g_worldPlayoff.winner;
    }
    if (g_worldPlayoff.kind == WORLD_PLAYOFF_GROUP_TIE && g_worldPlayoff.winner) {
        se.tie_a = g_worldPlayoff.team[0];
        se.tie_b = g_worldPlayoff.team[1];
        se.tie_w = g_worldPlayoff.winner;
    }
    if (sr_italy(&s_rules, &lg, &se) < 0 || sr_knockouts(&s_rules, &lg, &se) < 0) {
        sr_check_error e;
        if (sr_league_check(&lg, &e) == SR_CHECK_OK)
            sr_season_check(&se, &e);
        SWOS_DBG("[playoffs] country %d: the rules refused the league (code %d, play-off %d, byte %u: %s)\n",
                (int)country, (int)e.code, (int)e.pidx, (unsigned)e.offset, e.field ? e.field : "?");
    }
    {   /* gp_champ: a 'title' play-off's champion first in the top division's top 9, the clubs
         * above it one place down -- the qualifiers read it as the champion (a winner outside
         * the top 9: the table's 1st stays) */
        extern uint8_t _primaryQualifiedTeamGrids[];
        extern uint16_t _cachedPrimaryQualifiedTeamIds[];
        static TeamFileEx tmp;
        for (int32_t k = 0; k < s_rules.gp.nres; k++) {
            const sr_result *x = &s_rules.gp.res[k];
            if (x->kind != SR_KIND_TITLE)
                continue;
            int32_t j = 0;
            while (j < 9 && BE_READ_U16(_primaryQualifiedTeamGrids + (uint32_t)j * sizeof(TeamFileEx)) != x->team[0])
                j++;
            if (j == 0 || j >= 9)
                continue;
            uint8_t *g = _primaryQualifiedTeamGrids;
            const uint16_t id = _cachedPrimaryQualifiedTeamIds[j];
            tmp = *(TeamFileEx *)(g + (uint32_t)j * sizeof(TeamFileEx));
            for (int32_t i = j; i > 0; i--) {
                *(TeamFileEx *)(g + (uint32_t)i * sizeof(TeamFileEx)) = *(TeamFileEx *)(g + (uint32_t)(i - 1) * sizeof(TeamFileEx));
                _cachedPrimaryQualifiedTeamIds[i] = _cachedPrimaryQualifiedTeamIds[i - 1];
            }
            *(TeamFileEx *)g = tmp;
            _cachedPrimaryQualifiedTeamIds[0] = id;
            SWOS_DBG("[playoffs] country %d: the title play-off's champion %04X (was %d.)\n", (int)country,
                     (unsigned)x->team[0], (int)j + 1);
        }
    }

    /* Each club's move as the Mod makes it: its record's by place, the play-offs' on top, the
     * B-team rules after. */
    const LeagueGroups *lgr = LeagueGroups_ForCountry(country);
    const int32_t lowest = lgr ? lgr->level[lgr->levels - 1].first : lp->divisions - 1;
    const u16 base = _teamsCountryNumbers[country], manager = (u16)_careerTeam.team.globalTeamNumber;
    for (int32_t i = 0; i < s_rules.nmv; i++)
        SWOS_DBG("[playoffs] global %u: %s by %s\n", (unsigned)s_rules.mv[i].glob,
                 s_rules.mv[i].delta < 0 ? "up" : "down", i < s_rules.dp_moves ? "the Italian rules" : "a knockout");
    sr_final_moves(&s_rules, &lg, levels_of(lgr, &lvs), base);

    /* A club whose move differs from the season's (scratch) gets the Mod's, one level; out
     * of the lowest division, below the league is the pool: the slot stays and takes the
     * next club of it (team_pool.c), never the manager's. */
    u8 out[TEAM_POOL_SLOTS / 8];
    int32_t career = 0x7FFF;
    for (int32_t i = 0; i < TEAM_POOL_SLOTS / 8; i++)
        out[i] = 0;
    for (int32_t d = 0; d < lp->divisions; d++)
        for (int32_t k = 0; k < s_in.n[d]; k++) {
            const u16 g = s_in.t[d][k].glob;
            int32_t want = s_rules.want[d][k];
            if (want > 0 && d >= lowest) {
                if (g != manager && g >= base && g - base < TEAM_POOL_SLOTS)
                    out[(g - base) >> 3] |= (u8)(1 << ((g - base) & 7));
                want = 0;
            }
            if (sign(scratch[g] - table[g]) != want)
                scratch[g] = (int8_t)(table[g] + want);
            if (g == manager)
                career = want;
        }
    for (int32_t o = 0; o < TEAM_POOL_SLOTS; o++)      /* (the slots in their order, as the Mod's) */
        if (out[o >> 3] & (1 << (o & 7))) {
            const int32_t in = TeamPool_Swap((u8)country, (u16)o);
            if (in >= 0) {
                LeagueGroups_Enter((u16)(base + o));
                CupD_NoteArrival((uint8_t)country, (uint16_t)o);    /* (the Coppa D's entry order) */
                SWOS_DBG("[pool] slot %d (global %u) -> identity %d\n", (int)o, (unsigned)(base + o), (int)in);
            }
        }
    TeamPool_SwapsDone();
    if (career != 0x7FFF) {
        _playPromotedAnimation = career < 0;
        _playRelegatedAnimation = career > 0;
    }
}
