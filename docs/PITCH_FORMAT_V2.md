# Pitch format v2 — Tiled maps with layers and animation

The pitch today is 2310 tiles and nothing else: one grid, one tile each, drawn
before everything, fully opaque. The only thing that ever moves is the crowd,
and it moves because 300 lines of C know that tile indices 1..22 come in
sit/stand pairs.

v2 replaces that with an **extended Tiled map** (`.tmj`): several layers, some of
them above the players, with a frame-based animation system that tweens with
easing. A beach pitch can have surf on one layer, birds taken from a hidden
sheet of frames, and a cloud layer drifting over the top of the match. A pitch
author sees the tiles they are animating, in Tiled, before we ever write an
editor of our own.

> **2026-10-01:** the format became Tile Map Editor's (Tile Agent then; `D:\Projects\GitHub\TileAgent`,
> `docs/FORMAT.md` there), which supersedes §2–§5 below, and its first stage is in the game:
> [PITCH_MAPS.md](PITCH_MAPS.md). §1, §6 (the Amiga), §7 and §8.1 still describe the engine.

This is the design. Nothing here is implemented yet except the easing
(`src/easing.c`), which is done and verified. (Still true 2026-09-30: no `.tmj`
loader or layer code exists in `src/`. Source line numbers below were written
against an older tree and have drifted by up to ~30 lines; search for the name.)

---

## 1. What we are extending, and what it costs

Measured, not assumed — these are the facts that shape every decision below.

| | today |
|---|---|
| grid | 42 x 55 of 16 x 16 tiles = **2310 cells**, `PITCH_POINTER_COUNT` (`src/pitch.c:36`) |
| file | `grafs*/SWCPICHn.MAP`, RNC v2: 2310 **big-endian u32 byte offsets** then the tile pixels (`src/pitch.c:225-241`) |
| depth | inferred, not stated — `pitchTilePlanes()` guesses 4bpp vs 8bpp from the offsets (`src/pitch.c:253`). **There is no version field.** |
| draw | `DrawChunkyPitch()` (`src/pitch.c:786`), whole visible area every frame, from `ScrollToCurrent()` (`src/camera.c:165`) |
| order | pitch → simulation → adverts → `DrawAnimatedPatterns()` → `DrawSprites()` (`src/game_loop.c:674-729`) |
| layers | **none.** One index space, one tile per cell, no flags, no priority |
| alpha | **none.** `dstRow[px] = srcRow[col]`, unconditional; index 0 in a pitch tile is *grass*, not transparent (`src/pitch.c:445, 797`) |
| over sprites | **nothing is drawn after `DrawSprites()`** except the bench menu and the stats HUD |
| Amiga budget | `_pitchDatBuffer` is **82 KB** and lives in `SWOS_CHIP_BSS`; the AGA maps already reach **78 KB** |

That last row is the hard one. **There is about 4 KB of Chip RAM headroom on the
Amiga**, so v2 cannot simply be "the same thing but with more layers" there.
§6 says what the Amiga takes and what it declines.

The other two tiers already fabricate what `DrawChunkyPitch` wants and will keep
working untouched: the DOS tier parses its own `PITCHn.DAT`/`.BLK`
(`src/pitch.c:165-215`) and the arcade tier dices a BMP into an identity map
(`src/arcade.c:1397`).

---

## 2. The shape of a v2 pitch

A plain Tiled map, readable and editable in Tiled with no plugin, plus custom
properties under a `swos:` prefix. Everything we add is either a **layer** or a
**property on a layer**, because that is what an editor shows you.

```
pitch1-normal.tmj
├── tileset  "tiles"            the 16x16 sheet, as today
├── layer    "pitch"            swos:role=pitch   swos:z=-100   the ground
├── layer    "crowd-sit"        swos:role=crowd-sit   swos:z=-90
├── layer    "crowd-stand"      swos:role=crowd-stand visible=false
├── layer    "surf-frames"      visible=false        a sheet of frames to copy from
├── layer    "surf"             swos:z=-80           where they get copied to
└── layer    "clouds"           swos:z=+100          drawn OVER the players
```

Two kinds of layer, and the difference is the whole trick:

