/*
 * input_sdl.c -- SDL keyboard / gamepad → Amiga input state.
 *
 * The Amiga code reads input through three globals that the original
 * hardware kept live:
 *
 *   keyState[128]   populated by Int2KeyboardHandler from CIAA SDR.
 *                   Indexed by Amiga raw scancode.
 *   joy0Dat         Paula JOY0DAT register snapshot: directional bits
 *                   for player 1 stick.
 *   joy1Dat         Same for player 2.
 *   ciaa_pra        CIAA Port A. Bit 7 (active low) is the Port-0 fire
 *                   button -- clear = pressed, set = released.
 *
 * On SDL we own all four as software globals (swos_globals.c) and write
 * them from this file based on SDL keyboard + game-controller events.
 *
 * Keyboard bindings (PC key → Amiga equivalent):
 *   Arrow keys        player-1 joystick direction + keyState
 *   Right-Alt / Right-Ctrl  player-1 fire (FS-UAE default for menu select)
 *   Left-Ctrl / Space / Enter  also fire (legacy convenience bindings)
 *   Enter             keyState[AKEY_ENTER] (menus also accept it)
 *   Escape            quit (handled in platform_sdl.c)
 */

#include "platform_sdl.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "match_step.h"   /* MatchStep_LocalBenchButton: the DOS second button */
#include "dos_mode.h"     /* _gDosGameplay */
#ifdef SWOS_WASM_BUILD
#include <emscripten.h>     /* EMSCRIPTEN_KEEPALIVE: the page's on-screen controls */
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

/* ---- Game-side globals (defined in swos_globals.c) ---- */
extern uint8_t  _keyState[128];
extern uint16_t _joy0Dat;
extern uint16_t _joy1Dat;
extern uint8_t  _ciaa_pra;
/* gameState transitions into the ST_STARTING_GAME (21) … ST_PENALTIES (31)
 * range while a match is on screen; everything else (frontend, season-end
 * menus, edit screens, text entry) is outside that window. Used to gate
 * the SPACE multi-role binding so SPACE during a live match falls through
 * to the Amiga highlight-capture path only, not the menu fire/skip path. */
extern uint16_t _gameState;

/* ---- Keyboard FIFO consumed by GetKey() ----
 * The Amiga build's level-2 ISR pushes raw scan codes into keyBuffer
 * and increments keyCount; GetKey() pops one, runs it through
 * keycodeLut[] to get ASCII, and exposes that as currentKey for menu
 * input + InputTextFilterKey. On SDL we have no ISR, so we push
 * directly here from SDL_KEYDOWN. */
extern volatile uint8_t  _keyBuffer[12];
extern volatile uint16_t _keyCount;

/* ---- Amiga raw-scancode table (full set, used by both
 * keyState[] and keyBuffer FIFO). keycodeLut[] in
 * swos_data_bulk_imports.c maps these back to ASCII for GetKey(). */
#define AKEY_BACKTICK 0x00
#define AKEY_1        0x01
#define AKEY_0        0x0A
#define AKEY_MINUS    0x0B
#define AKEY_EQUALS   0x0C
#define AKEY_BACKSL   0x0D
#define AKEY_Q        0x10
#define AKEY_LBRACK   0x1A
#define AKEY_RBRACK   0x1B
#define AKEY_A        0x20
#define AKEY_SEMI     0x29
#define AKEY_QUOTE    0x2A
#define AKEY_Z        0x31
#define AKEY_COMMA    0x38
#define AKEY_PERIOD   0x39
#define AKEY_SLASH    0x3A
#define AKEY_SPACE    0x40
#define AKEY_BSPACE   0x41
#define AKEY_TAB      0x42
#define AKEY_ENTER    0x44
#define AKEY_ESC      0x45
#define AKEY_DEL      0x46
#define AKEY_UP       0x4C
#define AKEY_DOWN     0x4D
#define AKEY_RIGHT    0x4E
#define AKEY_LEFT     0x4F
#define AKEY_F1       0x50
#define AKEY_F10      0x59
#define AKEY_LSHIFT   0x60
#define AKEY_RSHIFT   0x61
#define AKEY_LCTRL    0x63
#define AKEY_LALT     0x64
#define AKEY_RALT     0x65

/* ---- Controller buttons -> in-match key functions --------------------------
 * Play uses only TWO pad buttons — A and B for fire, B doubling as skip — plus
 * direction. Everything else the match understands is a KEY: pause, the
 * statistics overlay, the two replay kinds, quit. On a
 * console there is no keyboard at all, so without this none of it is reachable;
 * on a desktop pad it meant reaching for the keyboard mid-match.
 *
 * Pushed as RAW Amiga scancodes into the same _keyBuffer a keypress goes
 * through, so `_keycodeLut` and every consumer downstream are untouched — this
 * adds a producer, not a code path. (That LUT is also why the match compares
 * against 0x50 for pause: 0x50 is ASCII 'P' AFTER the translation, not the raw
 * scancode for P, which is 0x19.)
 *
 * Deliberately NOT mapped: A and B (play), the D-pad and sticks (direction), the
 * shoulder buttons (below), and GUIDE — which is HOME on a Switch and belongs to the system.
 *
 * `hold` is the key a button sends once it has been held PAD_HOLD_MS. ESC is a hold
 * because it ends the match: a handheld (Retroid) has no keyboard and no reachable
 * ESC, so leaving training needs a pad button, and a hold is one nobody presses by
 * accident. A button with a hold sends its `tap` key on RELEASE, and only when the
 * hold did not fire -- so Start pauses as it did, one tap's length later. */
#define PAD_HOLD_MS 700
static const struct { int button; uint8_t tap, hold; const char *what; } k_padKeys[] = {
    { SDL_CONTROLLER_BUTTON_START,      0x19, 0x45, "P   pause; hold: ESC quit the match" },
    { SDL_CONTROLLER_BUTTON_BACK,       0x21, 0,    "S   statistics"                   },
    { SDL_CONTROLLER_BUTTON_Y,          0x13, 0,    "R   instant replay"               },
    { SDL_CONTROLLER_BUTTON_X,          0x25, 0,    "H   replay after the match"       },
    /* The shoulder buttons do NOTHING until there is a CONTROLS menu to assign them
     * in (ReportBot 0030, the owner's decision). They used to send UP and DOWN -- the
     * keys that call the two substitutes' benches -- but those are the cursor keys
     * too: they moved a menu's cursor and steered the keyboard's player, by accident,
     * mid-match. The bench is still the game's own way: three taps left or right
     * with the ball out of play, or the second fire button in the PC style. */
    /* Quit is on a stick CLICK on purpose: it ends the match, and a click is
     * not something you hit by accident mid-game the way a face button is --
     * except a handheld's grip can, so it is a hold too, with no tap. */
    { SDL_CONTROLLER_BUTTON_RIGHTSTICK, 0,    0x45, "hold: ESC quit the match"         },
};

static int sdl_to_amiga(SDL_Keycode k)
{
    /* Letters: SDLK_a..SDLK_z are contiguous (0x61..0x7A), and the
     * Amiga A-Z scancodes lay out in QWERTY rows, so use a small LUT. */
    if (k >= SDLK_a && k <= SDLK_z) {
        static const uint8_t letter_to_amiga[26] = {
            /* a */ AKEY_A,        /* b */ 0x35,           /* c */ 0x33,
            /* d */ 0x22,          /* e */ 0x12,           /* f */ 0x23,
            /* g */ 0x24,          /* h */ 0x25,           /* i */ 0x17,
            /* j */ 0x26,          /* k */ 0x27,           /* l */ 0x28,
            /* m */ 0x37,          /* n */ 0x36,           /* o */ 0x18,
            /* p */ 0x19,          /* q */ AKEY_Q,         /* r */ 0x13,
            /* s */ 0x21,          /* t */ 0x14,           /* u */ 0x16,
            /* v */ 0x34,          /* w */ 0x11,           /* x */ 0x32,
            /* y */ 0x15,          /* z */ AKEY_Z
        };
        return letter_to_amiga[k - SDLK_a];
    }
    /* Top-row digits 0..9. Amiga has 1..9 then 0. */
    if (k >= SDLK_1 && k <= SDLK_9) return AKEY_1 + (k - SDLK_1);
    if (k == SDLK_0)                return AKEY_0;

    switch (k) {
    case SDLK_ESCAPE:       return AKEY_ESC;
    case SDLK_AC_BACK:      return AKEY_ESC;    /* (Android's back button: ESC) */
    case SDLK_RETURN:       return AKEY_ENTER;
    case SDLK_KP_ENTER:     return AKEY_ENTER;
    case SDLK_SPACE:        return AKEY_SPACE;
    case SDLK_BACKSPACE:    return AKEY_BSPACE;
    case SDLK_TAB:          return AKEY_TAB;
    case SDLK_DELETE:       return AKEY_DEL;
    case SDLK_UP:           return AKEY_UP;
    case SDLK_DOWN:         return AKEY_DOWN;
    case SDLK_LEFT:         return AKEY_LEFT;
    case SDLK_RIGHT:        return AKEY_RIGHT;
    case SDLK_F1:           return AKEY_F1;
    case SDLK_F2:           return AKEY_F1 + 1;
    case SDLK_F3:           return AKEY_F1 + 2;
    case SDLK_F4:           return AKEY_F1 + 3;
    case SDLK_F5:           return AKEY_F1 + 4;
    case SDLK_F6:           return AKEY_F1 + 5;
    case SDLK_F7:           return AKEY_F1 + 6;
    case SDLK_F8:           return AKEY_F1 + 7;
    case SDLK_F9:           return AKEY_F1 + 8;
    case SDLK_F10:          return AKEY_F10;
    case SDLK_MINUS:        return AKEY_MINUS;
    case SDLK_EQUALS:       return AKEY_EQUALS;
    case SDLK_BACKSLASH:    return AKEY_BACKSL;
    case SDLK_LEFTBRACKET:  return AKEY_LBRACK;
    case SDLK_RIGHTBRACKET: return AKEY_RBRACK;
    case SDLK_SEMICOLON:    return AKEY_SEMI;
    case SDLK_QUOTE:        return AKEY_QUOTE;
    case SDLK_COMMA:        return AKEY_COMMA;
    case SDLK_PERIOD:       return AKEY_PERIOD;
    case SDLK_SLASH:        return AKEY_SLASH;
    case SDLK_BACKQUOTE:    return AKEY_BACKTICK;
    case SDLK_LSHIFT:       return AKEY_LSHIFT;
    case SDLK_RSHIFT:       return AKEY_RSHIFT;
    case SDLK_LCTRL:        return AKEY_LCTRL;
    case SDLK_LALT:         return AKEY_LALT;
    case SDLK_RALT:         return AKEY_RALT;
    default:                return -1;
    }
}

/* ---- Tracked keyboard state for joystick/fire synthesis ---- */
static int s_kbd_up, s_kbd_down, s_kbd_left, s_kbd_right, s_kbd_fire;
/* The arrows as they physically are, beside the four above, which are what the game
 * is told. A keyboard, unlike a stick, can hold LEFT and RIGHT at once -- and does,
 * for a few frames, every time a player rolls from one arrow to the other. DOS SWOS
 * never lets that through: its keyboard handler (SetKeyboardControlFlags, swos.asm)
 * sets a direction and CLEARS ITS OPPOSITE on every press, so the arrow pressed last
 * wins. Passing both on instead let Player1StatusProc's fixed order decide -- Right
 * over Left, Up over Down -- so a roll from RIGHT to LEFT, or from UP to DOWN, kept
 * the player going the old way until the old key came up: about a hundred frames a
 * match of a keyboard player turning late, always mid-turn and in two of the four
 * directions, so worse attacking one way than the other (ReportBot 0014: "ball
 * control ... second half"). And as SWOS 2020 does for its online players ("improved
 * keyboard control", controls.cpp), letting go of the newer arrow with the older one
 * still held goes back to the older one, where DOS would leave the player stopped. */
static int s_kbd_held_up, s_kbd_held_down, s_kbd_held_left, s_kbd_held_right;

static void kbd_arrow(SDL_Keycode k, int down)
{
    switch (k) {
    case SDLK_UP:    s_kbd_held_up    = down; break;
    case SDLK_DOWN:  s_kbd_held_down  = down; break;
    case SDLK_LEFT:  s_kbd_held_left  = down; break;
    case SDLK_RIGHT: s_kbd_held_right = down; break;
    default: return;
    }
    if (down) {
        /* The press wins over its opposite. */
        if (k == SDLK_UP)    { s_kbd_up = 1;    s_kbd_down  = 0; }
        if (k == SDLK_DOWN)  { s_kbd_down = 1;  s_kbd_up    = 0; }
        if (k == SDLK_LEFT)  { s_kbd_left = 1;  s_kbd_right = 0; }
        if (k == SDLK_RIGHT) { s_kbd_right = 1; s_kbd_left  = 0; }
    } else {
        /* The release goes, and an opposite still held takes over. */
        if (k == SDLK_UP)    { s_kbd_up = 0;    s_kbd_down  = s_kbd_held_down; }
        if (k == SDLK_DOWN)  { s_kbd_down = 0;  s_kbd_up    = s_kbd_held_up; }
        if (k == SDLK_LEFT)  { s_kbd_left = 0;  s_kbd_right = s_kbd_held_right; }
        if (k == SDLK_RIGHT) { s_kbd_right = 0; s_kbd_left  = s_kbd_held_left; }
    }
}
/* Fire is bound to THREE keys (Right-Ctrl / Left-Ctrl / Enter) but the Amiga
 * has ONE button, so the three have to behave as one: held while ANY of them
 * is down. A single shared s_kbd_fire flag got that wrong — releasing one key
 * cleared it even with another still held, and since a held key sends no
 * further KEYDOWN, fire stayed off until the player pressed something again.
 * Mid-charge that reads as a release: the fire counter goes positive and the
 * hold becomes a pass (counter <= 4) or an early shot, then nothing at all
 * while the key is still down — i.e. "I'm holding fire and he won't shoot".
 * One bit per key, OR'd. */
#define KBD_FIRE_RCTRL  0x1
#define KBD_FIRE_LCTRL  0x2
#define KBD_FIRE_RETURN 0x4
#define KBD_FIRE_BOUND  0x8     /* the player's own fire key ([input] key_fire) */
static int s_kbd_fire_keys;

