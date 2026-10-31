/*
 * world_cupd.c -- the Coppa Italia Serie D (include/world_cupd.h; docs/WORLD_2026_27.md,
 * phase 4). A port of the SWOS 96/97 Mod's tools/gplay.c cd_* (MIT, Davide Lorigliola).
 */
#include "game_types.h"
#include "swos_types.h"
#include "world_cupd.h"
#include "world_cups.h"
#include "team_pool.h"
#include "world_playoff.h"
#include "diy_competition.h"
#include "career_teams.h"
#include "career_status.h"
#include "endian_helpers.h"

extern uint8_t _diyLeagueSeasonStateTable[];
extern uint16_t _teamsCountryNumbers[];
extern TeamFileEx _careerTeam;
extern DIYCompetitionEx _diyCompetitionStart;
extern CompetitionSlotBuffer _diyCompetitionContextBuf;
extern CompetitionSlotBuffer _primaryCompetitionBuf;
extern uint8_t *_diyCompetitionPtr;
extern uint32_t *_diyCompetitionTablePtr;
extern uint16_t _competitionSlotUpdatedFlag;
extern uint16_t _teamSelectionKeyCacheCount;
extern uint8_t _teamSelectionKeyCache[];
extern Playoffs _div1Playoffs;
extern void Career_RetireSlotCalendarEntries(uint8_t slot);
extern void Career_SetCurrentCompetitionSlot(uint8_t *comp, uint16_t slot, uint16_t divisionIndex,
                                             uint16_t hasSelectedTeams);
extern void Career_LoadCompetitionSlotStateFromBuffer(uint8_t *slot);
extern void Career_StoreCompetitionSlotStateToBuffer(uint8_t *slot);
extern void DIY_BuildTeamSelectionPointerCache(void);
extern void DIY_UpdateCompetitionProgressOrAdvanceSlot(uint8_t *blk);

/* The tie's competition id: a free one (comp_identity.h: 162..255; the play-off has 200). */
#define COMPID_worldCupD 201
#define COMPID_worldCupD64 202          /* (the slot-2 cup from the round of 64) */

static CompetitionCup s_rec;
static struct __attribute__((packed)) {
    CompetitionCup rec;
    TeamId more[32];                    /* (its list past the record's 32: 64 clubs) */
} s_cup64;
extern uint8_t *_tertiaryCompetitionPtr;
extern CompetitionSlotBuffer _tertiaryCompetitionContextBuf;
extern uint8_t *GetCurrentSeasonPointer(void);

WorldCupDState g_worldCupD;

#define CD_REF(i) ((uint16_t)((i) << 8 | 0xFF))     /* (a tie as a side: a file is never 255) */
#define CD_ISREF(v) (((v) & 0xFF) == 0xFF)
#define CD_DRAWN 0xFEFF

/* The Mod's generator (gplay.c rng / below / cd_below). */
static uint32_t s_rng;
static uint32_t below(uint32_t n)
{
    s_rng = s_rng * 1103515245u + 12345u;
    return ((s_rng >> 16) * n) >> 16;
}
static uint32_t cd_below(uint32_t n)
{
    s_rng = s_rng * 1103515245u + 12345u;
    return (uint32_t)(((uint64_t)(s_rng >> 16) * n) >> 16);
}

void CupD_Reset(void)
{
    uint8_t *p = (uint8_t *)&g_worldCupD;
    for (uint32_t i = 0; i < sizeof g_worldCupD; i++)
        p[i] = 0;
}

/* The Mod's season byte (GP_SEAS: the year from 1900) -- its generator's seeds. */
static uint32_t gp_seas(void)
{
    return 126u + g_worldCups.seasons;
}

static int32_t first_season(void)
{
    return g_worldCups.seasons == 0;
}

/* The season's rounds: the table's (the first season), else generated (gplay.c cd_gen). */
static int32_t cd_gen(void)
{
    return !first_season() && g_worldCupD.gen;
}

static int32_t cd_n(const WorldCupD *c, int32_t r)
{
    if (!cd_gen())
        return c->round[r][4];
    return r ? 1 << (c->rounds - 1 - r) : g_worldCupD.np;
}

static int32_t cd_first(const WorldCupD *c, int32_t r)
{
    if (!cd_gen())
        return c->round[r][6] | c->round[r][7] << 8;
    int32_t f = 0;
    for (int32_t i = 0; i < r; i++)
        f += cd_n(c, i);
    return f;
}

static int32_t cd_ties(const WorldCupD *c)
{
    return cd_gen() ? cd_first(c, c->rounds) : c->ties;
}

