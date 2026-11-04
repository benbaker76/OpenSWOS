# CLASSIC_COMPETITIONS.md — HISTORIC TOURNAMENTS (SWOS 2020's DLC cups) and the PC `.DIY` format

The menu is HISTORIC TOURNAMENTS; the code and folders still say *classic*
(`src/classic_comps.c`, `classic/`).

> Status (2026-11-04): **current**, both targets. The Amiga plays the same competitions from
> OpenSWOS's JSON `.DIY` (converted on a PC, section 6), 2026's 48 teams included.

## 1. What it is

PRESET COMPETITION's world list has a row above WORLD CUP: **HISTORIC TOURNAMENTS**. It opens

    HISTORIC TOURNAMENTS -> NATIONAL -> EURO | WORLD CUP -> the competitions in that group, by year
                         -> CLUB     ----------------------> the club competitions, by year
                         -> the teams screen -> PLAY

EXIT goes back one screen: from a list to NATIONAL's two groups, or to HISTORIC TOURNAMENTS
for CLUB, which has only the one.

The competitions are SWOS 2020's DLC cups: every Euro (1960–2024) and World Cup
(1930–2026, no 2022 in the DLC), and its club cups -- the European Cup 1991–92 and the
Champions League 1992–93 to 1995–96 (8 or 16 clubs, as they played that season). The teams
screen lists every team three across, row by row, in 100-pixel buttons as the game's own team
lists are (`chooseTeamsFriendlyMenu`), so a 16-letter name fits -- up to 48 teams; a file of
more keeps the menu's four narrower columns (`lay_out_wide` in `src/classic_comps.c`). Every
one starts as COMPUTER and a click cycles it COMPUTER -> PLAYER-COACH -> COACH, in the
friendly team list's colours (red / purple / blue). PLAY starts the competition the way LOAD
OLD COMPETITION starts a loaded one (`StartContest`), and SAVE then offers the file's own name
(`WC1990.DIY`).

Two things differ from loading the same file with LOAD OLD COMPETITION:

- **Every team starts as COMPUTER.** SWOS 2020's cups set them all to COACH (control 3); its
  challenge files use COMPUTER/PLAYER-COACH. A classic competition is chosen, not resumed.
- **The player's settings stay.** The six option words (game length, pitch type, ...) and the
  six USER tactics are kept; LOAD OLD COMPETITION takes the file's, as the original does.

On SDL, LOAD OLD COMPETITION also reads the PC game's and SWOS 2020's `.DIY` files (any `.DIY`
that is not JSON), with the file's control types, options and tactics.

## 2. Where the files live

**None are in this repository**, like the rest of the game's data: the competitions are the
player's own copy of SWOS 2020's DLC. HISTORIC TOURNAMENTS lists what it finds; with no files
a group's list is empty.

**The Amiga:** `classic/{uefa,worldcup,club}/` in the game's drawer, as OpenSWOS's JSON
`.DIY`, converted on a PC with `--convert-diy` (section 6) -- the Amiga cannot read the DOS
dump.

**The player's own (SDL):** `<assets>/classic/{uefa,worldcup,club}/`
(`<assets>` is `swos_assets_dir()`, by default `Documents\SWOS\assets`), SWOS 2020's files as
they are — `scripts/copy_classics.ps1` copies them from an install (`-From`, default
`C:\Games\SWOS 2020\dlc\cup`). A folder named for a club cup (CHAMPIONS LEAGUE, EUROPEAN CUP,
CUP WINNERS, UEFA CUP, EUROPA, CLUB) goes to `club` -- tested FIRST, since "UEFA CHAMPIONS
LEAGUE" and "UEFA EUROPEAN CUP" say UEFA and EURO too -- then WORLD CUP to `worldcup`, EURO or
UEFA to `uefa`. The club cups' `CUSTOMS.EDT` is not needed: a `.DIY` carries its teams.

