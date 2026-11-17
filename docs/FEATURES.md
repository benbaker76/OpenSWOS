# Features / enhancements (beyond the faithful port)

Forward-looking features for OpenSWOS — additions on top of a faithful SWOS
port. Each is a grounded design sketch (with the relevant code already located),
not yet implemented. The Amiga game stays the definitive base; these are opt-in
enhancements.

> Status (2026-09-30): features 1 and 2 have landed (notes under each); feature 3 is
> partly done. The sketches below are kept as the original design record — the
> file:line references in them are from 2026-06 and have drifted.

---

## 1. Copa Libertadores (South American Champions League)

Add the **CONMEBOL** equivalent of the European Cup / Champions League — a
continental club cup contested by South American league champions.

> **Status: done** (206ffd3, 580ca70, 653918b and later fixes). Detailed design in
> `docs/COPA_LIBERTADORES.md`. It went the preferred way: a pure-data competition in
> `bin/data_json/competitions/tournaments.json`, installed at runtime by
> `DataJson_InstallCompetitionGraph` on SDL and baked for the Amiga in
> `src/competition_data_copa_baked.c`; the career side is `src/career_copa.c`.

### Why it's tractable
The engine already runs **three continental club competitions**, all defined as
`CompetitionTournament` / `CompetitionCup` records in `src/competition_data.c`:
- `euroCup` (European Cup, `competition_data.c:196`)
- `cupWinnersCup` (`:221`)
- `uefaCup` (`:261`)

Each is a fixed-shape record: a bracket descriptor (e.g. `{16,4,4},{8,0,8},…` round
sizes), name pair (`{aEuropeanChampi_1, aEuropeanCup}`), and a **16-team seed
list** of `{ country, teamIndex }` pairs (e.g. `{ CN_ITALY, 0 /* AC MILAN */ }`).
The career layer mirrors each into a per-career `*Copy` record
(`career_comp_copies.c`) and drives qualification/draw in `career_eurocups.c`.

### Plan
1. **Competition record**: define `CompetitionTournament copaLibertadores` in
   `competition_data.c`, same shape as `euroCup`, seeded with South-American
   champions (`CN_BRAZIL`, `CN_ARGENTINA`, `CN_URUGUAY`, … — SWOS already has
   South-American nations; `tourStringTable` includes `aSouthAmericanT`).
2. **Career integration**: add `copaLibertadoresCopy` via the `career_comp_copies.c`
   pattern and register it in `careerContests[]` (cap 35 — room available).
   Generalise the `career_eurocups.c` qualification/draw to be **confederation-
   parameterised** (European league finishers → euro cups; South-American league
   finishers → Libertadores) rather than hard-wired to Europe.
3. **Strings/menus**: `aCopaLibertadores` etc. (`strings_data.c`) + menu entries +
   a `TRANSLATIONS.md` key.
4. **Save**: a new `*Copy` slot in the career overlay — **or, preferred**, define
   it through the data-driven competition graph (feature 3) so it's pure data, no
   new overlay slot.

### Constraints (see `docs/DATA_CUSTOMIZE.md` caps)
- `tmdFileBuffer` = `TeamFileEx[80]` — the continental-cup team pool cap.
- `careerContests[35]` — career competition index cap.
- 16-team seed list shape (or group+knockout like the World Cup type-2 path).

**Best built on feature 3** — Copa Libertadores as a JSON competition definition
rather than a hardcoded record.

---

## 2. Configurable career length (default 20, e.g. 35 seasons)

Career mode currently runs a **fixed 20 seasons** then forces retirement. Make the
length a **setting** (default 20; allow e.g. 35).

> **CAREER YEARS (SWOS United, 2026-11-17):** the length is now the player's, per career: a grey
> CAREER YEARS row under START CAREER AS on the career screen, its blue toggle 20 / 30 / 40
> (`CareerYearsToggle`, career_init.c; entries 12 / 13 of `careerMenu`, appended; SDL only -- hidden
> on the Amiga, whose careers stay 20). A new career starts on `career_params.json`'s `num_seasons`
> (`_careerYearsDefault`), never on a loaded career's. The career save keeps it (`career_years`,
> written only when not 20, so every 20-year save is what it was; a save without it is 20, and one
> already past its length is lengthened on load). `CAREER_MAX_SEASONS` is 40; a save writes 35 season
> rows, a 40-year career's 40. Proved: a 40-year career played to its end (season 40, retired) by the
> career autopilot.
>
> **Status: done** (c011c12). `num_seasons` in `bin/data_json/career_params.json`
> (`_careerParams.num_seasons`, default 20, clamped to `CAREER_MAX_SEASONS` = 35 in
> `include/career_params.h`) — a data knob, not the `[career] length` INI setting or
> `--career-length` flag sketched below. `_seasonInformationsArray` is `[35]`, the three
> `career_init.c` sites are parameterised, and the save writes 35 season rows
> (`SAVE_SI_ENTRY_COUNT`) and stores `num_seasons`. The two `menus.c` final-season
> checks (`CareerOverUpdateJobOffersEntry` and its caller) still compare with a literal
> `19`.

### Current mechanism
`currentSeasonShowing` (`career_status_flags.c`) counts seasons `0..19`;
retirement fires at 19, the counter wraps at 20.

