/*
 * mod_arcade.c -- the arcade mod: the arcade's GAME MODES and their rules (docs/ARCADE.md "Game
 * modes -- the arcade mod", docs/MODS.md §36). SDL builds. The owner's decision, 2026-11-12: the cabinet (the
 * boot gate, the coins, the attract reels, the F2 service menu: src/arcade.c) stays in the game;
 * how an arcade match is PLAYED is this mod's, so it can be played in an ordinary friendly too.
 *
 * A mode is a row of k_modes: its name and its rules -- a time limit, a golden goal (the first goal
 * ends the match), a penalty shoot-out on a draw at the limit, the goals' instant replays -- and
 * which of them the settings may switch off. GOLDEN GOAL is the first and so far the only one.
 *
 * Where the rules are decided (friendlyRules): a friendly the mod is on for -- the FRIENDLY menu's,
 * an online single friendly on the HOST, the cabinet's (which hands its F2 settings over through
 * the "arcade" service, ARM, first). They are written into the mod's words of the MATCH STATE, and
 * matchStep plays them from there on every frame simulated: a rollback takes them back with the
 * rest of the match, the joiner of an online match gets the host's in the kick-off state, a .SRP
 * keeps them. Nothing of a match is kept anywhere else.
 *
 * "changes": "gameplay": online, both players need the mod alike, and the host's settings are the
 * match's. A plain match never meets the words (they are zero, and absent from the state while
 * the mod is off), so it plays as it always did.
 */
#include "mod_api.h"

#ifdef SWOS_SDL_BUILD

#include "mjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLUG "arcade"

/* ---- the modes ---- */
enum {
    RULE_TIME_LIMIT  = 1,   /* the match ends at its length (game_seconds) */
    RULE_GOLDEN_GOAL = 2,   /* the first goal ends it (once its celebration and replay are over) */
    RULE_PENALTIES   = 4,   /* level at the limit: a penalty shoot-out */
    RULE_GOAL_REPLAY = 8,   /* the goals' instant replays */
};

typedef struct {
    const char *name;       /* in capitals: the F2 menu, the stadium screen, the high scores;
                             * also its "mode" choice in bin/mods/arcade/mod.json */
    uint32_t    rules;      /* RULE_*: how it plays */
    uint32_t    settable;   /* the RULE_* of those a setting may switch off (the rest are fixed) */
    /* Its own rule, each frame after the common ones (NULL: none): w its words (W_*), frames
     * the frames played from kick-off. Changes the match only through the ModApi. */
    void      (*step)(uint32_t *w, uint32_t frames);
} ArcadeMode;

/* TO ADD A MODE: a row here, its name in mod.json's "mode" choices, and (for a rule none of the
 * RULE_* is) a step of its own -- its state in the words W_MODE_0.. (docs/ARCADE.md). */
static const ArcadeMode k_modes[] = {
    { "GOLDEN GOAL", RULE_TIME_LIMIT | RULE_GOLDEN_GOAL | RULE_PENALTIES | RULE_GOAL_REPLAY,
      RULE_GOLDEN_GOAL | RULE_PENALTIES | RULE_GOAL_REPLAY, NULL },
};
#define MODE_COUNT ((int32_t)(sizeof k_modes / sizeof k_modes[0]))

/* An arcade second is a real second: the match's frames a second by its gameplay style (Amiga 50,
 * XBOX 60, DOS 70 -- ModApi_FrameRate). Online the joiner plays the host's style, and the limit
 * travels as frames in the words, so both count the same. (Until 2026-11-12 it was 50 frames
 * whatever the style: a "45 s" match under DOS gameplay lasted 32 s.) */
static uint32_t frames_per_second(void)
{
    const int32_t fps = ModApi_FrameRate();
    return fps > 0 ? (uint32_t)fps : 50u;
}
#define SECONDS_MIN       10
#define SECONDS_MAX       600

/* ---- its words of the match state (ModApi_MatchState) ---- */
enum {
    W_ACTIVE,               /* 1: this match plays by the mod's rules */
    W_MODE,                 /* k_modes index */
    W_RULES,                /* RULE_* in force */
    W_LIMIT,                /* the time limit, frames */
    W_PLAYED,               /* frames played from kick-off (paused while a golden goal resolves) */
    W_STATE,                /* ST_* */
    W_GOALS,                /* both teams' goals at kick-off */
    W_MODE_0,               /* (a mode's own, from here) */
    W_COUNT = 8
};
enum {
    ST_STARTED   = 1,       /* kicked off: the time runs */
    ST_GOLDEN    = 2,       /* a golden goal: ends once its celebration and replay are over */
    ST_SEEN      = 4,       /* ... and its celebration has been seen running */
    ST_OVER      = 8,       /* the rules have ended the match (or sent it to penalties) */
};

