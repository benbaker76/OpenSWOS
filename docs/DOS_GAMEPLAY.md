# DOS gameplay (70 FPS) support

> **Status (2026-11-02): implemented, with a third style, XBOX (section 9).**
> **(2026-09-30): DOS implemented.** Written as the research + porting plan;
> the plan has landed. `src/dos_gameplay.c` (`ApplyGameStyle`,
> `DosGameplay_TargetFps`) switches the §3 constants, the formations
> (`TacticsData_SelectRelease`, 71e174a) and the §8.3 movement scale
> (`_gMoveDeltaScaleNum/Den` = 41/64); `platform_sdl.c` paces at
> `Platform_ActivePacingFps()` (70 Hz in DOS mode). It is selected by
> `[gameplay] style = amiga | dos` in SWOS.ini (or `--dos-gameplay=on|off`), and
> since 27c56b2 the style sets the **view** too (the DOS 320x200 view with DOS
> gameplay; `[gameplay] view` is gone, `--view=amiga|dos` still pins one). Online,
> a match plays the HOST's style (`GameplayStyle_Borrow`). The "plan" wording
> below is kept as the record of how.

The Amiga SWOS runs its match simulation at **50 FPS** (PAL VBlank); the DOS
version runs at **70 FPS**. SWOS is **frame-locked** — exactly one logic step per
rendered frame, no `deltaTime` multiplier — so to make the two versions *feel* the
same despite different frame rates, a set of physics/timer constants are
**pre-scaled per frame rate**. This doc captures which constants change and how,
so the SDL port can offer a DOS-gameplay mode (the `_gDosGameplay` flag /
`[gameplay] style = dos`; originally the reserved `[dos] gameplay=true`, which the
settings reader no longer knows).

Source of truth: **swos-2020** (`/mnt/c/Projects/GitHub/swos-2020`), the most
up-to-date reference, which patches every FPS-dependent constant at startup in
`src/options.cpp::setGameStyleVariables()`. (An older spreadsheet exists at
`D:\Projects\SWOS\DOS\2_code comparison table of 70-50-60FPS
version-r1.6.xlsx`, superseded by the source.)

## 1. Game-style / frame-rate selection

swos-2020 selects everything from one integer `m_gameStyle` (`options.cpp:89`,
default 0), set via `--game-style=N` / ini `gameStyle`:

| style | name | FPS | notes |
|---|---|---|---|
| 0 | **DOS** | **70** | native DOS tuning |
| 1 | **Amiga** | **50** | native Amiga tuning |
| 2 | Moon Soccer | 50 | Amiga physics, low gravity novelty |
| 3 | Amiga | 60 | experimental |
| 4 | XBOX | 60 | XBLA tuning (DOS keeper dist + strong keeper AI) |

**0 (DOS/70)** and **1 (Amiga/50)** are the two releases; **4 (XBOX/60)** is
OpenSWOS's third style since 2026-11-02 (section 9). 2 and 3 are reference points. The style→FPS map (`render.cpp::getTargerFPS()` 4620-4640,
`game.cpp:8927`) is `0→70, 1→50, 2→50, 3→60, 4→60`.

**For our port:** `_gDosGameplay` (`include/dos_mode.h`) selects DOS(70) vs
Amiga(50). It is **independent of `gDosGraphics`** (`[graphics] style`) — you
can run DOS gameplay with Amiga graphics or vice-versa — but NOT of the view:
since 27c56b2 the gameplay style also picks the DOS/Amiga view. Recommend a single
internal "game style" enum so 60 FPS modes can be added later.

## 2. Frame pacing (the game loop)

- DOS native tick was a PIT countdown of **17045** = "approximately 70 times a
  second" (`swos.asm:11705, 15522`). Amiga = 50 Hz VBlank.
- swos-2020 paces with a high-res busy-wait/`SDL_Delay`: delay =
  `perfFreq * factor / kTargetFps`, `kTargetFps` from the style
  (`render.cpp::frameDelay()` 4719-4806); menus fall back to 30 FPS.
