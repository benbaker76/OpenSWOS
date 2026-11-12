# Controls: the player's own keys, and the press latch (SDL builds)

Added 2026-10-13, after ReportBot 0041: a tester steered with WASD and fired with LEFT SHIFT
through an external key remapper, and reported fire presses the game never acted on. The
recording agreed with them as far as it could -- his game read no FIRE at all for two
seconds -- and could say nothing about why: the match reads fire as a LEVEL once a frame,
so "never pressed", "pressed but the event never reached the game" and "pressed and released
between two reads" all look the same. Three things now cover that.

## 1. Your own keys

`[input] key_up`, `key_down`, `key_left`, `key_right`, `key_fire` in `SWOS.ini`, or
**OPTIONS -> CONTROLS -> REDEFINE KEYS** (`src/controls_menu.c`,
`bin/data_json/menus/openswos/controlsMenu.json`; `--menu=openswos/controlsMenu`).

- A key is named as SDL names its **scancode** -- the physical key, whatever the layout or
  the shift state: `W`, `Space`, `Left Shift`, `Keypad 8`. Empty: none.
- They work **beside** the built-in keys (the cursor keys; either CTRL or ENTER to fire),
  which cannot be taken away or given a second meaning, so a menu can always be driven.
- A bound key is a control **and nothing else**: it no longer reaches the game's key buffer,
  so `W` steers without calling the bench and `S` without opening the statistics. The one
  exception is a text field (`_gTextInputActive`, set by `InputText`), where a letter is a
  letter.
- On the menu: fire on a row, then the key. ESC (or fire again) leaves it as it was,
  BACKSPACE or DELETE takes the key away, DEFAULT KEYS clears all five. Each row says what
  the control uses: its built-in key (CURSOR UP ... CTRL), or the player's and it ("W OR
  CURSOR UP"; the player's alone when both do not fit). The choice is written to `SWOS.ini`
  at once.
- There is no key for the bench: the port's bench keys (`Q`/`W`) are a team each, and the
  DOS second button is a pad button. Player 2 has no keyboard unless P1 AND P2 ON KEYBOARD is
  chosen (§1b).

SWOS 2020 had REDEFINE CONTROLS inside a larger CONTROLS MENU (device per player, joypad
configuration, three keyboard styles; its `controlOptionsMenu.cpp`). Only the redefinition
is here; the rest of that menu is not imported.

## 1b. P1 AND P2 ON KEYBOARD

The SWOS 96/97 Mod 4.1's device (its CONTROL2.DAT), in every world: **KEYBOARD** on the CONTROLS
menu switches between PLAYER 1 (the default) and **P1 AND P2 ON KEYBOARD** (`[input] p2_keyboard`).
Player 2 then plays on the keyboard too, by default **W / S / A / D** and **LEFT SHIFT** to fire
(`[input] p2_key_up`, `p2_key_down`, `p2_key_left`, `p2_key_right`, `p2_key_fire`; empty = the
default). **KEYS OF** chooses whose keys the five rows show and redefine; DEFAULT KEYS puts that
player's back. Player 1 keeps everything (his own keys come first); a key cannot be both players'.

- Player 2's keys are controls and nothing else while the device is chosen (W no longer calls the
  bench, S no longer opens the statistics), as player 1's own keys are; in a text field a letter is
  a letter.