static uint16_t cd_raw(const WorldCupD *c, int32_t t, int32_t sd)
{
    if (!cd_gen())
        return (uint16_t)(c->tie[t][2 * sd] | c->tie[t][2 * sd + 1] << 8);
    int32_t r = 0;
    while (r + 1 < c->rounds && t >= cd_first(c, r + 1))
        r++;
    if (r < 2)
        return g_worldCupD.side[2 * t + sd];
    if (c->round[r][5] & 1)
        return CD_DRAWN;
    return CD_REF(cd_first(c, r - 1) + 2 * (t - cd_first(c, r)) + sd);
}

/* tie t's side sd (0 a, 1 b): a team word, 0 (off-file), FFFFh undecided */
static uint16_t cd_side(const WorldCupD *c, int32_t t, int32_t sd)
{
    const uint16_t v = cd_raw(c, t, sd);
    if (!CD_ISREF(v))
        return v;
    int32_t i = v >> 8;
    if (v == CD_DRAWN) {                /* (a drawn round: its k-th tie = the previous round's sd[2k], sd[2k + 1]) */
        /* (from ITS round's first tie: gplay.c takes the round under way's, so once the next
         * round is, a drawn round's winners are read below sd -- the end of the hosts array,
         * zeros: the preliminary round's first tie -- and the final is played by its clubs) */
        int32_t r = 0;
        while (r + 1 < c->rounds && t >= cd_first(c, r + 1))
            r++;
        i = g_worldCupD.sd[2 * (t - cd_first(c, r)) + sd];
    }
    if (i >= cd_ties(c) || !g_worldCupD.res[i])
        return 0xFFFF;
    return cd_side(c, i, g_worldCupD.res[i] - 1);
}

static int32_t cd_hosted(const WorldCupD *c, int32_t t, uint16_t club)
{
    return cd_side(c, t, g_worldCupD.home[t]) == club;
}

/* A team word's slot in the country (include/world.h), and a slot's team word. */
static uint16_t cd_slot(uint16_t w)
{
    return World_Slot((uint8_t)w, (uint8_t)(w >> 8));
}

static uint16_t cd_word(uint8_t country, uint16_t slot)
{
    const uint16_t t = World_SlotTeam(country, slot);          /* (file << 8 | ordinal) */
    return (uint16_t)(t >> 8 | (t & 0xFF) << 8);
}

/* A club's strength: the best 11's prices of the identity its slot has (team_pool.h),
 * squared (gplay.c cd_str). */
static uint32_t cd_str(const WorldCupD *c, uint16_t w)
{
    const int32_t i = cd_slot(w);
    int32_t id = i;
    const TeamPoolState *pool = TeamPool_State(c->country);
    if (pool && pool->map[i])
        id = pool->map[i] - 1;
    uint32_t v = id < TEAM_POOL_SLOTS ? c->strength[id]
               : id - TEAM_POOL_SLOTS < c->pool ? c->poolStrength[id - TEAM_POOL_SLOTS] : 5;
    if (v < 5)
        v = 5;
    return v * v;
}

/* A tie nobody plays, by strength (the Mod's statesim): h 0 a hosts (the first leg), 1 b;
 * 1 a through, 2 b. */
static int32_t cd_sim(const WorldCupD *c, uint16_t a, uint16_t b, int32_t legs, int32_t h)
{
    const uint32_t sa = cd_str(c, a), sb = cd_str(c, b);
    int32_t ga = 0, gb = 0;
    for (int32_t l = 0; l < legs; l++) {
        const int32_t homeA = (l == 0) == (h == 0);
        const uint32_t x = homeA ? sa * 121 / 100 : sa, y = homeA ? sb : sb * 121 / 100;
        int32_t p, q;
        if (cd_below(100) < 26) {
            p = (int32_t)cd_below(3);
            q = p;
        } else {
            const int32_t win = cd_below(x + y) < x, gw = 1 + (int32_t)cd_below(3), gl = (int32_t)cd_below((uint32_t)gw);
            p = win ? gw : gl;
            q = win ? gl : gw;
        }
        ga += p;
        gb += q;
    }
    if (ga != gb)
        return ga > gb ? 1 : 2;
    return cd_below(3 * (sa + sb)) < 2 * sa + sb ? 1 : 2;
}

/* The career club as the cup's team words are. */
static uint16_t career_word(void)
{
    return (uint16_t)(_careerTeam.team.countryNumber | _careerTeam.team.teamOrdinal << 8);
}

/* Tie t's host: 0 a, 1 b -- listed, drawn, or alternate (the club away in its previous tie
 * hosts; both home / both away: drawn), as gplay.c cd_step_c (the generator seeded). */
