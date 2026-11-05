/*
 * mod_practice.c -- the practice mod (canonical, bin/mods/practice; docs/MODS.md §18): SWOS 2020's
 * PRACTICE (its practice.mnu, game.cpp ChallengeGame), ported onto the mod API. Both targets.
 *
 * MODES -> PRACTICE opens its menu (bin/mods/practice/data_json/menus/practice/practiceMenu.json,
 * its handlers practice.<name>):
 *   PENALTY SHOOTOUT  the friendly's team screen (who plays each side), each player's side's SHOOTING
 *                     ORDER (shootoutOrderMenu.json; petarku's), then straight to a shoot-out with a
 *                     player's keeper his to dive (SWOS 2020: challengeGame2Practice_OnSelect, minute
 *                     120 with penalties)
 *   DEMO GAME         two random national teams, CPU v CPU (demoGamePractice_OnSelect)
 *   TEAM TRAINING     its settings (trainingMenu.json; SWOS 2020's practiceSetting.mnu): each line,
 *                     yours and the CPU's, on or off, the goal you attack; START, the team screen,
 *                     then a match whose clock stays at 0 -- no half time, no end but leaving it
 *                     (ESC) -- with no cards and no injuries (trainingModePractice_OnSelect)
 *   SHOOTING PRACTICE petarku's (shootingMenu.json): you against the CPU's goalkeeper (its
 *                     defenders and midfield on or off), no clock, every kick-off yours
 * Each match returns here (the menu again), as SWOS 2020's do. Training's drills (F1-F12), ghost
 * players and the manual keeper are next (docs/MODS.md §18).
 *
 * "changes": "modes": its matches are its own, a normal one untouched -- never matters online.
 */
#include "mod_api.h"


#include <stdio.h>
#include <stdlib.h>

extern void SetExitMenuFlag(void);

/* ---- the shoot-out's SHOOTING ORDER (petarku's, the5ar: his fork's penalty_shootout.c) ----
 * For each side a player plays, its ten outfielders as the queue (shootoutOrderMenu.json): 1-5 the
 * best-of-five takers, 6-10 after them. Fire on one, then on another, and the two swap. It starts
 * as the game's own order (NextPenalty walks line-up slots 10, 9 .. 1), so START SHOOTOUT at once
 * changes nothing. */
enum { Or_header, Or_subTitle, Or_row0, Or_note = Or_row0 + 10, Or_start, Or_exit };
enum { kRowColour = 13, kPickedColour = 14 };

static uint8_t s_order[10];             /* the side on the screen: line-up slots, shooting order */
static int32_t s_orderTeam;             /* 0 / 1: that side */
static int32_t s_picked = -1;           /* the row picked first, -1 none */
static int32_t s_orderGo;               /* START SHOOTOUT pressed */
static char    s_orderText[10][40];
static char    s_orderTitle[40];

static void order_refresh(void)
{
    static const char *const kPos[8] = { "GK", "RB", "LB", "D", "RW", "LW", "M", "A" };
    ModTeamPlayer p;

    snprintf(s_orderTitle, sizeof s_orderTitle, "%s", ModApi_ChosenName(s_orderTeam));
    ModApi_MenuText(Or_subTitle, s_orderTitle);
    for (int32_t i = 0; i < 10; i++) {
        if (ModApi_ChosenTeamPlayer(s_orderTeam, s_order[i], &p))
            snprintf(s_orderText[i], sizeof s_orderText[i], "%2d  %-18.18s %s", (int)i + 1, p.name,
                     kPos[p.position & 7]);
        else
            snprintf(s_orderText[i], sizeof s_orderText[i], "%2d", (int)i + 1);
        ModApi_MenuText(Or_row0 + i, s_orderText[i]);
        ModApi_MenuColour(Or_row0 + i, i == s_picked ? kPickedColour : kRowColour);
    }
    ModApi_MenuText(Or_note, s_picked < 0 ? "PICK A PLAYER, THEN ANOTHER: THEY SWAP" : "NOW THE ONE TO SWAP WITH");
}

static void order_init(void)
{
    for (int32_t i = 0; i < 10; i++)
        ModApi_MenuNav4(Or_row0 + i, -1, -1, i ? Or_row0 + i - 1 : -1, i < 9 ? Or_row0 + i + 1 : Or_start);
    ModApi_MenuNav4(Or_start, -1, Or_exit, Or_row0 + 9, -1);
    ModApi_MenuNav4(Or_exit, Or_start, -1, Or_row0 + 9, -1);
    order_refresh();
}

