/*
 * mod_challenges.c -- the challenges mod (canonical, bin/mods/challenges; docs/MODS.md §18):
 * SWOS 2020's scenario mode (game.cpp ScenarioGame), ported onto the mod API, its challenges in
 * a JSON format of OpenSWOS's own (the owner: JSON is our format for data -- not challenges.ini,
 * not .SCN). Both targets.
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
 *       "teams": [ { "uuid": "...", "team": "0a12", "name": "MANCHESTER UTD" }, { ... } ],   database teams by uuid
 *                                                    (and number, hex: the Amiga's, which has no database)
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

#include "mjson.h"
#include "mods.h"
#include "swos_io.h"        /* (the Amiga's file read) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void SetExitMenuFlag(void);

enum { kMax = 32, kRows = 8 };
enum { W_WIN, W_DRAW, W_EXACT };

typedef struct {
    char     id[40], name[32], about[96], teamName[2][24], teamUuid[2][40];
    uint16_t teamNumber[2];     /* "team": its number (hex), for the Amiga, which has no database */
    int8_t   difficulty, yours, score[2], winScore[2];
    uint8_t  win, extraTime, penalties, length;
    int16_t  minute;
    uint32_t sentOff[2];
    int8_t   pitchType;                 /* -1 the game's */
    uint8_t  pitchNumber;               /* 0 the game's */
    uint8_t  restartKind, restartTeam, restartWing;
    int16_t  restartX, restartY;
    struct { int8_t team; uint8_t shirt, minute, penalty; } scorers[8];
    uint8_t  custom;                    /* the player's own (custom.json, the editor) */
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
    long v;     /* (whole numbers: mjson_get_long, which the Amiga has too -- no doubles there) */
    return mjson_get_long(json, len, path, &v) ? (int32_t)v : fallback;
}

static void read_text(char *json, int len, const char *path, int32_t custom);

#ifndef SWOS_SDL_BUILD
static void read_file(const char *path)
{
    uint8_t *json = NULL;
    uint32_t n = 0;
    if (swos_io_load_data_alloc(path, &json, &n) != 0 || !json || !n) {
        free(json);
        return;
    }
    read_text((char *)json, (int)n, path, 0);  /* (NUL-terminated by swos_io) */
}
#else
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
    read_text(json, (int)n, path, 0);
}
#endif

/* A challenges file's text (freed here): its challenges after those loaded. */
static void read_text(char *json, int len, const char *path, int32_t custom)
{
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
            {
                char hexNumber[8] = "";
                snprintf(q, sizeof q, "$.challenges[%d].teams[%d].team", (int)i, (int)t);
                if (mjson_get_string(json, len, q, hexNumber, (int)sizeof hexNumber) > 0)
                    c->teamNumber[t] = (uint16_t)strtol(hexNumber, NULL, 16);
            }
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
        /* where it starts (H6, H7): the pitch, the break, who has scored */
        c->pitchType = (int8_t)NUM("pitch_type", -1);
        c->pitchNumber = (uint8_t)NUM("pitch_number", 0);
        {
            char kind[16] = "";
            snprintf(q, sizeof q, "$.challenges[%d].restart.kind", (int)i);
            mjson_get_string(json, len, q, kind, (int)sizeof kind);
            c->restartKind = !strcmp(kind, "corner") ? MODRESTART_CORNER
                           : !strcmp(kind, "free_kick") ? MODRESTART_FREE_KICK
                           : !strcmp(kind, "penalty") ? MODRESTART_PENALTY : MODRESTART_KICK_OFF;
            c->restartTeam = (uint8_t)(NUM("restart.team", 0) ? 1 : 0);
            c->restartWing = (uint8_t)(NUM("restart.wing", 0) ? 1 : 0);
            c->restartX = (int16_t)NUM("restart.x", 336);
            c->restartY = (int16_t)NUM("restart.y", 449);
        }
        for (int32_t k = 0; k < 8; k++) {
            snprintf(q, sizeof q, "$.challenges[%d].scorers[%d].team", (int)i, (int)k);
            const int32_t team = number(json, len, q, -1);
            c->scorers[k].team = (int8_t)(team == 0 || team == 1 ? team : -1);
            if (c->scorers[k].team < 0)
                continue;
            snprintf(q, sizeof q, "$.challenges[%d].scorers[%d].shirt", (int)i, (int)k);
            c->scorers[k].shirt = (uint8_t)number(json, len, q, 0);
            snprintf(q, sizeof q, "$.challenges[%d].scorers[%d].minute", (int)i, (int)k);
            c->scorers[k].minute = (uint8_t)number(json, len, q, 0);
            b = 0;
            snprintf(q, sizeof q, "$.challenges[%d].scorers[%d].penalty", (int)i, (int)k);
            c->scorers[k].penalty = (uint8_t)(mjson_get_bool(json, len, q, &b) && b);
        }
        snprintf(q, sizeof q, "$.challenges[%d].win", (int)i);
        mjson_get_string(json, len, q, w, (int)sizeof w);
        c->win = !strcmp(w, "draw") ? W_DRAW : !strcmp(w, "exact") ? W_EXACT : W_WIN;
#undef STR
#undef NUM
        c->custom = (uint8_t)custom;
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

/* (ModApi_ListFiles' callback: one file of a mod's data_json/challenges/) */
static int32_t each_file(const char *name, void *ctx)
{
    const size_t l = strlen(name);
    if (l > 5 && !strcmp(name + l - 5, ".json")) {
        char path[1500];
        snprintf(path, sizeof path, "%s/%s", (const char *)ctx, name);
        read_file(path);
    }
    return 0;
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
        ModApi_ListFiles(dir, each_file, dir);
    }
    qsort(s_ch, (size_t)s_count, sizeof s_ch[0], by_difficulty);
    {   /* the player's own, after the packs' (the editor writes them: custom.json) */
        uint32_t size = 0;
        char *json = (char *)ModApi_LoadFile("challenges", "custom.json", &size);
        if (json)
            read_text(json, (int)size, "custom.json", 1);
    }
    read_progress();
    fprintf(stderr, "[challenges] %d challenges\n", (int)s_count);
}

