/*
 * settings_menus.c -- SWOS 2020's OPTIONS screens, ported (SDL builds):
 *
 *   SETTINGS (openswos/settingsMenu)   SWOS 2020's OPTIONS buttons: CONTROLS, VIDEO, AUDIO,
 *                                      GAMEPLAY -- opened from OPTIONS' SETTINGS row
 *   a page (openswos/settingsPage)     one of those four screens, as rows of OUR settings
 *                                      (SWOS 2020's controlOptions.mnu, videoOptions.mnu,
 *                                      audioOptions.mnu, gameplayOptions in options.mnu):
 *                                      fire or right changes a value, left changes it back
 *   CONTROLLER SETUP                   SWOS 2020's joypadConfig.mnu, imported as it is
 *   (openswos/joypadConfigMenu)        (scripts/import_swos2020_menus.py): a pad's name, GUID,
 *                                      buttons, axes, power and raw stick, its FIRE and BENCH
 *                                      buttons and its four stick cut-offs with the box
 *
 * and F6, SWOS 2020's quick save from the menus (controls.cpp:2202), and the menu cursor's
 * flashing (SWOS 2020's --flashing-menu-cursor).
 *
 * Every row is a setting the game already has in SWOS.ini: it changes now where the game can
 * (the window, the volumes, the sounds, the controls), and says "from the next start" where it
 * cannot. The settings themselves are where they always were -- the controls in input_sdl.c,
 * the volumes in audio_sdl.c, the replay viewer in replay_v2.c -- this file only shows and
 * changes them and saves SWOS.ini at once, as every row of OPTIONS does. Nothing here changes
 * a match: online, each game's controls only decide what its own input words say.
 *
 * Menus JSON-only (docs/MENUS.md section 10), flagged dos_redraw so onDraw runs every frame.
 */
#ifdef SWOS_SDL_BUILD

#include "menu_load_json.h"
#include "menu_symbols.h"
#include "menu_system.h"
#include "platform_sdl.h"
#include "swos_settings.h"
#include "dos_mode.h"
#include "dos_sound.h"
#include "dos_music.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#define SETTINGS_MENU_NAME   "openswos/settingsMenu"
#define SETTINGS_PAGE_NAME   "openswos/settingsPage"
#define JOYPAD_CONFIG_NAME   "openswos/joypadConfigMenu"

extern PlatformInitOptions *g_arcadeOpts;
extern const char *g_arcadeIniPath;

extern void      MenuCheckControls(void);
extern void      CallDrawMenuItem(MenuEntry *entry);
extern void      SetExitMenuFlag(void);
extern volatile uint16_t _shortFire;
extern volatile uint16_t _fire;
extern uint16_t  _controlMask;
extern uint8_t   _currentMenu[];

/* input_sdl.c */
extern void        InputSDL_CaptureKey(int on);
extern int32_t     InputSDL_CapturedKey(void);
extern void        InputSDL_ClearKeyState(void);
extern const char *InputSDL_PlayerPref(int p);
extern void        InputSDL_SetPlayerPref(int p, const char *pref);
extern int         InputSDL_PlayerDevice(int p);
extern int         InputSDL_DeviceOpen(int d);
extern int         InputSDL_MaxDevices(void);
extern void        InputSDL_DeviceLabel(int d, char *out, int cap);
extern void        InputSDL_DevicePref(int d, char *out, int cap);
extern void        InputSDL_DeviceInfo(int d, char guid[33], int *id, int *buttons, int *axes, const char **power,
                                       int *rawX, int *rawY, int dirs[4]);
extern int         InputSDL_PadButton(int d, int bench);
extern void        InputSDL_PadButtonName(int d, int bench, int b, char *out, int cap);
extern int         InputSDL_SetPadButton(int d, int bench, int b);
extern int         InputSDL_PadCut(int d, int k);
extern void        InputSDL_SetPadCut(int d, int k, int pct);
extern int         InputSDL_StickDeadzonePercent(void);
extern void        InputSDL_SetStickDeadzone(int percent);
extern void        InputSDL_PadPressBegin(void);
extern int         InputSDL_PadPressPoll(int only, int *dev, int *button);
extern void        InputSDL_GivePlayerDevice(int p, int d);
extern int         InputSDL_AutoDetect(void);
extern void        InputSDL_SetAutoDetect(int on);
extern int         InputSDL_ShouldersOff(void);
extern void        InputSDL_SetShouldersOff(int on);
extern int         InputSDL_CallBench(void);
extern void        InputSDL_SetCallBench(int mode);
extern void        InputSDL_StoreConfig(PlatformInitOptions *o);
extern int         InputSDL_TakeQuickSave(void);

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

/* SWOS.ini at once, like every row of OPTIONS. */
static void save(void)
{
    if (!g_arcadeOpts)
        return;
    InputSDL_StoreConfig(g_arcadeOpts);
    if (g_arcadeIniPath)
        Settings_Save(g_arcadeOpts, g_arcadeIniPath);
}

static const char *on_off(int on) { return on ? "ON" : "OFF"; }

/* A choice stepped by `dir` (+1 / -1) through `n`, wrapping. */
static int32_t step(int32_t at, int32_t dir, int32_t n)
{
    return ((at + dir) % n + n) % n;
}

/* ===================================================================== the menu look */

/* SWOS 2020's flashing menu cursor (setFlashingMenuCursorVariables): its mode 1 is the frame in
 * one colour, not flashing. Ours: [video] menu_cursor flashing (the game's own) | steady. The
 * cursor's colours are _kColorTableShine (DrawSelectedMenuItemFrame), so steady is the table at
 * its first colour throughout. */
static uint16_t s_shineDefault[8];
static int32_t  s_shineSaved;
static int32_t  s_cursorSteady;

void MenuLook_SetCursor(int steady)
{
    extern uint16_t _kColorTableShine[8];
    int32_t i;
    if (!s_shineSaved) {
        memcpy(s_shineDefault, _kColorTableShine, sizeof s_shineDefault);
        s_shineSaved = 1;
    }
    s_cursorSteady = steady != 0;
    for (i = 0; i < 8; i++)
        _kColorTableShine[i] = s_cursorSteady ? s_shineDefault[0] : s_shineDefault[i];
}

/* ===================================================================== a page of rows */

typedef struct {
    const char *label;
    void (*show)(char *out, int32_t cap);
    void (*change)(int32_t dir);            /* +1: fire or right, -1: left */
} Row;

typedef struct {
    const char *title;
    const Row  *rows;
    int32_t     count;
} Page;

