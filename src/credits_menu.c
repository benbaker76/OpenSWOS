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
#include "gfx.h"            /* the full credits: drawn over the picture */
#include "chat_font.h"      /* ... in the chat's typeface */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

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

/* ---- FULL CREDITS: bin/data/CREDITS.txt scrolling up, as at the end of a film ----
 *
 * The CREDITS screen's FULL CREDITS button opens openswos/fullCreditsMenu: only an exit
 * button. CreditsMenu_DrawOverlay (platform_sdl.c draw_overlays) blacks out the screen above
 * it and draws the file's lines over that in the chat font (chat_font.h), sharp at the
 * window's size, rising at a steady rate and fading in at the bottom and out at the top; when
 * the last line has gone, they start again.
 *
 * The file is the README's credits as they are written: a title alone on a line ("OpenSWOS");
 * "Role<two or more spaces>Names", the names going on in the lines indented under it; and
 * SWOS 2020's own list, "* <a heading>" and "- <role>: <names>" (the role may run on to the
 * indented lines until its ':', the names follow it). A role and its names are drawn either
 * side of the middle when both fit, else the role above its names, all centred. */

enum { FC_GAP, FC_TITLE, FC_HEADING, FC_ROLE, FC_NAME, FC_PAIR };
typedef struct { uint8_t kind; int16_t head; char *a, *b; } FcLine;  /* PAIR: a role, b names; NAME: head = its PAIR or -1 */

static FcLine  *s_fc;
static int32_t  s_fcCount, s_fcCap, s_fcLoaded;
static uint64_t s_fullSince;

static char *fc_dup(const char *p, size_t n)
{
    char *d;
    while (n && (*p == ' ' || *p == '\t')) { p++; n--; }
    while (n && (p[n - 1] == ' ' || p[n - 1] == '\t')) n--;
    d = (char *)malloc(n + 1);
    if (d) { memcpy(d, p, n); d[n] = 0; }
    return d;
}

static void fc_add(uint8_t kind, char *a, char *b)
{
    if (s_fcCount == s_fcCap) {
        FcLine *g = (FcLine *)realloc(s_fc, (size_t)(s_fcCap ? s_fcCap * 2 : 64) * sizeof *g);
        if (!g) return;
        s_fc = g;
        s_fcCap = s_fcCap ? s_fcCap * 2 : 64;
    }
    s_fc[s_fcCount].kind = kind;
    s_fc[s_fcCount].head = -1;
    if (kind == FC_NAME && s_fcCount > 0)       /* the names of the PAIR above go on */
        s_fc[s_fcCount].head = s_fc[s_fcCount - 1].kind == FC_PAIR ? (int16_t)(s_fcCount - 1) :
                               s_fc[s_fcCount - 1].kind == FC_NAME ? s_fc[s_fcCount - 1].head : -1;
    s_fc[s_fcCount].a = a;
    s_fc[s_fcCount].b = b;
    s_fcCount++;
}

/* Long text is cut at spaces into lines of at most kFcWrap characters, so the widest still
 * fits a 4:3 picture; a role's continuation is a ROLE, a names' a NAME of the same head. */
enum { kFcWrap = 50 };

static void fc_add_wrapped(uint8_t kind, char *text)
{
    while (text && strlen(text) > kFcWrap) {
        size_t cut = kFcWrap;
        while (cut > 0 && text[cut] != ' ') cut--;
        if (!cut) break;
        fc_add(kind, fc_dup(text, cut), NULL);
        memmove(text, text + cut + 1, strlen(text + cut + 1) + 1);
    }
    fc_add(kind, text, NULL);
}

/* A role and its first names: a PAIR, or a ROLE alone (then the names under it) when the line
 * has none or either is too long for its column. */
static void fc_role(char *role, char *names)
{
    if (names && names[0] && strlen(role) <= kFcWrap / 2 + 4 && strlen(names) <= kFcWrap)
        fc_add(FC_PAIR, role, names);
    else {
        fc_add_wrapped(FC_ROLE, role);
        if (names && names[0])
            fc_add_wrapped(FC_NAME, names);
        else
            free(names);
    }
}

