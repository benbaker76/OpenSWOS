/*
 * world_packs.c -- another world's clubs in a friendly or a DIY competition (include/world_packs.h,
 * docs/WORLD_2026_27.md §6k). SDL builds with the worlds.
 */
#include "world_packs.h"

#if defined(SWOS_SDL_BUILD) && SWOS_WORLDS

#include "game_types.h"
#include "mjson.h"
#include "swos_io.h"
#include "team_db.h"
#include "team_packs.h"
#include "world.h"

#include <SDL.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void *_countriesTable[];
extern void *_competitionsTable[];
extern CompetitionLeague _customsLeague;
extern uint8_t _lastLoadedExtNumber;
extern int32_t World_Installed(int32_t w);

enum {
    kSlots = TEAMPACK_LAST - TEAMPACK_FIRST + 1,
    kMaxClubs = 256,            /* a team file's (World 2026-27's Italy) */
    kPerDivision = 48,          /* what a team pack's division lists (team_packs.c) */
    kMaxCountries = 128,
    kSecondFirst = 186, kSecondLast = 197,  /* a world's second files: never a pack's */
};

/* ---- the worlds and their countries ---- */

typedef struct {
    char    file[40];           /* "ITALY.json" */
    char    name[24];           /* "ITALY" */
} WpCountry;

static struct {
    int32_t   read;
    int32_t   count;
    WpCountry c[kMaxCountries];
} s_worlds[WORLD_COUNT];

static char *slurp(const char *rel, int *len)
{
    char found[1024];
    FILE *f = fopen(swos_data_path(rel, found, (uint32_t)sizeof found), "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = n > 0 ? (char *)malloc((size_t)n + 1) : NULL;
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(b);
        return NULL;
    }
    fclose(f);
    b[n] = '\0';
    *len = (int)n;
    return b;
}

/* The team file's path of a world's country file. */
static void file_path(int32_t world, const char *file, char *out, size_t size)
{
    if (world == WORLD_9697)
        snprintf(out, size, "data_json/teams/%s", file);
    else
        snprintf(out, size, "data_json/worlds/%s/teams/%s", World_TagOf((WorldId)world), file);
}

/* A club country's file: not the national teams' (80-85), the custom teams' (72), a second
 * file or a pool (186-197, no name of its own). */
static int32_t club_country(int32_t number)
{
    return number >= 0 && number < 256 && number != 72 && (number < 80 || number > 85) &&
           (number < kSecondFirst || number > kSecondLast);
}

static void add_country(int32_t world, const char *file, const char *name)
{
    if (s_worlds[world].count >= kMaxCountries)
        return;
    WpCountry *c = &s_worlds[world].c[s_worlds[world].count++];
    snprintf(c->file, sizeof c->file, "%s", file);
    snprintf(c->name, sizeof c->name, "%s", name);
    for (char *q = c->name; *q; q++)
        *q = *q == '_' ? ' ' : (*q >= 'a' && *q <= 'z') ? (char)(*q - 32) : *q;
}

static int by_name(const void *a, const void *b)
{
    return strcmp(((const WpCountry *)a)->name, ((const WpCountry *)b)->name);
}

/* A team file's header: its country number and name (before "teams"). */
static int32_t file_header(const char *rel, int32_t *number, char *name, int32_t name_size)
{
    char found[1024], head[2048];
    FILE *f = fopen(swos_data_path(rel, found, (uint32_t)sizeof found), "rb");
    if (!f)
        return 0;
    const int n = (int)fread(head, 1, sizeof head - 1, f);
    fclose(f);
    head[n > 0 ? n : 0] = '\0';
    const char *teams = strstr(head, "\"teams\"");
    char probe[2200];
    double num = -1;
    snprintf(probe, sizeof probe, "%.*s\"end\": 0}", teams ? (int)(teams - head) : 0, head);
    if (!mjson_get_number(probe, (int)strlen(probe), "$.country", &num) ||
        mjson_get_string(probe, (int)strlen(probe), "$.country_name", name, name_size) <= 0)
        return 0;
    *number = (int32_t)num;
    return 1;
}

