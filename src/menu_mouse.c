/*
 * menu_mouse.c -- the mouse in the menus (SDL builds).
 *
 * Moving the mouse over an entry selects it (only when the mouse moves, so the keyboard and the
 * pads keep the selection otherwise); a left click fires the entry under it, as player 1's fire
 * does; a right click fires the screen's EXIT (its menus back out through that: ESC does nothing
 * in them); the wheel steps up and down, as the arrow keys do. The cursor
 * shows when the mouse moves in a menu and goes after a few seconds still, and in a match.
 *
 * Nothing of it is match state: it runs in MenuProc (menu_system.c), after the controls are
 * read and before the selected entry is dispatched -- where the career autopilot works too --
 * and a match frame only hides the cursor and forgets the clicks (MenuMouse_InMatch).
 * Phones and handhelds: their touches make no mouse events (mobile.c), and a mouse that never
 * moves never selects anything.
 */
#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "menu_system.h"

#include <string.h>
#include <ctype.h>

#define MOUSE_IDLE_MS   3000u       /* the cursor goes after this long still */
#define MOUSE_STALE_MS  250u        /* a click older than this (made outside a menu) is dropped */

extern volatile uint16_t _shortFire, _fire;
extern uint8_t  _pl1Fire;
extern int16_t  _menuControlsDirection, _menuControlsDirection2;
extern int      _menu_draw_x_offset;
extern uint8_t  _currentMenu[];           /* (the menu itself: menu_system.c) */

extern int  Platform_MouseToPicture(int wx, int wy, int *x, int *y);   /* platform_sdl.c */
extern void Platform_ShowMouseCursor(int on);
extern const char *tr(const char *symbol);                             /* translations.c */

static volatile int32_t s_left, s_right, s_wheel;     /* since the last menu frame */
static Uint32 s_eventAt;                              /* when the last of them came */
static int    s_lastX = -1, s_lastY = -1;             /* the picture's pixel the mouse was on */
static Uint32 s_movedAt;
static Uint32 s_lastFrame;

/* From the event pump (platform_sdl.c), on the main thread: kept for the next menu frame. */
void MenuMouse_Event(const SDL_Event *e)
{
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.which != SDL_TOUCH_MOUSEID) {
        if (e->button.button == SDL_BUTTON_LEFT)
            s_left++;
        else if (e->button.button == SDL_BUTTON_RIGHT)
            s_right++;
        s_eventAt = SDL_GetTicks();
    } else if (e->type == SDL_MOUSEWHEEL && e->wheel.which != SDL_TOUCH_MOUSEID) {
        s_wheel += e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e->wheel.y : e->wheel.y;
        s_eventAt = SDL_GetTicks();
    }
}

/* The entry under the picture's pixel x, y: one that can be selected (visible, enabled, with
 * something to do), the smallest when two overlap. -1 for none. */
static int32_t entry_at(int x, int y)
{
    const Menu *menu = (const Menu *)_currentMenu;
    int32_t best = -1;
    uint32_t bestArea = 0xFFFFFFFFu;
    x -= _menu_draw_x_offset;
    for (uint16_t i = 0; i < menu->numEntries; i++) {
        const MenuEntry *e = CalcMenuEntryAddress(i);
        if (e->isInvisible || e->isDisabled || !e->onSelect)
            continue;
        if (x < (int)e->x || x >= (int)e->x + (int)e->dwWidth || y < (int)e->y || y >= (int)e->y + (int)e->dwHeight)
            continue;
        const uint32_t area = (uint32_t)e->dwWidth * e->dwHeight;
        if (area < bestArea) {
            bestArea = area;
            best = i;
        }
    }
    return best;
}

/* `t` (spaces round it aside) is `word`, any case. */
static int32_t label_is(const char *t, const char *word)
{
    size_t n, w = strlen(word);
    while (*t == ' ')
        t++;
    n = strlen(t);
    while (n && t[n - 1] == ' ')
        n--;
    if (n != w)
        return 0;
    for (size_t k = 0; k < n; k++)
        if (toupper((unsigned char)t[k]) != toupper((unsigned char)word[k]))
            return 0;
    return 1;
}

/* The screen's way back: an entry labelled EXIT or ABORT (the menus' two: 49 and 4 screens),
 * in English or the language on screen. The menus back out through it; ESC does nothing in
 * them. -1 for none. */
