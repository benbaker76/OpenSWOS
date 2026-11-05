/*
 * mod_comps.c -- competitions from mods (include/mod_api.h MODPOINT_COMPETITIONS; docs/MODS.md
 * §20): the PRESET COMPETITION row, loading a mod's .DIY as a competition to START, its teams,
 * and the menu helpers a list-and-teams screen needs. SDL builds.
 *
 * The row: one, above WORLD CUP in PRESET COMPETITION's world list (teams.c
 * SelectTeamsFinalMenu), named by the entry when there is one and MORE COMPETITIONS when there
 * are more (it then lists them). The entry's select shows the mod's screens; when one of them
 * has loaded a competition (ModApi_LoadCompetition) and the player chose PLAY, it calls
 * ModApi_StartCompetition: the chooser closes as if backed out of and PresetCompetitionMenu
 * starts it, as LOAD OLD COMPETITION's is started (StartContest).
 */
#include "mod_api.h"

#ifdef SWOS_SDL_BUILD
#include "diy_import_dos.h"
#include <dirent.h>
#endif
#include "game_types.h"
#include "menu_system.h"
#include "menus.h"
#include "play_match_menu.h"
#include "save_format_json.h"
#include "swos_io.h"
#include "tactics_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint16_t _numSelectedTeams;
extern uint16_t _gameType;
extern uint8_t  _competitionFilename[14];
extern CompetitionSlotBuffer _primaryCompetitionBuf;

enum { kGameTypeDiy = 1 };

static int32_t s_ready;         /* a competition is loaded, for PresetCompetitionMenu to start */
static int32_t s_loaded;        /* one is loaded (options saved, to be restored on unload) */
static char    s_rowLabel[40];

/* ---- the row (teams.c) ---- */

const char *ModComps_RowLabel(void)
{
    const int32_t n = ModApi_EntryCount(MODPOINT_COMPETITIONS);
    if (n <= 0)
        return NULL;
    if (n > 1)
        return "MORE COMPETITIONS";
    snprintf(s_rowLabel, sizeof s_rowLabel, "%s", ModApi_EntryLabel(MODPOINT_COMPETITIONS, 0));
    return s_rowLabel;
}

/* The row chosen: the entry (or the list of them). 1 when a competition is ready to start. */
int32_t ModComps_Choose(void)
{
    s_ready = 0;
    if (ModApi_EntryCount(MODPOINT_COMPETITIONS) == 1)
        ModApi_RunEntry(MODPOINT_COMPETITIONS, 0);
    else
        ModApi_ShowEntries(MODPOINT_COMPETITIONS);
    return s_ready;
}

int32_t ModComps_Pending(void) { return s_ready; }

int32_t ModComps_TakeReady(void)
{
    const int32_t r = s_ready;
    s_ready = 0;
    if (r)
        s_loaded = 0;           /* (it is the game's competition now) */
    return r;
}

/* ---- loading one ---- */

static int32_t is_json(const uint8_t *data, uint32_t size)
{
    while (size && (*data == ' ' || *data == '\t' || *data == '\r' || *data == '\n'))
        data++, size--;
    return size && *data == '{';
}

