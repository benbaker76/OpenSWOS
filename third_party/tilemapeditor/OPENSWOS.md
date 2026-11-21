# The Tile Map Editor runtime in OpenSWOS

Loads a Tile Map Editor pitch map (`.tmj`), runs it and draws its layers and blocks (the pitchmaps
mod, `src/mod_pitchmaps.c`, docs/PITCH_MAPS.md). SDL builds only.

| | |
|---|---|
| upstream | Tile Map Editor (Tile Agent until 2026-11-20), the owner's own tile map editor — `benbaker76/TileMapEditor` (`D:\Projects\GitHub\TileAgent`), `runtime/` |
| taken | 2026-11-20, `benbaker76/TileMapEditor` main @ `c5244e4` (the rename, b749709: the map's object `"tileagent"` became `"tilemapeditor"`, a package's format `tileagent-package` became `tilemapeditor-package`; and a map's `uuid` and `info`); before that 2026-11-14 @ `8a5e1e9` ("Named points"), 2026-10-02 (layer groups, each layer's `tileset`). Its own tests run the C runtime against the editor's player on Linux gcc and MSVC, clean under ASan/UBSan |
| files | `tilemapeditor.c`, `tilemapeditor.h` — **unmodified** |
| format | `TA_FORMAT_VERSION` 2 (its `docs/FORMAT.md`) |

It is compiled once, by `src/tilemapeditor_impl.c`, which defines `TA_EASE` to the engine's
`Ease()` (`src/easing.c`) so the two cannot drift apart. One C99 file: integer only, no
64-bit division, and from the C library only `memcpy`, `memset`, `memcmp`, `strcmp`,
`strlen`, `malloc` and `free`.

**To update:** copy the two files over these, rebuild, and run the check
in docs/PITCH_MAPS.md ("Checking it") for all six pitches. The map and its images in
`bin/grafs_dos/` come from Tile Map Editor's `examples/` (`swos-pitch.tmj` is `pitch.tmj` here)
and are regenerated there by `scripts/make-swos-example.py`; take them together with the
runtime, since a newer map may use a newer format.
