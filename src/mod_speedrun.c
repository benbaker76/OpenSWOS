/*
 * mod_speedrun.c -- the speedrun mod (canonical, bin/mods/speedrun; docs/MODS.md §18): SWOS 2020's
 * SPEEDRUN (game.cpp challengeGame0Extras_OnSelect, checkSpeedrunWinningCondition), ported onto
 * the mod API. Both targets.
 *
 * MODES -> SPEEDRUN: eight targets, each with its best time. Pick one, then SWOS 2020's
 * difficulty (selectSpeedrunDifficultyMenu: FREE TEAM CHOICE or one of five PRECONFIGURED TEAMS
 * pairs, BEGINNER .. MASTER; the card targets go straight on), then the teams (the friendly's team
 * screen: yours is the one you play, the other the CPU's or a friend's), and the clock runs from
 * kick-off -- SWOS 2020's timer on the pitch (sr_drawn) -- -- a 3-minute match on a normal pitch -- until the target is met, which
 * ends the match, or the final whistle, which fails it (MATCH: win it):
 *   1 GOAL, 3 GOALS, 10 GOALS   your team scores that many
 *   HEADER                      your team scores a header
 *   TACKLE                      your team scores, its last tackle the last thing before the goal
 *                               (SWOS 2020's: a goal while the last commentary was a tackle's)
 *   YELLOW CARD, RED CARD       one of yours is booked / sent off
 *   MATCH                       your team wins the match
 * At the final whistle SWOS 2020's result band (SPEEDRUN COMPLETED and the time, or FAILED), then
 * the time, the best, TRY AGAIN (the same teams) or EXIT.
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
#include "game_types.h"       /* TeamFileEx (the preconfigured teams) */
#include "endian_helpers.h"


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
static uint8_t  s_goals, s_done, s_tackled;
static uint32_t s_doneAt;
static int32_t  s_retry;

static int32_t s_level = -1;             /* SWOS 2020's difficulty of the run, 0..4 (a free choice's
                                         * by the teams' skill points); -1 none (the card targets) */
static uint8_t s_over;                  /* the final whistle has gone: the result on the pitch */
static uint32_t s_overAt;               /* (its frame: the time stops there) */

#ifdef SWOS_SDL_BUILD
/* ---- the run on the pitch: SWOS 2020's showSpeedrunTimerInfo (game.cpp) -- the time from
 * kick-off top left in a dark box, the target and its difficulty top right -- and, after the final
 * whistle, showSpeedrunShowResult: a dark band at the foot of the screen, the difficulty, SPEEDRUN
 * COMPLETED and the time, or SPEEDRUN FAILED. (Report 0060 asked for the target and the time on the
 * pitch: this is SWOS 2020's own way of showing them.) Drawn into the match's 8bpp frame. ---- */
#include "display.h"            /* HUD_MARGIN, HUD_WIDTH */
#include "mod_hudbox.h"

static const char *const kLevelName[5] = { "BEGINNER", "AMATEUR", "ADVANCED", "PRO", "MASTER" };

static void frame_time(char *out, size_t cap, uint32_t frames)   /* (getFrameTimerString: m:ss.cc) */
{
    const uint32_t fps = (uint32_t)ModApi_FrameRate();
    const uint32_t cs = fps ? frames * 100 / fps : 0;
    snprintf(out, cap, "%u:%02u.%02u", (unsigned)(cs / 6000), (unsigned)(cs / 100 % 60), (unsigned)(cs % 100));
}

/* Each box drawn at the present (mod_hudbox.h): in the frame buffer they went with the sub-pixel
 * scroll and flickered as the camera moved, in black where SWOS 2020's are its VGA palette's 180
 * (putPixelDirect(..., 180): 8, 8, 8 of 63, a dark grey) -- Playa, 2026-11-12. The result's band
 * is shaded as SWOS 2020's result band. */
static HudBox s_boxTime, s_boxTarget, s_boxLevel, s_boxResult, s_boxTrophy;
enum { kGrey = 32 };                    /* (VGA 8 of 63) */

typedef struct { const char *text[3]; int32_t x[3], y[3], small[3], centre[3], n, level; } BoxText;

static void box_contents(void *ctx)
{
    const BoxText *t = (const BoxText *)ctx;
    for (int32_t i = 0; i < t->n; i++) {
        if (!t->text[i])
            continue;
        const int32_t tw = ModApi_TextWidth(t->text[i], t->small[i]);
        int32_t x = t->x[i] - (t->centre[i] ? tw / 2 : 0);
        ModApi_DrawText(x < 0 ? 0 : x, t->y[i], 2 /* kWhiteText2 */, t->text[i], t->small[i]);
        if (i == 0 && t->level >= 0 && t->level < 5) {
            /* the difficulty's stars before its name (SWOS 2020's completion board); its trophy is
             * over the penalty spot (sr_trophy) */
            static const int8_t kTrophy[5] = { 1, 3, 5, 4, 8 };
            const int32_t k = t->level, starsW = 8 + 6 * k;
            (void)kTrophy;
            ModApi_DrawMap("speedrun", "starsMap", x - 4 - starsW, t->y[i] - 2, 0, 10 * (1 + 2 * k), starsW, 10, 0);
        }
    }
}