static void read_world(int32_t world)
{
    if (s_worlds[world].read)
        return;
    s_worlds[world].read = 1;
    if (world == WORLD_9697) {
        /* 96/97's countries (its own list: the world being played has its files over these) */
        int len = 0;
        char *j = slurp("data_json/competitions/countries.json", &len);
        const char *arr;
        int alen;
        if (j && mjson_find(j, len, "$.countries", &arr, &alen) == MJSON_TOK_ARRAY) {
            int koff, klen, voff, vlen, vt, off = 0;
            while ((off = mjson_next(arr, alen, off, &koff, &klen, &voff, &vlen, &vt)) != 0) {
                double num = -1;
                char name[32] = "", tf[64] = "";
                if (vt != MJSON_TOK_OBJECT || !mjson_get_number(arr + voff, vlen, "$.number", &num) ||
                    !club_country((int32_t)num) ||
                    mjson_get_string(arr + voff, vlen, "$.name", name, (int)sizeof name) <= 0 ||
                    mjson_get_string(arr + voff, vlen, "$.team_file", tf, (int)sizeof tf) <= 0 ||
                    strncmp(tf, "teams/", 6) != 0)
                    continue;
                add_country(world, tf + 6, name);
            }
        }
        free(j);
    } else {
        char rel[128], found[1024];
        snprintf(rel, sizeof rel, "data_json/worlds/%s/teams", World_TagOf((WorldId)world));
        DIR *d = opendir(swos_data_path(rel, found, (uint32_t)sizeof found));
        struct dirent *e;
        while (d && (e = readdir(d)) != NULL) {
            const size_t l = strlen(e->d_name);
            char path[200], name[32];
            int32_t number = -1;
            if (l <= 5 || l >= sizeof s_worlds[0].c[0].file || strcmp(e->d_name + l - 5, ".json") != 0)
                continue;
            snprintf(path, sizeof path, "%s/%s", rel, e->d_name);
            if (file_header(path, &number, name, (int32_t)sizeof name) && club_country(number))
                add_country(world, e->d_name, name);
        }
        if (d)
            closedir(d);
    }
    qsort(s_worlds[world].c, (size_t)s_worlds[world].count, sizeof s_worlds[0].c[0], by_name);
}

int32_t WorldPacks_WorldCount(void)
{
    int32_t n = 0;
    while (WorldPacks_World(n, NULL, NULL) == 0)
        n++;
    return n;
}

int32_t WorldPacks_World(int32_t i, int32_t *world, const char **label)
{
    static const char *const kLabel[WORLD_COUNT] = { "WORLD 1996-97", "WORLD 2026-27", "EUROPE 1984-85" };
    for (int32_t w = 0; w < WORLD_COUNT; w++) {
        if (w == (int32_t)World_Current() || !World_Installed(w) || i--)
            continue;
        if (world) *world = w;
        if (label) *label = kLabel[w];
        return 0;
    }
    return -1;
}

int32_t WorldPacks_CountryCount(int32_t world)
{
    if (world < 0 || world >= WORLD_COUNT)
        return 0;
    read_world(world);
    return s_worlds[world].count;
}

int32_t WorldPacks_Country(int32_t world, int32_t i, const char **name, const char **file)
{
    if (i < 0 || i >= WorldPacks_CountryCount(world))
        return -1;
    if (name) *name = s_worlds[world].c[i].name;
    if (file) *file = s_worlds[world].c[i].file;
    return 0;
}

/* ---- the packs of this session ---- */

typedef struct {
    int32_t  world;             /* -1 unused */
    char     file[40];
    char     path[128];
    int32_t  clubs;
    uint8_t  division[kMaxClubs];
    uint8_t *name;              /* the country's, as _countriesTable has them (length first) */
    CompetitionLeague *league;
    char    *divNames[LEAGUE_MAX_DIVISIONS];
} WpPack;

static WpPack s_pack[kSlots];
static int32_t s_init;

static void init(void)
{
    if (s_init)
        return;
    s_init = 1;
    for (int32_t k = 0; k < kSlots; k++)
        s_pack[k].world = -1;
}

static WpPack *pack_of(int32_t number)
{
    init();
    if (number < TEAMPACK_FIRST || number > TEAMPACK_LAST || s_pack[number - TEAMPACK_FIRST].world < 0)
        return NULL;
    return &s_pack[number - TEAMPACK_FIRST];
}

/* A number free for a pack: none of the game's (the world being played has its own at
 * start-up), no mod's pack, not a world's second file. */
