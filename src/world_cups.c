/*
 * world_cups.c -- a world's national cups as the SWOS 96/97 Mod's packs have them
 * (include/world_cups.h; docs/WORLD_2026_27.md, phase 4).
 */
#include "game_types.h"
#include "swos_types.h"
#include "world_cups.h"
#include "world_cupd.h"
#include "endian_helpers.h"
#include "league_groups.h"

extern void *_competitionsTable[];
extern DIYCompetitionEx _diyCompetitionStart;
extern TeamFileEx _careerTeam;
extern uint8_t _diyLeagueSeasonStateTable[];
extern uint16_t _teamsCountryNumbers[];
extern void Comp_Register(Competition *c);
extern void DIY_ShuffleTeamGridPairs(void);
extern uint16_t _internationalAutoAdvanceFlag;
extern void SaveSelectedTeams(void);
extern void RestoreSelectedTeams(void);
extern void Career_AutoAdvanceCupCompetitionFromRecord(uint8_t *record);

/* The world cup a record is (0: none, or 96/97's). */
static const WorldCup *cup_of(const uint8_t *record)
{
    int32_t count = 0;
    const WorldCup *cups = World_Cups(&count);
    for (int32_t i = 0; i < count; i++)
        if ((const uint8_t *)cups[i].record == record)
            return &cups[i];
    return 0;
}

/* The world cup under way in the DIY competition, by its id. */
static const WorldCup *cup_under_way(void)
{
    int32_t count = 0;
    const WorldCup *cups = World_Cups(&count);
    const uint8_t id = (uint8_t)_diyCompetitionStart.base.field_2E;
    for (int32_t i = 0; i < count; i++)
        if ((uint8_t)((const Competition *)cups[i].record)->compId == id)
            return &cups[i];
    return 0;
}

/* The round to come's ties, by club (the team file's ordinals), for the log. */
static void log_round(const char *what, int32_t ties)
{
    const DIYCompetitionEx *x = &_diyCompetitionStart;
    const uint8_t *grid = x->teamGridPairings;
    SWOS_DBG("[world-cup] %u %s:", (unsigned)(uint8_t)x->base.field_2E, what);
    for (int32_t k = 0; k < ties && k < 32; k++)
        SWOS_DBG(" %u-%u", (unsigned)(BE_READ_U16(&x->cupTeamIds[grid[2 * k]]) & 0xFF),
                 (unsigned)(BE_READ_U16(&x->cupTeamIds[grid[2 * k + 1]]) & 0xFF));
    SWOS_DBG("\n");
}

WorldCupsState g_worldCups;

void WorldCups_Reset(void)
{
    uint8_t *p = (uint8_t *)&g_worldCups;
    for (uint32_t i = 0; i < sizeof g_worldCups; i++)
        p[i] = 0;
}

void WorldCups_SeasonEnded(void)
{
    if (g_worldCups.seasons < 255)
        g_worldCups.seasons++;
}

/* The world's first season (to its season end's background cups): the real draw. */
static int32_t first_season(void)
{
    return g_worldCups.seasons == 0;
}

/* Each cup's real list (its record's, as installed: the first season's). */
#define WORLD_CUPS_MAX 128
#define WORLD_CUP_CLUBS 64
static TeamId s_real[WORLD_CUPS_MAX][WORLD_CUP_CLUBS];

static int32_t cup_index(const WorldCup *c)
{
    int32_t count = 0;
    const WorldCup *cups = World_Cups(&count);
    return (int32_t)(c - cups);
}

/* A division's level among the country's levels of groups (Serie C's three groups: one). */
static int32_t level_of(const LeagueGroups *lg, int32_t division)
{
    for (int32_t i = 0; lg && i < lg->levels; i++)
        if (division >= lg->level[i].first && division < lg->level[i].first + lg->level[i].groups)
            return i;
    return division;
}

/* This season's clubs of a cup (gplay.c cf_build, CF_SEED 1): 0 when it plays the real list
 * (the first season; a division with fewer clubs than its quota). */
