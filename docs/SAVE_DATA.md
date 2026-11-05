# SWOS save file formats

> **Status (2026-09-30): partly historical.** The "Format migration / read-support
> policy" section is current. The `.CAR`/`.SEA`/`.PRE`/`.DIY` layout sections
> below describe the **legacy raw-dump** format of the Amiga `careerFileBuffer`
> overlay: its reader and writer are both deleted, and the overlay itself is gone
> (`src/swos_globals_career_overlay.c` now holds only `careerFileBuffer` and
> `numSelectedTeams`). Those four save kinds are JSON-only (`src/save_format_json.c`,
> `schema_version` 3 — see `docs/SAVE_FORMAT_JSON.md`); the offsets are kept as a
> map of which fields exist. The C functions have since lost their leading
> underscore (`_LoadCareerFile` → `LoadCareerFile`); names below use the current form.

This document describes the on-disk layout of every save file the game
writes, with particular attention to fields that contain absolute
pointers — these need rebuilding on load (see `RebuildPointersAfterLoad`)
and are the things that have to be replaced when we eventually move to a
structured (JSON / YAML / tagged binary) save format.

All multi-byte integers are **big-endian** on disk (Amiga m68k convention).
The SDL build uses the `BE_*` / `PACK_BE_U16` helpers in
`src/endian_helpers.h` to read them; the macros are no-ops on a BE host so
the same code compiles for the Amiga target. (This byte order now applies only to the binary `.TAC`/`.HIL`/`.EDT` records; the JSON saves are text.)

---

## Inventory

| Extension | Buffer | Save fn | Load fn | Format |
|---|---|---|---|---|
| `.CAR` / `.SEA` | `careerFileBuffer` (256 KB overlay) | `SaveCareerFile` | `LoadCareerFile` | Career save — header + selected-teams body |
| `.PRE` | `competitionFileBuffer` (overlay at +93308) | `SavePresetCompetitionFile` | `LoadPresetFile` | Preset competition save |
| `.DIY` | `competitionFileBuffer` (overlay at +93308) | `SaveDIYFile` | `LoadDIYFile` | DIY competition save (same layout as preset) |
| `.TAC` | `editTacticsCurrentTactics` | `WriteTacticsFile` | `ReadTacticsFile` | Single 370-byte tactics record |
| `.HIL` | `hilFileBuffer` | `DoSaveHighlights` | `LoadHilFile` | Match highlights ring — header + N goal frames |
| `.EDT` | `teamFileBuffer` | `WriteFile(aCustomsEdt)` (edit_tactics.c) | `LoadFileEx(aCustomsEdt)` (teams.c) | Edited teams (`CUSTOMS.EDT`) — count word + N TeamFile records, **BE** (byte-swapped to host on load) |
| `.CFG` (options) | — | — | — | **No such file is written or read by this port** (see ".CFG — options" below); the six option words ride inside the career/preset/DIY JSON |

---

## Format migration / read-support policy

Two classes of save format, handled differently as we move to the structured
(JSON) format (roadmap step 4). **This split is load-bearing — do not collapse
it during the "retire legacy reads" cleanup.**

**JSON-migrated — legacy raw-dump READ retired.** `.CAR` / `.SEA` (career),
`.PRE` (preset), `.DIY` — these were the big `careerFileBuffer` /
`competitionFileBuffer` overlay dumps. The writer is JSON-only (`SaveJson_*`);
the legacy raw-memory-dump *reader* is dead after the overlay untangle (it loaded
extracted fields into `.skip` holes / overflowed the now-standalone
`competitionFileBuffer` → broken state). All three entry points
(`LoadCareerFile`, `LoadPresetFile`, `LoadDIYFile`) are now JSON-only and emit
a graceful "unsupported format" error otherwise. A non-JSON career/preset/DIY
file no longer loads — accepted (all current saves are JSON). The then-unreferenced
`_LoadCompetitionFile` (legacy header verifier) and `_TranslateAmigaAddressesAfterLoad`
(legacy Amiga-pointer LUT, with its whole `save_pointer_lut.c` module +
`generated/amiga_address_map.c`) were removed 2026-06-11 — see "Cross-build pointer
LUT … RETIRED" below.