static int32_t number_free(int32_t number)
{
    return (number < kSecondFirst || number > kSecondLast) && !TeamPacks_File(number) &&
           !_countriesTable[number] && !_competitionsTable[number];
}

/* The names of a country's divisions: its league's in the world's leagues.json (else 96/97's,
 * which a world plays where it has no record of its own), by the file's country number. */
static int32_t division_names(int32_t world, int32_t country, char names[LEAGUE_MAX_DIVISIONS][32])
{
    const char *files[2] = { NULL, "data_json/competitions/leagues.json" };
    char wrel[96];
    int32_t found = 0;
    if (world != WORLD_9697) {
        snprintf(wrel, sizeof wrel, "data_json/worlds/%s/competitions/leagues.json", World_TagOf((WorldId)world));
        files[0] = wrel;
    }
    for (int32_t f = 0; f < 2 && !found; f++) {
        int len = 0;
        char *j = files[f] ? slurp(files[f], &len) : NULL;
        const char *arr;
        int alen;
        if (j && mjson_find(j, len, "$.leagues", &arr, &alen) == MJSON_TOK_ARRAY) {
            int koff, klen, voff, vlen, vt, off = 0;
            while (!found && (off = mjson_next(arr, alen, off, &koff, &klen, &voff, &vlen, &vt)) != 0) {
                double tn = -1, dc = 0;
                if (vt != MJSON_TOK_OBJECT || !mjson_get_number(arr + voff, vlen, "$.comp.team_number", &tn) ||
                    (int32_t)tn != country)
                    continue;
                mjson_get_number(arr + voff, vlen, "$.divisions_count", &dc);
                for (int32_t d = 0; d < (int32_t)dc && d < LEAGUE_MAX_DIVISIONS; d++) {
                    char path[32];
                    snprintf(path, sizeof path, "$.name[%d]", (int)(2 * d));
                    if (mjson_get_string(arr + voff, vlen, path, names[d], 32) > 0)
                        found = d + 1;
                }
            }
        }
        free(j);
    }
    return found;
}

static void drop(WpPack *p, int32_t number)
{
    if (_countriesTable[number] == p->name)
        _countriesTable[number] = NULL;
    if (p->league && _competitionsTable[number] &&
        ((QualifyingGroup *)_competitionsTable[number])->compCup[0] == (Competition *)p->league) {
        free(_competitionsTable[number]);
        _competitionsTable[number] = NULL;
    }
    free(p->name);
    free(p->league);
    for (int32_t d = 0; d < LEAGUE_MAX_DIVISIONS; d++)
        free(p->divNames[d]);
    memset(p, 0, sizeof *p);
    p->world = -1;
    TeamDb_ForgetCountry(number);
    if (_lastLoadedExtNumber == (uint8_t)number)
        _lastLoadedExtNumber = 0xFF;            /* (LoadTeamFile reads it again) */
}

