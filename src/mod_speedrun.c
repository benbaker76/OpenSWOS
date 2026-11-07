/*
 * mod_speedrun.c -- the speedrun mod (canonical, bin/mods/speedrun; docs/MODS.md §18): SWOS 2020's
 * SPEEDRUN (game.cpp challengeGame0Extras_OnSelect, checkSpeedrunWinningCondition), ported onto
 * the mod API. Both targets.
 *
 * MODES -> SPEEDRUN: eight targets, each with its best time. Pick one, then the teams (the
 * friendly's team screen: yours is the one you play, the other the CPU's or a friend's), and the
 * clock runs from kick-off -- a 3-minute match on a normal pitch -- until the target is met, which
 * ends the match, or the final whistle, which fails it (MATCH: win it):
 *   1 GOAL, 3 GOALS, 10 GOALS   your team scores that many
 *   HEADER                      your team scores a header
 *   TACKLE                      your team wins the ball with a good tackle
 *   YELLOW CARD, RED CARD       one of yours is booked / sent off
 *   MATCH                       your team wins the match
 * Then the time, the best, TRY AGAIN (the same teams) or EXIT.
 *
 * Times are FRAMES from kick-off (the match's own, so a run is the same however fast the machine
 * draws), shown in seconds at the gameplay style's rate -- SWOS 2020 kept wall-clock seconds. The
 * records are JSON of named fields (the owner: JSON is our format for data), in the save folder's
 * mods/speedrun/records.json:
 *   { "format": "openswos-speedrun", "version": 1,
 *     "records": [ { "target": "1 goal", "played": 3, "best_frames": 812, "last_frames": 940,
 *                    "best_teams": "ENGLAND v SCOTLAND" }, ... ] }
 * The Amiga parses no JSON (docs/DATA_BSON.md): it keeps them as a fixed binary record, the
 * save folder's mods/speedrun/RECORDS.BIN -- "OSWSPRN" + a version byte (1), a u16 count, then
 * per target its key[16], played, best and last frames (u32) and best_teams[48], big-endian.
 * Online (docs/MODS.md §33): when a mod offers the "online-records" service (SWOS United's,
 * SWOS 2020's leaderboards), each run is sent to it and the result screen shows its answer --
 *   speedrun.finished  {"target","key","done","frames","ms","style","yours","theirs","teams"}
 *                      (yours/theirs: each team's skill points, the sum of its sixteen
 *                      players' seven skills -- SWOS 2020 sets a run's difficulty by them)
 *   speedrun.line      {"target","style"}   a line for the result screen ("WORLD RANK 3")
 * never waiting on it. No such service: the records stay the player's own.
 *
 * "changes": "modes": its matches are its own -- never matters online.
 */
#include "mod_api.h"


#include "mjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void SetExitMenuFlag(void);

enum { T_GOALS, T_HEADER, T_TACKLE, T_YELLOW, T_RED, T_MATCH };
enum { kTargets = 8 };

static const struct {
    const char *name, *key;
    uint8_t kind, goals;
} kTarget[kTargets] = {
    { "1 GOAL", "1 goal", T_GOALS, 1 },       { "HEADER", "header", T_HEADER, 1 },
    { "3 GOALS", "3 goals", T_GOALS, 3 },     { "TACKLE", "tackle", T_TACKLE, 0 },
    { "10 GOALS", "10 goals", T_GOALS, 10 },  { "YELLOW CARD", "yellow card", T_YELLOW, 0 },
    { "MATCH", "match", T_MATCH, 0 },         { "RED CARD", "red card", T_RED, 0 },
};

typedef struct {
    uint32_t played, best, last;            /* frames; 0: none yet */
    char     bestTeams[48];
} Record;

static Record s_rec[kTargets];
static int    s_recRead;

/* ---- the records ---- */

