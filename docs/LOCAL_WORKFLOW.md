# LOCAL_WORKFLOW.md — build, run and verify on this machine

**Read this before running anything.** It is the session-startup answer to "how do I build
it, how do I run it, how do I prove a change works" on *this* box. Everything in it was paid
for once by rediscovery; none of it needs deriving again.

**The paths in `.claude/CLAUDE.md` and `build.cmd` are the ORIGINAL AUTHOR's machine**
(`C:\Users\headk\.vscode\extensions\bartmanabyss.amiga-debug-1.8.2\...`, `cd /d
C:\Projects\AI\SWOS\OpenSWOS`). Neither exists here. A build driven from them fails as
`Error 127` (compiler not found) or silently in the wrong directory. Use what is below.

## Where everything is

| | path |
|---|---|
| repo | `C:\Users\A7126\orca\OpenSWOS` |
| m68k toolchain | `C:\Users\A7126\tools\vscode-amiga-debug\bin\win32` — a plain **clone of the owner's fork**, NOT under `.vscode\extensions`. Holds `opt\bin\m68k-amiga-elf-*` (GCC 15.1.0), `gnumake.exe`, `fs-uae\`, `elf2hunk.exe`, `vasmm68k_mot.exe`, `winuae-gdb.exe` |
| Cygwin (the SDL build) | `C:\cygwin64` — 3.6.11, gcc 14.4.0, python3 3.12.12, `libSDL2-devel` 2.32.74 |
| real Python for git-bash | `C:\Users\A7126\AppData\Local\Programs\Python\Python312\python.exe` |
| Kickstart ROMs | `C:\Users\A7126\Downloads\kickstarts` — the FS-UAE script auto-picks `Kickstart v3.1 rev 40.68 (1993)(Commodore)(A1200).rom` |
| **saves + `SWOS.log`** | `C:\Users\A7126\Documents\SWOS\` — **not** `bin/SWOS.log`, which is what CLAUDE.md's prose implies |
| assets (converted game data) | `C:\Users\A7126\Documents\SWOS\assets`; `bin/` already has `data grafs grafs_aga sound` staged, which is what the Amiga build needs |

## Building

```sh
# SDL -> bin/SWOS.exe (a Cygwin binary; the Makefile picks the target from the HOST SHELL,
# never from a flag, so it must be driven from the Cygwin shell)
/c/cygwin64/bin/bash.exe -lc "cd /cygdrive/c/Users/A7126/orca/OpenSWOS && make -j8"

# Amiga m68k -> bin/SWOS2
AMIGA=/c/Users/A7126/tools/vscode-amiga-debug/bin/win32
PATH="$AMIGA/opt/bin:$AMIGA:$PATH" gnumake -f Makefile.amiga -j8 \
    PYTHON=/c/Users/A7126/AppData/Local/Programs/Python/Python312/python
