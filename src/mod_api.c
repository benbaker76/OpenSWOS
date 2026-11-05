/*
 * mod_api.c -- what a built-in mod may ask the game (include/mod_api.h; docs/MODS.md §16). The one
 * place a mod's questions meet the game's globals.
 */
#include "mod_api.h"
#include "tweaks.h"      /* ModApi_SetTweak */


#include "display.h"
#include "game_types.h"
#include "mods.h"
#include "player_face3.h"

#include "swos_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef SWOS_SDL_BUILD
#include "platform_sdl.h"
#include <SDL.h>
#include <sys/stat.h>
#define mod_strcasecmp SDL_strcasecmp
#else
/* (the Amiga: no SDL; these are what the SDL calls below were) */
static int mod_strcasecmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb || !ca) return ca - cb;
    }
}
#endif

extern uint16_t  _pitchNumberAndType;
extern uint32_t  _gameTime;
extern int32_t   GameLoop_Active(void);
extern Sprite  **_team1SpritesTable;
extern Sprite  **_team2SpritesTable;
extern TeamGame *_topTeamPtr;
extern TeamGame *_bottomTeamPtr;
extern uint8_t  *_spriteGraphicsPtr;
extern volatile uintptr_t _frontbufferBasePtr;
extern FixedPoint _cameraX, _cameraY;      /* (Render_CameraX/Y on the Amiga: game_types.h) */

/* ---- when (mod_hooks.c tells these) ---- */

static uint32_t s_presents, s_replayPresent = 0xFFFFFF00u;

void ModApi_NotePresent(void) { s_presents++; }
void ModApi_NoteReplayFrame(void) { s_replayPresent = s_presents; }

int32_t ModApi_InMatch(void) { return GameLoop_Active() != 0; }
int32_t ModApi_InReplay(void) { return s_presents - s_replayPresent <= 2; }
#ifdef SWOS_SDL_BUILD
uint32_t ModApi_Ticks(void) { return SDL_GetTicks(); }
#else
/* (the VBlank's count, 50 a second on a PAL Amiga: 20 ms each) */
uint32_t ModApi_Ticks(void) { extern volatile uint16_t _currentTick; return (uint32_t)_currentTick * 20u; }
#endif

/* ---- the match ---- */

int32_t ModApi_PitchType(void)
{
    /* (SWOS_WEATHER_PITCH=<0..6>: that pitch type whatever the match's -- a test's) */
    const char *force = getenv("SWOS_WEATHER_PITCH");
    return force && *force ? atoi(force) : (_pitchNumberAndType & 0xFF);
}

int32_t ModApi_MatchMinute(void)
{
    const uint8_t *t = (const uint8_t *)&_gameTime;     /* its digits: [1] hundreds, [2] tens, [3] ones */
    return t[1] * 100 + t[2] * 10 + t[3];
}

int32_t ModApi_Players(ModPlayer *out, int32_t max)
{
    int32_t n = 0;
    if (!_team1SpritesTable || !_team2SpritesTable)
        return 0;
    for (int t = 0; t < 2; t++) {
        Sprite **tab = t == 0 ? _team1SpritesTable : _team2SpritesTable;
        const TeamGame *team = t == 0 ? _topTeamPtr : _bottomTeamPtr;
        for (int i = 0; i < 11 && n < max; i++) {
            const Sprite *s = tab[i];
            if (!s)
                continue;
            ModPlayer *p = &out[n++];
            p->x = s->x.s.i;
            p->y = s->y.s.i;
            p->z = s->z.s.i;
            p->direction = (int16_t)(s->direction & 7);
            p->team = (uint8_t)t;
            p->index = (uint8_t)i;
            p->onPitch = s->visible && !s->sentAway;
            p->face = team ? (uint8_t)(team->players[i].blond ? 3 : team->players[i].face & 3) : 0;
            p->name = team ? (const char *)team->players[i].fullName : "";
            p->club = team ? (const char *)team->teamName : "";
        }
    }
    return n;
}

