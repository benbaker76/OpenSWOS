/*
 * league_split.c -- a world league division's mid-season split (include/league_split.h).
 *
 * The SWOS 96/97 Mod's design (tools/split.py: sp_init, sp_stage, sp_rows, sp_pair, sp_count,
 * sp_freeze, sp_calendar, sp_post, sp_early, sp_res, sp_sort, sp_cpu; MIT, Davide
 * Lorigliola), written for our engine. Its table, compiled by scripts/world_split.py, is the
 * Mod's SP_TAB entry, byte for byte (words little-endian):
 *
 *   +0 world, +1 file (here: the country), +2 division, +3 clubs, +4 cycles, +5 days before,
 *   +6 days after, +7 stages, +8 word the days' match counts, +10 word each day's first pair,
 *   +12 word the pairs (home, away, flags: 1 the host from the matrix), +14 word the length;
 *   +16 the stages, 28 bytes each: +0 its first day after the split, +1 groups, +2 flags (1
 *   reset, 2 keep, 4 the pairs are rows: a fixture list, 8 groups by membership, 16 the wins
 *   tie-break, 20h explicit places), +4 (first place, size, points rule) x 4, +16 word the
 *   slots' host matrix, +18 word the places' permutation, +20 word the rows' groups, +22 the
 *   after-stage routine, +23 the stage table, +24 annual, +26 word the days' weeks (stage 0).
 *
 * The after-stage routines and the stage tables (the Apertura and Clausura titles') are not
 * played yet; 'keep' (Malta's kept opening table, for its title play-off's seeds) neither.
 *
 * The engine's names for the Mod's: leagueMatchProgress the day ([1C1h]), leagueCurrentSlot
 * "the day is done" ([1BFh]), halfGroupsCounter the day's matches played ([1C9h]),
 * matchesPerHalf the day's matches ([1C7h]), minSubstitutes the cycles left ([5Fh]),
 * shuffleAndPad the slot list (+0EDh), teamEntryOffsets the table's order (+6Dh).
 */
#include "league_split.h"
#include "game_types.h"
#include "diy_competition.h"
#include "endian_helpers.h"
#include "league_playoffs.h"

extern DIYCompetitionEx _diyCompetitionStart;
extern uint8_t  _teamSelectionKeyCache[];
extern uint16_t _teamSelectionKeyCacheCount;
extern uint8_t  _viewRandomPhase;
extern void DIY_RecomputeCompetitionDerivedProgressionTable(void);
extern void DIY_SortLeagueTableByStandings(void);

#define SR 28                   /* a stage's record */
#define ROW 18                  /* a table row's stride (the rows' offsets are row * 18) */
#define F_RESET   1
#define F_KEEP    2
#define F_ROWS    4
#define F_MEMBERS 8
#define F_WINS    16
#define F_PERM    0x20

uint8_t g_leagueSplitH2H[LEAGUE_SPLIT_H2H];
static uint8_t s_simH2H[LEAGUE_SPLIT_H2H];      /* (a simulated league's: the season end's) */
static uint8_t s_walking;                       /* the career calendar's walk */
static uint8_t s_pend;                          /* the day (+ 1) whose stage starts once this
                                                   match's result is in */
static uint8_t s_end;                           /* the day (+ 1) a stage ended on, its table kept
                                                   once this match's result is in */

static uint16_t w16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static DIYTeamStats *row_stats(int32_t row)
{
    return (DIYTeamStats *)((uint8_t *)&_diyCompetitionStart + (uint32_t)row * ROW + DIY_STANDINGS_OFFSET);
}

/* The current league's split table, or 0 (no split running). */
static const uint8_t *active(void)
{
    DIYCompetitionEx *c = &_diyCompetitionStart;
    const LeagueSplitState *st = &c->leagueSectionState.split;
    int32_t n = 0;
    const LeagueSplit *sp = World_LeagueSplits(&n);
    if (c->base.competitionType != 0 || st->magic != LEAGUE_SPLIT_MAGIC || !st->entry || st->entry > n ||
        st->compId != c->base.field_2E || st->n != c->base.numTeams)
        return 0;
    return sp[st->entry - 1].table;
}

