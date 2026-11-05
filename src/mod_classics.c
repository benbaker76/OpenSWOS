/*
 * mod_classics.c -- HISTORIC TOURNAMENTS: famous tournaments, played like a preset competition
 * (bin/mods/classics; by petarku / the5ar, PR #11, made a mod: docs/MODS.md §20). SDL builds.
 *
 * Each one is a .DIY kept by group in a classic/ folder -- the player's assets folder first
 * (<assets>/classic/{uefa,worldcup,club}/: SWOS 2020's own cups, which the player copies from
 * their install), then the mod's own folder (mods/classics/classic/...). A file is the PC
 * game's or SWOS 2020's .DIY, or this game's JSON .DIY. None is in this repository.
 *
 * The row is the game's: MODPOINT_COMPETITIONS, above WORLD CUP in PRESET COMPETITION's world
 * list (src/mod_comps.c). From it: NATIONAL (EURO, WORLD CUP) or CLUB, a group's list by year,
 * then its teams, each COMPUTER until the player cycles it through PLAYER-COACH and COACH; PLAY
 * starts it (ModApi_StartCompetition). EXIT goes back one screen.
 */
#include "mod_api.h"

#ifdef SWOS_SDL_BUILD

#include <stdio.h>
#include <string.h>

#define SLUG "classics"

/* The menus' entries, in the order their JSON lists them. */
enum { List_header, List_first, kListRows = 32, kListColumn = kListRows / 2,   /* 2 columns of 16 */
       List_exit = List_first + kListRows };
enum { Teams_header, Teams_hint, Teams_first, kTeamSlots = 64,
       Teams_play = Teams_first + kTeamSlots, Teams_exit,
       kTeamColumns = 4, kTeamRows = kTeamSlots / kTeamColumns };

enum { kComputerColour = 10, kPlayerCoachColour = 11, kCoachColour = 13 };   /* the friendly list's */
enum { kNameLen = 36, kFileLen = 64, kRoots = 2 };

enum { kGroupUefa, kGroupWorldCup, kGroupClub, kGroups };
static const char *const k_groupDirs[kGroups]   = { "uefa", "worldcup", "club" };
static const char *const k_groupTitles[kGroups] = { "EURO", "WORLD CUP", "CLUB" };

static struct {
    int32_t kind;                       /* 0 NATIONAL, 1 CLUB, -1 EXIT */
    int32_t group;                      /* -1 EXIT */
    int32_t pick;                       /* the competition picked, -1 EXIT */
    int32_t play;                       /* PLAY on the teams screen */
    int32_t count;
    char    file[kListRows][kFileLen];
    int8_t  root[kListRows];
    char    name[kListRows][kNameLen];
    char    title[kNameLen];
    char    teams[kTeamSlots][18];
    char    header[kNameLen];
} s;

/* ---- files ---- */

/* <root>/classic/<group>[/<file>]: root 0 the assets folder, 1 the mod's. "" when there is none. */
static int32_t classic_path(char *out, uint32_t cap, int32_t root, int32_t group, const char *file)
{
    const char *r = root == 0 ? ModApi_AssetsDir() : ModApi_ModDir(SLUG);
    if (!r || !r[0])
        return 0;
    if (file)
        snprintf(out, cap, "%s/classic/%s/%s", r, k_groupDirs[group], file);
    else
        snprintf(out, cap, "%s/classic/%s", r, k_groupDirs[group]);
    return 1;
}