static void box_one(HudBox *b, int32_t x, int32_t y, int32_t w, int32_t h, const char *text, int32_t tx,
                    int32_t ty, int32_t centre)
{
    BoxText t;
    memset(&t, 0, sizeof t);
    t.level = -1;
    t.n = 1;
    t.text[0] = text;
    t.x[0] = tx;
    t.y[0] = ty;
    t.small[0] = 1;
    t.centre[0] = centre;
    HudBox_Draw(b, x, y, w, h, HUDBOX_SOLID, kGrey, kGrey, kGrey, box_contents, &t);
}

static void sr_off(void)
{
    HudBox_Off(&s_boxTime);
    HudBox_Off(&s_boxTarget);
    HudBox_Off(&s_boxLevel);
    HudBox_Off(&s_boxResult);
    HudBox_Off(&s_boxTrophy);
}

/* A completed run: the difficulty's trophy by the top penalty spot (336, 187, where the camera rests
 * by the stand), centred on it a little above, as SWOS 2020's completion screen has it, in
 * the menus' colours, three times its 16 x 14 (Playa) -- with the completion banner. */
static void trophy_contents(void *ctx)
{
    static const int8_t kTrophy[5] = { 1, 3, 5, 4, 8 };
    const int32_t k = *(const int32_t *)ctx;
    ModApi_DrawMap("speedrun", "trophiesMap", 0, 0, 0, 14 * kTrophy[k], 16, 14, 0);
}

static void sr_trophy(void)
{
    int16_t cx, cy;
    int32_t level = s_level;
    if (!s_done || level < 0 || level > 4) {
        HudBox_Off(&s_boxTrophy);
        return;
    }
    /* (the menus' palette, SWOS2.s menuPalette: the trophies are menu pictures) */
    static const uint16_t kMenuPalette[16] = { 0x001, 0xAAB, 0xFFF, 0x102, 0x621, 0xA40, 0xF71, 0x667,
                                               0x204, 0x445, 0xF00, 0x00F, 0x702, 0x88F, 0x380, 0xFF0 };
    ModApi_Camera(&cx, &cy);
    enum { kScale = 3 };                /* (three times its 16 x 14: Playa) */
    /* each trophy's own middle (twice it: they sit at their cell's left, 8 to 13 wide) on the spot's
     * x, so it is centred on the spot and over the completion board (Playa) */
    static const int8_t kMid2X[5] = { 7, 8, 12, 12, 12 }, kMid2Y[5] = { 14, 14, 11, 12, 11 };
    HudBox_Draw(&s_boxTrophy, 336 - cx - kMid2X[level] * kScale / 2, 182 - cy - kMid2Y[level] * kScale / 2, 16, 14,
                HUDBOX_CLEAR, 0, 0, 0, trophy_contents, &level);
    s_boxTrophy.pal16 = kMenuPalette;
    s_boxTrophy.scale = kScale;
}

