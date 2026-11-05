/*
 * mod_challenges.c -- the challenges mod (canonical, bin/mods/challenges; docs/MODS.md §18):
 * SWOS 2020's scenario mode (game.cpp ScenarioGame), ported onto the mod API, its challenges in
 * a JSON format of OpenSWOS's own (the owner: JSON is our format for data -- not challenges.ini,
 * not .SCN). SDL builds.
 *
 * MODES -> CHALLENGES: the challenges, each OPEN, DONE or LOCKED (locked while an easier one is
 * not done, as SWOS 2020 locks them). One: where the match stands and what wins it, PLAY; then
 * the match from there -- your team is player 1's, the other the CPU's -- and YOU BEAT THE
 * CHALLENGE or NOT THIS TIME, QUICK RETRY or EXIT.
 *
 * A challenge (data_json/challenges/<any>.json in this mod -- or in any mod that is on: a
 * challenge pack is a mod of such files):
 *   { "format": "openswos-challenges", "version": 1, "category": "CLASSICS",
 *     "challenges": [ {
 *       "id": "ten-men", "name": "TEN MEN", "difficulty": 3,          1 (easiest) .. 5
 *       "about": "1-1 AND YOUR NUMBER 5 SENT OFF. WIN WITH TEN.",
 *       "teams": [ { "uuid": "...", "name": "MANCHESTER UTD" }, { ... } ],   database teams by uuid
 *       "yours": 0,                                                    which team you play
 *       "score": [1, 1], "minute": 60,                                 where it stands (minute 0-89)
 *       "sent_off": [[5], []],                                         shirt numbers already off
 *       "extra_time": false, "penalties": false, "length": 0,          (optional)
 *       "win": "win" | "draw" (at least) | "exact", "win_score": [2, 2]   (exact: yours, theirs)
 *     } ] }
 * Progress: save/mods/challenges/progress.json, named fields:
 *   { "format": "openswos-challenges-progress", "version": 1,
 *     "challenges": [ { "id": "ten-men", "completed": true, "attempts": 4 } ] }
 * Not yet (docs/MODS.md §18): golden / silver goal, a starting event (a free kick, a corner),
 * minutes from extra time, SWOS 2020's own challenge files as a DLC pack (its teams are others').
 *
 * "changes": "modes": its matches are its own -- never matters online.
 */
#include "mod_api.h"

#ifdef SWOS_SDL_BUILD

#include "mjson.h"
#include "mods.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void SetExitMenuFlag(void);

enum { kMax = 32, kRows = 8 };
enum { W_WIN, W_DRAW, W_EXACT };

typedef struct {
    char     id[40], name[32], about[96], teamName[2][24], teamUuid[2][40];
    int8_t   difficulty, yours, score[2], winScore[2];
    uint8_t  win, extraTime, penalties, length;
    int16_t  minute;
    uint32_t sentOff[2];
    /* progress */
    uint8_t  completed;
    uint32_t attempts;
} Challenge;

static Challenge s_ch[kMax];
static int32_t   s_count, s_loaded;

/* ---- reading ---- */

static void upper(char *s)
{
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'z')
            *s = (char)(*s - 32);
}

static int32_t number(const char *json, int len, const char *path, int32_t fallback)
{
    double v;
    return mjson_get_number(json, len, path, &v) ? (int32_t)v : fallback;
}

