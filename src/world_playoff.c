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

/* The bracket's places: its clubs (Serie C's 28 in a bracket of 64: the byes and the
 * fillers after each round's real ties). */
static int32_t bracket_size(void)
{
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

static void first_round(void)
{
    DIYCompetitionEx *c = &_diyCompetitionStart;
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

int32_t WorldPlayoff_CareerLeagueEnded(void)
{
    DIYCompetitionEx *ex = &_diyCompetitionStart;
    int32_t count = 0;
    const LeaguePlayoffs *all = World_LeaguePlayoffs(&count), *lp = 0;
    if (g_worldPlayoff.kind)
        return 1;                               /* (built already: slot 4 is ours) */
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
            !group_placement(lp, (int32_t)_currentDivisionIndex, team, pts, n, mine))
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

    /* Into slot 4, as 96/97's play-offs: the record (its calendar), then its clubs. */
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
    SWOS_DBG("[world-playoff] %s: %u clubs, the career club %d%s\n",
             g_worldPlayoff.kind == WORLD_PLAYOFF_C_PLAYOFF ? "PLAY-OFF SERIE C" :
             g_worldPlayoff.kind == WORLD_PLAYOFF_B_PLAYOFF ? "PLAY-OFF SERIE B" :
             g_worldPlayoff.kind == WORLD_PLAYOFF_GROUP_TIE ? s_tieName :
             g_worldPlayoff.kind == WORLD_PLAYOFF_PLACEMENT ? s_pName : "PLAY-OUT SERIE B",
             (unsigned)g_worldPlayoff.n, (int)mine + 1, g_worldPlayoff.etp ? ", e.t. + penalties" : "");
    return 1;
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
    if (!our_tie(team1, team2) || g_worldPlayoff.etp)
        return 0;
    /* (the play-out: level -> the better placed, the 16th, stays: dplay.c dp_draw) */
    return prio_of(team1) <= prio_of(team2) ? 1 : 2;
}

void WorldPlayoff_TieDecided(uint16_t team1, uint16_t team2, int32_t team2Won)
{
    const DIYCompetition *b = &_diyCompetitionStart.base;
    if (!our_tie(team1, team2) || b->currentRoundIndex + 1 != b->numRounds)
        return;                                 /* (the last round's: the contest's result) */
    g_worldPlayoff.winner = team2Won ? team2 : team1;
    SWOS_DBG("[world-playoff] decided: %04X\n", (unsigned)g_worldPlayoff.winner);
}

void WorldPlayoff_ArrangeNextRound(void)
{
    DIYCompetitionEx *c = &_diyCompetitionStart;
    uint8_t *grid = c->teamGridPairings;
    if (!ours())
        return;
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
    if (ours() && g_worldPlayoff.kind == WORLD_PLAYOFF_C_PLAYOFF)
        _diyCompetitionStart.base.totalMatchesInRound = g_worldPlayoff.ties;
}

void WorldPlayoff_RestoreEarlyTable(uint16_t division)
{
    DIYCompetitionEx *ex = &_diyCompetitionStart;
    uint8_t *base = (uint8_t *)&ex->base;
    for (int32_t e = 0; e < g_worldPlayoff.early && e < 2; e++) {
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
    if (!g_worldPlayoff.kind)
        return;
    make_record();
    _diyCompetitionPtr = (uint8_t *)&s_rec;
    _diyCompetitionTablePtr = (uint32_t *)0;
}
