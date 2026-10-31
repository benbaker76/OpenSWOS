# WASM / Emscripten target — plan

Goal: build SWOS to WebAssembly so it runs in a browser from the existing SDL2
codebase, with as little divergence from the native SDL build as possible.

> **Status (2026-09-30): LIVE.** The plan below landed as designed (ASYNCIFY,
> `emscripten_sleep` in `Platform_VBlank`, IDBFS saves) in `dc33557`
> (2026-06-28). `make WASM=1` (with `<emsdk>/emsdk_env.sh` sourced; emsdk lives at
> `/mnt/c/Projects/Linux/emsdk` here, `EMSDK_DIR` in `scripts/build_dist.sh`)
> writes `dist/web/SWOS.{html,js,wasm,data}` — not `bin/`. Two things this plan
> never covered:
> - **It plays online by default** (`NETPLAY=1` is the WASM default,
>   `make WASM=1 NETPLAY=0` builds one without): a browser has no UDP, so each
>   datagram goes as a WebSocket message to the matchmaker's `--websocket` gate,
>   `wss://` since us.swos.eu got its certificate (2026-09-24); a match with a
>   browser in it is always relayed. See docs/NETPLAY.md §8 and
>   `scripts/wasm_netplay_test.sh`.
> - **A second web build, the recordings player:** `make WASM=1 PLAYER=1` →
>   `dist/playback` (page `wasm/player.html`, objects in `build/emscripten/wasm-player`,
>   no netplay), served at https://us.swos.eu/playback/ — docs/PLAYBACK.md.
>
> The sections below are the design record and are kept as written; where a
> detail changed, it is noted inline.

---

## 0. TL;DR — the decision that shapes everything

The browser has no place for a blocking `while(1)` — JS owns the event loop, so a
C program must return to it regularly or the tab freezes. Two ways to satisfy that:

1. **`emscripten_set_main_loop(cb, fps, …)`** — restructure the game so one
   "do one frame, return" callback drives everything. Faithful to "the browser way",
   but it requires turning **every** blocking loop into a resumable state machine.
2. **ASYNCIFY** — Emscripten rewrites the wasm so a normal blocking loop can
   *yield* at designated sleep points (`emscripten_sleep`) and resume later. The
   C keeps its blocking structure; the browser still gets control each frame.

**Recommendation: ASYNCIFY.** The investigation (below) found the game already
funnels every blocking loop through **one** frame-sync choke-point,
`Platform_VBlank()`. So ASYNCIFY needs us to yield in *one* function; the
`set_main_loop` path would need us to rewrite ~12 nested loops (the match loop, the
replay/highlights loops, the menu loop, the fades, the loader intro, the boot asset
polls) into callbacks — high effort, high regression risk, and a permanent
divergence from the native build. ASYNCIFY keeps the native and web builds running
the *same* control flow.

Trade-offs we accept with ASYNCIFY: a moderate code-size increase and a small
per-yield rewind/unwind cost (negligible at 50 Hz). We can shrink later with
`ASYNCIFY_ONLY`/`ASYNCIFY_REMOVE` lists once it works.

---

## 1. Current architecture (what we're porting) — verified

Entry: `main()` (main.c) → `startShim()` → `mainC()` (SWOS.c) → `FrontEndMainLoop()`.

```c
/* frontend.c — the single top-level loop */
void FrontEndMainLoop(void) {
    ReadTimerDelta();
    FrontEndPreLoopSetup();
    do { MenuProc(); } while (_quitToDos == 0);
}
```

Underneath, there are ~12 nested blocking loops, but **they all yield through the
same place**:

| Loop | File | Yields via |
|---|---|---|
| boot asset polls (`delay_ticks`) | SWOS.c | `WaitForVSync()` |
| loader intro fire-wait + music fades | loader_intro.c | `WaitForVSync()` |
| `FadeIn` / `FadeOutToBlack` | fade.c | `RunInt3HandlerThrottled()` + `SDL_Delay(2)` |
| in-match `GameLoop` | game_loop.c | `waitVBlankSync()` → `Platform_VBlank()` |
| pause / stats / replay / highlights | game_loop.c | `pauseSpinWaitOneTick()` → `Platform_VBlank()` |
| menu loop `MenuProc` | (asm) → drawing | `WaitForVSync()` |

The chokepoint:

```c
/* game_utils.c — every loop calls this (or waitVBlankSync/pauseSpinWaitOneTick,
 * which also call Platform_VBlank). */
void WaitForVSync(void) {
#ifdef SWOS_SDL_BUILD
    SwapBuffersAndCameraViewState();
    SetCameraCoordinates(cx, cy);
    Platform_VBlank();          /* <-- THE choke-point */
#else
    _waitingInterrupt = 1; while (_waitingInterrupt) {}   /* Amiga ISR busy-wait */
#endif
}
```

```c
/* platform_sdl.c — does ISR pump + render + 50 Hz pacing */
int Platform_VBlank(void) {
    RunInt3HandlerThrottled();      /* advance the VBlank/timer ISR */
    Platform_RenderFrame();         /* chunky_to_rgba -> texture -> RenderPresent */
    /* 50 Hz pacing: SDL_Delay(...) then spin ~2 ms  <-- this is what must change */
    return _quitToDos;
}
```

Threads (native SDL build) — **all removable on WASM**:
- `SDL_AddTimer` `tick_timer_cb` (20 ms): pumps the ISR + audio + a fallback
  render if the main thread stalls. Redundant on WASM — `Platform_VBlank` does the
  same work every frame.
- `audio_pump_thread` (`SDL_CreateThread`, 5 ms): keeps the SDL audio queue ~80 ms
  full. Redundant on WASM — `AudioSDL_Tick()` is already called from
  `Platform_RenderFrame`.
- Conclusion: **WASM runs single-threaded with no feature loss.** (Avoids the
  pthreads/SharedArrayBuffer/COOP+COEP hosting burden entirely — a big win.)

Audio: already **push mode** (`SDL_QueueAudio`, not the callback) — chosen because
WSL2/Windows callback scheduling is unreliable. This is exactly what WASM wants.

---

## 2. The WASM changes (small, all `#ifdef __EMSCRIPTEN__`)

### 2a. Platform layer — yield instead of busy-pace
`Platform_VBlank()` (platform_sdl.c): on WASM, after `Platform_RenderFrame()`,
replace the `SDL_Delay()+spin` pacing with `emscripten_sleep(frame_ms)`. ASYNCIFY
turns that into "render this frame, return to the browser, resume in ~20 ms". One
edit covers every blocking loop in the game.

`fade.c` `FADE_SPIN_BODY()`: replace `SDL_Delay(2)` with `emscripten_sleep(2)` on
WASM (so the fade spin also yields).

Everything else (`WaitForVSync`, `waitVBlankSync`, `pauseSpinWaitOneTick`, the menu
loop) is unchanged — they already route through `Platform_VBlank`.

### 2b. Threading — compile the workers out on WASM
Gate `SDL_AddTimer(tick_timer_cb)` and `SDL_CreateThread(audio_pump_thread)` behind
`#ifndef __EMSCRIPTEN__`. The work they did is already done synchronously in
`Platform_VBlank` → `Platform_RenderFrame` (ISR pump + `AudioSDL_Tick`). No mutexes
needed single-threaded (they become no-ops or stay; SDL mutex calls are cheap).

### 2c. Audio — push + user-gesture unlock
`SDL_QueueAudio` works as-is. Browsers start the audio context **suspended** until a
user gesture; SDL2-emscripten resumes it on the first input event. We just need to
make sure audio isn't required *before* the first click (the menu music will start
muted and kick in after the first key/click — acceptable, or we show a "click to
start" overlay).

### 2d. File I/O — preload data, persist saves
- **Game data** (`bin/data`, `bin/grafs`, `bin/grafs_2020`, `bin/sound`,
  `bin/data_json`): bundle into the virtual FS with `--preload-file` (read-only
  MEMFS, downloaded as a `.data` package on load). Open question: total size →
  initial download (see §4). *(As built: `data grafs grafs_aga grafs_jag sound
  data_json` — no `grafs_2020`; `SWOS.data` is ~23 MB. See the WASM `LDFLAGS`
  block in the `Makefile`.)*
- **The chdir-to-own-dir startup**: on WASM there's no binary dir. Either skip the
  chdir under `__EMSCRIPTEN__` and preload data at `/`, or chdir to `/`. Pick one.
- **Saves** (`bin/save/*.CAR/.HIL/...`): mount **IDBFS** at `/save` (persists in
  IndexedDB across page loads) and call `FS.syncfs()` after each save. The save/load
  paths are plain `fopen/fwrite` and work unchanged on the virtual FS. Saves are
  already JSON (portable) — good.
