# CLASSIC_COMPETITIONS.md — CLASSICS, SWOS 2020's DLC cups, and the PC `.DIY` format

> Status (2026-10-29): **current**, both targets. The Amiga plays the same competitions from
> OpenSWOS's JSON `.DIY` (converted on a PC, section 6); 2026 (48 teams) is SDL only.

## 1. What it is

PRESET COMPETITION's world list has a row above WORLD CUP: **CLASSICS**. It opens

    CLASSICS -> UEFA | WORLD CUP -> the competitions in that group, by year
             -> the teams screen -> PLAY

The competitions are SWOS 2020's DLC cups: every Euro (1960–2024) and World Cup
(1930–2026, no 2022 in the DLC). The teams screen lists every team four across, row by row;
every one starts as COMPUTER and a click cycles it COMPUTER -> PLAYER-COACH -> COACH, in the
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

**Shipped:** `bin/classic/uefa/` and `bin/classic/worldcup/` — the 38 competitions of up to
32 teams, as OpenSWOS's JSON `.DIY` (every team COMPUTER), converted from SWOS 2020's DLC
with `--convert-diy` (section 6). SWOS 2020's DLC is a community project; these files were
updated there by petarku, and they ship with that community's agreement (2026-10-29). Every
drop carries `classic/` (`FULL_DATA_DIRS` and `AMIGA_DATA_DIRS` in `build_dist.sh`,
`build_installer.sh`, `make macos-app`, `WASM_DATA_DIRS`); in the Amiga drawer it is
`SWOS/classic/`.

**The player's own (SDL):** `<assets>/classic/uefa/` and `<assets>/classic/worldcup/`
(`<assets>` is `swos_assets_dir()`, by default `Documents\SWOS\assets`), SWOS 2020's files as
they are — `scripts/copy_classics.ps1` copies them from an install (`-From`, default
`C:\Games\SWOS 2020\dlc\cup`; a folder named WORLD CUP goes to `worldcup`, EURO or UEFA to
`uefa`). This is how 2026 (48 teams, never converted) gets in.

SDL looks in the assets folder first, then in the game's own `classic/`; a file name already
listed from the first is not listed again. A file is listed by the name inside it, sorted by
the first four-digit number in that name, then by name; one the target cannot read is skipped
(SDL logs a `[classic]` line). A list holds 32 (two columns of 16).

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
| 0xB5E | USER_A .. USER_F tactics, 370 bytes each | the USER tactics (not for CLASSICS) |
| 0x140A | the six option words | the options (not for CLASSICS) |
| 5191 | the teams | `_selectedTeams` |

A tournament's stage is a league when its slot (`header.roundComplete`) has
`isLeagueFormat != 0`. Team selections that were byte offsets from the teams array (the
standings rows' `teamSelOffset`, the cup bracket's `teamOffsetTable`) become the team's index,
as `diy_team_to_identity` defines them.

## 4. The code

| file | what |
|---|---|
| `src/diy_import_dos.c` | SDL only. `DiyImportDos_IsDosFile` (size/type/team-count check), `DiyImportDos_LoadData` (section 3; `keepSettings` for CLASSICS), `DiyImportDos_Load` (a save-folder file), and `--convert-diy` (section 6). The copy is by blocks, right only where DOS words are host-native. |
| `src/classic_comps.c` | Both targets. The folder scan (`dirent` on SDL, `Resload_ListDir` on the Amiga), the load (JSON through `SaveJson_LoadDIYData` with the player's options and tactics put back; a DOS file through the importer, SDL), the three screens' handlers, `ClassicComps_TakeReady`. |
| `src/menu_data.c` | `_classicCompsMenu`, `_classicListMenu`, `_classicTeamsMenu`: classic packed menus like every other, with their JSON in `bin/data_json/menus/classic*.json` (exported from the C; `[menu-json] verify` 55/55), `s_staticMenus`, the symbol registry. The teams menu lists its 64 buttons a column at a time and `team_of_entry` fills them a row at a time. |
| `src/teams.c` | `SelectTeamsFinalMenu`: the CLASSICS row (a marker in `leaguesPtrTable`, caught before anything reads the slot as a `Competition`), world view of the preset chooser only. |
| `src/menus.c` | `PresetCompetitionMenu`: after the chooser, a loaded classic competition is started. |
| `src/file_io.c` | `LoadDIYFile` (SDL): JSON as before, anything else through the importer. |
| `src/save_format_json.c` | `SaveJson_LoadDIYData`: a `.DIY`'s JSON from memory (`load_competition_mem`), since `classic/` is not the save folder. |
| `src/resload.c` | `Resload_ListDir`; and `fix_filename` leaves a name that has a folder in it alone, so `classic/uefa/EC1964.DIY` is not sent to `save/` for its `.DIY`. |

## 5. Known limits

- **2026 is six groups of eight**, and SDL only. The group layout is SWOS 2020's file, not the
  import: 6 groups x 8 (7 matches each), 32 through to a round of 32 — the real format (12
  groups of four, best thirds) needs more groups than SWOS's DIY tournament offers. It is not
  converted (48 teams, more than the Amiga is to play for now), so it comes only from the
  player's own SWOS 2020 files.
- **ESC on the teams screen** was seen to start the competition, as PLAY does, instead of
  going back like EXIT (SDL, 2026-10-29, not looked into yet).
- **Unexplained:** a SWOS 2020 cup loaded through LOAD OLD COMPETITION (every team COACH), one
  team switched to PLAYER-COACH on the PLAY MATCH screen: the next PLAY MATCH screen took no
  input (2026-10-29; the main thread was in the normal `MenuProc` loop, no `[menu-nav]` line).
  CLASSICS avoids it by starting every team as COMPUTER; the cause is not found.
- An early report of VIEW RESULT freezing (EC1964, LOAD OLD COMPETITION) was never reproduced;
  EC1964 played to the final through VIEW RESULT on the Amiga.

## 6. The Amiga

The Amiga cannot read the DOS dump (its words are little-endian, section 3), so it plays the
same competitions from **OpenSWOS's JSON `.DIY`**, which is built to load on it
(`save_format_json.c`'s cross-target slot and stage serializers). They are made on a PC:

    SWOS.exe --convert-diy=<in.DIY>,<out.DIY>

imports the file, makes every team COMPUTER, writes the JSON (and, in a tester build, reloads
and diffs it: `[json-verify-diy] 0 mismatches` for all 38), and refuses one of more than 32
teams (exit 2). The shipped `bin/classic/` is its output.

Chosen over a field-table reader in the game (every DOS field read by kind on any host)
because the JSON was already proven on the Amiga, and that reader's hard part — byte arrays
that hold words, the cup score tables inside `cupRoundResults` and `phaseStartMonths` — would
have to be traced field by field. It stays the way to go if the Amiga ever has to read a PC
file itself.

Proved under FS-UAE (2026-10-29): CLASSICS above WORLD CUP; the UEFA list (17, by year) in
0.6 s; EURO 1964's teams screen, HUNGARY switched to PLAYER-COACH (read back from memory: 2,
the others 1) and still so after PLAY; EURO 1964 played to the final through LOAD OLD
COMPETITION (Hungary 3–2 Denmark a.e.t., about 1 s to load); a classic competition saved and
loaded again.

Not done: OpenSWOS Setup does not copy or convert a player's own SWOS 2020 files for the
Amiga; the shipped ones cover every competition of up to 32 teams.