int32_t ModApi_ReplayPlayerFeet(uint16_t image, int16_t left, int16_t top, int16_t *x, int16_t *y)
{
    /* the outfield sets: each team's three faces of 101 frames (227..832), and face 3's */
    const int face3 = image >= FACE3_SPRITE_FIRST && image < FACE3_SPRITE_FIRST + 2 * 101;
    if (!(image >= 227 && image < 227 + 6 * 101) && !face3)
        return 0;
    const SpriteGraphics *g = _spriteGraphicsPtr ? ((SpriteGraphics **)_spriteGraphicsPtr)[image] : NULL;
    if (!g)
        return 0;
    *x = (int16_t)(left + g->centerX);
    *y = (int16_t)(top + g->centerY);
    return 1;
}

/* ---- the screen ---- */

void ModApi_Camera(int16_t *x, int16_t *y)
{
    *x = (int16_t)Render_CameraX();
    *y = (int16_t)Render_CameraY();
}

void ModApi_ScreenSize(int32_t *w, int32_t *h)
{
    *w = _screen_width;
    *h = _screen_height;
}

uint8_t *ModApi_Frame(int32_t *stride)
{
    *stride = FRONTBUFFER_ROW_STRIDE_BYTES;
    return (uint8_t *)(uintptr_t)_frontbufferBasePtr;
}

int32_t ModApi_SquadGoalsView(void)
{
    extern uint16_t _isSquadViewMode;
    return _isSquadViewMode != 0;
}

int32_t ModApi_NationalityCode(uint8_t nationality, char *out)
{
    extern uint8_t _shortCountryNames[];
    enum { kCodes = 214 };              /* (swos_data_bulk_imports.c: _shortCountryNames[214 * 3]) */
    if (!out || nationality >= kCodes)
        return -1;
    for (int32_t i = 0; i < 3; i++)
        out[i] = (char)_shortCountryNames[nationality * 3 + i];
    out[3] = 0;
    return 0;
}

/* ---- colours ---- */

#ifdef SWOS_SDL_BUILD
void ModApi_PaletteColour(uint8_t index, uint8_t *r, uint8_t *g, uint8_t *b)
{
    Platform_PaletteColour(index, r, g, b);
}

uint8_t ModApi_NearestColour(uint8_t r, uint8_t g, uint8_t b) { return Platform_NearestColour(r, g, b); }
#endif  /* (the Amiga's: with the first mod that draws there, from its copper list) */

/* ---- the mod's own files ---- */

const char *ModApi_ModDir(const char *slug)
{
    for (int32_t i = 0; i < Mods_Count(); i++)
        if (strcmp(Mods_Id(i), slug) == 0)
            return Mods_Dir(i);
    return NULL;
}

/* ---- settings: SWOS.ini [mod.<slug>] (Settings_Save keeps sections it does not know) ---- */

#ifdef SWOS_SDL_BUILD
enum { kMaxSettings = 128 };
#else
enum { kMaxSettings = 32 };     /* (the Amiga: only what the mods set this run -- no SWOS.ini) */
#endif
typedef struct {
    char slug[MODS_ID_MAX], key[32], value[96];
} Setting;
static Setting s_settings[kMaxSettings];
static int32_t s_settingCount, s_settingsRead;

static void trim(char *t)
{
    size_t n = strlen(t);
    while (n && (t[n - 1] == ' ' || t[n - 1] == '\t' || t[n - 1] == '\r' || t[n - 1] == '\n'))
        t[--n] = '\0';
    size_t i = 0;
    while (t[i] == ' ' || t[i] == '\t')
        i++;
    if (i)
        memmove(t, t + i, n - i + 1);
}

