/*
 * player_face3.c -- black players with blond hair: face 3 (include/player_face3.h; the SWOS 96/97
 * Mod's blond.py; docs/WORLD_2026_27.md, phase 7).
 *
 * A player's face is bits 3-4 of positionAndFace, 0..2 (white, ginger, black); 3 was unused. The
 * game draws each team's outfield players from three sets of 101 sprites (team 1: 227, 328, 429;
 * team 2: 530, 631, 732), the same raw sprites recoloured per match with the kit colours and
 * skinAndHairColorConversionTable[face] (ConvertColorsAndTextSprites), each player's frames
 * offset by face * 101 (InitializePlayerSpriteFrameIndices). Here, as the Mod does:
 *   - the skin table's fourth entry, face 3: black skin, the hair yellow (15) and orange (6);
 *   - a face 3 player is a black player in a match (PlayerInfo.face 2: the bench, the face
 *     counts, the goalkeepers' faces, the menus), marked blond (PlayerInfo.blond);
 *   - each team's first raw set is copied before it is recoloured, then recoloured with face 3,
 *     a fourth set (sprite slots after the shirt numbers'); a marked outfield player's frames
 *     offset to it. A goalkeeper marked blond is drawn black (the keepers' sets follow the
 *     team's goalie faces);
 *   - the menus' face icon of face 3 is the black one with its hair yellow (made when needed).
 * Both targets: SDL's sprites are chunky; the Amiga's planar in chip RAM (the copies' masks are
 * the originals', the shapes being the same).
 */
#include "player_face3.h"
#include "game_types.h"
#include "display.h"
#include "swos_sections.h"
#include "shirt_numbers.h"

extern uint8_t *_spriteGraphicsPtr;
extern void ConvertSpriteColors(uint16_t sprIdx, uint16_t *colorTable);
extern void ConvertColorTableToSkinColor(uint16_t color, uint16_t *destTable);

#define SET 101
static const int16_t kSetBase[2] = { 227, 530 };   /* each team's first (raw, face 0) set */

/* The pixel pool of the two copies: chunky bytes (SDL) or planar words (the Amiga's chip RAM). */
#ifdef SWOS_SDL_BUILD
#define POOL_BYTES (64 * 1024)          /* (a set: 25.6 KB in the Amiga art) */
static uint8_t s_pool[2][POOL_BYTES];
#else
#define POOL_BYTES (20 * 1024)          /* (a set: ~16.6 KB planar, its running frames 8 planes) */
SWOS_CHIP_BSS static uint8_t s_pool[2][POOL_BYTES];
#endif
static SpriteGraphics s_hdr[2][SET];
static uint8_t s_ready[2];

static SpriteGraphics **table(void) { return (SpriteGraphics **)_spriteGraphicsPtr; }

static uint32_t data_bytes(const SpriteGraphics *s)
{
#ifdef SWOS_SDL_BUILD
    return (uint32_t)s->nlines * (uint32_t)s->wquads * 16u;
#else
    return (uint32_t)s->nlines * (uint32_t)s->wquads * 2u * spriteNumPlanes(s->ordinal);
#endif
}

void Face3_CopySets(void)
{
    for (int32_t t = 0; t < 2; t++) {
        uint32_t used = 0;
        s_ready[t] = 0;
        for (int32_t i = 0; i < SET; i++) {
            const SpriteGraphics *src = table()[kSetBase[t] + i];
            if (!src || !src->sprData)
                return;
            const uint32_t n = data_bytes(src);
            if (used + n > POOL_BYTES) {
                SWOS_DBG("[face3] team %d's set needs more than %u bytes: face 3 drawn black\n", (int)t,
                         (unsigned)POOL_BYTES);
                break;
            }
            SpriteGraphics *h = &s_hdr[t][i];
            *h = *src;
#ifdef SWOS_SDL_BUILD
            h->ordinal = (int16_t)(FACE3_SPRITE_FIRST + t * SET + i);
#endif                                          /* (the Amiga's depth is the ordinal's: a running frame is
                                                 *  padded to 8 planes, spriteNumPlanes -- kept the source's) */
            h->sprData = s_pool[t] + used;
            for (uint32_t k = 0; k < n; k++)
                h->sprData[k] = src->sprData[k];
            used += (n + 3) & ~3u;
            if (i == SET - 1)
                s_ready[t] = 1;
        }
        SWOS_DBG("[face3] team %d's copy: %u bytes of %u\n", (int)t, (unsigned)used, (unsigned)POOL_BYTES);
    }
    for (int32_t t = 0; t < 2; t++)
        for (int32_t i = 0; i < SET && s_ready[t]; i++)
            table()[FACE3_SPRITE_FIRST + t * SET + i] = &s_hdr[t][i];
}

