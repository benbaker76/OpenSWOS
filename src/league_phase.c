/*
 * league_phase.c -- the 36-club league phase of a world's European cups (include/league_phase.h).
 *
 * The draw is the SWOS 96/97 Mod's model (tools/lp36.py: draw, _bijection, _schedule, _matching
 * for 4 pots; draw6 for the Conference League's 6; MIT, Davide Lorigliola): for each ordered
 * pair of pots, in a random order, a one-to-one "p hosts q" found by backtracking, never a
 * rematch, a club of its own country, a third opponent of one country; then the matchdays,
 * perfect matchings of the matches, the club with the fewest choices first. The Conference
 * League: a perfect matching for every pair of pots (its own included), then each club's 6
 * matches turned 3 home / 3 away by walks along them (Euler), then 6 matchdays. Its random
 * numbers are its own (seeded by the caller), so a season replays the same.
 */
#include "league_phase.h"
#include "uefa_coef.h"

#define N   LEAGUE_PHASE_CLUBS
#define MAXM LEAGUE_PHASE_MATCHES

static uint32_t s_rng;

static uint32_t below(uint32_t n)
{
    s_rng = s_rng * 1103515245u + 12345u;
    return n ? ((s_rng >> 16) * n) >> 16 : 0;
}

static void shuffle(uint8_t *a, int32_t n)
{
    for (int32_t i = n - 1; i > 0; i--) {
        const int32_t j = (int32_t)below((uint32_t)(i + 1));
        const uint8_t x = a[i];
        a[i] = a[j];
        a[j] = x;
    }
}

/* ---- the opponents so far ---- */

static const uint8_t *s_country;
static uint8_t s_opp[N][8], s_nopp[N];

static int32_t same_country(int32_t c, uint8_t k)
{
    int32_t n = 0;
    for (int32_t i = 0; i < s_nopp[c]; i++)
        n += s_country[s_opp[c][i]] == k;
    return n;
}

static int32_t met(int32_t c, int32_t o)
{
    for (int32_t i = 0; i < s_nopp[c]; i++)
        if (s_opp[c][i] == o)
            return 1;
    return 0;
}

/* c may meet o: not its own country, not again, at most 2 opponents of one country (both sides);
 * relaxed (no draw holds every rule): 1 the cap of 2 dropped, 2 the own country too */
static int32_t s_relax;

static int32_t fits(int32_t c, int32_t o)
{
    if (met(c, o))
        return 0;
    if (s_relax < 2 && s_country[c] == s_country[o])
        return 0;
    return s_relax >= 1 || (same_country(c, s_country[o]) < 2 && same_country(o, s_country[c]) < 2);
}

static void link(int32_t c, int32_t o)
{
    s_opp[c][s_nopp[c]++] = (uint8_t)o;
    s_opp[o][s_nopp[o]++] = (uint8_t)c;
}

static void unlink(int32_t c, int32_t o)
{
    s_nopp[c]--;
    s_nopp[o]--;
}

/* ---- 4 pots: the one-to-one p hosts q of a pair of pots ---- */

static uint8_t s_P[9], s_f[9], s_used[N], s_same, s_Q;

static int32_t bij(int32_t i, int32_t pot)
{
    if (i == pot)
        return 1;
    const int32_t p = s_P[i];
    uint8_t cand[9];
    int32_t nc = 0;
    for (int32_t q = s_Q * pot; q < s_Q * pot + pot; q++) {
        if (s_used[q] || !fits(p, q))
            continue;
        if (s_same) {                           /* same pot: not itself, not each other's */
            int32_t back = 0;
            for (int32_t j = 0; j < i; j++)
                back |= s_P[j] == q && s_f[j] == p;
            if (q == p || back)
                continue;
        }
        cand[nc++] = (uint8_t)q;
    }
    shuffle(cand, nc);
    for (int32_t k = 0; k < nc; k++) {
        const int32_t q = cand[k];
        if (!fits(p, q))                        /* (a pair linked deeper may rule it out now) */
            continue;
        s_f[i] = (uint8_t)q;
        s_used[q] = 1;
        link(p, q);                             /* (at once: in one pot a club both hosts and */
        if (bij(i + 1, pot))                    /*  visits, its two opponents must see each other) */
            return 1;
        unlink(p, q);
        s_used[q] = 0;
    }
    return 0;
}

