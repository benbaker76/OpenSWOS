# UI_DRIVER.md — driving the game from a script to test the UI

`scripts/swos_drive.ps1` drives the Windows SDL build without a human at the keyboard:
it injects real key events into the game window, and the game takes its own screenshots.
That closes the loop — a menu change can be navigated to, pressed, and *looked at*, from
a terminal.

It exists because verifying a menu change by asking a person to click through the game is
slow and unreliable; worse, a "nothing happens when I press it" report is ambiguous until
you can see the cursor land on the entry and watch what the press did.

## Running it

```powershell
# launch, skip the intro, screenshot
scripts\swos_drive.ps1 -Launch -Keys "Z,Z,Z,Z,Z,Z,Z,Z,Z,Z,Z,WAIT700,SHOT"

# keep driving the instance that is already up
scripts\swos_drive.ps1 -Keys "DOWN,DOWN,Z,WAIT800,SHOT"

scripts\swos_drive.ps1 -Shutdown
```

| key | meaning |
|---|---|
| `UP` `DOWN` `LEFT` `RIGHT` | menu navigation |
| `FIRE` / `Z` | fire / select (the game's default `key_fire`; it logs `[input] key_fire = Z` at boot) |
| `X` `ENTER` `ESC` `SPACE` | as named |
| `SHOT` | F12 — the game writes `bin/swos_shotNNN.png` |
| `WAIT<ms>` | pause, e.g. `WAIT1500` |

`-KeyDelayMs` sets the gap between keys (default 350). Menus need time to redraw; too
fast and presses are swallowed.

## The three feedback channels

You are not driving blind. Use all three:

1. **`bin/swos_shotNNN.png`** — the game's own F12 screenshot, written from the render
   buffer. This is the ground truth for "what is on screen". Prefer it over an OS screen
   capture: when the terminal runs in a different Windows session, an OS capture shows the
   wrong desktop entirely, while the in-game screenshot is always correct.
2. **`%USERPROFILE%\Documents\SWOS\SWOS.log`, `[menu-nav]` lines** — which entry the cursor
   is on, which link it followed, and what it skipped:
   ```
   [menu-nav]   #7 dir 4 -> 69
   [menu-nav]   moved to #69
   [menu-nav]   #29 invisible
   [menu-nav]   #57 dir 2 -> -1 (no link: stays)
   ```
   A line is written only when a **direction** is pressed — a fire press alone logs
   nothing, so absence of a line after `Z` proves nothing.
3. **`SWOS.ini` / boot lines** — confirm the fire key and the asset paths actually in use.

## Writing a route

Navigate by **ordinal**, confirmed from the nav log, not by counting keypresses. A blind
`Z`-count breaks the moment the intro takes one frame longer, and in team selection an
extra `Z` silently cycles a team's control type
(`NOT_SELECTED → COMPUTER → PLAYER-COACH → COACH`, `teams.c`) instead of advancing.

Verify at each decision point rather than chaining one long sequence. For a friendly:

```powershell
# 1. intro -> continent -> country -> division -> team list   (verify with SHOT)
-Launch -Keys "Z,Z,Z,Z,Z,Z,Z,Z,Z,Z,Z,WAIT700,Z,WAIT900,SHOT"

# 2. first team: Z twice => PLAYER-COACH (blue). second: RIGHT then Z once => COMPUTER (red).
#    Check the colours in the screenshot before going on.
-Keys "Z,WAIT600,Z,WAIT600,RIGHT,WAIT400,Z,WAIT800,SHOT"

# 3. down to the bottom row, then PLAY FRIENDLY (watch for "moved to #57")
-Keys "DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,WAIT400,Z,WAIT2500,SHOT"
```

## Gotchas

- **Focus is everything.** The script prints `focus ok=True` and the foreground window
  title. If that says anything other than the game, the keys went to another window and
  the game only *looks* unresponsive.
- **Idle crashes the attract reel.** Left alone on the main menu for ~10 s the game starts
  its demo and (in the MinGW build) dies in `LoadSfxResources` (`src/audio.c`). Keep
  driving, or expect the instance to disappear. Same crash if the window is left idle
  while you type elsewhere.
- **Kill the game before rebuilding.** A running instance holds `bin/SWOS.exe` and the
  link fails with `cannot open output file ... Permission denied`, leaving you testing the
  *old* binary.
- **`fprintf(stderr, ...)` is not reliable evidence mid-match.** Logging added to
  match-start code did not reach `SWOS.log` even on a healthy run, so "my log line never
  printed" does not prove the code never ran. Use a debugger for crashes.

## Crashes: get a backtrace, don't guess

`pacman -S mingw-w64-x86_64-gdb` (MSYS2), then run the game under gdb and drive it as
usual — gdb catches the fault and prints the stack:

```powershell
Start-Process C:\msys64\mingw64\bin\gdb.exe `
  -ArgumentList '-batch','-ex','run','-ex','bt','--args','SWOS.exe' `
  -WorkingDirectory <repo>\bin -RedirectStandardOutput gdb_out.txt -NoNewWindow
# ...drive with swos_drive.ps1 until it crashes, then read gdb_out.txt
```

This answered in one run what several rounds of added logging could not:

```
#0  LoadSfxResources () at src/audio.c:794
#1  StartMatchSequence () at src/game_loop.c:1056
#5  InitializeInGameTeamsAndStartGame () at src/game_init.c:1230
```

Windows also keeps minidumps in `%LOCALAPPDATA%\CrashDumps\` if the process dies outside
a debugger.
