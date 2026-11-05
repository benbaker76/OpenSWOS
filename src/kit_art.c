/* kit_art.c -- the kit drawings of the menus, in the Amiga's art or the Jaguar's.
 * include/kit_art.h says what it is for; docs/UNIFORM.md has the screens.
 *
 * THE AMIGA'S. One shirt sprite (190) serves all four shirt types: its pixels are in
 * eight colours, one per region of the shirt, and a type is which regions take the
 * second colour (_shirtTypesTable, by way of _shirtsIndexTable -- the same two tables
 * the EDIT TEAM screen uses, so the two screens cannot disagree). Shorts (191) and
 * socks (192) are drawn in colour 11. The icons (199-202) are drawn as they are.
 *
 * THE JAGUAR'S. Its kit art has a shirt per type and draws a colour as a ramp of four
 * shades, in the same sixteen kit entries its sprites use (240-255: two shirt colours,
 * shorts, socks). A menu has no such entries, so every team colour's four shades are
 * written into entries the menus leave unused (192-231) while a kit screen is up, and
 * the icons' greys into the sixteen after them.
 *
 * Both are read as pixels and written to the frame directly, so nothing here changes a
 * sprite: the EDIT TEAM screen's own scratch sprites (193-198) are left alone. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kit_art.h"
#include "display.h"
#include "jag_graphics.h"
#include "swos_io.h"
#include "team_colors.h"

extern uint16_t  _menuPalette[256];
extern uint16_t  _kTeamColorsTable[KIT_COLOURS];
extern uint16_t  _shirtTypesTable[32];
extern uint16_t  _shirtsIndexTable[8];
extern uint16_t  _littlePlayersSpriteTable[4];
extern void     *_spriteGraphicsPtr;
extern volatile uintptr_t _frontbufferBasePtr;
extern void      FillSkinColorConversionTable(uint16_t skinColor, uint16_t *convertTable);

enum {
    kSprShirt = 190, kSprShorts = 191, kSprSocks = 192, kSprIcon0 = 199,
    kShadow = 8,                    /* SWOS's shadow colour, in both sets of art */
    kTeamColours = KIT_COLOURS,
    kLendFirst = 192,               /* 11 colours x 4 shades ... */
    kGreyFirst = kLendFirst + kTeamColours * JAG_KIT_SHADES,   /* ... then the icons' greys */
    kJagGreyFirst = 97, kJagGreys = 16,
    kLent = kTeamColours * JAG_KIT_SHADES + kJagGreys,
    kSockGap = 4,                   /* between the Jaguar's two socks, drawn as a pair */
};

/* JAGKITS.BIN, in the order scripts/make_jag_graphics.py writes it. */
enum { kJagShirt0, kJagIcon0 = 4, kJagShorts = 8, kJagSockL, kJagSockR, kJagGraphics };

static int32_t  s_jaguar;
static int32_t  s_jagLoaded;                    /* 1 ok, -1 failed, 0 not tried */
static uint8_t  s_jagFile[4 + 768 + 1 + kJagGraphics * 2 + 16384];
static struct { int32_t w, h; const uint8_t *px; } s_jag[kJagGraphics];
static uint16_t s_saved[kLent];
static int32_t  s_lent;

void KitArt_SetJaguar(int32_t on) { s_jaguar = on != 0; }

static int32_t jag_load(void)
{
    FILE *f;
    size_t got, at;
    int32_t i;

    if (s_jagLoaded)
        return s_jagLoaded > 0;
    s_jagLoaded = -1;
    {
        char anchored[1024];        /* the data root is not the working directory everywhere */
        f = fopen(swos_data_path("grafs_jag/JAGKITS.BIN", anchored, (uint32_t)sizeof anchored), "rb");
    }
    if (!f) {
        fprintf(stderr, "[kit] grafs_jag/JAGKITS.BIN is missing: the Amiga's kit art is used\n");
        return 0;
    }
    got = fread(s_jagFile, 1, sizeof s_jagFile, f);
    fclose(f);
    at = 4 + 768 + 1;
    if (got < at || memcmp(s_jagFile, "JGK1", 4) != 0 || s_jagFile[4 + 768] != kJagGraphics) {
        fprintf(stderr, "[kit] grafs_jag/JAGKITS.BIN is not a kit file of %d graphics\n", kJagGraphics);
        return 0;
    }
    for (i = 0; i < kJagGraphics; i++) {
        if (at + 2 > got)
            return 0;
        s_jag[i].w = s_jagFile[at];
        s_jag[i].h = s_jagFile[at + 1];
        s_jag[i].px = s_jagFile + at + 2;
        at += 2 + (size_t)s_jag[i].w * s_jag[i].h;
        if (at > got)
            return 0;
    }
    s_jagLoaded = 1;
    return 1;
}