static void sr_drawn(void)
{
    int32_t sw, sh;
    if (!ModApi_ModMatch("speedrun") || ModApi_InReplay() || s_target < 0) {
        sr_off();
        return;
    }
    ModApi_ScreenSize(&sw, &sh);
    const int32_t left = HUD_MARGIN, right = HUD_MARGIN + HUD_WIDTH;
    char t[32];
    frame_time(t, sizeof t, s_done ? s_doneAt : s_over ? s_overAt : ModApi_MatchFrames());
    /* (each text where it was: the time at 25, 20 in its box from 10, 19; the target centred at
     *  right - 50, 8 in its box from 6; the difficulty at 20 in its box from 19) */
    box_one(&s_boxTime, left + 10, 19, 71, 8, t, 15, 1, 0);
    box_one(&s_boxTarget, right - 90, 6, 81, 9, kTarget[s_target].name, 40, 2, 1);
    if (s_level >= 0)
        box_one(&s_boxLevel, right - 90, 19, 81, 8, kLevelName[s_level], 40, 1, 1);
    else
        HudBox_Off(&s_boxLevel);
    extern uint16_t _statsTimer;        /* (the statistics board: the banner gives way to it) */
    extern uint16_t _gameState;         /* (SWOS 2020's kResultAfterTheGame: the players gone, the
                                         *  camera up at the stand -- not before -- Playa) */
    if ((!s_over && !s_done) || _statsTimer || _gameState != ST_RESULT_AFTER_THE_GAME) {
        HudBox_Off(&s_boxResult);
        HudBox_Off(&s_boxTrophy);
        return;
    }
    sr_trophy();
    /* the result: SWOS 2020's showSpeedrunShowResult, from the final whistle (its
     * kResultAfterTheGame) with the game's result still up -- its band 50 high from 220 (DOS 40
     * from 158 of 200), the whole width, or 70 % of it centred on a wide screen */
    const int32_t tall = sh >= 240;
    const int32_t top = tall ? sh - 52 : sh - 42, h = tall ? 50 : 40;
    const int32_t wide = sw > 336;
    const int32_t bw = wide ? sw * 7 / 10 : sw, bx = wide ? (sw - bw) / 2 : 0;
    const int32_t cx = sw / 2 - bx;
    char line[48];
    BoxText r;
    memset(&r, 0, sizeof r);
    r.n = 3;
    for (int32_t i = 0; i < 3; i++) {
        r.x[i] = cx;
        r.centre[i] = 1;
        r.small[i] = 1;
    }
    r.y[0] = (tall ? sh - 44 : sh - 38) - top;
    r.y[1] = (tall ? sh - 31 : sh - 25) - top;
    r.y[2] = (tall ? sh - 17 : sh - 11) - top;
    r.text[0] = s_level >= 0 ? kLevelName[s_level] : NULL;
    r.level = s_level;
    r.small[1] = 0;
    if (s_done) {
        r.text[1] = "*** SPEEDRUN COMPLETED ***";
        snprintf(line, sizeof line, "%s - %s", kTarget[s_target].name, t);
        r.text[2] = line;
    } else {
        r.text[1] = "SPEEDRUN FAILED. TRY AGAIN.";
        r.text[2] = kTarget[s_target].name;
    }
    HudBox_Draw(&s_boxResult, bx, top, bw, h, HUDBOX_SHADE, 0, 0, 0, box_contents, &r);
}

static void sr_present(void)
{
    if (!ModApi_ModMatch("speedrun") || ModApi_InReplay())
        return;
    HudBox_Present(&s_boxTime);
    HudBox_Present(&s_boxTarget);
    HudBox_Present(&s_boxLevel);
    HudBox_Present(&s_boxResult);
    HudBox_Present(&s_boxTrophy);
}

static void sr_kept(void)
{
    HudBox_Kept(&s_boxTime);
    HudBox_Kept(&s_boxTarget);
    HudBox_Kept(&s_boxLevel);
    HudBox_Kept(&s_boxResult);
    HudBox_Kept(&s_boxTrophy);
}

#endif

static int32_t s_whistle;               /* the target met: 1 the cards' (yours wins), 2 by the score */

/* The final whistle, the frame after the target was met: from inside the event the goal's own
 * sequence ran on over it -- the kick-off again, the match on to full time (Playa, 2026-11-12). */
static void sr_frame(void)
{
    if (!s_whistle || !ModApi_ModMatch("speedrun"))
        return;
    const int32_t winner = s_whistle == 1 ? s_you : -1;
    s_whistle = 0;
    /* the game's result banner (names, score, scorers, its grid) would sit under the completion
     * banner, its FULL TIME over it: from the whistle only that, as SWOS 2020's (Playa) */
    ModApi_SetHideHud(MODHUD_PLAYER_NAME | MODHUD_RESULT);
    {
        extern void PlayEndGameWhistleSample(void);
        PlayEndGameWhistleSample();
    }
    ModApi_FinalWhistle(winner);
}

