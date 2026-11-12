/*
 * teamdb.c -- a team database mod: another season's teams in 96/97's game, as SWOS 2020's team
 * database DLC were (docs/MODS.md §10.4; include/teamdb.h). SDL builds.
 *
 * A teamdb mod ("slot": "teamdb", "changes": "data", "online": false) carries its teams as the
 * database's own JSON (data_json/teams/<COUNTRY>.json, read through the overlay as any mod's), its
 * European cups' and pool's binaries (data/<CUP>.TMD, data/POOLPLYR.DAT), its names
 * (data_json/translations/<lang>.json: strings and country pairs, merged over the game's), and
 * data_json/teamdb.json -- what SWOS 2020 did to its exe for each database:
 *
 *   { "format": "openswos-teamdb", "version": 1, "name": "...", "season_year": 1994,
 *     "fixes": "1111100", "points_for_win": 3, "nationality_codes": { "114": "LBY" },
 *     "leagues": { "51": { "divisions": [ { "promoted": 0, ... } ] } } }
 *
 * Applied once at start-up, after the competition graph is installed and before any career
 * copies a league from it (frontend.c), only while such a mod is on:
 *   - the leagues: SWOS 2020's fixupLeague (src/init.cpp), each country's league of the team
 *     files 0..79 -- division sizes from its teams' "league" byte (fix 1), a season grown a month
 *     (2) and played twice (3) when a division grew, the Greek, Israeli, Italian and Scottish
 *     rules (4-7) by the "fixes" switches (career.opt's first line); El Salvador's play-offs by
 *     the season year; "points_for_win"; then "leagues", explicit overrides by team-file number;
 *   - the European cups: their draw lists from the mod's TMDs (fixTMDData), no away goals from
 *     2022 (setSeasonYearVariables);
 *   - the nationality codes (_shortCountryNames), and the career's years (World_FirstYear);
 *   - real match days for odd divisions (FixtureRounds_Install, as a world's: the original's
 *     odd pair tables are no rounds, and a career refuses them).
 * The renames are the translations' (src/translations.c), not this file's.
 */
#include "teamdb.h"

#ifdef SWOS_SDL_BUILD

#include "game_types.h"
#include "mjson.h"
#include "mods.h"
#include "world.h"
#include "fixture_rounds.h"
#include "tweaks.h"         /* the league-rule tweaks: TeamDb_ApplyLeagueTweaks */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void *_competitionsTable[];
extern uint8_t _shortCountryNames[];
extern CompetitionTournament _euroCup;
extern CompetitionCup _cupWinnersCup, _uefaCup;
extern int DataJson_LoadTeamFileForCountry(int countryNum, unsigned char *teamFileBuf, int maxTeams);
extern int16_t LoadFileEx(uint8_t *name, uint8_t *dest, uint32_t *size);

enum { kNationalities = 214, kMaxTeams = 256 };

static struct {
    int32_t  on;
    uint16_t seasonYear;
    char     name[64];
    char     dir[1100];             /* the mod's folder */
} s_db;

int32_t  TeamDb_On(void) { return s_db.on; }
uint16_t TeamDb_SeasonYear(void) { return s_db.on ? s_db.seasonYear : 0; }

static char *slurp(const char *path, int *len)
{
    FILE *f = fopen(path, "rb");
    char *b = NULL;
    long n;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n > 0 && n < 4 * 1024 * 1024 && (b = (char *)malloc((size_t)n + 1)) != NULL) {
        if (fread(b, 1, (size_t)n, f) != (size_t)n) {
            free(b);
            b = NULL;
        } else {
            b[n] = '\0';
            *len = (int)n;
        }
    }
    fclose(f);
    return b;
}

/* ---- the leagues: SWOS 2020's fixupLeague ---- */

/* Each division's clubs in team file `file`, by their record's "league" byte (getNumLeagues). */
static int32_t division_counts(int32_t file, int32_t count[LEAGUE_MAX_DIVISIONS])
{
    static unsigned char buf[2 + kMaxTeams * sizeof(TeamFileEx)];
    memset(count, 0, LEAGUE_MAX_DIVISIONS * sizeof count[0]);
    const int n = DataJson_LoadTeamFileForCountry(file, buf, kMaxTeams);
    if (n <= 0)
        return 0;
    const TeamFileEx *t = (const TeamFileEx *)(buf + 2);
    for (int i = 0; i < n; i++)
        if (t[i].team.league < LEAGUE_MAX_DIVISIONS)
            count[t[i].team.league]++;
    return n;
}