/* ---- the matchdays: perfect matchings of the matches left ---- */

static uint8_t s_m[MAXM][2], s_left[MAXM];     /* the matches; 1 = not on a day yet */
static int32_t s_nm;
static uint8_t s_free[N], s_day[N / 2];
static int32_t s_steps, s_nday;

static int32_t choices(int32_t c)
{
    int32_t n = 0;
    for (int32_t e = 0; e < s_nm; e++)
        if (s_left[e] && (s_m[e][0] == c || s_m[e][1] == c) && s_free[s_m[e][0]] && s_free[s_m[e][1]])
            n++;
    return n;
}

static int32_t matching(void)
{
    int32_t c = -1, best = 99;
    for (int32_t x = 0; x < N; x++)
        if (s_free[x]) {
            const int32_t k = choices(x);
            if (k < best) {
                best = k;
                c = x;
            }
        }
    if (c < 0)
        return 1;                               /* every club has its match */
    if (++s_steps > 20000)
        return 0;
    uint8_t cand[8];                            /* (a club has at most 8 matches) */
    int32_t nc = 0;
    for (int32_t e = 0; e < s_nm && nc < 8; e++)
        if (s_left[e] && (s_m[e][0] == c || s_m[e][1] == c) && s_free[s_m[e][0]] && s_free[s_m[e][1]])
            cand[nc++] = (uint8_t)e;
    shuffle(cand, nc);
    for (int32_t k = 0; k < nc; k++) {
        const int32_t e = cand[k];
        s_free[s_m[e][0]] = s_free[s_m[e][1]] = 0;
        s_day[s_nday++] = (uint8_t)e;
        if (matching())
            return 1;
        s_nday--;
        s_free[s_m[e][0]] = s_free[s_m[e][1]] = 1;
    }
    return 0;
}

/* The s_nm matches into `days` matchdays of 18 (cal), or 0. */
static int32_t schedule(int32_t days, uint8_t (*cal)[2])
{
    for (int32_t t = 0; t < 200; t++) {
        int32_t d = 0;
        for (int32_t e = 0; e < s_nm; e++)
            s_left[e] = 1;
        for (; d < days; d++) {
            for (int32_t x = 0; x < N; x++)
                s_free[x] = 1;
            s_steps = 0;
            s_nday = 0;
            if (!matching())
                break;
            for (int32_t k = 0; k < N / 2; k++) {
                const int32_t e = s_day[k];
                s_left[e] = 0;
                cal[d * (N / 2) + k][0] = s_m[e][0];
                cal[d * (N / 2) + k][1] = s_m[e][1];
            }
        }
        if (d == days)
            return 1;
    }
    return 0;
}

/* ---- 6 pots: a perfect matching of a pair of pots ---- */

static uint8_t s_side[N];                       /* (the 6-pot matching: clubs taken) */

static int32_t match6(int32_t P, int32_t Q, int32_t taken)
{
    int32_t p = -1;
    for (int32_t x = 6 * P; x < 6 * P + 6 && p < 0; x++)
        if (!s_side[x])
            p = x;
    if (p < 0 || taken == 6)
        return 1;
    uint8_t cand[6];
    int32_t nc = 0;
    for (int32_t q = 6 * Q; q < 6 * Q + 6; q++)
        if (q != p && !s_side[q] && fits(p, q))
            cand[nc++] = (uint8_t)q;
    shuffle(cand, nc);
    for (int32_t k = 0; k < nc; k++) {
        const int32_t q = cand[k];
        /* (two pots: p and q each once; one pot: both of the pair at once) */
        s_side[p] = 1;
        s_side[q] |= 2;                         /* (q of pot Q taken: bit 2 on the Q side) */
        link(p, q);
        if (match6(P, Q, taken + (P == Q ? 2 : 1)))
            return 1;
        unlink(p, q);
        s_side[p] = 0;
        s_side[q] &= (uint8_t)~2;
    }
    return 0;
}