static void read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *json = n > 0 ? (char *)malloc((size_t)n + 1) : NULL;
    if (!json || fread(json, 1, (size_t)n, f) != (size_t)n) {
        free(json);
        fclose(f);
        return;
    }
    fclose(f);
    json[n] = '\0';
    const int len = (int)n;
    char fmt[40] = "";
    mjson_get_string(json, len, "$.format", fmt, (int)sizeof fmt);
    if (strcmp(fmt, "openswos-challenges") != 0) {
        fprintf(stderr, "[challenges] %s: not an openswos-challenges file\n", path);
        free(json);
        return;
    }
    for (int32_t i = 0; s_count < kMax; i++) {
        char q[80], w[16] = "";
        Challenge *c = &s_ch[s_count];
        memset(c, 0, sizeof *c);
        snprintf(q, sizeof q, "$.challenges[%d].id", (int)i);
        if (mjson_get_string(json, len, q, c->id, (int)sizeof c->id) < 0)
            break;
#define STR(field, key) snprintf(q, sizeof q, "$.challenges[%d]." key, (int)i), \
                        mjson_get_string(json, len, q, c->field, (int)sizeof c->field)
#define NUM(key, fb) (snprintf(q, sizeof q, "$.challenges[%d]." key, (int)i), number(json, len, q, fb))
        STR(name, "name");
        STR(about, "about");
        upper(c->name);
        upper(c->about);
        c->difficulty = (int8_t)NUM("difficulty", 1);
        c->yours = (int8_t)(NUM("yours", 0) ? 1 : 0);
        c->score[0] = (int8_t)NUM("score[0]", 0);
        c->score[1] = (int8_t)NUM("score[1]", 0);
        c->minute = (int16_t)NUM("minute", 0);
        c->length = (uint8_t)NUM("length", 0);
        c->winScore[0] = (int8_t)NUM("win_score[0]", 0);
        c->winScore[1] = (int8_t)NUM("win_score[1]", 0);
        for (int32_t t = 0; t < 2; t++) {
            snprintf(q, sizeof q, "$.challenges[%d].teams[%d].uuid", (int)i, (int)t);
            mjson_get_string(json, len, q, c->teamUuid[t], (int)sizeof c->teamUuid[t]);
            snprintf(q, sizeof q, "$.challenges[%d].teams[%d].name", (int)i, (int)t);
            mjson_get_string(json, len, q, c->teamName[t], (int)sizeof c->teamName[t]);
            upper(c->teamName[t]);
            for (int32_t k = 0; k < 11; k++) {
                snprintf(q, sizeof q, "$.challenges[%d].sent_off[%d][%d]", (int)i, (int)t, (int)k);
                const int32_t shirt = number(json, len, q, 0);
                if (shirt <= 0)
                    break;
                if (shirt < 32)
                    c->sentOff[t] |= 1u << shirt;
            }
        }
        int b = 0;
        snprintf(q, sizeof q, "$.challenges[%d].extra_time", (int)i);
        c->extraTime = (uint8_t)(mjson_get_bool(json, len, q, &b) && b);
        b = 0;
        snprintf(q, sizeof q, "$.challenges[%d].penalties", (int)i);
        c->penalties = (uint8_t)(mjson_get_bool(json, len, q, &b) && b);
        snprintf(q, sizeof q, "$.challenges[%d].win", (int)i);
        mjson_get_string(json, len, q, w, (int)sizeof w);
        c->win = !strcmp(w, "draw") ? W_DRAW : !strcmp(w, "exact") ? W_EXACT : W_WIN;
#undef STR
#undef NUM
        s_count++;                          /* (its teams are looked up at PLAY: a uuid search walks
                                             * the whole database, too slow for drawing the list) */
    }
    free(json);
}

static void read_progress(void)
{
    uint32_t size = 0;
    char *json = (char *)ModApi_LoadFile("challenges", "progress.json", &size);
    if (!json)
        return;
    for (int32_t i = 0; i < 256; i++) {
        char q[64], id[40];
        snprintf(q, sizeof q, "$.challenges[%d].id", (int)i);
        if (mjson_get_string(json, (int)size, q, id, (int)sizeof id) < 0)
            break;
        for (int32_t k = 0; k < s_count; k++)
            if (!strcmp(s_ch[k].id, id)) {
                int b = 0;
                snprintf(q, sizeof q, "$.challenges[%d].completed", (int)i);
                s_ch[k].completed = (uint8_t)(mjson_get_bool(json, (int)size, q, &b) && b);
                snprintf(q, sizeof q, "$.challenges[%d].attempts", (int)i);
                s_ch[k].attempts = (uint32_t)number(json, (int)size, q, 0);
            }
    }
    free(json);
}

