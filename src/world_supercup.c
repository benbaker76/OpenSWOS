/*
 * world_supercup.c -- a world's Super Cups (include/world_supercup.h; docs/WORLD_2026_27.md 6h).
 * A port of the SWOS 96/97 Mod's tools/gplay.c sc_* / usc_* and tools/supercup.py (MIT, Davide
 * Lorigliola): his GP_SCT is World_SuperCups, his SC_ST g_worldSuperCup, his slot-4 ko_one the
 * one-tie contest below (as world_cupd.c's).
 */
#include "game_types.h"
#include "swos_types.h"
#include "world_supercup.h"
#include "world.h"
#include "world_cups.h"
#include "world_cupd.h"
#include "world_playoff.h"
#include "league_playoffs.h"
#include "diy_competition.h"
#include "career_teams.h"
#include "endian_helpers.h"

/* Prevent GCC from converting byte loops to memcpy/memset (no libc on the Amiga) */
#pragma GCC optimize("no-tree-loop-distribute-patterns")

extern TeamFileEx _careerTeam;
extern DIYCompetitionEx _diyCompetitionStart;
extern CompetitionSlotBuffer _diyCompetitionContextBuf;
extern CompetitionSlotBuffer _primaryCompetitionBuf;
extern uint8_t *_diyCompetitionPtr;
extern uint32_t *_diyCompetitionTablePtr;
extern uint16_t _competitionSlotUpdatedFlag;
extern uint16_t _teamSelectionKeyCacheCount;
extern uint8_t _teamSelectionKeyCache[];
extern uint16_t _gameType;
extern uint8_t _isNationalSelector;
extern Playoffs _div1Playoffs;
extern void Career_RetireSlotCalendarEntries(uint8_t slot);
extern void Career_SetCurrentCompetitionSlot(uint8_t *comp, uint16_t slot, uint16_t divisionIndex,
                                             uint16_t hasSelectedTeams);
extern void Career_LoadCompetitionSlotStateFromBuffer(uint8_t *slot);
extern void Career_StoreCompetitionSlotStateToBuffer(uint8_t *slot);
extern void DIY_BuildTeamSelectionPointerCache(void);
extern void DIY_UpdateCompetitionProgressOrAdvanceSlot(uint8_t *blk);

#include "comp_identity.h"     /* (COMPID_worldSuperCup: a world's own, fixed -- saves hold it) */

WorldSuperCupState g_worldSuperCup;
static CompetitionCup s_rec;

/* The Mod's generator (gplay.c rng / below). */
static uint32_t s_rng;
static uint32_t below(uint32_t n)
{
    s_rng = s_rng * 1103515245u + 12345u;
    return ((s_rng >> 16) * n) >> 16;
}

void SuperCup_Reset(void)
{
    uint8_t *p = (uint8_t *)&g_worldSuperCup;
    for (uint32_t i = 0; i < sizeof g_worldSuperCup; i++)
        p[i] = 0;
}

/* The season this is (world_cups.h's seasons + 1), and the Mod's season byte (GP_SEAS, its seeds). */
static uint8_t this_season(void) { return (uint8_t)(g_worldCups.seasons + 1); }
static uint32_t gp_seas(void) { return 126u + g_worldCups.seasons; }

static uint16_t career_team(void)
{
    return PACK_BE_U16(_careerTeam.team.countryNumber, _careerTeam.team.teamOrdinal);
}

static const WorldSuperCup *table(int32_t *count)
{
    *count = 0;
    if (!World_Current())
        return 0;
    const WorldSuperCup *t = World_SuperCups(count);
    if (*count > WORLD_SUPERCUPS_MAX)
        *count = WORLD_SUPERCUPS_MAX;
    return t;
}

/* A country's national Super Cup: its index, -1 none. */
static int32_t entry_of(uint8_t country)
{
    int32_t n = 0;
    const WorldSuperCup *t = table(&n);
    for (int32_t i = 0; i < n; i++)
        if (t[i].country == country)
            return i;
    return -1;
}

/* ---- The season end: what the next season's entrants come from ---- */

void SuperCup_TableEnded(uint8_t country)
{
    const int32_t i = entry_of(country);
    if (i < 0)
        return;
    WorldSuperCupKept *k = &g_worldSuperCup.kept[i];
    uint16_t words[WORLD_SUPERCUP_TABLE];
    const int32_t n = LeaguePlayoffs_TopTable(words, WORLD_SUPERCUP_TABLE);
    if (n < 2)
        return;                         /* (no table kept for it: nothing known) */
    for (int32_t j = 0; j < WORLD_SUPERCUP_TABLE; j++)
        k->table[j] = j < n ? words[j] : 0xFFFF;
    k->n = (uint8_t)n;
    k->season = this_season();
}