/* ---- The player's own keys: [input] key_up / key_down / key_left / key_right / key_fire ----
 *
 * By SCANCODE -- the physical key, whatever the layout or the shift state -- and IN
 * ADDITION to the built-in ones (the cursor keys; either CTRL or ENTER to fire), which
 * keep working, so a menu can always be driven. A bound key is a control and nothing
 * else: it does not reach the game's key buffer, so W can steer without calling the
 * bench and S without opening the statistics. The one exception is a text field
 * (_gTextInputActive), where a letter is a letter.
 *
 * Why it exists: a tester steered with WASD and fired with LEFT SHIFT through an external
 * key remapper, and reported fire presses that the game never read (ReportBot 0041). A
 * remapper feeds the game synthetic key events of its own timing; bound here, the game
 * reads the keyboard itself. */
enum { BIND_UP, BIND_DOWN, BIND_LEFT, BIND_RIGHT, BIND_FIRE, BIND_COUNT };
static SDL_Scancode s_bind[BIND_COUNT];        /* SDL_SCANCODE_UNKNOWN: not bound */
static const char *const k_bindName[BIND_COUNT] = { "key_up", "key_down", "key_left", "key_right", "key_fire" };
extern volatile uint16_t _gTextInputActive;    /* input_text.c */

/* `which`: 0 up, 1 down, 2 left, 3 right, 4 fire. `name`: an SDL scancode name ("W",
 * "Left Shift", "Space"), or "" for none. Returns 0, or 1 for a name SDL does not know. */
int InputSDL_SetKeyBinding(int which, const char *name)
{
    SDL_Scancode sc = SDL_SCANCODE_UNKNOWN;
    if (which < 0 || which >= BIND_COUNT)
        return 1;
    if (name && name[0]) {
        sc = SDL_GetScancodeFromName(name);
        if (sc == SDL_SCANCODE_UNKNOWN) {
            fprintf(stderr, "[input] %s = \"%s\": no such key (use SDL's names: W, Left Shift, Space ...)\n",
                    k_bindName[which], name);
            return 1;
        }
    }
    s_bind[which] = sc;
    if (sc != SDL_SCANCODE_UNKNOWN)
        fprintf(stderr, "[input] %s = %s\n", k_bindName[which], SDL_GetScancodeName(sc));
    return 0;
}

/* The bound key's name, "" when not bound: for SWOS.ini and the redefine screen. */
const char *InputSDL_KeyBindingName(int which)
{
    if (which < 0 || which >= BIND_COUNT || s_bind[which] == SDL_SCANCODE_UNKNOWN)
        return "";
    return SDL_GetScancodeName(s_bind[which]);
}

static int bound_action(SDL_Scancode sc)
{
    int i;
    if (sc == SDL_SCANCODE_UNKNOWN || _gTextInputActive)
        return -1;
    for (i = 0; i < BIND_COUNT; i++)
        if (s_bind[i] == sc)
            return i;
    return -1;
}

/* ---- A press is never lost between two reads: [input] latch_presses ----
 *
 * The match reads fire as a LEVEL, once a frame: down or up at that moment. A press whose
 * key-down and key-up both arrive between two reads -- a very short tap, or events the
 * system delivered late and together -- was on and off again before anything looked, and
 * was lost whole. With the latch (on by default) a key-down that arrived since the last
 * read makes that read "pressed" even if the key is already up. It changes nothing
 * unless a press would otherwise have vanished.
 *
 * Either way the read says what happened (SRP_READ_FIRE_EVENT / _LATCHED, kept in a live
 * match's recording): "I pressed and nothing happened" can then be answered from the
 * recording -- the press reached the game, or it never did. */
static int s_latch_on = 1;
static int s_fire_event;            /* a fire key went down since the last read */
static int s_fire_event_pad;        /* ... or a fire button of player 1's game controller */
static uint8_t s_fire_evidence;     /* SRP_READ_FIRE_* of the last InputSDL_LocalMatchControls */

void InputSDL_SetPressLatch(int on) { s_latch_on = on != 0; }
int InputSDL_PressLatch(void) { return s_latch_on; }

/* The CONTROLS menu asks for a key (controls_menu.c): the next key to go down is kept
 * here, by scancode, and does nothing else. */
static int s_capture;
static SDL_Scancode s_captured;

void InputSDL_CaptureKey(int on)
{
    s_capture = on != 0;
    s_captured = SDL_SCANCODE_UNKNOWN;
}

int32_t InputSDL_CapturedKey(void)
{
    return (int32_t)s_captured;
}
uint8_t InputSDL_LastFireEvidence(void) { return s_fire_evidence; }

/* The last InputSDL_LocalMatchControls was thrown away -- GGPO would not take an input
 * this pass, and the frame will be read again. Its press is handed back, or a tap during
 * a stall would be spent on a read nobody used. */
void InputSDL_UnreadMatchControls(void)
{
    if (s_fire_evidence & 0x04)
        s_fire_event = 1;       /* key or pad: the next read is the same player's either way */
    s_fire_evidence = 0;
}

/* The fire level as the game should read it, and what to say about it. `withPad`: the
 * pad is this player's too (not when two share one machine as keyboard against pad). */
static int sample_fire(int level, int withPad, uint8_t *evidence)
{
    uint8_t bits = 0;
    if (s_fire_event || (withPad && s_fire_event_pad)) {
        bits |= 0x04;                           /* SRP_READ_FIRE_EVENT */
        if (!level && s_latch_on) {
            level = 1;
            bits |= 0x08;                       /* SRP_READ_FIRE_LATCHED */
        }
    }
    s_fire_event = 0;
    s_fire_event_pad = 0;
    if (evidence)
        *evidence = bits;
    return level;
}
/* SDL-only "skip auto-advance pause" input. Pressing SPACE (kb) or the
 * second button on a controller sets pauseSkipRequested so dwell loops
 * (e.g. season-end menu chains, future auto-advance pauses) can bail
 * early. Independent of fire so it doesn't interfere with menu select. */
static int s_kbd_skip;
volatile uint16_t _pauseSkipRequested;
/* Set during the season-end / catch-up transition (_Career_AdvanceToNextSeason_
 * UpdateCompetitions). While set there is no live interactive match on screen —
 * used to let SPACE drive the season-skip even though gameState can be left
 * lingering in the match-state range (21..31) after the player's last match. */
extern uint16_t _seasonUpdateInProgress;

/* Every connected controller is opened (up to INPUTSDL_MAX_DEVICES), and two of
 * them are PLAYED: s_player_dev[0] drives player 1, shared with the keyboard, and
 * s_player_dev[1] player 2 exclusively (no keyboard binding for P2 -- matches the
 * Amiga, which always read P2 from physical Port 0). A new device takes the lowest
 * free player, so plugging a single pad always lands on P1, and a device that is
 * left over waits: it steps in when a played one is unplugged, or claims P1 by
 * pressing fire on the intro (InputSDL_SetIntroClaim). That is for a player with
 * more pads connected than players -- three, say, and the one they want is the
 * Amiga joystick on an Arduino Leonardo, which was opened third and so never at all.
 *
 * Each device is either an SDL_GameController (high-level mapped pad) or a raw
 * SDL_Joystick (no mapping in SDL's DB -- we read axes/buttons by index). The raw
 * fallback is what makes generic "Gameport to USB" adapters and other DIY pads work
 * without the user having to author a gamecontrollerdb.txt entry. */
#define INPUTSDL_NUM_PADS    2
#define INPUTSDL_MAX_DEVICES 8
static SDL_GameController *s_dev_gc[INPUTSDL_MAX_DEVICES];
static SDL_Joystick       *s_dev_raw[INPUTSDL_MAX_DEVICES];  /* one of gc[i] / raw[i] is set */
static SDL_JoystickID      s_dev_id[INPUTSDL_MAX_DEVICES];   /* for matching DEVICEREMOVED */
static int                 s_player_dev[INPUTSDL_NUM_PADS] = { -1, -1 };
static int                 s_intro_claim;                     /* the intro is on screen */
static int                 s_frames;                          /* PostFrame calls, for the test knobs */

static int dev_open(int d) { return s_dev_gc[d] || s_dev_raw[d]; }

static const char *dev_name(int d)
{
    const char *nm = s_dev_gc[d] ? SDL_GameControllerName(s_dev_gc[d])
                   : s_dev_raw[d] ? SDL_JoystickName(s_dev_raw[d]) : NULL;
    return nm ? nm : "controller";
}

/* Player 1's controller by name (SWOS 2020's getControllerName, the lobby's "USING ..."), or NULL
 * when no pad is player 1's: the keyboard. */
const char *InputSDL_Player1DeviceName(void)
{
    const int d = s_player_dev[0];
    return d >= 0 && d < INPUTSDL_MAX_DEVICES && dev_open(d) ? dev_name(d) : NULL;
}

/* Whether player 1 / 2 (0, 1) plays with a pad (else the keyboard). */
static int pref_no_pad(int player);

/* Two players, one pad (InputSDL_PostFrame): player 1's pad is player 2's, the keyboard player 1's
 * -- unless player 2 is set to have none. */
static int split_pad(void)
{
    extern uint16_t _twoPlayers;
    extern int32_t  NetplayMatch_Enabled(void);
    return _twoPlayers != 0 && s_player_dev[1] < 0 && s_player_dev[0] >= 0 && !NetplayMatch_Enabled() &&
           !pref_no_pad(1);
}

/* Whether player 1 / 2 (0, 1) plays on a pad -- as InputSDL_PostFrame reads them: when two players
 * share one pad, it is player 2's and player 1 has the keyboard (Playaveli's, 2026-11-09). */
int InputSDL_PlayerHasPad(int player)
{
    const int split = split_pad();
    const int d = player == 0 ? (split ? -1 : s_player_dev[0]) : player == 1 ? s_player_dev[split ? 0 : 1] : -1;
    return d >= 0 && d < INPUTSDL_MAX_DEVICES && dev_open(d);
}

/* ---- SWOS 2020's CONTROLS MENU and CONTROLLER SETUP (SWOS 2020 port, agent A) ----
 *
 * SWOS 2020 (controls/joypads.cpp, controlOptionsMenu.cpp, joypadConfigMenu.cpp) lets each
 * player choose a control -- the keyboard, none (player 2), or a joypad by its GUID, the n-th
 * of several alike -- and keeps per joypad GUID its FIRE and BENCH buttons and four stick
 * cut-offs. Here, in SWOS.ini [input]:
 *
 *   pad1, pad2        ""/auto (as before: the pads as they come), keyboard (player 1: no pad),
 *                     none (player 2: no control), or <guid>[#n]
 *   auto_detect_pads  AUTO-DETECT JOYPADS: a pad plugged in later is given to a free AUTO
 *                     player (on, as before); off, it waits until SELECT JOYPAD picks it
 *   pad_config        <guid>:<fire>:<bench>:<x+>:<x->:<y+>:<y->;...  -- a button number, -1
 *                     the default (A or B / any button; X / none), a cut-off in percent of the
 *                     stick's travel, 0 = stick_deadzone
 *   shoulder_buttons  DISABLE SHOULDER BUTTONS (on): the shoulders and stick clicks cannot be
 *                     FIRE or BENCH; off, they can
 *   call_bench        CALL BENCH: both (the 2nd fire button or three taps), taps, button
 *
 * None of it touches the match: it decides which device reads as which player and how, and
 * what a device reads goes on into the same registers / input bits as ever. All defaults are
 * the behaviour before it existed. */
typedef struct {
    char    guid[33];
    int8_t  fire, bench;        /* a button, -1 the default */
    uint8_t cut[4];             /* x+, x-, y+, y-: percent, 0 = [input] stick_deadzone */
} PadConfig;
#define PAD_CONFIGS 8
static PadConfig s_padCfg[PAD_CONFIGS];
static int       s_padCfgCount;
static char      s_pref[INPUTSDL_NUM_PADS][48];    /* "" auto, "keyboard", "none", guid[#n] */
static int       s_autoDetect = 1;
static int       s_shouldersOff = 1;
static int       s_callBench;                       /* INPUTSDL_BENCH_* */
static int       s_assigningAtStart;                /* InputSDL_Init: the pads already there */
enum { INPUTSDL_BENCH_BOTH = 0, INPUTSDL_BENCH_TAPS = 1, INPUTSDL_BENCH_BUTTON = 2 };

static SDL_Joystick *dev_joystick(int d)
{
    return s_dev_gc[d] ? SDL_GameControllerGetJoystick(s_dev_gc[d]) : s_dev_raw[d];
}

static void dev_guid(int d, char out[33])
{
    SDL_Joystick *j = dev_open(d) ? dev_joystick(d) : NULL;
    out[0] = '\0';
    if (j)
        SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(j), out, 33);
}

/* Which of several pads alike (the same GUID) this one is, 0 the first: SWOS 2020's
 * joypadOccurrence -- two Arduino Leonardos have one GUID between them. */
static int dev_occurrence(int d)
{
    char g[33], o[33];
    int n = 0;
    dev_guid(d, g);
    for (int e = 0; e < d; e++) {
        if (!dev_open(e))
            continue;
        dev_guid(e, o);
        if (strcmp(o, g) == 0)
            n++;
    }
    return n;
}

/* The [input] padN value naming this device. */
void InputSDL_DevicePref(int d, char *out, int cap)
{
    char g[33];
    int n;
    if (d < 0 || d >= INPUTSDL_MAX_DEVICES || !dev_open(d)) {
        snprintf(out, (size_t)cap, "%s", "");
        return;
    }
    dev_guid(d, g);
    n = dev_occurrence(d);
    if (n)
        snprintf(out, (size_t)cap, "%s#%d", g, n);
    else
        snprintf(out, (size_t)cap, "%s", g);
}

static int pref_auto(int p)     { return s_pref[p][0] == '\0' || SDL_strcasecmp(s_pref[p], "auto") == 0; }
static int pref_no_pad(int p)   { return SDL_strcasecmp(s_pref[p], "keyboard") == 0 || SDL_strcasecmp(s_pref[p], "none") == 0; }

static int pref_names_dev(int p, int d)
{
    char mine[48];
    if (pref_auto(p) || pref_no_pad(p))
        return 0;
    InputSDL_DevicePref(d, mine, sizeof mine);
    return SDL_strcasecmp(mine, s_pref[p]) == 0;
}

static int dev_played(int d)
{
    for (int p = 0; p < INPUTSDL_NUM_PADS; ++p)
        if (s_player_dev[p] == d)
            return 1;
    return 0;
}

/* Give player p an open device nobody plays: the one they chose, else (AUTO, and
 * `any`) the first. */
