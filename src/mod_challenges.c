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
 * CHALLENGE or NOT THIS TIME, QUICK RETRY, CHANGE TACTICS (the PLAY MATCH screen first) or EXIT.
 * NEW CHALLENGE (and EDIT on one of the player's own) is SWOS 2020's DESIGN SCENARIO editor; the
 * player's own have SORT BY in their list (both below).
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
 *       "record_name": "...",                                          its name online when longer
 *                                                                      than "name"'s 31 letters
 *       "restart": { "kind": "corner" | "free_kick" | "penalty" | "throw_in" | "goal_kick"
 *                            | "random" (SWOS 2020's RANDOM: drawn at PLAY),
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

#include <stdarg.h>
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
                                         * the whole database, too slow for drawing the list)
                                         * 1 the player's own, 2 fetched for the player */
        c->mod = (int16_t)(custom ? -1 : s_loadingMod);
        if (custom == 1 || (custom && !c->category[0]))
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

/* Progress of challenges not loaded now (a pack mod switched off): kept as it was read and
 * written back, so playing one challenge never loses another pack's. */
typedef struct { char id[40]; uint8_t completed; uint32_t attempts; } Kept;
enum { kKept = 256 };
static Kept   *s_kept;
static int32_t s_numKept;
static void keep(const char *id, uint8_t completed, uint32_t attempts)
{
    for (int32_t k = 0; k < s_count; k++)
        if (!strcmp(s_ch[k].id, id))
            return;
    for (int32_t k = 0; k < s_numKept; k++)
        if (!strcmp(s_kept[k].id, id))
            return;
    if (!s_kept && (s_kept = (Kept *)malloc(sizeof(Kept) * kKept)) == NULL)
        return;
    if (s_numKept == kKept)
        return;
    snprintf(s_kept[s_numKept].id, sizeof s_kept[0].id, "%s", id);
    s_kept[s_numKept].completed = completed;
    s_kept[s_numKept++].attempts = attempts;
}

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
        int b = 0;
        snprintf(q, sizeof q, "$.challenges[%d].completed", (int)i);
        const uint8_t completed = (uint8_t)(mjson_get_bool(json, (int)size, q, &b) && b);
        snprintf(q, sizeof q, "$.challenges[%d].attempts", (int)i);
        const uint32_t attempts = (uint32_t)number(json, (int)size, q, 0);
        int32_t found = 0;
        for (int32_t k = 0; k < s_count; k++)
            if (!strcmp(s_ch[k].id, id)) {
                s_ch[k].completed = completed;
                s_ch[k].attempts = attempts;
                found = 1;
            }
        if (!found)
            keep(id, completed, attempts);
    }
    free(json);
}

static void save_progress(void)
{
    const uint32_t cap = (uint32_t)(s_count + s_numKept) * 96 + 128;
    char *buf = (char *)malloc(cap);
    if (!buf)
        return;
    const int32_t total = s_count + s_numKept;
    int n = snprintf(buf, cap, "{\n \"format\": \"openswos-challenges-progress\",\n \"version\": 1,\n \"challenges\": [\n");
    for (int32_t i = 0; i < total && n < (int)cap - 120; i++) {
        const int32_t kept = i >= s_count;
        const char *id = kept ? s_kept[i - s_count].id : s_ch[i].id;
        const int32_t completed = kept ? s_kept[i - s_count].completed : s_ch[i].completed;
        const uint32_t attempts = kept ? s_kept[i - s_count].attempts : s_ch[i].attempts;
        n += snprintf(buf + n, cap - (size_t)n, "  { \"id\": \"%s\", \"completed\": %s, \"attempts\": %u }%s\n",
                      id, completed ? "true" : "false", (unsigned)attempts, i + 1 < total ? "," : "");
    }
    n += snprintf(buf + n, cap - (size_t)n, " ]\n}\n");
    ModApi_SaveFile("challenges", "progress.json", buf, (uint32_t)n);
    free(buf);
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
            const uint32_t attempts = ((uint32_t)r[41] << 24) | ((uint32_t)r[42] << 16) | ((uint32_t)r[43] << 8) | r[44];
            int32_t found = 0;
            for (int32_t k = 0; k < s_count; k++)
                if (!strcmp(s_ch[k].id, id)) {
                    s_ch[k].completed = r[40];
                    s_ch[k].attempts = attempts;
                    found = 1;
                }
            if (!found)
                keep(id, r[40], attempts);
        }
    }
    free((void *)p);
}

static void save_progress(void)
{
    const int32_t total = s_count + s_numKept;
    uint8_t *buf = (uint8_t *)malloc((size_t)(10 + total * 45));
    if (!buf)
        return;
    memcpy(buf, "OSWCPRG", 7);
    buf[7] = 1;
    buf[8] = (uint8_t)(total >> 8);
    buf[9] = (uint8_t)total;
    for (int32_t i = 0; i < total; i++) {
        const int32_t kept = i >= s_count;
        const char *id = kept ? s_kept[i - s_count].id : s_ch[i].id;
        const uint32_t attempts = kept ? s_kept[i - s_count].attempts : s_ch[i].attempts;
        uint8_t *r = buf + 10 + i * 45;
        memset(r, 0, 40);
        memcpy(r, id, strlen(id) < 39 ? strlen(id) : 39);
        r[40] = kept ? s_kept[i - s_count].completed : s_ch[i].completed;
        r[41] = (uint8_t)(attempts >> 24);
        r[42] = (uint8_t)(attempts >> 16);
        r[43] = (uint8_t)(attempts >> 8);
        r[44] = (uint8_t)attempts;
    }
    ModApi_SaveFile("challenges", "PROGRESS.BIN", buf, (uint32_t)(10 + total * 45));
    free(buf);
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
 * name order across ALL the mods -- the order a listing gives is the file system's, and the
 * mods' is the folder's: so a pack's categories come where its file names put them, "01-..."
 * before "challenges.json") */
typedef struct { char name[64]; int16_t mod; } PackFile;
enum { kMaxPacks = 64 };
static PackFile *s_packs;               /* (kMaxPacks, from the heap while load() runs) */
static int32_t   s_numPacks;
static int32_t each_file(const char *name, void *ctx)
{
    const size_t l = strlen(name);
#ifdef SWOS_SDL_BUILD
    if (l > 5 && !strcmp(name + l - 5, ".json") && l < sizeof s_packs[0].name && s_numPacks < kMaxPacks)
#else
    if (l > 4 && (!strcmp(name + l - 4, ".BIN") || !strcmp(name + l - 4, ".bin")) && l < sizeof s_packs[0].name &&
        s_numPacks < kMaxPacks)
#endif
    {
        snprintf(s_packs[s_numPacks].name, sizeof s_packs[0].name, "%s", name);
        s_packs[s_numPacks++].mod = (int16_t)*(const int32_t *)ctx;
    }
    return 0;
}

static int by_name(const void *a, const void *b)
{
    const PackFile *x = (const PackFile *)a, *y = (const PackFile *)b;
    const int c = strcmp(x->name, y->name);
    return c ? c : x->mod - y->mod;
}

static void pack_dir(int32_t m, char *dir, uint32_t cap)
{
#ifdef SWOS_SDL_BUILD
    snprintf(dir, cap, "%s/data_json/challenges", Mods_Dir(m));
#else
    snprintf(dir, cap, "%s/challenges", Mods_Dir(m));   /* (the setup's binary) */
#endif
}