enum { kRows = 12, kValueLen = 32 };
enum {                                      /* settingsPage.json's entries */
    Page_header,
    Page_label0,                            /* label<i> = label0 + 2i, value<i> = label0 + 2i + 1 */
    Page_note = Page_label0 + 2 * kRows,
    Page_exit,
    Page_kNumEntries
};

static const Page *s_page;
static char s_values[kRows][kValueLen];
static char s_note[96];

static void note(const char *text)
{
    snprintf(s_note, sizeof s_note, "%s", text);
}

static void later(void)
{
    note("FROM THE NEXT START");
}

static void page_refresh(void)
{
    int32_t i;
    if (!s_page)
        return;
    entry_at(Page_header)->fg = (uintptr_t)s_page->title;
    for (i = 0; i < kRows; i++) {
        MenuEntry *label = entry_at(Page_label0 + 2 * i), *value = entry_at(Page_label0 + 2 * i + 1);
        label->isInvisible = value->isInvisible = i >= s_page->count;
        if (i >= s_page->count)
            continue;
        s_page->rows[i].show(s_values[i], kValueLen);
        label->fg = (uintptr_t)s_page->rows[i].label;
        value->fg = (uintptr_t)s_values[i];
    }
    entry_at(Page_note)->fg = (uintptr_t)s_note;
}

static void nav(int32_t ordinal, int32_t up, int32_t down)
{
    MenuEntry *e = entry_at(ordinal);
    e->upEntry = (int8_t)up;
    e->downEntry = (int8_t)down;
    e->directionUp = e->directionDown = -1;
}

static void settingsPage_OnInit(void)
{
    int32_t i, n = s_page ? s_page->count : 0;
    for (i = 0; i < n; i++) {
        const int32_t value = Page_label0 + 2 * i + 1;
        nav(value, i ? value - 2 : -1, i + 1 < n ? value + 2 : Page_exit);
    }
    nav(Page_exit, n ? Page_label0 + 2 * n - 1 : -1, -1);
    page_refresh();
}

static void settingsPage_OnDraw(void) { page_refresh(); }

static void page_row(int32_t i)
{
    int32_t dir = (_controlMask & 0x40) ? -1 : 1;      /* (0x40 left, 0x80 right, 0x20 fire) */
    if (!s_page || i >= s_page->count)
        return;
    note("");
    s_page->rows[i].change(dir);
    page_refresh();
}

static void settingsPage_row0_OnSelect(void)  { page_row(0); }
static void settingsPage_row1_OnSelect(void)  { page_row(1); }
static void settingsPage_row2_OnSelect(void)  { page_row(2); }
static void settingsPage_row3_OnSelect(void)  { page_row(3); }
static void settingsPage_row4_OnSelect(void)  { page_row(4); }
static void settingsPage_row5_OnSelect(void)  { page_row(5); }
static void settingsPage_row6_OnSelect(void)  { page_row(6); }
static void settingsPage_row7_OnSelect(void)  { page_row(7); }
static void settingsPage_row8_OnSelect(void)  { page_row(8); }
static void settingsPage_row9_OnSelect(void)  { page_row(9); }
static void settingsPage_row10_OnSelect(void) { page_row(10); }
static void settingsPage_row11_OnSelect(void) { page_row(11); }
static void settingsPage_exit_OnSelect(void)  { SetExitMenuFlag(); }

/* Draw the note now, over its own background: for the prompts that wait inside a row. */
static void note_now(const char *text)
{
    MenuEntry *n = entry_at(Page_note);
    note(text);
    n->fg = (uintptr_t)s_note;
    DrawMenuItemBackgroundMasked(n->x, n->y, n->dwWidth, n->dwHeight);
    CallDrawMenuItem(n);
}

/* After a prompt answered from a pad: the button let go, and its press no menu's. */
static void release_wait(void)
{
    int32_t frames;
    for (frames = 0; frames < 100 && _fire; frames++)
        MenuCheckControls();
    _shortFire = 0;
    InputSDL_ClearKeyState();
}

/* ===================================================================== CONTROLS */

static int32_t s_setupDev = -1;     /* CONTROLLER SETUP's pad */

static void control_show(int32_t p, char *out, int32_t cap)
{
    const char *pref = InputSDL_PlayerPref(p);
    const int d = InputSDL_PlayerDevice(p);
    char name[64];
    if (d >= 0)
        InputSDL_DeviceLabel(d, name, sizeof name);
    if (!pref[0]) {
        if (d >= 0) {
            char line[80];
            snprintf(line, sizeof line, "AUTO: %s", name);
            upper(out, cap, line);
        } else
            snprintf(out, (size_t)cap, "%s", p ? "AUTO: NO PAD" : "AUTO: KEYBOARD");
    } else if (!SDL_strcasecmp(pref, "keyboard") || !SDL_strcasecmp(pref, "none"))
        upper(out, cap, pref);
    else if (d >= 0)
        upper(out, cap, name);
    else
        snprintf(out, (size_t)cap, "NOT CONNECTED");
}

/* PLAYER n: AUTO -> KEYBOARD (player 1) / NONE (player 2) -> each pad -> AUTO (SWOS 2020's list
 * of controls, pl1SelectControls / pl2SelectControls). */
static void control_change(int32_t p, int32_t dir)
{
    char opts[2 + 8][48];
    int32_t n = 0, at = 0, i, d;
    const char *cur = InputSDL_PlayerPref(p);

    snprintf(opts[n++], sizeof opts[0], "%s", "");
    snprintf(opts[n++], sizeof opts[0], "%s", p ? "none" : "keyboard");
    for (d = 0; d < InputSDL_MaxDevices() && n < (int32_t)(sizeof opts / sizeof opts[0]); d++)
        if (InputSDL_DeviceOpen(d))
            InputSDL_DevicePref(d, opts[n++], (int)sizeof opts[0]);
    for (i = 0; i < n; i++)
        if (!SDL_strcasecmp(opts[i], cur))
            at = i;
    at = step(at, dir, n);
    InputSDL_SetPlayerPref(p, opts[at]);
    save();
}

static void select_show(char *out, int32_t cap) { snprintf(out, (size_t)cap, "PRESS FIRE"); }

/* SELECT JOYPAD (SWOS 2020's selectJoypadWithButtonPress): the next pad a button is pressed on
 * is this player's. ESC leaves it as it was. */