static void sr_event(const ModMatchEvent *e)
{
    if (!ModApi_ModMatch("speedrun") || s_target < 0)
        return;
    /* (the 90 minutes up: a run has no extra time -- FINAL_WHISTLE comes only as the match closes) */
    if ((e->kind == MODEVENT_FULL_TIME || e->kind == MODEVENT_FINAL_WHISTLE) && !s_over) {
        s_over = 1;                     /* (the result on the pitch from now: sr_drawn) */
        ModApi_SetHideHud(MODHUD_PLAYER_NAME | MODHUD_RESULT);  /* (only it: sr_frame) */
        s_overAt = e->frame;
        if (!s_done && kTarget[s_target].kind == T_MATCH) {     /* (MATCH: won when it ends) */
            int16_t g[2];
            ModApi_Score(g);
            if (g[s_you] > g[1 - s_you]) {
                s_done = 1;
                s_doneAt = e->frame;
            }
        }
    }
    if (s_done)
        return;
    const uint8_t kind = kTarget[s_target].kind;
    int32_t met = 0;
    if (e->team == s_you) {
        if (e->kind == MODEVENT_GOAL || e->kind == MODEVENT_PENALTY_GOAL) {
            s_goals++;
            met = (kind == T_GOALS && s_goals >= kTarget[s_target].goals) ||
                  (kind == T_HEADER && (e->arg & MODGOAL_HEADER)) ||
                  (kind == T_TACKLE && s_tackled && e->kind == MODEVENT_GOAL);
        }
        met = met || (kind == T_YELLOW && e->kind == MODEVENT_YELLOW_CARD) ||
              (kind == T_RED && (e->kind == MODEVENT_RED_CARD || e->kind == MODEVENT_SECOND_YELLOW));
    } else if (e->kind == MODEVENT_OWN_GOAL && e->team == s_you) {
        s_goals++;
    }
    /* TACKLE: your tackle stays the last thing that happened until something else does -- a
     * tackle of theirs, a foul, a card, an injury, a goal, a break (SWOS 2020 looked at the last
     * commentary line) */
    if (e->kind == MODEVENT_TACKLE)
        s_tackled = e->team == s_you;
    else if (e->kind != MODEVENT_SHOOTOUT_KICK)
        s_tackled = 0;
    if (met) {
        s_done = 1;
        s_doneAt = e->frame;
        fprintf(stderr, "[speedrun] %s in %u frames\n", kTarget[s_target].key, (unsigned)e->frame);
        /* SWOS 2020's endSpeedrun: the end-of-game whistle and the game's own end (EndOfGame) --
         * the winner celebrates, the result is told, the players leave -- while the completion
         * banner shows (sr_drawn). Ending it at once (ModApi_EndMatch) took the result and the
         * banner off the screen the frame they came up (Playa, 2026-11-12). The card targets'
         * winner is yours, as its winningTeamPtr = &leftTeamIngame. */
        s_whistle = kind == T_YELLOW || kind == T_RED ? 1 : 2;     /* (sr_frame: the next frame) */
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

/* SWOS 2020's preconfigured teams, a pair a difficulty (its selectSpeedrunDifficulty: BEGINNER ..
 * MASTER; game.cpp: tno1 / tno2 by global team number from ITS TEAM.072, where SWOS 2020 put its
 * own teams over ten of the PC game's custom clubs -- PLAYER TEAM v BEGINNER .. MASTER). Those ten,
 * as SWOS 2020's file has them, are this mod's teams/SPEEDRUN.EDT (a big-endian count, then the
 * 684-byte records), found there by global number as SWOS 2020 found them. Yours first. (Until
 * 2026-11-12 96/97 nations stood in for them -- Playa.) */
static const struct { uint16_t global[2]; const char *name[2]; } kPreset[5] = {
    { { 1558, 1548 }, { "PLAYER TEAM", "BEGINNER" } },
    { { 1571, 1567 }, { "PLAYER TEAM", "AMATEUR" } },
    { { 1568, 1570 }, { "PLAYER TEAM", "ADVANCED" } },
    { { 1566, 1569 }, { "PLAYER TEAM", "PRO" } },
    { { 1547, 1557 }, { "PLAYER TEAM", "MASTER" } },
};
static TeamFileEx s_presetTeam[2];      /* the run's pair, a setup's teamData */

/* SWOS 2020's team of that global number from teams/SPEEDRUN.EDT into *out (the number in the
 * host's order, as a teamData wants it). The file read once, kept for the run (6.8 KB). */
static int32_t preset_team(uint16_t global, TeamFileEx *out)
{
    static uint8_t *s_file;
    static uint32_t s_size;
    if (!s_file) {
        char path[1100];
        snprintf(path, sizeof path, "%s/teams/SPEEDRUN.EDT", ModApi_ModDir("speedrun"));
        s_file = (uint8_t *)ModApi_ReadFile(path, &s_size);
        if (!s_file) {
            fprintf(stderr, "[speedrun] no %s: the preconfigured teams are missing\n", path);
            return 0;
        }
    }
    const uint32_t count = s_size >= 2 ? ((uint32_t)s_file[0] << 8) | s_file[1] : 0;
    for (uint32_t k = 0; k < count && 2 + (k + 1) * sizeof(TeamFileEx) <= s_size; k++) {
        const uint8_t *rec = s_file + 2 + k * sizeof(TeamFileEx);
        if ((uint16_t)(((uint16_t)rec[2] << 8) | rec[3]) == global) {      /* (big-endian in the file) */
            memcpy(out, rec, sizeof *out);
            out->team.globalTeamNumber = (int16_t)BE16_TO_HOST((uint16_t)out->team.globalTeamNumber);
            return 1;
        }
    }
    fprintf(stderr, "[speedrun] team %u is not in SPEEDRUN.EDT\n", (unsigned)global);
    return 0;
}
static int32_t s_difficulty = -1;       /* the run's preset (0..4), -1 the teams chosen */

static char s_resultTeams[48];
static int32_t s_test;                  /* SWOS_MOD_MATCH_TEST=speedrun:<n>: fixed teams, no screens */

/* (each team's skill points, SWOS 2020's getTeamSkillPoints: its sixteen players' seven skills) */
static int32_t skill_points(int32_t team)
{
    int32_t sum = 0;
    for (int32_t slot = 0; slot < 16; slot++) {
        ModTeamPlayer p;
        const int32_t got = s_difficulty >= 0
            ? ModApi_TeamDataPlayer(&s_presetTeam[team], slot, &p)
            : ModApi_ChosenTeamPlayer(team, slot, &p);
        if (got)
            sum += p.passing + p.shooting + p.heading + p.tackling + p.control + p.speed + p.finishing;
    }
    return sum;
}

/* The run's difficulty as SWOS 2020 shows it: a preset's own; a free choice's by the skill points,
 * yours less theirs (>= 375 BEGINNER, 150.. AMATEUR, -149..149 ADVANCED, ..-150 PRO, <= -375
 * MASTER); none for the card targets (its 105 / 107, which have no picker). */
static int32_t level_of(int32_t target)
{
    if (kTarget[target].kind == T_YELLOW || kTarget[target].kind == T_RED)
        return -1;
    if (s_difficulty >= 0)
        return s_difficulty;
    const int32_t d = skill_points(s_you) - skill_points(1 - s_you);
    return d >= 375 ? 0 : d >= 150 ? 1 : d >= -149 ? 2 : d >= -374 ? 3 : 4;
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
             "\"yours\":%d,\"theirs\":%d,\"teams\":\"%s\",\"difficulty\":%d}",
             (int)target, kTarget[target].key, s_done ? "true" : "false", (unsigned)(s_done ? s_doneAt : 0),
             (unsigned)(s_done && fps ? s_doneAt / fps * 1000u + s_doneAt % fps * 1000u / fps : 0),
             Gameplay_StyleName(_gGameplayStyle), (int)skill_points(s_you), (int)skill_points(1 - s_you),
             ModApi_JsonEscape(s_resultTeams, teams, sizeof teams), (int)s_difficulty);
    ModApi_CallService("online-records", "speedrun.finished", args, NULL, 0);
    s_onlineAsked = 0;
    s_onlineLine[0] = 0;
}

#ifdef SWOS_SDL_BUILD   /* (the Amiga: no such screens -- a run plays as before) */
/* SWOS 2020's DIFFICULTY SETTING (difficultySetting.mnu, game.cpp's difficultySettingMenu_OnDraw):
 * after the teams are chosen for a free choice, the difficulty their skill points give, the
 * difference and each side's, and PLAY / EXIT (EXIT: the difficulty picker again, as its goto
 * speedrunSelectDifficulty_label). And its TRY AGAIN / EXIT (retryExit.mnu) after a run left with
 * ESC: TRY AGAIN plays the same teams again, through their line-up. Both this mod's menus. */
enum { DS_header = 0, DS_panel, DS_msg0, DS_msg1, DS_msg2, DS_msg3, DS_play, DS_exit };
static char    s_dsText[4][64];
static int32_t s_dsLevel, s_dsChoice;

static void ds_init(void)
{
    for (int32_t i = 0; i < 4; i++)
        ModApi_MenuText(DS_msg0 + i, s_dsText[i]);
}

static void ds_draw(void)       /* (its trophy left of PLAY, its stars right: the level's) */
{
    static const int8_t kTrophy[5] = { 1, 3, 5, 4, 8 };
    const int32_t t = kTrophy[s_dsLevel], shift = t == 1 || t == 3;
    /* SWOS 2020's 92 + 2 * shift, 165 - 2 * shift and 215, 168, against PLAY drawn at 110, 163 */
    ModApi_DrawMapAtEntry("speedrun", "trophiesMap", DS_play, -18 + 2 * shift, 2 - 2 * shift, 0, 14 * t, 16, 14, 0);
    ModApi_DrawMapAtEntry("speedrun", "starsMap", DS_play, 105, 5, 0, 10 * (1 + 2 * s_dsLevel), 36, 10, 0);
}

static void ds_play(void) { s_dsChoice = 1; SetExitMenuFlag(); }
static void ds_exit(void) { s_dsChoice = 0; SetExitMenuFlag(); }

static int32_t s_retryChoice;
static void retry_try(void) { s_retryChoice = 1; SetExitMenuFlag(); }
static void retry_exit(void) { s_retryChoice = 0; SetExitMenuFlag(); }

/* The difficulty screen for the chosen teams (yours first): 1 PLAY, 0 EXIT. */
static int32_t difficulty_setting(void)
{
    const int32_t yours = skill_points(s_you), theirs = skill_points(1 - s_you), d = yours - theirs;
    s_dsLevel = d >= 375 ? 0 : d >= 150 ? 1 : d >= -149 ? 2 : d >= -374 ? 3 : 4;
    snprintf(s_dsText[0], sizeof s_dsText[0], "DIFFICULTY : %s", kLevelName[s_dsLevel]);
    snprintf(s_dsText[1], sizeof s_dsText[1], "SKILL POINT DIFFERENCE : %d", (int)d);
    snprintf(s_dsText[2], sizeof s_dsText[2], "PLAYER (%s) : %d", ModApi_ChosenName(s_you), (int)yours);
    snprintf(s_dsText[3], sizeof s_dsText[3], "COMPUTER (%s) : %d", ModApi_ChosenName(1 - s_you), (int)theirs);
    s_dsChoice = 0;
    if (ModApi_ShowMenu("speedrun/difficultySettingMenu") != 0)
        return 1;                       /* (no such menu: play, as before it) */
    return s_dsChoice;
}

#endif

/* the run: the match, then the result screen, again while TRY AGAIN. difficulty: one of SWOS
 * 2020's preconfigured pairs (0..4, yours the first), -1 the teams chosen on the friendly's
 * screen. 0 when nothing was played (the team screen left, a team missing). */
static int32_t run_at(int32_t target, int32_t again, int32_t difficulty)
{
    int32_t played = 0;
    s_target = target;
    s_difficulty = difficulty;
    for (;;) {
        const int32_t preset = difficulty >= 0 && difficulty < 5;
        const int32_t retried = again;
        if (!s_test && !preset && (again ? !ModApi_ChosenAgain() : !ModApi_ChooseTeams()))
            return played;
        again = 1;
        s_you = s_test || preset ? 0 : ModApi_ChosenIsPlayer(0) ? 0 : ModApi_ChosenIsPlayer(1) ? 1 : -1;
        if (s_you < 0) {
            fprintf(stderr, "[speedrun] no team is yours: pick one to play\n");
            return played;
        }
#ifdef SWOS_SDL_BUILD
        if (!s_test && !preset && !retried && kTarget[target].kind != T_YELLOW && kTarget[target].kind != T_RED &&
            !difficulty_setting())
            return played;
#else
        (void)retried;
#endif
        if (preset)
            snprintf(s_resultTeams, sizeof s_resultTeams, "%s v %s", kPreset[difficulty].name[0],
                     kPreset[difficulty].name[1]);
        else
            snprintf(s_resultTeams, sizeof s_resultTeams, "%s v %s", ModApi_ChosenName(s_you),
                     ModApi_ChosenName(1 - s_you));
        ModMatchSetup setup;
        ModMatchResult r;
        ModApi_DefaultSetup(&setup);
        setup.chosen = !s_test && !preset;
        if (preset) {
            for (int32_t t = 0; t < 2; t++) {
                if (!preset_team(kPreset[difficulty].global[t], &s_presetTeam[t]))
                    return played;
                setup.teamData[t] = &s_presetTeam[t];
                setup.side[t] = (uint8_t)(t == 0 && !s_test ? MODSIDE_PLAYER1 : MODSIDE_CPU);
            }
        }
        if (s_test && !preset) {            /* (the test: Norway, the CPU's but "yours", v New Zealand) */
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
        /* the friendly's PLAY MATCH screen first, as SWOS 2020's PlayFriendly showed it: the line-up,
         * the tactics, the uniform (Playa, 2026-11-12) */
        setup.playMatchScreen = (uint8_t)!s_test;
#ifdef SWOS_SDL_BUILD
        setup.hideClock = 1;                /* (SWOS 2020's run time sits where the clock was: sr_drawn) */
        setup.hideHud = MODHUD_PLAYER_NAME; /* (the on-ball name sat over the run's time -- Playa) */
#endif
        s_goals = s_done = s_tackled = s_over = s_whistle = 0;
        s_doneAt = s_overAt = 0;
        s_level = level_of(target);
        if (ModApi_PlayMatch("speedrun", &setup, &r) != 0)
            return played;
        played = 1;
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
#ifdef SWOS_SDL_BUILD
        if (r.quit && !s_done) {        /* (left with ESC: SWOS 2020's retryExitMenu) */
            s_retryChoice = 0;
            if (ModApi_ShowMenu("speedrun/retryExitMenu") == 0 && !s_retryChoice)
                return played;
            continue;                   /* (TRY AGAIN: the same teams, their line-up first) */
        }
#endif
        s_retry = 0;
        ModApi_ShowMenu("speedrun/speedrunResult");
        if (!s_retry)
            return played;
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

/* ---- SWOS 2020's difficulty picker (selectSpeedrunDifficulty.mnu): FREE TEAM CHOICE (SELECT
 * TEAMS: the friendly's team screen, the difficulty then the teams' skill points') or one of the
 * five PRECONFIGURED TEAMS pairs, BEGINNER .. MASTER. The card targets have none (SWOS 2020's
 * selectSpeedrunDifficultyCard: SELECT TEAMS alone), so they go straight to the teams. Leaving the
 * team screen comes back here, as SWOS 2020's goto speedrunSelectDifficulty_label. LEADERBOARD
 * needs SWOS 2020's server: not shown. ---- */
enum { D_header = 0, D_teams = 2, D_beginner = 10, D_master = 14, D_news = 15, D_exit = 16 };
static int32_t s_pickTarget, s_pick;
static char    s_pickHead[40];
static int32_t s_pickTest, s_pickDrawn;     /* SWOS_SPEEDRUN_TEST_PICKER=1 (ModSpeedrun_Test) */

static void diff_init(void)
{
    snprintf(s_pickHead, sizeof s_pickHead, "%s - DIFFICULTY", kTarget[s_pickTarget].name);
    ModApi_MenuText(D_header, s_pickHead);
    ModApi_MenuVisible(D_news, 0);
    ModApi_MenuNav4(D_teams, -1, D_beginner, -1, D_exit);
    ModApi_MenuNav4(D_master, D_teams, -1, D_master - 1, D_exit);
    ModApi_MenuNav4(D_exit, D_teams, D_master, D_master, -1);
}

static void diff_set(int32_t v);
static void diff_draw(void)     /* (selectSpeedrunDifficultyMenuOnDraw: its trophies and stars) */
{
    /* SWOS 2020's screen x, y: its menus' entries 8 to the right of where they are imported, the
     * header's imported at 72, 0 */
#define AT(name, sx, sy, ox, oy, w, h) \
    (s_pickDrawn += ModApi_DrawMapAtEntry("speedrun", name, D_header, (sx) - 8 - 72, (sy), ox, oy, w, h, 0))
    static const int8_t kTrophy[5] = { 1, 3, 5, 4, 8 };
    const int32_t gap = 27, ox0 = 94 + 70, oy0 = 47, ox = 212 + 70, oy = 48;
    for (int32_t k = 0; k < 5; k++) {
        const int32_t t = kTrophy[k], shift = t == 1 || t == 3;
        AT("trophiesMap", ox0 + 2 * shift, oy0 + gap * k - 2 * shift, 0, 14 * t, 16, 14);
        AT("starsMap", ox, oy + gap * k, 0, 10 * (1 + 2 * k), 36, 10);
    }
    AT("arrows_default_scaled_Map", 40 + 42, 44 + 20, 0, 0, 24, 24);
#undef AT
    if (s_pickTest && ++s_pickTest > 3) {   /* (the test: a few frames drawn, then a preset) */
        fprintf(stdout, "SPEEDRUN PICKER: '%s', %d pixels of pictures a frame\n", s_pickHead,
                (int)(s_pickDrawn / (s_pickTest - 1)));
        fflush(stdout);
        s_pickTest = 0;
        diff_set(2);
    }
}

static void diff_set(int32_t v) { s_pick = v; SetExitMenuFlag(); }
static void diff0(void) { diff_set(0); }
static void diff1(void) { diff_set(1); }
static void diff2(void) { diff_set(2); }
static void diff3(void) { diff_set(3); }
static void diff4(void) { diff_set(4); }
static void diff_teams(void) { diff_set(5); }
static void diff_exit(void) { diff_set(-1); }

static void run(int32_t target, int32_t again)
{
    if (again || kTarget[target].kind == T_YELLOW || kTarget[target].kind == T_RED) {
        run_at(target, again, -1);
        return;
    }
    s_pickTarget = target;
    for (;;) {
        s_pick = -1;
        if (ModApi_ShowMenu("speedrun/selectSpeedrunDifficultyMenu") != 0) {
            run_at(target, 0, -1);          /* (no picker: the teams, as before it) */
            return;
        }
        if (s_pick < 0)
            return;
        if (run_at(target, 0, s_pick == 5 ? -1 : s_pick) || s_pick != 5)
            return;
    }
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
static void sr_open(void)
{
    /* a fork's own screens for it (SWOS United's: SWOS 2020's), when a mod offers them */
    if (ModApi_CallService("speedrun-screens", "open", "{}", NULL, 0) == 0)
        return;
    ModApi_ShowMenu("speedrun/speedrunMenu");
}

/* ---- the "speedrun" service: the targets for another mod's screens (docs/MODS.md §18) ----
 *   targets {}                      "<n>\t<name>\t<played>\t<best frames>\t<best: 12.34>" a target
 *   style   {}                      the gameplay style's name ("AMIGA", "DOS", "XBOX") and its
 *                                   frame rate: "<name>\t<fps>"
 *   run     {"target","difficulty"} plays it (difficulty 0..4 SWOS 2020's preconfigured teams,
 *                                   -1 the friendly's team screen), this mod's result screen and
 *                                   TRY AGAIN, then "done\t<0/1>\t<frames>" */
static int32_t sr_number(const char *args, const char *key, int32_t fallback)
{
#ifdef SWOS_SDL_BUILD
    char path[32];
    double v;
    snprintf(path, sizeof path, "$.%s", key);
    return mjson_get_number(args, (int)strlen(args), path, &v) ? (int32_t)v : fallback;
#else
    (void)args; (void)key;
    return fallback;
#endif
}

static int32_t sr_service(const char *method, const char *args, char *out, uint32_t cap)
{
    if (!out || !cap)
        return -1;
    out[0] = 0;
    if (!s_recRead)
        records_read();
    if (!strcmp(method, "targets")) {
        size_t n = 0;
        for (int32_t t = 0; t < kTargets && n < cap; t++) {
            char best[16];
            time_text(best, sizeof best, s_rec[t].best);
            n += (size_t)snprintf(out + n, cap - n, "%d\t%s\t%u\t%u\t%s\n", (int)t, kTarget[t].name,
                                  (unsigned)s_rec[t].played, (unsigned)s_rec[t].best, best);
        }
        return 0;
    }
    if (!strcmp(method, "style")) {
        extern int _gGameplayStyle;
        extern const char *Gameplay_StyleName(int style);
        snprintf(out, cap, "%s\t%d\n", Gameplay_StyleName(_gGameplayStyle), (int)ModApi_FrameRate());
        return 0;
    }
    if (!strcmp(method, "run")) {
        const int32_t target = sr_number(args, "target", -1), difficulty = sr_number(args, "difficulty", -1);
        if (target < 0 || target >= kTargets)
            return -1;
        run_at(target, 0, difficulty >= 0 && difficulty < 5 ? difficulty : -1);
        snprintf(out, cap, "done\t%d\t%u\n", (int)s_done, (unsigned)s_doneAt);
        return 0;
    }
    return -1;
}

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
        { "diffInit", diff_init }, { "diffDraw", diff_draw }, { "diffTeams", diff_teams },
#ifdef SWOS_SDL_BUILD
        { "dsInit", ds_init }, { "dsDraw", ds_draw }, { "dsPlay", ds_play }, { "dsExit", ds_exit },
        { "retryTry", retry_try }, { "retryExit", retry_exit },
#endif
        { "diff0", diff0 }, { "diff1", diff1 }, { "diff2", diff2 }, { "diff3", diff3 }, { "diff4", diff4 },
        { "diffNews", result_none }, { "diffExit", diff_exit },
    };
    ModApi_RegisterHandlers("speedrun", kHandlers, (int32_t)(sizeof kHandlers / sizeof kHandlers[0]));
    ModApi_RegisterService("speedrun", "speedrun", sr_service);
    ModApi_AddEntry(MODPOINT_MODES, "speedrun", "speedrun", sr_open);
}

#ifdef SWOS_SDL_BUILD
/* (src/mod_match.c ModMatch_SelfTest: SWOS_MOD_MATCH_TEST=speedrun:<n>) */
void ModSpeedrun_Test(int32_t target)
{
    if (target < 0 || target >= kTargets)
        exit(2);
    s_test = 1;
    if (getenv("SWOS_SPEEDRUN_TEST_PICKER")) {  /* the picker shown, drawn, ADVANCED picked */
        s_pickTest = 1;
        run(target, 0);
        exit(2);
    }
    {   /* SWOS_SPEEDRUN_TEST_DIFFICULTY=0..4: a preconfigured pair's (both the CPU's) */
        const char *d = getenv("SWOS_SPEEDRUN_TEST_DIFFICULTY");
        run_at(target, 0, d && *d >= '0' && *d <= '4' ? *d - '0' : -1);
    }
}
#endif

const ModDef ModDef_speedrun = {
    .slug = "speedrun",
    .init = sr_init,
    .matchEvent = sr_event,
    .matchFrame = sr_frame,
    .matchEnded = sr_ended,
#ifdef SWOS_SDL_BUILD
    .matchDrawn = sr_drawn,
    .frameUnder = sr_present,
    .frameKept = sr_kept,
#endif
};

