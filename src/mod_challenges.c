/*
 * mod_challenges.c -- the challenges mod (canonical, bin/mods/challenges; docs/MODS.md §18):
 * SWOS 2020's scenario mode (game.cpp ScenarioGame), ported onto the mod API, its challenges in
 * a JSON format of OpenSWOS's own (the owner: JSON is our format for data -- not challenges.ini,
 * not .SCN). Both targets.
 *
 * MODES -> CHALLENGES: the categories (with more than one: each pack's "category"), then a
 * category's challenges, each OPEN, DONE or LOCKED (locked while an easier one of its category is
 * not done, as SWOS 2020 locks them; a category with a "locked" challenge is closed whole). One: where the match stands and what wins it, PLAY; then
 * the match from there -- your team is player 1's, the other the CPU's -- and YOU BEAT THE
 * CHALLENGE or NOT THIS TIME, QUICK RETRY or EXIT.
 *
 * A challenge (data_json/challenges/<any>.json in this mod -- or in any mod that is on: a
 * challenge pack is a mod of such files):
 *   { "format": "openswos-challenges", "version": 2, "category": "CLASSICS",
 *     "challenges": [ {
 *       "id": "ten-men", "name": "TEN MEN", "difficulty": 3,          1 (easiest) .. 5
 *       "about": "1-1 AND YOUR NUMBER 5 SENT OFF. WIN WITH TEN.",
 *       "teams": [ { "uuid": "...", "team": "0a12", "name": "MANCHESTER UTD" }, { ... } ],   database teams by uuid
 *                                                    (and number, hex: the Amiga's, which has no database)
 *       "yours": 0,                                                    which team you play
 *       "score": [1, 1], "minute": 60,                                 where it stands (minute 0-119:
 *                                                                      from 90 extra time)
 *       "sent_off": [[5], []],                                         shirt numbers already off
 *       "extra_time": false, "penalties": false, "length": 0,          (optional)
 *       "win": "win" | "draw" (at least) | "exact", "win_score": [2, 2]   (exact: yours, theirs)
 *     -- version 2, each optional (SWOS 2020's scenarios, scripts/convert_swos2020_challenges.py):
 *       "category": "...",                                             its own, over the pack's
 *       "title": "...", "description": ["5 lines"], "win_message": "..."
 *       "teams": [ { "file": "teams/X.EDT", "name": "ENGLAND 1966" }, ... ]   a team from a .EDT in
 *                                                    this mod, found by its name (not the database)
 *       "playing_up": 0|1, "kick_off": 0|1,                            -1 / absent: drawn
 *       "subs": [min, max], "extra_time_rule": "golden_goal" | "silver_goal",
 *       "shootout": true,                                              a shoot-out alone
 *       "first_leg": [0, 4],                                           the second leg of a tie
 *       "injured": [[[shirt, level 1-7], ...], [...]], "locked": true, its category closed
 *       "restart": { "kind": "corner" | "free_kick" | "penalty" | "throw_in" | "goal_kick",
 *                    "team": 0|1|-1, "wing": 0|1|-1 (his left / right as he attacks),
 *                    "spot": 1-7|-1 (SWOS 2020's free-kick spots; 0 "x","y"), "deep": true }
 *     } ] }
 * Progress: save/mods/challenges/progress.json, named fields:
 *   { "format": "openswos-challenges-progress", "version": 1,
 *     "challenges": [ { "id": "ten-men", "completed": true, "attempts": 4 } ] }
 * The Amiga parses no JSON (docs/DATA_BSON.md): it reads each pack as the binary OpenSWOS Setup
 * writes beside it, challenges/<name>.BIN (include/challenge_pack.h, one reader of the
 * JSON for both), and keeps the player's own and the progress as binary records too:
 * CUSTOM.BIN (a pack) and PROGRESS.BIN ("OSWCPRG" + version, u16 count, then id[40], completed
 * u8, attempts u32 -- big-endian).
 * SWOS 2020's own challenges (challenges.ini and its team files) convert to a pack mod with
 * scripts/convert_swos2020_challenges.py -- never shipped in OpenSWOS: its teams are others'.
 *
 * "changes": "modes": its matches are its own -- never matters online.
 *
 * Online records (docs/MODS.md §33): when a mod offers the "online-records" service (SWOS
 * United's, on its server), the challenges ask it -- never waiting on it -- for
 *   challenges.completed  {}                           the player's: "<category>\t<name>" lines
 *   challenges.stats      {"name","category"}          a line for the challenge's page
 *   challenges.won        {"id","name","category"}     a challenge just beaten (sent, queued)
 * A challenge is known online by its NAME and its pack's "category", as SWOS 2020's were. No
 * such service: none of this, and nothing else changes.
 */