static void save_progress(void)
{
    char buf[8192];
    int n = snprintf(buf, sizeof buf, "{\n \"format\": \"openswos-challenges-progress\",\n \"version\": 1,\n \"challenges\": [\n");
    for (int32_t i = 0; i < s_count && n < (int)sizeof buf - 200; i++)
        n += snprintf(buf + n, sizeof buf - (size_t)n, "  { \"id\": \"%s\", \"completed\": %s, \"attempts\": %u }%s\n",
                      s_ch[i].id, s_ch[i].completed ? "true" : "false", (unsigned)s_ch[i].attempts,
                      i + 1 < s_count ? "," : "");
    n += snprintf(buf + n, sizeof buf - (size_t)n, " ]\n}\n");
    ModApi_SaveFile("challenges", "progress.json", buf, (uint32_t)n);
}

static int by_difficulty(const void *a, const void *b)
{
    return ((const Challenge *)a)->difficulty - ((const Challenge *)b)->difficulty;
}

/* every mod that is on: its data_json/challenges/ */
static void load(void)
{
    if (s_loaded)
        return;
    s_loaded = 1;
    for (int32_t m = 0; m < Mods_Count(); m++) {
        char dir[1200];
        snprintf(dir, sizeof dir, "%s/data_json/challenges", Mods_Dir(m));
        DIR *d = opendir(dir);
        if (!d)
            continue;
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            const size_t l = strlen(e->d_name);
            if (l > 5 && !strcmp(e->d_name + l - 5, ".json")) {
                char path[1500];
                snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
                read_file(path);
            }
        }
        closedir(d);
    }
    qsort(s_ch, (size_t)s_count, sizeof s_ch[0], by_difficulty);
    read_progress();
    fprintf(stderr, "[challenges] %d challenges\n", (int)s_count);
}

/* locked while an easier one is not done (SWOS 2020's setScenariosLocked) */
static int32_t locked(int32_t i)
{
    for (int32_t k = 0; k < s_count; k++)
        if (s_ch[k].difficulty < s_ch[i].difficulty && !s_ch[k].completed)
            return 1;
    return 0;
}

/* ---- playing one ---- */

static int32_t s_cur = -1, s_won, s_retry, s_test;

static int32_t won(const Challenge *c, const ModMatchResult *r)
{
    if (r->quit)
        return 0;
    const int32_t y = c->yours, t = 1 - y;
    const int32_t mine = r->goals[y] * 100 + r->penaltyGoals[y], theirs = r->goals[t] * 100 + r->penaltyGoals[t];
    switch (c->win) {
    case W_DRAW: return mine >= theirs;
    case W_EXACT: return r->goals[y] == c->winScore[0] && r->goals[t] == c->winScore[1];
    default: return mine > theirs;
    }
}

static void play(int32_t i)
{
    Challenge *c = &s_ch[i];
    s_cur = i;
    if (!ModApi_TeamByUuid(c->teamUuid[0]) || !ModApi_TeamByUuid(c->teamUuid[1])) {
        fprintf(stderr, "[challenges] %s: a team is not in the database (or a pack that is on)\n", c->id);
        return;
    }
    for (;;) {
        ModMatchSetup setup;
        ModMatchResult r;
        ModApi_DefaultSetup(&setup);
        for (int32_t t = 0; t < 2; t++) {
            setup.team[t] = ModApi_TeamByUuid(c->teamUuid[t]);
            setup.side[t] = (uint8_t)(t == c->yours && !s_test ? MODSIDE_PLAYER1 : MODSIDE_CPU);
            setup.score[t] = c->score[t];
            setup.sentOff[t] = c->sentOff[t];
        }
        setup.minute = c->minute;
        setup.length = c->length;
        setup.extraTime = c->extraTime;
        setup.penalties = c->penalties;
        setup.autoReplays = 0;              /* (SWOS 2020 turns them off for a challenge) */
        setup.replayMenu = 0;
        setup.stadium = !s_test;
        setup.name = "challenge";
        if (ModApi_PlayMatch("challenges", &setup, &r) != 0)
            return;
        c->attempts++;
        s_won = won(c, &r);
        if (s_won)
            c->completed = 1;
        save_progress();
        fprintf(stderr, "[challenges] %s: %d-%d, %s\n", c->id, (int)r.goals[0], (int)r.goals[1], s_won ? "won" : "not won");
        if (s_test) {
            fprintf(stdout, "CHALLENGE TEST %s: %d-%d %s, attempts %u\n", c->id, (int)r.goals[0], (int)r.goals[1],
                    s_won ? "won" : "lost", (unsigned)c->attempts);
            fflush(stdout);
            exit(0);
        }
        s_retry = 0;
        ModApi_ShowMenu("challenges/challengeResult");
        if (!s_retry)
            return;
    }
}

