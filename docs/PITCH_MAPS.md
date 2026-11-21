# PITCH_MAPS.md — the pitch's picture from a Tile Map Editor map

The **pitchmaps** mod (OPTIONS -> MODS -> PITCH MAPS; canonical, off by default;
`src/mod_pitchmaps.c`, `bin/mods/pitchmaps`) draws a DOS-graphics pitch from a
[Tile Map Editor](../third_party/tilemapeditor/OPENSWOS.md) map instead of the game's
`PITCHn.DAT`/`.BLK`: `<mod>/grafs_dos/pitch<N>.tmj` (or `pitch.tmj`) in the mod's own folder,
else the graphics folder's (`bin/grafs_dos/pitch.tmj`). Until 2026-11-07 it was the setting
`[graphics] pitch_maps` / `--pitch-maps=on`; now the engine has only hooks (`ModDef`
`pitchLoaded`, `pitchColoured`, `pitchDropped`, `pitchTiles`, `pitchCondition`), so another
pitch format could be a mod beside it. With the map that ships, the picture is the game's
own, entry for entry; the point is that the map can be opened in the editor and changed.

This is **stage 1: the ground**. The crowd's behaviour, the adverts, the corner flags and
the goals are still the engine's code; what the map could take over next is at the end.

## What it does

| | |
|---|---|
| applies to | `[graphics] style = dos` (`grafs_dos`), pitches 1–6. Not the Amiga tiers, not the arcade's themed pitch |
| the map | `bin/grafs_dos/pitch.tmj`: ONE map of all six pitches, whose `layout-<N>` event shows pitch N's layers. A `pitch<N>.tmj` beside it, if there is one, is used for that pitch instead |
| its images | `swos-grass.bmp`, `swos-field.bmp`, `swos-fence.bmp`, `swos-crowd.bmp`, `swos-touch-ups.bmp` (and the adverts, flag, goals and clouds sheets, unused in stage 1): 8-bit BMPs in the game's palette |
| no map, or one that will not load | the game's own files, with a line in the log saying why |
| the code | `src/mod_pitchmaps.c` (the mod; the engine's hooks in `src/pitch.c`); the runtime in `third_party/tilemapeditor`, compiled by `src/tilemapeditor_impl.c` |

At kick-off each of the 2310 cells is **baked** into a 16 × 16 tile: every tile layer the
layout shows that is drawn under the sprites (`depth` below), bottom first, skipping the
map's transparent entry — for the game's pitches that is grass, field, fence (the
hoardings and stands), the seated crowd and that pitch's touch-ups. A cell where a layer with a tile-swap track and the layer it
swaps in both have a tile is a **fan**, and is baked a second time with the swapped-in
tile: the fan standing. `DrawChunkyPitch` and the crowd's redraw then show the baked tile
for a cell instead of the bank's.

The pitch condition (frozen … hard) takes its nine palette entries from the map's
`pitch-<condition>` event, through the same six bits as the engine's own table.

## The match is not played from the map

The crowd is simulation here: which fan gets up is picked from the match's `Rand()`, and
the slot table that holds it is hashed, rolled back and recorded (docs/ROLLBACK.md). Two
netplay peers must hold the same table whatever each has set, and the handshake's rules
byte has no bit left to make them agree on this setting. So:

- the engine still loads `PITCHn.DAT`/`.BLK` and runs the crowd on them exactly as before;
- the map only answers *what a cell looks like*: where the engine's tile for a cell is a
  fan's standing variant (an even tile index up to `MAX_FAN_TILE_INDEX`), the baked
  standing tile is shown; everywhere else the ordinary one;
- the fans' kits (palette entries 10 and 11) are copied from what the engine gave its own
  tile for that cell (`Pitch_FanTileKit`).

A match, a replay, a recording and a netplay peer are therefore bit for bit the same with
the setting on or off. The cost: a map that puts fans where the game has none will show
them seated for ever, and where the game has fans and the map none, nothing moves.

## Checking it

`SWOS_PITCH_MAP_CHECK=1` compares, at every kick-off, each baked cell with the engine's
own recoloured tile, each animated fan's standing tile with the engine's, and all seven
conditions' palette entries with the engine's table. `SWOS_PITCH_NUMBER=1..6` pins the
pitch drawn (a highlight's too, as does `SWOS_PITCH_TYPE`), so one recording walks all six:

```bash
cd bin
for n in 1 2 3 4 5 6; do
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SWOS_PITCH_MAP_CHECK=1 SWOS_PITCH_NUMBER=$n \
    ./SWOS.x86_64 --dos-graphics=on --mods=pitchmaps --view-replay=<a recording>.SRP 2>&1 |
    grep 'pitch-map'
done
```

2026-10-01, with the map as shipped: `0 of 2310 cells differ` and `0 animated fans'
standing tiles differ` for pitches 1–5 (and again 2026-11-07 through the mod: 0 of 2310 for 1–6), 0 channels of 27 for each of the seven conditions,
with the Jaguar sprites on and off. Pitch 6 is the training ground: 0 of 2310 cells
differ; it has no fans in the map, and the engine animates none in a training game. (Its
tiles 1..22 are ordinary ground that the engine WOULD toggle if a real match were played
there; the map would not.)

Since the example map left row 0 empty (Tile Map Editor d1c1403; row 0 is never on screen, see
"Row 0" below), the check compares the 2268 cells of rows 1-54 and says `0 of 2268`. Again
2026-11-20 with the Tile Map Editor runtime and map (c5244e4): 0 of 2268 cells, 0 fans' standing
tiles and 0 channels, all six pitches.

## Stage 2: over the players, among the sprites, the benches (2026-11-14)

Everything here is **drawing only**. The engine still decides when and where: its sprites,
its adverts' dice, its camera and its substitutions are match state and stay exactly as they
are. A match, a replay and a netplay peer are the same with the mod on or off (the replay rig,
`check_netplay_compat.sh`). The map's own time is the game's frame counter (`_frameCount`,
unwrapped), so animations that run "always" are in step with the engine's.

| | how | the map's |
|---|---|---|
| **layers over the players** | `frameDrawn`: each `depth: above` layer drawn from the camera into a buffer of the picture's size (`ta_draw_layer32`, solid) and blended over the frame at the layer's opacity (`ta_player_layer_pose`) through gfx.h, on either presenter, over the match and its replays | the example's `clouds` (0.2, drifting, its own wind each match): the `clouds` event on a frozen, muddy, wet or soft pitch, `clear-sky` on a normal, dry or hard one, to go with the Weather mod's rain and snow |
| **the goals' fronts** | `spriteDraw` / `replaySpriteDraw`: where the engine draws sprite 1091 or 1092, the nearest block of the `goals` layer is drawn instead, in the same place in the drawing order (the net over a ball that crossed the line) | `top goal`, `bottom goal` |
| **the corner flags** | the same, for 1070–1073: the block of `flags` nearest the engine's flag | `wave`, four cels on the engine's beat |
| **the adverts** | the mod follows the engine's `_advertIndex`: `advert-N` when it starts one, with the head start that puts the map's 8-frame steps on the engine's (`((frameCount − 1) & 7) + 1`), `adverts-off` when it stops; where the engine draws its eight strips (113–136), the `adverts` block is drawn once. Live only: a replay keeps its recorded strips | `advert-1..3`, the masked block |
| **the benches** | `benchPoint` (bench_subs.c `bench_geom`): the coach, the seats and the selection arrow drawn from the map's point and layout; the bench camera, when the bench shows (`_benchXLimit + 8`) and a substitute's walk-on stay the engine's | `bench-top`, `bench-bottom` on the topmost shown object layer (the training ground's layout its own: both at 27,456); `swos:seat-step`, `swos:column-seats`, `swos:column-step`, `swos:column-dy`, `swos:arrow-dx` |

The engine's hooks for these (include/mod_api.h): `frameDrawn`, `spritesBegin`, `spriteDraw`,
`replayFrameBegin`, `replaySpriteDraw` (new: a replay's sprite, which a mod may leave out and
draw its own) and `benchPoint` (new). A layer's name says what it stands for (`goals`, `flags`,
`adverts`, Tile Map Editor's docs/SWOS_UNITED.md §3.4–3.8); a map without one leaves that to the
engine.

**Where a map is found** (2026-11-16): the pitchmaps mod's **Pitch map** setting (`[mod.pitchmaps]
map`) names one by its **uuid**. It is the folder `<player folder>/moddata/pitchmaps/<uuid>/`, where
the DLC Manager's **Pitch Maps** install them (else `pitchmaps/<uuid>/` in the mod's own folder or
an enabled mod's): a **Tile Map Editor package** (Tile Map Editor's own,
generic "Export As Zip" -- nothing in it is OpenSWOS's; we read its format): `package.json`
(`"format": "tilemapeditor-package"`, `uuid`, `name`, `author`, `version`, `about`, `"map"`: the .tmj,
`"preview"`: a thumbnail the DLC Manager shows) beside the map and its images. The game opens the
map `"map"` names (one map of all six pitches and every condition), and shows `"name"` in the
setting. Without a package.json, the folder's `pitch<N>.tmj`, else `pitch.tmj`. One map is chosen at a time: the setting, not a slot. A
pitch map's uuid is its identity: Tile Map Editor gives a new map one and keeps it through every edit, a
newer version replaces the folder in place, and a map made into another pitch gets a new uuid.

With the setting off, a developer's map is found as before: `grafs_dos/pitch<N>.tmj`, then
`grafs_dos/pitch.tmj`, through `swos_data_path` (each enabled mod, the assets folder, beside the
game), its images from the folder it was found in.

**No map ships.** The game's pitches as a map are cut from the game's own art, so they are a
developer's (Tile Map Editor's `examples/swos-pitch.tmj` and its BMPs, copied into a `grafs_dos/`);
pitches made in the format come from players, as mods.

**Checked** with that example in a scratch assets folder (`SWOS_PITCH_MAP_CHECK=1`, PC graphics):
- A corpus match (`tests/dos/2xcpu-08030320`, `--replay-test`): each corner-flag cel, 1070–1073,
  drawn from the map is 0 of its 19–23 pixels from the engine's sprite. That holds since the map
  runs on `_frameCount`; on a clock of its own the cels were out of phase, 7–12 pixels off.
- The goals' fronts drawn from the map: 0 of 1126 (bottom) and 0 of 429 (top) pixels from the
  engine's sprites, in three corpus matches.
- The adverts with `SWOS_TEST_ADVERTS=1` (every chance shows one; the same `Rand()` drawn), which
  the corpus needs because one is rare (one 50% chance a stoppage at the top end, and five corpus
  matches lost every one): the map's `advert-2` fired on the engine's start and `adverts-off` on
  its end, and over the 640 strips drawn, 0 of 61,440 pixels differ from the engine's -- its
  scrolling, its 40-frame pause and its end, on the engine's 8-frame beat.
- The benches come from the map at the engine's values: top 27,389 and bottom 27,485.
- Clouds show on a muddy pitch with the Weather mod's rain, and a normal pitch has neither.
- The ground is unchanged: 0 of 2310 cells, 0 fans' standing tiles and 0 condition channels
  differ.

## Stage 3: the crowd, the map's own (2026-11-15)

With a map that has a crowd (a layer with a swap track: the example's `crowd-sit` /
`crowd-stand`), **the fans are the map's**: which cells are fans, how many stand, how often and
in what rhythm, all from the map, picked from **the map's own random numbers**
(`ta_player_set_auto_swaps(player, 1)`), never the match's.

The engine's crowd is **left exactly as it is**. `DrawAnimatedPatterns` still runs on the
match's `Rand()`, fills its slot table and is rolled back and hashed. It is only unseen: when it
redraws a cell, the pitch-map hook answers with the map's state for that cell. So the random
stream, a recording, a replay, a resim and two netplay peers are bit for bit what they are with
the mod off. Pitch maps stays a cosmetic mod, and two players online may even use different
pitches. The original game's crowd, pick for pick, is what plays whenever no map draws the crowd.

What the map's crowd follows is the match's **mood**, read and never changed: the engine's crowd
state (`_animPatternsState`), each change an event of the map's (Tile Map Editor's
docs/SWOS_UNITED.md §3.3):

| the engine's state | the map's event |
|---|---|
| 6 the game has not started | `match-start` |
| 7 in play | `play-on` |
| 1 the ball out of play | `stoppage` |
| 2, 3, 4, 5 a goal (entered from another state) | `goal-top` when the fans in the top stand (`_fansSidesSwapped` says whose) are the scorers', else `goal-bottom`; aged by the goal clock (`_patternsGoalCounter − 1`), in a replay by its phase (0, 24, 199) |

The seed is taken from the match as it starts (`_seed`, `_randXorIndex`, the pitch), so two
peers see the same crowd and the same clouds. Nothing requires it.

**Drawing.** The pitch is not drawn whole each frame: a fan that changes is drawn into the frame
and the mask buffer, then again next frame for the other buffer, as the engine does its own. The
mod finds each map fan whose state changed since it was last drawn and redraws it at
`spritesBegin` / `replayFrameBegin`, before the sprites, through the new
`ModApi_RedrawPitchCell` (the engine's `drawPatternFromIndex`, SDL only: no slot, no `Rand()`).
Cells scrolled into view are drawn by `DrawChunkyPitch` through the same hook, so they show the
map's state too.

**Checked:**
- `check_netplay_compat.sh` with PC graphics and the pitchmaps mod on, through `SWOS.ini` in its
  `HOME`, with the example map in the assets folder: **30 unchanged, 0 changed**, NETPLAY_COMPAT
  15. A corpus match under that `HOME` confirms the rig ran the map's crowd ("crowd the map's own
  (its random numbers, not the match's)").
- In a match's pre-kick-off view of the top stand, the stand's pixels change by 2,000–6,000 from
  one capture to the next with the camera still, and the cells are the map's sitting and standing
  fans.

## Stage 4: everything the format draws (2026-11-15)

The renderer now draws every feature of Tile Map Editor's format a pitch can use. All of it is
picture only (the match, its replays and two netplay peers are what they are without the mod):

| | how |
|---|---|
| **the ground, live** | Each frame (`spritesBegin`, `replayFrameBegin`) the cells around the camera, two cells of margin, are composed again from every visible layer under the players through the runtime's own drawing (`ta_draw_layer8`): swap tracks (the crowd's among them), layer tracks (a layer that moves, drifts or fades), blit tracks (animated blocks on the ground), and layers an event shows or hides. Each cell is treated as the bake treats it (the Jaguar tier's donated entries, the fans' kits from the cell's own engine tile). A cell that changed is drawn again into the frame and the mask buffer, now and next frame (both buffers), with `ModApi_RedrawPitchCell`. Cells scrolled into view are drawn by `DrawChunkyPitch` through `pitchTiles`. This replaced the crowd-only redraw of stage 3. |
| **the palette** | The presenter rebuilds the PC palette every frame and asks `pitchCondition` (`DosGfx_BuildPalette`, which now takes up to 256 entries). While a match or a replay shows the map, the mod adds the map's colour cycles as turned this frame and the entries of a palette variant an event put in force (`ta_player_palette`, `ta_player_palette_choice`). Every place an entry a cycle turns is used changes, as on any paletted screen. |
| **any sorted layer** | Layers whose `depth` is `sorted`, besides `goals`, `flags` and `adverts`, an author's own (a ball boy, a camera tower...): their blocks of the frame, by anchor y, are drawn among the engine's sprites. Each is drawn just before the first sprite standing lower on the pitch, all before the HUD's screen-pinned sprites, and the rest at the new `spritesEnd` hook (called after `DrawQueuedSprites` and after both replay sprite loops). |

**Not in the format, so not here:** image layers. Tile Map Editor reads them but does not draw them
(its docs/FORMAT.md: "does not understand (image layers ...)").

**A pitch map is the game's grid:** 42 x 55 cells of 16 x 16, indexed in the game's palette.
That is fixed by the match, not the renderer: the camera, the crowd's slots and every sprite's
position are in that space.

### Row 0

Row 0 (world y 0-15) is **never on screen**: the camera's top stops at y 16 (`MoveCamera`'s
clamp, `targetY <= 0x10`). The engine still uses its own `PITCHn.DAT`'s row 0, as the table of
the 42 fan tiles its recolouring starts from (`pitch.c`), but that is the engine's file, never the
map. **In a map, row 0 is unused.** The mod never composes it, and an author may leave it empty.
The row stays in the grid and is not removed from the format, so that a map's pixels stay the
engine's world pixels: the goals at 300,129, the benches at 27,389, the flags, the adverts, every
anchor and named point is where the engine's own coordinates say. A 54-row map would put all of
them 16 pixels out and need a second coordinate system for nothing (the row costs 42 cells).

**Checked** with a test copy of the example map (scratch only): a colour cycle on the grass
(entries 78-81, a step every 8 frames), a ground layer with an animated block moving 160 pixels
and back over 100 frames, and a sorted layer with a flag block on the centre circle.
- In a match, `SWOS_PITCH_MAP_SHOT` frames 40 and 80 show the grass in different colours. At
  frames 200 and 230 the block has moved about 48 pixels along the ground under the players, with
  no trail left, and the flag stands among the players.
- `SWOS_PITCH_MAP_CHECK` over the first 500 frames: 228 ground cells changed by the map, and 500
  blocks of the sorted layer drawn. The cells as first composed: 476, 0 different from the bake,
  which is itself 0 of 2310 from the engine's pitch.
- `check_netplay_compat.sh` with the mod and that map on: see the commit.

Test switches: `SWOS_PITCH_MAP_CHECK=1` (all the checks above),
`SWOS_PITCH_MAP_SHOT=<frames>[,...]` (the game's frame, 8-bit, written as `pitchmap_shot_<n>.ppm`
that many frames into the match: a picture at a known moment however long the start-up takes),
and `SWOS_TEST_ADVERTS=1`.

## What is left

- **A crowd that stays the game's.** With the map's crowd on, the game's pitches drawn from the
  example map have the same character of crowd but not the same fans, cell for cell. That is the
  price of keeping the match's random stream. A map that wants the engine's picks exactly could
  carry a property for it (the stage-2 behaviour); left out until someone asks.
- **A goal undone by a rollback** (online): the crowd follows what is drawn, so a celebration
  may start and stop. It is cosmetic, like the rest of the picture. The goal events could wait for
  `netConfirmed` at the cost of the input delay.
