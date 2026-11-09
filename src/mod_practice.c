/*
 * mod_practice.c -- the practice mod (canonical, bin/mods/practice; docs/MODS.md §18): SWOS 2020's
 * PRACTICE (its practice.mnu, game.cpp ChallengeGame), ported onto the mod API. Both targets.
 * SWOS 2020's alone: petarku's modes are their own mod (src/mod_petarku_practice.c), and neither is
 * built on the other (the owner, 2026-11-09).
 *
 * MODES -> PRACTICE opens its menu (bin/mods/practice/data_json/menus/practice/practiceMenu.json,
 * its handlers practice.<name>):
 *   PENALTY SHOOTOUT  SWOS 2020's (challengeGame2Practice_OnSelect, ChallengeGame gameMode 1): its
 *                     team screen (shootoutTeamsMenu.json: CHOOSE TEAMS, the random groups,
 *                     GOALKEEPER CONTROL), then VERSUS / PLAY MATCH (the kick order beside the line-
 *                     up) / the stadium, named SHOOTOUT, and straight to penalties: the camera at the
 *                     top goal, no result banner, a player's keeper his to dive, the board of kicks,
 *                     the takers and the kits on the pitch; the statistics show the shoot-out
 *   DEMO GAME         two random national teams, CPU v CPU (demoGamePractice_OnSelect)
 *   TEAM TRAINING     its settings (trainingMenu.json; SWOS 2020's practiceSetting.mnu): each line,
 *                     yours and the CPU's, on or off, the goal you attack; START, the team screen,
 *                     then a match whose clock stays at 0 -- no half time, no end but leaving it
 *                     (ESC) -- with no cards and no injuries (trainingModePractice_OnSelect)
 * Each match returns here (the menu again), as SWOS 2020's do. Training's drills (F1-F12) and ghost
 * players are next (docs/MODS.md §18).
 *
 * "changes": "modes": its matches are its own, a normal one untouched -- never matters online.
 */
#include "mod_api.h"
#include "game_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void SetExitMenuFlag(void);

/* ---- PENALTY SHOOTOUT (SWOS 2020's: challengeGame2Practice_OnSelect, ChallengeGame::run with
 * gameMode 1; docs/MODS.md §18) ----
 * Its team screen (selectTeams.mnu as its gameMode 1 shows it): CHOOSE TEAMS (the friendly's team
 * screen titled SHOOTOUT, START SHOOTOUT; a coach plays as a player), or two random teams of a
 * group -- team 1 the player's, team 2 the CPU's -- and GOALKEEPER CONTROL, PLAYER (each time the
 * screen opens) or COMPUTER. */
enum { St_header, St_free, St_choose, St_random, St_all, St_europe, St_africa, St_national, St_nAmerica,
       St_asia, St_original, St_sAmerica, St_oceania, St_custom, St_gkCaption, St_gk, St_exit };
enum { GRP_ALL = -1, GRP_EUROPE, GRP_N_AMERICA, GRP_S_AMERICA, GRP_AFRICA, GRP_ASIA, GRP_OCEANIA,
       GRP_NATIONAL, GRP_ORIGINAL, GRP_CUSTOM };

static int32_t s_test, s_testFrames;  /* SWOS_PRACTICE_SHOOTOUT_TEST: 1 armed, 2 played */
static void shootout_play(ModMatchSetup *setup);
static int32_t s_stPick;                /* -2 left, -1 CHOOSE TEAMS, else a GRP_* (GRP_ALL as -3) */
static uint8_t s_gkPlayer = 1;          /* GOALKEEPER CONTROL: PLAYER */
static uint8_t s_shootout;              /* our shoot-out is the match (its drawing) */
static uint32_t s_rand;

static void st_init(void)
{
    s_gkPlayer = 1;                     /* (SWOS 2020 sets g_GKManual = -1 before the screen) */
    ModApi_MenuText(St_gk, "PLAYER");
}
static void st_pick(int32_t pick) { s_stPick = pick; SetExitMenuFlag(); }
static void st_choose(void) { st_pick(-1); }
static void st_all(void) { st_pick(-3); }
static void st_europe(void) { st_pick(GRP_EUROPE); }
static void st_africa(void) { st_pick(GRP_AFRICA); }
static void st_national(void) { st_pick(GRP_NATIONAL); }
static void st_namerica(void) { st_pick(GRP_N_AMERICA); }
static void st_asia(void) { st_pick(GRP_ASIA); }
static void st_original(void) { st_pick(GRP_ORIGINAL); }
static void st_samerica(void) { st_pick(GRP_S_AMERICA); }
static void st_oceania(void) { st_pick(GRP_OCEANIA); }
static void st_custom(void) { st_pick(GRP_CUSTOM); }
static void st_exit(void) { st_pick(-2); }
static void st_keeper(void)             /* (gkOption_OnSelect) */
{
    s_gkPlayer ^= 1;
    ModApi_MenuText(St_gk, s_gkPlayer ? "PLAYER" : "COMPUTER");
    ModApi_MenuRedraw(St_gk);
}