void SuperCup_CupEnded(uint8_t country, uint16_t winner, uint16_t finalist)
{
    const int32_t i = entry_of(country);
    if (i < 0)
        return;
    WorldSuperCupKept *k = &g_worldSuperCup.kept[i];
    k->cup[0] = winner;
    k->cup[1] = finalist;
    k->cupSeason = this_season();
}

void SuperCup_HoldersEnded(uint16_t cl, uint16_t el, uint16_t ecl)
{
    int32_t n = 0;
    if (!table(&n) || entry_of(WORLD_SUPERCUP_UEFA) < 0)
        return;
    g_worldSuperCup.hold[0] = cl;
    g_worldSuperCup.hold[1] = el;
    g_worldSuperCup.hold[2] = ecl;
    g_worldSuperCup.holdSeason = this_season();
}

/* ---- The entrants (gplay.c sc_rule) ---- */

/* Kept by the season just ended (the one before this). */
static int32_t last_season(uint8_t season)
{
    return season && season == g_worldCups.seasons;
}

static uint16_t place(const WorldSuperCupKept *k, int32_t p)     /* last season's club at place p, 0 none */
{
    return last_season(k->season) && p <= k->n && k->table[p - 1] != 0xFFFF ? k->table[p - 1] : 0;
}

static int32_t rank_of(int32_t e, uint16_t team)                 /* its place last season, 9 none (sc_rank) */
{
    const WorldSuperCupKept *k = &g_worldSuperCup.kept[e];
    if (!last_season(k->season))
        return 9;
    for (int32_t j = 0; j < k->n && j < WORLD_SUPERCUP_TABLE; j++)
        if (k->table[j] == team)
            return j + 1;
    return 9;
}

static uint16_t rule_club(const WorldSuperCup *sc, int32_t e, int32_t r)
{
    const WorldSuperCupKept *k = &g_worldSuperCup.kept[e];
    switch (r & 15) {
    case SC_CHAMPION:     return place(k, 1);
    case SC_SECOND:       return place(k, 2);
    case SC_THIRD:        return place(k, 3);
    case SC_FOURTH:       return place(k, 4);
    case SC_CUP_WINNER:
    case SC_CUP_FINALIST: {
        const uint16_t w = k->cup[(r & 15) == SC_CUP_FINALIST];
        return last_season(k->cupSeason) && w != 0xFFFF ? w : 0;
    }
    case SC_UEFA_CL:
    case SC_UEFA_EL:
    case SC_UEFA_ECL: {             /* (last season's holders, usc_snap's) */
        const uint16_t w = g_worldSuperCup.hold[(r & 15) - SC_UEFA_CL];
        return last_season(g_worldSuperCup.holdSeason) && w != 0xFFFF ? w : 0;
    }
    case SC_TITLE0:
    case SC_TITLE1: {               /* (Mexico: last season's stage titles -- the last season's winners) */
        const LeagueWinners *w = &g_leagueWinners;
        const uint8_t pi = sc->title[(r & 15) - SC_TITLE0];
        if (!last_season(w->season))
            return 0;
        for (int32_t i = 0; i < w->n && i < LEAGUE_WINNERS_MAX; i++)
            if (w->e[i].type == 'K' && w->e[i].idx == pi)
                return w->e[i].team;
        return 0;
    }
    }
    return 0;
}

/* The better placed of last season wins 3 in 5 (gplay.c: a semi-final or final nobody plays). */
static uint16_t decide(int32_t e, uint16_t a, uint16_t b, uint32_t seed)
{
    const int32_t ra = rank_of(e, a), rb = rank_of(e, b);
    s_rng = seed;
    return below(5) < 3 ? (ra <= rb ? a : b) : (ra <= rb ? b : a);
}

/* ---- The tie in slot 4 (as world_cupd.c's) ---- */