static int32_t fc_indented(const char *l) { return l[0] == ' ' || l[0] == '\t'; }

static int32_t fc_load(void)
{
    FILE *f;
    char raw[512], **L = NULL;
    int32_t n = 0, cap = 0, i;

    if (s_fcLoaded)
        return s_fcLoaded > 0;
    s_fcLoaded = -1;
    f = fopen("data/CREDITS.txt", "rb");
    if (!f) {
        fprintf(stderr, "[credits] data/CREDITS.txt is missing: no full credits\n");
        return 0;
    }
    while (fgets(raw, sizeof raw, f)) {                  /* the lines, without their ends */
        size_t m = strcspn(raw, "\r\n");
        while (m && (raw[m - 1] == ' ' || raw[m - 1] == '\t')) m--;
        raw[m] = 0;
        if (n == cap) {
            char **g = (char **)realloc(L, (size_t)(cap = cap ? cap * 2 : 128) * sizeof *g);
            if (!g) break;
            L = g;
        }
        L[n] = (char *)malloc(m + 1);
        if (!L[n]) break;
        memcpy(L[n++], raw, m + 1);
    }
    fclose(f);

    for (i = 0; i < n; i++) {
        const char *line = L[i], *gap, *colon;
        if (!line[0]) {
            fc_add(FC_GAP, NULL, NULL);
            continue;
        }
        if (fc_indented(line)) {                         /* names going on */
            fc_add_wrapped(FC_NAME, fc_dup(line, strlen(line)));
            continue;
        }
        if (line[0] == '*' && line[1] == ' ') {
            fc_add(FC_HEADING, fc_dup(line + 2, strlen(line + 2)), NULL);
            continue;
        }
        if (line[0] == '-' && line[1] == ' ') {
            /* "- role: names". A role without its ':' runs on to the indented line that ends
             * it with one, if there is such a line before the group ends; else it is the first
             * line alone and the indented lines are its names. */
            char role[1024];
            int32_t k, end = -1;
            snprintf(role, sizeof role, "%s", line + 2);
            colon = strstr(role, ": ");
            if (!colon && role[0] && role[strlen(role) - 1] == ':')
                colon = role + strlen(role) - 1;
            if (!colon) {
                for (k = i + 1; k < n && L[k][0] && fc_indented(L[k]); k++)
                    if (strchr(L[k], ':') && !strstr(L[k], "://")) { end = k; break; }
                if (end > 0) {
                    for (k = i + 1; k <= end; k++) {
                        size_t m = strlen(role);
                        snprintf(role + m, sizeof role - m, " %s", L[k] + strspn(L[k], " \t"));
                    }
                    i = end;
                    colon = strstr(role, ": ");
                    if (!colon) colon = strrchr(role, ':');
                }
            }
            if (colon)
                fc_role(fc_dup(role, (size_t)(colon - role)), fc_dup(colon + 1, strlen(colon + 1)));
            else
                fc_role(fc_dup(role, strlen(role)), NULL);
            continue;
        }
        if ((gap = strstr(line, "  ")) != NULL)
            fc_role(fc_dup(line, (size_t)(gap - line)), fc_dup(gap, strlen(gap)));
        else
            fc_add(FC_TITLE, fc_dup(line, strlen(line)), NULL);
    }
    for (i = 0; i < n; i++)
        free(L[i]);
    free(L);
    s_fcLoaded = 1;
    return 1;
}

static void openswosFullCredits_OnInit(void)
{
    fc_load();
    s_fullSince = SDL_GetTicks64();
}

/* The FULL CREDITS menu is the one being shown (as the lobby knows its own: menu.onInit). */
static int32_t full_on_screen(void)
{
    extern uint8_t _currentMenu[];
    return ((const Menu *)_currentMenu)->onInit == (MenuVoidFn)openswosFullCredits_OnInit;
}

static void fc_text(float x, float y, const char *t, SDL_Color c, float alpha, float sh)
{
    const SDL_Color shadow = { 0, 0, 0, (uint8_t)(200 * alpha) };
    c.a = (uint8_t)(c.a * alpha);
    ChatFont_Draw(x + sh, y + sh, t, -1, shadow);
    ChatFont_Draw(x, y, t, -1, c);
}

