/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "shirt_numbers.h"
#include "color_sprites.h"
#include "team_colors.h"   /* SHIRT_* kit-pattern constants */
#include "game_types.h"
#include "dos_mode.h"
#ifdef SWOS_SDL_BUILD
#include "dos_graphics.h"   /* DosGfx_DecodePlanarSprite, DOS_TO_AMIGA_OFFSET */
#include <stdio.h>          /* snprintf */
#include <stdlib.h>         /* malloc / calloc */
#include <string.h>         /* memcpy */
#endif

/* ---- Sprite indices ---- */
#define SPR_BIG_DASH            195
#define SPR_BIG_0               173
/* The animated replay "R" indicator (32 frames). It lives inside the Amiga
 * bench.dat index range (1065..1219), but the DOS bench.dat has different content
 * there and the DOS score/HUD sprites are deliberately omitted — so letting the DOS
 * bench overwrite these slots draws garbage (kit-coloured) frames during a replay.
 * Keep the Amiga replay-R sprites in DOS-graphics mode. */
#define SPR_REPLAY_FRAME_00     1095
#define SPR_REPLAY_FRAME_LAST   1126
#define SPR_RESULT_BIG_0        183
#define SPR_TEAM1_NAME          193
#define SPR_TEAM2_NAME          194

#include "jag_graphics.h"

/* ---- External functions (already ported) ---- */
extern void ProcessImageCommandStream(const uint16_t *cmdBuffer, uint8_t *workBuffer);
extern void CopyAdvertisementsData(void);
extern void MakeSpritesFromPlayerNames(TeamGame *leftTeam, TeamGame *rightTeam);
extern void HWSprite_LoadFromSpriteGraphics(uint16_t sprIdx);
extern uint16_t Text2Sprite(uint16_t x, uint16_t y, uint16_t sprIdx,
                             uint8_t *text, uint8_t *charTable,
                             uint16_t planeMask, uint16_t *outTotalWidth);
extern void ConvertSpriteColors(uint16_t sprIdx, uint16_t *colorTable);
extern void CountFaces(TeamGame *team);
extern void ConvertColorTableToSkinColor(uint16_t color, uint16_t *destTable);
extern void DetermineGoalieFaces(TeamGame *team, uint8_t *gkFaceToIndex, uint8_t *gkFaceIndices);

/* ---- External globals ---- */
extern void *_teamFilenamesTable[];
extern uintptr_t _team1Filename;
extern uintptr_t _team2Filename;
extern uint16_t _menuColorAndTextScript[];
extern uint8_t *_menuImageStreamBaseEnd;
extern uint8_t _bigCharsTable[];
extern uint16_t _team1NameLength;
extern uint16_t _team2NameLength;
extern uint16_t _team1PlayersColorTable[];
extern uint16_t _team2PlayersColorTable[];
extern uint16_t _colorTableReserves[];
extern int16_t _bigNumbersColorConversionTable[];
extern uint16_t _useIndividualPlayerSkinColor;
extern uint16_t _team1MostFrequentFace;
extern uint16_t _team2MostFrequentFace;
extern uint8_t _facesFrequency[];
extern void *_skinAndHairColorConversionTable[];
extern GoalieFaceState _goalieFaces;     /* GK face/skin state (was 4+2 adjacency-aliased globals) */