**One exception: a `.DIY` from the PC game or SWOS 2020** (SDL, little-endian hosts only;
`src/diy_import_dos.c`, the5ar, 2026-11). That is not our old dump but THEIR raw DOS dump
(5189-byte region, LE words, a 0x5F-byte competition header), which SWOS 2020's challenges
and the community's competitions are written in. `LoadDIYFile` loads a JSON `.DIY` as before
and imports any other file that is laid out as a DOS `.DIY` into the same named fields a JSON
load fills. `--convert-diy=<in>,<out>` writes one as our JSON `.DIY` (every team COMPUTER),
which the Amiga loads; the Amiga refuses the DOS file itself. `SaveJson_LoadDIYData` loads a
JSON `.DIY` already in memory, in any world (a `.DIY` carries all its teams); a mod's
competitions folder is listed on the Amiga by `Resload_ListDir`, mods first, and read with
`swos_io_load_data_alloc` (the data root, not `save/`).

**KEEP binary read/write — DO NOT retire.** `.TAC` (tactics), `.HIL`
(highlights), `.EDT` (edited teams / `CUSTOMS.EDT`), and `.CFG` (options — no such file exists in this port today; see ".CFG" below) are
small, fixed-layout binary records that the user/game exchanges as **binary**
(`.EDT` is also shared with the wider SWOS community / the original game, so its
on-disk BE layout is an interop format, not just our save). Their binary
`_Read*`/`_Load*`/`LoadFileEx` path **must be maintained** — these files are
still authored and consumed in binary form. A JSON variant may exist alongside
(`SaveJson_LoadTactics` / `SaveJson_LoadHighlights` auto-detect by header), but
the binary read path is NOT legacy and must not be removed. Do not fold
`.TAC`/`.HIL`/`.EDT`/`.CFG` into the career/preset/DIY legacy-read retirement.

---

## .CAR / .SEA — career save

The career save is a **raw memory dump** of the careerFileBuffer overlay
region (see `src/swos_globals_career_overlay.c` for the full label table).
On Amiga BSS this region was one contiguous `ds.b` block; on SDL we
restore that adjacency by aliasing every label into one
`careerFileBufferBuf[256*1024]`.

**File size formula:**
```
size = (&numSelectedTeams - &careerFileBuffer)        [= 98476]
     + numSelectedTeams * 684                          [team-data tail]
     + 2                                               [trailing word]
```

For a typical 32-team career: `98476 + 32*684 + 2 = 120366` bytes.

### Header region (offsets 0 .. 98477)

The full label list lives in `swos_globals_career_overlay.c` — what
follows is a summary by section. Offsets are relative to the start of the
file (which equals `&careerFileBuffer`).

#### 0 .. 54721 — `tmdFileBuffer` (54720 bytes)
Career-team pool. Holds up to ~80 TeamFile records plus auxiliary tables.
Format-internal — see `_LoadTeamPool` for the layout.

**Pointer fields inside tmdFileBuffer (runtime-only; should be zeroed
before save):** none currently identified, but each TeamFileEx slot
contains `inGameTeamPtr` (offset 10), `teamStatsPtr` (14), `spritesTable`
(20), `shotChanceTable` (24), `controlledPlayer` (32), `passToPlayerPtr`
(36), `passingKickingPlayer` (104), `lastHeadingTacklingPlayer` (72) —
none of these should ever survive a round-trip. They are reset by
`InitializeInGameTeamsAndStartGame` when a match begins.

#### 54722 .. 56189 — Career counters and finance (1468 bytes)
Single-field globals: `currentSeasonShowing`, `stayingInOldClub`,
`overdraft*`, `youreSacked`, `retired`, `newBalance`, `old_balance`,
`playerPurchases / playerSales / generalRunningCosts / playerWagesBill /
gateReceipts / startingBalance(+Copy) / competitionBonuses /
sensisoftPlcSponsorship / weeklyRunningCosts / weeklyPlayerWages /
totalTeamValue`, plus the per-player `weeklyWageByPlayerIndex[128]` and
`playerFormModifiers[32]` tables, board-confidence history, qualifying
flags, `gamesPlayed / numWins / numDrawn / numLost / goalsScored /
goalsTaken`, contract negotiation state, transfer/job/national-team
counters and tables.