static void fixup_league(CompetitionLeague *lg, int32_t file, const char *fixes, int32_t points)
{
    const int fixTeam = fixes[0] == '1', fixMonth = fixes[1] == '1', fixEach = fixes[2] == '1',
              fixGreek = fixes[3] == '1', fixIsrael = fixes[4] == '1', fixItaly = fixes[5] == '1',
              fixScot = fixes[6] == '1';
    const int32_t divisions = lg->header.divisionsCount;
    int32_t count[LEAGUE_MAX_DIVISIONS], grew = 0;

    if (points > 0)
        lg->header.pointsForWin = (int8_t)points;
    if (fixes[0] && division_counts(file, count)) {     /* #1 team_num_fix */
        for (int32_t i = 0; i < divisions && i < LEAGUE_MAX_DIVISIONS; i++) {
            if (count[i] == lg->divisions[i].teams)
                continue;
            if (count[i] > lg->divisions[i].teams)
                grew = 1;
            if (fixTeam) {
                fprintf(stderr, "[teamdb] file %d division %d: %d clubs (was %d)\n", (int)file, (int)i, (int)count[i],
                        (int)lg->divisions[i].teams);
                lg->divisions[i].teams = (int8_t)count[i];
            }
        }
    }
    if (grew && fixMonth) {                             /* #2 month_fix: the season a month longer */
        int32_t b = (uint8_t)lg->header.comp.beginMonth / 8, e = (uint8_t)lg->header.comp.endMonth / 8;
        if (b < e) {                                    /* (yearly: January at the earliest) */
            if (b - 1 >= 0) b--; else e++;
        } else {                                        /* (seasonal: July at the earliest) */
            if (b - 1 >= 6) b--; else e++;
        }
        lg->header.comp.beginMonth = (int8_t)(b * 8);
        lg->header.comp.endMonth = (int8_t)(e * 8);
    }
    if (grew && fixEach)                                /* #3 each_match_fix */
        for (int32_t i = 0; i < divisions && i < LEAGUE_MAX_DIVISIONS; i++)
            if (lg->divisions[i].teams > 12) {
                lg->header.rounds = 2;
                break;
            }
    if (file == 15 && fixGreek)                         /* #4 Greece: October */
        lg->header.comp.beginMonth = 7 * 8;
    if (file == 19)                                     /* #5 Israel */
        lg->header.rounds = fixIsrael ? 2 : 3;
    if (file == 20 && divisions >= 2) {                 /* #6 Italy */
        lg->divisions[0].relegated = fixItaly ? 3 : 4;
        lg->divisions[1].promoted = fixItaly ? 3 : 4;
    }
    if (file == 33)                                     /* #7 Scotland */
        lg->header.rounds = fixScot ? 3 : 4;
    if (file == 51 && s_db.seasonYear >= 2021)          /* El Salvador: no moves between its levels */
        for (int32_t j = 0; j < divisions && j < 4; j++) {
            lg->divisions[j].promoted = lg->divisions[j].promotedPo = 0;
            lg->divisions[j].relegated = lg->divisions[j].relegatedPo = 0;
        }
}

/* "leagues": { "<file>": { "divisions_count", "divisions": [ {"teams", "promoted", "promoted_po",
 * "relegated", "relegated_po"}, ... ], "rounds", "points_for_win", "begin_month", "end_month" } }
 * -- "divisions_count" 1..the record's own (All National Teams: El Salvador's one); each
 * field given replaces the league's, after the fixes (months 0-11, as SWOS 2020's /8). */
