/*
 * world_playoff.c -- the career club's own play-off, played (include/world_playoff.h).
 *
 * After the SWOS 96/97 Mod's dplay.py / dplay.c (dp_lend, dp_draw, dp_rec, dp_end; MIT,
 * Davide Lorigliola): the contest is built the way 96/97 builds its English play-offs in
 * slot 4 -- a Playoffs record, its clubs from the final table (DIY_Rebuild...'s steps) --
 * but when the league ends, since which one the club plays (if any) is known only then.
 */
#include "world_playoff.h"
#include "world_cups.h"
#include "world_cupd.h"
#include "league_playoffs.h"
#include "league_groups.h"
#include "game_types.h"
#include "comp_identity.h"
#include "diy_competition.h"
#include "career_teams.h"
#include "career_status.h"
#include "endian_helpers.h"

extern TeamFileEx _careerTeam;
extern DIYCompetitionEx _diyCompetitionStart;
extern CompetitionSlotBuffer _diyCompetitionContextBuf;
extern CompetitionSlotBuffer _primaryCompetitionBuf;
extern uint8_t *_diyCompetitionPtr;
extern uint32_t *_diyCompetitionTablePtr;
extern uint16_t _currentDivisionIndex;
extern uint16_t _competitionSlotUpdatedFlag;
extern uint16_t _internationalAutoAdvanceFlag;
extern uint8_t *_currentCompetitionPtr;
extern uint16_t _numSelectedTeams;
extern Playoffs _div1Playoffs;
extern void Career_RetireSlotCalendarEntries(uint8_t slot);
extern void Career_SetCurrentCompetitionSlot(uint8_t *comp, uint16_t slot, uint16_t divisionIndex,
                                             uint16_t hasSelectedTeams);
extern void Career_LoadCompetitionSlotStateFromBuffer(uint8_t *slot);
extern void Career_StoreCompetitionSlotStateToBuffer(uint8_t *slot);
extern void DIY_BuildTeamSelectionPointerCache(void);
extern void DIY_UpdateCompetitionProgressOrAdvanceSlot(uint8_t *blk);

/* The contest's competition id: one no competition has. The original's ids are a whole
 * byte -- its cups are stored negative (-127 = 129, the Icelandic cup) -- and run to 161;
 * 129 was taken, and a season end's Icelandic cup played as this contest. */
#define COMPID_worldPlayoff 200

WorldPlayoffState g_worldPlayoff;
static CompetitionCup s_rec;
static char s_outName[] = "PLAY-OUT SERIE B", s_outShort[] = "PLAY-OUT";
static char s_offName[] = "PLAY-OFF SERIE B", s_offShort[] = "PLAY-OFF";
static char s_tieName[24] = "PLAY-OUT", s_tieShort[] = "PLAY-OUT";
static char s_cName[] = "PLAY-OFF SERIE C", s_cShort[] = "PLAY-OFF";
static char s_pName[24] = "PLAY-OFF", s_pShort[] = "PLAY-OFF";

static const uint8_t *k_spec(void);
static const uint8_t *k_round(const uint8_t *s, int32_t r);
static uint8_t k_flags(int32_t legs, int32_t level);
static char s_kName[24], s_kShort[16];
enum { K_GAMES = 12 };
static int32_t k_games(const uint8_t *s, uint8_t *gsr, uint8_t *gno);
static int32_t k_end(int32_t ng)                /* (this part's end game round) */
{
    return g_worldPlayoff.gend && g_worldPlayoff.gend < ng ? g_worldPlayoff.gend : ng;
}

/* The bracket's places: its clubs (Serie C's 28 in a bracket of 64: the byes and the
 * fillers after each round's real ties). */
static int32_t bracket_size(void)
{
    if (g_worldPlayoff.kind == WORLD_PLAYOFF_SPEC) {
        const uint8_t *s = LeaguePlayoffs_SpecAt(g_worldPlayoff.country, g_worldPlayoff.pidx);
        uint8_t gsr[K_GAMES], gno[K_GAMES];
        int32_t b = s ? 1 << (k_end(k_games(s, gsr, gno)) - g_worldPlayoff.g0) : g_worldPlayoff.n;
        while (b < g_worldPlayoff.n)            /* (its list: the clubs of the rounds decided at the */
            b <<= 1;                            /*  build too -- the rounds then numRoundsOverride's) */
        return b;
    }
    return g_worldPlayoff.kind == WORLD_PLAYOFF_C_PLAYOFF ? 64 : g_worldPlayoff.n;
}

void WorldPlayoff_Reset(void)
{
    uint8_t *p = (uint8_t *)&g_worldPlayoff;
    for (uint32_t i = 0; i < sizeof g_worldPlayoff; i++)
        p[i] = 0;
}

/* The contest's record from the state: a cup record (a Playoffs record carries only two
 * rounds' rules, and the career calendar is built from them when the contest is made),
 * headed as 96/97's Division One play-off -- its dates after the league -- with no draw. */
static void make_record(void)
{
    const int32_t off = g_worldPlayoff.kind == WORLD_PLAYOFF_B_PLAYOFF;
    uint8_t *z = (uint8_t *)&s_rec;
    for (uint32_t i = 0; i < sizeof s_rec; i++)
        z[i] = 0;
    s_rec.header.comp = _div1Playoffs.comp;
    s_rec.header.comp.compId = (uint8_t)COMPID_worldPlayoff;
    s_rec.header.comp.teamNumber = (int8_t)_careerTeam.team.countryNumber;   /* ("ITALIAN ...") */
    s_rec.header.comp.isPlayoff = 0;
    s_rec.header.comp.reserved1 = 15;           /* (a cup's, inert) */
    s_rec.header.comp.reserved2 = 21;
    s_rec.header.comp.seedReorderFlag = 1;      /* the clubs as listed: no draw */
    s_rec.header.teams = (int8_t)bracket_size();
    s_rec.header.awayGoalsMode = 0;             /* (Italy: no away goals) */
    s_rec.header.subsAndPointsForWin = _div1Playoffs.subsAndMaxSubs;
    s_rec.header.roundStringBase = 1;
    /* per round: legs (bits 7-6: 0 single, 2 two), extra time (5-4), penalties (3-2) */
    if (off) {
        s_rec.roundRuleFlags[0] = 0x10;         /* 5v8, 6v7: single, e.t. */
        s_rec.roundRuleFlags[1] = (int8_t)0x80; /* semis: two legs */
        s_rec.roundRuleFlags[2] = (int8_t)0x80; /* final: two legs (e.t. + pens: ArrangeNextRound) */
        s_rec.name[0] = s_offName;
        s_rec.name[1] = s_offShort;
    } else if (g_worldPlayoff.kind == WORLD_PLAYOFF_C_PLAYOFF) {
        s_rec.roundRuleFlags[0] = 0x00;         /* group rounds: single, level -> the better placed */
        s_rec.roundRuleFlags[1] = 0x00;
        s_rec.roundRuleFlags[2] = (int8_t)0x80; /* national rounds: two legs, level -> the seed */
        s_rec.roundRuleFlags[3] = (int8_t)0x80;
        s_rec.roundRuleFlags[4] = (int8_t)0x94; /* Final Four: two legs, e.t. + penalties */
        s_rec.roundRuleFlags[5] = (int8_t)0x94;
        s_rec.name[0] = s_cName;
        s_rec.name[1] = s_cShort;
    } else if (g_worldPlayoff.kind == WORLD_PLAYOFF_PLACEMENT) {
        s_rec.roundRuleFlags[0] = (int8_t)g_worldPlayoff.rules;
        s_rec.roundRuleFlags[1] = (int8_t)g_worldPlayoff.rules2;
        s_rec.name[0] = s_pName;
        s_rec.name[1] = s_pShort;
    } else if (g_worldPlayoff.kind == WORLD_PLAYOFF_SPEC) {
        const uint8_t *sp = k_spec();
        uint8_t gsr[K_GAMES], gno[K_GAMES];
        const int32_t ng = sp ? k_games(sp, gsr, gno) : 0;
        int32_t days = 0;
        for (int32_t q = 0; g_worldPlayoff.g0 + q < k_end(ng) && q < 6; q++) {   /* (its game rounds') */
            const uint8_t *rd = k_round(sp, gsr[g_worldPlayoff.g0 + q]);
            s_rec.roundRuleFlags[q] = (int8_t)k_flags(rd[1] == 3 ? 1 : rd[1], rd[3]);
            days += rd[1] == 2 ? 2 : 1;
        }
        if (g_worldPlayoff.week) {              /* (mid-season: its own weeks, a leg a week) */
            s_rec.header.comp.beginMonth = (int8_t)g_worldPlayoff.week;
            s_rec.header.comp.endMonth = (int8_t)(g_worldPlayoff.week + days - 1);
        }
        s_rec.name[0] = s_kName;
        s_rec.name[1] = s_kShort;
        if (bracket_size() != 1 << (k_end(ng) - g_worldPlayoff.g0))
            s_rec.header.numRoundsOverride = (int8_t)(k_end(ng) - g_worldPlayoff.g0);
    } else if (g_worldPlayoff.kind == WORLD_PLAYOFF_GROUP_TIE) {
        s_rec.roundRuleFlags[0] = (int8_t)g_worldPlayoff.rules;
        s_rec.name[0] = s_tieName;
        s_rec.name[1] = s_tieShort;
    } else {
        s_rec.roundRuleFlags[0] = (int8_t)(g_worldPlayoff.etp ? 0x94 : 0x80);
        s_rec.name[0] = s_outName;
        s_rec.name[1] = s_outShort;
    }
    for (uint8_t i = 0; i < g_worldPlayoff.n && i < 32; i++) {
        s_rec.teamSeedList[i].countryNumber = (uint8_t)(g_worldPlayoff.team[i] >> 8);
        s_rec.teamSeedList[i].teamOrdinal = (uint8_t)g_worldPlayoff.team[i];
    }
}

#ifdef SWOS_SDL_BUILD
/* Test (SWOS_TEST_PLAYOFF_PLACE=<place>): the career club takes that place of its final
 * table, figures and all (the Mod's DPLAY_FORCE / dp_forcecfb); the play-out is held. */
static int32_t force_place(DIYCompetitionEx *ex, int32_t n)
{
    const char *v = getenv("SWOS_TEST_PLAYOFF_PLACE");
    const int32_t want = v ? atoi(v) - 1 : -1;
    uint8_t *base = (uint8_t *)&ex->base;
    uint16_t *order = ex->matchData.league.teamEntryOffsets;
    int32_t mine = -1;
    if (want < 0 || want >= n)
        return 0;
    for (int32_t k = 0; k < n; k++)
        if (((DIYTeamStats *)(base + order[k] + DIY_STANDINGS_OFFSET))->teamSelOffset == -1)
            mine = k;
    if (mine < 0)
        return 0;
    if (mine != want) {
        DIYTeamStats *a = (DIYTeamStats *)(base + order[mine] + DIY_STANDINGS_OFFSET);
        DIYTeamStats *b = (DIYTeamStats *)(base + order[want] + DIY_STANDINGS_OFFSET);
        DIYTeamStats x = *a;
        const int32_t sa = a->teamSelOffset, sb = b->teamSelOffset;
        *a = *b; *b = x;                        /* the figures change places, */
        a->teamSelOffset = sa; b->teamSelOffset = sb;   /* the clubs keep their rows */
        const uint16_t o = order[mine];
        order[mine] = order[want];
        order[want] = o;
    }
    {   /* and in the country's stage tables written this season (the Mod's gp_force): the same place
         * there -- a play-off seeded by one (the MLS's) sees the forced place */
        extern uint16_t _selTeamNumber;
        for (int32_t bk = 0; bk < LEAGUE_STAGE_BANKS; bk++) {
            LeagueStageBank *b = &g_leagueStages[bk];
            if (!b->used || b->key != (uint8_t)_careerTeam.team.countryNumber)
                continue;
            for (int32_t k = 0; k < LEAGUE_STAGE_TABLES; k++) {
                if (!(b->fresh >> k & 1) || want >= b->n[k])
                    continue;
                for (int32_t i = 0; i < b->n[k]; i++)
                    if (b->r[k][i].team == _selTeamNumber && i != want) {
                        const LeagueStageRow x = b->r[k][i];
                        b->r[k][i] = b->r[k][want];
                        b->r[k][want] = x;
                        break;
                    }
            }
        }
    }
    LeaguePlayoffs_TestHeld = 1;
    return 1;
}
#endif

/* A round's rules, in the competition state (a Playoffs record carries two rounds'):
 * legs 0 single / 2 two; extra time, penalties 1 on. */
static void round_rules(int32_t r, uint16_t legs, uint16_t et, uint16_t pens)
{
    _diyCompetitionStart.matchData.cup.roundLegs[r] = legs;
    _diyCompetitionStart.matchData.cup.roundExtraTime[r] = et;
    _diyCompetitionStart.matchData.cup.roundPenalties[r] = pens;
}

