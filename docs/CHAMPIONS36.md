# CHAMPIONS36.md — the 36-team european presets (modern UEFA formats)

Status 2026-10-26: implemented and verified on both targets. SDL-first PoC of
the modern formats; career mode keeps the legacy competition formats.

## What it is

**PRESET COMPETITION → EUROPE** offers the modern three-tier club pyramid —
the chooser lists them in tier order:

| preset button | replaces | compId | clubs |
|---|---|---|---|
| **CHAMPIONS LEAGUE** | European Champions Cup (`_euroCup`) | 129 | top 4 of Italy/England/Germany/Spain/France, top 2 of Portugal/Holland, + the 12 strongest champions of the other leagues |
| **EUROPA LEAGUE** | EUFA Cup (`_uefaCup`) | 130 | the strongest clubs left after the CL picks, max 3 per country |
| **CONFERENCE LEAGUE** | European Cup-Winners Cup (`_cupWinnersCup`) | 131 | the tier below that, max 3 per country |

All three play the same format:

1. **League phase** — all 36 clubs in ONE table; each club plays **8 games**
   (8 distinct opponents, 4 home / 4 away), 144 matches, 3 points a win.
2. Ranks **1–8** go straight to the last 16; ranks **9–24** contest two-leg
   **play-offs** (24 v 9, 23 v 10, … 17 v 16, unseeded club at home first);
   ranks **25–36** are out.
3. **R16 → QF → SF** two-leg with extra time + penalties, away goals OFF,
   then a single **final**. 189 matches in all.

Club strength is the game's own metric (sum of the 16 players' price bytes,
as `DIY_ComputeAverageTeamStrengthTier`); the tiers were generated from the
team database (`assets/data_json/teams`) — regenerate with a ranking script if
the database changes. UEFA's seeding pots, country separation and draw
ceremony are NOT modelled: the schedule is a deterministic balanced rotation
over the per-run shuffled entry order (so opponents differ each run), the
play-off/R16 paths are fixed rank-based pairings keeping ranks 1 and 2 in
opposite halves. Standings use the game's own tie-breaks.

## The renames

The legacy records' DISPLAY names changed globally (`src/strings_data.c`):
`_aEuropeanChampi_1` → CHAMPIONS LEAGUE (`_aEuropeanCup` short → CHAMPIONS L),
`_aEufaCup` → EUROPA LEAGUE (`_aEufaCup_0` → EUROPA L), `_aEuropeanCupWin` →
CONFERENCE LEAGUE (`_aEuroCwCup` → CONFERENCE L). Career seasons therefore
SHOW the new names but still PLAY the legacy formats (16-team group cup /
32-team knockouts) — nothing keys off the text, because the presets are
detected by compId (below). Translations were regenerated
(`scripts/export_translations.py` + `gen_translations.py`; the new names are
English-fallback proper nouns). The chooser's tier order is the europe region
table — `_europeTable` in `src/competition_data.c` AND the europe `comp_cup`
list in `bin/data_json/competitions/qualifying.json`, which must stay
identical (the loader verifies region tables byte-for-byte).

## Scope guard — what is NOT changed

- **The legacy records, their career copies, cups.json/tournaments.json and
  the TMD files are untouched.** `Champions36_PresetRecord` substitutes a
  separate packed 36-seed tournament record, identity-checked against the
  three legacy record addresses, and only on the preset path.
- **Detection is by compId, never by name:** the substitutes carry their own
  ids 129–131 in the DIY buffers' `field_2E` (serialized with every save),
  which career never produces — so the career euro cups safely share the new
  display names, and `Champions36_Is()` is one range test
  (`include/comp_identity.h`).
- `CompetitionTournament.teamSeedList[32]` is NOT resized — the substitute is
  its own packed struct with 36 seed entries at the same header/name/seed
  offsets (compile-time asserted).
- No TMD pool is loaded for a substituted preset: all 36 curated clubs
  resolve from their `TEAM.<country>` files (the AppendTeams fallback), i.e.
  their league rosters.

## How to run it

- UI: main menu → PRESET COMPETITION → EUROPE → pick a tier → PLAY
  TOURNAMENT. Works the same on the Amiga build.
- `bin/SWOS.exe --champions36-selftest` (SDL only) proves the schedule, all
  three substitutes (ids, 3×36 unique seeds, no club in two competitions),
  the full 189-match engine run on the Champions League (league → play-offs
  with top-eight byes → R16 → QF → SF → final), and a preset save/load at two
  points. All three lines must PASS and both `[json-verify-preset]` lines
  must read **0 mismatches**.

## Implementation map