static void fill_player(int p, int any)
{
    if (s_player_dev[p] >= 0 || pref_no_pad(p))
        return;
    for (int d = 0; d < INPUTSDL_MAX_DEVICES; ++d) {
        if (dev_open(d) && !dev_played(d) && pref_names_dev(p, d)) {
            s_player_dev[p] = d;
            return;
        }
    }
    if (!any || !pref_auto(p))
        return;
    for (int d = 0; d < INPUTSDL_MAX_DEVICES; ++d) {
        int wanted = 0;
        for (int q = 0; q < INPUTSDL_NUM_PADS; ++q)
            wanted |= q != p && pref_names_dev(q, d);   /* (the other player's own: it waits for them) */
        if (dev_open(d) && !dev_played(d) && !wanted) {
            s_player_dev[p] = d;
            return;
        }
    }
}

/* A device that has just been opened: the player who chose it, or the lowest free AUTO
 * player (at start, or with AUTO-DETECT JOYPADS on). -1 if it waits. */
static int dev_assign(int d)
{
    for (int p = 0; p < INPUTSDL_NUM_PADS; ++p) {
        if (s_player_dev[p] == d)
            return p;
    }
    for (int p = 0; p < INPUTSDL_NUM_PADS; ++p) {
        if (pref_names_dev(p, d)) {
            s_player_dev[p] = d;            /* (an AUTO pad it displaces waits) */
            return p;
        }
    }
    if (!s_autoDetect && !s_assigningAtStart)
        return -1;
    for (int p = 0; p < INPUTSDL_NUM_PADS; ++p) {
        if (s_player_dev[p] < 0 && pref_auto(p)) {
            s_player_dev[p] = d;
            return p;
        }
    }
    return -1;
}

const char *InputSDL_PlayerPref(int p)
{
    return p >= 0 && p < INPUTSDL_NUM_PADS ? s_pref[p] : "";
}

/* The CONTROLS MENU's choice for player p: "" auto, "keyboard", "none", or InputSDL_DevicePref's. */
void InputSDL_SetPlayerPref(int p, const char *pref)
{
    int q = 1 - p, was;
    if (p < 0 || p >= INPUTSDL_NUM_PADS)
        return;
    snprintf(s_pref[p], sizeof s_pref[p], "%s", pref && SDL_strcasecmp(pref, "auto") != 0 ? pref : "");
    was = s_player_dev[p];
    s_player_dev[p] = -1;
    for (int d = 0; d < INPUTSDL_MAX_DEVICES; ++d) {
        if (dev_open(d) && pref_names_dev(p, d)) {
            if (s_player_dev[q] == d)
                s_player_dev[q] = -1;       /* the other player had it: they look again below */
            s_player_dev[p] = d;
            break;
        }
    }
    fill_player(p, 1);
    fill_player(q, s_autoDetect);
    (void)was;
    fprintf(stderr, "[input] P%d control: %s -> %s\n", p + 1, s_pref[p][0] ? s_pref[p] : "auto",
            s_player_dev[p] >= 0 ? dev_name(s_player_dev[p]) : p ? "none" : "keyboard");
}

int  InputSDL_AutoDetect(void)          { return s_autoDetect; }
void InputSDL_SetAutoDetect(int on)     { s_autoDetect = on != 0; }
int  InputSDL_ShouldersOff(void)        { return s_shouldersOff; }
void InputSDL_SetShouldersOff(int on)   { s_shouldersOff = on != 0; }
int  InputSDL_CallBench(void)           { return s_callBench; }
void InputSDL_SetCallBench(int mode)    { s_callBench = mode >= 0 && mode <= 2 ? mode : 0; }

/* The device's config, NULL when it has none (all defaults). */
static PadConfig *dev_cfg(int d)
{
    char g[33];
    if (d < 0 || d >= INPUTSDL_MAX_DEVICES || !dev_open(d))
        return NULL;
    dev_guid(d, g);
    for (int i = 0; i < s_padCfgCount; i++)
        if (strcmp(s_padCfg[i].guid, g) == 0)
            return &s_padCfg[i];
    return NULL;
}

/* ... made when a setting of it changes. */
static PadConfig *dev_cfg_make(int d)
{
    PadConfig *c = dev_cfg(d);
    if (c || d < 0 || d >= INPUTSDL_MAX_DEVICES || !dev_open(d))
        return c;
    if (s_padCfgCount == PAD_CONFIGS)
        memmove(&s_padCfg[0], &s_padCfg[1], sizeof s_padCfg[0] * (PAD_CONFIGS - 1)), s_padCfgCount--;
    c = &s_padCfg[s_padCfgCount++];
    memset(c, 0, sizeof *c);
    dev_guid(d, c->guid);
    c->fire = c->bench = -1;
    return c;
}

static int cfg_is_default(const PadConfig *c)
{
    return c->fire < 0 && c->bench < 0 && !c->cut[0] && !c->cut[1] && !c->cut[2] && !c->cut[3];
}

/* [input] pad_config. Returns how many were read. */
int InputSDL_ParsePadConfig(const char *s)
{
    s_padCfgCount = 0;
    while (s && *s && s_padCfgCount < PAD_CONFIGS) {
        PadConfig c;
        int f, b, xp, xn, yp, yn, used = 0;
        char g[33];
        memset(&c, 0, sizeof c);
        if (sscanf(s, "%32[0-9a-fA-F]:%d:%d:%d:%d:%d:%d%n", g, &f, &b, &xp, &xn, &yp, &yn, &used) == 7 && used > 0) {
            snprintf(c.guid, sizeof c.guid, "%s", g);
            for (char *k = c.guid; *k; k++)
                if (*k >= 'A' && *k <= 'F') *k = (char)(*k - 'A' + 'a');   /* (SDL writes lower case) */
            c.fire = (int8_t)(f >= 0 && f < 64 ? f : -1);
            c.bench = (int8_t)(b >= 0 && b < 64 && b != f ? b : -1);
            c.cut[0] = (uint8_t)(xp >= 5 && xp <= 95 ? xp : 0);
            c.cut[1] = (uint8_t)(xn >= 5 && xn <= 95 ? xn : 0);
            c.cut[2] = (uint8_t)(yp >= 5 && yp <= 95 ? yp : 0);
            c.cut[3] = (uint8_t)(yn >= 5 && yn <= 95 ? yn : 0);
            if (!cfg_is_default(&c))
                s_padCfg[s_padCfgCount++] = c;
            s += used;
        }
        s = strchr(s, ';');
        if (s)
            s++;
    }
    return s_padCfgCount;
}

void InputSDL_FormatPadConfig(char *out, int cap)
{
    int at = 0;
    out[0] = '\0';
    for (int i = 0; i < s_padCfgCount && at < cap; i++) {
        const PadConfig *c = &s_padCfg[i];
        if (cfg_is_default(c))
            continue;
        at += snprintf(out + at, (size_t)(cap - at), "%s%s:%d:%d:%d:%d:%d:%d", at ? ";" : "", c->guid,
                       c->fire, c->bench, c->cut[0], c->cut[1], c->cut[2], c->cut[3]);
    }
    if (at >= cap)
        out[cap - 1] = '\0';
}

/* The stick, its cut-offs applied: each half of each axis scaled so that its own cut-off
 * lands on [input] stick_deadzone's radius, then the eight-way reading below as ever. All
 * four at 0 (or at the dead zone itself) is the reading before cut-offs existed. */
static void stick_directions(int32_t ax, int32_t ay, int *_up, int *_down, int *left, int *right);
static int32_t s_stickDeadzone;
static void stick_directions_cut(const PadConfig *c, int32_t ax, int32_t ay,
                                 int *_up, int *_down, int *left, int *right)
{
    if (c) {
        const int k = ax >= 0 ? 0 : 1, l = ay >= 0 ? 2 : 3;
        if (c->cut[k])
            ax = (int32_t)((int64_t)ax * s_stickDeadzone / (32768 * c->cut[k] / 100));
        if (c->cut[l])
            ay = (int32_t)((int64_t)ay * s_stickDeadzone / (32768 * c->cut[l] / 100));
    }
    stick_directions(ax, ay, _up, _down, left, right);
}

/* A shoulder button or a stick click (SWOS 2020's processEvent list): with DISABLE SHOULDER
 * BUTTONS on, never FIRE or BENCH. A plain joystick's buttons have no names: none is one. */
static int dev_is_shoulder(int d, int b)
{
    if (!s_dev_gc[d])
        return 0;
    return b == SDL_CONTROLLER_BUTTON_LEFTSHOULDER || b == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER ||
           b == SDL_CONTROLLER_BUTTON_LEFTSTICK || b == SDL_CONTROLLER_BUTTON_RIGHTSTICK
#if SDL_VERSION_ATLEAST(2, 0, 14)
           || (b >= SDL_CONTROLLER_BUTTON_PADDLE1 && b <= SDL_CONTROLLER_BUTTON_PADDLE4)
#endif
           ;
}

/* Buttons that are the menus' and the system's on a mapped pad (START, BACK, GUIDE, R3's hold):
 * never FIRE or BENCH. */
static int dev_is_reserved(int d, int b)
{
    return s_dev_gc[d] && (b == SDL_CONTROLLER_BUTTON_START || b == SDL_CONTROLLER_BUTTON_BACK ||
                           b == SDL_CONTROLLER_BUTTON_GUIDE || b == SDL_CONTROLLER_BUTTON_RIGHTSTICK);
}

static int dev_button_count(int d)
{
    if (s_dev_gc[d])
        return SDL_CONTROLLER_BUTTON_MAX;
    return s_dev_raw[d] ? SDL_JoystickNumButtons(s_dev_raw[d]) : 0;
}

static int dev_button(int d, int b)
{
    if (b < 0)
        return 0;
    if (s_dev_gc[d])
        return b < SDL_CONTROLLER_BUTTON_MAX && SDL_GameControllerGetButton(s_dev_gc[d], (SDL_GameControllerButton)b);
    return s_dev_raw[d] && b < SDL_JoystickNumButtons(s_dev_raw[d]) && SDL_JoystickGetButton(s_dev_raw[d], b);
}

/* The configured button, -1 the default, and -1 too for a shoulder while they are off. */
static int cfg_button(int d, const PadConfig *c, int bench)
{
    const int b = c ? (bench ? c->bench : c->fire) : -1;
    return b >= 0 && !(s_shouldersOff && dev_is_shoulder(d, b)) ? b : -1;
}

/* ---- for the CONTROLS MENU and CONTROLLER SETUP (src/controls_menu.c) ---- */

int InputSDL_PlayerDevice(int p)
{
    const int d = p >= 0 && p < INPUTSDL_NUM_PADS ? s_player_dev[p] : -1;
    return d >= 0 && dev_open(d) ? d : -1;
}

int InputSDL_DeviceOpen(int d) { return d >= 0 && d < INPUTSDL_MAX_DEVICES && dev_open(d); }
int InputSDL_MaxDevices(void)  { return INPUTSDL_MAX_DEVICES; }

/* The device's name, numbered when several alike are open ("ARDUINO LEONARDO 2"). */
void InputSDL_DeviceLabel(int d, char *out, int cap)
{
    int total = 0, rank = 0;
    const char *nm;
    if (!InputSDL_DeviceOpen(d)) {
        snprintf(out, (size_t)cap, "%s", "<NONE>");
        return;
    }
    nm = dev_name(d);
    for (int e = 0; e < INPUTSDL_MAX_DEVICES; e++) {
        if (dev_open(e) && strcmp(dev_name(e), nm) == 0) {
            total++;
            if (e == d)
                rank = total;
        }
    }
    if (total > 1)
        snprintf(out, (size_t)cap, "%s %d", nm, rank);
    else
        snprintf(out, (size_t)cap, "%s", nm);
}

/* NAME / GUID / ID / BUTTONS / AXES / POWER / RAW X / RAW Y, and what the stick reads now. */
void InputSDL_DeviceInfo(int d, char guid[33], int *id, int *buttons, int *axes, const char **power,
                         int *rawX, int *rawY, int dirs[4])
{
    SDL_Joystick *j = InputSDL_DeviceOpen(d) ? dev_joystick(d) : NULL;
    int up = 0, down = 0, left = 0, right = 0;
    dev_guid(d, guid);
    *id = j ? (int)SDL_JoystickInstanceID(j) : -1;
    *buttons = j ? SDL_JoystickNumButtons(j) : 0;
    *axes = j ? SDL_JoystickNumAxes(j) : 0;
    *rawX = j && *axes >= 1 ? SDL_JoystickGetAxis(j, 0) : 0;
    *rawY = j && *axes >= 2 ? SDL_JoystickGetAxis(j, 1) : 0;
    if (s_dev_gc[d]) {
        *rawX = SDL_GameControllerGetAxis(s_dev_gc[d], SDL_CONTROLLER_AXIS_LEFTX);
        *rawY = SDL_GameControllerGetAxis(s_dev_gc[d], SDL_CONTROLLER_AXIS_LEFTY);
    }
    *power = "UNKNOWN";
    switch (j ? SDL_JoystickCurrentPowerLevel(j) : SDL_JOYSTICK_POWER_UNKNOWN) {
    case SDL_JOYSTICK_POWER_EMPTY:  *power = "EMPTY"; break;
    case SDL_JOYSTICK_POWER_LOW:    *power = "LOW"; break;
    case SDL_JOYSTICK_POWER_MEDIUM: *power = "MEDIUM"; break;
    case SDL_JOYSTICK_POWER_FULL:   *power = "FULL"; break;
    case SDL_JOYSTICK_POWER_WIRED:  *power = "WIRED"; break;
    case SDL_JOYSTICK_POWER_MAX:    *power = "MAX"; break;
    default: break;
    }
    stick_directions_cut(dev_cfg(d), *rawX, *rawY, &up, &down, &left, &right);
    dirs[0] = up; dirs[1] = down; dirs[2] = left; dirs[3] = right;
}

/* FIRE (bench 0) or BENCH (1): the button, -1 the default. */
int InputSDL_PadButton(int d, int bench)
{
    const PadConfig *c = dev_cfg(d);
    return c ? (bench ? c->bench : c->fire) : -1;
}

/* What a button is called on this device: SDL's name for a mapped pad's, a number for a
 * plain joystick's; the default when b is -1. */
