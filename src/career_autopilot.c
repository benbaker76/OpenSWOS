/*
 * career_autopilot.c -- a career that plays itself (include/career_autopilot.h).
 *
 * Phase 0 of docs/WORLD_2026_27.md: a golden test of today's career, so every later
 * change to the season engine can be shown to leave the 96/97 world exactly as it was.
 * It drives the game's own menus, as a player would, rather than calling the season
 * code directly: the menus' handlers are part of what a season does.
 *
 * The policy is the gdb harness's (scripts/swos_plugin.py, drive_inseason /
 * drive_transition), run in-process:
 *   - the career club's own matches are simulated (teamControls = computer), like
 *     every other fixture;
 *   - the season-end result screens are skipped (the SPACE latch);
 *   - the season-end hub: CONTINUE CAREER; the squad screen: player 1's fire (it reads
 *     the joystick, not the menu's fire); every other screen: fire on its selection.
 */
#include "career_autopilot.h"
#include "world_winners.h"
#include "comp_identity.h"
#include "menu_system.h"
#include "game_types.h"
#include "mod_api.h"
#include "save_format_json.h"
#include "swos_io.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string.h>

extern uint8_t  _currentMenu[];
extern volatile uint16_t _shortFire;
extern uint8_t  _pl1Fire;
extern uint16_t _currentSeasonShowing;
extern TeamFileEx _careerTeam;
extern volatile uint16_t _currentTick;
extern volatile uint16_t _currentGameTick;
extern void Platform_QuiesceTimerThread(void);   /* platform_sdl.c */
extern uint8_t  _seed, _randXorIndex, _randXorKey;
extern uint8_t  _seed2, _randXorIndex2, _randXorKey2;
extern int8_t   _kRandomTable[];   /* swos_data_bulk_imports.c: int8_t[257] */

extern void SWOSMainMenuInit(void);
extern void CareerOverInit(void);
extern void PlayMatchMenuInit(void);
extern void ContinueCareer(void);
extern void StartContest(void);
extern void DIYLeagueTournamentInit(void);
extern void ContestMenuInit(void);
extern void NextMatch(MenuEntry *);
extern void ViewWorldMenu(void);
extern void SelectTeamsInit(void);
extern void CareerMenu(void);                   /* menus.c */
extern void JobOffersMenu(void);
extern void JobOffersMenuRetired(void);
extern void JobOffersInit(void);
extern void JobOffersCommon(MenuEntry *);       /* job_offers.c: an offer row, opens _gotJobOfferMenu */
extern void GotJobOfferInit(void);
extern void OldJobOffersCommon(void);
extern void AcceptJobOffer(void);
extern void ChooseTeamAndStartCareer(void);     /* career_init.c */
extern void DIYLeagueTournamentNextMatch(void);   /* diy_competition.c: takes no entry */
extern void DIYLeagueTournamentViewResult(MenuEntry *);
extern void DIYLeagueTournamentResult(MenuEntry *);
extern void DIYLeagueTournamentMatch(MenuEntry *);
extern void MenuSystem_LatchSeasonSkip(void);

static const char *s_save;          /* --career-golden */
static const char *s_outDir = ".";  /* --golden-out */
static int32_t     s_seasons = 3;   /* --golden-seasons */
static int32_t     s_started;
static int32_t     s_hubs;          /* season-end hubs reached (and saves written) */
static int32_t     s_hubSeason = -1;
static int32_t     s_jobs;          /* job offers taken after a sacking */
static int32_t     s_nationalTaken; /* SWOS_TEST_NATIONAL_JOB: the national offer gone to */
static int32_t     s_infoShown;
static int32_t     s_viewShot;      /* (SWOS_GOLDEN_VIEW_SLOT3: screens left to shoot) */
static int32_t     s_winPage, s_winWait;    /* (SWOS_GOLDEN_VIEW_WINNERS: the page shown, drawn yet) */
extern DIYCompetitionEx _diyCompetitionStart;
extern void Platform_RequestScreenshot(void);

/* Test (SWOS_GOLDEN_VIEW_WINNERS=<n>): the SEASON WINNERS page (world_winners.c) from every n-th
 * match-day screen of a contest with titles (SWOS_GOLDEN_SHOT_FROM: from that season-end hub on),
 * its pages each a screenshot; at most 48 times. */
static int32_t view_winners(void)
{
    static const char *env = (const char *)1;
    static int32_t visits, opened;
    if (env == (const char *)1)
        env = getenv("SWOS_GOLDEN_VIEW_WINNERS");
    const char *from = getenv("SWOS_GOLDEN_SHOT_FROM");
    if (!env || atoi(env) < 1 || opened >= 48 || s_hubs < (from ? atoi(from) : 0) || !WorldWinners_Available())
        return 0;
    if (++visits % atoi(env))
        return 0;
    opened++;
    return 1;
}

/* Test (SWOS_GOLDEN_VIEW_WORLD=<continent>): from the first season hub, VIEW WORLD, that
 * continent's row, a screenshot of the screen it opens (its competitions above its
 * countries: the layout World 2026-27's Europe overran), then exit -- with
 * SWOS_MENU_OVERLAP=1, its overlaps in SWOS.log. 1 while it is driving the screen. */