- **Visible layers** are drawn. `swos:z` says when: **negative = before the
  sprites** (part of the ground), **positive = after them** (over the players).
  `0` is reserved — the sprites themselves are at 0.
- **Hidden layers** (`"visible": false`, which is
  [Tiled's own field](https://doc.mapeditor.org/en/stable/reference/json-map-format/))
  are never drawn. They are **frame storage**: an animation copies rectangles
  out of them into a visible layer. All twelve frames of a wave, every bird
  pose, the sun's whole cycle — laid out in the editor where the author can see
  them, costing nothing at runtime but the tiles themselves.

So "animate the surf" is: a hidden `surf-frames` layer holding the frames side by
side, and an animation on the visible `surf` layer that copies frame *n* into
place each step. The author sees both.

### 2.1 Where the animation data lives

**On the layer it draws into.** A cloud drift belongs to the cloud layer; the
surf animation belongs to `surf`, not to `surf-frames`, even though that is where
its pixels come from. The rule is *the animation lives where you see it happen*,
which is the only rule that stays obvious inside an editor. A source layer is
named, not owned:

```json
{ "name": "surf", "type": "tilelayer", "visible": true,
  "properties": [
    { "name": "swos:z", "type": "int", "value": -80 },
    { "name": "swos:anim", "type": "string", "value": "…" }
  ] }
```

Tiled's own `properties` are a flat list of scalars, so the animation is carried
as a JSON **string** in `swos:anim` and parsed by us. That is deliberate: it
survives a Tiled round-trip untouched, our editor can later present it properly,
and no part of it depends on Tiled growing a feature.

### 2.2 An animation, specified

```jsonc
{
  "id": "waves",             // unique within its layer
  "trigger": "always",       // always | kickoff | goal | halftime | whistle | never
  "delay": 0,                // frames before the FIRST pass, not before each loop
  "duration": 24,            // frames in one pass, >= 1
  "loop": true,
  "pingpong": false,
  "ease": "linear",          // any name from src/easing.c; eases TIME (see below)
  "tracks": [
    { "kind": "blit",        // copy tiles: a frame from a hidden layer -> the grid
      "from": "surf-frames",
      "src": { "x": 0, "y": 0, "w": 6, "h": 2, "frames": 12, "stride": 6 },
      "dst": { "x": 4, "y": 51 } },                       // all in TILES

    { "kind": "shift", "dxPx": 48, "dyPx": 0, "wrap": true },   // PIXELS

    { "kind": "fade", "from": 255, "to": 96 }                   // 0..255
  ]
}
```

**Time, not value, is what gets eased.** One `ease` field, one meaning
everywhere: the curve maps *elapsed fraction* to *progress fraction*, and each
track then reads that progress linearly. For `shift` and `fade` this is
identical to easing the value (easing `t` and lerping is what "ease a value"
means anyway); for `blit` it is the only reading that makes sense, because a
frame index cannot be interpolated. So there is no second field and no per-kind
special case — and the honest consequence is that **easing a flipbook is a time
warp**, which is why `blit` defaults to `linear`. An `outbounce` on a twelve
frame wave will re-show frames as the curve doubles back. Occasionally that is
what you want; mostly it is a mistake, and now it is a documented one.

**The clock, exactly.** `startFrame` is the match frame the trigger fired on:

```
e = frame - startFrame - delay            e < 0  -> nothing drawn yet
n = e / duration                          which pass
u = (e % duration) / duration             0..1 within the pass
if pingpong and n is odd:  u = 1 - u
if not loop and n >= (pingpong ? 2 : 1):  hold at the final value
p = Ease(ease, u)
```

Everything derives from the frame number, so a replay re-deriving an animation
at frame N always gets what the live match drew. `delay` applies once, before
the first pass — a looping animation that re-waited every cycle would stutter,
and a gap between cycles belongs in `duration` as hold frames, not in a second
field. `pingpong` without `loop` means out and back once, then hold.

**Units are in the field names.** Grid rectangles (`x`, `y`, `w`, `h`, `stride`,
`dst`) are in **tiles**, because they address cells. Motion is in **pixels**
(`dxPx`, `dyPx`), because a drifting layer that can only move in 16 px steps is
not drifting. A tile is exactly 16 px, so the two are not really different units
— but mixing them silently is how a format gets misread, so the pixel ones say
so.

**A `blit` picks its frame by flooring:**
`index = clamp(floor(p * frames), 0, frames - 1)`. Floor gives every frame an
equal share of the duration; rounding would give the first and last frames half
a share each, which makes a looping flipbook stutter at the wrap. The clamp is
what absorbs `back` and `elastic` overshooting outside 0..1, so no curve needs
special-casing.

**Re-triggering restarts, and the game already told us so.** A `goal` animation
that is still running when a second goal goes in re-arms from `delay` and starts
again. That is not a coin toss: it is exactly what the game's own celebration
clock does — `patternsGoalCounter` is *armed to 1* when a goal fires
(`src/game_timers.c:614`), restarting the 300-frame celebration rather than
queueing or ignoring it. Matching it keeps a v2 animation in step with the crowd
reacting beside it. `always` starts at kick-off and free-runs; `never` is for
animations only something else will start, which today means the editor.

**When two tracks write the same cell, the last one wins** — layer order in the
file, then track order within the animation. It is defined rather than forbidden
because deliberate occlusion is legitimate (a bird passing in front of surf on
one layer), but it is almost always a mistake, so **the emitter warns** on
overlapping `dst` rectangles within a layer. Warn, not reject: the same call the
exporter makes about a suspicious fan range, and for the same reason — a tool
that refuses to build is worse than one that says what it saw.

**A `fade` where nothing can blend is ignored, with one warning at load.** The
pipeline is 8bpp indexed and has no alpha (§7.1), so until the GPU path exists
this track does nothing on most targets. Ignoring it means a pitch authored for
a machine that can blend still loads and plays everywhere else, just flatter —
which is the same bargain the capability word in §6 makes for the Amiga.

## 3. Easing — done

`include/easing.h` and `src/easing.c` are in the tree: Penner's 31 curves,
`Ease`, `EasePenner`, `EaseBetween`, and `EaseName`/`EaseFromName` for the JSON.

**In 16.16 fixed point, not float, and not by preference.** The m68k target is
built `-nostdlib` and its toolchain has no `<math.h>` at all — not `sinf`, not
`powf`, and not even the soft-float helpers (`__muldf3`, `__floatsidf`) that a
float expression pulls in. `src/mjson.c:22-28, 314` hit exactly this and ended up
with an integer-only number parser for the same reason. So the curves are
computed with a 65-entry quarter-sine table, a 17-entry `exp2` table and an
integer square root, and angles are measured in **turns** so there is no π in the
file at all.

Verified against a float reference at 11 points on all 31 curves: **worst error
20/65536, 0.03% of the range** (`inoutquint`, accumulated rounding in the
repeated multiplies). The 64-bit intermediate in `fmul` is fine on m68k — the
68020 has a 32x32→64 multiply and gcc inlines it, exactly as `src/camera.c:61`
already relies on.

One trap worth keeping, because the Amiga link found it rather than review: the
obvious `((int64_t)a << 16) / b` for the ratio in `EasePenner` **does not link**
on m68k — a 64-bit divide is a call to `__divdi3` and `-nostdlib` has no libgcc
to call. The 64-bit *multiply* in `fmul` is fine (the 68020 has 32x32->64), but
its `divu.l` traps when the quotient will not fit 32 bits, so gcc will not use it
either. `fdiv` is therefore a 32-bit divide for the integer part and sixteen
fraction bits one at a time. `obj/easing.o` has no undefined symbols at all.

The side benefit of fixed point is that both targets get **bit-identical**
curves, which matters the moment an animation is allowed to touch anything the
simulation can see.

---

## 4. The crowd: a binding, not a trigger

The question was whether the crowd should be driven by the event/trigger system.
**No — and getting this wrong would break netplay and every existing replay.**

`docs/CROWD_ANIMATION.md` is the detail; what matters here is that the crowd is
**not decoration, it is simulation**:

- picking a cell consumes `Rand()` (`animated_patterns.c:104`, two calls per
  pick), so the crowd is in the game's RNG stream;
- `animPatternsState`, `animPatternsIndex`, `patternStatic`,
  `patternsGoalCounter`, `showFansCounter` and `otherTeamScored` are all in the
  **SIM** tier of the rollback registry (`src/sim_state.c:541-544`);
- the state word is written per frame into `.HIL` and into the `.SRP` frame head
  (`src/replay_v2.c:487`), and a replay *reads it back* rather than deriving it.

So if a data-driven animation system picked the crowd's cells, it would either
have to reproduce the existing `Rand()` consumption bit for bit — or every
existing recording would desync and every netplay match would diverge at the
first stoppage.

**Therefore: the crowd's driver stays exactly where it is.** What v2 changes is
only what the driver *knows*, which is currently hard-coded:

| hard-coded today | becomes |
|---|---|
| "fan tiles are indices 1..22" (`MAX_FAN_TILE_INDEX`) | the cells on the `crowd-sit` layer |
| "standing is the tile after sitting" (±`TILE_SIZE`) | the tile at the same cell on `crowd-stand` |
| "rows 1-3 and 51-53" (`ResetAnimatedPatternsForBothTeams`) | the extent of those layers |

The two layers are also **why this is worth doing**: the ±1 pairing does not
survive the trip through Tiled at all. The exporter deduplicates tiles by pixel
content, so "the next tile" in the sheet is not the standing version of anything
(§8). Two layers say it in a form the author can see and the tooling cannot get
wrong.

A pitch with no `crowd-sit` layer simply has no crowd, which is what the arcade
tier already does by accident.

### 4.1 Then what are triggers for?

Decorative animations, and they get a strict rule:

> **A v2 animation may not call `Rand()`, may not write simulation state, and may
> not be read by anything that does.** It is VIEW tier. If an animation ever
> needs randomness it gets its own generator, seeded per match from the match
> seed, advanced only by the animation system.

With that rule, triggers are safe and simple: `kickoff`, `goal`, `halftime`,
`whistle`, `always`, `never` — edges published by the game loop that the
animation system samples. It reads the same counters the crowd does
(`patternsGoalCounter`, `showFansCounter`) but never writes them.

That is the line: **the crowd reacts because it is part of the match; the seagulls
react because someone scored, and nothing downstream may care whether they did.**

---

## 5. Drawing: three passes instead of one

Today `DrawChunkyPitch()` runs once, before everything. v2 splits it:

```
ScrollToCurrent()      -> layers with swos:z < 0, in z order   (the ground)
  … simulation …
DrawAnimatedPatterns()                                          (crowd, unchanged)
DrawSprites()                                                   (z = 0)
DrawPitchOverlays()    -> layers with swos:z > 0, in z order    (NEW)
```

Consequences worth stating before anyone writes the code:

- **Over-sprite layers need transparency, which pitch tiles do not have.** Index
  0 on the pitch is grass. An overlay layer must treat 0 as transparent — the
  sprite convention (`src/sprites.c:307`) — so the loader marks a layer's tiles
  as sprite-convention when `swos:z > 0`. A tile sheet cannot be shared between
  a ground layer and an overlay layer unless index 0 is unused in it.
- **The over-sprite pass must not write anything the simulation reads.** It runs
  after `DrawSprites()`, which is inside the frame GGPO may re-run
  (`src/netplay_match.c`), so it must be pure output. The existing crowd
  spot-blit is *not* pure — it edits the tile map — which is precisely why it
  stays where it is rather than becoming a v2 animation.
- **The back-buffer dance.** `drawPatternFromIndex()` draws twice, into the
  frontbuffer and the mask buffer, so a change survives the swap
  (`animated_patterns.c:199-208`). Overlay layers are redrawn from scratch every
  frame like the pitch is, so they need none of that — but anyone tempted to
  "optimise" them into partial updates will meet the same problem.

---

## 6. The Amiga, which gets a subset and a binary

Two constraints decide this, and both are already measured:

1. **`bin/data_json` never reaches the Amiga as JSON.** Parsing text JSON on a
   68k takes seconds per file (`docs/DATA_BSON.md` §1), so the pipeline bakes
   JSON to C at *build* time and the m68k compiles the result
   (`docs/DATA_PIPELINE.md`). A pitch is too big for that treatment — a 2310-cell
   grid per layer per pitch as C initialisers would be absurd — so pitches follow
   the **`emit_teams.py` precedent instead: a Python emitter writes a compact
   binary, RNC-wrapped, and the game loads it at runtime.**
2. **There is ~4 KB of Chip RAM headroom** (§1). So the Amiga takes the base
   layer and the crowd layers and **declines decorative layers**, which is not a
   loss: it is the machine that cannot spare the blitter time either.

The binary (`.PCH`, alongside the `.MAP` it eventually replaces) follows the
rules the save format already learned the hard way
(`docs/64BIT_TRANSITION.md` §1, §6, and CLAUDE.md "no addresses, no ranges"):

- **Tile *indices*, `u16`, never byte offsets and never relocated pointers.** The
  current format's offsets are the entire reason `s_pitchFullPtrs[]` exists, why
  `ANIM_TILE_BASE` subtraction tricks exist, and why rollback needs
  `AnimatedPatterns_SyncTileMirror()`. 2310 cells fit a `u16` with room to spare.
- **Big-endian**, via `BE_READ_U16`/`BE_READ_U32` — free on the Amiga, one
  byteswap on SDL, and the same choice the existing `.MAP` made.
- **A magic and a version**, which the current format conspicuously lacks (its
  bit depth is *guessed*).
- **A capability word**, so a pitch that needs over-sprite layers or alpha says
  so and an Amiga build can refuse it cleanly instead of rendering nonsense.
- Every count range-checked at load against the live caps
  (`PITCH_POINTER_COUNT`, `PITCHFILE_MAX_SIZE`) — `docs/DATA_CUSTOMIZE.md` §1a.

Sketch:

```
"SWPC" u16 version u16 caps
u16 cols, rows, tileW, tileH, layerCount, animBytes
per layer: u8 role, i16 z, u8 flags(visible, alpha), u16 tileCount, u16 cells[cols*rows]
anim block: fixed-width records, no strings and no floats
  per anim:  u8 trigger, u8 ease, u8 flags(loop, pingpong), u8 trackCount,
             u16 delay, u16 duration
  per track: u8 kind, u8 srcLayer, then the kind's fields as i16
```

Everything symbolic is resolved by the emitter, not the game: `"inoutsine"`
becomes `EASE_IN_OUT_SINE` through `EaseFromName()`, a `from` layer name becomes
that layer's index, and a trigger name becomes its enum. The runtime never sees
a string, which is what makes the Amiga side a table walk rather than a parser.

The animation table is **baked from the JSON at build time too** — the runtime
never sees `"inoutsine"`, only `EASE_IN_OUT_SINE`. That is what `EaseFromName()`
is for, and it runs in the emitter, not the game.

---

## 7. What is genuinely missing from the engine

Not design — capability. In rough order of effort:

0. **Or it goes away**: if a layer declares itself `rgba` rather than `indexed`,
   items 1 and 3 below stop being problems — it blends and it shifts like any
   truecolour surface. Only the crowd layer has to stay indexed, because its
   pixels are recoloured to the kits. See `docs/PALETTE_V2.md` §8.
1. **Per-layer alpha (`fade`) has no renderer.** The pipeline is 8bpp indexed
   end to end; blending two palette indices means either a 64 KB blend LUT per
   palette pair, or the GPU path in `docs/SHADERS.md`. **The cloud example needs
   this.** Until then a `fade` track can only step between palette ramps the
   pitch already has, which is a real constraint on what the first beach pitch
   can look like.
2. **Over-sprite drawing** (§5) — a new pass, plus the index-0 convention.
3. **Sub-tile `shift`** — `DrawChunkyPitch` draws on tile boundaries with a
   camera offset; a drifting layer needs its own fractional offset, which is the
   same arithmetic the sub-pixel scroll already does (`--subpixel`).
4. **A second tile bank.** `_pitchChunkyBuffer` is exactly 2310 tiles
   (`src/swos_globals.c:1028`); extra layers with their own tiles need their own
   storage and a per-layer base.

---

## 8. First job: the exporter — and the landmine in it

The plan is to change SwosGfx so the 42 default pitches export with the crowd on
its own layer. Two things have to happen, and the second is not obvious.

**The writer is a hand-built string, hardcoded to one layer.**
`TiledMap.Write` (`SwosGfx/TiledMap.cs:67`) appends `"layers":[ { … } ]` with a
literal `"id":1` and `"nextlayerid":2`, and asserts `Gids.Length == Width*Height`
(`:74`). `MapData` (`:37`) has a single flat `int[] Gids`. Multi-layer means
turning that into a list of layers and emitting `properties` — mechanical, but it
touches the reader too (`ReadJson` takes the **first** tile layer and ignores the
rest, `:234`).

**The landmine: the exporter throws away the engine's tile index.**
`DosPitch.SavePitchAsTiled` (`SwosGfx/DosPitch.cs:125`) deduplicates tiles **by
pixel content** and assigns ids in first-appearance order (`:143-194`), then
discards the `patternIndex` that `GetPatternIndexForPointer` returned — and that
index is what the engine means by "fan tile 1..22". The two happen to line up on
pitch1–5 because those `.blk` files are stored in first-use order, **and they do
not line up on pitch6**, where `-insertblanktile` shifts everything by one
(fan tiles land on ids 2..23 instead of 1..22).

So classifying the crowd by gid would silently produce a wrong crowd layer on one
pitch in six. The fix is to **keep the pattern index** through the dedup loop and
classify cells by it:

```csharp
// in the loop at DosPitch.cs:176-192, alongside tileToId
int patternIndex = _patterns.GetPatternIndexForPointer(pitchIndex, pointerIndex);
bool isFan = patternIndex >= 1 && patternIndex <= 22;      // the engine's rule
```

and then emit, per cell: the pitch layer gets the tile when it is not a fan,
`crowd-sit` gets it when it is, and `crowd-stand` gets the tile that
`patternIndex + 1` resolves to — **resolved through the same dedup map**, which is
the whole reason the pair cannot be expressed as "+1" in the output.

While in there, the exporter also writes `swos:role` and `swos:z` properties on
each layer.

### 8.1 Row 0 is the fan pointer table, not scenery

Worth knowing before anyone "tidies up" the top of a pitch map. **Row 0 is never
drawn** — the camera clamps at `newIntY < 0x10` (`src/move_camera.c:411`),
exactly one tile row — and in pitches 1-5 it holds **one of every fan pattern
1..22, eleven sitting and eleven standing**, while the visible stands below it
hold sitting tiles only.

It is not a bank, though. It is **the fan pointer table**:
`FixupAndColorPitchFile` step 3 aliases slots 0..20 as fan group A and slots
21..41 as fan group B (`FAN_PTR_TABLE_OFFSET / 4 = 21`, `src/pitch.c:527-532`)
and walks those two groups to recolour the fans into the two playing kits, with
`_fansSidesSwapped` deciding which stand gets which. One row of the map doing two
jobs — the same overlay economy as the adjacent-globals patterns in CLAUDE.md.

So in the **current** format the row is load-bearing twice over: remove it and
every row below shifts up one (a bug we have already had once — it misaligned
overlay sprites against pitch content) *and* the standing halves lose their only
reference, so the fans never get recoloured.

**In v2 it should go, by being replaced rather than deleted.** "The 21 tiles of
the top stand" and "the 21 tiles of the bottom stand" are exactly what the
`crowd-sit`/`crowd-stand` layers already say, split by row band — so a v2 loader
derives the two recolour groups from the layers and the magic row has no job
left. Until the engine actually loads v2, it stays. The exporter therefore keeps
row 0's tiles in the `pitch` layer for the old loader, but **does not put them on
the crowd layers**, where they would be 22 spectators nobody can see.

### 8.2 What the export then found: pitch6 has two different stands

Doing this surfaced something the hard-coded rule has been hiding. **DOS pitch6
does not obey `1..22` at all.** Its patterns 3, 4 and 10..21 are ordinary grass
in the middle of the field, and its stands are tiled from two disjoint sets —
**top 0..9, bottom 202..210** — with the sit/stand pairs at (1,2) (5,6) (7,8) up
top and (203,204) (205,206) (207,208) (209,210) at the bottom. It is the only
pitch whose two ends are drawn with different tiles, and **no single contiguous
range can describe it.**

Which is itself an argument for this format: a crowd *layer* just enumerates
cells, so it does not care whether the two stands share tiles.

Two consequences, one of them possibly a live bug:

- **The exporter keeps the engine's rule as its default** (`1..22`, plus a
  restriction to the stand bands so a stray index can never scatter "crowd"
  across the field), so a default export reproduces exactly the crowd the game
  animates today. `-derivefans` infers the set instead — patterns used only in
  the stands that pair off odd/even, which reproduces `1..22` on pitches 1-5 and
  finds pitch6's two sets — but it also picks up the hoarding and barrier rows
  the engine never touches, so it stays opt-in. Pitch6 exports with a warning
  rather than a confident wrong answer.
- **Does DOS-graphics mode animate pitch 6's crowd correctly today?** The crowd
  tests the tile index at the picked cell against `1..22`
  (`animated_patterns.c`), and `DosGfx_ParsePitchMap` hands it DOS pattern
  indices unremapped (`src/dos_graphics.c:154-166`). On DOS pitch6 that test is
  simply wrong — most of its fans are outside the range and some of its grass is
  inside it. Worth looking at on its own, before v2 inherits the question.

---

## 9. Phases

| phase | what | done when |
|---|---|---|
| **0** | Easing in fixed point | **done** — 31 curves, ≤0.03% vs float, compiles for m68k |
| **1** | SwosGfx: multi-layer writer + crowd split on the engine's pattern index | **done** — 42/42 export with `pitch` / `crowd-sit` / `crowd-stand`, crowd confined to the stands, both states present, pitch layer holed to match; pitch6 flagged (§8.1) |
| **2** | Loader + binary emitter: read the `.tmj`, bake `.PCH`, render the base layer through the existing path | a v2 pitch1-normal is pixel-identical to the `.MAP` one |
| **3** | Crowd driven from the two layers instead of `MAX_FAN_TILE_INDEX` | `replay_v2_test.sh --all`, `netplay_test.sh --all`, `rollback_test.sh --all` unchanged — the RNG stream must not move |
| **4** | The animation runtime: `blit` and `shift` tracks, triggers, VIEW tier | a beach pitch with surf and birds |
| **5** | `fade`, over-sprite layers | needs §7.1 and §7.2 — or the GPU path |

Phase 3 is the one to be careful with: it changes nothing visible and must prove
it, because the crowd is in the simulation's RNG stream.

---

## 9a. The palette

Layers, and the crowd on its own layer, both run into the same wall: which
palette entries may a pitch use, and which belong to the sprites? That is
`docs/PALETTE_V2.md` — **two banks selected per pixel, with only the 16-entry kit
block shared** — and it is what makes a per-layer palette possible at all. The
crowd's tie to the sprite palette is exactly the kit block: fan tiles are pitch
art wearing the teams' shirts.

## 10. Open questions

- **Do hidden frame layers cost tiles in the sheet?** They must, since their
  pixels have to exist — but then a busy beach pitch's sheet outgrows 256 tiles
  and the `u16` cell becomes load-bearing rather than generous. Worth deciding
  before phase 2 freezes the binary.
- **Does a v2 pitch replace `SWCPICHn.MAP` or sit beside it?** Beside it, at
  first — the six stock pitches must keep loading from the existing files while
  v2 is proven, and `src/resload.c`'s tier search already gives us a clean way to
  prefer one.
- **Nothing consumes the animation schema yet**, which is the point at which a
  format is cheapest to change. §2.2 is settled enough to build the emitter
  against; expect the first real pitch to argue with one of its defaults, and
  change it then rather than guessing more now.
- **Per-tile animation, Tiled's own kind?** Tiled supports an `animation` array
  on a tile in the tileset (flipbook, fixed ms per frame). It is tempting for
  flags and water, but its timing is in milliseconds and its frames are a fixed
  cycle — no easing, no trigger, no tween. Probably worth supporting as a
  convenience *and* converting to frames at bake time, but it is not the system.
- **Replays.** A v2 animation is VIEW tier, so a recording does not carry it and
  playback re-derives it from the frame number — which is right, and means a
  recorded match on a v2 pitch must produce the same animation state at frame N
  every time it is played. Any use of wall-clock time breaks that.