void InputSDL_PadButtonName(int d, int bench, int b, char *out, int cap)
{
    const char *n;
    if (b < 0) {
        if (InputSDL_DeviceOpen(d) && s_dev_gc[d])
            snprintf(out, (size_t)cap, "%s", bench ? "X" : "A OR B");
        else
            snprintf(out, (size_t)cap, "%s", bench ? "NONE" : "ANY");
        return;
    }
    n = InputSDL_DeviceOpen(d) && s_dev_gc[d] ? SDL_GameControllerGetStringForButton((SDL_GameControllerButton)b) : NULL;
    if (n)
        snprintf(out, (size_t)cap, "%s", n);
    else
        snprintf(out, (size_t)cap, "BUTTON %d", b);
}

/* Set FIRE or BENCH; the other one, if it was that button, takes the old one's place
 * (SWOS 2020's selectButton swaps). Returns 0, 1 for a button that cannot be either. */
int InputSDL_SetPadButton(int d, int bench, int b)
{
    PadConfig *c;
    if (b >= 0 && (dev_is_reserved(d, b) || (s_shouldersOff && dev_is_shoulder(d, b))))
        return 1;
    c = dev_cfg_make(d);
    if (!c)
        return 1;
    if (bench) {
        if (b >= 0 && c->fire == b)
            c->fire = c->bench;
        c->bench = (int8_t)b;
    } else {
        if (b >= 0 && c->bench == b)
            c->bench = c->fire;
        c->fire = (int8_t)b;
    }
    return 0;
}

/* The cut-offs: k 0 x+, 1 x-, 2 y+, 3 y-; percent, 0 = stick_deadzone. */
int InputSDL_PadCut(int d, int k)
{
    const PadConfig *c = dev_cfg(d);
    return c && k >= 0 && k < 4 ? c->cut[k] : 0;
}

void InputSDL_SetPadCut(int d, int k, int pct)
{
    PadConfig *c = k >= 0 && k < 4 ? dev_cfg_make(d) : NULL;
    if (c)
        c->cut[k] = (uint8_t)(pct >= 5 && pct <= 95 ? pct : 0);
}

int InputSDL_StickDeadzonePercent(void)
{
    return (int)((s_stickDeadzone * 100 + 16384) / 32768);
}

/* "Press any button": the buttons down when it began, so a press is a NEW one. */
static uint64_t s_pressBase[INPUTSDL_MAX_DEVICES];

static uint64_t dev_buttons_now(int d)
{
    uint64_t m = 0;
    const int n = dev_open(d) ? dev_button_count(d) : 0;
    for (int b = 0; b < n && b < 64; b++)
        if (dev_button(d, b))
            m |= (uint64_t)1 << b;
    return m;
}

void InputSDL_PadPressBegin(void)
{
    SDL_JoystickUpdate();
    for (int d = 0; d < INPUTSDL_MAX_DEVICES; d++)
        s_pressBase[d] = dev_buttons_now(d);
}

/* A button pressed since InputSDL_PadPressBegin, on device `only` (or any, -1): 1 and the
 * device and button, else 0. A button let go is forgotten, so pressing it again counts. */
int InputSDL_PadPressPoll(int only, int *dev, int *button)
{
    SDL_JoystickUpdate();
    for (int d = 0; d < INPUTSDL_MAX_DEVICES; d++) {
        uint64_t now, fresh;
        if (!dev_open(d) || (only >= 0 && d != only))
            continue;
        now = dev_buttons_now(d);
        fresh = now & ~s_pressBase[d];
        s_pressBase[d] &= now;
        for (int b = 0; b < 64; b++) {
            if (fresh & ((uint64_t)1 << b)) {
                *dev = d;
                *button = b;
                return 1;
            }
        }
    }
    return 0;
}

/* SELECT JOYPAD: device d is player p's (by name: the choice is kept), swapping with the other player. */
void InputSDL_GivePlayerDevice(int p, int d)
{
    char pref[48];
    if (p < 0 || p >= INPUTSDL_NUM_PADS || !InputSDL_DeviceOpen(d))
        return;
    InputSDL_DevicePref(d, pref, sizeof pref);
    if (s_player_dev[1 - p] == d) {
        s_player_dev[1 - p] = s_player_dev[p];          /* (theirs was mine: swap) */
        if (!pref_auto(1 - p)) {
            if (s_player_dev[1 - p] >= 0)
                InputSDL_DevicePref(s_player_dev[1 - p], s_pref[1 - p], sizeof s_pref[1 - p]);
            else
                s_pref[1 - p][0] = '\0';               /* (nothing to swap: they are AUTO again) */
        }
    }
    snprintf(s_pref[p], sizeof s_pref[p], "%s", pref);
    s_player_dev[p] = d;
    fprintf(stderr, "[input] SELECT JOYPAD: P%d is \"%s\" (%s)\n", p + 1, dev_name(d), pref);
}

/* SWOS.ini [input]: pad1, pad2, auto_detect_pads, pad_config, shoulder_buttons, call_bench. */
void InputSDL_Configure(const PlatformInitOptions *o)
{
    for (int p = 0; p < INPUTSDL_NUM_PADS; p++)
        snprintf(s_pref[p], sizeof s_pref[p], "%s", SDL_strcasecmp(o->pad_pref[p], "auto") == 0 ? "" : o->pad_pref[p]);
    if (SDL_strcasecmp(s_pref[0], "none") == 0)
        snprintf(s_pref[0], sizeof s_pref[0], "keyboard");      /* (player 1 always has the keyboard) */
    if (SDL_strcasecmp(s_pref[1], "keyboard") == 0)
        snprintf(s_pref[1], sizeof s_pref[1], "none");          /* (and player 2 never has it) */
    s_autoDetect = o->auto_detect_pads != 0;
    s_shouldersOff = o->shoulder_buttons_off != 0;
    InputSDL_SetCallBench(o->call_bench);
    InputSDL_ParsePadConfig(o->pad_config);
}

/* ... and back, for Settings_Save. */
void InputSDL_StoreConfig(PlatformInitOptions *o)
{
    for (int p = 0; p < INPUTSDL_NUM_PADS; p++)
        snprintf(o->pad_pref[p], sizeof o->pad_pref[p], "%s", s_pref[p][0] ? s_pref[p] : "auto");
    o->auto_detect_pads = s_autoDetect;
    o->shoulder_buttons_off = s_shouldersOff;
    o->call_bench = s_callBench;
    InputSDL_FormatPadConfig(o->pad_config, (int)sizeof o->pad_config);
}

/* Push a raw Amiga scancode into the FIFO GetKey() drains, exactly as a
 * keyboard press does: same full-buffer drop, and no repeat handling, since a
 * pad button generates one event per press. */
static void push_amiga_key(uint8_t akey)
{
    if (_keyCount < (sizeof(_keyBuffer) - 3)) {
        _keyBuffer[_keyCount] = akey;
        _keyCount++;
    }
}

/* ---- The web page's on-screen controls (wasm/shell.html) ----
 * A phone has no keyboard and usually no pad, so the page draws a stick, FIRE, PAUSE and
 * ESC over the game and reports them here. They are PLAYER 1's, as the keyboard is:
 * combined with it every frame (InputSDL_PostFrame) and sent as this player's in an
 * online match (InputSDL_LocalMatchControls). A press of FIRE is a fire event, as a
 * key's is, so a tap shorter than a frame is still read; PAUSE and ESC go into the key
 * FIFO, as the pad's START and stick click do (k_padKeys). */
static int s_touch_dirs;        /* 1 up, 2 down, 4 left, 8 right */
static int s_touch_fire;

EMSCRIPTEN_KEEPALIVE void InputSDL_TouchControls(int32_t dirs, int32_t fire)
{
    if (fire && !s_touch_fire)
        s_fire_event = 1;
    s_touch_dirs = (int)(dirs & 0x0F);
    s_touch_fire = fire != 0;
}

/* `akey`: a raw Amiga scancode, 0x19 P (pause) or 0x45 ESC. */
EMSCRIPTEN_KEEPALIVE void InputSDL_TouchKey(int32_t akey)
{
    if (akey >= 0 && akey < 0x68)
        push_amiga_key((uint8_t)akey);
}

/* RAW TEXT (InputText's, for a password): the characters themselves, as typed -- small
 * letters, symbols -- from SDL's text input, where the game's keys only give capitals and
 * digits. A queue the editor empties each frame. */
static uint8_t s_rawq[64];
static int     s_rawHead, s_rawTail, s_rawOn;

void InputSDL_RawText(int on)
{
    s_rawHead = s_rawTail = 0;
    s_rawOn = on;
#ifndef SWOS_WASM_BUILD
    if (on)
        SDL_StartTextInput();
#endif
}

static void raw_push(int ch)
{
    if (ch < 0x20 || ch > 0x7E || ((s_rawTail + 1) & 63) == s_rawHead)
        return;
    s_rawq[s_rawTail] = (uint8_t)ch;
    s_rawTail = (s_rawTail + 1) & 63;
}

int InputSDL_RawChar(void)
{
    int ch;
    if (s_rawHead == s_rawTail)
        return -1;
    ch = s_rawq[s_rawHead];
    s_rawHead = (s_rawHead + 1) & 63;
    return ch;
}

/* A character the page's hidden text field got from a phone's on-screen keyboard
 * (wasm/shell.html), while the game asks for text (InputText): the key that types it
 * into the FIFO, as a key press does. `ch`: a printable ASCII character, or 8 BACKSPACE,
 * 13 ENTER, 27 ESC. A character no key types is dropped. */
EMSCRIPTEN_KEEPALIVE void InputSDL_WebChar(int32_t ch)
{
    int a;
    if (s_rawOn && ch >= 0x20 && ch <= 0x7E) {     /* a password: the character itself */
        raw_push(ch);
        return;
    }
    if (ch == 8)
        a = sdl_to_amiga(SDLK_BACKSPACE);
    else if (ch == 13)
        a = sdl_to_amiga(SDLK_RETURN);
    else if (ch == 27)
        a = sdl_to_amiga(SDLK_ESCAPE);
    else if (ch >= 'A' && ch <= 'Z')
        a = sdl_to_amiga((SDL_Keycode)(ch - 'A' + 'a'));    /* (the field is capitals anyway) */
    else if (ch > ' ' && ch < 0x7F)
        a = sdl_to_amiga((SDL_Keycode)ch);
    else if (ch == ' ')
        a = sdl_to_amiga(SDLK_SPACE);
    else
        a = -1;
    if (a >= 0)
        push_amiga_key((uint8_t)a);
}

static void touch_controls(int *_up, int *_down, int *left, int *right, int *_fire)
{
    if (s_touch_dirs & 1) *_up    = 1;
    if (s_touch_dirs & 2) *_down  = 1;
    if (s_touch_dirs & 4) *left   = 1;
    if (s_touch_dirs & 8) *right  = 1;
    if (s_touch_fire)     *_fire  = 1;
}

/* The one k_padKeys button being held for its `hold` key (-1: none): the entry,
 * the pad, when it went down, and whether the hold has already been sent. */
static struct { int entry; SDL_JoystickID which; uint32_t since; int sent; } s_pad_hold = { -1, 0, 0, 0 };

static void pad_hold_poll(uint32_t now)
{
    if (s_pad_hold.entry >= 0 && !s_pad_hold.sent && now - s_pad_hold.since >= PAD_HOLD_MS) {
        push_amiga_key(k_padKeys[s_pad_hold.entry].hold);
        s_pad_hold.sent = 1;
    }
}

static void pad_key_button(int entry, SDL_JoystickID which, int down, uint32_t now)
{
    if (!k_padKeys[entry].hold) {
        if (down)
            push_amiga_key(k_padKeys[entry].tap);
    } else if (down) {
        s_pad_hold.entry = entry;
        s_pad_hold.which = which;
        s_pad_hold.since = now;
        s_pad_hold.sent = 0;
    } else if (s_pad_hold.entry == entry && s_pad_hold.which == which) {
        pad_hold_poll(now);
        if (!s_pad_hold.sent && k_padKeys[entry].tap)
            push_amiga_key(k_padKeys[entry].tap);
        s_pad_hold.entry = -1;
    }
}

/* Open a device and give it a player if one is free. Returns the player (0 or 1),
 * -1 if it is open but waiting for a player, -2 if it could not be opened. */
static int pad_open_into_first_empty(int joystick_index)
{
    for (int i = 0; i < INPUTSDL_MAX_DEVICES; ++i) {
        if (!dev_open(i)) {
            if (SDL_IsGameController(joystick_index)) {
                SDL_GameController *p = SDL_GameControllerOpen(joystick_index);
                if (p) {
                    static int announced;
                    s_dev_gc[i] = p;
                    s_dev_id[i] = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(p));
                    /* Print the in-match button map once. On a console there
                     * is no manual to check and no keyboard to fall back on,
                     * so the log is where you find out what the pad does. */
                    if (!announced) {
                        const char *nm = SDL_GameControllerName(p);
                        size_t k;
                        announced = 1;
                        fprintf(stderr, "[input] pad: %s\n", nm ? nm : "controller");
                        fprintf(stderr, "[input]   A / B          -> fire (B also skips)\n");
                        for (k = 0; k < sizeof k_padKeys / sizeof k_padKeys[0]; k++)
                            fprintf(stderr, "[input]   %-14s -> %s\n",
                                    SDL_GameControllerGetStringForButton(
                                        (SDL_GameControllerButton)k_padKeys[k].button),
                                    k_padKeys[k].what);
                    }
                    return dev_assign(i);
                }
            } else {
                /* No GameController mapping — open as a raw joystick.
                 * Standard "two axes + one fire" gameport pads (the
                 * Amiga/Atari shape SWOS expects) fall through here. */
                SDL_Joystick *j = SDL_JoystickOpen(joystick_index);
                if (j) {
                    s_dev_raw[i] = j;
                    s_dev_id[i] = SDL_JoystickInstanceID(j);
                    return dev_assign(i);
                }
            }
            return -2;
        }
    }
    return -2;
}

static int dev_find_instance(SDL_JoystickID id)
{
    for (int i = 0; i < INPUTSDL_MAX_DEVICES; ++i) {
        if (dev_open(i) && s_dev_id[i] == id)
            return i;
    }
    return -1;
}

/* SWOS_TEST_VIRTUAL_PADS=N attaches N virtual gameport sticks (two axes, two
 * buttons, no mapping: the raw path) before the real ones are counted, and
 * SWOS_TEST_VIRTUAL_FIRE=<pad>:<frame> holds fire on virtual pad <pad> (0-based) for
 * ten frames from the <frame>-th -- how the intro's "fire picks player 1" is checked
 * without three controllers on the desk. */