static int32_t build(const WorldCup *c, TeamId *out)
{
    const LeagueGroups *lg = LeagueGroups_ForCountry(c->country);
    const int32_t ranked = g_worldCups.placesCountry == c->country &&
                           (uint8_t)(g_worldCups.placesSeason + 1) == g_worldCups.seasons;
    TeamId byes[WORLD_CUP_CLUBS];
    uint8_t cand[256];
    uint32_t key[256];
    int32_t at = 0, nb = 0;
    if (first_season() || !lg || !c->groups)
        return 0;
    for (int32_t g = 0; g < c->groups; g++) {
        const int32_t dv = c->quota[g][0], want = c->quota[g][1], wb = c->quota[g][2];
        const int32_t nl = level_of(lg, dv);
        int32_t m = 0;
        /* the division's clubs this season (its base, moved by the league's table), by ordinal */
        for (int32_t q = 0; q < lg->clubs; q++) {
            const LeagueGroupClub *k = &lg->club[q];
            const uint16_t glob = (uint16_t)(_teamsCountryNumbers[c->country] + k->ordinal);
            if (k->base + (int8_t)_diyLeagueSeasonStateTable[glob] == dv)
                cand[m++] = k->ordinal;
        }
        if (m < want)
            return 0;
        /* relegated into it, stayed, promoted, without a place; then the place, the ordinal */
        for (int32_t i = 0; i < m; i++) {
            const int32_t v = ranked ? g_worldCups.place[cand[i]] : 0;
            const int32_t r = v ? (v & 31) : 0xFF, ol = v ? v >> 5 : nl;
            const int32_t grp = !v ? 3 : ol < nl ? 0 : ol == nl ? 1 : 2;
            key[i] = (uint32_t)grp << 16 | (uint32_t)r << 8 | cand[i];
        }
        for (int32_t i = 1; i < m; i++)
            for (int32_t j = i; j > 0 && key[j - 1] > key[j]; j--) {
                const uint32_t kk = key[j]; key[j] = key[j - 1]; key[j - 1] = kk;
                const uint8_t x = cand[j]; cand[j] = cand[j - 1]; cand[j - 1] = x;
            }
        if (_careerTeam.team.countryNumber == c->country)       /* the career club first */
            for (int32_t i = 0; i < m; i++)
                if (cand[i] == _careerTeam.team.teamOrdinal) {
                    const uint8_t x = cand[i];
                    for (int32_t j = i; j > 0; j--) cand[j] = cand[j - 1];
                    cand[0] = x;
                    break;
                }
        for (int32_t k = 0; k < want; k++) {
            TeamId *t = k >= want - wb ? (nb < WORLD_CUP_CLUBS ? &byes[nb++] : 0) :
                                          (at < WORLD_CUP_CLUBS ? &out[at++] : 0);
            if (t) {
                t->countryNumber = c->country;
                t->teamOrdinal = cand[k];
            }
        }
    }
    for (int32_t k = 0; k < nb && at < WORLD_CUP_CLUBS; k++)
        out[at++] = byes[k];                    /* (the byes after round 1's clubs) */
    return at == c->n ? at : 0;
}

/* The cup's clubs this season, in its record's order (round 1's, then the byes). */
static const TeamId *entrants(const WorldCup *c)
{
    static TeamId list[WORLD_CUP_CLUBS];
    const int32_t i = cup_index(c);
    if (build(c, list))
        return list;
    return i < WORLD_CUPS_MAX ? s_real[i] : comp_team_seed_list((Competition *)c->record);
}

int32_t WorldCups_Plays(const uint8_t *record)
{
    const WorldCup *c = cup_of(record);
    if (!c)
        return 1;
    const TeamId *list = entrants(c);
    for (int32_t i = 0; i < c->n; i++)
        if (list[i].countryNumber == _careerTeam.team.countryNumber &&
            list[i].teamOrdinal == _careerTeam.team.teamOrdinal)
            return 1;
    return 0;
}

void WorldCups_KeepPlace(uint8_t country, uint16_t division, uint16_t team, uint8_t place)
{
    int32_t count = 0, ours = 0;
    const WorldCup *cups = World_Cups(&count);
    for (int32_t i = 0; i < count; i++)
        if (cups[i].country == country)
            ours = 1;
    if (!ours || (team >> 8) != country)
        return;
    if (g_worldCups.placesCountry != country || g_worldCups.placesSeason != g_worldCups.seasons) {
        for (int32_t i = 0; i < 256; i++)
            g_worldCups.place[i] = 0;
        g_worldCups.placesCountry = country;
        g_worldCups.placesSeason = g_worldCups.seasons;
    }
    g_worldCups.place[team & 0xFF] = (uint8_t)((place > 31 ? 31 : place) |
                                               level_of(LeagueGroups_ForCountry(country), division) << 5);
}

/* Each world country's own table (its 96/97 one has no room for a cup it lacked): its
 * leagues as they were, -2, its national cup, its league cup, -1. */
#define WORLD_CUP_COUNTRIES 96
static Competition *s_tables[WORLD_CUP_COUNTRIES][12];
static uint8_t s_tableCountry[WORLD_CUP_COUNTRIES];
static int32_t s_tables_n;

