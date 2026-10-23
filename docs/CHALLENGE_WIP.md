# SAVE STATES / CHALLENGES — work in progress, 2026-10-23

Handover for whoever picks this up. Nothing here is committed. The feature works on SDL
and is broken on the Amiga in one specific way (§3). §6 is about the debugger, which is
the thing to fix first.

### Debugger investigation update (2026-10-23)

Repeatable Windows workflow and full findings: [AMIGA_DEBUGGING.md](AMIGA_DEBUGGING.md).

The debugger is now usable in a persistent native Windows GDB session. A missing
step in §6 was **hunk relocation**: `symbol-file` alone leaves symbols at ELF link
addresses. `maintenance packet qOffsets` reports the actual section bases. Load
them with `add-symbol-file <elf> <text-base> -s .data <data-base>
-s BSSInChip.MEMF_CHIP <chip-bss-base> -s .bss <bss-base>`. Query each launch;
do not hard-code addresses. This made `Challenge_LoadState` breakpoints fire.
The current ELF has a standalone function; the previous inlining explanation was
not established. Some file-static symbols still read incorrectly; the camera
globals and source breakpoints were verified against live instructions/state.

Live debugger measurements found a scroll-phase mismatch after challenge load:
both buffers' world cameras were `(198,585)`, but physical scroll offsets
alternated between `(168,16)` and `(198,41)`. A normal friendly maintained matching
phases in both buffers across consecutive frame boundaries.

The load replaces `_cameraX/Y` while retaining the renderer's scroll offsets.
The first `SetCameraCoordinates` in the repaint therefore cannot apply the loaded
camera delta to the first buffer. The second buffer still has its old camera,
so it gets that delta. There is also one shared clean-pitch mask used for undraw.

`src/game_loop.c` now remembers the render camera before loading, remembers the
loaded target, and temporarily restores only the old integer camera before the
existing replay repaint. The loaded fractional words remain intact. In a live GDB
experiment, both buffers then had camera `(165,273)`, scroll `(165,1)` on frames
8004 and 8005. **The user confirmed the corrected live match is working.**
The source fix builds for Amiga and SDL. The user subsequently cold-booted the
rebuilt Amiga binary and confirmed CHAL02 and the original CHAL01 both work.
The early-exit/reopen menu follow-up in AMIGA_DEBUGGING.md was also rebuilt and
confirmed working by the user. A single target-specific SAMPLE.CHL is now installed
when entering CHALLENGES; personal captures are not bundled.

The `C` capture was also exercised: `Challenge_SaveNow` returned 0 and wrote
`CHAL02.CHL` (18,752 bytes). Its header and payload both contain frame 8004;
camera raw words `0x00a50000,0x0111c000` exactly match the debugger at save and
after reload (165,273.75). This checks those fields, not the whole simulation.
Audit follow-up: the header hash is computed from live state after serialization,
and its halves use separate `SimState_Hash` calls. Interrupt-driven ticks can
change between those operations, so it is not a reliable payload-integrity check.

Primary evidence: `build/amiga_debug/friendly-gdb.log` and
`build/amiga_debug/INVESTIGATION.md`; captured CHAL02 is retained there too.
Screenshots are ancillary. The old debug session was stopped and a fresh emulator
launched for the user's confirmation. Rebuilding alone never updates a running program.

---

## 1. What the feature is

Press a key mid-match and the position — ball, all 22 players, clock, score, scorers,
bookings, substitutes left, crowd — is written to a `.CHL` file. Load it from the main
menu's CHALLENGES entry and the match carries on from exactly there. Player versus the
computer only.

The file is a header + a small setup block + `SimState_SavePortable(SIM_TIER_SIM)` — the
same 14,994-byte portable match state netplay resyncs with (docs/ROLLBACK.md). 18,752
bytes total, uncompressed, so the m68k build needs no deflater.

Design and rationale: `include/challenge.h`, `src/challenge.c` (both heavily commented).

## 2. State of play

| | |
|---|---|
| **SDL** | **Works.** `C` saves, main menu → CHALLENGES → pick file → match resumes correctly. Verified by screenshot: ITALY 1-0 MALTA at 21 mins, clean pitch. `--challenge=<file>` does the same from the command line, `--challenge-selftest` round-trips the format. |
| **Amiga** | Saves correctly (18,752 bytes, same as SDL). Loads, and the SIMULATION is perfect — players, ball, clock, names, bench all correct. **The display flickers** (§3). |
| **Cross-target** | Parked by the user's decision. The two builds compute different `SimState_LayoutHash` (the identity-list shapes differ: `_teamSpritesTable` has 104 entries on SDL, 100 on m68k, because the four widescreen HUD sprites are in the table only under `SWOS_SDL_BUILD`). The PC correctly REFUSES an Amiga file rather than misreading it. Same-target save/load is unaffected. |

