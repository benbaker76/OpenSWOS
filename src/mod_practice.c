/*
 * mod_practice.c -- the practice mod (canonical, bin/mods/practice; docs/MODS.md §18): SWOS 2020's
 * PRACTICE (its practice.mnu, game.cpp ChallengeGame), ported onto the mod API. SDL builds.
 *
 * MODES -> PRACTICE opens its menu (bin/mods/practice/data_json/menus/practice/practiceMenu.json,
 * its handlers practice.<name>):
 *   PENALTY SHOOTOUT  the friendly's team screen (who plays each side), then straight to a shoot-out
 *                     (SWOS 2020: challengeGame2Practice_OnSelect, minute 120 with penalties)
 *   DEMO GAME         two random national teams, CPU v CPU (demoGamePractice_OnSelect)
 * Each match returns here (the menu again), as SWOS 2020's do. TEAM TRAINING (its drills, the
 * frozen clock, the lines left out) comes with the hooks it needs (docs/MODS.md §18).
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

static void practice_exit(void) { SetExitMenuFlag(); }

static void practice_open(void) { ModApi_ShowMenu("practice/practiceMenu"); }

static void practice_init(void)
{
    static const ModHandler kHandlers[] = {
        { "shootout", practice_shootout },
        { "demo", practice_demo },
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