- Build `SWOS_SAVE_FORMAT_JSON_VERIFY=0` for WASM (the verify path reloads-on-save;
  unnecessary overhead in the browser).

**Importing saves from the local disk (2026-08-19).** `wasm/shell.html` has an
import button (the ⬆ icon under the gear / fullscreen toggles). It reads picked
files, writes them into `/save` with `FS.writeFile`, then calls
`FS.syncfs(false, …)` so they persist in IndexedDB — after which they show up in
the game's own LOAD dialog. Two constraints it has to respect:

- **Names must be UPPERCASE.** The in-game selector matches the extension with a
  4-byte compare against `'.CAR'`/`'.TAC'`/… (`file_io.c`), and `fs_listdir`
  (`resload.c`) returns directory names verbatim — so `milan.car` would be
  listed by the FS but silently rejected by the selector. The importer
  uppercases and sanitises to 8.3.
- **The selector caps at 45 files** (`SelectFilesInputSaveFilenameOnSelect`).

**The write-side hook landed (2026-09-12).** `rob_save` / `rob_delete`
(`resload.c`) now call `FS.syncfs(false, …)` via `EM_ASM` after a successful
write, so a career saved IN THE BROWSER survives a reload. That single pair of
functions is the whole SDL-side file path — every save type (`.CAR`, `.HIL`,
`.TAC`, `.EDT`, `.CFG`) funnels through it — so one hook covers them all, and
deleting a file sticks too.

Asynchronous on purpose: the game does not block, the browser flushes while play
continues, and the next load picks it up through the existing `syncfs(true)` in
`wasm/pre.js`. Saves are user-initiated and rare, so one flush per write needs no
debouncing.

Before this, only the importer called `syncfs(false)` — so an IMPORTED save
persisted while a save made in the game did not, which is a confusing pair of
behaviours to hit in that order.

### 2e. Rendering — SDL2 as-is
The 8bpp-chunky → RGBA → `SDL_Texture` → `SDL_RenderCopy/Present` path is standard
SDL2 and runs on Emscripten's SDL2 (WebGL-backed). The `<canvas>` is the window.
Need: pick an initial canvas size / scaling (the game renders ~336×... at 4:3) and
decide fullscreen/resize behaviour. `-sUSE_SDL=2`.

### 2f. Timing
`SDL_GetTicks` / `SDL_GetPerformanceCounter` work in Emscripten. The only thing we
remove is the busy-spin pacing in `Platform_VBlank` (→ `emscripten_sleep`). TURBO
mode is harness-only and irrelevant to WASM.

### 2g. Platform define
Add to `include/swos_platform.h` (inside the `SWOS_SDL_BUILD` block):
```c
#ifdef __EMSCRIPTEN__
#define SWOS_WASM_BUILD 1
#endif
```
so WASM-specific bits read `#ifdef SWOS_WASM_BUILD` rather than raw `__EMSCRIPTEN__`.

### 2h. Makefile — a new `WASM`/`EMSCRIPTEN` branch
Mirror the existing per-target branches:
- `CC := emcc`, `TARGET_NAME := SWOS.html` (emits `.html`+`.js`+`.wasm`+`.data`),
  `BUILD_OS := emscripten`, `BUILD_ARCH := wasm`.
- SDL via `-sUSE_SDL=2` (NOT pkg-config) — needs a WASM branch around
  `SDL2_CFLAGS`/`SDL2_LIBS`.
- Link flags: `-sASYNCIFY`, `-sALLOW_MEMORY_GROWTH=1`, `-sINITIAL_MEMORY=…`,
  `--preload-file bin/data@/data` (+ grafs/sound/data_json), `-lidbfs.js`,
  `-sEXPORTED_RUNTIME_METHODS=[ccall,FS]`, an `--shell-file` for the page, and
  `-sASYNCIFY_STACK_SIZE=…` if the default is too small.
- Keep `make clean` per-target object dir: `build/emscripten/wasm`.
- The JSON→C regen rules already run via `$(PYTHON)` and are toolchain-independent —
  no change.

---

## 3. Phased plan (each phase independently testable)

0. **Prerequisite — install Emscripten.** *(Done: emsdk is at
   `/mnt/c/Projects/Linux/emsdk`.)* `emcc` was not on this machine when this was written.
   Install the SDK (`git clone https://github.com/emscripten-core/emsdk && cd emsdk
   && ./emsdk install latest && ./emsdk activate latest && source ./emsdk_env.sh`).
   The build will pick it up once `emcc` is on PATH. Run from WSL (Linux emsdk), the
   same environment that builds the Linux/Amiga targets.