static uint32_t st_rand(uint32_t n)
{
    if (!s_rand)
        s_rand = ModApi_Ticks() * 2654435761u | 1u;
    s_rand = s_rand * 1664525u + 1013904223u;
    return n ? (s_rand >> 16) % n : 0;
}

/* A team file of the group (SWOS 2020's getRandomTeamNr, its 96/97 files). */
static int32_t group_file(int32_t group)
{
    static const uint8_t kEurope[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
                                       22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
                                       40, 41, 76, 78 };
    static const uint8_t kNAmerica[] = { 51, 60, 73 };
    static const uint8_t kSAmerica[] = { 43, 45, 46, 48, 49, 50, 64, 65, 66, 71, 77 };
    static const uint8_t kAfrica[] = { 42, 69, 79 };
    static const uint8_t kAsia[] = { 55, 67, 75 };
    static const uint8_t kOceania[] = { 44, 62 };
    static const uint8_t kNational[] = { 80, 81, 82, 83, 84, 85 };
#define PICK(a) a[st_rand((uint32_t)sizeof a)]
    switch (group) {
    case GRP_EUROPE: return PICK(kEurope);
    case GRP_N_AMERICA: return PICK(kNAmerica);
    case GRP_S_AMERICA: return PICK(kSAmerica);
    case GRP_AFRICA: return PICK(kAfrica);
    case GRP_ASIA: return PICK(kAsia);
    case GRP_OCEANIA: return PICK(kOceania);
    case GRP_NATIONAL: return PICK(kNational);
    case GRP_ORIGINAL: return 72;
    case GRP_CUSTOM: return 100;
    default:
        for (;;) {                      /* (getRandomTeamNr(): every file there is -- 96/97's, as
                                         *  its grouped list leaves out the numbers with none) */
            const int32_t f = (int32_t)st_rand(87);
            if (f != 9 && f != 47 && !(f >= 52 && f <= 54) && !(f >= 56 && f <= 59) && f != 61 && f != 63 &&
                f != 68 && f != 70)
                return f;
        }
    }
#undef PICK
}

/* Two different random teams of the group (getRandomTeamBuffer: two files, different but in
 * ORIGINAL and CUSTOM, then a team of each, again while the two are one). */
static int32_t random_pair(int32_t group, TeamFileEx out[2])
{
    for (int32_t tries = 0; tries < 64; tries++) {
        const int32_t f0 = group_file(group), f1 = group_file(group);
        if (f0 == f1 && group != GRP_ORIGINAL && group != GRP_CUSTOM)
            continue;
        if (!ModApi_RandomTeamData(f0, &out[0]) || !ModApi_RandomTeamData(f1, &out[1]))
            continue;
        if (memcmp(&out[0], &out[1], sizeof out[0]) != 0)
            return 1;
    }
    return 0;
}

/* The board's bookkeeping (showPenaltyGoalsInfo): who scored each kick, whose turn it is. */
static uint8_t s_kicks[2][256];         /* teamNPenaltyGoalsArray: 1 a kick scored */
static int16_t s_lastGoals[2];
static uint8_t s_turnStarted;           /* penaltyShootoutStarted */
static int8_t  s_turnFirst;             /* penaltyShootoutTeamTurn */
static int16_t s_orderRowY[11];         /* PLAY MATCH: the line-up rows' y, by row (0 the keeper) */
static int8_t  s_orderRows;

static void practice_shootout(void)
{
    ModMatchSetup setup;
    static TeamFileEx teams[2];
    s_stPick = -2;
    ModApi_ShowMenu("practice/shootoutTeamsMenu");
    if (s_stPick == -2)
        return;
    ModApi_DefaultSetup(&setup);
    if (s_stPick == -1) {
        if (!ModApi_ChooseTeamsTitled("SHOOTOUT", "START SHOOTOUT"))
            return;
        setup.chosen = 1;
    } else {
        if (!random_pair(s_stPick == -3 ? GRP_ALL : s_stPick, teams))
            return;
        setup.teamData[0] = &teams[0];
        setup.teamData[1] = &teams[1];
        setup.side[0] = MODSIDE_PLAYER1;    /* (cGame.control1 2, control2 1) */
        setup.side[1] = MODSIDE_CPU;
    }
    shootout_play(&setup);
}