All scalar fields. **No pointers.**

#### 56190 .. 57481 — `careerTeam` (1292 bytes)
The user's team. TeamFile (76 bytes) + 32 × PlayerFile (38 bytes) — the
"extended" 32-slot layout that holds bench players too. On Amiga the
declared type is `TeamFileEx` (16 players) but pointer arithmetic reaches
slot 31.

**Runtime pointers inside (must be zeroed before save):** same set as
tmdFileBuffer slots above.

#### 57482 .. 61609 — `careerNumPlayers`, `seasonInformationsArray`, `diyLeagueSeasonStateTable` and friends
Mostly counters and tables. **No pointers.**

#### 61610 .. 62901 — `currentTeam` (1292 bytes)
Working slot for the AI opponent during a match. Same shape as
careerTeam. Same runtime-pointer caveat.

#### 62902 .. 63319 — Per-team / per-match state
`playerPositions`, `numPlayers`, `currentTeamNumber`,
`currentTeamSelectedPlayerPosition`, `trainingMatch*`, `lastMatch*`,
`currentTactics`, `careerLastMatchIndexByCompetitionSlot`,
`seasonFinished`, `careerFinished`. All scalars and small tables.

#### 63320 .. 63339 — `careerCompetitionSlotPtrTable[5]` ⚠ POINTERS
**This is a 5-entry array of `uint8_t *` pointers**, populated by
`InitNewSeason` (career_init.c:1165) to point at:

```
[0] -> &competitionFileBuffer       (careerFileBufferBuf + 93308)
[1] -> &competitionContext          (careerFileBufferBuf + 63340)
[2] -> &tertiaryCompetitionContext  (careerFileBufferBuf + 66240)
[3] -> &euroCupsCompetitionContext  (careerFileBufferBuf + 68424)
[4] -> &diyCompetitionContext       (careerFileBufferBuf + 67332)
```

Saved values are absolute addresses. **Always rebuild on load** — these
five entries can be re-derived directly from the current run's
`careerFileBufferBuf` base.

#### 63340 .. 67331 — `competitionContext` + `secondaryCompetition*` (3992 bytes)
Holds the full DIYCompetition struct for slot 1 (secondary cup): name,
string offset, winner, slot states. The first 4 bytes are the struct
header `(compId, slotId)` packed BE.

⚠ **`secondaryCompStringOffset` (offset +63380, 4 bytes) is currently a
raw pointer** into the strings table (`uint32_t`). On Amiga it was
typed as a "string offset" but is actually used as an absolute address.
**Rebuild on load.**

#### 67332 .. 68423 — `diyCompetitionContext` (1092 bytes)
DIYCompetition struct for the user's DIY tournament slot. Same shape as
competitionContext.

#### 68424 .. 71323 — `euroCupsCompetitionContext` + quaternary state
DIYCompetition for slot 3 (Euro cup). ⚠ `quaternaryCompStringOffset`
(offset +68464) — same as secondaryCompStringOffset — rebuild on load.

#### 71324 .. 73291 — Competition copies, team-budget tables
33 `*Copy` competition struct snapshots
(`euroCupCopy / cupWinnersCupCopy / uefaCupCopy / …`), each storing the
runtime state of a static cup struct. The byte layout is whatever the
matching `CompetitionCup_*` struct encodes — internal pointer fields
inside *those* structs (slot pointers, table pointers) are runtime-only.

⚠ Several of the copy structs end with 4-byte trailers that look
pointer-shaped (`b8c85c56` / `44c95c56` / `…`). These need confirming
field-by-field whether they're real pointers or coincidental data.

#### 73292 .. 93305 — `teamSelectionKeyCache` workspace (~20 KB)
Variable-length cache of competition slot keys. Overlaps
`careerForeignMarketPlayers` (Amiga adjacency reuse — both at the same
bytes, used at different times). No pointers within the cache itself.