1. **Build green.** Add the Makefile WASM branch + `SWOS_WASM_BUILD`. Gate out the
   two worker threads. Get `emcc` to compile + link to `SWOS.html` (may not run
   yet). Catch any non-portable code (inline asm `.equ` aliases — verify they
   compile under clang/wasm; this is the biggest unknown — see §4).
2. **Loop runs.** `-sASYNCIFY` + `emscripten_sleep` in `Platform_VBlank` (+ fade).
   Boot to the main menu in the browser; confirm it doesn't freeze the tab.
3. **Data loads.** `--preload-file` the data dirs; fix the chdir; menus + graphics
   render.
4. **Input.** SDL keyboard/gamepad events already flow through the SDL event pump;
   verify the menu + a match are playable. (Touch/on-screen controls = later.)
5. **Audio.** Confirm push audio + the user-gesture unlock; add a "click to start"
   if needed.
6. **Saves.** IDBFS at `/save` + `FS.syncfs`; a career round-trips across reloads.
7. **Polish.** Canvas sizing/fullscreen, the shell HTML, memory tuning, code-size
   (ASYNCIFY allow-list), load-time (data compression / lazy packages).

---

## 4. Open questions / risks

- **RESOLVED by probe (2026-06-28, emcc 6.0.1):** the WASM blocker is **asm-defined
  STORAGE**, not the aliases:
  - `__asm__(".section .bss … .globl x … x: … .skip N")` → **FAILS**:
    `fatal error: data symbols must live in a data section`. This is the pervasive
    BSS-stub / merged-backing pattern. **Surface: 28 files, 341 `.skip` sites.**
  - `__asm__(".globl a\n.equ a, base")` and `.equ a, base + offset` → **COMPILE
    FINE** on wasm (with a C-array backing). So the `.equ` overlay *aliases* are NOT
    a blocker.
  ⇒ The WASM prerequisite is exactly: **convert the asm `.skip`/`.section` storage
  to plain C.** The globals are already *separated/named* (the untangle's hard part
  is mostly done) — they're just still *asm-defined*. Making them pure C is the
  remaining gap (and it also kills the existing Mach-O `.equ`-dropped and Cygwin/COFF
  `.pushsection` fragilities — a cross-platform win, not just WASM).
  - Conversion shapes: a single `.globl x; x: .skip N` → `char x[N];` (trivial,
    all-target). A struct-with-member-labels block (e.g. `career_team_scalars.c`:
    `CareerTeamScalars` + `_careerNumPlayers`/… labels) → define the C struct and
    repoint the member call sites to struct fields (the real untangle). Avoid the
    "C storage + keep `.equ` member aliases" shortcut: it works on wasm/ELF but
    Mach-O silently drops `.equ a,sym+off` (swos_globals.c:332). Pure C is the only
    all-target answer.
- **Still to probe once the `.skip` conversion is underway:** compile a real TU with
  `emcc` to surface any *other* non-portable constructs beyond `.skip` (other inline
  asm, etc.). The probe above only isolated the asm-data patterns.
- **Data package size** → initial download. How big are `grafs*` + `sound` +
  `data`? If tens of MB, consider gzip (`-sGZIP`/server), lazy `file_packager`
  sub-packages, or fetching on demand.
- **ASYNCIFY stack size + code size**: defaults may need bumping; measure.
- **64-bit-ness**: wasm32 is ILP32 (32-bit pointers, like armhf) — the
  `size_check[]` asserts should be happy (they pass on armhf). wasm64 is not worth
  it. Good: ILP32 sidesteps the 64-bit struct-size questions.
- **`SDL_Delay` elsewhere**: audit for any other bare `SDL_Delay`/busy-wait outside
  `Platform_VBlank`/fade that would stall the tab (the map found only those two;
  re-grep before Phase 2).
- **Persistence UX**: IDBFS is per-origin and can be evicted; fine for a game, but
  note it (and maybe an export/import-save button later).

---

## 4b. ✅ DONE (2026-06-28) — asm `.skip`/`.section` storage → pure C

**COMPLETE.** All asm-defined `.skip` BSS storage is removed from the wasm code
path (25 files). `emcc` (6.0.1) now compiles the converted TUs at exit 0
(`swos_globals.c` — the hardest — included). Validated:
- x86_64 links; m68k `-fsyntax-only` clean on every file; **full Amiga m68k build
  links** (`SWOS2.elf`/`Elf2Hunk`, DBG=1).