static int32_t host_of(const WorldCupD *c, int32_t t, int32_t host, uint16_t a, uint16_t b)
{
    const uint16_t ra = cd_raw(c, t, 0), rb = cd_raw(c, t, 1);
    if (host == 2)
        return (int32_t)below(2);
    if (host == 4) {
        const int32_t pa = CD_ISREF(ra) && ra != CD_DRAWN ? ra >> 8 : -1;
        const int32_t pb = CD_ISREF(rb) && rb != CD_DRAWN ? rb >> 8 : -1;
        const int32_t ha = pa >= 0 && pa < cd_ties(c) && cd_hosted(c, pa, a);
        const int32_t hb = pb >= 0 && pb < cd_ties(c) && cd_hosted(c, pb, b);
        return ha && !hb ? 1 : !ha && hb ? 0 : (int32_t)below(2);
    }
    return 0;
}

/* The earliest week a tie built now can be played (the next entry's; at the season start
 * any: the rounds before its first entry keep their weeks). */
static uint16_t s_floor;

/* The tie's record (as the play-off's, world_playoff.c): one tie, its weeks, its legs; level:
 * penalties, no extra time (the Mod's LV_PENS). */
static void make_record(const WorldCupD *c)
{
    const WorldCupDState *s = &g_worldCupD;
    uint8_t *z = (uint8_t *)&s_rec;
    for (uint32_t i = 0; i < sizeof s_rec; i++)
        z[i] = 0;
    s_rec.header.comp = _div1Playoffs.comp;
    s_rec.header.comp.compId = (uint8_t)COMPID_worldCupD;
    s_rec.header.comp.teamNumber = (int8_t)c->country;          /* ("ITALIAN ...") */
    s_rec.header.comp.isPlayoff = 0;
    s_rec.header.comp.reserved1 = 15;
    s_rec.header.comp.reserved2 = 21;
    s_rec.header.comp.seedReorderFlag = 1;                      /* (the clubs as listed) */
    s_rec.header.comp.beginMonth = (int8_t)s->d1;
    s_rec.header.comp.endMonth = (int8_t)(s->legs == 2 ? s->d2 : s->d1);
    s_rec.header.teams = 2;
    s_rec.header.awayGoalsMode = 0;
    s_rec.header.subsAndPointsForWin = _div1Playoffs.subsAndMaxSubs;
    s_rec.header.roundStringBase = 1;
    s_rec.roundRuleFlags[0] = (int8_t)(s->legs == 2 ? 0x84 : 0x04);
    s_rec.name[0] = c->name[0];
    s_rec.name[1] = c->name[1];
    s_rec.teamSeedList[0].countryNumber = (uint8_t)(s->teamA >> 8);
    s_rec.teamSeedList[0].teamOrdinal = (uint8_t)s->teamA;
    s_rec.teamSeedList[1].countryNumber = (uint8_t)(s->teamB >> 8);
    s_rec.teamSeedList[1].teamOrdinal = (uint8_t)s->teamB;
}

/* The career club's tie t into slot 4 (the play-off's steps, world_playoff.c). */
static void build_tie(const WorldCupD *c, int32_t t, uint16_t a, uint16_t b, uint16_t week)
{
    WorldCupDState *s = &g_worldCupD;
    const uint8_t *rec = c->round[g_worldCupD.round];
    const int32_t w1 = rec[0], w2 = rec[1];
    const uint16_t home = s->home[t] ? b : a, away = s->home[t] ? a : b;
    s->built = 1;
    s->bt = (uint8_t)t;
    s->legs = rec[2];
    (void)week;
    s->d1 = (uint8_t)(w1 >= s_floor ? w1 : s_floor);
    s->d2 = (uint8_t)(s->legs == 2 ? (w2 > s->d1 ? w2 : s->d1 + 1) : s->d1);
    s->teamA = (uint16_t)((home & 0xFF) << 8 | home >> 8);       /* (team words -> {country, ordinal}) */
    s->teamB = (uint16_t)((away & 0xFF) << 8 | away >> 8);
    make_record(c);
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
    SWOS_DBG("[world-cupd] round %d tie %d, the career club's: %04X v %04X, weeks %u-%u\n",
             (int)g_worldCupD.round, (int)t, (unsigned)s->teamA, (unsigned)s->teamB,
             (unsigned)s->d1, (unsigned)s->d2);
}

/* Slot 4 can take the tie: free, or the cup's own last tie (decided). */
static int32_t slot4_free(void)
{
    return !g_worldPlayoff.kind && (!_diyCompetitionPtr || _diyCompetitionPtr == (uint8_t *)&s_rec);
}

/* The season end's tables and the pool's new arrivals (the season start's entry order: in
 * one session with it, so not saved -- as the Mod's GP_IN). */
