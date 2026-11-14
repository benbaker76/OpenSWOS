/*
 * player_looks.c -- the players' looks of the SWOS 96/97 Mod (include/player_looks.h; the Mod's
 * tools/blond.py, bald.c, shavedart.py; docs/WORLD_2026_27.md §6l): a receding hairline, a shaved
 * head (Marco's hand-drawn heads), white hair.
 *
 * The look is the player record's byte 25 (the Mod's), which our team files, saves and custom teams
 * carry; the Mod's own looks of the 1996-97 game, which the original team files cannot hold, are a
 * rule here (Ravanelli's white hair). Drawing only, as the Mod's: a match keeps every player's look
 * in its own table (not match state: src/sim_state.c never sees it), and a player sprite is DRAWN
 * with its look's frame -- its image index, the sprite, every hash stay as they were. So a live
 * match plays the same with or without looks, on any build (NETPLAY_COMPAT unchanged).
 *
 * In a match (SDL) each team gets six sets of 101 frames, every look in light and dark skin (the
 * Mod's flags 2..7), made from a copy of its raw first set as the Mod's copy_sets does, reshaped by
 * bald.c's rules in raw colours and recoloured with the team's table and face 0 (light) or 2 (dark):
 *   - receding ('E', Davide): the hair's top row becomes skin, and the second row's hair with a
 *     pixel on both sides; the sides and the back keep the hair;
 *   - shaved: every frame's head drawn by Marco (player_looks_art.c), the rest of the hair kept;
 *   - white: the hair white, its darkest shade and the rows below the top two grey, the top row's
 *     two end pixels off.
 * The Mod draws the shaved skull in two NEW palette tones (84, 85); our palettes have no entry free
 * in every tier, so the skull takes the skin's own lightest shade -- the Mod's half / EGA mode.
 * All six are made for both teams, whoever plays, so a replay (the highlights, a .SRP played later)
 * finds every look's frames. A goalkeeper keeps his hair (the Mod's). The Jaguar's art is drawn in
 * ramps the rules do not know: no looks there.
 *
 * The Amiga carries the looks (team files, .EDT) but draws none: six sets a team is ~200 KB of chip
 * RAM (docs §6l).
 */
#include "player_looks.h"
#include "player_face3.h"
#include "game_types.h"
#include "display.h"

#include <string.h>
#ifdef SWOS_SDL_BUILD
#include <stdlib.h>
#include <stdio.h>
#include "jag_graphics.h"
#endif

extern uint8_t *_spriteGraphicsPtr;
extern TeamGame _topTeamInGame, _bottomTeamInGame;

/* ---- the record ---- */

int32_t PlayerLooks_Room(const PlayerFile *pf)
{
    return pf && pf->playerName[21] == 0;
}

int32_t PlayerLooks_FromBits(uint8_t bits)
{
    if (bits & LOOKS_BIT_BALD)
        return (bits & LOOKS_BIT_SHAVED) ? LOOK_SHAVED : LOOK_RECEDING;
    return (bits & LOOKS_BIT_WHITE) ? LOOK_WHITE : LOOK_NONE;
}

uint8_t PlayerLooks_Bits(int32_t look)
{
    return look == LOOK_RECEDING ? LOOKS_BIT_BALD
         : look == LOOK_SHAVED   ? (LOOKS_BIT_BALD | LOOKS_BIT_SHAVED)
         : look == LOOK_WHITE    ? LOOKS_BIT_WHITE : 0;
}

const char *PlayerLooks_Name(int32_t look)
{
    return look == LOOK_RECEDING ? "receding" : look == LOOK_SHAVED ? "shaved" : look == LOOK_WHITE ? "white" : "";
}

/* The Mod's own looks of the 1996-97 game (its packs/white-hair.csv, written into its TEAM.008 and
 * TEAM.080: Middlesbrough's and Italy's Ravanelli, face 1, Italian). Ours are the original files,
 * so the look is a rule: by name, nationality and face -- never written into a record, so a save of
 * a 1996-97 career is the one it always was. */
static const struct { const char *name; int8_t nationality, face; uint8_t look; } kMod9697[] = {
    { "FABRIZIO RAVANELLI", 18, 1, LOOK_WHITE },
};