## 3. The open bug

**On the Amiga (AGA), a loaded challenge match flickers**, and everything after it —
the post-match screen, the main menu — is corrupted as a consequence. The user's words:
"challenge match is still flickering, so that is where it starts and ends, everything
after is because of that."

A single screenshot of the match looks CORRECT (pitch, goal, penalty box, the D, crowd
rows, players, scorer name, clock). That is why it was wrongly called fixed: stills cannot
show flicker. See §7 — the verification method was inadequate, not just unlucky.

**Leading hypothesis, NOT yet confirmed:** `SwapBuffersAndCameraViewState` (src/display.c
~line 895) keeps **per-buffer** camera and scroll state — `_cameraXSaved0/1`,
`_oldCameraXSaved0/1`, `_dstXByteOffsetSaved0/1`, `_cameraXMod320` — and swaps them in
and out on every buffer flip. None of it is in the state registry, by design (sim_state.h:
"Render state that describes what is ALREADY DRAWN ... is in neither tier on purpose").
A loaded position teleports the camera, so one buffer can hold the loaded view and the
other the kick-off view, alternating every frame. That matches flicker + "lines in two
places" + misplaced crowd exactly.

`ReplayUpdateCameraAndSwapBuffers` (src/highlight.c) is supposed to fill both buffers and
is what the game itself uses after an instant replay, and it IS called — so either it does
not cover this larger jump, or something after it re-desynchronises the two slots.

**The decisive measurement nobody has run yet:** burst-capture consecutive frames of a
loaded challenge match and diff adjacent pairs. If two views alternate, that shows it
immediately, and the per-frame delta says how far apart the cameras are. Stills and the
harness's per-shot classifier cannot see this.

## 4. What was fixed along the way (all real, all kept)

1. **Tile slots** — `tile_identity`/`tile_slot` in `src/sim_state.c` assumed the SDL chunky
   pitch buffer and a 256-byte stride. m68k is planar with `TILE_SIZE` (128 OCS / 256 AGA),
   so all 252 fan-row slots failed to encode and the Amiga refused to save at all. Now uses
   `AnimatedPatterns_TileBase()/TileShift()`, which `src/animated_patterns.c` already had
   per target (exposed via `include/animated_patterns.h`).
2. **Fade** — a position is saved mid-match where the kick-off fade has finished, so the
   restored `_fadeState = 1` meant nothing ever lit the palette: black screen. Fixed by
   setting `_fadeState = 0; _fadeInitFlag = FADE_SENTINEL` so `GameLoop_Frame` starts its
   own fade-in. **The first attempt — writing the palette directly with `ApplyFadeStep` plus
   a hand-rolled 336px column pan — is what corrupted the Amiga pitch, crowd and menus.**
   Do not reintroduce it.
3. **Menu entry ordering** — `ChallengesMenu` drew the menu before `CommonMenuExit`, so the
   main menu came back showing a MULTIPLAYER button the Amiga does not have plus two blank
   career rows. `CommonMenuExit` now runs first.
4. **Menu background** — the match overwrites the menu's graphics; `PrepareMenuForFadeIn()`
   rebuilds them. Must be done for BOTH buffers, with swaps in pairs (an odd number leaves
   the display on a buffer nothing drew into — symptom: no menu at all).
5. **`MirrorFrontbuffer()`** after each menu fill, as every other full-screen fill does.
   On AGA the frontbuffer is double height and the copper displays from its mirror half.
   *(This did NOT fix the match flicker — the match is still wrong, see §3.)*
6. **Menus' own globals** — a challenge applies a setup that overwrites `_gameType`,
   `_isCareer`, the cup rules; they are snapshotted and restored around the match so the
   main menu is not left believing a competition is under way.

## 5. Traps worth knowing (cost real time today)

- **`EXTRA_CFLAGS` does not invalidate the m68k build stamp.** `obj/.build-flags` is only
  compared against `CC/CCFLAGS/...`, and in practice a `-DFOO` change produced
  "Nothing to be done for 'all'". **A/B builds silently test the OLD binary unless you
  `touch` the sources.** At least one A/B result today was invalid because of this.
- **Any `.CHL`-like extension must be added to `ext_table` in `src/resload.c`**, or the
  Amiga writes the file to the current directory instead of `save/`. The SDL side prefixes
  the save dir in `swos_io.c`; the Amiga does it by extension in resload.