typedef struct {
    int32_t mode, seconds;
    uint32_t rules;         /* RULE_* as the settings have them (the mode masks them) */
} ArcadeRules;

/* The cabinet's rules for its next match (the service's ARM): one match, then the settings'. */
static ArcadeRules s_pending;
static int32_t     s_pendingSet;
/* The last match the rules played, for the cabinet's TIME (the service's PLAYED): not the match's
 * state, only what it was when it ended. */
static uint32_t s_lastPlayed;
static int32_t  s_lastActive;

static int32_t mode_from_name(const char *name)
{
    for (int32_t i = 0; name && i < MODE_COUNT; i++) {
        const char *a = name, *b = k_modes[i].name;
        while (*a && *b && (*a == *b || (*a >= 'a' && *a <= 'z' && *a - 32 == *b)))
            a++, b++;
        if (!*a && !*b)
            return i;
    }
    return -1;
}

static int32_t clamp_seconds(int32_t s)
{
    return s < SECONDS_MIN ? SECONDS_MIN : s > SECONDS_MAX ? SECONDS_MAX : s;
}

/* The player's settings ([mod.arcade] in SWOS.ini, the mod's page). 0: not in friendlies. */
static int32_t settings_rules(ArcadeRules *r)
{
    if (!ModApi_SettingInt(SLUG, "friendlies", 1))
        return 0;
    r->mode = mode_from_name(ModApi_Setting(SLUG, "mode", k_modes[0].name));
    if (r->mode < 0)
        r->mode = 0;
    r->seconds = clamp_seconds(ModApi_SettingInt(SLUG, "game_seconds", 45));
    r->rules = RULE_TIME_LIMIT;
    if (ModApi_SettingInt(SLUG, "golden_goal", 1))       r->rules |= RULE_GOLDEN_GOAL;
    if (ModApi_SettingInt(SLUG, "penalties_on_draw", 1)) r->rules |= RULE_PENALTIES;
    if (ModApi_SettingInt(SLUG, "goal_replay", 1))       r->rules |= RULE_GOAL_REPLAY;
    return 1;
}

/* ---- the game asks ---- */

static int32_t arcade_friendly_rules(ModFriendlyRules *out)
{
    int32_t n;
    uint32_t *w = ModApi_MatchState(SLUG, &n);
    ArcadeRules r;
    if (!w || n < W_COUNT)
        return 0;
    if (ModApi_AnyModMatch() && !ModApi_ModMatch(SLUG))
        return 0;                               /* (another mod's match: its own rules) */
    if (s_pendingSet) {
        r = s_pending;
        s_pendingSet = 0;
    } else if (!settings_rules(&r)) {
        return 0;
    }
    const ArcadeMode *m = &k_modes[r.mode];
    memset(w, 0, (size_t)n * sizeof w[0]);
    w[W_ACTIVE] = 1;
    w[W_MODE] = (uint32_t)r.mode;
    w[W_RULES] = (m->rules & ~m->settable) | (r.rules & m->rules & m->settable);
    w[W_LIMIT] = (uint32_t)clamp_seconds(r.seconds) * frames_per_second();
    /* the slowest clock: at 45 s, or even 240, the engine's half time is still minutes away */
    out->slowClock = (uint8_t)((w[W_RULES] & RULE_TIME_LIMIT) != 0);
    out->autoReplays = (int8_t)((w[W_RULES] & RULE_GOAL_REPLAY) != 0);
    fprintf(stderr, "[arcade] %s: %d s, golden goal %s, penalties on a draw %s, goal replays %s\n", m->name,
            (int)clamp_seconds(r.seconds), (w[W_RULES] & RULE_GOLDEN_GOAL) ? "on" : "off",
            (w[W_RULES] & RULE_PENALTIES) ? "on" : "off", (w[W_RULES] & RULE_GOAL_REPLAY) ? "on" : "off");
    return 1;
}