int32_t PlayerLooks_Of(const PlayerFile *pf)
{
    if (!pf)
        return LOOK_NONE;
    if (PlayerLooks_Room(pf)) {
        const int32_t look = PlayerLooks_FromBits((uint8_t)pf->playerName[22]);
        if (look != LOOK_NONE)
            return look;
    }
    for (uint32_t i = 0; i < sizeof kMod9697 / sizeof kMod9697[0]; i++)
        if (pf->nationality == kMod9697[i].nationality &&
            ((pf->positionAndFace & PF_FACE_MASK) >> 3) == (uint8_t)kMod9697[i].face &&
            strcmp((const char *)pf->playerName, kMod9697[i].name) == 0)
            return kMod9697[i].look;
    return LOOK_NONE;
}

int32_t PlayerLooks_Set(PlayerFile *pf, int32_t look)
{
    if (!PlayerLooks_Room(pf))
        return 0;
    pf->playerName[22] = (int8_t)(((uint8_t)pf->playerName[22] & (uint8_t)~LOOKS_BITS) | PlayerLooks_Bits(look));
    return 1;
}

/* ---- a match's players ---- */

static int8_t s_matchLook[2][64];       /* _topTeamInGame / _bottomTeamInGame, by file index */

static int32_t team_key(const TeamGame *t)
{
    return t == &_topTeamInGame ? 0 : t == &_bottomTeamInGame ? 1 : -1;
}

#ifdef SWOS_SDL_BUILD
/* SWOS_TEST_LOOK=receding|shaved|white|all: every outfield player that look (all: by his place,
 * the three in turn and one without) -- to look at them. */
static int32_t test_look(int32_t slot)
{
    const char *e = getenv("SWOS_TEST_LOOK");
    if (!e || !*e)
        return -1;
    if (strcmp(e, "all") == 0)
        return slot % 4;
    for (int32_t l = 1; l < LOOK_COUNT; l++)
        if (strcmp(e, PlayerLooks_Name(l)) == 0)
            return l;
    return -1;
}
#endif

void PlayerLooks_NotePlayer(const PlayerInfo *pi, const PlayerFile *pf)
{
    const uint8_t *p = (const uint8_t *)pi;
    int32_t k = -1;
    if (p >= (const uint8_t *)&_topTeamInGame && p < (const uint8_t *)(&_topTeamInGame + 1))
        k = 0;
    else if (p >= (const uint8_t *)&_bottomTeamInGame && p < (const uint8_t *)(&_bottomTeamInGame + 1))
        k = 1;
    if (k < 0)
        return;
    int32_t look = PlayerLooks_Of(pf);
#ifdef SWOS_SDL_BUILD
    {
        const int32_t t = test_look((int32_t)(pi - (k ? _bottomTeamInGame.players : _topTeamInGame.players)));
        if (t >= 0)
            look = t;
    }
#endif
    s_matchLook[k][(uint8_t)pi->playerIndex & 63] = (int8_t)look;
}

int32_t PlayerLooks_InMatch(const TeamGame *team, const PlayerInfo *pi)
{
    const int32_t k = team_key(team);
    return k < 0 || !pi ? LOOK_NONE : s_matchLook[k][(uint8_t)pi->playerIndex & 63];
}

/* ---- the rules over a frame (raw colours, 16 wide, chunky; both targets) ---- */

enum { W = 16, HMAX = 24, SET = 101 };
static const int16_t kSetBase[2] = { 227, 530 };   /* each team's first (raw, face 0) set */

static int32_t is_hair(uint8_t c) { return c == 13 || c == 9 || c == 12; }

/* bald.c whiten(): over the first n cells; mask = the hair's colours */
static void whiten(uint8_t *q, int32_t n, int32_t icon)
{
    int32_t f = -1, l = 0, c = 0;
    for (int32_t i = 0; i < n; i++) {
        const int32_t hair = icon ? q[i] == 3 : is_hair(q[i]);
        if (!hair)
            continue;
        if (f < 0)
            f = i;
        if (i / W == f / W) {
            l = i;
            c++;
        }
        q[i] = (uint8_t)(q[i] == 12 || i / W > f / W + 1 ? 1 : 2);    /* (grey, white: no face changes them) */
    }
    if (c >= 3)
        q[f] = q[l] = 0;
}

static void receding(uint8_t *px, int32_t nl)
{
    int32_t y0 = -1;
    for (int32_t y = 0; y < nl && y0 < 0; y++)
        for (int32_t x = 0; x < W; x++)
            if (is_hair(px[y * W + x])) { y0 = y; break; }
    if (y0 < 0)
        return;
    for (int32_t y = y0; y <= y0 + 1 && y < nl; y++)
        for (int32_t x = 0; x < W; x++) {
            uint8_t *c = &px[y * W + x];
            if (!is_hair(*c))
                continue;
            const int32_t sides = x > 0 && x < W - 1 && px[y * W + x - 1] && px[y * W + x + 1];
            if (y == y0 || sides)
                *c = 6;
        }
}

