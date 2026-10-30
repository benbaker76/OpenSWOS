/* Our port (src/league_playoffs.c, included) over the same tables: its moves, in its order
 * (Italian rules first, then the pack's knockouts). */
#include <stdio.h>
#include <stdlib.h>
#include "../../src/league_playoffs.c"
#include "tables.h"
DIYCompetitionEx _diyCompetitionStart;
uint8_t *diy_resolve_team(int32_t sel) { (void)sel; return 0; }
TeamFileEx _careerTeam;
uint16_t _selTeamNumber;
uint8_t _primaryQualifiedTeamGrids[10 * 684];
uint16_t _cachedPrimaryQualifiedTeamIds[9];
uint16_t _playPromotedAnimation, _playRelegatedAnimation;
uint16_t _teamsCountryNumbers[256];
int32_t TeamPool_Swap(uint8_t c, uint8_t o) { (void)c; (void)o; return -1; }
void TeamPool_SwapsDone(void) { }
void LeagueGroups_Enter(uint16_t g) { (void)g; }
WorldPlayoffState g_worldPlayoff;
int32_t WorldPlayoff_PlayedTie(int32_t p, uint16_t a, uint16_t b, uint16_t *w) { (void)p; (void)a; (void)b; (void)w; return 0; }
const WorldStageTitle *WorldPlayoff_StageTitle(int32_t k) { (void)k; return 0; }
static uint16_t s_cupw;
uint16_t WorldCups_LeagueCupWinner(uint8_t c) { (void)c; return s_cupw; }
void WorldCups_KeepPlace(uint8_t c, uint16_t d, uint16_t t, uint8_t p) { (void)c; (void)d; (void)t; (void)p; }
void CupD_KeepPlace(uint8_t c, uint16_t d, uint16_t t, int32_t p, int32_t n) { (void)c; (void)d; (void)t; (void)p; (void)n; }
void CupD_NoteArrival(uint8_t c, uint8_t o) { (void)c; (void)o; }

static const uint8_t PROM[16] = { 0, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
static const uint8_t REL[16] = { 3, 3, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2 };
static LeaguePlayoffs lp = { 20, 14, PROM, REL, 1, 1, 2, 3, spec };
const LeaguePlayoffs *World_LeaguePlayoffs(int32_t *count) { *count = 1; return &lp; }
const WorldPromedio *World_Promedio(uint8_t c, int32_t *count) { (void)c; *count = 0; return 0; }   /* (the Mod's GP_PROM_TAB here: none) */
const LeagueGroups *LeagueGroups_ForCountry(uint16_t c) { (void)c; return 0; }
static int8_t table[2000], scratch[2000];
int main(int argc, char **argv)
{
    unsigned N = argc > 2 ? (unsigned)atoi(argv[2]) : 20000;
    struct trow t[16][32]; int n[16];
    load_spec(argv[1]);
    if (argc > 3) { set_shape(argv[3]); lp.divisions = (uint8_t)ND; lp.italy = 0; }
    for (unsigned it = 0; it < N; it++) {
        make_tables(it, t, n);
        s_cupw = cup_winner(it, t, n);
        LeaguePlayoffs_Begin();
        for (int d = 0; d < ND; d++) {
            for (int k = 0; k < n[d]; k++) {
                struct lp_row *r = &s_in.t[d][k];
                r->team = t[d][k].team; r->glob = t[d][k].glob; r->won = t[d][k].won;
                r->gf = t[d][k].gf; r->ga = t[d][k].ga; r->pts = t[d][k].pts;
            }
            s_in.n[d] = (u8)n[d];
        }
        s_in.nd = (u8)ND;
        {
            struct stages st;
            LeagueStageBank *b = &g_leagueStages[0];
            make_stages(it, t, n, &st);
            b->used = 1; b->key = 20; b->fresh = (uint8_t)st.fresh;
            for (int k = 0; k < 4; k++) {
                b->n[k] = (uint8_t)st.n[k];
                for (int i = 0; i < st.n[k]; i++) {
                    const struct trow *x = &st.r[k][i];
                    b->r[k][i].team = x->team; b->r[k][i].glob = x->glob; b->r[k][i].won = x->won;
                    b->r[k][i].gf = x->gf; b->r[k][i].ga = x->ga; b->r[k][i].pts = x->pts;
                }
            }
            b->keptDiv = 0; b->nKept = (uint8_t)st.nkeep;
            for (int i = 0; i < st.nkeep; i++) {
                b->kept[i].team = st.keep[i].team; b->kept[i].glob = st.keep[i].glob;
                b->kept[i].won = b->kept[i].gf = b->kept[i].ga = 0; b->kept[i].pts = st.keep[i].pts;
            }
        }
        LeaguePlayoffs_Decide(20, table, scratch);
        printf("%u", it);
        /* dp moves carry a delta of +-1 and come first; the knockouts' after (as the Mod's
         * two lists): told apart by how many dp_season made */
        for (int i = 0; i < s_nmv; i++)
            printf(" %c%u:%d", i < s_dp_moves ? 'd' : 'g', s_mv[i].glob, s_mv[i].delta);
        for (int i = 0; i < s_nres; i++)
            printf(" T%u:%u/%u", s_res[i].kind, s_res[i].glob[0], s_res[i].glob[1]);
        for (int k = 0; k < 3; k++)
            if (s_sres[k].kind) printf(" S%d:%u/%u", k, s_sres[k].glob[0], s_sres[k].glob[1]);
        printf("\n");
    }
    return 0;
}
