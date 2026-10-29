/* Our port (src/world_cupd.c, included) of the Mod's cd_rank, cd_season, cd_make. */
#include <stdio.h>
#include <stdlib.h>
#include "../../src/world_cupd.c"
#include "cupd_cases.h"
uint8_t _diyLeagueSeasonStateTable[2000];
uint16_t _teamsCountryNumbers[256];
TeamFileEx _careerTeam;
DIYCompetitionEx _diyCompetitionStart;
CompetitionSlotBuffer _diyCompetitionContextBuf, _primaryCompetitionBuf, _tertiaryCompetitionContextBuf;
uint8_t *_tertiaryCompetitionPtr;
static uint8_t s_season[256];
uint8_t *GetCurrentSeasonPointer(void) { return s_season; }
uint8_t *_diyCompetitionPtr;
uint32_t *_diyCompetitionTablePtr;
uint16_t _competitionSlotUpdatedFlag, _teamSelectionKeyCacheCount;
uint8_t _teamSelectionKeyCache[16];
Playoffs _div1Playoffs;
WorldCupsState g_worldCups;
WorldPlayoffState g_worldPlayoff;
TeamPoolState *TeamPool_State(uint8_t c) { (void)c; return 0; }
void Career_SetCurrentCompetitionSlot(uint8_t *c, uint16_t s, uint16_t d, uint16_t h) { (void)c; (void)s; (void)d; (void)h; }
void Career_LoadCompetitionSlotStateFromBuffer(uint8_t *s) { (void)s; }
void Career_StoreCompetitionSlotStateToBuffer(uint8_t *s) { (void)s; }
void DIY_BuildTeamSelectionPointerCache(void) { }
void DIY_UpdateCompetitionProgressOrAdvanceSlot(uint8_t *b) { (void)b; }
void AppendTeamsToSelectionFromTmdOrTeamFile(uint16_t *o, uint16_t *t, uint16_t n) { (void)o; (void)t; (void)n; }
static uint8_t s_base[256];
static const uint8_t s_round[CD_ROUNDS][8];
static WorldCupD s_cup = { 20, CD_ROUNDS, s_round, 0, 0, CD_FIRST, CD_GROUPS, s_base, 0, 0, 0, { 0, 0 } };
const WorldCupD *World_CupD(void) { return &s_cup; }
int main(int argc, char **argv)
{
    unsigned N = argc > 1 ? (unsigned)atoi(argv[1]) : 20000;
    struct cd_case cc;
    for (unsigned it = 0; it < N; it++) {
        make_cd_case(it, &cc);
        for (int i = 0; i < 256; i++) s_base[i] = cc.base[i];
        g_worldCups.seasons = (uint8_t)(cc.season - 126 - 1);   /* (the season ended) */
        s_ranked = 0;                                           /* (each case its own season end) */
        for (int d = 0; d < 14; d++)
            for (int k = 0; k < cc.n[d]; k++)
                CupD_KeepPlace(20, (uint16_t)d, (uint16_t)(20 << 8 | cc.order[d][k]), k, cc.n[d]);
        for (int i = 0; i < 256; i++)
            if (cc.arrival[i >> 3] & (1 << (i & 7))) CupD_NoteArrival(20, (uint8_t)i);
        g_worldCups.seasons++;                                  /* (the season to come) */
        for (int i = 0; i < 256; i++) _diyLeagueSeasonStateTable[i] = (uint8_t)cc.next[i];
        cd_make(&s_cup);
        printf("%u %u:", it, g_worldCupD.np);
        for (int t = 0; t < g_worldCupD.np + 64; t++)
            printf(" %04x.%04x", g_worldCupD.side[2 * t], g_worldCupD.side[2 * t + 1]);
        printf("\n");
    }
    return 0;
}
