/*
 * world_nationals.c -- a classic world's national competitions (include/world_nationals.h).
 * Both targets: the data is baked (src/world_data_8485.c).
 */
#include "world_nationals.h"

#include "competition_data.h"
#include "game_types.h"
#include "national_teams.h"
#include "endian_helpers.h"
#include "world.h"

#include <string.h>
#ifdef SWOS_SDL_BUILD
#include <stdio.h>
#endif

extern uint16_t _worldCupHostTeam;          /* the World Cup's holder (the game's name) */
extern uint16_t _worldCupQualifyingTeam;    /* the World Cup's host */
extern uint16_t _euroChampionshipQualTeam;  /* the European Championship's host */
extern uint16_t _currentSeasonShowing;
extern uint8_t  _nationalTeams[];
extern uint16_t _numNationalTeams, _nat_teams_rand_limit1, _natTeamsRandLimit2;

#if SWOS_WORLDS
/* This season's cycle (careerworld.py w_cyc: the season / 4, the last one after). */
static const WorldNatCycle *cycle(const WorldNationals *n)
{
    uint16_t c = (uint16_t)(_currentSeasonShowing >> 2);
    if (c >= n->cycles)
        c = (uint16_t)(n->cycles - 1);
    return n->cycle[c];
}

/* The Mod's European Championship 1980-92 (careerworld.py EC_STAGES): 8 teams, 2 groups of 4,
 * the semi-finals, the final -- from the record's round-string base on. */
static const uint8_t kEcStages[] = { 3, 8, 2, 4, 4, 0, 4, 2, 0, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x14, 0x14, 0 };
#endif

void WorldNationals_Install(void)
{
#if SWOS_WORLDS
    const WorldNationals *n = World_Nationals();
    if (!n)
        return;
    /* the World Cup: the 1986 format, the preset's (24 teams, 6 groups, the best thirds) */
    _worldCup2.header.roundStringBase = _worldCup.header.roundStringBase;
    memcpy(_worldCup2.header.rounds, _worldCup.header.rounds, sizeof _worldCup2.header.rounds);
    _worldCup2.header.reserved = _worldCup.header.reserved;
    memcpy(_worldCup2.roundRuleFlags, _worldCup.roundRuleFlags, sizeof _worldCup2.roundRuleFlags);
    _worldCup2.header.awayGoalsMode = (int8_t)n->points;     /* (byte 12: a win's points) */
    /* the European Championship: 8 teams */
    {
        CompetitionTournament *ec = &_europeanChampionships2;
        const uint8_t *s = kEcStages;
        ec->header.roundStringBase = (int8_t)*s++;
        for (int32_t r = 0; r < 6; r++) {
            ec->header.rounds[r].teamCount = (int8_t)*s++;
            ec->header.rounds[r].groupCount = (int8_t)*s++;
            ec->header.rounds[r].teamGroupCount = (int8_t)*s++;
        }
        ec->header.reserved = (int8_t)*s++;
        for (int32_t r = 0; r < 4; r++)
            ec->roundRuleFlags[r] = (int8_t)*s++;
        ec->header.awayGoalsMode = (int8_t)n->points;
    }
    /* the national teams (careerworld.py nat_table): the era's nations only, each its
     * nationality and home league in the world; one without loses them (no job) */
    {
        static uint8_t out[NATIONAL_TEAMS_MAX * 6];
        uint8_t group[3][NATIONAL_TEAMS_MAX * 6];
        int32_t len[3] = { 0, 0, 0 };
        const int32_t total = _natTeamsRandLimit2;
        for (int32_t k = 0; k < total && k < (int32_t)NATIONAL_TEAMS_MAX; k++) {
            uint8_t r[6];
            memcpy(r, _nationalTeams + 6 * k, 6);
            int32_t g = k < _nat_teams_rand_limit1 ? 0 : k < _numNationalTeams ? 1 : 2;
            const WorldNatRule *rule = 0;
            for (int32_t i = 0; i < n->rules; i++)
                if (n->rule[i].file == r[0] && n->rule[i].ordinal == r[1])
                    rule = &n->rule[i];
            if (rule && rule->unused)
                continue;
            if (rule) {
                if (rule->nationality) {
                    r[2] = rule->nationality;
                    r[3] = rule->home;
                    if (g > 1)
                        g = 1;          /* (a team of no nationality in 96/97 gets one: Croatia's slot) */
                } else {
                    r[2] = r[3] = 0;
                    g = 2;
                }
            }
            memcpy(group[g] + 6 * len[g], r, 6);
            len[g]++;
        }
        int32_t at = 0;
        for (int32_t g = 0; g < 3; g++) {
            memcpy(out + 6 * at, group[g], 6 * (size_t)len[g]);
            at += len[g];
        }
        memset(_nationalTeams, 0, 6 * (size_t)total);
        memcpy(_nationalTeams, out, 6 * (size_t)at);
        _nat_teams_rand_limit1 = (uint16_t)len[0];
        _numNationalTeams = (uint16_t)(len[0] + len[1]);
        _natTeamsRandLimit2 = (uint16_t)at;
#ifdef SWOS_SDL_BUILD
        fprintf(stderr, "[world-nationals] %d national teams (%d for a job, %d with a nationality); "
                        "%d cycles, holder %04X\n", (int)at, (int)len[0], (int)(len[0] + len[1]),
                (int)n->cycles, (unsigned)n->holder);
#endif
    }
#endif
}