- **Our port** drives the tick from `tick_timer_cb` / `_RunInt3HandlerThrottled`
  in `platform_sdl.c`, originally hard-coded to ~50 Hz (20 ms; throttle `< 18`).
  **Done:** the timer now runs at `1000 / Platform_ActivePacingFps()` — 70 Hz
  (~14 ms; throttle ~12 ms) when the DOS style is active. The multiplayer frame budget per mode (from
  swos-2020 `issue-list.txt`): `1000/50 = 20 ms` (Amiga), `1000/70 ≈ 14.29 ms`
  (DOS), `1000/60 ≈ 16.67 ms` (XBOX).

## 3. The per-FPS constant table (load-bearing)

All set in `src/options.cpp::setGameStyleVariables()` (lines ~1563-1843). Apply
these to the matching SWOS globals in our tree (they exist as data from
`SWOS2.s`/`Data.c`) in an equivalent `ApplyGameStyle()` called at match init.
Columns: **DOS 70 / Amiga 50** (60-FPS shown where relevant).

### Ball physics
| Constant | DOS 70 | Amiga 50 | 60 FPS | Meaning / consumer site |
|---|---|---|---|---|
| `ballGroundConstant` | **13** | **16** | 16 | Per-frame speed lost to ground friction; `UpdateBall: speed -= ballGroundConstant` (`swos.asm:110451`). Higher at 50 FPS to lose the same speed over fewer frames. |
| `ballAirConstant` | **4** | **10** | 10 | Per-frame speed lost while airborne (z>0) (`swos.asm:110467`). |
| `gravityConstant` (int32) | **3291** | **4608** | 3840 | Per-frame downward Z-velocity decrement; `UpdateBall: D3 -= gravityConstant` (`swos.asm:110579`). (Moon=2560.) |
| `pitchBallSpeedInfluence[7]` | **-3, 4, 1**, 0,0,-1,-1 | **-2, 2, 3**, 0,0,-1,-1 | (=Amiga) | Per-pitch-type ball-friction modifier added to `ballGroundConstant` (`swos.asm:110463`). Only the first 3 entries differ. |

### Goalkeeper
| Constant | DOS 70 | Amiga 50 | Meaning |
|---|---|---|---|
| `keeperSaveDistance` | **16** | **24** | Dive/save distance threshold (`ShouldGoalkeeperDive`, `swos.asm:106705`). |
| `dseg_11061B[8]` | `0x28000…0x60000` | `0x30000…0x68000` (or `0x28000…0x60000` "stronger AI") | Keeper reach/dive distance table, difficulty-interpolated (`game.cpp::KeeperAI` 484-519; consumer `swos.asm:106773`). DOS reacts at shorter range. |

### Timers / match-clock thresholds (all int16 `word` compares)
| Constant | DOS 70 | Amiga 50 | 60 FPS | Gates (asm site) |
|---|---|---|---|---|
| `GS_54_49` | **70**¹ | **49** | 59 | **Frames per in-game second** — clock accumulator reload (`swos.asm:97826`). The central clock-rate constant (see §4). |
| `GS_55_50` | 55 | 50 | 60 | `endGameCounter` reload, last-minute switch (`swos.asm:98021`). |
| `GS_27_22` | 27 | 22 | 27 | Ball defensive-Z threshold for goalie catch (`swos.asm:114226`). |
| `GS_110_100` / `GS_PT_110_100` | 110 | 100 | 120 | Penalty timer → `NextPenalty` (`swos.asm:101431`). |
| `GS_385_350` | 385 | 350 | 420 | Halftime-result stoppage timer (`swos.asm:118468`). |
| `GS_550_500` | 550 | 500 | 600 | `ballOutOfGameTimer` (`swos.asm:101968`). |
| `GS_660_600` | 660 | 600 | 720 | Auto-advance final/halftime result (`swos.asm:118450`). |
| `GS_825_750` | 825 | 750 | 900 | Kickoff stoppage → `PrepareForInitialKick` (`swos.asm:101465`). |
| `GS_GK_55_50` | 55 | 50 | 60 | `stoppageEventTimer` reload, ball still (`swos.asm:101710`). |
| `GS_275_250` | 275 | 250 | — | Half/full-time result-banner display length — `ShowResult`'s `30000` marker (`game_state.c`). Found by the replay rig; swos-2020 hard-codes 275 (`game.cpp:13024`). |
| `GS_165_150` | 165 | 150 | — | Stoppage result-banner display length — `ShowResult`'s `31000` marker. Found by the replay rig: a DOS recording sets `resultTimer` to 165 at a stoppage where we set 150. |
| `GS_PT_55_50` | 50² | 50 | 60 | Penalty keeper-still-down compare (`swos.asm:113363`). |
| `GS_SF_128__128` / `GS_SF_8000h__8000h` | 128 / 0x8000 | -128 / -0x8000 | — | Sign-fix constants (negative for styles 1-3). |

