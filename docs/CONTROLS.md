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
| CONTROLLER SETUP | `pad_config` | SWOS 2020's joypadConfig.mnu, imported as it is (`openswos/joypadConfigMenu`): NAME, GUID, ID, BUTTONS, AXES, POWER, RAW X/Y, OpenSWOS's STICK row (below), the pad's FIRE and BENCH buttons (press one; ESC leaves it, BACKSPACE the default), the four stick CUT-OFFs (fire on the value +5%, the +/- boxes 1%) and the cut-off box lit as the stick moves |
| KEYBOARD KEYS | `key_*` | the keys screen (§1) |
| AUTO-DETECT JOYPADS | `auto_detect_pads` | on (default): a pad plugged in later goes to a free AUTO player; off: it waits until SELECT JOYPAD |
| DISABLE SHOULDER BUTTONS | `shoulder_buttons` | on (default, `off` in the file): the shoulders, paddles and stick clicks can be no FIRE or BENCH |
| CALL BENCH (under each player) | `call_bench_p1`, `call_bench_p2` | how that player calls his bench in every match he plays -- offline and online, in any gameplay style: `both` (default): the second fire button or three taps (the PC's way); `taps`: three taps alone, the button calls nothing (the Amiga's); `button`: the button alone, his taps are not counted. An older `call_bench` (one for both) counts for a player without his own key |
| STICK DEAD ZONE | `stick_deadzone` | the radius every cut-off defaults to |

`pad_config` is `guid:fire:bench:x+:x-:y+:y-[:stick];...` -- a button (a mapped pad's SDL button
number, a plain joystick's index), `-1` the default (fire: A or B / any button but BENCH;
bench: X / none); a cut-off in percent of the stick's travel, `0` = `stick_deadzone`. A
cut-off scales its half of the axis so that it lands on the dead zone's radius, then the
eight-way reading (ReportBot 0032) is unchanged -- all four at 0 read exactly as before. A button made
FIRE or BENCH sends nothing else (Y is no replay key then). START, BACK, GUIDE and R3 (whose
hold leaves a match) cannot be either.

**STICK** (OpenSWOS's row on CONTROLLER SETUP, GitHub issue 28: a USB adapter read up as down
and left as right): NORMAL, FLIP L/R, FLIP U/D, FLIP ALL, then SWAP and the same three swapped --
fire steps through them. It turns everything the pad reads as a direction (the stick, a mapped
pad's d-pad, a plain joystick's hat): X and Y swapped first, then flipped. The stick is turned
before the cut-offs, so CUT-OFF X+ is always the game's right. In `pad_config` it is an 8th
field, bits 1 flip X, 2 flip Y, 4 swap, written only when set -- an older build reads the seven
before it and skips it. The cut-off box shows the stick turned and lights the directions the game
reads; RAW X/Y stay what the pad sends.

**`gamecontrollerdb.txt`** (SDL's mapping format, a line per pad model) is read from beside the
game and then from the game folder (`Documents\SWOS`, beside SWOS.ini; issue 28), whose lines win.
A line can invert an axis too (`leftx:a0~`), but STICK is the way a player finds.

**Netplay.** None of it reaches the match: it decides which device is which player and how a
device reads, and what is read goes into the same `MATCH_*` input bits. The second button is
`MATCH_BENCH` in every gameplay style (the frame step opens the bench of the player's team, the
same on both games); CALL BENCH `taps` withholds it at the source (an input, so the other game
plays what it gets). `button` changes the simulation -- that player's taps are not counted
(`bench_subs.c` `bench_taps_off`, per team, by the player behind it) -- so both games must agree:
each player's CALL BENCH travels in his lobby STATE (`LOBBY_VERSION` 8), the host puts both in
the match setup (`SETUP_VERSION` 5) and both games play by the setup's, online and in a
re-simulation (a setup before version 5: every tap counts). Offline it is this machine's
(`input_sdl.c`); the rigs count every tap. `NETPLAY_COMPAT` 19.

**The Amiga.** The original read no second button (only JOY0DAT/JOY1DAT and the CIAA fire bits).
OpenSWOS reads pin 9 of each port as other Amiga games do (`input.c` `Input_SecondButtons`:
POTGOR bit 14 port 1 / player 1, bit 10 port 0 / player 2; POTGO `$CC00`): a two-button stick, a
Sega pad, a CD32 pad's blue button -- a mouse's right button in port 0 too. OPTIONS has P1 and
P2 CALL BENCH (entries 29..32, the Amiga only), kept until the game is switched off like the
other options, not in a career. SWOS 2020's IMPROVED KEYBOARD CONTROL is not here: it fixes its DOS keyboard
polling; ours reads SDL's keys (and already lets the newer arrow win, §1).

`--input-selftest` checks `pad_config` read and written back, the cut-offs and ESC PRESSING
TO QUIT; `--settings-menus-selftest` shows every page's rows and loads the three menus.