static void order_draw(void) { order_refresh(); }

static void order_row(int32_t i)
{
    if (getenv("SWOS_MOD_EVENT_LOG"))
        fprintf(stderr, "[practice] row %d fire at %u (picked %d)\n", (int)i + 1, (unsigned)ModApi_Ticks(),
                (int)s_picked + 1);
    if (s_picked < 0) {
        s_picked = i;
    } else {
        uint8_t t = s_order[i];
        s_order[i] = s_order[s_picked];
        s_order[s_picked] = t;
        fprintf(stderr, "[practice] shooting order: %d <-> %d\n", (int)s_picked + 1, (int)i + 1);
        s_picked = -1;
    }
    order_refresh();
    for (int32_t r = 0; r < 10; r++)
        ModApi_MenuRedraw(Or_row0 + r);
    ModApi_MenuRedraw(Or_note);
}

static void orow0(void) { order_row(0); }
static void orow1(void) { order_row(1); }
static void orow2(void) { order_row(2); }
static void orow3(void) { order_row(3); }
static void orow4(void) { order_row(4); }
static void orow5(void) { order_row(5); }
static void orow6(void) { order_row(6); }
static void orow7(void) { order_row(7); }
static void orow8(void) { order_row(8); }
static void orow9(void) { order_row(9); }
static void order_start(void) { s_orderGo = 1; SetExitMenuFlag(); }
static void order_exit(void)  { s_orderGo = 0; SetExitMenuFlag(); }

/* The order of each side a player plays, chosen on the screen; a CPU side shoots as the game
 * picks. 0 when the player left. */
static int32_t choose_orders(ModMatchSetup *setup)
{
    for (int32_t t = 0; t < 2; t++) {
        if (!ModApi_ChosenIsPlayer(t))
            continue;
        for (int32_t i = 0; i < 10; i++)
            s_order[i] = (uint8_t)(10 - i);
        s_orderTeam = t;
        s_picked = -1;
        s_orderGo = 0;
        ModApi_ShowMenu("practice/shootoutOrderMenu");
        if (!s_orderGo)
            return 0;
        for (int32_t i = 0; i < 10; i++)
            setup->shootoutOrder[t][i] = s_order[i];
    }
    return 1;
}