static void make_record(const WorldSuperCup *sc)
{
    const WorldSuperCupState *s = &g_worldSuperCup;
    uint8_t *z = (uint8_t *)&s_rec;
    for (uint32_t i = 0; i < sizeof s_rec; i++)
        z[i] = 0;
    s_rec.header.comp = _div1Playoffs.comp;
    s_rec.header.comp.compId = (uint8_t)COMPID_worldSuperCup;
    s_rec.header.comp.teamNumber = -1;     /* (no country before its name: the names carry it) */
    s_rec.header.comp.isPlayoff = 0;
    s_rec.header.comp.reserved1 = 15;
    s_rec.header.comp.reserved2 = 21;
    s_rec.header.comp.seedReorderFlag = 1;                      /* (the clubs as listed) */
    s_rec.header.comp.beginMonth = (int8_t)s->week;
    s_rec.header.comp.endMonth = (int8_t)s->week;
    s_rec.header.teams = 2;
    s_rec.header.awayGoalsMode = 0;
    s_rec.header.subsAndPointsForWin = _div1Playoffs.subsAndMaxSubs;
    s_rec.header.roundStringBase = 1;
    s_rec.roundRuleFlags[0] = (int8_t)(sc->level ? 0x14 : 0x04);   /* (one match; extra time? penalties) */
    s_rec.name[0] = sc->name[0];
    s_rec.name[1] = sc->name[1];
    s_rec.teamSeedList[0].countryNumber = (uint8_t)(s->teamA >> 8);
    s_rec.teamSeedList[0].teamOrdinal = (uint8_t)s->teamA;
    s_rec.teamSeedList[1].countryNumber = (uint8_t)(s->teamB >> 8);
    s_rec.teamSeedList[1].teamOrdinal = (uint8_t)s->teamB;
}

/* The contest under way's Super Cup (the UEFA one's or the national one's). */
static const WorldSuperCup *built_cup(void)
{
    int32_t n = 0;
    const WorldSuperCup *t = table(&n);
    const int32_t e = g_worldSuperCup.built == 2 ? entry_of(WORLD_SUPERCUP_UEFA) : g_worldSuperCup.entry;
    return t && e >= 0 && e < n ? &t[e] : 0;
}

static void build(const WorldSuperCup *sc, int32_t kind, uint16_t home, uint16_t away, int32_t final,
                  uint16_t floor)
{
    WorldSuperCupState *s = &g_worldSuperCup;
    const int32_t w = final ? sc->finalWeek : sc->week;
    s->built = (uint8_t)kind;
    s->final = (uint8_t)final;
    s->week = (uint8_t)(w >= floor ? w : floor);
    s->teamA = home;
    s->teamB = away;
    make_record(sc);
    _diyCompetitionTablePtr = (uint32_t *)0;
    Career_RetireSlotCalendarEntries(4);  /* (the slot's earlier contest's entries) */
    Career_SetCurrentCompetitionSlot((uint8_t *)&s_rec, 4, 0, 0);
    {
        uint8_t *ctx = (uint8_t *)&_diyCompetitionContextBuf;
        uint16_t ids[2];
        BE_WRITE_U16(&ids[0], s->teamA);
        BE_WRITE_U16(&ids[1], s->teamB);
        AppendTeamsToSelectionFromTmdOrTeamFile(((DIYCompetitionEx *)ctx)->cupTeamIds, ids, 2);
        Career_LoadCompetitionSlotStateFromBuffer(ctx);
        DIY_BuildTeamSelectionPointerCache();
        DIY_UpdateCompetitionProgressOrAdvanceSlot((uint8_t *)&_diyCompetitionStart);
        Career_StoreCompetitionSlotStateToBuffer(ctx);
        _competitionSlotUpdatedFlag = 1;
        _diyCompetitionContextBuf.tourn.header.slotStateA = _primaryCompetitionBuf.tourn.header.slotStateA;
        _diyCompetitionContextBuf.tourn.header.slotStateB = _primaryCompetitionBuf.tourn.header.slotStateB;
        _diyCompetitionContextBuf.tourn.header.slotStateC = _primaryCompetitionBuf.tourn.header.slotStateC;
    }
    SWOS_DBG("[supercup] %s%s: %04X v %04X, week %u\n", sc->name[0], final ? "" : " (semi-final)",
             (unsigned)home, (unsigned)away, (unsigned)s->week);
}

/* Slot 4 can take a tie: free, or holding a decided one (ours, the Coppa D's). */
static int32_t slot4_free(void)
{
    return !g_worldPlayoff.kind &&
           (!_diyCompetitionPtr || _diyCompetitionPtr == (uint8_t *)&s_rec || CupD_InSlot4() == 1) &&
           !g_worldSuperCup.built;
}