static const char *arcade_match_name(void)
{
    uint32_t *w = ModApi_MatchState(SLUG, NULL);
    if (!w || !w[W_ACTIVE])
        return NULL;
    return k_modes[w[W_MODE] < (uint32_t)MODE_COUNT ? w[W_MODE] : 0].name;
}

/* Each frame simulated (re-simulations too): everything it keeps is in the words. */
static void arcade_step(void)
{
    uint32_t *w = ModApi_MatchState(SLUG, NULL);
    ModMatchPhase ph;
    int16_t g[2];
    if (!w || !w[W_ACTIVE])
        return;
    s_lastActive = 1;
    s_lastPlayed = w[W_PLAYED];
    if ((w[W_STATE] & ST_OVER) || !ModApi_MatchPhase(&ph) || ph.shootout)
        return;                                 /* (a shoot-out runs and ends itself) */
    ModApi_Score(g);
    const uint32_t goals = (uint32_t)(g[0] + g[1]);

    if (!(w[W_STATE] & ST_STARTED)) {
        if (!ph.inPlay)
            return;                             /* the time starts at the kick-off */
        w[W_STATE] |= ST_STARTED;
        w[W_PLAYED] = 0;
        w[W_GOALS] = goals;
    }
    /* A golden goal: the match is not cut at once -- the goal's celebration runs, and its replay
     * if the replays are on; it ends when both are over. The scorer is ahead: no penalties. The
     * time stands still meanwhile. */
    if ((w[W_RULES] & RULE_GOLDEN_GOAL) && !(w[W_STATE] & ST_GOLDEN) && goals != w[W_GOALS])
        w[W_STATE] |= ST_GOLDEN;
    if (w[W_STATE] & ST_GOLDEN) {
        if (ph.goalCelebration)
            w[W_STATE] |= ST_SEEN;
        if ((w[W_STATE] & ST_SEEN) && !ph.goalCelebration && !ph.replay) {
            w[W_STATE] |= ST_OVER;
            ModApi_EndMatch();
        }
        return;
    }

    const ArcadeMode *m = &k_modes[w[W_MODE] < (uint32_t)MODE_COUNT ? w[W_MODE] : 0];
    if (m->step)
        m->step(w, w[W_PLAYED]);
    if (!(w[W_RULES] & RULE_TIME_LIMIT) || (w[W_STATE] & ST_OVER))
        return;
    w[W_PLAYED]++;
    s_lastPlayed = w[W_PLAYED];
    if (w[W_PLAYED] < w[W_LIMIT])
        return;
    /* Time: level goes to penalties (when on), else it ends on the score. */
    w[W_STATE] |= ST_OVER;
    if (g[0] == g[1] && (w[W_RULES] & RULE_PENALTIES))
        ModApi_StartShootout();
    else
        ModApi_EndMatch();
}

/* ---- the "arcade" service: what the cabinet (src/arcade.c) asks ----
 *   modes                 the modes' names, one a line, in their order (the F2 menu's MODE)
 *   arm   {"mode":"GOLDEN GOAL","game_seconds":45,"golden_goal":true,
 *          "penalties_on_draw":true,"goal_replay":true}
 *                         the next friendly plays by these (the operator's F2 settings), once
 *   played                {"active":1,"frames":N,"seconds":S}: the last match's rules, how long it ran */
static int32_t args_bool(const char *args, const char *path, int32_t fallback)
{
    int b = 0;
    return mjson_get_bool(args, (int)strlen(args), path, &b) ? (b != 0) : fallback;
}

static int32_t arcade_service(const char *method, const char *args, char *out, uint32_t cap)
{
    if (!strcmp(method, "modes")) {
        uint32_t n = 0;
        for (int32_t i = 0; out && i < MODE_COUNT && n < cap; i++)
            n += (uint32_t)snprintf(out + n, cap - n, "%s\n", k_modes[i].name);
        return 0;
    }
    if (!strcmp(method, "arm")) {
        char name[48];
        double v = 45;
        ArcadeRules r;
        if (mjson_get_string(args, (int)strlen(args), "$.mode", name, (int)sizeof name) < 0)
            name[0] = 0;
        r.mode = mode_from_name(name);
        if (r.mode < 0)
            r.mode = 0;
        mjson_get_number(args, (int)strlen(args), "$.game_seconds", &v);
        r.seconds = clamp_seconds((int32_t)v);
        r.rules = RULE_TIME_LIMIT;
        if (args_bool(args, "$.golden_goal", 1))       r.rules |= RULE_GOLDEN_GOAL;
        if (args_bool(args, "$.penalties_on_draw", 1)) r.rules |= RULE_PENALTIES;
        if (args_bool(args, "$.goal_replay", 1))       r.rules |= RULE_GOAL_REPLAY;
        s_pending = r;
        s_pendingSet = 1;
        s_lastActive = 0;
        s_lastPlayed = 0;
        return 0;
    }
    if (!strcmp(method, "played")) {
        if (out && cap)
            snprintf(out, cap, "{\"active\":%d,\"frames\":%u,\"seconds\":%u}", (int)s_lastActive,
                     (unsigned)s_lastPlayed, (unsigned)(s_lastPlayed / frames_per_second()));
        return 0;
    }
    return -1;
}

