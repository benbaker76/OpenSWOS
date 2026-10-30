/*
 * states.c -- a world's state championships at the season end (include/states.h; docs/
 * WORLD_2026_27.md): Brazil's estaduais, as the SWOS 96/97 Mod's statesim / ripool glue runs
 * them (its ss_entry, ri_season, st_post): the simulation's table with the contests' overrides
 * (a club that came in through a spare in the place of a league club relegated from its state),
 * every state's stage 1 and knockouts simulated (state_sim.c), then the state pools' swaps
 * (state_pool.c): the identities of the state-only clubs' slots, into the country's file
 * (team_pool.c). The career club's own state is simulated too, until it plays it (its block 0).
 *
 * The tables' team words are the Mod's two files' (World_States): a league club the country's
 * number and its ordinal, a state-only club statesFile and its ordinal among them (ours:
 * firstState + it).
 */
#include "states.h"
#include "state_sim.h"
#include "state_pool.h"
#include "team_pool.h"
#include "world.h"
#include "game_types.h"
#include "comp_identity.h"
#include "endian_helpers.h"
#include "diy_competition.h"
#include "league_phase.h"

#if SWOS_WORLDS

extern uint16_t _teamsCountryNumbers[256];
extern uint8_t _diyLeagueSeasonStateTable[];
extern TeamFileEx _careerTeam;

uint16_t g_stateChampions[STATE_SIM_STATES];    /* (last season's: each state's champion, our team word) */
uint16_t g_stateSeasons;                        /* (the season ends simulated: the season word's, saved) */
static uint8_t s_tab[4096];
static uint8_t s_rank[STATE_SIM_RANK + 16];
static uint8_t s_sb[48];                        /* (S2 below: the knockouts' lists) */

static uint16_t ours(const WorldStates *ws, uint16_t w)
{
    if (!w)
        return 0;
    const uint8_t file = (uint8_t)w, o = (uint8_t)(w >> 8);
    return (uint16_t)(ws->country << 8 | (file == ws->statesFile ? ws->firstState + o : o));
}

void States_Reset(void)
{
    uint8_t *p = (uint8_t *)&RI_STATE;
    for (uint32_t i = 0; i < sizeof RI_STATE; i++)
        p[i] = 0;
    for (int32_t s = 0; s < STATE_SIM_STATES; s++)
        g_stateChampions[s] = 0;
    g_stateSeasons = 0;
    g_stateCup = 0xFF;
    for (uint32_t i = 0; i < sizeof g_stateRank0; i++)
        g_stateRank0[i] = 0;
    for (uint32_t i = 0; i < sizeof s_sb; i++)
        s_sb[i] = 0xFF;
}

/* ---- S2: the career club's own state played (the Mod's statecup / st_ko / st_kend / st_rec) ----
 *
 * Its contest (one id, COMPID_stateContest; the state saved: g_stateCup) in the career's slot 2
 * (Brazil's league cup's, empty): the Mod's statecup.contest_bytes on 96/97's European Cup
 * header -- its stages, round bytes, months, points, meetings, its clubs as the contest has them
 * now (the pools' overrides). The first stage on its fixed calendar (the Mod's lp36.FIXTURES:
 * cross groups, a drawn league phase, a group's round robin), in the rows a league phase keeps
 * its own (DIYCompetitionEx.leaguePhase.cal). Each knockout stage's fixed draw (st_ko) over the
 * group stage's merit order: a label kX the k-th club of group X in it (a club's group: its row
 * / the group's size), the b last waiting (only the ties played), a stage after one with byes
 * its winners and byes by their ties ('pairs'), merit positions; the better placed of stage 1 at
 * home. The group stage's final order is the season end's played ranking (ST_RANK block 0). */
uint8_t g_stateCup = 0xFF;                      /* (the state the career club plays: saved) */
uint8_t g_stateRank0[4 + 2 * STATE_SIM_CLUBS];  /* (its played ranking, block 0: saved) */
static CompetitionTournament s_state;
static uint8_t s_merit[64], s_meritN;           /* (the group stage's merit order: rows) */
static uint8_t s_sb[48];                        /* (the Mod's SB: the round's list, then its ties' winners) */
extern CompetitionTournament _euroCup;
extern uint16_t _selTeamNumber, _isNationalSelector;
extern DIYCompetitionEx _diyCompetitionStart;