static void select_joypad(int32_t p)
{
    int dev = -1, button = -1, found = 0, any = 0;
    char line[96];
    for (int d = 0; d < InputSDL_MaxDevices(); d++)
        any |= InputSDL_DeviceOpen(d);
    if (!any) {
        note("NO JOYPAD IS CONNECTED");
        return;
    }
    snprintf(line, sizeof line, "PRESS A BUTTON ON PLAYER %d'S PAD   ESC: LEAVE IT", (int)p + 1);
    note_now(line);
    InputSDL_PadPressBegin();
    InputSDL_CaptureKey(1);
    for (;;) {
        MenuCheckControls();
        if (InputSDL_CapturedKey() == SDL_SCANCODE_ESCAPE)
            break;
        if (InputSDL_CapturedKey() != SDL_SCANCODE_UNKNOWN)
            InputSDL_CaptureKey(1);         /* (another key: still waiting) */
        if (InputSDL_PadPressPoll(-1, &dev, &button)) {
            found = 1;
            break;
        }
        CallDrawMenuItem(entry_at(Page_note));
    }
    InputSDL_CaptureKey(0);
    if (found) {
        char name[64];
        InputSDL_GivePlayerDevice(p, dev);
        InputSDL_DeviceLabel(dev, name, sizeof name);
        snprintf(line, sizeof line, "%s SELECTED", name);
        upper(s_note, sizeof s_note, line);
        save();
    } else
        note("");
    release_wait();
}

static void setup_show(int32_t p, char *out, int32_t cap)
{
    snprintf(out, (size_t)cap, "%s", InputSDL_PlayerDevice(p) >= 0 ? "BUTTONS, STICK" : "NO PAD");
}

static void joypad_setup(int32_t p)
{
    uint8_t *menu;
    s_setupDev = InputSDL_PlayerDevice(p);
    if (s_setupDev < 0) {
        note(p ? "PLAYER 2 HAS NO PAD" : "PLAYER 1 PLAYS WITH THE KEYBOARD ALONE");
        return;
    }
    menu = MenuJson_Load(JOYPAD_CONFIG_NAME);
    if (!menu) {
        fprintf(stderr, "[settings] cannot show %s\n", JOYPAD_CONFIG_NAME);
        return;
    }
    ShowMenu(menu);
}

static void p1_show(char *o, int32_t c)       { control_show(0, o, c); }
static void p2_show(char *o, int32_t c)       { control_show(1, o, c); }
static void p1_change(int32_t dir)            { control_change(0, dir); }
static void p2_change(int32_t dir)            { control_change(1, dir); }
static void p1_select(int32_t dir)            { (void)dir; select_joypad(0); }
static void p2_select(int32_t dir)            { (void)dir; select_joypad(1); }
static void p1_setup_show(char *o, int32_t c) { setup_show(0, o, c); }
static void p2_setup_show(char *o, int32_t c) { setup_show(1, o, c); }
static void p1_setup(int32_t dir)             { (void)dir; joypad_setup(0); }
static void p2_setup(int32_t dir)             { (void)dir; joypad_setup(1); }

static void keys_show(char *o, int32_t c)     { snprintf(o, (size_t)c, "REDEFINE"); }
static void keys_change(int32_t dir)
{
    extern void ControlsMenu_Show(void);    /* the keyboard's keys (src/controls_menu.c) */
    (void)dir;
    ControlsMenu_Show();
}

static void autodetect_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", on_off(InputSDL_AutoDetect())); }
static void autodetect_change(int32_t dir)
{
    (void)dir;
    InputSDL_SetAutoDetect(!InputSDL_AutoDetect());
    note(InputSDL_AutoDetect() ? "A PAD PLUGGED IN GOES TO A FREE PLAYER" : "A PAD PLUGGED IN WAITS FOR SELECT JOYPAD");
    save();
}

static void shoulders_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", on_off(InputSDL_ShouldersOff())); }
static void shoulders_change(int32_t dir)
{
    (void)dir;
    InputSDL_SetShouldersOff(!InputSDL_ShouldersOff());
    note(InputSDL_ShouldersOff() ? "SHOULDERS AND STICK CLICKS ARE NO FIRE OR BENCH"
                                 : "SHOULDERS CAN BE FIRE OR BENCH");
    save();
}

static void bench_show(char *o, int32_t c)
{
    static const char *const k[3] = { "BUTTON OR TAPS", "TAPS ONLY", "BUTTON ONLY" };
    snprintf(o, (size_t)c, "%s", k[InputSDL_CallBench()]);
}
static void bench_change(int32_t dir)
{
    InputSDL_SetCallBench(step(InputSDL_CallBench(), dir, 3));
    note(_gDosGameplay ? "THE PC STYLE'S SECOND FIRE BUTTON AND THREE TAPS"
                       : "THE AMIGA STYLE HAS NO 2ND BUTTON: TAPS ALWAYS");
    save();
}

static void deadzone_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%d%%", InputSDL_StickDeadzonePercent()); }
static void deadzone_change(int32_t dir)
{
    int v = InputSDL_StickDeadzonePercent() + 5 * dir;
    v = v < 5 ? 90 : v > 90 ? 5 : v;
    InputSDL_SetStickDeadzone(v);
    if (g_arcadeOpts)
        g_arcadeOpts->stick_deadzone = v;
    note("HOW FAR A STICK MUST GO BEFORE IT COUNTS");
    save();
}

static const Row k_controlRows[] = {
    { "PLAYER 1",                 p1_show,         p1_change },
    { "  SELECT JOYPAD",          select_show,     p1_select },
    { "  CONTROLLER SETUP",       p1_setup_show,   p1_setup },
    { "PLAYER 2",                 p2_show,         p2_change },
    { "  SELECT JOYPAD",          select_show,     p2_select },
    { "  CONTROLLER SETUP",       p2_setup_show,   p2_setup },
    { "KEYBOARD KEYS",            keys_show,       keys_change },
    { "AUTO-DETECT JOYPADS",      autodetect_show, autodetect_change },
    { "DISABLE SHOULDER BUTTONS", shoulders_show,  shoulders_change },
    { "CALL BENCH",               bench_show,      bench_change },
    { "STICK DEAD ZONE",          deadzone_show,   deadzone_change },
};
static const Page k_controlsPage = { "CONTROLS", k_controlRows, (int32_t)(sizeof k_controlRows / sizeof k_controlRows[0]) };

/* ===================================================================== VIDEO */

static PlatformInitOptions s_none;      /* (no options published: nothing to show, nothing kept) */
static PlatformInitOptions *O(void) { return g_arcadeOpts ? g_arcadeOpts : &s_none; }