/* ---- the screens ---- */

enum { M_header = 0, M_subTitle, M_label0, M_note = M_label0 + 2 * kRows, M_exit };

static char s_lines[kRows][2][40], s_head[48], s_sub[96];

static void row(int32_t i, const char *label, const char *value, int32_t labelShown)
{
    snprintf(s_lines[i][0], sizeof s_lines[i][0], "%s", label);
    snprintf(s_lines[i][1], sizeof s_lines[i][1], "%s", value);
    ModApi_MenuText(M_label0 + 2 * i, s_lines[i][0]);
    ModApi_MenuText(M_label0 + 2 * i + 1, s_lines[i][1]);
    ModApi_MenuVisible(M_label0 + 2 * i, labelShown);
    ModApi_MenuVisible(M_label0 + 2 * i + 1, 1);
}

static void hide_from(int32_t from)
{
    for (int32_t i = from; i < kRows; i++) {
        ModApi_MenuVisible(M_label0 + 2 * i, 0);
        ModApi_MenuVisible(M_label0 + 2 * i + 1, 0);
    }
}

static void nav_rows(int32_t n)
{
    for (int32_t i = 0; i < n; i++) {
        const int32_t v = M_label0 + 2 * i + 1;
        ModApi_MenuNav(v, i ? v - 2 : -1, i + 1 < n ? v + 2 : M_exit);
    }
    ModApi_MenuNav(M_exit, n ? M_label0 + 2 * n - 1 : -1, -1);
}

static void list_draw(void)
{
    static const char *const kStars[6] = { "", "*", "**", "***", "****", "*****" };
    load();
    ModApi_MenuText(M_header, "CHALLENGES");
    ModApi_MenuText(M_subTitle, "WIN FROM WHERE THE MATCH STANDS");
    const int32_t n = s_count < kRows ? s_count : kRows;
    for (int32_t i = 0; i < n; i++) {
        char label[40];
        snprintf(label, sizeof label, "%s %s", s_ch[i].name,
                 kStars[s_ch[i].difficulty < 0 ? 0 : s_ch[i].difficulty > 5 ? 5 : s_ch[i].difficulty]);
        row(i, label, s_ch[i].completed ? "DONE" : locked(i) ? "LOCKED" : "OPEN", 1);
    }
    hide_from(n);
    ModApi_MenuText(M_note, s_count ? "THE EASIER ONES OPEN THE HARDER" : "NONE: A CHALLENGE PACK IS A MOD");
}

static void list_init(void)
{
    load();
    nav_rows(s_count < kRows ? s_count : kRows);
    list_draw();
}

static void pick(int32_t i)
{
    if (i >= s_count || locked(i))
        return;
    s_cur = i;
    ModApi_ShowMenu("challenges/challengeInfo");
}

static void info_draw(void)
{
    const Challenge *c = &s_ch[s_cur];
    static const char *const kWin[3] = { "WIN IT", "DON'T LOSE IT", "" };
    char score[16], minute[16], win[24];
    snprintf(s_head, sizeof s_head, "%s", c->name);
    snprintf(s_sub, sizeof s_sub, "%s", c->about);
    ModApi_MenuText(M_header, s_head);
    ModApi_MenuText(M_subTitle, s_sub);
    snprintf(score, sizeof score, "%d-%d", (int)c->score[0], (int)c->score[1]);
    snprintf(minute, sizeof minute, "%d", (int)c->minute);
    if (c->win == W_EXACT)
        snprintf(win, sizeof win, "END IT %d-%d", (int)c->winScore[c->yours == 0 ? 0 : 1],
                 (int)c->winScore[c->yours == 0 ? 1 : 0]);
    else
        snprintf(win, sizeof win, "%s", kWin[c->win]);
    row(0, c->teamName[0], c->yours == 0 ? "YOU" : "CPU", 1);
    row(1, c->teamName[1], c->yours == 1 ? "YOU" : "CPU", 1);
    row(2, "THE SCORE", score, 1);
    row(3, "THE MINUTE", minute, 1);
    row(4, "TO WIN", win, 1);
    row(5, "", "PLAY", 0);
    hide_from(6);
    ModApi_MenuText(M_note, c->completed ? "DONE ALREADY: PLAY IT AGAIN" : "");
}