static int32_t exit_entry(void)
{
    static const char *const kBack[] = { "_aExit", "_aAbort" };
    static const char *const kBackEn[] = { "EXIT", "ABORT" };
    const Menu *menu = (const Menu *)_currentMenu;
    for (uint16_t i = 0; i < menu->numEntries; i++) {
        const MenuEntry *e = CalcMenuEntryAddress(i);
        const char *t = (const char *)e->fg;
        if (e->isInvisible || e->isDisabled || !e->onSelect || e->content != ENTRY_STRING || !t)
            continue;
        for (int k = 0; k < 2; k++) {
            const char *local = tr(kBack[k]);
            if (label_is(t, kBackEn[k]) || (local && label_is(t, local)))
                return i;
        }
    }
    return -1;
}

static void select_entry(int32_t i)
{
    Menu *menu = (Menu *)_currentMenu;
    MenuEntry *old = menu->selectedEntry;
    MenuEntry *e = CalcMenuEntryAddress((uint16_t)i);
    if (old == e)
        return;
    SetCurrentEntry((uint16_t)i);
    /* As the arrow keys move it (menu_system.c): the entry left is drawn again, which takes its
     * highlight away -- the whole menu for one with no background (its cursor a frame around
     * it), else the entry itself. */
    if (old) {
        if (old->background == NO_BACKGROUND)
            DrawMenu();
        else
            CallDrawMenuItem(old);
    }
}

/* SWOS_MOUSE_TEST=1 (with SWOS_SKIP_INTRO, SWOS_HARNESS_TURBO): at the main menu, the mouse is
 * moved over OPTIONS (it must be selected), clicks PRESET COMPETITION (its screen must open) and
 * right-clicks (its EXIT: the main menu again) -- through the same hit test and firing as a mouse, the
 * window's own point aside. Logs each step, PASS or FAIL, and quits. */
static int32_t exit_entry(void);
static int32_t s_test = -1, s_testStep, s_testWait;
static int32_t s_testX = -1, s_testY = -1, s_testLeft, s_testRight;
static uint8_t s_testMain[2];                   /* (which screen: the main menu's first bytes) */

static int32_t entry_with(void (*fn)(void), int *cx, int *cy)
{
    const Menu *menu = (const Menu *)_currentMenu;
    for (uint16_t i = 0; i < menu->numEntries; i++) {
        const MenuEntry *e = CalcMenuEntryAddress(i);
        if ((void (*)(void))(uintptr_t)e->onSelect == fn && !e->isInvisible) {
            *cx = _menu_draw_x_offset + e->x + e->dwWidth / 2;
            *cy = e->y + e->dwHeight / 2;
            return i;
        }
    }
    return -1;
}