New: `include/champions36.h`, `src/champions36.c` (shared; the self-test and
its stdio are `SWOS_SDL_BUILD`-guarded — the Amiga support libc has no stdio
and no `strstr`, so nothing here uses libc beyond memcpy).

| piece | where |
|---|---|
| the three substitutes (36 seeds, 6 stages, rules, titles) | `Champions36_PresetRecord` + the `k_tiers`/seed tables (champions36.c) |
| substitution hooks | `SelectTeamsFinalMenu`'s contest-type dispatch (teams.c — the substitute must be dispatched as type 2, while `_selectedContest` keeps the original for the continental-cup/club checks) and `PresetCompetitionMenu` before ITS type dispatch (menus.c) |
| detection | `Champions36_Is` — `field_2E` in `[COMPID_championsLeague .. COMPID_conferenceLeague]` |
| fixtures | `Champions36_Fixture` — eight rounds of the circle method, round-pairs oriented as alternating cycles ⇒ exactly 4 home + 4 away; reproducible from the match index, no draw state to serialize |
| league shape | `Champions36_ConfigureLeague` (144 matches, 8 matchdays, 18/matchday, 3 pts) + hooks in `DIYTournament_RecalculateMatchesPerTeam`, `DIY_RecomputeCompetitionDerivedFields`, `getCurrentFixtureTeamPtrs` (bypasses the 2–24 fixture-schedule table) |
| stage advancement | `Champions36_Advance` (from `DIY_AdvanceToNextCompetitionSlot`, career_teams.c): league end stashes ranks 1–8 in `teamIndexOrder[56..63]` (an unused, already-serialized tail — no save-schema change) and seeds the play-offs with ranks 9–24; play-off end reinserts the byes into the R16 bracket; later rounds keep the bracket |
| the six stages | `rounds[6]` = 36/16/16/8/4/2 with `roundStringBase = 6` (the loader's stage count) and `header.reserved = 1` — byte 33 is the SIXTH stage's trailing qualifier count when all six rounds are used (the World Cup does the same) |
| standings UI | two pages of 18 (ranks 1–18 / 19–36) on the existing scroll buttons; rank zones coloured 1–8 / 9–24 / 25–36 (diy_competition.c) |
| roster backup | `SELECTED_TEAMS_CAPACITY` 32→64 in career_teams.c (`_selectedTeams[100]` has room; the restore guard scales with it) |

## Saves

Preset saves (`.PRE`, JSON) carry everything: the stashed byes live inside the
already-serialized `teamIndexOrder`, and detection re-derives from the saved
`field_2E`. The round-trip verifier ignores exactly one range:
`stageSnapshot`'s `bracketScratch.teamOffsetTable`, a DERIVED cache the loader
rebuilds id-based over all master teams while a live cup stage populated only
its participants. With the ignore in place **every** preset verify must be 0
mismatches — any count at all is a regression
(`verify_ignore_stage_team_offsets`, save_format_json.c).

> Saves made by the interim "CHAMPIONS CUP 36" build (compId 0 + title-based
> detection) are STALE: they load, but the modern-format hooks no longer fire
> for them. Delete them.

## Verified

- `--champions36-selftest`: schedule, 3×36 disjoint curated seeds with marker
  ids, 189-match engine run, save/load at two stages — PASS, 0 verify
  mismatches.
- Real UI (SDL, swos_drive.ps1): the renamed chooser in tier order; each of
  the three opens with its 36 curated clubs resolved from team files; league
  screens titled CHAMPIONS/EUROPA/CONFERENCE LEAGUE with two-page standings
  and zone colours; matches simulate and re-sort; the earlier CHAMPIONS CUP 36
  run drove play-offs → R16 (byes merged correctly on screen) → QF → SF →
  final → back to the main menu's CONTINUE/SAVE rows, which carry the new
  names. Regression: the WORLD CUP preset keeps its legacy 24-nation group
  format, the SEASON flow and career screens are unaffected.
- Amiga: `Makefile.amiga` builds clean (the interim 36-team build was also
  played under FS-UAE by the user; the three-tier build is the same shared
  code).

## Known cosmetic limits

- COMPETITION INFO prints the generic derivations "TOP 16 IN GROUP QUALIFY"
  (really top 8 + play-offs for 9–24) and "PLAY EACH TEAM × 1" (really 8 of
  35 opponents). The structure rows and rules are correct.
- Round names are the generic "FIRST/SECOND/THIRD ROUND…" strings; the
  play-off round is "SECOND ROUND".

## Possible next steps

UEFA seeding pots + country constraints in the league draw; a real R16 draw
instead of the fixed rank paths; UEFA's full tie-break list; proper stage
names ("LEAGUE PHASE", "PLAY-OFFS"); data-driven tier records (the compIds
129–131 already fit the dynamic-competition range) instead of substitution.
