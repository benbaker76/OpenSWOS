# AMIGA_DRIVER.md — driving the Amiga build under FS-UAE from a script

For agent-driven menu tests, start with [AMIGA_UI_TESTING.md](AMIGA_UI_TESTING.md).
That is the tested, fast debugger workflow with isolated saves, state assertions
and timing reports. This page documents the **legacy host-key/screenshot driver**.
Its launch/untargeted shutdown can stop other emulators; its challenge route
predates the collection hub. Use it with an explicit `-EmulatorId` for screenshots
or host-input checks, not as the default automated route.

`scripts/amiga_drive.ps1` drives the **m68k** build inside FS-UAE without a human at the
keyboard: it injects real OS key events into the emulator window, grabs that window to
PNG, and collects the small `.BIN` dumps the game writes into `bin/`. It is the Amiga
counterpart of `scripts/swos_drive.ps1` (docs/UI_DRIVER.md), and it exists for the same
reason — "does this menu change work on the Amiga?" should be answerable from a terminal.

Use `-EmulatorId <pid>` to drive an existing specific session while other emulators
(for example, isolated challenge generators) run. Do not combine it with `-Launch`.

The difference from the SDL harness is what you get to look at. **The Amiga build writes
no `SWOS.log` and has no `[menu-nav]` lines**, so this legacy driver has no logged ordinal to navigate by:
every step of a route below was derived from a *screenshot*, and the screenshots a run
leaves behind are how this driver checks where it landed. The newer debugger
workflow reads menu ordinals and state directly, without needing Amiga logs.

## Running it

```powershell
# cold boot -> live match -> ESC back to the main menu   (~80 s, nothing to press)
scripts\amiga_drive.ps1 -Route friendly

# cold boot -> CHALLENGES -> first .CHL loaded -> back   (~50 s)
scripts\amiga_drive.ps1 -Route challenge

# a single screen of your own
scripts\amiga_drive.ps1 -Launch -WaitMenu -Shot -Tag mainmenu
scripts\amiga_drive.ps1 -Keys "LEFT,DOWN,DOWN,DOWN,DOWN,FIRE,WAIT3000,SHOT:challenges"
scripts\amiga_drive.ps1 -CollectDiags -Tag after
scripts\amiga_drive.ps1 -Shutdown

# and the two that make a route a test rather than a demo (see "Checking the result")
scripts\amiga_drive.ps1 -Route friendly -Check
scripts\amiga_drive.ps1 -Route friendly -Compare build\baseline\friendly
```