#### 93308 .. 98457 — `competitionFileBuffer` overlay (5150 bytes)
Slot 0 of the competition pointer table. Same DIYCompetition shape as
competitionContext, plus DIY-tournament fields:
`diyPresetNameOfCompetition`, `numDiyTeams`, `primaryCompWinnerTeam`,
the three `diyTeamIdListType[012]` arrays, etc.

⚠ `primaryCompStringOffset` (offset +93348) — same pattern as the other
`*CompStringOffset` fields. **Rebuild on load.**

#### 98458 .. 98477 — Save header + global config + RNG state
- `saveFileVersion` (2 bytes) — must be `0x0012`
- `saveFileMagic1..3` (12 bytes) — `"CJ0611143807"`
- `saveFileAmigaType` (4 bytes) — must be `0x00100000` (Amiga binary's
  j_start entry-point address; **this is a vestigial field** that the
  validator still checks)
- `numSelectedTeams` (2 bytes) — body team count

Plus `gameLength`, `autoReplays`, `menuMusic`, `autoSaveHighlights`,
`allPlayerTeamsEqual`, `pitchType`, `seed2 / randXorIndex2 / randXorKey2 /
newRandomSeedCopy / randomSeed`, and the 6 user tactics
(`teamTacticsUSER[A-F]`, 370 bytes each).

**No pointers here.**

### Body region (offsets 98478 .. EOF)

`numSelectedTeams * 684` bytes of TeamFile records, one per selected team.
The Amiga BSS overlays the named labels `selTeamsPtr` (= start),
`selTeamIsComputerFlag` (+4), `selTeamNameBase` (+5),
`selTeamImportField` (+25), `selTeamSocksColor` (+30),
`selTeamPlayerIndices` (+60, 1308 bytes), `editTeamsBuffer` (+1368,
43778 bytes) onto this region — they are **field-name labels into the
first TeamFile entry plus working buffers that re-use the array memory**,
not separate variables.

Each 684-byte TeamFile slot has the same internal pointer fields
(`inGameTeamPtr`, `spritesTable`, etc.) as `careerTeam` — must be zeroed
before save / runtime-rebuilt after load.

### Pointer-field summary for .CAR

| Offset | Field | Type | Rebuild source | Handled? |
|---|---|---|---|---|
| 63244 | `currentCompetitionPtr` | `uint32_t` (ptr) | `competitionsTable[selTeamNumber>>8][0]` | ✅ |
| 63248 | `secondaryCompetitionPtr` | `uint32_t` (ptr) | `competitionsTable[…][1]` | ✅ |
| 63252 | `tertiaryCompetitionPtr` | `uint32_t` (ptr) | `competitionsTable[…][2]` | ✅ |
| 63256 | `quaternaryCompetitionPtr` | `uint32_t` (ptr) | `euroCupSlotIndex` → euroCupCopy / cupWinnersCupCopy / uefaCupCopy | ✅ |
| 63260 | `diyCompetitionPtr` | `uint32_t` (ptr) | derived from primary comp DIY link | ✅ |
| 63320 | `careerCompetitionSlotPtrTable[0]` | `uint8_t *` | `&competitionFileBuffer` | ✅ |
| 63324 | `careerCompetitionSlotPtrTable[1]` | `uint8_t *` | `&competitionContext` | ✅ |
| 63328 | `careerCompetitionSlotPtrTable[2]` | `uint8_t *` | `&tertiaryCompetitionContext` | ✅ |
| 63332 | `careerCompetitionSlotPtrTable[3]` | `uint8_t *` | `&euroCupsCompetitionContext` | ✅ |
| 63336 | `careerCompetitionSlotPtrTable[4]` | `uint8_t *` | `&diyCompetitionContext` | ✅ |
| 63380 | `secondaryCompStringOffset` | `uint32_t` (ptr) | zeroed on load (consumer tolerates NULL) | ✅ |
| 68464 | `quaternaryCompStringOffset` | `uint32_t` (ptr) | zeroed on load | ✅ |
| 93348 | `primaryCompStringOffset` | `uint32_t` (ptr) | zeroed on load | ✅ |
| 66280 | `tertiaryCompStringOffset` | `uint32_t` (ptr) | zeroed on load | ✅ |
| varies | cup-record `text[2]` at `(*Copy + 5 + nameOffset)` | 2× `uint32_t` (ptr) | careerContests table src→dest, both pointers | ✅ |
| 55086 | `careerCupPickList[]` | `uint32_t *` | rebuilt for nat-selectors via `Career_SetCupPickList_ForSelectedRegion(0)` | ✅ |
| trailing inside copy structs | `unk1[]` bytes | scalar lookup tables | n/a (not pointers) | ✅ |