- **Byte sizes/offsets verified on BOTH 32-bit (m68k) and 64-bit (x86_64)** via
  objdump/nm vs the original `.skip` (`_selectedTeams`=68400, the 34 comp-copies
  base/+8, `_careerTeam`=1292, every `.equ` alias offset exact).
- **SDL runtime:** menu enum (all 13 entries), `MILAN.CAR` load rc=0,
  `selectedTeams[0]`="AC MILAN" at `+5`, career globals sane, season 3→4 advance.
  A pre-conversion-build diff run sticks at the *identical* `DIYCupInit` point
  (pre-existing harness in-season-stepper limitation, NOT the conversion).
- **`SWOS_SAVE_FORMAT_JSON_VERIFY=1` links** (save descriptors resolve).

Three conversion shapes were used (see the per-file notes in the converted .c
files + `project_wasm_target` memory):
1. **Simple split** (most files): typed base unused → each member becomes an
   independent C global, same name/type/size, zero call-site changes.
2. **Decouple + repoint** (few uses): `career_key_cache.c` `_teamSelectionKeyCacheEnd`
   (address-as-bound → `cache + sizeof`); `selected_teams.c` 68400-B union →
   `TeamFileEx _selectedTeams[100]` + pure-C offset MACROS in `game_types.h`.