#ifdef SWOS_SDL_BUILD
static void records_read(void)
{
    uint32_t size = 0;
    char *json = (char *)ModApi_LoadFile("speedrun", "records.json", &size);
    s_recRead = 1;
    if (!json)
        return;
    for (int32_t i = 0; i < 64; i++) {
        char q[64], key[24];
        snprintf(q, sizeof q, "$.records[%d].target", (int)i);
        if (mjson_get_string(json, (int)size, q, key, (int)sizeof key) < 0)
            break;
        for (int32_t t = 0; t < kTargets; t++) {
            if (strcmp(key, kTarget[t].key) != 0)
                continue;
            long v;     /* (whole numbers: mjson_get_long, which the Amiga has -- no doubles there) */
            snprintf(q, sizeof q, "$.records[%d].played", (int)i);
            if (mjson_get_long(json, (int)size, q, &v)) s_rec[t].played = (uint32_t)v;
            snprintf(q, sizeof q, "$.records[%d].best_frames", (int)i);
            if (mjson_get_long(json, (int)size, q, &v)) s_rec[t].best = (uint32_t)v;
            snprintf(q, sizeof q, "$.records[%d].last_frames", (int)i);
            if (mjson_get_long(json, (int)size, q, &v)) s_rec[t].last = (uint32_t)v;
            snprintf(q, sizeof q, "$.records[%d].best_teams", (int)i);
            mjson_get_string(json, (int)size, q, s_rec[t].bestTeams, (int)sizeof s_rec[t].bestTeams);
        }
    }
    free(json);
}

static void json_string(char *out, size_t cap, const char *s)   /* (quotes and backslashes escaped) */
{
    size_t n = 0;
    for (; *s && n + 2 < cap; s++) {
        if (*s == '"' || *s == '\\')
            out[n++] = '\\';
        out[n++] = *s;
    }
    out[n] = '\0';
}

static void records_save(void)
{
    char buf[4096];
    int n = snprintf(buf, sizeof buf, "{\n \"format\": \"openswos-speedrun\",\n \"version\": 1,\n \"records\": [\n");
    for (int32_t t = 0; t < kTargets && n < (int)sizeof buf; t++) {
        char teams[100];
        json_string(teams, sizeof teams, s_rec[t].bestTeams);
        n += snprintf(buf + n, sizeof buf - (size_t)n,
                      "  { \"target\": \"%s\", \"played\": %u, \"best_frames\": %u, \"last_frames\": %u, "
                      "\"best_teams\": \"%s\" }%s\n",
                      kTarget[t].key, (unsigned)s_rec[t].played, (unsigned)s_rec[t].best, (unsigned)s_rec[t].last,
                      teams, t + 1 < kTargets ? "," : "");
    }
    n += snprintf(buf + n, sizeof buf - (size_t)n, " ]\n}\n");
    if (ModApi_SaveFile("speedrun", "records.json", buf, (uint32_t)n) != 0)
        fprintf(stderr, "[speedrun] could not save the records\n");
}
#else
enum { kRecSize = 16 + 12 + 48 };

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static void records_read(void)
{
    uint32_t size = 0;
    const uint8_t *p = (const uint8_t *)ModApi_LoadFile("speedrun", "RECORDS.BIN", &size);
    s_recRead = 1;
    if (!p)
        return;
    if (size >= 10 && memcmp(p, "OSWSPRN", 7) == 0 && p[7] == 1) {
        const uint32_t count = ((uint32_t)p[8] << 8) | p[9];
        for (uint32_t i = 0; i < count && 10 + (i + 1) * kRecSize <= size; i++) {
            const uint8_t *r = p + 10 + i * kRecSize;
            char key[16];
            memcpy(key, r, 16);
            key[15] = 0;
            for (int32_t t = 0; t < kTargets; t++) {
                if (strcmp(key, kTarget[t].key) != 0)
                    continue;
                s_rec[t].played = be32(r + 16);
                s_rec[t].best = be32(r + 20);
                s_rec[t].last = be32(r + 24);
                memcpy(s_rec[t].bestTeams, r + 28, 48);
                s_rec[t].bestTeams[47] = 0;
            }
        }
    }
    free((void *)p);
}

static void records_save(void)
{
    static uint8_t buf[10 + kTargets * kRecSize];
    memset(buf, 0, sizeof buf);
    memcpy(buf, "OSWSPRN", 7);
    buf[7] = 1;
    buf[9] = kTargets;
    for (int32_t t = 0; t < kTargets; t++) {
        uint8_t *r = buf + 10 + t * kRecSize;
        memcpy(r, kTarget[t].key, strlen(kTarget[t].key));
        put32(r + 16, s_rec[t].played);
        put32(r + 20, s_rec[t].best);
        put32(r + 24, s_rec[t].last);
        memcpy(r + 28, s_rec[t].bestTeams, 47);
    }
    if (ModApi_SaveFile("speedrun", "RECORDS.BIN", buf, (uint32_t)sizeof buf) != 0)
        fprintf(stderr, "[speedrun] could not save the records\n");
}
#endif

