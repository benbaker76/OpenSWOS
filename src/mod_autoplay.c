/*
 * mod_autoplay.c -- the autoplay mod (canonical, bin/mods/autoplay; docs/MODS.md §35): SWOS 2020's
 * AUTOPLAY (game.cpp toggleAutoPlayMode / setAutoPlayMode / closeAutoPlayMode, controls.cpp's F1).
 * SDL builds.
 *
 * F1 in a match of a player against the computer hands the player's team to the computer, and F1
 * again gives it back; "AUTO-PLAY" is shown while the computer has it. Only offline, never in a
 * mod's own match (SWOS 2020: not in a speedrun, training, a shoot-out or a scenario -- here those
 * are mods' matches), never online or in a rig (ModApi_OfflineMatch). The team is the player's
 * again at the final whistle.
 *
 * "changes": "modes": a normal match plays as ever until F1 is pressed, and online it cannot be.
 */
#ifdef SWOS_SDL_BUILD
#include "mod_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int8_t  s_team = -1;             /* the player's team (0, 1), -1 none chosen */
static uint8_t s_want, s_on;            /* F1's wish; what is in force */
static int32_t s_test;                  /* SWOS_AUTOPLAY_TEST (below) */

/* A player against the computer: the player's team (0, 1), else -1. */
static int32_t human_team(void)
{
    const int32_t a = ModApi_TeamControl(0) != MODSIDE_CPU, b = ModApi_TeamControl(1) != MODSIDE_CPU;
    return a == b ? -1 : a ? 0 : 1;
}

static int32_t autoplay_key(const char *key)
{
    if (!key || strcmp(key, "F1") != 0 || !ModApi_OfflineMatch() || ModApi_AnyModMatch())
        return 0;
    if (!s_on) {
        const int32_t t = human_team();
        if (t < 0)
            return 0;               /* (SWOS 2020: only a player v the CPU) */
        s_team = (int8_t)t;
    }
    s_want ^= 1;
    return 1;
}

static void autoplay_start(void)
{
    s_want = s_on = 0;
    s_team = -1;
}

/* The wish in force at the next frame played (the match's work belongs to matchFrame). */
static void autoplay_frame(void)
{
    if (s_team < 0 || s_want == s_on)
        return;
    if (ModApi_SetTeamControl(s_team, s_want) == 0)
        s_on = s_want;
}

static void autoplay_ended(const ModMatchResult *r)
{
    (void)r;
    s_want = s_on = 0;              /* (the engine gives every team back: closeAutoPlayMode) */
}

/* showDebugInfo_MPGame's "AUTO-PLAY" at 15, 25. */
static void autoplay_drawn(void)
{
    if (s_on && !ModApi_InReplay())
        ModApi_DrawText(15, 25, 2 /* kWhiteText2 */, "AUTO-PLAY", 0);
}

/* SWOS_AUTOPLAY_TEST=1 (SWOS_SKIP_INTRO=1): at the first main menu, a mod's match of player 1 against
 * the CPU through ModApi_PlayMatch -- which autoplay refuses, so the test drives the engine's own
 * calls on it instead -- then a check that AUTOPLAY is refused there and that the engine handed the
 * team over and back. PASS / FAIL, then quits. */
static void autoplay_test_menu(void)
{
    if (s_test != 1 || !ModApi_IsMainMenu())
        return;
    s_test = 4;
    ModMatchSetup setup;
    ModMatchResult r;
    ModApi_DefaultSetup(&setup);
    setup.team[0] = 0x5020;                 /* Norway */
    setup.team[1] = 0x5502;                 /* New Zealand */
    setup.side[0] = MODSIDE_PLAYER1;
    setup.side[1] = MODSIDE_CPU;
    setup.kickOff = 1;                  /* (the CPU kicks off: nobody is at player 1's controls) */
    setup.stadium = 0;
    setup.replayMenu = 0;
    setup.name = "autoplay test";
    ModApi_PlayMatch("autoplay", &setup, &r);
    const int32_t ok = s_test == 7;
    fprintf(stdout, "AUTOPLAY TEST %s (stage %d, %u frames)\n", ok ? "PASS" : "FAIL", (int)s_test,
            (unsigned)r.frames);
    exit(ok ? 0 : 1);
}

/* In the test's (a mod's) match: F1 is refused, the engine's call is not. */
static void autoplay_test_frame(void)
{
    if (s_test < 4 || !ModApi_ModMatch("autoplay"))
        return;
    static uint32_t f;                  /* (frames this hook saw: the clock may wait at a kick-off) */
    f++;
    if (f == 100 && s_test == 4 && autoplay_key("F1") == 0 && human_team() == 0 &&
        ModApi_SetTeamControl(0, 1) == 0)
        s_test = 5;
    if (f == 400 && s_test == 5 && ModApi_TeamControl(0) == MODSIDE_CPU && ModApi_SetTeamControl(0, 0) == 0)
        s_test = 6;
    if (f == 700 && s_test == 6 && ModApi_TeamControl(0) == MODSIDE_PLAYER1)
        s_test = 7;
    if (f >= 800)
        ModApi_EndMatch();
}

static void autoplay_frame_all(void)
{
    autoplay_test_frame();
    autoplay_frame();
}

static void autoplay_init(void)
{
    s_test = getenv("SWOS_AUTOPLAY_TEST") != NULL;
}

const ModDef ModDef_autoplay = {
    .slug = "autoplay",
    .init = autoplay_init,
    .menuDrawn = autoplay_test_menu,
    .matchStart = autoplay_start,
    .matchFrame = autoplay_frame_all,
    .matchEnded = autoplay_ended,
    .matchDrawn = autoplay_drawn,
    .matchKey = autoplay_key,
};
#endif