static const WorldStates *career_states(void)
{
    return _isNationalSelector ? 0 : World_States((uint8_t)(_selTeamNumber >> 8));
}

static uint16_t mod_word(const WorldStates *ws, uint16_t team)  /* ours -> the tables' (the Mod's) */
{
    const uint8_t o = (uint8_t)team;
    return (uint16_t)(o < ws->firstState ? (team >> 8) | o << 8 : ws->statesFile | (o - ws->firstState) << 8);
}

static uint16_t entry_team(const WorldStates *ws, int32_t s, int32_t j)   /* the contest's j-th club now */
{
    const WorldStateContest *ct = &ws->contest[s];
    if (s < STATE_POOL_POOLS && j < STATE_POOL_ENTRIES && RI_STATE.ovr[s][j])
        return ours(ws, RI_STATE.ovr[s][j]);
    return ct->teams[j];
}

static uint8_t *make_contest(const WorldStates *ws, int32_t s)
{
    const WorldStateContest *ct = &ws->contest[s];
    CompetitionTournament *t = &s_state;
    *t = _euroCup;
    t->header.comp.compId = (int8_t)COMPID_stateContest;
    t->header.comp.beginMonth = (int8_t)ct->beginMonth;
    t->header.comp.endMonth = (int8_t)ct->endMonth;
    t->header.comp.seedReorderFlag = 1;         /* (a fixed list) */
    t->header.seedingType = 0;                  /* (no seeding) */
    t->header.menuState = 0;                    /* (no away goals) */
    t->header.playEachTeam = (int8_t)ct->meetings;
    t->header.awayGoalsMode = (int8_t)ct->winPoints;    /* (byte 12: a win's points, the Mod's h[12]) */
    t->header.roundStringBase = (int8_t)ct->stages;     /* (the stages) */
    uint8_t *z = (uint8_t *)t->header.rounds;
    for (uint32_t i = 0; i < sizeof t->header.rounds; i++)
        z[i] = 0;
    for (int32_t k = 0; k < ct->stages && k < 5; k++) {
        t->header.rounds[k].teamCount = (int8_t)(k ? ct->stage[k - 1][2] : ct->clubs);
        t->header.rounds[k].groupCount = (int8_t)ct->stage[k][0];
        t->header.rounds[k].teamGroupCount = (int8_t)ct->stage[k][1];
    }
    if (ct->stages < 6)
        t->header.rounds[ct->stages].teamCount = (int8_t)ct->stage[ct->stages - 1][2];
    t->header.reserved = 0;
    for (int32_t k = 0; k < 6; k++)
        t->roundRuleFlags[k] = (int8_t)(k < ct->stages ? ct->stageByte[k] : 0);
    t->name[0] = (char *)ct->name;
    t->name[1] = (char *)ct->shortName;
    for (int32_t i = 0; i < 32; i++) {
        const uint16_t w = i < ct->clubs ? entry_team(ws, s, i) : 0;
        t->teamSeedList[i].countryNumber = (uint8_t)(w >> 8);
        t->teamSeedList[i].teamOrdinal = (uint8_t)w;
    }
    g_stateCup = (uint8_t)s;
    return (uint8_t *)t;
}

/* The slot-2 contest of the career club's state (0: none -- not a state's club). */
uint8_t *States_SlotContest(void)
{
    const WorldStates *ws = career_states();
    if (!ws)
        return 0;
    for (int32_t s = 0; s < ws->states; s++)
        for (int32_t j = 0; j < ws->contest[s].clubs; j++)
            if (entry_team(ws, s, j) == _selTeamNumber)
                return make_contest(ws, s);
    return 0;
}

/* A loaded career: its state's contest again (the saved state). */
uint8_t *States_RestoreContest(void)
{
    const WorldStates *ws = career_states();
    return ws && g_stateCup < ws->states ? make_contest(ws, g_stateCup) : 0;
}

