/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "adverts.h"
#include "game_types.h"
#include "swos_types.h"
#ifdef SWOS_SDL_BUILD
#include "display.h"   /* WIDE_MARGIN — the second corner flag is widescreen-only */
#ifdef SWOS_SDL_BUILD
#include <stdlib.h>      /* getenv: SWOS_TEST_ADVERTS */
#endif
#endif

/* Sprite image indices */
#define SPR_ADVERT_1_PART_1     113
#define SPR_ADVERT_2_PART_1     121
#define SPR_ADVERT_3_PART_1     129
#define SPR_CORNER_FLAG_FRAME_0 1070
#define BIG_S_FRAME_00          1127

/* Game state constants */
#define ST_KEEPER_HOLDS_BALL        3
#define ST_RESULT_AFTER_THE_GAME    26
#define ST_GAME_IN_PROGRESS         100

/* ---- External globals (BSS / data section) ---- */
extern uint16_t _trainingGame;
extern uint16_t _gameState;
extern FixedPoint _cameraX;
extern FixedPoint _cameraY;
extern uint16_t _showingUserStats;
extern uint16_t _statsTimer;
extern uint16_t _inSubstitutesMenu;
extern uint16_t _subsState;
extern uint16_t _currentPlayerIndicatorPosition;
extern uint8_t *_spriteGraphicsPtr;
extern uint16_t _frameCount;
extern uint16_t _lastFrameTicks;

/* ---- Data labels in SWOS2.s code section ---- */
extern uint16_t _advertIndex;
extern uint16_t _advertSlideTimer;
extern uint16_t _currentAnimatedAdLine;
extern uint8_t *_advert1Pointers[8];
extern uint8_t *_advert2Pointers[8];
extern uint8_t *_advert3Pointers[8];
extern uint16_t _blockAdvertisements;
extern uint16_t _gameStatePl;
extern volatile uint16_t _goalCameraMode;
extern uint16_t _foulXCoordinate;
extern uint16_t _spinBigS;
extern uint16_t _bigSIndex;

/* Sprite arrays in data section */
extern Sprite   _advertInGameSprites[8]; /* 8 * SPRITE_SIZE bytes */
extern Sprite   _cornerFlagSprite;       /* SPRITE_SIZE bytes */
#ifdef SWOS_SDL_BUILD
extern Sprite   _cornerFlagSprite2;      /* widescreen 2nd corner flag — sprite_data.c */
#endif
extern Sprite   _big_S_Sprite;           /* SPRITE_SIZE bytes */
extern uint8_t  _cornerFlagPictureIndices[32];

/* ---- External C/asm functions ---- */
extern void     InitDisplaySprites(void);
extern uint16_t Rand(void);

/* ================================================================
 * CopyAdvertisementsData  (SWOS2.s line 11822)
 *
 * Copies sprite-data pointers for the 3 advertisement strips out of
 * the sprite-graphics table. Amiga slot indices: 113 (0x71), 121
 * (0x79), 129 (0x81); 8 sprites per advert.
 *
 * Originally written with byte offsets (0x1C4 / 0x1E4 / 0x204 =
 * idx * 4) into a uint32_t* slot table. On 64-bit the slots are 8
 * bytes wide and the byte offsets land mid-pointer, so use typed
 * SpriteGraphics** indexing instead. Each advertN slot holds the
 * sprite's `sprData` pointer, used later by _UpdateAdvertisements
 * to scroll the ad strip. */
__attribute__((used, externally_visible))
void CopyAdvertisementsData(void)
{
    SpriteGraphics **table = (SpriteGraphics **)_spriteGraphicsPtr;
    int i;

    for (i = 0; i < 8; i++)
        _advert1Pointers[i] = table[113 + i]->sprData;
    for (i = 0; i < 8; i++)
        _advert2Pointers[i] = table[121 + i]->sprData;
    for (i = 0; i < 8; i++)
        _advert3Pointers[i] = table[129 + i]->sprData;
}

#ifdef SWOS_SDL_BUILD
/* ================================================================
 * Adverts_ApplyStripLine -- a replay's ad tile, as the match drew it
 *
 * The animated ad behind the goal is not a sprite position but a change to the SPRITE
 * GRAPHICS: each frame the update below cuts the current ad's 8 tiles to a 6-line window
 * (nlines) scrolled down by _currentAnimatedAdLine (sprData). A replay has only the
 * image, so it drew each tile at whatever size and scroll its graphics were left in --
 * the whole ad unrolled over the goal (the .SRP player, 2026-09-29). The recorder keeps
 * the line (replay_v2.c, the frame head's word 18) and playback puts the window back
 * with this, before drawing the tile. Returns 1 if `image` is an ad tile.
 * ================================================================ */
