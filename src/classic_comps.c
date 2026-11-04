/* classic_comps.c -- HISTORIC TOURNAMENTS: famous tournaments, played like a preset competition.
 *
 * Each one is a .DIY kept by group in a classic/ folder:
 *
 *   SDL:    <assets>/classic/{uefa,worldcup,club}/
 *           the PC game's or SWOS 2020's own files (its DLC cups), read by
 *           diy_import_dos.c, or this game's JSON .DIY
 *   Amiga:  classic/{uefa,worldcup,club}/ in the game's drawer
 *           this game's JSON .DIY only, converted on a PC (--convert-diy): the
 *           Amiga cannot read the DOS dump (docs/CLASSIC_COMPETITIONS.md section 6)
 *
 * They are not in this repository, like the rest of the original game's data. A file
 * is listed by the name inside it, by year, and loaded with the player's own options
 * and USER tactics kept: the competition is STARTED, not resumed. Every team then
 * starts as COMPUTER, whatever the file says (SWOS 2020's cups set them all to COACH),
 * and the teams screen lets the player cycle each one through COMPUTER / PLAYER-COACH /
 * COACH, as the friendly team list does.
 *
 * The screens are classic packed menus (menu_data.c _classicCompsMenu -- NATIONAL or
 * CLUB --, _classicNationalMenu -- UEFA or WORLD CUP --, _classicListMenu,
 * _classicTeamsMenu; JSON in bin/data_json/menus/ on SDL). CLUB has one group, so it
 * goes straight to its list. They open
 * from HISTORIC TOURNAMENTS, the row above WORLD CUP in PRESET COMPETITION's world list (teams.c
 * SelectTeamsFinalMenu); PRESET COMPETITION has asked about the competition under way
 * and seeded the RNG by then. Once one is loaded the chooser closes as if backed out
 * of, and PresetCompetitionMenu starts it (ClassicComps_TakeReady, then StartContest,
 * as after LOAD OLD COMPETITION).
 */

#include <string.h>

#include "classic_comps.h"
#include "game_types.h"
#include "menu_system.h"
#include "mjson.h"
#include "play_match_menu.h"    /* TEAM_COMPUTER .. TEAM_COACH */
#include "save_format_json.h"
#include "swos_io.h"

#ifdef SWOS_SDL_BUILD
#include <dirent.h>
#include <stdio.h>
#include "diy_import_dos.h"
#endif

extern uint16_t    _numSelectedTeams;
extern uint16_t    _gameType;
extern uint8_t     _competitionFilename[14];
extern CompetitionSlotBuffer _primaryCompetitionBuf;
extern TeamTactics _teamTacticsUSERA, _teamTacticsUSERB, _teamTacticsUSERC;
extern TeamTactics _teamTacticsUSERD, _teamTacticsUSERE, _teamTacticsUSERF;
extern uint8_t     _classicCompsMenu[], _classicNationalMenu[], _classicListMenu[];
extern uint8_t     _classicTeamsMenu[];
extern void SetExitMenuFlag(void);
extern void SaveOptions(void);
extern void RestoreOptions(void);
extern void InitCareerVariables(uint16_t gameTypeParam);
extern void ShowErrorMenu(uint8_t *text);
extern void CallDrawMenuItem(MenuEntry *entry);
#ifndef SWOS_SDL_BUILD
extern uint32_t Resload_ListDir(const uint8_t *dirname, uint8_t *buffer, uint32_t buffersize);
#endif

/* The menus' entries, in the order menu_data.c lists them. */
enum { ClassicList_header, ClassicList_first, kListRows = 32,     /* 2 columns of 16 */
       kListColumn = kListRows / 2,
       ClassicList_exit = ClassicList_first + kListRows };
enum { ClassicTeams_header, ClassicTeams_hint, ClassicTeams_first, kTeamSlots = 64,
       ClassicTeams_play = ClassicTeams_first + kTeamSlots, ClassicTeams_exit,
       kTeamColumns = 4, kTeamRows = kTeamSlots / kTeamColumns };

enum {
    kComputerColour    = 10,    /* the friendly team list's: red */
    kPlayerCoachColour = 11,    /* purple */
    kCoachColour       = 13,    /* blue */
    kGameTypeDiy       = 1,
    kNameLen           = 36,
    kFileLen           = 64,
};

enum { kGroupUefa, kGroupWorldCup, kGroupClub };
static const char *const k_groupDirs[] = { "uefa", "worldcup", "club" };
static const char *const k_groupTitles[] = { "EURO", "WORLD CUP", "CLUB" };

