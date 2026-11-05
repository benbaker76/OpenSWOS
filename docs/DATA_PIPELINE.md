# SWOS data pipeline — JSON as the source of truth

SWOS's game data — competitions, menus, teams — originated as hand-written C tables
(`competition_data.c`, `menu_data.c`) and RNC-compressed binary files (`TEAM.NNN`,
`*.TMD`, `POOLPLYR.DAT`). All of it is now **editable as JSON** under `bin/data_json/`,
with tooling that converts each direction **losslessly** (round-trip / byte-identical), so
you edit JSON and the change reaches the build — on both targets (SDL and Amiga build from
the one `src/` tree since the 2026-08-12 merge).

This is the umbrella doc for the whole pipeline. Deep-dives:
- `docs/DATA_JSON.md` — the teams/TMD/POOLPLYR **binary** layouts + the RNC details.
- `docs/SAVE_DATA.md` / `SAVE_FORMAT_JSON.md` — the **save-file** JSON (a separate format).

---

## Directory layout

```
bin/data_json/
  competitions/   leagues, cups, tournaments, playoffs, qualifying, league_blocks,
                  career, country tables   →  src/competition_data.c     (committed source)
  menus/          one *.json per menu + _symbols.json  →  src/menu_data.c  (committed source)
  menus/swos2020/ imported SWOS 2020 menus — JSON-only, loaded at run time, never baked
                  (docs/MENUS.md §10)
  menus/p2p/      OpenSWOS's own JSON-only menus (p2pMenu, rateMatchMenu)
  tactics/        <id>.json (+ <id>_original / <id>_dos variants)  →  src/tactics_data.c
  teams/          <COUNTRY>.json (→ TEAM.NNN), EUROCUP/EUROCWC/UEFACUP.json (→ *.TMD),
                  POOLPLYR.json (→ POOLPLYR.DAT)
  translations/   <lang>.json  (UI string localization, docs/TRANSLATIONS.md)
  career_params.json  career-mode balance knobs (below)
```

`competitions/`, `menus/` and `tactics/` are **committed** and are the source of truth for
their `.c` files. `teams/` and the binary `bin/data/` files are both tracked too
(`.gitignore` re-includes them); `teams/` is the editable view that `emit_teams.py`
regenerates the binaries from. Only `bin/data_json/menus/*.json` (top level) feeds
`menu_data.c` — the `swos2020/` and `p2p/` subfolders are deliberately outside that
wildcard.

---

## Two mechanisms — runtime data-drive vs build-time bake

There are **two** independent ways the JSON reaches the game; don't confuse them:

1. **Runtime data-drive (SDL only).** With `SWOS_DATA_JSON` / `SWOS_MENU_JSON` /
   `SWOS_TACTICS_JSON` (all default-on; `=0` turns one off), the SDL build loads the JSON at startup and **overwrites the compiled
   data in place** — the fast edit-and-rerun loop, no rebuild.
2. **Build-time bake (the "flip", both ports).** At build, the Python emitters regenerate
   `competition_data.c` / `menu_data.c` / `tactics_data.c` **from the JSON**, then the
   compiler compiles them. The committed JSON is the source; the `.c` is a generated
   artifact. The Amiga build compiles the same generated C from the shared `src/`. This is what makes a JSON edit a permanent,
   cross-port change.

The two share the JSON and produce identical bytes; (1) is for iteration, (2) is the
source of truth.

---

## The editable JSON format (field splits)

The raw SWOS structures pack several logical values into one byte/array. The JSON splits
those into named fields so they're editable; the converters recombine them losslessly.

**Player** (`teams/*.json`):

| Packed source | Editable fields |
| --- | --- |
| `velocity_heading` (nibble pair) | `velocity`, `heading` |
| `tackling_control` | `tackling`, `control` |
| `speed_finishing` | `speed`, `finishing` |
| `position_and_face` | `position` (0–7 role), `face` (0–3), `flags` (inform/loan/trialist) |
| `cards_injuries` | `cards`, `injury`, `status_bucket` |

> Skill values are the raw **0–15** nibble; the match engine masks to bits 0–2 (**0–7**),
> so some original players read 8–15 (high bit set-but-ignored). Edit within 0–7.

**Team** (`teams/*.json`): `kit_colors[10]` → `primary_`/`secondary_` ×
`{shirt_type, stripes_color, basic_color, shorts_color, socks_color}`.

**Competition** (`competitions/*.json`):