static void say(const char *fmt, ...);
static int32_t entry_with(MenuEntryFn fn);
static int32_t entry_labelled(const char *label);
static int32_t view_world(const Menu *menu)
{
    static const char *env = (const char *)1;
    static char row[64];
    static const char *then;        /* <continent>/<row>: that row of the continent's screen too */
    static int32_t stage, waited;
    int32_t i;
    if (env == (const char *)1) {
        env = getenv("SWOS_GOLDEN_VIEW_WORLD");
        const char *slash = env ? strchr(env, '/') : NULL;
        if (slash) {
            snprintf(row, sizeof row, "%.*s", (int)(slash - env), env);
            then = slash + 1;
            env = row;
        }
    }
    if (!env || !*env)
        return 0;
    {   /* (SWOS_GOLDEN_SHOT_FROM=<n>: from the hub after the n-th season's end) */
        const char *from = getenv("SWOS_GOLDEN_SHOT_FROM");
        if (stage == 0 && from && s_hubs < atoi(from))
            return 0;
    }
    if (stage == 0 && menu->onInit == ContestMenuInit) {
        if ((i = entry_with((MenuEntryFn)ViewWorldMenu)) < 0)
            return 0;
        SetCurrentEntry((uint16_t)i);
        _shortFire = 1;
        stage = 1;
        return 1;
    }
    if (stage == 1 && menu->onInit == SelectTeamsInit) {
        if ((i = entry_labelled(env)) < 0)
            return ++waited > 300 ? (say("[golden] '%s' is not on VIEW WORLD\n", env), exit(2), 1) : 1;
        SetCurrentEntry((uint16_t)i);
        _shortFire = 1;
        stage = 2;
        waited = 0;
        return 1;
    }
    if (stage == 2 && then && menu->onInit == SelectTeamsInit) {
        if (++waited < 10)
            return 1;
        if ((i = entry_labelled(then)) < 0)
            return waited > 300 ? (say("[golden] '%s' is not on %s\n", then, env), exit(2), 1) : 1;
        say("[golden] view world: %s -> %s\n", env, then);
        SetCurrentEntry((uint16_t)i);
        _shortFire = 1;
        stage = 3;
        waited = 0;
        return 1;
    }
    if (stage == 3) {
        if (++waited == 2) {
            extern void Platform_RequestScreenshot(void);
            Platform_RequestScreenshot();
        } else if (waited == 80) {
            say("[golden] done\n");
            exit(0);
        }
        return 1;
    }
    if (stage == 2 && menu->onInit == SelectTeamsInit) {
        if (++waited == 10) {
            extern void Platform_RequestScreenshot(void);
            Platform_RequestScreenshot();
            say("[golden] view world: %s\n", env);
        } else if (waited == 40) {
            say("[golden] done\n");
            exit(0);
        }
        return 1;
    }
    return 0;
}

/* Test (SWOS_GOLDEN_HUB_PATH=<label>/<label>/...): at the in-season hub's n-th pass
 * (SWOS_GOLDEN_HUB_PASS=<n>, default 1; counted from the SWOS_GOLDEN_SHOT_FROM-th season-end
 * hub on), each label pressed in turn on whatever screen is up -- a screenshot of each screen
 * before its label is pressed, and of the last one -- then exit 0 ('*': the entry the cursor is
 * on, '#<n>': entry n; a label not on its screen: its rows listed, exit 2). SWOS_GOLDEN_HUB_PLAYER=1:
 * the career club a player's meanwhile (its match shows the stadium screen). To look at screens a career reaches
 * from its hub (VIEW COMPETITIONS, CONTINENTAL CUPS, ...). 1: this tick was the test's. */
static int32_t hub_path(const Menu *menu)
{
    extern void Platform_RequestScreenshot(void);
    static const char *env = (const char *)1;
    static char buf[512];
    static const char *step[24];
    static int32_t steps, k, stage, wait, passes, waited;
    int32_t i;
    if (env == (const char *)1) {
        env = getenv("SWOS_GOLDEN_HUB_PATH");
        if (env && *env) {
            snprintf(buf, sizeof buf, "%s", env);
            for (char *p = buf; p && *p && steps < 24; ) {
                step[steps++] = p;
                p = strchr(p, '/');
                if (p)
                    *p++ = '\0';
            }
        }
    }
    if (!steps)
        return 0;
    if (stage > 0 && getenv("SWOS_GOLDEN_HUB_PLAYER"))    /* (=1: the career club a player's while the path */
        _careerTeam.team.teamControls = 2;              /*  runs: its match shows the stadium screen) */
    if (stage == 0) {
        const char *from = getenv("SWOS_GOLDEN_SHOT_FROM"), *pass = getenv("SWOS_GOLDEN_HUB_PASS");
        if (menu->onInit != ContestMenuInit || s_hubs < (from ? atoi(from) : 0) ||
            ++passes < (pass ? atoi(pass) : 1))
            return 0;
        stage = 1;
        wait = 8;
        say("[golden] hub path: %d label(s)\n", (int)steps);
    }
    if (wait > 0) {
        if (--wait == 3) {
            Platform_RequestScreenshot();
            say("[golden] hub path screenshot %d\n", (int)k);
        }
        return 1;
    }
    if (k >= steps) {
        say("[golden] done\n");
        exit(0);
    }
    if (strcmp(step[k], "*") == 0)
        i = ((const Menu *)_currentMenu)->selectedEntry ? -2 : -1;
    else if (step[k][0] == '#')         /* ('#<n>': entry n, a row with no label) */
        i = atoi(step[k] + 1) < menu->numEntries ? atoi(step[k] + 1) : -1;
    else
        i = entry_labelled(step[k]);
    if (i == -1) {
        if (++waited < 100)
            return 1;
        say("[golden] '%s' is not on this screen; its rows:\n", step[k]);
        for (uint16_t r = 0; r < menu->numEntries; r++) {
            const MenuEntry *e = CalcMenuEntryAddress(r);
            if (!e->isInvisible && e->content == ENTRY_STRING && e->fg)
                say("[golden]   %s\n", (const char *)e->fg);
        }
        exit(2);
    }
    waited = 0;
    if (i >= 0)
        SetCurrentEntry((uint16_t)i);
    _shortFire = 1;
    if (menu->onInit == PlayMatchMenuInit)
        _pl1Fire = 1;                   /* (PlayMatchSelected goes on at a player's fire: see below) */
    say("[golden] hub path: %s\n", step[k]);
    k++;
    wait = 8;
    return 1;
}

/* --career-golden=new:<continent>/<country>/<division>/<club>: a new career, the club
 * picked through CAREER -> CHOOSE TEAM AND START CAREER by the rows' labels (the picker
 * uses one handler for every row, so only the label tells them apart). */