**Plus runtime-only fields that should be zeroed before save** (or after
load): everything in any TeamFile slot at field offsets
`{10, 14, 20, 24, 32, 36, 72, 104}` — these are pointers to in-game
runtime structures and should never appear in a save file.

### Runtime structures NOT in the save

Several runtime structs are populated by the game code from the saved
data, but are not themselves part of the file. Loading a .CAR does not
restore these — they're rebuilt on demand:

- `diyCompetitionStart` (separate 4 KB BSS buffer outside the careerFileBuffer
  overlay). Rebuilt by `DIY_LoadCurrentCompetitionSlot` from
  `diyFileBufferCopy` (which itself comes from `competitionFileBuffer`
  via `Career_LoadCompetitionSlotStateFromBuffer`).
  **VIEW SQUADS reads from this struct.** If we don't repopulate it
  post-load, `StatsViewSquads` reads zeros and panics on `VerifyTeamLoaded(0)`.
- `topTeamData / bottomTeamData` (TeamGeneralInfo runtime structs).
  Rebuilt by `InitializeInGameTeamsAndStartGame` at match start.
- `diyFileBufferCopy / diyCompetitionStart` workspace buffers.
- All sprite tables (`team1SpritesTable / team2SpritesTable / allSpritesArray`).

---

## .PRE / .DIY — preset / DIY competition save

Same byte layout as the .CAR header region, but rooted at
`competitionFileBuffer` (the slot-0 buffer inside the careerFileBuffer
overlay). Holds a single DIYCompetition struct plus the team list for
that competition.

**File size:**
```
size = (&numSelectedTeams - &competitionFileBuffer) + numDiyTeams * 684 + 2
```

(`&numSelectedTeams - &competitionFileBuffer` = `98476 - 93308` = 5168
bytes of header, then numDiyTeams × 684 of team data, then 2.)

**Pointer fields:** same `*StringOffset` issue as the parent .CAR.
careerCompetitionSlotPtrTable is *not* in this file — it's only in .CAR.

---

## .TAC — single tactics record

A flat 370-byte dump of one `TeamTactics` struct. No pointers, no header.

`TeamTactics` layout: 9-byte name + 9 × 35-byte
`TacticsPositionEntry[5][7]` arrays + 10-byte zone weights + 1-byte
defender-marking flag = 370 bytes total.

---

## .HIL — match highlights

Variable-size record describing one match's worth of replay frames.

```
size = hilNumGoals * 19000 + 2254
```

`hilFileBuffer` layout (from `hilStartOfGoalsBuffer` / `inGameHilBuffer`
adjacent BSS labels — see `src/swos_globals.c`):
- 2254 bytes of header (`hilStartOfGoalsBuffer` region) — match
  metadata, scoreboard, frame-count tables.
- N × 19000 bytes of replay frames (one per goal).

**Pointer fields:** TBD — needs a dedicated audit when we add highlight
save round-trips. Likely candidates: any `Sprite *` arrays inside the
frame format.

---

## .CFG — options

