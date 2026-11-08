/*
 * menu_dialog.c -- a dialog drawn by the game (include/menu_dialog.h): the JSON-only menu
 * openswos/menuDialog (docs/MENUS.md section 10) -- a header, four button entries -- filled in
 * by its onInit (the title, the buttons used, where they stand, their neighbours) and the text
 * drawn by its onDraw, wrapped to the screen, under the header.
 *
 * The menu font has capitals, digits and . , : - ! only: the text is shown in capitals, its
 * brackets as dashes and its quotes and apostrophes left out, so a caller can write it as it
 * would for the OS's message box. SDL builds.
 */
#include "menu_dialog.h"

#ifdef SWOS_SDL_BUILD

#include "menu_load_json.h"
#include "menu_symbols.h"
#include "menu_system.h"
#include "text_render.h"        /* GetTextWidth, DrawMenuTextCentered */
#include "mod_api.h"            /* ModApi_MenuText / Visible / Nav / Select / SetMenuEntryRect */

#include <stdio.h>
#include <string.h>

#define DIALOG_MENU "openswos/menuDialog"

enum { D_HEADER, D_BUTTON0 };   /* menuDialog.json's entries: the header, then the four buttons */
enum {
    kTextY = 22,                /* the text's first line, under the header */
    kTextWidth = 300,           /* px a line may take */
    kLine = 9,                  /* px from one line to the next */
    kButtonBottom = 210,        /* the last button's y; the ones before it stand above it */
    kButtonStep = 19,
};

extern void *_charTables[];
extern void  SetExitMenuFlag(void);

static struct {
    char        title[48];
    char        text[1024];
    const char *button[MENU_DIALOG_MAX_BUTTONS];
    int32_t     count, def, chosen;
} D;

/* What the menu font can show: capitals; brackets as dashes; quotes left out. */
static void to_menu_font(char *dst, size_t cap, const char *src)
{
    size_t n = 0;
    for (; *src && n + 1 < cap; src++) {
        char c = *src;
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 32);
        else if (c == '(' || c == ')' || c == '[' || c == ']')
            c = '-';
        else if (c == '\'' || c == '"')
            continue;
        else if (c == '\n')
            c = '|';
        dst[n++] = c;
    }
    dst[n] = '\0';
}

static void openswosDialog_OnInit(void)
{
    ModApi_MenuText(D_HEADER, D.title);
    for (int32_t i = 0; i < MENU_DIALOG_MAX_BUTTONS; i++) {
        const int32_t e = D_BUTTON0 + i, used = i < D.count;
        ModApi_MenuVisible(e, used);
        if (!used)
            continue;
        ModApi_MenuText(e, D.button[i]);
        ModApi_SetMenuEntryRect(e, -1, kButtonBottom - (D.count - 1 - i) * kButtonStep, -1, -1);
        ModApi_MenuNav(e, i > 0 ? e - 1 : -1, i + 1 < D.count ? e + 1 : -1);
    }
    ModApi_MenuSelect(D_BUTTON0 + (D.def >= 0 && D.def < D.count ? D.def : 0));
}

/* The text, wrapped ('|' a new paragraph), centred under the header. */
static void openswosDialog_OnDraw(void)
{
    const MenuEntry *header = CalcMenuEntryAddress(D_HEADER);
    const int32_t cx = header->x + header->dwWidth / 2;
    int32_t y = header->y + kTextY;
    const char *p = D.text;
    char line[128];
    while (*p) {
        int32_t n = 0, lastSpace = -1;
        if (*p == '|') {                /* an empty paragraph: half a line more */
            y += kLine / 2 + 1;
            p++;
            continue;
        }
        while (p[n] && p[n] != '|' && n < (int32_t)sizeof line - 1) {
            memcpy(line, p, (size_t)n + 1);
            line[n + 1] = '\0';
            if (GetTextWidth((uint8_t *)line, (uint8_t *)_charTables[0]) > kTextWidth)
                break;
            if (p[n] == ' ')
                lastSpace = n;
            n++;
        }
        if (p[n] && p[n] != '|' && lastSpace > 0)
            n = lastSpace;              /* (break at the last space that fits) */
        memcpy(line, p, (size_t)n);
        line[n] = '\0';
        DrawMenuTextCentered((uint16_t)cx, (uint16_t)y, 2, (uint8_t *)line, (uint8_t *)_charTables[0]);
        y += kLine;
        p += n;
        while (*p == ' ')
            p++;
        if (*p == '|') {                /* a paragraph ends: the gap, then the next */
            y += kLine / 2 + 1;
            p++;
        }
    }
}

static void chose(int32_t i) { D.chosen = i; SetExitMenuFlag(); }
static void openswosDialog_b0_OnSelect(void) { chose(0); }
static void openswosDialog_b1_OnSelect(void) { chose(1); }
static void openswosDialog_b2_OnSelect(void) { chose(2); }
static void openswosDialog_b3_OnSelect(void) { chose(3); }

#define HANDLER(fn) { #fn, (void *)fn, SYM_FUNC }
static const MenuSymbol k_handlers[] = {
    HANDLER(openswosDialog_OnInit),
    HANDLER(openswosDialog_OnDraw),
    HANDLER(openswosDialog_b0_OnSelect),
    HANDLER(openswosDialog_b1_OnSelect),
    HANDLER(openswosDialog_b2_OnSelect),
    HANDLER(openswosDialog_b3_OnSelect),
};
#undef HANDLER

int32_t MenuDialog_Show(const char *title, const char *text, const char *const *buttons, int32_t count,
                        int32_t defaultButton)
{
    static int32_t registered;
    static char buttonText[MENU_DIALOG_MAX_BUTTONS][32];
    uint8_t *menu;
    if (!registered) {
        registered = 1;
        MenuJson_RegisterSymbols(k_handlers, (int)(sizeof k_handlers / sizeof k_handlers[0]));
    }
    if (count < 1)
        count = 0;
    if (count > MENU_DIALOG_MAX_BUTTONS)
        count = MENU_DIALOG_MAX_BUTTONS;
    to_menu_font(D.title, sizeof D.title, title ? title : "");
    to_menu_font(D.text, sizeof D.text, text ? text : "");
    for (int32_t i = 0; i < count; i++) {
        to_menu_font(buttonText[i], sizeof buttonText[i], buttons[i] ? buttons[i] : "");
        D.button[i] = buttonText[i];
    }
    D.count = count;
    D.def = defaultButton;
    D.chosen = -1;
    menu = MenuJson_Load(DIALOG_MENU);
    if (!menu) {
        fprintf(stderr, "[dialog] cannot show %s: \"%s\"\n", DIALOG_MENU, D.title);
        return -1;
    }
    ShowMenu(menu);
    fprintf(stderr, "[dialog] %s: %s\n", D.title, D.chosen >= 0 ? D.button[D.chosen] : "(esc)");
    return D.chosen;
}

#endif /* SWOS_SDL_BUILD */
