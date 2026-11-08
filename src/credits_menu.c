/*
 * credits_menu.c -- the CREDITS screen (SDL builds), from the main menu's CREDITS button
 * (menus.c CreditsMenuSelected). The Amiga AGA screen is menus.c CreditsAgaDraw; both
 * draw the same words, include/credits_text.h.
 *
 * The menu is JSON only (bin/data_json/menus/openswos/creditsMenu.json, docs/MENUS.md
 * section 10): a header and the exit button. Everything between them is drawn here every
 * frame (the menu is flagged dos_redraw): the lines of credits_text.h, and at its
 * CREDITS_PHOTOS marker the developer photos with their names under them.
 *
 * The photos come from bin/data/credits_icons.bmp, made by scripts/make_credits_icons.py:
 * one 8-bit strip, 40 px a photo, whose pixels are already palette indices 128..255. The
 * menu screens use only 0..31, so those entries are LENT to the photos while this screen
 * is up and given back when it closes.
 */
#include "credits_menu.h"

#ifdef SWOS_SDL_BUILD

#include "menu_load_json.h"
#include "menu_symbols.h"
#include "menu_system.h"
#include "display.h"        /* FRONTBUFFER_ROW_STRIDE_BYTES */
#include "text_render.h"    /* GetTextWidth, DrawMenuTextCentered */
#include "credits_text.h"

#include <stdio.h>
#include <string.h>

/* The menu's entries, in the order creditsMenu.json lists them. */
enum {
    CreditsMenu_header,
    CreditsMenu_exit,
    CreditsMenu_kNumEntries
};

extern uint16_t _menuPalette[256];
extern void     WriteCopperPalette(const uint16_t *palette);
extern volatile uintptr_t _frontbufferBasePtr;
extern int      _menu_draw_x_offset;
extern void    *_charTables[];
extern void     SetExitMenuFlag(void);

enum {
    kCreditPhoto    = 40,          /* px, square */
    kCreditStep     = 56,          /* photo + 16 px: six across the 336 px menu (the names stagger) */
    kCreditTextY    = 20,          /* the first line, under the header */
    kCreditPalBase  = 128,         /* the lent block: 128..255 */
    kCreditShadow   = 3,           /* the menu palette's black, for the 1 px drop shadow */
};

/* The photos shown, in the order drawn: where each is in the strip, and its name. */
static const struct { uint8_t strip; const char *name; } k_creditPhotos[] = {
    { CREDITS_STRIP_BENBAKER76, "BENBAKER76" },
    { CREDITS_STRIP_ZKZ,        "ZKZ"        },
    { CREDITS_STRIP_PLAYAVELI,  "PLAYAVELI"  },
    { CREDITS_STRIP_STARWINDZ,  "STARWINDZ"  },
    { CREDITS_STRIP_THD79,      "THD79"      },
    { CREDITS_STRIP_REDHAIR,    "REDHAIR"    },
};
enum { kCreditPhotos = sizeof k_creditPhotos / sizeof k_creditPhotos[0] };

static uint8_t  s_creditPixels[kCreditPhoto][kCreditPhoto * CREDITS_STRIP_COUNT];
static uint16_t s_creditPal[256 - kCreditPalBase];     /* 12-bit, the photos' */
static uint16_t s_creditSaved[256 - kCreditPalBase];   /* what they borrowed */
static int32_t  s_creditLoaded;                         /* 1 ok, -1 failed, 0 not tried */

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* An uncompressed 8-bit BMP of exactly the strip's size; anything else is refused. */
static int32_t credits_load(void)
{
    enum { W = kCreditPhoto * CREDITS_STRIP_COUNT, H = kCreditPhoto };
    uint8_t hdr[54], pal[256 * 4], row[(W + 3) & ~3];
    FILE *f;
    int32_t y, i;

    if (s_creditLoaded)
        return s_creditLoaded > 0;
    s_creditLoaded = -1;
    f = fopen("data/credits_icons.bmp", "rb");
    if (!f) {
        fprintf(stderr, "[credits] data/credits_icons.bmp is missing: no photos\n");
        return 0;
    }
    if (fread(hdr, 1, sizeof hdr, f) != sizeof hdr || hdr[0] != 'B' || hdr[1] != 'M' ||
        (int32_t)le32(hdr + 18) != W || (int32_t)le32(hdr + 22) != H ||
        (hdr[28] | hdr[29] << 8) != 8 || le32(hdr + 30) != 0) {
        fprintf(stderr, "[credits] data/credits_icons.bmp is not a %dx%d 8-bit BMP\n", W, H);
        fclose(f);
        return 0;
    }
    fseek(f, 14 + (long)le32(hdr + 14), SEEK_SET);          /* the palette follows the info header */
    if (fread(pal, 4, 256, f) != 256) {
        fclose(f);
        return 0;
    }
    for (i = kCreditPalBase; i < 256; i++)                  /* BGRA, 8-bit -> 12-bit */
        s_creditPal[i - kCreditPalBase] = (uint16_t)((pal[i * 4 + 2] >> 4) << 8 |
                                                     (pal[i * 4 + 1] >> 4) << 4 | pal[i * 4] >> 4);
    fseek(f, (long)le32(hdr + 10), SEEK_SET);
    for (y = H - 1; y >= 0; y--) {                          /* bottom-up rows */
        if (fread(row, 1, sizeof row, f) != sizeof row) {
            fclose(f);
            return 0;
        }
        memcpy(s_creditPixels[y], row, W);
    }
    fclose(f);
    s_creditLoaded = 1;
    return 1;
}