/* Day `day` after the split: its stage (the last whose first day it has reached). */
static int32_t stage_of(const uint8_t *t, int32_t day)
{
    for (int32_t k = t[7] - 1; k > 0; k--)
        if (day >= t[16 + SR * k])
            return k;
    return 0;
}

/* The career club's league (its row in it): its calendar, its saved head-to-head points. */
static int32_t is_career_league(void)
{
    for (int32_t r = 0; r < _diyCompetitionStart.base.numTeams; r++)
        if (row_stats(r)->teamSelOffset == -1)
            return 1;
    return 0;
}

void LeagueSplit_Walking(int32_t on)
{
    s_walking = (uint8_t)(on != 0);
}

/* ---- the season start ---- */

void LeagueSplit_Init(uint8_t country, uint16_t division, int32_t after)
{
    DIYCompetitionEx *c = &_diyCompetitionStart;
    LeagueSplitState *st = &c->leagueSectionState.split;
    int32_t n = 0, k = -1;
    const LeagueSplit *sp = World_LeagueSplits(&n);
    for (int32_t i = 0; i < n; i++)
        if (sp[i].country == country && sp[i].division == division && sp[i].table[3] == c->base.numTeams)
            k = i;
    if (!after) {
        if (st->magic == LEAGUE_SPLIT_MAGIC)    /* (a previous split league's mark: only it is */
            st->magic = 0;                      /*  cleared, its weeks may cover the rest) */
        if (k >= 0)
            c->base.playEachTeam = sp[k].table[4];
        return;
    }
    if (k < 0 || !sp[k].table[7])
        return;
    const uint8_t *t = sp[k].table;
    const int32_t days = t[5] + t[6];
    c->leagueTotalMatches = (uint16_t)days;
    DIY_RecomputeCompetitionDerivedProgressionTable();
    if (w16(t + 16 + 26))                       /* the days' own weeks (their real dates) */
        for (int32_t d = 0; d < days; d++)
            c->leagueSectionState.phaseStartMonths[d] = t[w16(t + 16 + 26) + d];
    c->base.viewRandomPhase = c->leagueSectionState.phaseStartMonths[0];
    uint8_t *p = (uint8_t *)st;
    for (uint32_t i = 0; i < sizeof *st; i++)
        p[i] = 0;
    st->magic = LEAGUE_SPLIT_MAGIC;
    st->entry = (uint8_t)(k + 1);
    st->n = t[3];
    st->compId = c->base.field_2E;
    SWOS_DBG("[split] country %d division %d: %d days before the split, %d after, %d stage(s)\n",
             (int)country, (int)division, (int)t[5], (int)t[6], (int)t[7]);
}

/* ---- the days after the split ---- */

/* The slot of a row (its place in the slot list), as the pairs before the split knew it. */
static int32_t slot_of(int32_t row, int32_t n)
{
    for (int32_t s = 0; s < n; s++)
        if ((int8_t)_diyCompetitionStart.matchData.league.shuffleAndPad[s] == row)
            return s;
    return 0;
}

/* Day `day` after the split, its match m: the home row and the away row. The places are the
 * table's frozen for the day's stage (a fixture list's pairs are rows already; in the
 * calendar's walk, before the stage is played: places = rows). */