#define NEW_PATH_MAX 8
static char        s_pathBuf[256];
static int32_t     s_rebuild;       /* rebuild:<original .CAR> (src/car_rebuild.c) */
extern int32_t     CarRebuild_Start(const char *path);
static const char *s_path[NEW_PATH_MAX];
static int32_t     s_pathN, s_pathK;
static int32_t     s_choosing;      /* still picking the club */
/* --career-golden=menu:<label>/<label>/...: no career -- from the main menu, each label in
 * turn, then a screenshot of the screen it ends on and exit 0 (a screen that crashes or
 * panics on the way says so). For screens no career passes through: PRESET COMPETITION,
 * SEASON, a world's cups. */
static int32_t     s_menuMode;
static int32_t     s_waited;        /* passes without the next label showing */

/* To stdout (the game sends stderr to SWOS.log), flushed: the test script reads it. */
static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
}

int32_t CareerAutopilot_Active(void)   { return s_save != NULL; }
int32_t CareerAutopilot_OwnsClock(void) { return s_save != NULL && s_started; }

int32_t CareerAutopilot_ParseArg(const char *a)
{
    if (strncmp(a, "--career-golden=", 16) == 0) {
        s_save = a + 16;
        /* Turbo: the game tick then advances on every Platform_VBlank call instead of by
         * the wall clock (platform_sdl.c, RunInt3HandlerThrottled) -- needed for the run
         * to repeat, so not left to the caller. Headless too: a batch run. */
#ifdef _WIN32
        _putenv("SWOS_HARNESS_TURBO=1");
        if (!getenv("SDL_VIDEODRIVER")) _putenv("SDL_VIDEODRIVER=dummy");
        if (!getenv("SDL_AUDIODRIVER")) _putenv("SDL_AUDIODRIVER=dummy");
#else
        setenv("SWOS_HARNESS_TURBO", "1", 1);
        if (!getenv("SDL_VIDEODRIVER")) setenv("SDL_VIDEODRIVER", "dummy", 1);
        if (!getenv("SDL_AUDIODRIVER")) setenv("SDL_AUDIODRIVER", "dummy", 1);
#endif
        if (strncmp(s_save, "new:", 4) == 0 || strncmp(s_save, "menu:", 5) == 0) {
            char *p = s_pathBuf;
            s_menuMode = s_save[0] == 'm';
            snprintf(s_pathBuf, sizeof s_pathBuf, "%s", s_save + (s_menuMode ? 5 : 4));
            for (s_pathN = 0; *p && s_pathN < NEW_PATH_MAX; s_pathN++) {
                s_path[s_pathN] = p;
                while (*p && *p != '/') p++;
                if (*p) *p++ = '\0';
            }
            return s_pathN > 0 ? 1 : -1;
        }
        if (strncmp(s_save, "rebuild:", 8) == 0) {
            /* An original Amiga .CAR, rebuilt (src/car_rebuild.c): written to REBUILT.CAR
             * in --golden-out, then played for --golden-seasons (0: not played). */
            s_rebuild = 1;
            s_save += 8;
        }
        return s_save[0] ? 1 : -1;
    }
    if (strncmp(a, "--golden-seasons=", 17) == 0) {
        s_seasons = atoi(a + 17);
        return s_seasons >= 0 ? 1 : -1;
    }
    if (strncmp(a, "--golden-out=", 13) == 0) {
        s_outDir = a + 13;
        return s_outDir[0] ? 1 : -1;
    }
    return 0;
}

/* Both generators from one fixed value, as DETERMINISTIC_MODE does at build time. */
static void pin_random(void)
{
    const uint16_t seed = 0x1234;
    _seed = (uint8_t)seed;
    _randXorIndex = (uint8_t)(seed >> 8);
    _randXorKey = (uint8_t)_kRandomTable[_randXorIndex];
    _seed2 = (uint8_t)(seed >> 4);
    _randXorIndex2 = (uint8_t)(seed >> 12);
    _randXorKey2 = (uint8_t)_kRandomTable[_randXorIndex2];
    _currentTick = 0;
    _currentGameTick = 0;
}

/* The entry whose onSelect is fn, if a player could fire it (visible and not disabled:
 * the hub disables NEXT MATCH once the season is over, and firing it then panics, 0x8341),
 * or -1. */
static int32_t entry_with(MenuEntryFn fn)
{
    const Menu *menu = (const Menu *)_currentMenu;
    for (uint16_t i = 0; i < menu->numEntries; i++) {
        const MenuEntry *e = CalcMenuEntryAddress(i);
        if (e->onSelect == fn && !e->isInvisible && !e->isDisabled)
            return i;
    }
    return -1;
}

/* Select the first visible entry of fns[] (n of them); 0 if none is showing. */
static int32_t select_first(const MenuEntryFn *fns, int32_t n)
{
    for (int32_t k = 0; k < n; k++) {
        int32_t i = entry_with(fns[k]);
        if (i >= 0) {
            SetCurrentEntry((uint16_t)i);
            return 1;
        }
    }
    return 0;
}

/* The visible row labelled `label` (case-insensitive; else the first that starts with
 * it, so "NOTT" finds "NOTT'M FOREST"), or -1. */
static int32_t entry_labelled(const char *label)
{
    const Menu *menu = (const Menu *)_currentMenu;
    const size_t n = strlen(label);
    int32_t prefix = -1;
    for (uint16_t i = 0; i < menu->numEntries; i++) {
        const MenuEntry *e = CalcMenuEntryAddress(i);
        const char *t;
        if (e->isInvisible || e->isDisabled || e->content != ENTRY_STRING || !e->fg || !e->onSelect)
            continue;
        t = (const char *)e->fg;
        if (strcasecmp(t, label) == 0)
            return i;
        if (prefix < 0 && strncasecmp(t, label, n) == 0)
            prefix = i;
    }
    return prefix;
}

/* One pass of starting a new career: CAREER, CHOOSE TEAM AND START CAREER, then each
 * label of the path. Nothing is fired while the next label is not on screen yet. */