static void winmode_show(char *o, int32_t c)
{
    static const char *const k[3] = { "WINDOWED", "FULLSCREEN", "EXCLUSIVE FULLSCREEN" };
    snprintf(o, (size_t)c, "%s", k[Platform_GetFullscreenMode()]);
}
static void winmode_change(int32_t dir)
{
    const PlatformFullscreenMode m = (PlatformFullscreenMode)step((int32_t)Platform_GetFullscreenMode(), dir, 3);
    Platform_SetFullscreenMode(m);
    O()->fullscreen = m;
    save();
}

static void scale_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%dX", Platform_GetWindowScale()); }
static void scale_change(int32_t dir)
{
    const int s = 1 + step(Platform_GetWindowScale() - 1, dir, 4);
    Platform_SetWindowScale(s);
    O()->win_scale = s;
    if (Platform_GetFullscreenMode() != PLATFORM_FULLSCREEN_NONE)
        note("THE WINDOW'S SIZE, WHEN IT IS ONE");
    save();
}

static void vsync_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", on_off(O()->vsync)); }
static void vsync_change(int32_t dir) { (void)dir; O()->vsync = !O()->vsync; later(); save(); }

static void wide_show(char *o, int32_t c)
{
    static const char *const k[3] = { "OFF", "ON", "STRETCH" };
    snprintf(o, (size_t)c, "%s", k[O()->widescreen >= 0 && O()->widescreen <= 2 ? O()->widescreen : 0]);
}
static void wide_change(int32_t dir) { O()->widescreen = step(O()->widescreen, dir, 3); later(); save(); }

static void scroll_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", on_off(O()->subpixel_scroll)); }
static void scroll_change(int32_t dir) { (void)dir; O()->subpixel_scroll = !O()->subpixel_scroll; later(); save(); }

static void scaling_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", O()->scaling_smooth ? "SMOOTH" : "SHARP"); }
static void scaling_change(int32_t dir) { (void)dir; O()->scaling_smooth = !O()->scaling_smooth; later(); save(); }

static void monitor_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%d", O()->screen + 1); }
static void monitor_change(int32_t dir)
{
    int n = SDL_GetNumVideoDisplays();
    O()->screen = step(O()->screen, dir, n > 0 ? n : 1);
    later();
    save();
}

static void renderer_show(char *o, int32_t c)
{
    static const char *const k[3] = { "SOFTWARE", "GPU", "SDL" };
    snprintf(o, (size_t)c, "%s", k[O()->renderer >= 0 && O()->renderer <= 2 ? O()->renderer : 1]);
}
static void renderer_change(int32_t dir) { O()->renderer = (PlatformRendererMode)step((int32_t)O()->renderer, dir, 3); later(); save(); }

static void cursor_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", s_cursorSteady ? "STEADY" : "FLASHING"); }
static void cursor_change(int32_t dir)
{
    (void)dir;
    MenuLook_SetCursor(!s_cursorSteady);
    O()->menu_cursor = s_cursorSteady;
    save();
}

static const Row k_videoRows[] = {
    { "WINDOW MODE",        winmode_show,  winmode_change },
    { "WINDOW SCALE",       scale_show,    scale_change },
    { "VSYNC",              vsync_show,    vsync_change },
    { "WIDESCREEN",         wide_show,     wide_change },
    { "SMOOTH SCROLLING",   scroll_show,   scroll_change },
    { "SCALING",            scaling_show,  scaling_change },
    { "MONITOR",            monitor_show,  monitor_change },
    { "RENDERER",           renderer_show, renderer_change },
    { "MENU CURSOR",        cursor_show,   cursor_change },
};
static const Page k_videoPage = { "VIDEO OPTIONS", k_videoRows, (int32_t)(sizeof k_videoRows / sizeof k_videoRows[0]) };

/* ===================================================================== AUDIO */

extern uint16_t _menuMusic;
extern int _gNoAmigaSound;

static void volumes_apply(void)
{
    extern void AudioSDL_SetKindVolumes(int music, int chants, int commentary, int effects);
    PlatformInitOptions *o = O();
    AudioSDL_SetKindVolumes(o->vol_music, o->vol_chants, o->vol_commentary, o->vol_effects);
    save();
}

static void vol_show(int v, char *o, int32_t c) { snprintf(o, (size_t)c, "%d%%", v); }
static void vol_change(int *v, int32_t dir)
{
    *v += 10 * dir;
    *v = *v < 0 ? 100 : *v > 100 ? 0 : *v;
    volumes_apply();
}
static void volm_show(char *o, int32_t c) { vol_show(O()->vol_music, o, c); }
static void volc_show(char *o, int32_t c) { vol_show(O()->vol_chants, o, c); }
static void volt_show(char *o, int32_t c) { vol_show(O()->vol_commentary, o, c); }
static void vole_show(char *o, int32_t c) { vol_show(O()->vol_effects, o, c); }
static void volm_change(int32_t d) { vol_change(&O()->vol_music, d); }
static void volc_change(int32_t d) { vol_change(&O()->vol_chants, d); }
static void volt_change(int32_t d) { vol_change(&O()->vol_commentary, d); }
static void vole_change(int32_t d) { vol_change(&O()->vol_effects, d); }

static void menumusic_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", on_off(_menuMusic)); }
static void menumusic_change(int32_t dir)
{
    extern void ChangeMenuMusic(MenuEntry *entry);      /* OPTIONS' row: the music, and [options] */
    (void)dir;
    ChangeMenuMusic(entry_at(Page_note));
}

/* The menus' music again, in the style chosen: StopMusic, then the menu's song. */
static void music_restart(void)
{
    extern void StopMusic(void);
    extern void TryPlayMusic(void);
    if (!_menuMusic)
        return;
    StopMusic();
    TryPlayMusic();
}

static void sound_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", DosSound_Style() ? "PC" : "AMIGA"); }
static void sound_change(int32_t dir)
{
    (void)dir;
    if (_gNoAmigaSound) {
        note("NO AMIGA SOUND: THE SETUP MADE NONE");
        return;
    }
    DosSound_SetStyle(!DosSound_Style());
    O()->sound_style = DosSound_Style();
    save();
}

static void music_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", DosMusic_Style() ? "PC" : "AMIGA"); }
static void music_change(int32_t dir)
{
    (void)dir;
    if (_gNoAmigaSound) {
        note("NO AMIGA MUSIC: THE SETUP MADE NONE");
        return;
    }
    DosMusic_SetStyle(!DosMusic_Style());
    O()->music_style = DosMusic_Style();
    music_restart();
    save();
}