/* Give `number` the world's file: read its clubs' divisions, install its name and league. */
static int32_t install(int32_t number, int32_t world, const char *file)
{
    WpPack *p = &s_pack[number - TEAMPACK_FIRST];
    char rel[128], cname[32] = "";
    int32_t country = -1;
    file_path(world, file, rel, sizeof rel);
    if (!file_header(rel, &country, cname, (int32_t)sizeof cname))
        return 0;
    int len = 0;
    char *j = slurp(rel, &len);
    const char *arr;
    int alen;
    if (!j || mjson_find(j, len, "$.teams", &arr, &alen) != MJSON_TOK_ARRAY) {
        free(j);
        return 0;
    }
    /* each club's real division, in file order */
    uint8_t real[kMaxClubs];
    int32_t n = 0;
    {
        int koff, klen, voff, vlen, vt, off = 0;
        while (n < kMaxClubs && (off = mjson_next(arr, alen, off, &koff, &klen, &voff, &vlen, &vt)) != 0) {
            double lg = 0;
            if (vt != MJSON_TOK_OBJECT)
                continue;
            mjson_get_number(arr + voff, vlen, "$.team.league", &lg);
            real[n++] = (uint8_t)lg;
        }
    }
    free(j);
    if (n == 0)
        return 0;

    /* the pack's divisions: the real ones in order, each in lists of 48 at most */
    char names[LEAGUE_MAX_DIVISIONS][32];
    memset(names, 0, sizeof names);
    const int32_t named = division_names(world, country, names);
    int32_t counts[LEAGUE_MAX_DIVISIONS] = { 0 }, divs = 0;
    char labels[LEAGUE_MAX_DIVISIONS][40];
    for (int32_t lg = 0; lg < 256 && divs < LEAGUE_MAX_DIVISIONS; lg++) {
        int32_t inLg = 0, part = 0;
        for (int32_t i = 0; i < n; i++)
            if (real[i] == lg)
                inLg++;
        if (!inLg)
            continue;
        const int32_t parts = (inLg + kPerDivision - 1) / kPerDivision;
        for (int32_t i = 0, done = 0; i < n && divs < LEAGUE_MAX_DIVISIONS; i++) {
            if (real[i] != lg)
                continue;
            if (done % kPerDivision == 0) {
                const char *base = lg < named && names[lg][0] ? names[lg] : "OTHER CLUBS";
                if (parts > 1)
                    snprintf(labels[divs], sizeof labels[0], "%.28s %d", base, (int)(++part));
                else
                    snprintf(labels[divs], sizeof labels[0], "%s", base);
                divs++;
            }
            p->division[i] = (uint8_t)(divs - 1);
            counts[divs - 1]++;
            done++;
        }
    }

    /* its name: the country's and the world's years ("ITALY 84-85") */
    const char *tag = World_TagOf((WorldId)world);
    char shown[40];
    for (char *q = cname; *q; q++)
        *q = *q == '_' ? ' ' : (*q >= 'a' && *q <= 'z') ? (char)(*q - 32) : *q;
    snprintf(shown, sizeof shown, "%.24s %c%c-%c%c", cname, tag[0], tag[1], tag[2], tag[3]);
    const size_t sl = strlen(shown);
    p->name = (uint8_t *)calloc(sl + 2, 1);
    p->league = (CompetitionLeague *)malloc(sizeof *p->league);
    QualifyingGroup *table = (QualifyingGroup *)calloc(1, sizeof *table);
    if (!p->name || !p->league || !table) {
        free(table);
        drop(p, number);
        return 0;
    }
    p->name[0] = (uint8_t)sl;
    memcpy(p->name + 1, shown, sl);
    memcpy(p->league, &_customsLeague, sizeof *p->league);
    p->league->header.comp.teamNumber = (int8_t)number;
    p->league->header.divisionsCount = (int8_t)divs;
    memset(p->league->divisions, 0, sizeof p->league->divisions);
    for (int32_t d = 0; d < divs; d++) {
        p->league->divisions[d].teams = (int8_t)counts[d];
        p->divNames[d] = strdup(labels[d]);
        p->league->name[2 * d] = p->league->name[2 * d + 1] = p->divNames[d];
    }
    table->compCup[0] = (Competition *)p->league;
    table->compCup[1] = (Competition *)(intptr_t)-2;
    table->compCup[2] = (Competition *)(intptr_t)-1;
    _countriesTable[number] = p->name;
    _competitionsTable[number] = table;
    TeamDb_ForgetCountry(number);               /* (an ask before -- the other game's team -- found none) */
    if (_lastLoadedExtNumber == (uint8_t)number)
        _lastLoadedExtNumber = 0xFF;
    p->world = world;
    p->clubs = n;
    snprintf(p->file, sizeof p->file, "%s", file);
    snprintf(p->path, sizeof p->path, "%s", rel);
    fprintf(stderr, "[world-packs] %d: %s (%s, %d clubs, %d divisions)\n", (int)number, shown, rel, (int)n,
            (int)divs);
    return number;
}

int32_t WorldPacks_Use(int32_t world, const char *file)
{
    init();
    if (world < 0 || world >= WORLD_COUNT || world == (int32_t)World_Current() || !file)
        return 0;
    for (int32_t k = 0; k < kSlots; k++)
        if (s_pack[k].world == world && strcmp(s_pack[k].file, file) == 0)
            return TEAMPACK_FIRST + k;
    for (int32_t k = 0; k < kSlots; k++)
        if (s_pack[k].world < 0 && number_free(TEAMPACK_FIRST + k))
            return install(TEAMPACK_FIRST + k, world, file);
    fprintf(stderr, "[world-packs] no pack number left for %s of world %s\n", file, World_TagOf((WorldId)world));
    return 0;
}