/* SWOS 2020's rules for its shoot-out, and the match. */
static void shootout_play(ModMatchSetup *setup0)
{
    ModMatchSetup setup = *setup0;
    ModMatchResult r;
    setup.shootoutOnly = 1;
    setup.minute = 120;                     /* (ResetTime: g_time 1, 2, 0) */
    setup.keeperDive = s_gkPlayer;
    setup.cameraTop = 1;
    setup.shootoutStats = 1;
    setup.shortStops = 1;                   /* (GS_GK_55_50 = 1, and StartingMatch's 1 for 100) */
    setup.hideHud = MODHUD_RESULT;          /* (ShowResult: out at once) */
    setup.replayMenu = 0;                   /* (ReplayExitMenuAfterFriendly: replaySelected 0) */
    setup.playMatchScreen = s_test == 1 && !getenv("SWOS_PRACTICE_SHOOTOUT_PLAYMATCH") ? 0 : 1;
    if (s_test)
        setup.stadium = 0;
    setup.name = "shootout";
    memset(s_kicks, 0, sizeof s_kicks);
    s_lastGoals[0] = s_lastGoals[1] = 0;
    s_turnStarted = 0;
    s_turnFirst = 0;
    s_orderRows = 0;
    s_shootout = 1;
    if (ModApi_PlayMatch("practice", &setup, &r) == 0)
        fprintf(stderr, "[practice] shoot-out: %d-%d\n", (int)r.penaltyGoals[0], (int)r.penaltyGoals[1]);
    s_shootout = 0;
    if (s_test == 1) {                  /* (SWOS_PRACTICE_SHOOTOUT_TEST, below) */
        s_test = 2;
        const int32_t ok = r.penaltyGoals[0] != r.penaltyGoals[1] && r.goals[0] == 0 && r.goals[1] == 0 &&
                           s_testFrames > 0;
        fprintf(stdout, "PRACTICE SHOOTOUT TEST %s: %d-%d on penalties, %u frames, board drawn %d times\n",
                ok ? "PASS" : "FAIL", (int)r.penaltyGoals[0], (int)r.penaltyGoals[1], (unsigned)r.frames,
                (int)s_testFrames);
#ifdef SWOS_SDL_BUILD
        exit(ok ? 0 : 1);
#endif
    }
}

/* SWOS_PRACTICE_SHOOTOUT_TEST=1 (SWOS_SKIP_INTRO=1): at the first main menu, SWOS 2020's shoot-out
 * between two random teams of every file, CPU v CPU, no screens before it; PASS when the shoot-out
 * had a winner (and no goal in play) and its board was drawn, then quits. */
static void shootout_test_menu(void)
{
    static TeamFileEx teams[2];
    ModMatchSetup setup;
    if (s_test != 1 || !ModApi_IsMainMenu() || s_shootout)
        return;
    if (!random_pair(GRP_ALL, teams)) {
        fprintf(stderr, "[practice] shoot-out test: no two random teams\n");
        return;
    }
    ModApi_DefaultSetup(&setup);
    setup.teamData[0] = &teams[0];
    setup.teamData[1] = &teams[1];
    setup.side[0] = setup.side[1] = MODSIDE_CPU;
    if (getenv("SWOS_PRACTICE_SHOOTOUT_PLAYMATCH"))     /* (to see PLAY MATCH: the player's team 1) */
        setup.side[0] = MODSIDE_PLAYER1;
    shootout_play(&setup);
}

static int32_t our_shootout(void) { return s_shootout && ModApi_ModMatch("practice"); }

/* The board's state as SWOS 2020 keeps it, a frame at a time. */
static int32_t shootout_turn(const ModShootout *so)
{
    for (int32_t t = 0; t < 2; t++) {
        if (so->goals[t] > s_lastGoals[t] && so->attempts[t] >= 1 && so->attempts[t] <= 256)
            s_kicks[t][so->attempts[t] - 1] = 1;    /* (Team1PenaltyGoalInc) */
        s_lastGoals[t] = so->goals[t];
    }
    const int16_t t1 = so->attempts[0], t2 = so->attempts[1];
    if (!s_turnStarted && t1 != t2) {
        s_turnFirst = (int8_t)(t1 > t2 ? 0 : 1);
        s_turnStarted = 1;
    }
    return t1 > t2 ? 0 : t1 < t2 ? 1 : s_turnFirst == 0 ? 1 : 0;
}