/* Marco's head of frame f over px (bald.c art_head): the light skull's tone and the dark one's
 * shade (raw 13 in the art) in the skin's lightest (6) / middle (5) shade. */
static void shaved(uint8_t *px, int32_t nl, int32_t f, int32_t dark)
{
    for (int32_t i = 0; i < nl * W; i++)        /* (hair kept outside the head stays hair) */
        if (px[i] == 13)
            px[i] = 9;
    if (f < 0 || f >= LOOKS_ART_FRAMES)
        return;
    const uint8_t *fr = g_looksArtFrames[f];
    const LooksArtHead *h = dark ? &g_looksArtDark[fr[0]] : &g_looksArtLight[fr[0]];
    const int32_t oy = h->ry, ox = fr[dark ? 1 : 2] + h->rx;
    for (int32_t y = 0; y < h->h; y++)
        for (int32_t x = 0; x < h->w; x++) {
            const int32_t v = h->cells[y][x], py = oy + y, pxx = ox + x;
            if (v < 0 || py >= nl || pxx >= W)
                continue;
            px[py * W + pxx] = (uint8_t)(v == 13 ? (dark ? 5 : 6) : v);
        }
}

static void apply_look(uint8_t *px, int32_t nl, int32_t f, int32_t look, int32_t dark)
{
    if (look == LOOK_RECEDING)
        receding(px, nl);
    else if (look == LOOK_SHAVED)
        shaved(px, nl, f, dark);
    else if (look == LOOK_WHITE)
        whiten(px, nl * W, 0);
}

/* bald.c bald_icon's receding shape over an icon (menu colours, hair 3 in the top three rows) */
static void icon_receding(uint8_t *px, int32_t nl)
{
    uint8_t hm[3][W];
    int32_t skin = 0, y0 = -1;
    for (int32_t y = 3; y < nl && !skin; y++)
        for (int32_t x = 0; x < W; x++)
            if (px[y * W + x] && px[y * W + x] != 3) { skin = px[y * W + x]; break; }
    if (!skin)
        return;
    for (int32_t y = 0; y < 3; y++)
        for (int32_t x = 0; x < W; x++) {
            hm[y][x] = px[y * W + x] == 3;
            if (hm[y][x]) {
                px[y * W + x] = (uint8_t)skin;
                if (y0 < 0)
                    y0 = y;
            }
        }
    if (y0 < 0)
        return;
    int32_t lo = 0, hi = W - 1;
    while (lo < W && !px[y0 * W + lo]) lo++;
    while (hi > 0 && !px[y0 * W + hi]) hi--;
    if (lo > hi)
        return;
    px[y0 * W + lo] = px[y0 * W + hi] = 0;
    int32_t top = 0, next = 0;
    for (int32_t x = 0; x < W; x++) {
        top += px[y0 * W + x] != 0;
        next += px[(y0 + 1) * W + x] != 0;
    }
    if (top < next - 3)
        for (int32_t x = 0; x < W; x++)
            px[y0 * W + x] = 0;
    for (int32_t y = 2, n = 0; y >= 0 && n < 2; y--) {
        int32_t a = -1, b = -1;
        for (int32_t x = 0; x < W; x++)
            if (hm[y][x] && px[y * W + x]) { if (a < 0) a = x; b = x; }
        if (a < 0)
            continue;
        px[y * W + a] = px[y * W + b] = 3;
        n++;
    }
}

static SpriteGraphics **table(void) { return (SpriteGraphics **)_spriteGraphicsPtr; }

#ifdef SWOS_SDL_BUILD

/* ---- a match's sets ---- */


static uint8_t        s_raw[2][SET][HMAX * W];
static SpriteGraphics s_rawHdr[2][SET];
static uint8_t        s_haveRaw[2];
static uint8_t        (*s_pix)[LOOKS_VARIANTS][SET][HMAX * W];    /* [2] (180 KB: on the heap) */
static SpriteGraphics s_hdr[2][LOOKS_VARIANTS][SET];
static uint8_t        s_ready[2];

void PlayerLooks_CopySets(void)
{
    for (int32_t t = 0; t < 2; t++) {
        s_haveRaw[t] = s_ready[t] = 0;
        if (JagGfx_Enabled())
            continue;
        int32_t i;
        for (i = 0; i < SET; i++) {
            const SpriteGraphics *src = table()[kSetBase[t] + i];
            if (!src || !src->sprData || src->wquads != 1 || src->nlines > HMAX)
                break;
            s_rawHdr[t][i] = *src;
            memcpy(s_raw[t][i], src->sprData, (size_t)src->nlines * W);
        }
        s_haveRaw[t] = i == SET;
    }
}