static void pcmusic_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", DosMusic_MenuRecording() ? "MP3" : "MIDI"); }
static void pcmusic_change(int32_t dir)
{
    (void)dir;
    DosMusic_SetMenuRecording(!DosMusic_MenuRecording());
    O()->menu_recording = DosMusic_MenuRecording();
    if (DosMusic_Style())
        music_restart();
    else
        note("WITH MUSIC: PC");
    save();
}

static void pctitle_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", DosMusic_TitleMidi() ? "MIDI" : "MP3"); }
static void pctitle_change(int32_t dir)
{
    (void)dir;
    DosMusic_SetTitleMidi(!DosMusic_TitleMidi());
    O()->title_midi = DosMusic_TitleMidi();
    note(DosMusic_Style() ? "FROM THE NEXT START" : "WITH MUSIC: PC; FROM THE NEXT START");
    save();
}

static void commentary_show(char *o, int32_t c)
{
    static const char *const k[3] = { "OFF", "ON", "AUTO" };
    snprintf(o, (size_t)c, "%s", k[O()->commentary >= 0 && O()->commentary <= 2 ? O()->commentary : 2]);
}
static void commentary_change(int32_t dir)
{
    O()->commentary = step(O()->commentary, dir, 3);
    DosSound_SetCommentary(O()->commentary);
    note(O()->commentary == 2 ? "AUTO: WITH THE PC SOUND" : "THE PC GAME'S COMMENTARY");
    save();
}

static void mono_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", O()->audio_mono ? "MONO" : "STEREO"); }
static void mono_change(int32_t dir) { (void)dir; O()->audio_mono = !O()->audio_mono; later(); save(); }

static const Row k_audioRows[] = {
    { "MUSIC VOLUME",           volm_show,       volm_change },
    { "CROWD CHANTS VOLUME",    volc_show,       volc_change },
    { "COMMENTARY VOLUME",      volt_show,       volt_change },
    { "BALL AND WHISTLE",       vole_show,       vole_change },
    { "MENU MUSIC",             menumusic_show,  menumusic_change },
    { "MATCH SOUNDS",           sound_show,      sound_change },
    { "MUSIC",                  music_show,      music_change },
    { "PC MENU MUSIC",          pcmusic_show,    pcmusic_change },
    { "PC TITLE MUSIC",         pctitle_show,    pctitle_change },
    { "COMMENTARY",             commentary_show, commentary_change },
    { "OUTPUT",                 mono_show,       mono_change },
};
static const Page k_audioPage = { "AUDIO OPTIONS", k_audioRows, (int32_t)(sizeof k_audioRows / sizeof k_audioRows[0]) };

/* ===================================================================== GAMEPLAY */

static void style_show(char *o, int32_t c)
{
    static const char *const k[GAMEPLAY_STYLES] = { "AMIGA", "PC DOS", "XBOX", "AMIGA 60", "MOON" };
    snprintf(o, (size_t)c, "%s", k[_gGameplayStyle >= 0 && _gGameplayStyle < GAMEPLAY_STYLES ? _gGameplayStyle : 0]);
}
static void style_change(int32_t dir)
{
    extern void GameplayStyle_Set(int style, int persist);  /* OPTIONS' GAMEPLAY row (menus.c) */
    GameplayStyle_Set(step(_gGameplayStyle, dir, GAMEPLAY_STYLES), 1);
}

static void viewer_apply(void)
{
    extern void ReplayV2_ViewerConfigure(int32_t overlay, int32_t halfSpeed);
    ReplayV2_ViewerConfigure(O()->replay_frame_overlay, O()->replay_half_speed);
    save();
}

static void autosave_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", on_off(O()->replay_auto_save)); }
static void autosave_change(int32_t dir)
{
    (void)dir;
    O()->replay_auto_save = !O()->replay_auto_save;
    note(O()->replay_auto_save ? "A DATED COPY OF EVERY MATCH, FROM THE NEXT START"
                               : "LATEST.SRP ONLY, FROM THE NEXT START");
    save();
}

static void hlspeed_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", O()->replay_half_speed ? "HALF" : "NORMAL"); }
static void hlspeed_change(int32_t dir) { (void)dir; O()->replay_half_speed = !O()->replay_half_speed; viewer_apply(); }

static void overlay_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", on_off(O()->replay_frame_overlay)); }
static void overlay_change(int32_t dir) { (void)dir; O()->replay_frame_overlay = !O()->replay_frame_overlay; viewer_apply(); }

static const Row k_gameplayRows[] = {
    { "GAME STYLE",              style_show,    style_change },
    { "AUTO SAVE REPLAYS",       autosave_show, autosave_change },
    { "HIGHLIGHT DEFAULT SPEED", hlspeed_show,  hlspeed_change },
    { "FRAME INFO OVERLAY",      overlay_show,  overlay_change },
};
static const Page k_gameplayPage = { "GAMEPLAY OPTIONS", k_gameplayRows, (int32_t)(sizeof k_gameplayRows / sizeof k_gameplayRows[0]) };

/* ===================================================================== SETTINGS (the hub) */

static void page_show(const Page *page)
{
    uint8_t *menu = MenuJson_Load(SETTINGS_PAGE_NAME);
    if (!menu) {
        fprintf(stderr, "[settings] cannot show %s\n", SETTINGS_PAGE_NAME);
        return;
    }
    s_page = page;
    note("FIRE OR RIGHT CHANGES A SETTING, LEFT GOES BACK");
    ShowMenu(menu);
}

enum { Hub_header, Hub_subTitle, Hub_controls, Hub_video, Hub_audio, Hub_gameplay, Hub_note, Hub_exit };
static char s_hubNote[64];

static void settingsMenu_OnInit(void) { entry_at(Hub_note)->fg = (uintptr_t)s_hubNote; }
static void settingsMenu_OnDraw(void) { entry_at(Hub_note)->fg = (uintptr_t)s_hubNote; }
static void settingsMenu_controls_OnSelect(void) { page_show(&k_controlsPage); }
static void settingsMenu_video_OnSelect(void)    { page_show(&k_videoPage); }
static void settingsMenu_audio_OnSelect(void)    { page_show(&k_audioPage); }
static void settingsMenu_gameplay_OnSelect(void) { page_show(&k_gameplayPage); }
static void settingsMenu_exit_OnSelect(void)     { SetExitMenuFlag(); }

/* ===================================================================== CONTROLLER SETUP */