/* The first round, the contest just made from its record: only its real ties played
 * (the byes after them). */
static void lay_out(int32_t pairs);

static void k_lay_out(const uint8_t *s, int32_t r, int32_t sr);
static int32_t s_pairs;                         /* (the round laid out: its places, WorldPlayoff_RoundSize) */

static void first_round(void)
{
    DIYCompetitionEx *c = &_diyCompetitionStart;
    if (g_worldPlayoff.kind == WORLD_PLAYOFF_SPEC) {
        const uint8_t *s = k_spec();
        uint8_t gsr[K_GAMES], gno[K_GAMES];
        if (s && g_worldPlayoff.g0 < k_games(s, gsr, gno))
            k_lay_out(s, 0, gsr[g_worldPlayoff.g0]);
        c->base.matchPairsPerRound = (int16_t)s_pairs;  /* (its places: k_lay_out) */
        c->base.totalMatchesInRound = g_worldPlayoff.ties;
    }
    if (g_worldPlayoff.kind == WORLD_PLAYOFF_B_PLAYOFF)
        c->base.totalMatchesInRound = 2;        /* (the 3rd's and the 4th's byes not played) */
    if (g_worldPlayoff.kind == WORLD_PLAYOFF_C_PLAYOFF) {
        /* the group rounds' first: 5th v 10th, 6th v 9th, 7th v 8th of each group (the list's
         * first 18), at the better placed's; everyone else waits */
        g_worldPlayoff.ties = 9;
        for (uint8_t t = 0; t < 9; t++) {
            g_worldPlayoff.tieA[t] = (uint8_t)(2 * t);
            g_worldPlayoff.tieB[t] = (uint8_t)(2 * t + 1);
        }
        lay_out(c->base.matchPairsPerRound);
        c->base.totalMatchesInRound = 9;
    }
}

/* A group's play-out the career club is in (the pack's knockouts, compiled as gplay.c
 * reads them: kind, entrants [division, place], rounds of ties): its tie of the first
 * round, with that round's rules -- a contest of two. */
static int32_t group_tie(const LeaguePlayoffs *lp, int32_t d, const uint16_t *team,
                         const uint16_t *pts, int32_t n, int32_t mine)
{
    const uint8_t *s = lp->spec;
    while (s && *s) {
        const int32_t kind = s[0], ne = s[1], nr = s[2];
        const uint8_t *e = s + 6, *r = e + 4 * ne, *first = r;
        for (int32_t k = 0; k < nr; k++)
            r += 6 + 2 * r[0];                  /* (rounds of ties: compiled so) */
        if (kind == 4 && nr >= 1 && ne <= 16) {
            const int32_t ties = first[0], legs = first[1], host = first[2] & 0x7F, level = first[3];
            const int32_t gap = first[5];
            const uint8_t *m = first + 6;
            for (int32_t t = 0; t < ties; t++) {
                const int32_t ia = m[2 * t], ib = m[2 * t + 1];
                if (ia >= ne || ib >= ne || e[4 * ia] || e[4 * ib] || e[4 * ia + 1] != d || e[4 * ib + 1] != d)
                    continue;
                const int32_t a = e[4 * ia + 2] - 1, b = e[4 * ib + 2] - 1;   /* places, 0-based */
                if (a < 0 || b < 0 || a >= n || b >= n || (a != mine && b != mine))
                    continue;
                const int32_t diff = (int32_t)pts[a] - (int32_t)pts[b];
                if (gap && (diff >= gap || -diff >= gap) && !LeaguePlayoffs_TestHeld)
                    return 0;                   /* too far apart: no tie, the better placed through */
                /* the better placed hosts the single match / the return (gplay.c HOST_BETTER) */
                const int32_t better = a < b ? a : b, worse = a < b ? b : a;
                const int32_t h = host == 1 ? (legs == 2 ? worse : better) : a;
                const int32_t o = h == a ? b : a;
                g_worldPlayoff.kind = WORLD_PLAYOFF_GROUP_TIE;
                g_worldPlayoff.n = 2;
                g_worldPlayoff.team[0] = team[h]; g_worldPlayoff.prio[0] = (uint8_t)h; g_worldPlayoff.pts[0] = pts[h];
                g_worldPlayoff.team[1] = team[o]; g_worldPlayoff.prio[1] = (uint8_t)o; g_worldPlayoff.pts[1] = pts[o];
                /* level (gplay.c LV_*): 0 better, 1 et_pens, 2 et_better, 3 pens */
                g_worldPlayoff.etp = level == 1 || level == 3;
                g_worldPlayoff.rules = (uint8_t)((legs == 2 ? 0x80 : 0) | (level == 1 || level == 2 ? 0x10 : 0) |
                                                 (level == 1 || level == 3 ? 0x04 : 0));
                /* its name: the level's ("SERIE C"), as the pack calls them */
                {
                    const LeagueGroups *lg = LeagueGroups_ForCountry(lp->country);
                    const char *lv = 0, *src;
                    char *dst = s_tieName;
                    for (int32_t l = 0; lg && l < lg->levels; l++)
                        if (d >= lg->level[l].first && d < lg->level[l].first + lg->level[l].groups)
                            lv = lg->level[l].name;
                    for (src = "PLAY-OUT "; *src; ) *dst++ = *src++;
                    for (src = lv ? lv : ""; *src && dst < s_tieName + sizeof s_tieName - 1; ) *dst++ = *src++;
                    *dst = 0;
                }
                return 1;
            }
        }
        s = r;
    }
    return 0;
}

/* ---- Serie C's national play-off (dplay.c c_lend, dp_perm_c, dp_restore) ---- */

/* A final table as the season end leaves it in the DIY competition: by place. */
typedef struct { uint16_t team, glob; WorldPlayoffRow r; } TableRow;

static int32_t read_table(TableRow *out, int32_t cap)
{
    DIYCompetitionEx *ex = &_diyCompetitionStart;
    const int32_t n = (uint16_t)ex->base.numTeams;
    for (int32_t k = 0; k < n && k < cap; k++) {
        const DIYTeamStats *st = (const DIYTeamStats *)((uint8_t *)&ex->base +
                                 ex->matchData.league.teamEntryOffsets[k] + DIY_STANDINGS_OFFSET);
        const TeamFileEx *t = (const TeamFileEx *)diy_resolve_team(st->teamSelOffset);
        out[k].team = PACK_BE_U16(t->team.countryNumber, t->team.teamOrdinal);
        out[k].glob = (uint16_t)t->team.globalTeamNumber;
        out[k].r.team = out[k].team;
        out[k].r.played = st->gamesPlayed; out[k].r.won = st->wins; out[k].r.drawn = st->draws;
        out[k].r.lost = st->losses; out[k].r.gf = st->goalsFor; out[k].r.ga = st->goalsAgainst;
        out[k].r.pts = st->points;
    }
    return n < cap ? n : cap;
}

/* Another group's season, simulated now (as the season end will: the same seeded
 * simulation) and its table kept -- the way 96/97's English play-off rebuild plays the
 * other division (DIY_RebuildCompetitionAndTeamSelectionCaches). */
static int32_t simulate_early(uint16_t d, TableRow *out, int32_t cap)
{
    int32_t n;
    _internationalAutoAdvanceFlag = 1;
    SWOS_DBG("[world-playoff] early season of division %u: %u selected teams parked\n",
             (unsigned)d, (unsigned)_numSelectedTeams);
    SaveSelectedTeams();
    Career_AutoAdvanceInternationalCompetitionFixtures(_currentCompetitionPtr, d);
    n = read_table(out, cap);
    RestoreSelectedTeams();
    _internationalAutoAdvanceFlag = 0;
    return n;
}

static uint32_t c_rng;
static uint32_t c_below(uint32_t n)
{
    c_rng = c_rng * 1103515245u + 12345u;
    return ((c_rng >> 16) * n) >> 16;
}

/* 'better placed' between two entrants: place, points, wins, goals scored, lot */
static int32_t sbetter(int32_t a, int32_t b)
{
    const WorldPlayoffState *v = &g_worldPlayoff;
    if (v->place[a] != v->place[b]) return v->place[a] < v->place[b];
    if (v->pts[a] != v->pts[b]) return v->pts[a] > v->pts[b];
    if (v->won[a] != v->won[b]) return v->won[a] > v->won[b];
    if (v->gf[a] != v->gf[b]) return v->gf[a] > v->gf[b];
    return (uint8_t)(v->team[a] * 37) < (uint8_t)(v->team[b] * 37);
}

static void ssort(uint8_t *c, int32_t n)
{
    for (int32_t i = 1; i < n; i++) {
        uint8_t x = c[i]; int32_t j = i;
        while (j > 0 && sbetter(x, c[j - 1])) { c[j] = c[j - 1]; j--; }
        c[j] = x;
    }
}

static void sshuffle(uint8_t *c, int32_t n)
{
    for (int32_t i = n - 1; i > 0; i--) { int32_t j = (int32_t)c_below((uint32_t)(i + 1)); uint8_t x = c[i]; c[i] = c[j]; c[j] = x; }
}

/* The round's grid: its real ties (the first leg's / the single match's home club first),
 * then a bye -- a tie with itself -- for each club waiting, then fillers (a waiting club
 * again) up to the engine's `pairs`; only the real ties are played. */
static void lay_out(int32_t pairs)
{
    uint8_t *grid = _diyCompetitionStart.teamGridPairings;
    const int32_t next = _diyCompetitionStart.base.currentRoundIndex;   /* (see the callers) */
    uint8_t wait[16];
    int32_t nw = 0, k = 0;
    (void)next;
    for (int32_t t = 0; t < g_worldPlayoff.ties && k < pairs; t++, k++) {
        grid[2 * k] = g_worldPlayoff.tieA[t];
        grid[2 * k + 1] = g_worldPlayoff.tieB[t];
    }
    /* who waits: in the group rounds the 4ths, 3rds, the cup place and the 2nds; in the
     * national rounds' first, the 2nds */
    for (int32_t i = 18; i < g_worldPlayoff.n; i++) {
        int32_t playing = 0;
        for (int32_t t = 0; t < g_worldPlayoff.ties; t++)
            if (g_worldPlayoff.tieA[t] == i || g_worldPlayoff.tieB[t] == i) playing = 1;
        if (!playing && g_worldPlayoff.prio[i] != 0xFE && nw < 16)
            wait[nw++] = (uint8_t)i;
    }
    for (int32_t i = 0; i < nw && k < pairs; i++, k++)
        grid[2 * k] = grid[2 * k + 1] = wait[i];
    for (; k < pairs; k++)
        grid[2 * k] = grid[2 * k + 1] = nw ? wait[0] : g_worldPlayoff.tieA[0];
}

/* The next round's ties from the last one's winners, in tie order (dp_perm_c; DP_PR =
 * the round to come, 1 the group rounds' second ... 5 the final). */
static void c_next_round(int32_t pr, const uint8_t *t, int32_t pairs)
{
    WorldPlayoffState *v = &g_worldPlayoff;
    uint32_t h = (uint32_t)pr;
    uint8_t *A = v->tieA, *B = v->tieB;
    for (int32_t k = 0; k < v->n; k++) h = h * 31 + v->team[k];
    c_rng = h;
    if (pr == 1) {                      /* group round 2: the 3 winners + the 4th, best v worst */
        for (int32_t g = 0; g < 3; g++) {
            uint8_t c[4] = { t[3 * g], t[3 * g + 1], t[3 * g + 2], (uint8_t)(18 + g) };
            ssort(c, 4);
            A[2 * g] = c[0]; B[2 * g] = c[3];
            A[2 * g + 1] = c[1]; B[2 * g + 1] = c[2];
        }
        v->ties = 6;
    } else if (pr == 2 || pr == 3) {    /* national rounds: seeds v the others drawn */
        uint8_t q[6], s[5], o[5];
        const int32_t nq = pr == 2 ? 6 : 5;
        int32_t b = 0, k = 0, ns = 0;
        for (int32_t i = 0; i < nq; i++) { q[i] = t[i]; if (i && sbetter(q[i], q[b])) b = i; }
        if (pr == 2) { s[ns++] = 21; s[ns++] = 22; s[ns++] = 23; s[ns++] = v->cup; }
        else         { s[ns++] = 25; s[ns++] = 26; s[ns++] = 27; }
        s[ns++] = q[b];
        for (int32_t i = 0; i < nq; i++) if (i != b) o[k++] = q[i];
        sshuffle(o, ns);
        for (int32_t i = 0; i < ns; i++) {
            v->prio[s[i]] = 0; v->prio[o[i]] = 1;       /* (level -> the seed) */
            A[i] = o[i]; B[i] = s[i];                   /* (the return at the seed's) */
        }
        v->ties = (uint8_t)ns;
    } else if (pr == 4) {               /* Final Four: a free draw */
        uint8_t c[4] = { t[0], t[1], t[2], t[3] };
        sshuffle(c, 4);
        A[0] = c[0]; B[0] = c[1]; A[1] = c[2]; B[1] = c[3];
        v->ties = 2;
    } else {                            /* the final */
        A[0] = t[0]; B[0] = t[1];
        v->ties = 1;
    }
    /* who no longer waits has played; who has gone out waits no more (prio 0xFE) */
    for (int32_t i = 18; i < v->n; i++) {
        int32_t in = 0;
        for (int32_t k = 0; k < v->ties; k++) if (A[k] == i || B[k] == i) in = 1;
        if (pr >= 2 && i < 25 && !in) v->prio[i] = 0xFE;           /* (the 3rds, the cup place: drawn by now) */
        if (pr >= 3 && !in) v->prio[i] = 0xFE;
    }
    lay_out(pairs);
}