/* A PAIR too wide for its two columns is drawn as its role above its names. */
static int32_t fc_stacked(const FcLine *l, float half, float gap)
{
    return l->kind == FC_PAIR &&
           (ChatFont_Measure(l->a, -1) + gap > half - gap || ChatFont_Measure(l->b, -1) + gap > half - gap);
}

void CreditsMenu_DrawOverlay(void)
{
    extern int _screen_width, _screen_height;
    const SDL_Color title = { 255, 214, 90, 255 }, heading = { 120, 200, 255, 255 },
                    role = { 170, 170, 190, 255 }, name = { 255, 255, 255, 255 },
                    black = { 0, 0, 0, 255 };
    float px, py, pw, ph, sy, lh, top, bottom, cx, y, fade, speed, total, gap;
    int32_t size, i;
    GfxSpace space;
    GfxBlend blend;
    const MenuEntry *exitEntry;

    if (!Gfx_InFrame() || !full_on_screen() || !s_fcCount)
        return;
    Gfx_PictureRect(&px, &py, &pw, &ph);
    if (pw <= 0.0f || ph <= 0.0f || _screen_width <= 0 || _screen_height <= 0)
        return;
    sy = ph / _screen_height;
    size = (int32_t)(sy * 9.0f + 0.5f);
    if (size < 12) size = 12;
    if (size > 96) size = 96;
    space = Gfx_Space();
    blend = Gfx_Blend();
    Gfx_SetSpace(GFX_SPACE_OUTPUT);
    Gfx_SetBlend(GFX_BLEND_ALPHA);
    if (ChatFont_Prepare(size) != 0) {
        Gfx_SetBlend(blend);
        Gfx_SetSpace(space);
        return;
    }
    exitEntry = CalcMenuEntryAddress(0);
    top = py;
    bottom = py + (exitEntry ? exitEntry->y - 4 : _screen_height - 24) * sy;
    {
        /* Black all round the exit button: above it (where the credits run), either side of it
         * and below it. */
        const float sx = pw / _screen_width;
        const float bx = exitEntry ? px + (_menu_draw_x_offset + exitEntry->x) * sx : px;
        const float bw = exitEntry ? exitEntry->dwWidth * sx : 0;
        const float by = exitEntry ? py + exitEntry->y * sy : bottom, bh = exitEntry ? exitEntry->dwHeight * sy : 0;
        const SDL_FRect r[4] = {
            { px, top, pw, by - top },                                    /* above */
            { px, by, bx - px, bh },                                      /* left of it */
            { bx + bw, by, px + pw - bx - bw, bh },                       /* right of it */
            { px, by + bh, pw, py + ph - by - bh },                       /* below */
        };
        for (i = 0; i < 4; i++)
            if (r[i].w > 0 && r[i].h > 0)
                Gfx_FillRect(&r[i], black);
    }
    /* Every line's place a whole number of pixels from the one before it, and the scroll itself
     * snapped once: the font snaps each line to a whole pixel, so lines at different fractions
     * (a blank line was 0.7 of one) tipped over on different frames -- one line a pixel up, the
     * next a frame later: the text shimmered, seeming to move up and back. Now they all move
     * together, a whole pixel at a time. */
    lh = (float)ChatFont_LineHeight();
    const float gapH = SDL_roundf(lh * 0.7f);
    gap = SDL_floorf(size * 0.6f);                        /* either side of the middle */
    cx = px + pw / 2;
    fade = 28.0f * sy;
    speed = 20.0f * sy;                                   /* picture lines a second */
    total = 0;
    for (i = 0; i < s_fcCount; i++)
        total += s_fc[i].kind == FC_GAP ? gapH : fc_stacked(&s_fc[i], pw / 2, gap) ? 2 * lh : lh;
    {
        /* From just below the bottom, up until the last line is gone; then again. */
        const float run = (bottom - top) + total + lh;
        float t = (float)(SDL_GetTicks64() - s_fullSince) / 1000.0f * speed;
        t -= SDL_floorf(t / run) * run;
        y = SDL_floorf(bottom - t);
    }
    for (i = 0; i < s_fcCount && y < bottom; i++) {
        const FcLine *l = &s_fc[i];
        const float wa = l->a ? ChatFont_Measure(l->a, -1) : 0, wb = l->b ? ChatFont_Measure(l->b, -1) : 0;
        const int32_t stacked = fc_stacked(l, pw / 2, gap);
        const float h = l->kind == FC_GAP ? gapH : stacked ? 2 * lh : lh;
        if (y + h > top && l->kind != FC_GAP) {
            int32_t row;
            for (row = 0; row < (stacked ? 2 : 1); row++) {
                const float ly = y + row * lh;
                float a = 1.0f;
                if (ly < top + fade) a = (ly - top) / fade;
                if (ly + lh > bottom - fade) a = SDL_min(a, (bottom - ly - lh) / fade);
                if (a <= 0.0f) continue;
                if (a > 1.0f) a = 1.0f;
                switch (l->kind) {
                case FC_TITLE:   fc_text(cx - wa / 2, ly, l->a, title, a, SDL_ceilf(size / 12.0f)); break;
                case FC_HEADING: fc_text(cx - wa / 2, ly, l->a, heading, a, SDL_ceilf(size / 12.0f)); break;
                case FC_ROLE:    fc_text(cx - wa / 2, ly, l->a, role, a, SDL_ceilf(size / 12.0f)); break;
                case FC_NAME:
                    /* names under a two-column role go on in the names' column */
                    if (l->head >= 0 && !fc_stacked(&s_fc[l->head], pw / 2, gap) && wa + gap <= pw / 2 - gap)
                        fc_text(cx + gap, ly, l->a, name, a, SDL_ceilf(size / 12.0f));
                    else
                        fc_text(cx - wa / 2, ly, l->a, name, a, SDL_ceilf(size / 12.0f));
                    break;
                case FC_PAIR:
                    if (stacked)
                        fc_text(row ? cx - wb / 2 : cx - wa / 2, ly, row ? l->b : l->a, row ? name : role, a,
                                SDL_ceilf(size / 12.0f));
                    else {
                        fc_text(cx - gap - wa, ly, l->a, role, a, SDL_ceilf(size / 12.0f));
                        fc_text(cx + gap, ly, l->b, name, a, SDL_ceilf(size / 12.0f));
                    }
                    break;
                }
            }
        }
        y += h;
    }
    Gfx_SetBlend(blend);
    Gfx_SetSpace(space);
}