void PlayerLooks_ConvertSets(int32_t t, uint16_t *colorTable)
{
    extern void ConvertColorTableToSkinColor(uint16_t color, uint16_t *destTable);
    if (t < 0 || t > 1 || !s_haveRaw[t])
        return;
    if (!s_pix && !(s_pix = calloc(2, sizeof *s_pix)))
        return;
    for (int32_t v = 0; v < LOOKS_VARIANTS; v++) {
        const int32_t look = v / 2 + 1, dark = v & 1;
        uint16_t tab[16];
        memcpy(tab, colorTable, sizeof tab);
        ConvertColorTableToSkinColor(dark ? 2 : 0, tab);
        for (int32_t f = 0; f < SET; f++) {
            SpriteGraphics *h = &s_hdr[t][v][f];
            uint8_t *px = s_pix[t][v][f];
            const int32_t nl = s_rawHdr[t][f].nlines;
            memcpy(px, s_raw[t][f], (size_t)nl * W);
            apply_look(px, nl, f, look, dark);
            for (int32_t i = 0; i < nl * W; i++)
                if (px[i] && px[i] < 16)
                    px[i] = (uint8_t)tab[px[i]];
            *h = s_rawHdr[t][f];
            h->sprData = px;
            h->ordinal = (int16_t)(LOOKS_SPRITE_FIRST + (t * LOOKS_VARIANTS + v) * SET + f);
            table()[h->ordinal] = h;
        }
    }
    s_ready[t] = 1;
}

/* ---- drawing ---- */

extern Sprite  **_team1SpritesTable;
extern Sprite  **_team2SpritesTable;
extern TeamGame *_topTeamPtr;
extern TeamGame *_bottomTeamPtr;
extern uint16_t  _useIndividualPlayerSkinColor;

int16_t PlayerLooks_Image(const Sprite *s, int16_t image)
{
    if (!s || image < 0 || !_useIndividualPlayerSkinColor || !_team1SpritesTable || !_team2SpritesTable)
        return image;
    int32_t t = -1, slot = -1;
    for (int32_t i = 1; i < 11 && t < 0; i++) {         /* (0, the goalkeeper, keeps his hair) */
        if (_team1SpritesTable[i] == s) { t = 0; slot = i; }
        else if (_team2SpritesTable[i] == s) { t = 1; slot = i; }
    }
    if (t < 0 || !s_ready[t])
        return image;
    const TeamGame *team = t == 0 ? _topTeamPtr : _bottomTeamPtr;
    if (!team)
        return image;
    const PlayerInfo *pi = &team->players[slot];
    const int32_t look = PlayerLooks_InMatch(team, pi);
    if (look == LOOK_NONE)
        return image;
    int32_t f;
    const int16_t f3 = Face3_Set(t);
    if (image >= kSetBase[t] && image < kSetBase[t] + 3 * SET)
        f = (image - kSetBase[t]) % SET;
    else if (f3 >= 0 && image >= f3 && image < f3 + SET)
        f = image - f3;
    else
        return image;
    const int32_t v = (look - 1) * 2 + (pi->face == 2);
    return (int16_t)(LOOKS_SPRITE_FIRST + (t * LOOKS_VARIANTS + v) * SET + f);
}

/* SWOS_LOOKS_DUMP=<file.png> (a test's, to look at them): at a match's first drawn look, team 1's
 * frames 0..33 four times their size -- the face 0 and face 2 sets, then the six looks -- in the
 * Amiga palette (_gamePalette: the rig's screen may still be dark). Once per run. */
