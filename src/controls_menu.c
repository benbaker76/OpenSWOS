/*
 * controls_menu.c -- the CONTROLS menu: the player's own keys, and the press latch (SDL
 * builds). Opened from OPTIONS.
 *
 * SWOS 2020 had REDEFINE CONTROLS (its controlOptionsMenu.cpp: a screen that asked for UP,
 * DOWN, LEFT, RIGHT, FIRE and BENCH in turn). This is the same thing as rows of a menu: fire
 * on a row, then the key. The keys themselves -- what a bound key does, and why it stops
 * being a game key -- are src/input_sdl.c ([input] key_* in SWOS.ini); this file only
 * chooses them and saves the choice.
 *
 * The menu is JSON only (bin/data_json/menus/openswos/controlsMenu.json, docs/MENUS.md
 * section 10) and flagged dos_redraw, so its onDraw runs every frame.
 */
#include "controls_menu.h"

#ifdef SWOS_SDL_BUILD

#include "menu_load_json.h"
#include "menu_symbols.h"
#include "menu_system.h"
#include "platform_sdl.h"
#include "swos_settings.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

/* The menu's entries, in the order controlsMenu.json lists them. */
enum {
    ControlsMenu_header,
    ControlsMenu_subTitle,
    ControlsMenu_upLabel,    ControlsMenu_up,
    ControlsMenu_downLabel,  ControlsMenu_down,
    ControlsMenu_leftLabel,  ControlsMenu_left,
    ControlsMenu_rightLabel, ControlsMenu_right,
    ControlsMenu_fireLabel,  ControlsMenu_fire,
    ControlsMenu_latchLabel, ControlsMenu_latch,
    ControlsMenu_note,
    ControlsMenu_clear,
    ControlsMenu_exit,
    ControlsMenu_kNumEntries
};

enum { kKeys = 5 };     /* up, down, left, right, fire: input_sdl.c's order */

/* input_sdl.c */
extern int         InputSDL_SetKeyBinding(int which, const char *name);
extern const char *InputSDL_KeyBindingName(int which);
extern void        InputSDL_SetPressLatch(int on);
extern int         InputSDL_PressLatch(void);
extern void        InputSDL_CaptureKey(int on);
extern int32_t     InputSDL_CapturedKey(void);

extern void      MenuCheckControls(void);
extern void      CallDrawMenuItem(MenuEntry *entry);
extern void      SetExitMenuFlag(void);
extern volatile uint16_t _shortFire;

static char s_rowText[kKeys][32];       /* what each key row shows */
static char s_note[64];                 /* the line under the rows */

static MenuEntry *entry_at(int32_t ordinal)
{
    return CalcMenuEntryAddress((uint16_t)ordinal);
}

/* The game's letters are capitals. */
static void upper(char *out, int32_t size, const char *in)
{
    int32_t i;
    for (i = 0; i < size - 1 && in[i]; i++)
        out[i] = (char)((in[i] >= 'a' && in[i] <= 'z') ? in[i] - 32 : in[i]);
    out[i] = '\0';
}

static void note(const char *text)
{
    snprintf(s_note, sizeof s_note, "%s", text);
}

/* The choice goes to SWOS.ini at once, like every row of OPTIONS. */
static void save(void)
{
    extern PlatformInitOptions *g_arcadeOpts;
    extern const char *g_arcadeIniPath;
    int32_t i;

    if (!g_arcadeOpts)
        return;
    for (i = 0; i < kKeys; i++)
        snprintf(g_arcadeOpts->key_bind[i], sizeof g_arcadeOpts->key_bind[i], "%s", InputSDL_KeyBindingName(i));
    g_arcadeOpts->latch_presses = InputSDL_PressLatch();
    if (g_arcadeIniPath)
        Settings_Save(g_arcadeOpts, g_arcadeIniPath);
}

/* The keys every control has whatever is chosen here (built_in below): what a row shows
 * with no key of the player's ("NOT SET" read as no key at all: tester report 0044). */
static const char *const k_builtIn[kKeys] = { "CURSOR UP", "CURSOR DOWN", "CURSOR LEFT", "CURSOR RIGHT", "CTRL" };

static void refresh(void)
{
    int32_t i;

    for (i = 0; i < kKeys; i++) {
        const char *name = InputSDL_KeyBindingName(i);
        if (name[0]) {
            char own[16];
            upper(own, sizeof own, name);
            snprintf(s_rowText[i], sizeof s_rowText[i], "%s OR %s", own, k_builtIn[i]);
            if (strlen(s_rowText[i]) > 24)      /* (the row's 150 px: the player's key alone) */
                snprintf(s_rowText[i], sizeof s_rowText[i], "%s", own);
        } else
            snprintf(s_rowText[i], sizeof s_rowText[i], "%s", k_builtIn[i]);
        entry_at(ControlsMenu_up + 2 * i)->fg = (uintptr_t)s_rowText[i];
    }
    entry_at(ControlsMenu_latch)->fg = (uintptr_t)(InputSDL_PressLatch() ? "ALWAYS COUNT" : "CAN BE MISSED");
    entry_at(ControlsMenu_note)->fg = (uintptr_t)s_note;
}

/* The keys that are controls already, whatever is chosen here: a second meaning for one of
 * them would take the first away. */