#include "mod_api.h"

#include "challenge_pack.h"
#include "endian_helpers.h"
#include "game_types.h"
#include "mjson.h"
#include "mods.h"
#include "swos_io.h"        /* (the Amiga's file read) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void SetExitMenuFlag(void);

#ifdef SWOS_SDL_BUILD
enum { kMax = 192 };                    /* (SWOS 2020's pack alone is 92) */
#else
enum { kMax = 112 };                    /* (each ~1.1 KB, and a stock A1200 has 2 MB) */
#endif
enum { kRows = 8 };
enum { W_WIN = CHALLENGE_WIN, W_DRAW = CHALLENGE_DRAW, W_EXACT = CHALLENGE_EXACT };
_Static_assert((int)CHALLENGE_RESTART_KICK_OFF == (int)MODRESTART_KICK_OFF &&
               (int)CHALLENGE_RESTART_CORNER == (int)MODRESTART_CORNER &&
               (int)CHALLENGE_RESTART_FREE_KICK == (int)MODRESTART_FREE_KICK &&
               (int)CHALLENGE_RESTART_PENALTY == (int)MODRESTART_PENALTY &&
               (int)CHALLENGE_RESTART_THROW_IN == (int)MODRESTART_THROW_IN &&
               (int)CHALLENGE_RESTART_GOAL_KICK == (int)MODRESTART_GOAL_KICK &&
               (int)CHALLENGE_ET_GOLDEN_GOAL == (int)MODET_GOLDEN_GOAL &&
               (int)CHALLENGE_ET_SILVER_GOAL == (int)MODET_SILVER_GOAL,
               "challenge_pack.h's restart kinds are MODRESTART_*, its extra-time rules MODET_*");

typedef struct {
    union {                             /* (the pack's fields, by name or as one ChallengeDef) */
        ChallengeDef def;
        struct { CHALLENGE_DEF_FIELDS };
    };
    uint8_t  custom;                    /* the player's own (custom.json, the editor) */
    int16_t  mod;                       /* the mod it came from (its teams' files); -1 none */
    int16_t  catIndex, seq;             /* (the list's order: its category's, then as read) */
    /* progress */
    uint8_t  completed;
    uint32_t attempts;
} Challenge;

static Challenge *s_ch;                 /* (kMax of them, from the heap when the list is first wanted:
                                         *  ~100 KB the Amiga keeps only while it is used) */
static int32_t   s_count, s_loaded;

/* ---- reading ---- */

static int32_t s_loadingMod = -1;       /* (the mod whose files load() is reading) */

static void add_defs(const ChallengeDef *defs, int32_t n, int32_t custom)
{
    for (int32_t i = 0; i < n && s_count < kMax; i++) {
        Challenge *c = &s_ch[s_count++];
        memset(c, 0, sizeof *c);
        c->def = defs[i];
        c->custom = (uint8_t)custom;    /* (its teams are looked up at PLAY: a uuid search walks
                                         * the whole database, too slow for drawing the list) */
        c->mod = (int16_t)(custom ? -1 : s_loadingMod);
        if (custom)
            snprintf(c->category, sizeof c->category, "CUSTOM");
    }
}

static ChallengeDef *s_defs;            /* (a file's, before they are added: kMax of them) */
static int32_t defs_ready(void)
{
    if (!s_defs)
        s_defs = (ChallengeDef *)malloc(sizeof(ChallengeDef) * kMax);
    return s_defs != NULL;
}
static void online_args(const Challenge *c, char *out, uint32_t cap);

#ifdef SWOS_SDL_BUILD
static int32_t number(const char *json, int len, const char *path, int32_t fallback)
{
    long v;
    return mjson_get_long(json, len, path, &v) ? (int32_t)v : fallback;
}