static void dump_sets(void)
{
    extern int stbi_write_png(char const *filename, int w, int h, int comp, const void *data, int stride);
    extern uint16_t _gamePalette[];
    static int32_t done;
    const char *path = getenv("SWOS_LOOKS_DUMP");
    if (done || !path || !*path || !s_ready[0])
        return;
    done = 1;
    enum { kCols = 34, kRows = 2 + LOOKS_VARIANTS, kS = 4, kCw = 18, kCh = 26 };
    const int32_t w = kCols * kCw * kS, h = kRows * kCh * kS;
    uint8_t *img = calloc((size_t)w * h, 3);
    if (!img)
        return;
    memset(img, 40, (size_t)w * h * 3);
    for (int32_t r = 0; r < kRows; r++)
        for (int32_t c = 0; c < kCols; c++) {
            const int32_t idx = r < 2 ? kSetBase[0] + r * 2 * SET + c : LOOKS_SPRITE_FIRST + (r - 2) * SET + c;
            const SpriteGraphics *g = table()[idx];
            if (!g || !g->sprData)
                continue;
            for (int32_t y = 0; y < g->nlines && y < kCh; y++)
                for (int32_t x = 0; x < W; x++) {
                    const uint8_t v = g->sprData[y * W + x];
                    uint8_t rgb[3];
                    if (!v)
                        continue;
                    rgb[0] = (uint8_t)(((_gamePalette[v] >> 8) & 15) * 17);
                    rgb[1] = (uint8_t)(((_gamePalette[v] >> 4) & 15) * 17);
                    rgb[2] = (uint8_t)((_gamePalette[v] & 15) * 17);
                    for (int32_t dy = 0; dy < kS; dy++)
                        for (int32_t dx = 0; dx < kS; dx++)
                            memcpy(img + (((size_t)(r * kCh + y) * kS + dy) * w + (c * kCw + x) * kS + dx) * 3, rgb, 3);
                }
        }
    stbi_write_png(path, w, h, 3, img, w * 3);
    free(img);
}

const SpriteGraphics *PlayerLooks_Graphics(const Sprite *s, const SpriteGraphics *g)
{
    if (!g)
        return g;
    const int16_t image = PlayerLooks_Image(s, g->ordinal);
    if (image == g->ordinal)
        return g;
    dump_sets();
    return table()[image];
}

/* ---- the menus' face icons ---- */

static SpriteGraphics s_icon[LOOKS_VARIANTS];
static uint8_t        s_iconPix[LOOKS_VARIANTS][16 * 16];
static const SpriteGraphics *s_iconSource[LOOKS_VARIANTS];

int16_t PlayerLooks_MenuIcon(uint16_t face, int32_t look)
{
    if (look <= LOOK_NONE || look >= LOOK_COUNT)
        return Face_MenuIcon(face);
    const int32_t dark = (face & 3) >= 2, v = (look - 1) * 2 + dark;
    const SpriteGraphics *src = table()[dark ? 188 : 186];        /* (the black / the white face) */
    if (!src || !src->sprData || src->wquads != 1 || src->nlines > 16 || src->nlines < 4)
        return Face_MenuIcon(face);
    const int16_t slot = (int16_t)(LOOKS_MENU_ICON_FIRST + v);
    if (s_iconSource[v] != src) {
        uint8_t *px = s_iconPix[v];
        memcpy(px, src->sprData, (size_t)src->nlines * W);
        if (look == LOOK_RECEDING)
            icon_receding(px, src->nlines);
        else if (look == LOOK_WHITE)
            whiten(px, 3 * W, 1);
        else
            for (int32_t y = 0; y < 8 && y < src->nlines; y++)
                memcpy(px + y * W, g_looksArtIcons[dark][y], W);
        s_icon[v] = *src;
        s_icon[v].sprData = px;
        s_icon[v].ordinal = slot;
        table()[slot] = &s_icon[v];
        s_iconSource[v] = src;
    }
    return slot;
}

/* ---- the line-up's little players ---- */

static uint16_t       s_littleSrc[LOOKS_LITTLE_SLOTS];
static SpriteGraphics s_little[LOOKS_LITTLE_SLOTS];
static uint8_t        s_littlePix[LOOKS_LITTLE_SLOTS][32 * 32];
static const SpriteGraphics *s_littleFrom[LOOKS_LITTLE_SLOTS];

/* A little player without the top of his hair (the receding look's rule over its few rows). */
static uint16_t little_receding(uint16_t srcSprite)
{
    const SpriteGraphics *src = table()[srcSprite];
    if (!src || !src->sprData)
        return srcSprite;
    const int32_t pitch = src->wquads * 16, nl = src->nlines;
    if ((size_t)pitch * (size_t)nl > sizeof s_littlePix[0])
        return srcSprite;
    int32_t k = 0;
    while (k < LOOKS_LITTLE_SLOTS && s_littleSrc[k] && s_littleSrc[k] != srcSprite + 1)
        k++;
    if (k == LOOKS_LITTLE_SLOTS)
        return srcSprite;
    if (s_littleFrom[k] != src) {
        uint8_t *px = s_littlePix[k];
        int32_t y0 = -1;
        memcpy(px, src->sprData, (size_t)pitch * (size_t)nl);
        for (int32_t y = 0; y < nl && y0 < 0; y++)
            for (int32_t x = 0; x < pitch; x++)
                if (is_hair(px[y * pitch + x])) { y0 = y; break; }
        if (y0 >= 0)
            for (int32_t x = 0; x < pitch; x++)
                if (is_hair(px[y0 * pitch + x]))
                    px[y0 * pitch + x] = 6;
        s_little[k] = *src;
        s_little[k].sprData = px;
        s_little[k].ordinal = (int16_t)(LOOKS_LITTLE_FIRST + k);
        table()[LOOKS_LITTLE_FIRST + k] = &s_little[k];
        s_littleFrom[k] = src;
        s_littleSrc[k] = (uint16_t)(srcSprite + 1);
    }
    return (uint16_t)(LOOKS_LITTLE_FIRST + k);
}