- They are added to player 2's pad, as the keyboard is to player 1's (`InputSDL_PostFrame`); with
  the device chosen the one-pad split (player 1's pad to player 2) does not happen: player 2 has
  the keyboard.
- **Offline only**: an online match reads each game's own player 1 (`InputSDL_LocalMatchControls`),
  as always. Not chosen, nothing here is read: the input chain is as it was (the replay rig writes
  the joystick registers itself).
- Many keyboards cannot read many keys held at once (ghosting): if a key of one player does not
  answer while both press a lot, choose player 2's keys far from player 1's.
- Desktops only: a phone's CONTROLS has its ON-SCREEN row there.

## 2. The press latch

`[input] latch_presses = true` (the default), SHORT PRESSES on the same menu. A fire key or
pad button that went DOWN since the last read makes that read "pressed" even if it is up
again already -- for one read. It changes nothing unless a press would otherwise have been
lost whole. Covered: the keyboard, and player 1's game controller (buttons A and B). A plain
joystick has no button event and is read as before.

Online, a read that GGPO refuses (a stall) is thrown away and made again, so its press is
handed back (`InputSDL_UnreadMatchControls`). The latch is one player's own setting: it only
changes what that player's game reads, and what it reads is sent as always, so the two
players need not agree on it.

This is input reading, not gameplay: the match sees the same one-bit fire it always did.

## 3. What the recording says

A live match's `READ` chunk (docs/REPLAY_FORMAT_V2.md §4) has two more bits in each read's
flags: `0x04` a fire press arrived since the read before, `0x08` it was kept by the latch.
The recordings player shows them on the read line (*pressed now* / *a tap, kept by the
latch* / *a fire press came and went unread*) and `scripts/srp_inputs.py` counts them. So a
report of "I pressed and nothing happened" is answered from the file:

- presses recorded on those frames: the game had them, look at what the match did with them;
- none recorded: nothing reached the game -- the keyboard, a remapper, the OS (five taps of
  SHIFT open Windows' Sticky Keys prompt), or the window had lost focus.

## Testing

`bin/SWOS.x86_64 --input-selftest` drives the bindings and the latch with made-up key events
(no window). `--menu-selftest` checks the menu against its JSON.

## 4. SWOS 2020's CONTROLS MENU and CONTROLLER SETUP (2026-11-09)

OPTIONS' CONTROLS row is **SETTINGS** on SDL: SWOS 2020's OPTIONS buttons -- CONTROLS, VIDEO,
AUDIO, GAMEPLAY (`openswos/settingsMenu`, each a page of `openswos/settingsPage`, handlers in
`src/settings_menus.c`; docs/MENUS.md §10f). The keys screen above is CONTROLS -> KEYBOARD KEYS.
The CONTROLS page is SWOS 2020's controlOptions.mnu (its `controls/controlOptionsMenu.cpp`)
as rows:

| row | SWOS.ini `[input]` | what it does |
|---|---|---|
| PLAYER 1 / PLAYER 2 | `pad1`, `pad2` | AUTO (as before: the pads as they come, the first player 1's), KEYBOARD (player 1: no pad) / NONE (player 2: no control), or one pad by its GUID -- `#2` for the second of two alike (SWOS 2020's `joypad1`/`joypad2` + occurrence). A pad chosen by name goes to its player whenever it is connected; it takes the place of an AUTO one |
| SELECT JOYPAD | | the next pad a button is pressed on is that player's (SWOS 2020's `selectJoypadWithButtonPress`); swaps with the other player's |
| CONTROLLER SETUP | `pad_config` | SWOS 2020's joypadConfig.mnu, imported as it is (`openswos/joypadConfigMenu`): NAME, GUID, ID, BUTTONS, AXES, POWER, RAW X/Y, the pad's FIRE and BENCH buttons (press one; ESC leaves it, BACKSPACE the default), the four stick CUT-OFFs (fire on the value +5%, the +/- boxes 1%) and the cut-off box lit as the stick moves |
| KEYBOARD KEYS | `key_*` | the keys screen (§1) |
| AUTO-DETECT JOYPADS | `auto_detect_pads` | on (default): a pad plugged in later goes to a free AUTO player; off: it waits until SELECT JOYPAD |
| DISABLE SHOULDER BUTTONS | `shoulder_buttons` | on (default, `off` in the file): the shoulders, paddles and stick clicks can be no FIRE or BENCH |
| CALL BENCH | `call_bench` | `both` (default): the PC style's second fire button or three taps; `taps`: the button calls nothing; `button`: the taps are not counted (offline only) |
| STICK DEAD ZONE | `stick_deadzone` | the radius every cut-off defaults to |

`pad_config` is `guid:fire:bench:x+:x-:y+:y-;...` -- a button (a mapped pad's SDL button
number, a plain joystick's index), `-1` the default (fire: A or B / any button but BENCH;
bench: X / none); a cut-off in percent of the stick's travel, `0` = `stick_deadzone`. A
cut-off scales its half of the axis so that it lands on the dead zone's radius, then the
eight-way reading (ReportBot 0032) is unchanged -- all four at 0 read exactly as before. A button made
FIRE or BENCH sends nothing else (Y is no replay key then). START, BACK, GUIDE and R3 (whose
hold leaves a match) cannot be either.

**Netplay.** None of it reaches the match: it decides which device is which player and how a
device reads, and what is read goes into the same `MATCH_*` input bits. CALL BENCH `taps`
withholds `MATCH_BENCH` at the source (an input, so the other game plays what it gets);
`button` changes the simulation (`_gBenchTapsOff`, `bench_subs.c`), so `input_sdl.c` sets it
only for a match of this machine's alone -- never online, in the rigs or on the Amiga -- and
only in the DOS style, the one with a second button. `scripts/check_netplay_compat.sh` passes
unchanged. SWOS 2020's IMPROVED KEYBOARD CONTROL is not here: it fixes its DOS keyboard
polling; ours reads SDL's keys (and already lets the newer arrow win, §1).

`--input-selftest` checks `pad_config` read and written back, the cut-offs and ESC PRESSING
TO QUIT; `--settings-menus-selftest` shows every page's rows and loads the three menus.