#if SDL_VERSION_ATLEAST(2, 0, 14)
static int s_virt_first = -1, s_virt_count;
static void virtual_pads_attach(void)
{
    const char *v = getenv("SWOS_TEST_VIRTUAL_PADS");
    int n = v ? atoi(v) : 0;
    /* A test runs windowless, and SDL drops joystick input with no window focused. */
    if (n > 0)
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    for (int i = 0; i < n && i < INPUTSDL_MAX_DEVICES; ++i) {
        int idx = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_UNKNOWN, 2, 2, 0);
        if (idx < 0) {
            fprintf(stderr, "[input] test: virtual pad %d not attached: %s\n", i, SDL_GetError());
            break;
        }
        if (s_virt_first < 0)
            s_virt_first = idx;
        s_virt_count++;
    }
}

static void virtual_pads_frame(void)
{
    const char *v = getenv("SWOS_TEST_VIRTUAL_FIRE");
    int pad, at, frame = s_frames;
    if (s_virt_count)
        SDL_JoystickUpdate();       /* a virtual stick's state lands on an update */
    if (!v || s_virt_count == 0 || sscanf(v, "%d:%d", &pad, &at) != 2 || pad < 0 || pad >= s_virt_count)
        return;
    if (frame == at || frame == at + 10) {
        SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(s_virt_first + pad);
        SDL_Joystick *j = SDL_JoystickFromInstanceID(id);
        if (!j || SDL_JoystickSetVirtualButton(j, 0, frame == at ? SDL_PRESSED : SDL_RELEASED) != 0)
            fprintf(stderr, "[input] test: virtual fire not set (%s)\n", j ? SDL_GetError() : "not open");
        if (frame == at)
            fprintf(stderr, "[input] test: fire on virtual pad %d at frame %d%s\n", pad, frame,
                    s_intro_claim ? " (the intro is on)" : "");
    }
}
#else
static void virtual_pads_attach(void) {}
static void virtual_pads_frame(void) {}
#endif

int InputSDL_Init(void)
{
    memset(_keyState, 0, sizeof(_keyState));
    _joy0Dat = 0;
    _joy1Dat = 0;
    _ciaa_pra = 0xFF;                  /* both fire bits set (idle) */

    /* Load extra controller mappings if the user dropped a
     * gamecontrollerdb.txt next to the binary. Without a matching
     * mapping, SDL_IsGameController() returns false for the device and
     * we silently skip it — that's the most common reason a pad
     * "isn't working". The file is the community-maintained mapping
     * DB (https://github.com/gabomdq/SDL_GameControllerDB). Built-in
     * SDL mappings cover popular pads (Xbox, PS4/5, Switch Pro, most
     * Logitechs); the file is for everything else. */
    {
        int loaded = SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");
        if (loaded > 0)
            fprintf(stderr, "[input] loaded %d controller mappings from gamecontrollerdb.txt\n",
                    loaded);
        else if (loaded < 0)
            /* missing file is fine; SDL only sets the error on parse failure */
            fprintf(stderr, "[input] gamecontrollerdb.txt not loaded: %s\n", SDL_GetError());
    }

    virtual_pads_attach();

    int n = SDL_NumJoysticks();
    fprintf(stderr, "[input] SDL_NumJoysticks=%d\n", n);
    s_assigningAtStart = 1;         /* (AUTO-DETECT JOYPADS is about pads plugged in later) */
    for (int i = 0; i < n; ++i) {
        const char *name = SDL_JoystickNameForIndex(i);
        int is_gc = SDL_IsGameController(i);
        fprintf(stderr, "[input]   joystick %d: \"%s\" %s\n",
                i, name ? name : "(null)",
                is_gc ? "→ game controller" : "→ raw joystick (no mapping)");
        int slot = pad_open_into_first_empty(i);
        char pref[48];
        {   /* (its [input] pad1/pad2 name: SETTINGS -> CONTROLS chooses by it) */
            const int d = dev_find_instance(SDL_JoystickGetDeviceInstanceID(i));
            InputSDL_DevicePref(d, pref, sizeof pref);
        }
        if (slot >= 0)
            fprintf(stderr, "[input]     opened as P%d (%s, %s)\n",
                    slot + 1, is_gc ? "GameController" : "raw Joystick", pref);
        else if (slot == -1)
            fprintf(stderr, "[input]     opened, not played (%s): both players have a pad or chose "
                            "their control (press fire on it during the intro to play it as P1)\n", pref);
        else
            fprintf(stderr, "[input]     skipped (open failed, or %d devices already open)\n",
                    INPUTSDL_MAX_DEVICES);
    }
    s_assigningAtStart = 0;
    /* A player who chose a pad by name that opened after an AUTO one took their place. */
    for (int p = 0; p < INPUTSDL_NUM_PADS; ++p)
        fill_player(p, 1);
    return 0;
}

void InputSDL_Shutdown(void)
{
    for (int i = 0; i < INPUTSDL_MAX_DEVICES; ++i) {
        if (s_dev_gc[i]) {
            SDL_GameControllerClose(s_dev_gc[i]);
            s_dev_gc[i] = NULL;
        }
        if (s_dev_raw[i]) {
            SDL_JoystickClose(s_dev_raw[i]);
            s_dev_raw[i] = NULL;
        }
        s_dev_id[i] = 0;
    }
    s_player_dev[0] = s_player_dev[1] = -1;
}

/* ESC PRESSING TO QUIT (SWOS 2020's MULTIPLAYER SETTINGS, [netplay] esc_presses): how many ESC
 * presses, each within two seconds of the last, leave a live match -- 1 as before, or 2 or 3 so a
 * stray one does not end it. Each press short of the count says so in the chat box. */
static int      s_escPresses = 1;
static int      s_escCount;
static uint32_t s_escAt;
void InputSDL_SetEscPresses(int n) { s_escPresses = n >= 1 && n <= 3 ? n : 1; }
int  InputSDL_EscPresses(void)     { return s_escPresses; }

/* F6's quick save: when it was pressed (0 none). Taken by the next menu frame, and only if it
 * is fresh -- a press that waited out a match or a long load is no request any more. */
static uint32_t s_quickSaveAt;
int InputSDL_TakeQuickSave(void)
{
    const uint32_t at = s_quickSaveAt;
    s_quickSaveAt = 0;
    return at && SDL_GetTicks() - at < 500;
}

static void esc_press_in_match(void (*leave)(void))
{
    extern void MatchChat_Notice(const char *text);     /* match_chat.c */
    const uint32_t now = SDL_GetTicks();
    if (s_escCount && now - s_escAt > 2000)
        s_escCount = 0;
    s_escAt = now;
    if (++s_escCount >= s_escPresses) {
        s_escCount = 0;
        leave();
        return;
    }
    {
        char line[64];
        const int left = s_escPresses - s_escCount;
        snprintf(line, sizeof line, "Press ESC %d more time%s to leave the match", left, left > 1 ? "s" : "");
        MatchChat_Notice(line);
    }
}

void InputSDL_HandleEvent(const SDL_Event *e)
{
    switch (e->type) {
    case SDL_TEXTINPUT:
        if (s_rawOn) {
            const char *t;
            for (t = e->text.text; *t; t++)
                raw_push((uint8_t)*t);
        }
        break;
    case SDL_KEYDOWN: {
        /* The CONTROLS menu is waiting for a key: this is it. */
        if (s_capture) {
            if (e->key.repeat == 0 && e->key.keysym.scancode != SDL_SCANCODE_UNKNOWN) {
                s_captured = e->key.keysym.scancode;
                s_capture = 0;
            }
            break;
        }
        /* F6 in a menu: SWOS 2020's quick save (src/settings_menus.c takes it in MenuProc). Not in
         * a match, nor while a text field types: there it is no request at all. */
        if (e->key.keysym.sym == SDLK_F6 && e->key.repeat == 0 && !_gTextInputActive &&
            !(e->key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_SHIFT | KMOD_GUI))) {
            extern int32_t GameLoop_Active(void);
            extern int32_t NetplayMatch_Enabled(void);
            if (!GameLoop_Active() && !NetplayMatch_Enabled())
                s_quickSaveAt = SDL_GetTicks() | 1u;
        }
        /* One of the player's own keys ([input] key_*): a control, and nothing else. */
        int act = bound_action(e->key.keysym.scancode);
        if (act >= 0) {
            static const SDL_Keycode arrow[4] = { SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT };
            if (act == BIND_FIRE) {
                if (e->key.repeat == 0)
                    s_fire_event = 1;
                s_kbd_fire_keys |= KBD_FIRE_BOUND;
                s_kbd_fire = 1;
            } else if (e->key.repeat == 0) {
                kbd_arrow(arrow[act], 1);
            }
            break;
        }
        int a = sdl_to_amiga(e->key.keysym.sym);
        /* Arrow keys drive joy1Dat directly (s_kbd_up/down/left/right
         * below) so we DON'T also push their Amiga scancodes into
         * keyBuffer — `GetKey()` then `keycodeLut` maps the arrow
         * scancodes to 0x10/0x11/0x12/0x13, the same numeric range
         * that `MainKeysCheck` interprets as the bench-call hotkey
         * (0x10 = bench2, 0x11 = bench1) and that menu nav uses for
         * up/right/down/left. On Amiga the player used a joystick
         * in-game so arrow scancodes never fired during play; on
         * SDL we double-purpose the same physical keys. Without
         * this filter, holding DOWN arrow to control the keeper
         * fires bench1 every time the keypress lands in keyBuffer.
         * Menu navigation still works because the joystick path
         * drives menuControlsDirection from pl1Direction. */
        int is_movement_arrow = (e->key.keysym.sym == SDLK_UP   ||
                                  e->key.keysym.sym == SDLK_DOWN ||
                                  e->key.keysym.sym == SDLK_LEFT ||
                                  e->key.keysym.sym == SDLK_RIGHT);
        /* Alt is dropped from the input layer entirely. The OS owns
         * Alt-Tab and Alt+space (window menu) and these complete in
         * orders SDL can't always linearise — the Alt KEYDOWN reaches
         * us, the game polls keyBuffer / keyState before SDL gets the
         * matching KEYUP, and the loader-intro is skipped + the
         * highlighted menu entry "clicked" on focus return. The fire
         * role is still covered by RCTRL / LCTRL / SPACE / ENTER, and
         * Alt+Enter for fullscreen reads KMOD_ALT off the modifier mask
         * (not our keyState), so it keeps working. */
        int is_alt = (e->key.keysym.sym == SDLK_LALT ||
                      e->key.keysym.sym == SDLK_RALT);
        /* A netplay match: no key reaches the game's own keyboard state at all.
         * keyBuffer feeds GetKey/MainKeysCheck and keyState is polled directly,
         * and either one acted on THIS machine only -- Q called the bench, S the
         * statistics -- so the two games parted (ReportBot 0011). The match's
         * controls do not come from here: InputSDL_LocalMatchControls reads the
         * s_kbd_* flags below and they travel in the shared input. ESC asks the
         * match to end cleanly instead (netplay_match.c). */
        extern int32_t NetplayMatch_Enabled(void);
        extern void    NetplayMatch_RequestLeave(void);
        int in_match = NetplayMatch_Enabled();
        if (in_match && (e->key.keysym.sym == SDLK_ESCAPE || e->key.keysym.sym == SDLK_AC_BACK) && e->key.repeat == 0)
            esc_press_in_match(NetplayMatch_RequestLeave);
        if (is_alt || in_match) {
            /* Don't record keyState, don't push to keyBuffer, don't
             * trigger fire. Alt is purely a modifier on this build. */
        } else if (a >= 0 && !is_movement_arrow) {
            _keyState[a] = 1;
            /* Push into the FIFO consumed by GetKey() — but only on
             * the initial press (SDL re-fires KEYDOWN for OS key-repeat,
             * and the Amiga ISR uses its own keyDelay/repeat logic that
             * we don't currently emulate; ignoring repeats prevents
             * runaway double-inserts in InputText). Drop the event when
             * the buffer is full. */
            if (e->key.repeat == 0 && _keyCount < (sizeof(_keyBuffer) - 3)) {
                _keyBuffer[_keyCount] = (uint8_t)a;
                _keyCount++;
            }
        } else if (a >= 0) {
            /* Arrow keys: still record key-down in keyState (some game
             * code polls keyState[AKEY_UP] etc.) but skip the keyBuffer
             * FIFO so GetKey doesn't see them as menu/hotkey input. */
            _keyState[a] = 1;
        }
        switch (e->key.keysym.sym) {
        case SDLK_UP: case SDLK_DOWN: case SDLK_LEFT: case SDLK_RIGHT:
            if (e->key.repeat == 0)
                kbd_arrow(e->key.keysym.sym, 1);
            break;
        case SDLK_RCTRL:
            s_kbd_fire_keys |= KBD_FIRE_RCTRL;
            s_kbd_fire = 1;
            if (e->key.repeat == 0)
                s_fire_event = 1;
            break;
        case SDLK_LCTRL:
            s_kbd_fire_keys |= KBD_FIRE_LCTRL;
            s_kbd_fire = 1;
            if (e->key.repeat == 0)
                s_fire_event = 1;
            break;
        case SDLK_RETURN:
            s_kbd_fire_keys |= KBD_FIRE_RETURN;
            s_kbd_fire = 1;
            if (e->key.repeat == 0)
                s_fire_event = 1;
            break;
        case SDLK_SPACE:
            /* In a live match SPACE is SWOS's highlight-capture key
             * (game_loop.c MainKeysCheck case 0x20 — sets
             * fadeAndSaveHighlightScene, which fades the screen).
             * Don't route it to fire/skip on the game pad in-match —
             * that would mash the player's fire button at the same
             * time as the highlight capture.
             *
             * Outside a match SPACE drives the season-end auto-skip
             * latch (pauseSkipRequested) ONLY. Fire (Right Ctrl /
             * Left Ctrl / Enter) is intentionally separate so SPACE
             * stays usable as a literal space character in filename /
             * tactics-name text input fields, which read shortFire
             * via the same MenuCheckControls path the menus do.
             *
             * gameState in [ST_STARTING_GAME .. ST_PENALTIES] means
             * a match is on screen — see game_types.h. EXCEPT: gameState
             * is never reset after a match, so it lingers at a post-match
             * value (ST_RESULT_AFTER_THE_GAME=26 / ST_GAME_ENDED=30, both
             * in 21..31) while the career runs the season-end / catch-up
             * playback. That playback is exactly when the user wants to
             * press SPACE to skip — and there is no live match then — so
             * `seasonUpdateInProgress` overrides the gameState gate. */
            if (_seasonUpdateInProgress != 0 ||
                !(_gameState >= 21 /*ST_STARTING_GAME*/ &&
                  _gameState <= 31 /*ST_PENALTIES*/)) {
                s_kbd_skip = 1;
            }
            break;
        default: break;
        }
    } break;
    case SDL_KEYUP: {
        /* Released whatever a text field says: a key held when one opened must not stick. */
        {
            int i, act = -1;
            for (i = 0; i < BIND_COUNT; i++)
                if (s_bind[i] != SDL_SCANCODE_UNKNOWN && s_bind[i] == e->key.keysym.scancode)
                    act = i;
            if (act >= 0) {
                static const SDL_Keycode arrow[4] = { SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT };
                if (act == BIND_FIRE) {
                    s_kbd_fire_keys &= ~KBD_FIRE_BOUND;
                    s_kbd_fire = (s_kbd_fire_keys != 0);
                } else {
                    kbd_arrow(arrow[act], 0);
                }
                if (!_gTextInputActive)
                    break;
            }
        }
        int a = sdl_to_amiga(e->key.keysym.sym);
        int is_alt = (e->key.keysym.sym == SDLK_LALT ||
                      e->key.keysym.sym == SDLK_RALT);
        if (!is_alt && a >= 0) _keyState[a] = 0;
        switch (e->key.keysym.sym) {
        case SDLK_UP: case SDLK_DOWN: case SDLK_LEFT: case SDLK_RIGHT:
            kbd_arrow(e->key.keysym.sym, 0);
            break;
        /* Clear only THIS key's bit — fire stays down while another
         * fire key is still held (see s_kbd_fire_keys). */
        case SDLK_RCTRL:  s_kbd_fire_keys &= ~KBD_FIRE_RCTRL;
                          s_kbd_fire = (s_kbd_fire_keys != 0); break;
        case SDLK_LCTRL:  s_kbd_fire_keys &= ~KBD_FIRE_LCTRL;
                          s_kbd_fire = (s_kbd_fire_keys != 0); break;
        case SDLK_RETURN: s_kbd_fire_keys &= ~KBD_FIRE_RETURN;
                          s_kbd_fire = (s_kbd_fire_keys != 0); break;
        case SDLK_SPACE:  s_kbd_skip = 0; break;
        default: break;
        }
    } break;
    case SDL_CONTROLLERBUTTONDOWN: {
        /* A fire button of player 1's pad went down (the latch and the recording's
         * evidence, above). A plain joystick has no such event and is read as before. */
        const int bd = dev_find_instance(e->cbutton.which);
        const int bFire = bd >= 0 ? cfg_button(bd, dev_cfg(bd), 0) : -1;
        const int bBench = bd >= 0 ? cfg_button(bd, dev_cfg(bd), 1) : -1;
        if ((bFire >= 0 ? e->cbutton.button == bFire
                        : e->cbutton.button == SDL_CONTROLLER_BUTTON_A || e->cbutton.button == SDL_CONTROLLER_BUTTON_B) &&
            s_player_dev[0] >= 0 && s_dev_gc[s_player_dev[0]] && s_dev_id[s_player_dev[0]] == e->cbutton.which)
            s_fire_event_pad = 1;
        /* A button CONTROLLER SETUP made FIRE or BENCH is that and nothing else (Y is no replay). */
        if (e->cbutton.button == bFire || e->cbutton.button == bBench)
            break;
        /* Only for pads SDL has a GameController mapping for — the raw
         * joystick path has no button NAMES to key off, so a gameport adapter
         * keeps working exactly as before and simply has no key buttons. */
        size_t i;
        for (i = 0; i < sizeof k_padKeys / sizeof k_padKeys[0]; i++) {
            if (e->cbutton.button == k_padKeys[i].button) {
                pad_key_button((int)i, e->cbutton.which, 1, SDL_GetTicks());
                break;
            }
        }
        break;
    }
    case SDL_CONTROLLERBUTTONUP: {
        size_t i;
        {
            const int bd = dev_find_instance(e->cbutton.which);
            if (bd >= 0 && (e->cbutton.button == cfg_button(bd, dev_cfg(bd), 0) ||
                            e->cbutton.button == cfg_button(bd, dev_cfg(bd), 1)))
                break;
        }
        for (i = 0; i < sizeof k_padKeys / sizeof k_padKeys[0]; i++) {
            if (e->cbutton.button == k_padKeys[i].button) {
                pad_key_button((int)i, e->cbutton.which, 0, SDL_GetTicks());
                break;
            }
        }
        break;
    }

    case SDL_JOYDEVICEADDED: {
        /* SDL fires JOYDEVICEADDED for every device. CONTROLLERDEVICEADDED
         * fires *additionally* for devices that have a GameController
         * mapping. We do all our opening here so a single code path
         * handles both mapped and unmapped pads — the
         * pad_open_into_first_empty helper picks the right SDL API
         * based on SDL_IsGameController. Skipping CONTROLLERDEVICEADDED
         * avoids opening the same device twice. */
        int idx = e->jdevice.which;
        const char *name = SDL_JoystickNameForIndex(idx);
        int is_gc = SDL_IsGameController(idx);
        int slot;
        /* SDL also announces every device that was already connected when it
         * started, and InputSDL_Init has opened those: opening one again gave it
         * a second slot -- a lone pad drove player 2 as well. */
        if (dev_find_instance(SDL_JoystickGetDeviceInstanceID(idx)) >= 0)
            break;
        slot = pad_open_into_first_empty(idx);
        fprintf(stderr, "[input] hot-plug: \"%s\" (%s) %s\n",
                name ? name : "(null)",
                is_gc ? "GameController" : "raw Joystick",
                slot >= 0 ? (slot == 0 ? "→ P1" : "→ P2")
                          : slot == -1 ? (s_autoDetect ? "(not played: both players have a pad)"
                                                       : "(waits: AUTO-DETECT JOYPADS is off -- SELECT JOYPAD)")
                                       : "(not opened)");
    } break;
    case SDL_JOYDEVICEREMOVED: {
        int d = dev_find_instance(e->jdevice.which);
        if (d >= 0) {
            if (s_dev_gc[d]) {
                SDL_GameControllerClose(s_dev_gc[d]);
                s_dev_gc[d] = NULL;
            }
            if (s_dev_raw[d]) {
                SDL_JoystickClose(s_dev_raw[d]);
                s_dev_raw[d] = NULL;
            }
            s_dev_id[d] = 0;
            for (int p = 0; p < INPUTSDL_NUM_PADS; ++p) {
                if (s_player_dev[p] != d)
                    continue;
                s_player_dev[p] = -1;
                fprintf(stderr, "[input] pad P%d removed\n", p + 1);
                /* A device that was waiting steps in: the one this player chose, or (AUTO,
                 * with AUTO-DETECT JOYPADS on) the first. */
                fill_player(p, s_autoDetect);
                if (s_player_dev[p] >= 0)
                    fprintf(stderr, "[input] P%d is now \"%s\"\n", p + 1, dev_name(s_player_dev[p]));
            }
        }
    } break;
    default:
        break;
    }
}