static void choose_step(const Menu *menu)
{
    int32_t i;
    if ((i = entry_with((MenuEntryFn)ChooseTeamAndStartCareer)) >= 0) {
        SetCurrentEntry((uint16_t)i);
        _shortFire = 1;
        return;
    }
    if (Menu_IsMainMenu()) {
        if ((i = entry_with((MenuEntryFn)CareerMenu)) >= 0) {
            SetCurrentEntry((uint16_t)i);
            _shortFire = 1;
        }
        return;
    }
    if ((i = entry_labelled(s_path[s_pathK])) >= 0) {
        SetCurrentEntry((uint16_t)i);
        _shortFire = 1;
        s_waited = 0;
        if (++s_pathK == s_pathN) {
            s_choosing = 0;     /* the club: the career starts (InitCareer) */
            say("[golden] new career: %s (tick %u, seeds %02x%02x %02x%02x)\n", s_path[s_pathN - 1],
                (unsigned)_currentTick, _seed, _randXorIndex, _seed2, _randXorIndex2);
        }
        return;
    }
    if (++s_waited > 300) {
        say("[golden] '%s' is not on this screen; its rows:\n", s_path[s_pathK]);
        for (uint16_t k = 0; k < menu->numEntries; k++) {
            const MenuEntry *e = CalcMenuEntryAddress(k);
            if (!e->isInvisible && e->content == ENTRY_STRING && e->fg)
                say("[golden]   %s\n", (const char *)e->fg);
        }
        exit(2);
    }
}

/* One pass of a menu: path: the next label, or, past the last, the screenshot and out. */
static void menu_path_step(const Menu *menu)
{
    static int32_t done;
    int32_t i;
    if (s_pathK < s_pathN) {
        if ((i = entry_labelled(s_path[s_pathK])) >= 0) {
            SetCurrentEntry((uint16_t)i);
            _shortFire = 1;
            s_waited = 0;
            say("[golden] menu: %s\n", s_path[s_pathK]);
            s_pathK++;
            return;
        }
        if (++s_waited > 300) {
            say("[golden] '%s' is not on this screen; its rows:\n", s_path[s_pathK]);
            for (uint16_t k = 0; k < menu->numEntries; k++) {
                const MenuEntry *e = CalcMenuEntryAddress(k);
                if (!e->isInvisible && e->content == ENTRY_STRING && e->fg)
                    say("[golden]   %s\n", (const char *)e->fg);
            }
            exit(2);
        }
        return;
    }
    if (++done == 30) {
        extern void Platform_RequestScreenshot(void);
        Platform_RequestScreenshot();
        say("[golden] menu path done: a screenshot of the last screen\n");
    } else if (done == 60) {
        exit(0);
    }
}

static void start(void)
{
    /* The original takes randomness from the game tick too -- the chairman's 50% (career
     * status) and a new career's default manager name -- and the SDL timer thread ticks
     * it by the wall clock. With that thread quiet the tick advances once per frame from
     * the main thread (Platform_VBlank; turbo, so every call ticks), the same every run. */
    Platform_QuiesceTimerThread();
    s_started = 1;
    swos_set_save_dir(s_outDir);
    pin_random();
    if (s_menuMode) {
        say("[golden] menu path, %d label(s)\n", (int)s_pathN);
        return;
    }
    if (s_pathN > 0) {
        say("[golden] new career, %d season(s) (SWOSMainMenuInit at %p)\n", (int)s_seasons,
            (void *)(uintptr_t)SWOSMainMenuInit);
        s_choosing = 1;         /* the main menu's passes now pick the club */
        return;
    }
    if (s_rebuild) {
        if (CarRebuild_Start(s_save) != 0)
            exit(2);
        if (SaveJson_WriteCareer((uint8_t *)"REBUILT.CAR") != 0) {
            say("[golden] cannot write %s/REBUILT.CAR\n", s_outDir);
            exit(2);
        }
        say("[golden] wrote %s/REBUILT.CAR\n", s_outDir);
        if (s_seasons == 0)
            exit(0);
        pin_random();
        StartContest();
        say("[golden] the career ended after %d of %d season(s)\n",
            (int)(s_hubs > 0 ? s_hubs - 1 : 0), (int)s_seasons);
        exit(3);
    }
    say("[golden] loading %s/%s, %d season(s) (SWOSMainMenuInit at %p)\n", s_outDir, s_save,
        (int)s_seasons, (void *)(uintptr_t)SWOSMainMenuInit);
    {   /* SWOS_LOAD_BENCH=<n>: load the career n times, write it back as REWRITE.CAR and exit --
         * per-phase load timings (make SWOS_SAVE_FORMAT_JSON_PROFILE=1), and a check that
         * load + save gives the same file a decoder change must leave alone. */
        const char *bench = getenv("SWOS_LOAD_BENCH");
        if (bench && atoi(bench) > 0) {
            for (int32_t k = 0; k < atoi(bench); k++)
                if (SaveJson_LoadCareer((uint8_t *)s_save, NULL, 0) != 0) {
                    say("[golden] bench: cannot load\n");
                    exit(2);
                }
            say("[golden] bench: %d loads done\n", atoi(bench));
            /* the career as loaded, written back: it must equal the file it came from */
            {   /* SWOS_SAVE_BENCH=<n>: the write n times (the writer's timings) */
                const char *sb = getenv("SWOS_SAVE_BENCH");
                int32_t writes = (sb && atoi(sb) > 0) ? atoi(sb) : 1;
                for (int32_t w = 0; w < writes; w++)
                    if (SaveJson_WriteCareer((uint8_t *)"REWRITE.CAR") != 0)
                        say("[golden] bench: cannot write REWRITE.CAR\n");
            }
            exit(0);
        }
    }
    if (SaveJson_LoadCareer((uint8_t *)s_save, NULL, 0) != 0) {
        say("[golden] cannot load %s/%s\n", s_outDir, s_save);
        exit(2);
    }
    /* (SWOS_GOLDEN_KEEP_RANDOM: the save's own random state and clock kept -- a career stopped and
     * loaded again goes on as the run that was not stopped; compared with it) */
    {   /* ([career] exact_continuation: the save's random state and clocks restored -- kept) */
        extern int32_t SaveJson_ExactStateLoaded(void);
        if (!getenv("SWOS_GOLDEN_KEEP_RANDOM") && !SaveJson_ExactStateLoaded())
            pin_random();
    }
    StartContest();             /* the career runs inside this, modal */
    say("[golden] the career ended after %d of %d season(s)\n",
        (int)(s_hubs > 0 ? s_hubs - 1 : 0), (int)s_seasons);
    exit(3);
}

