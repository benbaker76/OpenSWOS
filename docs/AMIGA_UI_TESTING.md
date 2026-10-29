# Fast Amiga UI testing with FS-UAE

This is the agent workflow verified on this Windows machine on 2026-10-29.
Use it for menu flows, challenge loading and state assertions. Read
[LOCAL_WORKFLOW.md](LOCAL_WORKFLOW.md) for building first. This guide supersedes
the older screenshot-driven route as the default for automated menu testing.

## Reproduce boot -> CHALLENGES -> play

Run from the repository in PowerShell. These commands use **Cygwin Python**,
not Windows Python (the transport requires Cygwin Unix sockets).

First inspect the owned session without changing the game:

```powershell
& C:/cygwin64/bin/bash.exe -lc 'cd /cygdrive/c/Users/A7126/orca/OpenSWOS && python3 scripts/testing_support/amiga_session.py status'
```

Use `status`, not `amiga_proxy.py health`. The health check only asks the debugger
daemon, and the daemon outlives its emulator. One session left behind with FS-UAE
closed still reported `DEGRADED target running` from health. `status` also checks
the recorded emulator, daemon and gdb processes and the build hashes. It prints one
line whose first word is the verdict:

| Verdict | Meaning | Next |
| --- | --- | --- |
| `NONE` | no owned session | `up` |
| `DEAD gdb exited` (or emulator, daemon) | nothing left to drive | `up` cleans up and boots |
| `STOPPED at <symbol>` | paused; `MenuInputInjectPoint` = at a menu | drive it |
| `RUNNING` | handed over (perhaps being played) | `pause`, if taking it is fine |
| `STALE ...` | `bin/SWOS2` rebuilt since boot | `down`, then `up` |

If there is no session, or the owned test session is paused at the main menu:

```powershell
& C:/cygwin64/bin/bash.exe -lc 'cd /cygdrive/c/Users/A7126/orca/OpenSWOS && python3 scripts/testing_support/amiga_session.py up && python3 scripts/testing_support/amiga_challenge_benchmark.py'
```

`up` either boots the current Amiga executable or reuses the same healthy,
paused session. The challenge script requires the **main menu**. Reuse does
not reset the game to that menu. It performs these actual menu activations:

1. CHALLENGES.
2. CHALLENGE COLLECTION.
3. The challenge by name: FINISH THE JOB, or the first argument
   (`amiga_challenge_benchmark.py "DERBY MIRACLE"`). It used to press "the
   selected entry", which is FINISH THE JOB only on a fresh boot. A list
   reopened after BACK keeps BACK selected, so the run backed out.
4. PLAY.

It verifies each menu transition, stops at `ChallengeProgress_PositionLoaded`
and then `GameLoop_Frame`, reads the match teams and human ownership, then
**releases** the game (below). The verified example is AC Milan against Juventus,
with Milan human-controlled. No user click is needed for these injected actions.

The game is deliberately **left running for the user**. Do not take it back or
rerun a main-menu script against it without a reason; `status` says `RUNNING`.

## Navigating by label: `amiga_goto.py`

```powershell
& C:/cygwin64/bin/bash.exe -lc 'cd /cygdrive/c/Users/A7126/orca/OpenSWOS/scripts/testing_support && python3 amiga_goto.py challenges'
& C:/cygwin64/bin/bash.exe -lc 'cd /cygdrive/c/Users/A7126/orca/OpenSWOS/scripts/testing_support && python3 amiga_goto.py BACK BACK FRIENDLY --release'
```

Each argument is a button label. The label is matched against the screen open at
that step: exactly one visible, enabled row, with surrounding spaces ignored. The
button is activated and the screen must change. Each step prints its time, the
screen and the buttons now shown. On failure it names the buttons that were
there. Presets: `challenges` (CHALLENGES, CHALLENGE COLLECTION) and
`saved-positions`. A preset checks that it starts on the main menu; plain labels
start wherever the stopped session is. Without `--release` the game stays stopped
at the menu anchor, ready for the next command. Measured: about 0.3 s per screen,
0.5 s for the collection list.

## Handing over and taking back: `release` / `pause`

```powershell
& C:/cygwin64/bin/bash.exe -lc 'cd /cygdrive/c/Users/A7126/orca/OpenSWOS && python3 scripts/testing_support/amiga_session.py release'
& C:/cygwin64/bin/bash.exe -lc 'cd /cygdrive/c/Users/A7126/orca/OpenSWOS && python3 scripts/testing_support/amiga_session.py pause'
```

- `release` disables the breakpoints and resumes with gdb's **background**
  `continue &`. The game runs, and gdb keeps its prompt.