/* An analog stick as the eight-way joystick the game was written for (ReportBot 0032).
 *
 * Inside the dead zone -- a RADIUS, [input] stick_deadzone percent of the travel -- it
 * is centred. Outside, it is whichever of the eight directions it points nearest to:
 * each gets the same 45 degrees. A diagonal is the stick more than 22.5 degrees off an
 * axis, i.e. its smaller part over tan(22.5) = 0.4142 of its larger.
 *
 * It used to be one threshold an axis (24% each), which made a diagonal of anything
 * more than 14 degrees off straight at full push: 62 degrees of every quarter turn
 * against 28 for a straight line, so a quick left-then-up read as up-left. */
static int32_t s_stickDeadzone = 8192;      /* of 32768: 25% */

void InputSDL_SetStickDeadzone(int percent)
{
    if (percent < 5)  percent = 5;
    if (percent > 90) percent = 90;
    s_stickDeadzone = 32768 * percent / 100;
}

static void stick_directions(int32_t ax, int32_t ay, int *_up, int *_down, int *left, int *right)
{
    const int64_t mx = ax < 0 ? -(int64_t)ax : ax, my = ay < 0 ? -(int64_t)ay : ay;
    int horizontal, vertical;

    if (mx * mx + my * my < (int64_t)s_stickDeadzone * s_stickDeadzone)
        return;
    horizontal = mx * 10000 > my * 4142;     /* not within 22.5 degrees of straight up/down */
    vertical   = my * 10000 > mx * 4142;     /* not within 22.5 degrees of straight left/right */
    if (horizontal) { if (ax < 0) *left = 1; else *right = 1; }
    if (vertical)   { if (ay < 0) *_up  = 1; else *_down = 1; }
}

#ifdef SWOS_WASM_BUILD
/* The web: a pad the browser does not map as a "standard" gamepad, read from the Gamepad API
 * itself. SDL's web port gives every pad its default Standard Gamepad mapping (directions on
 * axes 0/1 and buttons 12-15, fire on 0/1) and has no hats; a retro stick or a converter is
 * rarely standard, and Chrome on a Mac gives its POV hat as one more AXIS -- resting at 3.29, the
 * eight directions -1 .. 1 in steps of 2/7 -- which nothing read: the directions did not
 * register (tester report, 2026-11-08: two Macs, Chrome and Firefox, a USB stick and a Zipstick
 * on a converter, both fine on the desktop build). So for such a pad: axes 0/1 as a stick, any
 * axis seen resting above 1 as a hat, any button as fire -- the desktop's raw path. Bits: 1 up,
 * 2 down, 4 left, 8 right, 16 fire; -1 a standard pad (SDL's mapping is right), -2 not found. */
EM_JS(int, web_pad_bits, (const char *idPtr, int nth), {
    const id = UTF8ToString(idPtr);
    const pads = navigator.getGamepads ? navigator.getGamepads() : [];
    const hats = (Module.swosHatAxes = Module.swosHatAxes || {});
    let k = 0;
    for (let i = 0; i < pads.length; i++) {
        const p = pads[i];
        /* (SDL's name for it: the id trimmed, its spaces single -- SDL_CreateJoystickName) */
        if (!p || !p.connected || p.id.trim().replace(/ +/g, ' ') !== id || k++ !== nth) continue;
        if (p.mapping === 'standard') return -1;
        let bits = 0;
        const key = p.index + ':' + p.id;
        const seen = (hats[key] = hats[key] || {});
        if (!seen.logged) {
            seen.logged = 1;
            console.log('[input] web pad "' + p.id + '": mapping "' + p.mapping + '", ' + p.axes.length +
                        ' axes, ' + p.buttons.length + ' buttons: read from the Gamepad API');
        }
        for (let a = 0; a < p.axes.length; a++) {
            const v = p.axes[a];
            if (v > 1.05) { seen[a] = 1; continue; }        /* a hat at rest */
            if (seen[a]) {                                   /* a hat: eight steps of 2/7 */
                const step = Math.round((v + 1) * 3.5);
                if (step < 0 || step > 7) continue;
                const dirs = [1, 1 | 8, 8, 2 | 8, 2, 2 | 4, 4, 1 | 4];
                bits |= dirs[step];
            } else if (a < 2) {                              /* the stick */
                if (v < -0.5) bits |= a ? 1 : 4;
                if (v > 0.5) bits |= a ? 2 : 8;
            }
        }
        for (let b = 0; b < p.buttons.length; b++)
            if (p.buttons[b] && p.buttons[b].pressed) { bits |= 16; break; }
        return bits;
    }
    return -2;
});
#endif

/* Read directional + fire state from a single pad slot. `skip` (may be
 * NULL) is set when the conventional "second button" (B on mapped pads,
 * button index 1 on raw joysticks) is pressed — used for the SDL-only
 * "skip auto-advance pause" feature, independent of fire. */
static void read_device_state(int d,
                              int *_up, int *_down, int *left, int *right,
                              int *_fire, int *skip)
{
    SDL_GameController *pad;
    SDL_Joystick *raw;

    if (d < 0 || d >= INPUTSDL_MAX_DEVICES)
        return;
    pad = s_dev_gc[d];
    raw = s_dev_raw[d];

#ifdef SWOS_WASM_BUILD
    {   /* (a pad the browser does not map as standard: read as the desktop reads a raw one) */
        /* (the JOYSTICK's name, the browser's id: a controller's is "Standard Gamepad" for all) */
        SDL_Joystick *j = pad ? SDL_GameControllerGetJoystick(pad) : raw;
        const char *nm = j ? SDL_JoystickName(j) : NULL;
        int nth = 0, bits;
        for (int e = 0; e < d && nm; e++) {
            SDL_Joystick *je = s_dev_gc[e] ? SDL_GameControllerGetJoystick(s_dev_gc[e]) : s_dev_raw[e];
            const char *ne = je ? SDL_JoystickName(je) : NULL;
            if (ne && strcmp(ne, nm) == 0)
                nth++;
        }
        bits = nm ? web_pad_bits(nm, nth) : -2;
        if (bits >= 0) {
            if (bits & 1)  *_up = 1;
            if (bits & 2)  *_down = 1;
            if (bits & 4)  *left = 1;
            if (bits & 8)  *right = 1;
            if (bits & 16) *_fire = 1;
            return;
        }
    }
#endif

    const PadConfig *cfg = dev_cfg(d);      /* CONTROLLER SETUP's, NULL: as it always was */
    const int cfgFire = cfg_button(d, cfg, 0), cfgBench = cfg_button(d, cfg, 1);

    if (pad) {
        Sint16 ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
        Sint16 ay = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
        stick_directions_cut(cfg, ax, ay, _up, _down, left, right);
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP))    *_up    = 1;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  *_down  = 1;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  *left  = 1;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) *right = 1;
        if (cfgFire >= 0 ? dev_button(d, cfgFire)
                         : SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A) ||
                           SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B))
            *_fire = 1;
        if (skip && SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B))
            *skip = 1;
        return;
    }

    if (raw) {
        /* Raw joystick path: no GameController mapping in SDL's DB.
         * The classic Amiga / gameport-shaped pad has 2 axes + 1+ fire
         * buttons. Read axes 0 (X) and 1 (Y) and treat any pressed
         * button as fire — keeps generic gameport-to-USB adapters
         * working without a hand-authored mapping. Also probe a hat
         * (POV) if the device has one (some converters expose
         * direction as a hat instead of axes). */
        int naxes  = SDL_JoystickNumAxes(raw);
        int nbtns  = SDL_JoystickNumButtons(raw);
        int nhats  = SDL_JoystickNumHats(raw);
        if (naxes >= 2) {
            Sint16 ax = SDL_JoystickGetAxis(raw, 0);
            Sint16 ay = SDL_JoystickGetAxis(raw, 1);
            stick_directions_cut(cfg, ax, ay, _up, _down, left, right);
        }
        if (nhats >= 1) {
            Uint8 h = SDL_JoystickGetHat(raw, 0);
            if (h & SDL_HAT_UP)    *_up    = 1;
            if (h & SDL_HAT_DOWN)  *_down  = 1;
            if (h & SDL_HAT_LEFT)  *left  = 1;
            if (h & SDL_HAT_RIGHT) *right = 1;
        }
        /* FIRE: CONTROLLER SETUP's button, else any but the BENCH one. */
        for (int b = 0; b < nbtns; ++b) {
            if ((cfgFire >= 0 ? b == cfgFire : b != cfgBench) && SDL_JoystickGetButton(raw, b)) {
                *_fire = 1;
                break;
            }
        }
        /* Button index 1 (the "second" button on classic gameport-shaped
         * pads) also raises skip. Button 0 alone never triggers skip. */
        if (skip && nbtns >= 2 && SDL_JoystickGetButton(raw, 1))
            *skip = 1;
    }
}