static const WorldStateContest *playing(const WorldStates **wsp)
{
    extern DIYTournamentState _diyFileBufferCopy;
    const WorldStates *ws = career_states();
    if (!ws || g_stateCup >= ws->states ||          /* (the tournament's id: a stage's start sets the */
        (uint8_t)_diyFileBufferCopy.header.field_2E != COMPID_stateContest)  /* live one's at its end) */
        return 0;
    if (wsp)
        *wsp = ws;
    return &ws->contest[g_stateCup];
}

/* The first stage's days on its fixed calendar (LeaguePhase_StageDays' twin), 0: the game's own. */
int32_t States_StageDays(uint8_t compId, uint16_t groups, uint16_t perGroup)
{
    const WorldStates *ws = career_states();
    if (compId != COMPID_stateContest || !ws || g_stateCup >= ws->states)
        return 0;
    const WorldStateContest *ct = &ws->contest[g_stateCup];
    return ct->cal && groups == ct->stage[0][0] && perGroup == ct->stage[0][1] ? ct->calDays : 0;
}

static int32_t calendar_on(const WorldStateContest *ct)
{
    const DIYCompetition *b = &_diyCompetitionStart.base;
    return ct->cal && b->matchPairsPerRound == ct->stage[0][0] && b->totalMatchesInRound == ct->stage[0][1] &&
           b->numTeams == ct->clubs;
}

void States_StageStart(void)
{
    const WorldStateContest *ct = playing(0);
    if (!ct || !calendar_on(ct) || ct->calMatches > LEAGUE_PHASE_MATCHES)
        return;
    DIYCompetitionEx *c = &_diyCompetitionStart;
    const int32_t g = ct->stage[0][0] ? ct->stage[0][0] : 1;
    c->matchData.league.totalMatchesPerSection = (uint16_t)(ct->calMatches / g);
    c->matchData.league.totalMatchesPerSectionLive = (uint16_t)(ct->calMatches / g);
    c->leagueTotalMatches = ct->calDays;
    for (int32_t m = 0; m < ct->calMatches; m++) {      /* (list index -> its row) */
        c->leaguePhase.cal[m][0] = c->matchData.league.shuffleAndPad[ct->cal[m][0]];
        c->leaguePhase.cal[m][1] = c->matchData.league.shuffleAndPad[ct->cal[m][1]];
    }
}

int32_t States_Pair(uint8_t **team1, uint8_t **team2)
{
    const WorldStateContest *ct = playing(0);
    if (!ct || !calendar_on(ct))
        return 0;
    const DIYCompetitionEx *c = &_diyCompetitionStart;
    const int32_t g = ct->stage[0][0] ? ct->stage[0][0] : 1;
    const int32_t m = g * c->leagueSectionState.sectionMatchesProgress + c->base.subRoundCounter;
    if (m >= ct->calMatches)
        return 0;
    *team1 = (uint8_t *)&_diyCompetitionStart + (uint32_t)c->leaguePhase.cal[m][0] * 18;
    *team2 = (uint8_t *)&_diyCompetitionStart + (uint32_t)c->leaguePhase.cal[m][1] * 18;
    return 1;
}

/* The group stage's end (beside LeaguePhase_GroupsEnd): after the game's merit order (its
 * qualifiers best first, in bands) -- kept for the knockouts' draw; the first stage's final
 * order the played ranking (the Mod's st_rec: block 0, the tables' words). */
void States_GroupsEnd(uint8_t *diy, int32_t after)
{
    const WorldStates *ws;
    const WorldStateContest *ct = playing(&ws);
    if (!ct || !after)
        return;
    const uint8_t *list = ((DIYTournamentState *)diy)->teamIndexOrder;
    s_meritN = (uint8_t)_diyCompetitionStart.base.numTeams;
    for (int32_t i = 0; i < s_meritN && i < 64; i++)
        s_merit[i] = list[i];
    if (_diyCompetitionStart.base.numTeams != ct->clubs)
        return;                                 /* (a later group stage's: not the first's order) */
    const uint16_t key = ct->key;
    g_stateRank0[0] = (uint8_t)key;
    g_stateRank0[1] = (uint8_t)(key >> 8);
    g_stateRank0[2] = ct->clubs;
    g_stateRank0[3] = 0;
    for (int32_t i = 0; i < ct->clubs && i < STATE_SIM_CLUBS; i++) {
        const uint16_t w = mod_word(ws, entry_team(ws, g_stateCup, s_merit[i]));
        g_stateRank0[4 + 2 * i] = (uint8_t)w;
        g_stateRank0[5 + 2 * i] = (uint8_t)(w >> 8);
    }
}