#ifdef SWOS_SDL_BUILD
/* ================================================================
 * DOS player/keeper/bench sprite overlay (PC-only, gDosGraphics)
 *
 * Loads the DOS game-sprite `.dat` files from bin/grafs_dos and stores
 * them in our sprite-graphics table at `amigaIndex = dosIndex - 114`
 * (DOS_TO_AMIGA_OFFSET), so the unchanged renderer draws DOS players.
 * Each `.dat` is a run of `[24-byte header][4-bitplane planar]` records;
 * we decode the planar pixels to 8bpp chunky (DosGfx_DecodePlanarSprite)
 * and fill a SpriteGraphics the same way ProcessImageCommandStream does
 * for the Amiga path (type=0, bltsize=h*64+wq*2, maskPtr=0).
 *
 * Scope: players/keepers/bench only (DOS team1/team3/goal1×2/bench →
 * amiga 227..1219). The score/number/team-name/player-name sprites
 * (amiga 113..226, partly runtime-generated) and the charset/fonts stay
 * Amiga.
 *
 * Kit colours: the DOS player `.dat` sprites are 4-bitplane = 16-colour
 * (pixel values 0..15) with the kit at the SAME indices the Amiga uses
 * (10=stripes, 11=shirt, 14=shorts, 15=socks, plus skin indices). So the
 * stock Amiga per-team conversions (ConvertSpriteColors via
 * team1/2PlayersColorTable) recolour them correctly — we just have to put
 * the DOS sprites in the table BEFORE those conversions run, not after.
 *
 * The conversions recolour the sprite pixels IN PLACE. The DOS sprites are
 * decoded once into a persistent "working" buffer; we keep a pristine copy
 * and restore working from it at the start of every match (dos_prepare_sprites),
 * so the per-match conversions always start from the un-recoloured template
 * (otherwise successive matches would stack remaps and corrupt the kit).
 * ================================================================ */
extern uint8_t *_spriteGraphicsPtr;
extern int16_t  LoadFileEx(uint8_t *filename, uint8_t *dest, uint32_t *outFileSize);

#define DOS_SPRITE_TABLE_SLOTS  1220   /* spriteGraphics[] backing = 1220 ptrs */
#define DOS_SPRITE_MAX_REGIONS  8      /* one per dos_build_sprite_dat call */

static SpriteGraphics *s_dosSprite[DOS_SPRITE_TABLE_SLOTS];   /* NULL = keep Amiga */
static int s_dosSpritesBuilt;
/* Per-.dat chunky regions: `work` is what the table points at (recoloured each
 * match); `pristine` is the un-recoloured decode used to reset `work`. */
static struct { uint8_t *work; uint8_t *pristine; uint32_t size; SpriteGraphics *recs; }
    s_dosRegions[DOS_SPRITE_MAX_REGIONS];
static int s_dosRegionCount;
/* Which kit patterns the current build was made from, so a fixture with
 * different shirt types rebuilds instead of reusing the previous match's. */
static int s_dosBuiltKit1 = -1, s_dosBuiltKit2 = -1;

