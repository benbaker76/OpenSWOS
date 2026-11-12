/*
 * controls_menu.c -- the CONTROLS menu: the player's own keys, and the press latch (SDL
 * builds). Opened from OPTIONS -> SETTINGS -> CONTROLS -> KEYBOARD KEYS (src/settings_menus.c,
 * SWOS 2020's CONTROLS MENU, whose REDEFINE this is).
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
#include "touch_controls.h"   /* ON-SCREEN: the phones' touch controls */

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
    ControlsMenu_touchLabel, ControlsMenu_touch,    /* (appended: a phone's only) */
    ControlsMenu_deviceLabel, ControlsMenu_device,  /* (appended: P1 AND P2 ON KEYBOARD, the desktops') */
    ControlsMenu_playerLabel, ControlsMenu_player,  /* (whose keys the rows show) */
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
extern void        InputSDL_SetP2Keyboard(int on);
extern int         InputSDL_P2Keyboard(void);
extern int         InputSDL_SetP2KeyBinding(int which, const char *name);
extern const char *InputSDL_P2KeyBindingName(int which);
extern const char *InputSDL_P2KeyBindingSaved(int which);

/* The rows show (and redefine) player 1's keys, or player 2's for P1 AND P2 ON KEYBOARD. */
static int32_t s_p2;

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
    g_arcadeOpts->p2_keyboard = InputSDL_P2Keyboard();
    for (i = 0; i < kKeys; i++)
        snprintf(g_arcadeOpts->p2_key_bind[i], sizeof g_arcadeOpts->p2_key_bind[i], "%s",
                 InputSDL_P2KeyBindingSaved(i));
    g_arcadeOpts->touch_controls = TouchControls_Mode();
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
        if (s_p2) {                     /* (player 2's: his keys alone, no built-in ones) */
            upper(s_rowText[i], sizeof s_rowText[i], InputSDL_P2KeyBindingName(i));
            entry_at(ControlsMenu_up + 2 * i)->fg = (uintptr_t)s_rowText[i];
            continue;
        }
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
    entry_at(ControlsMenu_touch)->fg = (uintptr_t)(TouchControls_Mode() == TOUCH_CONTROLS_NEVER ? "NEVER"
                                                 : TouchControls_Mode() == TOUCH_CONTROLS_ALWAYS ? "ALWAYS" : "AUTO");
    entry_at(ControlsMenu_note)->fg = (uintptr_t)s_note;
    entry_at(ControlsMenu_device)->fg = (uintptr_t)(InputSDL_P2Keyboard() ? "P1 AND P2 ON KEYBOARD" : "PLAYER 1");
    entry_at(ControlsMenu_player)->fg = (uintptr_t)(s_p2 ? "PLAYER 2" : "PLAYER 1");
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
        if (s_p2)
            InputSDL_SetP2KeyBinding(which, "");    /* (player 2's: back to the default) */
        else
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
        /* (one key, one control: player 1's own and player 2's together) */
        if (((s_p2 || i != which) && SDL_GetScancodeFromName(InputSDL_KeyBindingName(i)) == sc) ||
            ((!s_p2 || i != which) && SDL_GetScancodeFromName(InputSDL_P2KeyBindingName(i)) == sc &&
             (s_p2 || InputSDL_P2Keyboard()))) {
            snprintf(s_note, sizeof s_note, "%s IS ALREADY USED", name);
            return;
        }
    }
    if ((s_p2 ? InputSDL_SetP2KeyBinding(which, SDL_GetScancodeName(sc))
              : InputSDL_SetKeyBinding(which, SDL_GetScancodeName(sc))) != 0) {
        note("THAT KEY CANNOT BE USED");
        return;
    }
    save();
}

/* ---- handlers ---- */

/* Set a row's links (the JSON's are the desktops'). */
static void nav(int32_t ordinal, int32_t up, int32_t down)
{
    MenuEntry *e = entry_at(ordinal);
    e->upEntry = (int8_t)up;
    e->downEntry = (int8_t)down;
    e->directionUp = e->directionDown = -1;
}

static void controlsMenu_OnInit(void)
{
    /* ON-SCREEN (the touch controls): a phone's row, between SHORT PRESSES and the buttons. */
#ifdef SWOS_MOBILE_BUILD
    const int32_t touch = 1;
#else
    const int32_t touch = 0;
#endif
    entry_at(ControlsMenu_touchLabel)->isInvisible = !touch;
    entry_at(ControlsMenu_touch)->isInvisible = !touch;
    /* P1 AND P2 ON KEYBOARD and whose keys: the desktops' (a phone has the touch row there) */
    entry_at(ControlsMenu_deviceLabel)->isInvisible = touch;
    entry_at(ControlsMenu_device)->isInvisible = touch;
    entry_at(ControlsMenu_playerLabel)->isInvisible = touch;
    entry_at(ControlsMenu_player)->isInvisible = touch;
    if (touch)
        s_p2 = 0;
    nav(ControlsMenu_latch, ControlsMenu_fire, touch ? ControlsMenu_touch : ControlsMenu_device);
    nav(ControlsMenu_device, ControlsMenu_latch, ControlsMenu_player);
    nav(ControlsMenu_player, ControlsMenu_device, ControlsMenu_clear);
    nav(ControlsMenu_clear, touch ? ControlsMenu_touch : ControlsMenu_player, -1);
    nav(ControlsMenu_exit,  touch ? ControlsMenu_touch : ControlsMenu_player, -1);
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

/* KEYBOARD: [input] p2_keyboard -- PLAYER 1, or P1 AND P2 ON KEYBOARD (the SWOS 96/97 Mod
 * 4.1's device). */
static void controlsMenu_device_OnSelect(void)
{
    InputSDL_SetP2Keyboard(!InputSDL_P2Keyboard());
    s_p2 = InputSDL_P2Keyboard();       /* (his keys on show: what was just chosen) */
    note(InputSDL_P2Keyboard() ? "PLAYER 2 ON THESE KEYS, OFFLINE MATCHES" : "THE KEYBOARD IS PLAYER 1'S");
    save();
}

/* KEYS OF: whose keys the rows show and redefine. */
static void controlsMenu_player_OnSelect(void)
{
    s_p2 = !s_p2;
    note(s_p2 && !InputSDL_P2Keyboard() ? "USED WITH P1 AND P2 ON KEYBOARD" : "");
}

/* ON-SCREEN: [input] touch_controls, AUTO -> ALWAYS -> NEVER. */
static void controlsMenu_touch_OnSelect(void)
{
    const int32_t mode = (TouchControls_Mode() + 1) % 3;
    TouchControls_SetMode(mode);
    note(mode == TOUCH_CONTROLS_NEVER  ? "OFF: FOR A HANDHELD WITH ITS OWN CONTROLS"
       : mode == TOUCH_CONTROLS_ALWAYS ? "ON SCREEN WHATEVER CONTROLLER IS USED"
                                       : "HIDDEN WHILE A CONTROLLER IS PLAYED");
    save();
}

static void controlsMenu_clear_OnSelect(void)
{
    int32_t i;
    for (i = 0; i < kKeys; i++) {
        if (s_p2)
            InputSDL_SetP2KeyBinding(i, "");    /* (player 2's: W S A D, LEFT SHIFT) */
        else
            InputSDL_SetKeyBinding(i, "");
    }
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
    HANDLER(controlsMenu_touch_OnSelect),
    HANDLER(controlsMenu_device_OnSelect),
    HANDLER(controlsMenu_player_OnSelect),
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