static struct {
    int32_t kind;                   /* HISTORIC TOURNAMENTS' pick: 0 NATIONAL, 1 CLUB, -1 = EXIT */
    int32_t group;                  /* the group picked, -1 = EXIT */
    int32_t pick;                   /* the competition picked, -1 = EXIT */
    int32_t play;                   /* PLAY pressed on the teams screen */
    int32_t ready;                  /* one is loaded, for PresetCompetitionMenu to start */
    int32_t count;
    char    file[kListRows][kFileLen];   /* under classic/<group>/ */
    int8_t  root[kListRows];             /* which classic/ folder it is in (classic_root) */
    char    name[kListRows][kNameLen];   /* the competition's own name */
    char    title[kNameLen];
    char    teams[kTeamSlots][18];
} s;

/* strlen/strcmp: the Amiga's minimal C library has neither. */
static uint32_t str_len(const char *p) { uint32_t n = 0; while (p[n]) n++; return n; }
static int32_t  str_cmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (uint8_t)*a - (uint8_t)*b;
}

/* ---- files ---- */

/* Where classic/ folders are, first first: on SDL the player's assets folder (their own
 * SWOS 2020 files, scripts/copy_classics.ps1), then a classic/ beside the binary; on the
 * Amiga the drawer's. "" = the game's folder. */
static const char *classic_root(int32_t root)
{
#ifdef SWOS_SDL_BUILD
    if (root == 0)
        return swos_assets_dir();
#endif
    (void)root;
    return "";
}

#ifdef SWOS_SDL_BUILD
enum { kRoots = 2 };
#else
enum { kRoots = 1 };
#endif

/* <root>/classic/<group>[/<file>] */
static void classic_path(char *out, uint32_t cap, int32_t root, int32_t group, const char *file)
{
    const char *r = classic_root(root);
    if (file)
        swos_io_snprintf(out, cap, "%s%sclassic/%s/%s", r, r[0] ? "/" : "", k_groupDirs[group], file);
    else
        swos_io_snprintf(out, cap, "%s%sclassic/%s", r, r[0] ? "/" : "", k_groupDirs[group]);
}

static int32_t is_json(const uint8_t *data, uint32_t size)
{
    while (size && (*data == ' ' || *data == '\t' || *data == '\r' || *data == '\n')) {
        data++;
        size--;
    }
    return size && *data == '{';
}

/* A competition this target can load: its name, or 0. */
static int32_t competition_name(const uint8_t *data, uint32_t size, char *name)
{
    if (is_json(data, size)) {
        int n = mjson_get_string((const char *)data, (int)size,
                                 "$.comp_scalars.diy_preset_name_of_competition", name, kNameLen);
        return n > 0;
    }
#ifdef SWOS_SDL_BUILD
    if (DiyImportDos_IsDosFile(data, size)) {
        memcpy(name, data + 4, 35);         /* the name of a DOS .DIY: 35 bytes at 4 */
        name[35] = 0;
        return 1;
    }
#endif
    return 0;
}

/* ---- the list: every competition in the group's folder, by year ---- */

static int32_t has_diy_extension(const char *f)
{
    uint32_t n = str_len(f);
    return n > 4 && f[n - 4] == '.' && (f[n - 3] | 0x20) == 'd' && (f[n - 2] | 0x20) == 'i' &&
           (f[n - 1] | 0x20) == 'y';
}

/* The first four-digit number in a name: "1966 FIFA WORLD CUP", "UEFA EURO 1964". */
static int32_t name_year(const char *name)
{
    for (; *name; name++)
        if (name[0] >= '0' && name[0] <= '9' && name[1] >= '0' && name[1] <= '9' &&
            name[2] >= '0' && name[2] <= '9' && name[3] >= '0' && name[3] <= '9')
            return (name[0] - '0') * 1000 + (name[1] - '0') * 100 + (name[2] - '0') * 10 +
                   (name[3] - '0');
    return 0;
}

/* By year, then by name: a group lists in date order. */
static int32_t comes_before(int32_t a, int32_t b)
{
    int32_t dy = name_year(s.name[a]) - name_year(s.name[b]);
    return dy ? dy < 0 : str_cmp(s.name[a], s.name[b]) < 0;
}

static int32_t same_name_nocase(const char *a, const char *b)
{
    while (*a && (*a | 0x20) == (*b | 0x20)) { a++; b++; }
    return *a == 0 && *b == 0;
}