enum {                                      /* joypadConfigMenu.json's entries */
    Jpc_header = 0,
    Jpc_name = 15, Jpc_guid, Jpc_id, Jpc_numButtons, Jpc_numAxes, Jpc_powerLevel, Jpc_rawX, Jpc_rawY,
    Jpc_fireButton, Jpc_benchButton, Jpc_deadZoneXPos, Jpc_deadZoneYPos, Jpc_deadZoneXNeg, Jpc_deadZoneYNeg,
    Jpc_decYNeg, Jpc_yNegPercentage, Jpc_incYNeg,
    Jpc_decYPos, Jpc_yPosPercentage, Jpc_incYPos,
    Jpc_incXNeg, Jpc_xNegPercentage, Jpc_decXNeg,
    Jpc_incXPos, Jpc_xPosPercentage, Jpc_decXPos,
    Jpc_exit,
    Jpc_kNumEntries
};
/* joypadConfig.mnu's exports: the cut-off box. */
enum { kBoxX1 = 160, kBoxY1 = 60, kBoxX2 = 280, kBoxY2 = 166 };
/* InputSDL_PadCut's k: x+, x-, y+, y- */
enum { CUT_XP, CUT_XN, CUT_YP, CUT_YN };

static char s_jpcName[64], s_jpcGuid[40], s_jpcHeader[48];
static char s_jpcNum[5][16];            /* id, buttons, axes, raw x, raw y */
static const char *s_jpcPower = "";
static char s_jpcButtons[2][24], s_jpcCut[4][12], s_jpcPct[4][8];
static int  s_jpcDirs[4];               /* up, down, left, right: what the stick reads */

static int cut_pct(int k)
{
    const int c = InputSDL_PadCut(s_setupDev, k);
    return c ? c : InputSDL_StickDeadzonePercent();
}

static void jpc_values(void)
{
    char guid[33];
    int id, buttons, axes, rawX, rawY, k;
    if (!InputSDL_DeviceOpen(s_setupDev))
        return;
    InputSDL_DeviceInfo(s_setupDev, guid, &id, &buttons, &axes, &s_jpcPower, &rawX, &rawY, s_jpcDirs);
    {
        char label[64];
        InputSDL_DeviceLabel(s_setupDev, label, sizeof label);
        upper(s_jpcName, sizeof s_jpcName, label);
    }
    upper(s_jpcGuid, sizeof s_jpcGuid, guid);
    snprintf(s_jpcNum[0], sizeof s_jpcNum[0], "%d", id);
    snprintf(s_jpcNum[1], sizeof s_jpcNum[1], "%d", buttons);
    snprintf(s_jpcNum[2], sizeof s_jpcNum[2], "%d", axes);
    snprintf(s_jpcNum[3], sizeof s_jpcNum[3], "%d", rawX);
    snprintf(s_jpcNum[4], sizeof s_jpcNum[4], "%d", rawY);
    for (k = 0; k < 2; k++) {
        char nm[24];
        InputSDL_PadButtonName(s_setupDev, k, InputSDL_PadButton(s_setupDev, k), nm, sizeof nm);
        upper(s_jpcButtons[k], sizeof s_jpcButtons[k], nm);
    }
    for (k = 0; k < 4; k++) {
        const int pct = cut_pct(k);
        /* SWOS 2020 shows each cut-off as the axis value it is, and its percent in the box. */
        snprintf(s_jpcCut[k], sizeof s_jpcCut[k], "%d", (k == CUT_XN || k == CUT_YN ? -1 : 1) * 32767 * pct / 100);
        snprintf(s_jpcPct[k], sizeof s_jpcPct[k], "%d%%", pct);
    }
}

static void jpc_texts(void)
{
    static const int32_t cutEntry[4] = { Jpc_deadZoneXPos, Jpc_deadZoneXNeg, Jpc_deadZoneYPos, Jpc_deadZoneYNeg };
    static const int32_t pctEntry[4] = { Jpc_xPosPercentage, Jpc_xNegPercentage, Jpc_yPosPercentage, Jpc_yNegPercentage };
    int32_t k;
    entry_at(Jpc_header)->fg = (uintptr_t)(s_jpcHeader[0] ? s_jpcHeader : "CONTROLLER SETUP");
    entry_at(Jpc_name)->fg = (uintptr_t)s_jpcName;
    entry_at(Jpc_guid)->fg = (uintptr_t)s_jpcGuid;
    entry_at(Jpc_id)->fg = (uintptr_t)s_jpcNum[0];
    entry_at(Jpc_numButtons)->fg = (uintptr_t)s_jpcNum[1];
    entry_at(Jpc_numAxes)->fg = (uintptr_t)s_jpcNum[2];
    entry_at(Jpc_powerLevel)->fg = (uintptr_t)s_jpcPower;
    entry_at(Jpc_rawX)->fg = (uintptr_t)s_jpcNum[3];
    entry_at(Jpc_rawY)->fg = (uintptr_t)s_jpcNum[4];
    entry_at(Jpc_fireButton)->fg = (uintptr_t)s_jpcButtons[0];
    entry_at(Jpc_benchButton)->fg = (uintptr_t)s_jpcButtons[1];
    for (k = 0; k < 4; k++) {
        entry_at(cutEntry[k])->fg = (uintptr_t)s_jpcCut[k];
        entry_at(pctEntry[k])->fg = (uintptr_t)s_jpcPct[k];
    }
}

/* The box (SWOS 2020's drawDeadZoneBox): the stick's whole travel, its cut-offs as the inner
 * rectangle, and each quarter lit while the stick is in it -- outside the cut-offs (it counts)
 * or inside (it does not). In the menu's own colours: 14 green, 10 red, 13 / 3 blue, 1 the frame. */
static void fill(int x1, int y1, int x2, int y2, int colour)
{
    if (x2 > x1 && y2 > y1)
        DrawMenuItemBackground((uint16_t)colour, (uint16_t)x1, (uint16_t)y1, (uint16_t)(x2 - x1), (uint16_t)(y2 - y1));
}