- `pause` sends gdb's `interrupt` (the remote break; the stub stops with SIGTRAP
  in about 0.07 s). It re-enables the breakpoints and gives the game 3 s to reach
  the menu anchor. If a menu is up it stops at `MenuInputInjectPoint`, ready for
  `amiga_goto.py`; measured at 0.33 s, three cycles in a row. In a match it stays
  paused where it stopped (`paused at GameLoop + 308 (no menu reached: in a
  match?)`), and `release` resumes the match. Checked: `_frameCount` kept advancing.

Before these existed, the scripts handed over with a plain `continue`. That left
gdb blocked in the foreground with no way back, so taking a session back cost a
30 s reboot. `pause` takes a game from the user, so ask first if they might be
playing it. `up` never pauses a `RUNNING` session; it refuses.

**Never signal gdb to interrupt it.** Cygwin's `kill -INT` to the native win32 gdb
does not interrupt: it kills gdb. The debugger port is one-shot, so the session is
then undrivable until a reboot (tried 2026-10-29). Use `pause`.

`amiga_proxy.py` tracks the background state for the daemon. The stock AmigaScope
daemon treats every gdb prompt as "stopped". The wrapper marks the target running
after `continue &` or `interrupt`, until a stop shows up. A breakpoint on the very
next frame can arrive inside the command's own reply. The wrapper checks for that,
under the buffer lock. An earlier version missed it: the daemon then waited for a
stop that had already happened, and the next `interrupt`, sent to a stopped target,
garbled the remote link (`Invalid hex digit 83`). That handling could move upstream
into AmigaScope's `gdb_proxy.py`.

## Rebuilds, reuse and ownership

- A changed `bin/SWOS2` or `bin/SWOS2.elf` needs a fresh emulator boot. `up`
  compares their SHA-256 hashes against the session record and refuses stale reuse.
- Python test-script changes can reuse the session. Asset changes also need
  restaging/restarting: the executable hashes do not check asset contents.
- `up` reuses a paused session but does not attach a second debugger. It refuses
  to start a new session when another FS-UAE is already running. Leave the
  user's game untouched rather than bypassing this guard.
- The drawer is `build/amiga-ui/drawer/`. It copies the game and data from
  `bin/`, including saves, and removes any copied `data/CHALGEN.DAT` generation
  job. In-game writes go to this test drawer, not the regular save directory.
  The drawer persists between boots; this is not a pristine-save fixture.
- `owner.json` records Cygwin PID, Windows PID, start ticks and command line
  for the emulator, the daemon and (since 2026-10-29) the daemon's gdb.
  `down` checks those identities before stopping only the owned process trees.
  Do not replace it with `taskkill /IM`, `Stop-Process -Name`, or broad `pkill`.
- If any recorded process has exited (an emulator closed by hand, or a gdb that
  died), `up` logs `owned session is dead (...)`. It stops what is left through
  `down`, which is identity-checked, then boots fresh. Checked by killing the owned
  gdb: `status` said `DEAD gdb exited`, and `up` recovered in 29.6 s. It used to
  refuse and leave the cleanup to you.
- Run one driver at a time. The session, socket and observation files are shared
  by this workflow; concurrent driving is not supported.

When the test session is disposable (not a match handed to the user), restart
after a rebuild with:

```powershell
& C:/cygwin64/bin/bash.exe -lc 'cd /cygdrive/c/Users/A7126/orca/OpenSWOS && python3 scripts/testing_support/amiga_session.py down && python3 scripts/testing_support/amiga_session.py up && python3 scripts/testing_support/amiga_challenge_benchmark.py'
```

For cleanup only, run the same `amiga_session.py down` command without the
following `up`. Missing/incomplete owner records must be investigated; do not
delete ownership evidence and kill every emulator to recover.

## Timing and evidence

Measured on 2026-10-29, normal startup settings, existing built executable:

| Stage | Seconds |
| --- | ---: |
| Fresh boot, debugger attach, symbols, intro skip, main menu | 29.397 |
| Main menu -> hub -> collection -> challenge details | 1.318 |
| PLAY -> loaded position -> first match frame | 12.679 |
| Combined measured execution | 43.394 |
| Reusing the paused session | approximately 0.1 |

`build/amiga-ui/challenge-benchmark.json` records the phases, menu steps,
team names and ownership. `total_seconds` is **boot + measured post-boot route**;
it excludes agent/tool gaps between commands, team inspection after the timing
boundary, and subsequent visual/play testing. When reusing a session,
`boot_seconds` is the original session's recorded cold cost, not a new boot.
Use `post_boot_seconds` for the warm route cost.

The user confirmed seeing the challenge start. The automated assertions prove
menu transitions and loading through the first match frame, not comprehensive
render correctness, keyboard delivery or a completed playable match.

For the small menu speed comparison, start paused at the main menu and run:

```powershell
& C:/cygwin64/bin/bash.exe -lc 'cd /cygdrive/c/Users/A7126/orca/OpenSWOS && python3 scripts/testing_support/amiga_benchmark.py'
```