static void time_text(char *out, size_t cap, uint32_t frames)
{
    if (!frames) {
        snprintf(out, cap, "--");
        return;
    }
    const uint32_t fps = (uint32_t)ModApi_FrameRate();
    const uint32_t cs = frames * 100 / fps;
    snprintf(out, cap, "%u.%02u", (unsigned)(cs / 100), (unsigned)(cs % 100));
}

/* ---- a run ---- */

static int32_t  s_target = -1, s_you;
static uint8_t  s_goals, s_done;
static uint32_t s_doneAt;
static int32_t  s_retry;

static void sr_event(const ModMatchEvent *e)
{
    if (!ModApi_ModMatch("speedrun") || s_done || s_target < 0)
        return;
    const uint8_t kind = kTarget[s_target].kind;
    int32_t met = 0;
    if (e->team == s_you) {
        if (e->kind == MODEVENT_GOAL || e->kind == MODEVENT_PENALTY_GOAL) {
            s_goals++;
            met = (kind == T_GOALS && s_goals >= kTarget[s_target].goals) ||
                  (kind == T_HEADER && (e->arg & MODGOAL_HEADER));
        }
        met = met || (kind == T_TACKLE && e->kind == MODEVENT_TACKLE) ||
              (kind == T_YELLOW && e->kind == MODEVENT_YELLOW_CARD) ||
              (kind == T_RED && (e->kind == MODEVENT_RED_CARD || e->kind == MODEVENT_SECOND_YELLOW));
    } else if (e->kind == MODEVENT_OWN_GOAL && e->team == s_you) {
        s_goals++;
    }
    if (met) {
        s_done = 1;
        s_doneAt = e->frame;
        fprintf(stderr, "[speedrun] %s in %u frames\n", kTarget[s_target].key, (unsigned)e->frame);
        ModApi_EndMatch();
    }
}

static void sr_ended(const ModMatchResult *r)
{
    if (!r->modMatch || !ModApi_ModMatch("speedrun") || s_done || s_target < 0)
        return;
    if (kTarget[s_target].kind == T_MATCH && !r->quit && r->winner == s_you) {
        s_done = 1;
        s_doneAt = r->frames;
    }
}

static char s_resultTeams[48];
static int32_t s_test;                  /* SWOS_MOD_MATCH_TEST=speedrun:<n>: fixed teams, no screens */

/* (each team's skill points, SWOS 2020's getTeamSkillPoints: its sixteen players' seven skills) */
static int32_t skill_points(int32_t team)
{
    int32_t sum = 0;
    for (int32_t slot = 0; slot < 16; slot++) {
        ModTeamPlayer p;
        if (ModApi_ChosenTeamPlayer(team, slot, &p))
            sum += p.passing + p.shooting + p.heading + p.tackling + p.control + p.speed + p.finishing;
    }
    return sum;
}

static int32_t s_onlineAsked;           /* the result screen's line asked for since the run */
static char    s_onlineLine[48];

/* A run, to the online records (queued: the service sends it in the background). */
static void online_finished(int32_t target)
{
    extern int _gGameplayStyle;
    extern const char *Gameplay_StyleName(int style);
    char args[320], teams[100];
    const uint32_t fps = (uint32_t)ModApi_FrameRate();
    snprintf(args, sizeof args,
             "{\"target\":%d,\"key\":\"%s\",\"done\":%s,\"frames\":%u,\"ms\":%u,\"style\":\"%s\","
             "\"yours\":%d,\"theirs\":%d,\"teams\":\"%s\"}",
             (int)target, kTarget[target].key, s_done ? "true" : "false", (unsigned)(s_done ? s_doneAt : 0),
             (unsigned)(s_done && fps ? s_doneAt / fps * 1000u + s_doneAt % fps * 1000u / fps : 0),
             Gameplay_StyleName(_gGameplayStyle), (int)skill_points(s_you), (int)skill_points(1 - s_you),
             ModApi_JsonEscape(s_resultTeams, teams, sizeof teams));
    ModApi_CallService("online-records", "speedrun.finished", args, NULL, 0);
    s_onlineAsked = 0;
    s_onlineLine[0] = 0;
}

