/* The B-team rules (src/league_playoffs.c second_teams, the Mod's sec_post) on built cases:
 * Italy 2026-27's B teams with their first teams placed on purpose. */
#include <stdio.h>
#include <stdlib.h>
#include "../../src/league_playoffs.c"
#include "sec_cases.h"
DIYCompetitionEx _diyCompetitionStart;
uint8_t *diy_resolve_team(int32_t sel) { (void)sel; return 0; }
TeamFileEx _careerTeam;
uint16_t _playPromotedAnimation, _playRelegatedAnimation;
uint16_t _teamsCountryNumbers[256];
int32_t TeamPool_Swap(uint8_t c, uint16_t o) { (void)c; (void)o; return -1; }
void TeamPool_SwapsDone(void) { }
void LeagueGroups_Enter(uint16_t g) { (void)g; }
WorldPlayoffState g_worldPlayoff;
uint16_t _selTeamNumber;
int32_t WorldPlayoff_PlayedTie(int32_t p, uint16_t a, uint16_t b, uint16_t *w) { (void)p; (void)a; (void)b; (void)w; return 0; }
const WorldStageTitle *WorldPlayoff_StageTitle(int32_t k) { (void)k; return 0; }
int32_t WorldPlayoff_PlayedTitle(int32_t p, uint16_t *w, uint16_t *l) { (void)p; (void)w; (void)l; return 0; }
const LeaguePlayoffs *World_LeaguePlayoffs(int32_t *count) { *count = 0; return 0; }
const WorldPromedio *World_Promedio(uint8_t c, int32_t *count) { (void)c; *count = 0; return 0; }   /* (the Mod's GP_PROM_TAB here: none) */
static const LeagueLevel LV[4] = { { 0, 1, { 20 }, 0 }, { 1, 1, { 20 }, 0 }, { 2, 3, { 20, 20, 20 }, 0 },
                                   { 5, 9, { 17, 17, 17, 18, 17, 18, 17, 17, 18 }, 0 } };