```

Three traps, each of which looks like something else:

- **`make` is not on the git-bash PATH.** The toolchain ships `gnumake.exe`; use that name.
- **Bare `python3` in git-bash is the Windows Store stub.** The data-regen rules
  (`menu_data.c`, `competition_data.c`, `tactics_data.c`) die with `Error 49` and abort the
  whole build. Pass `PYTHON=`. The *Cygwin* shell has a real `python3`, so the SDL build
  needs no override — and `python3 scripts/*.py` should always be run from that shell.
- **`gnumake -f Makefile.amiga clean` is the only thing that wipes `obj/`.** `rm -rf build`
  does not; a stale `obj/` reports "Nothing to be done for 'all'".

A `-D` flag change rebuilds everything on both targets (the `.build-flags` stamp), so a
toggle needs no manual clean.

## Running the SDL build

```sh
cd bin && ./SWOS.exe                 # the window opens on the loader intro
```

- **The intro waits on the configured SHOOT/fire button** (`z` with the current bindings).
  Without it a timed run spends its whole life in the intro and never reaches a menu. The
  boot log prints the binding: `[input] key_fire = Z`.
- `--skip-intro` **starts at the main menu**, which then runs as a player sees it. Any
  scripted run starts here; fire pressed through the intro instead falls onto FRIENDLY.
- `--menu=<name>` **skips the intro and opens that menu directly** (it sets
  `_skipLoaderIntro`), e.g. `--menu=editMenu`, `--menu=p2p/p2pMenu`. Fastest look at one
  screen by far.
- The log is `~/Documents/SWOS/SWOS.log`. stderr is redirected into it; stdout is block
  buffered and shows nothing useful while the game runs.
- `scripts/swos_drive.ps1` (docs/UI_DRIVER.md) injects keys and takes in-game screenshots
  into `bin/swos_shotNNN.png`:

  ```powershell
  scripts\swos_drive.ps1 -Launch -Keys "SHOT"     # no intro; returns once the main menu is up
  scripts\swos_drive.ps1 -Keys "LEFT,DOWN,DOWN,DOWN,DOWN,Z,MENU:challenge/hub,SHOT"
  scripts\swos_drive.ps1 -Keys "WAIT200"           # focus it, press nothing
  scripts\swos_drive.ps1 -Shutdown
  ```

  **Navigate by ordinal from the `[menu-nav]` log and wait on the `[menu]` lines
  (`MENU:<name>`), not by counting fire presses** — blind `Z`s through the intro land in
  the FRIENDLY flow, and in team selection a stray `Z` cycles a team's control type
  instead of advancing. The main menu's ordinals are in docs/UI_DRIVER.md. Delete the screenshots afterwards:
  they are untracked and NOT gitignored.

## Running the Amiga build

For automated FS-UAE menu testing, use [AMIGA_UI_TESTING.md](AMIGA_UI_TESTING.md):
the owned debugger session boots once per build, drives menus without focus,
and reports boot/navigation/match-load timings separately. The commands below
are the ordinary interactive launcher.

```powershell
scripts\run_amiga_fsuae.ps1              # finds FS-UAE + the A1200 Kickstart by itself here
scripts\run_amiga_fsuae.ps1 -Build       # build first (uses WINDOWS=1 + the bundled gnumake)
scripts\run_amiga_fsuae.ps1 -StageData   # copy the game data out of the assets folder into bin/
```

`bin/` is mounted as the boot volume, so `s/startup-sequence` runs `SYS:SWOS2` then
`c/UAEquit` — quitting the game closes the emulator. **FS-UAE does not notice a relink:**
after rebuilding, close only the appropriate disposable test session and relaunch,
or you are looking at the previous binary. Never kill all FS-UAE processes by name
or stop a game the user is playing. The owned test launcher checks build hashes
and provides a targeted `amiga_session.py down` command.

## Proving a data/menu change (before claiming it works)

```sh
# 1. the baked C and the JSON are the same bytes, leaf for leaf
/c/cygwin64/bin/bash.exe -lc "cd /cygdrive/c/Users/A7126/orca/OpenSWOS && \
    python3 scripts/emit_menus.py --out /tmp/menu_data_regen.c"
diff <(tr -d '\r' < src/menu_data.c) <(tr -d '\r' < /c/cygwin64/tmp/menu_data_regen.c)

# 2. the running game agrees with its JSON
cd bin && SWOS_MENU_JSON=1 SWOS_MENU_JSON_VERIFY=1 ./SWOS.exe
grep menu-json ~/Documents/SWOS/SWOS.log
#   [menu-json] verify: 48/48 byte-identical, 0 differ, 0 error
#   [menu-json] install: 48/48 menus loaded from JSON in place (0 edited vs compiled, ...)
```

The counts must go **up by one** when a menu is added. This check is logged at start-up,
before the intro matters, so a timed run with no input is enough for it.

> **`scripts/emit_menus.py` with no `--out` always crashes**, in its round-trip verify:
> `FileNotFoundError: ..._buyOtherPlayersMenu.json`. It looks for `_<name>.json` while the
> corpus files have no leading underscore. **Pre-existing and unrelated to whatever you just
> changed** — don't go hunting. The `--out` path (what the build uses) returns before it.

Also: `emit_menus.py` writes **cp1252**, so check `git diff src/menu_data.c` for mangled
accents afterwards (`Karakaš` in the file header is the canary), and it **splices values
into braces that already exist** — a structural change (an entry added or removed, a new
menu) must be hand-made in `src/menu_data.c` first or it aborts with "template has N leaves
but JSON has M values".

## Menu entry colours

The `colors` opcode is one byte, `frame<<4 | background`, each nibble an index into the
16-colour menu palette (`_menuPalette`, `src/swos_globals.c` — the AGA table and the OCS
fallback). `MenuEntryColors` in `include/menu_data.h` names them; the names describe the
gradient, so they are not obvious:

| idx | AGA / OCS | reads as | name |
|---|---|---|---|
| 3 | `0x0000` / `0x0102` | near-black blue | `BLUE_TO_BLACK_3` |
| 4 | `0x0620` / `0x0621` | dark brown-red | `ORANGE_TO_BROWN_4` |
| **5** | `0x0B40` / `0x0A40` | **burnt orange** | `ORANGE_TO_BROWN_5` |
| 6 | `0x0F60` / `0x0F71` | bright orange | `ORANGE_TO_BROWN_6` |
| 8 | `0x0222` / `0x0204` | very dark | `BLUE_TO_BLACK_8` |
| 10 | `0x0F00` | red | `RED_10` |
| 11 | `0x000F` | blue | `BLUE_TO_PURPLE_11` |
| 12 | `0x0602` / `0x0702` | maroon | `ORANGE_TO_BROWN_12` |
| 13 | `0x099F` / `0x088F` | light blue-violet | `BRIGHT_BLUE_TO_BLUE_13` |
| 14 | `0x0290` / `0x0380` | green | `YELLOW_GREEN_BLACK_14` |
| 15 | `0x0FF0` | pure yellow | `YELLOW_GREEN_BLACK_15` |

There is no amber between 6 and 15 — the palette has no entry there. A colour change is one
nibble in **two** files (the menu's JSON and `src/menu_data.c`), which must agree.

## Changing menus at all

`docs/MENUS.md` **§11** (adding an entry to a classic menu) and **§12** (adding a whole new
menu — five places, `s_staticMenus` being the forgotten fifth) are the checklists. Read them
before touching a menu; both exist because the failure mode is a button that draws and
highlights but does nothing when pressed.
