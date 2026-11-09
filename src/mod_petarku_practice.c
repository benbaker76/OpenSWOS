/*
 * mod_petarku_practice.c -- the petarku-practice mod (canonical, bin/mods/petarku-practice; docs/MODS.md
 * §18): petarku's practice modes, the5ar's port of his fork's penalty_shootout.c and
 * shooting_practice.c onto the mod API. Both targets. Kept apart from `practice` (SWOS 2020's
 * modes, src/mod_practice.c) on the owner's word (2026-11-09): a SWOS 2020 feature is never built
 * on petarku's, nor his on SWOS 2020's.
 *
 * MODES -> PETARKU PRACTICE opens its menu (petarku-practice/petarkuMenu.json, handlers
 * petarku-practice.<name>):
 *   PENALTY SHOOTOUT  the friendly's team screen (who plays each side), each player's side's SHOOTING
 *                     ORDER (shootoutOrderMenu.json), then straight to a shoot-out with a player's
 *                     keeper his to dive (manualKeeper)
 *   SHOOTING PRACTICE (shootingMenu.json): you against the CPU's goalkeeper (its defenders and
 *                     midfield on or off), no clock, every kick-off yours
 * Each match returns here (the menu again).
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
        fprintf(stderr, "[petarku-practice] row %d fire at %u (picked %d)\n", (int)i + 1, (unsigned)ModApi_Ticks(),
                (int)s_picked + 1);
    if (s_picked < 0) {
        s_picked = i;
    } else {
        uint8_t t = s_order[i];
        s_order[i] = s_order[s_picked];
        s_order[s_picked] = t;
        fprintf(stderr, "[petarku-practice] shooting order: %d <-> %d\n", (int)s_picked + 1, (int)i + 1);
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
        ModApi_ShowMenu("petarku-practice/shootoutOrderMenu");
        if (!s_orderGo)
            return 0;
        for (int32_t i = 0; i < 10; i++)
            setup->shootoutOrder[t][i] = s_order[i];
    }
    return 1;
}

static void pp_shootout(void)
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
    if (ModApi_PlayMatch("petarku-practice", &setup, &r) == 0)
        fprintf(stderr, "[petarku-practice] shoot-out: %d-%d\n", (int)r.penaltyGoals[0], (int)r.penaltyGoals[1]);
}

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
    if (ModApi_PlayMatch("petarku-practice", &setup, &r) == 0)
        fprintf(stderr, "[petarku-practice] shooting practice: %d-%d in %u frames\n", (int)r.goals[0],
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
static void pp_shooting(void) { ModApi_ShowMenu("petarku-practice/shootingMenu"); }

/* SWOS_SHOOTING_TEST=1 (SWOS_SKIP_INTRO=1, SWOS_HARNESS_TURBO=1): at the first main menu, the drill
 * CPU v CPU (team 0 "you", Norway v New Zealand) for 6000 frames, without the PLAY MATCH screen;
 * the keeper-only side's outfielders must never take the pitch, the clock must stay at 0 and no
 * period may end; logs the goals; PASS or FAIL, then quits. */
static int32_t s_shootTest, s_shootFailed, s_shootFrames;
static int32_t s_shootThrows, s_shootCorners;   /* (the keeper-only side's restarts given: the drill went on) */

