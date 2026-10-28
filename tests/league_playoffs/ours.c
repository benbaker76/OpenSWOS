/* Our port (src/league_playoffs.c, included) over the same tables: its moves, in its order
 * (Italian rules first, then the pack's knockouts). */
#include <stdio.h>
#include <stdlib.h>
#include "../../src/league_playoffs.c"
#include "tables.h"
DIYCompetitionEx _diyCompetitionStart;
uint8_t *diy_resolve_team(int32_t sel) { (void)sel; return 0; }
TeamFileEx _careerTeam;
uint16_t _playPromotedAnimation, _playRelegatedAnimation;
uint16_t _teamsCountryNumbers[256];
int32_t TeamPool_Swap(uint8_t c, uint8_t o) { (void)c; (void)o; return -1; }
void TeamPool_SwapsDone(void) { }
void LeagueGroups_Enter(uint16_t g) { (void)g; }
WorldPlayoffState g_worldPlayoff;
static uint16_t s_cupw;
uint16_t WorldCups_LeagueCupWinner(uint8_t c) { (void)c; return s_cupw; }
void WorldCups_KeepPlace(uint8_t c, uint16_t d, uint16_t t, uint8_t p) { (void)c; (void)d; (void)t; (void)p; }
void CupD_KeepPlace(uint8_t c, uint16_t d, uint16_t t, int32_t p, int32_t n) { (void)c; (void)d; (void)t; (void)p; (void)n; }
void CupD_NoteArrival(uint8_t c, uint8_t o) { (void)c; (void)o; }

static const uint8_t PROM[14] = { 0, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
static const uint8_t REL[14] = { 3, 3, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2 };
static LeaguePlayoffs lp = { 20, 14, PROM, REL, 1, 1, 2, 3, spec };
const LeaguePlayoffs *World_LeaguePlayoffs(int32_t *count) { *count = 1; return &lp; }
const LeagueGroups *LeagueGroups_ForCountry(uint16_t c) { (void)c; return 0; }
static int8_t table[2000], scratch[2000];
int main(int argc, char **argv)
{
    unsigned N = argc > 2 ? (unsigned)atoi(argv[2]) : 20000;
    struct trow t[14][32]; int n[14];
    load_spec(argv[1]);
    for (unsigned it = 0; it < N; it++) {
        make_tables(it, t, n);
        s_cupw = cup_winner(it, t, n);
        LeaguePlayoffs_Begin();
        for (int d = 0; d < 14; d++) {
            for (int k = 0; k < n[d]; k++) {
                struct lp_row *r = &s_in.t[d][k];
                r->team = t[d][k].team; r->glob = t[d][k].glob; r->won = t[d][k].won;
                r->gf = t[d][k].gf; r->ga = t[d][k].ga; r->pts = t[d][k].pts;
            }
            s_in.n[d] = (u8)n[d];
        }
        s_in.nd = 14;
        LeaguePlayoffs_Decide(20, table, scratch);
        printf("%u", it);
        /* dp moves carry a delta of +-1 and come first; the knockouts' after (as the Mod's
         * two lists): told apart by how many dp_season made */
        for (int i = 0; i < s_nmv; i++)
            printf(" %c%u:%d", i < s_dp_moves ? 'd' : 'g', s_mv[i].glob, s_mv[i].delta);
        printf("\n");
    }
    return 0;
}
