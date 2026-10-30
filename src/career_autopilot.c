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
#include "menu_system.h"
#include "game_types.h"
#include "save_format_json.h"
#include "swos_io.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
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
extern void CareerMenu(void);                   /* menus.c */
extern void JobOffersMenu(void);
extern void JobOffersMenuRetired(void);
extern void JobOffersInit(void);
extern void JobOffersCommon(MenuEntry *);       /* job_offers.c: an offer row, opens _gotJobOfferMenu */
extern void GotJobOfferInit(void);
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
static int32_t     s_infoShown;
static int32_t     s_viewShot;      /* (SWOS_GOLDEN_VIEW_SLOT3: screens left to shoot) */

/* --career-golden=new:<continent>/<country>/<division>/<club>: a new career, the club
 * picked through CAREER -> CHOOSE TEAM AND START CAREER by the rows' labels (the picker
 * uses one handler for every row, so only the label tells them apart). */
#define NEW_PATH_MAX 8
static char        s_pathBuf[256];
static const char *s_path[NEW_PATH_MAX];
static int32_t     s_pathN, s_pathK;
static int32_t     s_choosing;      /* still picking the club */
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
        if (strncmp(s_save, "new:", 4) == 0) {
            char *p = s_pathBuf;
            snprintf(s_pathBuf, sizeof s_pathBuf, "%s", s_save + 4);
            for (s_pathN = 0; *p && s_pathN < NEW_PATH_MAX; s_pathN++) {
                s_path[s_pathN] = p;
                while (*p && *p != '/') p++;
                if (*p) *p++ = '\0';
            }
            return s_pathN > 0 ? 1 : -1;
        }
        return s_save[0] ? 1 : -1;
    }
    if (strncmp(a, "--golden-seasons=", 17) == 0) {
        s_seasons = atoi(a + 17);
        return s_seasons >= 1 ? 1 : -1;
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
    if (menu->onInit == SWOSMainMenuInit) {
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
    if (s_pathN > 0) {
        say("[golden] new career, %d season(s) (SWOSMainMenuInit at %p)\n", (int)s_seasons,
            (void *)(uintptr_t)SWOSMainMenuInit);
        s_choosing = 1;         /* the main menu's passes now pick the club */
        return;
    }
    say("[golden] loading %s/%s, %d season(s) (SWOSMainMenuInit at %p)\n", s_outDir, s_save,
        (int)s_seasons, (void *)(uintptr_t)SWOSMainMenuInit);
    if (SaveJson_LoadCareer((uint8_t *)s_save, NULL, 0) != 0) {
        say("[golden] cannot load %s/%s\n", s_outDir, s_save);
        exit(2);
    }
    pin_random();
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

void CareerAutopilot_Tick(void)
{
    const Menu *menu;

    if (!s_save)
        return;
    menu = (const Menu *)_currentMenu;
    if (!s_started) {
        if (menu->onInit != SWOSMainMenuInit)
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
    if (s_choosing) {
        choose_step(menu);
        return;
    }
    _careerTeam.team.teamControls = 1;  /* the career club's matches are simulated */
    MenuSystem_LatchSeasonSkip();

    if (menu->onInit == CareerOverInit)
        at_hub();
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
            static const MenuEntryFn next[] = { NextMatch };
            select_first(next, 1);
            _shortFire = 1;
        }
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
        static const MenuEntryFn offer[] = { JobOffersCommon };
        select_first(offer, 1);
        _shortFire = 1;
    }
    else if (menu->onInit == GotJobOfferInit) {
        static const MenuEntryFn accept[] = { (MenuEntryFn)AcceptJobOffer };
        if (select_first(accept, 1))
            say("[golden] sacked: took job offer %d\n", (int)++s_jobs);
        _shortFire = 1;
    }
    else if (menu->onInit == PlayMatchMenuInit)
        _pl1Fire = 1;
    else
        _shortFire = 1;
}