/* the run: the match, then the result screen, again while TRY AGAIN */
static void run(int32_t target, int32_t again)
{
    s_target = target;
    for (;;) {
        if (!s_test && (again ? !ModApi_ChosenAgain() : !ModApi_ChooseTeams()))
            return;
        again = 1;
        s_you = s_test ? 0 : ModApi_ChosenIsPlayer(0) ? 0 : ModApi_ChosenIsPlayer(1) ? 1 : -1;
        if (s_you < 0) {
            fprintf(stderr, "[speedrun] no team is yours: pick one to play\n");
            return;
        }
        snprintf(s_resultTeams, sizeof s_resultTeams, "%s v %s", ModApi_ChosenName(s_you), ModApi_ChosenName(1 - s_you));
        ModMatchSetup setup;
        ModMatchResult r;
        ModApi_DefaultSetup(&setup);
        setup.chosen = !s_test;
        if (s_test) {                       /* (the test: Norway, the CPU's but "yours", v New Zealand) */
            setup.team[0] = 0x5020;
            setup.team[1] = 0x5502;
            snprintf(s_resultTeams, sizeof s_resultTeams, "TEST");
        }
        setup.length = 0;
        setup.pitchType = 4;                /* (normal) */
        setup.subsMin = 3;
        setup.subsMax = 5;
        setup.autoReplays = 0;
        setup.replayMenu = 0;
        setup.kickOff = (int8_t)s_you;
        setup.name = "speedrun";
        s_goals = s_done = 0;
        s_doneAt = 0;
        if (ModApi_PlayMatch("speedrun", &setup, &r) != 0)
            return;
        if (!s_recRead)
            records_read();
        Record *rec = &s_rec[target];
        rec->played++;
        rec->last = s_done ? s_doneAt : 0;
        if (s_done && (!rec->best || s_doneAt < rec->best)) {
            rec->best = s_doneAt;
            snprintf(rec->bestTeams, sizeof rec->bestTeams, "%s", s_resultTeams);
        }
        records_save();
        if (!s_test)
            online_finished(target);
        if (s_test) {
            fprintf(stdout, "SPEEDRUN TEST %s: %s, %u frames, best %u, played %u\n", kTarget[target].key,
                    s_done ? "PASS" : "FAIL", (unsigned)s_doneAt, (unsigned)rec->best, (unsigned)rec->played);
            fflush(stdout);
#ifdef SWOS_SDL_BUILD
            exit(s_done ? 0 : 1);
#endif
        }
        s_retry = 0;
        ModApi_ShowMenu("speedrun/speedrunResult");
        if (!s_retry)
            return;
    }
}

/* ---- the targets' screen ---- */

enum { kRows = 8, M_header = 0, M_subTitle, M_label0, M_note = M_label0 + 2 * kRows, M_exit };
static char s_best[kTargets][12];

static void menu_refresh(void)
{
    if (!s_recRead)
        records_read();
    ModApi_MenuText(M_subTitle, "PICK A TARGET: YOUR BEST TIMES");
    for (int32_t i = 0; i < kRows; i++) {
        time_text(s_best[i], sizeof s_best[i], s_rec[i].best);
        ModApi_MenuText(M_label0 + 2 * i, kTarget[i].name);
        ModApi_MenuText(M_label0 + 2 * i + 1, s_best[i]);
        ModApi_MenuVisible(M_label0 + 2 * i, 1);
        ModApi_MenuVisible(M_label0 + 2 * i + 1, 1);
    }
    ModApi_MenuText(M_note, "THEN YOUR TEAM AND THE OPPONENT");
}

static void menu_init(void)
{
    for (int32_t i = 0; i < kRows; i++) {
        const int32_t v = M_label0 + 2 * i + 1;
        ModApi_MenuNav(v, i ? v - 2 : -1, i + 1 < kRows ? v + 2 : M_exit);
    }
    ModApi_MenuNav(M_exit, M_label0 + 2 * kRows - 1, -1);
    menu_refresh();
}