static void dos_build_sprite_dat(const char *name, int dosStart, int count)
{
    char path[320];
    static uint8_t staging[64 * 1024];   /* >= largest game .dat (team*=45648) */
    uint32_t fileSize = 0;

    snprintf(path, sizeof path, "%s/%s", _gDosGraphicsDir, name);
    LoadFileEx((uint8_t *)path, staging, &fileSize);
    if (fileSize == 0 || fileSize > sizeof staging)
        return;

    /* Chunky output is 2 bytes/pixel-byte vs planar's 1; total < 2*fileSize. */
    uint8_t *chunky = (uint8_t *)malloc((size_t)fileSize * 2);
    SpriteGraphics *recs = (SpriteGraphics *)calloc((size_t)count, sizeof(SpriteGraphics));
    if (!chunky || !recs) { free(chunky); free(recs); return; }

    uint32_t inOff = 0, outOff = 0;
    for (int i = 0; i < count; i++) {
        if (inOff + 24 > fileSize) break;
        const uint8_t *h = staging + inOff;
        /* DOS header fields are little-endian (host is LE; DOS path only). */
        #define DOS_LE16(o) (int16_t)((uint16_t)h[(o)] | ((uint16_t)h[(o) + 1] << 8))
        int16_t pixW   = DOS_LE16(10);
        int16_t nlines = DOS_LE16(12);
        int16_t wquads = DOS_LE16(14);
        int16_t cX     = DOS_LE16(16);
        int16_t cY     = DOS_LE16(18);
        #undef DOS_LE16
        inOff += 24;

        int planarBytes = (int)nlines * (int)wquads * 8;
        if (nlines <= 0 || wquads <= 0 || planarBytes <= 0 ||
            inOff + (uint32_t)planarBytes > fileSize)
            break;

        uint8_t *dst = chunky + outOff;
        DosGfx_DecodePlanarSprite(staging + inOff, nlines, wquads, dst);
        inOff  += (uint32_t)planarBytes;
        outOff += (uint32_t)((int)nlines * (int)wquads * 16);

        int amigaIdx = dosStart + i - DOS_TO_AMIGA_OFFSET;

        SpriteGraphics *s = &recs[i];
        s->sprData  = dst;
        s->maskPtr  = 0;
        s->type     = 0;                                   /* 8bpp chunky */
        s->pixWidth = pixW;
        s->nlines   = nlines;
        s->wquads   = wquads;
        s->centerX  = cX;
        s->centerY  = cY;
        s->bltsize  = (uint16_t)(((int)nlines << 6) + wquads + wquads);
        s->ordinal  = (int16_t)amigaIdx;

        /* Don't let the DOS bench sprites clobber the replay-"R" indicator slots —
         * keep the correct Amiga frames there (the DOS asset has garbage here). */
        if (amigaIdx >= 0 && amigaIdx < DOS_SPRITE_TABLE_SLOTS
            && !(amigaIdx >= SPR_REPLAY_FRAME_00 && amigaIdx <= SPR_REPLAY_FRAME_LAST))
            s_dosSprite[amigaIdx] = s;
    }

    /* Keep a pristine copy of the decoded pixels so dos_prepare_sprites can
     * reset `chunky` before each match's in-place kit/skin recolour. */
    if (outOff > 0 && s_dosRegionCount < DOS_SPRITE_MAX_REGIONS) {
        uint8_t *pristine = (uint8_t *)malloc(outOff);
        if (pristine) {
            memcpy(pristine, chunky, outOff);
            s_dosRegions[s_dosRegionCount].work     = chunky;
            s_dosRegions[s_dosRegionCount].pristine = pristine;
            s_dosRegions[s_dosRegionCount].size     = outOff;
            s_dosRegions[s_dosRegionCount].recs     = recs;
            s_dosRegionCount++;
            return;
        }
    }
    free(chunky);
    free(recs);
}

/* The DOS kit .dat files are the same three PATTERNS as the Amiga's
 * cjcteam1/2/3, and map the same way teamFilenamesTable does
 * (SWOS2.s:126689 — {cjcteam1, cjcteam2, cjcteam1, cjcteam3}):
 *
 *   TEAM1.DAT  vertical stripes  — also serves ORDINARY, which is the same
 *                                  artwork with both kit slots one colour
 *   TEAM2.DAT  coloured sleeves
 *   TEAM3.DAT  horizontal stripes
 *
 * Verified by decoding the first sprites of each file: 96% of their bytes are
 * identical (same geometry, same sprite count) and they differ only in the kit
 * pixels. */
static const char *dos_kit_file(int shirtType)
{
    switch (shirtType) {
    case SHIRT_COLORED_SLEEVES:    return "team2.dat";
    case SHIRT_HORIZONTAL_STRIPES: return "team3.dat";
    default:                       return "team1.dat";
    }
}

static void dos_free_sprites(void)
{
    for (int i = 0; i < s_dosRegionCount; i++) {
        free(s_dosRegions[i].work);
        free(s_dosRegions[i].pristine);
        free(s_dosRegions[i].recs);
    }
    s_dosRegionCount = 0;
    for (int i = 0; i < DOS_SPRITE_TABLE_SLOTS; i++)
        s_dosSprite[i] = NULL;
    s_dosSpritesBuilt = 0;
}

