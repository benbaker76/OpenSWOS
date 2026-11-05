# OpenSWOS for PortMaster

A PortMaster port for aarch64 handhelds. Tested on an Anbernic RG35XX Plus and a
TrimUI Brick, both on Knulli. The launcher also has a fallback for stock TrimUI
firmware, which has only been tested by forcing it on the RG35XX Plus.

No data from the original game is included. You need your own copy.

## Build

On an aarch64 Linux machine with `libsdl2-dev`:

```bash
scripts/build_portmaster.sh     # writes dist/portmaster/swos.zip
```

`--no-build` packages the existing `bin/SWOS.aarch64`. Only files tracked by git are
taken from `bin/`, so game data in a local `bin/` cannot reach the zip.

## Install

**The easy way:** run OpenSWOS Setup on a PC or Mac, tick **Handheld**, and follow it. Its last
screen finds the handheld's SD card by its PortMaster `ports` folder (`ports/` on ArkOS,
ROCKNIX and AmberELEC, `roms/ports` on Knulli, `Data/ports` on stock TrimUI) and copies the
port (`portmaster/swos.zip`, beside the setup in its zip), the game's data and the options you
chose onto it. A World 2026-27 already on the card stays. By hand:

1. Copy `dist/portmaster/swos.zip` into PortMaster's `autoinstall` folder, then open PortMaster:

   | Firmware | Folder |
   |---|---|
   | Knulli | `/userdata/roms/ports/autoinstall` |
   | Stock TrimUI | `/mnt/SDCARD/Apps/PortMaster/PortMaster/autoinstall` |

2. Make the game data on a PC or Mac: run OpenSWOS Setup with the GOG PC version, the
   Amiga `.adf` floppies, or both ([INSTALL.md](../../INSTALL.md)). With both, you get
   the Amiga's music, sound and loading screens as well.

3. Copy the *contents* of the folder Setup filled into `ports/swos/assets/`, so that
   `ASSETS.ini` sits directly in it:

   | Setup ran on | Folder it filled |
   |---|---|
   | Windows | `Documents\SWOS\assets` |
   | macOS | `~/Library/Application Support/SWOS/assets` |
   | Linux | `~/.swos/assets` |

   `ports` is `/userdata/roms/ports` on Knulli and `/mnt/SDCARD/Data/ports` on stock
   TrimUI. Refresh the game list if the game does not appear.

## Controls

| Button | Action |
|---|---|
| D-pad or left stick | Move |
| A / B | Fire (B also skips) |
| Start | Pause |
| Select | Statistics |
| Y | Instant replay |
| X | Replay after the match |
| L2 | ESC (quit the match) |
| Select + Start | Quit the port |

## Settings

Settings and saves are in `ports/swos/conf/.swos/`, the log in `ports/swos/log.txt`.

The port defaults to the Amiga style, at 50 Hz. The DOS style paces at 70 Hz, which a
60 Hz panel would hold back to 60, about 14% slow, so the launcher sets
`SWOS_FRAME_SKIP=1`: the game keeps its full rate and shows 6 frames in every 7.