### Sites to parameterise (with a new `careerLengthSeasons`)
| File:line | Current | Change to |
|---|---|---|
| `career_init.c:281` | `if (currentSeasonShowing == 19)` → retirement | `== careerLengthSeasons - 1` |
| `career_init.c:971` | `if (currentSeasonShowing == 20)` → wrap to 0 | `== careerLengthSeasons` |
| `career_init.c:121` | `divisor = 20 - currentSeasonShowing` (transfer quota) | `careerLengthSeasons - currentSeasonShowing` — **must** change, or at season ≥20 the divisor goes ≤0 → divide-by-zero / garbage quota |
| `menus.c:3988` | `if (currentSeasonShowing == 19)` → hide entry (final season) | `== careerLengthSeasons - 1` |
| `menus.c:4106` | `if (currentSeasonShowing == 19)` → final-season UI | `== careerLengthSeasons - 1` |

### The big one — per-season history array
`seasonInformationsArray[20]` (`career_season_info.c`) is the per-season club
history table, **indexed by season number**. At 35 seasons, season ≥20 writes past
the end into adjacent globals → silent corruption → a panic somewhere unrelated
later (exactly the failure class `.claude/CLAUDE.md` warns about). It **must** grow
to the maximum supported length (e.g. `[35]`). This is the load-bearing change.

### Save-format impact
- Store the chosen length **with the career** (a save knows its own length).
- Decide career length at **career start** and keep it immutable for that career
  (changing mid-career would desync the season array / quota formula).
- Size `seasonInformationsArray` to the **max** supported (e.g. 35); serialize only
  the used seasons. (`SeasonInformations` is already typed in the JSON save — see
  `save_format_json.c` — so growing the count is a serializer change, not a raw
  layout hack.)

### Setting
`[career] length=20` (INI) / `--career-length=N`, range `1..35` (35 = the size we
allocate arrays for). Default 20 to match the original.

### Watch-out
The transfer-quota formula's other term `(7 - careerSeasonsCompleted) * 16` is an
early-career ramp; extending the horizon just spreads quota over more seasons —
verify it still feels balanced at 35.

---

## 3. Easy add/remove of Countries, Leagues, Cups, Teams

Adding or removing a country / league / cup / team should be a **simple, safe,
no-recompile data edit**.

> **Status: partly done.** The foundation below is live, and Copa Libertadores
> (feature 1) is the first competition defined purely by data. The validation layer,
> schema and `check_data.py`-style validator in "What's left" have not been built.

### Foundation already exists
The `SWOS_DATA_JSON` data-drive is well underway:
- `DataJson_InstallCompetitionGraph()` (`frontend.c`) rebuilds the normal-country
  competition tables from `data_json/` **at runtime** — done for all 65 normal
  countries (see the data-json wire-in work).
- Teams loader + competition loaders are data-driven; `bin/data_json/` holds the
  exported data; `SWOS_DATA_JSON_SELFTEST=1` validates the loaders.
- Docs: **`DATA_JSON.md`** (the data→JSON migration), **`DATA_INTERNAL_JSON.md`**
  (internal data format), **`DATA_CUSTOMIZE.md`** (the "make edits *safe*" design),
  `DATA_BSON.md`.

### What's left to make it "easy + safe"
1. **Finish the data-drive**: some data is still compiled-in (per the wire-in
   notes: region tables, tournaments, playoffs). Move the remainder to JSON so a
   country/league/cup/team is *fully* defined by data.
2. **Load-time validation layer** (`DATA_CUSTOMIZE.md`): a missing/extra/wrong
   entry must produce a **clear error at the door** ("competition `euroCup`
   references team `{CN_X, 99}` which does not exist"), never a crash or a panic
   three menus later. Validate once at load; past the door the engine keeps
   trusting its invariants.
3. **Caps**: respect or grow the fixed arrays — `allCountriesList[66]`,
   `careerContests[35]`, `tmdFileBuffer = TeamFileEx[80]`, `TeamFileEx.players[16]`
   (exact, not a max). Adding past a cap overflows; validation must catch it (or
   the cap must grow with a save-format bump).
4. **Schema + tooling + workflow doc**: a documented "how to add a country" flow,
   a JSON schema, and a validator (`check_data.py`-style) so non-programmers can
   edit safely.

### Why it matters
This is the **enabler** for feature 1 (Copa Libertadores becomes a JSON
competition, not hardcoded) and complements feature 2. It's the "jailbreak" that
turns SWOS from a fixed dataset into a moddable platform.

---

## Cross-cutting notes
- All three are **opt-in** and must not regress the faithful Amiga base.
- Features 1 and 3 are tightly linked — do the data-drive first, then Libertadores
  rides it.
- Anything touching the career save (1 and 2) needs a coordinated save-format
  change that works on **both** targets (SDL and Amiga, one shared tree since the
  2026-08-12 merge — see `.claude/CLAUDE.md`), with a version bump and back-compat
  handling.
- Translations: new competition/menu strings go through `docs/TRANSLATIONS.md`.

## Open questions
- Copa Libertadores format: straight 16-team knockout (like `euroCup`) or
  group-stage + knockout (like the World Cup type-2 path)? Which CONMEBOL nations
  /clubs seed it?
- Career length: hard max — is 35 the ceiling, or do we size for more? Per-career
  immutable, or allow changing between seasons?
- Data-drive: ship `data_json/` as the canonical data (retire the compiled
  tables), or keep compiled tables as a fallback?