static void dos_build_all_sprites(int kit1, int kit2)
{
    /* (file, DOS start index, count) — from the reference loadAllGameSprites.
     * goal1.dat is loaded twice (two keeper copies).
     * score.dat/charset deliberately omitted.
     *
     * The two player ranges take the kit pattern each TEAM actually wears. This
     * used to hardcode team1.dat and team3.dat, on the reading that team3.dat
     * was "the second team's players" — but they are PATTERNS, not per-team
     * sets, so team 1 always came out in vertical stripes and team 2 always in
     * horizontal stripes, and team2.dat (coloured sleeves) was never loaded at
     * all. A tester on dos_graphics: "my team is Freiburg, and they do not have
     * striped shirts. They should have red shirts with black sleeves" —
     * Freiburg is primary_shirt_type 1, COLORED_SLEEVES. */
    dos_build_sprite_dat(dos_kit_file(kit1), 341, 303);   /* amiga 227..529 */
    dos_build_sprite_dat(dos_kit_file(kit2), 644, 303);   /* amiga 530..832 */
    dos_build_sprite_dat("goal1.dat", 947, 116);   /* amiga 833..948 */
    dos_build_sprite_dat("goal1.dat", 1063, 116);  /* amiga 949..1064 */
    dos_build_sprite_dat("bench.dat", 1179, 155);  /* amiga 1065..1219 */
    s_dosSpritesBuilt = 1;
}

/* Put the DOS player/keeper/bench sprites into the table, pristine, ready for
 * the per-team kit/skin conversions that run later in ConvertColorsAndTextSprites.
 * Call this BEFORE those conversions (they recolour the pixels in place). */
static void dos_prepare_sprites(int kit1, int kit2)
{
    SpriteGraphics **table = (SpriteGraphics **)_spriteGraphicsPtr;
    if (!table) return;
    /* Rebuild when this fixture's shirt types differ from the built set. */
    if (s_dosSpritesBuilt && (kit1 != s_dosBuiltKit1 || kit2 != s_dosBuiltKit2))
        dos_free_sprites();
    if (!s_dosSpritesBuilt) {
        dos_build_all_sprites(kit1, kit2);
        s_dosBuiltKit1 = kit1;
        s_dosBuiltKit2 = kit2;
        fprintf(stderr, "[dos] kit sprites: team1 type %d -> %s, team2 type %d -> %s\n",
                kit1, dos_kit_file(kit1), kit2, dos_kit_file(kit2));
    }

    /* Reset each region to its un-recoloured decode so this match's
     * conversions don't stack on the previous match's remap. */
    for (int i = 0; i < s_dosRegionCount; i++)
        memcpy(s_dosRegions[i].work, s_dosRegions[i].pristine, s_dosRegions[i].size);

    for (int i = 0; i < DOS_SPRITE_TABLE_SLOTS; i++)
        if (s_dosSprite[i]) table[i] = s_dosSprite[i];
}
#endif /* SWOS_SDL_BUILD */

/* ---- Helper: convert a range of sprites with a color table ---- */
static void convertSpriteRange(uint16_t start, uint16_t end, uint16_t *colorTable)
{
    uint16_t d0;
#ifdef SWOS_SDL_BUILD
    /* Jaguar sprites carry four shades of each kit colour, so the 16-entry table
     * the game just built becomes a 256-entry one: each shade of each kit slot to
     * that shade of the team's colour, everything else as the game says
     * (jag_graphics.c). Built per call, because the skin and hair entries change
     * between ranges. */
    uint16_t jagTable[256];
    if (JagGfx_Enabled()) {
        JagGfx_TeamTable(colorTable, jagTable);
        colorTable = jagTable;
    }
#endif
    for (d0 = start; d0 != end; d0++) {
        ConvertSpriteColors(d0, colorTable);
    }
}