int32_t WorldPacks_UseAt(int32_t number, int32_t world, const char *file)
{
    init();
    if (number < TEAMPACK_FIRST || number > TEAMPACK_LAST || world < 0 || world >= WORLD_COUNT ||
        world == (int32_t)World_Current() || !file)
        return 0;
    WpPack *p = &s_pack[number - TEAMPACK_FIRST];
    if (p->world == world && strcmp(p->file, file) == 0)
        return number;
    if (p->world >= 0) {
        /* another country of this session had that number: it gets another the next time */
        drop(p, number);
    } else if (!number_free(number)) {
        fprintf(stderr, "[world-packs] %d is taken (a mod's pack or the game's): %s of world %s not given it\n",
                (int)number, file, World_TagOf((WorldId)world));
        return 0;
    }
    return install(number, world, file);
}

int32_t WorldPacks_Info(int32_t number, int32_t *world, const char **file)
{
    const WpPack *p = pack_of(number);
    if (!p)
        return 0;
    if (world) *world = p->world;
    if (file) *file = p->file;
    return 1;
}

const char *WorldPacks_Path(int32_t number)
{
    const WpPack *p = pack_of(number);
    return p ? p->path : NULL;
}

void WorldPacks_Fix(int32_t number, TeamFileEx *records, int32_t count)
{
    const WpPack *p = pack_of(number);
    if (!p)
        return;
    for (int32_t i = 0; i < count && i < kMaxClubs; i++) {
        records[i].team.countryNumber = (uint8_t)number;
        records[i].team.teamOrdinal = (uint8_t)i;
        records[i].team.globalTeamNumber = 0;
        records[i].team.league = p->division[i];
    }
}

/* ---- a club by its checksum ----
 * An index of every other world's clubs (checksum -> world, file, ordinal), made once on a thread of
 * its own from the start-up (WorldPacks_IndexStart): reading the worlds' team files (56 MB of JSON)
 * when a team arrives would hold the lobby's link up for seconds. */

typedef struct {
    char    sum[TEAM_CHECKSUM_TEXT];
    uint8_t world, country, ordinal;
} WpIndexEntry;

static WpIndexEntry *s_index;
static int32_t       s_indexCount;
static SDL_Thread   *s_indexThread;
static int32_t       s_indexDone;

static int index_build(void *unused)
{
    (void)unused;
    int32_t cap = 8192, n = 0;
    WpIndexEntry *ix = (WpIndexEntry *)malloc(sizeof *ix * (size_t)cap);
    for (int32_t w = 0; ix && w < WORLD_COUNT; w++) {
        if (!s_worlds[w].read)
            continue;
        for (int32_t c = 0; c < s_worlds[w].count && c < 256; c++) {
            char rel[128];
            int len = 0;
            file_path(w, s_worlds[w].c[c].file, rel, sizeof rel);
            char *j = slurp(rel, &len);
            const char *arr;
            int alen, i = 0;
            if (j && mjson_find(j, len, "$.teams", &arr, &alen) == MJSON_TOK_ARRAY) {
                int koff, klen, voff, vlen, vt, off = 0;
                while ((off = mjson_next(arr, alen, off, &koff, &klen, &voff, &vlen, &vt)) != 0) {
                    if (vt != MJSON_TOK_OBJECT)
                        continue;
                    if (n == cap) {
                        WpIndexEntry *g = (WpIndexEntry *)realloc(ix, sizeof *ix * (size_t)(cap *= 2));
                        if (!g)
                            break;
                        ix = g;
                    }
                    ix[n].sum[0] = '\0';
                    mjson_get_string(arr + voff, vlen, "$.checksum", ix[n].sum, (int)sizeof ix[n].sum);
                    ix[n].world = (uint8_t)w;
                    ix[n].country = (uint8_t)c;
                    ix[n].ordinal = (uint8_t)i++;
                    if (ix[n].sum[0])
                        n++;
                }
            }
            free(j);
        }
    }
    s_index = ix;
    s_indexCount = n;
    return 0;
}

void WorldPacks_IndexStart(void)
{
    if (s_indexThread || s_indexDone || WorldPacks_WorldCount() == 0)
        return;
    /* (the worlds' country lists read here, on the main thread: the index only reads them) */
    for (int32_t i = 0; ; i++) {
        int32_t w;
        if (WorldPacks_World(i, &w, NULL) != 0)
            break;
        read_world(w);
    }
    s_indexThread = SDL_CreateThread(index_build, "world-packs index", NULL);
    if (!s_indexThread) {           /* (no threads: made at the first ask) */
        index_build(NULL);
        s_indexDone = 1;
    }
}