int32_t Adverts_ApplyStripLine(uint16_t image, uint16_t line)
{
    SpriteGraphics **table = (SpriteGraphics **)_spriteGraphicsPtr;
    uint8_t **bases;
    SpriteGraphics *gfx;
    uint16_t tile;

    if (image >= SPR_ADVERT_1_PART_1 && image < SPR_ADVERT_1_PART_1 + 8) {
        bases = _advert1Pointers; tile = (uint16_t)(image - SPR_ADVERT_1_PART_1);
    } else if (image >= SPR_ADVERT_2_PART_1 && image < SPR_ADVERT_2_PART_1 + 8) {
        bases = _advert2Pointers; tile = (uint16_t)(image - SPR_ADVERT_2_PART_1);
    } else if (image >= SPR_ADVERT_3_PART_1 && image < SPR_ADVERT_3_PART_1 + 8) {
        bases = _advert3Pointers; tile = (uint16_t)(image - SPR_ADVERT_3_PART_1);
    } else {
        return 0;
    }
    if (!bases[tile])
        CopyAdvertisementsData();       /* the bases, captured once the sprites are in */
    gfx = table[image];
    gfx->nlines = 6;
    gfx->sprData = bases[tile] + (uint32_t)line * ((uint32_t)gfx->wquads * 16);   /* chunky, as above */
    return 1;
}
#endif

/* ================================================================
 * InitAdvertisements  (SWOS2.s line 11849)
 *
 * Clears ad state and hides all advert sprites.
 * ================================================================ */
__attribute__((used, externally_visible))
void InitAdvertisements(void)
{
    _advertIndex = 0;
    _advertSlideTimer = 0;
    _currentAnimatedAdLine = 0;
    SetAdvertisementsInvisible();
}

/* ================================================================
 * SetAdvertisementsVisible  (SWOS2.s line 11858)
 *
 * Sets visible=1 on all 8 advert in-game sprites, then calls
 * InitDisplaySprites to register them.
 * ================================================================ */
__attribute__((used, externally_visible))
void SetAdvertisementsVisible(void)
{
    int i;
    for (i = 0; i < 8; i++) {
        _advertInGameSprites[i].visible = 1;
    }
    InitDisplaySprites();
}

/* ================================================================
 * SetAdvertisementsInvisible  (SWOS2.s line 11870)
 *
 * Clears visible on all 8 advert in-game sprites, then calls
 * InitDisplaySprites.
 * ================================================================ */
__attribute__((used, externally_visible))
void SetAdvertisementsInvisible(void)
{
    int i;
    for (i = 0; i < 8; i++) {
        _advertInGameSprites[i].visible = 0;
    }
    InitDisplaySprites();
}

/* ================================================================
 * ManageAdvertisements  (SWOS2.s line 11882)
 *
 * State machine for animated pitch-side advertisements during
 * gameplay. Picks a random advert strip, animates it line by line,
 * pauses, then tears it down.
 * ================================================================ */