/* The same for player `slot` (0 or 1): whichever device that player has. */
static void read_pad_state(int slot,
                           int *_up, int *_down, int *left, int *right,
                           int *_fire, int *skip)
{
    read_device_state(s_player_dev[slot], _up, _down, left, right, _fire, skip);
}

/* While the intro is on screen, a device other than player 1's that presses fire
 * becomes player 1: "the one I press fire on is the one I play with". The one it
 * replaces takes its place -- player 2's pad if it was player 2's, or waits if it
 * was waiting -- so a two-player setup only ever swaps. The same press then skips
 * the intro, as fire always has, because it is now player 1's. */
void InputSDL_SetIntroClaim(int on)
{
    s_intro_claim = on != 0;
    if (getenv("SWOS_TEST_VIRTUAL_PADS"))
        fprintf(stderr, "[input] test: intro %s at frame %d\n", on ? "begins" : "ends", s_frames);
}

static void intro_claim_poll(void)
{
    for (int d = 0; d < INPUTSDL_MAX_DEVICES; ++d) {
        int up = 0, down = 0, left = 0, right = 0, fire = 0;
        int old = s_player_dev[0];
        if (!dev_open(d) || d == old)
            continue;
        read_device_state(d, &up, &down, &left, &right, &fire, NULL);
        if (!fire)
            continue;
        if (s_player_dev[1] == d)
            s_player_dev[1] = old;
        s_player_dev[0] = d;
        fprintf(stderr, "[input] fire on the intro: P1 is now controller %d, \"%s\"", d + 1, dev_name(d));
        if (old >= 0)
            fprintf(stderr, " (was controller %d, now %s)", old + 1, s_player_dev[1] == old ? "P2" : "not played");
        fprintf(stderr, "\n");
        return;
    }
}

/* Encode an up/down/left/right + fire state into Paula's JOY*DAT word.
 *
 * JOY*DAT uses 2-bit Gray-code quadrature per axis. SWOS decodes it as
 * (input.c:Player1StatusProc / Player2StatusProc):
 *   right = bit 1
 *   left  = bit 9
 *   down  = bit 0 XOR bit 1
 *   up    = bit 8 XOR bit 9
 *
 * Correct per-axis encoding:
 *   horizontal axis pair bits 1 (high) + 0 (low):
 *     bit 1 = right_pressed
 *     bit 0 = bit 1 XOR down_pressed
 *   vertical axis pair bits 9 (high) + 8 (low):
 *     bit 9 = left_pressed
 *     bit 8 = bit 9 XOR up_pressed
 *
 * Verification of all 9 cases (none + 4 cardinals + 4 diagonals):
 *   none:        v = 0x000  ->  R=0 L=0 D=0^0=0 U=0^0=0  ✓
 *   right:       v = 0x003  ->  R=1 L=0 D=1^1=0 U=0^0=0  ✓
 *   left:        v = 0x300  ->  R=0 L=1 D=0^0=0 U=1^1=0  ✓
 *   up:          v = 0x100  ->  R=0 L=0 D=0    U=0^1=1  ✓
 *   down:        v = 0x001  ->  R=0 L=0 D=1^0=1 U=0^0=0  ✓
 *   right+down:  v = 0x002  ->  R=1 L=0 D=0^1=1 U=0      ✓
 *   right+up:    v = 0x103  ->  R=1 L=0 D=1^1=0 U=0^1=1  ✓
 *   left+down:   v = 0x301  ->  R=0 L=1 D=0^1=1 U=1^1=0  ✓
 *   left+up:     v = 0x200  ->  R=0 L=1 D=0^0=0 U=1^1=1  ✓
 */
static uint16_t encode_joydat(int up, int down, int left, int right)
{
    uint16_t v = 0;
    if (right)                  v |= (1u << 1);
    if (right ^ (down ? 1 : 0)) v |= (1u << 0);
    if (left)                   v |= (1u << 9);
    if (left  ^ (up   ? 1 : 0)) v |= (1u << 8);
    return v;
}

/* Drop every kind of "key/button is currently down" state. Called on
 * SDL_WINDOWEVENT_FOCUS_LOST so an Alt-Tab away doesn't leave Alt
 * stuck pressed, which would (a) skip the intro on focus return and
 * (b) instantly fire the currently-highlighted menu entry as a "click".
 *
 * The Alt-Tab sequence on Windows takes focus away before SDL gets a
 * KEYUP for the Alt key, so without this clear our keyState[AKEY_LALT]
 * (and the keyBuffer FIFO entry that SDL_KEYDOWN pushed) stay set
 * indefinitely. The very first thing the menu/loader-intro does on
 * focus return is poll those, and acts as if the user was holding the
 * fire/select button down. */
void InputSDL_ClearKeyState(void)
{
    memset(_keyState, 0, sizeof(_keyState));
    memset((void *)_keyBuffer, 0, sizeof(_keyBuffer));
    _keyCount = 0;
    s_kbd_up = s_kbd_down = s_kbd_left = s_kbd_right = s_kbd_fire = 0;
    s_kbd_held_up = s_kbd_held_down = s_kbd_held_left = s_kbd_held_right = 0;
    s_kbd_fire_keys = 0;
    s_fire_event = s_fire_event_pad = 0;    /* a press from before is not one in what comes next */
    s_kbd_skip = 0;
    s_touch_dirs = s_touch_fire = 0;
    _pauseSkipRequested = 0;
    s_pad_hold.entry = -1;                  /* its release must not pause what comes next */
    /* joy0Dat / joy1Dat / ciaa_pra are recomputed each frame by
     * InputSDL_PostFrame from the cleared s_kbd_* + read_pad_state, so
     * they don't need explicit clearing here. */
}

/* Everything released: the keyboard state, and the joystick and fire registers the
 * game reads (joy0Dat, joy1Dat, CIAA port A).
 *
 * For a live netplay match, called as it starts. InputSDL_PostFrame stops writing
 * the registers the moment a netplay match is enabled -- the synced inputs own them
 * from the first frame -- but the pre-match screens run BEFORE that frame and read
 * them, and they would still hold whatever was down when the match was started. The
 * player who pressed START with fire started it with fire down, so the stadium
 * screen's wait for fire to be released (InitGameSaveTeams) waited forever. Released
 * is also the one state both peers can be sure to agree on. */
void InputSDL_ReleaseAll(void)
{
    InputSDL_ClearKeyState();
    _joy0Dat = 0;
    _joy1Dat = 0;
    _ciaa_pra |= (uint8_t)((1u << 7) | (1u << 6));     /* active low: both fire buttons up */
}

/* The DOS SECOND FIRE BUTTON (pad X), which opens the substitutes menu of the team this
 * player controls -- swos.asm routes it through TeamGeneralInfo.secondaryFire and
 * BenchCheckControls enters the menu on it. The AMIGA HAS NO SECOND BUTTON, so every
 * caller checks _gDosGameplay first and the Amiga input path is byte-for-byte unchanged.
 *
 * X, not B: A and B are both already fire (B is also the skip button), and a button that
 * shoots and opens the bench at once is not a second button. A keyboard player keeps the
 * 'W' scancode, which is the port's bench key in both modes. */
static int pad_secondary(int slot)
{
    int d;
    if (slot < 0 || slot >= INPUTSDL_NUM_PADS)
        return 0;
    d = s_player_dev[slot];
    if (s_callBench == INPUTSDL_BENCH_TAPS)     /* CALL BENCH: TRIPLE TAP ONLY -- the button is no bench */
        return 0;
    if (d >= 0 && dev_open(d)) {
        const int b = cfg_button(d, dev_cfg(d), 1);     /* CONTROLLER SETUP's BENCH */
        if (b >= 0)
            return dev_button(d, b);
        if (s_dev_gc[d])
            return SDL_GameControllerGetButton(s_dev_gc[d], SDL_CONTROLLER_BUTTON_X) ? 1 : 0;
    }
    return 0;
}

/* This machine's own controls, as MATCH_* bits, for a netplay match.
 *
 * A netplay peer always drives the LOCAL half from player 1's keyboard and
 * pad 0 -- which side of the pitch that is belongs to the session, not to the
 * hardware, so this deliberately does not care. The other half arrives over the
 * wire.
 *
 * The KEY is not sampled. A match input carries one (MatchPlayerInput.key) and
 * the recordings use it for the bench and the in-match hotkeys, but R, H, P, ESC
 * and S all LEAVE the match loop, which a session cannot roll back
 * (docs/NETPLAY.md section 5). Sending no key at all is how they stay out,
 * rather than filtering them one at a time and missing one. */
static int s_chatPadOff;                        /* [chat] controller_off */
void InputSDL_SetChatPadOff(int on) { s_chatPadOff = on != 0; }
int  InputSDL_ChatPadOff(void)      { return s_chatPadOff; }
extern int32_t MatchChat_Typing(void);          /* match_chat.c */

uint8_t InputSDL_LocalMatchControls(void)
{
    int up = s_kbd_up, down = s_kbd_down, left = s_kbd_left, right = s_kbd_right;
    int fire = s_kbd_fire, skip = 0;
    uint8_t bits = 0;

    touch_controls(&up, &down, &left, &right, &fire);
    /* DISABLE CONTROLLER IN CHAT (SWOS 2020's MULTIPLAYER SETTINGS): while this player types a
     * line, the pad steers nobody -- what it sends is an input like any other, so this is the
     * player's own choice and the other game plays what arrives. */
    if (!(s_chatPadOff && MatchChat_Typing()))
        read_pad_state(0, &up, &down, &left, &right, &fire, &skip);
    fire = sample_fire(fire, 1, &s_fire_evidence);

    if (up)    bits |= 0x01;    /* MATCH_UP    */
    if (down)  bits |= 0x02;    /* MATCH_DOWN  */
    if (left)  bits |= 0x04;    /* MATCH_LEFT  */
    if (right) bits |= 0x08;    /* MATCH_RIGHT */
    if (fire)  bits |= 0x20;    /* MATCH_FIRE  */
    /* DOS's second button, on the wire as MATCH_BENCH -- the same bit 6 DOS reads it
     * from. The frame step turns it into callBench for the team this player controls, so
     * a netplay peer needs nothing else. Amiga mode never sets it. */
    if (_gDosGameplay && pad_secondary(0))
        bits |= 0x40;          /* MATCH_BENCH */
    return bits;
}

/* --input-selftest: the player's own keys and the press latch, driven with made-up key
 * events -- no window, no device. Returns the number of failures. */
static void selftest_key(SDL_Scancode sc, SDL_Keycode sym, int down)
{
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    e.key.keysym.scancode = sc;
    e.key.keysym.sym = sym;
    InputSDL_HandleEvent(&e);
}

static int32_t s_selftestLeaves;
static void selftest_leave(void) { s_selftestLeaves++; }