static void index_ready(void)
{
    if (!s_indexDone && !s_indexThread)
        WorldPacks_IndexStart();
    if (s_indexThread) {
        SDL_WaitThread(s_indexThread, NULL);
        s_indexThread = NULL;
        s_indexDone = 1;
        fprintf(stderr, "[world-packs] the other worlds' clubs by checksum: %d\n", (int)s_indexCount);
    }
}

int32_t WorldPacks_FindTeam(const char *checksum, int32_t *number, int32_t *ordinal)
{
    if (!checksum || !checksum[0])
        return 0;
    index_ready();
    /* identical clubs (a file's SPARE ones; a club in two files, VADUZ in Switzerland's and
     * Liechtenstein's) are each other's: one in a pack given already first */
    for (int32_t pass = 0; pass < 2; pass++)
    for (int32_t i = 0; i < s_indexCount; i++) {
        const WpIndexEntry *e = &s_index[i];
        if (strcmp(e->sum, checksum) != 0)
            continue;
        const char *file = s_worlds[e->world].c[e->country].file;
        int32_t given = 0;
        for (int32_t k = 0; k < kSlots && !given; k++)
            if (s_pack[k].world == e->world && strcmp(s_pack[k].file, file) == 0)
                given = 1;
        if (pass == 0 && !given)
            continue;
        const int32_t num = WorldPacks_Use(e->world, file);
        if (!num)
            return 0;
        *number = num;
        *ordinal = e->ordinal;
        return 1;
    }
    return 0;
}

/* SWOS_WORLD_PACKS="<tag>:<file>,...": those packs given first, in order (150, 151 ...), for a test
 * that names their teams (SWOS_MOD_MATCH_TEAMS=9600,9701). */
void WorldPacks_FromEnv(void)
{
    const char *spec = getenv("SWOS_WORLD_PACKS");
    while (spec && *spec) {
        char tag[16] = "", file[40] = "";
        int used = 0;
        if (sscanf(spec, " %15[^:]:%39[^,]%n", tag, file, &used) != 2 || used == 0)
            break;
        const int32_t w = World_FromTag(tag);
        const int32_t n = w >= 0 ? WorldPacks_Use(w, file) : 0;
        fprintf(stderr, "[world-packs] SWOS_WORLD_PACKS %s:%s -> %d\n", tag, file, (int)n);
        spec += used;
        if (*spec == ',')
            spec++;
    }
}

/* ---- a .DIY round trip (SWOS_WORLD_PACKS_DIY_TEST=<a JSON .DIY>, with SWOS_WORLD_PACKS) ----
 * The .DIY loaded, its teams replaced by the packs' clubs (the first pack's, the second's, in
 * turn), saved; the packs' numbers then given to other countries (another session's), the save
 * loaded again: its "world_packs" must give the numbers back, its teams be the same clubs, and
 * the database find each one at its number. Run once the game's data is up (src/SWOS.c). */
int32_t WorldPacks_DiyTestArmed(void) { return getenv("SWOS_WORLD_PACKS_DIY_TEST") != NULL; }