int32_t CreditsMenu_FullCreditsLines(void)
{
    int32_t i;
    static const char *const k[] = { "gap", "title", "heading", "role", "name", "pair" };
    if (!fc_load())
        return 0;
    if (getenv("SWOS_FULL_CREDITS_DUMP"))
        for (i = 0; i < s_fcCount; i++)
            printf("[full-credits] %-7s|%s|%s%s\n", k[s_fc[i].kind], s_fc[i].a ? s_fc[i].a : "",
                   s_fc[i].b ? s_fc[i].b : "", s_fc[i].head >= 0 ? "  (names column)" : "");
    return s_fcCount;
}

static void openswosCredits_fullCredits_OnSelect(void)
{
    uint8_t *menu;

    if (!fc_load())
        return;
    menu = MenuJson_Load(FULL_CREDITS_MENU_NAME);
    if (!menu) {
        fprintf(stderr, "[credits] cannot show %s\n", FULL_CREDITS_MENU_NAME);
        return;
    }
    ShowMenu(menu);
}

/* ---- the menu ---- */

#define HANDLER(fn) { #fn, (void *)fn, SYM_FUNC }

static const MenuSymbol k_handlers[] = {
    HANDLER(openswosCredits_OnDraw),
    HANDLER(openswosCredits_exit_OnSelect),
    HANDLER(openswosCredits_fullCredits_OnSelect),
    HANDLER(openswosFullCredits_OnInit),
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
