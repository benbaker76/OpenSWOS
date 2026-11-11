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