- **FS-UAE does not notice a relink** — always kill it before testing a new build.
- **The pre-match FORMATION screen looks like a corrupt match** if you screenshot too
  early (olive palette, clock at 0). Wait for the match proper.

## 6. The debugger — what works, what does not

This is what the user wants solved first.

### SDL side
`gdb` for the Cygwin build was installed (Cygwin's own, NOT MSYS2's — a Cygwin binary
needs Cygwin gdb). **It attaches and runs, but every backtrace through `cygwin1.dll` is
useless** (`dirname+36258`, broken frame chain, no symbols). Crashes were diagnosed by log
bisection instead. Cygwin's gdb package was installed via
`setup-x86_64.exe -q -P gdb` (the installer is not on the machine by default; it was
downloaded to the scratchpad).

### Amiga side — a session DID work
FS-UAE's gdb stub plus `m68k-amiga-elf-gdb.exe` from the toolchain. A session connected,
**halted the CPU, read live globals and set a source-level breakpoint**:

```
=== CONNECTED ===
=== SYMBOLS LOADED ===
frameCount=0 playGame=0 camX=0 oldCamX=0 pitch=0x0000
Breakpoint 1 at 0x100ffa: file src/challenge.c, line 661
```

Four things have to be right, and each one cost an attempt:

1. **The trigger must match how the program is launched.** `bin/s/startup-sequence` runs
   `SYS:SWOS2`, so `remote_debugger_trigger = SYS:SWOS2`. With the harness's `:SWOS2` it
   never fires and the stub just listens forever.
2. **The connection window is short.** `remote_debugger = N` is that window in seconds;
   the log says `Waiting for connection...` then `timed out after 20s`. Raise it
   (`remote_debugger = 120`).
3. **Connect BEFORE loading symbols.** `SWOS2.elf` carries ~8.6 MB of DWARF and loading it
   takes longer than the window, so `file bin/SWOS2.elf` before `target remote` guarantees
   a timeout. Use `target remote` first, then `symbol-file bin/SWOS2.elf`.
4. **The port is ONE-SHOT.** Every connect/disconnect burns it — *including a
   `Test-NetConnection` probe*, which is how it was burned twice.

A working config and script are in the scratchpad; reproduce with:

```
remote_debugger = 120
remote_debugger_port = 2345
remote_debugger_trigger = SYS:SWOS2
```
```
set tcp connect-timeout 60
target remote 127.0.0.1:2345
symbol-file bin/SWOS2.elf
break <function>
continue
```
Launch FS-UAE, poll its log (`%USERPROFILE%\FS-UAE\Cache\Logs\fs-uae.log.txt`) for
`Waiting for connection`, then start gdb immediately. Reading the log does not burn the port.

### Why it still is not useful
- **The breakpoint originally never fired.** Later investigation found that plain ELF
  symbols used link addresses while the running Amiga executable was relocated. Loading
  actual section bases from `qOffsets` made this breakpoint fire. A standalone
  `Challenge_LoadState` exists in the ELF; inlining was an unverified explanation.