void WorldNationals_CareerStart(void)
{
#if SWOS_WORLDS
    const WorldNationals *n = World_Nationals();
    if (n)
        _worldCupHostTeam = n->holder;
#endif
}

int32_t WorldNationals_Hosts(void)
{
#if SWOS_WORLDS
    const WorldNationals *n = World_Nationals();
    if (!n)
        return 0;
    _worldCupQualifyingTeam = cycle(n)->wcHost;
    _euroChampionshipQualTeam = cycle(n)->ecHost;
    return 1;
#else
    return 0;
#endif
}

#if SWOS_WORLDS


extern void *_careerCupPickList[];
extern uint16_t _selTeamNumber;

/* careerworld.py w_fill: the 8 groups the cycle's, without skip1 / skip2 (the holder, the hosts);
 * a win's points, the places, the teams; the player's group his pick list's first. */
static void fill(Competition **table, const WorldNatGroup *g, uint8_t points, uint16_t skip1, uint16_t skip2,
                 int32_t teams)
{
    for (int32_t i = 0; i < 8; i++) {
        CompetitionLeague *group = (CompetitionLeague *)table[i];
        uint8_t *base = (uint8_t *)group;
        uint16_t *slot = (uint16_t *)(base + 7 + base[7] + 2u * (sizeof(char *) - sizeof(uint32_t)));
        int32_t n = 0;
        for (int32_t t = 0; t < g[i].teams; t++) {
            const uint16_t w = g[i].team[t];
            if (w == skip1 || w == skip2)
                continue;
            if (teams) {
                BE_WRITE_U16(slot + n, w);
                if (w == _selTeamNumber)
                    _careerCupPickList[0] = (void *)group;
            }
            n++;
        }
        group->header.pointsForWin = (int8_t)points;
        group->divisions[0].promoted = (int8_t)g[i].places;
        group->divisions[0].teams = (int8_t)n;
    }
}
#endif

int32_t WorldNationals_FillWorldCupGroups(void)
{
#if SWOS_WORLDS
    const WorldNationals *n = World_Nationals();
    if (!n)
        return 0;
    fill(_worldCupQualifyingGroupsTable, cycle(n)->wcq, n->points, _worldCupHostTeam, _worldCupQualifyingTeam, 1);
#ifdef SWOS_SDL_BUILD
    fprintf(stderr, "[world-nationals] season %u: the World Cup's qualifying groups (holder %04X, host %04X)\n",
            (unsigned)_currentSeasonShowing, (unsigned)_worldCupHostTeam, (unsigned)_worldCupQualifyingTeam);
#endif
    return 1;
#else
    return 0;
#endif
}

int32_t WorldNationals_FillEuroGroups(void)
{
#if SWOS_WORLDS
    const WorldNationals *n = World_Nationals();
    if (!n)
        return 0;
    fill(_ecQualifyingGroupsTable, cycle(n)->ecq, n->points, _euroChampionshipQualTeam,
         _euroChampionshipQualTeam, 1);
#ifdef SWOS_SDL_BUILD
    fprintf(stderr, "[world-nationals] season %u: the European Championship's qualifying groups (host %04X)\n",
            (unsigned)_currentSeasonShowing, (unsigned)_euroChampionshipQualTeam);
#endif
    return 1;
#else
    return 0;
#endif
}

/* The groups' points, places and teams again, not their lists (kept by the save): a loaded career's
 * records are their templates' (the season end's finalists). */
void WorldNationals_RefreshGroups(int32_t worldCup)
{
#if SWOS_WORLDS
    const WorldNationals *n = World_Nationals();
    if (!n)
        return;
    if (worldCup)
        fill(_worldCupQualifyingGroupsTable, cycle(n)->wcq, n->points, _worldCupHostTeam, _worldCupQualifyingTeam, 0);
    else
        fill(_ecQualifyingGroupsTable, cycle(n)->ecq, n->points, _euroChampionshipQualTeam,
             _euroChampionshipQualTeam, 0);
#else
    (void)worldCup;
#endif
}

/* (The finalists: src/career_eurocups.c, beside the game's group updates they use.) */
const WorldNatCycle *WorldNationals_Cycle(void)
{
#if SWOS_WORLDS
    const WorldNationals *n = World_Nationals();
    return n ? cycle(n) : 0;
#else
    return 0;
#endif
}