static void sort_custom(void);

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
    s_packs = (PackFile *)malloc(sizeof(PackFile) * kMaxPacks);
    s_numPacks = 0;
    for (int32_t m = 0; m < Mods_Count() && s_packs; m++) {
        char dir[1200];
        pack_dir(m, dir, sizeof dir);
        ModApi_ListFiles(dir, each_file, &m);
    }
    if (s_packs)
        qsort(s_packs, (size_t)s_numPacks, sizeof s_packs[0], by_name);
    for (int32_t k = 0; k < s_numPacks; k++) {
        char dir[1200], path[1300];
        pack_dir(s_packs[k].mod, dir, sizeof dir);
        snprintf(path, sizeof path, "%s/%s", dir, s_packs[k].name);
        s_loadingMod = s_packs[k].mod;
        read_file(path);
    }
    free(s_packs);
    s_packs = NULL;
    s_loadingMod = -1;
    {   /* the player's own, after the packs' (the editor writes them: custom.json) */
        uint32_t size = 0;
#ifdef SWOS_SDL_BUILD
        char *json = (char *)ModApi_LoadFile("challenges", "custom.json", &size);
        if (json)
            read_text(json, (int)size, "custom.json", 1);
        /* challenges another mod fetched for the player (SWOS United: SWOS 2020's shared custom
         * challenges), their teams' .EDT files beside it in the save folder: never locked, their
         * pack's category kept */
        json = (char *)ModApi_LoadFile("challenges", "shared.json", &size);
        if (json)
            read_text(json, (int)size, "shared.json", 2);
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
    sort_custom();                      /* (the player's own: SORT BY's order) */
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
    int32_t saved = 0;      /* (a fetched challenge's: in the save folder, malloc's) */
    if (!c->teamFile[t][0])
        return 0;
    if (c->mod < 0) {
        data = (uint8_t *)ModApi_LoadFile("challenges", c->teamFile[t], &size);
        saved = 1;
        snprintf(path, sizeof path, "save: %s", c->teamFile[t]);
    } else {
        snprintf(path, sizeof path, "%s/%s", Mods_Dir(c->mod), c->teamFile[t]);
        if (swos_io_load_data_alloc(path, &data, &size) != 0) {
            swos_io_free(data);
            data = NULL;
        }
    }
    if (!data || size < 2) {
        if (saved)
            free(data);
        else
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
    if (saved)
        free(data);
    else
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
static int32_t s_trying;                /* the editor's TEST: no attempt, no progress, nothing online */

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
    s_retry = 0;
    for (;;) {
        ModMatchSetup setup;
        ModMatchResult r;
        ModApi_DefaultSetup(&setup);
        /* CHANGE TACTICS (SWOS 2020's retryExitEx: g_RetryExit 2, g_SkipPlayMatchMenu 0): the
         * friendly's PLAY MATCH screen first, its EDIT TACTICS and line-up; QUICK RETRY skips it */
        setup.playMatchScreen = (uint8_t)(s_retry == 2 && !s_test);
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
        if (c->shootoutOnly)
            setup.manualKeeper = 1;         /* (SWOS 2020: "PENALTY SHOOTOUT, MANUAL GK CONTROL") */
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
            uint8_t kind = c->restartKind, deep = c->restartDeep;
            int8_t spotSet = c->restartSpot;
            if (kind == CHALLENGE_RESTART_RANDOM) {     /* (SWOS 2020's RANDOM: one of six, now) */
                static const uint8_t kDrawn[6] = { CHALLENGE_RESTART_KICK_OFF, CHALLENGE_RESTART_FREE_KICK,
                                                   CHALLENGE_RESTART_FREE_KICK, CHALLENGE_RESTART_CORNER,
                                                   CHALLENGE_RESTART_THROW_IN, CHALLENGE_RESTART_GOAL_KICK };
                const int32_t r = (int32_t)((ModApi_Ticks() >> 4) % 6);
                kind = kDrawn[r];
                deep = (uint8_t)(r == 2);
                spotSet = (int8_t)(r == 1 || r == 2 ? -1 : 0);
            }
            setup.restart.kind = kind;
            setup.restart.team = (uint8_t)taker;
            setup.restart.wing = (uint8_t)wing;
            setup.restart.x = c->restartX;
            setup.restart.y = c->restartY;
            if (kind == CHALLENGE_RESTART_FREE_KICK && spotSet) {
                const int32_t spot = spotSet > 0 ? spotSet : 1 + (int32_t)(ModApi_Ticks() % 7);
                setup.restart.x = kSpotX[spot];
                setup.restart.y = (int16_t)(attacksTop ? (deep ? 602 : 281) : (deep ? 401 : 602));
            } else if (kind == CHALLENGE_RESTART_THROW_IN) {
                /* (SWOS 2020's: 160 short of the goal he attacks, wing as he attacks) */
                const int32_t screenLeft = attacksTop ? !wing : wing;
                setup.restart.x = (int16_t)(screenLeft ? 71 : 600);
                setup.restart.y = (int16_t)(attacksTop ? 289 : 609);
            }
            if (kind != CHALLENGE_RESTART_KICK_OFF && setup.kickOff < 0)
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
        s_won = won(c, &r);
        if (!s_trying) {
            c->attempts++;
            if (s_won)
                c->completed = 1;
            save_progress();
        }
        if (s_won && !s_trying) {           /* (online: queued, sent in the background) */
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
    char name[140], category[64], id[80];
    snprintf(out, cap, "{\"id\":\"%s\",\"name\":\"%s\",\"category\":\"%s\"}",
             ModApi_JsonEscape(c->id, id, sizeof id),
             ModApi_JsonEscape(c->recordName[0] ? c->recordName : c->name, name, sizeof name),
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
                if (!s_ch[i].completed && !strcmp(s_ch[i].recordName[0] ? s_ch[i].recordName : s_ch[i].name, tab + 1) &&
                    !strcmp(s_ch[i].category, line)) {
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
/* SORT BY under the player's own (SWOS 2020's custom challenges list) */
static int32_t with_sort(void)
{
    if (show_categories() || s_numCats <= 1)    /* (one category: its rows hold NEW CHALLENGE) */
        return 0;
    for (int32_t i = 0; i < s_count; i++)
        if (s_ch[i].catIndex == s_cat && s_ch[i].custom)
            return 1;
    return 0;
}
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
                     s_ch[i].custom == 1 ? "(YOURS)"
                     : kStars[s_ch[i].difficulty < 0 ? 0 : s_ch[i].difficulty > 5 ? 5 : s_ch[i].difficulty]);
            row(r, label, s_ch[i].completed ? "DONE" : locked(i) ? "LOCKED" : "OPEN", 1);
        }
    }
    const int32_t withNew = show_categories() || s_numCats <= 1, withSort = with_sort();
    if (withNew)
        row(n, "", "NEW CHALLENGE", 0);
    if (withSort)
        row(n + withNew, "", "SORT BY", 0);
    if (page_count() > 1) {
        snprintf(more, sizeof more, "MORE (%d/%d)", (int)s_page + 1, (int)page_count());
        row(n + withNew + withSort, "", more, 0);
        hide_from(n + withNew + withSort + 1);
    } else {
        hide_from(n + withNew + withSort);
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
    nav_rows(page_rows() + (show_categories() || s_numCats <= 1) + with_sort() + (page_count() > 1));
    list_draw();
}

static void edit_new(void);

static void pick(int32_t r)
{
    int32_t first;
    item_count(&first);
    const int32_t n = page_rows();
    const int32_t withNew = show_categories() || s_numCats <= 1, withSort = with_sort();
    if (withNew && r == n) {                /* NEW CHALLENGE */
        edit_new();
        list_init();                        /* (it may have saved one) */
        return;
    }
    if (withSort && r == n + withNew) {     /* SORT BY */
        ModApi_ShowMenu("challenges/customChallengesSortByMenu");
        list_init();
        return;
    }
    if (r == n + withNew + withSort && page_count() > 1) {   /* MORE */
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

static int32_t s_editing;              /* (the editor is up) */

/* ---- the editor: SWOS 2020's DESIGN SCENARIO (customChallengesEdit.mnu, imported, and its
 * handlers in game.cpp: customChallengesEditMenu_*) over a challenge of the format's own fields.
 * NEW CHALLENGE picks the two teams on the friendly's screen (who plays each: yours and the CPU's),
 * then the screen: the teams (either picks both again), its name, the score at the start, the
 * minute, the first leg's score, extra time / penalties / a direct shoot-out, the substitutes,
 * SET CARDS AND INJURIES (customChallengesSentOffInjuryNew.mnu), the direction attacked, the
 * pitch, the starting event and its taker, what wins it and its difficulty; then TEST (played
 * once, kept nowhere), SAVE (the player's custom.json / CUSTOM.BIN), DELETE and EXIT. One of the
 * player's own opens here again from its page (EDIT). Not here: SWOS 2020's UNIFORMS (a challenge
 * keeps no kits) and PITCH SET (its own pitch art), hidden; its CPU INJURY was switched off in
 * SWOS 2020 itself. ---- */

enum {  /* customChallengesEditMenu's entries (scripts/import_swos2020_menus.py's ordinals) */
    E_header = 0, E_team1, E_teamScore, E_team2, E_nickname, E_scoreLabel, E_team1Score, E_score_,
    E_team2Score, E_scoreReset, E_uniformsLabel, E_uniforms, E_timeLabel, E_time, E_minus10, E_minus1,
    E_reset0, E_plus1, E_plus10, E_legLabel, E_secondLeg, E_legTeam1, E_leg_, E_legTeam2, E_legReset,
    E_etPenLabel, E_et, E_pen, E_directPen, E_subsLabel, E_subsMin, E_subsFrom, E_subsMax, E_cards,
    E_attackLabel, E_attack, E_pitchTypeLabel, E_pitchType, E_eventLabel, E_startTeam, E_event,
    E_winLabel, E_winTeam, E_win, E_diffLabel, E_difficulty, E_pitchSetLabel, E_pitchSet, E_test,
    E_save, E_delete, E_exit, E_background
};
/* SWOS 2020's menu colours */
enum { kDarkBlue = 3, kLightBrownOrange = 4, kRedTeam = 10, kPurpleTeam = 11, kLightBlue = 13, kGreen = 14 };
enum { kScoreLimit = 40 };      /* (SWOS 2020's SCORE_LIMIT) */

/* the starting event, SWOS 2020's names for it */
enum { SE_KICK_OFF, SE_FREE_KICK, SE_FREE_KICK_HOME, SE_CORNER, SE_THROW_IN, SE_GOAL_KICK, SE_RANDOM, SE_SHOOTOUT };
/* the win condition */
enum { WC_ANY, WC_DRAW, WC_GOLDEN, WC_SILVER };

static Challenge s_edit;
static int32_t   s_editExisting;        /* s_edit is one of the player's own (its id kept) */

static int32_t ask(const char *title, const char *text)   /* SWOS 2020's g_ScenMessage: YES / NO */
{
#ifdef SWOS_SDL_BUILD
    static const char *const kYesNo[2] = { "YES", "NO" };
    return ModApi_ShowDialog(title, text, kYesNo, 2, 0) == 0;
#else
    (void)title; (void)text;
    return 1;                           /* (the Amiga has no dialog) */
#endif
}

static void tell(const char *title, const char *text)
{
#ifdef SWOS_SDL_BUILD
    static const char *const kOk[1] = { "OK" };
    ModApi_ShowDialog(title, text, kOk, 1, 0);
#else
    (void)title;
    ModApi_ShowError(text);
#endif
}

static int32_t se_of(const Challenge *c)
{
    if (c->shootoutOnly)
        return SE_SHOOTOUT;
    switch (c->restartKind) {
    case CHALLENGE_RESTART_FREE_KICK: return c->restartDeep ? SE_FREE_KICK_HOME : SE_FREE_KICK;
    case CHALLENGE_RESTART_CORNER: return SE_CORNER;
    case CHALLENGE_RESTART_THROW_IN: return SE_THROW_IN;
    case CHALLENGE_RESTART_GOAL_KICK: return SE_GOAL_KICK;
    case CHALLENGE_RESTART_RANDOM: return SE_RANDOM;
    default: return SE_KICK_OFF;
    }
}

static void se_set(Challenge *c, int32_t se)
{
    static const uint8_t kKind[7] = { CHALLENGE_RESTART_KICK_OFF, CHALLENGE_RESTART_FREE_KICK,
                                      CHALLENGE_RESTART_FREE_KICK, CHALLENGE_RESTART_CORNER,
                                      CHALLENGE_RESTART_THROW_IN, CHALLENGE_RESTART_GOAL_KICK,
                                      CHALLENGE_RESTART_RANDOM };
    c->restartKind = kKind[se < 0 || se > SE_RANDOM ? 0 : se];
    c->restartSpot = (int8_t)(se == SE_FREE_KICK || se == SE_FREE_KICK_HOME ? -1 : 0);
    c->restartDeep = (uint8_t)(se == SE_FREE_KICK_HOME);
    c->restartWing = -1;                /* (SWOS 2020's RANDOM corner, throw-in and goal kick) */
    c->restartX = 336;
    c->restartY = 449;
}

static int32_t wc_of(const Challenge *c)
{
    if (c->extraTimeRule == CHALLENGE_ET_GOLDEN_GOAL)
        return WC_GOLDEN;
    if (c->extraTimeRule == CHALLENGE_ET_SILVER_GOAL)
        return WC_SILVER;
    return c->win == W_DRAW ? WC_DRAW : WC_ANY;
}

static void wc_set(Challenge *c, int32_t wc)
{
    c->win = (uint8_t)(wc == WC_DRAW ? W_DRAW : W_WIN);
    c->extraTimeRule = (uint8_t)(wc == WC_GOLDEN ? CHALLENGE_ET_GOLDEN_GOAL
                                 : wc == WC_SILVER ? CHALLENGE_ET_SILVER_GOAL : CHALLENGE_ET_NORMAL);
}

/* (SWOS 2020's ScenarioGame::isKickOffInitialTime: these minutes start with a kick-off) */
static int32_t kick_off_minute(void)
{
    const int32_t m = s_edit.minute;
    return m == 0 || m == 45 || m == 90 || m == 105;
}

/* (humanTeamHasSentOffOrInjuryPlayers) */
static int32_t yours_short(void)
{
    const int32_t y = s_edit.yours;
    if (s_edit.sentOff[y])
        return 1;
    for (int32_t k = 0; k < 8; k++)
        if (s_edit.injured[y][k].shirt)
            return 1;
    return 0;
}

static void reset_all_scores(void)
{
    s_edit.score[0] = s_edit.score[1] = 0;
    s_edit.firstLeg[0] = s_edit.firstLeg[1] = 0;
}

/* SWOS 2020's setTotalScoresToSame: a start that must be level is (a second leg's aggregate at 90
 * and 120, a golden / silver goal or a shoot-out from 90). mode 0: this leg's score moved, 1: the
 * first leg's; team: the one changed. */
static void same_scores(int32_t mode, int32_t team)
{
    Challenge *c = &s_edit;
    if (c->minute == 0) {
        c->score[0] = c->score[1] = 0;
        return;
    }
    const int32_t goldenSilver = c->extraTimeRule != CHALLENGE_ET_NORMAL;
    if (goldenSilver || c->shootoutOnly || c->secondLeg) {
        const int32_t level = c->secondLeg ? (c->minute == 90 || c->minute == 120) : c->minute > 90;
        if (level) {
            int32_t s1 = c->score[0], s2 = c->score[1], l1 = c->firstLeg[0], l2 = c->firstLeg[1];
            if (c->secondLeg) {
                const int32_t a = s1 + l1, b = s2 + l2;
                if (a != b) {
                    if (mode == 0) {    /* (the first leg's evens it) */
                        if (a > b) l2 += a - b; else l1 += b - a;
                    } else {
                        if (a > b) s2 += a - b; else s1 += b - a;
                    }
                    if (l1 > kScoreLimit || l2 > kScoreLimit || s1 > kScoreLimit || s2 > kScoreLimit) {
                        reset_all_scores();
                        return;
                    }
                    c->score[0] = (int8_t)s1; c->score[1] = (int8_t)s2;
                    c->firstLeg[0] = (int8_t)l1; c->firstLeg[1] = (int8_t)l2;
                }
            } else if (team == 0) {
                c->score[1] = c->score[0];
            } else {
                c->score[0] = c->score[1];
            }
        } else {
            if (goldenSilver && !c->secondLeg && c->minute == 90) {
                if (team == 0) c->score[1] = c->score[0]; else c->score[0] = c->score[1];
            }
            if (c->secondLeg && c->firstLeg[0] == c->firstLeg[1] && c->minute == 0)
                c->score[0] = c->score[1] = 0;
        }
    } else if (!c->secondLeg && c->minute == 90) {
        if (team == 0) c->score[1] = c->score[0]; else c->score[0] = c->score[1];
    }
}

/* (customChallengesEditMenu_directPen_onSelect) */
static void ed_directPen(void)
{
    Challenge *c = &s_edit;
    if (c->shootoutOnly) {
        c->shootoutOnly = 0;
        c->minute = 0;
        se_set(c, SE_KICK_OFF);
        c->subsSet = 1;
        c->subsMin = 2;
        c->subsMax = 5;
    } else {
        c->shootoutOnly = 1;
        c->minute = 120;
        wc_set(c, WC_ANY);
        c->extraTime = c->penalties = 0;
        se_set(c, SE_KICK_OFF);
        c->subsSet = 1;
        c->subsMin = 0;
        c->subsMax = 5;
        /* (the taker's goal: SWOS 2020 turns the attack to face it) */
        c->playingUp = (int8_t)(c->kickOff == 0 ? 1 : 0);
        same_scores(0, 0);
        same_scores(1, 0);
    }
    same_scores(0, 0);
}

static void change_minute(int32_t v)
{
    Challenge *c = &s_edit;
    int32_t m = v == 0 ? 0 : c->minute + v;
    m = m < 0 ? 0 : m > 120 ? 120 : m;
    c->minute = (int16_t)m;
    if (c->shootoutOnly && m != 120)
        ed_directPen();             /* (off again: SWOS 2020's) */
    if (kick_off_minute())
        se_set(c, SE_KICK_OFF);
    if (m == 120 && !c->shootoutOnly)
        ed_directPen();
    if (m >= 90 && m != 120)
        c->extraTime = 1;
    same_scores(0, 0);
}

static void ed_minus10(void) { change_minute(-10); }
static void ed_minus1(void) { change_minute(-1); }
static void ed_reset0(void) { change_minute(0); }
static void ed_plus1(void) { change_minute(1); }
static void ed_plus10(void) { change_minute(10); }

static void ed_team1Score(void)
{
    if (++s_edit.score[0] > kScoreLimit) s_edit.score[0] = 0;
    same_scores(0, 0);
}
static void ed_team2Score(void)
{
    if (++s_edit.score[1] > kScoreLimit) s_edit.score[1] = 0;
    same_scores(0, 1);
}
static void ed_scoreReset(void)
{
    s_edit.score[0] = s_edit.score[1] = 0;
    same_scores(0, 0);
}
static void ed_secondLeg(void)
{
    s_edit.secondLeg = (uint8_t)!s_edit.secondLeg;
    if (!s_edit.secondLeg)
        s_edit.firstLeg[0] = s_edit.firstLeg[1] = 0;
    same_scores(1, 0);
}
static void ed_legTeam(int32_t t)
{
    if (!s_edit.secondLeg)
        return;
    if (++s_edit.firstLeg[t] > kScoreLimit) s_edit.firstLeg[t] = 0;
    same_scores(1, 0);
}
static void ed_legTeam1(void) { ed_legTeam(0); }
static void ed_legTeam2(void) { ed_legTeam(1); }
static void ed_legReset(void)
{
    s_edit.firstLeg[0] = s_edit.firstLeg[1] = 0;
    same_scores(1, 0);
}
static void ed_et(void)
{
    Challenge *c = &s_edit;
    if (c->shootoutOnly || c->minute >= 90 || c->extraTimeRule != CHALLENGE_ET_NORMAL)
        return;
    c->extraTime = (uint8_t)!c->extraTime;
    if (!c->extraTime)
        c->penalties = 0;
}
static void ed_pen(void)
{
    if (s_edit.penalties)
        s_edit.penalties = 0;
    else if (s_edit.extraTime)
        s_edit.penalties = 1;
}
static void ed_subsMin(void)
{
    Challenge *c = &s_edit;
    if (c->shootoutOnly || yours_short())
        return;
    c->subsSet = 1;
    if (++c->subsMin > c->subsMax)
        c->subsMin = 0;
}
static void ed_subsMax(void)
{
    Challenge *c = &s_edit;
    if (c->shootoutOnly || yours_short())
        return;
    c->subsSet = 1;
    if (++c->subsMax > 5)
        c->subsMin = c->subsMax = 0;
}
static void ed_attack(void)     /* DOWN -> UP -> RANDOM: the first team's direction */
{
    if (s_edit.shootoutOnly)
        return;
    /* (playingUp is the team attacking the top goal: team 1 DOWN is team 2 attacking it) */
    s_edit.playingUp = (int8_t)(s_edit.playingUp == 1 ? 0 : s_edit.playingUp == 0 ? -1 : 1);
}
static void ed_startTeam(void)  /* TEAM1 -> TEAM2 -> RANDOM */
{
    Challenge *c = &s_edit;
    c->kickOff = (int8_t)(c->kickOff == 0 ? 1 : c->kickOff == 1 ? -1 : 0);
    c->restartTeam = c->kickOff;
    if (c->shootoutOnly)
        c->playingUp = (int8_t)(c->kickOff == 0 ? 1 : 0);
}
static void ed_event(void)
{
    Challenge *c = &s_edit;
    if (c->shootoutOnly || kick_off_minute())
        return;
    const int32_t se = se_of(c);
    se_set(c, se == SE_KICK_OFF ? SE_FREE_KICK : se == SE_RANDOM ? SE_KICK_OFF : se + 1);
}
static void ed_win(void)
{
    Challenge *c = &s_edit;
    if (c->shootoutOnly)
        return;
    const int32_t wc = wc_of(c);
    if (wc == WC_ANY) {
        wc_set(c, WC_DRAW);
    } else if (wc == WC_DRAW) {
        wc_set(c, WC_GOLDEN);
        c->extraTime = 1;
        same_scores(0, 0);
        same_scores(1, 0);
    } else if (wc == WC_GOLDEN) {
        wc_set(c, WC_SILVER);
        c->extraTime = 1;
        if (c->minute >= 105)
            c->minute = 104;
        same_scores(0, 0);
        same_scores(1, 0);
    } else {
        wc_set(c, WC_ANY);
    }
}
static void ed_difficulty(void)
{
    if (++s_edit.difficulty > 5)
        s_edit.difficulty = 1;
}
static void ed_pitchType(void)  /* FROZEN .. HARD, RANDOM */
{
    s_edit.pitchType = (int8_t)(s_edit.pitchType >= 6 ? -1 : s_edit.pitchType + 1);
}

/* the teams: the friendly's team screen, both (SWOS 2020 picks one and asks WILL YOU TAKE
 * CONTROL?: here the screen says who plays each) -- 1 when two were chosen */
static int32_t edit_teams(void)
{
    if (!ModApi_ChooseTeams())
        return 0;
    char uuid[2][40];
    for (int32_t t = 0; t < 2; t++)
        if (!ModApi_ChosenTeamUuid(t, uuid[t], (int32_t)sizeof uuid[t])) {
            ModApi_ShowError("THAT TEAM HAS NO ID: CHOOSE ANOTHER");
            return 0;
        }
    for (int32_t t = 0; t < 2; t++) {
        snprintf(s_edit.teamName[t], sizeof s_edit.teamName[t], "%s", ModApi_ChosenName(t));
        snprintf(s_edit.teamUuid[t], sizeof s_edit.teamUuid[t], "%s", uuid[t]);
        s_edit.teamNumber[t] = ModApi_ChosenTeamNumber(t);
        s_edit.teamFile[t][0] = 0;
    }
    s_edit.yours = (int8_t)(ModApi_ChosenIsPlayer(1) && !ModApi_ChosenIsPlayer(0) ? 1 : 0);
    s_edit.sentOff[0] = s_edit.sentOff[1] = 0;   /* (another squad: its shirts are not these) */
    memset(s_edit.injured, 0, sizeof s_edit.injured);
    return 1;
}
static void ed_team(void) { edit_teams(); }

static void ed_nickname(void)   /* SWOS 2020's scenario NICKNAME: the challenge's title */
{
    static char text[32];
    snprintf(text, sizeof text, "%s", s_edit.title);
    if (ModApi_MenuInputText(E_nickname, text, 30))
        snprintf(s_edit.title, sizeof s_edit.title, "%s", text);
}

static void edit_open(void)
{
    s_editing = 1;
    ModApi_ShowMenu("challenges/customChallengesEditMenu");
    s_editing = 0;
}

static void edit_new(void)
{
    memset(&s_edit, 0, sizeof s_edit);
    if (!edit_teams())
        return;
    for (int32_t k = 0; k < 8; k++)
        s_edit.scorers[k].team = -1;
    /* SWOS 2020's new scenario (customMakeScenario...): MATCH START, a random free kick by the
     * first team, either direction, WIN BY ANY SCORE, three stars, 2 to 5 substitutes */
    s_edit.minute = 0;
    s_edit.pitchType = -1;
    s_edit.playingUp = -1;
    s_edit.kickOff = s_edit.restartTeam = 0;
    se_set(&s_edit, SE_FREE_KICK);
    wc_set(&s_edit, WC_ANY);
    s_edit.difficulty = 3;
    s_edit.subsSet = 1;
    s_edit.subsMin = 2;
    s_edit.subsMax = 5;
    s_edit.custom = 1;
    s_edit.mod = -1;
    snprintf(s_edit.title, sizeof s_edit.title, "NEW CHALLENGE");
    snprintf(s_edit.category, sizeof s_edit.category, "CUSTOM");
    s_editExisting = 0;
    edit_open();
}

static void edit_existing(int32_t i)
{
    s_edit = s_ch[i];
    s_editExisting = 1;
    edit_open();
}

/* The player's own, written again: every one of the list's (the one with `skipId` left out), and
 * `extra` -- custom.json on the PC, CUSTOM.BIN (a pack) on the Amiga. 1 written. */
#ifndef SWOS_SDL_BUILD
static int32_t write_custom(const Challenge *extra, const char *skipId)
{
    const uint32_t cap = CHALLENGE_PACK_HEADER + kMax * CHALLENGE_PACK_RECORD;
    ChallengeDef *defs = (ChallengeDef *)malloc(sizeof(ChallengeDef) * kMax);
    uint8_t *buf = (uint8_t *)malloc(cap);
    int32_t count = 0, ok = 0;
    if (defs && buf) {
        for (int32_t i = 0; i <= s_count && count < kMax; i++) {
            const Challenge *c = i < s_count ? &s_ch[i] : extra;
            if (!c || c->custom != 1 || (i < s_count && skipId && !strcmp(c->id, skipId)))
                continue;
            defs[count++] = c->def;
        }
        const int32_t n = ChallengePack_Write(defs, count, buf, cap);
        ok = n > 0 && ModApi_SaveFile("challenges", "CUSTOM.BIN", buf, (uint32_t)n) == 0;
    }
    free(defs);
    free(buf);
    if (!ok)
        ModApi_ShowError("NO MEMORY TO SAVE IT");
    return ok;
}
#else
static int32_t write_custom(const Challenge *extra, const char *skipId)
{
    const uint32_t cap = (uint32_t)(s_count + 1) * 4096 + 256;
    char *buf = (char *)malloc(cap);
    if (!buf)
        return 0;
    int n = snprintf(buf, cap, "{\n \"format\": \"openswos-challenges\",\n \"version\": 2,\n \"category\": \"CUSTOM\",\n"
                                " \"challenges\": [\n");
    int32_t count = 0, ok = 1;
    for (int32_t i = 0; i <= s_count && ok; i++) {
        const Challenge *c = i < s_count ? &s_ch[i] : extra;
        if (!c || c->custom != 1 || (i < s_count && skipId && !strcmp(c->id, skipId)))
            continue;
        if (count++)
            n += snprintf(buf + n, cap - (size_t)n, ",\n");
        const int32_t w = ChallengePack_ToJson(&c->def, buf + n, cap - (uint32_t)n - 16);
        if (w < 0)
            ok = 0;
        else
            n += w;
    }
    n += snprintf(buf + n, cap - (size_t)n, "\n ]\n}\n");
    if (ok)
        ok = ModApi_SaveFile("challenges", "custom.json", buf, (uint32_t)n) == 0;
    free(buf);
    return ok;
}
#endif

static void reload_list(void)
{
    s_loaded = 0;
    s_count = 0;
    s_numKept = 0;                      /* (read again with the rest) */
    load();
}

/* SAVE: one of the player's own, new or replacing itself (its id kept). 1 saved. */
static int32_t edit_store(void)
{
    if (!s_editExisting) {
        int32_t number = 1;
        if (s_count >= kMax) {
            tell("CUSTOM CHALLENGES", "NO ROOM FOR ANOTHER CHALLENGE");
            return 0;
        }
        for (int32_t i = 0; i < s_count; i++)
            if (s_ch[i].custom == 1)
                number++;
        snprintf(s_edit.id, sizeof s_edit.id, "custom-%d-%u", (int)number, (unsigned)ModApi_Ticks());
        snprintf(s_edit.name, sizeof s_edit.name, "MY CHALLENGE %d", (int)number);
    }
    snprintf(s_edit.about, sizeof s_edit.about, "%s V %s", s_edit.teamName[0], s_edit.teamName[1]);
    snprintf(s_edit.category, sizeof s_edit.category, "CUSTOM");
    if (!write_custom(&s_edit, s_editExisting ? s_edit.id : NULL))
        return 0;
    s_editExisting = 1;
    reload_list();
    return 1;
}

static int32_t edit_remove(void)
{
    if (!write_custom(NULL, s_edit.id))
        return 0;
    reload_list();
    s_editExisting = 0;
    return 1;
}

static void ed_save(void)
{
    if (ask("CUSTOM CHALLENGES", "SAVE?") && edit_store())
        tell("CUSTOM CHALLENGES", "SAVED");
}

static void ed_delete(void)
{
    if (!s_editExisting) {              /* (never saved: nothing to delete -- leave it) */
        SetExitMenuFlag();
        return;
    }
    if (!ask("CUSTOM CHALLENGES", "DELETE?") || !edit_remove())
        return;
    tell("CUSTOM CHALLENGES", "DELETED");
    SetExitMenuFlag();
}

/* TEST: played once from where it is set, as SWOS 2020's mainLoop(true) -- no progress kept */
static void ed_test(void)
{
    if (s_count >= kMax) {
        tell("CUSTOM CHALLENGES", "NO ROOM TO TEST IT");
        return;
    }
    const int32_t at = s_count++;       /* (past the list's own, while it is played) */
    s_ch[at] = s_edit;
    if (!s_ch[at].id[0])
        snprintf(s_ch[at].id, sizeof s_ch[at].id, "test");
    s_trying = 1;
    play(at);
    s_trying = 0;
    s_count--;
}

static void ed_exit(void) { SetExitMenuFlag(); }
static void ed_none(void) {}

static const char *minute_text(char *out, size_t cap, int32_t m)  /* (its INITIAL TIME line) */
{
    if (m == 0) return "MATCH START";
    if (m == 45) return "2ND HALF";
    if (m == 90) return "EXTRA TIME";
    if (m == 120) return "A.E.T.";
    const int32_t t = m % 100;
    snprintf(out, cap, "%d%s MINUTE", (int)m,
             t >= 11 && t <= 13 ? "TH" : m % 10 == 1 ? "ST" : m % 10 == 2 ? "ND" : m % 10 == 3 ? "RD" : "TH");
    return out;
}

static void ed_init(void)
{
    ModApi_MenuVisible(E_uniformsLabel, 0);
    ModApi_MenuVisible(E_uniforms, 0);
    ModApi_MenuVisible(E_pitchSetLabel, 0);
    ModApi_MenuVisible(E_pitchSet, 0);
    /* (the hidden ones' neighbours: the rows beside them instead) */
    ModApi_MenuNav4(E_scoreReset, E_team2Score, -1, E_nickname, E_minus10);
    ModApi_MenuNav4(E_difficulty, -1, -1, E_win, E_exit);
    ModApi_MenuNav4(E_win, E_startTeam, -1, E_event, E_difficulty);
    ModApi_MenuNav4(E_exit, E_delete, -1, E_difficulty, -1);
}

static void ed_draw(void)
{
    static char team[2][32], score[8], s1[8], s2[8], minute[24], l1[8], l2[8], sMin[4], sMax[4],
                startTeam[32], winTeam[32], nick[32];
    static const char *const kEvent[8] = { "KICK OFF", "FREE KICK (OPPO. SIDE)", "FREE KICK (HOME SIDE)",
                                           "CORNER KICK", "THROW IN", "GOAL KICK", "RANDOM", "PENALTY SHOOTOUT" };
    static const char *const kWinText[4] = { "WIN BY ANY SCORE", "AT LEAST DRAW", "GOLDEN GOAL", "SILVER GOAL" };
    static const char *const kPitch[8] = { "RANDOM", "FROZEN", "MUDDY", "WET", "SOFT", "NORMAL", "DRY", "HARD" };
    const Challenge *c = &s_edit;
    const int32_t cpu = 1 - c->yours;
    snprintf(nick, sizeof nick, "%s", c->title);
    ModApi_MenuText(E_nickname, nick);
    snprintf(team[0], sizeof team[0], "%s ", c->teamName[0]);
    snprintf(team[1], sizeof team[1], " %s", c->teamName[1]);
    ModApi_MenuText(E_team1, team[0]);
    ModApi_MenuText(E_team2, team[1]);
    snprintf(score, sizeof score, "%d-%d", (int)c->score[0], (int)c->score[1]);
    ModApi_MenuText(E_teamScore, score);
    snprintf(s1, sizeof s1, "%d", (int)c->score[0]);
    snprintf(s2, sizeof s2, "%d", (int)c->score[1]);
    ModApi_MenuText(E_team1Score, s1);
    ModApi_MenuText(E_team2Score, s2);
    ModApi_MenuColour(E_team1, cpu == 1 ? kPurpleTeam : kRedTeam);
    ModApi_MenuColour(E_team2, cpu == 1 ? kRedTeam : kPurpleTeam);
    ModApi_MenuText(E_time, minute_text(minute, sizeof minute, c->minute));

    const int32_t dp = c->shootoutOnly;
    const int32_t dpOff = dp ? kDarkBlue : kLightBlue;
    ModApi_MenuText(E_directPen, dp ? "DIRECT PENALTY SHOOTOUT" : "NO DIRECT PENALTY SHOOTOUT");
    int32_t et = dpOff, pen = dpOff, win = dpOff, event = dpOff;
    ModApi_MenuColour(E_subsMin, dpOff);
    ModApi_MenuColour(E_subsMax, dpOff);
    ModApi_MenuColour(E_attack, dpOff);
    if (!dp && kick_off_minute())
        event = kDarkBlue;
    for (int32_t e = E_minus10; e <= E_plus10; e++)
        ModApi_MenuColour(e, kLightBlue);
    ModApi_MenuText(E_secondLeg, c->secondLeg ? "YES" : "NO");
    snprintf(l1, sizeof l1, "%d", (int)c->firstLeg[0]);
    snprintf(l2, sizeof l2, "%d", (int)c->firstLeg[1]);
    ModApi_MenuText(E_legTeam1, l1);
    ModApi_MenuText(E_legTeam2, l2);
    ModApi_MenuText(E_et, c->extraTime ? "ET" : "NO ET");
    ModApi_MenuText(E_pen, c->penalties ? "PEN" : "NO PEN");
    snprintf(sMin, sizeof sMin, "%d", (int)c->subsMin);
    snprintf(sMax, sizeof sMax, "%d", (int)c->subsMax);
    ModApi_MenuText(E_subsMin, sMin);
    ModApi_MenuText(E_subsMax, sMax);
    ModApi_MenuText(E_attack, c->playingUp == 1 ? "DOWN" : c->playingUp == 0 ? "UP" : "RANDOM");
    snprintf(startTeam, sizeof startTeam, "%s", c->kickOff == 0 ? c->teamName[0] : c->kickOff == 1 ? c->teamName[1]
                                                                                                  : "RANDOM");
    ModApi_MenuText(E_startTeam, startTeam);
    ModApi_MenuText(E_event, kEvent[se_of(c)]);
    snprintf(winTeam, sizeof winTeam, "%s", c->teamName[c->yours]);
    ModApi_MenuText(E_winTeam, winTeam);
    const int32_t wc = wc_of(c);
    ModApi_MenuText(E_win, kWinText[wc]);
    if (wc == WC_ANY && !dp)
        et = kLightBlue;
    if (wc == WC_DRAW)
        et = kLightBlue;
    if (c->minute >= 90)
        et = kDarkBlue;
    else if (!c->extraTime)
        pen = kDarkBlue;
    if (wc == WC_GOLDEN || wc == WC_SILVER)
        et = kDarkBlue;
    ModApi_MenuColour(E_et, et);
    ModApi_MenuColour(E_pen, pen);
    ModApi_MenuColour(E_win, win);
    ModApi_MenuColour(E_event, event);
    if (yours_short()) {
        ModApi_MenuColour(E_subsMin, kDarkBlue);
        ModApi_MenuColour(E_subsMax, kDarkBlue);
    }
    ModApi_MenuText(E_pitchType, kPitch[c->pitchType < 0 || c->pitchType > 6 ? 0 : c->pitchType + 1]);
    ModApi_MenuColour(E_legTeam1, c->secondLeg ? kLightBlue : kDarkBlue);
    ModApi_MenuColour(E_legTeam2, c->secondLeg ? kLightBlue : kDarkBlue);
    ModApi_MenuColour(E_scoreReset, c->score[0] || c->score[1] ? kLightBrownOrange : kDarkBlue);
    ModApi_MenuColour(E_legReset, c->firstLeg[0] || c->firstLeg[1] ? kLightBrownOrange : kDarkBlue);
    ModApi_MenuText(E_difficulty, "");
}

static void ed_background(void)  /* (customChallengesEditMenu_onBackground: the difficulty in stars) */
{
    const int32_t star = s_edit.difficulty < 1 ? 1 : s_edit.difficulty > 5 ? 5 : s_edit.difficulty;
    int32_t x, y, w, h;
    ModApi_MenuEntryRect(E_difficulty, &x, &y, &w, &h);
    ModApi_DrawMapAtEntry("challenges", "starsMap", E_difficulty, (w - (star * 8 - 2 * (star - 1))) / 2, 1, 0,
                          10 * (1 + 2 * (star - 1)), 36, 10, 0);
}

/* ---- CARDS AND INJURIES (customChallengesSentOffInjuryNew.mnu): each team's sixteen by shirt
 * number, a red card or an injury level (1..7) each -- never the goalkeeper's (shirt 1), never
 * both; OK keeps them (at most 4 sent off a team, and 8 injured: the format's), CANCEL not. ---- */
enum {
    SO_T1_SENT = 1, SO_T1_INJ = 19, SO_T1_NO = 37, SO_T1_NAME = 55, SO_T1_POS = 73,
    SO_T2_SENT = 109, SO_T2_INJ = 127, SO_T2_NO = 145, SO_T2_NAME = 163, SO_T2_POS = 181,
    SO_DESC = 199, SO_RESET, SO_OK, SO_EXIT
};
static uint8_t s_soSent[2][16], s_soInj[2][16];
static char    s_soName[2][16][16], s_soPos[2][16][4], s_soNo[16][4], s_soDesc[24];

static int32_t so_base(int32_t t, int32_t col)  /* col 0 sent, 1 injury, 2 number, 3 name, 4 position */
{
    static const int16_t kBase[2][5] = { { SO_T1_SENT, SO_T1_INJ, SO_T1_NO, SO_T1_NAME, SO_T1_POS },
                                         { SO_T2_SENT, SO_T2_INJ, SO_T2_NO, SO_T2_NAME, SO_T2_POS } };
    return kBase[t][col];
}

static void so_init(void)
{
    static const char *const kPos[8] = { "GK", "RB", "LB", "D", "RW", "LW", "M", "A" };
    memset(s_soSent, 0, sizeof s_soSent);
    memset(s_soInj, 0, sizeof s_soInj);
    memset(s_soName, 0, sizeof s_soName);
    memset(s_soPos, 0, sizeof s_soPos);
    for (int32_t t = 0; t < 2; t++) {
        for (int32_t j = 0; j < 16; j++)
            s_soSent[t][j] = (uint8_t)((s_edit.sentOff[t] >> (j + 1)) & 1);
        for (int32_t k = 0; k < 8; k++)
            if (s_edit.injured[t][k].shirt >= 1 && s_edit.injured[t][k].shirt <= 16)
                s_soInj[t][s_edit.injured[t][k].shirt - 1] = s_edit.injured[t][k].level;
        const uint16_t id = ModApi_TeamByUuidNumber(s_edit.teamUuid[t], s_edit.teamNumber[t]);
        for (int32_t slot = 0; slot < 16 && id; slot++) {
            ModTeamPlayer p;
            if (!ModApi_TeamPlayer(id, slot, &p) || p.shirt < 1 || p.shirt > 16)
                continue;
            const char *sur = strrchr(p.name, ' ');     /* (getSurname) */
            snprintf(s_soName[t][p.shirt - 1], sizeof s_soName[t][0], "%s", sur ? sur + 1 : p.name);
            snprintf(s_soPos[t][p.shirt - 1], sizeof s_soPos[t][0], "%s", kPos[p.position & 7]);
        }
    }
    for (int32_t j = 0; j < 16; j++)
        snprintf(s_soNo[j], sizeof s_soNo[j], "%d", (int)j + 1);
    /* the navigation (SWOS 2020 sets it each frame): rows 2..16 (shirt 1 never), across the four
     * columns, the last row down to CANCEL */
    for (int32_t j = 1; j < 16; j++)
        for (int32_t t = 0; t < 2; t++)
            for (int32_t col = 0; col < 2; col++) {
                const int32_t e = so_base(t, col) + 2 + j;
                const int32_t next = col == 0 ? so_base(t, 1) + 2 + j : so_base(1 - t, 0) + 2 + j;
                const int32_t prev = col == 1 ? so_base(t, 0) + 2 + j : so_base(1 - t, 1) + 2 + j;
                ModApi_MenuNav4(e, prev, next, j == 1 ? -1 : e - 1, j == 15 ? SO_EXIT : e + 1);
            }
}

static void so_draw(void)
{
    static char name[2][32];
    const int32_t cpu = 1 - s_edit.yours;
    for (int32_t t = 0; t < 2; t++) {
        const int32_t head = so_base(t, 0);
        snprintf(name[t], sizeof name[t], "%s", s_edit.teamName[t]);
        ModApi_SetMenuEntryRect(head, -1, -1, 132, 10);
        ModApi_MenuText(head, name[t]);
        ModApi_MenuColour(head, t == cpu ? kRedTeam : kPurpleTeam);
        for (int32_t j = 0; j < 16; j++) {
            ModApi_MenuText(so_base(t, 2) + 2 + j, s_soNo[j]);
            ModApi_MenuText(so_base(t, 3) + 2 + j, s_soName[t][j]);
            ModApi_MenuColour(so_base(t, 3) + 2 + j, t != cpu ? (j <= 10 ? kLightBlue : kPurpleTeam)
                                                           : (j <= 10 ? kRedTeam : kLightBrownOrange));
            ModApi_MenuText(so_base(t, 4) + 2 + j, s_soPos[t][j]);
            if (s_soSent[t][j])         /* (drawRedCard: the card, half size) */
                ModApi_DrawMapAtEntry("challenges", "cards_24x15_5x1_Map", so_base(t, 0) + 2 + j, 2, 0, 24, 0,
                                      24, 15, 1);
            if (s_soInj[t][j]) {        /* (drawInjuries) */
                const int32_t v = s_soInj[t][j], idx = v == 1 ? 1 : v + 2;
                ModApi_DrawMapAtEntry("challenges", "cards_injuries_map_16x10x_1x10_map", so_base(t, 1) + 2 + j,
                                      v == 1 ? 1 : 0, 0, 16 * idx, 0, 16, 10, 0);
            }
        }
    }
    const int32_t m = ModApi_MenuCurrentEntry();
    s_soDesc[0] = 0;
    for (int32_t t = 0; t < 2; t++) {
        if (m >= so_base(t, 0) + 2 && m <= so_base(t, 0) + 17)
            snprintf(s_soDesc, sizeof s_soDesc, "SET CARDS");
        else if (m >= so_base(t, 1) + 2 && m <= so_base(t, 1) + 17)
            snprintf(s_soDesc, sizeof s_soDesc, "SET INJURY LEVELS");
        else
            continue;
        int32_t x, y, w, h;
        ModApi_MenuEntryRect(so_base(t, 2) + 17, &x, &y, &w, &h);
        ModApi_SetMenuEntryRect(SO_DESC, x, y + h + 5, -1, -1);
    }
    ModApi_MenuText(SO_DESC, s_soDesc);
}

static void so_col(void)
{
    const int32_t m = ModApi_MenuCurrentEntry();
    for (int32_t t = 0; t < 2; t++) {
        const int32_t js = m - (so_base(t, 0) + 2), ji = m - (so_base(t, 1) + 2);
        if (js >= 1 && js < 16 && !s_soInj[t][js])
            s_soSent[t][js] ^= 1;
        if (ji >= 1 && ji < 16 && !s_soSent[t][ji])
            s_soInj[t][ji] = (uint8_t)(s_soInj[t][ji] >= 7 ? 0 : s_soInj[t][ji] + 1);
    }
}

static void so_reset(void)
{
    memset(s_soSent, 0, sizeof s_soSent);
    memset(s_soInj, 0, sizeof s_soInj);
}

static void so_ok(void)
{
    for (int32_t t = 0; t < 2; t++) {
        int32_t sent = 0, inj = 0;
        for (int32_t j = 0; j < 16; j++) {
            sent += s_soSent[t][j];
            inj += s_soInj[t][j] != 0;
        }
        if (sent > 4) {
            tell("CUSTOM CHALLENGES", "SENT OFF PLAYERS SHOULD NOT EXCEED 4.");
            return;
        }
        if (inj > 8) {
            tell("CUSTOM CHALLENGES", "INJURED PLAYERS SHOULD NOT EXCEED 8.");
            return;
        }
    }
    for (int32_t t = 0; t < 2; t++) {
        int32_t k = 0;
        s_edit.sentOff[t] = 0;
        memset(s_edit.injured[t], 0, sizeof s_edit.injured[t]);
        for (int32_t j = 0; j < 16; j++) {
            if (s_soSent[t][j])
                s_edit.sentOff[t] |= 1u << (j + 1);
            if (s_soInj[t][j] && k < 8) {
                s_edit.injured[t][k].shirt = (uint8_t)(j + 1);
                s_edit.injured[t][k].level = s_soInj[t][j];
                k++;
            }
        }
    }
    s_edit.subsSet = 1;
    s_edit.subsMin = (uint8_t)(yours_short() ? 0 : 2);
    s_edit.subsMax = 5;
    SetExitMenuFlag();
}

static void ed_cards(void) { ModApi_ShowMenu("challenges/customChallengesSentOffInjuryNewMenu"); }

/* ---- SORT BY (customChallengesSortBy.mnu): the player's own and fetched challenges' order, in
 * their category's list -- DATE (newest first), TEAM, CREATOR / TEAM, CREATOR / LEVEL, LEVEL. Ours
 * carry no creator's name: CREATOR puts the player's own before those fetched for them. Kept in
 * SWOS.ini ([mod.challenges] sort). ---- */
enum { SORT_DATE, SORT_TEAM, SORT_CREATOR, SORT_CREATOR2, SORT_LEVEL };
static const char *const kSortKey[5] = { "date", "name", "creator", "creator2", "level" };
static int32_t s_sort = -1;

static int32_t sort_option(void)
{
    if (s_sort < 0) {
        const char *v = ModApi_Setting("challenges", "sort", "date");
        s_sort = SORT_DATE;
        for (int32_t k = 0; k < 5; k++)
            if (!strcmp(v, kSortKey[k]))
                s_sort = k;
    }
    return s_sort;
}

static int by_custom_sort(const void *a, const void *b)
{
    const Challenge *x = (const Challenge *)a, *y = (const Challenge *)b;
    int r = 0;
    const int32_t o = sort_option();
    if (o == SORT_CREATOR || o == SORT_CREATOR2)
        r = (int)x->custom - (int)y->custom;
    if (!r && (o == SORT_LEVEL || o == SORT_CREATOR2))
        r = (int)x->difficulty - (int)y->difficulty;
    if (!r && o != SORT_DATE) {
        r = strcmp(x->teamName[0], y->teamName[0]);
        if (!r)
            r = strcmp(x->teamName[1], y->teamName[1]);
    }
    if (!r)
        r = o == SORT_DATE ? (int)y->seq - (int)x->seq : (int)x->seq - (int)y->seq;
    return r;
}

/* the CUSTOM category's challenges (together: the list is sorted by category) in the order chosen */
static void sort_custom(void)
{
    int32_t first = -1, n = 0;
    for (int32_t i = 0; i < s_count; i++)
        if (!strcmp(s_ch[i].category, "CUSTOM")) {
            if (first < 0)
                first = i;
            n++;
        }
    if (n > 1)
        qsort(&s_ch[first], (size_t)n, sizeof s_ch[0], by_custom_sort);
}

enum { SB_DATE = 1, SB_TEAM, SB_CREATOR, SB_CREATOR2, SB_LEVEL, SB_EXIT };
static void sort_draw(void)
{
    static const int8_t kEntry[5] = { SB_DATE, SB_TEAM, SB_CREATOR, SB_CREATOR2, SB_LEVEL };
    for (int32_t k = 0; k < 5; k++)
        ModApi_MenuColour(kEntry[k], k == sort_option() ? kLightBlue : kGreen);
}
static void sort_set(int32_t o)
{
    s_sort = o;
    ModApi_SetSetting("challenges", "sort", kSortKey[o]);
    sort_custom();
    SetExitMenuFlag();
}
static void sort_date(void) { sort_set(SORT_DATE); }
static void sort_team(void) { sort_set(SORT_TEAM); }
static void sort_creator(void) { sort_set(SORT_CREATOR); }
static void sort_creator2(void) { sort_set(SORT_CREATOR2); }
static void sort_level(void) { sort_set(SORT_LEVEL); }

static void info_draw(void)
{
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
    const int32_t own = c->custom == 1;     /* (the player's own: EDIT, SWOS 2020's LOAD) */
    if (own)
        row(6, "", "EDIT", 0);
    {   /* online: what the service says of it ("BEATEN BY 12 PLAYERS"), when there is one */
        char args[256];
        online_args(c, args, sizeof args);
        const int32_t r = ModApi_CallService("online-records", "challenges.stats", args, s_onlineLine,
                                             sizeof s_onlineLine);
        if (r == 0 && s_onlineLine[0])
            row(6 + own, "ONLINE", s_onlineLine, 1);
        else if (r == 1)
            row(6 + own, "ONLINE", "...", 1);
        hide_from((r == 0 && s_onlineLine[0]) || r == 1 ? 7 + own : 6 + own);
    }
    ModApi_MenuText(M_note, c->completed ? "DONE ALREADY: PLAY IT AGAIN" : "");
}

static void info_init(void)
{
    const int32_t playRow = M_label0 + 2 * 5 + 1, editRow = playRow + 2;
    const int32_t own = s_cur >= 0 && s_cur < s_count && s_ch[s_cur].custom == 1;
    ModApi_MenuNav(playRow, -1, own ? editRow : M_exit);
    if (own)
        ModApi_MenuNav(editRow, playRow, M_exit);
    ModApi_MenuNav(M_exit, own ? editRow : playRow, -1);
    ModApi_MenuSelect(playRow);
    info_draw();
}

static void info_edit(void)             /* EDIT: one of the player's own, in the editor */
{
    if (s_cur < 0 || s_cur >= s_count || s_ch[s_cur].custom != 1)
        return;
    edit_existing(s_cur);
    SetExitMenuFlag();                      /* (back to the list: it may have changed or gone) */
}

static void info_play(void)
{
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
    row(2, "", "CHANGE TACTICS", 0);
    hide_from(3);
    ModApi_MenuText(M_note, "");
}

static void result_init(void)
{
    const int32_t retryRow = M_label0 + 2 * 1 + 1, tacticsRow = retryRow + 2;
    ModApi_MenuNav(retryRow, -1, tacticsRow);
    ModApi_MenuNav(tacticsRow, retryRow, M_exit);
    ModApi_MenuNav(M_exit, tacticsRow, -1);
    ModApi_MenuSelect(retryRow);
    result_draw();
}

static void result_retry(void)
{
    s_retry = 1;
    SetExitMenuFlag();
}

static void result_tactics(void)        /* CHANGE TACTICS: again, by the PLAY MATCH screen */
{
    s_retry = 2;
    SetExitMenuFlag();
}

static void none(void) {}
static void ch_exit(void) { SetExitMenuFlag(); }
static void ch_open(void)
{
    s_cat = -1;
    s_page = 0;
    /* a fork's own screens for them (SWOS United's, SWOS 2020's), when a mod offers them: they
     * read and play the challenges through the "challenges" service below */
    if (ModApi_CallService("challenges-screens", "open", "{}", NULL, 0) == 0)
        return;
    ModApi_ShowMenu("challenges/challengesMenu");
}

/* ---- the "challenges" service: the challenges for another mod's screens (docs/MODS.md §18) ----
 *   categories {}                 "<name>\t<done>\t<all>\t<closed 0/1>" a line, in the list's order
 *   list       {"category"}       "<id>\t<title or name>\t<difficulty>\t<0 open, 1 done, 2 locked>
 *                                 \t<0 a pack's, 1 the player's own, 2 fetched>"
 *   save       {"challenge":{..}} one of the player's own, saved (its id; a new one without) -> "<id>"
 *   try        {"challenge":{..}} played once, not saved (an editor's TEST)
 *   delete     {"id"}             one of the player's own, gone
 *   info       {"id"}             "<field>\t<value>" lines: the challenge's fields (below)
 *   new        {}                 this mod's NEW CHALLENGE editor (then plays it)
 *   reload     {}                 reads every challenge again (another mod wrote shared.json)
 *   play       {"id"}             plays it (this mod's result screen, QUICK RETRY), then
 *                                 "won\t<0/1>"; -1 when it is locked or unknown
 * A challenge's category is "" for none; the player's own are "CUSTOM". */
static int32_t find_id(const char *id)
{
    for (int32_t i = 0; i < s_count; i++)
        if (!strcmp(s_ch[i].id, id))
            return i;
    return -1;
}

static void arg_str(const char *args, const char *key, char *out, int32_t cap)
{
    out[0] = 0;
#ifdef SWOS_SDL_BUILD
    char path[32];
    snprintf(path, sizeof path, "$.%s", key);
    if (mjson_get_string(args, (int)strlen(args), path, out, (int)cap) < 0)
        out[0] = 0;
#else
    (void)args; (void)key; (void)cap;   /* (the Amiga: no JSON, and no fork's screens) */
#endif
}

typedef struct { char *p; uint32_t left; } Out;
static void put(Out *o, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void put(Out *o, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(o->p, o->left, fmt, ap);
    va_end(ap);
    if (n > 0 && (uint32_t)n < o->left) {
        o->p += n;
        o->left -= (uint32_t)n;
    } else if (o->left) {
        o->p += o->left - 1;
        o->left = 1;
    }
}

static int32_t count_bits(uint32_t v) { int32_t n = 0; for (; v; v &= v - 1) n++; return n; }

static int32_t service(const char *method, const char *args, char *out, uint32_t cap)
{
    Out o = { out, cap };
    char key[64];
    if (!out || !cap)
        return -1;
    out[0] = 0;
    load();
#ifdef SWOS_SDL_BUILD
    if (!strcmp(method, "save") || !strcmp(method, "try")) {
        /* {"challenge": {...}}: one of the pack format's challenges -- saved as one of the
         * player's own (replacing one of its id; a new id when it has none), or played once and
         * forgotten (TEST) */
        const char *obj;
        int objLen;
        if (mjson_find(args, (int)strlen(args), "$.challenge", &obj, &objLen) != MJSON_TOK_OBJECT)
            return -1;
        const uint32_t docCap = (uint32_t)objLen + 128;
        char *doc = (char *)malloc(docCap);
        ChallengeDef *def = (ChallengeDef *)calloc(1, sizeof *def);
        int32_t n = -1;
        if (doc && def) {
            snprintf(doc, docCap, "{\"format\":\"openswos-challenges\",\"version\":2,\"challenges\":[%.*s]}", objLen, obj);
            n = ChallengePack_FromJson(doc, (int)strlen(doc), def, 1);
            if (n == 0) {   /* (no id yet: the reader skips it) -- a new one */
                char with[64];
                snprintf(with, sizeof with, "custom-%u", (unsigned)ModApi_Ticks());
                snprintf(doc, docCap, "{\"format\":\"openswos-challenges\",\"version\":2,\"challenges\":[{\"id\":\"%s\",%.*s]}",
                         with, objLen - 1, obj + 1);
                n = ChallengePack_FromJson(doc, (int)strlen(doc), def, 1);
            }
        }
        free(doc);
        if (n == 1 && !def->id[0])              /* ("id": "": a new one too) */
            snprintf(def->id, sizeof def->id, "custom-%u", (unsigned)ModApi_Ticks());
        if (n != 1 || s_count >= kMax) {
            free(def);
            return -1;
        }
        Challenge *c = &s_ch[s_count];          /* (past the list: not one of it until saved) */
        memset(c, 0, sizeof *c);
        c->def = *def;
        free(def);
        c->custom = 1;
        c->mod = -1;
        snprintf(c->category, sizeof c->category, "CUSTOM");
        if (!strcmp(method, "try")) {
            const int32_t at = s_count++;       /* (in the list while it is played) */
            s_test = 0;
            play(at);
            s_count--;
            put(&o, "won\t%d\n", (int)s_won);
            return 0;
        }
        const Challenge copy = *c;
        if (!write_custom(&copy, copy.id))
            return -1;
        put(&o, "%s\n", copy.id);
        s_loaded = 0;
        s_count = 0;
        s_numKept = 0;
        load();
        return 0;
    }
    if (!strcmp(method, "delete")) {
        arg_str(args, "id", key, sizeof key);
        const int32_t i = find_id(key);
        if (i < 0 || s_ch[i].custom != 1)
            return -1;
        if (!write_custom(NULL, key))
            return -1;
        s_loaded = 0;
        s_count = 0;
        s_numKept = 0;
        load();
        return 0;
    }
#endif
    if (!strcmp(method, "new")) {             /* (the mod's own NEW CHALLENGE editor) */
        edit_new();
        return 0;
    }
    if (!strcmp(method, "reload")) {          /* (another mod wrote shared.json) */
        s_loaded = 0;
        s_count = 0;
        s_numKept = 0;
        load();
        put(&o, "%d\n", (int)s_count);
        return 0;
    }
    if (!strcmp(method, "categories")) {
        for (int32_t k = 0; k < s_numCats; k++) {
            int32_t done, all;
            category_counts(k, &done, &all);
            put(&o, "%s\t%d\t%d\t%d\n", s_cats[k], (int)done, (int)all, (int)category_unavailable(s_cats[k]));
        }
        return 0;
    }
    if (!strcmp(method, "list")) {
        arg_str(args, "category", key, sizeof key);
        for (int32_t i = 0; i < s_count; i++)
            if (!strcmp(s_ch[i].category, key))
                put(&o, "%s\t%s\t%d\t%d\t%d\n", s_ch[i].id, s_ch[i].title[0] ? s_ch[i].title : s_ch[i].name,
                    (int)s_ch[i].difficulty, s_ch[i].completed ? 1 : locked(i) ? 2 : 0, (int)s_ch[i].custom);
        return 0;
    }
    arg_str(args, "id", key, sizeof key);
    const int32_t i = find_id(key);
    if (i < 0)
        return -1;
    const Challenge *c = &s_ch[i];
    if (!strcmp(method, "info")) {
        int32_t injured[2] = { 0, 0 };
        for (int32_t t = 0; t < 2; t++)
            for (int32_t k = 0; k < 8; k++)
                injured[t] += c->injured[t][k].shirt != 0;
        put(&o, "id\t%s\nname\t%s\ntitle\t%s\ncategory\t%s\nabout\t%s\n", c->id,
            c->recordName[0] ? c->recordName : c->name, c->title,
            c->category, c->about);
        put(&o, "team0\t%s\nteam1\t%s\nyours\t%d\nscore0\t%d\nscore1\t%d\nminute\t%d\n", c->teamName[0],
            c->teamName[1], (int)c->yours, (int)c->score[0], (int)c->score[1], (int)c->minute);
        put(&o, "difficulty\t%d\nstate\t%d\nattempts\t%u\ncustom\t%d\n", (int)c->difficulty,
            c->completed ? 1 : locked(i) ? 2 : 0, (unsigned)c->attempts, (int)c->custom);
        put(&o, "extra_time\t%d\npenalties\t%d\nextra_time_rule\t%d\nshootout\t%d\n", (int)c->extraTime,
            (int)c->penalties, (int)c->extraTimeRule, (int)c->shootoutOnly);
        put(&o, "second_leg\t%d\nfirst_leg0\t%d\nfirst_leg1\t%d\n", (int)c->secondLeg, (int)c->firstLeg[0],
            (int)c->firstLeg[1]);
        put(&o, "subs_set\t%d\nsubs_min\t%d\nsubs_max\t%d\n", (int)c->subsSet, (int)c->subsMin, (int)c->subsMax);
        put(&o, "restart\t%d\nrestart_team\t%d\nkick_off\t%d\nplaying_up\t%d\n", (int)c->restartKind,
            (int)c->restartTeam, (int)c->kickOff, (int)c->playingUp);
        put(&o, "win\t%d\nwin_score0\t%d\nwin_score1\t%d\nwin_message\t%s\n", (int)c->win,
            (int)c->winScore[0], (int)c->winScore[1], c->winMessage);
        for (int32_t k = 0; k < 5; k++)
            put(&o, "description%d\t%s\n", (int)k + 1, c->lines[k]);
        put(&o, "uuid0\t%s\nuuid1\t%s\nnumber0\t%u\nnumber1\t%u\npitch_type\t%d\nrestart_wing\t%d\nrestart_spot\t%d\n"
                "restart_deep\t%d\n", c->teamUuid[0], c->teamUuid[1], (unsigned)c->teamNumber[0],
            (unsigned)c->teamNumber[1], (int)c->pitchType, (int)c->restartWing, (int)c->restartSpot, (int)c->restartDeep);
        put(&o, "sent_off0\t%d\nsent_off1\t%d\ninjured0\t%d\ninjured1\t%d\n", (int)count_bits(c->sentOff[0]),
            (int)count_bits(c->sentOff[1]), (int)injured[0], (int)injured[1]);
        return 0;
    }
    if (!strcmp(method, "play")) {
        if (locked(i))
            return -1;
        play(i);
        put(&o, "won\t%d\n", (int)s_won);
        return 0;
    }
    return -1;
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
/* SWOS_MOD_MATCH_TEST=challenge:editor -- the editor's rules (SWOS 2020's customChallengesEdit
 * handlers), its save, read back and delete, then the made challenge played CPU v CPU. Writes
 * the save folder's custom.json: run it with HOME pointing at a scratch folder. */
static int32_t s_edFails;
static void ed_check(int32_t ok, const char *what)
{
    fprintf(stdout, "CHALLENGE EDITOR %s: %s\n", ok ? "ok  " : "FAIL", what);
    s_edFails += !ok;
}

static void editor_selftest(void)
{
    load();
    memset(&s_edit, 0, sizeof s_edit);
    static const char *const kUuid[2] = { "fad1a2cf-b275-5972-a3fa-35bda74afa6f",      /* ENGLAND */
                                          "2bb28358-8981-5501-bee3-0916d4b736cc" };    /* GERMANY */
    for (int32_t t = 0; t < 2; t++) {
        snprintf(s_edit.teamUuid[t], sizeof s_edit.teamUuid[t], "%s", kUuid[t]);
        s_edit.teamNumber[t] = (uint16_t)(t ? 0x5011 : 0x500b);
        snprintf(s_edit.teamName[t], sizeof s_edit.teamName[t], "%s", t ? "GERMANY" : "ENGLAND");
    }
    for (int32_t k = 0; k < 8; k++)
        s_edit.scorers[k].team = -1;
    s_edit.pitchType = -1;
    s_edit.playingUp = -1;
    se_set(&s_edit, SE_FREE_KICK);
    wc_set(&s_edit, WC_ANY);
    s_edit.difficulty = 3;
    s_edit.subsSet = 1;
    s_edit.subsMin = 2;
    s_edit.subsMax = 5;
    s_edit.custom = 1;
    s_edit.mod = -1;
    snprintf(s_edit.title, sizeof s_edit.title, "SELF TEST");
    s_editExisting = 0;

    ed_team1Score();
    ed_check(s_edit.score[0] == 0, "at MATCH START the score stays 0-0");
    for (int32_t k = 0; k < 9; k++)
        ed_plus10();
    ed_check(s_edit.minute == 90 && s_edit.extraTime && se_of(&s_edit) == SE_KICK_OFF,
             "minute 90: extra time on, the start a kick-off");
    ed_team1Score();
    ed_team1Score();
    ed_check(s_edit.score[0] == 2 && s_edit.score[1] == 2, "at 90 the score is level (2-2)");
    ed_win();
    ed_win();
    ed_check(wc_of(&s_edit) == WC_GOLDEN && s_edit.extraTimeRule == CHALLENGE_ET_GOLDEN_GOAL, "GOLDEN GOAL");
    ed_win();
    ed_win();
    ed_check(wc_of(&s_edit) == WC_ANY, "back to WIN BY ANY SCORE");
    for (int32_t k = 0; k < 3; k++)
        ed_plus10();
    ed_check(s_edit.minute == 120 && s_edit.shootoutOnly && se_of(&s_edit) == SE_SHOOTOUT,
             "minute 120: a direct shoot-out");
    ed_minus1();
    ed_check(s_edit.minute == 0 && !s_edit.shootoutOnly && s_edit.subsMin == 2, "off again: MATCH START");
    for (int32_t k = 0; k < 7; k++)
        ed_plus10();
    ed_minus1();
    ed_check(s_edit.minute == 69, "minute 69");
    for (int32_t k = 0; k < 6; k++)
        ed_event();
    ed_check(se_of(&s_edit) == SE_RANDOM, "starting event RANDOM");
    ed_secondLeg();
    ed_legTeam2();
    ed_check(s_edit.secondLeg && s_edit.firstLeg[1] == 1, "a second leg, first leg 0-1");
    ed_attack();
    ed_startTeam();
    ed_difficulty();
    ed_pitchType();
    s_edit.sentOff[0] = 1u << 5;
    s_edit.injured[1][0].shirt = 7;
    s_edit.injured[1][0].level = 3;
    ed_check(edit_store(), "saved");
    const Challenge made = s_edit;          /* (its id now) */
    int32_t at = find_id(made.id);
    ed_check(at >= 0, "read back");
    if (at >= 0) {
        const Challenge *c = &s_ch[at];
        ed_check(c->minute == 69 && c->score[0] == made.score[0] && c->score[1] == made.score[1] &&
                 c->secondLeg && c->firstLeg[1] == 1 && c->restartKind == CHALLENGE_RESTART_RANDOM &&
                 c->playingUp == made.playingUp && c->kickOff == made.kickOff && c->difficulty == 4 &&
                 c->pitchType == 0 && c->sentOff[0] == (1u << 5) && c->injured[1][0].shirt == 7 &&
                 c->injured[1][0].level == 3 && c->subsSet && c->subsMin == 2 && c->subsMax == 5 &&
                 !strcmp(c->title, "SELF TEST") && c->custom == 1,
                 "every field as it was set");
    }
    s_sort = SORT_LEVEL;
    sort_custom();
    ed_check(edit_remove() && find_id(made.id) < 0, "deleted");
    fflush(stdout);
    if (s_edFails) {
        fprintf(stdout, "CHALLENGE EDITOR: %d FAILED\n", (int)s_edFails);
        exit(1);
    }
    s_ch[s_count] = made;                   /* (played as TEST plays it: past the list) */
    s_test = 1;
    s_trying = 1;
    play(s_count++);
    exit(3);                                /* (play() exits after the match) */
}

/* (src/mod_match.c ModMatch_SelfTest: SWOS_MOD_MATCH_TEST=challenge:<id>) */
void ModChallenges_Test(const char *id)
{
    if (!strcmp(id, "editor"))
        editor_selftest();
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
        { "info0", none }, { "info1", none }, { "info2", none }, { "info3", none },
        { "info4", none }, { "info5", info_play }, { "info6", info_edit }, { "info7", none },
        { "resultInit", result_init }, { "resultDraw", result_draw },
        { "result0", none }, { "result1", result_retry }, { "result2", result_tactics }, { "result3", none },
        { "result4", none }, { "result5", none }, { "result6", none }, { "result7", none },
        { "exit", ch_exit }, { "listExit", list_exit },
        /* the editor (customChallengesEdit.mnu), CARDS AND INJURIES, SORT BY */
        { "ed_init", ed_init }, { "ed_draw", ed_draw }, { "ed_background", ed_background },
        { "ed_team", ed_team }, { "ed_nickname", ed_nickname }, { "ed_team1Score", ed_team1Score },
        { "ed_team2Score", ed_team2Score }, { "ed_scoreReset", ed_scoreReset }, { "ed_none", ed_none },
        { "ed_minus10", ed_minus10 }, { "ed_minus1", ed_minus1 }, { "ed_reset0", ed_reset0 },
        { "ed_plus1", ed_plus1 }, { "ed_plus10", ed_plus10 }, { "ed_secondLeg", ed_secondLeg },
        { "ed_legTeam1", ed_legTeam1 }, { "ed_legTeam2", ed_legTeam2 }, { "ed_legReset", ed_legReset },
        { "ed_et", ed_et }, { "ed_pen", ed_pen }, { "ed_directPen", ed_directPen }, { "ed_subsMin", ed_subsMin },
        { "ed_subsMax", ed_subsMax }, { "ed_cards", ed_cards }, { "ed_attack", ed_attack },
        { "ed_pitchType", ed_pitchType }, { "ed_startTeam", ed_startTeam }, { "ed_event", ed_event },
        { "ed_win", ed_win }, { "ed_difficulty", ed_difficulty }, { "ed_test", ed_test }, { "ed_save", ed_save },
        { "ed_delete", ed_delete }, { "ed_exit", ed_exit },
        { "so_init", so_init }, { "so_draw", so_draw }, { "so_col", so_col }, { "so_reset", so_reset },
        { "so_ok", so_ok }, { "so_exit", ed_exit },
        { "sort_draw", sort_draw }, { "sort_date", sort_date }, { "sort_team", sort_team },
        { "sort_creator", sort_creator }, { "sort_creator2", sort_creator2 }, { "sort_level", sort_level },
        { "sort_exit", ed_exit },
    };
    ModApi_RegisterHandlers("challenges", kHandlers, (int32_t)(sizeof kHandlers / sizeof kHandlers[0]));
    ModApi_RegisterService("challenges", "challenges", service);
    ModApi_AddEntry(MODPOINT_MODES, "challenges", "challenges", ch_open);
}

const ModDef ModDef_challenges = {
    .slug = "challenges",
    .init = ch_init,
};