/* ---- Helper: find most frequent face and apply skin/hair LUT ---- */
static uint16_t findMostFrequentFaceAndApplyLUT(TeamGame *team, uint16_t *colorTable)
{
    uint16_t d0;
    int16_t *lutPtr;

    CountFaces(team);

    /* Find face with highest frequency */
    d0 = 0;
    {
        uint8_t best = _facesFrequency[0];
        if (_facesFrequency[1] > best) {
            best = _facesFrequency[1];
            d0 = 1;
        }
        if (_facesFrequency[2] > best) {
            d0 = 2;
        }
    }

    /* Apply skin/hair color conversion from LUT */
    lutPtr = (int16_t *)_skinAndHairColorConversionTable[d0];
    for (;;) {
        int16_t idx = *lutPtr++;
        if (idx < 0)
            break;
        colorTable[idx] = (uint16_t)*lutPtr++;
    }

    return d0;
}

/* ================================================================
 * ConvertColorsAndTextSprites  (SWOS2.s line 12121)
 *
 * Master sprite color initialization function.
 * Sets team filenames, runs image command stream, builds team name
 * sprites, and converts all player/number/result sprites with
 * team-specific and skin-specific color tables.
 *
 * team1 and team2 are pointers to TeamGame structs (A5/A6 in asm).
 * ================================================================ */