static void test_step(void)
{
    extern void OptionsMenuSelected(void), PresetCompetitionMenu(void);
    extern int32_t Menu_IsMainMenu(void);
    const Menu *menu = (const Menu *)_currentMenu;
    int cx, cy;
    int32_t i;
    if (s_testWait > 0) {
        s_testWait--;
        return;
    }
    switch (s_testStep) {
    case 0:                                     /* the main menu up: over OPTIONS */
        if (!Menu_IsMainMenu())
            return;
        if ((i = entry_with(OptionsMenuSelected, &cx, &cy)) < 0) {
            fprintf(stdout, "MOUSE TEST FAIL: no OPTIONS entry\n");
            exit(1);
        }
        s_testX = cx; s_testY = cy;
        s_testStep = 1; s_testWait = 3;
        return;
    case 1:
        i = entry_with(OptionsMenuSelected, &cx, &cy);
        fprintf(stdout, "MOUSE TEST: hover over OPTIONS (%d,%d): %s\n", cx, cy,
                menu->selectedEntry == CalcMenuEntryAddress((uint16_t)i) ? "selected" : "NOT selected");
        if (menu->selectedEntry != CalcMenuEntryAddress((uint16_t)i))
            exit(1);
        {   /* on to another: it is selected (and OPTIONS drawn again, its highlight gone) */
            extern void EditTacticsMenu(void);
            if (entry_with(EditTacticsMenu, &cx, &cy) >= 0) {
                s_testX = cx; s_testY = cy;
                s_testStep = 10; s_testWait = 3;
                return;
            }
        }
        /* fall through */
    case 10:
        if (s_testStep == 10) {
            extern void EditTacticsMenu(void);
            i = entry_with(EditTacticsMenu, &cx, &cy);
            fprintf(stdout, "MOUSE TEST: then over EDIT TACTICS: %s\n",
                    menu->selectedEntry == CalcMenuEntryAddress((uint16_t)i) ? "selected" : "NOT selected");
            if (menu->selectedEntry != CalcMenuEntryAddress((uint16_t)i))
                exit(1);
        }
        if ((i = entry_with(PresetCompetitionMenu, &cx, &cy)) < 0) {
            fprintf(stdout, "MOUSE TEST FAIL: no PRESET COMPETITION entry\n");
            exit(1);
        }
        s_testX = cx; s_testY = cy; s_testLeft = 1;
        s_testStep = 2; s_testWait = 20;
        return;
    case 2:
        fprintf(stdout, "MOUSE TEST: click on PRESET COMPETITION: %s\n",
                !Menu_IsMainMenu() ? "its screen opened" : "NOTHING opened");
        if (Menu_IsMainMenu())
            exit(1);
        s_testRight = 1;
        s_testStep = 3; s_testWait = 20;
        return;
    case 3:
        fprintf(stdout, "MOUSE TEST: right click: %s\n", Menu_IsMainMenu() ? "back on the main menu" : "NOT back");
        fprintf(stdout, "MOUSE TEST %s\n", Menu_IsMainMenu() ? "PASS" : "FAIL");
        exit(Menu_IsMainMenu() ? 0 : 1);
    }
}

/* A menu frame, after its controls were read. */
void MenuMouse_Frame(void)
{
    const Uint32 now = SDL_GetTicks();
    int32_t left = s_left, right = s_right, wheel = s_wheel;
    int wx, wy, x = -1, y = -1;

    s_left = s_right = s_wheel = 0;
    /* (clicks made while no menu was up -- a match, a loading screen -- are not this menu's) */
    if (now - s_lastFrame > MOUSE_STALE_MS || now - s_eventAt > MOUSE_STALE_MS)
        left = right = wheel = 0;
    s_lastFrame = now;

    if (s_test < 0)
        s_test = getenv("SWOS_MOUSE_TEST") != NULL;
    int onPicture;
    if (s_test) {
        test_step();
        x = s_testX; y = s_testY;
        onPicture = x >= 0;
        left = s_testLeft; right = s_testRight;
        s_testLeft = s_testRight = 0;
    } else {
        SDL_GetMouseState(&wx, &wy);
        onPicture = Platform_MouseToPicture(wx, wy, &x, &y) == 0;
    }
    const int moved = onPicture && (x != s_lastX || y != s_lastY) && (s_lastX >= 0 || s_test);
    if (onPicture) {
        s_lastX = x;
        s_lastY = y;
    }
    if (moved || left || right || wheel)
        s_movedAt = now;
    Platform_ShowMouseCursor(s_movedAt != 0 && now - s_movedAt < MOUSE_IDLE_MS);

    if (moved) {
        const int32_t i = entry_at(x, y);
        if (i >= 0)
            select_entry(i);
    }
    if (left && onPicture) {
        const int32_t i = entry_at(x, y);
        if (i >= 0) {
            select_entry(i);
            _shortFire = 1;     /* the press */
            _fire = 1;
            _pl1Fire = 1;       /* (player 1's: some handlers go on only for a player's fire) */
        }
    }
    if (right) {                    /* back: the screen's EXIT, fired as a click on it */
        const int32_t i = exit_entry();
        if (i >= 0) {
            select_entry(i);
            _shortFire = 1;
            _fire = 1;
            _pl1Fire = 1;
        }
    }
    if (wheel) {
        _menuControlsDirection = wheel > 0 ? 0 : 4;     /* up / down, as the arrow keys */
        _menuControlsDirection2 = _menuControlsDirection;
    }
}

/* A match frame: no cursor over the pitch, and no click kept for the next menu. */
void MenuMouse_InMatch(void)
{
    s_left = s_right = s_wheel = 0;
    s_movedAt = 0;
    Platform_ShowMouseCursor(0);
}