/* Serie C's play-off, built: the career club 2nd-10th of its group, or its group's 11th
 * standing in for the Coppa Italia Serie C winner (dplay.c c_lend). */
static int32_t c_playoff(const LeaguePlayoffs *lp, const TableRow *own, int32_t nown, int32_t mine)
{
    static TableRow tab[3][WORLD_PLAYOFF_EARLY_ROWS];
    static const uint8_t r1[6] = { 4, 9, 5, 8, 6, 7 };
    int32_t ng = lp->groups, g0 = (int32_t)_currentDivisionIndex - lp->groupFirst, n[3] = { 0, 0, 0 };
    WorldPlayoffState *v = &g_worldPlayoff;
    if (ng != 3 || g0 < 0 || g0 >= 3 || mine < 1)
        return 0;
    /* the other groups' seasons, now (their tables kept for the season end) */
    for (int32_t g = 0, e = 0; g < 3; g++) {
        if (g == g0) {
            for (int32_t k = 0; k < nown && k < WORLD_PLAYOFF_EARLY_ROWS; k++) tab[g][k] = own[k];
            n[g] = nown;
            continue;
        }
        n[g] = simulate_early((uint16_t)(lp->groupFirst + g), tab[g], WORLD_PLAYOFF_EARLY_ROWS);
        v->earlyDiv[e] = (uint8_t)(lp->groupFirst + g);
        v->earlyN[e] = (uint8_t)n[g];
        for (int32_t k = 0; k < n[g]; k++) v->earlyRow[e][k] = tab[g][k].r;
        e++;
    }
    v->early = 2;
    for (int32_t g = 0; g < 3; g++)
        if (n[g] < 11)
            return 0;
    /* the Coppa Italia Serie C winner's place (dplay.c cup_place): the winner itself when it
     * finished 11th or lower, the 11th of its group when promoted or already in; unknown:
     * the best 11th. A career club not in the cup has it played now (world_cups.c). */
    const uint8_t country = (uint8_t)_careerTeam.team.countryNumber;
    WorldCups_PlayLeagueCupEarly(country);
    const uint16_t cupw = WorldCups_LeagueCupWinner(country);
    int32_t cg = -1, cp = 10;
    for (int32_t g = 0; g < 3 && cupw && cg < 0; g++)
        for (int32_t k = 0; k < n[g]; k++)
            if (tab[g][k].team == cupw) {
                cg = g;
                cp = k >= 10 ? k : 10;
            }
    if (cg < 0) {
        cg = 0;
        for (int32_t g = 1; g < 3; g++) {
            const WorldPlayoffRow *a = &tab[g][10].r, *b = &tab[cg][10].r;
            const uint8_t la = (uint8_t)(tab[g][10].glob * 37 + 11), lb = (uint8_t)(tab[cg][10].glob * 37 + 11);
            if (a->pts != b->pts ? a->pts > b->pts : a->won != b->won ? a->won > b->won :
                a->gf != b->gf ? a->gf > b->gf : la < lb)
                cg = g;
        }
    }
    if (!(mine >= 1 && mine <= 9) && !(cg == g0 && mine == cp))
        return 0;
    int32_t i = 0;
    for (int32_t g = 0; g < 3; g++)
        for (int32_t k = 0; k < 6; k++) { v->place[i] = r1[k]; v->group[i++] = (uint8_t)g; }
    for (int32_t p = 3; p >= 1; p--) {
        for (int32_t g = 0; g < 3; g++) { v->place[i] = (uint8_t)p; v->group[i++] = (uint8_t)g; }
        if (p == 2) { v->place[i] = (uint8_t)cp; v->group[i++] = (uint8_t)cg; }
    }
    v->kind = WORLD_PLAYOFF_C_PLAYOFF;
    v->n = (uint8_t)i;
    v->cup = 24;
    for (int32_t k = 0; k < v->n; k++) {
        const TableRow *r = &tab[v->group[k]][v->place[k]];
        v->team[k] = r->team; v->pts[k] = r->r.pts; v->won[k] = r->r.won; v->gf[k] = r->r.gf;
        v->prio[k] = v->place[k];
    }
    return 1;
}

/* A spec round's rules as the cup record's flag byte (gplay.c LV_*: 0 better, 1 et_pens,
 * 2 et_better, 3 pens): legs (bits 7-6), extra time (5-4), penalties (3-2). */
static uint8_t round_flags(int32_t legs, int32_t level)
{
    return (uint8_t)((legs == 2 ? 0x80 : 0) | (level == 1 || level == 2 ? 0x10 : 0) |
                     (level == 1 || level == 3 ? 0x04 : 0));
}

/* The name of division d's level ("SERIE D") after `what`, into dst. */
static void level_name(const LeaguePlayoffs *lp, int32_t d, const char *what, char *dst, int32_t cap)
{
    const LeagueGroups *lg = LeagueGroups_ForCountry(lp->country);
    const char *lv = 0;
    char *end = dst + cap - 1;
    for (int32_t l = 0; lg && l < lg->levels; l++)
        if (d >= lg->level[l].first && d < lg->level[l].first + lg->level[l].groups)
            lv = lg->level[l].name;
    while (*what && dst < end) *dst++ = *what++;
    for (lv = lv ? lv : ""; *lv && dst < end; ) *dst++ = *lv++;
    *dst = 0;
}

/* A group's placement play-off the career club is in (the pack's kind 3: entrants
 * [division, place], a round of two ties, then their winners): a contest of four. */
static int32_t group_placement(const LeaguePlayoffs *lp, int32_t d, const uint16_t *team,
                               const uint16_t *pts, int32_t n, int32_t mine)
{
    const uint8_t *s = lp->spec;
    while (s && *s) {
        const int32_t kind = s[0], ne = s[1], nr = s[2];
        const uint8_t *e = s + 6, *r = e + 4 * ne, *first = r, *second;
        for (int32_t k = 0; k < nr; k++)
            r += 6 + 2 * r[0];
        second = first + 6 + 2 * first[0];
        if (kind == 3 && ne == 4 && nr == 2 && first[0] == 2 && second[0] == 1) {
            int32_t place[4], in = 0;
            for (int32_t i = 0; i < 4; i++) {
                place[i] = e[4 * i + 2] - 1;
                if (e[4 * i] || e[4 * i + 1] != d || place[i] < 0 || place[i] >= n)
                    in = -99;
                in += place[i] == mine;
            }
            if (in == 1) {
                const uint8_t *m = first + 6;
                for (int32_t t = 0; t < 2; t++) {
                    const int32_t a = place[m[2 * t]], b = place[m[2 * t + 1]];
                    /* the better placed hosts the single match / the return */
                    const int32_t better = a < b ? a : b, worse = a < b ? b : a;
                    const int32_t h = (first[2] & 0x7F) == 1 ? (first[1] == 2 ? worse : better) : a;
                    const int32_t o = h == a ? b : a;
                    g_worldPlayoff.team[2 * t] = team[h]; g_worldPlayoff.prio[2 * t] = (uint8_t)h;
                    g_worldPlayoff.pts[2 * t] = pts[h];
                    g_worldPlayoff.team[2 * t + 1] = team[o]; g_worldPlayoff.prio[2 * t + 1] = (uint8_t)o;
                    g_worldPlayoff.pts[2 * t + 1] = pts[o];
                }
                g_worldPlayoff.kind = WORLD_PLAYOFF_PLACEMENT;
                g_worldPlayoff.n = 4;
                g_worldPlayoff.rules = round_flags(first[1], first[3]);
                g_worldPlayoff.rules2 = round_flags(second[1], second[3]);
                g_worldPlayoff.etp = 0;         /* (level: e.t., then the better placed) */
                level_name(lp, d, "PLAY-OFF ", s_pName, (int32_t)sizeof s_pName);
                return 1;
            }
        }
        s = r;
    }
    return 0;
}

/* ---- A play-off of the pack's spec, played (the Mod's gp_ko: ko_build, build_game) ----
 *
 * Its rounds of ties (one or two legs; a group round or a best-of-three not here: those are
 * decided at the season end), the members of each from the spec's codes -- entrants, a tie's
 * winner or loser, the previous round's best placed loser -- paired best v worst or as
 * listed, the better placed (the place: a stage entrant's stage place) hosting the single
 * match or the return. The bracket: 2 ^ rounds places, each round's real ties first, then a
 * bye -- a tie with itself -- for each club still to play a later round. */

WorldStageTitle g_worldStageTitle[3];
/* A stage's knockout to build at the calendar's advance (WorldPlayoff_StageEnded). */
static struct {
    uint8_t on, country, division, pidx, week, n;
    LeagueStageRow table[32];
} s_pendStage;
static char s_kName[24] = "PLAY-OFF", s_kShort[16] = "PLAY-OFF";   /* (the pack's names: not yet) */
enum { NOC = 0xFF };

static const uint8_t *k_spec(void)
{
    return LeaguePlayoffs_SpecAt(g_worldPlayoff.country, g_worldPlayoff.pidx);
}

static const uint8_t *k_round(const uint8_t *s, int32_t r)
{
    const uint8_t *x = s + 6 + 4 * s[1];
    for (int32_t k = 0; k < r; k++)
        x += 6 + 2 * x[0];
    return x;
}

static int32_t k_t0(const uint8_t *s, int32_t r)        /* the round's first tie index */
{
    int32_t t = 0;
    for (int32_t k = 0; k < r; k++)
        t += k_round(s, k)[0];
    return t;
}

static int32_t k_better(int32_t a, int32_t b)
{
    return g_worldPlayoff.prio[a] < g_worldPlayoff.prio[b];
}

static int32_t k_member(int32_t c)                      /* a member code -> entrant, or -1 */
{
    if (c < 0x3F) return c < g_worldPlayoff.n ? c : -1;
    if (c < 0x80) return -1;                    /* (group placings, the best loser: below) */
    const int32_t t = c & 0x3F, v = c & 0x40 ? g_worldPlayoff.tl[t] : g_worldPlayoff.tw[t];
    return t < WORLD_PLAYOFF_MAX && v != NOC ? v : -1;
}

static uint8_t k_flags(int32_t legs, int32_t level)     /* the round's rule byte (the Mod's rbyte) */
{
    static const uint8_t lv[6] = { 0x00, 0x14, 0x10, 0x04, 0x14, 0x00 };
    return (uint8_t)((legs == 2 ? 0x80 : 0) | lv[level < 6 ? level : 1]);
}

/* The spec's game rounds (the Mod's ko_build): a best-of-three round is three single-match
 * game rounds; game round g's spec round and game number in its series. Their count. */
static int32_t k_games(const uint8_t *s, uint8_t *gsr, uint8_t *gno)
{
    int32_t ng = 0;
    for (int32_t q = 0; q < s[2]; q++) {
        const int32_t games = k_round(s, q)[1] == 3 ? 3 : 1;
        for (int32_t g = 0; g < games && ng < K_GAMES; g++) {
            gsr[ng] = (uint8_t)q;
            gno[ng++] = (uint8_t)g;
        }
    }
    return ng;
}

/* The game round under way: the contest's first, the rounds played, the ones skipped. */
static int32_t k_cur(void)
{
    return g_worldPlayoff.g0 + _diyCompetitionStart.base.currentRoundIndex + g_worldPlayoff.skip;
}

/* Spec round r's members, its ties' clubs in order (the pairing applied); -1 when one is not
 * known yet. */