/* The season-end hub: write this season's save once, then CONTINUE (or stop).
 * SEASON00.CAR is the first hub (where the save started, or the end of the season it
 * was saved in); SEASON01.CAR the hub after one more season; ... */
static void at_hub(void)
{
    if (s_hubSeason != (int32_t)_currentSeasonShowing) {
        char name[32];
        s_hubSeason = (int32_t)_currentSeasonShowing;
        snprintf(name, sizeof name, "SEASON%02d.CAR", (int)s_hubs);
        if (SaveJson_WriteCareer((uint8_t *)name) != 0) {
            say("[golden] cannot write %s/%s\n", s_outDir, name);
            exit(2);
        }
        say("[golden] %s: season %u (tick %u, seeds %02x%02x %02x%02x)\n", name,
            (unsigned)_currentSeasonShowing, (unsigned)_currentTick, _seed, _randXorIndex,
            _seed2, _randXorIndex2);
        if (s_hubs++ >= s_seasons) {
            say("[golden] done\n");
            exit(0);
        }
    }
    {   /* SWOS_TEST_NATIONAL_JOB (job_offers.c): the national team's offer taken at the first hub
         * that has it -- JOB OFFERS, the first offer, ACCEPT -- the next season a national coach's */
        extern uint16_t _isNationalSelector, _numNewJobOffers, _numJobOffers;
        static const MenuEntryFn jobs[] = { (MenuEntryFn)JobOffersMenuRetired, (MenuEntryFn)JobOffersMenu };
        if (getenv("SWOS_TEST_NATIONAL_JOB") && _isNationalSelector == 0 && !s_nationalTaken &&
            _numNewJobOffers + _numJobOffers > 0 && select_first(jobs, 2)) {
            s_nationalTaken = 1;    /* (ACCEPT: GotJobOfferInit below, at the season's end) */
            say("[golden] the national team's job offer: accepted\n");
            _shortFire = 1;
            return;
        }
    }
    {
        int32_t i = entry_with((MenuEntryFn)ContinueCareer);
        if (i < 0) {
            /* No CONTINUE CAREER: the manager was sacked. Take a job, as a player would
             * (JOB OFFERS -> the first offer -> ACCEPT); with none, the career is over. */
            static const MenuEntryFn jobs[] = {
                (MenuEntryFn)JobOffersMenuRetired, (MenuEntryFn)JobOffersMenu };
            if (!select_first(jobs, 2)) {
                say("[golden] the career is over after %d of %d season(s): sacked, and no "
                    "JOB OFFERS\n", (int)(s_hubs - 1), (int)s_seasons);
                exit(4);
            }
        } else
            SetCurrentEntry((uint16_t)i);
    }
    _shortFire = 1;
}

/* Test (SWOS_GOLDEN_SHOT_NUMBERS=1): at the first in-season hub, the squad screen, where a player's
 * shirt number is edited as a player would (shirt_edit.c): the cursor on the second row, LEFT, UP,
 * '7' (a swap), ENTER; then LEFT, '2' '3', ESC (undone). A screenshot every 4 passes. SDL keys,
 * pressed through the event queue. 1: this tick was the test's. */
static int32_t numbers_test(const Menu *menu)
{
    extern void SquadInit(void), SquadMenu(void), SetExitMenuFlag(void), Platform_RequestScreenshot(void);
    extern void Platform_TestKey(int32_t key, int32_t down);
    extern MenuEntry _menuEntries[];
    static const struct { int16_t at; int32_t key; } kPress[] = {
        { 10, 0x40000050 /* LEFT */ }, { 22, 0x40000052 /* UP */ }, { 34, '7' }, { 46, 13 /* RETURN */ },
        { 62, 0x40000050 }, { 74, '2' }, { 78, '3' }, { 90, 27 /* ESCAPE */ },
    };
    static int32_t on = -1, stage, held;
    if (on < 0)
        on = getenv("SWOS_GOLDEN_SHOT_NUMBERS") != NULL;
    if (!on || stage < 0)
        return 0;
    if (stage == 0) {
        if (menu->onInit != ContestMenuInit)
            return 0;
        stage = 1;
        _careerTeam.team.teamControls = 2;  /* (the player's club while it is up, as in a game) */
        SquadMenu();                        /* (the hub's SQUAD: back here when it is left) */
        _careerTeam.team.teamControls = 1;
        stage = -1;
        return 1;
    }
    if (menu->onInit != SquadInit)
        return 0;
    if (held == 4)
        ((Menu *)menu)->selectedEntry = &_menuEntries[29];   /* (the second row) */
    for (uint32_t i = 0; i < sizeof kPress / sizeof kPress[0]; i++) {
        if (held == kPress[i].at)
            Platform_TestKey(kPress[i].key, 1);
        if (held == kPress[i].at + 2)
            Platform_TestKey(kPress[i].key, 0);
    }
    if (held % 4 == 0) {
        Platform_RequestScreenshot();
        say("[golden] numbers: pass %d\n", (int)held);
    }
    if (++held >= 110)
        SetExitMenuFlag();
    _shortFire = 0;
    return 1;
}

/* Test (SWOS_GOLDEN_SHOT_BTEAM=1): a world career's B-team screens, a screenshot each, at the first
 * in-season hub (bteam.c): SQUAD with its B TEAM button, the B squad, its page 2 (the players 17..),
 * a call-up's page and its answer (NEXT MATCH ONLY on page 2's first player), the B squad again (him
 * greyed), the squad again (him in it, LOAN). 1: this tick was the test's. */