void WorldPacks_DiyTestRun(void)
{
    extern int16_t SaveJson_LoadDIYData(const uint8_t *src, uint32_t src_size);
    extern int16_t SaveJson_WriteDIY(uint8_t *filename);
    extern int16_t SaveJson_LoadDIY(uint8_t *filename, const uint8_t *src_unused, uint32_t src_size_unused);
    extern int DataJson_LoadTeamFileForCountry(int countryNum, unsigned char *teamFileBuf, int maxTeams);
    extern TeamFileEx _selectedTeams[];
    extern uint16_t _numSelectedTeams, _gameType;
    static uint8_t buf[2 + kMaxClubs * sizeof(TeamFileEx)];
    static TeamFileEx before[64];
    static uint8_t name[] = "_WPTEST.DIY";
    int32_t fails = 0, packs[2] = { 0, 0 }, worlds[2];
    const char *files[2];
    char keep[2][40];

    for (int32_t k = 0, n = 0; k < kSlots && n < 2; k++)
        if (s_pack[k].world >= 0) {
            packs[n] = TEAMPACK_FIRST + k;
            worlds[n] = s_pack[k].world;
            snprintf(keep[n], sizeof keep[n], "%s", s_pack[k].file);
            files[n] = keep[n];
            n++;
        }
    const char *in = getenv("SWOS_WORLD_PACKS_DIY_TEST");
    FILE *f = fopen(in, "rb");
    long len = 0;
    uint8_t *data = NULL;
    if (f) {
        fseek(f, 0, SEEK_END);
        len = ftell(f);
        fseek(f, 0, SEEK_SET);
        data = (uint8_t *)malloc((size_t)len + 1);
        if (data && fread(data, 1, (size_t)len, f) == (size_t)len)
            data[len] = 0;
        else
            len = 0;
        fclose(f);
    }
    _gameType = 1;
    if (!packs[1] || len <= 0 || SaveJson_LoadDIYData(data, (uint32_t)len) != 0 || !_numSelectedTeams) {
        printf("[world-packs] DIY test: needs two SWOS_WORLD_PACKS and a JSON .DIY (%s)\nFAIL\n", in ? in : "-");
        exit(1);
    }
    free(data);
    const int32_t n = _numSelectedTeams < 64 ? _numSelectedTeams : 64;
    for (int32_t i = 0; i < n; i++) {
        const int32_t num = packs[i & 1];
        const int c = DataJson_LoadTeamFileForCountry(num, buf, kMaxClubs);
        const int32_t o = c > 0 ? (i / 2) % c : 0;
        if (c <= 0) {
            printf("[world-packs] DIY test: pack %d unreadable\nFAIL\n", (int)num);
            exit(1);
        }
        const uint8_t controls = _selectedTeams[i].team.teamControls;
        _selectedTeams[i] = ((const TeamFileEx *)(buf + 2))[o];
        _selectedTeams[i].team.teamControls = controls;
        before[i] = _selectedTeams[i];
    }
    if (SaveJson_WriteDIY(name) != 0) {
        printf("[world-packs] DIY test: not saved\nFAIL\n");
        exit(1);
    }
    /* another session: those numbers are other countries now, the teams gone */
    WorldPacks_UseAt(packs[0], worlds[1], "GERMANY.json");
    WorldPacks_UseAt(packs[1], worlds[0], "SPAIN.json");
    memset(_selectedTeams, 0, sizeof(TeamFileEx) * (size_t)n);
    printf("[world-packs] DIY test: %d teams saved (packs %d = %s %s, %d = %s %s); the numbers given to %s and %s\n",
           (int)n, (int)packs[0], World_TagOf((WorldId)worlds[0]), files[0], (int)packs[1],
           World_TagOf((WorldId)worlds[1]), files[1], (const char *)_countriesTable[packs[0]] + 1,
           (const char *)_countriesTable[packs[1]] + 1);
    if (SaveJson_LoadDIY(name, NULL, 0) != 0) {
        printf("[world-packs] DIY test: not loaded\nFAIL\n");
        exit(1);
    }
    for (int32_t k = 0; k < 2; k++) {
        int32_t w = -1;
        const char *file = NULL;
        const int32_t ok = WorldPacks_Info(packs[k], &w, &file) && w == worlds[k] && strcmp(file, files[k]) == 0;
        printf("  pack %d after the load: %s %s -- %s\n", (int)packs[k], w >= 0 ? World_TagOf((WorldId)w) : "-",
               file ? file : "-", ok ? "the save's" : "WRONG");
        fails += !ok;
    }
    for (int32_t i = 0; i < n; i++) {
        const TeamFileEx *t = &_selectedTeams[i];
        const TeamDbEntry *e = TeamDb_Team(t->team.countryNumber, t->team.teamOrdinal);
        const int32_t same = memcmp(t, &before[i], sizeof *t) == 0;
        const int32_t found = e && strncmp((const char *)e->team.team.teamName, (const char *)t->team.teamName, 16) == 0;
        if (i < 4 || !same || !found)
            printf("  team %2d %3d.%-3d %-16.16s %s, the database: %s\n", (int)i, t->team.countryNumber,
                   t->team.teamOrdinal, (const char *)t->team.teamName, same ? "as saved" : "CHANGED",
                   found ? "that club" : "NOT FOUND");
        fails += !same || !found;
    }
    printf("[world-packs] DIY round trip: %d teams, %d failed: %s\n", (int)n, (int)fails, fails ? "FAIL" : "PASS");
    fflush(stdout);
    exit(fails ? 1 : 0);
}