static void target0(void) { run(0, 0); }
static void target1(void) { run(1, 0); }
static void target2(void) { run(2, 0); }
static void target3(void) { run(3, 0); }
static void target4(void) { run(4, 0); }
static void target5(void) { run(5, 0); }
static void target6(void) { run(6, 0); }
static void target7(void) { run(7, 0); }

/* ---- the result screen ---- */

static char s_time[16], s_bestNow[16], s_title[40];

static void result_refresh(void)
{
    const Record *rec = &s_rec[s_target];
    time_text(s_time, sizeof s_time, s_done ? s_doneAt : 0);
    time_text(s_bestNow, sizeof s_bestNow, rec->best);
    snprintf(s_title, sizeof s_title, "%s: %s", kTarget[s_target].name,
             !s_done ? "NOT THIS TIME" : rec->best == s_doneAt ? "A NEW BEST!" : "DONE");
    ModApi_MenuText(M_subTitle, s_title);
    static const char *const kLabels[4] = { "YOUR TIME", "YOUR BEST", "PLAYED", "" };
    static char played[12];
    snprintf(played, sizeof played, "%u", (unsigned)rec->played);
    const char *values[4] = { s_time, s_bestNow, played, "TRY AGAIN" };
    for (int32_t i = 0; i < kRows; i++) {
        ModApi_MenuVisible(M_label0 + 2 * i, i < 3);
        ModApi_MenuVisible(M_label0 + 2 * i + 1, i < 4);
        if (i < 4) {
            ModApi_MenuText(M_label0 + 2 * i, kLabels[i]);
            ModApi_MenuText(M_label0 + 2 * i + 1, values[i]);
        }
    }
    if (!s_onlineAsked) {   /* the online line, once it has come (each frame until then) */
        extern int _gGameplayStyle;
        extern const char *Gameplay_StyleName(int style);
        char args[96];
        snprintf(args, sizeof args, "{\"target\":%d,\"style\":\"%s\"}", (int)s_target,
                 Gameplay_StyleName(_gGameplayStyle));
        const int32_t r = ModApi_CallService("online-records", "speedrun.line", args, s_onlineLine,
                                             sizeof s_onlineLine);
        s_onlineAsked = r != 1;
        if (r != 0)
            s_onlineLine[0] = 0;
    }
    ModApi_MenuText(M_note, s_onlineLine[0] ? s_onlineLine : s_resultTeams);
}

static void result_init(void)
{
    const int32_t again = M_label0 + 2 * 3 + 1;
    ModApi_MenuNav(again, -1, M_exit);
    ModApi_MenuNav(M_exit, again, -1);
    ModApi_MenuSelect(again);
    result_refresh();
}

static void result3(void)       /* TRY AGAIN */
{
    s_retry = 1;
    SetExitMenuFlag();
}
static void result_none(void) {}

static void sr_exit(void) { SetExitMenuFlag(); }
static void sr_open(void) { ModApi_ShowMenu("speedrun/speedrunMenu"); }

static void sr_init(void)
{
    static const ModHandler kHandlers[] = {
        { "menuInit", menu_init },     { "menuDraw", menu_refresh },
        { "target0", target0 }, { "target1", target1 }, { "target2", target2 }, { "target3", target3 },
        { "target4", target4 }, { "target5", target5 }, { "target6", target6 }, { "target7", target7 },
        { "resultInit", result_init }, { "resultDraw", result_refresh },
        { "result0", result_none }, { "result1", result_none }, { "result2", result_none }, { "result3", result3 },
        { "result4", result_none }, { "result5", result_none }, { "result6", result_none }, { "result7", result_none },
        { "exit", sr_exit },
    };
    ModApi_RegisterHandlers("speedrun", kHandlers, (int32_t)(sizeof kHandlers / sizeof kHandlers[0]));
    ModApi_AddEntry(MODPOINT_MODES, "speedrun", "speedrun", sr_open);
}

#ifdef SWOS_SDL_BUILD
/* (src/mod_match.c ModMatch_SelfTest: SWOS_MOD_MATCH_TEST=speedrun:<n>) */
void ModSpeedrun_Test(int32_t target)
{
    if (target < 0 || target >= kTargets)
        exit(2);
    s_test = 1;
    run(target, 0);
}
#endif

const ModDef ModDef_speedrun = {
    .slug = "speedrun",
    .init = sr_init,
    .matchEvent = sr_event,
    .matchEnded = sr_ended,
};

