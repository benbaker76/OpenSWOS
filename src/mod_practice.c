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

static int8_t s_trUp, s_trSkill;        /* (TEAM TRAINING's, below: its pictures draw from them) */
static uint32_t s_trTicks;
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
                f != 68 && f != 70 && f != 74 && f != 86)   /* (74 and 86: none either -- a file asked
                                         *  for that is not there halted the game, panic 0x8008; SWOS
                                         *  2020 skipped them by trying to open them) */
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

/* TEAM TRAINING's pictures (SWOS 2020's PixelmapAndStrings.h, in the mod's pixmaps.txt:
 * scripts/extract_swos2020_pixmaps.py): '1'..'F' a palette index, the rest clear. */
typedef struct { char name[48]; int32_t w, h; const char *rows; } Pixmap;
static Pixmap  s_pix[8];
static int32_t s_pixCount = -1;

static void pixmaps_load(void)
{
    char path[512];
    uint32_t size = 0;
    s_pixCount = 0;
    snprintf(path, sizeof path, "%s/pixmaps.txt", ModApi_ModDir("practice"));
    char *text = (char *)ModApi_ReadFile(path, &size);
    if (!text)
        return;
    char *p = text, *end = text + size;
    while (p < end && s_pixCount < (int32_t)(sizeof s_pix / sizeof s_pix[0])) {
        char *eol = memchr(p, '\n', (size_t)(end - p));
        if (!eol)
            break;
        *eol = '\0';
        if (*p != '#') {
            Pixmap *m = &s_pix[s_pixCount];
            if (sscanf(p, "%47s %d %d", m->name, &m->w, &m->h) != 3 || m->w <= 0 || m->h <= 0)
                break;
            m->rows = eol + 1;
            p = eol + 1 + (size_t)(m->w + 1) * (size_t)m->h;   /* (each row its width and a '\n') */
            if (p > end)
                break;
            s_pixCount++;
            continue;
        }
        p = eol + 1;
    }
}

static const Pixmap *pixmap_named(const char *name)
{
    if (s_pixCount < 0)
        pixmaps_load();
    for (int32_t i = 0; i < s_pixCount; i++)
        if (strcmp(s_pix[i].name, name) == 0)
            return &s_pix[i];
    return NULL;
}

/* drawMapEx(map, x, y, offsetx, offsety, w, h): a piece of a picture at SWOS 2020's screen x, y --
 * its menus 8 to the right of where their entries say (DOS ConvertMenu), so placed here by the
 * menu's header: the entry imported at `importX`, drawn where it is now. */
static int32_t s_menuDx, s_menuDy;

static void menu_origin(int32_t importX, int32_t importY)
{
    int32_t x = importX, y = importY;
    ModApi_MenuEntryRect(0, &x, &y, NULL, NULL);
    s_menuDx = ModApi_MenuMargin() + (x - importX) - 8;
    s_menuDy = y - importY;
}

static void draw_map(const char *name, int32_t sx, int32_t sy, int32_t ox, int32_t oy, int32_t w, int32_t h)
{
    const Pixmap *m = pixmap_named(name);
    if (!m || !(s_fb = ModApi_Frame(&s_stride)))
        return;
    ModApi_ScreenSize(&s_w, &s_h);
    for (int32_t j = 0; j < h && oy + j < m->h; j++) {
        const char *row = m->rows + (size_t)(oy + j) * (size_t)(m->w + 1);
        for (int32_t i = 0; i < w && ox + i < m->w; i++) {
            const char c = row[ox + i];
            const int32_t v = c >= '1' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0;
            if (v)
                put(sx + s_menuDx + i, sy + s_menuDy + j, (uint8_t)v);
        }
    }
}

/* practiceSetting_OnDraw's pictures: the pitch, the goal attacked, the CPU's skill in stars and its
 * keeper, bobbing. */