static void jpc_box(void)
{
    const int cx = kBoxX1 + (kBoxX2 - kBoxX1 + 1) / 2, cy = kBoxY1 + (kBoxY2 - kBoxY1 + 1) / 2;
    const int up = s_jpcDirs[0], down = s_jpcDirs[1], left = s_jpcDirs[2], right = s_jpcDirs[3];
    const int any = up || down || left || right;
    const int x1 = cx - (cx - kBoxX1) * cut_pct(CUT_XN) / 100, x2 = cx + (kBoxX2 - cx) * cut_pct(CUT_XP) / 100;
    const int y1 = cy - (cy - kBoxY1) * cut_pct(CUT_YN) / 100, y2 = cy + (kBoxY2 - cy) * cut_pct(CUT_YP) / 100;
    int rawX = 0, rawY = 0;
    sscanf(s_jpcNum[3], "%d", &rawX);
    sscanf(s_jpcNum[4], "%d", &rawY);
    /* the quarters: lit green where the stick reads a direction, red where it sits in the cut-off */
    fill(kBoxX1, kBoxY1, cx, cy, (up || left) ? 14 : 13);
    fill(cx, kBoxY1, kBoxX2, cy, (up || right) ? 14 : 13);
    fill(kBoxX1, cy, cx, kBoxY2, (down || left) ? 14 : 13);
    fill(cx, cy, kBoxX2, kBoxY2, (down || right) ? 14 : 13);
    fill(x1, y1, x2, y2, !any && (rawX || rawY) ? 10 : 3);
    /* the frame and the cross */
    fill(kBoxX1, kBoxY1, kBoxX2 + 1, kBoxY1 + 1, 1);
    fill(kBoxX1, kBoxY2, kBoxX2 + 1, kBoxY2 + 1, 1);
    fill(kBoxX1, kBoxY1, kBoxX1 + 1, kBoxY2 + 1, 1);
    fill(kBoxX2, kBoxY1, kBoxX2 + 1, kBoxY2 + 1, 1);
    fill(cx, kBoxY1, cx + 1, kBoxY2, 1);
    fill(kBoxX1, cy, kBoxX2, cy + 1, 1);
    /* where the stick is */
    {
        const int px = cx + (int)((int64_t)rawX * (kBoxX2 - cx) / 32768);
        const int py = cy + (int)((int64_t)rawY * (kBoxY2 - cy) / 32768);
        fill(px - 1, py - 1, px + 2, py + 2, 2);
    }
}

static void joypadConfig_joypadConfigMenuOnInit(void)
{
    jpc_values();
    jpc_texts();
}

static void joypadConfig_joypadConfigMenuOnDraw(void)
{
    if (!InputSDL_DeviceOpen(s_setupDev)) {     /* (unplugged: SWOS 2020 leaves too) */
        SetExitMenuFlag();
        return;
    }
    jpc_values();
    jpc_texts();
    jpc_box();
}

/* FIRE / BENCH (SWOS 2020's selectButton): the next button pressed on this pad. ESC leaves it,
 * BACKSPACE gives back the default. */
static void jpc_select(int bench)
{
    int dev = -1, button = -1, done = 0;
    MenuEntry *h = entry_at(Jpc_header);
    snprintf(s_jpcHeader, sizeof s_jpcHeader, "PRESS THE %s BUTTON", bench ? "BENCH" : "FIRE");
    h->fg = (uintptr_t)s_jpcHeader;
    CallDrawMenuItem(h);
    InputSDL_PadPressBegin();
    InputSDL_CaptureKey(1);
    while (!done) {
        const int32_t key = InputSDL_CapturedKey();
        MenuCheckControls();
        if (!InputSDL_DeviceOpen(s_setupDev))
            break;
        if (key == SDL_SCANCODE_ESCAPE)
            break;
        if (key == SDL_SCANCODE_BACKSPACE || key == SDL_SCANCODE_DELETE) {
            InputSDL_SetPadButton(s_setupDev, bench, -1);
            done = 1;
        } else if (key != SDL_SCANCODE_UNKNOWN)
            InputSDL_CaptureKey(1);
        if (!done && InputSDL_PadPressPoll(s_setupDev, &dev, &button)) {
            if (InputSDL_SetPadButton(s_setupDev, bench, button) != 0) {
                snprintf(s_jpcHeader, sizeof s_jpcHeader, "NOT THAT ONE: AGAIN");
                CallDrawMenuItem(h);
                continue;
            }
            done = 1;
        }
        CallDrawMenuItem(h);
    }
    InputSDL_CaptureKey(0);
    s_jpcHeader[0] = '\0';
    release_wait();
    save();
}

static void joypadConfig_selectFireButton(void)  { jpc_select(0); }
static void joypadConfig_selectBenchButton(void) { jpc_select(1); }

/* A cut-off by `by` percent (the +/- boxes: 1; fire on the value: 5, round from 95 to 5). */
static void jpc_cut(int k, int by)
{
    int v = cut_pct(k) + by;
    if (by == 5 && v > 95)
        v = 5;
    v = v < 5 ? 5 : v > 95 ? 95 : v;
    InputSDL_SetPadCut(s_setupDev, k, v);
    save();
}

static void joypadConfig_enterDeadZoneXPos(void) { jpc_cut(CUT_XP, 5); }
static void joypadConfig_enterDeadZoneXNeg(void) { jpc_cut(CUT_XN, 5); }
static void joypadConfig_enterDeadZoneYPos(void) { jpc_cut(CUT_YP, 5); }
static void joypadConfig_enterDeadZoneYNeg(void) { jpc_cut(CUT_YN, 5); }
static void joypadConfig_increaseXPos(void)      { jpc_cut(CUT_XP, 1); }
static void joypadConfig_decreaseXPos(void)      { jpc_cut(CUT_XP, -1); }
static void joypadConfig_increaseXNeg(void)      { jpc_cut(CUT_XN, 1); }
static void joypadConfig_decreaseXNeg(void)      { jpc_cut(CUT_XN, -1); }
static void joypadConfig_increaseYPos(void)      { jpc_cut(CUT_YP, 1); }
static void joypadConfig_decreaseYPos(void)      { jpc_cut(CUT_YP, -1); }
static void joypadConfig_increaseYNeg(void)      { jpc_cut(CUT_YN, 1); }
static void joypadConfig_decreaseYNeg(void)      { jpc_cut(CUT_YN, -1); }

/* ===================================================================== F6: quick save */

/* SWOS 2020's F6 (controls.cpp:2202): in a menu, with a game in progress, the SAVE of the main
 * menu without going back to it. SWOS 2020 does not offer it on the main menu (which has SAVE),
 * in a continue/abort question, or on PLAY MATCH once the match is played; here, neither on
 * PLAY MATCH at all (a fixture half set up), nor during the season's turn, a text field, a
 * dialog, or --career-golden's run. */