uint16_t PlayerLooks_LittlePlayer(int32_t look, uint16_t srcSprite, uint16_t *colTable)
{
    if (JagGfx_Enabled())
        return srcSprite;
    if (look == LOOK_WHITE) {
        colTable[13] = colTable[9] = 2;                 /* white, its darkest shade grey */
        colTable[12] = 1;
    } else if (look == LOOK_SHAVED) {
        colTable[13] = colTable[9] = colTable[6];       /* a light skull in his own skin */
        colTable[12] = colTable[5];
    } else if (look == LOOK_RECEDING) {
        return little_receding(srcSprite);
    }
    return srcSprite;
}

#else /* ---- the Amiga ---- */

/* The Amiga's sprites are planar in chip RAM, one word wide, a line's planes together
 * (line * planes + plane), and the blitter draws them through a mask the face sets share. A
 * team's six sets would be ~200 KB of chip RAM; instead (docs/WORLD_2026_27.md §6l):
 *   - each team's raw set is copied to FAST RAM before it is recoloured (as SDL's);
 *   - after the team's sets are, only the variants its players wear are made, in fast RAM,
 *     WITHIN the frame's own silhouette -- so the shared mask stays right: no pixel is added
 *     (Marco's head where the frame has none is left out), none is removed (the white hair's
 *     end pixels stay grey, the receding icon keeps its outline);
 *   - a player drawn with a look has a small CHIP slot of his own (one frame, 384 bytes; 20
 *     players: 7.5 KB), filled from fast RAM when his frame changes; he is blitted from it with
 *     his frame's own mask and ordinal (its depth: display.h spriteNumPlanes).
 * Replays (the highlights) record the frame's own ordinal: they show the plain face. */
#include "swos_sections.h"

enum { POOL_WORDS = 10 * 1024, SLOTS = 20, SLOT_WORDS = HMAX * 8 };

static uint16_t       s_raw[2][POOL_WORDS];                 /* (fast RAM: the raw sets) */
static uint16_t       s_var[2][LOOKS_VARIANTS][POOL_WORDS]; /* (fast RAM: the looks worn) */
static uint16_t       s_off[2][SET];                        /* a frame's first word in both */
static uint8_t        s_words[2][SET];
static uint8_t        s_haveRaw[2], s_varReady[2][LOOKS_VARIANTS];
SWOS_CHIP_BSS static uint16_t s_slot[SLOTS][SLOT_WORDS];   /* what the blitter reads */
static SpriteGraphics s_slotHdr[SLOTS];
static int32_t        s_slotKey[SLOTS];

static uint16_t colour_planes(int16_t ordinal)
{
    return spriteIsPadded8(ordinal) ? SPRITE_NUM_PLANES : spriteNumPlanes(ordinal);
}

static void decode(const uint16_t *w, int32_t nl, uint16_t sp, uint16_t cp, uint8_t *px)
{
    for (int32_t y = 0; y < nl; y++)
        for (int32_t x = 0; x < W; x++) {
            uint8_t c = 0;
            for (uint16_t p = 0; p < cp; p++)
                c |= (uint8_t)(((w[y * sp + p] >> (15 - x)) & 1) << p);
            px[y * W + x] = c;
        }
}

static void encode(const uint8_t *px, int32_t nl, uint16_t sp, uint16_t cp, uint16_t *w)
{
    for (int32_t y = 0; y < nl; y++)
        for (uint16_t p = 0; p < cp; p++) {
            uint16_t word = 0;
            for (int32_t x = 0; x < W; x++)
                word = (uint16_t)(word | (((px[y * W + x] >> p) & 1) << (15 - x)));
            w[y * sp + p] = word;
        }
}

