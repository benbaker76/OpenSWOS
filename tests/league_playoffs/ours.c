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

/* The career club's contest for each play-off (world_playoff.c k_make, its ContestEntrants and
 * presim), the club the first entrant that waits in round 1: as mod_ko. */
static void ours_ko(struct trow (*t)[32], const int *n)
{
    static LeagueStageRow tab[16][32];
    const LeagueStageRow *tabs[3];
    int32_t divs[3], ns[3];
    for (int d = 0; d < ND; d++)
        for (int k = 0; k < n[d]; k++) {
            tab[d][k].team = t[d][k].team; tab[d][k].glob = t[d][k].glob; tab[d][k].won = t[d][k].won;
            tab[d][k].gf = t[d][k].gf; tab[d][k].ga = t[d][k].ga; tab[d][k].pts = t[d][k].pts;
        }
    const uint8_t *s;
    for (int pi = 0; (s = LeaguePlayoffs_SpecAt(20, pi)) != 0; pi++) {
        int ne = s[1], grp = 0, wait = -1, in0[64] = { 0 };
        const uint8_t *r = s + 6 + 4 * ne, *r0 = r, *sp = 0;
        uint16_t team[64], glob[64], pts[64];
        uint8_t prio[64];
        for (int k = 0; k < s[2]; k++) { grp |= r[1] & 0x80; r += 6 + 2 * r[0]; }
        int dv[3], nd = 0;
        for (int i = 0; i < ne && !grp; i++) {  /* (its entrants' divisions: at most three) */
            int j = 0;
            while (j < nd && dv[j] != s[7 + 4 * i]) j++;
            if (j == nd) { if (nd < 3) dv[nd++] = s[7 + 4 * i]; else grp = 1; }
        }
        if (grp || r0[4] || ne > 64) continue;
        _selTeamNumber = 0xFFFF;
        for (int j = 0; j < nd; j++) { divs[j] = dv[j]; tabs[j] = tab[dv[j]]; ns[j] = n[dv[j]]; }
        if (dv[0] >= ND || LeaguePlayoffs_ContestEntrantsEx(20, nd, divs, tabs, ns, pi, team, glob, prio, pts, &sp) != ne)
            continue;
        for (int i = 0; i < r0[0]; i++)
            for (int x = 0; x < 2; x++) if (r0[6 + 2 * i + x] < ne) in0[r0[6 + 2 * i + x]] = 1;
        for (int i = 0; i < ne && wait < 0; i++) {
            if (in0[i]) continue;
            wait = i;
            for (int j = 0; j < ne; j++) if (j != i && team[j] == team[i]) wait = -1;
        }
        if (wait < 0) continue;
        _selTeamNumber = team[wait];
        LeaguePlayoffs_ContestEntrantsEx(20, nd, divs, tabs, ns, pi, team, glob, prio, pts, &sp);
        LeaguePlayoffs_ContestSeed(pi, team, ne);
        printf(" K%d:", pi);
        for (int i = 0; i < r0[0]; i++) {
            const int a = r0[6 + 2 * i], b = r0[7 + 2 * i];
            int w = -1;
            if (a < ne && b < ne)
                w = team[a] == team[b] ? a : LeaguePlayoffs_ContestTie(a, b, r0[1], r0[2] & 0x7F, r0[3]);
            printf("%04X,", w >= 0 ? team[w] : 0);
        }
        if (s[5] & 2)
            for (int p = 0; p < 256; p++)
                for (int j = 0; j < ne; j++) if (prio[j] == p) printf("r%04X", team[j]);
    }
    for (int d = 0; d < ND; d++)                /* (the club's play-off: gp_need, each club) */
        for (int k = 0; k < n[d]; k++) {
            uint8_t other[2];
            int32_t no = 0;
            const int32_t pi = LeaguePlayoffs_ClubPlayoff(20, d, tab[d], n[d], tab[d][k].team, other, &no);
            if (pi == -1) continue;
            printf(" N%d.%d:%d", d, k, pi < -1 ? -2 - pi : pi);
            for (int j = 0; j < no; j++) printf("/%d", other[j]);
        }
}
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
        if (argc > 4) ours_ko(t, n);
        printf("\n");
    }
    return 0;
}
