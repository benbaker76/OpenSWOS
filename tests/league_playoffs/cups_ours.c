/* Our port (src/world_cups.c, included) of the Mod's cf_build over the same cases. */
#include <stdio.h>
#include <stdlib.h>
#include "../../src/world_cups.c"
#include "cups_cases.h"
DIYCompetitionEx _diyCompetitionStart;
TeamFileEx _careerTeam;
uint8_t _diyLeagueSeasonStateTable[2000];
uint16_t _teamsCountryNumbers[256];
uint16_t _internationalAutoAdvanceFlag;
void *_competitionsTable[256];
int32_t CupD_RoundStringIndex(uint8_t c, uint16_t r) { (void)c; (void)r; return -1; }
void Comp_Register(Competition *c) { (void)c; }
void DIY_ShuffleTeamGridPairs(void) { }
void SaveSelectedTeams(void) { }
void RestoreSelectedTeams(void) { }
void Career_AutoAdvanceCupCompetitionFromRecord(uint8_t *r) { (void)r; }
TeamId *comp_team_seed_list(Competition *c) { (void)c; static TeamId t[32]; return t; }
static WorldCup s_cups[2];
const WorldCup *World_Cups(int32_t *count) { *count = 2; return s_cups; }
static const LeagueLevel s_levels[4] = { { 0, 1, { 20 }, 0 }, { 1, 1, { 20 }, 0 },
                                         { 2, 3, { 20, 20, 20 }, 0 }, { 5, 9, { 17 }, 0 } };
static LeagueGroupClub s_clubs[256];
static LeagueGroups s_lg = { 20, 4, s_levels, 256, s_clubs };
const LeagueGroups *LeagueGroups_ForCountry(uint16_t c) { return c == 20 ? &s_lg : 0; }
int main(int argc, char **argv)
{
    unsigned N = argc > 1 ? (unsigned)atoi(argv[1]) : 20000;
    struct cup_case cc;
    for (int c = 0; c < 2; c++) {
        s_cups[c].country = 20; s_cups[c].n = (uint8_t)CUP_N[c]; s_cups[c].groups = (uint8_t)CUP_G[c];
        s_cups[c].quota = CUP_Q[c];
    }
    for (unsigned it = 0; it < N; it++) {
        make_case(it, &cc);
        for (int i = 0; i < 256; i++) {
            s_clubs[i].ordinal = (uint8_t)i; s_clubs[i].base = cc.base[i];
            _diyLeagueSeasonStateTable[i] = (uint8_t)cc.delta[i];
            g_worldCups.place[i] = cc.place[i];
        }
        g_worldCups.seasons = 1;
        g_worldCups.placesCountry = 20;
        g_worldCups.placesSeason = cc.ranked ? 0 : 7;
        _careerTeam.team.countryNumber = cc.career < 0 ? 21 : 20;
        _careerTeam.team.teamOrdinal = (uint8_t)(cc.career < 0 ? 0 : cc.career);
        printf("%u", it);
        for (int c = 0; c < 2; c++) {
            TeamId out[32];
            int n = build(&s_cups[c], out);
            printf(" |");
            for (int k = 0; k < n; k++) printf(" %u", out[k].teamOrdinal);
        }
        printf("\n");
    }
    return 0;
}