/* The Jaguar's art, when asked for AND there to be drawn. */
int32_t KitArt_Jaguar(void) { return s_jaguar && jag_load(); }

void KitArt_Lend(void)
{
    const uint8_t *pal = s_jagFile + 4;
    int32_t c, shade, i;

    if (s_lent || !KitArt_Jaguar())
        return;
    memcpy(s_saved, &_menuPalette[kLendFirst], sizeof s_saved);
    for (c = 0; c < kTeamColours; c++) {
        uint16_t rgb = _menuPalette[_kTeamColorsTable[c]];
        for (shade = 0; shade < JAG_KIT_SHADES; shade++) {
            int32_t k = JagGfx_KitShade(shade);
            _menuPalette[kLendFirst + c * JAG_KIT_SHADES + shade] = (uint16_t)(
                ((((rgb >> 8) & 15) * k) >> 4) << 8 |
                ((((rgb >> 4) & 15) * k) >> 4) << 4 |
                (((rgb & 15) * k) >> 4));
        }
    }
    for (i = 0; i < kJagGreys; i++) {
        const uint8_t *e = pal + (kJagGreyFirst + i) * 3;
        _menuPalette[kGreyFirst + i] = (uint16_t)((e[0] >> 4) << 8 | (e[1] >> 4) << 4 | e[2] >> 4);
    }
    WriteCopperPalette(_menuPalette);
    s_lent = 1;
}

void KitArt_Return(void)
{
    if (!s_lent)
        return;
    memcpy(&_menuPalette[kLendFirst], s_saved, sizeof s_saved);
    WriteCopperPalette(_menuPalette);
    s_lent = 0;
}

void KitArt_FromTeam(const TeamFile *team, int32_t second, KitLook *out)
{
    if (second) {
        out->type = team->secShirtType;     out->stripes = team->secStripesColor;
        out->basic = team->secBasicColor;   out->shorts = team->secShortsColor;
        out->socks = team->secSocksColor;
    } else {
        out->type = team->prShirtType;      out->stripes = team->prStripesColor;
        out->basic = team->prBasicColor;    out->shorts = team->prShortsColor;
        out->socks = team->prSocksColor;
    }
    /* A team file is data: keep what is drawn inside the tables it indexes. */
    out->type &= 3;
    out->stripes %= kTeamColours;   out->basic %= kTeamColours;
    out->shorts %= kTeamColours;    out->socks %= kTeamColours;
}

void KitArt_ToTeam(const KitLook *look, int32_t second, TeamFile *team)
{
    if (second) {
        team->secShirtType = look->type;    team->secStripesColor = look->stripes;
        team->secBasicColor = look->basic;  team->secShortsColor = look->shorts;
        team->secSocksColor = look->socks;
    } else {
        team->prShirtType = look->type;     team->prStripesColor = look->stripes;
        team->prBasicColor = look->basic;   team->prShortsColor = look->shorts;
        team->prSocksColor = look->socks;
    }
}

/* ---- drawing ---- */

static void plot(int32_t x, int32_t y, uint8_t colour)
{
    uint8_t *fb = (uint8_t *)_frontbufferBasePtr + _menu_draw_x_offset;
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT)
        return;
    fb[(uint32_t)y * FRONTBUFFER_ROW_STRIDE_BYTES + x] = colour;
}

static const SpriteGraphics *sprite(int32_t index)
{
    return ((SpriteGraphics **)_spriteGraphicsPtr)[index];
}

/* A menu sprite through a 16-entry colour table; colour 0 is not drawn. */
static void draw_sprite(int32_t index, const uint16_t *table, int32_t x, int32_t y)
{
    const SpriteGraphics *spr = sprite(index);
    int32_t pitch = spr->wquads * 16, row, col;

    for (row = 0; row < spr->nlines; row++)
        for (col = 0; col < spr->pixWidth; col++) {
            uint8_t c = spr->sprData[row * pitch + col];
            if (c)
                plot(x + col, y + row, (uint8_t)(c < 16 ? table[c] : c));
        }
}

static void identity(uint16_t *table)
{
    int32_t i;
    for (i = 0; i < 16; i++)
        table[i] = (uint16_t)i;
}