static int32_t k_members(const uint8_t *s, int32_t r, int32_t *mem)
{
    const uint8_t *rd = k_round(s, r);
    const int32_t ties = rd[0], pairing = rd[4];
    for (int32_t i = 0; i < 2 * ties; i++) {
        const int32_t c = rd[6 + i];
        if (c == 0x3F && r > 0) {               /* the previous round's best placed loser */
            const uint8_t *pr = k_round(s, r - 1);
            const int32_t p0 = k_t0(s, r - 1);
            int32_t v = -1;
            for (int32_t t = p0; t < p0 + pr[0]; t++)
                if (g_worldPlayoff.tl[t] != NOC && (v < 0 || k_better(g_worldPlayoff.tl[t], v)))
                    v = g_worldPlayoff.tl[t];
            mem[i] = v;
        } else
            mem[i] = k_member(c);
        if (mem[i] < 0)
            return -1;
    }
    if (pairing && !(pairing & 0x80)) {         /* best v worst: sorted, the worst first */
        const int32_t pn = pairing <= 2 * ties ? pairing : 2 * ties;
        int32_t tmp[2 * 16];
        for (int32_t i = 1; i < pn; i++) {
            const int32_t x = mem[i];
            int32_t j = i;
            while (j > 0 && k_better(x, mem[j - 1])) { mem[j] = mem[j - 1]; j--; }
            mem[j] = x;
        }
        for (int32_t i = 0; i < pn / 2; i++) { tmp[2 * i] = mem[pn - 1 - i]; tmp[2 * i + 1] = mem[i]; }
        for (int32_t i = 0; i < pn; i++) mem[i] = tmp[i];
    }
    return 0;
}

/* Game round g's real ties into tieA/tieB/tieT (the Mod's build_game; ties decided without a
 * match recorded): the count, -1 when a member is not known. presim: the career club waits in
 * round 1, whose ties are simulated now (LeaguePlayoffs_ContestTie, seeded by the caller). */
static int32_t k_build(const uint8_t *s, int32_t g, int32_t presim)
{
    uint8_t gsr[K_GAMES], gno[K_GAMES];
    if (g >= k_games(s, gsr, gno))
        return -1;
    const int32_t r = gsr[g];
    const uint8_t *rd = k_round(s, r);
    const int32_t ties = rd[0], legs = rd[1], host = rd[2] & 0x7F, level = rd[3], gap = rd[5];
    const int32_t t0 = k_t0(s, r);
    int32_t mem[2 * 16], nt = 0;
    if ((legs & 0x80) || ties > 16 || k_members(s, r, mem) < 0)
        return -1;                              /* (a group round: not played here) */
    for (int32_t i = 0; i < ties; i++) {
        const int32_t t = t0 + i;
        int32_t a = mem[2 * i], b = mem[2 * i + 1];
        if (t >= WORLD_PLAYOFF_MAX || g_worldPlayoff.tw[t] != NOC)
            continue;
        const int32_t bt = k_better(a, b) ? a : b, wo = bt == a ? b : a;
        if (gno[g]) {                           /* a series' game 2 / 3: game 2 at the worse placed's */
            a = gno[g] == 1 ? wo : bt;
            b = gno[g] == 1 ? bt : wo;
        } else {
            if (presim && g_worldPlayoff.team[a] != g_worldPlayoff.team[b]) {
                const int32_t w = LeaguePlayoffs_ContestTie(a, b, legs, host, level);
                g_worldPlayoff.tw[t] = (uint8_t)w;
                g_worldPlayoff.tl[t] = (uint8_t)(w == a ? b : a);
                continue;
            }
            if (g_worldPlayoff.team[a] == g_worldPlayoff.team[b]) {    /* (one club twice: no tie) */
                g_worldPlayoff.tw[t] = g_worldPlayoff.tl[t] = (uint8_t)a;
                continue;
            }
            if (g == 0 && gap) {                /* (too far apart: the better placed through) */
                const int32_t d = (int32_t)g_worldPlayoff.pts[a] - (int32_t)g_worldPlayoff.pts[b];
                if (d >= gap || -d >= gap) {
                    g_worldPlayoff.tw[t] = (uint8_t)bt;
                    g_worldPlayoff.tl[t] = (uint8_t)wo;
                    continue;
                }
            }
            if (legs == 3 || host == 3) { a = bt; b = wo; }    /* (no neutral grounds: the better placed's) */
            else if (host == 1) { a = legs == 2 ? wo : bt; b = legs == 2 ? bt : wo; }
        }
        g_worldPlayoff.tieA[nt] = (uint8_t)a;
        g_worldPlayoff.tieB[nt] = (uint8_t)b;
        g_worldPlayoff.tieT[nt] = (uint8_t)t;
        nt++;
    }
    g_worldPlayoff.ties = (uint8_t)nt;
    return nt;
}

/* The clubs a spec round after sr still needs (entrants yet to play, decided ties' clubs), not
 * in the game round's real ties: its byes. */
static int32_t k_waiting(const uint8_t *s, int32_t sr, uint8_t *wait)
{
    uint8_t need[WORLD_PLAYOFF_MAX] = { 0 };
    int32_t nw = 0;
    for (int32_t q = sr + 1; q < s[2]; q++) {
        const uint8_t *rd = k_round(s, q);
        for (int32_t i = 0; i < 2 * rd[0]; i++) {
            const int32_t c = rd[6 + i];
            int32_t v = -1;
            if (c < 0x3F) v = c < g_worldPlayoff.n ? c : -1;
            else if (c >= 0x80) {
                const int32_t t = c & 0x3F;
                v = t < WORLD_PLAYOFF_MAX ? (c & 0x40 ? g_worldPlayoff.tl[t] : g_worldPlayoff.tw[t]) : NOC;
                if (v == NOC) v = -1;
            }
            if (v >= 0) need[v] = 1;
        }
    }
    for (int32_t t = 0; t < g_worldPlayoff.ties; t++)
        need[g_worldPlayoff.tieA[t]] = need[g_worldPlayoff.tieB[t]] = 0;
    for (int32_t i = 0; i < g_worldPlayoff.n; i++)
        if (need[i] && nw < 16)
            wait[nw++] = (uint8_t)i;
    return nw;
}

/* Engine round r's grid (spec round sr): the real ties, the byes, fillers -- its places the
 * engine's halving bracket of the contest's rounds left. */
static void k_lay_out(const uint8_t *s, int32_t r, int32_t sr)
{
    uint8_t *grid = _diyCompetitionStart.teamGridPairings, wait[16];
    uint8_t gsr[K_GAMES], gno[K_GAMES];
    const int32_t left = _diyCompetitionStart.base.numRounds - r - 1;
    const int32_t last = g_worldPlayoff.g0 + r + g_worldPlayoff.skip + 1 >= k_end(k_games(s, gsr, gno));
    const int32_t nw = last ? 0 : k_waiting(s, sr, wait);   /* (a part's last round: no byes, the next */
    int32_t pairs = left >= 0 ? 1 << left : 1, k = 0;       /*  part lays its own out) */
    while (pairs < g_worldPlayoff.ties + nw && pairs < 32)  /* (the halving bracket, wider where a round */
        pairs <<= 1;                                         /*  needs it: the MLS's wild card, 2 ties) */
    s_pairs = pairs;
    for (int32_t t = 0; t < g_worldPlayoff.ties && k < pairs; t++, k++) {
        grid[2 * k] = g_worldPlayoff.tieA[t];
        grid[2 * k + 1] = g_worldPlayoff.tieB[t];
    }
    for (int32_t i = 0; i < nw && k < pairs; i++, k++)
        grid[2 * k] = grid[2 * k + 1] = wait[i];
    for (; k < pairs; k++)
        grid[2 * k] = grid[2 * k + 1] = nw ? wait[0] : g_worldPlayoff.tieA[0];
}

/* The contest's names: the pack's (world_data.c), else PLAY-OFF. */
static void k_names(uint8_t country, int32_t pidx)
{
    int32_t count = 0;
    const LeaguePlayoffs *all = World_LeaguePlayoffs(&count);
    const char *ln = "PLAY-OFF", *sh = "PLAY-OFF";
    for (int32_t i = 0; i < count; i++)
        if (all[i].country == country && all[i].names && pidx < all[i].nnames && all[i].names[pidx][0]) {
            ln = all[i].names[pidx][0];
            sh = all[i].names[pidx][1];
        }
    int32_t k = 0;
    for (; ln[k] && k < (int32_t)sizeof s_kName - 1; k++) s_kName[k] = ln[k];
    s_kName[k] = 0;
    for (k = 0; sh[k] && k < (int32_t)sizeof s_kShort - 1; k++) s_kShort[k] = sh[k];
    s_kShort[k] = 0;
}

/* The career club's play-off pidx: its entrants, its first round with the club in it, its
 * bracket fitting; 1: the state made (WORLD_PLAYOFF_SPEC). */
static int32_t k_make_ex(uint8_t country, int32_t ntab, const int32_t *divs, const LeagueStageRow *const *tables,
                         const int32_t *ns, int32_t pidx, int32_t stage)
{
    const uint8_t *s = 0;
    extern uint16_t _selTeamNumber;
    WorldPlayoff_Reset();
    const int32_t ne = LeaguePlayoffs_ContestEntrantsEx(country, ntab, divs, tables, ns, pidx, g_worldPlayoff.team,
                                                        g_worldPlayoff.glob, g_worldPlayoff.prio,
                                                        g_worldPlayoff.pts, &s);
    if (ne < 2 || !s || s[2] < 1 || s[2] > 8) {
        WorldPlayoff_Reset();
        return 0;
    }
    g_worldPlayoff.kind = WORLD_PLAYOFF_SPEC;
    g_worldPlayoff.n = (uint8_t)ne;
    k_names(country, pidx);
    g_worldPlayoff.country = country;
    g_worldPlayoff.pidx = (uint8_t)pidx;
    g_worldPlayoff.stage = (uint8_t)stage;
    g_worldPlayoff.season = (uint8_t)(g_worldCups.seasons + 1);
    for (int32_t t = 0; t < WORLD_PLAYOFF_MAX; t++)
        g_worldPlayoff.tw[t] = g_worldPlayoff.tl[t] = NOC;
    uint8_t gsr[K_GAMES], gno[K_GAMES];
    const int32_t ng = k_games(s, gsr, gno);
    int32_t ok = 1, in = 0, in0 = 0;
    for (int32_t q = 0; q < s[2]; q++) {        /* (no group round; a series not last: ko_build) */
        const uint8_t *rd = k_round(s, q);
        if ((rd[1] & 0x80) || (rd[1] == 3 && q == s[2] - 1))
            ok = 0;
    }
    for (int32_t i = 0; i < ne; i++)
        in |= g_worldPlayoff.team[i] == _selTeamNumber;
    {   /* the club in a round-1 tie (one with itself -- Malta's split seeds -- is no match) */
        const uint8_t *rd = k_round(s, 0);
        for (int32_t i = 0; i < rd[0]; i++) {
            const int32_t a = rd[6 + 2 * i], b = rd[7 + 2 * i];
            if (a < ne && b < ne && g_worldPlayoff.team[a] != g_worldPlayoff.team[b] &&
                (g_worldPlayoff.team[a] == _selTeamNumber || g_worldPlayoff.team[b] == _selTeamNumber))
                in0 = 1;
        }
    }
    const int32_t presim = in && !in0;          /* (it waits: round 1 simulated now, the contest after it) */
    if (presim)
        LeaguePlayoffs_ContestSeed(pidx, g_worldPlayoff.team, ne);
    int32_t g0 = 0, nt = ok ? k_build(s, 0, presim) : -1;
    while (nt == 0 && g0 + 1 < ng && gno[g0 + 1] == 0)
        nt = k_build(s, ++g0, 0);
    g_worldPlayoff.g0 = (uint8_t)g0;
    if (ng - g0 > 6)                            /* (more rounds than the engine's six: in parts, the */
        g_worldPlayoff.gend = (uint8_t)(ng - 6);    /*  first the rounds past six -- the MLS's wild card) */
    const int32_t gr = k_end(ng) - g0;
    for (int32_t g = g0; g < k_end(ng); g++)    /* (every round's ties in the engine's grid of 32) */
        if (!gno[g] && k_round(s, gsr[g])[0] > 32)
            nt = -1;
    in = 0;
    for (int32_t t = 0; t < nt; t++)
        in |= g_worldPlayoff.team[g_worldPlayoff.tieA[t]] == _selTeamNumber ||
              g_worldPlayoff.team[g_worldPlayoff.tieB[t]] == _selTeamNumber;
    uint8_t wait[16];
    if (nt <= 0 || !in || ne > 32 || gr > 6 ||
        nt + (g0 + 1 >= k_end(ng) ? 0 : k_waiting(s, gsr[g0], wait)) > 32) {
        if (presim)
            SWOS_DBG("[world-playoff] play-off %d: not played (%d game rounds from %d, %d ties)\n", (int)pidx,
                     (int)ng, (int)g0, (int)nt);
        WorldPlayoff_Reset();                   /* (not the club's to play: the season end decides) */
        return 0;
    }
    if (g0)
        SWOS_DBG("[world-playoff] play-off %d: its first %d game rounds decided at the build (the club %04X)\n",
                 (int)pidx, (int)g0, (unsigned)_selTeamNumber);
    return 1;
}