static uint8_t s_place[WORLD_SLOTS];    /* a Serie D club's place from the bottom of its group + 2 */
static uint8_t s_lastDiv[WORLD_SLOTS];  /* every club's division (FFh: not seen) */
static uint8_t s_arrival[WORLD_SLOTS / 8];   /* (by slot) */
static uint8_t s_ranked;                /* the tables kept (the season they are of + 1) */

void CupD_KeepPlace(uint8_t country, uint16_t division, uint16_t team, int32_t place, int32_t clubs)
{
    const WorldCupD *c = World_CupD();
    if (!c || country != c->country || World_FileCountry((uint8_t)(team >> 8)) != country)
        return;
    if (s_ranked != (uint8_t)(g_worldCups.seasons + 1)) {
        for (int32_t i = 0; i < WORLD_SLOTS; i++) {
            s_place[i] = 0;
            s_lastDiv[i] = 0xFF;
        }
        for (int32_t i = 0; i < WORLD_SLOTS / 8; i++)
            s_arrival[i] = 0;
        s_ranked = (uint8_t)(g_worldCups.seasons + 1);
    }
    const uint16_t o = World_Slot((uint8_t)(team >> 8), (uint8_t)team);
    s_lastDiv[o] = (uint8_t)division;
    if (division >= c->first && division < c->first + c->groups)
        s_place[o] = (uint8_t)(2 + (clubs - 1 - place));        /* (place 0 the top) */
}

void CupD_NoteArrival(uint8_t country, uint16_t ordinal)
{
    const WorldCupD *c = World_CupD();
    if (c && country == c->country && s_ranked == (uint8_t)(g_worldCups.seasons + 1))
        s_arrival[ordinal >> 3] |= (uint8_t)(1 << (ordinal & 7));
}

/* m sides -> ties from side[at]: shuffled, each paired with the next of its group, else the
 * next (gplay.c cd_pair). */
static void cd_pair(uint16_t *side, uint8_t *sg, int32_t m, int32_t at)
{
    for (int32_t i = m - 1; i > 0; i--) {
        const int32_t j = (int32_t)below((uint32_t)i + 1);
        const uint16_t x = side[i]; const uint8_t y = sg[i];
        side[i] = side[j]; side[j] = x; sg[i] = sg[j]; sg[j] = y;
    }
    uint8_t used[256];
    for (int32_t i = 0; i < m; i++)
        used[i] = 0;
    for (int32_t i = 0; i < m; i++) {
        if (used[i])
            continue;
        int32_t j = -1;
        for (int32_t k = i + 1; k < m && j < 0; k++)
            if (!used[k] && sg[k] != 0xFF && sg[k] == sg[i]) j = k;
        for (int32_t k = i + 1; k < m && j < 0; k++)
            if (!used[k]) j = k;
        used[i] = 1;
        g_worldCupD.side[at++] = side[i];
        if (j >= 0) { used[j] = 1; g_worldCupD.side[at++] = side[j]; } else g_worldCupD.side[at++] = 0;
    }
}