static int32_t built_in(SDL_Scancode sc)
{
    return sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_DOWN || sc == SDL_SCANCODE_LEFT ||
           sc == SDL_SCANCODE_RIGHT || sc == SDL_SCANCODE_LCTRL || sc == SDL_SCANCODE_RCTRL ||
           sc == SDL_SCANCODE_RETURN;
}

/* Fire on a key's row: wait for the next key and make it that control. ESC, or fire
 * again, leaves it as it was; BACKSPACE or DELETE takes the player's key away. */
static void choose(int32_t which)
{
    MenuEntry   *e = entry_at(ControlsMenu_up + 2 * which);
    SDL_Scancode sc;
    char         name[24];
    int32_t      i;

    e->fg = (uintptr_t)"PRESS A KEY";
    note("ESC: LEAVE IT   BACKSPACE: NO KEY");
    {
        /* The line has no box of its own: the picture goes back under it first, or the
         * new words are drawn over the old. */
        MenuEntry *n = entry_at(ControlsMenu_note);
        n->fg = (uintptr_t)s_note;
        DrawMenuItemBackgroundMasked(n->x, n->y, n->dwWidth, n->dwHeight);
        CallDrawMenuItem(n);
    }

    InputSDL_CaptureKey(1);
    for (;;) {
        MenuCheckControls();
        sc = (SDL_Scancode)InputSDL_CapturedKey();
        if (sc != SDL_SCANCODE_UNKNOWN)
            break;
        if (_shortFire != 0) {          /* a pad's fire: nothing chosen */
            sc = SDL_SCANCODE_ESCAPE;
            break;
        }
        CallDrawMenuItem(e);
    }
    InputSDL_CaptureKey(0);

    note("");
    if (sc == SDL_SCANCODE_ESCAPE)
        return;
    if (sc == SDL_SCANCODE_BACKSPACE || sc == SDL_SCANCODE_DELETE) {
        InputSDL_SetKeyBinding(which, "");
        save();
        return;
    }

    upper(name, sizeof name, SDL_GetScancodeName(sc));
    if (!name[0]) {
        note("THAT KEY HAS NO NAME");
        return;
    }
    if (built_in(sc)) {
        snprintf(s_note, sizeof s_note, "%s IS A CONTROL ALREADY", name);
        return;
    }
    for (i = 0; i < kKeys; i++) {
        if (i != which && SDL_GetScancodeFromName(InputSDL_KeyBindingName(i)) == sc) {
            snprintf(s_note, sizeof s_note, "%s IS ALREADY USED", name);
            return;
        }
    }
    if (InputSDL_SetKeyBinding(which, SDL_GetScancodeName(sc)) != 0) {
        note("THAT KEY CANNOT BE USED");
        return;
    }
    save();
}

/* ---- handlers ---- */

static void controlsMenu_OnInit(void)
{
    note("");
    refresh();
}

static void controlsMenu_OnDraw(void)
{
    refresh();
}

static void controlsMenu_up_OnSelect(void)    { choose(0); }
static void controlsMenu_down_OnSelect(void)  { choose(1); }
static void controlsMenu_left_OnSelect(void)  { choose(2); }
static void controlsMenu_right_OnSelect(void) { choose(3); }
static void controlsMenu_fire_OnSelect(void)  { choose(4); }

/* SHORT PRESSES: [input] latch_presses. */
static void controlsMenu_latch_OnSelect(void)
{
    InputSDL_SetPressLatch(!InputSDL_PressLatch());
    note(InputSDL_PressLatch() ? "A TAP SHORTER THAN A FRAME STILL COUNTS"
                               : "ONLY WHAT IS HELD WHEN THE GAME LOOKS");
    save();
}

static void controlsMenu_clear_OnSelect(void)
{
    int32_t i;
    for (i = 0; i < kKeys; i++)
        InputSDL_SetKeyBinding(i, "");
    note("");
    save();
}

static void controlsMenu_exit_OnSelect(void)
{
    SetExitMenuFlag();
}

/* ---- the menu ---- */

#define HANDLER(fn) { #fn, (void *)fn, SYM_FUNC }

static const MenuSymbol k_handlers[] = {
    HANDLER(controlsMenu_OnInit),
    HANDLER(controlsMenu_OnDraw),
    HANDLER(controlsMenu_up_OnSelect),
    HANDLER(controlsMenu_down_OnSelect),
    HANDLER(controlsMenu_left_OnSelect),
    HANDLER(controlsMenu_right_OnSelect),
    HANDLER(controlsMenu_fire_OnSelect),
    HANDLER(controlsMenu_latch_OnSelect),
    HANDLER(controlsMenu_clear_OnSelect),
    HANDLER(controlsMenu_exit_OnSelect),
};

#undef HANDLER

void ControlsMenu_Init(void)
{
    static int32_t registered;

    if (registered)
        return;
    registered = 1;
    MenuJson_RegisterSymbols(k_handlers, (int)(sizeof k_handlers / sizeof k_handlers[0]));
}

void ControlsMenu_Show(void)
{
    uint8_t *menu;

    ControlsMenu_Init();
    menu = MenuJson_Load(CONTROLS_MENU_NAME);
    if (!menu) {
        fprintf(stderr, "[controls-menu] cannot show %s\n", CONTROLS_MENU_NAME);
        return;
    }
    ShowMenu(menu);
}

#endif /* SWOS_SDL_BUILD */