Screenshots land in **`build\amiga_shots\`** (`-ShotDir` to change it), numbered in the
order they were taken: `001_main_menu_start.png`, `002_friendly_menu.png`, … `build/` is
gitignored, so a run leaves nothing behind in `git status`. A `-Route` run clears the shot
directory first unless you pass `-KeepShots`.

| key | meaning |
|---|---|
| `UP` `DOWN` `LEFT` `RIGHT` | menu navigation |
| `FIRE` | **Right Ctrl** — the Amiga joystick's fire button. Select/confirm everywhere |
| `ENTER` | the *Amiga keyboard's* own menu fire (`currentKey == 0x0D`, `MenuCheckControls`). Works in menus, **not** in the loader intro |
| `ESC` | abandons a match → REPLAY / EXIT |
| `SHOT[:label]` | grab the emulator window into the shot directory |
| `WAIT<ms>` | pause, e.g. `WAIT2500` |
| `RCTRL` `LCTRL` `RALT` `LALT` `NUM0` `NUM5` `NUMENTER` `RSHIFT` … | raw names, for probing a mapping |

`-KeyDelayMs` (default 250) is the gap between keys, `-HoldMs` (120) how long each is
held; menus need both or presses are swallowed. `-FireKey` renames what `FIRE` sends.

## The fire button: Right Ctrl

**FIRE is Right Ctrl**, and it is a *joystick* button, not a keyboard key.

The Amiga build reads the hardware directly — `Player1StatusProc` (`src/input.c`) takes
player 1's directions from `JOY1DAT` and the fire button from `CIAA_PRA` bit 7, i.e. the
**joystick in port 1**. FS-UAE's UAE core maps the host keyboard onto that port by
default, with Right Ctrl as the fire button; **measured on this machine, that mapping is
on with the stock configuration and stays on even with `joystick_port_1 = none`** in the
config file. `scripts/amiga_drive.ps1` still writes

```
joystick_port_1 = keyboard
joystick_port_1_mode = joystick
```

into its generated config — not because the mapping needs it here (it does not: proved by
running without those lines and skipping the intro with Right Ctrl anyway), but because it
says what the harness depends on, and because a machine with a gamepad plugged in is the
one place where port 1 could go somewhere else. If FIRE ever stops working, unplug the
pad first.

Right Alt also reaches the game as fire. Left Ctrl does not. The arrow keys move the menu
cursor either way — the menu takes both joystick directions and the Amiga keyboard's
cursor keys (`currentKey` 0x10–0x13 in `MenuCheckControls`).

### Why this matters: the loader intro

The game sits in `ShowLoaderIntroSequence` until **fire** is pressed —
`loaderIntroWaitForFireOrTimeout` polls `_pl1Fire` and nothing else, so **ENTER will not
skip the intro**. Left alone the intro also runs out by itself, but that takes about two
minutes (twelve loader frames at 250 vblanks each, then `loader1.raw` for 1750).

`Wait-ForMainMenu` handles both: it presses FIRE **once** ~13 s after launch — comfortably
inside the intro — then polls the window until the main menu appears, retrying the FIRE
twice more, and finally waits the intro out. It never presses FIRE blindly in a loop,
because a FIRE that arrives when the main menu is *already* up opens FRIENDLY and derails
every route after it.

"Is this the main menu?" is answered by pixels, since there is no log to ask: the row
across LOAD OLD COMPETITION is strongly red-dominant on the main menu
(R=119 G=47 B=71) and on no other screen a route visits (a live match is R=127 G=153 B=0,
team selection R=0 G=0 B=78). `Test-MainMenu` in the script is that comparison.

## The routes

Both start on the main menu with the cursor on its default entry, **FRIENDLY** — which is
what a fresh boot gives. `-NoLaunch` drives an instance that is already up, and is only
safe if you have put it back on the main menu yourself.

### `-Route friendly`

FRIENDLY → OK → CHOOSE TEAMS → CLUB TEAMS → EUROPE → GERMANY → 1. LIGA is a straight run
of fire presses: every one of those screens opens with its cursor already on the entry we
want. Then, in team selection:

- **two** FIREs on the team under the cursor (FORT. DUSSELDORF) make it a blue
  **PLAYER-COACH** — the control type cycles `NOT_SELECTED → COMPUTER → PLAYER-COACH →
  COACH` (`teams.c`), so a stray press cycles a team instead of advancing;
- `LEFT` then **one** FIRE makes the neighbour (1860 MUNICH) a red **COMPUTER**;
- six `DOWN`s out of the 6-row grid and two `RIGHT`s reach **PLAY FRIENDLY** (the bottom
  row is VIEW SELECTED TEAMS / EXIT / PLAY FRIENDLY, and it only appears once two teams
  are picked);
- FIRE, then FIRE again on **PLAY/COACH MATCH** on the line-up screen → kick-off.

Shots: `004_team_list`, `005_teams_picked` (check the colours — blue one side, red the
other), `007_lineup`, `008_kickoff`, `009_match` (players on the pitch, "0 mins.").

### `-Route challenge`

CHALLENGES is the left column's bottom row, so from FRIENDLY: `LEFT`, four `DOWN`s, FIRE.
The picker lists only the `.CHL` files in `bin/save` (`CHAL01` / `MATCH`) with the first
one under the cursor, so one more FIRE loads it.

Shots: `002_challenge_picker`, `003_challenge_teams` (ITALY v LIECHTENSTEIN line-ups),
`004_challenge_match`.

### Coming back

Unless you pass `-Stay`, both routes finish with `ESC` (abandons the match → REPLAY /
EXIT), `DOWN`, FIRE (EXIT) → main menu, and screenshot it.

## Checking the result without a human

A screenshot nobody looks at proves nothing, so the harness judges its own shots. Two
independent checks, both usable on their own:

**What is on screen** (`-Check`, or `-Classify <dir>` on shots taken earlier). Each shot
is reduced to structural features of the *emulated screen only* — how much of it is
black, how much is pitch green, how much is white line art, how much is red or blue, how
noisy it is — and from those it gets a kind: `BLACK`, `PITCH`, `MAINMENU`, `GREEN-NOLINES`
or `OTHER`. Every route declares what its shots must be; everything else is `ANY`:

| route | shot | must be |
|---|---|---|
| friendly | `main_menu_start` | `MAINMENU` |
| friendly | `teams_picked` | `REDBLUE` — one side red (COMPUTER), the other blue (PLAYER-COACH) |
| friendly | `match` | `PITCH` — green pitch *with* line markings |
| friendly | `main_menu` | `MAINMENU` |
| challenge | `main_menu_start`, `challenge_match`, `main_menu` | `MAINMENU`, `PITCH`, `MAINMENU` |

```powershell
scriptsmiga_drive.ps1 -Route friendly -Check          # exit code 0 only if every shot is right
scriptsmiga_drive.ps1 -Classify buildmiga_shots -Route challenge   # offline, no emulator
```

```
  shot                       is             expected       black/green/white/noise %
  main_menu_start            MAINMENU       MAINMENU        31.8   0.1   6.0   5.8  ok
  challenge_match            BLACK          PITCH          100.0   0.0   0.0   0.0  FAIL
  main_menu                  OTHER          MAINMENU        15.9   3.3   7.6   8.3  FAIL