/* A later season's rounds 0-1 (gplay.c cd_season's keys, then cd_make). */
static void cd_make(const WorldCupD *c)
{
    WorldCupDState *s = &g_worldCupD;
    const int32_t df = c->first, dc = c->groups, lf = c->country;
    const int32_t fresh = s_ranked == g_worldCups.seasons;      /* (the last season end's tables) */
    uint16_t ord[WORLD_SLOTS];
    uint8_t grp[WORLD_SLOTS], sg[256], pg[WORLD_CUPD_PRE];
    uint16_t key[WORLD_SLOTS], side[256];
    int32_t n = 0;
    s_rng = gp_seas() * 104729u + 17;
    for (int32_t i = 0; i < WORLD_SLOTS; i++) {
        const int32_t b = c->base[i];
        if (b == 0xFF)
            continue;
        const int32_t dv = b + (int8_t)_diyLeagueSeasonStateTable[_teamsCountryNumbers[lf] + i];
        if (dv < df || dv >= df + dc)
            continue;
        int32_t e = 0x40;                                       /* (unknown) */
        if (fresh) {
            const int32_t od = s_lastDiv[i];
            if (s_arrival[i >> 3] & (1 << (i & 7)))
                e = 1;                                          /* (a pool club's identity) */
            else if (od == 0xFF)
                e = 0x40;                                       /* (in no table: unknown) */
            else if (od < df || od >= df + dc)
                e = 1;                                          /* (relegated from Serie C) */
            else if (s_place[i] >= 2)
                e = s_place[i];                                 /* (from the bottom of its group) */
        }
        ord[n] = (uint16_t)i;
        grp[n] = (uint8_t)(dv - df);
        key[n] = (uint16_t)(e << 8 | below(256));
        n++;
    }
    for (int32_t i = 1; i < n; i++)                             /* (the entry order: by key) */
        for (int32_t j = i; j > 0 && key[j - 1] > key[j]; j--) {
            uint16_t k = key[j]; key[j] = key[j - 1]; key[j - 1] = k;
            k = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = k;
            const uint8_t o = grp[j]; grp[j] = grp[j - 1]; grp[j - 1] = o;
        }
    const int32_t n1 = 1 << (c->rounds - 2);
    int32_t p = n - 2 * n1, m = 0;
    if (p < 0) p = 0;
    if (p > WORLD_CUPD_PRE) p = WORLD_CUPD_PRE;
    s->np = (uint8_t)p;
    for (int32_t k = 0; k < 2 * p; k++) { side[m] = cd_word((uint8_t)lf, ord[k]); sg[m++] = grp[k]; }
    cd_pair(side, sg, m, 0);
    for (int32_t t = 0; t < p; t++) {                           /* (a preliminary tie's group: its first side's) */
        const int32_t o = cd_slot(s->side[2 * t]);
        pg[t] = 0xFF;
        for (int32_t k = 0; k < 2 * p; k++) if (ord[k] == o) pg[t] = grp[k];
    }
    m = 0;
    for (int32_t t = 0; t < p; t++) { side[m] = CD_REF(t); sg[m++] = pg[t]; }
    for (int32_t k = 2 * p; k < n && m < 2 * n1; k++) { side[m] = cd_word((uint8_t)lf, ord[k]); sg[m++] = grp[k]; }
    while (m < 2 * n1) { side[m] = 0; sg[m++] = 0xFF; }
    cd_pair(side, sg, m, 2 * p);
    s->gen = 1;
    SWOS_DBG("[world-cupd] season %u: %d Serie D clubs, %d preliminary ties%s\n", (unsigned)gp_seas(),
             (int)n, (int)p, fresh ? "" : " (no tables kept: random entry order)");
}

/* A round's generator and, for a drawn round, its draw (as step's: gplay.c cd_step_c). */
static void begin_round(const WorldCupD *c, int32_t r)
{
    WorldCupDState *s = &g_worldCupD;
    s_rng = gp_seas() * 7919u + (uint32_t)r * 131u + (uint32_t)cd_first(c, r);
    if ((c->round[r][5] & 1) && s->drawn != r + 1) {
        const int32_t pf = cd_first(c, r - 1), pn = cd_n(c, r - 1);
        for (int32_t i = 0; i < 4 && i < pn; i++)
            s->sd[i] = (uint8_t)(pf + i);
        for (int32_t i = (pn < 4 ? pn : 4) - 1; i > 0; i--) {
            const int32_t j = (int32_t)below((uint32_t)i + 1);
            const uint8_t x = s->sd[i];
            s->sd[i] = s->sd[j];
            s->sd[j] = x;
        }
        s->drawn = (uint8_t)(r + 1);
    }
}

/* The slot-2 cup's record: its rounds from r64, their legs, the weeks from its first to the
 * final's; its 64 clubs, round r64's ties in order, home first (gplay.py cds1_src). */
static int32_t make_cup64(const WorldCupD *c, int32_t r64)
{
    const WorldCupDState *s = &g_worldCupD;
    const int32_t first = cd_first(c, r64);
    uint8_t *z = (uint8_t *)&s_cup64;
    for (uint32_t i = 0; i < sizeof s_cup64; i++)
        z[i] = 0;
    for (int32_t q = 0; q < 32; q++) {
        const int32_t t = first + q;
        const uint16_t a = cd_side(c, t, 0), b = cd_side(c, t, 1);
        if (!a || !b || a == 0xFFFF || b == 0xFFFF)
            return 0;                                   /* (a club outside the file, or not known) */
        const uint16_t home = s->home[t] ? b : a, away = s->home[t] ? a : b;
        TeamId *ids = (TeamId *)s_cup64.rec.teamSeedList;
        ids[2 * q].countryNumber = (uint8_t)home;
        ids[2 * q].teamOrdinal = (uint8_t)(home >> 8);
        ids[2 * q + 1].countryNumber = (uint8_t)away;
        ids[2 * q + 1].teamOrdinal = (uint8_t)(away >> 8);
    }
    s_cup64.rec.header.comp = _div1Playoffs.comp;
    s_cup64.rec.header.comp.compId = (uint8_t)COMPID_worldCupD64;
    s_cup64.rec.header.comp.teamNumber = (int8_t)c->country;
    s_cup64.rec.header.comp.isPlayoff = 0;
    s_cup64.rec.header.comp.reserved1 = 15;
    s_cup64.rec.header.comp.reserved2 = 21;
    s_cup64.rec.header.comp.seedReorderFlag = 1;
    s_cup64.rec.header.comp.beginMonth = (int8_t)c->round[r64][0];
    const uint8_t *last = c->round[c->rounds - 1];
    s_cup64.rec.header.comp.endMonth = (int8_t)(last[2] == 2 ? last[1] : last[0]);
    s_cup64.rec.header.teams = 64;
    s_cup64.rec.header.awayGoalsMode = 0;
    s_cup64.rec.header.subsAndPointsForWin = _div1Playoffs.subsAndMaxSubs;
    s_cup64.rec.header.roundStringBase = 1;
    for (int32_t r = r64; r < c->rounds && r - r64 < 6; r++)
        s_cup64.rec.roundRuleFlags[r - r64] = (int8_t)(c->round[r][2] == 2 ? 0x84 : 0x04);
    s_cup64.rec.name[0] = c->name[0];
    s_cup64.rec.name[1] = c->name[1];
    return 1;
}