static int32_t has_diy_extension(const char *f)
{
    size_t n = strlen(f);
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

static int32_t comes_before(int32_t a, int32_t b)
{
    const int32_t dy = name_year(s.name[a]) - name_year(s.name[b]);
    return dy ? dy < 0 : strcmp(s.name[a], s.name[b]) < 0;
}

static int32_t same_name_nocase(const char *a, const char *b)
{
    while (*a && (*a | 0x20) == (*b | 0x20))
        a++, b++;
    return *a == 0 && *b == 0;
}

typedef struct { int32_t root, group; } ScanCtx;

static int32_t add_file(const char *file, void *vctx)
{
    const ScanCtx *ctx = (const ScanCtx *)vctx;
    char path[1280];
    if (!has_diy_extension(file) || strlen(file) >= kFileLen || s.count == kListRows)
        return 0;
    for (int32_t i = 0; i < s.count; i++)       /* the same file in an earlier root wins */
        if (same_name_nocase(s.file[i], file))
            return 0;
    if (!classic_path(path, sizeof path, ctx->root, ctx->group, file))
        return 0;
    if (ModApi_CompetitionFileName(path, s.name[s.count], kNameLen)) {
        snprintf(s.file[s.count], kFileLen, "%s", file);
        s.root[s.count] = (int8_t)ctx->root;
        s.count++;
    } else {
        fprintf(stderr, "[classics] %s is not a competition this game reads: not listed\n", path);
    }
    return 0;
}

static int32_t scan_group(int32_t group)
{
    char dir[1280];
    s.count = 0;
    for (int32_t root = 0; root < kRoots; root++) {
        ScanCtx ctx = { root, group };
        if (classic_path(dir, sizeof dir, root, group, NULL))
            ModApi_ListFiles(dir, add_file, &ctx);
    }
    for (int32_t i = 1; i < s.count; i++)       /* at most 32: insertion sort */
        for (int32_t j = i; j > 0 && comes_before(j, j - 1); j--) {
            char file[kFileLen], name[kNameLen];
            int8_t r;
            memcpy(file, s.file[j], kFileLen);   memcpy(s.file[j], s.file[j - 1], kFileLen);
            memcpy(s.file[j - 1], file, kFileLen);
            memcpy(name, s.name[j], kNameLen);   memcpy(s.name[j], s.name[j - 1], kNameLen);
            memcpy(s.name[j - 1], name, kNameLen);
            r = s.root[j]; s.root[j] = s.root[j - 1]; s.root[j - 1] = r;
        }
    return s.count;
}

/* ---- the menus' handlers ---- */

extern void SetExitMenuFlag(void);      /* (a menu's EXIT, as every mod's handler leaves one) */

static void pickNational(void) { s.kind = 0; SetExitMenuFlag(); }
static void pickClub(void)     { s.kind = 1; SetExitMenuFlag(); }
static void pickUefa(void)     { s.group = kGroupUefa; SetExitMenuFlag(); }
static void pickWorldCup(void) { s.group = kGroupWorldCup; SetExitMenuFlag(); }

/* The list's buttons are a column at a time, 16 and 16. A list that needs both columns fills
 * them a row at a time, left then right, so the two end level; one that fits the first keeps it. */
static int32_t competition_of_entry(int32_t entry)
{
    const int32_t slot = entry - List_first;
    if (s.count <= kListColumn)
        return slot;
    return slot % kListColumn * 2 + slot / kListColumn;
}

static void listInit(void)
{
    static char none[] = "NO COMPETITIONS IN THIS FOLDER";
    int32_t left, right, x, i;

    /* A list that fits the first column is drawn in the middle, halfway between the two. */
    ModApi_MenuEntryRect(List_first, &left, NULL, NULL, NULL);
    ModApi_MenuEntryRect(List_first + kListColumn, &right, NULL, NULL, NULL);
    x = s.count <= kListColumn ? (left + right) / 2 : left;
    for (i = 0; i < kListColumn; i++)
        ModApi_SetMenuEntryRect(List_first + i, x, -1, -1, -1);
    ModApi_MenuText(List_header, s.title);
    for (i = 0; i < kListRows; i++) {
        const int32_t comp = competition_of_entry(List_first + i);
        ModApi_MenuText(List_first + i, comp < s.count ? s.name[comp] : none);
        ModApi_MenuVisible(List_first + i, comp < s.count || (comp == 0 && s.count == 0));
        ModApi_MenuDisabled(List_first + i, s.count == 0);
    }
    if (s.count > kListColumn) {
        /* Row by row: across, down a column, and from a column's last to EXIT. */
        const int32_t leftRows = (s.count + 1) / 2, rightRows = s.count / 2;
        for (i = 0; i < kListColumn; i++) {
            const int32_t l = List_first + i, r = List_first + kListColumn + i;
            ModApi_MenuNav4(l, -1, i < rightRows ? r : -1, i ? l - 1 : -1,
                            i + 1 < leftRows ? l + 1 : List_exit);
            ModApi_MenuNav4(r, l, -1, i ? r - 1 : -1, i + 1 < rightRows ? r + 1 : List_exit);
        }
    }
}

static void pickCompetition(void)
{
    const int32_t i = competition_of_entry(ModApi_MenuCurrentEntry());
    if (i >= 0 && i < s.count) {
        s.pick = i;
        SetExitMenuFlag();
    }
}

/* The teams screen. Up to 48 teams are laid out as the game's own team lists are: three
 * columns of 100-pixel buttons, wide enough for a 16-letter name. The menu's own 4 x 16 grid
 * of narrower ones is kept for more, up to a DIY competition's 64. */
enum { kWideColumns = 3, kWideWidth = 100, kWideTeams = kWideColumns * kTeamRows };

static int8_t s_wide;

static int32_t team_of_entry(int32_t entry)
{
    const int32_t slot = entry - Teams_first;
    if (s_wide)
        return slot;
    return slot % kTeamRows * kTeamColumns + slot / kTeamRows;
}

static void lay_out_wide(int32_t teams)
{
    static int32_t left, span, top, step;   /* the menu's grid, as unpacked */
    int32_t i, lastRow, gap;

    if (step == 0) {
        int32_t rx, rw, y1;
        ModApi_MenuEntryRect(Teams_first, &left, &top, NULL, NULL);
        ModApi_MenuEntryRect(Teams_first + (kTeamColumns - 1) * kTeamRows, &rx, NULL, &rw, NULL);
        ModApi_MenuEntryRect(Teams_first + 1, NULL, &y1, NULL, NULL);
        span = rx + rw - left;
        step = y1 - top;
    }
    gap = (span - kWideColumns * kWideWidth) / (kWideColumns - 1);
    lastRow = (teams - 1) / kWideColumns * kWideColumns;
    for (i = 0; i < kTeamSlots; i++) {
        const int32_t col = i % kWideColumns, me = Teams_first + i;
        ModApi_SetMenuEntryRect(me, left + col * (kWideWidth + gap), top + i / kWideColumns * step,
                                kWideWidth, -1);
        ModApi_MenuNav4(me, col > 0 ? me - 1 : -1,
                        col < kWideColumns - 1 && i + 1 < teams ? me + 1 : -1,
                        i >= kWideColumns ? me - kWideColumns : -1,
                        i + kWideColumns < teams ? me + kWideColumns : Teams_play);
    }
    ModApi_MenuNav4(Teams_play, -1, Teams_exit, Teams_first + lastRow, -1);
    ModApi_MenuNav4(Teams_exit, Teams_play, -1, Teams_first + lastRow, -1);
}

static void name_team_entries(void)
{
    const int32_t n = ModApi_CompTeamCount();
    snprintf(s.header, sizeof s.header, "%s", ModApi_CompName());
    ModApi_MenuText(Teams_header, s.header);
    for (int32_t i = 0; i < kTeamSlots; i++) {
        const int32_t team = team_of_entry(Teams_first + i);
        const int32_t c = team < n ? ModApi_CompTeamControls(team) : MODCONTROL_COMPUTER;
        ModApi_MenuVisible(Teams_first + i, team < n);
        ModApi_MenuText(Teams_first + i, s.teams[team]);
        ModApi_MenuColour(Teams_first + i, c == MODCONTROL_PLAYER_COACH ? kPlayerCoachColour
                                         : c == MODCONTROL_COACH        ? kCoachColour
                                                                        : kComputerColour);
    }
}

static void teamsInit(void)
{
    s_wide = ModApi_CompTeamCount() <= kWideTeams;
    if (s_wide)
        lay_out_wide(ModApi_CompTeamCount());
    name_team_entries();
}

static void toggleTeam(void)
{
    const int32_t entry = ModApi_MenuCurrentEntry();
    int32_t i, c;
    if (entry < Teams_first || entry >= Teams_play)
        return;
    i = team_of_entry(entry);
    if (i >= ModApi_CompTeamCount())
        return;
    c = ModApi_CompTeamControls(i);
    ModApi_SetCompTeamControls(i, c == MODCONTROL_COMPUTER     ? MODCONTROL_PLAYER_COACH
                                : c == MODCONTROL_PLAYER_COACH ? MODCONTROL_COACH
                                                               : MODCONTROL_COMPUTER);
    fprintf(stderr, "[classics] %s: %s\n", s.teams[i],
            ModApi_CompTeamControls(i) == MODCONTROL_COMPUTER ? "COMPUTER"
          : ModApi_CompTeamControls(i) == MODCONTROL_PLAYER_COACH ? "PLAYER-COACH" : "COACH");
    name_team_entries();
    ModApi_MenuRedraw(entry);
}

static void play(void) { s.play = 1; SetExitMenuFlag(); }

/* ---- the flow ---- */

static int32_t load_competition(int32_t group, int32_t pick)
{
    char path[1280];
    if (!classic_path(path, sizeof path, s.root[pick], group, s.file[pick]) ||
        ModApi_LoadCompetition(path) != 0) {
        ModApi_ShowError("THIS COMPETITION COULD NOT BE LOADED");
        return 0;
    }
    for (int32_t i = 0; i < ModApi_CompTeamCount() && i < kTeamSlots; i++)
        snprintf(s.teams[i], sizeof s.teams[i], "%s", ModApi_CompTeamName(i));
    return 1;
}

/* The list, then the teams screen: 1 = PLAY (started), 0 = EXIT. */
static int32_t choose_in_group(int32_t group)
{
    snprintf(s.title, sizeof s.title, "%s", k_groupTitles[group]);
    scan_group(group);
    for (;;) {
        s.pick = -1;
        ModApi_ShowMenu(SLUG "/listMenu");
        if (s.pick < 0)
            return 0;
        if (!load_competition(group, s.pick))
            continue;
        s.play = 0;
        ModApi_ShowMenu(SLUG "/teamsMenu");
        if (s.play) {
            ModApi_StartCompetition();
            return 1;
        }
        ModApi_UnloadCompetition();
    }
}

static void open_classics(void)
{
    for (;;) {
        s.kind = -1;
        ModApi_ShowMenu(SLUG "/kindMenu");
        if (s.kind < 0)
            return;
        if (s.kind == 1) {
            if (choose_in_group(kGroupClub))
                return;
            continue;
        }
        for (;;) {
            s.group = -1;
            ModApi_ShowMenu(SLUG "/nationalMenu");
            if (s.group < 0)
                break;
            if (choose_in_group(s.group))
                return;
        }
    }
}

static const ModHandler kHandlers[] = {
    { "pickNational", pickNational }, { "pickClub", pickClub },
    { "pickUefa", pickUefa },         { "pickWorldCup", pickWorldCup },
    { "listInit", listInit },         { "pickCompetition", pickCompetition },
    { "teamsInit", teamsInit },       { "toggleTeam", toggleTeam },
    { "play", play },
};

static void classics_init(void)
{
    int32_t any = 0;
    for (int32_t g = 0; g < kGroups && !any; g++)
        any = scan_group(g) > 0;
    s.count = 0;
    ModApi_RegisterHandlers(SLUG, kHandlers, (int32_t)(sizeof kHandlers / sizeof kHandlers[0]));
    /* The row only when there is something to play: none of the files is in this repository. */
    if (any)
        ModApi_AddEntry(MODPOINT_COMPETITIONS, SLUG, "historic tournaments", open_classics);
    else
        fprintf(stderr, "[classics] no classic/ folder with a competition: no HISTORIC TOURNAMENTS row\n");
}

const ModDef ModDef_classics = {
    .slug = SLUG,
    .init = classics_init,
};

#endif /* SWOS_SDL_BUILD */