static void practice_shootout(void)
{
    ModMatchSetup setup;
    ModMatchResult r;
    if (!ModApi_ChooseTeams())
        return;
    ModApi_DefaultSetup(&setup);
    if (!choose_orders(&setup))
        return;
    setup.chosen = 1;
    setup.shootoutOnly = 1;
    setup.manualKeeper = 1;                 /* a player's keeper dives where he holds (petarku's) */
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

/* ---- shooting practice (petarku's, the5ar: his fork's shooting_practice.c) ----
 * You against a CPU side stripped down to its goalkeeper, to practise finishing into an open
 * defence. The settings (shootingMenu.json) keep the CPU's DEFENDERS (RB, LB, D) and/or MIDFIELD
 * (M, LW, RW) -- its attackers always go -- and say which goal you attack; then the team screen
 * (exactly one side the CPU's), the PLAY MATCH screen (EDIT TACTICS, VIEW OPPO, substitutes) and
 * a match with no clock in which you take every kick-off (the other side may have nobody to send
 * to the centre circle). TEAM TRAINING with the CPU's lines out, plus kickOffAlways, hideClock
 * and playMatchScreen. SWOS_SHOOTING_TEST=1 plays it CPU v CPU, headless (below). */
enum { kShootRows = 4 };
enum { Sh_header, Sh_subTitle, Sh_label0, Sh_note = Sh_label0 + 2 * kShootRows, Sh_exit };

static uint8_t s_keepDefenders;         /* the CPU keeps its back line */
static uint8_t s_keepMidfield;          /* ... and/or its midfield */
static uint8_t s_shootUp = 1;           /* you attack the top goal */
static char    s_shootValues[kShootRows][12];

static void shooting_refresh(void)
{
    static const char *const kLabels[kShootRows] = { "CPU DEFENDERS", "CPU MIDFIELD", "YOU ATTACK", "" };
    for (int32_t i = 0; i < kShootRows; i++) {
        const char *v = i == 0 ? (s_keepDefenders ? "ON" : "OFF")
                      : i == 1 ? (s_keepMidfield ? "ON" : "OFF")
                      : i == 2 ? (s_shootUp ? "UP" : "DOWN") : "START";
        snprintf(s_shootValues[i], sizeof s_shootValues[i], "%s", v);
        ModApi_MenuText(Sh_label0 + 2 * i, kLabels[i]);
        ModApi_MenuText(Sh_label0 + 2 * i + 1, s_shootValues[i]);
        ModApi_MenuVisible(Sh_label0 + 2 * i, i < kShootRows - 1);
        ModApi_MenuVisible(Sh_label0 + 2 * i + 1, 1);
    }
    ModApi_MenuText(Sh_note, "THEN PICK YOUR TEAM AND THE CPU'S");
}

static void shooting_init(void)
{
    for (int32_t i = 0; i < kShootRows; i++) {
        const int32_t v = Sh_label0 + 2 * i + 1;
        ModApi_MenuNav(v, i ? v - 2 : -1, i + 1 < kShootRows ? v + 2 : Sh_exit);
    }
    ModApi_MenuNav(Sh_exit, Sh_label0 + 2 * kShootRows - 1, -1);
    shooting_refresh();
}

static void shooting_draw(void) { shooting_refresh(); }

/* The drill's rules, `you` being the team the player plays (the other the keeper-only CPU's). */
static void shooting_setup(ModMatchSetup *setup, int32_t you)
{
    ModApi_DefaultSetup(setup);
    setup->name = "shooting practice";
    setup->clockStopped = 1;
    setup->hideClock = 1;
    setup->playMatchScreen = 1;
    setup->autoReplays = 0;
    setup->stadium = 0;
    setup->replayMenu = 0;
    setup->linesOut[1 - you] = (uint8_t)(MODLINE_ATTACK | (s_keepDefenders ? 0 : MODLINE_DEFENCE) |
                                         (s_keepMidfield ? 0 : MODLINE_MIDFIELD));
    setup->playingUp = (int8_t)(s_shootUp ? you : 1 - you);
    setup->kickOff = (int8_t)you;
    setup->kickOffAlways = 1;
}

static void shooting_start(void)
{
    extern int32_t ModMatch_ChosenSide(int32_t team);   /* (src/mod_match.c: CPU or not) */
    ModMatchSetup setup;
    ModMatchResult r;
    int32_t cpu0, cpu1, you;

    if (!ModApi_ChooseTeams())
        return;
    cpu0 = ModMatch_ChosenSide(0) == 0;
    cpu1 = ModMatch_ChosenSide(1) == 0;
    if (cpu0 == cpu1) {         /* the drill needs exactly one side the CPU's */
        ModApi_ShowError("ONE TEAM MUST BE THE COMPUTER'S");
        return;
    }
    you = cpu0 ? 1 : 0;
    shooting_setup(&setup, you);
    setup.chosen = 1;
    if (ModApi_PlayMatch("practice", &setup, &r) == 0)
        fprintf(stderr, "[practice] shooting practice: %d-%d in %u frames\n", (int)r.goals[0],
                (int)r.goals[1], (unsigned)r.frames);
}

static void shooting_row(int32_t i)
{
    if (i == 0)
        s_keepDefenders ^= 1;
    else if (i == 1)
        s_keepMidfield ^= 1;
    else if (i == 2)
        s_shootUp ^= 1;
    else
        shooting_start();
}

static void srow0(void) { shooting_row(0); }
static void srow1(void) { shooting_row(1); }
static void srow2(void) { shooting_row(2); }
static void srow3(void) { shooting_row(3); }
static void practice_shooting(void) { ModApi_ShowMenu("practice/shootingMenu"); }

/* SWOS_SHOOTING_TEST=1 (SWOS_SKIP_INTRO=1, SWOS_HARNESS_TURBO=1): at the first main menu, the drill
 * CPU v CPU (team 0 "you", Norway v New Zealand) for 6000 frames, without the PLAY MATCH screen;
 * the keeper-only side's outfielders must never take the pitch, the clock must stay at 0 and no
 * period may end; logs the goals; PASS or FAIL, then quits. */
static int32_t s_shootTest, s_shootFailed, s_shootFrames;

static void shooting_test_frame(void)
{
    ModPlayer p[22];
    int32_t n;

    if (!s_shootTest || !ModApi_ModMatch("practice"))
        return;
    n = ModApi_Players(p, 22);
    for (int32_t i = 0; i < n; i++)
        if (p[i].team == 1 && p[i].index != 0 && p[i].onPitch && !s_shootFailed) {
            fprintf(stdout, "SHOOTING TEST: the CPU's player %d is on the pitch\n", (int)p[i].index);
            s_shootFailed = 1;
        }
    if (ModApi_MatchMinute() != 0 && !s_shootFailed) {
        fprintf(stdout, "SHOOTING TEST: the clock moved (%d)\n", (int)ModApi_MatchMinute());
        s_shootFailed = 1;
    }
    if (++s_shootFrames >= 6000)
        ModApi_EndMatch();
}

static void shooting_test_event(const ModMatchEvent *e)
{
    if (!s_shootTest || !ModApi_ModMatch("practice"))
        return;
    if (e->kind == MODEVENT_GOAL || e->kind == MODEVENT_OWN_GOAL || e->kind == MODEVENT_PENALTY_GOAL)
        fprintf(stdout, "SHOOTING TEST: goal, team %d, frame %u\n", (int)e->team, (unsigned)e->frame);
    else if (e->kind == MODEVENT_HALF_TIME || e->kind == MODEVENT_FULL_TIME) {
        fprintf(stdout, "SHOOTING TEST: the clock ended a period (%d)\n", (int)e->kind);
        s_shootFailed = 1;
    }
}

static void shooting_test_menu(void)
{
    ModMatchSetup setup;
    ModMatchResult r;

    if (s_shootTest != 1 || !ModApi_IsMainMenu())
        return;
    s_shootTest = 2;
    shooting_setup(&setup, 0);
    setup.team[0] = 0x5020;                 /* Norway */
    setup.team[1] = 0x5502;                 /* New Zealand */
    setup.side[0] = setup.side[1] = MODSIDE_CPU;
    setup.playMatchScreen = 0;              /* (it waits for a player) */
    ModApi_PlayMatch("practice", &setup, &r);
    fprintf(stdout, "SHOOTING TEST %s: %d-%d in %u frames\n",
            s_shootFailed || s_shootFrames < 6000 ? "FAIL" : "PASS", (int)r.goals[0], (int)r.goals[1],
            (unsigned)r.frames);
#ifdef SWOS_SDL_BUILD   /* (a test's, SWOS_SHOOTING_TEST: the Amiga has no environment to arm it) */
    exit(s_shootFailed || s_shootFrames < 6000);
#endif
}

static void practice_exit(void) { SetExitMenuFlag(); }

static void practice_open(void) { ModApi_ShowMenu("practice/practiceMenu"); }

static void practice_init(void)
{
    static const ModHandler kHandlers[] = {
        { "shootout", practice_shootout },
        { "orderInit", order_init }, { "orderDraw", order_draw },
        { "orow0", orow0 }, { "orow1", orow1 }, { "orow2", orow2 }, { "orow3", orow3 }, { "orow4", orow4 },
        { "orow5", orow5 }, { "orow6", orow6 }, { "orow7", orow7 }, { "orow8", orow8 }, { "orow9", orow9 },
        { "orderStart", order_start }, { "orderExit", order_exit },
        { "demo", practice_demo },
        { "training", practice_training },
        { "shooting", practice_shooting },
        { "shootingInit", shooting_init }, { "shootingDraw", shooting_draw },
        { "srow0", srow0 }, { "srow1", srow1 }, { "srow2", srow2 }, { "srow3", srow3 },
        { "trainingInit", training_init },
        { "trainingDraw", training_draw },
        { "trow0", trow0 }, { "trow1", trow1 }, { "trow2", trow2 }, { "trow3", trow3 },
        { "trow4", trow4 }, { "trow5", trow5 }, { "trow6", trow6 }, { "trow7", trow7 },
        { "exit", practice_exit },
    };
    ModApi_RegisterHandlers("practice", kHandlers, (int32_t)(sizeof kHandlers / sizeof kHandlers[0]));
    ModApi_AddEntry(MODPOINT_MODES, "practice", "practice", practice_open);
    s_shootTest = getenv("SWOS_SHOOTING_TEST") != NULL;
}

const ModDef ModDef_practice = {
    .slug = "practice",
    .init = practice_init,
    .menuDrawn = shooting_test_menu,
    .matchFrame = shooting_test_frame,
    .matchEvent = shooting_test_event,
};