/* The rest of the cup into slot 2 (from round r64: the career club is in it). */
static int32_t build_cup64(const WorldCupD *c, int32_t r64)
{
    WorldCupDState *s = &g_worldCupD;
    if (_tertiaryCompetitionPtr || cd_n(c, r64) != 32 || c->rounds - r64 > 6)
        return 0;
    begin_round(c, r64);
    for (int32_t q = 0; q < 32; q++) {                  /* (the round's hosts, as step draws them) */
        const int32_t t = cd_first(c, r64) + q;
        s->home[t] = (uint8_t)host_of(c, t, c->round[r64][3], cd_side(c, t, 0), cd_side(c, t, 1));
    }
    if (!make_cup64(c, r64))
        return 0;
    s->slot2 = (uint8_t)(r64 + 1);              /* (first: the calendar's round names read it) */
    s->round = (uint8_t)r64;
    Career_RetireSlotCalendarEntries(2);  /* (the slot's earlier contest's entries) */
    Career_SetCurrentCompetitionSlot((uint8_t *)&s_cup64.rec, 2, 0, 0);
    _tertiaryCompetitionContextBuf.tourn.header.slotStateA = _primaryCompetitionBuf.tourn.header.slotStateA;
    _tertiaryCompetitionContextBuf.tourn.header.slotStateB = _primaryCompetitionBuf.tourn.header.slotStateB;
    _tertiaryCompetitionContextBuf.tourn.header.slotStateC = _primaryCompetitionBuf.tourn.header.slotStateC;
    _competitionSlotUpdatedFlag = 1;
    ((SeasonInformations *)GetCurrentSeasonPointer())->strPtr2 =
        ((DIYCompetition *)&_tertiaryCompetitionContextBuf)->namePtr;
    SWOS_DBG("[world-cupd] from round %d in slot 2: the game's cup of 64\n", (int)r64);
    return 1;
}

static int32_t ours64(void)
{
    return g_worldCupD.slot2 && (uint8_t)_diyCompetitionStart.base.field_2E == COMPID_worldCupD64;
}

void CupD_ArrangeNextRound(void)
{
    const WorldCupD *c = World_CupD();
    WorldCupDState *s = &g_worldCupD;
    DIYCompetitionEx *x = &_diyCompetitionStart;
    if (!c || !ours64())
        return;
    const int32_t r = s->slot2 - 1 + x->base.currentRoundIndex + 1;      /* (the round to come) */
    if (r >= c->rounds)
        return;
    s->round = (uint8_t)r;
    begin_round(c, r);
    uint8_t *grid = x->teamGridPairings;
    const int32_t n = cd_n(c, r), first = cd_first(c, r);
    for (int32_t q = 0; q < n; q++) {
        const int32_t t = first + q;
        const uint16_t a = cd_side(c, t, 0), b = cd_side(c, t, 1);
        s->home[t] = (uint8_t)host_of(c, t, c->round[r][3], a, b);
        const uint16_t home = s->home[t] ? b : a, away = s->home[t] ? a : b;
        for (int32_t sd = 0; sd < 2; sd++) {             /* (the club's place in the list) */
            const uint16_t w = sd ? away : home;
            const uint16_t id = (uint16_t)((w & 0xFF) << 8 | w >> 8);
            for (int32_t e = 0; e < 64; e++)
                if (BE_READ_U16(&x->cupTeamIds[e]) == id) {
                    grid[2 * q + sd] = (uint8_t)e;
                    break;
                }
        }
    }
}

static void step(uint16_t week);

