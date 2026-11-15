# The Tile Agent runtime in OpenSWOS

Loads a Tile Agent pitch map (`.tmj`), runs it and draws its layers and blocks (the pitchmaps mod,
`src/mod_pitchmaps.c`, docs/PITCH_MAPS.md). SDL builds only.

| | |
|---|---|
| upstream | Tile Agent, the owner's own tile map editor — `D:\Projects\GitHub\TileAgent`, `runtime/` |
| taken | 2026-11-14, `benbaker76/TileAgent` main @ `8a5e1e9` ("Named points": object layers, `ta_player_object`, `ta_property_find`); before that 2026-10-02 (layer groups, each layer's `tileset`). Its own tests run the C runtime against the editor's player on Linux gcc and MSVC, clean under ASan/UBSan |
| files | `tileagent.c`, `tileagent.h` — **unmodified** |
| format | `TA_FORMAT_VERSION` 2 (its `docs/FORMAT.md`) |

It is compiled once, by `src/tileagent_impl.c`, which defines `TA_EASE` to the engine's
`Ease()` (`src/easing.c`) so the two cannot drift apart. One C99 file: integer only, no
64-bit division, and from the C library only `memcpy`, `memset`, `memcmp`, `strcmp`,
`strlen`, `malloc` and `free`.

**To update:** copy the two files over these, rebuild, and run the check in
docs/PITCH_MAPS.md ("Checking it") for all six pitches. The map and its images in
`bin/grafs_dos/` come from Tile Agent's `examples/` (`swos-pitch.tmj` is `pitch.tmj` here)
and are regenerated there by `scripts/make-swos-example.py`; take them together with the
runtime, since a newer map may use a newer format.
