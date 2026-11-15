# PITCH_MAPS.md — the pitch's picture from a Tile Agent map

The **pitchmaps** mod (OPTIONS -> MODS -> PITCH MAPS; canonical, off by default;
`src/mod_pitchmaps.c`, `bin/mods/pitchmaps`) draws a DOS-graphics pitch from a
[Tile Agent](../third_party/tileagent/OPENSWOS.md) map instead of the game's
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
| the code | `src/mod_pitchmaps.c` (the mod; the engine's hooks in `src/pitch.c`); the runtime in `third_party/tileagent`, compiled by `src/tileagent_impl.c` |

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
`adverts`, Tile Agent's docs/SWOS_UNITED.md §3.4–3.8); a map without one leaves that to the
engine.

**Where a map is found:** `grafs_dos/pitch<N>.tmj`, then `grafs_dos/pitch.tmj`, through
`swos_data_path`: each enabled mod's folder first (a pitch a player made and shipped as a mod),
then the assets folder, then beside the game. Its images are read from the folder it was found
in. (Before 2026-11-14 it was read from the game's own `grafs_dos/` only, so a map in the assets
folder or a mod was never seen, and a mod's map took its images from the game's folder.)

**No map ships.** The game's pitches as a map are cut from the game's own art, so they are a
developer's (Tile Agent's `examples/swos-pitch.tmj` and its BMPs, copied into a `grafs_dos/`);
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

## What is left for the map to take over

The crowd. Both rows below change what the simulation holds (which fan stands is picked from
the match's `Rand()`, and the slot table is rolled back and recorded). They need the Tile Agent
player's state in the rollback registry, and both netplay peers playing the same map: as a mod
whose `"changes"` is `data`, which the handshake already compares (`Mods_OnlineSignature`).

| | today | from the map |
|---|---|---|
| the crowd's rates and rhythms | `DrawAnimatedPatterns`' `d6`/`d0` per state | swap tracks keyed on `match-start`, `play-on`, `stoppage`, `goal-top`/`goal-bottom` |
| which cells are fans | tile indices 1..22 in the game's bank | the cells where both layers of a swap pair have a tile |