static void info_init(void)
{
    const int32_t playRow = M_label0 + 2 * 5 + 1;
    ModApi_MenuNav(playRow, -1, M_exit);
    ModApi_MenuNav(M_exit, playRow, -1);
    ModApi_MenuSelect(playRow);
    info_draw();
}

static void info_play(void)
{
    play(s_cur);
    SetExitMenuFlag();                      /* (back to the list, its states new) */
}

static void result_draw(void)
{
    const Challenge *c = &s_ch[s_cur];
    snprintf(s_head, sizeof s_head, "%s", c->name);
    ModApi_MenuText(M_header, s_head);
    ModApi_MenuText(M_subTitle, s_won ? "YOU BEAT THE CHALLENGE!" : "NOT THIS TIME");
    char tries[12];
    snprintf(tries, sizeof tries, "%u", (unsigned)c->attempts);
    row(0, "ATTEMPTS", tries, 1);
    row(1, "", "QUICK RETRY", 0);
    hide_from(2);
    ModApi_MenuText(M_note, "");
}

static void result_init(void)
{
    const int32_t retryRow = M_label0 + 2 * 1 + 1;
    ModApi_MenuNav(retryRow, -1, M_exit);
    ModApi_MenuNav(M_exit, retryRow, -1);
    ModApi_MenuSelect(retryRow);
    result_draw();
}

static void result_retry(void)
{
    s_retry = 1;
    SetExitMenuFlag();
}

static void none(void) {}
static void ch_exit(void) { SetExitMenuFlag(); }
static void ch_open(void) { ModApi_ShowMenu("challenges/challengesMenu"); }

static void pick0(void) { pick(0); }
static void pick1(void) { pick(1); }
static void pick2(void) { pick(2); }
static void pick3(void) { pick(3); }
static void pick4(void) { pick(4); }
static void pick5(void) { pick(5); }
static void pick6(void) { pick(6); }
static void pick7(void) { pick(7); }

/* (src/mod_match.c ModMatch_SelfTest: SWOS_MOD_MATCH_TEST=challenge:<id>) */
void ModChallenges_Test(const char *id)
{
    load();
    for (int32_t i = 0; i < s_count; i++)
        if (!strcmp(s_ch[i].id, id)) {
            s_test = 1;
            play(i);
        }
    fprintf(stdout, "CHALLENGE TEST %s: not found (%d loaded)\n", id, (int)s_count);
    fflush(stdout);
    exit(2);
}

static void ch_init(void)
{
    static const ModHandler kHandlers[] = {
        { "listInit", list_init }, { "listDraw", list_draw },
        { "pick0", pick0 }, { "pick1", pick1 }, { "pick2", pick2 }, { "pick3", pick3 },
        { "pick4", pick4 }, { "pick5", pick5 }, { "pick6", pick6 }, { "pick7", pick7 },
        { "infoInit", info_init }, { "infoDraw", info_draw },
        { "info0", none }, { "info1", none }, { "info2", none }, { "info3", none },
        { "info4", none }, { "info5", info_play }, { "info6", none }, { "info7", none },
        { "resultInit", result_init }, { "resultDraw", result_draw },
        { "result0", none }, { "result1", result_retry }, { "result2", none }, { "result3", none },
        { "result4", none }, { "result5", none }, { "result6", none }, { "result7", none },
        { "exit", ch_exit },
    };
    ModApi_RegisterHandlers("challenges", kHandlers, (int32_t)(sizeof kHandlers / sizeof kHandlers[0]));
    ModApi_AddEntry(MODPOINT_MODES, "challenges", "challenges", ch_open);
}

const ModDef ModDef_challenges = {
    .slug = "challenges",
    .init = ch_init,
};

#endif /* SWOS_SDL_BUILD */
