/* classic_comps.c -- CLASSIC COMPETITIONS (SDL builds): famous tournaments, played
 * like a preset competition.
 *
 * Each one is a .DIY from the PC game or SWOS 2020 (its DLC cups: EURO 1964, the
 * World Cups), kept in the assets folder by group:
 *
 *   assets/classic/uefa/<name>.DIY       assets/classic/worldcup/<name>.DIY
 *
 * They are not in this repository, like the rest of the original game's data. A
 * file is listed by the name inside it, and loaded by diy_import_dos.c with the
 * player's own options and USER tactics kept: the competition is STARTED, not
 * resumed. Every team then starts as COMPUTER, whatever the file says (SWOS 2020's
 * cups set them all to COACH), and the TEAMS screen lets the player cycle each one
 * through COMPUTER / PLAYER-COACH / COACH, as the friendly team list does.
 *
 * The three screens are JSON only (bin/data_json/menus/openswos/classic*.json,
 * docs/MENUS.md section 10). They open from CLASSICS, the row above WORLD CUP in
 * PRESET COMPETITION's world list (teams.c SelectTeamsFinalMenu); PRESET COMPETITION
 * has asked about the competition under way and seeded the RNG by then. Once one is
 * loaded the chooser closes as if backed out of, and PresetCompetitionMenu starts it
 * (ClassicComps_TakeReady, then StartContest, as after LOAD OLD COMPETITION).
 */

#ifdef SWOS_SDL_BUILD

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "classic_comps.h"
#include "diy_import_dos.h"
#include "game_types.h"
#include "menu_load_json.h"
#include "menu_symbols.h"
#include "menu_system.h"
#include "play_match_menu.h"    /* TEAM_COMPUTER .. TEAM_COACH */
#include "swos_io.h"

extern TeamFileEx _selectedTeams[100];
extern uint16_t   _numSelectedTeams;
extern uint16_t   _gameType;
extern uint8_t    _competitionFilename[14];
extern CompetitionSlotBuffer _primaryCompetitionBuf;
extern void SetExitMenuFlag(void);
extern void SaveOptions(void);
extern void RestoreOptions(void);
extern void InitCareerVariables(uint16_t gameTypeParam);
extern void ShowErrorMenu(uint8_t *text);
extern void CallDrawMenuItem(MenuEntry *entry);

/* The menus' entries, in the order their JSON lists them. */
enum { ClassicComps_header, ClassicComps_uefa, ClassicComps_worldCup, ClassicComps_exit };
enum { ClassicList_header, ClassicList_first, kListRows = 32,     /* 2 columns of 16 */
       ClassicList_exit = ClassicList_first + kListRows };
enum { ClassicTeams_header, ClassicTeams_hint, ClassicTeams_first, kTeamSlots = 64,
       ClassicTeams_play = ClassicTeams_first + kTeamSlots, ClassicTeams_exit,
       kTeamColumns = 4, kTeamRows = kTeamSlots / kTeamColumns };

/* The JSON lists the team buttons a column at a time; the teams fill them a row at a
 * time, four across, so a competition takes only the rows it needs. */
static int32_t team_of_entry(uint16_t ordinal)
{
    int32_t slot = (int32_t)ordinal - ClassicTeams_first;
    return slot % kTeamRows * kTeamColumns + slot / kTeamRows;
}

enum {
    kComputerColour    = 10,    /* the friendly team list's: red */
    kPlayerCoachColour = 11,    /* purple */
    kCoachColour       = 13,    /* blue */
    kGameTypeDiy       = 1,
};

static const struct { const char *dir; const char *title; } k_groups[] = {
    { "uefa",     "UEFA" },
    { "worldcup", "WORLD CUP" },
};
enum { kGroups = sizeof k_groups / sizeof k_groups[0] };

static struct {
    int32_t group;                  /* the group picked, -1 = EXIT */
    int32_t ready;                  /* one is loaded, for PresetCompetitionMenu to start */
    int32_t pick;                   /* the competition picked, -1 = EXIT */
    int32_t play;                   /* PLAY pressed on the teams screen */
    int32_t count;
    char    file[kListRows][64];    /* under assets/classic/<group>/ */
    char    name[kListRows][40];    /* the competition's own name */
    char    title[40];
    char    teams[kTeamSlots][18];
} s;

/* ---- the list: every DOS .DIY in the group's folder, by its name ---- */

static int32_t has_diy_extension(const char *f)
{
    size_t n = strlen(f);
    return n > 4 && strcasecmp(f + n - 4, ".diy") == 0;
}