Correction (2026-09-30): there is no `.CFG` file. `SaveOptions` /
`RestoreOptions` (`src/menus.c`, SWOS2.s 69187/69198) only copy the six
`uint16_t` words `gameLength`, `autoReplays`, `menuMusic`,
`autoSaveHighlights`, `allPlayerTeamsEqual`, `pitchType` to and from the
in-memory `saveGameLength` snapshot; no `_LoadOptions` exists and nothing
in `src/` opens a `.CFG`. The six words are persisted as named fields
(`game_length` … `pitch_type`) inside the career/preset/DIY JSON
(`src/save_format_json.c`); SDL-side settings live in `SWOS.ini`.
All scalars. **No pointers.**

---

## Cross-build pointer LUT (Amiga-canonical save format) — RETIRED 2026-06-11

> **RETIRED — superseded by the JSON format and DELETED.** This
> address-translation LUT was an interim design that was never adopted as
> the primary path. The JSON save format (`save_format_json.c`) serializes
> pointer fields as semantic identities (competition id, string-pool key,
> team index) and re-derives the runtime pointer on load via
> `RebuildPointersAfterLoad` — no address translation needed. So
> `src/save_pointer_lut.c`, `src/generated/amiga_address_map.c`, and the
> dead `_TranslateAmigaAddressesAfterLoad` entrypoint were removed on
> 2026-06-11. `tools/extract_amiga_addresses.py` /
> `tools/needed_lut_symbols.txt` remain only as orphaned reference. The
> design notes below are kept for historical context.

Until we move to the structured save format, every save file on disk is
**canonically Amiga-addressed** — every pointer in a `.CAR` / `.SEA` /
`.PRE` / `.DIY` is the *original Amiga binary's* absolute address for
the symbol it points to. Each port (original Amiga, Amiga C port, SDL
Linux, SDL Windows) translates at the save/load boundary:

```
   on save                           on load
   -------                           -------
   current_addr  →  symbol_name      amiga_addr  →  symbol_name
   symbol_name   →  amiga_addr       symbol_name →  current_addr
   write to disk                     use as runtime pointer
```

The original Amiga binary is the identity case: its `current_addr` *is*
the `amiga_addr`. For our ports we need a lookup table.

### LUT data source

Symbol → Amiga address mappings come from
`C:\Projects\Amiga\SWOS-RE\Amiga\WHDLoad\Static\SWOS\build\SWOS2.lst` —
a 12 MB listing file from a static-build of the original Amiga binary
that produces a 1:1 byte image of the original `.exe`. Addresses in
that file are absolute, post-relocation.

A Python extractor (`tools/extract_amiga_addresses.py`) parses the
`.lst`, pulls each `name:` label and its address + size from the
following `ds.b/dc.b/dc.w/dc.l` directive, and emits filtered output.

The curated needed-list lives in **`tools/needed_lut_symbols.txt`**.
It's intentionally lean (~230 symbols): only the pointer-target
symbols that **can't** be re-derived from a saved scalar identity
in `RebuildPointersAfterLoad`. That covers:

  - Every `&<name>` referenced inside `src/competition_data.c` —
    static cup/league templates, *Copy slots, qualifying-group tables,
    `nationalTeamCompetitionsAndQualifications`, `continentalCups`.
  - The BSS region anchors used by `careerCompetitionSlotPtrTable[5]`.
  - `careerCupPickList`, `competitionsTable`, the four
    `*CompStringOffset` fields, and the five top-level competition
    pointer globals.

**What's NOT in the LUT, and why:**

  - **Name strings** (`aLeague_1`, `aPremierDivisio`, `aWorldClubChamp`, …
    ~1 K Amiga-style string symbols). Cup-record `nameSource` pointers
    target these, but on load they're re-derived by
    `Rebuild_CupRecordNameSourcePointers()` — it walks the
    `careerContests` table and copies the canonical pointer from each
    static cup template into its `*Copy`. No address-translation
    needed. Same approach extends to `*CompStringOffset` when those
    are wired in.
  - **Runtime-only TeamFile pointers** (`inGameTeamPtr`, `spritesTable`
    et al.) — zeroed before save / repopulated at match start.

**If we ever want bidirectional compatibility with original Amiga
`.CAR` files** (loading saves from the unmodified Amiga binary), the
strings would need to go in the LUT too — original saves don't have a
"current build" template to copy from, only raw Amiga string
addresses. That's a future-feature consideration, not a current need.