static void rows(const uint8_t *t, int32_t day, int32_t m, int32_t *home, int32_t *away)
{
    const LeagueSplitState *st = &_diyCompetitionStart.leagueSectionState.split;
    const int32_t k = stage_of(t, day);
    const uint8_t *rec = t + 16 + SR * k;
    const uint8_t *pr = t + w16(t + 12) + 3 * (w16(t + w16(t + 10) + 2 * day) + m);
    int32_t a = pr[0], b = pr[1];
    if (st->frozen == k + 1 && !(rec[2] & F_ROWS)) {
        a = st->placeRow[a];
        b = st->placeRow[b];
    }
    if ((pr[2] & 1) && w16(rec + 16)) {         /* the host from the fixtures before the split */
        const int32_t n = t[3];
        if (t[w16(rec + 16) + slot_of(a, n) * n + slot_of(b, n)] == 0) {
            const int32_t x = a;
            a = b;
            b = x;
        }
    }
    *home = a;
    *away = b;
}

/* The career calendar's entries of the days from `from` after the split: each one's career
 * club match, or none (a day it does not play). */
static void calendar(const uint8_t *t, int32_t from)
{
    DIYCompetitionEx *c = &_diyCompetitionStart;
    /* (the slot key as the calendar's walk reads it: the word at compSlot, its low byte) */
    const uint8_t key = (uint8_t)*(uint16_t *)&c->base.compSlot;
    uint8_t *e = _teamSelectionKeyCache;
    int32_t block = 0;
    for (int32_t left = (int16_t)_teamSelectionKeyCacheCount; left > 0; left--) {
        TeamSelectionKeyCacheEntry *h = (TeamSelectionKeyCacheEntry *)e;
        if (h->compSlot == key) {
            const int32_t day = block++ - t[5];
            if (day >= from && day < t[6]) {
                h->totalElements = 0;
                h->remainingElements = 0;
                for (int32_t i = 0x0A; i < 0x16; i++)
                    e[i] = 0;
                for (int32_t m = 0; m < t[w16(t + 8) + day]; m++) {
                    int32_t a, b;
                    rows(t, day, m, &a, &b);
                    uint8_t *s1 = diy_resolve_team(row_stats(a)->teamSelOffset);
                    uint8_t *s2 = diy_resolve_team(row_stats(b)->teamSelOffset);
                    if (((TeamFile *)s1)->teamControls == 1 && ((TeamFile *)s2)->teamControls == 1)
                        continue;
                    e[0x0A] = ((TeamFile *)s1)->teamControls == 1 ? 2 : 1;     /* (its away, home) */
                    e[0x0B] = 0;
                    BE_WRITE_U16(e + 0x0C, BE_READ_U16(s1));
                    BE_WRITE_U16(e + 0x0E, BE_READ_U16(s2));
                    h->totalElements = 1;
                    h->remainingElements = 1;
                    break;
                }
            }
        }
        e += h->entryStride;
    }
}

/* The table, place by place (the log). */
static void dump(const char *what, const uint8_t *t)
{
    DIYCompetitionEx *c = &_diyCompetitionStart;
    const LeagueSplitState *st = &c->leagueSectionState.split;
    SWOS_DBG("[split] country %d division %d %s, day %d:", (int)t[1], (int)t[2], what, (int)c->leagueMatchProgress);
    for (int32_t p = 0; p < t[3]; p++) {
        const int32_t row = c->matchData.league.teamEntryOffsets[p] / ROW;
        const DIYTeamStats *s = row_stats(row);
        SWOS_DBG(" %d:%s/%d/%d/g%d", p + 1, (const char *)diy_resolve_team_name(s->teamSelOffset),
                 (int)s->gamesPlayed, (int)s->points, st->frozen ? (int)st->group[row] : -1);
    }
    SWOS_DBG("\n");
}

/* Stage k starts: the groups from the table now (by place, or by membership), each one's
 * points rule (the points before it kept: the first tie-break), every figure from 0 for a
 * stage that resets, the table in its groups, the career club's calendar. */