/* The first four-digit number in a name: "1966 FIFA WORLD CUP", "UEFA EURO 1964". */
static int32_t name_year(const char *name)
{
    for (; *name; name++)
        if (name[0] >= '0' && name[0] <= '9' && name[1] >= '0' && name[1] <= '9' &&
            name[2] >= '0' && name[2] <= '9' && name[3] >= '0' && name[3] <= '9')
            return atoi(name);
    return 0;
}

/* By year, then by name: a group lists in date order. */
static int compare_names(const void *a, const void *b)
{
    const char *x = s.name[*(const int32_t *)a], *y = s.name[*(const int32_t *)b];
    int32_t dy = name_year(x) - name_year(y);
    return dy ? dy : strcmp(x, y);
}

static void scan_group(int32_t group)
{
    char dir[1280], rel[160];
    char file[kListRows][64], name[kListRows][40];
    int32_t order[kListRows], i;
    struct dirent *d;
    DIR *dp;

    s.count = 0;
    snprintf(dir, sizeof dir, "%s%sclassic/%s", swos_assets_dir(),
             swos_assets_dir()[0] ? "/" : "", k_groups[group].dir);
    dp = opendir(dir);
    if (!dp) {
        fprintf(stderr, "[classic] no folder %s\n", dir);
        return;
    }
    while ((d = readdir(dp)) != NULL) {
        uint8_t *data = NULL;
        uint32_t size = 0;

        if (!has_diy_extension(d->d_name) || strlen(d->d_name) >= sizeof s.file[0])
            continue;
        if (s.count == kListRows) {
            fprintf(stderr, "[classic] %s: more than %d competitions, %s not listed\n",
                    dir, (int)kListRows, d->d_name);
            continue;
        }
        snprintf(rel, sizeof rel, "%s/%s", dir, d->d_name);
        if (swos_io_load_data_alloc(rel, &data, &size) != 0)
            continue;
        if (DiyImportDos_IsDosFile(data, size)) {
            snprintf(s.file[s.count], sizeof s.file[0], "%s", d->d_name);
            snprintf(s.name[s.count], sizeof s.name[0], "%.35s", (const char *)data + 4);
            s.count++;
        } else {
            fprintf(stderr, "[classic] %s is not a PC/SWOS 2020 .DIY: not listed\n", rel);
        }
        swos_io_free(data);
    }
    closedir(dp);

    for (i = 0; i < s.count; i++)
        order[i] = i;
    qsort(order, (size_t)s.count, sizeof order[0], compare_names);
    memcpy(file, s.file, sizeof file);
    memcpy(name, s.name, sizeof name);
    for (i = 0; i < s.count; i++) {
        memcpy(s.file[i], file[order[i]], sizeof s.file[0]);
        memcpy(s.name[i], name[order[i]], sizeof s.name[0]);
    }
}

/* ---- the menus ---- */

static void classicCompsMenu_OnInit(void) {}
static void classicCompsMenu_OnDraw(void) {}
static void classicCompsUefa(void)     { s.group = 0; SetExitMenuFlag(); }
static void classicCompsWorldCup(void) { s.group = 1; SetExitMenuFlag(); }

static void classicListMenu_OnInit(void) {}

static void classicListMenu_OnDraw(void)
{
    static char none[] = "NO COMPETITIONS IN THIS FOLDER";
    int32_t i;

    CalcMenuEntryAddress(ClassicList_header)->fg = (uintptr_t)s.title;
    for (i = 0; i < kListRows; i++) {
        MenuEntry *e = CalcMenuEntryAddress((uint16_t)(ClassicList_first + i));
        e->fg = (uintptr_t)(i < s.count ? s.name[i] : none);
        e->isInvisible = !(i < s.count || (i == 0 && s.count == 0));
        e->isDisabled = s.count == 0;
    }
}

static void classicListPick(MenuEntry *entry)
{
    int32_t i = (int32_t)entry->ordinal - ClassicList_first;
    if (i >= 0 && i < s.count) {
        s.pick = i;
        SetExitMenuFlag();
    }
}

static void classicTeamsMenu_OnInit(void) {}

static void classicTeamsMenu_OnDraw(void)
{
    int32_t i;

    CalcMenuEntryAddress(ClassicTeams_header)->fg =
        (uintptr_t)_primaryCompetitionBuf.tourn.header.competitionName;
    for (i = 0; i < kTeamSlots; i++) {
        MenuEntry *e = CalcMenuEntryAddress((uint16_t)(ClassicTeams_first + i));
        int32_t team = team_of_entry(e->ordinal);
        uint8_t controls = team < _numSelectedTeams ? _selectedTeams[team].team.teamControls : 0;

        e->isInvisible = team >= _numSelectedTeams;
        e->fg = (uintptr_t)s.teams[team];
        e->bgColor = controls == TEAM_PLAYER_COACH ? kPlayerCoachColour
                   : controls == TEAM_COACH        ? kCoachColour : kComputerColour;
    }
}

