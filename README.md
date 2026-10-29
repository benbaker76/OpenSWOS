<p align="center"><img src="docs/images/openswos-logo.png" alt="OpenSWOS" width="420"></p>

# OpenSWOS

***Sensible World of Soccer*, rebuilt in C from the original Amiga game.** Same game,
same feel, now on **Windows (10 or later), macOS, Linux, the Raspberry Pi** and the **Amiga 1200**,
with **online multiplayer**.

<p align="center"><b><a href="https://baker76.itch.io/openswos">Download OpenSWOS on itch.io</a></b></p>

## Getting started

**Official downloads: [baker76.itch.io/openswos](https://baker76.itch.io/openswos), linked
from [openswos.net](https://openswos.net).** Nowhere else is OpenSWOS's own.

1. **[Download it](https://baker76.itch.io/openswos)** for your computer.
2. **[Install it](INSTALL.md)**: a step-by-step guide with pictures.

OpenSWOS needs **your own copy of *Sensible World of Soccer 96/97*** (the PC version from
[GOG](https://www.gog.com/en/game/sensible_world_of_soccer_9697) is the easiest). The
game's graphics, music and teams aren't included: OpenSWOS Setup makes them from your
copy, once, and downloads nothing. [INSTALL.md](INSTALL.md) shows you how.

## Screenshots

| | |
|---|---|
| ![A goal from the stand camera](docs/images/screenshots/01_match_stand_goal.png) | ![Widescreen (16:9)](docs/images/screenshots/02_match_widescreen.png) |
| A match from the stand | Widescreen (16:9) |
| ![Match](docs/images/screenshots/03_match_stand.png) | ![Line-up](docs/images/screenshots/04_lineup.png) |
| In play | Team line-up |
| ![Team select](docs/images/screenshots/05_team_select.png) | ![Countries](docs/images/screenshots/06_countries.png) |
| Choosing teams | Picking a country |
| ![PC (DOS) graphics](docs/images/screenshots/07_dos_graphics.png) | ![Atari Jaguar sprites](docs/images/screenshots/08_jaguar_sprites.png) |
| PC (DOS) graphics | Atari Jaguar sprites |
| ![Credits](docs/images/screenshots/09_credits.png) | ![Amiga pitch](docs/images/screenshots/10_match_amiga_pitch.png) |
| The credits | The Amiga pitch |
| ![Amiga 1200 (AGA)](docs/images/screenshots/11_amiga_aga_match.png) | ![Amiga 1200 (AGA)](docs/images/screenshots/12_amiga_aga_corner.png) |
| The Amiga 1200 (AGA) build in FS-UAE | The Amiga 1200 (AGA) build in FS-UAE |

## What's in it

- **The whole game:** friendlies, leagues, cups, the career mode with transfers and
  management.
- **Online multiplayer:** play a friend over the internet (MULTIPLAYER on the main menu).
- **[Challenge collection](docs/CHALLENGE_COLLECTION.md):** six predefined challenges
  on SDL and Amiga, rated 1-5 stars, with automatic completion and saved
  progress. Press C during a match to create personal saved positions too.
- **Widescreen** at any resolution, fullscreen, CRT effects, gamepads, two players on one
  computer.
- **The PC and Amiga versions' looks and sounds**, and optionally the Atari Jaguar's
  sprites and SWOS 2020's pitches.
- **A real Amiga version** for the A1200/A4000 (AGA), built from the same source code.
- An **arcade (coin-op) mode** for cabinets.

## Documentation

| | |
|---|---|
| [INSTALL.md](INSTALL.md) | Installing and setting up, step by step |
| [docs/PLAYING.md](docs/PLAYING.md) | Keys, gamepads, two players, arcade mode |
| [docs/CHALLENGE.md](docs/CHALLENGE.md) | Saved positions, bundled challenges, compatibility and implementation |
| [docs/CHALLENGE_COLLECTION.md](docs/CHALLENGE_COLLECTION.md) | Collection UI, completion, persistent progress, retries and regeneration |
| [docs/SETTINGS.md](docs/SETTINGS.md) | `SWOS.ini`, the command line, display and CRT options, where saves go |
| [docs/BUILDING.md](docs/BUILDING.md) | Building from source, for every platform |
| [docs/DEVELOPERS.md](docs/DEVELOPERS.md) | How the code is laid out, and where it came from |
| [docs/INDEX.md](docs/INDEX.md) | Every other document (netplay, replays, formats, the Amiga, ...) |

## License

Copyright (C) 2025-2026 Ben Baker and the OpenSWOS contributors.

OpenSWOS's own source code is free software: you can redistribute it and/or modify it
under the terms of the **GNU General Public License, version 3 or (at your option) any
later version** (GPL-3.0-or-later). See [LICENSE](LICENSE). In short: anyone who
distributes a build of OpenSWOS, changed or not, must make its complete source code
available under the same license.

It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

The license does **not** cover:

- ***Sensible World of Soccer* itself.** The game and its graphics, sound, music, data
  files and code are © Sensible Software and their current rights holders, and nothing
  here grants any right to them. OpenSWOS is a fan project and is not affiliated with
  Sensible Software or its current rights holders.
- **Third-party code in `third_party/`**, which keeps its own license (see each folder):
  GGPO-X, ENet, miniz, SimpleMatchmaker and opl_models (MIT), Opal (public domain),
  dr_mp3 (public domain or MIT-0), and Tile Agent's runtime.

## Support development

If you enjoy OpenSWOS and want to support its development:

- Patreon: <https://patreon.com/Baker76>
- Ko-fi: <https://ko-fi.com/benbaker76>
- Buy Me a Coffee: <https://buymeacoffee.com/benbaker76>
- GitHub Sponsors: <https://github.com/sponsors/benbaker76>
- PayPal: <https://paypal.me/benbaker1976>

## Credits

```
Sensible World of Soccer

Developer                      Sensible Software
Publisher                      Renegade
Producer                       Graeme Boxall
Programmer / Coder             Chris Chapman
Graphics                       Jon Hare, Stoo Cambridge
Music & Sound                  Alex Joseph, Graham King, Jason Page,
                               Jon Hare, Richard Joseph

OpenSWOS

Port to C                      benbaker76
Reverse Engineering            benbaker76 (Amiga) / zkz (DOS)
AGA                            benbaker76
Multiplayer (GGPO-X)           thd79
Server Help                    Redhair
Test Lead                      Playaveli
Beta Testers                   crom1, ElMichaJ, EMPI, gatifun, petarku,
                               Predicador, Redhair, segra, starwindz, thd79
Donators                       benbaker76, Playaveli, starwindz, Petarku,
                               gatifun, burellapaolo, az0man0,
                               Cartoplotus68, JoSon, aliko
```

- **Ben Baker** ([github.com/benbaker76](https://github.com/benbaker76)) —
  reverse-engineered the SWOS Amiga 68k assembly in
  **[SWOS-RE](https://github.com/benbaker76/SWOS-RE)**, which this C port grew
  out of, and author of the port (originally two trees, `SWOS_C` for Amiga and
  `SWOS_C_SDL` for SDL, merged here into one).
- **Zlatko Karakaš** ([github.com/zlatkok](https://github.com/zlatkok/)) —
  reverse-engineered the original DOS version.
- **Richard Joseph** — RJP1 audio engine.
- **gabomdq** & contributors — SDL_GameControllerDB.