#ifdef SWOS_SDL_BUILD
/* ---- its pictures (SWOS 2020's PixelmapAndStrings.h, verbatim; the palette indices as there) ---- */
static const char *const kMarkCheck[11] = {
    ".......121.", ".......121.", "......121#.", "......121..", ".....121#..", ".1...121...",
    "121.121#...", "#121121....", ".#1221#....", "..#121.....", "...#1#.....",
};
static const char *const kMarkX[11] = {
    "33.......33", "323.....323", "#323...323#", ".#323.323#.", "..#32323#..", "...#323#...",
    "...32323...", "..323#323..", ".323#.#323.", "323#...#323", "33#.....#33",
};
static const char *const kArrowRight[7] = {
    ".....6F...", "......6F..", "4455566F2.", "455566FFF8", ".888886F88", ".....6F88.", "......88..",
};
static const char *const kArrowLeft[7] = {
    "..F6......", ".F688.....", "2F6655544.", "FFF6655548", ".F68888888", "..F6......", "...88.....",
};
static const char *const kArrowRightUp[9] = {
    "...66FF2.", ".....6FF8", "....6F6F8", "...566868", "..5658868", ".45588..8", "45488....", ".488.....",
    "..8......",
};
static const char *const kArrowLeftUp[9] = {
    "2FF66....", "FF6888...", "F6F6.....", "68665....", "68.565...", ".8..554..", ".....454.", "......488",
    ".......8.",
};
static const char *const kHeads[3][8] = {
    { "..333...", ".33333..", "33666338", "36261538", ".6666588", ".5635488", "..55488.", "...888.." },
    { "..656...", ".65545..", "65666458", "56261548", ".6666588", ".5635488", "..55488.", "...888.." },
    { "..333...", ".33333..", "33555338", "35251438", ".5555488", ".4534388", "..44388.", "...888.." },
};
static const char *const kUniforms[4][8] = {
    { "........", ".12221..", ".12221+.", "..222++.", "..333+..", "..333+..", "...+++..", "........" },
    { "........", ".21112..", ".21112+.", "..111++.", "..333+..", "..333+..", "...+++..", "........" },
    { "........", ".12121..", ".12121+.", "..212++.", "..333+..", "..333+..", "...+++..", "........" },
    { "........", ".11111..", ".12221+.", "..111++.", "..333+..", "..333+..", "...+++..", "........" },
};
static const char *const kSmallNumbers[10][8] = {     /* (the 9th, blank, row not drawn) */
    { "........", "...2....", "...28...", "...28...", "...28...", "...28...", "....8...", "........" },
    { "........", ".2222...", "..88828.", "..222.8.", ".28888..", ".22222..", "..88888.", "........" },
    { "........", ".2222...", "..8882..", "..222.8.", "...882..", ".2222.8.", "..8888..", "........" },
    { "........", ".2...2..", ".28..28.", ".222228.", "..88828.", ".....28.", "......8.", "........" },
    { "........", ".22222..", ".288888.", ".2222...", "..8882..", ".2222.8.", "..8888..", "........" },
    { "........", "..2222..", ".288888.", ".2222...", ".28882..", "..222.8.", "...888..", "........" },
    { "........", ".22222..", "..88828.", ".....28.", ".....28.", ".....28.", "......8.", "........" },
    { "........", "..222...", ".28882..", "..222.8.", ".28882..", "..222.8.", "...888..", "........" },
    { "........", "..222...", ".28882..", "..22228.", "..88828.", ".2222.8.", "..8888..", "........" },
    { "........", "2..222..", "2828882.", "2828..28", "2828..28", "28.222.8", ".8.8888.", "........" },
};

static uint8_t *s_fb;
static int32_t  s_stride, s_w, s_h;

static void put(int32_t x, int32_t y, uint8_t c)
{
    if (x >= 0 && y >= 0 && x < s_w && y < s_h)
        s_fb[y * s_stride + x] = c;
}

static void fill(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t c)     /* (inclusive) */
{
    for (int32_t y = y0; y <= y1; y++)
        for (int32_t x = x0; x <= x1; x++)
            put(x, y, c);
}

/* A picture: each character its palette index ('.' clear, or `bg` when it is not -1). */
static void pixmap(const char *const *rows, int32_t w, int32_t h, int32_t x, int32_t y, const char *keys,
                   const uint8_t *cols, int32_t bg)
{
    for (int32_t j = 0; j < h; j++)
        for (int32_t i = 0; i < w; i++) {
            const char *k = strchr(keys, rows[j][i]);
            if (rows[j][i] != '.' && k)
                put(x + i, y + j, cols[k - keys]);
            else if (bg >= 0)
                put(x + i, y + j, (uint8_t)bg);
        }
}

static void text(int32_t x, int32_t y, const char *t, int32_t small)
{
    if (x < 0)
        x = 0;
    ModApi_DrawText(x, y, 2 /* kWhiteText2 */, t, small);
}

static int32_t text_w(const char *t, int32_t small) { return ModApi_TextWidth(t, small); }