int16_t Face3_Set(int32_t team)
{
    return team >= 0 && team <= 1 && s_ready[team] ? (int16_t)(FACE3_SPRITE_FIRST + team * SET) : -1;
}

int16_t Face3_FrameOffset(int32_t team)
{
    if (team < 0 || team > 1 || !s_ready[team])
        return (int16_t)(2 * SET);              /* (no copy: drawn black) */
    return (int16_t)(FACE3_SPRITE_FIRST + team * SET - kSetBase[team]);
}

/* ---- the menus' face icon ---- */
int16_t Face_MenuIcon(uint16_t face)
{
    return face == 3 ? Face3_MenuIcon() : (int16_t)(186 + (face & 3));   /* SPR_PL_FACE_WHITE_GUY + face */
}

#define MENU_FACE_BLACK 188                     /* SPR_PL_FACE_WHITE_GUY + 2 */
static SpriteGraphics s_icon;
#ifdef SWOS_SDL_BUILD
static uint8_t s_iconPix[16 * 16];
#else
SWOS_CHIP_BSS static uint16_t s_iconPix[16 * 8];
#endif
static const SpriteGraphics *s_iconSource;

int16_t Face3_MenuIcon(void)
{
    const SpriteGraphics *src = table()[MENU_FACE_BLACK];
    if (!src || !src->sprData || src->wquads != 1 || src->nlines > 16)
        return MENU_FACE_BLACK;
    if (s_iconSource != src) {
        /* the black face, its hair (colour 3 in the top three rows) yellow (15) */
        const uint32_t n = data_bytes(src);
#ifdef SWOS_SDL_BUILD
        if (n > sizeof s_iconPix)
            return MENU_FACE_BLACK;
        for (uint32_t k = 0; k < n; k++)
            s_iconPix[k] = src->sprData[k];
        for (int32_t y = 0; y < 3 && y < src->nlines; y++)
            for (int32_t x = 0; x < 16; x++)
                if (s_iconPix[y * 16 + x] == 3)
                    s_iconPix[y * 16 + x] = 15;
#else
        {
            const uint16_t sp = spriteNumPlanes(src->ordinal);
            const uint16_t *w = (const uint16_t *)src->sprData;
            if (n > sizeof s_iconPix || sp < 4)
                return MENU_FACE_BLACK;
            for (uint32_t k = 0; k < n / 2; k++)
                s_iconPix[k] = w[k];
            for (int32_t y = 0; y < 3 && y < src->nlines; y++) {
                uint16_t *row = s_iconPix + y * sp, three = (uint16_t)0xFFFF;
                for (uint16_t p = 0; p < sp; p++)       /* (the pixels of colour 3: planes 0, 1 set, rest clear) */
                    three &= (p < 2) ? row[p] : (uint16_t)~row[p];
                for (uint16_t p = 0; p < 4; p++)        /* (to 15: planes 0..3 set) */
                    row[p] |= three;
            }
        }
#endif
        s_icon = *src;
        s_icon.sprData = (uint8_t *)s_iconPix;
        s_icon.ordinal = FACE3_MENU_ICON;
        table()[FACE3_MENU_ICON] = &s_icon;
        s_iconSource = src;
    }
    return FACE3_MENU_ICON;
}