static void league_overrides(CompetitionLeague *lg, int32_t file, const char *json, int len)
{
    char key[32];
    const char *o;
    int ol;
    double v;
    snprintf(key, sizeof key, "$.leagues.%d", (int)file);
    if (mjson_find(json, len, key, &o, &ol) != MJSON_TOK_OBJECT)
        return;
    if (mjson_get_number(o, ol, "$.divisions_count", &v) && v >= 1 && v <= lg->header.divisionsCount &&
        (int8_t)v != lg->header.divisionsCount) {   /* (fewer only: the record has no more) */
        lg->header.divisionsCount = (int8_t)v;
        /* the new bottom division relegates no one, as every league's last does: a club sent down to
           a division that is no longer played is never placed (the season end's scratch check,
           panic 0x4444 -- All National Teams' El Salvador, four divisions made one) */
        lg->divisions[(int)v - 1].relegated = lg->divisions[(int)v - 1].relegatedPo = 0;
    }
    if (mjson_get_number(o, ol, "$.rounds", &v)) lg->header.rounds = (int8_t)v;
    if (mjson_get_number(o, ol, "$.points_for_win", &v)) lg->header.pointsForWin = (int8_t)v;
    if (mjson_get_number(o, ol, "$.begin_month", &v)) lg->header.comp.beginMonth = (int8_t)((int)v * 8);
    if (mjson_get_number(o, ol, "$.end_month", &v)) lg->header.comp.endMonth = (int8_t)((int)v * 8);
    for (int32_t i = 0; i < lg->header.divisionsCount && i < LEAGUE_MAX_DIVISIONS; i++) {
        char q[64];
        Division *d = &lg->divisions[i];
#define FIELD(name, f) \
        snprintf(q, sizeof q, "$.divisions[%d]." name, (int)i); \
        if (mjson_get_number(o, ol, q, &v)) d->f = (int8_t)v;
        FIELD("teams", teams)
        FIELD("promoted", promoted)
        FIELD("promoted_po", promotedPo)
        FIELD("relegated", relegated)
        FIELD("relegated_po", relegatedPo)
#undef FIELD
    }
    fprintf(stderr, "[teamdb] file %d: the league's overrides applied\n", (int)file);
}

/* A country's cups (its competitionsTable entry's [2] and [3]) no bigger than its clubs: the season
 * change fills a cup by drawing distinct clubs from its team file at random until every place is
 * taken (career_teams.c fillCupTeamSelectionFromFile), so a database with fewer clubs than a cup's
 * places never finishes it -- 1994-95 1.0's Israel, 14 clubs for its 16-team cup, hung the first
 * season change. The cup goes down to the largest power of two that fits (SWOS 2020 changed no
 * cup's size: its fixupLeagueCup only set the substitutes). */
static void fit_cups(void **entry, int32_t file)
{
    int32_t count[LEAGUE_MAX_DIVISIONS];
    const int32_t clubs = division_counts(file, count);
    if (clubs <= 0 || (intptr_t)entry[2] == -1 || !entry[2])
        return;
    for (int32_t k = 2; k <= 3; k++) {
        if (!entry[k] || (intptr_t)entry[k] == -1)
            break;
        CompCupHeader *cup = (CompCupHeader *)entry[k];
        if (cup->comp.type != 1 || (uint8_t)cup->comp.teamNumber != (uint8_t)file || cup->teams <= clubs)
            continue;
        int32_t fit = 2;
        while (fit * 2 <= clubs)
            fit *= 2;
        fprintf(stderr, "[teamdb] file %d: its cup of %d clubs made %d (the file has %d)\n", (int)file,
                (int)cup->teams, (int)fit, (int)clubs);
        cup->teams = (int8_t)fit;
    }
}

/* ---- the European cups: their draw lists from the TMDs (SWOS 2020's fixTMDData) ---- */

static void cup_from_tmd(Competition *cup, const char *name, int32_t teams)
{
    static uint8_t buf[32 * sizeof(TeamFileEx)];
    char path[1200];
    uint32_t size = 0;
    TeamId *list = comp_team_seed_list(cup);
    FILE *f;
    snprintf(path, sizeof path, "%s/data/%s", s_db.dir, name);
    if (!list || !(f = fopen(path, "rb")))
        return;                         /* (not the mod's: the game's own, which match) */
    fclose(f);
    snprintf(path, sizeof path, "data/%s", name);
    if (LoadFileEx((uint8_t *)path, buf, &size) != 0 || size < (uint32_t)teams * sizeof(TeamFileEx)) {
        fprintf(stderr, "[teamdb] data/%s: %u bytes, not %d teams -- the cup's own list kept\n", name,
                (unsigned)size, (int)teams);
        return;
    }
    for (int32_t j = 0; j < teams; j++) {
        list[j].countryNumber = buf[j * sizeof(TeamFileEx) + 0];
        list[j].teamOrdinal = buf[j * sizeof(TeamFileEx) + 1];
    }
    fprintf(stderr, "[teamdb] %s: the draw list from its %d teams\n", name, (int)teams);
}

