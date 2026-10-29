# CLASSIC_COMPETITIONS.md — CLASSICS, SWOS 2020's DLC cups, and the PC `.DIY` format

> Status (2026-10-29): **current** for the SDL builds (`37400e2`); **plan** for the Amiga
> (section 6, not started — agreed: convert on the PC (B), at most 32 teams for now).

## 1. What it is

PRESET COMPETITION's world list has a row above WORLD CUP: **CLASSICS**. It opens

    CLASSICS -> UEFA | WORLD CUP -> the competitions in that group, by year
             -> the teams screen -> PLAY

The competitions are SWOS 2020's DLC cups: every Euro (1960–2024) and World Cup
(1930–2026, no 2022 in the DLC) as SWOS 2020 ships them, each a `.DIY` file. The teams screen
lists every team four across, row by row; every one starts as COMPUTER and a click cycles it
COMPUTER -> PLAYER-COACH -> COACH, in the friendly team list's colours (red / purple / blue).
PLAY starts the competition the way LOAD OLD COMPETITION starts a loaded one (`StartContest`).

Two things differ from loading the same file with LOAD OLD COMPETITION:

- **Every team starts as COMPUTER.** SWOS 2020's cups set them all to COACH (control 3); its
  challenge files use COMPUTER/PLAYER-COACH. A classic competition is chosen, not resumed.
- **The player's settings stay.** The six option words (game length, pitch type, ...) and the
  six USER tactics are kept; LOAD OLD COMPETITION takes the file's, as the original does.

LOAD OLD COMPETITION also reads these files now (any `.DIY` that is not JSON), with the file's
control types, options and tactics.

## 2. Where the files live

