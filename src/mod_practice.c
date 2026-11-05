/*
 * mod_practice.c -- the practice mod (canonical, bin/mods/practice; docs/MODS.md §18): SWOS 2020's
 * PRACTICE (its practice.mnu, game.cpp ChallengeGame), ported onto the mod API. SDL builds.
 *
 * MODES -> PRACTICE opens its menu (bin/mods/practice/data_json/menus/practice/practiceMenu.json,
 * its handlers practice.<name>):
 *   PENALTY SHOOTOUT  the friendly's team screen (who plays each side), then straight to a shoot-out
 *                     (SWOS 2020: challengeGame2Practice_OnSelect, minute 120 with penalties)
 *   DEMO GAME         two random national teams, CPU v CPU (demoGamePractice_OnSelect)
 *   TEAM TRAINING     its settings (trainingMenu.json; SWOS 2020's practiceSetting.mnu): each line,
 *                     yours and the CPU's, on or off, the goal you attack; START, the team screen,
 *                     then a match whose clock stays at 0 -- no half time, no end but leaving it
 *                     (ESC) -- with no cards and no injuries (trainingModePractice_OnSelect)
 * Each match returns here (the menu again), as SWOS 2020's do. Training's drills (F1-F12), ghost
 * players and the manual keeper are next (docs/MODS.md §18).
 *
 * "changes": "modes": its matches are its own, a normal one untouched -- never matters online.
 */
#include "mod_api.h"

#ifdef SWOS_SDL_BUILD

#include <stdio.h>

extern void SetExitMenuFlag(void);

static void practice_shootout(void)
{
    ModMatchSetup setup;
    ModMatchResult r;
    if (!ModApi_ChooseTeams())
        return;
    ModApi_DefaultSetup(&setup);
    setup.chosen = 1;
    setup.shootoutOnly = 1;
    setup.autoReplays = 0;
    setup.replayMenu = 0;
    setup.name = "shootout";
    if (ModApi_PlayMatch("practice", &setup, &r) == 0)
        fprintf(stderr, "[practice] shoot-out: %d-%d\n", (int)r.penaltyGoals[0], (int)r.penaltyGoals[1]);
}

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
}

const ModDef ModDef_practice = {
    .slug = "practice",
    .init = practice_init,
};

#endif /* SWOS_SDL_BUILD */