/* ---- start-up ---- */

void TeamDb_Apply(void)
{
    char path[1200], fixes[8] = "";
    int len = 0, found = -1;
    char *json = NULL;
    double v;

    memset(&s_db, 0, sizeof s_db);
    if (World_Current() != WORLD_9697)
        return;                         /* (a world has its own teams and leagues) */
    for (int32_t m = 0; m < Mods_Count() && found < 0; m++) {
        snprintf(path, sizeof path, "%s/data_json/teamdb.json", Mods_Dir(m));
        if ((json = slurp(path, &len)) != NULL)
            found = m;
    }
    if (found < 0)
        return;
    {
        char format[32] = "";
        mjson_get_string(json, len, "$.format", format, (int)sizeof format);
        if (strcmp(format, "openswos-teamdb") != 0 || !mjson_get_number(json, len, "$.version", &v) || v != 1) {
            fprintf(stderr, "[teamdb] %s: not an openswos-teamdb version 1 file -- ignored\n", path);
            free(json);
            return;
        }
    }
    s_db.on = 1;
    snprintf(s_db.dir, sizeof s_db.dir, "%s", Mods_Dir(found));
    mjson_get_string(json, len, "$.name", s_db.name, (int)sizeof s_db.name);
    if (mjson_get_number(json, len, "$.season_year", &v) && v >= 1900 && v < 2200)
        s_db.seasonYear = (uint16_t)v;
    mjson_get_string(json, len, "$.fixes", fixes, (int)sizeof fixes);
    for (int32_t i = (int32_t)strlen(fixes); i < 7; i++)
        fixes[i] = '0';                 /* (a short string: the rest off) */
    fixes[7] = '\0';
    const int32_t points = mjson_get_number(json, len, "$.points_for_win", &v) ? (int32_t)v : 0;
    fprintf(stderr, "[teamdb] %s (%s): season %u, fixes %s%s\n", s_db.name[0] ? s_db.name : Mods_Id(found),
            Mods_Id(found), (unsigned)s_db.seasonYear, fixes, points ? ", points for a win from the file" : "");

    {   /* "nationality_codes": { "<nationality>": "ABC" } */
        const char *o;
        int ol, pos = 0, ko, kl, vo, vl, vt;
        if (mjson_find(json, len, "$.nationality_codes", &o, &ol) == MJSON_TOK_OBJECT)
            while ((pos = mjson_next(o, ol, pos, &ko, &kl, &vo, &vl, &vt)) != 0) {
                const int nat = atoi(o + ko + 1);
                if (vt != MJSON_TOK_STRING || vl != 5 || nat < 0 || nat >= kNationalities)
                    continue;
                memcpy(_shortCountryNames + nat * 3, o + vo + 1, 3);
            }
    }
    for (int32_t file = 0; file < CN_EUROPE; file++) {
        void **entry = (void **)_competitionsTable[file];
        if (file == CN_CUSTOM || !entry || (intptr_t)entry == -1 || !entry[0] || (intptr_t)entry[0] == -1)
            continue;
        CompetitionLeague *lg = (CompetitionLeague *)entry[0];
        if ((uint8_t)lg->header.comp.teamNumber != (uint8_t)file || lg->header.divisionsCount < 1 ||
            lg->header.divisionsCount > LEAGUE_MAX_DIVISIONS)
            continue;                   /* (not a country's league) */
        fit_cups(entry, file);
        const CompetitionLeague was = *lg;
        fixup_league(lg, file, fixes, points);
        league_overrides(lg, file, json, len);
        if (memcmp(&was, lg, sizeof was) != 0) {
            char divs[256] = "";
            int at = 0;
            for (int32_t i = 0; i < lg->header.divisionsCount && at < (int)sizeof divs - 40; i++)
                at += snprintf(divs + at, sizeof divs - (size_t)at, " [%d clubs, up %d+%d, down %d+%d]",
                               lg->divisions[i].teams, lg->divisions[i].promoted, lg->divisions[i].promotedPo,
                               lg->divisions[i].relegated, lg->divisions[i].relegatedPo);
            fprintf(stderr, "[teamdb] file %d: months %d-%d, played %d times, %d points a win;%s\n", (int)file,
                    (uint8_t)lg->header.comp.beginMonth / 8, (uint8_t)lg->header.comp.endMonth / 8,
                    lg->header.rounds, lg->header.pointsForWin, divs);
        }
    }
    /* A database's divisions can be odd (a size from its teams, fix 1); the original's odd pair
     * tables are no rounds and a career league refuses them (panic 0x1313), so real ones, as a
     * world has them (src/fixture_rounds.c: odd sizes 5..23; an even league is not touched). */
    FixtureRounds_Install();
    cup_from_tmd(&_euroCup.header.comp, "EUROCUP.TMD", 16);
    cup_from_tmd(&_cupWinnersCup.header.comp, "EUROCWC.TMD", 32);
    cup_from_tmd(&_uefaCup.header.comp, "UEFACUP.TMD", 32);
    if (s_db.seasonYear >= 2022) {      /* (no away goals from 2021-22: SWOS 2020's setSeasonYearVariables) */
        _euroCup.header.awayGoalsMode = 0;
        _cupWinnersCup.header.awayGoalsMode = 0;
        _uefaCup.header.awayGoalsMode = 0;
    }
    free(json);
}