int InputSDL_SelfTest(void)
{
    int fails = 0, i;
    uint8_t c;
    int32_t keys;
#define EXPECT(cond, what) do { if (!(cond)) { fails++; fprintf(stderr, "[input-selftest] FAIL: %s\n", what); } } while (0)

    for (i = 0; i < BIND_COUNT; i++)
        InputSDL_SetKeyBinding(i, "");
    InputSDL_ClearKeyState();

    /* A tap that is down and up again between two reads. */
    InputSDL_SetPressLatch(1);
    selftest_key(SDL_SCANCODE_RCTRL, SDLK_RCTRL, 1);
    selftest_key(SDL_SCANCODE_RCTRL, SDLK_RCTRL, 0);
    c = InputSDL_LocalMatchControls();
    EXPECT((c & 0x20) && InputSDL_LastFireEvidence() == 0x0C, "latch on: a short tap reads as fire, EVENT+LATCHED");
    c = InputSDL_LocalMatchControls();
    EXPECT(!(c & 0x20) && InputSDL_LastFireEvidence() == 0, "latch on: the tap lasts one read");

    /* A read that was thrown away gives the tap back to the next one. */
    selftest_key(SDL_SCANCODE_RCTRL, SDLK_RCTRL, 1);
    selftest_key(SDL_SCANCODE_RCTRL, SDLK_RCTRL, 0);
    (void)InputSDL_LocalMatchControls();
    InputSDL_UnreadMatchControls();
    c = InputSDL_LocalMatchControls();
    EXPECT((c & 0x20) && InputSDL_LastFireEvidence() == 0x0C, "a refused read does not spend the tap");
    c = InputSDL_LocalMatchControls();
    EXPECT(!(c & 0x20), "and it is still one read long");

    InputSDL_SetPressLatch(0);
    selftest_key(SDL_SCANCODE_RCTRL, SDLK_RCTRL, 1);
    selftest_key(SDL_SCANCODE_RCTRL, SDLK_RCTRL, 0);
    c = InputSDL_LocalMatchControls();
    EXPECT(!(c & 0x20) && InputSDL_LastFireEvidence() == 0x04, "latch off: the tap is lost, but EVENT says it arrived");
    InputSDL_SetPressLatch(1);

    /* A held press: the latch adds nothing to it. */
    selftest_key(SDL_SCANCODE_RCTRL, SDLK_RCTRL, 1);
    c = InputSDL_LocalMatchControls();
    EXPECT((c & 0x20) && InputSDL_LastFireEvidence() == 0x04, "held: fire, EVENT on the first read");
    c = InputSDL_LocalMatchControls();
    EXPECT((c & 0x20) && InputSDL_LastFireEvidence() == 0, "held: fire, nothing new on the second");
    selftest_key(SDL_SCANCODE_RCTRL, SDLK_RCTRL, 0);
    c = InputSDL_LocalMatchControls();
    EXPECT(!(c & 0x20) && InputSDL_LastFireEvidence() == 0, "released: no fire, no extra frame from the latch");

    /* Not bound: W is a game key and steers nothing. */
    keys = _keyCount;
    selftest_key(SDL_SCANCODE_W, SDLK_w, 1);
    selftest_key(SDL_SCANCODE_W, SDLK_w, 0);
    EXPECT(_keyCount != keys, "unbound W reaches the game's key buffer");
    EXPECT(!(InputSDL_LocalMatchControls() & 0x0F), "unbound W steers nothing");
    InputSDL_ClearKeyState();

    /* Bound: WASD + LEFT SHIFT. */
    EXPECT(InputSDL_SetKeyBinding(BIND_UP, "W") == 0 && InputSDL_SetKeyBinding(BIND_DOWN, "S") == 0 &&
           InputSDL_SetKeyBinding(BIND_LEFT, "A") == 0 && InputSDL_SetKeyBinding(BIND_RIGHT, "D") == 0 &&
           InputSDL_SetKeyBinding(BIND_FIRE, "Left Shift") == 0, "W A S D and Left Shift are names SDL knows");
    EXPECT(InputSDL_SetKeyBinding(BIND_FIRE, "No Such Key") == 1, "an unknown name is refused");
    EXPECT(strcmp(InputSDL_KeyBindingName(BIND_FIRE), "Left Shift") == 0, "a refused name leaves the binding alone");
    keys = _keyCount;
    selftest_key(SDL_SCANCODE_S, SDLK_s, 1);
    selftest_key(SDL_SCANCODE_A, SDLK_a, 1);
    selftest_key(SDL_SCANCODE_LSHIFT, SDLK_LSHIFT, 1);
    c = InputSDL_LocalMatchControls();
    EXPECT((c & 0x2F) == (0x02 | 0x04 | 0x20), "bound S + A + Left Shift read as DOWN + LEFT + FIRE");
    EXPECT(_keyCount == keys, "a bound key does not reach the game's key buffer");
    selftest_key(SDL_SCANCODE_LSHIFT, SDLK_LSHIFT, 0);
    selftest_key(SDL_SCANCODE_A, SDLK_a, 0);
    selftest_key(SDL_SCANCODE_S, SDLK_s, 0);
    EXPECT(!(InputSDL_LocalMatchControls() & 0x2F), "and are released");
    /* The built-in keys still work beside them. */
    selftest_key(SDL_SCANCODE_UP, SDLK_UP, 1);
    selftest_key(SDL_SCANCODE_RETURN, SDLK_RETURN, 1);
    EXPECT((InputSDL_LocalMatchControls() & 0x2F) == (0x01 | 0x20), "the cursor keys and ENTER still work");
    selftest_key(SDL_SCANCODE_RETURN, SDLK_RETURN, 0);
    selftest_key(SDL_SCANCODE_UP, SDLK_UP, 0);
    InputSDL_ClearKeyState();

    /* In a text field a bound key is a letter. */
    _gTextInputActive = 1;
    keys = _keyCount;
    selftest_key(SDL_SCANCODE_W, SDLK_w, 1);
    EXPECT(_keyCount != keys, "typing: a bound W is a letter");
    EXPECT(!(InputSDL_LocalMatchControls() & 0x0F), "typing: and steers nothing");
    selftest_key(SDL_SCANCODE_W, SDLK_w, 0);
    _gTextInputActive = 0;

    /* Pad keys, on a made-up clock: Start taps pause on release, a hold sends ESC
     * once and no pause, R3 sends nothing but its hold. */
#define PAD(btn, down, now) do { size_t k_; for (k_ = 0; k_ < sizeof k_padKeys / sizeof k_padKeys[0]; k_++) \
        if (k_padKeys[k_].button == (btn)) pad_key_button((int)k_, 0, (down), (now)); } while (0)
    InputSDL_ClearKeyState();
    PAD(SDL_CONTROLLER_BUTTON_START, 1, 1000);
    EXPECT(_keyCount == 0, "Start: nothing while it is down");
    PAD(SDL_CONTROLLER_BUTTON_START, 0, 1150);
    EXPECT(_keyCount == 1 && _keyBuffer[0] == 0x19, "Start tapped: P on release");
    InputSDL_ClearKeyState();
    PAD(SDL_CONTROLLER_BUTTON_START, 1, 2000);
    pad_hold_poll(2000 + PAD_HOLD_MS - 1);
    EXPECT(_keyCount == 0, "Start: no ESC before the hold time");
    pad_hold_poll(2000 + PAD_HOLD_MS);
    pad_hold_poll(2000 + PAD_HOLD_MS + 500);
    EXPECT(_keyCount == 1 && _keyBuffer[0] == 0x45, "Start held: ESC, once");
    PAD(SDL_CONTROLLER_BUTTON_START, 0, 4000);
    EXPECT(_keyCount == 1, "Start held: no P on release");
    InputSDL_ClearKeyState();
    PAD(SDL_CONTROLLER_BUTTON_RIGHTSTICK, 1, 5000);
    PAD(SDL_CONTROLLER_BUTTON_RIGHTSTICK, 0, 5100);
    EXPECT(_keyCount == 0, "R3 tapped: nothing");
    PAD(SDL_CONTROLLER_BUTTON_RIGHTSTICK, 1, 6000);
    PAD(SDL_CONTROLLER_BUTTON_RIGHTSTICK, 0, 6000 + PAD_HOLD_MS);
    EXPECT(_keyCount == 1 && _keyBuffer[0] == 0x45, "R3 held: ESC, even when the release is the first look");
    InputSDL_ClearKeyState();
    PAD(SDL_CONTROLLER_BUTTON_Y, 1, 7000);
    EXPECT(_keyCount == 1 && _keyBuffer[0] == 0x13, "Y: R on the press, as before");
    InputSDL_ClearKeyState();
#undef PAD

    /* SWOS 2020's CONTROLLER SETUP: [input] pad_config reads back as written, nonsense is
     * dropped, and the stick's cut-offs scale each half of an axis. */
    {
        char out[512];
        int up, down, left, right;
        PadConfig c;
        EXPECT(InputSDL_ParsePadConfig("03000000c82d00000090000011010000:1:3:40:0:0:30;zz:1;"
                                       "0300000000f000000300000000010000:-1:-1:0:0:0:0") == 1,
               "pad_config: one real config read, the default one and the nonsense dropped");
        InputSDL_FormatPadConfig(out, sizeof out);
        EXPECT(strcmp(out, "03000000c82d00000090000011010000:1:3:40:0:0:30") == 0, "pad_config written back as read");
        memset(&c, 0, sizeof c);
        up = down = left = right = 0;
        stick_directions_cut(&c, s_stickDeadzone + 400, 0, &up, &down, &left, &right);
        EXPECT(right && !left && !up && !down, "no cut-offs: just past the dead zone is RIGHT, as before");
        c.cut[0] = (uint8_t)(InputSDL_StickDeadzonePercent() + 20);
        up = down = left = right = 0;
        stick_directions_cut(&c, s_stickDeadzone + 400, 0, &up, &down, &left, &right);
        EXPECT(!right, "a higher x+ cut-off: the same push is not RIGHT yet");
        up = down = left = right = 0;
        stick_directions_cut(&c, -(s_stickDeadzone + 400), 0, &up, &down, &left, &right);
        EXPECT(left, "... and x- still has the dead zone: LEFT");
        InputSDL_ParsePadConfig("");
    }
    /* ESC PRESSING TO QUIT: 1 leaves at once, 3 on the third press within two seconds. */
    {
        s_selftestLeaves = 0;
        InputSDL_SetEscPresses(1);
        esc_press_in_match(selftest_leave);
        EXPECT(s_selftestLeaves == 1, "esc_presses 1: one ESC leaves");
        InputSDL_SetEscPresses(3);
        esc_press_in_match(selftest_leave);
        esc_press_in_match(selftest_leave);
        EXPECT(s_selftestLeaves == 1, "esc_presses 3: two ESCs do not");
        esc_press_in_match(selftest_leave);
        EXPECT(s_selftestLeaves == 2, "esc_presses 3: the third does");
        InputSDL_SetEscPresses(1);
    }

    for (i = 0; i < BIND_COUNT; i++)
        InputSDL_SetKeyBinding(i, "");
    InputSDL_ClearKeyState();
#undef EXPECT
    fprintf(stderr, "[input-selftest] %s\n", fails ? "FAILED" : "ok");
    return fails;
}

void InputSDL_PostFrame(void)
{
    /* CALL BENCH: 2ND FIRE BUTTON ONLY -- BenchCheckControls (bench_subs.c) counts no taps. Only
     * where there is a second button (the DOS style: the Amiga has none, so its bench stays the
     * taps), and only in a match of this machine's alone: off for a rig's and a netplay match,
     * both of which return below before it is set (two games must agree on what a tap does,
     * and a recording is played by the rules it was made with). */
    { extern int32_t _gBenchTapsOff; _gBenchTapsOff = 0; }
    /* The replay rig writes joy0Dat/joy1Dat/ciaa_pra itself, once per game
     * loop iteration. Recomputing them from the real keyboard/pads here
     * would overwrite the recorded stick with whatever is physically held. */
    { extern int ReplayTest_Running(void); if (ReplayTest_Running()) return; }

    /* A held Start/R3 sends its key here, on the same terms as a press always has:
     * k_padKeys pushes from HandleEvent, which a netplay match does not stop. */
    pad_hold_poll(SDL_GetTicks());

    /* A netplay match owns them for the same reason, and more sharply: they
     * carry the SYNCED inputs of both players, so recomputing them from this
     * machine's pad would replace the opponent's input with nothing and make
     * every re-simulated frame differ from the first. */
    { extern int32_t NetplayMatch_Enabled(void); if (NetplayMatch_Enabled()) return; }

    s_frames++;
    virtual_pads_frame();
    if (s_intro_claim && pref_auto(0))      /* (not when player 1 chose a control on CONTROLS) */
        intro_claim_poll();

    /* A KEYBOARD AND ONE PAD ARE TWO PLAYERS. Player 1 is the keyboard plus the first
     * pad, player 2 a second pad -- so with one pad there is no player 2, and a local
     * two-player friendly cannot start: the second team's PLAY MATCH screen listens to
     * player 2 alone (PlayMatchMenuInit clears player 1's input by playMatchControlFlags,
     * as SWOS2.s does), and neither CTRL nor the stick's fire -- both player 1's -- can
     * press it (a tester, 2026-09-29). So when both teams are human here (_twoPlayers)
     * and player 2 has no pad of their own, player 1's pad goes to player 2 and the
     * keyboard stays player 1's. One player, two pads, or an online match (which reads
     * InputSDL_LocalMatchControls, not these registers): as before. */
    int split = split_pad();
    {
        static int s_wasSplit;
        if (split != s_wasSplit) {
            s_wasSplit = split;
            if (split)
                fprintf(stderr, "[input] two players, one pad: \"%s\" is player 2, the keyboard "
                                "player 1\n", dev_name(s_player_dev[0]));
            else
                fprintf(stderr, "[input] the pad is player 1's again\n");
        }
    }

    /* Player 1: keyboard + pad 0 (combined), or the keyboard alone when split. The page's
     * on-screen controls count as the keyboard. */
    int p1_up = s_kbd_up, p1_down = s_kbd_down, p1_left = s_kbd_left, p1_right = s_kbd_right;
    int p1_fire = s_kbd_fire;
    int skip = s_kbd_skip;
    touch_controls(&p1_up, &p1_down, &p1_left, &p1_right, &p1_fire);
    if (!split)
        read_pad_state(0, &p1_up, &p1_down, &p1_left, &p1_right, &p1_fire, &skip);
    p1_fire = sample_fire(p1_fire, !split, NULL);       /* a press is never lost between two reads */

    /* Player 2: pad 1 only -- or pad 0 when split. No keyboard binding — keeping the
     * keyboard single-player avoids hijacking arrow / fire keys when a P2 pad is
     * plugged in but unused. */
    int p2_up = 0, p2_down = 0, p2_left = 0, p2_right = 0, p2_fire = 0;
    read_pad_state(split ? 0 : 1, &p2_up, &p2_down, &p2_left, &p2_right, &p2_fire, &skip);

    _pauseSkipRequested = (uint16_t)(skip ? 1 : 0);

    /* SWOS reads P1 from JOY1DAT (Amiga Port 1) and P2 from JOY0DAT
     * (Port 0). The port numbering is reversed from the player number —
     * see input.c Player1StatusProc/Player2StatusProc. */
    _joy1Dat = encode_joydat(p1_up, p1_down, p1_left, p1_right);
    _joy0Dat = encode_joydat(p2_up, p2_down, p2_left, p2_right);

    /* CIAA Port A fire bits (active low):
     *   bit 7 = Port 0 fire = Player 1 fire
     *   bit 6 = Port 1 fire = Player 2 fire
     * Clear bit to signal press; set to signal release. Other bits
     * stay at their default (1). */
    if (p1_fire) _ciaa_pra &= (uint8_t)~(1u << 7);
    else         _ciaa_pra |= (uint8_t)(1u << 7);
    if (p2_fire) _ciaa_pra &= (uint8_t)~(1u << 6);
    else         _ciaa_pra |= (uint8_t)(1u << 6);

    /* DOS's second button in LOCAL play. The registers above carry what the Amiga has;
     * this is the one input DOS has and the Amiga does not, so it goes in beside them
     * rather than through them, and only in DOS mode. */
    { extern int32_t _gBenchTapsOff; _gBenchTapsOff = _gDosGameplay && s_callBench == INPUTSDL_BENCH_BUTTON; }
    if (_gDosGameplay) {
        MatchStep_LocalBenchButton(1, split ? 0 : pad_secondary(0));
        MatchStep_LocalBenchButton(2, pad_secondary(split ? 0 : 1));
    }
}
