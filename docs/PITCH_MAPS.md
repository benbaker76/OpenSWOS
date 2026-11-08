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

## What is left for the map to take over

Each is in Tile Agent's `docs/OPENSWOS.md` §3, with the runtime call that replaces the
engine's code. All of them change what the simulation holds or when it draws, so they are
not stage 1:

| | today | from the map |
|---|---|---|
| the crowd's rates and rhythms | `DrawAnimatedPatterns`' `d6`/`d0` per state | swap tracks keyed on `match-start`, `play-on`, `stoppage`, `goal-top`/`goal-bottom` |
| which cells are fans | tile indices 1..22 in the game's bank | the cells where both layers of a swap pair have a tile |
| the adverts | `ManageAdvertisements` + three sprites | a masked block on the `adverts` layer |
| the corner flags | `UpdateCornerFlagSprite` | a four-cel flipbook on `flags` |
| the goals | two sprites | blocks on `goals` (and, later, animated ones) |
| layers over the players | none | `depth` above — the example's `clouds` |

The first two need the Tile Agent player's state in the rollback registry and a way for
two netplay peers to agree that both play from the map.