void CupD_Step(uint16_t week)
{
    s_floor = (uint16_t)(week + 1);
    step(week);
}

static void step(uint16_t week)
{
    const WorldCupD *c = World_CupD();
    WorldCupDState *s = &g_worldCupD;
    if (!c)
        return;
    if (s->season != (uint8_t)(g_worldCups.seasons + 1)) {     /* (a new season: the cup from its start) */
        CupD_Reset();
        s->season = (uint8_t)(g_worldCups.seasons + 1);
        if (!first_season())
            cd_make(c);
    }
    const uint16_t mgr = career_word();
    if (s->built || s->slot2)
        return;                                         /* (its tie first; the slot-2 cup plays the rest) */
    while (s->round < c->rounds) {
        const int32_t r = s->round;
        const uint8_t *rec = c->round[r];
        const int32_t w1 = rec[0], legs = rec[2], host = rec[3], n = cd_n(c, r), first = cd_first(c, r);
        if (n == 32 && (!r || cd_n(c, r - 1) != 32)) {  /* (the round of 64, the career club in it) */
            int32_t me = 0;
            for (int32_t q = 0; q < n; q++)
                me |= cd_side(c, first + q, 0) == mgr || cd_side(c, first + q, 1) == mgr;
            if (me && build_cup64(c, r))
                return;
        }
        if ((int32_t)week + 1 < w1) {
            /* not come yet: the career club's tie built ahead when its clubs are known */
            for (int32_t q = 0; q < n && slot4_free(); q++) {
                const int32_t t = first + q;
                const uint16_t a = cd_side(c, t, 0), b = cd_side(c, t, 1);
                if (!s->res[t] && (a == mgr || b == mgr) && a && b && a != 0xFFFF && b != 0xFFFF &&
                    !(rec[5] & 1)) {
                    s_rng = gp_seas() * 7919u + (uint32_t)r * 131u + (uint32_t)t;
                    s->home[t] = (uint8_t)host_of(c, t, host, a, b);
                    build_tie(c, t, a, b, week);
                    return;
                }
            }
            return;
        }
        s_rng = gp_seas() * 7919u + (uint32_t)r * 131u + (uint32_t)first;
        if ((rec[5] & 1) && s->drawn != r + 1) {        /* (the pairs drawn: the previous round's ties) */
            const int32_t pf = cd_first(c, r - 1), pn = cd_n(c, r - 1);
            for (int32_t i = 0; i < 4 && i < pn; i++)
                s->sd[i] = (uint8_t)(pf + i);
            for (int32_t i = (pn < 4 ? pn : 4) - 1; i > 0; i--) {
                const int32_t j = (int32_t)below((uint32_t)i + 1);
                const uint8_t x = s->sd[i];
                s->sd[i] = s->sd[j];
                s->sd[j] = x;
            }
            s->drawn = (uint8_t)(r + 1);
        }
        for (int32_t q = 0; q < n; q++) {
            const int32_t t = first + q;
            if (s->res[t])
                continue;
            const uint16_t a = cd_side(c, t, 0), b = cd_side(c, t, 1);
            const int32_t h = host_of(c, t, host, a, b);    /* (0: a hosts, 1: b hosts) */
            s->home[t] = (uint8_t)h;
            if ((a == mgr || b == mgr) && a && b && a != 0xFFFF && b != 0xFFFF && slot4_free()) {
                build_tie(c, t, a, b, week);                    /* (the career club's: played) */
                return;
            }
            int32_t w = first_season() ? c->tie[t][4] : 0;      /* (the real result, the first season) */
            if (!w)
                w = !a ? 2 : !b ? 1 : cd_sim(c, a, b, legs, h);
            s->res[t] = (uint8_t)w;
            if (a == mgr || b == mgr)
                SWOS_DBG("[world-cupd] round %d tie %d: %04X v %04X, the career club's, won by %s\n",
                         (int)r, (int)t, (unsigned)a, (unsigned)b, w == 1 ? "a" : "b");
        }
        s->round = (uint8_t)(r + 1);
        SWOS_DBG("[world-cupd] round %d decided (week %u)\n", (int)r, (unsigned)week);
    }
    if (s->round == c->rounds && c->rounds) {
        const int32_t f = cd_first(c, c->rounds - 1);
        SWOS_DBG("[world-cupd] won by %04X\n", (unsigned)cd_side(c, f, s->res[f] - 1));
        s->round++;                                     /* (logged once) */
    }
}

void CupD_SeasonStart(void)
{
    if (!World_CupD() || !_teamSelectionKeyCacheCount)
        return;
    const uint8_t first = ((TeamSelectionKeyCacheEntry *)_teamSelectionKeyCache)->sortKey;
    s_floor = 0;
    step((uint16_t)(first ? first - 1 : 0));
}