3. **Per-target storage** (`swos_globals.c` menu/HIL/grids/numberOfFiles —
   contiguous + too-many-uses to repoint): `#if defined(__APPLE__)` keeps
   real-label `.skip` (Mach-O drops `.equ a,sym+off`); `#else` (wasm/ELF/COFF) =
   pure-C array carrier + `.equ alias, carrier+off`. The remaining 19 `.skip` in
   the tree are ALL inside these `__APPLE__` branches → invisible to wasm.
   (emcc also requires asm data symbols to have an explicit `.size` — C-array
   carriers get it automatically; raw asm `.skip` symbols don't.)

**NEXT = the actual WASM build (§2–3): Makefile emcc branch + ASYNCIFY.**
*(Done the same day — `dc33557`; see the status note at the top.)*

The historical plan that produced this is kept below for reference.

---

## 4b-plan (historical) — asm `.skip`/`.section` storage → pure C

This is the WASM prerequisite (confirmed by the §4 probe) AND the finish of the
roadmap untangle. Do this *before* the Makefile/ASYNCIFY work. It is independent of
Emscripten — every step is verifiable on the existing x86_64 + Amiga builds, and
emcc only confirms at the end.

**Surface: 28 files, 341 `.skip` sites** (`grep -rcE '\.skip ' src/*.c`). Counts:

```
career_comp_copies.c:69  career_settings.c:25     career_counters.c:21
career_transfer_market.c:20  career_selector_state.c:17  career_match_state.c:16
career_trophy_flags.c:16  career_status_flags.c:13  career_finance_tail.c:13
career_finances.c:12      career_setup.c:11        selected_teams.c:9
career_key_cache.c:8      career_season_record.c:7 career_manager_identity.c:6
career_team_buffers.c:6   career_index_tables.c:5  career_unavailable_players.c:5
career_season_state.c:5   career_diy_league_state.c:4  career_team_scalars.c:4
career_season_info.c:3    save_format_json.c:2     career_foreign_market.c:1
file_io.c:1               swos_competition_slots.c:1
swos_globals.c:22         career_board_confidence.c:19
```

**Two conversion shapes:**
1. **Single-global storage** — `__asm__(".globl x\n x:\n .skip N\n")` → `char x[N]
   __attribute__((used, aligned(...)));` (or the typed equivalent). All-target safe,
   no call-site changes. The bulk; do these first for volume + to prove the build
   stays green everywhere.
2. **Struct-with-member-labels** — e.g. `career_team_scalars.c`: a typed struct
   (`CareerTeamScalars`) whose storage is asm-emitted with member labels
   (`_careerNumPlayers` @0, `_selTeamSelectedPlayerPosition` @2, …). Convert to a C
   definition (`CareerTeamScalars _careerTeamScalars;`) and **repoint the member
   call sites to struct fields** (incl. the save FX/FA `&_member` descriptors). This
   is the real untangle work — same care as the prior overlay retirements (watch for
   range-bound `&sym` reads). Known careful ones: `career_team_scalars.c`,
   `swos_globals.c` (the `_currentMenu`/`_menuEntries` block @ ~349, the goalie/
   stadium/offerOrdinals adjacency groups), `career_comp_copies.c` (the `*Copy`
   regions + the reconstruct descriptors).
   - **Do NOT** take the "keep C storage + `.equ` member aliases" shortcut: it
     compiles on wasm/ELF but Mach-O silently drops `.equ a, sym+off`
     (swos_globals.c:332). Pure C struct fields are the only all-target answer.

**Per-file gotchas:**
- These files are **synced to the Amiga tree** — m68k supports both asm and C, but
  verify the Amiga build after each (user commits the Amiga side; don't auto-commit).
  *(Pre-merge: since 2026-08-12 there is one tree and no sync — build both targets.)*
- Preserve **alignment** (`.p2align N` → `aligned(1<<N)`) and **zero-init vs
  initialized** (`.bss`/`.skip` = zero-init C global; initialized data stays a C
  initializer).
- The save format must be unchanged: descriptors that take `&_member` must still
  resolve to the same bytes (now via struct fields). Run `SWOS_SAVE_FORMAT_JSON_VERIFY=1`
  spot-checks where a converted global participates in a save.
- Some `.skip` are *holes* (`.skip 6` with no label = padding for a retired overlay
  run) — keep the hole as a struct pad / array gap, or drop it only if nothing reads
  across it.

**Verify recipe (per file):**
1. `make X86_64=1 SWOS_SAVE_FORMAT_JSON_VERIFY=0` (green).
2. Amiga: `source` the m68k toolchain (linux bin), `make -C /mnt/c/Projects/AI/SWOS/OpenSWOS` (green).
3. Periodically: a real `emcc` compile of the converted TU (see below) — should stop
   erroring on `.skip` once the file is pure C.
4. After a batch: the MILAN harness season (no-regression) once the daemon's running.

**emcc / probe recipe:**
- `source "/mnt/c/Projects/Linux/emsdk/emsdk_env.sh"` (emcc 6.0.1).
- Re-probe a pattern: the 3 probe files are in the session scratchpad
  (`probe_skip.c` FAILS, `probe_equ.c` / `probe_equ_off.c` PASS).
- `emcc -c src/<file>.c -o /tmp/x.o -DCOMPETITION_DATA_C=1 … -include include/swos_platform.h
  -Isrc -Iinclude` (the build's `-D` data flags + `-include`; SDL not needed to
  compile a data TU). Will fail on `.skip` until converted.

**Order:** single-global files first (volume, low risk), then the struct-aliased
ones. Track the 28 files as a checklist.

## Phones: on-screen controls and the keyboard

On a touch screen the page (`wasm/shell.html`) draws a stick, FIRE, PAUSE and ESC
(`InputSDL_TouchControls` / `InputSDL_TouchKey`, src/input_sdl.c). **Text fields** -- a file
name, a team's name, the online user name: whatever goes through `InputText`
(src/input_text.c) -- put up the phone's own keyboard (2026-10-30, players' request):
`InputText` calls `Module.swosTextInput(1)` / `(0)` around the field, and the page focuses a
hidden `<input id="textKbd">` meanwhile (a phone shows its keyboard only for a focused text
field). What is typed into it goes to the game a character at a time (`InputSDL_WebChar`:
printable ASCII, 8 BACKSPACE, 13 ENTER, 27 ESC, into the key FIFO as a key press), and the
field is emptied again; its key events stop there, so a tablet's own keyboard does not type
twice. Only on a touch screen: a computer's keyboard reaches the game as before. iOS Safari
opens a keyboard only from a tap, so any tap while a field is open and the keyboard is down
gives it the focus. Checked in headless Chrome as a phone: "Ab1", BACKSPACE, ENTER reached the
game as 65 98 49 8 13, once each; as a desktop the field never takes the focus.

## 5. Why not `set_main_loop` (kept for the record)

It's the "correct" browser pattern, but it assumes one shallow frame function. SWOS
has deep, nested, `goto`-driven blocking loops (the match loop alone is a
`goto`-based state machine across ~180 lines). Converting all of them to re-entrant
callbacks would be a large, risky rewrite that permanently forks the web build from
the native/Amiga builds. ASYNCIFY gets the same result (browser gets control each
frame) while every build runs identical control flow. If ASYNCIFY's size/perf ever
becomes a problem, we revisit — but it won't for a 50 Hz 2D game.