/* locked while an easier one is not done (SWOS 2020's setScenariosLocked) */
static int32_t locked(int32_t i)
{
    if (s_ch[i].custom)
        return 0;
    for (int32_t k = 0; k < s_count; k++)
        if (!s_ch[k].custom && s_ch[k].difficulty < s_ch[i].difficulty && !s_ch[k].completed)
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
    if (!ModApi_TeamByUuidNumber(c->teamUuid[0], c->teamNumber[0]) ||
        !ModApi_TeamByUuidNumber(c->teamUuid[1], c->teamNumber[1])) {
        fprintf(stderr, "[challenges] %s: a team is not in the database (or a pack that is on)\n", c->id);
        return;
    }
    for (;;) {
        ModMatchSetup setup;
        ModMatchResult r;
        ModApi_DefaultSetup(&setup);
        for (int32_t t = 0; t < 2; t++) {
            setup.team[t] = ModApi_TeamByUuidNumber(c->teamUuid[t], c->teamNumber[t]);
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
        setup.pitchType = c->pitchType;
        setup.pitchNumber = c->pitchNumber;
        setup.restart.kind = c->restartKind;
        setup.restart.team = c->restartTeam;
        setup.restart.wing = c->restartWing;
        setup.restart.x = c->restartX;
        setup.restart.y = c->restartY;
        for (int32_t k = 0; k < 8; k++) {
            setup.scorers[k].team = c->scorers[k].team;
            setup.scorers[k].shirt = c->scorers[k].shirt;
            setup.scorers[k].minute = c->scorers[k].minute;
            setup.scorers[k].penalty = c->scorers[k].penalty;
        }
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
#ifdef SWOS_SDL_BUILD   /* (a test's: the Amiga has no environment to arm one) */
            exit(0);
#endif
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

/* The list's pages: six challenges a page, then NEW CHALLENGE, then MORE when there are more. */
enum { kPerPage = kRows - 2 };
static int32_t s_page;
static int32_t page_count(void) { return s_count ? (s_count + kPerPage - 1) / kPerPage : 1; }
static int32_t page_rows(void)
{
    const int32_t left = s_count - s_page * kPerPage;
    return left < kPerPage ? (left < 0 ? 0 : left) : kPerPage;
}

static void list_draw(void)
{
    static const char *const kStars[6] = { "", "*", "**", "***", "****", "*****" };
    static char more[24];
    load();
    ModApi_MenuText(M_header, "CHALLENGES");
    ModApi_MenuText(M_subTitle, "WIN FROM WHERE THE MATCH STANDS");
    const int32_t n = page_rows();
    for (int32_t r = 0; r < n; r++) {
        const int32_t i = s_page * kPerPage + r;
        char label[40];
        snprintf(label, sizeof label, "%s %s", s_ch[i].name,
                 s_ch[i].custom ? "(YOURS)"
                 : kStars[s_ch[i].difficulty < 0 ? 0 : s_ch[i].difficulty > 5 ? 5 : s_ch[i].difficulty]);
        row(r, label, s_ch[i].completed ? "DONE" : locked(i) ? "LOCKED" : "OPEN", 1);
    }
    row(n, "", "NEW CHALLENGE", 0);
    if (page_count() > 1) {
        snprintf(more, sizeof more, "MORE (%d/%d)", (int)s_page + 1, (int)page_count());
        row(n + 1, "", more, 0);
        hide_from(n + 2);
    } else {
        hide_from(n + 1);
    }
    ModApi_MenuText(M_note, s_count ? "THE EASIER ONES OPEN THE HARDER" : "NONE: A CHALLENGE PACK IS A MOD");
}

static void list_init(void)
{
    load();
    if (s_page >= page_count())
        s_page = 0;
    nav_rows(page_rows() + 1 + (page_count() > 1));
    list_draw();
}

static void edit_new(void);

static void pick(int32_t r)
{
    const int32_t n = page_rows();
    if (r == n) {                           /* NEW CHALLENGE */
        edit_new();
        return;
    }
    if (r == n + 1 && page_count() > 1) {   /* MORE */
        s_page = (s_page + 1) % page_count();
        list_init();
        return;
    }
    const int32_t i = s_page * kPerPage + r;
    if (r > n || i >= s_count || locked(i))
        return;
    s_cur = i;
    ModApi_ShowMenu("challenges/challengeInfo");
}

/* ---- the editor (SWOS 2020's custom challenge, map 3.2): two teams chosen on the friendly's
 * screen, then the score, the minute and what winning is, on the info screen's rows; SAVE AND
 * PLAY writes it to the player's custom.json (the challenges format) and plays it. ---- */

static int32_t s_editing;
static Challenge s_edit;

static void edit_new(void)
{
    if (!ModApi_ChooseTeams())
        return;
    memset(&s_edit, 0, sizeof s_edit);
    for (int32_t t = 0; t < 2; t++) {
        snprintf(s_edit.teamName[t], sizeof s_edit.teamName[t], "%s", ModApi_ChosenName(t));
        if (!ModApi_ChosenTeamUuid(t, s_edit.teamUuid[t], (int32_t)sizeof s_edit.teamUuid[t])) {
            ModApi_ShowError("THAT TEAM HAS NO ID: CHOOSE ANOTHER");
            return;
        }
        s_edit.teamNumber[t] = ModApi_ChosenTeamNumber(t);
        s_edit.scorers[t].team = -1;
    }
    for (int32_t k = 0; k < 8; k++)
        s_edit.scorers[k].team = -1;
    s_edit.yours = (int8_t)(ModApi_ChosenIsPlayer(1) && !ModApi_ChosenIsPlayer(0) ? 1 : 0);
    s_edit.score[s_edit.yours] = 0;
    s_edit.score[!s_edit.yours] = 1;        /* (a challenge: you start behind) */
    s_edit.minute = 70;
    s_edit.pitchType = -1;
    s_edit.win = W_WIN;
    s_edit.custom = 1;
    snprintf(s_edit.name, sizeof s_edit.name, "MY CHALLENGE");
    s_editing = 1;
    ModApi_ShowMenu("challenges/challengeInfo");
    s_editing = 0;
}

static void edit_draw(void)
{
    static const char *const kWin[3] = { "WIN IT", "DON'T LOSE IT", "" };
    static char score[2][8], minute[8];
    ModApi_MenuText(M_header, "NEW CHALLENGE");
    ModApi_MenuText(M_subTitle, "FIRE CHANGES A ROW");
    snprintf(score[0], sizeof score[0], "%d", (int)s_edit.score[0]);
    snprintf(score[1], sizeof score[1], "%d", (int)s_edit.score[1]);
    snprintf(minute, sizeof minute, "%d", (int)s_edit.minute);
    char a[40], b[40];
    snprintf(a, sizeof a, "%s: %s", s_edit.teamName[0], s_edit.yours == 0 ? "YOU" : "CPU");
    snprintf(b, sizeof b, "%s: %s", s_edit.teamName[1], s_edit.yours == 1 ? "YOU" : "CPU");
    row(0, a, score[0], 1);
    row(1, b, score[1], 1);
    row(2, "THE MINUTE", minute, 1);
    row(3, "TO WIN", kWin[s_edit.win == W_DRAW ? 1 : 0], 1);
    row(4, "WHO YOU ARE", "SWAP", 1);
    row(5, "", "SAVE AND PLAY", 0);
    hide_from(6);
    ModApi_MenuText(M_note, "IT GOES IN YOUR CHALLENGES");
}

static void save_custom(void)
{
    char buf[16384];
    int n = snprintf(buf, sizeof buf, "{\n \"format\": \"openswos-challenges\",\n \"version\": 1,\n \"challenges\": [\n");
    int32_t count = 0, number = 1;
    for (int32_t i = 0; i < s_count; i++)
        if (s_ch[i].custom)
            number++;
    snprintf(s_edit.id, sizeof s_edit.id, "custom-%d-%u", (int)number, (unsigned)ModApi_Ticks());
    snprintf(s_edit.name, sizeof s_edit.name, "MY CHALLENGE %d", (int)number);
    for (int32_t i = 0; i <= s_count && n < (int)sizeof buf - 600; i++) {
        const Challenge *c = i < s_count ? &s_ch[i] : &s_edit;
        if (!c->custom)
            continue;
        n += snprintf(buf + n, sizeof buf - (size_t)n,
                      "%s  { \"id\": \"%s\", \"name\": \"%s\", \"about\": \"%s\", \"difficulty\": 0,\n"
                      "    \"teams\": [ { \"uuid\": \"%s\", \"team\": \"%04x\", \"name\": \"%s\" },\n"
                      "               { \"uuid\": \"%s\", \"team\": \"%04x\", \"name\": \"%s\" } ],\n"
                      "    \"yours\": %d, \"score\": [ %d, %d ], \"minute\": %d, \"win\": \"%s\" }",
                      count ? ",\n" : "", c->id, c->name, "YOUR OWN CHALLENGE", c->teamUuid[0],
                      (unsigned)c->teamNumber[0], c->teamName[0], c->teamUuid[1], (unsigned)c->teamNumber[1],
                      c->teamName[1], (int)c->yours, (int)c->score[0], (int)c->score[1],
                      (int)c->minute, c->win == W_DRAW ? "draw" : "win");
        count++;
    }
    n += snprintf(buf + n, sizeof buf - (size_t)n, "\n ]\n}\n");
    ModApi_SaveFile("challenges", "custom.json", buf, (uint32_t)n);
}

static void edit_row(int32_t r)
{
    switch (r) {
    case 0: s_edit.score[0] = (int8_t)((s_edit.score[0] + 1) % 10); break;
    case 1: s_edit.score[1] = (int8_t)((s_edit.score[1] + 1) % 10); break;
    case 2: s_edit.minute = (int16_t)(s_edit.minute >= 85 ? 5 : s_edit.minute + 5); break;
    case 3: s_edit.win = s_edit.win == W_WIN ? W_DRAW : W_WIN; break;
    case 4: s_edit.yours = (int8_t)!s_edit.yours; break;
    case 5: {
        if (s_count >= kMax) {
            ModApi_ShowError("NO ROOM FOR ANOTHER CHALLENGE");
            return;
        }
        save_custom();
        s_loaded = 0;                       /* (read again: the new one in its place) */
        s_count = 0;
        load();
        for (int32_t i = s_count - 1; i >= 0; i--)
            if (s_ch[i].custom) {
                s_cur = i;
                break;
            }
        s_editing = 0;
        play(s_cur);
        SetExitMenuFlag();
        return;
    }
    default: return;
    }
    edit_draw();
    for (int32_t i = 0; i < 6; i++)
        ModApi_MenuRedraw(M_label0 + 2 * i + 1);
}

static void info_draw(void)
{
    if (s_editing) {
        edit_draw();
        return;
    }
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
    if (s_editing) {
        nav_rows(6);
        ModApi_MenuSelect(M_label0 + 1);
        edit_draw();
        return;
    }
    const int32_t playRow = M_label0 + 2 * 5 + 1;
    ModApi_MenuNav(playRow, -1, M_exit);
    ModApi_MenuNav(M_exit, playRow, -1);
    ModApi_MenuSelect(playRow);
    info_draw();
}

static void info_edit0(void) { if (s_editing) edit_row(0); }
static void info_edit1(void) { if (s_editing) edit_row(1); }
static void info_edit2(void) { if (s_editing) edit_row(2); }
static void info_edit3(void) { if (s_editing) edit_row(3); }
static void info_edit4(void) { if (s_editing) edit_row(4); }

static void info_play(void)
{
    if (s_editing) {
        edit_row(5);
        return;
    }
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

#ifdef SWOS_SDL_BUILD
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
#endif

static void ch_init(void)
{
    static const ModHandler kHandlers[] = {
        { "listInit", list_init }, { "listDraw", list_draw },
        { "pick0", pick0 }, { "pick1", pick1 }, { "pick2", pick2 }, { "pick3", pick3 },
        { "pick4", pick4 }, { "pick5", pick5 }, { "pick6", pick6 }, { "pick7", pick7 },
        { "infoInit", info_init }, { "infoDraw", info_draw },
        { "info0", info_edit0 }, { "info1", info_edit1 }, { "info2", info_edit2 }, { "info3", info_edit3 },
        { "info4", info_edit4 }, { "info5", info_play }, { "info6", none }, { "info7", none },
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