Run:

```bash
python3 tools/extract_amiga_addresses.py \
    /path/to/SWOS2.lst \
    --needed tools/needed_lut_symbols.txt \
    --c-out src/generated/amiga_address_map.c
```

Outputs:

- `tools/amiga_addresses.txt` — text dump for inspection
  (`name  amiga_addr  size  section` per line, ~230 rows, ~9 KB).
- `src/generated/amiga_address_map.c` — generated C array consumed by
  the runtime LUT (~13 KB).

Symbols not present in the listing emit a warning so we notice typos.
When new code paths reference Amiga symbols not yet in
`needed_lut_symbols.txt`, append them and re-run.

The runtime side keeps the LUT in `src/save_pointer_lut.c` (TBD), which
exposes:

```c
uint32_t amiga_addr_for_current(void *p);   /* save: cur → amiga */
void *current_addr_for_amiga(uint32_t a);   /* load: amiga → cur */
```

These wrap two parallel sorted tables (sorted by amiga_addr and by
current_addr respectively) for `O(log N)` lookup. Range lookup
(pointer falls *inside* a labeled block) is supported via the size
field — `addr - block_start` is the within-block offset, which we
re-apply to the current build's same-symbol address.

### Symbol categories needed

Every pointer value that can appear in a save file resolves to one
of these:

| Category | Symbols | Used by pointer field |
|---|---|---|
| **A. Static competition templates** | `euroCup`, `cupWinnersCup`, `uefaCup`, `worldCup`, `worldCup2`, `europeanChampionships(2)`, `copaAmerica`, `africanNationsCup`, `northAmericanCup`, `asianCup`, `oceaniaCup`, `friendly`, `tour`, `invitationTournament`, `ecQualifyingGroup1..8`, `worldCupQualifyingGroup1..8`, `northAmerica/southAmerica/africa/asia/oceaniaWorldCupQualification`, plus per-country league records (e.g. `liga1`, `premiereDivisione`, …) | `currentCompetitionPtr`, `secondary/tertiary/quaternaryCompetitionPtr`, `careerCupPickList[]`, `careerContests.competition` |
| **B. Static cup runtime copies** | The 33 `*Copy` slots from `careerContests`: `euroCupCopy`, `cupWinnersCupCopy`, `uefaCupCopy`, `worldCup2Copy`, … | `currentCompetitionPtr` / etc. when slot points to its career-mode runtime copy; also referenced by `nationalTeamCompetitionsAndQualifications[]` |
| **C. Static name strings** | `aLeague_1`, `aPlayoffs`, `aPremierDivisio`, `aFirstDiv`, `aSecondDiv`, … all comp-name strings reachable via cup-record `nameSource` (offset `+5+nameOffset`) | cup-record embedded `nameSource` ptr; `*CompStringOffset` |
| **D. BSS region anchors** | `competitionFileBuffer`, `competitionContext`, `tertiaryCompetitionContext`, `euroCupsCompetitionContext`, `diyCompetitionContext`, `careerFileBuffer`, `tmdFileBuffer`, `careerCupPickList` | `careerCompetitionSlotPtrTable[5]`; runtime pickList pointers |
| **E. Static qualifying-group tables** | `worldCupQualifyingGroups`, `continentalCups`, `nationalTeamCompetitionsAndQualifications` | `diyCompetitionTablePtr` |

Every symbol that any save-file pointer can ever resolve to falls in
A–E.

### Per-format pointer-field audit

#### .CAR / .SEA

All pointer fields are documented in the table above
("Pointer-field summary for .CAR"). The category column maps each to
the LUT category needed:

| Field | Category |
|---|---|
| `currentCompetitionPtr / secondary / tertiary / quaternary` | A or B |
| `diyCompetitionPtr` | A or B |
| `diyCompetitionTablePtr` | E |
| `careerCompetitionSlotPtrTable[5]` | D |
| `*CompStringOffset` (4 fields) | C |
| `careerCupPickList[]` (nat-selector only) | A or B |
| Cup-record `nameSource` ptrs | C |
| TeamFile runtime ptrs (`inGameTeamPtr` etc.) | zeroed, no LUT |