| Packed source | Editable fields |
| --- | --- |
| `subs_and_points_for_win` (cups/tournaments) | `substitutes`, `points_for_win` |
| `subs_and_max_subs` (leagues/playoffs) | `substitutes`, `max_subs` |
| `round_rule_flags[]` | per-round `{ legs, extra_time, penalties }` (+ `low` when bits 1-0 set) |

**Menu** (`menus/*.json`): the `colors` op byte → `frame_color` (hi nibble) +
`background_color` (lo). Coords (`x/y/w/h`), nav (`left/right/up/down`), and commands are
already named ops.

**Left raw on purpose:** `begin_month`/`end_month` is a single ⅛-month calendar position
(not a packed pair; some values are half-months), and `team_controls` is a 0/1/2 enum, not
a bitfield — neither is a meaningful "split".

---

## Scripts

Export = C/binary → JSON (the bootstrap). Emit = JSON → C/binary (the source-of-truth
direction). `swos_team_file.py` is the shared team-data library (decode/encode + RNC).

| Direction | Competitions | Menus | Teams / TMD / POOLPLYR |
| --- | --- | --- | --- |
| **export** (→ JSON) | `export_competitions.py` | `export_menus.py` | `decode_data.py --dump-json` |
| **emit** (→ C/binary) | `emit_competitions.py` | `emit_menus.py` | `emit_teams.py` |

Tactics follow the same pattern: `export_tactics.py` / `emit_tactics.py` ↔
`src/tactics_data.c` (runtime loader `src/tactics_load_json.c`, `SWOS_TACTICS_JSON_VERIFY`
for the byte-exact check). Translations: `export_translations.py` writes `en.json`,
`gen_translations.py` the other languages.

How each emitter works:

- **`emit_competitions.py`** — splices regenerated `CompetitionCup/Tournament/League/
  Playoffs/QualifyingGroup/Leagues/CompetitionCareer` definitions + the country tables and
  `competitionsTable[256]` index into `competition_data.c`'s preamble. Recombines the split
  subs / round-rule bytes. 246 definitions.
- **`emit_menus.py`** — for each menu, parses the **original brace structure** out of
  `menu_data.c` (the bespoke per-menu type dictates it — flat vs entry-wrapped) and
  substitutes the JSON values into the leaves, re-emitting hand-written-looking C.
  Recombines the colors nibble. Pointer rule: dotted field-refs + sprite-copy structs take
  `&`; function/string/table names decay.
- **`emit_teams.py`** — `encode_*` the dict → raw blob → `rnc_pack` (v1 for `TEAM.NNN`,
  v2 for `*.TMD`/`POOLPLYR.DAT`). `rnc_pack` reproduces the original RNC compression
  byte-for-byte, so the output is 1:1 with the originals. (Binary layouts: `DATA_JSON.md`.)

---

## Workflows

### Competitions / menus / tactics (C — the flip)

```sh
# edit bin/data_json/competitions/*.json, menus/*.json or tactics/*.json
make                       # regenerates competition_data.c / menu_data.c / tactics_data.c, then builds
make regen-data            # force the regen without a full build
git add bin/data_json/... src/competition_data.c src/menu_data.c src/tactics_data.c   # commit JSON + generated C
```

The SDL Makefile rule makes each `.c` depend on its JSON, so editing the JSON regenerates the
`.c` at build (via `emit_*.py --out`). `Makefile.amiga` has **no** regen rule: it compiles the
committed `.c` from the shared `src/`, so run `make regen-data` (or an SDL build) before an
Amiga build that should see a JSON edit.

### Teams / TMD / POOLPLYR (binary)

```sh
# edit bin/data_json/teams/*.json
python3 scripts/emit_teams.py            # VERIFY 1:1 vs the current bin/data files (no write)
python3 scripts/emit_teams.py --write    # regenerate the RNC-compressed bin/data files
```

Commit the JSON and the regenerated binaries together. Refresh the JSON from the binaries with
`python3 scripts/decode_data.py --dump-json bin/data_json/teams`.

### National teams (`national_teams.json`, a mod's)

The national teams' records and the career manager's nationalities are compiled (the original's
63 records -- of the 144 its table holds, the rest without nationality or team file -- and 63
nationalities + OTHER; include/national_teams.h). A mod may bring
`data_json/national_teams.json` through the overlay (SDL; OpenSWOS ships none):

```json
{ "records": [ { "country": 80, "ordinal": 0, "nationality": 0, "team_file": 0, "skill": 19 }, ... ],
  "nationalities": [ 8, 31, ... ],      // the manager's choices; OTHER (255) is added last
  "job_offer_pick": 41 }                // the first records a random national job is drawn from
```