static Competition **own_table(uint8_t country)
{
    for (int32_t i = 0; i < s_tables_n; i++)
        if (s_tableCountry[i] == country)
            return s_tables[i];
    if (s_tables_n >= WORLD_CUP_COUNTRIES)
        return 0;
    Competition **old = (Competition **)_competitionsTable[country], **t = s_tables[s_tables_n];
    int32_t k = 0;
    while (old && (intptr_t)old[k] != -1 && k < 11) {
        t[k] = old[k];
        k++;
    }
    t[k] = (Competition *)(intptr_t)-1;
    s_tableCountry[s_tables_n++] = country;
    _competitionsTable[country] = t;
    return t;
}

void WorldCups_Install(void)
{
    int32_t count = 0;
    const WorldCup *cups = World_Cups(&count);
    for (int32_t i = 0; i < count; i++) {
        Competition **tbl = own_table(cups[i].country);
        int32_t sep = 0, end;
        if (!tbl)
            continue;
        while ((intptr_t)tbl[sep] != -2)
            sep++;
        for (end = sep + 1; (intptr_t)tbl[end] != -1; end++)
            ;
        /* [leagues, -2, national cup, league cup, -1] */
        if (sep + cups[i].slot >= end) {
            if (end < sep + cups[i].slot)
                continue;                       /* (a league cup without a national cup: none) */
            tbl[sep + cups[i].slot + 1] = (Competition *)(intptr_t)-1;
        }
        tbl[sep + cups[i].slot] = (Competition *)cups[i].record;
        Comp_Register((Competition *)cups[i].record);
        const TeamId *list = comp_team_seed_list((Competition *)cups[i].record);
        for (int32_t k = 0; i < WORLD_CUPS_MAX && k < cups[i].n && k < WORLD_CUP_CLUBS; k++)
            s_real[i][k] = list[k];
    }
}

uint16_t WorldCups_Entrants(const uint8_t *record, uint16_t count)
{
    const WorldCup *c = cup_of(record);
    if (!c)
        return count;
    /* this season's clubs into the record, where the loader reads them */
    const TeamId *list = entrants(c);
    TeamId *seeds = comp_team_seed_list((Competition *)record);
    SWOS_DBG("[world-cup] %u's clubs (season %u):", (unsigned)(uint8_t)((const Competition *)record)->compId,
             (unsigned)g_worldCups.seasons);
    for (int32_t k = 0; k < c->n; k++) {
        seeds[k] = list[k];
        SWOS_DBG(" %u", (unsigned)list[k].teamOrdinal);
    }
    SWOS_DBG("\n");
    return c->n;
}

void WorldCups_Padded(const uint8_t *record)
{
    const WorldCup *c = cup_of(record);
    uint16_t *ids = _diyCompetitionStart.cupTeamIds;
    if (!c)
        return;
    for (int32_t i = c->n; i < ((const CompCupHeader *)record)->teams; i++)
        ids[i] = ids[0];                        /* (BE pairs: copied as they are) */
}

void WorldCups_CupMade(const uint8_t *record)
{
    const WorldCup *c = cup_of(record);
    DIYCompetitionEx *x = &_diyCompetitionStart;
    uint8_t *grid = x->teamGridPairings;
    if (!c)
        return;
    if (!first_season()) {
        /* a later season's draw: round 1's clubs only (lib97 nc_bye), the byes wait */
        const int16_t pairs = x->base.matchPairsPerRound;
        x->base.matchPairsPerRound = c->ties;
        DIY_ShuffleTeamGridPairs();
        x->base.matchPairsPerRound = pairs;
    }
    if (c->ties * 2 == c->n)
        return;
    /* the real ties first, as listed; then each waiting club's tie with itself */
    const int32_t pairs = x->base.matchPairsPerRound;
    for (int32_t k = c->ties; k < pairs; k++) {
        int32_t e = 2 * c->ties + (k - c->ties);
        if (e >= c->n)
            e = c->n - 1;                       /* (a bracket wider than its clubs: never) */
        grid[2 * k] = grid[2 * k + 1] = (uint8_t)e;
    }
    x->base.totalMatchesInRound = c->ties;
    log_round("round 1", c->ties);
}