void PlayerLooks_CopySets(void)
{
    for (int32_t k = 0; k < SLOTS; k++)
        s_slotKey[k] = -1;
    for (int32_t t = 0; t < 2; t++) {
        uint32_t used = 0;
        int32_t i;
        s_haveRaw[t] = 0;
        for (int32_t v = 0; v < LOOKS_VARIANTS; v++)
            s_varReady[t][v] = 0;
        for (i = 0; i < SET; i++) {
            const SpriteGraphics *src = table()[kSetBase[t] + i];
            if (!src || !src->sprData || src->wquads != 1 || src->nlines > HMAX)
                break;
            const uint32_t n = (uint32_t)src->nlines * spriteNumPlanes(src->ordinal);
            if (used + n > POOL_WORDS)
                break;
            s_off[t][i] = (uint16_t)used;
            s_words[t][i] = (uint8_t)n;
            for (uint32_t k = 0; k < n; k++)
                s_raw[t][used + k] = ((const uint16_t *)src->sprData)[k];
            used += n;
        }
        s_haveRaw[t] = i == SET;
    }
}

void PlayerLooks_ConvertSets(int32_t t, uint16_t *colorTable)
{
    extern void ConvertColorTableToSkinColor(uint16_t color, uint16_t *destTable);
    extern TeamGame *_topTeamPtr, *_bottomTeamPtr;
    const TeamGame *team = t == 0 ? _topTeamPtr : _bottomTeamPtr;
    uint8_t need[LOOKS_VARIANTS] = { 0 };
    static uint8_t raw[HMAX * W], px[HMAX * W];
    if (t < 0 || t > 1 || !s_haveRaw[t] || !team)
        return;
    for (int32_t i = 1; i < 16; i++) {                  /* (the looks this side wears; keepers keep their hair) */
        const int32_t look = PlayerLooks_InMatch(team, &team->players[i]);
        if (look != LOOK_NONE)
            need[(look - 1) * 2 + (team->players[i].face == 2)] = 1;
    }
    for (int32_t v = 0; v < LOOKS_VARIANTS; v++) {
        if (!need[v])
            continue;
        const int32_t look = v / 2 + 1, dark = v & 1;
        uint16_t tab[16];
        for (int32_t k = 0; k < 16; k++)
            tab[k] = colorTable[k];
        ConvertColorTableToSkinColor(dark ? 2 : 0, tab);
        for (int32_t f = 0; f < SET; f++) {
            const SpriteGraphics *g = table()[kSetBase[t] + f];
            const int32_t nl = g->nlines;
            const uint16_t sp = spriteNumPlanes(g->ordinal), cp = colour_planes(g->ordinal);
            decode(&s_raw[t][s_off[t][f]], nl, sp, cp, raw);
            for (int32_t k = 0; k < nl * W; k++)
                px[k] = raw[k];
            apply_look(px, nl, f, look, dark);
            for (int32_t k = 0; k < nl * W; k++) {
                if (!raw[k])
                    px[k] = 0;                          /* (the silhouette is the mask's) */
                else if (!px[k])
                    px[k] = look == LOOK_WHITE ? 1 : 6;
                if (px[k])
                    px[k] = (uint8_t)tab[px[k] & 15];
            }
            uint16_t *dst = &s_var[t][v][s_off[t][f]];
            for (uint32_t k = 0; k < s_words[t][f]; k++)
                dst[k] = 0;                             /* (a padded frame's planes 4..7: zeros) */
            encode(px, nl, sp, cp, dst);
        }
        s_varReady[t][v] = 1;
    }
}

int16_t PlayerLooks_Image(const Sprite *s, int16_t image) { (void)s; return image; }