/* A challenges file's text (freed here): its challenges after those loaded. */
static void read_text(char *json, int len, const char *path, int32_t custom)
{
    if (!defs_ready()) {
        free(json);
        return;
    }
    const int32_t n = ChallengePack_FromJson(json, len, s_defs, kMax - s_count);
    if (n < 0) {
        fprintf(stderr, "[challenges] %s: not an openswos-challenges file\n", path);
    } else {
        add_defs(s_defs, n, custom);
    }
    free(json);
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
    read_text(json, (int)n, path, 0);
}
#else
/* A pack's binary (the caller frees it): its challenges after those loaded. */
static void read_pack(const uint8_t *data, uint32_t size, const char *path, int32_t custom)
{
    if (!defs_ready())
        return;
    const int32_t n = ChallengePack_Read(data, size, s_defs, kMax - s_count);
    if (n < 0)
        fprintf(stderr, "[challenges] %s: not a challenge pack\n", path);
    else
        add_defs(s_defs, n, custom);
}

static void read_file(const char *path)
{
    uint8_t *data = NULL;
    uint32_t n = 0;
    if (swos_io_load_data_alloc(path, &data, &n) != 0 || !data || !n) {
        swos_io_free(data);
        return;
    }
    read_pack(data, n, path, 0);
    swos_io_free(data);     /* (swos_io's heap, not malloc's) */
}
#endif

#ifdef SWOS_SDL_BUILD
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
#else
static void read_progress(void)
{
    uint32_t size = 0;
    const uint8_t *p = (const uint8_t *)ModApi_LoadFile("challenges", "PROGRESS.BIN", &size);
    if (!p)
        return;
    if (size >= 10 && memcmp(p, "OSWCPRG", 7) == 0 && p[7] == 1) {
        const uint32_t count = ((uint32_t)p[8] << 8) | p[9];
        for (uint32_t i = 0; i < count && 10 + (i + 1) * 45 <= size; i++) {
            const uint8_t *r = p + 10 + i * 45;
            char id[40];
            memcpy(id, r, 40);
            id[39] = 0;
            for (int32_t k = 0; k < s_count; k++)
                if (!strcmp(s_ch[k].id, id)) {
                    s_ch[k].completed = r[40];
                    s_ch[k].attempts = ((uint32_t)r[41] << 24) | ((uint32_t)r[42] << 16) | ((uint32_t)r[43] << 8) | r[44];
                }
        }
    }
    free((void *)p);
}

static void save_progress(void)
{
    static uint8_t buf[10 + kMax * 45];
    memcpy(buf, "OSWCPRG", 7);
    buf[7] = 1;
    buf[8] = (uint8_t)(s_count >> 8);
    buf[9] = (uint8_t)s_count;
    for (int32_t i = 0; i < s_count; i++) {
        uint8_t *r = buf + 10 + i * 45;
        memset(r, 0, 40);
        memcpy(r, s_ch[i].id, strlen(s_ch[i].id) < 39 ? strlen(s_ch[i].id) : 39);
        r[40] = s_ch[i].completed;
        r[41] = (uint8_t)(s_ch[i].attempts >> 24);
        r[42] = (uint8_t)(s_ch[i].attempts >> 16);
        r[43] = (uint8_t)(s_ch[i].attempts >> 8);
        r[44] = (uint8_t)s_ch[i].attempts;
    }
    ModApi_SaveFile("challenges", "PROGRESS.BIN", buf, (uint32_t)(10 + s_count * 45));
}
#endif

/* The list's order: the categories as they were first met (the packs' files in name order), and
 * in each the easiest first, then as the pack has them. */
static char    s_cats[24][32];
static int32_t s_numCats;
static int32_t category_of(const char *name)
{
    for (int32_t k = 0; k < s_numCats; k++)
        if (!strcmp(s_cats[k], name))
            return k;
    if (s_numCats == 24)
        return 23;
    snprintf(s_cats[s_numCats], sizeof s_cats[0], "%s", name);
    return s_numCats++;
}

static int by_category_difficulty(const void *a, const void *b)
{
    const Challenge *x = (const Challenge *)a, *y = (const Challenge *)b;
    if (x->catIndex != y->catIndex)
        return x->catIndex - y->catIndex;
    if (x->difficulty != y->difficulty)
        return x->difficulty - y->difficulty;
    return x->seq - y->seq;
}

/* (ModApi_ListFiles' callback: one file of a mod's data_json/challenges/, kept to be read in
 * name order -- the order a listing gives is the file system's) */