```

`BLACK` is the kind that matters most: a match that never drew looks exactly like a match
that is fine to a run nobody watches. The verdict is always printed; it only decides the
**exit code** when `-Check` is passed, because "the route landed where it should" and "the
game drew the right thing" are different questions and the challenge route currently
answers them differently.

**Did anything change** (`-Compare <baseline-dir>`, `-SaveBaseline <dir>`). Shots are
compared against a saved set, paired by *label* (the part after the leading number, so a
baseline still matches when a route gains a shot before the one you care about), and a
shot differing by more than `-DiffPct` (default 2%) of sampled pixels is a FAIL.

```powershell
scriptsmiga_drive.ps1 -Route friendly -SaveBaseline buildaselineriendly
# ...change something, rebuild, then:
scriptsmiga_drive.ps1 -Route friendly -Compare buildaselineriendly
scriptsmiga_drive.ps1 -Compare buildaselineriendly   # offline, compares -ShotDir
```

Both comparisons look at the **emulated screen only** and allow a per-channel tolerance.
FSEMU's title bar and LED panels are outside that box — the VSYNC and POWER LEDs change
colour between frames, so comparing whole windows makes every shot "differ" — and FS-UAE
scales the Amiga frame with interpolation, so the same screen can land a shade apart.

## Gotchas

- **Focus is everything, and so is what is on top.** Keys go to the foreground window, and
  a screen grab copies whatever covers that rectangle. The script re-focuses the emulator
  before every key run and before every shot, but **typing in another window while a route
  runs still steals the foreground between calls** — the symptom is a "frozen emulator"
  screenshot that is actually a picture of your editor. Start a route and leave the
  keyboard alone.
- **The grab needs DPI awareness.** The emulator window is DPI-scaled (125% here).
  Without `SetProcessDPIAware`, `GetClientRect`/`ClientToScreen` return *logical* pixels
  while `CopyFromScreen` works in *physical* ones, and the capture lands a couple of
  hundred pixels up and left of the window — half desktop, half game. The script calls it
  before it reads any window rectangle.
- **Only one emulator at a time.** The script remembers the pid it launched and watches
  *that* one. If something else starts an FS-UAE while a route runs — the amiga-debug
  VSCode extension's run button does, and it kills the emulator already there — the route
  dies mid-flight; the script says so by pid instead of failing obscurely
  ("the emulator this run launched (pid N) has exited ... Another fs-uae IS running").
  It also prints a `note:` line when it has to kill instances it did not start.
- **FS-UAE does not notice a relink.** After rebuilding `bin/SWOS2`, `-Launch`/`-Route`
  kill `fs-uae` first; if you start the emulator yourself, do the same or you are testing
  the previous binary.
- **Keep the window at 1440x900.** A 960x600 window makes FS-UAE die with "Not a valid
  drawable size for glViewport".
- **The window is not the Amiga screen.** FSEMU paints its own title bar and two LED
  panels around the picture; `Get-EmuRect` finds the title bar by colour and takes the
  4:3 box centred under it, which is what the pixel tests use. A shot keeps the whole
  window, chrome included — the LEDs change colour between frames, so two screenshots of
  the same game screen are not always byte-identical.
- **`bin/` must hold the game data** (`data grafs grafs_aga sound` and a `save/`): the
  Amiga build reads relative paths from the mounted volume and will not create `save/`.
  `scripts/run_amiga_fsuae.ps1 -StageData` fills it from the assets folder.
- **Diagnostics.** `-Launch`/`-Route` delete `CHALDBG.BIN CHALCAM.BIN PITCH.BIN RTRIP.BIN
  LAYOUT.BIN` from `bin/` before the run and `-CollectDiags` copies what the run wrote
  into `<shots>/diags_<tag>/`, printing each one's first 32 bytes (big-endian). A friendly
  match writes none of them; the challenge path writes `PITCH.BIN`, `RTRIP.BIN` and
  `LAYOUT.BIN`.

## Known, and not the harness's fault

Both reproduce on every run of `-Route challenge` (2026-10-23, build 3304748-dirty):

- **The challenge match renders black.** `003_challenge_teams` draws the ITALY v
  LIECHTENSTEIN line-ups correctly, then the match itself is an all-black screen. The
  match *is* running — `ESC` brings up REPLAY / EXIT, and `PITCH.BIN`/`RTRIP.BIN`/
  `LAYOUT.BIN` are written — it just never draws.
- **The main menu comes back corrupted after a challenge.** Leaving a challenge match
  through EXIT redraws the main menu over a garbage background, with extra rows
  (MULTIPLAYER and two blank bars) and LOAD OLD COMPETITION pushed down. The friendly
  route's EXIT returns to a clean main menu, so it is specific to the challenge path.

## Related

- `scripts/run_amiga_fsuae.ps1` — plain "run it under FS-UAE", no driving.
- `docs/LOCAL_WORKFLOW.md` — building both targets on this machine.
- `docs/UI_DRIVER.md` — the SDL harness, which has a log to navigate by.