void LeaguePhase_Pots(const uint32_t *key, const uint8_t *country, int32_t six, uint8_t *order)
{
    uint8_t by[N], used[N] = { 0 };
    const int32_t pot = six ? 6 : 9, cap = six ? 2 : 3;
    for (int32_t i = 0; i < N; i++) {           /* strongest first, stable */
        int32_t j = i;
        while (j > 0 && key[by[j - 1]] < key[i]) {
            by[j] = by[j - 1];
            j--;
        }
        by[j] = (uint8_t)i;
    }
    for (int32_t q = 0; q < N; q++) {
        int32_t pick = -1;
        for (int32_t i = 0; i < N && pick < 0; i++) {
            if (used[by[i]])
                continue;
            int32_t same = 0;
            for (int32_t r = q - q % pot; r < q; r++)
                same += country[order[r]] == country[by[i]];
            if (same < cap)
                pick = by[i];
        }
        for (int32_t i = 0; i < N && pick < 0; i++)
            if (!used[by[i]])
                pick = by[i];
        used[pick] = 1;
        order[q] = (uint8_t)pick;
    }
}

static int32_t draw(int32_t six, uint8_t (*cal)[2]);

int32_t LeaguePhase_Draw(const uint8_t *country, int32_t six, uint32_t seed, uint8_t (*cal)[2])
{
    s_country = country;
    s_rng = seed;
    for (s_relax = 0; s_relax < 3; s_relax++)   /* (a field no draw fits: a country's rule relaxed, */
        if (draw(six, cal))                     /*  never a season without its league phase) */
            return s_relax + 1;
    return 0;
}

int32_t LeaguePhase_Relaxed(void)
{
    return s_relax;
}

static int32_t draw(int32_t six, uint8_t (*cal)[2])
{
    for (int32_t attempt = 0; attempt < (s_relax ? 200 : 1000); attempt++) {
        for (int32_t c = 0; c < N; c++)
            s_nopp[c] = 0;
        s_nm = 0;
        int32_t ok = 1;
        if (!six) {
            uint8_t order[16];
            for (int32_t i = 0; i < 16; i++)
                order[i] = (uint8_t)i;
            shuffle(order, 16);
            for (int32_t k = 0; k < 16 && ok; k++) {
                const int32_t P = order[k] >> 2, Q = order[k] & 3;
                for (int32_t i = 0; i < 9; i++)
                    s_P[i] = (uint8_t)(9 * P + i);
                shuffle(s_P, 9);
                for (int32_t c = 0; c < N; c++)
                    s_used[c] = 0;
                s_same = (uint8_t)(P == Q);
                s_Q = (uint8_t)Q;
                if (!bij(0, 9)) {
                    ok = 0;
                    break;
                }
                for (int32_t i = 0; i < 9; i++) {
                    s_m[s_nm][0] = s_P[i];
                    s_m[s_nm++][1] = s_f[i];
                }
            }
            if (ok && schedule(8, cal))
                return 1;
            continue;
        }
        /* 6 pots: every pair of pots (its own included) a perfect matching */
        uint8_t pairs[21];
        int32_t np = 0;
        for (int32_t P = 0; P < 6; P++)
            for (int32_t Q = P; Q < 6; Q++)
                pairs[np++] = (uint8_t)(P * 6 + Q);
        shuffle(pairs, np);
        static uint8_t adj[N][N];               /* (static: the Amiga's stack is small) */
        for (int32_t k = 0; k < np && ok; k++) {
            const int32_t P = pairs[k] / 6, Q = pairs[k] % 6;
            for (int32_t c = 0; c < N; c++)
                s_side[c] = 0;
            ok = match6(P, Q, 0);
        }
        if (!ok)
            continue;
        /* each club's 6 matches: 3 at home, by walks along them */
        for (int32_t a = 0; a < N; a++)
            for (int32_t b = 0; b < N; b++)
                adj[a][b] = 0;
        for (int32_t c = 0; c < N; c++)
            for (int32_t i = 0; i < s_nopp[c]; i++)
                adj[c][s_opp[c][i]] = 1;
        for (int32_t v = 0; v < N; v++)
            for (;;) {
                int32_t cur = v, any = 0;
                for (int32_t w = 0; w < N; w++)
                    any |= adj[v][w];
                if (!any)
                    break;
                for (;;) {
                    int32_t w = 0;
                    while (w < N && !adj[cur][w])
                        w++;
                    if (w == N)
                        break;
                    adj[cur][w] = adj[w][cur] = 0;
                    s_m[s_nm][0] = (uint8_t)cur;
                    s_m[s_nm++][1] = (uint8_t)w;
                    cur = w;
                }
            }
        if (s_nm == 108 && schedule(6, cal))
            return 1;
    }
    return 0;
}