static void add_file(int32_t root, int32_t group, const char *file)
{
    char path[1280];
    uint8_t *data = NULL;
    uint32_t size = 0;
    int32_t i;

    if (!has_diy_extension(file) || str_len(file) >= kFileLen || s.count == kListRows)
        return;
    for (i = 0; i < s.count; i++)           /* the same file in an earlier root wins */
        if (same_name_nocase(s.file[i], file))
            return;
    classic_path(path, sizeof path, root, group, file);
    if (swos_io_load_data_alloc(path, &data, &size) != 0)
        return;
    if (competition_name(data, size, s.name[s.count])) {
        memcpy(s.file[s.count], file, str_len(file) + 1);
        s.root[s.count] = (int8_t)root;
        s.count++;
    }
#ifdef SWOS_SDL_BUILD
    else
        fprintf(stderr, "[classic] %s is not a competition this game reads: not listed\n", path);
#endif
    swos_io_free(data);
}

static void scan_group(int32_t group)
{
    char dir[1280], file[kFileLen], name[kNameLen];
    int32_t i, j, root;
    int8_t r;

    s.count = 0;
    for (root = 0; root < kRoots; root++) {
#ifdef SWOS_SDL_BUILD
        DIR *dp;
        struct dirent *d;
        if (root == 0 && !classic_root(0)[0])
            continue;                       /* no assets folder */
        classic_path(dir, sizeof dir, root, group, 0);
        dp = opendir(dir);
        if (!dp)
            continue;
        while ((d = readdir(dp)) != NULL)
            add_file(root, group, d->d_name);
        closedir(dp);
#else
        static uint8_t names[2048];
        uint32_t n;
        const char *p = (const char *)names;
        classic_path(dir, sizeof dir, root, group, 0);
        n = Resload_ListDir((const uint8_t *)dir, names, sizeof names);
        while (n--) {
            add_file(root, group, p);
            p += str_len(p) + 1;
        }
#endif
    }

    for (i = 1; i < s.count; i++)           /* at most 32: insertion sort */
        for (j = i; j > 0 && comes_before(j, j - 1); j--) {
            memcpy(file, s.file[j], kFileLen);
            memcpy(s.file[j], s.file[j - 1], kFileLen);
            memcpy(s.file[j - 1], file, kFileLen);
            memcpy(name, s.name[j], kNameLen);
            memcpy(s.name[j], s.name[j - 1], kNameLen);
            memcpy(s.name[j - 1], name, kNameLen);
            r = s.root[j];
            s.root[j] = s.root[j - 1];
            s.root[j - 1] = r;
        }
}

/* ---- the menus' handlers (named in menu_data.c) ---- */

void ClassicComps_PickNational(void) { s.kind = 0; SetExitMenuFlag(); }
void ClassicComps_PickClub(void)     { s.kind = 1; SetExitMenuFlag(); }
void ClassicComps_PickUefa(void)     { s.group = kGroupUefa; SetExitMenuFlag(); }
void ClassicComps_PickWorldCup(void) { s.group = kGroupWorldCup; SetExitMenuFlag(); }

/* The list's buttons are a column at a time, 16 and 16. A list that needs both columns
 * fills them a row at a time, left then right, so the two end level; one that fits the
 * first column keeps it, one competition per row. */
static int32_t competition_of_entry(uint16_t ordinal)
{
    int32_t slot = (int32_t)ordinal - ClassicList_first;
    if (s.count <= kListColumn)
        return slot;
    return slot % kListColumn * 2 + slot / kListColumn;
}

void ClassicComps_ListInit(void)
{
    static char none[] = "NO COMPETITIONS IN THIS FOLDER";
    /* A list that fits the first column is drawn in the middle, halfway between the two
     * columns; a second column puts it back on the left. The entries are unpacked afresh
     * each time the menu opens, so the columns' x is the menu's own. */
    uint16_t left = CalcMenuEntryAddress(ClassicList_first)->x;
    uint16_t right = CalcMenuEntryAddress(ClassicList_first + kListColumn)->x;
    uint16_t x = s.count <= kListColumn ? (uint16_t)((left + right) / 2) : left;
    int32_t i;

    for (i = 0; i < kListColumn; i++)
        CalcMenuEntryAddress((uint16_t)(ClassicList_first + i))->x = x;
    CalcMenuEntryAddress(ClassicList_header)->fg = (uintptr_t)s.title;
    for (i = 0; i < kListRows; i++) {
        MenuEntry *e = CalcMenuEntryAddress((uint16_t)(ClassicList_first + i));
        int32_t comp = competition_of_entry(e->ordinal);
        e->fg = (uintptr_t)(comp < s.count ? s.name[comp] : none);
        e->isInvisible = !(comp < s.count || (comp == 0 && s.count == 0));
        e->isDisabled = s.count == 0;
    }
    if (s.count > kListColumn) {
        /* Row by row, the cursor's links follow: across, down a column, and from a
         * column's last competition to EXIT. */
        int32_t left_rows = (s.count + 1) / 2, right_rows = s.count / 2;
        for (i = 0; i < kListColumn; i++) {
            MenuEntry *l = CalcMenuEntryAddress((uint16_t)(ClassicList_first + i));
            MenuEntry *r = CalcMenuEntryAddress((uint16_t)(ClassicList_first + kListColumn + i));
            l->rightEntry = i < right_rows ? (int8_t)r->ordinal : -1;
            l->downEntry  = i + 1 < left_rows ? (int8_t)(l->ordinal + 1) : (int8_t)ClassicList_exit;
            r->downEntry  = i + 1 < right_rows ? (int8_t)(r->ordinal + 1) : (int8_t)ClassicList_exit;
        }
    }
}