static int32_t k_make(uint8_t country, int32_t d, const LeagueStageRow *table, int32_t n, int32_t pidx,
                      int32_t stage)
{
    const LeagueStageRow *tables[1] = { table };
    const int32_t divs[1] = { d }, ns[1] = { n };
    return k_make_ex(country, 1, divs, tables, ns, pidx, stage);
}

/* The decided contest's result: a stage title for the season end. */
static void k_done(void)
{
    const uint8_t *s = k_spec();
    if (!s || g_worldPlayoff.stage > 2)
        return;
    const int32_t last = k_t0(s, s[2] - 1) + k_round(s, s[2] - 1)[0] - 1;
    if (last < 0 || last >= WORLD_PLAYOFF_MAX || g_worldPlayoff.tw[last] == NOC)
        return;
    WorldStageTitle *x = &g_worldStageTitle[g_worldPlayoff.stage];
    const int32_t w = g_worldPlayoff.tw[last], l = g_worldPlayoff.tl[last];
    x->season = g_worldPlayoff.season;
    x->done = 1;
    x->team[0] = g_worldPlayoff.team[w];
    x->glob[0] = g_worldPlayoff.glob[w];
    x->team[1] = l != NOC && l != w ? g_worldPlayoff.team[l] : 0xFFFF;
    x->glob[1] = l != NOC && l != w ? g_worldPlayoff.glob[l] : 0xFFFF;
    SWOS_DBG("[world-playoff] stage %d title: %04X (runner-up %04X)\n", (int)g_worldPlayoff.stage,
             (unsigned)x->team[0], (unsigned)x->team[1]);
    {   /* the play-off built when this one ends ('then_build': Mexico's Campeon de Campeones),
         * at the calendar's advance, from the same stage's table */
        int32_t count = 0;
        const LeaguePlayoffs *all = World_LeaguePlayoffs(&count);
        for (int32_t i = 0; i < count; i++)
            if (all[i].country == g_worldPlayoff.country && all[i].then && g_worldPlayoff.pidx < all[i].nnames &&
                all[i].then[g_worldPlayoff.pidx] && s_pendStage.n) {
                s_pendStage.on = 1;
                s_pendStage.pidx = (uint8_t)(all[i].then[g_worldPlayoff.pidx] - 1);
            }
    }
}

void WorldPlayoff_ResetStageTitles(void)
{
    uint8_t *p = (uint8_t *)g_worldStageTitle;
    for (uint32_t i = 0; i < sizeof g_worldStageTitle; i++)
        p[i] = 0;
}

const WorldStageTitle *WorldPlayoff_StageTitle(int32_t stage)
{
    const WorldStageTitle *x = stage >= 0 && stage < 3 ? &g_worldStageTitle[stage] : 0;
    return x && x->done && x->season == (uint8_t)(g_worldCups.seasons + 1) ? x : 0;
}

int32_t WorldPlayoff_PlayedTie(int32_t pidx, uint16_t a, uint16_t b, uint16_t *winner)
{
    const WorldPlayoffState *v = &g_worldPlayoff;
    if (v->kind != WORLD_PLAYOFF_SPEC || v->pidx != pidx || v->season != (uint8_t)(g_worldCups.seasons + 1))
        return 0;
    for (int32_t t = 0; t < WORLD_PLAYOFF_MAX; t++) {
        if (v->tw[t] == NOC || v->tl[t] == NOC || v->tw[t] == v->tl[t])
            continue;
        const uint16_t w = v->team[v->tw[t]], l = v->team[v->tl[t]];
        if ((w == a && l == b) || (w == b && l == a)) {
            *winner = w;
            return 1;
        }
    }
    return 0;
}

static void put_in_slot4(void);
static void k_slot4(void);

/* A stage's end (league_split.c, inside the league's match day): its table kept, the build
 * deferred to the calendar's advance (the Mod's gp_defer) -- slot 4 is made through the active
 * competition, the league's own while its day is played. */


void WorldPlayoff_StageEnded(uint8_t country, uint16_t division, int32_t pidx, uint8_t week)
{
    DIYCompetitionEx *ex = &_diyCompetitionStart;
    const int32_t n = (uint16_t)ex->base.numTeams;
    if (n < 2 || n > 32 || pidx < 0 || pidx > 255)
        return;
    for (int32_t k = 0; k < n; k++) {
        const DIYTeamStats *st = (const DIYTeamStats *)((uint8_t *)&ex->base +
                                 ex->matchData.league.teamEntryOffsets[k] + DIY_STANDINGS_OFFSET);
        const TeamFileEx *t = (const TeamFileEx *)diy_resolve_team(st->teamSelOffset);
        LeagueStageRow *r = &s_pendStage.table[k];
        r->team = PACK_BE_U16(t->team.countryNumber, t->team.teamOrdinal);
        r->glob = (uint16_t)t->team.globalTeamNumber;
        r->won = st->wins; r->gf = st->goalsFor; r->ga = st->goalsAgainst; r->pts = st->points;
    }
    s_pendStage.on = 1;
    s_pendStage.country = country;
    s_pendStage.division = (uint8_t)division;
    s_pendStage.pidx = (uint8_t)pidx;
    s_pendStage.week = week;
    s_pendStage.n = (uint8_t)n;
}

/* The calendar's advance (career_teams.c, beside CupD_Step): a stage's knockout due, built
 * into slot 4 when the career club plays its first round (else the season end decides it). */
void WorldPlayoff_Advance(uint16_t week)
{
    if (g_worldPlayoff.kind == WORLD_PLAYOFF_SPEC && g_worldPlayoff.more) {
        /* a contest's next part (its first part's rounds decided): from the game round after,
         * the career club in it -- else the season end decides the rest, the part's ties kept */
        const uint8_t *s = k_spec();
        uint8_t gsr[K_GAMES], gno[K_GAMES];
        extern uint16_t _selTeamNumber;
        g_worldPlayoff.more = 0;
        if (!s)
            return;
        const int32_t ng = k_games(s, gsr, gno);
        int32_t g0 = g_worldPlayoff.gend, nt = k_build(s, g0, 0), in = 0;
        while (nt == 0 && g0 + 1 < ng && gno[g0 + 1] == 0)
            nt = k_build(s, ++g0, 0);
        for (int32_t t = 0; t < nt; t++)
            in |= g_worldPlayoff.team[g_worldPlayoff.tieA[t]] == _selTeamNumber ||
                  g_worldPlayoff.team[g_worldPlayoff.tieB[t]] == _selTeamNumber;
        if (nt <= 0 || !in) {
            SWOS_DBG("[world-playoff] its next part without the career club: the season end decides it\n");
            g_worldPlayoff.gend = (uint8_t)ng;
            return;
        }
        g_worldPlayoff.g0 = (uint8_t)g0;
        g_worldPlayoff.skip = 0;
        g_worldPlayoff.gend = (uint8_t)(ng - g0 > 6 ? ng - 6 : 0);
        g_worldPlayoff.week = (uint8_t)(week + 1 < 255 ? week + 1 : 255);
        SWOS_DBG("[world-playoff] its next part: game rounds %d.., from week %u\n", (int)g0, (unsigned)g_worldPlayoff.week);
        k_slot4();
        return;
    }
    if (!s_pendStage.on)
        return;
    s_pendStage.on = 0;
    const uint8_t *s = LeaguePlayoffs_SpecAt(s_pendStage.country, s_pendStage.pidx);
    if (!s || (g_worldPlayoff.kind && !g_worldPlayoff.winner))
        return;                                 /* (a contest under way in slot 4: none) */
    if (_diyCompetitionPtr && !g_worldPlayoff.kind && CupD_InSlot4() != 1)
        return;
    if (!k_make(s_pendStage.country, s_pendStage.division, s_pendStage.table, s_pendStage.n, s_pendStage.pidx,
                s[3] & 3))
        return;
    /* its weeks: from the week after the stage's last (and after this one), a leg a week */
    const int32_t w = s_pendStage.week + 1 > week + 1 ? s_pendStage.week + 1 : week + 1;
    g_worldPlayoff.week = (uint8_t)(w < 255 ? w : 255);
    SWOS_DBG("[world-playoff] stage %d knockout (play-off %d): %u clubs, from week %u\n", (int)(s[3] & 3),
             (int)s_pendStage.pidx, (unsigned)g_worldPlayoff.n, (unsigned)g_worldPlayoff.week);
    k_slot4();
}

/* ---- A group contest the career club plays (the Mod's ko_t2: the Poule Scudetto, the Serie D
 * groups' winners) ----
 * A title of g groups of m clubs, then knockout stages (LeaguePlayoffs_GroupShape), played as the
 * game's own type-2 contest -- the Champions Cup's header, its groups, the best runners-up as its
 * best thirds, its knockouts -- in slot 3 (index GROUPS_INDEX), from the week after the league's
 * last; the other divisions its clubs come from simulated now (up to nine) and kept for the
 * season end. The last decisive match is the final: its winner and loser are the champion and the
 * runner-up the season end takes (WorldPlayoff_PlayedTitle, the Mod's pl_t2). */
#define GROUPS_INDEX 9
static CompetitionTournament s_groups;
extern CompetitionTournament _euroCup;

static uint8_t *groups_record(void)
{
    LeagueGroupShape x;
    if (g_worldPlayoff.kind != WORLD_PLAYOFF_GROUPS ||
        !LeaguePlayoffs_GroupShape(g_worldPlayoff.country, g_worldPlayoff.pidx, &x))
        return 0;
    CompetitionTournament *t = &s_groups;
    int32_t days = x.meet * (x.m & 1 ? x.m : x.m - 1);
    for (int32_t i = 0; i < x.nk; i++)
        days += (x.kb[i] & 0x80) ? 2 : 1;
    *t = _euroCup;
    t->header.comp.compId = (int8_t)COMPID_worldGroups;
    t->header.comp.teamNumber = (int8_t)g_worldPlayoff.country;      /* ("ITALIAN ...") */
    t->header.comp.beginMonth = (int8_t)g_worldPlayoff.week;
    t->header.comp.endMonth = (int8_t)(g_worldPlayoff.week + days < 255 ? g_worldPlayoff.week + days : 255);
    t->header.comp.seedReorderFlag = 1;         /* (the entrants in group order) */
    t->header.seedingType = 0;                  /* (no seeding) */
    t->header.menuState = 0;                    /* (no away goals) */
    t->header.playEachTeam = (int8_t)x.meet;
    t->header.roundStringBase = (int8_t)(1 + x.nk);     /* (the stages) */
    uint8_t *z = (uint8_t *)t->header.rounds;
    for (uint32_t i = 0; i < sizeof t->header.rounds; i++)
        z[i] = 0;
    t->header.rounds[0].teamCount = (int8_t)g_worldPlayoff.n;
    t->header.rounds[0].groupCount = (int8_t)x.g;
    t->header.rounds[0].teamGroupCount = (int8_t)x.m;
    for (int32_t i = 0; i < x.nk && 1 + i < 6; i++) {
        t->header.rounds[1 + i].teamCount = (int8_t)(i ? x.kt[i - 1] : x.q);
        t->header.rounds[1 + i].teamGroupCount = (int8_t)(2 * x.kt[i]);
    }
    if (1 + x.nk < 6)
        t->header.rounds[1 + x.nk].teamCount = (int8_t)x.kt[x.nk - 1];
    for (int32_t k = 0; k < 6; k++)
        t->roundRuleFlags[k] = (int8_t)(k == 0 || k > x.nk ? 0 : x.kb[k - 1]);
    t->name[0] = s_kName;
    t->name[1] = s_kShort;
    for (int32_t i = 0; i < 32; i++) {
        const uint16_t w = i < g_worldPlayoff.n ? g_worldPlayoff.team[i] : 0;
        t->teamSeedList[i].countryNumber = (uint8_t)(w >> 8);
        t->teamSeedList[i].teamOrdinal = (uint8_t)w;
    }
    return (uint8_t *)t;
}