int32_t LeaguePhase_Check(const uint8_t *country, int32_t six, const uint8_t (*cal)[2])
{
    const int32_t days = six ? 6 : 8, pots = six ? 6 : 4, pot = N / pots;
    for (int32_t d = 0; d < days; d++) {        /* 1: every club once a matchday */
        uint8_t seen[N] = { 0 };
        for (int32_t k = 0; k < N / 2; k++) {
            seen[cal[d * (N / 2) + k][0]]++;
            seen[cal[d * (N / 2) + k][1]]++;
        }
        for (int32_t c = 0; c < N; c++)
            if (seen[c] != 1)
                return 1;
    }
    for (int32_t c = 0; c < N; c++) {
        uint8_t opps[8], ho[6] = { 0 }, aw[6] = { 0 };
        int32_t n = 0, home = 0;
        for (int32_t m = 0; m < days * (N / 2); m++) {
            if (cal[m][0] == c) {
                opps[n++] = cal[m][1];
                ho[cal[m][1] / pot]++;
                home++;
            } else if (cal[m][1] == c) {
                opps[n++] = cal[m][0];
                aw[cal[m][0] / pot]++;
            }
        }
        if (n != days || home != days / 2)
            return 2;                           /* 2: half the matches at home */
        for (int32_t p = 0; p < pots; p++)      /* 3: the pots (4: one home, one away each; 6: one each) */
            if (six ? ho[p] + aw[p] != 1 : ho[p] != 1 || aw[p] != 1)
                return 3;
        for (int32_t i = 0; i < n; i++) {
            if (country[opps[i]] == country[c])
                return 4;                       /* 4: never its own country */
            int32_t same = 0;
            for (int32_t j = 0; j < n; j++) {
                if (j != i && opps[j] == opps[i])
                    return 5;                   /* 5: never twice */
                same += country[opps[j]] == country[opps[i]];
            }
            if (same > 2)
                return 6;                       /* 6: at most 2 of one country */
        }
    }
    return 0;
}

/* ---- the table ---- */

void LeaguePhase_Rank(const LeaguePhaseRow *row, const uint8_t (*cal)[2], int32_t matches, int32_t finished,
                      uint8_t *order)
{
    static uint32_t key[N][4];                  /* (static: the Amiga's stack) */
    int32_t opts[N] = { 0 }, ogd[N] = { 0 }, ogf[N] = { 0 };
    for (int32_t m = 0; m < matches; m++)       /* each side's figures added to the other's */
        for (int32_t s = 0; s < 2; s++) {
            const int32_t a = cal[m][s], b = cal[m][!s];
            opts[a] += row[b].points;
            ogf[a] += row[b].goalsFor;
            ogd[a] += (int16_t)row[b].goalsFor - (int16_t)row[b].goalsAgainst;
        }
    for (int32_t c = 0; c < N; c++) {
        const LeaguePhaseRow *r = &row[c];
        key[c][0] = (uint32_t)r->points << 16 | (uint16_t)(r->goalsFor - r->goalsAgainst + 0x8000);
        key[c][1] = (uint32_t)(r->goalsFor & 0xFF) << 16 | (uint32_t)r->awayGoals << 8 |
                    (uint32_t)(r->wins & 0xF) << 4 | (r->awayWins & 0xF);
        if (!finished) {                        /* (UEFA: while it runs, then the names) */
            key[c][2] = 0;
            key[c][3] = 255u - r->nameRank;
        } else {
            key[c][2] = (uint32_t)(opts[c] & 0xFF) << 24 | (uint32_t)((ogd[c] + 0x800) & 0xFFF) << 12 |
                        (uint32_t)(ogf[c] & 0xFFF);
            key[c][3] = (uint32_t)(uint8_t)~r->discipline << 24 | (r->coef < 0xFFFFFF ? r->coef : 0xFFFFFF);
        }
    }
    for (int32_t i = 0; i < N; i++) {           /* best first, the contest order among equals */
        int32_t j = i;
        while (j > 0) {
            const int32_t a = order[j - 1];
            int32_t k = 0;
            while (k < 4 && key[a][k] == key[i][k])
                k++;
            if (k == 4 || key[a][k] > key[i][k])
                break;
            order[j] = order[j - 1];
            j--;
        }
        order[j] = (uint8_t)i;
    }
}