void ClassicComps_PickCompetition(MenuEntry *entry)
{
    int32_t i = competition_of_entry(entry->ordinal);
    if (i >= 0 && i < s.count) {
        s.pick = i;
        SetExitMenuFlag();
    }
}

/* The teams screen. A competition of up to 48 teams is laid out as the game's own team
 * lists are (chooseTeamsFriendlyMenu, selectTeamsMenu): three columns of 100-pixel
 * buttons, wide enough for a 16-letter name. The menu's own 4 x 16 grid of narrower
 * buttons is kept for more than that, up to a DIY competition's 64. */
enum { kWideColumns = 3, kWideWidth = 100, kWideTeams = kWideColumns * kTeamRows };

static int8_t s_wide;     /* the buttons are in three columns, row by row */

/* The menu lists its buttons a column at a time; the teams fill them a row at a time,
 * so a competition takes only the rows it needs. */
static int32_t team_of_entry(uint16_t ordinal)
{
    int32_t slot = (int32_t)ordinal - ClassicTeams_first;
    if (s_wide)
        return slot;
    return slot % kTeamRows * kTeamColumns + slot / kTeamRows;
}

/* Three columns across the four's span, from the top-left button down, and the cursor's
 * links to match: across a row, down a column, and from the last row to PLAY and EXIT. */
static void lay_out_wide(int32_t teams)
{
    static uint16_t left, span, top, step;    /* the menu's grid, as unpacked */
    int32_t i, last_row;
    uint16_t gap;

    if (step == 0) {
        const MenuEntry *first = CalcMenuEntryAddress(ClassicTeams_first);
        const MenuEntry *right =
            CalcMenuEntryAddress((uint16_t)(ClassicTeams_first + (kTeamColumns - 1) * kTeamRows));
        left = first->x;
        span = (uint16_t)(right->x + right->dwWidth - left);
        top  = first->y;
        step = (uint16_t)(CalcMenuEntryAddress(ClassicTeams_first + 1)->y - top);
    }
    gap = (uint16_t)((span - kWideColumns * kWideWidth) / (kWideColumns - 1));
    last_row = (teams - 1) / kWideColumns * kWideColumns;

    for (i = 0; i < kTeamSlots; i++) {
        MenuEntry *e = CalcMenuEntryAddress((uint16_t)(ClassicTeams_first + i));
        int32_t col = i % kWideColumns;
        int8_t  me  = (int8_t)(ClassicTeams_first + i);

        e->x = (uint16_t)(left + col * (kWideWidth + gap));
        e->y = (uint16_t)(top + i / kWideColumns * step);
        e->dwWidth    = kWideWidth;
        e->leftEntry  = col > 0 ? (int8_t)(me - 1) : -1;
        e->rightEntry = col < kWideColumns - 1 && i + 1 < teams ? (int8_t)(me + 1) : -1;
        e->upEntry    = i >= kWideColumns ? (int8_t)(me - kWideColumns) : -1;
        e->downEntry  = i + kWideColumns < teams ? (int8_t)(me + kWideColumns)
                                                  : (int8_t)ClassicTeams_play;
    }
    CalcMenuEntryAddress(ClassicTeams_play)->upEntry = (int8_t)(ClassicTeams_first + last_row);
    CalcMenuEntryAddress(ClassicTeams_exit)->upEntry = (int8_t)(ClassicTeams_first + last_row);
}

static void name_team_entries(void)
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