/* ---- the self-test ---- */

int32_t WorldPacks_SelfTest(void)
{
    int32_t fails = 0, packs = 0, clubs = 0;
    static uint8_t buf[2 + kMaxClubs * sizeof(TeamFileEx)];
    extern int DataJson_LoadTeamFileForCountry(int countryNum, unsigned char *teamFileBuf, int maxTeams);

    printf("[world-packs] world played: %s; other worlds: %d\n", World_Tag(), (int)WorldPacks_WorldCount());
    for (int32_t wi = 0; ; wi++) {
        int32_t world;
        const char *label;
        if (WorldPacks_World(wi, &world, &label) != 0)
            break;
        printf("[world-packs] %s: %d countries\n", label, (int)WorldPacks_CountryCount(world));
        for (int32_t c = 0; c < WorldPacks_CountryCount(world); c++) {
            const char *name, *file;
            WorldPacks_Country(world, c, &name, &file);
            const int32_t num = WorldPacks_Use(world, file);
            if (!num) {
                printf("  %-20s no number (all %d taken)\n", name, (int)kSlots);
                continue;
            }
            packs++;
            const int n = DataJson_LoadTeamFileForCountry(num, buf, kMaxClubs);
            const TeamFileEx *t = (const TeamFileEx *)(buf + 2);
            int32_t bad = n <= 0;
            for (int i = 0; i < n; i++)
                if (t[i].team.countryNumber != num || t[i].team.teamOrdinal != i)
                    bad = 1;
            /* the first and the last club: the database has them at the pack's number, and finds
             * them by their checksum */
            for (int k = 0; !bad && k < 2; k++) {
                const int i = k ? n - 1 : 0;
                const TeamDbEntry *e = TeamDb_Team(num, i);
                int32_t fn = 0, fo = -1;
                const TeamDbEntry *g = 0;
                if (!e || strncmp((const char *)e->team.team.teamName, (const char *)t[i].team.teamName, 16) != 0 ||
                    !WorldPacks_FindTeam(e->ids.checksum, &fn, &fo) || !(g = TeamDb_Team(fn, fo)) ||
                    strcmp(g->ids.checksum, e->ids.checksum) != 0) {
                    /* (identical clubs -- SPARE ones, a club in two files -- are each other's) */
                    printf("  %-20s club %d (%.16s): not found again (%d/%d)\n", name, i,
                           (const char *)t[i].team.teamName, (int)fn, (int)fo);
                    bad = 1;
                }
            }
            /* the other game's numbers are its session's: its first club sent under another number
             * is still that club to the online rule (src/custom_team.c) */
            if (!bad && c == 0) {
                extern int32_t CustomTeam_Allowed(const TeamFileEx *team);
                TeamFileEx theirs = t[0];
                theirs.team.countryNumber = (uint8_t)(num == TEAMPACK_LAST ? num - 1 : num + 1);
                const int32_t ok = CustomTeam_Allowed(&theirs);
                printf("  %-20s club 0 (%.16s) sent as %d.0 by the other game: %s\n", name,
                       (const char *)t[0].team.teamName, (int)theirs.team.countryNumber, ok ? "accepted" : "REFUSED");
                bad = !ok;
            }
            if (bad)
                fails++;
            clubs += n > 0 ? n : 0;
            if (c < 3 || bad)
                printf("  %-20s -> %d %-22.22s %3d clubs, %d divisions, first %.16s%s\n", name, (int)num,
                       (const char *)_countriesTable[num] + 1, n,
                       (int)((const CompetitionLeague *)((QualifyingGroup *)_competitionsTable[num])->compCup[0])
                           ->header.divisionsCount,
                       n > 0 ? (const char *)t[0].team.teamName : "-", bad ? "  FAIL" : "");
            drop(&s_pack[num - TEAMPACK_FIRST], num);    /* (more countries than numbers: one at a time) */
        }
    }
    printf("[world-packs] %d packs, %d clubs, %d failed: %s\n", (int)packs, (int)clubs, (int)fails,
           fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}

#endif