static int32_t groups_playoff(uint8_t country, int32_t d, const LeagueStageRow *own, int32_t nown, int32_t pidx,
                              const uint8_t *other, int32_t no)
{
    static LeagueStageRow tab[WORLD_PLAYOFF_EARLY + 1][32];
    static TableRow early[32];
    static WorldPlayoffRow rows[WORLD_PLAYOFF_EARLY][WORLD_PLAYOFF_EARLY_ROWS];
    extern uint16_t _selTeamNumber;
    extern uint8_t *_quaternaryCompetitionPtr;
    extern uint8_t _teamSelectionKeyCache[];
    extern uint16_t _teamSelectionKeyCacheOffset;
    const LeagueStageRow *tables[WORLD_PLAYOFF_EARLY + 1];
    int32_t divs[WORLD_PLAYOFF_EARLY + 1], ns[WORLD_PLAYOFF_EARLY + 1];
    LeagueGroupShape x;
    const uint8_t *s = 0;
    if (_quaternaryCompetitionPtr || no > WORLD_PLAYOFF_EARLY || !LeaguePlayoffs_GroupShape(country, pidx, &x)) {
        SWOS_DBG("[world-playoff] play-off %d: a group contest, decided at the season end\n", (int)pidx);
        return 0;                               /* (slot 3 taken: the season end decides it) */
    }
    divs[0] = d; ns[0] = nown; tables[0] = tab[0];
    for (int32_t k = 0; k < nown && k < 32; k++) tab[0][k] = own[k];
    for (int32_t e = 0; e < no; e++) {          /* (the other divisions, now: as the season end will) */
        const int32_t n = simulate_early(other[e], early, 32);
        if (n > WORLD_PLAYOFF_EARLY_ROWS)
            return 0;
        divs[e + 1] = other[e]; ns[e + 1] = n; tables[e + 1] = tab[e + 1];
        for (int32_t k = 0; k < n; k++) {
            LeagueStageRow *r = &tab[e + 1][k];
            r->team = early[k].team; r->glob = early[k].glob; r->won = early[k].r.won;
            r->gf = early[k].r.gf; r->ga = early[k].r.ga; r->pts = early[k].r.pts;
            rows[e][k] = early[k].r;
        }
    }
    WorldPlayoff_Reset();
    const int32_t ne = LeaguePlayoffs_ContestEntrantsEx(country, no + 1, divs, tables, ns, pidx, g_worldPlayoff.team,
                                                        g_worldPlayoff.glob, g_worldPlayoff.prio, g_worldPlayoff.pts, &s);
    int32_t in = 0;
    for (int32_t i = 0; i < ne; i++)
        in |= g_worldPlayoff.team[i] == _selTeamNumber;
    if (ne < 2 || ne > 32 || !in) {
        WorldPlayoff_Reset();
        return 0;
    }
    g_worldPlayoff.kind = WORLD_PLAYOFF_GROUPS;
    g_worldPlayoff.n = (uint8_t)ne;
    g_worldPlayoff.country = country;
    g_worldPlayoff.pidx = (uint8_t)pidx;
    g_worldPlayoff.stage = 0xFF;
    g_worldPlayoff.season = (uint8_t)(g_worldCups.seasons + 1);
    for (int32_t t = 0; t < WORLD_PLAYOFF_MAX; t++)
        g_worldPlayoff.tw[t] = g_worldPlayoff.tl[t] = NOC;
    {
        const uint8_t now = ((const TeamSelectionKeyCacheEntry *)(_teamSelectionKeyCache +
                             _teamSelectionKeyCacheOffset))->sortKey;
        g_worldPlayoff.week = (uint8_t)(now < 254 ? now + 1 : 255);
    }
    g_worldPlayoff.early = (uint8_t)no;
    for (int32_t e = 0; e < no; e++) {
        g_worldPlayoff.earlyDiv[e] = other[e];
        g_worldPlayoff.earlyN[e] = (uint8_t)ns[e + 1];
        for (int32_t k = 0; k < ns[e + 1]; k++)
            g_worldPlayoff.earlyRow[e][k] = rows[e][k];
    }
    k_names(country, pidx);
    {   /* into slot 3, as another confederation's main round (conti_season.c into_slot3) */
        extern uint16_t _teamSelectionKeyCacheIndex;
        uint16_t Career_SelectEuroCupSlotIfTeamParticipates(uint8_t *cupCopy, uint16_t euroCupIndex);
        const uint16_t idx = _teamSelectionKeyCacheIndex, off = _teamSelectionKeyCacheOffset;
        Career_RetireSlotCalendarEntries(3);
        Career_SelectEuroCupSlotIfTeamParticipates(groups_record(), GROUPS_INDEX);
        _teamSelectionKeyCacheIndex = idx;
        _teamSelectionKeyCacheOffset = off;
        _competitionSlotUpdatedFlag = 1;
    }
    SWOS_DBG("[world-playoff] %s: %d clubs in groups, %d other division(s) simulated early, from week %u\n",
             s_kName, (int)ne, (int)no, (unsigned)g_worldPlayoff.week);
    return 1;
}

uint8_t *WorldPlayoff_GroupsRecord(void)
{
    if (g_worldPlayoff.kind != WORLD_PLAYOFF_GROUPS)
        return 0;
    k_names(g_worldPlayoff.country, g_worldPlayoff.pidx);
    return groups_record();
}

int32_t WorldPlayoff_PlayedTitle(int32_t pidx, uint16_t *champion, uint16_t *runnerUp)
{
    const WorldPlayoffState *v = &g_worldPlayoff;
    if (v->kind != WORLD_PLAYOFF_GROUPS || v->pidx != pidx || v->season != (uint8_t)(g_worldCups.seasons + 1) ||
        v->tw[0] == NOC || v->tw[0] >= v->n)
        return 0;
    *champion = v->team[v->tw[0]];
    *runnerUp = v->tl[0] < v->n ? v->team[v->tl[0]] : 0xFFFF;
    return 1;
}

/* The career club's play-off of the pack's, built at the league's end (the Mod's gp_need +
 * ko_build, a career): the first it can play with the club among its entrants -- a title (AUS,
 * CAN, MLT, PER, URU, the MLS), a promotion play-off (England's, a barrage), a continental one;
 * the other divisions its entrants come from (France's and Germany's barrages: the division
 * above or below) simulated now, at most two, their tables kept for the season end
 * (WorldPlayoff_RestoreEarlyTable, the Mod's gp_simkeep / gp_restore). A group contest (the
 * Mod's type 2) and a play-off not the club's to start are decided at the season end. */
static int32_t spec_playoff(const LeaguePlayoffs *lp, int32_t d, const TableRow *own, int32_t nown)
{
    static LeagueStageRow table[3][32];
    static TableRow early[32];
    extern uint16_t _selTeamNumber;
    const uint8_t country = (uint8_t)lp->country;
    uint8_t other[WORLD_PLAYOFF_EARLY];
    int32_t no = 0, divs[3] = { d, 0, 0 }, ns[3] = { nown < 32 ? nown : 32, 0, 0 };
    const LeagueStageRow *tables[3] = { table[0], table[1], table[2] };
    for (int32_t k = 0; k < ns[0]; k++) {
        table[0][k].team = own[k].team; table[0][k].glob = own[k].glob; table[0][k].won = own[k].r.won;
        table[0][k].gf = own[k].r.gf; table[0][k].ga = own[k].r.ga; table[0][k].pts = own[k].r.pts;
    }
    const int32_t pidx = LeaguePlayoffs_ClubPlayoff(country, d, table[0], ns[0], _selTeamNumber, other, &no);
    if (pidx < -1)
        return groups_playoff(country, d, table[0], ns[0], -2 - pidx, other, no);
    if (pidx < 0)
        return 0;
    WorldPlayoffRow rows[2][WORLD_PLAYOFF_EARLY_ROWS];
    for (int32_t e = 0; e < no; e++) {          /* (the other divisions, now: as the season end will) */
        const int32_t n = simulate_early(other[e], early, 32);
        if (n > WORLD_PLAYOFF_EARLY_ROWS)
            return 0;                           /* (more clubs than an early table keeps) */
        divs[e + 1] = other[e];
        ns[e + 1] = n;
        for (int32_t k = 0; k < n; k++) {
            LeagueStageRow *r = &table[e + 1][k];
            r->team = early[k].team; r->glob = early[k].glob; r->won = early[k].r.won;
            r->gf = early[k].r.gf; r->ga = early[k].r.ga; r->pts = early[k].r.pts;
            rows[e][k] = early[k].r;
        }
    }
    if (!k_make_ex(country, no + 1, divs, tables, ns, pidx, 0xFF))
        return 0;
    g_worldPlayoff.early = (uint8_t)no;         /* (k_make_ex reset the state: kept now) */
    for (int32_t e = 0; e < no; e++) {
        g_worldPlayoff.earlyDiv[e] = other[e];
        g_worldPlayoff.earlyN[e] = (uint8_t)ns[e + 1];
        for (int32_t k = 0; k < ns[e + 1]; k++)
            g_worldPlayoff.earlyRow[e][k] = rows[e][k];
    }
    if (no)
        SWOS_DBG("[world-playoff] play-off %d: %d other division(s) simulated early\n", (int)pidx, (int)no);
    return 1;
}

static int32_t league_ended(void);

/* A decided stage title's contest stays in slot 4 (its matches in the career's list) unless
 * the league's end builds another there. */
int32_t WorldPlayoff_CareerLeagueEnded(void)
{
    static WorldPlayoffState keep;
    const int32_t stage = g_worldPlayoff.kind == WORLD_PLAYOFF_SPEC && g_worldPlayoff.winner &&
                          g_worldPlayoff.stage <= 2;
    if (stage)
        keep = g_worldPlayoff;
    if (league_ended())
        return 1;
    if (stage) {
        g_worldPlayoff = keep;
        return 1;
    }
    return 0;
}

