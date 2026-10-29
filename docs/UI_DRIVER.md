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
# launch (no intro), wait for the main menu, go to the challenge list, screenshot
scripts\swos_drive.ps1 -Launch -Keys "LEFT,DOWN,DOWN,DOWN,DOWN,Z,MENU:challenge/hub,Z,MENU:challenge/list,SHOT"

# keep driving the instance that is already up
scripts\swos_drive.ps1 -Keys "DOWN,DOWN,Z,WAIT800,SHOT"

scripts\swos_drive.ps1 -Shutdown
```

**`-Launch` starts at the main menu.** It runs the game with `--skip-intro` and returns
only once `SWOS.log` says `[menu] show swosMainMenu` (~4 s), so the first key lands on the
main menu, with FRIENDLY (#6) selected. Never press fire through the intro instead: the
presses left over once it ends start the FRIENDLY flow, which is how a route to CHALLENGES
used to detour through friendlies. `-Intro` keeps the intro (when the intro is what is
tested; nothing can be waited on then), `-GameArgs` adds arguments.

| key | meaning |
|---|---|
| `UP` `DOWN` `LEFT` `RIGHT` | menu navigation |
| `FIRE` / `Z` | fire / select (the game's default `key_fire`; it logs `[input] key_fire = Z` at boot) |
| `X` `ENTER` `ESC` `SPACE` | as named |
| `SHOT` | F12 — the game writes `bin/swos_shotNNN.png` |
| `WAIT<ms>` | pause, e.g. `WAIT1500` |
| `MENU:<name>` | wait until the latest `[menu]` line names that menu or screen (`swosMainMenu`, `challengeMenu`, `challenge/list`, `swos2020/logInMenu` ...); the run stops with the menu it IS on if not within `-MenuTimeoutMs` (8000). Names are case sensitive |

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
3. **`SWOS.log`, `[menu]` lines** — which menu is up, every time one opens (`show`, from
   `ActivateMenu`) or comes back after a nested one closes (`back`), with the selected
   ordinal. A menu that draws several screens in one packed menu names the screen too:
   ```
   [menu] show swosMainMenu on #6
   [menu] show challengeMenu (challenge/hub) on #2
   [menu] back swosMainMenu on #5
   [menu] show challengeMenu (challenge/list) on #3
   ```
   (The `back` between two collection screens is real: the collection is a loop of
   `ShowMenu` calls from the main menu.) This is what `MENU:` waits on. Both kinds of line
   are on in a tester build; `SWOS_MENU_NAV_LOG=0/1` overrides.
4. **`SWOS.ini` / boot lines** — confirm the fire key and the asset paths actually in use.

## Writing a route

Navigate by **ordinal**, confirmed from the nav log, not by counting keypresses. A blind
`Z`-count breaks the moment the intro takes one frame longer, and in team selection an
extra `Z` silently cycles a team's control type
(`NOT_SELECTED → COMPUTER → PLAYER-COACH → COACH`, `teams.c`) instead of advancing.

Verify at each decision point rather than chaining one long sequence; `MENU:` after
every press that opens a menu.

**The main menu** (`swosMainMenu`, opens on #6):

| left column | | right column | |
|---|---|---|---|
| #1 | EDIT | #6 | FRIENDLY |
| #2 | PRACTICE | #7 | DIY COMPETITION |
| #3 | HIGHLIGHTS | #8 | PRESET COMPETITION |
| #4 | OPTIONS | #9 | SEASON |
| #5 | CHALLENGES | #10 | CAREER |

Below them MULTIPLAYER, LOAD OLD COMPETITION and CREDITS. From #6, `LEFT` reaches #1.

**A challenge** — the collection list, then the first challenge's details:

```powershell
-Launch -Keys "LEFT,DOWN,DOWN,DOWN,DOWN,Z,MENU:challenge/hub,Z,MENU:challenge/list,SHOT"
-Keys "Z,MENU:challenge/details,SHOT"       # PLAY from there starts the match
```

The hub's first row is CHALLENGE COLLECTION, the list opens on its first challenge. A
SAVED POSITION (`.CHL` in the save folder) skips the menus altogether:
`-GameArgs "--challenge=CHAL01.CHL"`.

**A friendly:**

```powershell
# 1. FRIENDLY (#6, where the main menu opens) -> continent -> country -> division ->
#    team list: one Z per screen, checking each with SHOT and the [menu] lines
-Launch -Keys "Z,WAIT900,SHOT"

# 2. first team: Z twice => PLAYER-COACH (blue). second: RIGHT then Z once => COMPUTER (red).
#    Check the colours in the screenshot before going on.
-Keys "Z,WAIT600,Z,WAIT600,RIGHT,WAIT400,Z,WAIT800,SHOT"

# 3. down to the bottom row, then PLAY FRIENDLY (watch for "moved to #57")
-Keys "DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,WAIT400,Z,WAIT2500,SHOT"
```

## The Amiga build is not driven by this script

`swos_drive.ps1` drives the SDL window only; a menu change on the m68k build is checked
by hand under FS-UAE for now. There the keyboard joystick's fire is **Right Ctrl**
(arrows move), not `Z`, and the m68k build writes no log, so a script would have to
recognise screens from captures of the window. The flow-testing framework drives it
through gdb memory writes instead (docs/TESTING_FLOWS_DESIGN.md).

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