void WorldCups_ArrangeNextRound(void)
{
    const WorldCup *c = cup_under_way();
    DIYCompetitionEx *x = &_diyCompetitionStart;
    const int32_t played = x->base.currentRoundIndex;   /* (the round just played) */
    if (!c || !first_season() || played + 1 >= c->rounds)
        return;
    /* the draw undone: the winners as gathered, tie by tie (DIY_ReorderTeamGridByOrderList
     * put them in teamShuffleOrder's order) */
    uint8_t *grid = x->teamGridPairings, w[64];
    const int32_t n = x->base.matchPairsPerRound;
    for (int32_t i = 0; i < n && i < 64; i++)
        w[x->teamShuffleOrder[i]] = grid[i];
    for (int32_t i = 0; i < n && i < 64; i++)
        grid[i] = w[i];
    if (played == 0 && c->order1) {             /* (round 1's winners to their bracket places) */
        for (int32_t i = 0; i < n && i < 64; i++)
            w[c->order1[i]] = grid[i];
        for (int32_t i = 0; i < n && i < 64; i++)
            grid[i] = w[i];
    }
    const uint16_t flips = c->flips[played];
    for (int32_t k = 0; 2 * k + 1 < n; k++)
        if (flips & (1u << k)) {
            const uint8_t t = grid[2 * k];
            grid[2 * k] = grid[2 * k + 1];
            grid[2 * k + 1] = t;
        }
    log_round("the next round (the real bracket)", n / 2);
}

static int32_t lists(const WorldCup *c, uint16_t team)
{
    const TeamId *list = comp_team_seed_list((Competition *)c->record);
    for (int32_t i = 0; i < c->n; i++)
        if (PACK_BE_U16(list[i].countryNumber, list[i].teamOrdinal) == team)
            return 1;
    return 0;
}

void WorldCups_TieDecided(uint16_t team1, uint16_t team2, int32_t team2Won)
{
    const WorldCup *c = cup_under_way();
    const DIYCompetition *b = &_diyCompetitionStart.base;
    /* (the id alone is not enough: a later cup's state can keep it) */
    if (!c || c->slot != 2 || b->currentRoundIndex + 1 != b->numRounds ||
        !lists(c, team1) || !lists(c, team2))
        return;
    g_worldCups.leagueCupWinner = team2Won ? team2 : team1;
    g_worldCups.leagueCupSeason = g_worldCups.seasons;
    SWOS_DBG("[world-cup] %u won by %04X\n", (unsigned)(uint8_t)b->field_2E,
             (unsigned)g_worldCups.leagueCupWinner);
}

static const WorldCup *league_cup(uint8_t country)
{
    int32_t count = 0;
    const WorldCup *cups = World_Cups(&count);
    for (int32_t i = 0; i < count; i++)
        if (cups[i].country == country && cups[i].slot == 2)
            return &cups[i];
    return 0;
}

uint16_t WorldCups_LeagueCupWinner(uint8_t country)
{
    if (!league_cup(country) || g_worldCups.leagueCupSeason != g_worldCups.seasons)
        return 0;
    return g_worldCups.leagueCupWinner;
}

void WorldCups_PlayLeagueCupEarly(uint8_t country)
{
    const WorldCup *c = league_cup(country);
    if (!c || WorldCups_LeagueCupWinner(country) || WorldCups_Plays((const uint8_t *)c->record))
        return;
    /* as the season end will: the same seeded run (the selected teams parked, as for a
     * group's early season: world_playoff.c simulate_early) */
    _internationalAutoAdvanceFlag = 1;
    SaveSelectedTeams();
    Career_AutoAdvanceCupCompetitionFromRecord((uint8_t *)c->record);
    RestoreSelectedTeams();
    _internationalAutoAdvanceFlag = 0;
}

int32_t WorldCups_RoundStringIndex(uint8_t compId, uint16_t round, uint16_t rounds)
{
    const int32_t cupd = CupD_RoundStringIndex(compId, round);   /* (the Coppa Italia Serie D: world_cupd.c) */
    if (cupd >= 0)
        return cupd;
    int32_t count = 0, ours = 0;
    const WorldCup *cups = World_Cups(&count);
    for (int32_t i = 0; i < count; i++)
        if ((uint8_t)((const Competition *)cups[i].record)->compId == compId)
            ours = 1;
    if (!ours || round + 3 >= rounds)
        return -1;                              /* (the last three: the engine's names) */
    for (int32_t i = 0; i < count; i++)
        if ((uint8_t)((const Competition *)cups[i].record)->compId == compId && cups[i].numbered) {
            const int32_t k = round + cups[i].firstRound - 1;
            return k < 6 ? k : 5;               /* (FIRST .. SIXTH ROUND) */
        }
    if (round + 4 == rounds)
        return 12;                              /* ROUND OF 16 (_cupRoundStringTable) */
    return round;                               /* FIRST ROUND ... */
}

int32_t WorldCups_LeagueCupEurope(const uint8_t *record)
{
    const WorldCup *c = cup_of(record);
    return !c || c->europe;
}