static int32_t rank0(const WorldStates *ws, uint8_t row)      /* a row's place in block 0, 0xFF none */
{
    const uint16_t w = mod_word(ws, entry_team(ws, g_stateCup, row));
    for (int32_t i = 0; i < g_stateRank0[2] && i < STATE_SIM_CLUBS; i++)
        if ((g_stateRank0[4 + 2 * i] | g_stateRank0[5 + 2 * i] << 8) == w)
            return i;
    return 0xFF;
}

/* A knockout stage's start, after the game's round (the Mod's st_ko after call 0x16711): the
 * stage's fixed draw into the round's list (the live grid), its byes waiting. */
void States_KnockoutStart(void)
{
    const WorldStates *ws;
    const WorldStateContest *ct = playing(&ws);
    if (!ct)
        return;
    DIYCompetitionEx *c = &_diyCompetitionStart;
    const uint8_t *e = ct->draw;
    for (int32_t k = 0; k < ct->draws; k++, e += 4 + e[0]) {
        const int32_t N = e[0], b = e[1], g = e[2], sz = e[3] & 0x3F;
        if (c->base.numTeams != N)
            continue;
        uint8_t t[32];
        if (g == 0) {                           /* 'pairs': c < b ties -> its winner, else a bye */
            for (int32_t i = 0; i < N; i++) {
                const int32_t code = e[4 + i];
                const uint8_t v = code < b ? s_sb[16 + code] : s_sb[code + b];
                if (v == 0xFF)
                    return;                     /* (not known: the game's order) */
                t[i] = v;
            }
        } else if (g == 255) {                  /* merit positions */
            for (int32_t i = 0; i < N; i++)
                t[i] = s_merit[e[4 + i]];
        } else {                                /* labels: the place-th club of a group in the merit order */
            for (int32_t i = 0; i < 32; i++)
                s_sb[i] = 0xFF;
            for (int32_t i = 0; i < N; i++) {
                const int32_t code = e[4 + i], place = code / g, grp = code % g;
                int32_t left = place, found = -1;
                for (int32_t j = 0; j < s_meritN; j++)
                    if (s_merit[j] / (sz ? sz : 1) == grp && left-- == 0) {
                        found = s_merit[j];
                        break;
                    }
                if (found < 0)
                    return;
                t[i] = (uint8_t)found;
            }
        }
        if (e[3] & 0x80) {                      /* host: by stage 1's place */
            const int32_t ties = (g && g != 255 ? N - b : N) / 2;
            for (int32_t i = 0; i < ties; i++) {
                const int32_t ra = rank0(ws, t[2 * i]), rb = rank0(ws, t[2 * i + 1]);
                if ((e[3] & 0x40) ? rb > ra : rb < ra) {
                    const uint8_t x = t[2 * i];
                    t[2 * i] = t[2 * i + 1];
                    t[2 * i + 1] = x;
                }
            }
        }
        for (int32_t i = 0; i < N; i++)
            c->teamGridPairings[i] = s_sb[i] = t[i];
        for (int32_t i = 0; i < 16; i++)
            s_sb[16 + i] = 0xFF;
        if (g && g != 255 && b) {               /* the b last wait: only the ties played */
            c->base.matchPairsPerRound = (int16_t)((N - b) / 2);
            c->base.totalMatchesInRound = (int16_t)((N - b) / 2);
        }
        SWOS_DBG("[states] %s: a knockout stage of %d drawn (%d waiting)\n", ct->name, (int)N, (int)b);
        return;
    }
}

/* A knockout stage's end (the Mod's st_kend): each tie's winner, for a stage after one with byes. */
void States_KnockoutEnd(void)
{
    const DIYCompetitionEx *c = &_diyCompetitionStart;
    if (!playing(0) || s_sb[0] == 0xFF)
        return;
    const int32_t ties = c->base.matchPairsPerRound;
    for (int32_t k = 0; k < ties && k < 16; k++)
        s_sb[16 + k] = c->teamGridPairings[2 * k + (c->matchData.cup.cupRoundResults[k] & 1)];
}