/* The Jaguar's: a kit pixel is (slot, shade); the slot's colour comes from the look. */
static void draw_jag(int32_t graphic, const KitLook *look, int32_t x, int32_t y)
{
    const uint8_t slot[JAG_KIT_SLOTS] = { look->stripes, look->basic, look->shorts, look->socks };
    int32_t row, col;

    for (row = 0; row < s_jag[graphic].h; row++)
        for (col = 0; col < s_jag[graphic].w; col++) {
            uint8_t c = s_jag[graphic].px[row * s_jag[graphic].w + col];
            if (!c)
                continue;
            if (c >= JAG_KIT_FIRST)
                c = (uint8_t)(kLendFirst + slot[(c - JAG_KIT_FIRST) / JAG_KIT_SHADES] * JAG_KIT_SHADES +
                              (c - JAG_KIT_FIRST) % JAG_KIT_SHADES);
            else if (c >= kJagGreyFirst && c < kJagGreyFirst + kJagGreys)
                c = (uint8_t)(kGreyFirst + (c - kJagGreyFirst));
            else if (c != kShadow && c != 1)
                continue;                   /* nothing else is in the kit art */
            plot(x + col, y + row, c);
        }
}

static int32_t player_sprite(const KitLook *look) { return _littlePlayersSpriteTable[look->type & 3]; }

void KitArt_Size(int32_t part, int32_t *w, int32_t *h)
{
    static const KitLook plain;
    int32_t jag = KitArt_Jaguar(), index = -1;

    *w = *h = 0;
    switch (part) {
    case KIT_ART_SHIRT:
        if (jag) { *w = s_jag[kJagShirt0 + 2].w; *h = s_jag[kJagShirt0 + 2].h; }   /* the tallest */
        else index = kSprShirt;
        break;
    case KIT_ART_SHORTS:
        if (jag) { *w = s_jag[kJagShorts].w; *h = s_jag[kJagShorts].h; }
        else index = kSprShorts;
        break;
    case KIT_ART_SOCKS:
        if (jag) { *w = s_jag[kJagSockL].w + kSockGap + s_jag[kJagSockR].w; *h = s_jag[kJagSockL].h; }
        else index = kSprSocks;
        break;
    case KIT_ART_ICON:
        if (jag) { *w = s_jag[kJagIcon0].w; *h = s_jag[kJagIcon0].h; }
        else index = kSprIcon0;
        break;
    case KIT_ART_PLAYER:
        index = player_sprite(&plain);
        break;
    }
    if (index >= 0) {
        *w = sprite(index)->pixWidth;
        *h = sprite(index)->nlines;
    }
}

void KitArt_Draw(int32_t part, const KitLook *look, int32_t x, int32_t y, int32_t w, int32_t h)
{
    uint16_t table[16];
    int32_t jag = KitArt_Jaguar(), pw, ph, i;
    uint16_t stripes = _kTeamColorsTable[look->stripes % kTeamColours];
    uint16_t basic   = _kTeamColorsTable[look->basic % kTeamColours];

    KitArt_Size(part, &pw, &ph);
    if (jag && part == KIT_ART_SHIRT)
        ph = s_jag[kJagShirt0 + (look->type & 3)].h;
    x += (w - pw) / 2;
    y += (h - ph) / 2;
    identity(table);

    switch (part) {
    case KIT_ART_SHIRT:
        if (jag) { draw_jag(kJagShirt0 + (look->type & 3), look, x, y); break; }
        for (i = 0; i < 8; i++)
            table[_shirtsIndexTable[i] / 2] =
                _shirtTypesTable[(look->type & 3) * 8 + i] == 2 ? basic : stripes;
        draw_sprite(kSprShirt, table, x, y);
        break;
    case KIT_ART_SHORTS:
        if (jag) { draw_jag(kJagShorts, look, x, y); break; }
        table[11] = _kTeamColorsTable[look->shorts % kTeamColours];
        draw_sprite(kSprShorts, table, x, y);
        break;
    case KIT_ART_SOCKS:
        if (jag) {
            draw_jag(kJagSockL, look, x, y);
            draw_jag(kJagSockR, look, x + s_jag[kJagSockL].w + kSockGap, y);
            break;
        }
        table[11] = _kTeamColorsTable[look->socks % kTeamColours];
        draw_sprite(kSprSocks, table, x, y);
        break;
    case KIT_ART_ICON:
        if (jag) draw_jag(kJagIcon0 + (look->type & 3), look, x, y);
        else     draw_sprite(kSprIcon0 + (look->type & 3), table, x, y);
        break;
    case KIT_ART_PLAYER: {
        /* As the stadium screen dresses its little players (StadiumPlayerSpritesBeforeDraw):
         * the type-0 shirt is all one colour, and vertical stripes swap the two. */
        uint16_t col1 = stripes, col2 = look->type == SHIRT_ORDINARY ? stripes : basic;
        if (look->type == SHIRT_VERTICAL_STRIPES) { uint16_t t = col1; col1 = col2; col2 = t; }
        table[11] = col1;
        table[10] = col2;
        table[14] = _kTeamColorsTable[look->shorts % kTeamColours];
        table[15] = _kTeamColorsTable[look->socks % kTeamColours];
        FillSkinColorConversionTable(0, table);
        draw_sprite(player_sprite(look), table, x, y);
        break;
    }
    }
}