void ClassicComps_TeamsInit(void)
{
    s_wide = _numSelectedTeams <= kWideTeams;
    if (s_wide)
        lay_out_wide(_numSelectedTeams);
    name_team_entries();
}

void ClassicComps_ToggleTeam(MenuEntry *entry)
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
    name_team_entries();
    CallDrawMenuItem(entry);
}

void ClassicComps_Play(void) { s.play = 1; SetExitMenuFlag(); }

/* ---- loading one ---- */

/* A JSON .DIY also carries options and USER tactics: a classic competition keeps the
 * player's (SaveOptions/RestoreOptions for the first, a copy for the second). */
static int16_t load_json(const uint8_t *data, uint32_t size)
{
    TeamTactics *const tactics[6] = {
        &_teamTacticsUSERA, &_teamTacticsUSERB, &_teamTacticsUSERC,
        &_teamTacticsUSERD, &_teamTacticsUSERE, &_teamTacticsUSERF,
    };
    static TeamTactics kept[6];
    int16_t r;
    int32_t i;

    for (i = 0; i < 6; i++)
        kept[i] = *tactics[i];
    r = SaveJson_LoadDIYData(data, size);
    for (i = 0; i < 6; i++)
        *tactics[i] = kept[i];
    RestoreOptions();
    return r;
}

/* 1 = loaded, every team COMPUTER; 0 = it would not load (the error was shown). */
static int32_t load_competition(int32_t group, int32_t pick)
{
    char path[1280];
    uint8_t *data = NULL;
    uint32_t size = 0;
    int16_t r = 1;
    int32_t i;

    classic_path(path, sizeof path, s.root[pick], group, s.file[pick]);
    SaveOptions();
    _gameType = kGameTypeDiy;
    if (swos_io_load_data_alloc(path, &data, &size) == 0) {
        if (is_json(data, size))
            r = load_json(data, size);
#ifdef SWOS_SDL_BUILD
        else
            r = DiyImportDos_LoadData(path, data, size, 1);
#endif
        swos_io_free(data);
    }
    if (r != 0) {
        ShowErrorMenu((uint8_t *)"THIS COMPETITION COULD NOT BE LOADED");
        RestoreOptions();
        InitCareerVariables(0);
        return 0;
    }
    for (i = 0; i < _numSelectedTeams; i++) {
        _selectedTeams[i].team.teamControls = TEAM_COMPUTER;
        memcpy(s.teams[i], _selectedTeams[i].team.teamName, 17);
        s.teams[i][17] = 0;
    }
    return 1;
}

/* The list, then the teams screen: 1 = PLAY (a competition is ready), 0 = EXIT. */
static int32_t choose_in_group(int32_t group)
{
    memcpy(s.title, k_groupTitles[group], str_len(k_groupTitles[group]) + 1);
    scan_group(group);
    for (;;) {
        s.pick = -1;
        ShowMenu(_classicListMenu);
        if (s.pick < 0)
            return 0;
        if (!load_competition(group, s.pick))
            continue;
        s.play = 0;
        ShowMenu(_classicTeamsMenu);
        if (s.play) {
            /* SAVE offers the file's own name, as after LOAD OLD COMPETITION: WITH its
             * extension, which SAVE's dialog reads back from after the '.' when the
             * name is accepted (file_io.c CopyFilenameExtensionAndSetExitMenuFlag) --
             * without one it saved "WC1990" outside save/. 8.3, as SWOS's names are. */
            uint32_t n = str_len(s.file[s.pick]) - 4;
            if (n > 8)
                n = 8;
            memcpy(_competitionFilename, s.file[s.pick], n);
            memcpy(_competitionFilename + n, ".DIY", 5);
            s.ready = 1;
            return 1;
        }
        InitCareerVariables(0);
        RestoreOptions();
    }
}

/* EXIT goes back one screen: from a list to NATIONAL's groups, or to HISTORIC TOURNAMENTS for CLUB,
 * which has only the one. */
int32_t ClassicComps_Choose(void)
{
    s.ready = 0;
    for (;;) {
        s.kind = -1;
        ShowMenu(_classicCompsMenu);
        if (s.kind < 0)
            return 0;
        if (s.kind == 1) {
            s.group = kGroupClub;
            if (choose_in_group(s.group))
                return 1;
            continue;
        }
        for (;;) {
            s.group = -1;
            ShowMenu(_classicNationalMenu);
            if (s.group < 0)
                break;
            if (choose_in_group(s.group))
                return 1;
        }
    }
}

int32_t ClassicComps_TakeReady(void)
{
    int32_t ready = s.ready;
    s.ready = 0;
    return ready;
}