static char    s_names[64][64];
static int32_t s_numNames;
static int32_t each_file(const char *name, void *ctx)
{
    const size_t l = strlen(name);
    (void)ctx;
#ifdef SWOS_SDL_BUILD
    if (l > 5 && !strcmp(name + l - 5, ".json") && l < sizeof s_names[0] && s_numNames < 64)
#else
    if (l > 4 && (!strcmp(name + l - 4, ".BIN") || !strcmp(name + l - 4, ".bin")) && l < sizeof s_names[0] &&
        s_numNames < 64)
#endif
        snprintf(s_names[s_numNames++], sizeof s_names[0], "%s", name);
    return 0;
}

static int by_name(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

/* every mod that is on: its data_json/challenges/ */
static void load(void)
{
    if (s_loaded)
        return;
    s_loaded = 1;
    if (!s_ch && (s_ch = (Challenge *)calloc(kMax, sizeof *s_ch)) == NULL) {
        fprintf(stderr, "[challenges] no memory for the list\n");
        return;
    }
    for (int32_t m = 0; m < Mods_Count(); m++) {
        char dir[1200];
#ifdef SWOS_SDL_BUILD
        snprintf(dir, sizeof dir, "%s/data_json/challenges", Mods_Dir(m));
#else
        snprintf(dir, sizeof dir, "%s/challenges", Mods_Dir(m));   /* (the setup's binary) */
#endif
        s_loadingMod = m;
        s_numNames = 0;
        ModApi_ListFiles(dir, each_file, dir);
        qsort(s_names, (size_t)s_numNames, sizeof s_names[0], by_name);
        for (int32_t k = 0; k < s_numNames; k++) {
            char path[1500];
            snprintf(path, sizeof path, "%s/%s", dir, s_names[k]);
            read_file(path);
        }
    }
    s_loadingMod = -1;
    {   /* the player's own, after the packs' (the editor writes them: custom.json) */
        uint32_t size = 0;
#ifdef SWOS_SDL_BUILD
        char *json = (char *)ModApi_LoadFile("challenges", "custom.json", &size);
        if (json)
            read_text(json, (int)size, "custom.json", 1);
#else
        uint8_t *pack = (uint8_t *)ModApi_LoadFile("challenges", "CUSTOM.BIN", &size);
        if (pack) {
            read_pack(pack, size, "CUSTOM.BIN", 1);
            free(pack);     /* (ModApi_LoadFile's: malloc's) */
        }
#endif
    }
    s_numCats = 0;
    for (int32_t i = 0; i < s_count; i++) {
        s_ch[i].seq = (int16_t)i;
        s_ch[i].catIndex = (int16_t)category_of(s_ch[i].category);
    }
    qsort(s_ch, (size_t)s_count, sizeof s_ch[0], by_category_difficulty);
    read_progress();
    free(s_defs);
    s_defs = NULL;
    fprintf(stderr, "[challenges] %d challenges\n", (int)s_count);
}

/* A category is out of reach while any of its challenges is "locked" (SWOS 2020's
 * isCategoryLocked: its LOCKED scenarios close their category). */
static int32_t category_unavailable(const char *category)
{
    for (int32_t k = 0; k < s_count; k++)
        if (s_ch[k].unavailable && !strcmp(s_ch[k].category, category))
            return 1;
    return 0;
}

/* locked while an easier one of its category is not done (SWOS 2020's setScenariosLocked, over
 * the category's scenarios), or its category is (category_unavailable) */
static int32_t locked(int32_t i)
{
    if (s_ch[i].custom)
        return 0;
    if (category_unavailable(s_ch[i].category))
        return 1;
    for (int32_t k = 0; k < s_count; k++)
        if (!s_ch[k].custom && !strcmp(s_ch[k].category, s_ch[i].category) &&
            s_ch[k].difficulty < s_ch[i].difficulty && !s_ch[k].completed)
            return 1;
    return 0;
}

/* A team given in a .EDT of the challenge's mod (teams[t].file, SWOS 2020's teams): found there by
 * its name, as SWOS 2020 found it. 1 when it was. */
static int32_t file_team(const Challenge *c, int32_t t, TeamFileEx *out)
{
    char path[1300];
    uint8_t *data = NULL;
    uint32_t size = 0;
    int32_t found = 0;
    if (c->mod < 0 || !c->teamFile[t][0])
        return 0;
    snprintf(path, sizeof path, "%s/%s", Mods_Dir(c->mod), c->teamFile[t]);
    if (swos_io_load_data_alloc(path, &data, &size) != 0 || !data || size < 2) {
        swos_io_free(data);
        fprintf(stderr, "[challenges] %s: no team file %s\n", c->id, path);
        return 0;
    }
    const uint32_t count = ((uint32_t)data[0] << 8) | data[1];     /* (.EDT: big-endian) */
    for (uint32_t k = 0; k < count && 2 + (k + 1) * sizeof(TeamFileEx) <= size && !found; k++) {
        const uint8_t *rec = data + 2 + k * sizeof(TeamFileEx);
        char name[18];
        memcpy(name, ((const TeamFileEx *)(const void *)rec)->team.teamName, 17);
        name[17] = 0;
        for (char *e = name + strlen(name); e > name && e[-1] == ' ';)
            *--e = 0;
        if (!strcmp(name, c->teamName[t])) {
            memcpy(out, rec, sizeof *out);
            out->team.globalTeamNumber = (int16_t)BE16_TO_HOST((uint16_t)out->team.globalTeamNumber);
            found = 1;
        }
    }
    swos_io_free(data);
    if (!found)
        fprintf(stderr, "[challenges] %s: no %s in %s\n", c->id, c->teamName[t], path);
    return found;
}

/* SWOS 2020's free-kick spots across the pitch (game.cpp setAutoFreeKickPos), 1..7 */
static const int16_t kSpotX[8] = { 336, 148, 256, 304, 357, 405, 513, 548 };

static int32_t draw2(void) { return (int32_t)(ModApi_Ticks() >> 3) & 1; }   /* (a coin: SWOS 2020's RANDOM) */

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
    static TeamFileEx given[2];             /* (teams from the mod's files: the match plays copies) */
    int32_t fromFile[2];
    s_cur = i;
    for (int32_t t = 0; t < 2; t++) {
        fromFile[t] = c->teamFile[t][0] != 0;
        if (fromFile[t] ? !file_team(c, t, &given[t]) : !ModApi_TeamByUuidNumber(c->teamUuid[t], c->teamNumber[t])) {
            fprintf(stderr, "[challenges] %s: a team is not in the database (or a pack that is on)\n", c->id);
            return;
        }
    }
    for (;;) {
        ModMatchSetup setup;
        ModMatchResult r;
        ModApi_DefaultSetup(&setup);
        for (int32_t t = 0; t < 2; t++) {
            if (fromFile[t])
                setup.teamData[t] = &given[t];
            else
                setup.team[t] = ModApi_TeamByUuidNumber(c->teamUuid[t], c->teamNumber[t]);
            for (int32_t k = 0; k < 8; k++) {
                setup.injured[t][k].shirt = c->injured[t][k].shirt;
                setup.injured[t][k].level = c->injured[t][k].level;
            }
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
        setup.playingUp = c->playingUp;
        setup.kickOff = c->kickOff;
        if (c->subsSet) {
            setup.subsMin = c->subsMin;
            setup.subsMax = c->subsMax;
        }
        setup.extraTimeRule = c->extraTimeRule;
        setup.shootoutOnly = c->shootoutOnly;
        setup.secondLeg = c->secondLeg;
        setup.firstLeg[0] = c->firstLeg[0];
        setup.firstLeg[1] = c->firstLeg[1];
        /* the start: what is drawn, drawn now (SWOS 2020's RANDOM), then where */
        if (setup.playingUp < 0)
            setup.playingUp = (int8_t)draw2();
        {
            const int32_t taker = c->restartTeam >= 0 ? c->restartTeam
                                : c->kickOff >= 0 ? c->kickOff : draw2();
            const int32_t wing = c->restartWing >= 0 ? c->restartWing : draw2();
            const int32_t attacksTop = setup.playingUp == taker;
            setup.restart.kind = c->restartKind;
            setup.restart.team = (uint8_t)taker;
            setup.restart.wing = (uint8_t)wing;
            setup.restart.x = c->restartX;
            setup.restart.y = c->restartY;
            if (c->restartKind == CHALLENGE_RESTART_FREE_KICK && c->restartSpot) {
                const int32_t spot = c->restartSpot > 0 ? c->restartSpot : 1 + (int32_t)(ModApi_Ticks() % 7);
                setup.restart.x = kSpotX[spot];
                setup.restart.y = (int16_t)(attacksTop ? (c->restartDeep ? 602 : 281) : (c->restartDeep ? 401 : 602));
            } else if (c->restartKind == CHALLENGE_RESTART_THROW_IN) {
                /* (SWOS 2020's: 160 short of the goal he attacks, wing as he attacks) */
                const int32_t screenLeft = attacksTop ? !wing : wing;
                setup.restart.x = (int16_t)(screenLeft ? 71 : 600);
                setup.restart.y = (int16_t)(attacksTop ? 289 : 609);
            }
            if (c->restartKind != CHALLENGE_RESTART_KICK_OFF && setup.kickOff < 0)
                setup.kickOff = (int8_t)taker;
        }
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
        if (s_won) {                        /* (online: queued, sent in the background) */
            char args[256];
            online_args(c, args, sizeof args);
            ModApi_CallService("online-records", "challenges.won", args, NULL, 0);
        }
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
/* ---- online records (the "online-records" service, when a mod offers it) ---- */

static int32_t s_onlineMerged;          /* the player's online completions merged this run */
static char    s_onlineLine[48];

static void online_args(const Challenge *c, char *out, uint32_t cap)
{
    char name[64], category[64], id[80];
    snprintf(out, cap, "{\"id\":\"%s\",\"name\":\"%s\",\"category\":\"%s\"}",
             ModApi_JsonEscape(c->id, id, sizeof id), ModApi_JsonEscape(c->name, name, sizeof name),
             ModApi_JsonEscape(c->category, category, sizeof category));
}

/* Each frame the list is up, until the answer has come: the challenges the player has beaten on
 * another machine (or before this one had them) marked done here too. */
static void online_merge(void)
{
#ifdef SWOS_SDL_BUILD
    static char lines[16384];
#else
    static char lines[64];                  /* (the Amiga: no service, nothing to merge) */
#endif
    if (s_onlineMerged)
        return;
    const int32_t r = ModApi_CallService("online-records", "challenges.completed", "{}", lines, sizeof lines);
    if (r == 1)
        return;
    s_onlineMerged = 1;
    if (r != 0)
        return;
    int32_t marked = 0;
    for (char *line = lines; line && *line;) {
        char *next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        char *tab = strchr(line, '\t');
        if (tab) {
            *tab = 0;
            for (int32_t i = 0; i < s_count; i++)
                if (!s_ch[i].completed && !strcmp(s_ch[i].name, tab + 1) && !strcmp(s_ch[i].category, line)) {
                    s_ch[i].completed = 1;
                    marked++;
                }
        }
        line = next;
    }
    if (marked) {
        save_progress();
        fprintf(stderr, "[challenges] %d done online, marked here\n", (int)marked);
    }
}

/* The list: with more than one category, the categories first (SWOS 2020's SELECT CATEGORY), a
 * category's challenges on picking it; EXIT there goes back to them. */
static int32_t s_page, s_cat = -1;

static int32_t show_categories(void) { return s_numCats > 1 && s_cat < 0; }
static int32_t cur_category(void) { return s_numCats > 1 ? s_cat : 0; }

/* The items of the view: categories, or the challenges of one (their first index, their count:
 * a category's are together, the list being sorted by it). */
static int32_t item_count(int32_t *first)
{
    *first = 0;
    if (show_categories())
        return s_numCats;
    if (s_numCats <= 1)
        return s_count;
    int32_t n = 0;
    for (int32_t i = 0; i < s_count; i++)
        if (s_ch[i].catIndex == cur_category()) {
            if (!n)
                *first = i;
            n++;
        }
    return n;
}

static int32_t page_count(void)
{
    int32_t first;
    const int32_t n = item_count(&first);
    return n ? (n + kPerPage - 1) / kPerPage : 1;
}
static int32_t page_rows(void)
{
    int32_t first;
    const int32_t left = item_count(&first) - s_page * kPerPage;
    return left < kPerPage ? (left < 0 ? 0 : left) : kPerPage;
}

static void category_counts(int32_t k, int32_t *done, int32_t *all)
{
    *done = *all = 0;
    for (int32_t i = 0; i < s_count; i++)
        if (s_ch[i].catIndex == k) {
            (*all)++;
            *done += s_ch[i].completed;
        }
}

static void list_draw(void)
{
    static const char *const kStars[6] = { "", "*", "**", "***", "****", "*****" };
    static char more[24], head[40];
    int32_t first;
    load();
    item_count(&first);
    const int32_t n = page_rows();
    if (show_categories()) {
        ModApi_MenuText(M_header, "CHALLENGES");
        ModApi_MenuText(M_subTitle, "CHOOSE A CATEGORY");
        for (int32_t r = 0; r < n; r++) {
            const int32_t k = s_page * kPerPage + r;
            int32_t done, all;
            char value[16];
            category_counts(k, &done, &all);
            if (category_unavailable(s_cats[k]))
                snprintf(value, sizeof value, "LOCKED");
            else
                snprintf(value, sizeof value, "%d/%d", (int)done, (int)all);
            row(r, s_cats[k][0] ? s_cats[k] : "CHALLENGES", value, 1);
        }
    } else {
        snprintf(head, sizeof head, "%s", s_numCats > 1 && s_cats[cur_category()][0] ? s_cats[cur_category()]
                                                                                     : "CHALLENGES");
        ModApi_MenuText(M_header, head);
        ModApi_MenuText(M_subTitle, "WIN FROM WHERE THE MATCH STANDS");
        for (int32_t r = 0; r < n; r++) {
            const int32_t i = first + s_page * kPerPage + r;
            char label[48];
            snprintf(label, sizeof label, "%s %s", s_ch[i].title[0] ? s_ch[i].title : s_ch[i].name,
                     s_ch[i].custom ? "(YOURS)"
                     : kStars[s_ch[i].difficulty < 0 ? 0 : s_ch[i].difficulty > 5 ? 5 : s_ch[i].difficulty]);
            row(r, label, s_ch[i].completed ? "DONE" : locked(i) ? "LOCKED" : "OPEN", 1);
        }
    }
    const int32_t withNew = show_categories() || s_numCats <= 1;
    if (withNew)
        row(n, "", "NEW CHALLENGE", 0);
    if (page_count() > 1) {
        snprintf(more, sizeof more, "MORE (%d/%d)", (int)s_page + 1, (int)page_count());
        row(n + withNew, "", more, 0);
        hide_from(n + withNew + 1);
    } else {
        hide_from(n + withNew);
    }
    online_merge();
    ModApi_MenuText(M_note, !s_count ? "NONE: A CHALLENGE PACK IS A MOD"
                            : show_categories() ? "" : "THE EASIER ONES OPEN THE HARDER");
}

static void list_init(void)
{
    load();
    if (s_page >= page_count())
        s_page = 0;
    nav_rows(page_rows() + (show_categories() || s_numCats <= 1) + (page_count() > 1));
    list_draw();
}

static void edit_new(void);

static void pick(int32_t r)
{
    int32_t first;
    item_count(&first);
    const int32_t n = page_rows();
    const int32_t withNew = show_categories() || s_numCats <= 1;
    if (withNew && r == n) {                /* NEW CHALLENGE */
        edit_new();
        return;
    }
    if (r == n + withNew && page_count() > 1) {   /* MORE */
        s_page = (s_page + 1) % page_count();
        list_init();
        ModApi_MenuSelect(M_label0 + 2 * r + 1);
        return;
    }
    if (r >= n)
        return;
    if (show_categories()) {
        const int32_t k = s_page * kPerPage + r;
        if (category_unavailable(s_cats[k])) {
            ModApi_ShowError("NOT IN THIS VERSION YET");
            return;
        }
        s_cat = k;
        s_page = 0;
        list_init();
        ModApi_MenuSelect(M_label0 + 1);
        return;
    }
    const int32_t i = first + s_page * kPerPage + r;
    if (i >= s_count || locked(i))
        return;
    s_cur = i;
    ModApi_ShowMenu("challenges/challengeInfo");
}

static void list_exit(void)
{
    if (s_numCats > 1 && s_cat >= 0) {      /* (a category's list: back to the categories) */
        s_page = s_cat / kPerPage;
        s_cat = -1;
        list_init();
        return;
    }
    SetExitMenuFlag();
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
    s_edit.playingUp = s_edit.kickOff = -1;     /* (drawn, as a friendly's) */
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

#ifndef SWOS_SDL_BUILD
static void save_custom(void)   /* (the Amiga: CUSTOM.BIN, a pack) */
{
    const uint32_t cap = CHALLENGE_PACK_HEADER + kMax * CHALLENGE_PACK_RECORD;
    ChallengeDef *defs = (ChallengeDef *)malloc(sizeof(ChallengeDef) * kMax);
    uint8_t *buf = (uint8_t *)malloc(cap);
    int32_t count = 0, number = 1;
    if (!defs || !buf) {
        free(defs);
        free(buf);
        ModApi_ShowError("NO MEMORY TO SAVE IT");
        return;
    }
    for (int32_t i = 0; i < s_count; i++)
        if (s_ch[i].custom)
            number++;
    snprintf(s_edit.id, sizeof s_edit.id, "custom-%d-%u", (int)number, (unsigned)ModApi_Ticks());
    snprintf(s_edit.name, sizeof s_edit.name, "MY CHALLENGE %d", (int)number);
    snprintf(s_edit.about, sizeof s_edit.about, "YOUR OWN CHALLENGE");
    for (int32_t i = 0; i <= s_count && count < kMax; i++) {
        const Challenge *c = i < s_count ? &s_ch[i] : &s_edit;
        if (c->custom)
            defs[count++] = c->def;
    }
    const int32_t n = ChallengePack_Write(defs, count, buf, cap);
    if (n > 0)
        ModApi_SaveFile("challenges", "CUSTOM.BIN", buf, (uint32_t)n);
    free(defs);
    free(buf);
}
#else
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
#endif

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
    char score[24], minute[16], win[24];
    snprintf(s_head, sizeof s_head, "%s", c->title[0] ? c->title : c->name);
    snprintf(s_sub, sizeof s_sub, "%s", c->winMessage[0] ? c->winMessage : c->about);
    ModApi_MenuText(M_header, s_head);
    ModApi_MenuText(M_subTitle, s_sub);
    if (c->secondLeg)       /* (the aggregate: this leg's start and the first's) */
        snprintf(score, sizeof score, "%d-%d (%d-%d)", (int)c->score[0], (int)c->score[1],
                 (int)(c->score[0] + c->firstLeg[0]), (int)(c->score[1] + c->firstLeg[1]));
    else
        snprintf(score, sizeof score, "%d-%d", (int)c->score[0], (int)c->score[1]);
    if (c->shootoutOnly)
        snprintf(minute, sizeof minute, "PENALTIES");
    else if (c->minute >= 90)
        snprintf(minute, sizeof minute, "%d (E.T.)", (int)c->minute);
    else
        snprintf(minute, sizeof minute, "%d", (int)c->minute);
    if (c->extraTimeRule == CHALLENGE_ET_GOLDEN_GOAL)
        snprintf(win, sizeof win, "GOLDEN GOAL");
    else if (c->extraTimeRule == CHALLENGE_ET_SILVER_GOAL)
        snprintf(win, sizeof win, "SILVER GOAL");
    else if (c->win == W_EXACT)
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
    {   /* online: what the service says of it ("BEATEN BY 12 PLAYERS"), when there is one */
        char args[256];
        online_args(c, args, sizeof args);
        const int32_t r = ModApi_CallService("online-records", "challenges.stats", args, s_onlineLine,
                                             sizeof s_onlineLine);
        if (r == 0 && s_onlineLine[0])
            row(6, "ONLINE", s_onlineLine, 1);
        else if (r == 1)
            row(6, "ONLINE", "...", 1);
        hide_from(r == 0 && s_onlineLine[0] ? 7 : r == 1 ? 7 : 6);
    }
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
    snprintf(s_head, sizeof s_head, "%s", c->title[0] ? c->title : c->name);
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
static void ch_open(void)
{
    s_cat = -1;
    s_page = 0;
    ModApi_ShowMenu("challenges/challengesMenu");
}

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
        { "exit", ch_exit }, { "listExit", list_exit },
    };
    ModApi_RegisterHandlers("challenges", kHandlers, (int32_t)(sizeof kHandlers / sizeof kHandlers[0]));
    ModApi_AddEntry(MODPOINT_MODES, "challenges", "challenges", ch_open);
}

const ModDef ModDef_challenges = {
    .slug = "challenges",
    .init = ch_init,
};