static void classicTeamsToggle(MenuEntry *entry)
{
    int32_t i;
    uint8_t *controls;

    if (entry->ordinal < ClassicTeams_first || entry->ordinal >= ClassicTeams_play)
        return;
    i = team_of_entry(entry->ordinal);
    if (i >= _numSelectedTeams)
        return;
    controls = &_selectedTeams[i].team.teamControls;
    *controls = *controls == TEAM_COMPUTER     ? TEAM_PLAYER_COACH
              : *controls == TEAM_PLAYER_COACH ? TEAM_COACH : TEAM_COMPUTER;
    classicTeamsMenu_OnDraw();
    CallDrawMenuItem(entry);
}

static void classicTeamsPlay(void) { s.play = 1; SetExitMenuFlag(); }

#define HANDLER(fn) { #fn, (void *)fn, SYM_FUNC }
static const MenuSymbol k_handlers[] = {
    HANDLER(classicCompsMenu_OnInit),
    HANDLER(classicCompsMenu_OnDraw),
    HANDLER(classicCompsUefa),
    HANDLER(classicCompsWorldCup),
    HANDLER(classicListMenu_OnInit),
    HANDLER(classicListMenu_OnDraw),
    HANDLER(classicListPick),
    HANDLER(classicTeamsMenu_OnInit),
    HANDLER(classicTeamsMenu_OnDraw),
    HANDLER(classicTeamsToggle),
    HANDLER(classicTeamsPlay),
};
#undef HANDLER

static uint8_t *load_menu(const char *name)
{
    static int32_t registered;

    if (!registered) {
        registered = 1;
        MenuJson_RegisterSymbols(k_handlers, (int)(sizeof k_handlers / sizeof k_handlers[0]));
    }
    return MenuJson_Load(name);
}

/* ---- loading one ---- */

/* 1 = loaded, every team COMPUTER; 0 = it would not load (the error was shown). */
static int32_t load_competition(int32_t group, int32_t pick)
{
    char rel[1280];
    uint8_t *data = NULL;
    uint32_t size = 0;
    int16_t r = 1;

    snprintf(rel, sizeof rel, "%s%sclassic/%s/%s", swos_assets_dir(),
             swos_assets_dir()[0] ? "/" : "", k_groups[group].dir, s.file[pick]);
    SaveOptions();
    _gameType = kGameTypeDiy;
    if (swos_io_load_data_alloc(rel, &data, &size) == 0) {
        r = DiyImportDos_LoadData(rel, data, size, 1);
        swos_io_free(data);
    }
    if (r != 0) {
        ShowErrorMenu((uint8_t *)"THIS COMPETITION COULD NOT BE LOADED");
        RestoreOptions();
        InitCareerVariables(0);
        return 0;
    }
    for (int32_t i = 0; i < _numSelectedTeams; i++) {
        _selectedTeams[i].team.teamControls = TEAM_COMPUTER;
        snprintf(s.teams[i], sizeof s.teams[i], "%.17s",
                 (const char *)_selectedTeams[i].team.teamName);
    }
    return 1;
}

int32_t ClassicComps_Choose(void)
{
    uint8_t *groups = load_menu("openswos/classicCompsMenu");
    uint8_t *list = load_menu("openswos/classicListMenu");
    uint8_t *teams = load_menu("openswos/classicTeamsMenu");

    s.ready = 0;
    if (!groups || !list || !teams)
        return 0;
    for (;;) {
        s.group = -1;
        ShowMenu(groups);
        if (s.group < 0)
            return 0;
        snprintf(s.title, sizeof s.title, "%s", k_groups[s.group].title);
        scan_group(s.group);
        for (;;) {
            s.pick = -1;
            ShowMenu(list);
            if (s.pick < 0)
                break;
            if (!load_competition(s.group, s.pick))
                continue;
            s.play = 0;
            ShowMenu(teams);
            if (s.play) {
                /* SAVE offers the file's own name, as after LOAD OLD COMPETITION. */
                snprintf((char *)_competitionFilename, sizeof _competitionFilename, "%.*s",
                         (int)(strlen(s.file[s.pick]) - 4), s.file[s.pick]);
                s.ready = 1;
                return 1;
            }
            InitCareerVariables(0);
            RestoreOptions();
        }
    }
}

int32_t ClassicComps_TakeReady(void)
{
    int32_t ready = s.ready;
    s.ready = 0;
    return ready;
}

#endif /* SWOS_SDL_BUILD */