/* SWOS 2020's three layouts (its screen modes): 0 the DOS view (320 x 200), 1 the Amiga's (336),
 * 2 its wide one (480): each centred in the frame by the difference. */
typedef struct {
    int32_t mode, base;                     /* the layout, the width it was laid out for */
    int32_t nameRight, goalGap, cellX0, cellGap, y0, y1, y2;
    int32_t ox, oy, headGap;                /* the takers panel */
    int32_t bandY, bandH, kitX, kitY1, kitY2;
} Layout;

static void layout(Layout *l)
{
    memset(l, 0, sizeof *l);
    if (s_h <= 200) {
        l->mode = 0; l->base = 320; l->nameRight = 320 - 180; l->goalGap = 12; l->cellX0 = 190; l->cellGap = 22;
        l->y0 = 161; l->y1 = 173; l->y2 = 186; l->ox = 15; l->oy = 60;
        l->bandY = 168; l->bandH = 32; l->kitX = 143; l->kitY1 = 170; l->kitY2 = 183;
    } else if (s_w >= 480) {
        l->mode = 2; l->base = 480; l->nameRight = 480 - 280; l->goalGap = 0; l->cellX0 = 267; l->cellGap = 25;
        l->y0 = 233; l->y1 = 245; l->y2 = 258; l->ox = 50; l->oy = 120; l->headGap = 11;
        l->bandY = 230; l->bandH = 40; l->kitX = 210; l->kitY1 = 242; l->kitY2 = 255;
    } else {
        l->mode = 1; l->base = 336; l->nameRight = 336 - 195; l->goalGap = 10; l->cellX0 = 196; l->cellGap = 25;
        l->y0 = 233; l->y1 = 245; l->y2 = 258; l->ox = 15; l->oy = 120;
        l->bandY = 230; l->bandH = 40; l->kitX = 148; l->kitY1 = 242; l->kitY2 = 255;
    }
}

static void draw_mark(char c, int32_t x, int32_t y)
{
    static const uint8_t cols[] = { 14, 2, 8, 10, 3, 1 };
    pixmap(c == '1' ? kMarkCheck : kMarkX, 11, 11, x, y, "12#304", cols, -1);
}

/* The board (showPenaltyGoalsInfo): the names, the shoot-out's score, five kicks a side and whose
 * turn it is; then the takers of the side kicking (but while the players leave and at the result). */