void States_SeasonEnd(uint8_t country)
{
    const WorldStates *ws = World_States(country);
    if (!ws || ws->simBytes > sizeof s_tab)
        return;
    /* the table, each state's entries as its contest has them now (the Mod's st_ovr) */
    for (uint32_t i = 0; i < ws->simBytes; i++)
        s_tab[i] = ws->sim[i];
    for (int32_t s = 0; s < ws->states && s < STATE_POOL_POOLS; s++) {
        uint8_t *w = s_tab + ws->wordsAt[s];
        const int32_t n = w[-5];                /* (+2 of the 7-byte header: its clubs) */
        for (int32_t j = 0; j < n && j < STATE_POOL_ENTRIES; j++)
            if (RI_STATE.ovr[s][j]) {
                w[2 * j] = (uint8_t)RI_STATE.ovr[s][j];
                w[2 * j + 1] = (uint8_t)(RI_STATE.ovr[s][j] >> 8);
            }
    }
    for (uint32_t i = 0; i < sizeof s_rank; i++)
        s_rank[i] = 0;
    if (g_stateRank0[2] && country == (uint8_t)(_selTeamNumber >> 8))   /* (the career club's played state) */
        for (uint32_t i = 0; i < sizeof g_stateRank0; i++)
            s_rank[i] = g_stateRank0[i];
    for (uint32_t i = 0; i < sizeof g_stateRank0; i++)
        g_stateRank0[i] = 0;
    ss_season(s_tab, s_rank, (uint16_t)(2026 + g_stateSeasons++));
    for (int32_t s = 0; s < ws->states && s < STATE_SIM_STATES; s++)
        g_stateChampions[s] = ours(ws, ss_champ[s]);

    /* the pools: the identities of the states' file's slots, into ours */
    const int32_t base = _teamsCountryNumbers[country] + ws->firstState;
    RI_MGR = (int16_t)_careerTeam.team.globalTeamNumber;
    ri_season(ws->pools, (const signed char *)_diyLeagueSeasonStateTable, ws->world, s_rank, base);
    const int32_t nmap = ws->pools[8] | ws->pools[9] << 8;
    int32_t swapped = 0;
    for (int32_t i = 0; i < nmap && ws->firstState + i < 256; i++)
        if (RI_SWAPPED[0][i >> 3] & (1 << (i & 7))) {
            const int32_t id = RI_STATE.maps[i] ? RI_STATE.maps[i] - 1 : i;
            TeamPool_Assign(country, (uint8_t)(ws->firstState + i),
                            (uint16_t)(id < 256 ? ws->firstState + id : id));
            swapped++;
        }
    if (swapped)
        TeamPool_SwapsDone();
    SWOS_DBG("[states] country %d: %d states simulated, %d slots swapped; champions", (int)country, (int)ws->states,
             (int)swapped);
    for (int32_t s = 0; s < ws->states && s < STATE_SIM_STATES; s++)
        SWOS_DBG(" %04X", (unsigned)g_stateChampions[s]);
    SWOS_DBG("\n");
}

#else

void States_Reset(void) { }
void States_SeasonEnd(uint8_t country) { (void)country; }
uint8_t *States_SlotContest(void) { return 0; }
uint8_t *States_RestoreContest(void) { return 0; }
int32_t States_StageDays(uint8_t compId, uint16_t groups, uint16_t perGroup) { (void)compId; (void)groups; (void)perGroup; return 0; }
void States_StageStart(void) { }
int32_t States_Pair(uint8_t **team1, uint8_t **team2) { (void)team1; (void)team2; return 0; }
void States_GroupsEnd(uint8_t *diy, int32_t after) { (void)diy; (void)after; }
void States_KnockoutStart(void) { }
void States_KnockoutEnd(void) { }
uint8_t g_stateCup = 0xFF;
uint8_t g_stateRank0[4 + 2 * 16];

#endif /* SWOS_WORLDS */