static const LeagueGroups LG = { 20, 4, LV, 0, 0 };
const LeagueGroups *LeagueGroups_ForCountry(uint16_t c) { return c == 20 ? &LG : 0; }
static const uint8_t PROM[14] = { 0, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
static const uint8_t REL[14] = { 3, 3, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2 };
static const uint8_t SEC[3][2] = { { 44, 5 }, { 67, 6 }, { 89, 0 } };   /* Juve NG, Atalanta U23, Inter U23 */
static const LeaguePlayoffs LP = { 20, 14, PROM, REL, 1, 1, 2, 3, 0, 3, SEC, 1, 3 };
enum { BASE = 424, JUVE = 5, JUVENG = 44, INTER = 0, INTERU23 = 89 };

static void tables(void)                /* every place a club of its own, no B team anywhere */
{
    static const int SZ[14] = { 20, 20, 20, 20, 20, 17, 17, 17, 18, 17, 18, 17, 17, 18 };
    s_in.nd = 14;
    for (int d = 0; d < 14; d++) {
        s_in.n[d] = SZ[d];
        for (int k = 0; k < SZ[d]; k++) s_in.t[d][k].glob = (u16)(1000 + d * 32 + k);
    }
}
static void put(int d, int place, int ordinal) { s_in.t[d][place - 1].glob = (u16)(BASE + ordinal); }
static void record(void)                /* the Mod's moves by place (no play-offs here) */
{
    for (int d = 0; d < 14; d++)
        for (int k = 0; k < s_in.n[d]; k++)
            s_want[d][k] = (s8)(k < PROM[d] && d > 0 ? -1 : k >= s_in.n[d] - REL[d] ? 1 : 0);
}
static int fails;
static void expect(const char *what, int d, int place, int want)
{
    int got = s_want[d][place - 1];
    if (got != want) { printf("FAIL %s: division %d place %d moves %d, not %d\n", what, d, place, got, want); fails++; }
}
/* `random N`: the cases of sec_cases.h, each club's move after the rules, as mod_sec.c prints */
static int random_cases(unsigned N)
{
    unsigned changed = 0;
    for (unsigned it = 0; it < N; it++) {
        unsigned short glob[14][32]; int move[14][32];
        make_case(it, glob, move);
        s_in.nd = 14;
        for (int d = 0; d < 14; d++) {
            s_in.n[d] = (u8)SZ[d];
            for (int k = 0; k < SZ[d]; k++) { s_in.t[d][k].glob = glob[d][k]; s_want[d][k] = (s8)move[d][k]; }
        }
        second_teams(&LP, &LG, BASE);
        for (int d = 0, ch = 0; d < 14 && !ch; d++)
            for (int k = 0; k < SZ[d]; k++) if (s_want[d][k] != move[d][k]) { changed++; ch = 1; break; }
        printf("%u", it);
        for (int d = 0; d < 14; d++)
            for (int k = 0; k < SZ[d]; k++) printf("%c", s_want[d][k] < 0 ? 'u' : s_want[d][k] > 0 ? 'd' : '.');
        printf("\n");
    }
    for (int c = 0; c < 4; c++) {          /* the countries of SHAPES, no groups */
        const SecShape *sh = &SHAPES[c];
        uint8_t prom[3], rel[3], sec[4][2];
        for (int d = 0; d < sh->nd; d++) { prom[d] = (uint8_t)sh->prom[d]; rel[d] = (uint8_t)sh->rel[d]; }
        for (int i = 0; i < sh->nb; i++) { sec[i][0] = (uint8_t)sh->bt[i][0]; sec[i][1] = (uint8_t)sh->bt[i][1]; }
        const LeaguePlayoffs lp = { (uint8_t)sh->country, (uint8_t)sh->nd, prom, rel, 0, 0, 0, 0, 0,
                                    (uint8_t)sh->nb, sec, (uint8_t)sh->top, (uint8_t)sh->flags };
        for (unsigned it = 0; it < N; it++) {
            unsigned short glob[14][32]; int move[14][32];
            make_shape_case(sh, it, glob, move);
            s_in.nd = (u8)sh->nd;
            for (int d = 0; d < sh->nd; d++) {
                s_in.n[d] = (u8)sh->sz[d];
                for (int k = 0; k < sh->sz[d]; k++) { s_in.t[d][k].glob = glob[d][k]; s_want[d][k] = (s8)move[d][k]; }
            }
            second_teams(&lp, 0, (u16)sh->base);
            for (int d = 0, ch = 0; d < sh->nd && !ch; d++)
                for (int k = 0; k < sh->sz[d]; k++) if (s_want[d][k] != move[d][k]) { changed++; ch = 1; break; }
            printf("%s %u", sh->name, it);
            for (int d = 0; d < sh->nd; d++)
                for (int k = 0; k < sh->sz[d]; k++) printf("%c", s_want[d][k] < 0 ? 'u' : s_want[d][k] > 0 ? 'd' : '.');
            printf("\n");
        }
    }
    fprintf(stderr, "%u of %u cases changed by the rules\n", changed, 5 * N);
    return 0;
}

int main(int argc, char **argv)
{
    _teamsCountryNumbers[20] = BASE;
    if (argc > 2 && argv[1][0] == 'r')
        return random_cases((unsigned)atoi(argv[2]));
    /* 1: Juventus NG wins Serie C group A, Juventus in Serie A: up to Serie B */
    tables(); put(0, 10, JUVE); put(2, 1, JUVENG); record(); second_teams(&LP, &LG, BASE);
    expect("B team up, first team above", 2, 1, -1);
    expect("B team up: the 2nd stays", 2, 2, 0);
    /* 2: Juventus in Serie B: its B team may not join it -- held, the 2nd goes up */
    tables(); put(1, 10, JUVE); put(2, 1, JUVENG); record(); second_teams(&LP, &LG, BASE);
    expect("B team held (first team in Serie B)", 2, 1, 0);
    expect("the next club up instead", 2, 2, -1);
    /* 3: Inter relegated from Serie B to Serie C, Inter U23 mid-table in group C: one down,
     *    the best placed relegated of its group stays */
    tables(); put(1, 19, INTER); put(4, 10, INTERU23); record(); second_teams(&LP, &LG, BASE);
    expect("first team down into its level: B team down", 4, 10, 1);
    expect("the best placed relegated stays", 4, 20, 0);
    /* 4: a B team never above Serie B: Juventus NG 1st of Serie B, held */
    tables(); put(0, 5, JUVE); put(1, 1, JUVENG); record(); second_teams(&LP, &LG, BASE);
    expect("B team held (top: Serie B)", 1, 1, 0);
    expect("the 3rd up instead", 1, 3, -1);
    printf("%s: B-team rules, %d case(s) failed\n", fails ? "FAIL" : "PASS", fails);
    return fails != 0;
}