/* The UEFA Super Cup (gplay.c usc_step): 1 the career club's tie built, -1 it is pending, 0 nothing. */
static int32_t uefa_step(uint16_t floor)
{
    WorldSuperCupState *s = &g_worldSuperCup;
    int32_t n = 0;
    const WorldSuperCup *t = table(&n);
    const int32_t e = entry_of(WORLD_SUPERCUP_UEFA);
    if (e < 0)
        return 0;
    const WorldSuperCup *sc = &t[e];
    if (s->uSeason == this_season())
        return s->uState == 1 ? -1 : 0;
    if (!slot4_free())
        return 0;
    s->uSeason = this_season();
    s->uState = 3;
    s->uWon = 0;
    s->uWinner = 0;
    if (g_worldCups.seasons == 0 && sc->fromSeason > 1)
        return 0;                       /* (the world's first season: played before it) */
    uint16_t c[2];
    c[0] = rule_club(sc, e, sc->rule[0]);
    c[1] = rule_club(sc, e, sc->rule[1]);
#ifdef SWOS_SDL_BUILD
    if (getenv("SWOS_TEST_USC_FORCE") && c[1] != career_team())   /* (test: the career club the Champions */
        c[0] = career_team();                                       /*  League holder, the Mod's USC_FORCE) */
#endif
    if (c[1] && c[1] == c[0])
        c[1] = rule_club(sc, e, sc->rule[1] >> 4);      /* (one club holds both: the Conference League's) */
    if (!c[0] || !c[1] || c[0] == c[1]) {
        SWOS_DBG("[supercup] %s: no edition (%04X v %04X)\n", sc->name[0], (unsigned)c[0], (unsigned)c[1]);
        return 0;
    }
    if (c[0] != career_team() && c[1] != career_team())
        return 0;                       /* (not the career club's: neither played nor simulated) */
    build(sc, 2, c[0], c[1], 1, floor);
    s->uState = 1;
    return 1;
}

static void step(uint16_t week, uint16_t floor)
{
    WorldSuperCupState *s = &g_worldSuperCup;
    int32_t n = 0;
    const WorldSuperCup *t = table(&n);
    if (!t || _gameType != 4 || _isNationalSelector)
        return;
    {
        const int32_t u = uefa_step(floor);
        if (u)
            return;                     /* (the UEFA Super Cup first) */
    }
    const int32_t e = entry_of(World_FileCountry(_careerTeam.team.countryNumber));
    if (e < 0)
        return;
    const WorldSuperCup *sc = &t[e];
    const uint16_t me = career_team();
    if (s->season == this_season()) {   /* (handled: a final four's final still due) */
        if (s->round != 2 || (int32_t)week + 1 < sc->finalWeek || !slot4_free())
            return;
        build(sc, 1, me, s->other, 1, floor);
        s->round = 3;
        return;
    }
    if (((int32_t)week + 1 < sc->week && !sc->early) || !slot4_free())
        return;
    s->season = this_season();
    s->entry = (uint8_t)e;
    s->round = 0;
    s->won = 0;
    s->other = 0;
    s->winner = 0;
    if (g_worldCups.seasons == 0 && sc->fromSeason > 1)
        return;                         /* (a summer one: played before the world's first season) */
    const int32_t m = sc->n == 4 ? 4 : 2;
    uint16_t c[4];
    for (int32_t i = 0; i < m; i++) {
        if (g_worldCups.seasons == 0) {     /* (the world's first season: the real entrants) */
            c[i] = PACK_BE_U16(sc->first[i].countryNumber, sc->first[i].teamOrdinal);
            continue;
        }
        c[i] = rule_club(sc, e, sc->rule[i]);
        for (int32_t j = 0; j < i; j++)
            if (c[i] && c[i] == c[j])
                c[i] = rule_club(sc, e, sc->rule[i] >> 4);     /* (a double: the fallback) */
        for (int32_t j = 0; j < i; j++)
            if (c[i] == c[j])
                c[i] = 0;
    }
    for (int32_t i = 0; i < m; i++)
        if (!c[i]) {
            SWOS_DBG("[supercup] %s: its entrants not known\n", sc->name[0]);
            return;
        }
    int32_t mi = -1;
    for (int32_t i = 0; i < m; i++)
        if (c[i] == me)
            mi = i;
    if (mi < 0)
        return;                         /* (not the career club's: neither played nor simulated) */
    if (m == 4) {                       /* (the other semi-final, decided now) */
        const int32_t o = mi < 2 ? 2 : 0;
        s->other = decide(e, c[o], c[o + 1], gp_seas() * 7919u + sc->seed);
        s->round = 1;
        SWOS_DBG("[supercup] %s: the other semi-final %04X v %04X, won by %04X\n", sc->name[0],
                 (unsigned)c[o], (unsigned)c[o + 1], (unsigned)s->other);
    }
    build(sc, 1, c[mi & 2], c[(mi & 2) + 1], m == 2, floor);
}