static void freeze(const uint8_t *t, int32_t k)
{
    DIYCompetitionEx *c = &_diyCompetitionStart;
    LeagueSplitState *st = &c->leagueSectionState.split;
    const uint8_t *rec = t + 16 + SR * k;
    const int32_t n = t[3], career = is_career_league();
    const uint8_t *h2h = career ? g_leagueSplitH2H : s_simH2H;
    DIY_SortLeagueTableByStandings();           /* (the last results in the table's order) */
    if (rec[2] & F_KEEP)                        /* (the table before it: Malta's Opening Round) */
        LeaguePlayoffs_KeepStage(t[1], t[2]);
    if (rec[2] & F_RESET)
        for (int32_t r = 0; r < n; r++) {
            DIYTeamStats *s = row_stats(r);
            s->gamesPlayed = s->wins = s->draws = s->losses = 0;
            s->goalsFor = s->goalsAgainst = s->points = 0;
        }
    for (int32_t p = 0; p < n; p++)
        st->placeRow[p] = (uint8_t)(c->matchData.league.teamEntryOffsets[p] / ROW);
    if (rec[2] & F_PERM) {                      /* new place p = the old place perm[p] */
        uint8_t old[LEAGUE_SPLIT_MAX_CLUBS];
        for (int32_t p = 0; p < n; p++)
            old[p] = st->placeRow[p];
        for (int32_t p = 0; p < n; p++)
            st->placeRow[p] = old[t[w16(rec + 18) + p]];
    }
    for (int32_t r = 0; r < LEAGUE_SPLIT_MAX_CLUBS; r++)
        st->group[r] = 0xFF;
    if (rec[2] & F_MEMBERS) {                   /* groups by membership (Panama's zones) */
        for (int32_t r = 0; r < n; r++) {
            const uint32_t pts = row_stats(r)->points;
            st->group[r] = t[w16(rec + 20) + r];
            st->pre[r] = (uint8_t)(pts < 255 ? pts : 255);
        }
    } else {
        for (int32_t g = 0; g < rec[1]; g++) {
            const int32_t first = rec[4 + 3 * g], end = first + rec[5 + 3 * g], rule = rec[6 + 3 * g];
            for (int32_t p = first; p < end; p++) {
                const int32_t row = st->placeRow[p];
                DIYTeamStats *s = row_stats(row);
                uint32_t pts = s->points;
                st->group[row] = (uint8_t)g;
                st->pre[row] = (uint8_t)(pts < 255 ? pts : 255);
                if (rule == 1)                  /* half, rounded up */
                    pts = (pts + 1) >> 1;
                else if (rule == 2)             /* half, rounded down */
                    pts >>= 1;
                else if (rule == 3)             /* from 0 */
                    pts = st->pre[row] = 0;
                else if (rule == 4) {           /* half those won against the group's clubs */
                    pts = 0;
                    for (int32_t q = first; q < end; q++)
                        pts += h2h[row * n + st->placeRow[q]];
                    st->pre[row] = (uint8_t)(pts < 255 ? pts : 255);
                    pts >>= 1;
                }
                s->points = (uint16_t)pts;
            }
        }
    }
    st->frozen = (uint8_t)(k + 1);
    DIY_SortLeagueTableByStandings();
    dump("stage", t);
    if (career)
        calendar(t, rec[0]);
}

/* The day's stage frozen (not in the calendar's walk). */
static void stage_due(const uint8_t *t, int32_t day)
{
    const int32_t k = stage_of(t, day);
    if (_diyCompetitionStart.leagueSectionState.split.frozen != k + 1 && !s_walking)
        freeze(t, k);
}

int32_t LeagueSplit_Pair(uint8_t **team1, uint8_t **team2)
{
    const uint8_t *t = active();
    if (!t)
        return 0;
    DIYCompetitionEx *c = &_diyCompetitionStart;
    const int32_t day = (int32_t)c->leagueMatchProgress - t[5];
    if (day < 0) {
        c->leagueSectionState.split.frozen = 0;     /* (before the split: a new season) */
        return 0;
    }
    if (day >= t[6])
        return 0;
    stage_due(t, day);
    const int32_t m = c->leagueCurrentSlot ? 0 : c->leagueSectionState.halfGroupsCounter;
    int32_t a, b;
    rows(t, day, m, &a, &b);
    *team1 = (uint8_t *)&_diyCompetitionStart + (uint32_t)a * ROW;
    *team2 = (uint8_t *)&_diyCompetitionStart + (uint32_t)b * ROW;
    return 1;
}

