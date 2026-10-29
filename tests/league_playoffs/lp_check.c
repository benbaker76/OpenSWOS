/* The league phase's draw (src/league_phase.c) on random fields, its rules checked (the Mod's
 * lp36.check / check6): countries with up to 5 clubs each, as the access list gives, random
 * strengths, the pots by them as the Mod makes them (LeaguePhase_Pots: a country capped). */
#include <stdio.h>
#include <stdlib.h>
#include "../../src/league_phase.c"
/* The table's order on built rows: points, then goal difference, goals, away goals; equals in
 * the contest order; while the phase runs, the names after the away wins. */
static int rank_cases(void)
{
    LeaguePhaseRow r[36] = { { 0 } };
    uint8_t cal[144][2] = { { 0 } }, o[36];
    int fails = 0;
    for (int c = 0; c < 36; c++) { r[c].points = 10; r[c].goalsFor = 10; r[c].goalsAgainst = 10; r[c].nameRank = (uint8_t)(35 - c); }
    r[5].points = 20;                           /* 1st: the most points */
    r[7].goalsFor = 14; r[7].goalsAgainst = 12; /* 2nd: goal difference +2 */
    r[9].goalsFor = 12; r[9].goalsAgainst = 10; /* 3rd: +2 as well, fewer goals */
    r[11].awayGoals = 3;                        /* 4th: level, more away goals */
    LeaguePhase_Rank(r, cal, 0, 1, o);
    if (o[0] != 5 || o[1] != 7 || o[2] != 9 || o[3] != 11) { printf("FAIL rank: %d %d %d %d\n", o[0], o[1], o[2], o[3]); fails++; }
    if (o[4] != 0 || o[5] != 1 || o[35] != 35) { printf("FAIL rank: equals not in the contest order\n"); fails++; }
    LeaguePhase_Rank(r, cal, 0, 0, o);          /* running: the names (row 35 sorts first) */
    if (o[4] != 35 || o[35] != 0) { printf("FAIL rank: while running, not by name (%d %d)\n", o[4], o[35]); fails++; }
    printf("%s: the league phase's table, %d case(s) failed\n", fails ? "FAIL" : "PASS", fails);
    return fails;
}

int main(int argc, char **argv)
{
    if (rank_cases())
        return 1;
    const int n = argc > 1 ? atoi(argv[1]) : 2000;
    static const int sizes[2][14] = { { 5, 5, 4, 4, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1 },     /* CL / EL */
                                       { 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1 } };   /* UECL */
    int fails = 0, relaxed = 0, broke[7] = { 0 };
    for (int six = 0; six < 2; six++)
        for (int it = 0; it < n; it++) {
            uint8_t country[36], cal[144][2];
            int k = 0;
            unsigned r = (unsigned)it * 2654435761u + (unsigned)six;
            for (int c = 0; k < 36; c++) {
                const int m = c < 14 ? sizes[six][c] : 1;
                for (int j = 0; j < m && k < 36; j++) country[k++] = (uint8_t)c;
            }
            uint32_t key[36];                   /* random strengths; the pots by them, capped */
            uint8_t order[36], potted[36];
            for (int i = 0; i < 36; i++) {
                r = r * 1664525u + 1013904223u;
                key[i] = r >> 12;
            }
            LeaguePhase_Pots(key, country, six, order);
            for (int i = 0; i < 36; i++) potted[i] = country[order[i]];
            for (int i = 0; i < 36; i++) country[i] = potted[i];
            const int got = LeaguePhase_Draw(country, six, (uint32_t)it * 7919u + 1, cal);
            if (!got) { fails++; printf("no draw: %s field %d\n", six ? "6 pots" : "4 pots", it); continue; }
            const int e = LeaguePhase_Check(country, six, (const uint8_t (*)[2])cal);
            if (got > 1) {                      /* relaxed: only the country rules may give */
                relaxed++;
                if (e && e != 4 && e != 6) broke[e]++;
                continue;
            }
            if (e) broke[e]++;
        }
    int b = 0;
    for (int i = 1; i < 7; i++) b += broke[i];
    printf("%s: the league phase's draw, %d fields x 2 formats: %d without a draw, %d relaxed (a country's rule), %d breaking another"
           " (%d %d %d %d %d %d)\n", fails || b ? "FAIL" : "PASS", n, fails, relaxed, b, broke[1], broke[2],
           broke[3], broke[4], broke[5], broke[6]);
    return fails || b;
}