This runs main -> FRIENDLY -> EXIT three times per mode and leaves the game
paused at the main menu. Recorded results: approximately 2.10 s / 26 requests
without input batching, and 1.00 s / 8 requests with batching. Results go to
`build/amiga-ui/benchmark.json`.

Do not add fixed multi-second sleeps to menu routes. Report actual boot, action,
observation and match-load timings separately. Expect boot after each rebuilt
executable; reuse only removes repeated boot for the same build. Experimental
`SWOS_AMIGA_WARP=1` and `SWOS_AMIGA_CPU_SPEED=max` each still took about 29 s
here. Leave both unset for ordinary checks; do not promise accelerated startup.

## Extending the driver correctly

Use `amiga_goto.py` (label routes) and `amiga_challenge_benchmark.py` (into a
match) as the working examples. The client setup in
`amiga_benchmark.py` selects these values **before importing the adapter**:

| Setting | Value |
| --- | --- |
| Proxy | `scripts/testing_support/amiga_proxy.py` |
| `SWOS_GDB_SOCKET` | `/tmp/swos-amiga-ui.sock` |
| `SWOS_TEST_SCRIPT` | Cygwin absolute path to `build/amiga-ui/observe.gdb` |
| `SWOS_TEST_SCRIPT_GDB` | Windows path to that same file, obtained with `cygpath -am` |

`FastGdb` talks directly to the resident daemon. Keep `SWOS_TEST_FAST` and
`SWOS_TEST_BATCH` at their default enabled values. The older shared endpoint
`/tmp/swos_gdb.sock` and old `amiga_gdb.sh` recovery are not this owned workflow.
Prefer `observe(prime=False)` at a known stopped boundary: it refuses a running
target rather than invoking the plugin's older automatic recovery paths.

Resolve an action to exactly one visible, enabled row using its current label
or callback. Strip surrounding label whitespace. `activate(ordinal)` sets the
selection, pulses fire for one frame through normal menu dispatch and releases
it, in one sourced GDB script. Check the resulting complete snapshot/fingerprint
or expected labels. Challenge hub/list/details share one initializer, so checking
only `ChallengeCollection_MenuInit` does not prove which screen is open.

Directly selecting an entry tests menu actions, **not arrow-navigation links**.
Use directional input and selected-entry assertions for navigation tests. Debugger
injection also bypasses Windows/SDL input delivery. Do not describe it as proof
that physical keyboard input or focus works.

For game entry, install the expected match/load breakpoint before pressing PLAY.
The ordinary menu anchor will not be hit while a match runs. Bound waits; a batch
without `ACTION_DONE` is a failure, not permission to fire again. Check where the
target stopped before continuing. Never retry a potentially applied action blindly.

The FS-UAE GDB port is **one-shot**. Never probe it with `Test-NetConnection` or
a temporary TCP client. The launcher connects first, uses `qOffsets` to relocate
symbols, checks memory writes, skips the intro, and stops at
`MenuInputInjectPoint`. Use the matching ELF. Do not use inferior function calls
as a substitute for the supported memory reads/writes on this fork.

## Focus, screenshots and diagnostics

The Amiga build does not emit the SDL `SWOS.log`/`[menu-nav]` stream. Read menu
state via the debugger and write reports on the host. The relevant paths are:

- `build/amiga-ui/owner.json`: owned process identities and build hashes.
- `build/amiga-ui/challenge-benchmark.json` and `benchmark.json`: timings/evidence.
- `C:/cygwin64/tmp/amiga_daemon.log`: attach/daemon diagnostics.
- `C:/Users/A7126/AppData/Local/FS-UAE/fs-uae.log`: emulator log observed in this run.
  Older docs name a different log path; check freshness before trusting it.

For a screenshot or real host-input check, get `emulator.winpid` from the owner
record and explicitly target it:

```powershell
$testSession = Get-Content build/amiga-ui/owner.json -Raw | ConvertFrom-Json
scripts/amiga_drive.ps1 -EmulatorId $testSession.emulator.winpid -Shot -Tag challenge
```

This is the legacy desktop-capture path; its capture can be obscured by another
window. Check the image. Its focus helper can fail and currently only warns:
if focus is not confirmed, stop that input attempt and do not claim it passed.
Do not use its `-Launch`, untargeted `-Shutdown`, or old `-Route challenge` as
the new default. Those lifecycle/routes predate this isolated workflow and the
collection hub. See [AMIGA_DRIVER.md](AMIGA_DRIVER.md) for the legacy controls.

A stopped debugger makes the window look frozen. Explain whether the target is
paused or running. Hand control to the user with `amiga_session.py release` (or
`amiga_goto.py ... --release`), as the challenge benchmark does. A plain
`continue` also runs the game, but it leaves gdb unable to `pause` it. Do not
leave per-frame stops armed.