static int32_t league_ended(void)
{
    DIYCompetitionEx *ex = &_diyCompetitionStart;
    int32_t count = 0;
    const LeaguePlayoffs *all = World_LeaguePlayoffs(&count), *lp = 0;
    if (g_worldPlayoff.kind && !(g_worldPlayoff.kind == WORLD_PLAYOFF_SPEC && g_worldPlayoff.winner))
        return 1;                               /* (built already: slot 4 is ours; a decided stage
                                                   title's contest gives way) */
    if (_diyCompetitionPtr && CupD_InSlot4() != 1)
        return 0;                               /* (slot 4 is 96/97's; a decided Coppa D tie gives way) */
    for (int32_t i = 0; i < count; i++)
        if (all[i].country == _careerTeam.team.countryNumber)
            lp = &all[i];
    if (!lp || ex->base.competitionType != 0)
        return 0;

    const int32_t n = (uint16_t)ex->base.numTeams;
    if (n < 4 || n > 32)
        return 0;
#ifdef SWOS_SDL_BUILD
    if (force_place(ex, n))
        Career_StoreCompetitionSlotStateToBuffer((uint8_t *)&_primaryCompetitionBuf);
#endif
    static uint16_t team[32], pts[32];      /* (static: in BSS, no memset on the Amiga) */
    static TableRow own[32];
    int32_t mine = -1;
    for (int32_t k = 0; k < n; k++) {
        const DIYTeamStats *st = (const DIYTeamStats *)((uint8_t *)&ex->base +
                                 ex->matchData.league.teamEntryOffsets[k] + DIY_STANDINGS_OFFSET);
        const TeamFileEx *t = (const TeamFileEx *)diy_resolve_team(st->teamSelOffset);
        team[k] = PACK_BE_U16(t->team.countryNumber, t->team.teamOrdinal);
        pts[k] = st->points;
        if (st->teamSelOffset == -1)
            mine = k;
    }
    const int32_t nown = read_table(own, 32);
    /* Serie B's play-off (dplay.c dp_lend): the 3rd-8th, unless the 3rd leads the 4th by
     * more than 14 points (it goes up directly); the list 5th, 8th, 6th, 7th, then the
     * 3rd and the 4th each with itself (its bye). */
    static const uint8_t kPlayoffPlaces[8] = { 4, 7, 5, 6, 2, 2, 3, 3 };
    const int32_t serieB = lp->italy && _currentDivisionIndex == lp->serieB && n >= 17;
    WorldPlayoff_Reset();
    const int32_t serieC = lp->italy && (int32_t)_currentDivisionIndex >= lp->groupFirst &&
                           (int32_t)_currentDivisionIndex < lp->groupFirst + lp->groups;
    /* (the cup winner placed 11th or lower plays in its place: in the cup, so it is known) */
    if (serieC && mine >= 1 &&
        (mine <= 10 || WorldCups_LeagueCupWinner((uint8_t)_careerTeam.team.countryNumber) == team[mine])) {
        /* the play-off (2nd-10th, or the cup place); else its play-out, if any */
        if (!c_playoff(lp, own, nown, mine)) {
            const uint8_t early = g_worldPlayoff.early;
            WorldPlayoffRow keep[2][WORLD_PLAYOFF_EARLY_ROWS];
            uint8_t kd[2], kn[2];
            for (int32_t e = 0; e < 2; e++) {   /* (simulated already: kept all the same) */
                kd[e] = g_worldPlayoff.earlyDiv[e]; kn[e] = g_worldPlayoff.earlyN[e];
                for (int32_t k = 0; k < WORLD_PLAYOFF_EARLY_ROWS; k++) keep[e][k] = g_worldPlayoff.earlyRow[e][k];
            }
            WorldPlayoff_Reset();
            if (!early || !group_tie(lp, (int32_t)_currentDivisionIndex, team, pts, n, mine))
                return 0;
            g_worldPlayoff.early = early;
            for (int32_t e = 0; e < 2; e++) {
                g_worldPlayoff.earlyDiv[e] = kd[e]; g_worldPlayoff.earlyN[e] = kn[e];
                for (int32_t k = 0; k < WORLD_PLAYOFF_EARLY_ROWS; k++) g_worldPlayoff.earlyRow[e][k] = keep[e][k];
            }
        }
    } else if (!serieB) {
        if (!group_tie(lp, (int32_t)_currentDivisionIndex, team, pts, n, mine) &&
            !group_placement(lp, (int32_t)_currentDivisionIndex, team, pts, n, mine) &&
            !spec_playoff(lp, (int32_t)_currentDivisionIndex, own, nown))
            return 0;
    } else if (mine >= 2 && mine <= 7 && LeaguePlayoffs_PlayoffHeld(pts[2], pts[3])) {
        g_worldPlayoff.kind = WORLD_PLAYOFF_B_PLAYOFF;
        g_worldPlayoff.n = 8;
        for (uint8_t i = 0; i < 8; i++) {
            g_worldPlayoff.team[i] = team[kPlayoffPlaces[i]];
            g_worldPlayoff.prio[i] = kPlayoffPlaces[i];
            g_worldPlayoff.pts[i] = pts[kPlayoffPlaces[i]];
        }
    /* Serie B's play-out (dplay.c dp_lend): the 16th and the 17th, held unless the 16th
     * leads by more than 4 points; the first leg at the 17th's. */
    } else if ((mine == 15 || mine == 16) && LeaguePlayoffs_OutHeld(pts[15], pts[16])) {
        g_worldPlayoff.kind = WORLD_PLAYOFF_B_PLAYOUT;
        g_worldPlayoff.n = 2;
        g_worldPlayoff.etp = pts[15] == pts[16];
        g_worldPlayoff.team[0] = team[16]; g_worldPlayoff.prio[0] = 16; g_worldPlayoff.pts[0] = pts[16];
        g_worldPlayoff.team[1] = team[15]; g_worldPlayoff.prio[1] = 15; g_worldPlayoff.pts[1] = pts[15];
    } else
        return 0;

    if (g_worldPlayoff.kind == WORLD_PLAYOFF_SPEC && !g_worldPlayoff.week) {
        /* a league ending after 96/97's play-off dates (Germany's, France's): its days from the
         * week after, a leg a week -- dated before the calendar's place, the walk went back */
        extern uint8_t _teamSelectionKeyCache[];
        extern uint16_t _teamSelectionKeyCacheOffset;
        const uint8_t now = ((const TeamSelectionKeyCacheEntry *)(_teamSelectionKeyCache +
                             _teamSelectionKeyCacheOffset))->sortKey;
        if ((uint8_t)_div1Playoffs.comp.beginMonth <= now)
            g_worldPlayoff.week = (uint8_t)(now < 254 ? now + 1 : 255);
    }
    if (g_worldPlayoff.kind == WORLD_PLAYOFF_GROUPS)
        return 1;                               /* (in slot 3: groups_playoff) */
    put_in_slot4();
    SWOS_DBG("[world-playoff] %s: %u clubs, the career club %d%s\n",
             g_worldPlayoff.kind == WORLD_PLAYOFF_C_PLAYOFF ? "PLAY-OFF SERIE C" :
             g_worldPlayoff.kind == WORLD_PLAYOFF_B_PLAYOFF ? "PLAY-OFF SERIE B" :
             g_worldPlayoff.kind == WORLD_PLAYOFF_GROUP_TIE ? s_tieName :
             g_worldPlayoff.kind == WORLD_PLAYOFF_SPEC ? s_kName :
             g_worldPlayoff.kind == WORLD_PLAYOFF_PLACEMENT ? s_pName : "PLAY-OUT SERIE B",
             (unsigned)g_worldPlayoff.n, (int)mine + 1, g_worldPlayoff.etp ? ", e.t. + penalties" : "");
    return 1;
}

/* Into slot 4, as 96/97's play-offs: the record (its calendar), then its clubs. */
static void put_in_slot4(void)
{
    make_record();
    _diyCompetitionTablePtr = (uint32_t *)0;
    Career_RetireSlotCalendarEntries(4);  /* (the slot's earlier contest's entries) */
    Career_SetCurrentCompetitionSlot((uint8_t *)&s_rec, 4, 0, 0);
    {
        uint8_t *ctx = (uint8_t *)&_diyCompetitionContextBuf;
        uint16_t ids[WORLD_PLAYOFF_MAX];
        for (uint8_t i = 0; i < g_worldPlayoff.n; i++)
            BE_WRITE_U16(&ids[i], g_worldPlayoff.team[i]);
        AppendTeamsToSelectionFromTmdOrTeamFile(((DIYCompetitionEx *)ctx)->cupTeamIds, ids,
                                                g_worldPlayoff.n);
        Career_LoadCompetitionSlotStateFromBuffer(ctx);
        first_round();
        DIY_BuildTeamSelectionPointerCache();
        DIY_UpdateCompetitionProgressOrAdvanceSlot((uint8_t *)&_diyCompetitionStart);
        Career_StoreCompetitionSlotStateToBuffer(ctx);
        _competitionSlotUpdatedFlag = 1;
        _diyCompetitionContextBuf.tourn.header.slotStateA = _primaryCompetitionBuf.tourn.header.slotStateA;
        _diyCompetitionContextBuf.tourn.header.slotStateB = _primaryCompetitionBuf.tourn.header.slotStateB;
        _diyCompetitionContextBuf.tourn.header.slotStateC = _primaryCompetitionBuf.tourn.header.slotStateC;
    }
}

static void k_slot4(void)
{
    put_in_slot4();
}

static int32_t ours(void)
{
    return g_worldPlayoff.kind && (uint8_t)_diyCompetitionStart.base.field_2E == COMPID_worldPlayoff;
}

static int32_t prio_of(uint16_t team)
{
    for (uint8_t i = 0; i < g_worldPlayoff.n; i++)
        if (g_worldPlayoff.team[i] == team)
            return g_worldPlayoff.prio[i];
    return 0xFF;
}

/* A tie of the contest: the competition under way is ours and both clubs are entrants
 * (the id alone is not enough: a later cup's state can keep it). */
static int32_t our_tie(uint16_t team1, uint16_t team2)
{
    return ours() && prio_of(team1) != 0xFF && prio_of(team2) != 0xFF;
}

int32_t WorldPlayoff_LevelWinner(uint16_t team1, uint16_t team2)
{
    if (!our_tie(team1, team2))
        return 0;
    if (g_worldPlayoff.kind == WORLD_PLAYOFF_SPEC) {   /* (the round's level: the better placed?) */
        const uint8_t *sp = k_spec();
        uint8_t gsr[K_GAMES], gno[K_GAMES];
        const int32_t g = k_cur(), level = sp && g < k_games(sp, gsr, gno) ? k_round(sp, gsr[g])[3] : 1;
        if (level != 0 && level != 2 && level != 5)
            return 0;
    } else if (g_worldPlayoff.etp)
        return 0;
    /* (the play-out: level -> the better placed, the 16th, stays: dplay.c dp_draw) */
    return prio_of(team1) <= prio_of(team2) ? 1 : 2;
}

void WorldPlayoff_TieDecided(uint16_t team1, uint16_t team2, int32_t team2Won)
{
    if (g_worldPlayoff.kind == WORLD_PLAYOFF_GROUPS) {  /* (a group contest: its last decisive match is the
                                                         *  final -- the Mod's gp_rec_c, KT2) */
        extern DIYTournamentState _diyFileBufferCopy;
        if ((uint8_t)_diyFileBufferCopy.header.field_2E != COMPID_worldGroups &&
            (uint8_t)_diyCompetitionStart.base.field_2E != COMPID_worldGroups)
            return;
        const int32_t a = prio_of(team1) != 0xFF ? 1 : 0, b = prio_of(team2) != 0xFF ? 1 : 0;
        if (!a || !b)
            return;
        for (uint8_t i = 0; i < g_worldPlayoff.n; i++) {
            if (g_worldPlayoff.team[i] == (team2Won ? team2 : team1)) g_worldPlayoff.tw[0] = i;
            if (g_worldPlayoff.team[i] == (team2Won ? team1 : team2)) g_worldPlayoff.tl[0] = i;
        }
        SWOS_DBG("[world-playoff] group contest: %04X beat %04X\n", (unsigned)(team2Won ? team2 : team1),
                 (unsigned)(team2Won ? team1 : team2));
        return;
    }
    const DIYCompetition *b = &_diyCompetitionStart.base;
    if (!our_tie(team1, team2) || b->currentRoundIndex + 1 != b->numRounds)
        return;                                 /* (the last round's: the contest's result) */
    if (g_worldPlayoff.kind == WORLD_PLAYOFF_SPEC && g_worldPlayoff.gend) {
        /* a part's last round: its ties' winners, the next part built when all are decided */
        int32_t open = 0;
        for (int32_t t = 0; t < g_worldPlayoff.ties; t++) {
            const uint8_t a = g_worldPlayoff.tieA[t], c = g_worldPlayoff.tieB[t], tt = g_worldPlayoff.tieT[t];
            if ((g_worldPlayoff.team[a] == team1 && g_worldPlayoff.team[c] == team2) ||
                (g_worldPlayoff.team[a] == team2 && g_worldPlayoff.team[c] == team1)) {
                const uint8_t w = g_worldPlayoff.team[c] == (team2Won ? team2 : team1) ? c : a;
                g_worldPlayoff.tw[tt] = w;
                g_worldPlayoff.tl[tt] = w == a ? c : a;
            }
            open += g_worldPlayoff.tw[tt] == NOC;
        }
        if (!open)
            g_worldPlayoff.more = 1;
        SWOS_DBG("[world-playoff] part decided: %04X v %04X, %d tie(s) left\n", (unsigned)team1, (unsigned)team2,
                 (int)open);
        return;
    }
    g_worldPlayoff.winner = team2Won ? team2 : team1;
    SWOS_DBG("[world-playoff] decided: %04X\n", (unsigned)g_worldPlayoff.winner);
    if (g_worldPlayoff.kind == WORLD_PLAYOFF_SPEC && g_worldPlayoff.ties) {   /* (the final's tie) */
        const uint8_t a = g_worldPlayoff.tieA[0], b = g_worldPlayoff.tieB[0];
        const uint8_t w = g_worldPlayoff.team[b] == g_worldPlayoff.winner ? b : a;
        g_worldPlayoff.tw[g_worldPlayoff.tieT[0]] = w;
        g_worldPlayoff.tl[g_worldPlayoff.tieT[0]] = w == a ? b : a;
        k_done();
    }
}

/* The pack's names of the contest's game rounds (_cupRoundStringTable's indices, 0xFF the
 * game's), or 0. */
static const uint8_t *k_round_names(void)
{
    int32_t count = 0;
    const LeaguePlayoffs *all = World_LeaguePlayoffs(&count);
    for (int32_t i = 0; i < count; i++)
        if (all[i].country == g_worldPlayoff.country && all[i].rounds && g_worldPlayoff.pidx < all[i].nnames)
            return all[i].rounds[g_worldPlayoff.pidx];
    return 0;
}

int32_t WorldPlayoff_RoundStringIndex(uint8_t compId, uint16_t round)
{
    if (compId != (uint8_t)COMPID_worldPlayoff || g_worldPlayoff.kind != WORLD_PLAYOFF_SPEC)
        return -1;
    const uint8_t *nm = k_round_names();
    const int32_t g = g_worldPlayoff.g0 + round + g_worldPlayoff.skip;
    return nm && g < 12 && nm[g] != 0xFF ? nm[g] : -1;
}

/* After a skip: the contest's calendar entries still to come named and numbered by the game
 * rounds they now hold, from game round g (engine round r). */
static void relabel_entries(const uint8_t *s, int32_t g, int32_t r)
{
    extern uint8_t _teamSelectionKeyCache[];
    extern uint16_t _teamSelectionKeyCacheCount, _teamSelectionKeyCacheIndex, _teamSelectionKeyCacheOffset;
    const DIYCompetition *b = &_diyCompetitionStart.base;
    const uint8_t slot = (uint8_t)*(uint16_t *)&b->compSlot;
    uint8_t gsr[K_GAMES], gno[K_GAMES];
    const int32_t ng = k_games(s, gsr, gno);
    uint8_t *e = _teamSelectionKeyCache + _teamSelectionKeyCacheOffset;
    int32_t leg = 0;
    for (int32_t i = _teamSelectionKeyCacheIndex; i < _teamSelectionKeyCacheCount && g < ng; i++) {
        TeamSelectionKeyCacheEntry *h = (TeamSelectionKeyCacheEntry *)e;
        if (i > _teamSelectionKeyCacheIndex && h->compSlot == slot && !h->phaseFlag) {
            const int32_t nm = WorldPlayoff_RoundStringIndex((uint8_t)COMPID_worldPlayoff, (uint16_t)r);
            h->cupRoundStringIdx = (uint8_t)(nm >= 0 ? nm : GetNextCupRoundStringIndex((uint16_t)r,
                                             (uint16_t)b->numRounds, (uint16_t)b->subRoundCounter));
            h->roundIdx = (uint8_t)r;
            if (++leg >= (k_round(s, gsr[g])[1] == 2 ? 2 : 1)) {
                leg = 0;
                g++;
                r++;
            }
        }
        e += h->entryStride;
    }
}