/* ---- SWOS 2020's league rules as tweaks, in 96/97 (include/tweaks.h; on by default) ----
 *
 * The launcher always passed SWOS 2020 --career-crash-fix=1111111 --always-points-per-win-3=1
 * (core/src/game_launch.cpp), so its players had all of fixupLeague (src/init.cpp) in 96/97 too:
 *   league_crash_fixes   1-3: a division sized by its file's clubs, and if one grew a season a
 *                        month longer and fewer meetings -- nothing in 96/97's own leagues;
 *   league_rules_2020    4-7: Greece from October (its cup September-June), Israel twice,
 *                        Italy 3 down / 3 up, Scotland three times;
 *   three_points_for_a_win, five_substitutes (SWOS 2020's mSub 5*16+5, leagues and cups).
 * Applied to the competition graph at start-up and whenever RULES OPTIONS changes one, always
 * from the leagues as they were installed (a snapshot), so switching one off gives the original
 * back. A career copies its leagues when it starts and saves them: it keeps the rules it began
 * with. Not with a team database mod (it brings its own: TeamDb_Apply) nor in another world
 * (a world's leagues are its own); and as every tweak, not under the rigs (golden careers). */
enum { kRuleFiles = CN_EUROPE };
static CompetitionLeague s_ruleLeague[kRuleFiles];
static int8_t   s_ruleCupSubs[kRuleFiles][2];
static uint8_t  s_ruleSaved[kRuleFiles];
static uint8_t  s_ruleSnapped;

static CompetitionLeague *rule_league(int32_t file, void ***entryOut)
{
    void **entry = (void **)_competitionsTable[file];
    if (file == CN_CUSTOM || !entry || (intptr_t)entry == -1 || !entry[0] || (intptr_t)entry[0] == -1)
        return NULL;
    CompetitionLeague *lg = (CompetitionLeague *)entry[0];
    if ((uint8_t)lg->header.comp.teamNumber != (uint8_t)file || lg->header.divisionsCount < 1 ||
        lg->header.divisionsCount > LEAGUE_MAX_DIVISIONS)
        return NULL;
    *entryOut = entry;
    return lg;
}

static CompCupHeader *rule_cup(void **entry, int32_t k, int32_t file)
{
    if (!entry[k] || (intptr_t)entry[k] == -1)
        return NULL;
    CompCupHeader *cup = (CompCupHeader *)entry[k];
    return (cup->comp.type == 1 && (uint8_t)cup->comp.teamNumber == (uint8_t)file) ? cup : NULL;
}

