# SHOOTING_PRACTICE.md — the computer plays with only its goalkeeper

A training drill: in a friendly against the computer, the computer's ten outfield
players are taken off and only its goalkeeper remains, so the human eleven can
practise finishing against a keeper instead of a defence.

Both targets. SDL and Amiga both get the button and the drill.

## Using it

PLAY MATCH screen → **PRACTICE**, under VIEW OPPO (the way UNIFORM sits under
EDIT TACTICS). Blue = off, green = on; the column is ~68 px, so the state is the
colour rather than an ON/OFF suffix.

Shown only when all of these hold, and forced off otherwise:

- `_squadChangesAllowed` — a match that can still be arranged, not a result screen
- not viewing the opponent's team
- not a training game (career's A-v-B squad match, a different feature entirely)
- `_gameType == 0` (a friendly — not career, season, cup or DIY)
- exactly one side is `TEAM_COMPUTER`

## How it works

| piece | where |
|---|---|
| mode latch + the strip + forced kickoff | `src/shooting_practice.c` |
| button: visibility, layout, toggle handler | `src/play_match_menu.c` (entry 69) |
| menu entry data | `bin/data_json/menus/playMatchMenu.json`, `src/menu_data.c`, `include/menu_data.h` |
| symbol registry (SDL only) | `src/menu_symbols.c` ← **regenerate it**, see MENUS.md §11 |
| strip hook | `InitPlayersBeforeEnteringPitch` (`src/match_setup.c`) |
| kickoff hook | `PrepareForInitialKick` (`src/match_setup.c`) |
| latch set | `InitializeInGameTeamsAndStartGame` (`src/game_init.c`) |

**The strip reuses `sentAway`** — the same `Sprite` flag a red card sets
(`SendPlayerAway`, `src/tackle_foul.c`). That flag is already honoured everywhere
it matters: AI (`ai.c`), ball possession (`ball_possession.c`), team control
(`team_control.c`) and the per-frame update (`update_players.c`) all skip those
players. A team a man down is an ordinary SWOS situation, so taking ten off
breaks no invariant — it is the same mechanism at a larger dose. The disciplinary
side effects are deliberately *not* applied: no card, no `sendingsOff` stat, no
SENT OFF banner. Nothing happened in the match; those players are simply not in
this drill.

Indices 1..10 of `TeamGeneralInfo.spritesTable` are stripped; index 0 is the
goalkeeper (`goalkeeper.c`: "first sprite = goalkeeper").

**The mode is latched for the match.** `ShootingPractice_BeginMatch()` records
*whether* the drill is on and *which* side is the computer, once, as the in-game
teams are set up. Everything afterwards reads that latch and never the menu
toggle — see the bug log below for why that matters.

**Every kickoff is taken by the human side.** The computer has nobody who can
take one. All kickoffs funnel through `PrepareForInitialKick`, so one hook there
covers match start, half time, extra time and the restart after a goal.

## Known rough edges

- Restarts that pick the "nearest player" (throw-ins, corners, free kicks) resolve
  to the computer's goalkeeper, so he leaves his area to take them. Harmless but
  odd-looking; it is how SWOS already behaves for a side with nobody closer.
- Offline friendlies only, by design. Online would need the host-switch treatment
  that CARDS/INJURIES gets (docs/NETPLAY.md), or the two peers would desync.

---

# The bug log — what went wrong, and how each was found

Kept because every one of these cost real time, and most would cost it again.

## 1. The button drew, highlighted, and did nothing (SDL)

**Symptom:** the entry rendered, the cursor landed on it (`[menu-nav] moved to
#69`), fire did nothing at all. No crash, no log, no clue.

**Cause:** `src/menu_symbols.c` is a *generated* name→address registry
(`scripts/gen_menu_symbols.py` from `bin/data_json/menus/_symbols.json`). Menus
are bound **by symbol name** through it. Names missing from that table resolve to
**NULL**, and the loader writes the NULL straight into the entry's `fg` and
`onSelect`. `MenuProc` then does `if (onSelectPtr == 0) goto no_event_or_done;`
— a silent no-op.

**How it was finally found:** dumping the live menu bytes. The giveaway is that
the *opcodes survive* and only the relocated pointer slots are zero:

```
UNIFORM  (registered):  08 00 00 00 | 58 0D 64 97 F7 7F 00 00   <- text ptr OK
                        0D 00       | 40 3F 5B 97 F7 7F 00 00   <- onSelect OK
PRACTICE (missing):     08 00 00 00 | 00 00 00 00 00 00 00 00   <- NULL
                        0D 00       | 00 00 00 00 00 00 00 00   <- NULL
```

That pattern rules out memory corruption (which would wipe the opcodes too) and a
link failure (the linked `.exe` on disk held the correct preferred-base
addresses — they were zeroed later, at bind time).

**Lesson:** adding a classic menu entry means four files, not three. MENUS.md §11.

## 2. The same symptom again on the Amiga — different cause

After enabling the feature for m68k, the button drew and highlighted but the
press did nothing. This time `menu_symbols.c` was irrelevant (it is SDL-only; the
Amiga binds straight from `menu_data.c`). The handler's *body* was still wrapped
in `#ifdef SWOS_SDL_BUILD`, so on the Amiga it compiled to an **empty function**.

**Lesson:** two different root causes can present identically. The second was
self-inflicted by copying UNIFORM's SDL-only guards into a feature that had no
reason to be SDL-only.

## 3. The restart after a goal hung the match

**Symptom:** scoring worked; then the ball went to the computer, its keeper
walked to the centre circle several times, and the match sat there.

**Cause:** the hooks read `g_playMatchShootingPractice` **live**, and
`UpdateShootingPracticeButton` clears that toggle whenever its button is off
screen. Mid-match the flag was 0, the kickoff hook returned "no change", and the
restart went to the side that conceded — the computer, whose keeper cannot take a
kickoff. `game_timers.c` then re-sends him on a timer
(`ST_WAITING_ON_PLAYER` + `playerNumber == 0`), which is the repeated walking.

**Fix:** latch the mode at match start instead of reading a menu global that
outlives the menu, and move the kickoff forcing into `PrepareForInitialKick`
(one hook, every kickoff) instead of two narrower ones.

**Lesson:** a menu global is menu state. Anything the match needs must be
captured into match state at kick-off.

## 4. The first position looked free and wasn't

The button was first put in USE MATCH TEAM's slot (entry 5), which is *text*-empty
during a friendly. It is not *pixel*-empty: the tactics/formation grid is drawn
there, and the button landed on top of it. Moved to share VIEW OPPO's column, the
way UNIFORM shares EDIT TACTICS'.

## 5. Every match crashed at kick-off (pre-existing, MinGW only)

`LoadSfxResources` segfaulted the moment a match started. Not caused by this
feature — a clean baseline with the feature stashed crashed identically, and the
gdb backtrace named no code of ours:

```
#0  LoadSfxResources () at src/audio.c:794
#1  StartMatchSequence ()  game_loop.c:1056
#5  InitializeInGameTeamsAndStartGame ()  game_init.c:1230
```

The faulting instruction was the tell:

```
=> mov %r8w,0xa8(%eax)        <- %eax, a TRUNCATED 32-bit address
rax = 0x7ff67a278500 <s_custom_storage>   (the valid 64-bit pointer)
```

`CUSTOM_BASE` was `((unsigned long)(_custom))`. Windows x64 is **LLP64**, where
`unsigned long` is 32 bits, so the custom-chip stub pointer was truncated and the
offset pokes dereferenced a remnant. LP64 hosts (Linux, Cygwin, macOS) and the
32-bit Amiga were all unaffected, which is why it had survived. Now `uintptr_t`.

The same crash is what killed the game ~12 s after boot if left idle: the attract
reel starts a match.

**Lesson:** `unsigned long` is not a pointer-sized type. CLAUDE.md already says
avoid `long`; this is what it costs.

## 6. The Amiga would not boot on a Windows checkout

AmigaDOS stopped at:

```
: Unknown command
  failed returncode 10
```

`core.autocrlf=true` had rewritten `bin/s/startup-sequence` to CRLF, so AmigaDOS
read `SYS:SWOS2\r`. `git ls-files --eol` showed it plainly (`i/lf w/crlf`). The
release drawer is staged on Linux, so the shipped zips were never affected — only
a Windows developer checkout. Pinned with `-text` in `.gitattributes`.

## 7. Toolchain hurdles (Windows)

- **MinGW vs Cygwin.** The shipped Windows binary is a *Cygwin* build whose SDL is
  `sdl2-compat` on **SDL3**; a MinGW build links real **SDL2**. Two defects showed
  up only on MinGW (#5 above, and `swos_io.c` including `<sys/stat.h>` on just the
  Cygwin/Linux/macOS branch). Develop on Cygwin to match what testers run; MinGW
  is still useful as a second compiler precisely because it is LLP64.
- **`python3` was the Windows Store stub.** `make` runs the JSON→C emitters, and
  the stub fails with `Error 49`, aborting the whole build. Either pass the real
  interpreter (`make PYTHON=...`) or keep the committed `.c` newer than its JSON.
  `scripts/emit_menus.py` also currently mangles UTF-8 em-dashes in
  `src/menu_data.c`, so check `git diff` after a regen.
- **Amiga build from native Windows** needs the bundled `gnumake.exe` plus
  `WINDOWS=1`. From MSYS2/Cygwin the recipes' `$(CURDIR)/$<` becomes `/c/...` or
  `/cygdrive/c/...`, which the native compiler mis-parses — the error looks like
  `'amera.o' is not recognized as an internal or external command`. docs/BUILDING.md.
- **ggpo-x submodule.** Netplay is default-on for Cygwin64, so
  `git submodule update --init third_party/ggpo-x` is required or the link fails.

## 8. FS-UAE hurdles

- Needs a **Kickstart ROM** (copyrighted; supply your own).
- Use `fast_memory`, **not** `zorro_iii_memory`: the A1200 has no Zorro bus, the
  option is ignored, and the game dies with "not enough memory available".
  2 MB chip + 8 MB fast, matching `.vscode/launch.json`.
- The Amiga build reads its data from the **mounted volume**, so `bin/` must hold
  `data grafs grafs_aga sound` and an existing `save/` — the SDL build reads the
  per-user assets folder instead, so a tree that runs fine under SDL can be empty
  here. `scripts/run_amiga_fsuae.ps1 -StageData` handles it.

## 9. Testing without a human at the keyboard

Most of the above was only pinned down once the game could be driven from a
script: `scripts/swos_drive.ps1` injects real key events and the game takes its
own F12 screenshots. See **docs/UI_DRIVER.md** — including the two traps that
wasted the most time (window focus, and `fprintf(stderr, ...)` not being reliable
evidence mid-match) and the gdb recipe that answered in one run what several
rounds of added logging could not.
