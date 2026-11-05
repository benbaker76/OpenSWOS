/*
 * mod_api.c -- what a built-in mod may ask the game (include/mod_api.h; docs/MODS.md §16). The one
 * place a mod's questions meet the game's globals.
 */
#include "mod_api.h"

#ifdef SWOS_SDL_BUILD

#include "display.h"
#include "game_types.h"
#include "mods.h"
#include "platform_sdl.h"
#include "player_face3.h"

#include <SDL.h>
#include <string.h>

extern uint16_t  _pitchNumberAndType;
extern uint32_t  _gameTime;
extern int32_t   GameLoop_Active(void);
extern Sprite  **_team1SpritesTable;
extern Sprite  **_team2SpritesTable;
extern TeamGame *_topTeamPtr;
extern TeamGame *_bottomTeamPtr;
extern uint8_t  *_spriteGraphicsPtr;
extern volatile uintptr_t _frontbufferBasePtr;

/* ---- when (mod_hooks.c tells these) ---- */

static uint32_t s_presents, s_replayPresent = 0xFFFFFF00u;

void ModApi_NotePresent(void) { s_presents++; }
void ModApi_NoteReplayFrame(void) { s_replayPresent = s_presents; }

int32_t ModApi_InMatch(void) { return GameLoop_Active() != 0; }
int32_t ModApi_InReplay(void) { return s_presents - s_replayPresent <= 2; }
uint32_t ModApi_Ticks(void) { return SDL_GetTicks(); }

/* ---- the match ---- */

int32_t ModApi_PitchType(void)
{
    /* (SWOS_WEATHER_PITCH=<0..6>: that pitch type whatever the match's -- a test's) */
    const char *force = SDL_getenv("SWOS_WEATHER_PITCH");
    return force && *force ? SDL_atoi(force) : (_pitchNumberAndType & 0xFF);
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

/* ---- colours ---- */

void ModApi_PaletteColour(uint8_t index, uint8_t *r, uint8_t *g, uint8_t *b)
{
    Platform_PaletteColour(index, r, g, b);
}

uint8_t ModApi_NearestColour(uint8_t r, uint8_t g, uint8_t b) { return Platform_NearestColour(r, g, b); }

/* ---- the mod's own files ---- */

const char *ModApi_ModDir(const char *slug)
{
    for (int32_t i = 0; i < Mods_Count(); i++)
        if (strcmp(Mods_Id(i), slug) == 0)
            return Mods_Dir(i);
    return NULL;
}

#endif /* SWOS_SDL_BUILD */