static int32_t bteam_shots(const Menu *menu)
{
    extern void SquadInit(void), BuyOtherPlayersInit(void), SquadMenu(void), BTeamSquadButton(void);
    extern void BuyOtherPlayersPage(void), BuyOtherPlayersCommon(void *entry), SetExitMenuFlag(void);
    extern void Platform_RequestScreenshot(void);
    extern MenuEntry _menuEntries[];
    static int32_t on = -1, stage, wait;
    static MenuVoidFn dialogSeen;
    if (on < 0)
        on = getenv("SWOS_GOLDEN_SHOT_BTEAM") != NULL;
    if (!on || stage < 0)
        return 0;
    if (stage > 0)                      /* (a player's club while its screens are up, as they are */
        _careerTeam.team.teamControls = 2;  /*  in a game: the autopilot's own club is the computer's) */
    if (wait > 0) {                     /* (a screenshot asked: let it be drawn first) */
        wait--;
        return 1;
    }
#define SHOT(what) do { Platform_RequestScreenshot(); say("[golden] bteam screenshot: %s\n", what); wait = 3; } while (0)
    if (stage == 0) {
        if (menu->onInit != ContestMenuInit)
            return 0;
        stage = 1;
        _careerTeam.team.teamControls = 2;
        SquadMenu();                    /* (the hub's SQUAD; this test drives it, back here when done) */
        stage = -1;
        return 1;
    }
    if (menu->onInit == SquadInit) {
        switch (stage) {
        case 1: SHOT("the squad"); stage = 2; return 1;
        case 2: stage = 3; BTeamSquadButton(); stage = 9; _shortFire = 0; return 1;
        case 9: {
            extern void BTeam_TestShowCalled(void), DrawMenu(void);
            BTeam_TestShowCalled();     /* (SWOS_TEST_BTEAM_SHOWN: him on a drawn row) */
            DrawMenu();
            SHOT("the squad after the call-up"); stage = 10; return 1;
        }
        default: SetExitMenuFlag(); return 1;
        }
    }
    if (menu->onInit == BuyOtherPlayersInit) {
        switch (stage) {
        case 3: SHOT("the B squad"); stage = 4; return 1;
        case 4: BuyOtherPlayersPage(); stage = 5; return 1;
        case 5: SHOT("the B squad, page 2"); stage = 6; return 1;
        case 6:
            stage = 7;
            dialogSeen = NULL;
            BuyOtherPlayersCommon(&_menuEntries[3]);
            stage = 8;
            _shortFire = 0;             /* (the answer's fire is spent: not this screen's) */
            return 1;
        case 8: SHOT("the B squad after the call-up"); stage = 81; return 1;
        default: SetExitMenuFlag(); return 1;
        }
    }
    if (stage == 7) {                   /* (the call-up page, then its answer: a shot, then fire) */
        static int32_t shots;
        if (menu->onInit != dialogSeen || shots == 0) {
            dialogSeen = menu->onInit;
            shots = 1;
            SHOT("a call-up dialog");
            return 1;
        }
        shots = 0;
        _shortFire = 1;
        return 1;
    }
#undef SHOT
    return 0;
}

/* SWOS_TEST_NATIONAL_JOB (job_offers.c): the national team's offer, as soon as the hub shows JOB
 * OFFERS -- the first offer (the only one then), CONSIDER (ACCEPT is the season end's: at_hub); a
 * national coach's from the next season. 1 when it went to the offers. */
static int32_t national_offer(void)
{
    extern uint16_t _isNationalSelector, _numNewJobOffers, _numJobOffers;
    int32_t j;
    static int32_t considered;
    if (!getenv("SWOS_TEST_NATIONAL_JOB") || _isNationalSelector != 0 || considered ||
        _numNewJobOffers + _numJobOffers == 0 || (j = entry_with((MenuEntryFn)JobOffersMenu)) < 0 ||
        CalcMenuEntryAddress((uint16_t)j)->isDisabled)
        return 0;
    considered = 1;             /* (CONSIDER: kept to the season's end, when ACCEPT is there) */
    say("[golden] the national team's job offer: considered\n");
    SetCurrentEntry((uint16_t)j);
    _shortFire = 1;
    return 1;
}

/* A national coach's squad short of its 20 (the job's first season, a retirement): called up as a
 * coach would, the best players of the nation by value -- 2 goalkeepers, the rest outfield, as the
 * squad's slots take them -- from its home league's clubs first, then every club (the mods' call-up,
 * ModApi_CallUpPlayer, then the game's add). 1 when it did some (the hub then drawn again). */
static int32_t national_callups(void)
{
    extern uint16_t _isNationalSelector, _careerNumPlayers;
    extern uint8_t  _nationalTeamNationality, _nationalTeamFileId;
    extern int32_t  Career_PeekTeam(uint16_t teamId, TeamFileEx *out);
    extern void     Career_ShowAddInternationalPlayerDialog(uint16_t homeOrAbroadFlag);
    extern void     SetNationalSelectorButtons(void);
    extern void     ContestMenuOnReturn(void);
    enum { kMax = 400 };
    static struct { uint16_t team, slot, price; uint8_t gk; } cand[kMax];
    static TeamFileEx t;
    int32_t n = 0, added = 0;
    if (_isNationalSelector == 0 || _careerNumPlayers >= 20)
        return 0;
    for (int32_t pass = 0; pass < 2 && _careerNumPlayers < 20; pass++) {
        n = 0;
        for (int32_t f = 0; f < 100 && n < kMax; f++) {
            if ((pass == 0) != (f == _nationalTeamFileId) || f == 72 || (f >= 80 && f <= 85))
                continue;
            for (int32_t o = 0; o < 256 && n < kMax; o++) {
                const uint16_t team = (uint16_t)(f << 8 | o);
                if (!Career_PeekTeam(team, &t))
                    break;
                for (int32_t p = 0; p < 16 && n < kMax; p++)
                    if ((uint8_t)t.players[p].playerName[0] != 0xFF &&
                        (uint8_t)t.players[p].nationality == _nationalTeamNationality) {
                        cand[n].team = team;
                        cand[n].slot = (uint16_t)p;
                        cand[n].price = (uint8_t)t.players[p].price;
                        cand[n].gk = ((t.players[p].positionAndFace & PF_POSITION_MASK) >> 5) == 0;
                        n++;
                    }
            }
        }
        /* by value (dearest first), then team and slot: the same squad every run */
        for (int32_t i = 1; i < n; i++)
            for (int32_t j = i; j > 0 && (cand[j].price > cand[j - 1].price ||
                                          (cand[j].price == cand[j - 1].price &&
                                           (cand[j].team < cand[j - 1].team ||
                                            (cand[j].team == cand[j - 1].team && cand[j].slot < cand[j - 1].slot)))); j--) {
                const __typeof__(cand[0]) x = cand[j];
                cand[j] = cand[j - 1];
                cand[j - 1] = x;
            }
        for (int32_t i = 0; i < n && _careerNumPlayers < 20; i++) {
            const uint16_t before = _careerNumPlayers;
            if (ModApi_CallUpPlayer(cand[i].team, cand[i].slot) != 0)
                continue;
            Career_ShowAddInternationalPlayerDialog(2);
            if (_careerNumPlayers > before)
                added++;
        }
    }
    say("[golden] national squad: %d called up, %u of 20\n", (int)added, (unsigned)_careerNumPlayers);
    ContestMenuOnReturn();
    SetNationalSelectorButtons();
    return added > 0;
}