void SettingsMenus_MenuFrame(void)
{
    extern uint16_t _gameType;
    extern uint16_t _seasonUpdateInProgress;
    extern volatile uint16_t _gTextInputActive;
    extern uint8_t _saveFileTitle[];
    extern int32_t Menu_IsMainMenu(void);
    extern void PlayMatchMenuInit(void);
    extern void QuickSave_FromMenu(void);
    extern int32_t CareerAutopilot_Active(void);
    static int32_t s_busy;
    const Menu *menu = (const Menu *)_currentMenu;

    if (!InputSDL_TakeQuickSave() || s_busy)
        return;
    if (_gameType == 0 || !_saveFileTitle[0] || Menu_IsMainMenu() || _seasonUpdateInProgress ||
        _gTextInputActive || (menu && menu->onInit == (MenuVoidFn)PlayMatchMenuInit) || CareerAutopilot_Active()) {
        fprintf(stderr, "[quicksave] F6: not here\n");
        return;
    }
    fprintf(stderr, "[quicksave] F6: %s\n", (const char *)_saveFileTitle);
    s_busy = 1;
    QuickSave_FromMenu();
    s_busy = 0;
    _shortFire = 0;                 /* (the dialog's last press is not this menu's) */
    InputSDL_ClearKeyState();
}

/* ===================================================================== registration */

#define HANDLER(fn) { #fn, (void *)fn, SYM_FUNC }

static const MenuSymbol k_handlers[] = {
    HANDLER(settingsMenu_OnInit),
    HANDLER(settingsMenu_OnDraw),
    HANDLER(settingsMenu_controls_OnSelect),
    HANDLER(settingsMenu_video_OnSelect),
    HANDLER(settingsMenu_audio_OnSelect),
    HANDLER(settingsMenu_gameplay_OnSelect),
    HANDLER(settingsMenu_exit_OnSelect),
    HANDLER(settingsPage_OnInit),
    HANDLER(settingsPage_OnDraw),
    HANDLER(settingsPage_row0_OnSelect),
    HANDLER(settingsPage_row1_OnSelect),
    HANDLER(settingsPage_row2_OnSelect),
    HANDLER(settingsPage_row3_OnSelect),
    HANDLER(settingsPage_row4_OnSelect),
    HANDLER(settingsPage_row5_OnSelect),
    HANDLER(settingsPage_row6_OnSelect),
    HANDLER(settingsPage_row7_OnSelect),
    HANDLER(settingsPage_row8_OnSelect),
    HANDLER(settingsPage_row9_OnSelect),
    HANDLER(settingsPage_row10_OnSelect),
    HANDLER(settingsPage_row11_OnSelect),
    HANDLER(settingsPage_exit_OnSelect),
    HANDLER(joypadConfig_joypadConfigMenuOnInit),
    HANDLER(joypadConfig_joypadConfigMenuOnDraw),
    HANDLER(joypadConfig_selectFireButton),
    HANDLER(joypadConfig_selectBenchButton),
    HANDLER(joypadConfig_enterDeadZoneXPos),
    HANDLER(joypadConfig_enterDeadZoneXNeg),
    HANDLER(joypadConfig_enterDeadZoneYPos),
    HANDLER(joypadConfig_enterDeadZoneYNeg),
    HANDLER(joypadConfig_increaseXPos),
    HANDLER(joypadConfig_decreaseXPos),
    HANDLER(joypadConfig_increaseXNeg),
    HANDLER(joypadConfig_decreaseXNeg),
    HANDLER(joypadConfig_increaseYPos),
    HANDLER(joypadConfig_decreaseYPos),
    HANDLER(joypadConfig_increaseYNeg),
    HANDLER(joypadConfig_decreaseYNeg),
};

#undef HANDLER

void SettingsMenus_Init(void)
{
    static int32_t registered;
    if (registered)
        return;
    registered = 1;
    MenuJson_RegisterSymbols(k_handlers, (int)(sizeof k_handlers / sizeof k_handlers[0]));
}

/* OPTIONS -> SETTINGS. */
void SettingsMenus_Show(void)
{
    uint8_t *menu;
    SettingsMenus_Init();
    menu = MenuJson_Load(SETTINGS_MENU_NAME);
    if (!menu) {
        fprintf(stderr, "[settings] cannot show %s\n", SETTINGS_MENU_NAME);
        return;
    }
    s_hubNote[0] = '\0';
    ShowMenu(menu);
}

/* --settings-menus-selftest: every page's rows shown and changed both ways and back, with no
 * window or device, and the three menus loaded. Returns the number of failures. */
int SettingsMenus_SelfTest(void)
{
    static const Page *const pages[] = { &k_controlsPage, &k_videoPage, &k_audioPage, &k_gameplayPage };
    int fails = 0;
    SettingsMenus_Init();
    for (size_t p = 0; p < sizeof pages / sizeof pages[0]; p++) {
        if (pages[p]->count > kRows) {
            fprintf(stderr, "[settings-selftest] FAIL: %s has %d rows, the page %d\n",
                    pages[p]->title, (int)pages[p]->count, kRows);
            fails++;
        }
        for (int32_t i = 0; i < pages[p]->count; i++) {
            char v[kValueLen];
            pages[p]->rows[i].show(v, kValueLen);
            if (!v[0] || strlen(v) > 21) {
                fprintf(stderr, "[settings-selftest] FAIL: %s / %s shows \"%s\"\n", pages[p]->title, pages[p]->rows[i].label, v);
                fails++;
            }
        }
    }
    {   /* the menu cursor: steady is one colour, flashing the table as it was */
        extern uint16_t _kColorTableShine[8];
        uint16_t was[8];
        memcpy(was, _kColorTableShine, sizeof was);
        MenuLook_SetCursor(1);
        for (int i = 1; i < 8; i++)
            if (_kColorTableShine[i] != _kColorTableShine[0]) { fails++; fprintf(stderr, "[settings-selftest] FAIL: steady cursor flashes\n"); break; }
        MenuLook_SetCursor(0);
        if (memcmp(was, _kColorTableShine, sizeof was)) { fails++; fprintf(stderr, "[settings-selftest] FAIL: flashing cursor not restored\n"); }
    }
    {   /* the three menus load (their entry count is checked by --menu-selftest) */
        static const char *const names[] = { SETTINGS_MENU_NAME, SETTINGS_PAGE_NAME, JOYPAD_CONFIG_NAME };
        for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
            if (!MenuJson_Load(names[i])) { fails++; fprintf(stderr, "[settings-selftest] FAIL: %s does not load\n", names[i]); }
    }
    fprintf(stderr, "[settings-selftest] %s\n", fails ? "FAILED" : "ok");
    return fails;
}

#endif /* SWOS_SDL_BUILD */

#ifndef SWOS_SDL_BUILD
typedef int settings_menus_amiga_has_none;     /* (ISO C: a translation unit is not empty) */
#endif