/* The photos, their tops at y, centred on cx; returns the height they and their names
 * take. When two neighbouring names would run into each other, every other one drops a
 * line. */
static int32_t draw_photos(int32_t cx, int32_t y)
{
    uint8_t *fb = (uint8_t *)_frontbufferBasePtr + _menu_draw_x_offset;
    int32_t p, row, x0 = cx - ((kCreditPhotos - 1) * kCreditStep + kCreditPhoto) / 2, stagger = 0;

    for (p = 1; p < kCreditPhotos; p++)
        if ((GetTextWidth((uint8_t *)k_creditPhotos[p - 1].name, (uint8_t *)_charTables[0]) +
             GetTextWidth((uint8_t *)k_creditPhotos[p].name, (uint8_t *)_charTables[0])) / 2 > kCreditStep - 4)
            stagger = 1;
    if (credits_load()) {
        for (p = 0; p < kCreditPhotos; p++) {
            int32_t px = x0 + p * kCreditStep;
            const uint8_t *src = &s_creditPixels[0][k_creditPhotos[p].strip * kCreditPhoto];
            /* A 1 px drop shadow, right and down; the photos are full squares. */
            for (row = 0; row < kCreditPhoto; row++)
                memset(fb + (uint32_t)(y + row + 1) * FRONTBUFFER_ROW_STRIDE_BYTES + px + 1,
                       kCreditShadow, kCreditPhoto);
            for (row = 0; row < kCreditPhoto; row++)
                memcpy(fb + (uint32_t)(y + row) * FRONTBUFFER_ROW_STRIDE_BYTES + px,
                       src + row * sizeof s_creditPixels[0], kCreditPhoto);
        }
    }
    for (p = 0; p < kCreditPhotos; p++)
        DrawMenuTextCentered((uint16_t)(x0 + p * kCreditStep + kCreditPhoto / 2),
                             (uint16_t)(y + kCreditPhoto + CREDITS_NAME_DROP +
                                        (stagger && (p & 1) ? CREDITS_NAME_PITCH : 0)),
                             2, (uint8_t *)k_creditPhotos[p].name, (uint8_t *)_charTables[0]);
    return kCreditPhoto + CREDITS_NAME_DROP + CREDITS_NAME_PITCH * (1 + stagger);
}

/* Runs every frame, after the background and before the entries (DrawMenu). The
 * entries' coordinates are the frame buffer's, so the header places everything. */
static void openswosCredits_OnDraw(void)
{
    const MenuEntry *header = CalcMenuEntryAddress(CreditsMenu_header);
    int32_t cx = header->x + header->dwWidth / 2, y = header->y + kCreditTextY;
    uint32_t i;

    for (i = 0; i < sizeof k_creditsLines / sizeof k_creditsLines[0]; i++) {
        const char *line = k_creditsLines[i];
        if (line == CREDITS_PHOTOS)
            y += draw_photos(cx, y);
        else if (!line[0])
            y += CREDITS_GAP_PITCH;
        else {
            DrawMenuTextCentered((uint16_t)cx, (uint16_t)y, 2, (uint8_t *)line, (uint8_t *)_charTables[0]);
            y += CREDITS_LINE_PITCH;
        }
    }
}

static void openswosCredits_exit_OnSelect(void)
{
    SetExitMenuFlag();
}

/* ---- the menu ---- */

#define HANDLER(fn) { #fn, (void *)fn, SYM_FUNC }

static const MenuSymbol k_handlers[] = {
    HANDLER(openswosCredits_OnDraw),
    HANDLER(openswosCredits_exit_OnSelect),
};

#undef HANDLER

void CreditsMenu_Init(void)
{
    static int32_t registered;

    if (registered)
        return;
    registered = 1;
    MenuJson_RegisterSymbols(k_handlers, (int)(sizeof k_handlers / sizeof k_handlers[0]));
}

/* Around ShowMenu rather than in the exit button, so every way out -- ESC included --
 * gives the palette entries back. */
void CreditsMenu_Show(void)
{
    uint8_t *menu;
    int32_t i, lent = 0;

    CreditsMenu_Init();
    menu = MenuJson_Load(CREDITS_MENU_NAME);
    if (!menu) {
        fprintf(stderr, "[credits] cannot show %s\n", CREDITS_MENU_NAME);
        return;
    }
    if (credits_load()) {
        for (i = 0; i < 256 - kCreditPalBase; i++) {
            s_creditSaved[i] = _menuPalette[kCreditPalBase + i];
            _menuPalette[kCreditPalBase + i] = s_creditPal[i];
        }
        WriteCopperPalette(_menuPalette);
        lent = 1;
    }
    ShowMenu(menu);
    if (lent) {
        for (i = 0; i < 256 - kCreditPalBase; i++)
            _menuPalette[kCreditPalBase + i] = s_creditSaved[i];
        WriteCopperPalette(_menuPalette);
    }
}

#endif /* SWOS_SDL_BUILD */