__attribute__((used, externally_visible))
void ManageAdvertisements(void)
{
    uint16_t d0, d1, d2;
    int16_t d7;
    uint8_t **a6;   /* advert sprite-data pointers */
    Sprite *spr;

    /* Bail out conditions */
    if (_trainingGame != 0)
        return;
    if (_gameStatePl == ST_GAME_IN_PROGRESS)
        goto reset_ads_for_gameplay_state;
    if (_cameraY.s.i > 84)
        goto reset_ads_for_gameplay_state;
    if (_blockAdvertisements != 0)
        return;
    if (_goalCameraMode != 0)
        return;
    if ((int16_t)_advertIndex < 0)
        return;
    if (_gameState == ST_KEEPER_HOLDS_BALL)
        return;
    if (_gameState == ST_RESULT_AFTER_THE_GAME)
        return;

    /* If advertIndex == 0, try to start a new ad cycle */
    if (_advertIndex == 0) {
        d0 = Rand();
#ifdef SWOS_SDL_BUILD
        /* SWOS_TEST_ADVERTS=1: every chance shows one (a test of their picture: the pitchmaps
         * mod's check, docs/PITCH_MAPS.md); the same Rand() is drawn either way */
        { static int8_t force = -1;
          if (force < 0) force = getenv("SWOS_TEST_ADVERTS") != NULL;
          if (force) d0 = 0; }
#endif
        if (d0 > 128)
            goto stop_ad_cycle;

        /* If advert sprites not yet visible, make them visible */
        if ((uint16_t)_advertInGameSprites[0].visible == 0)
            SetAdvertisementsVisible();

        /* Pick which advert strip to show based on foulXCoordinate */
        d0 = Rand();
        d0 = (d0 & 1) + 1;   /* d0 = 1..2 */
        if (_foulXCoordinate >= 326) {
            d0 += 1;           /* d0 = 2..3 */
            if (_foulXCoordinate <= 346) {
                d0 = Rand();
                d0 = (uint16_t)((uint16_t)(d0 * 3) >> 8) + 1;  /* d0 = 1..3 */
            }
        }
        _advertIndex = d0;
    }

    /* Route to selected ad setup */
    if (_advertIndex == 2) {
        d7 = SPR_ADVERT_2_PART_1;
        a6 = _advert2Pointers;
        d1 = 0x111;
    } else if (_advertIndex == 3) {
        d7 = SPR_ADVERT_3_PART_1;
        a6 = _advert3Pointers;
        d1 = 0x1B0;
    } else {
        /* advertIndex == 1 (default) */
        d7 = SPR_ADVERT_1_PART_1;
        a6 = _advert1Pointers;
        d1 = 112;
    }

    /* Check slide timer or advance animation */
    if (_advertSlideTimer != 0) {
        _advertSlideTimer -= 1;
    } else {
        /* Only advance on every 8th frame */
        d0 = _frameCount & 7;
        if (d0 == 0) {
            _currentAnimatedAdLine += 1;
            if (_currentAnimatedAdLine == 6) {
                _advertSlideTimer = 0x28;    /* pause for 40 frames */
            } else if (_currentAnimatedAdLine == 0x0D) {
                goto stop_ad_cycle;
            }
        }
    }

    /* Build ad strip: update 8 sprite tiles.
     *
     * The Amiga code reached the per-sprite header via
     * `((uint32_t *)spriteGraphicsPtr) + (d7 << 2)` byte-offsetting
     * into a uint32_t* slot table. On 64-bit each slot is 8 bytes, so
     * the byte arithmetic lands mid-pointer; index the table as
     * SpriteGraphics** at slot d7 instead, which is pointer-width
     * correct on both targets. */
    {
        SpriteGraphics **sprGfxTable = (SpriteGraphics **)_spriteGraphicsPtr + (uint16_t)d7;
        int16_t imgIdx = d7;
        for (d2 = 0; d2 < 8; d2++) {
            SpriteGraphics *gfx = sprGfxTable[d2];
            spr = &_advertInGameSprites[d2];
            spr->imageIndex = (int16_t)imgIdx;
            spr->x.s.i = (int16_t)d1;
            spr->y.s.i = 0x5A;
            spr->z.s.i = 0;
            /* Set nlines to 6 */
            gfx->nlines = 6;
            /* Compute data offset: scroll the source image down by one
             * scanline per `currentAnimatedAdLine` step. The byte stride
             * per scanline differs by format:
             *   Amiga bitplane: wquads * 8  (4 planes * 2 bytes/word)
             *   SDL    chunky:  wquads * 16 (1 byte/pixel * 16 px/wquad)
             * Using the Amiga formula on chunky data shifts only half a
             * scanline per increment, producing visible diagonal motion
             * (8 px right + half a row down) instead of vertical scroll. */
            {
                uint16_t wquads = (uint16_t)gfx->wquads;
#ifdef SWOS_SDL_BUILD
                uint32_t lineBytes = (uint32_t)wquads * 16;
#else
                uint32_t lineBytes = (uint32_t)wquads * 8;
#endif
                /* a6[d2] is the per-sprite pixel-data base captured at
                 * load time by CopyAdvertisementsData. Now stored as a
                 * real uint8_t* (advertNPointers retyped to uint8_t*[8]),
                 * so the scroll offset just adds onto it directly. */
                uint32_t scrollOffset = (uint32_t)_currentAnimatedAdLine * lineBytes;
                gfx->sprData = a6[d2] + scrollOffset;
            }
            d1 += 0x10;
            imgIdx += 1;
        }
    }
    return;

stop_ad_cycle:
    _advertIndex = 0xFFFF;
    goto teardown_ads_and_reset_counters;

reset_ads_for_gameplay_state:
    _advertIndex = 0;
    /* fall through */

teardown_ads_and_reset_counters:
    if ((uint16_t)_advertInGameSprites[0].visible != 0)
        SetAdvertisementsInvisible();
    _advertSlideTimer = 0;
    _currentAnimatedAdLine = 0;
}

