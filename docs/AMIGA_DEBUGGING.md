# Debugging OpenSWOS under FS-UAE on Windows

## Challenge rendering bug found on 2026-10-23

Loading a challenge restored the simulation camera but not the renderer's circular
scroll offsets. This separation is intentional in the simulation registry: offsets,
framebuffers and sprite undraw queues describe what is already drawn, and do not
belong in a portable simulation snapshot.

The load path then called `ReplayUpdateCameraAndSwapBuffers` with the restored
camera as its target. Its initial `SetCameraCoordinates` saw that the camera was
already at that target, so it did not advance the first buffer's scroll offsets
by the jump from kick-off. After swapping, the second buffer still had its old
camera and received the missing delta. The two buffers ended up with different
relationships between world coordinates and physical scroll offsets.

Live GDB readings after the original load:

| Buffer | World camera | Physical scroll |
| --- | --- | --- |
| `0x13f8f8` | `(198,585)` | `(168,16)` |
| `0x10c5f8` | `(198,585)` | `(198,41)` |

These are observations from one launch, not fixed addresses. Both cameras had
converged, so merely checking `_cameraXSaved0/1` would have missed the discrepancy.
The renderer also uses one shared clean-pitch mask for sprite undraw; the mismatched
phases are relevant to that shared background, as well as to displayed bitplanes.

Normal friendly readings at consecutive `GameLoop_Frame` entries:

| Frame | Current buffer | World camera | Old camera | Physical scroll |
| --- | --- | --- | --- | --- |
| 3609 | `0x10c5f8` | `(165,273)` | `(165,273)` | `(165,1)` |
| 3610 | `0x13f8f8` | `(165,273)` | `(165,273)` | `(165,1)` |

The correction in `src/game_loop.c` remembers the render camera before loading,
retains the loaded target, temporarily restores the pre-load integer camera, and
uses the existing replay repaint to move to the target. The loaded fractional
words are preserved. This lets the repaint advance the first buffer's offsets too.
Do not repair this by serializing framebuffers or forcing saved scroll offsets
into an unrelated newly initialized display.

In a live GDB experiment using the newly saved `CHAL02.CHL`, both buffers then had
camera `(165,273)` and scroll `(165,1)` on frames 8004 and 8005. The loaded camera's
fractional Y remained `.75`. The user confirmed that the live corrected match was
working. The source correction compiled and linked for both Amiga and Windows SDL.
Both builds emitted an existing CRLF error from `scripts/gen_build_info.sh`; the
Amiga compiler also emitted existing layout warnings.

The user subsequently cold-booted the rebuilt Amiga executable and confirmed
both CHAL02 and the original CHAL01 work. After the early-exit menu follow-up below,
the user also confirmed that returning mid-match and reopening CHALLENGES works.
SDL runtime verification is still outstanding.
`SWOS.exe --challenge-selftest` passed after putting `C:/cygwin64/bin` first on
the launch PATH: 35 setup scalars, both teams, all six tactics and the 14,994-byte
simulation payload round-tripped. Earlier launches without that PATH exited 1
without diagnostic output. Use the same PATH when launching the Cygwin SDL build.

## What pressing C captures

### Early-exit menu follow-up

After the first fix, the user reported another case: load a challenge, press ESC
mid-match, choose EXIT, then reopen CHALLENGES. The file picker could contain
parts of the main menu. The follow-up change resets both current/saved camera and
physical-scroll slots with `ResetMenuViewState` before a complete menu rebuild.
It also replaces the challenge handler's extra manual fills/swaps with one normal
`PrepareMenuForFadeIn` call: `DrawMenu` already presents and copies the completed
menu to the other buffer. Amiga and SDL build checks are recorded separately;
the user confirmed the rebuilt early-exit/reopen case now works. The installed
Amiga `SAMPLE.CHL` was also checked byte-for-byte against the bundled CHAL02 asset.

`GameLoop_Frame` reads the key and dispatches `MainKeysCheck` before that frame's
camera movement and gameplay update. `C` calls `Challenge_SaveNow`, which checks
the save gate and hold-off, encodes `SIM_TIER_SIM`, adds match setup and metadata,
and writes the next available `CHALnn.CHL`. It captures a simulation position, not
the physical contents or scroll state of either display buffer. `_frameCount` has
already been incremented earlier in this frame; input/timer processing has begun.