void CupD_TieDecided(uint16_t team1, uint16_t team2, int32_t team2Won)
{
    const WorldCupD *c = World_CupD();
    WorldCupDState *s = &g_worldCupD;
    if (c && ours64() && s->round < c->rounds) {
        /* a tie of the slot-2 cup's round: the table's tie of these two clubs */
        const uint16_t w1 = (uint16_t)((team1 >> 8) | (team1 & 0xFF) << 8);
        const uint16_t w2 = (uint16_t)((team2 >> 8) | (team2 & 0xFF) << 8);
        const int32_t first = cd_first(c, s->round), n = cd_n(c, s->round);
        for (int32_t t = first; t < first + n; t++) {
            const uint16_t a = cd_side(c, t, 0), b = cd_side(c, t, 1);
            if ((a == w1 && b == w2) || (a == w2 && b == w1)) {
                s->res[t] = (uint8_t)((team2Won ? w2 : w1) == a ? 1 : 2);
                break;
            }
        }
        if (s->round == c->rounds - 1 && s->res[first]) {
            s->round = c->rounds;                       /* (the final: the cup over) */
            SWOS_DBG("[world-cupd] won by %04X\n", (unsigned)cd_side(c, first, s->res[first] - 1));
        }
        return;
    }
    if (!c || !s->built || (uint8_t)_diyCompetitionStart.base.field_2E != COMPID_worldCupD ||
        !((team1 == s->teamA && team2 == s->teamB) || (team1 == s->teamB && team2 == s->teamA)))
        return;
    const uint16_t w = team2Won ? team2 : team1;
    const uint16_t word = (uint16_t)((w >> 8) | (w & 0xFF) << 8);
    s->res[s->bt] = (uint8_t)(cd_side(c, s->bt, 0) == word ? 1 : 2);
#ifdef SWOS_SDL_BUILD
    if (getenv("SWOS_TEST_CUPD_WIN"))       /* (test: the career club wins its ties, the Mod's CD_FORCE) */
        s->res[s->bt] = (uint8_t)(cd_side(c, s->bt, 0) == career_word() ? 1 : 2);
#endif
    s->built = 0;
    SWOS_DBG("[world-cupd] the career club's tie %d: won by %04X\n", (int)s->bt, (unsigned)w);
}

int32_t CupD_InSlot4(void)
{
    return _diyCompetitionPtr == (uint8_t *)&s_rec ? (g_worldCupD.built ? 2 : 1) : 0;
}

void CupD_RestoreSlot(void)
{
    const WorldCupD *c = World_CupD();
    if (c && g_worldCupD.slot2 && make_cup64(c, g_worldCupD.slot2 - 1))
        _tertiaryCompetitionPtr = (uint8_t *)&s_cup64.rec;     /* (the slot-2 cup under way) */
    if (!c || !g_worldCupD.built)
        return;
    make_record(c);
    _diyCompetitionPtr = (uint8_t *)&s_rec;
    _diyCompetitionTablePtr = (uint32_t *)0;
}

int32_t CupD_RoundStringIndex(uint8_t compId, uint16_t round)
{
    const WorldCupD *c = World_CupD();
    if (!c || (compId != COMPID_worldCupD && compId != COMPID_worldCupD64))
        return -1;
    /* by the ties of the round under way (the slot-2 cup's: its round): 1 final, 2 semi, 4
     * quarter, 8 round of 16, 16 of 32, 32 of 64; before them the first round, the
     * preliminary */
    int32_t r = g_worldCupD.round < c->rounds ? g_worldCupD.round : c->rounds - 1;
    if (compId == COMPID_worldCupD64 && g_worldCupD.slot2)
        r = g_worldCupD.slot2 - 1 + round < c->rounds ? g_worldCupD.slot2 - 1 + round : c->rounds - 1;
    switch (cd_n(c, r)) {
    case 1:  return 8;
    case 2:  return 7;
    case 4:  return 6;
    case 8:  return 12;
    case 16: return 13;
    case 32: return 14;
    default: return r == 0 ? 15 : 0;
    }
}

int32_t CupD_Member(uint8_t file, uint8_t ordinal)
{
    const WorldCupD *c = World_CupD();
    const uint16_t slot = World_Slot(file, ordinal);
    if (!c || World_FileCountry(file) != c->country || c->base[slot] == 0xFF)
        return 0;
    const uint16_t glob = (uint16_t)(_teamsCountryNumbers[c->country] + slot);
    const int32_t dv = c->base[slot] + (int8_t)_diyLeagueSeasonStateTable[glob];
    return dv >= c->first && dv < c->first + c->groups;
}