/* ================================================================
 * UpdateCornerFlagSprite  (SWOS2.s line 12012)
 *
 * Updates the corner flag sprite position based on camera and
 * animates it using a frame-based lookup table.
 * ================================================================ */
__attribute__((used, externally_visible))
void UpdateCornerFlagSprite(void)
{
    int16_t d0;
    Sprite *spr = &_cornerFlagSprite;

    /* X position: if cameraX.s.i + 168 >= 336, hide far right; else show at x=81.
     * A world sprite in the draw list, so measured from the simulation draw view
     * (display.h) -- its position also orders the draw list, which is match state. */
    d0 = Sim_DrawCameraX() + 168;
    if ((uint16_t)d0 >= 336)
        spr->x.s.i = 590;
    else
        spr->x.s.i = 81;

    /* Y position: if cameraY.s.i + 136 >= 449, hide far bottom; else show at y=129 */
    d0 = _cameraY.s.i + 136;
    if ((uint16_t)d0 >= 449)
        spr->y.s.i = 769;
    else
        spr->y.s.i = 129;

    /* Animation: 32-entry lookup table indexed by (frameCount >> 1) & 0x1F */
    d0 = (_frameCount >> 1) & 0x1F;
    d0 = (uint16_t)_cornerFlagPictureIndices[d0] + SPR_CORNER_FLAG_FRAME_0;
    spr->imageIndex = (int16_t)d0;

#ifdef SWOS_SDL_BUILD
    /* Widescreen shows the full pitch width, so both side corners on the camera's current
     * row are visible — but the single camera-tracked flag above can only animate one of
     * them. Drive a second flag at the opposite-side corner (same row, same frame) so it
     * waves too. In 4:3 / Amiga (WIDE_MARGIN == 0) the far corner is off-screen anyway, so
     * we just hide it (imageIndex = -1) — this is purely a widescreen extra. */
    {
        Sprite *spr2 = &_cornerFlagSprite2;
        if (_sim_draw_width > SCREEN_WIDTH) {
            spr2->x.s.i      = (spr->x.s.i == 81) ? 590 : 81;  /* opposite side */
            spr2->y.s.i      = spr->y.s.i;                     /* same row as the primary */
            spr2->imageIndex = spr->imageIndex;                /* in sync with the primary */
        } else {
            spr2->imageIndex = -1;                             /* hidden */
        }
    }
#endif
}

/* ================================================================
 * SpinBig_S_Sprite  (SWOS2.s line 12046)
 *
 * Updates the spinning "S" indicator sprite in the upper-right
 * corner during gameplay.
 * ================================================================ */
__attribute__((used, externally_visible))
void SpinBig_S_Sprite(void)
{
    Sprite *spr = &_big_S_Sprite;
    int16_t d0;

    /* Default: set imageIndex to -1 (invisible) */
    spr->imageIndex = -1;

    if (_spinBigS == 0)
        return;

    /* Advance spin index unless in substitutes menu or showing stats */
    if (_inSubstitutesMenu != 0) {
        if (_subsState != 0)
            goto skip_advance;
    }
    if (_showingUserStats != 0)
        goto skip_advance;
    if (_statsTimer != 0)
        goto skip_advance;

    /* Advance by lastFrameTicks, wrap at 64 */
    _bigSIndex = (_bigSIndex + _lastFrameTicks) & 0x3F;

skip_advance:
    d0 = _bigSIndex >> 1;
    d0 += BIG_S_FRAME_00;
    spr->imageIndex = (int16_t)d0;

    /* Position relative to camera */
    spr->x.s.i = Render_CameraX() + 0x12D;
    spr->y.s.i = Render_CameraY() + 0x271E;

    if (_currentPlayerIndicatorPosition == 3)
        spr->y.s.i += 0x0A;

    spr->z.s.i = 10000;
}