/* The contest's calendar entries after the one under way: all but the first `keep` leave the
 * calendar (done: the walk passes them). */
static void retire_entries(int32_t keep)
{
    extern uint8_t _teamSelectionKeyCache[];
    extern uint16_t _teamSelectionKeyCacheCount, _teamSelectionKeyCacheIndex, _teamSelectionKeyCacheOffset;
    const uint8_t slot = (uint8_t)*(uint16_t *)&_diyCompetitionStart.base.compSlot;
    uint8_t *e = _teamSelectionKeyCache + _teamSelectionKeyCacheOffset;
    for (int32_t i = _teamSelectionKeyCacheIndex; i < _teamSelectionKeyCacheCount; i++) {
        TeamSelectionKeyCacheEntry *h = (TeamSelectionKeyCacheEntry *)e;
        if (i > _teamSelectionKeyCacheIndex && h->compSlot == slot && !h->phaseFlag && keep-- <= 0) {
            h->phaseFlag = 1;
            h->remainingElements = 0;
        }
        e += h->entryStride;
    }
}

void WorldPlayoff_ArrangeNextRound(void)
{
    DIYCompetitionEx *c = &_diyCompetitionStart;
    uint8_t *grid = c->teamGridPairings;
    if (!ours())
        return;
    if (g_worldPlayoff.kind == WORLD_PLAYOFF_SPEC) {
        /* the game round just played: each real tie's winner, the one of its clubs among the
         * winners gathered (a series': a win, its tie decided at two); then the next game
         * round with a match from the spec's codes -- a game 3 nobody needs is skipped, the
         * contest a round shorter (the Mod's gp_perm_c, GP_SKIP) */
        const uint8_t *sp = k_spec();
        const int32_t played = c->base.matchPairsPerRound, r = c->base.currentRoundIndex;
        uint8_t gsr[K_GAMES], gno[K_GAMES];
        if (!sp)
            return;
        const int32_t ng = k_games(sp, gsr, gno), g = k_cur();
        const int32_t series = g < ng && k_round(sp, gsr[g])[1] == 3;
        for (int32_t t = 0; t < g_worldPlayoff.ties; t++) {
            const uint8_t a = g_worldPlayoff.tieA[t], b = g_worldPlayoff.tieB[t], tt = g_worldPlayoff.tieT[t];
            uint8_t w = b;
            for (int32_t i = 0; i < played; i++)
                if (grid[i] == a) w = a;
#ifdef SWOS_SDL_BUILD
            {   /* test (SWOS_TEST_SERIES=1): every series game won by the better placed -- each
                 * series 2-0, no game 3 */
                const char *e = getenv("SWOS_TEST_SERIES");
                if (series && e && *e == '1')
                    w = k_better(a, b) ? a : b;
            }
#endif
            if (series) {
                g_worldPlayoff.sw[tt] += w == (k_better(a, b) ? a : b) ? 0x01 : 0x10;
                SWOS_DBG("[world-playoff] game round %d, tie %d: %04X v %04X, %04X won (%02X)\n", (int)g, (int)tt,
                         (unsigned)g_worldPlayoff.team[a], (unsigned)g_worldPlayoff.team[b],
                         (unsigned)g_worldPlayoff.team[w], (unsigned)g_worldPlayoff.sw[tt]);
                if ((g_worldPlayoff.sw[tt] & 0x0F) < 2 && (g_worldPlayoff.sw[tt] >> 4) < 2)
                    continue;
            }
            g_worldPlayoff.tw[tt] = w;
            g_worldPlayoff.tl[tt] = w == a ? b : a;
        }
        if (g + 1 >= k_end(ng))
            return;                             /* (the final, a part's last: WorldPlayoff_TieDecided) */
        int32_t gn = g + 1, nb;
        while ((nb = k_build(sp, gn, 0)) == 0 && gn + 1 < k_end(ng))
            gn++;
        if (nb > 0) {
            if (gn > g + 1) {                   /* (game rounds with no match: the contest shorter) */
                int32_t keep = 0;
                for (int32_t q = gn; q < ng; q++)
                    keep += k_round(sp, gsr[q])[1] == 2 ? 2 : 1;
                c->base.numRounds = (int16_t)(c->base.numRounds - (gn - g - 1));
                g_worldPlayoff.skip = (uint8_t)(g_worldPlayoff.skip + gn - g - 1);
                retire_entries(keep);
                relabel_entries(sp, gn, r + 1);
                SWOS_DBG("[world-playoff] game round %d next (%d skipped)\n", (int)gn, (int)(gn - g - 1));
            }
            const uint8_t *rd = k_round(sp, gsr[gn]);
            const uint8_t f = k_flags(rd[1] == 3 ? 1 : rd[1], rd[3]);
            round_rules(r + 1, (f >> 6) & 3, (f >> 4) & 3, (f >> 2) & 3);
            k_lay_out(sp, r + 1, gsr[gn]);
        } else if (nb == 0) {
            /* no game round left with a match (one club twice: Uruguay's final when the
             * semi-final's winner is the 3rd seed too, skip_if_same) -- the contest decided:
             * finished as the engine finishes a cup after its final, its winner the club */
            const int32_t last = k_t0(sp, sp[2] - 1) + k_round(sp, sp[2] - 1)[0] - 1;
            const uint8_t w = last >= 0 && last < WORLD_PLAYOFF_MAX ? g_worldPlayoff.tw[last] : NOC;
            if (w != NOC) {
                grid[0] = grid[1] = w;
                c->matchData.cup.cupRoundResults[0] = 0;
                c->base.competitionFinished = 1;
                c->base.resultCode = (int16_t)BE_READ_U16(&c->cupTeamIds[w]);
                g_worldPlayoff.winner = g_worldPlayoff.team[w];
                SWOS_DBG("[world-playoff] decided without a final: %04X\n", (unsigned)g_worldPlayoff.winner);
                k_done();
                retire_entries(0);
            }
        }
        return;
    }
    if (g_worldPlayoff.kind == WORLD_PLAYOFF_C_PLAYOFF) {
        /* each real tie's winner: the one of its two clubs among the winners gathered */
        uint8_t w[16];
        const int32_t played = c->base.matchPairsPerRound;
        for (int32_t t = 0; t < g_worldPlayoff.ties; t++) {
            w[t] = g_worldPlayoff.tieB[t];
            for (int32_t i = 0; i < played; i++)
                if (grid[i] == g_worldPlayoff.tieA[t]) w[t] = g_worldPlayoff.tieA[t];
        }
        if (c->base.currentRoundIndex + 1 < c->base.numRounds)
            c_next_round(c->base.currentRoundIndex + 1, w, played >> 1);
        return;
    }
    if (g_worldPlayoff.kind == WORLD_PLAYOFF_PLACEMENT) {
        /* the final: the better placed hosts the single match (the return of two) */
        const uint8_t a = grid[0], b = grid[1];
        const int32_t aBetter = g_worldPlayoff.prio[a] < g_worldPlayoff.prio[b];
        const int32_t twoLegs = (g_worldPlayoff.rules2 >> 6) == 2;
        const uint8_t better = aBetter ? a : b, worse = aBetter ? b : a;
        grid[0] = twoLegs ? worse : better;
        grid[1] = twoLegs ? better : worse;
        return;
    }
    if (g_worldPlayoff.kind != WORLD_PLAYOFF_B_PLAYOFF)
        return;
    if (c->base.currentRoundIndex == 0) {
        /* the winners (entrant indices): 0/1 W(5v8), 2/3 W(6v7), 4 the 3rd, 6 the 4th --
         * semis 3rd v W(6v7), 4th v W(5v8), the first leg at the winner's */
        uint8_t w58 = 0xFF, w67 = 0xFF;
        for (int32_t i = 0; i < 4; i++) {
            if (grid[i] <= 1) w58 = grid[i];
            else if (grid[i] <= 3) w67 = grid[i];
        }
        if (w58 == 0xFF || w67 == 0xFF)
            return;
        grid[0] = w67; grid[1] = 4;
        grid[2] = w58; grid[3] = 6;
    } else if (c->base.currentRoundIndex == 1) {
        /* the final: the first leg at the worse placed's; level on points: e.t. + penalties */
        const uint8_t a = grid[0], b = grid[1];
        const int32_t aBetter = g_worldPlayoff.prio[a] < g_worldPlayoff.prio[b];
        grid[0] = aBetter ? b : a;
        grid[1] = aBetter ? a : b;
        g_worldPlayoff.etp = g_worldPlayoff.pts[a] == g_worldPlayoff.pts[b];
        round_rules(2, 2, g_worldPlayoff.etp, g_worldPlayoff.etp);
    }
}

/* A round's name where the engine's (by the rounds left) would mislead: Serie B's first
 * round is its preliminary (the Mod's rnames), a single round has none (not "FINAL"). */
static char s_prelim[] = "PRELIMINARY ROUND", s_noRound[] = "";

const char *WorldPlayoff_RoundName(uint16_t round, uint16_t rounds)
{
    if (!ours() || rounds != (uint16_t)_diyCompetitionStart.base.numRounds)
        return 0;
    if (rounds == 1)
        return s_noRound;
    if (g_worldPlayoff.kind == WORLD_PLAYOFF_B_PLAYOFF && round == 0)
        return s_prelim;
    return 0;
}

void WorldPlayoff_RoundSize(void)
{
    DIYCompetition *b = &_diyCompetitionStart.base;
    if (!ours())
        return;
    if (g_worldPlayoff.kind == WORLD_PLAYOFF_SPEC)  /* (the round's places: the halving bracket of the
                                                     *  rounds left, wider where it needs: k_lay_out) */
        b->matchPairsPerRound = (int16_t)s_pairs;
    if (g_worldPlayoff.kind == WORLD_PLAYOFF_C_PLAYOFF || g_worldPlayoff.kind == WORLD_PLAYOFF_SPEC)
        b->totalMatchesInRound = g_worldPlayoff.ties;
}

void WorldPlayoff_RestoreEarlyTable(uint16_t division)
{
    DIYCompetitionEx *ex = &_diyCompetitionStart;
    uint8_t *base = (uint8_t *)&ex->base;
    for (int32_t e = 0; e < g_worldPlayoff.early && e < WORLD_PLAYOFF_EARLY; e++) {
        if (g_worldPlayoff.earlyDiv[e] != division)
            continue;
        const int32_t n = (uint16_t)ex->base.numTeams;
        /* place by place: the row of the club the early table has there gets its figures
         * (the rows by club: the competition's own pointers stay) */
        for (int32_t k = 0; k < g_worldPlayoff.earlyN[e] && k < n; k++) {
            const WorldPlayoffRow *r = &g_worldPlayoff.earlyRow[e][k];
            for (int32_t i = 0; i < n; i++) {
                DIYTeamStats *st = (DIYTeamStats *)(base + i * 18 + DIY_STANDINGS_OFFSET);
                const TeamFileEx *t = (const TeamFileEx *)diy_resolve_team(st->teamSelOffset);
                if (PACK_BE_U16(t->team.countryNumber, t->team.teamOrdinal) != r->team)
                    continue;
                st->gamesPlayed = r->played; st->wins = r->won; st->draws = r->drawn;
                st->losses = r->lost; st->goalsFor = r->gf; st->goalsAgainst = r->ga;
                st->points = r->pts;
                ex->matchData.league.teamEntryOffsets[k] = (uint16_t)(i * 18);
                break;
            }
        }
        SWOS_DBG("[world-playoff] division %u: the early season's table restored\n", (unsigned)division);
    }
}

void WorldPlayoff_RestoreSlot(void)
{
    if (!g_worldPlayoff.kind || g_worldPlayoff.kind == WORLD_PLAYOFF_GROUPS)
        return;                                 /* (a group contest is slot 3's: WorldPlayoff_GroupsRecord) */
    if (g_worldPlayoff.kind == WORLD_PLAYOFF_SPEC)
        k_names(g_worldPlayoff.country, g_worldPlayoff.pidx);
    make_record();
    _diyCompetitionPtr = (uint8_t *)&s_rec;
    _diyCompetitionTablePtr = (uint32_t *)0;
}