/* =====================================================================
 * The league phase in the engine: the SWOS 96/97 Mod's hooks (lp36.py: lp_days, lp_start,
 * lp_pair, lp_res, lp_sort, lp_gend, lp_draw), on our type-2 contest. A world's European cup
 * (ids 0..2, World 2026-27) whose first stage is 2 groups of 18; the engine's counters already
 * make 18 matches a day of 2 groups of 18; ours: the draw's 144 (108) matches in 72 (54) pair
 * steps a cycle, one table of 36, the knockout play-offs' list.
 * ===================================================================== */
#include "game_types.h"
#include "diy_competition.h"
#include "world.h"
#include "random.h"

extern DIYCompetitionEx _diyCompetitionStart;
extern DIYTournamentState _diyFileBufferCopy;
extern uint8_t  _seasonPlaying;

typedef char _lp_state_check[(sizeof(((LeaguePhaseState *)0)->cal) == LEAGUE_PHASE_MATCHES * 2) ? 1 : -1];

#define ROW 18
#define CONFERENCE 2                            /* (the world's UEFA Cup slot: 6 pots of 6) */

static const uint8_t kPlayoffTies[16] = {       /* the Mod's playoff_slots(): places (0-based) of */
    16, 9, 22, 13, 18, 11, 20, 15,              /* the play-off ties, unseeded first, in the order */
    23, 8, 17, 12, 21, 10, 19, 14,              /* of the round-of-16 bracket */
};
static const uint8_t kR16Seeds[8] = { 0, 7, 2, 4, 1, 6, 3, 5 };     /* (lp36 R16_SEEDS) */

static int32_t ours_id(uint8_t compId)
{
    return SWOS_WORLDS && World_Current() != WORLD_9697 && compId <= CONFERENCE;
}

int32_t LeaguePhase_StageDays(uint8_t compId, uint16_t groups, uint16_t perGroup)
{
    return ours_id(compId) && groups == 2 && perGroup == 18 ? (compId == CONFERENCE ? 6 : 8) : 0;
}

/* The league phase is in _diyCompetitionStart (any of its matches). */
static int32_t ours(void)
{
    const DIYCompetition *b = &_diyCompetitionStart.base;
    return ours_id((uint8_t)b->field_2E) && b->matchPairsPerRound == 2 && b->totalMatchesInRound == 18;
}

static DIYTeamStats *lp_row(int32_t row)
{
    return (DIYTeamStats *)((uint8_t *)&_diyCompetitionStart + (uint32_t)row * ROW + DIY_STANDINGS_OFFSET);
}

static TeamFileEx *lp_team(int32_t row)
{
    return (TeamFileEx *)diy_resolve_team(lp_row(row)->teamSelOffset);
}

static uint16_t lp_word(int32_t row)
{
    const TeamFileEx *t = lp_team(row);
    return PACK_BE_U16(t->team.countryNumber, t->team.teamOrdinal);
}