static void tr_draw_pictures(void)
{
    static const int32_t kFrame[6] = { 0, 1, 2, 3, 2, 1 }, kOffX[4] = { 0, 0, 0, -2 }, kOffY[4] = { 0, 2, 3, 5 };
    menu_origin(89, 0);                     /* (its header, imported at 89, 0) */
    draw_map("fields_default_99x101_1x1_Map", 207, 33, 0, 0, 99, 101);
    const int32_t ax = 250, ay = 76;
    if (!s_trUp)
        draw_map("arrows_default_scaled_Map", ax, ay + 12, 0, 0, 24, 24);
    else
        draw_map("arrows_default_scaled_2_Map", ax, ay - 16, 0, 0, 24, 24);
    if (s_trSkill <= 4)
        draw_map("starsMap", ax + 3 - 3 * s_trSkill, s_trUp ? 33 : 126, 0, 10 * (s_trSkill * 2 + 1), 36, 10);
    const int32_t f = kFrame[(s_trTicks % 120u) / 20u];
    draw_map("goalkeeper_gloved_scaled_32x44_4x1_Map", 157 - 6 - kOffX[f], 159 - kOffY[f], f * 32, 0, 32, 44);
}

/* teamTrainingControlsMenu_OnDraw: the keys. */
static void tr_draw_controls(void)
{
    menu_origin(92, 0);                     /* (its header, imported at 92, 0) */
    draw_map("team_training_controls_230x68_1x1_map", 55, 18, 0, 0, 230, 68);
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
static void training_test_menu(void);
static void shootout_menu_drawn(void)
{
    shootout_test_menu();
    training_test_menu();
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


/* ---- DEMO GAME (SWOS 2020's demoGamePractice_OnSelect: teamSelect 0, CPU v CPU) ----
 * Two random teams of every file, the first kicking off, from the 89th minute: a minute to watch. */
static void practice_demo(void)
{
    static TeamFileEx teams[2];
    ModMatchSetup setup;
    ModMatchResult r;
    if (!random_pair(GRP_ALL, teams))
        return;
    ModApi_DefaultSetup(&setup);
    setup.teamData[0] = &teams[0];
    setup.teamData[1] = &teams[1];
    setup.side[0] = setup.side[1] = MODSIDE_CPU;
    setup.minute = 89;                      /* (cGame.time 0, 8, 9) */
    setup.kickOff = 0;                      /* (InitGame_202: teamStarting 1) */
    setup.replayMenu = 0;
    setup.name = "demo game";
    if (ModApi_PlayMatch("practice", &setup, &r) == 0)
        fprintf(stderr, "[practice] demo game: %d-%d\n", (int)r.goals[0], (int)r.goals[1]);
}

/* The menu's help line (practiceMenuOnDraw), for the entry the cursor is on. */
enum { Pm_header, Pm_friendly, Pm_training, Pm_shootout, Pm_demo, Pm_help, Pm_exit };
static void practice_menu_draw(void)
{
    const int32_t e = ModApi_MenuCurrentEntry();
    ModApi_MenuText(Pm_help, e == Pm_friendly ? "FRIENDLY GAME WITH FREELY SELECTABLE TEAMS"
                           : e == Pm_training ? "PRACTICE WITH NO OPPONENT PLAYERS EXCEPT FOR GOALKEEPER"
                           : e == Pm_shootout ? "PENALTY SHOOTOUT WITH FREELY SELECTABLE TEAMS"
                           : e == Pm_demo ? "SIT BACK, RELAX AND WATCH GAMEPLAY OF TWO RANDOM CPU TEAMS"
                                          : "BACK TO MAIN MENU");
}

/* ---- TEAM TRAINING (SWOS 2020's trainingModePractice_OnSelect, ChallengeGame gameMode 3) ----
 * Its settings (practiceSetting.mnu): the goal attacked, the CPU's skill (which of the original
 * custom teams it is: JUST NIPPING OUT .. PROFESSIONALS, or one at random), the lines of each side
 * on or off, the CPU's players unseen, tackles, the pitch; CHOOSE TEAM (one team, titled TEAM
 * TRAINING) or RANDOM TEAM. Then a match whose clock stays at 0, no cards and no injuries, the
 * player's team kicking off, its drills on the keys (TEAM TRAINING CONTROLS); after it, the
 * settings again. Kept for the session, as SWOS 2020 keeps its globals. */
enum { Ps_header, Ps_upCaption, Ps_skillCaption, Ps_defCaption, Ps_midCaption, Ps_attCaption, Ps_ghostCaption,
       Ps_tackleCaption, Ps_pitchCaption, Ps_help, Ps_up, Ps_skill, Ps_playerCaption, Ps_def0, Ps_mid0, Ps_att0,
       Ps_cpuCaption, Ps_def, Ps_mid, Ps_att, Ps_ghost, Ps_tackle, Ps_pitch, Ps_helpCustom, Ps_msg, Ps_choose,
       Ps_random, Ps_exit };
static int8_t s_trUp;                   /* g_TeamPlayingUp: 0 DOWN, 1 UP */
static int8_t s_trSkill;                /* g_GoalkeeperSkill: 0 BEGINNER .. 4 MASTER, 5 RANDOM */
static int8_t s_trLine[2][3] = { { 1, 1, 1 }, { 0, 0, 0 } };    /* [player / CPU][defence, midfield, attack] */
static int8_t s_trGhost;                /* g_GhostPlayers */
static int8_t s_trTackle = 1;           /* g_TrainingModeTackleAllowed */
static int8_t s_trPitch = -1;           /* g_PitchTypeCustom: -1 random, 0 frozen .. 6 hard */
static int8_t s_trChoice;               /* g_PracticeSetting: -1 left, 0 CHOOSE TEAM, 1 RANDOM TEAM */

static void tr_up(void) { s_trUp ^= 1; }
static void tr_skill(void) { if (++s_trSkill > 5) s_trSkill = 0; }
static void tr_def0(void) { s_trLine[0][0] ^= 1; }
static void tr_mid0(void) { s_trLine[0][1] ^= 1; }
static void tr_att0(void) { s_trLine[0][2] ^= 1; }
static void tr_def(void) { s_trLine[1][0] ^= 1; }
static void tr_mid(void) { s_trLine[1][1] ^= 1; }
static void tr_att(void) { s_trLine[1][2] ^= 1; }
static void tr_ghost(void) { s_trGhost ^= 1; }
static void tr_tackle(void) { s_trTackle ^= 1; }
static void tr_pitch(void) { if (++s_trPitch > 6) s_trPitch = -1; }
static void tr_controls(void) { ModApi_ShowMenu("practice/teamTrainingControlsMenu"); }
static int32_t tr_player_lines(void) { return s_trLine[0][0] | s_trLine[0][1] | s_trLine[0][2]; }
static void tr_choose(void) { if (tr_player_lines()) { s_trChoice = 0; SetExitMenuFlag(); } }
static void tr_random(void) { if (tr_player_lines()) { s_trChoice = 1; SetExitMenuFlag(); } }
static void tr_exit(void) { s_trChoice = -1; SetExitMenuFlag(); }
static void tr_init(void) { }
static void tr_draw(void);
static void tr_controls_draw(void);
static void tr_controls_init(void) { }

/* practiceSetting_OnDraw: the values, and what can be reached -- CPU GHOSTED only with a CPU line
 * on, the team buttons only with a line of the player's on. */
static void tr_refresh(void)
{
    static const char *const kSkill[6] = { "BEGINNER", "AMATEUR", "ADVANCED", "PRO", "MASTER", "RANDOM" };
    static const char *const kPitch[8] = { "RANDOM", "FROZEN", "MUDDY", "WET", "SOFT", "NORMAL", "DRY", "HARD" };
    ModApi_MenuText(Ps_up, s_trUp ? "UP" : "DOWN");
    ModApi_MenuText(Ps_pitch, kPitch[s_trPitch + 1]);
    ModApi_MenuText(Ps_skill, kSkill[s_trSkill]);
    ModApi_MenuText(Ps_def, s_trLine[1][0] ? "ON" : "OFF");
    ModApi_MenuText(Ps_mid, s_trLine[1][1] ? "ON" : "OFF");
    ModApi_MenuText(Ps_att, s_trLine[1][2] ? "ON" : "OFF");
    ModApi_MenuText(Ps_def0, s_trLine[0][0] ? "ON" : "OFF");
    ModApi_MenuText(Ps_mid0, s_trLine[0][1] ? "ON" : "OFF");
    ModApi_MenuText(Ps_att0, s_trLine[0][2] ? "ON" : "OFF");
    ModApi_MenuText(Ps_ghost, s_trGhost ? "ON" : "OFF");
    ModApi_MenuText(Ps_tackle, s_trTackle ? "ON" : "OFF");
    if (s_trLine[1][0] | s_trLine[1][1] | s_trLine[1][2]) {
        ModApi_MenuNav4(Ps_att, Ps_att0, -1, Ps_mid, Ps_ghost);
        ModApi_MenuNav4(Ps_att0, -1, Ps_att, Ps_mid0, Ps_ghost);
        ModApi_MenuNav4(Ps_tackle, -1, -1, Ps_ghost, Ps_pitch);
        ModApi_MenuColour(Ps_ghost, 13);
        ModApi_MenuDisabled(Ps_ghost, 0);
    } else {
        ModApi_MenuNav4(Ps_att, Ps_att0, -1, Ps_mid, Ps_tackle);
        ModApi_MenuNav4(Ps_att0, -1, Ps_att, Ps_mid0, Ps_tackle);
        ModApi_MenuNav4(Ps_tackle, -1, -1, Ps_att, Ps_pitch);
        ModApi_MenuColour(Ps_ghost, 7);
        ModApi_MenuDisabled(Ps_ghost, 1);
    }
    const int32_t teams = tr_player_lines();
    ModApi_MenuColour(Ps_choose, teams ? 14 : 7);
    ModApi_MenuColour(Ps_random, teams ? 14 : 7);
    ModApi_MenuDisabled(Ps_choose, !teams);
    ModApi_MenuDisabled(Ps_random, !teams);
    ModApi_MenuNav4(Ps_helpCustom, -1, -1, Ps_pitch, teams ? Ps_choose : Ps_exit);
    ModApi_MenuNav4(Ps_exit, Ps_choose, Ps_random, teams ? Ps_choose : Ps_helpCustom, -1);
}

static uint32_t s_trTicks;              /* (the menu's own animation clock) */
static void tr_draw(void)
{
    tr_refresh();
    s_trTicks++;
#ifdef SWOS_SDL_BUILD
    tr_draw_pictures();
#endif
}

static void tr_controls_draw(void)
{
#ifdef SWOS_SDL_BUILD
    tr_draw_controls();
#endif
}

/* TEAM TRAINING's drills (TEAM TRAINING CONTROLS; SWOS 2020's controls.cpp, the training keys):
 * the player's team's free kick at one of seven spots before the goal it attacks (F1-F7: x 148
 * .. 548, 152 from the line -- FREE_KICK_UP / _DOWN) or where its controlled player is (ALT), its
 * corner from the left or the right (F8, F9), a kick (F10) or a high kick (F11) of the ball at
 * its player's feet, the kick-off again (F12), a goal kick at its own goal (9 left, 0 right) or
 * the CPU's at the other (- left, = right). Taken at the next frame. */
enum { DR_NONE, DR_FREE1, DR_FREE7 = DR_FREE1 + 6, DR_FREE_HERE, DR_CORNER_LEFT, DR_CORNER_RIGHT, DR_KICK,
       DR_HIGH_KICK, DR_KICK_OFF, DR_GOAL_KICK_HOME_LEFT, DR_GOAL_KICK_HOME_RIGHT, DR_GOAL_KICK_OPPO_LEFT,
       DR_GOAL_KICK_OPPO_RIGHT };
static int8_t s_drill;
static uint8_t s_training;              /* our training is the match */

static int32_t training_key(const char *key)
{
    static const char *const kKeys[] = { "F1", "F2", "F3", "F4", "F5", "F6", "F7", "Left Alt", "F8", "F9", "F10",
                                         "F11", "F12", "9", "0", "-", "=" };
    if (!s_training || !ModApi_ModMatch("practice") || !key)
        return 0;
    for (int32_t i = 0; i < (int32_t)(sizeof kKeys / sizeof kKeys[0]); i++)
        if (strcmp(key, kKeys[i]) == 0) {
            s_drill = (int8_t)(DR_FREE1 + i);
            return 1;
        }
    return 0;
}

/* The match's end the player's team (team 0) attacks: 1 the top goal. */
static int32_t tr_attacks_top(void) { return s_trUp; }

static int32_t s_trTest, s_trTestOk, s_trTestDone;    /* SWOS_PRACTICE_TRAINING_TEST (below) */
static int32_t drill_apply(int8_t d);

static void training_frame(void)
{
    if (!s_training || !ModApi_ModMatch("practice"))
        return;
    if (s_trTest == 2) {                /* (the test: every drill in turn, 400 frames apart; one the
                                         *  game refuses -- the ball dead -- tried each frame after) */
        static int32_t next = DR_FREE1, tries;
        static uint32_t due = 400;
        const uint32_t f = ModApi_MatchFrames();
        if (next <= DR_GOAL_KICK_OPPO_RIGHT && f >= due) {
            if (drill_apply((int8_t)next) == 0) {
                fprintf(stdout, "TRAINING TEST: drill %d taken at frame %u%s\n", (int)next, (unsigned)f,
                        tries ? " (after the ball came alive)" : "");
                s_trTestOk++;
                next++;
                tries = 0;
                due = f + 400;
            } else if (++tries > 600) {
                fprintf(stdout, "TRAINING TEST: drill %d refused for 600 frames\n", (int)next);
                next++;
                tries = 0;
                due = f + 400;
            }
            s_trTestDone = next - DR_FREE1;
        }
        if (next > DR_GOAL_KICK_OPPO_RIGHT && f >= due)
            ModApi_EndMatch();
        return;
    }
    if (s_drill == DR_NONE)
        return;
    const int8_t d = s_drill;
    s_drill = DR_NONE;
    drill_apply(d);
}

/* A drill now: 0 when the game took it (a kick is always taken). */
static int32_t drill_apply(int8_t d)
{
    static const int16_t kFreeX[7] = { 148, 256, 304, 357, 405, 513, 548 };
    const int32_t top = tr_attacks_top();
    /* a wing as its taker attacks: his left is the screen's left when he attacks the top goal */
    const int32_t leftWingAttacking = top ? 0 : 1, leftWingDefending = top ? 1 : 0;
    if (d >= DR_FREE1 && d <= DR_FREE7) {
        return ModApi_Restart(MODRESTART_FREE_KICK, 0, 0, kFreeX[d - DR_FREE1], (int16_t)(top ? 281 : 602));
    } else if (d == DR_FREE_HERE) {
        int16_t x, y;
        return ModApi_ControlledPlayer(0, &x, &y) ? ModApi_Restart(MODRESTART_FREE_KICK, 0, 0, x, y) : -1;
    } else if (d == DR_CORNER_LEFT || d == DR_CORNER_RIGHT) {
        const int32_t screenLeft = d == DR_CORNER_LEFT;
        return ModApi_Restart(MODRESTART_CORNER, 0, screenLeft ? leftWingAttacking : 1 - leftWingAttacking, 0, 0);
    } else if (d == DR_KICK || d == DR_HIGH_KICK) {
        ModApi_PressFire(1, d == DR_KICK ? 2 : 20);
        return 0;
    } else if (d == DR_KICK_OFF) {
        return ModApi_Restart(MODRESTART_KICK_OFF, 0, 0, 0, 0);
    } else {
        const int32_t home = d == DR_GOAL_KICK_HOME_LEFT || d == DR_GOAL_KICK_HOME_RIGHT;
        const int32_t screenLeft = d == DR_GOAL_KICK_HOME_LEFT || d == DR_GOAL_KICK_OPPO_LEFT;
        /* the player's goal kick at its own goal (a taker's left as he attacks: the player's
         * team's as it attacks, the CPU's the other way), the CPU's at the goal the player attacks */
        const int32_t left = home ? leftWingAttacking : leftWingDefending;
        return ModApi_Restart(MODRESTART_GOAL_KICK, home ? 0 : 1, screenLeft ? left : 1 - left, 0, 0);
    }
}

#ifdef SWOS_SDL_BUILD
/* RADAR IN TEAM TRAINING (SWOS 2020's showRadar, render.cpp; its option options.mnu, on by default:
 * here the mod's setting "radar", OPTIONS -> MODS -> PRACTICE): the pitch a tenth of its size at the
 * top left, 10 in and 20 down, on a darkened box -- its lines in colour 249, each player a pixel in
 * his shirt's colour (team 2's shorts' when the shirts are the same), the ball in colour 3. */
static int32_t s_radarDrawn;            /* (the training test counts them) */
static void radar_drawn(void)
{
    enum { F = 10, GX = 10, GY = 20 };
    ModPlayer pl[22];
    ModBall ball;
    int32_t c1 = 0, c2 = 0, shorts2 = 0;
    if (!s_training || !ModApi_ModMatch("practice") || ModApi_InReplay() ||
        !ModApi_SettingInt("practice", "radar", 1) || !(s_fb = ModApi_Frame(&s_stride)))
        return;
    ModApi_ScreenSize(&s_w, &s_h);
    ModApi_MatchKit(0, NULL, &c1, NULL, NULL);
    ModApi_MatchKit(1, NULL, &c2, NULL, &shorts2);
    if (c1 == c2)
        c2 = shorts2;
    /* drawDarkenRect (inclusive): 672 x 898 world */
    for (int32_t y = GY; y <= GY + 898 / F; y++)
        for (int32_t x = GX; x <= GX + 672 / F; x++)
            if (x < s_w && y < s_h && s_fb[y * s_stride + x] < 128)
                s_fb[y * s_stride + x] += 128;
    /* the lines: the touchlines' box, the half way line, the penalty areas */
    const int32_t bx = 81 / F + GX, by = 129 / F + GY, bw = 510 / F, bh = 641 / F;
    fill(bx, by, bx + bw - 1, by, 249);
    fill(bx, by + bh - 1, bx + bw - 1, by + bh - 1, 249);
    fill(bx, by, bx, by + bh - 1, 249);
    fill(bx + bw - 1, by, bx + bw - 1, by + bh - 1, 249);
    fill(bx, 449 / F + GY, bx + bw - 1, 449 / F + GY, 249);
    static const int16_t kArea[2] = { 216, 682 };
    for (int32_t a = 0; a < 2; a++) {
        const int32_t ly = kArea[a] / F + GY, top = (a ? 682 : 129) / F + GY;
        fill(193 / F + GX, ly, 193 / F + GX + 286 / F - 1, ly, 249);
        fill(193 / F + GX, top, 193 / F + GX, top + 88 / F - 1, 249);
        fill(478 / F + GX, top, 478 / F + GX, top + 88 / F - 1, 249);
    }
    const int32_t n = ModApi_Players(pl, 22);
    for (int32_t i = 0; i < n; i++)
        if (pl[i].onPitch)
            put(pl[i].x / F + GX, pl[i].y / F + GY, (uint8_t)(pl[i].team ? c2 : c1));
    if (ModApi_Ball(&ball) && ball.x <= 672)
        put(ball.x / F + GX, ball.y / F + GY, 3);
    s_radarDrawn++;
}
#endif

#ifdef SWOS_SDL_BUILD
static void practice_match_drawn(void)
{
    shootout_drawn();
    radar_drawn();
}
#endif

/* SWOS_PRACTICE_TRAINING_TEST=1 (SWOS_SKIP_INTRO=1): at the first main menu, TEAM TRAINING with
 * a random team, the CPU's at its default skill, every line on, both sides the CPU's; every
 * drill in turn (each 400 frames apart), then the end. PASS when every drill was taken, the clock
 * never moved, and the match ended, then quits. */
static void practice_training(void);
static void training_test_menu(void)
{
    if (s_trTest != 1 || !ModApi_IsMainMenu())
        return;
    s_trTest = 2;
    s_trLine[1][0] = s_trLine[1][1] = s_trLine[1][2] = 1;
    s_trChoice = 1;
    practice_training();
}

static void practice_training(void)
{
    static TeamFileEx teams[2];
    static const uint8_t kSkillTeam[5] = { 21, 40, 43, 42, 30 };   /* TEAM.072: global 1548, 1567, 1570,
                                                                    * 1569, 1557 */
    for (;;) {
        ModMatchSetup setup;
        ModMatchResult r;
        if (s_trTest != 2) {
            s_trChoice = -1;
            ModApi_ShowMenu("practice/practiceSettingMenu");
        }
        if (s_trChoice < 0)
            return;
        if (s_trChoice == 0) {
            if (!ModApi_ChooseOneTeam("TEAM TRAINING", "START TEAM TRAINING", &teams[0]))
                continue;               /* (back to the settings) */
        } else if (!random_pair(GRP_ALL, teams)) {
            continue;
        }
        ModApi_DefaultSetup(&setup);
        setup.teamData[0] = &teams[0];
        const int32_t skill = s_trSkill == 5 ? (int32_t)st_rand(5) : s_trSkill;
        setup.team[1] = (uint16_t)(72 << 8 | kSkillTeam[skill]);
        setup.side[0] = s_trTest ? MODSIDE_CPU : MODSIDE_PLAYER1;
        setup.side[1] = MODSIDE_CPU;
        setup.clockStopped = 1;
        setup.noCards = 1;
        setup.noInjuries = 1;
        setup.noTackles = (uint8_t)!s_trTackle;
        setup.pitchType = s_trPitch;
        setup.stadium = 0;                  /* (g_SkipStadiumMenu) */
        setup.replayMenu = 0;
        setup.playMatchScreen = s_trTest ? 0 : 1;     /* (the test has no player to leave it) */
        setup.hideHud = MODHUD_RESULT;
        setup.name = "team training";
        for (int32_t t = 0; t < 2; t++)
            setup.linesOut[t] = (uint8_t)((s_trLine[t][0] ? 0 : MODLINE_DEFENCE) | (s_trLine[t][1] ? 0 : MODLINE_MIDFIELD) |
                                          (s_trLine[t][2] ? 0 : MODLINE_ATTACK));
        if (s_trGhost)
            setup.ghosts[1] = 0x7FE;        /* (the CPU's outfielders: its keeper is always seen) */
        setup.playingUp = (int8_t)(s_trUp ? 0 : 1);   /* (DOWN: teamPlayingUp 1, team 1 attacks down) */
        setup.kickOff = 0;
        s_drill = DR_NONE;
        s_training = 1;
        if (ModApi_PlayMatch("practice", &setup, &r) == 0)
            fprintf(stderr, "[practice] team training: %d-%d in %u frames\n", (int)r.goals[0], (int)r.goals[1],
                    (unsigned)r.frames);
        s_training = 0;
        if (s_trTest == 2) {
            const int32_t want = DR_GOAL_KICK_OPPO_RIGHT;
#ifdef SWOS_SDL_BUILD
            const int32_t radarOk = !ModApi_SettingInt("practice", "radar", 1) || s_radarDrawn > 0;
#else
            const int32_t radarOk = 1;
#endif
            const int32_t ok = s_trTestDone == want && s_trTestOk == want && r.minute == 0 && radarOk;
            fprintf(stdout, "PRACTICE TRAINING TEST %s: %d of %d drills taken, minute %d, %u frames%s\n",
                    ok ? "PASS" : "FAIL", (int)s_trTestOk, (int)want, (int)r.minute, (unsigned)r.frames,
                    radarOk ? "" : ", no radar drawn");
#ifdef SWOS_SDL_BUILD
            exit(ok ? 0 : 1);
#endif
            return;
        }
    }
}


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
        { "menuDraw", practice_menu_draw },
        { "training", practice_training },
        { "practiceSetting_OnInit", tr_init }, { "practiceSetting_OnDraw", tr_draw },
        { "teamPlayingUp_OnSelect", tr_up }, { "goalkeeperSkill_OnSelect", tr_skill },
        { "defense0_OnSelect", tr_def0 }, { "midfield0_OnSelect", tr_mid0 }, { "attack0_OnSelect", tr_att0 },
        { "defense_OnSelect", tr_def }, { "midfield_OnSelect", tr_mid }, { "attack_OnSelect", tr_att },
        { "ghostPlayers_OnSelect", tr_ghost }, { "tackleAllowed_OnSelect", tr_tackle },
        { "pitchTypeCustom_OnSelect", tr_pitch }, { "helpCustom_OnSelect", tr_controls },
        { "chooseTeam_OnSelect", tr_choose }, { "randomTeam_OnSelect", tr_random },
        { "exitPracticeSetting_OnSelect", tr_exit },
        { "teamTrainingControlsMenu_OnInit", tr_controls_init },
        { "teamTrainingControlsMenu_OnDraw", tr_controls_draw },
        { "exit", practice_exit },
    };
    ModApi_RegisterHandlers("practice", kHandlers, (int32_t)(sizeof kHandlers / sizeof kHandlers[0]));
    ModApi_AddEntry(MODPOINT_MODES, "practice", "practice", practice_open);
    s_test = getenv("SWOS_PRACTICE_SHOOTOUT_TEST") != NULL;
    s_trTest = getenv("SWOS_PRACTICE_TRAINING_TEST") != NULL;
}

const ModDef ModDef_practice = {
    .slug = "practice",
    .init = practice_init,
    .matchFrame = training_frame,
#ifdef SWOS_SDL_BUILD
    .matchDrawn = practice_match_drawn,
    .menuDrawn = shootout_menu_drawn,
    .playerRowDrawn = shootout_row,
    .matchKey = training_key,
#else
    .menuDrawn = shootout_test_menu,   /* (the tests are the SDL build's) */
#endif
};

