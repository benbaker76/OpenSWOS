/*
 * player_looks.h -- the players' looks of the SWOS 96/97 Mod: receding hairline, shaved head and
 * white hair (src/player_looks.c; the Mod's tools/blond.py, bald.c, shavedart.py; docs/WORLD_2026_27.md
 * §6l). Drawing only: nothing here is match state.
 *
 * A look is in the player record's byte 25 (PlayerFile.playerName[22], the name's terminator when it
 * has the full 22 letters, so a look needs a shorter name), as the Mod keeps it in its team files and
 * CUSTOMS.EDT: bit 0 bald (alone: receding), bit 1 shaved (with bit 0), bit 2 white hair (bit 0
 * wins). Bits 3..7 are kept as they are.
 */
#ifndef PLAYER_LOOKS_H
#define PLAYER_LOOKS_H

#include "swos_types.h"
#include "player_face3.h"

struct PlayerFile;
struct PlayerInfo;
struct TeamGame;
struct Sprite;
struct SpriteGraphics;

enum { LOOK_NONE = 0, LOOK_RECEDING = 1, LOOK_SHAVED = 2, LOOK_WHITE = 3, LOOK_COUNT = 4 };

#define LOOKS_BIT_BALD    1
#define LOOKS_BIT_SHAVED  2
#define LOOKS_BIT_WHITE   4
#define LOOKS_BITS        (LOOKS_BIT_BALD | LOOKS_BIT_SHAVED | LOOKS_BIT_WHITE)

/* A player record's look (LOOK_*): its byte 25, else the Mod's own looks of the 1996-97 game
 * (Ravanelli's white hair), which the original team files cannot hold. */
int32_t PlayerLooks_Of(const struct PlayerFile *pf);
/* The look written into the record (LOOK_NONE clears it); 0 when the name is 22 letters long. */
int32_t PlayerLooks_Set(struct PlayerFile *pf, int32_t look);
/* Can the record hold a look (a name shorter than 22 letters)? */
int32_t PlayerLooks_Room(const struct PlayerFile *pf);
/* Byte 25 as the Mod writes it, for a look (LOOK_*). */
uint8_t PlayerLooks_Bits(int32_t look);
int32_t PlayerLooks_FromBits(uint8_t bits);
const char *PlayerLooks_Name(int32_t look);   /* "receding", "shaved", "white", "" */

/* The face icon of the menus for a face (0..3) and a look: the look's own when it has one. */
int16_t PlayerLooks_MenuIcon(uint16_t face, int32_t look);
int16_t PlayerLooks_PlayerMenuIcon(const struct PlayerFile *pf);

/* ---- a match ---- */

/* InitInGamePlayer: the look of a player of _topTeamInGame / _bottomTeamInGame (by his file index). */
void PlayerLooks_NotePlayer(const struct PlayerInfo *pi, const struct PlayerFile *pf);
/* The look of a player of a match's team. */
int32_t PlayerLooks_InMatch(const struct TeamGame *team, const struct PlayerInfo *pi);

/* ConvertColorsAndTextSprites: the teams' raw first sets copied before they are recoloured; after a
 * team's sets are, its look sets made from the copy with its colour table (team 0 / 1). */
void PlayerLooks_CopySets(void);
void PlayerLooks_ConvertSets(int32_t team, uint16_t *colorTable);

/* The picture a player sprite is drawn with: its look's frame, else the one it has. Its image
 * index the same way (what a replay records). The sprite itself is never changed. */
const struct SpriteGraphics *PlayerLooks_Graphics(const struct Sprite *s, const struct SpriteGraphics *g);
int16_t PlayerLooks_Image(const struct Sprite *s, int16_t image);

/* The line-up screen's little players (StadiumPlayerSpritesBeforeDraw): the look's colours in the
 * table, and its sprite (receding: a copy without the top of the hair). */
uint16_t PlayerLooks_LittlePlayer(int32_t look, uint16_t srcSprite, uint16_t *colTable);

/* ---- the sprite table's slots (SDL: a match's looks; the Amiga draws none, docs §6l) ---- */
#define LOOKS_VARIANTS        6                   /* (look - 1) * 2 + dark */
#define LOOKS_SPRITE_FIRST    FACE3_SPRITES_END
#define LOOKS_MENU_ICON_FIRST (LOOKS_SPRITE_FIRST + 2 * LOOKS_VARIANTS * 101)
#define LOOKS_LITTLE_FIRST    (LOOKS_MENU_ICON_FIRST + LOOKS_VARIANTS)
#define LOOKS_LITTLE_SLOTS    8
#define LOOKS_SPRITES_END     (LOOKS_LITTLE_FIRST + LOOKS_LITTLE_SLOTS)

#ifdef SWOS_SDL_BUILD
#define LOOKS_ICON_FIRST      LOOKS_MENU_ICON_FIRST
#define SPRITE_TABLE_SLOTS    LOOKS_SPRITES_END
#else                                             /* (the Amiga: the six face icons only) */
#define LOOKS_ICON_FIRST      FACE3_SPRITES_END
#define SPRITE_TABLE_SLOTS    (FACE3_SPRITES_END + LOOKS_VARIANTS)
#endif

/* ---- Marco's shaved heads (src/player_looks_art.c, scripts/gen_shaved_art.py) ---- */
#define LOOKS_ART_HEADS  83
#define LOOKS_ART_FRAMES 101
typedef struct {
    int8_t ry, rx, h, w;
    int8_t cells[8][12];                    /* raw colour, -1 keep; 13 the skull tone */
} LooksArtHead;
extern const LooksArtHead g_looksArtLight[LOOKS_ART_HEADS], g_looksArtDark[LOOKS_ART_HEADS];
extern const uint8_t g_looksArtFrames[LOOKS_ART_FRAMES][3];
extern const uint8_t g_looksArtIcons[2][8][16];

#endif
