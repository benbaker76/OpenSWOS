/*
 * mod_pitchselect.c -- the pitch selection mod (canonical, bin/mods/pitchselect; docs/MODS.md §35):
 * SWOS 2020's SELECT PITCH (menu.cpp showPitchSelectMenu / selectPitchFunc, its
 * selectPitchNonWidescreen.mnu; the option PITCH SELECTION, options.cpp enableSelectPitch).
 * SDL builds.
 *
 * PLAY MATCH (a friendly's, a cup's, a career's -- the first player's screen, not VIEW RESULT, not a
 * training match) shows SELECT PITCH first: PITCH 1-5, TRAINING PITCH or RANDOM (one of 1-5), and
 * the match is played on that pitch's art (ModApi_SetMatchPitch). Offline only: online and in the
 * rigs the engine refuses it, and the screen is not shown. SWOS 2020's preview of the pitch beside
 * the list is not drawn. With the mod off, PLAY MATCH starts the match as ever.
 *
 * "changes": "modes": a normal match plays as ever (the pitch's picture and number only; its random
 * numbers are drawn as ever), never online.
 */
#ifdef SWOS_SDL_BUILD
#include "mod_api.h"

#include <stdio.h>
#include <stdlib.h>

extern void SetExitMenuFlag(void);

static uint32_t s_rand;
static int32_t  s_test;                 /* SWOS_PITCHSELECT_TEST=n (below) */

static void pick(int32_t p)             /* selectPitchFunc: 1..6 that pitch, 7 a random one of 1..5 */
{
    if (p < 1 || p > 6) {
        s_rand = s_rand * 1103515245u + 12345u + ModApi_Ticks();
        p = 1 + (int32_t)((s_rand >> 16) % 5u);
    }
    ModApi_SetMatchPitch(p);
    SetExitMenuFlag();
}
static void pick1(void) { pick(1); }
static void pick2(void) { pick(2); }
static void pick3(void) { pick(3); }
static void pick4(void) { pick(4); }
static void pick5(void) { pick(5); }
static void pick6(void) { pick(6); }
static void pick7(void) { pick(7); }
static void draw(void) { }              /* (selectPitchFuncOnDraw: its preview, not drawn here) */

static void play_match(void)            /* showPitchSelectMenu */
{
    ModApi_SetMatchPitch(0);
    ModApi_ShowMenu("pitchselect/selectPitchMenu");
}

/* SWOS_PITCHSELECT_TEST=n (1..6; SWOS_SKIP_INTRO=1): at the first main menu the pitch is set to n
 * as SELECT PITCH would, then a CPU v CPU match is played; PASS when it was played on pitch n. */
static int32_t s_testPitch;
static void test_menu(void)
{
    if (s_test < 1 || !ModApi_IsMainMenu())
        return;
    const int32_t want = s_test;
    s_test = -1;
    ModMatchSetup setup;
    ModMatchResult r;
    ModApi_DefaultSetup(&setup);
    setup.team[0] = 0x5020;
    setup.team[1] = 0x5502;
    setup.stadium = 0;
    setup.replayMenu = 0;
    ModApi_SetMatchPitch(want);
    ModApi_PlayMatch("pitchselect", &setup, &r);
    fprintf(stdout, "PITCHSELECT TEST %s: pitch %d played, %d asked\n", s_testPitch == want ? "PASS" : "FAIL",
            (int)s_testPitch, (int)want);
    exit(s_testPitch == want ? 0 : 1);
}

static void test_frame(void)
{
    if (s_test == -1 && ModApi_ModMatch("pitchselect")) {
        static uint32_t f;
        s_testPitch = ModApi_PitchNumber();
        if (++f >= 100)
            ModApi_EndMatch();
    }
}

static void pitchselect_init(void)
{
    static const ModHandler kHandlers[] = {
        { "pitch1", pick1 }, { "pitch2", pick2 }, { "pitch3", pick3 }, { "pitch4", pick4 },
        { "pitch5", pick5 }, { "pitch6", pick6 }, { "pitch7", pick7 }, { "draw", draw },
    };
    ModApi_RegisterHandlers("pitchselect", kHandlers, (int32_t)(sizeof kHandlers / sizeof kHandlers[0]));
    ModApi_OnPlayMatch("pitchselect", play_match);
    const char *t = getenv("SWOS_PITCHSELECT_TEST");
    s_test = t ? atoi(t) : 0;
    s_rand = ModApi_Ticks();
}

const ModDef ModDef_pitchselect = {
    .slug = "pitchselect",
    .init = pitchselect_init,
    .menuDrawn = test_menu,
    .matchFrame = test_frame,
};
#endif