static void arcade_init(void)
{
    ModApi_RegisterService(SLUG, "arcade", arcade_service);
}

/* ---- the self-test: SWOS_MOD_MATCH_TEST=arcade[:seconds] (src/mod_match.c ModMatch_SelfTest) --
 * At the first main menu, one friendly by the GOLDEN GOAL rules, CPU v CPU, headless (with
 * SWOS_HARNESS_TURBO it runs as fast as it can), then the game quits: PASS when the rules decided
 * it -- a golden goal (one goal, the match over by the mod) or, level at the time limit, a
 * shoot-out with a winner -- within the limit. A short limit (arcade:10) all but makes it
 * penalties. */
void ModArcade_Test(int32_t seconds)
{
    ModMatchSetup setup;
    ModMatchResult r;
    if (seconds <= 0)
        seconds = 45;
    ModApi_DefaultSetup(&setup);
    setup.team[0] = 0x5020;                     /* Norway */
    setup.team[1] = 0x5502;                     /* New Zealand */
    setup.stadium = 0;
    setup.replayMenu = 0;
    setup.name = NULL;                          /* (the rules' name: GOLDEN GOAL) */
    /* (through the service, as the cabinet arms its matches: arc_arm_rules) */
    char args[160], played[64];
    snprintf(args, sizeof args, "{\"mode\":\"golden goal\",\"game_seconds\":%d,\"golden_goal\":true,"
             "\"penalties_on_draw\":true,\"goal_replay\":true}", (int)seconds);
    if (ModApi_CallService("arcade", "arm", args, NULL, 0) != 0) {
        fprintf(stdout, "ARCADE MATCH TEST FAIL (no arcade service)\n");
        exit(1);
    }
    const int32_t rc = ModApi_PlayMatch(SLUG, &setup, &r);
    const uint32_t limit = (uint32_t)clamp_seconds(seconds) * frames_per_second();
    ModApi_CallService("arcade", "played", NULL, played, sizeof played);
    fprintf(stderr, "[arcade] test: the service says %s\n", played);
    const int32_t golden = r.goals[0] + r.goals[1] == 1 && r.endedByMod;
    const int32_t shootout = r.goals[0] == r.goals[1] && r.penaltyGoals[0] != r.penaltyGoals[1];
    const int32_t ok = rc == 0 && s_lastActive && s_lastPlayed > 0 && s_lastPlayed <= limit && r.winner >= 0 &&
                       (golden || (shootout && s_lastPlayed == limit));
    fprintf(stderr, "[arcade] test: rc %d, %d-%d (pens %d-%d), winner %d, minute %d, %u of %u frames played by "
            "the rules, ended by the mod %d\n", (int)rc, (int)r.goals[0], (int)r.goals[1], (int)r.penaltyGoals[0],
            (int)r.penaltyGoals[1], (int)r.winner, (int)r.minute, (unsigned)s_lastPlayed, (unsigned)limit,
            (int)r.endedByMod);
    fprintf(stdout, "ARCADE MATCH TEST (%s) %s\n", golden ? "golden goal" : shootout ? "penalties" : "undecided",
            ok ? "PASS" : "FAIL");
    fflush(stdout);
    exit(ok ? 0 : 1);
}

const ModDef ModDef_arcade = {
    .slug = SLUG,
    .init = arcade_init,
    .matchWords = W_COUNT,
    .friendlyRules = arcade_friendly_rules,
    .matchName = arcade_match_name,
    .matchStep = arcade_step,
};

#endif /* SWOS_SDL_BUILD */