static void shooting_test_frame(void)
{
    ModPlayer p[22];
    int32_t n;

    if (!s_shootTest || !ModApi_ModMatch("petarku-practice"))
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
    /* The keeper-only side's throw-ins (report 0058: its keeper went to take one, and the
     * game crashed as he threw -- or never reached the touchline, and the drill stood there):
     * one from each touchline, early, while the ball is in play. The other side takes them
     * (game_timers.c), so the drill goes on and the goals keep coming. */
    {
        int32_t throws = s_shootThrows;
        if ((throws == 0 && s_shootFrames >= 100) || (throws == 1 && s_shootFrames >= 1500)) {
            const int16_t x = throws == 0 ? 100 : 580;      /* (refused while the ball is dead) */
            if (ModApi_Restart(MODRESTART_THROW_IN, 1, 0, x, 400) == 0) {
                throws = ++s_shootThrows;
                fprintf(stdout, "SHOOTING TEST: a throw-in for the keeper's side, frame %d\n", (int)s_shootFrames);
            }
        }
    }
    /* ... and two corners for it (report 0059: its corner waited for good for a taker it has not
     * got): each becomes the other side's goal kick (game_timers.c), and the drill goes on. */
    {
        const int32_t corners = s_shootCorners;
        if ((corners == 0 && s_shootFrames >= 700) || (corners == 1 && s_shootFrames >= 2500)) {
            if (ModApi_Restart(MODRESTART_CORNER, 1, corners, 0, 0) == 0) {
                s_shootCorners++;
                fprintf(stdout, "SHOOTING TEST: a corner for the keeper's side, frame %d\n", (int)s_shootFrames);
            }
        }
    }
    if (++s_shootFrames >= 6000)
        ModApi_EndMatch();
}

static void shooting_test_event(const ModMatchEvent *e)
{
    if (!s_shootTest || !ModApi_ModMatch("petarku-practice"))
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
    ModApi_PlayMatch("petarku-practice", &setup, &r);
    /* (a drill that stood still at a restart its keeper-only side could not take scores nothing
     * after it, and never reaches the later restarts: reports 0058, 0059) */
    const int32_t stood = s_shootThrows < 2 || s_shootCorners < 2 || r.goals[0] + r.goals[1] == 0;
    fprintf(stdout, "SHOOTING TEST %s: %d-%d in %u frames, %d throw-ins and %d corners for the keeper's side%s\n",
            s_shootFailed || s_shootFrames < 6000 || stood ? "FAIL" : "PASS", (int)r.goals[0], (int)r.goals[1],
            (unsigned)r.frames, (int)s_shootThrows, (int)s_shootCorners, stood ? " -- the drill stood still" : "");
#ifdef SWOS_SDL_BUILD   /* (a test's, SWOS_SHOOTING_TEST: the Amiga has no environment to arm it) */
    exit(s_shootFailed || s_shootFrames < 6000);
#endif
}

static void pp_exit(void) { SetExitMenuFlag(); }

static void pp_open(void) { ModApi_ShowMenu("petarku-practice/petarkuMenu"); }

static void pp_init(void)
{
    static const ModHandler kHandlers[] = {
        { "shootout", pp_shootout },
        { "orderInit", order_init }, { "orderDraw", order_draw },
        { "orow0", orow0 }, { "orow1", orow1 }, { "orow2", orow2 }, { "orow3", orow3 }, { "orow4", orow4 },
        { "orow5", orow5 }, { "orow6", orow6 }, { "orow7", orow7 }, { "orow8", orow8 }, { "orow9", orow9 },
        { "orderStart", order_start }, { "orderExit", order_exit },
        { "shooting", pp_shooting },
        { "shootingInit", shooting_init }, { "shootingDraw", shooting_draw },
        { "srow0", srow0 }, { "srow1", srow1 }, { "srow2", srow2 }, { "srow3", srow3 },
        { "exit", pp_exit },
    };
    ModApi_RegisterHandlers("petarku-practice", kHandlers, (int32_t)(sizeof kHandlers / sizeof kHandlers[0]));
    ModApi_AddEntry(MODPOINT_MODES, "petarku-practice", "petarku practice", pp_open);
    s_shootTest = getenv("SWOS_SHOOTING_TEST") != NULL;
}

const ModDef ModDef_petarkuPractice = {
    .slug = "petarku-practice",
    .init = pp_init,
    .menuDrawn = shooting_test_menu,
    .matchFrame = shooting_test_frame,
    .matchEvent = shooting_test_event,
};