¹ **MEASURED, 2026-09-03: the DOS value is 70, not 54.** This row previously read
54, taken from swos-2020's `GS_54_49 = 69; // original = 54` (`options.cpp:1581`).
The recorded-game replay rig settles it against a real DOS recording
(docs/TESTS.md §5): `zkz-12065730` reaches game-minute 1 at frame 702 and minute 2
at frame 842 — **140 frames per game-minute**. The accumulator subtracts
`gameLenSecondsTable[0] = 30` per frame and reloads by `N`, so seconds advance at
`30/N` per frame and 60 of them take `2N` frames: **N = 70**. 54 would give 108
frames/minute, 69 would give 138. swos-port's independent decompile agrees
(`gameTime.cpp: kPcTicksPerGameSeconds = 70`) and its replay of this same corpus
passes. `dos_gameplay.c` now uses 70. The Amiga value is the clean **49** and is
unchanged.
² `GS_PT_55_50` DOS value happens to equal the Amiga value (50).

### Why a DOS-vs-Amiga difference needs proof

The DOS executable is a **static recompilation of the Amiga one** — same release
(SWOS 96/97), fifteen dwords standing in for the 68000's registers, non-portable
routines rewritten and the rest converted (SWOS-RE README, "DOS Version"). The
two therefore agree by construction, and the replay corpus measures that
agreement at thousands of frames. So every entry in the tables above is a
deliberate hand edit by whoever did the conversion, and anything NEW claimed as
a difference should be read twice and then measured: the recorded-game rig has
already dissolved two apparent differences that were really one constant
expressed at a different view height, and one table entry reached by adjacency
on one side and spelled out on the other.

### NOT differentiated (important)
- **Kick / pass / shot power** are the **same** for DOS and Amiga in shipping
  swos-2020 (`setDeltaZVariables`, `options.cpp:1490-1524`): `ballKickingSpeed=2208,
  highKickBallSpeed=2688, normalKickBallSpeed=2560`, banded `passingSpeed*`
  1536→2218. Only styles ≥3 (60 FPS) apply a ×1.2 boost. Do **not** rescale
  kick/pass power between 50 and 70.