void SuperCup_SeasonStart(void)
{
    if (!_teamSelectionKeyCacheCount)
        return;
    const uint8_t first = ((TeamSelectionKeyCacheEntry *)_teamSelectionKeyCache)->sortKey;
    step((uint16_t)(first ? first - 1 : 0), 0);
}

void SuperCup_Step(uint16_t week)
{
    step(week, (uint16_t)(week + 1));
}

void SuperCup_TieDecided(uint16_t team1, uint16_t team2, int32_t team2Won)
{
    WorldSuperCupState *s = &g_worldSuperCup;
    if (!s->built || (uint8_t)_diyCompetitionStart.base.field_2E != COMPID_worldSuperCup ||
        !((team1 == s->teamA && team2 == s->teamB) || (team1 == s->teamB && team2 == s->teamA)))
        return;
    const uint16_t w = team2Won ? team2 : team1, me = career_team();
    int32_t won = w == me;
#ifdef SWOS_SDL_BUILD
    {   /* (test: the career club wins (1) or loses (2) its Super Cup ties, the Mod's GPLAY_KOFORCE) */
        const char *f = getenv("SWOS_TEST_SUPERCUP_WIN");
        if (f && (f[0] == '1' || f[0] == '2'))
            won = f[0] == '1';
    }
#endif
    const uint16_t winner = won ? me : (team1 == me ? team2 : team1);
    const int32_t kind = s->built;
    s->built = 0;
    SWOS_DBG("[supercup] the career club's tie: won by %04X\n", (unsigned)winner);
    if (kind == 2) {                    /* (the UEFA Super Cup) */
        s->uState = 3;
        s->uWon = (uint8_t)won;
        s->uWinner = winner;
        return;
    }
    if (s->round == 1) {                /* (a final four's semi-final) */
        s->round = won ? 2 : 3;
        if (!won && s->other) {         /* (lost: the final simulated now, as the other semi-final) */
            int32_t n = 0;
            const WorldSuperCup *t = table(&n);
            const uint32_t seed = gp_seas() * 7919u + (t && s->entry < n ? t[s->entry].seed : 0) + 1;
            s->winner = decide(s->entry, winner, s->other, seed);
            SWOS_DBG("[supercup] the final simulated: %04X v %04X, won by %04X\n", (unsigned)winner,
                     (unsigned)s->other, (unsigned)s->winner);
        }
        return;
    }
    s->won = (uint8_t)won;              /* (the final, or the one match) */
    s->round = 3;
    s->winner = winner;
}

int32_t SuperCup_InSlot4(void)
{
    return _diyCompetitionPtr == (uint8_t *)&s_rec ? (g_worldSuperCup.built ? 2 : 1) : 0;
}

void SuperCup_RestoreSlot(void)
{
    const WorldSuperCup *sc = g_worldSuperCup.built ? built_cup() : 0;
    if (!sc)
        return;
    make_record(sc);
    _diyCompetitionPtr = (uint8_t *)&s_rec;
    _diyCompetitionTablePtr = (uint32_t *)0;
}

int32_t SuperCup_RoundStringIndex(uint8_t compId)
{
    if (compId != COMPID_worldSuperCup)
        return -1;
    return g_worldSuperCup.final ? 8 : 7;      /* (the final, a semi-final) */
}

const char *SuperCup_RecordLine(int32_t uefa)
{
    const WorldSuperCupState *s = &g_worldSuperCup;
    int32_t n = 0;
    const WorldSuperCup *t = table(&n);
    if (!t)
        return 0;
    if (uefa) {
        const int32_t e = entry_of(WORLD_SUPERCUP_UEFA);
        return e >= 0 && s->uSeason == this_season() && s->uState == 3 && s->uWon ? t[e].record : 0;
    }
    return s->season == this_season() && s->won && s->entry < n ? t[s->entry].record : 0;
}

const char *SuperCup_WinnersLine(uint16_t *winner)
{
    const WorldSuperCupState *s = &g_worldSuperCup;
    int32_t n = 0;
    const WorldSuperCup *t = table(&n);
    const int32_t e = entry_of(World_FileCountry(_careerTeam.team.countryNumber));
    if (!t || e < 0 || s->season != this_season() || s->entry != e)
        return 0;
    /* (the career club's tie built this season: its winner, or to be played while it waits) */
    if (!s->winner && !(s->built == 1) && s->round != 2)
        return 0;
    *winner = s->winner;
    return t[e].record;
}