void LeaguePhase_StageStart(void)
{
    if (!ours())
        return;
    DIYCompetitionEx *c = &_diyCompetitionStart;
    const int32_t six = c->base.field_2E == CONFERENCE, matches = six ? 108 : 144;
    c->matchData.league.totalMatchesPerSection = (uint16_t)(matches / 2);      /* (2 groups a pair step) */
    c->matchData.league.totalMatchesPerSectionLive = (uint16_t)(matches / 2);
    c->leagueTotalMatches = (uint16_t)(six ? 6 : 8);
    LeaguePhaseState *st = &c->leaguePhase;
    for (int32_t r = 0; r < N; r++)
        st->awayGoals[r] = st->awayWins[r] = st->discipline[r] = 0;
    /* The pots: positions = the contest list's, their rows' clubs by strength (the 16 players'
     * prices; the club coefficients later), a country capped; the draw over them. */
    uint8_t row_of[N], country[N], order[N], drawn[N];
    uint16_t words[N];
    uint32_t key[N], seed = (uint32_t)c->base.field_2E * 7919u + _seasonPlaying * 131u;
    for (int32_t p = 0; p < N; p++) {
        row_of[p] = c->matchData.league.shuffleAndPad[p];
        const TeamFileEx *t = lp_team(row_of[p]);
        uint32_t v = 0;
        for (int32_t k = 0; k < 16; k++)
            v += (uint8_t)t->players[k].price;
        words[p] = PACK_BE_U16(t->team.countryNumber, t->team.teamOrdinal);
        key[p] = UefaCoef_PotKey((uint8_t)c->base.field_2E, words[p], v);  /* (club coefficients) */
        country[p] = World_FileCountry(t->team.countryNumber);    /* (a second team file: its country, include/world.h) */
        seed = seed * 31u + t->team.teamOrdinal + ((uint32_t)t->team.countryNumber << 8);
    }
    LeaguePhase_Pots(key, country, six, order);
    for (int32_t q = 0; q < N; q++)
        drawn[q] = country[order[q]];
    uint8_t cal[LEAGUE_PHASE_MATCHES][2];
    if (!LeaguePhase_Draw(drawn, six, seed, cal))
        return;                                 /* (never: a relaxed draw at worst) */
    for (int32_t m = 0; m < matches; m++) {
        st->cal[m][0] = row_of[order[cal[m][0]]];
        st->cal[m][1] = row_of[order[cal[m][1]]];
    }
    UefaCoef_StageStart((uint8_t)c->base.field_2E, words, N);
}

int32_t LeaguePhase_Pair(uint8_t **team1, uint8_t **team2)
{
    if (!ours())
        return 0;
    const DIYCompetitionEx *c = &_diyCompetitionStart;
    const int32_t m = 2 * c->leagueSectionState.sectionMatchesProgress + c->base.subRoundCounter;
    if (m >= (c->base.field_2E == CONFERENCE ? 108 : 144))
        return 0;
    *team1 = (uint8_t *)&_diyCompetitionStart + (uint32_t)c->leaguePhase.cal[m][0] * ROW;
    *team2 = (uint8_t *)&_diyCompetitionStart + (uint32_t)c->leaguePhase.cal[m][1] * ROW;
    return 1;
}

/* The match the engine is about to play (game_init.c; the Mod's lp_game): a league-phase result
 * between these two clubs takes its cards from the match's statistics. */
static const void *s_game1, *s_game2;
static uint8_t s_gamePlay;

void LeaguePhase_GameStarting(const void *team1, const void *team2)
{
    s_game1 = team1;
    s_game2 = team2;
    s_gamePlay = 1;
}

/* Criterion 9 (the Mod's lp_disc): a yellow card 1 point, a sending-off 3; a simulated match
 * draws them at the real rates (2025-26's league phases: 1.90 yellows and 0.119 expulsions a
 * club), Poisson thresholds in 1/256 -- the Mod's tables, read as it reads them (its counting
 * stops at the first 255, the tables' end). */
static const uint8_t kYellows[] = { 38, 111, 180, 224, 245, 253, 255 };
static const uint8_t kReds[] = { 227, 254, 255 };

static int32_t cards(const uint8_t *t)
{
    const uint8_t b = (uint8_t)Rand2();
    int32_t k = 0;
    while (t[k] != 0xFF && b >= t[k])
        k++;
    return k;
}

static void disc_add(int32_t row, int32_t points)
{
    LeaguePhaseState *st = &_diyCompetitionStart.leaguePhase;
    const int32_t v = st->discipline[row] + points;
    st->discipline[row] = (uint8_t)(v > 255 ? 255 : v);
}