The counts are the arrays' lengths (at most 256 records, 127 nationalities on SDL; the Amiga
keeps the original's). `team_file` 255 means no club league in the game: that national
selector's ADD PLAYER has no HOME, only ABROAD. A file that does not parse is refused whole,
said in `[data-json]`, and the original's tables stay. Order matters where the original's does:
a nationality's national team is the first record with it (`GetNationalTeamNumber`).

### Career-mode balance knobs (`career_params.json`)

`bin/data_json/career_params.json` exposes career-mode economy + board-expectation
constants that used to be hard-coded — **starting balance**, **gate receipts**, **wages**,
and **chairman-verdict thresholds**. It's loaded at startup over the compiled defaults
(which equal the original SWOS values), so editing a value and relaunching changes the
running game with **no rebuild** (and no `SWOS_DATA_JSON` flag needed). A removed field
falls back to its default, so behaviour is identical until you change a knob. The loader is
`DataJson_LoadCareerParams` (SDL); the defaults live in `src/career_params.c` and are used
as-is on the Amiga build (which doesn't run the loader). See the file's inline comments for
what each knob does.

> Season **count** (20) is *not* a knob here — it's a compile-time array size tied to the
> save-file layout, so changing it needs a code + save-format change, not a JSON edit.

**List capacities** (`foreign_pool_capacity` 432, `foreign_listing_capacity` 15,
`transfer_list_capacity` 5, `job_offers_capacity` 5 -- the original's; `include/career_capacity.h`):
how much of a career list a career uses. The arrays are compiled at the SDL build's maximum
(50,000 / 45 / 10 / 20) and the Amiga's at the original's, and the loader clamps a value to
[original, maximum]. Every bound reads the param. The save writes what a list HOLDS, not its
capacity: a list's storage past the original's size is written only when something is in it
(`emitted_len`, the pool's and job offers' emitters), so a default career's save is the one it
always was -- the golden careers prove it. A save holding a longer list loads whole (up to the
maximum) whatever the param; the param only decides how far a list may grow.
`trialist_count_mode` (0, the original's): 1 counts injured AND suspended players out in every
non-training match when deciding on trialists (benefactorr's Career+).

**Finances beyond the original's knobs** (each default the original's; Career+'s value in
brackets): `wage_pct` 100 [40], `team_value_wage_pct` 100 [250] (the team value -- and so the
gate and the running costs -- made from more than the wages), `running_cost_mode` 0 [1: halved,
then progressive brackets above 20,000] and `running_cost_pct` 100 [45], `league_gate_mult` 1
[100] (the career club's league home gate), `sponsorship_mode` 0 [1: every season, the weekly
wages times `sponsorship_weeks` by division, 18/14/10/6], `league_distribution_weeks` 0/0/0/0
[9/7/5/3: the league's money, booked with the competition bonuses], `profit_loss_sponsorship` 0
[1: the sponsor counted in the season's profit / loss]. A param at its default runs the
original's code, so the golden careers are unchanged.

**Transfers** (Career+'s in brackets): `transfer_quota_mode` 0 [1: a fixed `transfer_quota` 256
a season, half for sales and half for buys, and buys then limited only by the pool's room --
the original's formula budgets from 432, so a bigger pool left it at 0 and stopped every bid],
`foreign_refill_mult` 1 [3] (at most 3: the listing's 45), `listed_bid_chance` 11 [36] in 256,
`foreign_bid_chance` 8 [64] in 256 (a bid from abroad rather than home), `domestic_buyer_mode` 0
[1: also a home club with a player priced at half the listed one's or more].

**Job offers** (Career+'s in brackets, with `job_offers_capacity` 20): `job_offer_expiry` 1 [0],
`job_offer_first_week` 0 [20] (routine offers from that week; sacked or searching, at once),
`job_offer_waves` 0 [1: none before week 20, then 6, 12 from week 25, 19-20 from week 30],
`job_offer_stop_season7` 1 [0] (the original's stop once 7 seasons are complete),
`job_offer_phase_gate` 1 [0], `job_offer_own_national` 1 [0: only when searching],
`job_offer_pass` 42 [220] in 256, `job_offer_rep_high_pct` 100 [125] and `job_offer_rep_low_pct`
75 [55] (the clubs' reputation window), and three routes of Career+'s, each 0 off, in 256:
`job_offer_selector_clubs` [96] (a national selector's: a national job, else a club abroad),
`job_offer_national_chance` [192] (a club manager two seasons in, the board not against him,
looked at for national jobs), `job_offer_abroad_chance` [128] (a club offer from abroad first).

---

## Verification

Every converter is checked so a silent corruption can't slip in:

| Asset | Faithfulness check |
| --- | --- |
| Competitions | `python3 scripts/emit_competitions.py` → emit → re-export → diff JSON (**0 diff**). Runtime: `SWOS_DATA_JSON_SELFTEST=1 ./SWOS.x86_64` (loaded-from-JSON == compiled). |
| Menus | `python3 scripts/emit_menus.py` → round-trip (**0 diff**). Byte-exact: `SWOS_MENU_JSON_VERIFY=1 SWOS_MENU_JSON=1 ./SWOS.x86_64` → builds each menu from JSON with raw symbols and memcmps vs the compiled bytes (**47/47 byte-identical**). |
| Teams | `python3 scripts/emit_teams.py` → emit + `rnc_pack` → byte-compare vs the originals (**76/76 1:1**). |

The round-trip is the key idea: `export(emit(JSON)) == JSON` ⇒ the generated C/binary is
semantically identical to the original. `--out` on the C emitters skips the (slow) verify
for fast builds — the compiler catches syntax, the bare-script run does the faithfulness
check. The regen is **idempotent** — re-running it is byte-identical, so a build never
dirties the tree even though the `.c` is generated.

---

## The build wiring

`Makefile`:

```make
COMP_JSON := $(wildcard bin/data_json/competitions/*.json)
MENU_JSON := $(wildcard bin/data_json/menus/*.json)

src/competition_data.c: $(COMP_JSON) scripts/emit_competitions.py scripts/export_competitions.py scripts/swos_team_file.py
	python3 scripts/emit_competitions.py --out $@

src/menu_data.c: $(MENU_JSON) scripts/emit_menus.py scripts/export_menus.py
	python3 scripts/emit_menus.py --out $@

regen-data:        # force both
	python3 scripts/emit_competitions.py --out src/competition_data.c
	python3 scripts/emit_menus.py        --out src/menu_data.c
```

(Abridged: the real rules also cover `tactics_data.c` from `bin/data_json/tactics/*.json`
via `emit_tactics.py`, and `regen-data` regenerates all three.)

The regen needs **python3**; without it on PATH the Makefile warns and compiles the committed
`.c` as-is (a JSON edit then does not take effect). `competition_data.c` / `menu_data.c` are read by the
emitter for their preamble (includes/stubs) and rewritten with the JSON-derived
definitions — so they must stay committed (the preamble isn't in the JSON).

---

## The Amiga target

The Amiga build compiles the **generated** `competition_data.c` / `menu_data.c` /
`tactics_data.c` from the same `src/` (one tree since the 2026-08-12 merge; there is no sync
step) — so a JSON edit, once regenerated, is in the Amiga build too. The emitters bake JSON→C at *build* time (not runtime) because parsing
JSON on a 68k at startup would take minutes; the generated C compiles natively. RNC
(`rnc_pack`/`rnc_unpack`) is byte-exact for v1 (`TEAM.NNN`) and v2 (`*.TMD`,
`POOLPLYR.DAT`), so regenerated team binaries are indistinguishable from the originals on
either platform.

---

## Gotchas / key facts

- **Two mechanisms, one JSON.** Runtime data-drive (`SWOS_DATA_JSON`/`SWOS_MENU_JSON`/`SWOS_TACTICS_JSON`,
  SDL, edit-and-rerun) vs build-time bake (the emitters, both ports, the committed source).
  They produce identical bytes.
- **The `.c` is generated — edit the JSON.** `competition_data.c` / `menu_data.c` are
  regenerated; hand-edits to them are overwritten on the next regen. Edit `bin/data_json/`.
- **Menu structural edits need a type change.** `emit_menus.py` substitutes values into the
  existing brace template, so adding/removing an entry (changing the leaf count) raises —
  it needs a matching `menu_data.h` per-menu type change too (see MENUS.md §1e).
- **Skill nibbles are 0–15 but the engine uses 0–7.** Edit within 0–7 for predictable play.
- **`emit_teams.py` defaults to verify, `--write` to overwrite.** Always 1:1 with the
  originals; a reported `DIFF` means the JSON and binary have diverged — re-export.
- **The flip's first commit reformats the `.c`** (one-time: macros→numbers, normalized
  layout; comments/structure kept). Subsequent JSON edits produce minimal diffs.
