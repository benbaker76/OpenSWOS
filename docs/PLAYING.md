# Playing OpenSWOS

The keys and gamepad buttons, and the arcade (coin-op) mode. New to OpenSWOS? Start with
[INSTALL.md](../INSTALL.md).

## Controls

### Menus

| Action       | Keyboard           | Gamepad (any pad)    |
|--------------|--------------------|----------------------|
| Navigate     | Arrow keys         | D-pad / left stick   |
| Select       | Right Ctrl / Space / Left Ctrl / Enter | A or B button |
| Quit         | Esc (only at the top-level main menu) or window close (✕) | — |

### In-match (Player 1)

| Action       | Keyboard           | Gamepad 1            |
|--------------|--------------------|----------------------|
| Move         | Arrow keys         | D-pad / left stick   |
| Kick / shoot / pass / tackle | Right Ctrl / Space / Left Ctrl / Enter | A or B |
| Pause / unpause | `P`             | **+ / Start**        |
| Save a challenge position | `C` (one human side vs computer) | — |
| Statistics   | `S`                | **− / Back**         |
| Instant replay | `R`              | **Y**                |
| Replay after the match | `H`      | **X**                |
| Substitutions, bottom team | Triple-tap any direction | **L** |
| Substitutions, top team    | Triple-tap any direction | **R** |
| Quit the match | `Esc`            | **Right-stick click** |

Play itself only ever uses **two** pad buttons — A and B for fire, B doubling
as skip — so the rest are free, and everything the match understands beyond
fire and direction is a KEY. The pad bindings above inject those keys, which
is what makes them reachable on a console (no keyboard) and without reaching
for one on a desktop. Quit is on a stick click deliberately: it ends the match,
and a click is not something you hit by accident the way a face button is.
HOME / Guide is left alone — it belongs to the system on a Switch.

> Older versions of this table listed `Q` and `W` for the substitution menus.
> That was wrong: through the keycode LUT `Q` lands on a value the match does
> not handle and `W` on its explicit no-op. The real keys are UP and DOWN, and
> arrow keys are deliberately kept out of the key FIFO so they stay movement —
> so before the pad bindings there was **no key route to the substitution
> menus at all**, only the triple-tap.

### In-match (Player 2)

Player 2 is **gamepad-only** (the keyboard stays single-player so the
arrow keys don't get hijacked when a P2 pad is plugged in but unused).

| Action       | Gamepad 2          |
|--------------|--------------------|
| Move         | D-pad / left stick |
| Kick / etc.  | A or B             |

Two pads connected at startup map to P1 / P2 in enumeration order. Pads can
be hot-plugged; new pads fill the lowest empty slot.

**More pads than players?** Every connected controller is opened (up to
eight), but only two are played. To choose yours, **press fire on it during
the intro**: it becomes P1 (and the press skips the intro, as usual). If it
was P2's pad the two swap; otherwise the old P1 pad is left unused. A pad
that isn't played takes over when a played one is unplugged. `SWOS.log`
says which is which.

### Function keys

| Key          | Action                                                           |
|--------------|------------------------------------------------------------------|
| Alt + Enter  | Toggle fullscreen / windowed (uses the launch `--fullscreen` flavour) |
| `+` / `-`    | Increase / decrease windowed scale (1× → 2× → 3× → 4×). Ignored while fullscreen. |
| F9 / F11 / F12 / PrintScreen | Save screenshot to `bin/swos_shotNNN.png`            |

### Gamepad not detected?

SDL only drives gamepads it has a mapping for. If your pad isn't in SDL's
built-in DB (Xbox, PS4/5, Switch Pro, most Logitechs are covered), download
[`gamecontrollerdb.txt`](https://github.com/gabomdq/SDL_GameControllerDB)
and drop it next to the binary — it'll be picked up at startup.
`SWOS.log` lists every detected joystick and whether SDL has a mapping for
it.

## Saved challenges

Press **C** during a match against the computer to save the position. Choose
**CHALLENGES** on the main menu to resume a save or try the bundled **SAMPLE**.
See [CHALLENGE.md](CHALLENGE.md) for file locations, compatibility and limitations.

## Arcade mode

A coin-op presentation layer over the same match engine: no front-end, no
career, no save/load UI. The cabinet boots into an **attract loop**, takes
credits, plays short **first-goal-wins** matches, and has an operator
**service menu** in the style of a Midway/NBA Hangtime cabinet. It's a
PC-only extra — the Amiga game is the definitive product, and with arcade
mode off nothing here is reachable.

Turn it on in `SWOS.ini` (there is no command-line flag, so a stray argument
can't knock a cabinet out of coin-op):

```ini
[arcade]
enabled=true
```

**Default rules:** 45-second matches, first goal wins (golden goal), a draw
goes straight to sudden-death penalties, winner stays on. A challenger
inserts a credit to take the seat. Match length is counted in *game frames*,
not wall-clock, so it stays deterministic.

**Cabinet keys** (MAME layout, all rebindable via `key_*` in `[arcade]`):

| Key   | Function             |
|-------|----------------------|
| `5` / `6` | Insert coin — player 1 / player 2 |
| `1` / `2` | Start — player 1 / player 2 |
| `F2`  | Service (operator) menu |

The F2 service menu edits the game rules live — mode, `game_seconds`,
golden goal, penalties-on-draw, goal replay — and writes them back to
`SWOS.ini`, so an operator never has to hand-edit the file.

`docs/ARCADE.md` is the full spec: attract-reel structure, the service-menu
visual style and font, arcade-exclusive pitch art, high-score/name entry,
the CONTINUE? countdown, and the real-cabinet hardware target.