#### .PRE / .DIY

The 5168-byte header is a subset of the .CAR header (slot 0 region
only, rooted at `competitionFileBuffer`). Same pointer fields appear
within that subset: `primaryCompStringOffset`, the embedded cup
`nameSource` if the saved comp is a cup, plus the runtime team ptrs.

| Field | Category |
|---|---|
| `primaryCompStringOffset` | C |
| Cup-record `nameSource` (if comp is a cup) | C |
| TeamFile runtime ptrs | zeroed |

#### .TAC

No pointers. `TeamTactics` is 370 bytes of pure scalars/positions.
**No LUT entries needed.**

#### .HIL

Pointer fields TBD — see audit note above. Likely `Sprite *` arrays
inside per-frame replay records. Will need a dedicated pass when we
add highlight save round-trips. Once known, those go in **Category F
(static sprite assets)** in the table above.

#### .CFG

Scalars only. **No LUT entries needed.**

### Save format version

Adding LUT translation changes the on-disk byte layout (pointer fields
flip from current-build addresses to canonical Amiga addresses). Bump
`saveFileVersion` from `0x0012` → `0x0013` so the loader can
distinguish:

- v0x0012 — legacy raw-pointer save. Apply existing
  `RebuildPointersAfterLoad` heuristics (incomplete; only same-build
  loads are reliable).
- v0x0013 — Amiga-canonical save. Translate every pointer field
  through the LUT on load.

`saveFileMagic1..3` ("CJ0611143807") and `saveFileAmigaType`
(0x00100000) stay unchanged.

### Migration plan to structured save format

When we eventually move to JSON / YAML / tagged binary (CLAUDE.md
roadmap step 4), every pointer field becomes a **symbolic reference**:

- `careerCompetitionSlotPtrTable[N]` → omitted (purely runtime).
- `*CompStringOffset` → `{ "stringId": "<symbol name>" }`.
- Cup-record `nameSource` ptr → omitted (re-derived from comp template
  on load).
- Runtime ptrs inside TeamFile / TeamGeneralInfo / hilFileBuffer →
  omitted.

The Amiga LUT becomes redundant at that point — symbol names are the
serialized identity. The LUT scaffolding we build now is the migration
bridge; saves written today by v0x0013 will still load on a future
v2 binary via a one-time `Amiga LUT → JSON` migrator.

### TODO / FUTURE

- **DOS version support.** SWOS DOS uses a different binary layout
  with a different address space, but the same logical save fields. To
  read DOS `.CAR` files, parse the equivalent DOS listing/map and
  build a second LUT (`amiga_addr` becomes `dos_addr` for that table).
  Detect on load via `saveFileAmigaType` value (0x00100000 = Amiga,
  TBD = DOS) and route to the appropriate LUT. The DOS map would
  share categories A–E but with different absolute addresses; the
  symbol names are identical.
- **`.HIL` pointer-field audit** — replay frames may carry sprite
  pointers that need LUT entries (Category F).
- **Trailing 4-byte slots inside cup *Copy records** — the
  `b8c85c56 / 44c95c56 / …` trailers flagged in the .CAR section need
  per-record verification before they're declared real pointers.

The JSON save format (`src/save_format_json.c`, on by default) is the only
path for `.CAR`/`.SEA`/`.PRE`/`.DIY` — pointer fields are emitted as semantic
identities rather than raw address bytes, so saves round-trip across rebuilds,
32/64-bit and LE/BE. ~~Load auto-detects format so legacy `.CAR` dumps still
open via the raw-load + LUT bridge~~ — that fallback is deleted (2026-06-10/11):
a non-JSON career/preset/DIY file is refused with an "unsupported format"
error (`src/file_io.c`). `RebuildPointersAfterLoad()` stays, re-deriving each
pointer from its saved JSON identity. `.TAC`/`.HIL` still auto-detect JSON vs
binary on load (see the policy section above).