#ifndef SWOS_SDL_BUILD
/* (the Amiga: through swos_io, NUL-terminated; free() is swos_io_free there) */
static int32_t read_file(const char *path, uint8_t **data, uint32_t *size)
{
    *data = NULL;
    return swos_io_load_data_alloc(path, data, size) != 0 || !*data || !*size;
}
#else
static int32_t read_file(const char *path, uint8_t **data, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    long n;
    if (!f)
        return 1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *data = (uint8_t *)malloc(n > 0 ? (size_t)n + 1 : 1);
    if (!*data || n <= 0 || fread(*data, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(*data);
        *data = NULL;
        return 1;
    }
    fclose(f);
    (*data)[n] = 0;
    *size = (uint32_t)n;
    return 0;
}
#endif

int32_t ModApi_CompetitionFileName(const char *path, char *out, int32_t cap)
{
    uint8_t *data = NULL;
    uint32_t size = 0;
    int32_t ok = 0;
    if (!out || cap <= 0 || read_file(path, &data, &size) != 0)
        return 0;
    out[0] = 0;
    if (is_json(data, size)) {
        extern int mjson_get_string(const char *s, int len, const char *path, char *to, int n);
        ok = mjson_get_string((const char *)data, (int)size,
                              "$.comp_scalars.diy_preset_name_of_competition", out, cap) > 0;
    }
#ifdef SWOS_SDL_BUILD   /* (the DOS game's .DIY: the SDL build reads those, docs/SAVE_DATA.md) */
    else if (DiyImportDos_IsDosFile(data, size)) {
        const int32_t n = cap - 1 < 35 ? cap - 1 : 35;     /* a DOS .DIY's name: 35 bytes at 4 */
        memcpy(out, data + 4, (size_t)n);
        out[n] = 0;
        ok = 1;
    }
#endif
    free(data);
    return ok;
}

int32_t ModApi_LoadCompetition(const char *path)
{
    static TeamTactics kept[6];
    TeamTactics *const tactics[6] = {
        &_teamTacticsUSERA, &_teamTacticsUSERB, &_teamTacticsUSERC,
        &_teamTacticsUSERD, &_teamTacticsUSERE, &_teamTacticsUSERF,
    };
    uint8_t *data = NULL;
    uint32_t size = 0;
    int16_t r = 1;

    if (s_loaded)
        ModApi_UnloadCompetition();
    if (!path || read_file(path, &data, &size) != 0)
        return 1;
    SaveOptions();
    _gameType = kGameTypeDiy;
    if (is_json(data, size)) {
        /* A JSON .DIY carries options and USER tactics too: a started competition keeps the
         * player's. */
        for (int32_t i = 0; i < 6; i++)
            kept[i] = *tactics[i];
        r = SaveJson_LoadDIYData(data, size);
        for (int32_t i = 0; i < 6; i++)
            *tactics[i] = kept[i];
        RestoreOptions();
    } else {
#ifdef SWOS_SDL_BUILD
        r = DiyImportDos_LoadData(path, data, size, 1);
#else
        r = 1;          /* (a DOS .DIY: the SDL build's only) */
#endif
    }
    free(data);
    if (r != 0) {
        RestoreOptions();
        InitCareerVariables(0);
        return 1;
    }
    for (int32_t i = 0; i < _numSelectedTeams; i++)
        _selectedTeams[i].team.teamControls = TEAM_COMPUTER;

    /* SAVE then offers the file's own name, 8.3 WITH its extension, as after LOAD OLD
     * COMPETITION (file_io.c reads the name back from after the '.'). */
    {
        const char *base = strrchr(path, '/');
        size_t n;
        base = base ? base + 1 : path;
        n = strlen(base);
        if (n > 4 && base[n - 4] == '.')
            n -= 4;
        if (n > 8)
            n = 8;
        memcpy(_competitionFilename, base, n);
        memcpy(_competitionFilename + n, ".DIY", 5);
    }
    s_loaded = 1;
    return 0;
}

void ModApi_UnloadCompetition(void)
{
    if (!s_loaded)
        return;
    InitCareerVariables(0);
    RestoreOptions();
    s_loaded = 0;
}

void ModApi_StartCompetition(void)
{
    if (s_loaded)
        s_ready = 1;
}

const char *ModApi_CompName(void)
{
    return s_loaded ? (const char *)_primaryCompetitionBuf.tourn.header.competitionName : "";
}

int32_t ModApi_CompTeamCount(void) { return s_loaded ? _numSelectedTeams : 0; }

const char *ModApi_CompTeamName(int32_t i)
{
    static char name[18];
    if (i < 0 || i >= ModApi_CompTeamCount())
        return "";
    memcpy(name, _selectedTeams[i].team.teamName, 17);
    name[17] = 0;
    return name;
}

int32_t ModApi_CompTeamControls(int32_t i)
{
    if (i < 0 || i >= ModApi_CompTeamCount())
        return -1;
    switch (_selectedTeams[i].team.teamControls) {
    case TEAM_PLAYER_COACH: return MODCONTROL_PLAYER_COACH;
    case TEAM_COACH:        return MODCONTROL_COACH;
    default:                return MODCONTROL_COMPUTER;
    }
}

void ModApi_SetCompTeamControls(int32_t i, int32_t controls)
{
    if (i < 0 || i >= ModApi_CompTeamCount())
        return;
    _selectedTeams[i].team.teamControls =
        controls == MODCONTROL_PLAYER_COACH ? TEAM_PLAYER_COACH
      : controls == MODCONTROL_COACH        ? TEAM_COACH : TEAM_COMPUTER;
}

/* ---- Europe's league phases in 96/97 (H8) ---- */

int32_t ModApi_SetLeaguePhases(const char *slug, void *cups, const uint8_t (*places)[4], int32_t count,
                               const struct WorldUefaSeeds *seeds)
{
    extern void LeaguePhase_SetModEurope(CompetitionLeaguePhase *cups, const uint8_t (*places)[4],
                                         int32_t count, const struct WorldUefaSeeds *seeds);
    if (!cups || !places || count <= 0)
        return 1;
    LeaguePhase_SetModEurope((CompetitionLeaguePhase *)cups, places, count, seeds);
    fprintf(stderr, "[mods] %s: Europe's cups as league phases, %d countries' places%s\n",
            slug ? slug : "?", (int)count, seeds ? ", UEFA seeds" : "");
    return 0;
}

/* ---- files ---- */

#ifndef SWOS_SDL_BUILD
/* (the Amiga: no assets folder -- its data is the drawer's -- and no whole-match replays) */
const char *ModApi_AssetsDir(void) { return ""; }
int32_t ModApi_LatestReplay(char *out, int32_t cap) { if (out && cap > 0) out[0] = 0; return 0; }

/* Resload_ListDir: the mods' folders first, then the drawer's; NUL-separated names. */
int32_t ModApi_ListFiles(const char *dir, int32_t (*each)(const char *name, void *ctx), void *ctx)
{
    extern uint32_t Resload_ListDir(const uint8_t *dirname, uint8_t *buffer, uint32_t buffersize);
    static char names[4096];
    if (!dir)
        return 0;
    const uint32_t count = Resload_ListDir((const uint8_t *)dir, (uint8_t *)names, sizeof names);
    const char *p = names;
    int32_t n = 0;
    for (uint32_t i = 0; i < count && p < names + sizeof names; i++, p += strlen(p) + 1) {
        if (p[0] == '.')
            continue;
        n++;
        if (each && each(p, ctx) != 0)
            break;
    }
    return n;
}
#else
const char *ModApi_AssetsDir(void) { return swos_assets_dir(); }

int32_t ModApi_LatestReplay(char *out, int32_t cap)
{
    extern void ReplayV2_LatestPath(char *out, size_t cap);
    FILE *f;
    if (!out || cap <= 0)
        return 0;
    ReplayV2_LatestPath(out, (size_t)cap);
    f = fopen(out, "rb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

int32_t ModApi_ListFiles(const char *dir, int32_t (*each)(const char *name, void *ctx), void *ctx)
{
    DIR *dp = dir ? opendir(dir) : NULL;
    struct dirent *d;
    int32_t n = 0;
    if (!dp)
        return 0;
    while ((d = readdir(dp)) != NULL) {
        if (d->d_name[0] == '.')
            continue;
        n++;
        if (each && each(d->d_name, ctx) != 0)
            break;
    }
    closedir(dp);
    return n;
}
#endif

void ModApi_ShowError(const char *text)
{
    static char kept[80];
    snprintf(kept, sizeof kept, "%s", text ? text : "");
    ShowErrorMenu((uint8_t *)kept);
}

/* ---- text on the menu frame ---- */

#include "display.h"
#include "text_render.h"
extern void *_charTables[];

void ModApi_DrawText(int32_t x, int32_t y, int32_t colour, const char *text, int32_t small)
{
    if (text && x >= 0 && y >= 0)
        DrawMenuText((uint16_t)x, (uint16_t)y, (uint16_t)colour, (uint8_t *)text,
                     (uint8_t *)_charTables[small ? 0 : 1]);
}

int32_t ModApi_TextWidth(const char *text, int32_t small)
{
    return text ? GetTextWidth((uint8_t *)text, (uint8_t *)_charTables[small ? 0 : 1]) : 0;
}

int32_t ModApi_MenuTickerEntry(void)
{
#ifdef SWOS_SDL_BUILD
    extern int32_t MenuExtras_TickerEntry(void);
    return MenuExtras_TickerEntry();
#else
    return -1;          /* (menu extras -- the ticker row among them -- are the SDL build's) */
#endif
}

void ModApi_RestoreMenuBackground(int32_t x, int32_t y, int32_t w, int32_t h)
{
    extern volatile uintptr_t _frontbufferBasePtr;
    extern uint8_t _frontbufferMaskBuffer[];
    uint8_t *dst = (uint8_t *)(uintptr_t)_frontbufferBasePtr;
    x += _menu_draw_x_offset;
    if (!dst || w <= 0 || h <= 0)
        return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > _screen_width) w = _screen_width - x;
    if (y + h > _screen_height) h = _screen_height - y;
    for (int32_t r = 0; r < h && w > 0; r++) {
        const size_t off = (size_t)(y + r) * FRONTBUFFER_ROW_STRIDE_BYTES + (size_t)x;
        memcpy(dst + off, _frontbufferMaskBuffer + off, (size_t)w);
    }
}

/* ---- the menu being shown ---- */

int32_t ModApi_MenuCurrentEntry(void)
{
    extern uint8_t _currentMenu[];
    const Menu *menu = (const Menu *)(void *)_currentMenu;
    return menu && menu->selectedEntry ? menu->selectedEntry->ordinal : -1;
}

void ModApi_MenuEntryRect(int32_t entry, int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    const MenuEntry *e = CalcMenuEntryAddress((uint16_t)entry);
    if (!e)
        return;
    if (x) *x = e->x;
    if (y) *y = e->y;
    if (w) *w = e->dwWidth;
    if (h) *h = e->dwHeight;
}

void ModApi_SetMenuEntryRect(int32_t entry, int32_t x, int32_t y, int32_t w, int32_t h)
{
    MenuEntry *e = CalcMenuEntryAddress((uint16_t)entry);
    if (!e)
        return;
    if (x >= 0) e->x = (uint16_t)x;
    if (y >= 0) e->y = (uint16_t)y;
    if (w >= 0) e->dwWidth = (uint16_t)w;
    if (h >= 0) e->dwHeight = (uint16_t)h;
}

void ModApi_MenuNav4(int32_t entry, int32_t left, int32_t right, int32_t up, int32_t down)
{
    MenuEntry *e = CalcMenuEntryAddress((uint16_t)entry);
    if (!e)
        return;
    e->leftEntry = (int8_t)left;
    e->rightEntry = (int8_t)right;
    e->upEntry = (int8_t)up;
    e->downEntry = (int8_t)down;
    e->directionLeft = e->directionRight = e->directionUp = e->directionDown = -1;
}

void ModApi_MenuColour(int32_t entry, int32_t colour)
{
    MenuEntry *e = CalcMenuEntryAddress((uint16_t)entry);
    if (e)
        e->bgColor = (uint16_t)colour;
}

void ModApi_MenuDisabled(int32_t entry, int32_t disabled)
{
    MenuEntry *e = CalcMenuEntryAddress((uint16_t)entry);
    if (e)
        e->isDisabled = disabled != 0;
}

void ModApi_MenuRedraw(int32_t entry)
{
    MenuEntry *e = CalcMenuEntryAddress((uint16_t)entry);
    if (e)
        CallDrawMenuItem(e);
}