static void read_settings(void)
{
    s_settingsRead = 1;
#ifdef SWOS_SDL_BUILD
    char line[256], section[64] = "";
    FILE *f = fopen(swos_ini_path(), "r");
    if (!f)
        return;
    while (fgets(line, sizeof line, f) && s_settingCount < kMaxSettings) {
        trim(line);
        if (line[0] == '[') {
            char *e = strchr(line, ']');
            if (e) {
                *e = '\0';
                snprintf(section, sizeof section, "%s", line + 1);
            }
            continue;
        }
        char *eq = strchr(line, '=');
        if (!eq || line[0] == ';' || line[0] == '#' || strncmp(section, "mod.", 4) != 0)
            continue;
        *eq = '\0';
        Setting *st = &s_settings[s_settingCount++];
        snprintf(st->slug, sizeof st->slug, "%s", section + 4);
        snprintf(st->key, sizeof st->key, "%s", line);
        snprintf(st->value, sizeof st->value, "%s", eq + 1);
        trim(st->key);
        trim(st->value);
    }
    fclose(f);
#endif  /* (the Amiga has no SWOS.ini: a mod's settings are its defaults, and what it sets this run) */
}

static Setting *find_setting(const char *slug, const char *key)
{
    if (!s_settingsRead)
        read_settings();
    for (int32_t i = 0; i < s_settingCount; i++)
        if (mod_strcasecmp(s_settings[i].slug, slug) == 0 && mod_strcasecmp(s_settings[i].key, key) == 0)
            return &s_settings[i];
    return NULL;
}

const char *ModApi_Setting(const char *slug, const char *key, const char *fallback)
{
    const Setting *st = find_setting(slug, key);
    return st ? st->value : fallback;
}

int32_t ModApi_SettingInt(const char *slug, const char *key, int32_t fallback)
{
    const Setting *st = find_setting(slug, key);
    if (!st || !st->value[0])
        return fallback;
    if (mod_strcasecmp(st->value, "true") == 0 || mod_strcasecmp(st->value, "on") == 0)
        return 1;
    if (mod_strcasecmp(st->value, "false") == 0 || mod_strcasecmp(st->value, "off") == 0)
        return 0;
    return (int32_t)strtol(st->value, NULL, 10);
}

int32_t ModApi_SetTweak(const char *slug, const char *key, int32_t on)
{
    const uint32_t bit = Tweaks_Bit(key);
    return bit ? Tweaks_SetByMod(bit, on, slug) : 1;
}

/* SWOS.ini rewritten with the key's line in the mod's section (added when it has none). */
int32_t ModApi_SetSetting(const char *slug, const char *key, const char *value)
{
    Setting *st = find_setting(slug, key);
    if (!st) {
        if (s_settingCount == kMaxSettings)
            return -1;
        st = &s_settings[s_settingCount++];
        snprintf(st->slug, sizeof st->slug, "%s", slug);
        snprintf(st->key, sizeof st->key, "%s", key);
    }
    snprintf(st->value, sizeof st->value, "%s", value);
#ifndef SWOS_SDL_BUILD
    return 0;       /* (no SWOS.ini on the Amiga: kept for this run) */
#else
    const char *path = swos_ini_path();
    static char lines[512][256];
    int32_t n = 0, sectionAt = -1, keyAt = -1, sectionEnd = -1;
    char section[64] = "", want[80];
    snprintf(want, sizeof want, "mod.%s", slug);
    FILE *f = fopen(path, "r");
    if (f) {
        while (n < 512 && fgets(lines[n], sizeof lines[0], f))
            n++;
        fclose(f);
    }
    for (int32_t i = 0; i < n; i++) {
        char t[256];
        snprintf(t, sizeof t, "%s", lines[i]);
        trim(t);
        if (t[0] == '[') {
            char *e = strchr(t, ']');
            if (e) *e = '\0';
            if (sectionAt >= 0 && sectionEnd < 0)
                sectionEnd = i;
            snprintf(section, sizeof section, "%s", t + 1);
            if (mod_strcasecmp(section, want) == 0)
                sectionAt = i;
            continue;
        }
        char *eq = strchr(t, '=');
        if (eq && sectionAt >= 0 && sectionEnd < 0) {
            *eq = '\0';
            trim(t);
            if (mod_strcasecmp(t, key) == 0)
                keyAt = i;
        }
    }
    FILE *out = fopen(path, "w");
    if (!out)
        return -1;
    for (int32_t i = 0; i < n; i++) {
        if (i == keyAt) {
            fprintf(out, "%s = %s\n", key, value);
            continue;
        }
        if (keyAt < 0 && sectionAt >= 0 && i == (sectionEnd >= 0 ? sectionEnd : n))
            fprintf(out, "%s = %s\n", key, value);
        fputs(lines[i], out);
        if (lines[i][0] && lines[i][strlen(lines[i]) - 1] != '\n')
            fputc('\n', out);
    }
    if (keyAt < 0 && sectionAt >= 0 && sectionEnd < 0)
        fprintf(out, "%s = %s\n", key, value);
    if (sectionAt < 0)
        fprintf(out, "\n[%s]\n%s = %s\n", want, key, value);
    fclose(out);
    swos_save_commit();
    return 0;
#endif
}