### Differentiated — the formations (switched by `ApplyGameStyle` since 71e174a)
The DOS release re-tuned three of the twelve built-in formations, and
`GetPlayerPrice` walks the tactics table for every player, so this is a
gameplay difference that reaches player values and in-game skill nibbles as
well as where men stand. When this section was written `ApplyGameStyle` swapped
only the physics and timer constants; since 71e174a ("tactics: --dos-gameplay
switches the formations too") it also calls `TacticsData_SelectRelease(dos)`
(`src/tactics_data.c`), because the replay corpus showed the DOS formations are
part of what DOS gameplay means. The build flag below still pins a set.

Available as a **build flag** since 2026-09-03: `make clean && make
SWOS_DOS_TACTICS=1` bakes the DOS coverage
(`bin/data_json/tactics/<id>_dos.json`, extracted from the DOS disassembly).
It was at first not wired to `--dos-gameplay=on` — a deliberate hold, because
the recorded-game corpus disagrees with the DOS data's two forward slots
(docs/TACTICS_POSITION_VALUE.md §9.6; sizes and the caveat are there). It is
wired now (71e174a, above); a `-DSWOS_DOS_TACTICS` build keeps its set either way.

## 4. Match clock

Frame-driven accumulator, **same algorithm** both modes, differing only by
`GS_54_49`:

- Per in-progress frame: `secondsSwitchAccumulator -= timeDelta`; on underflow →
  `secondsSwitchAccumulator += GS_54_49; gameSeconds++` (`swos.asm:97793-97830`).
  So `GS_54_49` ≈ frames-per-game-second: 49 @ 50 FPS, **70 @ 70 FPS** —
  measured against a DOS recording, see §3 note ¹.
- `timeDelta` comes from `gameLenSecondsTable dw 30,18,12,9` indexed by the chosen
  match length (`swos.asm:96458`) — **FPS-independent**; the FPS dependence is
  entirely in the `GS_54_49` reload.
- On-screen / record time reconstructs real seconds as `frames / fps`
  (`getFrameTimerString`, `game.cpp:2146`; `fps_rate = 1/{70,50,60}`,
  `game.cpp:8927`), so elapsed time is correct per mode.

Net: match length in minutes is identical; the 70 FPS mode just spends
proportionally more real frames per game-minute.

## 5. Scaling helpers

- **No `SCALE_FOR_FPS()` macro.** The only FPS-aware scaler is
  `setFixedPoint()` (`util/util.cpp:485-495`): identity for DOS/Amiga, ×(50/60)
  for the 60-FPS styles. It feeds ball-Z deltas — but that path
  (`setDeltaZVariables`) is **disabled** (`return;` at `options.cpp:1469`), so
  those deltas stay at asm defaults. Treat as "intended but inert."
- The 60-FPS `GS_*` values are hand-computed (×1.2), not macro-derived.
- `KeeperAI::set()` (`game.cpp:503-519`) linearly interpolates `dseg_11061B[]`
  between DOS and Amiga base arrays by difficulty — the clearest DOS-vs-Amiga
  table in the tree.

## 6. NOT FPS-scaled in the *shipping* asm — but see §8 (the missing piece)

> ⚠️ **CORRECTION (2026-07-08).** This section was written from
> `setGameStyleVariables()` only, and concluded player/animation/camera speed is
> "faster at 70 FPS by design." That is true of the **raw shipping DOS/Amiga
> binaries**, but it MISSED the **`speed_factor`** (a per-frame `deltaX/deltaY`
> scale = `50/fps`) that the test-team preset system applies to hold
> distance-per-second constant across frame rates. Our port implements neither,
> which is why DOS-70 players cover ~1.4× the ground per second. Read **§8**
> before acting on this section.

The following are unscaled in the raw shipping asm (they run one step per frame,
so they are effectively faster in wall-clock terms at 70 FPS in the *original*
binaries — but the preset system in §8 scales them):

- **Player movement speed & acceleration** — `playerSpeedsGameInProgress`,
  `kPlayerSpeed=14`, `playerGroundConstant` (debug-tunable only). Not touched by
  `setGameStyleVariables`.
- **Animation step rates** — `Sprite.frameDelay` counts down once per frame; no
  `GS_*` modifies it, so anims cycle faster at higher FPS.
- **Camera speed/acceleration** — no FPS-scaled camera-speed constant exists in
  swos-2020. (Our `move_camera.c` `MAX_CAMERA_MOVEMENT`/velocity caps are
  per-frame; if DOS-70 camera feels too fast, this is unreferenced territory —
  decide empirically.)
- **Injury/foul timers** — no distinct FPS-scaled constants; they ride the
  `GS_PT_*` / stoppage `GS_*` timers above.

## 7. Porting plan (for this SDL tree)

1. **Game-style state**: drive off `gDosGameplay` (→ DOS 70 vs Amiga 50). Keep an
   internal enum so 60 FPS can be added.
2. **Pacing**: make the `platform_sdl.c` tick rate (currently 50 Hz / 20 ms /
   throttle 18) a function of the active style — 70 Hz / ~14.29 ms / throttle ~12
   for DOS.
3. **`ApplyGameStyle()`**: at match init, write the §3 constants into the
   corresponding SWOS globals in our tree (locate `ballGroundConstant`,
   `ballAirConstant`, `gravityConstant`, `pitchBallSpeedInfluence`,
   `keeperSaveDistance`, the `dseg_11061B`/keeper table, and the `GS_*` timer
   globals — they come from `Data.c`/`SWOS2.s`). Our data defaults are the
   **Amiga** values (the original SWOS2.s the tree was reversed from); apply the
   DOS column when DOS gameplay is selected, restore Amiga otherwise.
4. **Clock**: confirm our match-clock accumulator uses the `GS_54_49` reload (it
   should — same code path); just make sure the constant is set per style.
5. **Leave** player speed, animation, camera, and kick/pass power **unscaled** —
   match swos-2020.
6. **Attract/replay note**: `.HIL`/`.RPL` reels are frame-rate-specific — a 70 FPS
   reel plays back wrong at 50 FPS and vice-versa (see `docs/ARCADE.md`). Tag/keep
   them by mode.

## 8. `speed_factor` and high-FPS gameplay authenticity (2026-07-08 — the missing piece)

Prompted by a tester report ("DOS-70 players move too fast; game speed should be
×0.714") + the EINTR. FRANKFURT test-team preset table. This section supersedes
§6's "faster by design" conclusion for the **preset system**.

### 8.1 The `speed_factor` (what §1–7 missed)

Beyond the `setGameStyleVariables` constants, the preset system carries a
**`speed_factor = 50 / fps`** that is applied **per frame to the player movement
deltas** (`deltaX`, `deltaY`) — NOT to the speed *table*. It holds
**distance-per-second constant** across frame rates:

| fps | speed_factor | effect |
|---|---|---|
| 50 (Amiga) | 1.000 | baseline |
| 60 | 0.833 (=50/60) | each frame's `deltaX/deltaY` × 0.833 |
| 70 (DOS) | 0.714 (=50/70) | each frame's `deltaX/deltaY` × 0.714 |

Test-team physics-preset table (Amiga family; the `g`/`maxFrames`/`maxDistance`/
`maxHeight` columns are the *ball kick trajectory* tuned per preset, with `_ratio`
columns = vs the 50 fps baseline):

| preset | 1 = 50fps_amiga | 3 = 60fps_amiga | 5 = 60fps_amiga(2) | 7 = 60fps_amiga(3) |
|---|---|---|---|---|
| fps | 50 | 60 | 60 | 60 |
| speed_factor | 1.000 | 0.833 | 0.833 | 1.000 |
| g (gravity) | 4608 | 3840 | 4608 | 4608 |
| maxFrames | 22 | 18 | 16 | 16 |
| maxDistance | 184 | 172 | 153 | 184 |
| maxDistance_ratio | 1.000 | 0.935 | 0.833 | 1.000 |
| maxHeight | 22 | 25 | 22 | 22 |
| maxHeight_ratio | 1.000 | 1.136 | 1.000 | 1.000 |

Presets 3/5/7 are **experiments** trading speed_factor vs gravity vs ball-trajectory
to find the best 60 fps feel; the test team hadn't converged.

### 8.2 CRITICAL authenticity distinction — `speed_factor` is NORMALIZATION, not real DOS

Verified from the retail DOS disassembly (`swos-2020/swos/swos.asm`):
- Player speed table (`playerSpeedsGameInProgress dw 928,974,…,1250` @247873) is
  **byte-identical to the Amiga** — and to our `_kPlayerSpeedsGameInProgress`.
- `MovePlayer` (@103063) does `x += deltaX` / `y += deltaY` with **no scaling**.
- DOS tick = 70 Hz (PIT 17045).

⟹ The **real DOS binary used speed_factor = 1.000** — players genuinely moved
**1.4× faster** than the Amiga (same per-frame deltas, 70 vs 50 frames/sec). That
is authentic DOS; it is *the* reason DOS SWOS feels more frantic than the Amiga.
So `speed_factor = 0.714` is a **modern normalization** ("make DOS play at Amiga
wall-clock pace"), NOT a reproduction of the real DOS.

**Decision axis for our port:**
- *Authentic real DOS* → speed_factor 1.000 (current behaviour; players fast).
- *Consistent/normalized speed* (test-team goal) → speed_factor 0.714.
Recommend: keep authentic-fast as the DOS default; expose 0.714 as an optional
"consistent speed" preset if wanted. (Mirrors the `GS_54_49 54-vs-69` decision.)

> ⚠️ **CORRECTION (2026-09-03) — the real DOS binary DOES scale, by 41/64, and
> it does it inside `CalculateDeltaXAndY`, not in `MovePlayer`.** The reasoning
> above is sound about `MovePlayer` (`x += deltaX`, unscaled) and about the
> speed table, but the scale is applied one level earlier, where the deltas are
> produced. Three independent lines of evidence, all from the replay work
> (docs/TESTS.md):
>
> 1. swos-port's decompile of the routine applies
>    `x - (x>>2) - (x>>4) - (x>>5) - (x>>6)` (= ×41/64) whenever Amiga mode is
>    off, with the comment *"using this method gets exactly the same result as
>    SWOS"*.
> 2. Its `CalculateDeltaXAndY` reference vectors carry the scaled values — e.g.
>    92160 → 59040, exactly ×41/64. `bin/SWOS --replay-selftest` runs them.
> 3. Replaying a DOS recording with the scale ON reaches frame 972 before
>    diverging; with it OFF, frame 339.
>
> So `_gMoveDeltaScaleNum/Den = 41/64` in DOS mode is not a normalization
> deviation — it is what the DOS binary did, and it is required to reproduce
> DOS recordings. §8.1's "our port implements neither" is likewise superseded.
>
> **It must be done as the shift chain, not `* 41 / 64`.** Arithmetic shifts
> floor; C division truncates toward zero, so the two disagree by one on
> negative deltas and the trajectory forks within a few frames. `math68k.c`
> special-cases 41/64 for this reason; the vector test went 4/10 → 10/10 on the
> change.

### 8.3 WHERE swos-2020 implements it — the LIVE `CalculateDeltaXAndY_125` hook

> ⚠️ **CORRECTION (2026-07-08, 2nd pass).** A first read concluded "dead
> scaffolding / never solved" after finding `g_SpeedFactor` / `AdjustBallSpeed_2`
> / `AdjustDestFactor_2` commented out. **Those are a decoy** (an abandoned
> earlier attempt). The REAL, LIVE speed_factor is an asm-injection hook — found
> only after searching `render.cpp` + `symbols.txt`, not the movement files.

- `swos/symbols.txt` is the hook table: a line `<AsmFunc>, <offset>[, <hookName>]`
  injects a call to `<AsmFunc>_<offset>()` at that instruction offset; a leading
  `#` disables the line.
- Line 2083 — `CalculateDeltaXAndY, 125` (**ACTIVE**) → injects
  `SWOS::CalculateDeltaXAndY_125()` at offset 125 of `CalculateDeltaXAndY` (right
  after the deltas are computed). Line 2082 `#CalculateDeltaXAndY, 153` and 2084
  `#UpdateBall, 155` are `#`-disabled.
- `SWOS::CalculateDeltaXAndY_125()` (`src/video/render.cpp:4682`, live / not
  `#if(0)`) scales the movement deltas `D1`/`D2` by game style:

  | style | fps | delta scale |
  |---|---|---|
  | 0 DOS/PC | 70 | **× 41/64 ≈ 0.641** |
  | 1 Amiga | 50 | ×1 |
  | 3/4 Amiga | 60 | **× 50/60 = 0.833** (when `g_BallMoved==0`) |

So the speed_factor scales **player** movement (every consumer of
`CalculateDeltaXAndY`'s deltas). The **ball is NOT delta-scaled** — its hooks
(`#UpdateBall, 155`; the `CalculateNextBallPosition_*` / `AdjustDeltaZ_2` bodies in
`controls.cpp` are all `#if(0)`) are disabled; the ball rides the §3 physics
constants (gravity 3291, etc.) instead.

⚠️ The DOS factor is **41/64 ≈ 0.641**, *below* the naive `50/70 = 0.714` — port
swos-2020's **exact 41/64**, not the frame-ratio guess.

**Authenticity note:** swos-2020 *injects* this scale; the raw retail DOS
`MovePlayer` (§8.2) has none (players 1.4× faster). So 41/64 is swos-2020's
deliberate normalization, not baked into the original DOS binary — but it IS what
the reference does, and it is exactly the "missing movement scale" the test team
reported.

### 8.3-port — what OUR port must add (small + clean)

Our `CalculateDeltaXAndY` is one C function (`src/math68k.c`). swos-2020's hook
injects at its END (offset 125), so the faithful port is: at the end of
`CalculateDeltaXAndY`, multiply `outDeltaX`/`outDeltaY` by the active game-style
factor (41/64 DOS-70 / 50/60 Amiga-60 / 1 otherwise). That reproduces the hook
exactly — every mover using those deltas is scaled, just like swos-2020. Ball
stays on the §3 constants (already applied by `ApplyGameStyle()`).

### 8.4 Amiga 60 Hz — `speed_factor` is NOT good enough; decouple instead

For an **authentic** Amiga 60 Hz mode, `speed_factor 0.833` is insufficient:
scaling `deltaX/deltaY` by 0.833 keeps the *average* distance/second but each
frame's delta is **fixed-point-truncated**, so discrete positions drift from the
true 50 Hz path — it can never be bit-exact (this is exactly the tester's "needs
decoupling"). The correct approach is **temporal decoupling**:

1. Run the game **logic at a fixed 50 Hz timestep** (accumulate real time; step
   the sim once per 20 ms) — byte-identical to the Amiga.
2. **Render at 60 Hz**, drawing each sprite / the ball / the camera at an
   **interpolated** position `prev + (curr - prev) × alpha`, where `alpha` is the
   fraction of the way to the next logic step. (Store prev+curr per entity.)

Result: exact 50 Hz gameplay + smooth 60 Hz visuals. This is **our own approach**
(the reference didn't do it) and is the only way to keep Amiga gameplay authentic
at a higher refresh. Cost: an interpolated render layer (all match-render position
reads consult prev/curr + alpha). Bigger than the DOS speed_factor, but the only
faithful path. NOTE: `.HIL`/`.RPL` replays and the arcade attract reels are
frame-rate-tagged (§7.6) — a decoupled loop must record/replay at the logic rate.

## 9. XBOX (60 FPS) — SWOS 2020's style 4 (implemented 2026-11-02)

`[gameplay] style = xbox` (`--gameplay=xbox`, OPTIONS -> GAMEPLAY's third value, the
online OPEN prompt's third button). `_gGameplayStyle` (`include/dos_mode.h`) is the
style; `_gDosGameplay` stays (style == DOS) for the DOS release's code paths, and
`_gDosKeepers` (DOS or XBOX) is the DOS goalkeeper's code. `Gameplay_SetStyle`
(`src/dos_gameplay.c`) sets all three. SDL only: the Amiga build has no 60 Hz tick.

What it is, from swos-2020 (`options.cpp` setGameStyleVariables style 4, `render.cpp`
CalculateDeltaXAndY_125) -- "60 FPS, Amiga physics, DOS goalkeepers":

| | XBOX | |
|---|---|---|
| frame rate | 60 | `DosGameplay_TargetFps` |
| ball | ground 16, air 10, the Amiga pitch table; gravity **3840** | the Amiga's ball at 60 (x50/60) |
| player movement | deltas **x50/60**, C truncation (`math68k.c`'s general path) | swos-2020's `g_BallMoved` is never set, so always |
| keeper | save distance **16**, `GS_27_22` **27**, reach table **0x40000..0x78000** | "XBOX GK Values"; DOS's is 0x28000.., Amiga's 0x30000.. |
| keeper code | the DOS dive index and the DOS diving-catch ball placement | `_gDosKeepers`: goalkeeper.c, ball_physics.c |
| keeper's loose ball | the deltaZ window **[+0x8000, 0x8000]** | below |
| timers | clock **59**; the rest the Amiga's x1.2 (60, 120, 420, 600, 720, 900; the rig's later reloads the same x1.2: 300/180, 60, 120/300/840/1800) | swos-2020 has GS_* for the first set only |
| formations, view, everything else | the Amiga's | |
| online input delay | the Amiga table x50/60: 1 to 42 ms, 2 to 83, 3 to 146, then 4 | no table of Playa's for 60 |

**The keeper's loose ball** (`update_players.c`, `_gsKeeperLooseBallMinDeltaZ`): a slow
ball near the keeper, low, with its vertical speed in [-0x8000, 0x8000], and he goes for
it -- both releases. swos-2020 replaced the DOS compare's -0x8000 with `GS_SF_8000h__8000h`,
**+0x8000** in its DOS and XBOX styles ("sign fix", swos.asm 113924), so its keepers
leave almost every such ball. Its other five `GS_SF` sites add +/-128 to a direction and
mask it to a byte straight after, which makes the sign irrelevant. Our DOS style keeps the
real DOS -0x8000 (the recorded-game rig matches it); XBOX takes swos-2020's.

**What is not ported:** swos-2020's `setDeltaZVariables` (x1.2 kick and pass speeds) and
`setGameStyleVariablesAdditional` both `return` on their first line; its ball-trajectory
hooks are `#if (0)`. `KeeperAI::set` is the challenge mode's CPU keeper levels only.

**Online:** a match plays the host's style. The handshake's setup carries it (byte 19,
`setup_style`: 0 from an older build reads the rules byte's DOS bit), recordings carry it
(SIMV byte 3, META flag 32, the recorder stream's byte 9 -- `--srp-resim` replays an XBOX
match as one), NETPLAY_COMPAT 9. `P2PLink_SetPlayer` tells a punched link which side it is:
without it both sides of the direct-IP lobby's rematch, and of the test harnesses, borrowed
each other's style. `HOST_STYLE=xbox scripts/netplay_lobby_test.sh` plays it.

## Authoritative swos-2020 files
- `src/options.cpp` — `setGameStyleVariables()` (1563-1843, THE table),
  `setDeltaZVariables()` (1467-1525), `--game-style` parse (619-634).
- `src/util/util.cpp:485-495` — `setFixedPoint()`.
- `src/game.cpp` — clock (`UpdateTime` ~15101), `getFrameTimerString` (2122-2195),
  `KeeperAI` (482-522), style enum (474-481), `fps_rate` (8927).
- `src/video/render.cpp` — pacing (`getTargerFPS` 4620, `frameDelay` 4719).
- `swos/swos.asm` — DOS defaults + consumer sites; `GS_*` block ~250950, physics
  ~203742. `swos/symbols.txt` — per-symbol C types.
- `issue-list.txt` — multiplayer frame-time + per-mode notes.

## Open questions
- **`GS_54_49` — SETTLED BY MEASUREMENT:** `70` (§3 note ¹). The earlier
  decision here ("default 54, runtime tweak to 69") is superseded; both 54 and 69
  are wrong against a DOS recording. `src/dos_gameplay.c` uses 70.
- Do we want the 60-FPS styles at all, or just DOS-70/Amiga-50?
- ~~Does our match-clock path already key off `GS_54_49`?~~ Yes:
  `game_state.c:UpdateTime` reads `_gsFramesPerGameSecond`, which
  `ApplyGameStyle` sets.