int32_t LeagueSplit_Count(void)
{
    const uint8_t *t = active();
    if (!t)
        return 0;
    DIYCompetitionEx *c = &_diyCompetitionStart;
    DIYCompetitionEx_LeagueSectionState *ls = &c->leagueSectionState;
    const int32_t day = (int32_t)c->leagueMatchProgress - t[5];
    if (day < 0) {
        /* The last day before the split, its last match: the split once its result is in. */
        if (day == -1 && !s_walking &&
            (c->leagueCurrentSlot ? 0 : ls->halfGroupsCounter) + 1 == ls->matchesPerHalf)
            s_pend = 1;
        return 0;
    }
    if (day >= t[6])
        return 0;
    if (c->leagueCurrentSlot) {
        c->leagueCurrentSlot = 0;
        ls->halfGroupsCounter = 0;
    }
    ls->halfGroupsCounter++;
    const uint8_t *counts = t + w16(t + 8);
    const uint16_t today = counts[day];
    const uint16_t next = day + 1 < t[6] ? counts[day + 1] : (uint16_t)(t[3] / 2);
    ls->matchesPerHalf = today;
    if (ls->halfGroupsCounter < today)
        return 1;
    c->leagueCurrentSlot = 1;                   /* the day is over */
    ls->matchesPerHalf = next;
    c->leagueMatchProgress++;
    c->base.viewRandomPhase = ls->phaseStartMonths[c->leagueMatchProgress];
    _viewRandomPhase = c->base.viewRandomPhase;
    if (!s_walking)                             /* (a stage starting tomorrow: once this */
        s_pend = (uint8_t)(day + 2);            /*  match's result is in) */
    if (!s_walking) {                           /* (the day a stage ends on: its table) */
        const int32_t k = stage_of(t, day);
        const int32_t next = k + 1 < t[7] ? t[16 + SR * (k + 1)] : t[6];
        if (day + 1 == next)
            s_end = (uint8_t)(day + 1);
    }
    if (day + 1 >= t[6]) {
        c->minSubstitutes = 0;                  /* the last day: the league is over */
        if (!s_walking)
            dump("played", t);                  /* (the last result not in it yet) */
    }
    return 1;
}

void LeagueSplit_AfterResult(uint8_t *team1, uint8_t *team2, uint16_t goals1, uint16_t goals2)
{
    const uint8_t *t = active();
    if (!t)
        return;
    DIYCompetitionEx *c = &_diyCompetitionStart;
    const uint8_t *rec = t + 16;                /* (the first stage's groups) */
    const int32_t n = t[3];
    int32_t h2h = 0;
    for (int32_t g = 0; g < rec[1]; g++)
        h2h |= rec[6 + 3 * g] == 4;
    if (h2h && c->leagueSectionState.split.frozen == 0 && !s_walking) {
        /* Before the split, every pair's points (a group of head-to-head points). */
        uint8_t *b = is_career_league() ? g_leagueSplitH2H : s_simH2H;
        const int32_t r1 = (int32_t)(team1 - (uint8_t *)&_diyCompetitionStart) / ROW;
        const int32_t r2 = (int32_t)(team2 - (uint8_t *)&_diyCompetitionStart) / ROW;
        if (c->matchesPlayedCount == 1)         /* (the season's first result) */
            for (int32_t i = 0; i < LEAGUE_SPLIT_H2H; i++)
                b[i] = 0;
        if (r1 < n && r2 < n) {
            if (goals1 == goals2) {
                b[r1 * n + r2] += 1;
                b[r2 * n + r1] += 1;
            } else if (goals1 > goals2) {
                b[r1 * n + r2] += (uint8_t)c->pointsForAWin;
            } else {
                b[r2 * n + r1] += (uint8_t)c->pointsForAWin;
            }
        }
    }
    if (s_end) {                                /* a stage over on day s_end - 1: its table kept */
        const uint8_t *rec = t + 16 + SR * stage_of(t, s_end - 1);
        s_end = 0;
        if (rec[23]) {                          /* (the Mod's sp_stage_end: gplay.c gp_stage) */
            DIY_SortLeagueTableByStandings();
            LeaguePlayoffs_StageTable(t[1], (uint32_t)(rec[23] - 1), rec[24]);
            SWOS_DBG("[split] country %d division %d: stage table %d kept\n", (int)t[1], (int)t[2], (int)rec[23] - 1);
        }
    }
    if (s_pend) {                               /* a stage starting on day s_pend - 1 */
        const int32_t day = s_pend - 1;
        s_pend = 0;
        if (day < t[6] && t[16 + SR * stage_of(t, day)] == day)
            stage_due(t, day);
    }
}