static void draw_board(const ModShootout *so, const Layout *l, int32_t m)
{
    const int32_t turn = shootout_turn(so);
    ModMatchTeam team[2];
    char buf[24];
    for (int32_t t = 0; t < 2; t++) {
        if (!ModApi_MatchTeam(t, &team[t]))
            team[t].name[0] = '\0';
        const int32_t y = t ? l->y2 : l->y1;
        text(m + l->nameRight - text_w(team[t].name, 0), y, team[t].name, 0);
        snprintf(buf, sizeof buf, "%d", (int)so->goals[t]);
        text(m + (l->base - text_w(buf, 0)) / 2 + l->goalGap, y, buf, 0);
    }
    const int32_t start = so->attempts[0] > 5 ? so->attempts[0] - 5 : 0;
    for (int32_t i = start; i <= start + 5; i++) {
        const int32_t c = i - start, x = m + l->cellX0 + l->cellGap * c;
        if (l->mode != 0 && c <= 4) {
            snprintf(buf, sizeof buf, "%d", (int)i + 1);
            text(x, l->y0, buf, 0);
        }
        for (int32_t t = 0; t < 2; t++) {
            const int32_t y = t ? l->y2 : l->y1, n = so->attempts[t];
            char cell = 0;
            if (c <= 4) {
                if (i < n - 1)
                    cell = s_kicks[t][i] ? '1' : '0';
                else if (i == n - 1)
                    cell = turn == t && !so->over && so->inProgress ? '*' : s_kicks[t][i] ? '1' : '0';
                else
                    cell = '-';
            } else if (turn == t) {
                cell = '*';
            }
            if (cell == '1')
                draw_mark('1', x - 1, y);
            else if (cell == '0')
                draw_mark('0', x - 2, y);
            else if (cell) {
                buf[0] = cell;
                buf[1] = '\0';
                text(x, y, buf, 0);
            }
        }
    }
    if (so->gameState == 24 || so->gameState == 26)
        return;
    /* the takers panel: the kicking side's ten, its first kicker at the top */
    const int32_t ox = m + l->ox, oy = l->oy, hg = l->headGap;
    fill(ox - 13 - hg, oy + 6, ox + 102 - hg, oy + 96, 199);
    const int32_t kicker = so->shooter[turn];
    const int32_t cpu = so->side[turn] == MODSIDE_CPU;
    for (int32_t i = 10, r = 1; i >= 1; i--, r++) {
        char name[20];
        int32_t shirt = 0, face = 0;
        const int32_t y = oy + r * 9 - 1;
        if (!ModApi_ShootoutTaker(turn, i, name, sizeof name, &shirt, &face))
            continue;
        const uint8_t numBox = (uint8_t)(i == kicker ? 240 : 200);
        const uint8_t nameBox = (uint8_t)(i == kicker ? 240 : cpu ? 10 : 28);
        const int32_t bx = l->mode == 2 ? ox - 11 : ox;
        fill(bx - 1, y - 1, bx + 8, y + 6, numBox);
        snprintf(buf, sizeof buf, "%d", (int)shirt);
        text(bx + 5 - text_w(buf, 1) / 2, y, buf, 1);
        const int32_t nx = l->mode == 2 ? ox : ox + 11;
        fill(nx - 1, y - 1, nx + 90, y + 6, nameBox);
        text(nx, y, name, 1);
        const int32_t hx = l->mode == 2 ? ox - 22 : ox - 11, hy = oy + r * 9 - 2;
        static const uint8_t headCols[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
        if (face < 0 || face > 2)
            face = face == 3 ? 2 : 0;   /* (SWOS 2020 has three heads: black with blond hair, its black) */
        for (int32_t j = 0; j < 8; j++) {
            put(hx - 1, hy + j, nameBox);
            put(hx + 8, hy + j, nameBox);
        }
        pixmap(kHeads[face], 8, 8, hx, hy, "123456789ABCDEF", headCols, nameBox);
    }
}

/* The darkened band under the board and the two kits (render.cpp's drawSideDarkenRectangles: at the
 * picture's colours there, the frame's lower half of the palette in its upper half here). */
static void draw_band_and_kits(const Layout *l, int32_t m)
{
    int32_t x0 = 0, x1 = s_w - 1;
    if (l->mode == 2) {
        x0 = m + 48;
        x1 = m + 48 + 384;
    }
    for (int32_t y = l->bandY; y <= l->bandY + l->bandH; y++)
        for (int32_t x = x0; x <= x1; x++)
            if (x >= 0 && x < s_w && y >= 0 && y < s_h && s_fb[y * s_stride + x] < 128)
                s_fb[y * s_stride + x] += 128;
    for (int32_t t = 0; t < 2; t++) {
        int32_t type, shirt, stripes, shorts;
        if (!ModApi_MatchKit(t, &type, &shirt, &stripes, &shorts))
            continue;
        const uint8_t cols[4] = { (uint8_t)shirt, (uint8_t)stripes, (uint8_t)shorts, 19 };
        const char *const *u = kUniforms[type & 3];
        const int32_t kx = m + l->kitX, ky = t ? l->kitY2 : l->kitY1;
        for (int32_t j = 0; j < 15; j++)
            for (int32_t i = 0; i < 15; i++) {
                const char *k = strchr("123+", u[j / 2][i / 2]);
                if (u[j / 2][i / 2] != '.' && k)
                    put(kx + i, ky + j, cols[k - "123+"]);
            }
    }
}

/* The keeper's arrow (showGKIndicator: the ball on the spot, the keeper in the middle of his line;
 * not when both sides are players', which would show the kicker where he goes) and who plays which
 * side (showControllerInfo: the defending side's above, the kicker's below, at the right edge). */
static void draw_keeper(const ModShootout *so)
{
    static const uint8_t cols[] = { 2, 15, 6, 5, 4, 8 };
    int16_t cx, cy;
    ModApi_Camera(&cx, &cy);
    const int32_t gx = 336 - cx, yl = 131 - cy - 15;
    const int32_t both = so->side[0] != MODSIDE_CPU && so->side[1] != MODSIDE_CPU;
    if (so->keeperTurn >= 0 && so->ballOnSpot && !both)
        switch (so->keeperStick) {
        case 2: case 3: pixmap(kArrowRight, 10, 7, gx + 7, yl + 5, "2F6548", cols, -1); break;
        case 6: case 5: pixmap(kArrowLeft, 10, 7, gx - 16, yl + 5, "2F6548", cols, -1); break;
        case 1: pixmap(kArrowRightUp, 9, 9, gx + 6, yl + 1, "2F6548", cols, -1); break;
        case 7: pixmap(kArrowLeftUp, 9, 9, gx - 14, yl + 1, "2F6548", cols, -1); break;
        default: break;
        }
    if (so->ballOnSpot) {
        const int32_t def = 1 - so->kicking;
        for (int32_t k = 0; k < 2; k++) {
            const int32_t t = k ? so->kicking : def;
            const char *name = so->side[t] == MODSIDE_CPU ? "CPU"
                             : ModApi_ControllerName(so->side[t] == MODSIDE_PLAYER2 ? 2 : 1);
            text(s_w - text_w(name, 1) - 2, k ? 165 : 100, name, 1);   /* (the text routine
                                                     * stops short of a glyph that ends at the edge) */
        }
    }
}

static void shootout_drawn(void)
{
    ModShootout so;
    Layout l;
    if (!our_shootout() || ModApi_InReplay() || !ModApi_Shootout(&so))
        return;
    s_fb = ModApi_Frame(&s_stride);
    ModApi_ScreenSize(&s_w, &s_h);
    if (!s_fb)
        return;
    layout(&l);
    s_testFrames++;
    const int32_t m = (s_w - l.base) / 2;
    draw_keeper(&so);
    draw_board(&so, &l, m);
    draw_band_and_kits(&l, m);
}

/* PLAY MATCH: the kick order beside the line-up (PlayMatchOnDraw: 10 by the second man down to 1
 * by the eleventh -- the slots kick 10, 9 .. 1). */
static void shootout_row(const ModPlayerRow *row)
{
    if (row->screen != MODROWS_PLAY_MATCH || !our_shootout() || row->row < 0 || row->row > 10)
        return;
    int32_t y = 0;
    ModApi_MenuEntryRect(row->firstCell, NULL, &y, NULL, NULL);
    s_orderRowY[row->row] = (int16_t)y;
    if (row->row >= s_orderRows)
        s_orderRows = (int8_t)(row->row + 1);
}

static void shootout_menu(void);
static void shootout_menu_drawn(void)
{
    shootout_test_menu();
    shootout_menu();
}

static void shootout_menu(void)
{
    static const uint8_t cols[] = { 15, 8 };
    if (!our_shootout() || !ModApi_IsPlayMatchMenu() || s_orderRows < 11)
        return;
    s_fb = ModApi_Frame(&s_stride);
    ModApi_ScreenSize(&s_w, &s_h);
    if (!s_fb)
        return;
    const int32_t x = ModApi_MenuMargin();
    for (int32_t r = 1; r <= 10; r++)
        pixmap(kSmallNumbers[10 - r], 8, 8, x, s_orderRowY[r], "28", cols, -1);
}
#endif


static void practice_demo(void)
{
    ModMatchSetup setup;
    ModMatchResult r;
    ModApi_DefaultSetup(&setup);
    setup.team[0] = ModApi_RandomTeam();
    do
        setup.team[1] = ModApi_RandomTeam();
    while (setup.team[1] == setup.team[0] && setup.team[0]);
    if (!setup.team[0] || !setup.team[1])
        return;
    setup.replayMenu = 0;
    setup.name = "demo game";
    if (ModApi_PlayMatch("practice", &setup, &r) == 0)
        fprintf(stderr, "[practice] demo game: %d-%d\n", (int)r.goals[0], (int)r.goals[1]);
}

/* ---- team training ---- */

enum { kTrainRows = 8 };
enum { Tr_header, Tr_subTitle, Tr_label0, Tr_note = Tr_label0 + 2 * kTrainRows, Tr_exit };

static uint8_t s_lineOn[2][3] = { { 1, 1, 1 }, { 1, 1, 1 } };    /* [you / the CPU][defence, midfield, attack] */
static uint8_t s_attackUp = 1;                                   /* you attack the top goal first */
static char    s_values[kTrainRows][12];

static void training_refresh(void)
{
    static const char *const kLabels[kTrainRows] = { "YOUR DEFENCE", "YOUR MIDFIELD", "YOUR ATTACK", "CPU DEFENCE",
                                                     "CPU MIDFIELD", "CPU ATTACK", "YOU ATTACK", "" };
    for (int32_t i = 0; i < kTrainRows; i++) {
        const char *v = i < 6 ? (s_lineOn[i / 3][i % 3] ? "ON" : "OFF") : i == 6 ? (s_attackUp ? "UP" : "DOWN") : "START";
        snprintf(s_values[i], sizeof s_values[i], "%s", v);
        ModApi_MenuText(Tr_label0 + 2 * i, kLabels[i]);
        ModApi_MenuText(Tr_label0 + 2 * i + 1, s_values[i]);
        ModApi_MenuVisible(Tr_label0 + 2 * i, i < 7);
        ModApi_MenuVisible(Tr_label0 + 2 * i + 1, 1);
    }
    ModApi_MenuText(Tr_note, "THEN PICK YOUR TEAM AND THE CPU'S");
}

static void training_init(void)
{
    for (int32_t i = 0; i < kTrainRows; i++) {
        const int32_t v = Tr_label0 + 2 * i + 1;
        ModApi_MenuNav(v, i ? v - 2 : -1, i + 1 < kTrainRows ? v + 2 : Tr_exit);
    }
    ModApi_MenuNav(Tr_exit, Tr_label0 + 2 * kTrainRows - 1, -1);
    training_refresh();
}

static void training_draw(void) { training_refresh(); }

static void training_start(void)
{
    ModMatchSetup setup;
    ModMatchResult r;
    if (!ModApi_ChooseTeams())
        return;
    ModApi_DefaultSetup(&setup);
    setup.chosen = 1;
    setup.clockStopped = 1;
    setup.noCards = 1;
    setup.noInjuries = 1;
    setup.autoReplays = 0;
    setup.replayMenu = 0;
    setup.stadium = 0;
    setup.playMatchScreen = 1;          /* (the line-up first: EDIT TACTICS, substitutes -- as SHOOTING
                                           PRACTICE; report 0061) */
    setup.name = "team training";
    /* you: the team the player plays (the team screen's: the first that is not the CPU's) */
    int32_t you = 0;
    {
        extern int32_t ModMatch_ChosenSide(int32_t team);   /* (src/mod_match.c: CPU or not) */
        if (ModMatch_ChosenSide(0) == 0 && ModMatch_ChosenSide(1) != 0)
            you = 1;
    }
    for (int32_t who = 0; who < 2; who++) {
        const int32_t team = who == 0 ? you : 1 - you;
        setup.linesOut[team] = (uint8_t)((s_lineOn[who][0] ? 0 : MODLINE_DEFENCE) |
                                         (s_lineOn[who][1] ? 0 : MODLINE_MIDFIELD) |
                                         (s_lineOn[who][2] ? 0 : MODLINE_ATTACK));
    }
    setup.playingUp = (int8_t)(s_attackUp ? you : 1 - you);
    setup.kickOff = (int8_t)you;
    if (ModApi_PlayMatch("practice", &setup, &r) == 0)
        fprintf(stderr, "[practice] team training: %d-%d in %u frames\n", (int)r.goals[0], (int)r.goals[1],
                (unsigned)r.frames);
}

static void training_row(int32_t i)
{
    if (i < 6)
        s_lineOn[i / 3][i % 3] ^= 1;
    else if (i == 6)
        s_attackUp ^= 1;
    else
        training_start();
}

static void trow0(void) { training_row(0); }
static void trow1(void) { training_row(1); }
static void trow2(void) { training_row(2); }
static void trow3(void) { training_row(3); }
static void trow4(void) { training_row(4); }
static void trow5(void) { training_row(5); }
static void trow6(void) { training_row(6); }
static void trow7(void) { training_row(7); }

static void practice_training(void) { ModApi_ShowMenu("practice/trainingMenu"); }


static void practice_exit(void) { SetExitMenuFlag(); }

static void practice_open(void) { ModApi_ShowMenu("practice/practiceMenu"); }

static void practice_init(void)
{
    static const ModHandler kHandlers[] = {
        { "shootout", practice_shootout },
        { "stInit", st_init }, { "stChoose", st_choose }, { "stAll", st_all }, { "stEurope", st_europe },
        { "stAfrica", st_africa }, { "stNational", st_national }, { "stNAmerica", st_namerica },
        { "stAsia", st_asia }, { "stOriginal", st_original }, { "stSAmerica", st_samerica },
        { "stOceania", st_oceania }, { "stCustom", st_custom }, { "stKeeper", st_keeper }, { "stExit", st_exit },
        { "demo", practice_demo },
        { "training", practice_training },
        { "trainingInit", training_init },
        { "trainingDraw", training_draw },
        { "trow0", trow0 }, { "trow1", trow1 }, { "trow2", trow2 }, { "trow3", trow3 },
        { "trow4", trow4 }, { "trow5", trow5 }, { "trow6", trow6 }, { "trow7", trow7 },
        { "exit", practice_exit },
    };
    ModApi_RegisterHandlers("practice", kHandlers, (int32_t)(sizeof kHandlers / sizeof kHandlers[0]));
    ModApi_AddEntry(MODPOINT_MODES, "practice", "practice", practice_open);
    s_test = getenv("SWOS_PRACTICE_SHOOTOUT_TEST") != NULL;
}

const ModDef ModDef_practice = {
    .slug = "practice",
    .init = practice_init,
#ifdef SWOS_SDL_BUILD
    .matchDrawn = shootout_drawn,
    .menuDrawn = shootout_menu_drawn,
    .playerRowDrawn = shootout_row,
#else
    .menuDrawn = shootout_test_menu,
#endif
};