static void discipline(int32_t home, int32_t away)
{
    extern TeamStatisticsData _team1StatsData, _team2StatsData;
    const void *th = lp_team(home), *ta = lp_team(away);
    if (s_gamePlay && ((th == s_game1 && ta == s_game2) || (th == s_game2 && ta == s_game1))) {
        const TeamStatisticsData *sh = th == s_game1 ? &_team1StatsData : &_team2StatsData;
        const TeamStatisticsData *sa = th == s_game1 ? &_team2StatsData : &_team1StatsData;
        s_gamePlay = 0;
        disc_add(home, sh->bookings + 3 * sh->sendingsOff);
        disc_add(away, sa->bookings + 3 * sa->sendingsOff);
        return;
    }
    const int32_t yh = cards(kYellows), rh = cards(kReds);
    disc_add(home, yh + 3 * rh);
    const int32_t ya = cards(kYellows), ra = cards(kReds);
    disc_add(away, ya + 3 * ra);
}

void LeaguePhase_Result(uint8_t *team1, uint8_t *team2, uint16_t goals1, uint16_t goals2)
{
    if (!ours())
        return;
    LeaguePhaseState *st = &_diyCompetitionStart.leaguePhase;
    const int32_t away = (int32_t)(team2 - (uint8_t *)&_diyCompetitionStart) / ROW;
    if (away < 0 || away >= N)
        return;
    st->awayGoals[away] = (uint8_t)(st->awayGoals[away] + goals2);
    if (goals2 > goals1)
        st->awayWins[away]++;
    const int32_t home = (int32_t)(team1 - (uint8_t *)&_diyCompetitionStart) / ROW;
    if (home >= 0 && home < N)
        discipline(home, away);
    if (home >= 0 && home < N)
        UefaCoef_Result((uint8_t)_diyCompetitionStart.base.field_2E, lp_word(home), lp_word(away), goals1, goals2);
}

/* The table's order, rows best first (finished: the phase over -- the full criteria). */
static void rank_rows(int32_t finished, uint8_t *order)
{
    DIYCompetitionEx *c = &_diyCompetitionStart;
    LeaguePhaseRow r[N];
    for (int32_t i = 0; i < N; i++) {
        const DIYTeamStats *s = lp_row(i);
        r[i].points = s->points;
        r[i].goalsFor = s->goalsFor;
        r[i].goalsAgainst = s->goalsAgainst;
        r[i].wins = s->wins;
        r[i].awayGoals = c->leaguePhase.awayGoals[i];
        r[i].awayWins = c->leaguePhase.awayWins[i];
        r[i].discipline = c->leaguePhase.discipline[i];
        r[i].coef = UefaCoef_On() ? UefaCoef_Club(lp_word(i))     /* (criterion 10) */
                                  : 255u - (uint32_t)i;             /* (no coefficients: the contest order) */
        r[i].nameRank = 0;
        const uint8_t *ni = diy_resolve_team_name(s->teamSelOffset);
        for (int32_t j = 0; j < N; j++) {       /* (how many sort before it by name) */
            const uint8_t *nj = diy_resolve_team_name(lp_row(j)->teamSelOffset), *a = nj, *b = ni;
            while (*a && *a == *b)
                a++, b++;
            r[i].nameRank = (uint8_t)(r[i].nameRank + (*a < *b));
        }
    }
    LeaguePhase_Rank(r, (const uint8_t (*)[2])c->leaguePhase.cal, c->base.field_2E == CONFERENCE ? 108 : 144,
                     finished, order);
}

int32_t LeaguePhase_Sort(void)
{
    if (!ours())
        return 0;
    DIYCompetitionEx *c = &_diyCompetitionStart;
    uint8_t order[N];
    rank_rows(c->minSubstitutes == 0, order);
    for (int32_t p = 0; p < N; p++)             /* places 1-18 in the first group's, 19-36 the second's */
        c->matchData.league.teamEntryOffsets[p] = (uint16_t)(order[p] * ROW);
    return 1;
}

static uint8_t s_final[N];                      /* the final table (GroupsEnd before -> after) */
static uint8_t s_finalOk;