void TeamDb_ApplyLeagueTweaks(void)
{
    const uint32_t mask = TWEAK_LEAGUE_CRASH_FIXES | TWEAK_LEAGUE_RULES_2020 | TWEAK_THREE_POINTS |
                          TWEAK_FIVE_SUBSTITUTES;
    int32_t file, changed = 0;
    void **entry;

    if (s_db.on || World_Current() != WORLD_9697)
        return;
    if (!s_ruleSnapped) {                               /* the leagues as installed, once */
        for (file = 0; file < kRuleFiles; file++) {
            CompetitionLeague *lg = rule_league(file, &entry);
            if (!lg)
                continue;
            s_ruleLeague[file] = *lg;
            for (int32_t k = 0; k < 2; k++) {
                CompCupHeader *cup = rule_cup(entry, k + 2, file);
                s_ruleCupSubs[file][k] = cup ? cup->subsAndPointsForWin : 0;
            }
            s_ruleSaved[file] = 1;
        }
        s_ruleSnapped = 1;
    }
    const int fixes = Tweaks_On(TWEAK_LEAGUE_CRASH_FIXES), rules = Tweaks_On(TWEAK_LEAGUE_RULES_2020),
              three = Tweaks_On(TWEAK_THREE_POINTS), five = Tweaks_On(TWEAK_FIVE_SUBSTITUTES);
    for (file = 0; file < kRuleFiles; file++) {
        CompetitionLeague *lg = rule_league(file, &entry);
        if (!lg || !s_ruleSaved[file])
            continue;
        *lg = s_ruleLeague[file];                       /* the original, then the tweaks on it */
        for (int32_t k = 0; k < 2; k++) {
            CompCupHeader *cup = rule_cup(entry, k + 2, file);
            if (cup)
                cup->subsAndPointsForWin = s_ruleCupSubs[file][k];
        }
        const int32_t divisions = lg->header.divisionsCount;
        if (fixes) {
            int32_t count[LEAGUE_MAX_DIVISIONS], grew = 0;
            if (division_counts(file, count))           /* #1 team_num_fix */
                for (int32_t i = 0; i < divisions && i < LEAGUE_MAX_DIVISIONS; i++)
                    if (count[i] > 0 && count[i] != lg->divisions[i].teams) {
                        if (count[i] > lg->divisions[i].teams)
                            grew = 1;
                        lg->divisions[i].teams = (int8_t)count[i];
                    }
            if (grew) {                                 /* #2 month_fix, #3 each_match_fix */
                int32_t b = (uint8_t)lg->header.comp.beginMonth / 8, e = (uint8_t)lg->header.comp.endMonth / 8;
                if (b < e) { if (b - 1 >= 0) b--; else e++; }
                else       { if (b - 1 >= 6) b--; else e++; }
                lg->header.comp.beginMonth = (int8_t)(b * 8);
                lg->header.comp.endMonth = (int8_t)(e * 8);
                for (int32_t i = 0; i < divisions && i < LEAGUE_MAX_DIVISIONS; i++)
                    if (lg->divisions[i].teams > 12) {
                        lg->header.rounds = 2;
                        break;
                    }
            }
        }
        if (rules) {
            if (file == 15) {                           /* #4 Greece: October; its cup September-June */
                lg->header.comp.beginMonth = 7 * 8;
                for (int32_t k = 2; k <= 3; k++) {
                    CompCupHeader *cup = rule_cup(entry, k, file);
                    if (cup) {
                        cup->comp.beginMonth = 8 * 8;
                        cup->comp.endMonth = 5 * 8;
                    }
                }
            }
            if (file == 19)                             /* #5 Israel: twice */
                lg->header.rounds = 2;
            if (file == 20 && divisions >= 2) {         /* #6 Italy: 3 down, 3 up */
                lg->divisions[0].relegated = 3;
                lg->divisions[1].promoted = 3;
            }
            if (file == 33)                             /* #7 Scotland: three times */
                lg->header.rounds = 3;
        }
        if (three)
            lg->header.pointsForWin = 3;
        if (five) {
            lg->header.subsAndMaxSubs = 5 * 16 + 5;
            for (int32_t k = 2; k <= 3; k++) {
                CompCupHeader *cup = rule_cup(entry, k, file);
                if (cup)
                    cup->subsAndPointsForWin = 5 * 16 + 5;
            }
        }
        if (memcmp(&s_ruleLeague[file], lg, sizeof *lg) != 0)
            changed++;
    }
    fprintf(stderr, "[tweaks] 96/97 leagues:%s%s%s%s%s (%d leagues changed; a new career takes them)\n",
            fixes ? " league_crash_fixes" : "", rules ? " league_rules_2020" : "",
            three ? " three_points_for_a_win" : "", five ? " five_substitutes" : "",
            (fixes || rules || three || five) ? "" : " the original's", (int)changed);
    (void)mask;
}

#endif /* SWOS_SDL_BUILD */