SDL looks in the assets folder first, then in a `classic/` beside the binary; a file name already
listed from the first is not listed again. A file is listed by the name inside it, sorted by
the first four-digit number in that name, then by name; one the target cannot read is skipped
(SDL logs a `[classic]` line). A list holds 32 (two columns of 16); one that fits the first
column is drawn in the middle, halfway between the two (`ClassicComps_ListInit`), as CLUB's
five are; a longer one fills both columns a row at a time, left then right, so they end level
(EURO's 17: 9 and 8; `competition_of_entry`). The EURO group's folder is still `uefa/`.

## 3. The file format

A `.DIY` is the same in the PC game and SWOS 2020: a raw dump of everything from
`competitionFileBuffer` up to `numSelectedTeams`, then `numSelectedTeams`, then that many
684-byte team records — `SaveDIYFile` in both `data/SWOS2.s` and swos-port's `swos.asm`. So

    size = 5189 + 2 + numSelectedTeams * 684          (the Amiga's region is 5168)

The DOS region holds the Amiga's fields in the Amiga's order (both `ds.b`/`db` walks, checked
against the SWOS 2020 files), with these differences:

| | Amiga (`SWOS2.s`) | DOS / SWOS 2020 |
|---|---|---|
| byte order | big-endian | **little-endian** |
| competition name | 36 bytes | **35** — so a `DIYCompetition` header is 0x5F bytes, not 0x60 |
| `careerLoadedFlag` | present | missing |
| options | 6 words | 8 (`commentary`, `chairmanScenes` after `pitchType`) |
| tail | seeds, `saveFileVersion`, 3 magic words, `saveFileAmigaType` | seeds (20-byte `randomSeed`), `selTeamsPtr`, `chairmanIndex`, ... — no version or magic |

Team ids are `{file, ordinal}` byte pairs in both, so they need no swap. The file holds two
headers — the competition at 0 and, for a tournament, the current stage at 0x41F — so DOS
offsets are the Amiga's − 1 past the name, and − 2 inside the stage.

The region is the **legacy** `competitionFileBuffer` layout, which this port untangled into
`CompetitionSlotBuffer` (`include/game_types.h`). Where each part goes:

| DOS offset | what | goes to |
|---|---|---|
| 0x000 | header (type 2: the tournament's) | `tourn.header` (name re-terminated, `namePtr` = the name) |
| 0x059 | 64-byte team-index order, overlaying the header tail | `tourn.teamIndexOrder` |
| 0x099 / 0x119 / 0x159 | team ids, group map, six stage slots (0x76 each) | `tourn.teamIds` / `groupMap` / `slots` |
| 0x41F | the current stage, a `DIYCompetitionEx` image | `stageSnapshot` |
| 0x000 (type 0/1) | a `DIYCompetitionEx` image | the buffer as a `DIYCompetitionEx` |
| image + 0x05F | the body, `minSubstitutes` .. `bracketScratch` | the same fields |
| image + 0x2B3 | 64 standings rows (18 bytes), overlaying the cup data | `leagueStandings` — league stages only |
| 0xB5E | USER_A .. USER_F tactics, 370 bytes each | the USER tactics (not for HISTORIC TOURNAMENTS) |
| 0x140A | the six option words | the options (not for HISTORIC TOURNAMENTS) |
| 5191 | the teams | `_selectedTeams` |

A tournament's stage is a league when its slot (`header.roundComplete`) has
`isLeagueFormat != 0`. Team selections that were byte offsets from the teams array (the
standings rows' `teamSelOffset`, the cup bracket's `teamOffsetTable`) become the team's index,
as `diy_team_to_identity` defines them.

## 4. The code

| file | what |
|---|---|
| `src/diy_import_dos.c` | SDL only. `DiyImportDos_IsDosFile` (size/type/team-count check), `DiyImportDos_LoadData` (section 3; `keepSettings` for HISTORIC TOURNAMENTS), `DiyImportDos_Load` (a save-folder file), and `--convert-diy` (section 6). The copy is by blocks, right only where DOS words are host-native. |
| `src/classic_comps.c` | Both targets. The folder scan (`dirent` on SDL, `Resload_ListDir` on the Amiga), the load (JSON through `SaveJson_LoadDIYData` with the player's options and tactics put back; a DOS file through the importer, SDL), the screens' handlers, `ClassicComps_TakeReady`. |
| `src/menu_data.c` | `_classicCompsMenu` (NATIONAL, CLUB), `_classicNationalMenu` (EURO, WORLD CUP; the same `Menu_ClassicComps` shape), `_classicListMenu`, `_classicTeamsMenu`: classic packed menus like every other, with their JSON in `bin/data_json/menus/classic*.json` (`[menu-json] verify` 51/51), `s_staticMenus`, the symbol registry. The teams menu lists its 64 buttons a column at a time; `lay_out_wide` re-lays them three across for up to 48 teams, and `team_of_entry` maps a button to its team. |
| `src/teams.c` | `SelectTeamsFinalMenu`: the HISTORIC TOURNAMENTS row (a marker in `leaguesPtrTable`, caught before anything reads the slot as a `Competition`), world view of the preset chooser only. |
| `src/menus.c` | `PresetCompetitionMenu`: after the chooser, a loaded classic competition is started. |
| `src/file_io.c` | `LoadDIYFile` (SDL): JSON as before, anything else through the importer. |
| `src/save_format_json.c` | `SaveJson_LoadDIYData`: a `.DIY`'s JSON from memory (`load_competition_mem`), since `classic/` is not the save folder, loaded whatever world it was converted in (section 6). |
| `src/swos_io.c` | `swos_io_load_data_alloc`: a file under the data root rather than `save/`. |
| `src/resload.c` | `Resload_ListDir`; and `fix_filename` leaves a name that has a folder in it alone, so `classic/uefa/EC1964.DIY` is not sent to `save/` for its `.DIY`. |

## 5. Known limits

- **2026 is six groups of eight.** The group layout is SWOS 2020's file, not the
  import: 6 groups x 8 (7 matches each), 32 through to a round of 32 — the real format (12
  groups of four, best thirds) needs more groups than SWOS's DIY tournament offers.
- **ESC on the teams screen** was seen to start the competition, as PLAY does, instead of
  going back like EXIT (SDL, 2026-10-29, not looked into yet).
- **LOAD OLD COMPETITION's list** was made for 8-character DOS names: longer ones (SWOS 2020's
  challenge files) run into the type column. Rename, or shorten them in the list.
- **Unexplained:** a SWOS 2020 cup loaded through LOAD OLD COMPETITION (every team COACH), one
  team switched to PLAYER-COACH on the PLAY MATCH screen: the next PLAY MATCH screen took no
  input (2026-10-29; the main thread was in the normal `MenuProc` loop, no `[menu-nav]` line).
  HISTORIC TOURNAMENTS avoids it by starting every team as COMPUTER; the cause is not found.
- An early report of VIEW RESULT freezing (EC1964, LOAD OLD COMPETITION) was never reproduced;
  EC1964 played to the final through VIEW RESULT on the Amiga.

## 6. The Amiga

The Amiga cannot read the DOS dump (its words are little-endian, section 3), so it plays the
same competitions from **OpenSWOS's JSON `.DIY`**, which is built to load on it
(`save_format_json.c`'s cross-target slot and stage serializers). They are made on a PC:

    SWOS.exe --convert-diy=<in.DIY>,<out.DIY>

imports the file, makes every team COMPUTER, writes the JSON (and, in a tester build, reloads
and diffs it: `[json-verify-diy] 0 mismatches` for all 44), and refuses one of more than 64
teams, a DIY competition's own limit (exit 2). Until 2026-10-29 it refused more than 32, a
caution nothing on the Amiga needed: every structure the competition uses is sized for 64.
An Amiga drawer's `classic/` is its output.

**Any world.** The JSON names the world the converting PC was playing (`"world"`), and a
save of another world is refused (`save_world_ok`). A classic competition is not: a `.DIY`
carries every team it plays, nothing in it comes from the world's database, so
`SaveJson_LoadDIYData` skips the check. Without that, a drawer on 96/97 refused every file
converted on a PC playing World 2026-27. LOAD OLD COMPETITION still checks it.

**Loading time.** A competition loads in one pass over its JSON (`capture_object_keys`, and
the team array walked once). Before that, every field the loader looked up re-parsed the
whole file through mjson, and the team walk re-parsed the team array once per team: a 48-team
`.DIY` took far too long on a 14 MHz 68020.

### Converting a competition

    cd bin
    SDL_VIDEODRIVER=dummy ./SWOS.exe --convert-diy=<in.DIY>,classic/<uefa|worldcup|club>/<NAME>.DIY

- **Input:** a PC game's or SWOS 2020's `.DIY` (the raw dump of section 3; up to 64 teams).
  An OpenSWOS JSON `.DIY`, or anything else, is refused as "not a PC/SWOS 2020 .DIY".
- **It needs the game's data.** It runs once the game has loaded its data, as a match would,
  because the save writer wants that data. `SDL_VIDEODRIVER=dummy` keeps the window shut.
  Relative paths are taken from the working directory.
- **It goes through the save folder:** it writes `_CONVERT.DIY` there and then moves it to
  `<out>`, replacing a file already at that path.
- **Exit codes:** 0 converted, 1 failed (could not read, not a PC file, would not load or
  write; the `[diy-convert]` line says which), 2 refused (more than 64 teams).
- **Checking it:** a build with `SWOS_SAVE_FORMAT_JSON_VERIFY=1` (a plain `make`'s default;
  `build_dist.sh` turns it off) prints `[json-verify-diy] N mismatches`, which must be 0.
  Then open it on the Amiga under HISTORIC TOURNAMENTS.
  The list shows it by the name inside the file, so the file name only has to keep its `.DIY`
  (SAVE reads the extension back from it).

A whole SWOS 2020 folder, from PowerShell in `bin` (on Windows, run the Cygwin `SWOS.exe` from
PowerShell, not from a nested Cygwin bash, which can exit 127 without a word):

    $env:SDL_VIDEODRIVER = 'dummy'
    Get-ChildItem "$HOME\Documents\SWOS\assets\classic\worldcup\*.DIY" | ForEach-Object {
        .\SWOS.exe "--convert-diy=$($_.FullName),$PWD\classic\worldcup\$($_.Name)" 2>&1 |
            Select-String 'diy-convert|json-verify'
    }

Chosen over a field-table reader in the game (every DOS field read by kind on any host)
because the JSON was already proven on the Amiga, and that reader's hard part — byte arrays
that hold words, the cup score tables inside `cupRoundResults` and `phaseStartMonths` — would
have to be traced field by field. It stays the way to go if the Amiga ever has to read a PC
file itself.

Proved under FS-UAE (2026-10-29): HISTORIC TOURNAMENTS above WORLD CUP; the UEFA list (17, by
year) in
0.6 s; EURO 1964's teams screen, HUNGARY switched to PLAYER-COACH (read back from memory: 2,
the others 1) and still so after PLAY; EURO 1964 played to the final through LOAD OLD
COMPETITION (Hungary 3–2 Denmark a.e.t., about 1 s to load); a classic competition saved and
loaded again.

Proved under FS-UAE (2026-10-29), 2026: its 48 teams on the teams screen (5.6 s to load);
played through VIEW RESULT, all 168 group matches and every knockout round (extra time and
penalties) to the final (Spain 1-0 Japan a.e.t.); saved during it and loaded again.

Proved under FS-UAE (2026-10-29), the club cups: HISTORIC TOURNAMENTS shows NATIONAL / CLUB,
CLUB lists the five by year, centred, and every EXIT goes back one screen (EURO's and WORLD
CUP's lists to NATIONAL, NATIONAL and CLUB to HISTORIC TOURNAMENTS, HISTORIC TOURNAMENTS to
the world list). Champions League 1994–95 (16 clubs, 2.4 s to load) played through VIEW RESULT: four groups of four, then
two-legged quarter- and semi-finals decided on aggregate, to the final (Galatasaray 1-0
Paris SG).

Proved again on `main` (2026-11-04), SDL and FS-UAE: all 44 converted with 0 mismatches,
and byte-identical before and after the one-pass loader; the teams screen three across
(Champions League 1995–96: SPARTAK MOSCOW, BLACKBURN ROVERS, STEAUA BUCHAREST fit); the EURO
list level; a drawer on 96/97 playing competitions converted under World 2026-27.

Not done: OpenSWOS Setup does not copy or convert a player's own SWOS 2020 files for the
Amiga: a player converts them with `--convert-diy` and copies them into the drawer.