__attribute__((used, externally_visible))
void ConvertColorsAndTextSprites(TeamGame *team1, TeamGame *team2)
{
    TeamColors_MatchPalette(team1);      /* purple: maroon's entry 12 for this match (team_colors.c) */
    JagGfx_NewMatch();      /* these two teams' kit ramps, not the last pair's */
    uint16_t d0;
    uint16_t totalWidth;

    /* Set team filenames from shirt type index */
    d0 = (uint16_t)team1->prShirtType;
    _team1Filename = (uintptr_t)_teamFilenamesTable[d0];
    d0 = (uint16_t)team2->prShirtType;
    _team2Filename = (uintptr_t)_teamFilenamesTable[d0];

    /* Process image command stream (colors menu graphics) */
    /* Force read from memory — LTO must not use a stale cached value */
    ProcessImageCommandStream(_menuColorAndTextScript, *(uint8_t * volatile *)&_menuImageStreamBaseEnd);

#ifdef SWOS_SDL_BUILD
    /* PC-only: drop the DOS player/keeper/bench sprites into the table NOW —
     * before the kit/skin colour conversions below — so those conversions
     * recolour the DOS sprites (16-colour, same kit indices as the Amiga)
     * instead of the Amiga ones we just loaded. dos_prepare_sprites resets the
     * sprites to their pristine decode first, so per-match remaps don't stack. */
    /* ... unless the JAGUAR tier is on too: both want to be the players, and then
     * the Jaguar sprites are what was asked for -- DOS keeps the pitch and the rest
     * of its look (docs/JAGUAR.md, "With the DOS graphics"). */
    if (_gDosGraphics && !JagGfx_Enabled())
        dos_prepare_sprites((int)team1->prShirtType, (int)team2->prShirtType);
#endif

    /* Copy advertisement sprite data pointers */
    CopyAdvertisementsData();

    /* Build player name sprites for both teams */
    MakeSpritesFromPlayerNames(team1, team2);

    /* --- Big number / result sprites --- */
    /* Set bigNumbersColorConversionTable indices 10 and 11 to 2 */
    _bigNumbersColorConversionTable[11] = 2;
    _bigNumbersColorConversionTable[10] = 2;

    /* Convert big dash sprite */
    ConvertSpriteColors(SPR_BIG_DASH, (uint16_t *)_bigNumbersColorConversionTable);

    /* Convert big digit sprites (SPR_BIG_0 through SPR_RESULT_BIG_0-1) */
    convertSpriteRange(SPR_BIG_0, SPR_RESULT_BIG_0, (uint16_t *)_bigNumbersColorConversionTable);

    /* Convert result digit sprites (SPR_RESULT_BIG_0 through SPR_TEAM1_NAME-1) */
    convertSpriteRange(SPR_RESULT_BIG_0, SPR_TEAM1_NAME, (uint16_t *)_bigNumbersColorConversionTable);

    /* --- Team 1 name sprite --- */
    HWSprite_LoadFromSpriteGraphics(SPR_TEAM1_NAME);
    Text2Sprite(0, 0, SPR_TEAM1_NAME, (uint8_t *)team1->teamName,
                 _bigCharsTable, 2, &totalWidth);
    _team1NameLength = totalWidth;

    /* --- Team 2 name sprite --- */
    HWSprite_LoadFromSpriteGraphics(SPR_TEAM2_NAME);
    Text2Sprite(0, 0, SPR_TEAM2_NAME, (uint8_t *)team2->teamName,
                 _bigCharsTable, 2, &totalWidth);
    _team2NameLength = totalWidth;

    /* --- Reserve player sprites for team 1 --- */
    _colorTableReserves[15] = (uint16_t)team1->prShirtCol;
    {
        uint16_t stripesCol = (uint16_t)team1->prStripesCol;
        if (stripesCol == (uint16_t)team1->prShirtCol)
            stripesCol = (uint16_t)team1->prShortsCol;
        _colorTableReserves[11] = stripesCol;
    }
    convertSpriteRange(1196, 1208, _colorTableReserves);

    /* --- Reserve player sprites for team 2 --- */
    _colorTableReserves[15] = (uint16_t)team2->prShirtCol;
    {
        uint16_t stripesCol = (uint16_t)team2->prStripesCol;
        if (stripesCol == (uint16_t)team2->prShirtCol)
            stripesCol = (uint16_t)team2->prShortsCol;
        _colorTableReserves[11] = stripesCol;
    }
    convertSpriteRange(1208, 1220, _colorTableReserves);

    /* --- Determine goalkeeper faces for both teams --- */
    DetermineGoalieFaces(team1, _goalieFaces.team1FaceToPosition, _goalieFaces.team1Faces);
    DetermineGoalieFaces(team2, _goalieFaces.team2FaceToPosition, _goalieFaces.team2Faces);

    /* --- Team color tables: skin/hair from face frequency --- */
    if (_useIndividualPlayerSkinColor == 0) {
        /* Use most frequent face for both teams */
        _team1MostFrequentFace = findMostFrequentFaceAndApplyLUT(team1, _team1PlayersColorTable);
        _team2MostFrequentFace = findMostFrequentFaceAndApplyLUT(team2, _team2PlayersColorTable);
    }

    /* --- Team 1 shirt/stripes/shorts/socks colors --- */
    {
        uint16_t d1 = (uint16_t)team1->prShirtCol;
        uint16_t d2 = (uint16_t)team1->prStripesCol;
        /* If shirt type == 2 (striped), swap shirt and stripes colors */
        if ((uint16_t)team1->prShirtType == 2) {
            uint16_t tmp = d1; d1 = d2; d2 = tmp;
        }
        _team1PlayersColorTable[11] = d1;
        _team1PlayersColorTable[10] = d2;
        _team1PlayersColorTable[14] = (uint16_t)team1->prShortsCol;
        _team1PlayersColorTable[15] = (uint16_t)team1->prSocksCol;
    }

    /* Team 1 skin 0: convert sprites 227-327 */
    if (_useIndividualPlayerSkinColor != 0)
        ConvertColorTableToSkinColor(0, _team1PlayersColorTable);
    convertSpriteRange(227, 328, _team1PlayersColorTable);

    /* Team 1 skin 1: convert sprites 328-428 */
    if (_useIndividualPlayerSkinColor != 0) {
        ConvertColorTableToSkinColor(1, _team1PlayersColorTable);
        convertSpriteRange(328, 429, _team1PlayersColorTable);
    }

    /* Team 1 skin 2: convert sprites 429-529 */
    if (_useIndividualPlayerSkinColor != 0) {
        ConvertColorTableToSkinColor(2, _team1PlayersColorTable);
        convertSpriteRange(429, 530, _team1PlayersColorTable);
    }

    /* Team 1 goalkeeper: convert sprites 833-890.
     * goalieFaces.team1Faces[0] == 0xFF means DetermineGoalieFaces didn't assign
     * a face slot. Mirroring the reserve-GK guard below, skip the conversion
     * — otherwise ConvertColorTableToSkinColor sign-extends 0xFF to -1 and
     * indexes the 3-entry skinAndHairColorConversionTable[] out-of-bounds,
     * walking random memory writing (idx,value) pairs and corrupting
     * unrelated globals (we hit `bigCharsTable + 64`, which silently broke
     * the 'R' and 'S' glyph lookups). */
    if (_useIndividualPlayerSkinColor != 0) {
        int8_t face = (int8_t)_goalieFaces.team1Faces[0];
        if (face >= 0)
            ConvertColorTableToSkinColor((uint16_t)(uint8_t)face, _team1PlayersColorTable);
    }
    convertSpriteRange(833, 891, _team1PlayersColorTable);

    /* Team 1 goalkeeper reserve: sprites 891-948
     * Only converted when useIndividualPlayerSkinColor != 0 AND face >= 0.
     * Note: this is team 1's reserve GK face (team1Faces[1]). */
    if (_useIndividualPlayerSkinColor != 0) {
        int8_t face2 = (int8_t)_goalieFaces.team1Faces[1];
        if (face2 >= 0) {
            ConvertColorTableToSkinColor((uint16_t)(uint8_t)face2, _team1PlayersColorTable);
            convertSpriteRange(891, 949, _team1PlayersColorTable);
        }
    }

    /* --- Team 2 shirt/stripes/shorts/socks colors --- */
    {
        uint16_t d1 = (uint16_t)team2->prShirtCol;
        uint16_t d2 = (uint16_t)team2->prStripesCol;
        if ((uint16_t)team2->prShirtType == 2) {
            uint16_t tmp = d1; d1 = d2; d2 = tmp;
        }
        _team2PlayersColorTable[11] = d1;
        _team2PlayersColorTable[10] = d2;
        _team2PlayersColorTable[14] = (uint16_t)team2->prShortsCol;
        _team2PlayersColorTable[15] = (uint16_t)team2->prSocksCol;
    }

    /* Team 2 skin 0: convert sprites 530-630 */
    if (_useIndividualPlayerSkinColor != 0)
        ConvertColorTableToSkinColor(0, _team2PlayersColorTable);
    convertSpriteRange(0x212, 0x277, _team2PlayersColorTable);

    /* Team 2 skin 1: convert sprites 631-731 */
    if (_useIndividualPlayerSkinColor != 0) {
        ConvertColorTableToSkinColor(1, _team2PlayersColorTable);
        convertSpriteRange(0x277, 0x2DC, _team2PlayersColorTable);
    }

    /* Team 2 skin 2: convert sprites 732-832 */
    if (_useIndividualPlayerSkinColor != 0) {
        ConvertColorTableToSkinColor(2, _team2PlayersColorTable);
        convertSpriteRange(0x2DC, 833, _team2PlayersColorTable);
    }

    /* Team 2 goalkeeper: convert sprites 949-1006.
     * Same 0xFF-sentinel guard as team 1's main goalkeeper above. */
    if (_useIndividualPlayerSkinColor != 0) {
        int8_t face = (int8_t)_goalieFaces.team2Faces[0];
        if (face >= 0)
            ConvertColorTableToSkinColor((uint16_t)(uint8_t)face, _team2PlayersColorTable);
    }
    convertSpriteRange(949, 1007, _team2PlayersColorTable);

    /* Team 2 goalkeeper reserve: sprites 1007-1064
     * Only converted when useIndividualPlayerSkinColor != 0 AND face >= 0. */
    if (_useIndividualPlayerSkinColor != 0) {
        int8_t reserveFace = (int8_t)_goalieFaces.team2Faces[1];
        if (reserveFace >= 0) {
            ConvertColorTableToSkinColor((uint16_t)(uint8_t)reserveFace, _team2PlayersColorTable);
            convertSpriteRange(1007, 1065, _team2PlayersColorTable);
        }
    }

    ShirtNumbers_Build();   /* the shirt numbers 17..99 from this match's digits (shirt_numbers.c) */
}