The save path was exercised under GDB: `Challenge_SaveNow` returned 0 and wrote
18,752 bytes. At save, the debugger reported frame 8004 and camera raw words
`0x00a50000,0x0111c000` (165,273.75). CHAL02's header and payload frame counts and
payload camera matched those readings exactly; loading it restored the same values.
This verifies those fields, not every region or deterministic continuation.

There is a separate checksum diagnostic weakness: `Challenge_SaveNow` serializes
first, then computes the header hash from live state, with a separate
`SimState_Hash` call for each half. VBlank-driven tick regions can change in between.
Likewise, hashing live state after load can disagree with the file as clocks advance.
A checksum mismatch alone therefore does not establish a broken capture. A future
integrity check should hash the serialized payload using the same LOCAL-field
normalization as `SimState_Hash`, and compare regions rather than stopping at the
first timer difference. This checksum issue has not been changed in this fix.

## Launch and attach

Use the native Windows FS-UAE and m68k GDB from
`%USERPROFILE%\tools\vscode-amiga-debug\bin\win32`. Start a fresh emulator for each
new connection: this stub's connection is one-shot. Do not use `Test-NetConnection`
or another TCP client to test readiness; a probe consumes the connection.

`scripts/run_amiga_fsuae.ps1` normally writes the base config to
`%TEMP%\swos-a1200.fs-uae`. For debugging, make a separate config with those same
ROM, hard-drive, memory and window settings, plus:

```ini
remote_debugger = 120
remote_debugger_port = 2345
remote_debugger_trigger = SYS:SWOS2
```

`bin/s/startup-sequence` runs `SYS:SWOS2`, so the trigger must match that spelling.
The old AmigaScope config's `:SWOS2` and original author's WSL paths are not usable
as-is on this machine. The normal PowerShell launcher does not enable debugging.

Launch FS-UAE with the debug config, then start GDB promptly. Reading
`%USERPROFILE%\FS-UAE\Cache\Logs\fs-uae.log.txt` for `Waiting for connection` is
safe. Keep one interactive GDB process alive; do not use `-batch` for ongoing work.

```powershell
$gdbPath = "$env:USERPROFILE\tools\vscode-amiga-debug\bin\win32\opt\bin\m68k-amiga-elf-gdb.exe"
& $gdbPath -q -nx
```

In GDB, connect **before** loading the ELF's debug symbols:

```gdb
set pagination off
set confirm off
set tcp connect-timeout 60
set remotetimeout 60
target remote 127.0.0.1:2345
maintenance packet qOffsets
```

The large ELF can take long enough to load that loading it before attaching burns
the connection window. A TCP connection alone also does not establish that the
trigger fired and CPU/register access is healthy.

## Load symbols at their actual addresses

AmigaDOS relocates the executable's hunk sections. Plain `symbol-file` loads ELF
link addresses, which can produce plausible but wrong globals, code resolving as
data, and breakpoints that never fire. Successful symbol loading is not verification
that the addresses are right.

This FS-UAE fork's custom `qOffsets` reply reports absolute section bases in order:
text, data, chip BSS, BSS. One session returned:

```text
00217188;002d2e90;00005ce8;002f1778
```

For that reply, use this single-line command:

```gdb
add-symbol-file C:/Users/A7126/orca/OpenSWOS/bin/SWOS2.elf 0x217188 -s .data 0x2d2e90 -s BSSInChip.MEMF_CHIP 0x5ce8 -s .bss 0x2f1778
```

Query and substitute the addresses for every launch. If plain symbols were already
loaded, delete old breakpoints and run `symbol-file` without an argument to clear
them before `add-symbol-file`. Use the ELF from the exact executable FS-UAE booted.
Rebuilding does not update the running emulator. Restart it to test a rebuilt binary,
then reconnect and query section addresses again.

Verify the mapping before trusting reads or breakpoints:

```gdb
info registers pc sp
x/8i $pc
info symbol $pc
break Challenge_LoadState
```

Before relocation was applied, live `WaitForVSync` instructions resolved into
`_teamFileBuffer`. Afterward, the code resolved correctly and the challenge-load
breakpoint fired. The earlier assertion that the missed breakpoint was caused by
inlining was not established: the ELF contains a standalone `Challenge_LoadState`.
Optimization can still affect local-variable visibility and line breakpoints.

Some file-static symbols (including `s_info`) still read as garbage in this session,
despite working global camera symbols and source breakpoints. Do not infer corrupt
game state from those reads until their addresses have been checked against the
instructions that actually access them. This GDB build has no Python scripting.