const SpriteGraphics *PlayerLooks_Graphics(const Sprite *s, const SpriteGraphics *g)
{
    extern Sprite  **_team1SpritesTable, **_team2SpritesTable;
    extern TeamGame *_topTeamPtr, *_bottomTeamPtr;
    extern uint16_t  _useIndividualPlayerSkinColor;
    if (!s || !g || !_useIndividualPlayerSkinColor || !_team1SpritesTable || !_team2SpritesTable)
        return g;
    int32_t t = -1, slot = -1;
    for (int32_t i = 1; i < 11 && t < 0; i++) {         /* (0, the goalkeeper, keeps his hair) */
        if (_team1SpritesTable[i] == s) { t = 0; slot = i; }
        else if (_team2SpritesTable[i] == s) { t = 1; slot = i; }
    }
    if (t < 0)
        return g;
    const TeamGame *team = t == 0 ? _topTeamPtr : _bottomTeamPtr;
    const PlayerInfo *pi = team ? &team->players[slot] : NULL;
    const int32_t look = PlayerLooks_InMatch(team, pi);
    if (look == LOOK_NONE)
        return g;
    const int16_t image = g->ordinal, f3 = Face3_Set(t);
    int32_t f;
    if (image >= kSetBase[t] && image < kSetBase[t] + 3 * SET)
        f = (image - kSetBase[t]) % SET;
    else if (f3 >= 0 && image >= f3 && image < f3 + SET)
        f = image - f3;
    else
        return g;
    const int32_t v = (look - 1) * 2 + (pi->face == 2);
    if (!s_varReady[t][v] || g->nlines != table()[kSetBase[t] + f]->nlines)
        return g;
    const int32_t k = t * 10 + slot - 1, key = (v << 16) | (f << 8) | image;
    if (s_slotKey[k] != key) {                          /* (his frame changed: the chip copy) */
        const uint16_t *src = &s_var[t][v][s_off[t][f]];
        for (uint32_t i = 0; i < s_words[t][f] && i < SLOT_WORDS; i++)
            s_slot[k][i] = src[i];
        s_slotKey[k] = key;
    }
    s_slotHdr[k] = *g;                                  /* (its mask, its ordinal: its depth) */
    s_slotHdr[k].sprData = (uint8_t *)s_slot[k];
    return &s_slotHdr[k];
}

/* The menus' face icons: the look over the white / black face, in its own outline. */
static SpriteGraphics s_icon[LOOKS_VARIANTS];
SWOS_CHIP_BSS static uint16_t s_iconPix[LOOKS_VARIANTS][16 * 8];
static const SpriteGraphics *s_iconSource[LOOKS_VARIANTS];

int16_t PlayerLooks_MenuIcon(uint16_t face, int32_t look)
{
    static uint8_t px[HMAX * W], was[HMAX * W];
    if (look <= LOOK_NONE || look >= LOOK_COUNT)
        return Face_MenuIcon(face);
    const int32_t dark = (face & 3) >= 2, v = (look - 1) * 2 + dark;
    const SpriteGraphics *src = table()[dark ? 188 : 186];
    if (!src || !src->sprData || src->wquads != 1 || src->nlines > HMAX || src->nlines < 4)
        return Face_MenuIcon(face);
    const uint16_t sp = spriteNumPlanes(src->ordinal);
    if ((uint32_t)src->nlines * sp > sizeof s_iconPix[0] / 2 || sp < 4)
        return Face_MenuIcon(face);
    const int16_t slot = (int16_t)(LOOKS_ICON_FIRST + v);
    if (s_iconSource[v] != src) {
        const int32_t nl = src->nlines;
        decode((const uint16_t *)src->sprData, nl, sp, 4, was);
        for (int32_t k = 0; k < nl * W; k++)
            px[k] = was[k];
        if (look == LOOK_RECEDING)
            icon_receding(px, nl);
        else if (look == LOOK_WHITE)
            whiten(px, 3 * W, 1);
        else
            for (int32_t y = 0; y < 8 && y < nl; y++)
                for (int32_t x = 0; x < W; x++)
                    px[y * W + x] = g_looksArtIcons[dark][y][x];
        for (int32_t k = 0; k < nl * W; k++)            /* (its outline: the menus' mask) */
            if (!was[k])
                px[k] = 0;
            else if (!px[k])
                px[k] = was[k];
        for (uint32_t k = 0; k < (uint32_t)nl * sp; k++)
            s_iconPix[v][k] = ((const uint16_t *)src->sprData)[k];
        encode(px, nl, sp, 4, s_iconPix[v]);
        s_icon[v] = *src;
        s_icon[v].sprData = (uint8_t *)s_iconPix[v];
        s_icon[v].ordinal = slot;                       /* (4 planes deep, as the face's) */
        table()[slot] = &s_icon[v];
        s_iconSource[v] = src;
    }
    return slot;
}

uint16_t PlayerLooks_LittlePlayer(int32_t look, uint16_t srcSprite, uint16_t *colTable)
{
    if (look == LOOK_WHITE) {
        colTable[13] = colTable[9] = 2;
        colTable[12] = 1;
    } else if (look == LOOK_SHAVED) {
        colTable[13] = colTable[9] = colTable[6];
        colTable[12] = colTable[5];
    }
    return srcSprite;                                   /* (receding needs a copy of the sprite: SDL's) */
}

#endif

int16_t PlayerLooks_PlayerMenuIcon(const PlayerFile *pf)
{
    const uint16_t face = (uint16_t)((pf->positionAndFace & PF_FACE_MASK) >> 3);
    return PlayerLooks_MenuIcon(face, PlayerLooks_Of(pf));
}