void LeagueSplit_Sort(void)
{
    const uint8_t *t = active();
    DIYCompetitionEx *c = &_diyCompetitionStart;
    const LeagueSplitState *st = &c->leagueSectionState.split;
    if (!t || !st->frozen)
        return;
    const uint8_t *rec = t + 16 + SR * (st->frozen - 1);
    const int32_t n = t[3], groups = rec[1], wins = rec[2] & F_WINS;
    uint16_t *order = c->matchData.league.teamEntryOffsets;
    uint16_t tmp[LEAGUE_SPLIT_MAX_CLUBS];
    int32_t k = 0;
    for (int32_t g = 0; g < groups; g++)        /* the groups in order, each in the table's */
        for (int32_t p = 0; p < n; p++)
            if (st->group[order[p] / ROW] == g)
                tmp[k++] = order[p];
    for (int32_t p = 0; p < k; p++)
        order[p] = tmp[p];
    /* Equal points in one group: more points before rounding first -- or more wins (MLS) --
     * (stable passes: the engine's order kept otherwise). */
    for (int32_t pass = 0; pass < n; pass++)
        for (int32_t p = 0; p + 1 < n; p++) {
            const int32_t a = order[p] / ROW, b = order[p + 1] / ROW;
            if (row_stats(a)->points != row_stats(b)->points || st->group[a] != st->group[b])
                continue;
            if (wins ? row_stats(a)->wins < row_stats(b)->wins : st->pre[a] < st->pre[b]) {
                const uint16_t x = order[p];
                order[p] = order[p + 1];
                order[p + 1] = x;
            }
        }
}

int32_t LeagueSplit_RestOfDayWithoutCareerClub(void)
{
    const uint8_t *t = active();
    if (!t)
        return 0;
    DIYCompetitionEx *c = &_diyCompetitionStart;
    const int32_t day = (int32_t)c->leagueMatchProgress - t[5];
    if (day < 0 || day >= t[6])
        return 0;
    stage_due(t, day);
    const int32_t today = t[w16(t + 8) + day];
    for (int32_t m = c->leagueCurrentSlot ? 0 : c->leagueSectionState.halfGroupsCounter; m < today; m++) {
        int32_t a, b;
        rows(t, day, m, &a, &b);
        if (((TeamFile *)diy_resolve_team(row_stats(a)->teamSelOffset))->teamControls != 1 ||
            ((TeamFile *)diy_resolve_team(row_stats(b)->teamSelOffset))->teamControls != 1)
            return 0;                           /* (the career club plays today) */
    }
    do
        DIY_PlayOrSimulateLeagueFixture(0, 0);
    while (!c->leagueCurrentSlot);
    return 1;
}