## Record comparable frame data

Create the output directory first, then log the session:

```gdb
set logging file C:/Users/A7126/orca/OpenSWOS/build/amiga_debug/render-session.log
set logging overwrite on
set logging enabled on
```

`continue` resumes the game. Ctrl+C in the interactive GDB terminal interrupts it;
this stub reports the interruption as SIGTRAP. Read state while stopped. Keep that
same session attached across input, breakpoints and inspection.

For baseline comparison, stop at `GameLoop_Frame` entry twice in succession, so
both alternating drawing buffers are sampled at the same point in the frame.
For example, repeat these commands after each stop:

```gdb
printf "FRAME=%d cam=%d,%d raw=%lx,%lx old=%d,%d scroll=%d,%d front=%lx alt=%lx\n",_frameCount,_cameraX.s.i,_cameraY.s.i,_cameraX.raw,_cameraY.raw,_oldCameraX,_oldCameraY,_cameraXMod320,_cameraYMod320,_frontbufferBasePtr,_frontbufferBaseAltPtr
printf "SLOT0 cam=%d,%d old=%d,%d scroll=%d,%d\n",_cameraXSaved0,_cameraYSaved0,_oldCameraXSaved0,_oldCameraYSaved0,_dstXByteOffsetSaved0,_dstYStartLinesSaved0
printf "SLOT1 cam=%d,%d old=%d,%d scroll=%d,%d\n",_cameraXSaved1,_cameraYSaved1,_oldCameraXSaved1,_oldCameraYSaved1,_dstXByteOffsetSaved1,_dstYStartLinesSaved1
```

Also record `_cameraCoordinatesValid`, `_usePlaneOffsetFlag`, `_useAltBaseFlag`,
`_playGame`, `_fadeState`, and copper bitplane pointers/fine scroll when relevant.
Take copper offsets from `include/display.h` for the actual target/configuration.
Remember the copper is programmed for the presented buffer before the drawing-buffer
swap; comparing its pointers to the post-swap frontbuffer without that context misleads.

For load diagnosis, record state at these points:

1. `Challenge_SaveNow` entry and successful return: frame, raw camera, file written.
2. `Challenge_LoadState` entry and return: old render view versus restored world view.
3. `ReplayUpdateCameraAndSwapBuffers` entry and return: target and repaint result.
4. Two consecutive `GameLoop_Frame` entries: both buffers and their scroll phases.
5. Menu after exit: both buffer states and copper pointers, compared with a cold menu.

Saved slots describe the last time a buffer was swapped out. The active buffer's
current globals can be newer than its saved slot. A camera difference during normal
motion is therefore expected. Compare each buffer's world-to-scroll relationship,
not just equality of raw slot values at an arbitrary breakpoint. Scroll X wraps at
384 on AGA (352 on OCS); Y wraps at 272. Establish the normal relationship with a
fresh friendly before declaring an offset difference corrupt.

Disable diagnostic breakpoints before leaving the game running. Prefer manual menu
navigation when automation has not reached a verified screen. Read the debugger
state to confirm that loading really occurred. Screenshots can support visual checks,
but a single still cannot prove the absence of alternating-buffer corruption.

Session evidence is retained under `build/amiga_debug`, particularly
`friendly-gdb.log`, `INVESTIGATION.md`, and `CHAL02-capture.CHL`.

## Bundled challenge

There is one built-in challenge per target, shown as `SAMPLE.CHL`. Opening
CHALLENGES installs it into the normal save folder if that name is absent;
existing saves are not overwritten. Deleting the sample lets it be installed
again on the next visit. The source files are:

| Target | Tracked runtime asset | Original capture |
| --- | --- | --- |
| Amiga | `bin/data/CHALAMI.DAT` | `bin/save/CHAL02.CHL`, frame 8004, Liechtenstein vs Italy |
| SDL | `bin/data/CHALSDL.DAT` | Windows user's `Documents/SWOS/save/CHAL02.CHL`, frame 3017, Italy vs Malta |

The `.DAT` files are unchanged CHL payloads. The extension lets the Amiga data
loader read them from the bundled data directory rather than redirecting them to
save/. Each executable selects only its own asset and checks layout/rules before
installing it. Single-target staging excludes the other target's asset. The combined
portable archive contains both because it includes both executables; each game
still exposes only its own sample. Personal CHAL01/CHAL02 files are not packaged.