void CareerAutopilot_Tick(void)
{
    const Menu *menu;

    if (!s_save)
        return;
    menu = (const Menu *)_currentMenu;
    if (!s_started) {
        if (!Menu_IsMainMenu())
            return;
        start();                /* loads and runs a saved career; returns for a new one */
    }
    if (getenv("SWOS_GOLDEN_TRACE")) {  /* each new screen: its onInit and selection */
        static MenuVoidFn last;
        static int32_t listed;
        if (menu->onInit != last && listed < 4) {   /* the first few screens' visible entries */
            listed++;
            for (uint16_t i = 0; i < menu->numEntries; i++) {
                const MenuEntry *e = CalcMenuEntryAddress(i);
                if (!e->isInvisible && e->onSelect)
                    say("[golden-trace]   entry %u %p%s\n", (unsigned)i, (void *)(uintptr_t)e->onSelect,
                        e == menu->selectedEntry ? " (selected)" : "");
            }
        }
        if (menu->onInit != last) {
            last = menu->onInit;
            say("[golden-trace] tick %u screen %p entry %p season %u\n", (unsigned)_currentTick,
                (void *)(uintptr_t)menu->onInit,
                (void *)(uintptr_t)(menu->selectedEntry ? menu->selectedEntry->onSelect : 0),
                (unsigned)_currentSeasonShowing);
        }
    }
    {
        /* Test (SWOS_GOLDEN_SHOT_COMP=<competition id>): a screenshot of each new screen
         * while that competition is the one under way -- to look at it (a world's
         * play-off: world_playoff.c); SWOS_GOLDEN_SHOT_FROM=<n>: from the n-th season-end
         * hub on (a later season's screens). */
        static const char *shot = (const char *)1;
        static MenuVoidFn shotLast;
        static int32_t shots, shotFrom;
        extern DIYCompetitionEx _diyCompetitionStart;
        extern void Platform_RequestScreenshot(void);
        if (shot == (const char *)1) {
            const char *from = getenv("SWOS_GOLDEN_SHOT_FROM");
            shot = getenv("SWOS_GOLDEN_SHOT_COMP");
            shotFrom = from ? atoi(from) : 0;
        }
        if (s_viewShot && menu->onInit != shotLast) {
            s_viewShot--;
            shotLast = menu->onInit;
            Platform_RequestScreenshot();
            say("[golden] view screenshot (contest %d)\n", (int)(uint8_t)_diyCompetitionStart.base.field_2E);
        } else
        if (shot && (uint8_t)_diyCompetitionStart.base.field_2E == (uint8_t)atoi(shot) &&
            s_hubs > shotFrom && menu->onInit != shotLast && shots < 40) {
            shotLast = menu->onInit;
            shots++;
            Platform_RequestScreenshot();
            say("[golden] screenshot %d (tick %u)\n", (int)shots, (unsigned)_currentTick);
        }
    }
    _currentTick++;                     /* the clock the game's seeds read, once per pass */
    if (s_menuMode) {
        menu_path_step(menu);
        return;
    }
    if (s_choosing) {
        choose_step(menu);
        return;
    }
    if (bteam_shots(menu))              /* (test: SWOS_GOLDEN_SHOT_BTEAM) */
        return;
    if (hub_path(menu))                 /* (test: SWOS_GOLDEN_HUB_PATH) */
        return;
    if (numbers_test(menu))             /* (test: SWOS_GOLDEN_SHOT_NUMBERS) */
        return;
    _careerTeam.team.teamControls = 1;  /* the career club's matches are simulated */
    MenuSystem_LatchSeasonSkip();
    if (menu->onInit == ContestMenuInit) {
        extern void BTeam_TestRequests(void), SquadExtras_TestRequests(void);
        BTeam_TestRequests();           /* (test: SWOS_TEST_BTEAM's call-ups, at the first hub: bteam.c) */
        SquadExtras_TestRequests();     /* (test: SWOS_TEST_BUY_EXTRA, squad_extras.c) */
    }

    if (menu->onInit == CareerOverInit)
        at_hub();
    else if (menu->onInit == ContestMenuInit && view_world(menu)) {
    }
    else if (menu->onInit == SelectTeamsInit && view_world(menu)) {
    }
    else if (menu->onInit == ContestMenuInit && national_offer()) {
    }
    else if (menu->onInit == ContestMenuInit && national_callups()) {
    }
    else if (menu->onInit == ContestMenuInit) {
        /* The in-season hub: NEXT MATCH plays the next match day (PLAY MATCH, its
         * default, only opens the table). Test (SWOS_GOLDEN_VIEW_SLOT3=<n>): the first n
         * match days of slot 3 (the European cup) by PLAY MATCH, its table on screen --
         * with SWOS_GOLDEN_SHOT_COMP, to look at it. */
        static const char *view = (const char *)1;
        static int32_t viewed;
        extern uint8_t _teamSelectionKeyCache[];
        extern uint16_t _teamSelectionKeyCacheOffset;
        if (view == (const char *)1)
            view = getenv("SWOS_GOLDEN_VIEW_SLOT3");
        const char *from = getenv("SWOS_GOLDEN_SHOT_FROM");
        if (view && viewed < atoi(view) && s_hubs > (from ? atoi(from) : 0) &&
            ((TeamSelectionKeyCacheEntry *)(_teamSelectionKeyCache + _teamSelectionKeyCacheOffset))->compSlot == 3) {
            viewed++;
            s_viewShot = 2;             /* (the screens it opens: a screenshot each) */
            say("[golden] viewing slot 3 (%d)\n", (int)viewed);
            _shortFire = 1;             /* (its default: PLAY MATCH) */
        } else {
            /* Test (SWOS_GOLDEN_SHOT_HUB=<n>): a screenshot of the hub -- its fixture list --
             * the n-th time it is up (cal_label.c's labels, docs/MENUS.md section 11). */
            static const MenuEntryFn next[] = { NextMatch };
            static int32_t hubPasses;
            const char *hubShot = getenv("SWOS_GOLDEN_SHOT_HUB");
            if (hubShot && ++hubPasses == atoi(hubShot)) {
                extern void Platform_RequestScreenshot(void);
                Platform_RequestScreenshot();
                say("[golden] hub screenshot\n");
                return;
            }
            {   /* Test (SWOS_GOLDEN_SAVE_MID=<season>:<n>): the career saved as MIDSAVE.CAR at the n-th
                 * pass of that season's hub -- a save in the middle of a season, to go on from
                 * (--career-golden=MIDSAVE.CAR) and compare with the run that was not stopped */
                static const char *mid = (const char *)1;
                static int32_t midPasses, midDone;
                if (mid == (const char *)1)
                    mid = getenv("SWOS_GOLDEN_SAVE_MID");
                if (mid && !midDone && strchr(mid, ':') && (int32_t)_currentSeasonShowing == atoi(mid) &&
                    ++midPasses == atoi(strchr(mid, ':') + 1)) {
                    midDone = 1;
                    say(SaveJson_WriteCareer((uint8_t *)"MIDSAVE.CAR") == 0
                            ? "[golden] MIDSAVE.CAR: season %u, hub pass %d (tick %u)\n"
                            : "[golden] MIDSAVE.CAR not written (season %u, pass %d, tick %u)\n",
                        (unsigned)_currentSeasonShowing, (int)midPasses, (unsigned)_currentTick);
                }
            }
            select_first(next, 1);
            _shortFire = 1;
        }
    }
    else if (menu->onInit == WorldWinnersMenuInit && (s_winWait || s_winPage + 1 < WorldWinners_Pages())) {
        if (s_winWait)                  /* (SWOS_GOLDEN_VIEW_WINNERS: a page drawn, its screenshot taken) */
            s_winWait = 0;
        else {                          /* (then the next, a screenshot each) */
            s_winPage++;
            WorldWinnersNextPage();
            Platform_RequestScreenshot();
            s_winWait = 1;
            say("[golden] winners page %d\n", (int)s_winPage + 1);
        }
    }
    else if (menu->onInit == DIYLeagueTournamentInit && view_winners()) {
        s_winPage = 0;
        s_winWait = 1;
        s_viewShot = 1;
        say("[golden] viewing the season winners (contest %d)\n", (int)(uint8_t)_diyCompetitionStart.base.field_2E);
        CompetitionStatsWinners();
    }
    else if (menu->onInit == DIYLeagueTournamentInit && s_viewShot && !s_infoShown) {
        extern void ShowTournamentInfo(void);       /* (SWOS_GOLDEN_VIEW_SLOT3: its info screen once) */
        s_infoShown = 1;
        s_viewShot = 1;
        ShowTournamentInfo();
    }
    else if (menu->onInit == DIYLeagueTournamentInit) {
        /* The match-day screen: on with the fixtures (it opens on EXIT, which only
         * goes back to the hub); EXIT only once none of these is showing. */
        static const MenuEntryFn play[] = {
            (MenuEntryFn)DIYLeagueTournamentNextMatch, DIYLeagueTournamentViewResult,
            DIYLeagueTournamentResult, DIYLeagueTournamentMatch };
        select_first(play, (int32_t)(sizeof play / sizeof play[0]));
        _shortFire = 1;
    }
    else if (menu->onInit == JobOffersInit) {
        /* The first offer; with none showing, back out (the hub then says it is over). */
        /* (one considered before -- an old offer's row -- only where it can be accepted: the
         * season end's JOB OFFERS; in the season the screen is left as it was) */
        extern uint16_t _jobOffersFromRetired;
        static const MenuEntryFn offer[] = { JobOffersCommon, (MenuEntryFn)OldJobOffersCommon };
        select_first(offer, _jobOffersFromRetired ? 2 : 1);
        _shortFire = 1;
    }
    else if (menu->onInit == GotJobOfferInit) {
        static const MenuEntryFn accept[] = { (MenuEntryFn)AcceptJobOffer };
        if (select_first(accept, 1))
            say("[golden] sacked: took job offer %d\n", (int)++s_jobs);
        _shortFire = 1;
    }
    else if (menu->onInit == PlayMatchMenuInit) {
        /* A national coach's screen is laid out differently: its VIEW RESULT (the entry
         * PlayMatchSelected) is not the one selected at first, so select it and fire it now
         * -- the menu's press (_shortFire) AND player 1's (_pl1Fire): PlayMatchSelected goes
         * on only when it sees a player's fire, and this runs after the frame's controls
         * were read (menu_system.c), so _shortFire alone left a national career on this
         * screen for good (petarku's BRASIL, 2026-11-07). A club's stays as it was. */
        extern uint16_t _isNationalSelector;
        extern void PlayMatchSelected(void);
        int32_t i;
        if (_isNationalSelector != 0 && (i = entry_with((MenuEntryFn)PlayMatchSelected)) >= 0) {
            SetCurrentEntry((uint16_t)i);
            _shortFire = 1;
        }
        _pl1Fire = 1;
    }
    else
        _shortFire = 1;
}