Not in this repository, like the rest of the original game's data. In the assets folder:

    <assets>/classic/uefa/*.DIY        <assets>/classic/worldcup/*.DIY

(`<assets>` is `swos_assets_dir()`, by default `Documents\SWOS\assets`.) A file is listed by the
name inside it, sorted by the first four-digit number in that name, then by name; a file that
is not a PC/SWOS 2020 `.DIY` is skipped with a `[classic]` log line. A list holds 32 (two
columns of 16); more are logged as not listed.

`scripts/copy_classics.ps1` copies them from a SWOS 2020 install (`-From`, default
`C:\Games\SWOS 2020\dlc\cup`): a folder whose name says WORLD CUP goes to `worldcup`, EURO or
UEFA to `uefa`, anything else is reported and skipped. Safe to run again. OpenSWOS Setup does
not do this yet; it is the natural home for it (it already takes SWOS 2020's pitches).

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
| `src/diy_import_dos.c` | `DiyImportDos_IsDosFile` (size/type/team-count check), `DiyImportDos_LoadData` (section 3; `keepSettings` for CLASSICS), `DiyImportDos_Load` (a save-folder file). SDL only: the copy is by blocks, right only where DOS words are host-native. |
| `src/file_io.c` | `LoadDIYFile`: JSON as before, anything else through the importer. |
| `src/classic_comps.c` | the three screens (JSON menus `bin/data_json/menus/openswos/classic{Comps,List,Teams}Menu.json`, handlers registered at run time), the folder scan, the load, `ClassicComps_TakeReady`. |
| `src/teams.c` | `SelectTeamsFinalMenu`: the CLASSICS row (a marker in `leaguesPtrTable`, caught before anything reads the slot as a `Competition`), world view of the preset chooser only. |
| `src/menus.c` | `PresetCompetitionMenu`: after the chooser, a loaded classic competition is started. |

The menus were generated once by a throwaway script; edit the JSON directly now. The teams
menu lists its 64 buttons a column at a time and `team_of_entry` fills them a row at a time.

## 5. Known limits

- **2026 is six groups of eight.** That is SWOS 2020's file, not the import: its first stage
  is 6 groups x 8 (7 matches each), 32 through to a round of 32. The real format (12 groups of
  four, best thirds) needs more groups than SWOS's DIY tournament offers.
- **LOAD OLD COMPETITION's list** was made for 8-character DOS names: longer ones (SWOS 2020's
  challenge files) run into the type column. Rename, or shorten them in the list.
- **Unexplained:** a SWOS 2020 cup loaded through LOAD OLD COMPETITION (every team COACH), one
  team switched to PLAYER-COACH on the PLAY MATCH screen: the next PLAY MATCH screen took no
  input (2026-10-29; the main thread was in the normal `MenuProc` loop, no `[menu-nav]` line).
  CLASSICS avoids it by starting every team as COMPUTER; the cause is not found.
- An early report of VIEW RESULT freezing (EC1964, LOAD OLD COMPETITION) was never reproduced.

## 6. The Amiga — plan, not started

Two things stand in the way: reading the files, and the screens.

### 6a. Reading the files: two ways

**A. A real converter in the game, both targets.** Replace the block copies in
`diy_import_dos.c` with a table of the DOS layout — each field's offset, size and kind (byte,
word, int32, team id, raw bytes) — and one loop that reads it on any host. What it covers:

- the header (`DIYCompetition`): about 30 word/byte fields and the 35-byte name;
- the `DIYCompetitionEx` body: the round settings, team-id lists, `cupAggregateScores`, the
  counters, `leagueSectionState`, `bracketScratch` (its offset table is int32), the standings
  (an int32 and 7 words per row);
- the tournament: the six slots (13 words, then 90 bytes), `groupMap`, `teamIds`,
  `teamIndexOrder` (bytes);
- the teams: only `globalTeamNumber` (a word) in each 684-byte record; the rest is bytes.

**The work is in the byte arrays that hold words.** The cup score tables live inside
`cupRoundResults` (from +0x40) and `phaseStartMonths`, and parts of `teamShuffleOrder` /
`scratchTail` may too; each has to be traced in `diy_competition.c` to byte or word. A wrong
guess swaps scores — the JSON format met exactly that ("GERMANY 2-0 shown 0-2",
`save_format_json.c`). Proof: on SDL the table must produce the same bytes as the block copies
for all 39 files, then a cup, a group tournament and 2026 played in FS-UAE.

**B. Convert on the PC, play JSON on the Amiga.** OpenSWOS's own JSON `.DIY` is built to load
on the Amiga (`save_format_json.c`'s cross-target slot and stage serializers). So:

- the SDL build gets a headless `--convert-diy <in> <out>`: import, then `SaveJson_WriteDIY`;
- the setup tool (or `scripts/copy_classics.ps1`) puts the converted files in the Amiga
  drawer; the Amiga never reads a DOS file.

What to prove: an SDL-written JSON tournament — 2026 above all, the largest — plays right on
the Amiga, and its load time on a 68020 (the files would be ~200–250 KB; the `.PRE` loads
were once slow enough to look crashed, CHAMPIONS36.md).

B is the smaller step and leans on a format the Amiga already reads; A is the clean end
state and would replace the SDL block copies too.

**Agreed (2026-10-29): B**, and on the Amiga only competitions of **at most 32 teams** for
now — every Euro, and every World Cup but 2026 (48). The converter skips a larger one (or the
Amiga's list leaves it out), so a 48-team stage never has to be proved on the 68020. A stays
the option if B's JSON does not hold up on the Amiga. The order of work:

1. `--convert-diy <in> <out>` in the SDL build; convert all 38 files of 32 teams or fewer.
2. Prove the converted files on the Amiga through what exists already — LOAD OLD COMPETITION
   in FS-UAE: a 4-team cup (EC1964), a group tournament (WC1966, 16), the largest (WC2018, 32);
   draw, groups, results, the knockout, and the load time.
3. Only then the screens (6b) and the setup step that fills the drawer.

### 6b. The screens (either way)

- The three screens are JSON menus; the Amiga has no JSON menu loader. Each becomes a
  compiled menu in `menu_data.c` — the five places of MENUS.md section 12 — the teams screen
  64 buttons plus PLAY and EXIT.
- `classic_comps.c` lists the folder with `dirent`: the Amiga needs AmigaDOS
  `Lock`/`Examine`/`ExNext`, or an index file the setup writes.
- `classic/uefa` and `classic/worldcup` inside the SWOS drawer, filled by the setup's "Add
  the game data".
- `classic_comps.c` (and, for A, `diy_import_dos.c`) leave `SDL_ONLY_SRCS` in
  `Makefile.amiga`; the `SWOS_SDL_BUILD` guards around CLASSICS in `teams.c` and `menus.c` go.
- Check `bin/SWOS2.map` after the build (memory is not the issue with 8 MB of fast RAM), and
  test a cup, a tournament with groups and the largest (32 teams) under FS-UAE.