void LeaguePhase_GroupsEnd(uint8_t *diy, int32_t after)
{
    if (!after) {
        s_finalOk = (uint8_t)ours();
        if (s_finalOk) {
            uint16_t w[N];
            rank_rows(1, s_final);
            for (int32_t p = 0; p < N; p++)
                w[p] = lp_word(s_final[p]);
            UefaCoef_PhaseEnd((uint8_t)_diyCompetitionStart.base.field_2E, w);
        }
        return;
    }
    if (!s_finalOk)
        return;
    s_finalOk = 0;
    uint8_t *list = ((DIYTournamentState *)diy)->teamIndexOrder;
    for (int32_t k = 0; k < 16; k++)            /* the play-off ties, unseeded first */
        list[k] = s_final[kPlayoffTies[k]];
    for (int32_t k = 0; k < 8; k++)             /* the round of 16's seeded club of tie k */
        list[16 + k] = s_final[kR16Seeds[k]];
    for (int32_t p = 24; p < N; p++)            /* places 25-36 (out): the list stays the 36, once each */
        list[p] = s_final[p];
    for (int32_t b = 0; b < 4; b++) {           /* the draws: which seeded club meets which, which */
        uint8_t x;                              /* half each seeded pair goes in */
        if (Rand2() & 1) {
            x = list[2 * b + 1];
            list[2 * b + 1] = list[2 * b + 9];
            list[2 * b + 9] = x;
        }
        if (Rand2() & 1) {
            x = list[16 + b];
            list[16 + b] = list[20 + b];
            list[20 + b] = x;
        }
    }
}

int32_t LeaguePhase_KnockoutDraw(uint8_t *diy, uint16_t numTeams)
{
    DIYTournamentState *t = (DIYTournamentState *)diy;
    if (!ours_id((uint8_t)t->header.field_2E) || t->slots[0].groups != 2 || t->slots[0].teamsPerGroup != 18 ||
        t->header.roundComplete == 0)
        return 0;
    if (numTeams == 16 && t->header.roundComplete == 2) {   /* the round of 16 */
        /* Tie k: play-off winner k v seeded club k. The play-off's losers ([8..15]) take the
         * seeds' places [16..23]: the tournament's list stays the 36 once each (a club's place
         * in it is how far it got: FindTeamInDIY). */
        uint8_t w[8], l[8], seed[8];
        for (int32_t k = 0; k < 8; k++) {
            w[k] = t->teamIndexOrder[k];        /* (the play-off winners, tie order) */
            l[k] = t->teamIndexOrder[8 + k];
            seed[k] = t->teamIndexOrder[16 + k];
        }
        for (int32_t k = 0; k < 8; k++) {
            t->teamIndexOrder[2 * k] = w[k];
            t->teamIndexOrder[2 * k + 1] = seed[k];
            t->teamIndexOrder[16 + k] = l[k];
        }
    }
    uint16_t w[16];                             /* (the round's clubs: their coefficients) */
    const int32_t n = numTeams < 16 ? numTeams : 16;
    for (int32_t i = 0; i < n; i++)
        w[i] = BE_READ_U16((const uint8_t *)&t->teamIds[t->teamIndexOrder[i]]);
    UefaCoef_Draw((uint8_t)t->header.field_2E, t->header.roundComplete, w, n);
    return 1;
}


int32_t LeaguePhase_Showing(void)
{
    return ours();
}

const char *LeaguePhase_PageHeader(int32_t page)
{
    static const char *const kHeaders[2] = { "STANDINGS 1-18", "STANDINGS 19-36" };
    return kHeaders[page ? 1 : 0];
}

uint16_t LeaguePhase_RowColour(int32_t place)
{
    return place <= 8 ? 13 : place <= 24 ? 14 : 0;     /* (light blue, green; out: plain) */
}


/* ---- a world's European cups ---- */

extern CompetitionCareer _careerContests[];

void LeaguePhase_InstallCups(CompetitionLeaguePhase *cups)
{
    for (int32_t k = 0; k < 3; k++) {           /* (the contest table's first three: the euro cups) */
        _careerContests[k].competition = (Competition *)&cups[k];
        _careerContests[k].templateSize = (uint16_t)sizeof(CompetitionLeaguePhase);
    }
}