- **`gdb -batch` exits at the end of its script**, so there is no persistent session to
  interrupt and inspect at will; and the one-shot port means reconnecting costs an emulator
  restart. The project's own answer to this is the **AmigaScope harness**
  (`scripts/amiga/amigascope`, now cloned): `amiga_gdb.sh up` + `gdb_proxy.py cmd "..."`
  keeps a daemon so commands can be sent one at a time. It is WSL-oriented
  (`scripts/amiga/amigascope.env` holds the ORIGINAL AUTHOR's `/mnt/c/...` paths) and
  **has not been adapted to this machine**. That is the single highest-value piece of work.
- Everything diagnosed today was diagnosed WITHOUT the debugger: by making the game write
  small binary dumps (§8) and by screenshots.

### Worth knowing
The installed `vscode-amiga-debug` at `C:\Users\A7126\tools\vscode-amiga-debug` **is the
owner's fork** (git remote `benbaker76/vscode-amiga-debug`), so its FS-UAE accepts gdb
write packets. CLAUDE.md warns the two builds are indistinguishable after install; the git
remote settles it.

## 7. How verification was done, and why it was not enough

`scripts/amiga_drive.ps1` + `docs/AMIGA_DRIVER.md` (built by a subagent) drive the Amiga
build unattended: launch, inject keys, screenshot, collect diagnostics, and classify each
shot (black/green/white %) into `PITCH` / `MAINMENU` / `OTHER` with a per-route expectation
and an exit code. FIRE on the Amiga is **Right Ctrl** (joystick port 1; ENTER works in
menus but cannot skip the loader intro).

```powershell
scripts\amiga_drive.ps1 -Route challenge -Check     # cold boot -> CHAL01 -> menu
scripts\amiga_drive.ps1 -Route friendly  -Check
scripts\amiga_drive.ps1 -NoLaunch -KeepShots -Keys "WAIT1500,SHOT,C,WAIT1000,SHOT"
```

**Its blind spot is time.** Every check is a single still, so flicker is invisible to it,
and `-Check` returning 0 was wrongly reported as "done" while the screen was alternating
between two buffers. A frame-to-frame burst comparison (capture N frames back to back,
diff adjacent pairs, report the MAXIMUM delta) is needed before any claim that the display
is correct. An attempt to sample 4 frames 300 ms apart aliased against the flicker and
produced a false "stable" result.

The one genuinely good A/B available: drive a friendly, press `C`, screenshot that frame,
then load that same position and screenshot — the two should match. Done once, and they
did match (same camera, players, ball, crowd; clock advanced, which is correct).

## 8. Diagnostics the game currently writes (all TEMPORARY, remove before committing)

Written into `bin/` (or the save dir) as small big-endian blobs, because the m68k build has
no log:

| file | written by | contents |
|---|---|---|
| `CHALDBG.BIN` | `chal_dbg()` in src/challenge.c | how far the save key got: stage (1 key, 2 gate, 3 hold-off, 4 out of memory, 5 unresolved pointers, 6 write failed, 7 OK), plus playGame / human sides / game state |
| `PITCH.BIN` | `Challenge_DumpPitch()` | `_pitchNumberAndType`, SetPitch call count, a 64-slot sample of the tile map, and the pin trace (rolled / pinned / armed) |
| `RTRIP.BIN` | the self-check in `Challenge_LoadState` | how many registry regions differ after a load, and the first ten indices |
| `LAYOUT.BIN` | `layout_check()` | regions whose field layout does not tile on m68k (currently **0 of 355** — layouts are fine) |
| `CHALCAM.BIN` | `Challenge_DebugCamera()` | the camera numbers around the repaint |

Region indices map to names by parsing `g_simStateRegions` in `src/sim_state.c` in order.
Findings so far: layouts clean; the 19 regions that differ after a load are all explained
(the two VBlank-driven clocks, seven HUD sprites whose `LOCAL` fields are deliberately kept
per-display by `SimState_RestoreLocal`, and the fan rows kept by an isolation experiment).
**The state round-trips correctly on m68k** — the bug is in the display, not the data.

Also temporary: the `CHAL_NO_STATE`, `CHAL_NO_REPAINT`, `CHAL_KEEP_REGIONS` and
`CHAL_AUTOPROBE` compile-time A/B knobs, and the pin trace in `src/highlight.c`.

## 9. Files touched (none committed)

New: `include/challenge.h`, `src/challenge.c`, `scripts/amiga_drive.ps1`,
`docs/AMIGA_DRIVER.md`, this file.

Changed: `src/game_loop.c` (the `C` key; the one-shot load before the loop),
`src/menus.c` (`ChallengesMenu`; `SaveDiskFilingMenu` lost `CommonMenuExit`),
`src/file_io.c` (`SelectFileOfExtension`, the `.CHL` picker label),
`src/sim_state.c` (target-aware tile identity, `swos_io_alloc`, portable string helpers,
diagnostics), `include/sim_state.h`, `src/animated_patterns.c` + header (tile base/shift),
`src/resload.c` (`.CHL` in `ext_table`), `src/highlight.c` (pitch pin), `src/sprite_data.c`
(four widescreen sprites defined on both targets), `include/match_step.h` +
`src/match_step.c` + `src/swos_globals.c` (`g_matchInputLatch` on both targets),
`Makefile.amiga` (`sim_state.c` out of `SDL_ONLY_SRCS`), the menu data
(`bin/data_json/menus/swosMainMenu.json`, `editMenu.json`, `_symbols.json`,
`include/menu_data.h`, `src/menu_data.c`, `src/menu_symbols.c`,
`src/swos_amiga_strings.c`, `src/string_symbols.c`).

## 10. Suggested order for the next session

1. **Make the Amiga debugger usable** — §6. Adapt the AmigaScope harness to this machine so
   there is a persistent gdb session, and break on something that is not inlined.
2. **Measure the flicker** — burst capture, adjacent-frame diffs, maximum delta.
3. With the per-buffer camera slots readable live (`_cameraXSaved0/1`,
   `_oldCameraXSaved0/1`), confirm or kill the hypothesis in §3 before changing code.
4. Only then fix, and re-verify with a temporal check, not a still.
5. Re-verify the **SDL** build afterwards: the fade change is in shared code and has not
   been re-run since.
6. Strip the §8 scaffolding and commit.