/* ---- the mod's save data: <save folder>/mods/<slug>/<name>; on the Amiga save/<slug>_<name>
 *      (resload makes no folders, and FFS names are 30 characters: keep a mod's names short) ---- */

#ifndef SWOS_SDL_BUILD
static int32_t amiga_save_name(const char *slug, const char *name, char *out, uint32_t cap)
{
    if (!slug || !name || !name[0] || strchr(name, '/') || strchr(name, ':') || strstr(name, ".."))
        return -1;
    const int n = snprintf(out, cap, "save/%s_%s", slug, name);     /* (a bare name is the drawer's) */
    return n > 0 && (uint32_t)n < cap && n - 5 <= 30 ? 0 : -1;
}

int32_t ModApi_SaveFile(const char *slug, const char *name, const void *data, uint32_t size)
{
    char path[64];
    if (amiga_save_name(slug, name, path, sizeof path) != 0)
        return -1;
    return swos_io_save((const uint8_t *)path, (const uint8_t *)data, size) == 0 ? 0 : -1;
}

void *ModApi_LoadFile(const char *slug, const char *name, uint32_t *size)
{
    char path[64];
    uint8_t *buf = NULL;
    *size = 0;
    if (amiga_save_name(slug, name, path, sizeof path) != 0 || swos_io_load_alloc((const uint8_t *)path, &buf, size) != 0)
        return NULL;
    return buf;         /* (NUL-terminated, as on SDL; free() it) */
}
#else
extern int32_t Resload_SaveFileAt(const char *path, const uint8_t *src, uint32_t n);

static void make_dir(const char *path)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

static int32_t save_path(const char *slug, const char *name, char *out, size_t cap)
{
    if (!slug || !name || !name[0] || strchr(name, '/') || strchr(name, '\\') || strstr(name, ".."))
        return -1;
    snprintf(out, cap, "%s/mods", swos_save_dir());
    make_dir(out);
    snprintf(out, cap, "%s/mods/%s", swos_save_dir(), slug);
    make_dir(out);
    snprintf(out, cap, "%s/mods/%s/%s", swos_save_dir(), slug, name);
    return 0;
}

int32_t ModApi_SaveFile(const char *slug, const char *name, const void *data, uint32_t size)
{
    char path[1024];
    if (save_path(slug, name, path, sizeof path) != 0)
        return -1;
    return Resload_SaveFileAt(path, (const uint8_t *)data, size);
}

void *ModApi_LoadFile(const char *slug, const char *name, uint32_t *size)
{
    char path[1024];
    *size = 0;
    if (save_path(slug, name, path, sizeof path) != 0)
        return NULL;
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = n >= 0 ? (uint8_t *)malloc((size_t)n + 1) : NULL;
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    buf[n] = 0;                             /* (a text file reads as a string) */
    *size = (uint32_t)n;
    return buf;
}
#endif
