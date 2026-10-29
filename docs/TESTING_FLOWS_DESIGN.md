# Testing Flows: design and implementation

Status: **implemented and validated on both targets** (2026-10-23). All five
delivery steps are done; see "Implementation status" at the end for what
exists, how to run it, and the deliberate capability gaps. The design text
below is kept as the specification the implementation is held to.

## Objective

Let a person or coding agent describe a SWOS task, then execute it through the
existing debugger harness using game state as feedback. Different teams,
countries, competitions and combinations of menu actions must be data, not new
scripts.

Examples of requests:

- “Open career of England and Arsenal.”
- “Go to career, go to Yugoslavia and choose Partizan Belgrade.”
- “Start a friendly between Italy player and England CPU.”
- “Open the squad, inspect the goalkeeper, then return to the career hub.”
- “Load my Arsenal career and save it under a new name.”

The system is generic across supported SWOS menu flows. It is not a promise that
an unseen game mechanic can be operated without teaching the adapter how to read
and act on it. Adding a new primitive or screen model can be necessary; adding a
new country, club, or sequence of already supported actions must not require code.

## Constraints

1. All new implementation lives outside game code, in Python tools and tests.
   Reuse existing symbols and debugger hooks; no new in-game API or instrumentation
   is required by this design. One narrow exception is permitted as a last resort:
   if no existing symbol proves a stable boundary (the match-loop boundary is the
   known risk), a one-line no-op anchor function in shared code — the
   `MenuInputInjectPoint` pattern, which exists precisely because symbols survive
   code motion where line numbers do not — is acceptable and preferred over
   breaking on a line number.
2. Screenshots are optional diagnostics and visual assertions. They are not the
   navigation loop, a prerequisite for interpreting a screen, or the default
   success criterion.
3. One executor and action vocabulary serve SDL and FS-UAE once the game is ready.
   Target-specific startup, debugger transport and memory representation belong
   below the flow layer.
4. Use normal menu handlers for gameplay changes. Do not create careers, choose
   teams or start matches by patching gameplay state into the desired result.
5. One run owns the debugger session at a time. A timeout or lost connection must
   produce an explicit outcome, not a blind restart and replay of side effects.

## Existing foundation

The repository already provides these layers:

| Component | Current responsibility |
| --- | --- |
| AmigaScope `gdb_proxy.py` | Persistent debugger transport; native SDL or FS-UAE remote target |
| `scripts/harness_lib.py` | Debugger commands, reads, writes, breakpoints and captures |
| `scripts/swos_plugin.py` | SWOS menu knowledge, input injection, label scanning and career helpers |
| `scripts/swos_harness.py` | Low-level command-line interface |

`MenuInputInjectPoint` in `src/menu_system.c` is an existing stable breakpoint
after input polling and before menu dispatch. The plugin can inject one frame of
menu input there, then read the resulting menu state. `_currentMenu->onInit`
identifies a menu family, and entry callbacks and labels distinguish actions.

Existing helpers are useful building blocks, but are not yet a generic executor:

- `scan_text()` batches visible labels in one debugger command.
- `find_text()` uses a first prefix match, which is insufficient for safe entity
  resolution when several teams have similar names.
- `start_fresh_career()` already follows a supplied country/division/team path,
  but bundles a particular route into a procedure.
- `goto()` selects by callback, but callback identity alone cannot distinguish
  all dynamic rows; many countries and teams share one handler.
- Some selection helpers detect progress only through a changed `onInit`.
  Dynamic lists can change while keeping the same menu initializer.

See [GDB_HARNESS.md](GDB_HARNESS.md) for the current harness. Existing PowerShell
window drivers remain useful for testing actual OS input and rendering.

## Architecture

```text
User description / structured request
                |
        intent interpretation
                |
        typed goal and constraints
                |
      screen-graph walker <---- SWOS action registry
                |                  |
       plan / executor <---- entity resolver
                |
        observe -> act -> verify
                |
          SWOS state adapter
                |
        existing debugger harness
                |
         SDL          FS-UAE
```

Proposed external package: `scripts/testing_support/`, with a thin CLI named
`scripts/swos_test.py`. The package contains models, planning, execution, reporting
and a SWOS adapter. The adapter owns SWOS symbols and layouts; generic planning
and execution must not embed team names, country lists or menu ordinals. The
screen graph describes SWOS navigation; entity catalogs supply its parameters.

### Intent interpretation

The coding agent can translate conversational language into a validated structured
goal. This does not require an LLM call for every menu step. The executor remains
deterministic and can run independently from a saved goal or plan.

The coding agent is the conversational interpreter. The tooling accepts only the
versioned JSON goal/action schema through its Python API and plan/run/inspect CLI.
There is no standalone English grammar or additional language-model dependency in
the executor. Unsupported or ambiguous intent is resolved by the agent before
dependent actions execute.

For example, the agent interprets “open career of England and Arsenal” as:

```json
{
  "schema_version": 1,
  "goal": "career.active",
  "constraints": {
    "club": {"name": "Arsenal", "country": "England"},
    "source": "new"
  }
}
```

In a fresh main-menu context, a club-specific career request defaults to creating
a new career. Explicit “load” or “continue” requests select those operations.
If an existing career is active and the request could mean either continuing it
or replacing it, resolve that ambiguity before creating a different career.
Default choices appear in the plan and report.

Friendlies use a general participant model rather than positional key counts:

```json
{
  "schema_version": 1,
  "goal": "match.active",
  "constraints": {
    "competition": "friendly",
    "participants": [
      {"team": {"name": "Italy", "kind": "national"}, "control": "player"},
      {"team": {"name": "England", "kind": "national"}, "control": "cpu"}
    ]
  }
}
```

“Player” maps explicitly to SWOS player-coach control; “CPU” maps to computer.
Home/away order is a separate optional constraint. SWOS can randomly swap friendly
teams, so selection order alone does not prove the requested home/away assignment.

### Action registry and declarative screen graph

An action definition supplies:

- A typed name and parameters, such as `menu.activate(target)` or
  `team.set_control(team, control)`.
- Preconditions describing where it is applicable.
- An observable completion predicate and the expected destination or state change.
- The adapter operation that performs it.
- Whether retrying it is safe, and bounded retry/timeout limits.

Initial vocabulary includes observing state, activating an available action,
entering/exiting a hierarchy, resolving an entity, choosing an
entity, setting team control, dismissing a recognized memo, loading/saving a
career, starting a match and asserting state. Higher-level operations compose
these primitives through explicit graph edges and state guards. They are reusable domain
knowledge, not per-club or per-request scripts.

Model menus as a declarative graph: each node identifies a screen family and
relevant modal/hierarchy context; outgoing edges specify an activation, typed
parameters, guards and completion checks. Dynamic pickers are parameterized nodes,
not a node or route per team. `onInit` alone is not always sufficient node identity.

A plain graph walker expands supported goals into explicit compositions of these
edges from the current known screen. Ordinary path finding through declared menu
edges is sufficient; v1 has no general precondition/effect search engine. Plans
can defer runtime row binding until a picker is observed. An unsupported edge or
unknown screen yields a capability error rather than speculative exploration.
Each step records its purpose and completion check. General planning search is
deferred until a concrete flow demonstrates a need for it.

Arsenal and Partizan use the same career goal and actions. Country, division and
club are resolved parameters. Changing Italy to Brazil in a friendly must change
only request data. A new composition of known operations must not require a new
Python file, command handler or hard-coded route.

### Entity resolution

Resolve offline first, then verify online. Build an entity index from the same
team and competition data used by the game. Produce the expected continent,
country, division and team picker path before executing it. Verify every hop
against the live snapshot and bind it to the actual enabled row. Runtime wins
when catalog data and the target disagree; never press the closest-looking row.

Catalog inputs must be explicit. The teams catalog
([TEAM_IDS.md](TEAM_IDS.md)) is found the same way the game finds its data:
the **assets folder first** (`swos_assets_dir()` — `$SWOS_ASSETS`, default
`assets` in the per-user game folder, e.g.
`Documents/SWOS/assets/data_json/teams/`), then beside the binary. The repo's
`bin/data_json/teams/` is *expected* to be absent: team data derives from the
untracked original game data, and the setup builds it into the assets folder.
A portable drop's `data_json` carries only the tracked set (verified identical
to the repo's, no `teams/`).

The file envelope (verified 2026-10-23 against a setup-built assets folder):
one file per country, `{type: "team", country, country_name, count, teams: []}`;
each entry is `{uuid, checksum, team: {...}, players: [16]}`, where `team`
carries `team_name`, `country_number`, `team_ordinal`, `league` (division
membership — what lets the resolver derive an omitted division offline) and
`global_team_number`. Country/division/path metadata still needs mapping against
the competition records and menu translations.

Expected sources to inspect when implementing:

| Source | Purpose |
| --- | --- |
| `<assets>/data_json/teams/*.json` (setup-built; repo copy absent by design) | Team names, country/ordinal/league identity, UUIDs and checksums |
| `bin/data_json/competitions/countries.json` | Country names and identifiers |
| `bin/data_json/competitions/country_competitions.json` | Country-to-league relationships |
| Competition/division records and menu translations | Division membership and actual picker labels |

If the needed files are missing or insufficient, `plan` reports the specific
missing source/fields and accepts a supplied catalog via an explicit path. It
must not claim a complete path or require new scenario code to fill the gap.
Offline files predict navigation; they do not prove what the running game loaded.

When no setup-built assets folder exists on the machine (so no
`data_json/teams/` anywhere), two fallback catalog sources exist and should be
implemented in this order of preference:

1. **A one-time read-only bulk dump of the loaded team database over the
   debugger at session start**, cached per session. This does not violate the
   "planning must not change game state" rule — it is a read — and it is the
   strongest catalog of all: it contains exactly the names the pickers will
   display, because it is what the running game loaded.
2. **An offline parser of `bin/data/TEAM.*`**, the binary team files the game
   loads (present in a developer checkout, untracked per the data policy).
   Labels parsed from these match runtime by construction, and this source
   works with no live session at all.

Either fallback makes the "supplied catalog path" an override rather than a
prerequisite, so v1 is not blocked on a directory that may not exist.

Cache by catalog content digest, target executable/symbol identity and relevant
runtime data configuration. Division and continent picker labels come from menu
translations, not team files, so `bin/data_json/translations/` (and any other
label source actually used) is part of the catalog digest: an edited translation
changes picker labels without touching team data. `Build_Id()` and `Build_Platform()` in
`src/build_info.c` provide useful report metadata, but commit plus dirty flag is
not a data fingerprint: different dirty trees, external assets and save-state
changes can share it. Team UUID/checksum also does not fingerprint picker labels;
the documented checksum excludes names. Invalidate session-derived paths after
load/restart or any observed mismatch.

Resolution proceeds through normalized exact matches, explicit aliases and
context constraints (club versus national team, country, competition/division).
Punctuation, case and whitespace normalization are safe. Prefix/fuzzy matches
produce candidates; they must not silently choose the first row.

If a division is omitted, derive it from catalog membership rather than visiting
every division. The common case performs zero exploratory navigation. A missing
membership or conflicting match returns candidates or an incomplete resolution.
V1 stops on catalog/runtime mismatches with the expected hop and actual choices;
automatic live discovery/backtracking is deferred. Later bounded discovery can
use the same screen graph. Pagination remains a supported action only where a
known picker needs it to reach an already resolved entity.

“Partizan Belgrade” can resolve to an abbreviated in-game name through an explicit
alias, if present. Otherwise report the candidates and ask for clarification.
Do not invent an alias based on an unverified guess. Missing historical countries
or teams produce a useful `entity_not_found` result with the actual choices.

## State observation and performance

The SWOS adapter returns a structured snapshot, not text that the planner must
scrape repeatedly:

```text
snapshot:
  phase: menu | match | transition | unknown
  screen identity and relevant modal/hierarchy context
  selected entry
  visible actionable entries:
    ordinal, label, callback, enabled, control mask
  relevant domain state:
    selected teams and controls, career club, save identity, match state
  fingerprint and observation sequence
```

Collect the needed fields in one batched GDB script per ordinary snapshot, with
at most one additional request for required domain state or symbol resolution.
Frame and escape the output so labels cannot corrupt its records. Inspect domain-specific state
only when an action needs it. Cache static symbol/layout information; invalidate
dynamic snapshots after execution resumes. Do not perform several debugger
round trips per entry or dump full team/player records for ordinary navigation.

Treat labels as data, including proper string decoding and escaping. Read only
entry content forms whose pointer/layout semantics the adapter understands;
an arbitrary nonzero `fg` value is not proof of a safe string pointer. Respect
entry bounds and normalize scalar versus one-element-array globals in the adapter.

Use a fingerprint containing menu identity, visible actionable row identities,
selection and relevant domain fields. A changed list can show progress even when
the menu initializer is unchanged. A highlight change alone does not prove that
an action dispatched or that a career/match was created.

Initial performance targets, to be measured rather than assumed:

- Ordinary observation: target 1–2 debugger requests, independent of menu length.
  Measure this rather than assuming the target will always meet it.
- No image captures or fixed multi-second sleeps in normal menu navigation.
- Cache resolved hierarchy paths within a compatible session.
- Report elapsed time and debugger request counts per step, distinguishing
  transport latency from game execution and expensive initialization.

## Execution and verification

For each step:

1. Observe the stopped target and validate the action's preconditions.
2. Resolve its target to exactly one visible, enabled entry or domain entity.
3. Apply a bounded input pulse through the existing injection point, or use an
   established selection helper followed by normal handler dispatch.
4. Resume to an appropriate existing breakpoint; observe the new state.
5. Check the action-specific completion predicate and log evidence.
6. Advance, safely retry, bind a declared alternative graph edge, or fail explicitly.

Directly setting the highlighted entry is allowed for semantic flows through the
existing helper. It bypasses directional navigation, so a test intended to verify
navigation links must request a separate navigation policy using real directional
steps. Reports identify the policy used.

Retries must be effect-aware. Team control cycles unselected → CPU → player-coach
→ coach → unselected, so blindly repeating fire is incorrect. Read the actual
control, pulse once, then verify the new control. Likewise, a dispatched save,
load or career creation must not be replayed simply because its stop notification
arrived late. Unknown outcomes stop the run for reconciliation.

Selection and navigation remain menu operations. Match start needs a different
completion boundary: the menu injection point stops firing once gameplay begins.
Use `GameLoop_Frame` (`include/game_loop.h`, `src/game_loop.c`) as the initial
shared boundary candidate. Install that breakpoint before the final menu
activation and verify the expected teams/controls in match state. An entry stop
shows the loop was reached, not that a frame completed; if completed-frame
evidence is required, resume to a subsequent validated boundary and check progress.
Reaching a lineup screen or
entering a match-start function alone does not prove an active match. The adapter
must validate its chosen match-loop boundary on both targets before declaring
this capability supported. A first-frame stop proves execution reached gameplay;
it does not prove rendered pixels are correct or the entire match is healthy.

After completion, leave the target stopped at the verified boundary by default.
The CLI must prominently print `TARGET STOPPED` and how to resume; the report
includes `target_stopped`, stop location and resume instructions, because a halted
FS-UAE window otherwise looks hung. An explicit resume operation releases it for
interactive use. On timeout, first
reconcile debugger health, stop state and PC without issuing another action.
Recovering transport is distinct from restarting the game; never silently reboot
a session containing user progress.

### Career example

For the Arsenal request, resolve the new-career action, open its picker, resolve
England and Arsenal's division from the offline catalog, verify each live hop,
and select the exact club. Dismiss only recognized welcome memos. Completion requires
the career hub and the actual career club identity to match Arsenal in England.
Default manager details are recorded, with no implicit save or season simulation.

The Yugoslavia/Partizan request uses the same operations and predicates. Neither
example gets a dedicated script.

### Friendly example

Open friendly setup and national-team selection using available actions. Resolve
Italy and England; set their controls from observed state, not assumed fire
counts. Verify exactly those two teams are selected with the requested controls.
Activate play, handle the recognized lineup screen with its raw fire requirement,
and stop at the verified match boundary. Assert teams and controls again there.
If home/away was not requested, accept either orientation and report it.

## Interfaces and reports

Proposed interfaces (not existing commands yet):

```text
python scripts/swos_test.py plan --request request.json
python scripts/swos_test.py run --request request.json --report run.json
python scripts/swos_test.py inspect
```

The Python API accepts the same typed goals and composable action plans. An agent
can turn a conversation into JSON and invoke it; users do not need to author JSON
in ordinary conversation. The low-level harness CLI remains available for debug.

Planning may observe state but must not activate menus or change game state.
If full entity discovery requires visiting dynamic pickers, the dry plan marks
that resolution as deferred. It must not secretly execute half a flow to produce
a supposedly read-only preview.

Reports contain the original request, interpreted goal, defaults, target/build
identity, plan revisions, resolved entities, per-step before/after fingerprints,
assertion evidence, timing, debugger request counts and final disposition.
Statuses distinguish success, clarification needed, unsupported capability,
missing entity, failed precondition/assertion, timeout and transport failure.
Failures include the last known screen and available relevant actions. Screenshots
are opt-in attachments for unexpected states or explicit visual checks.

## Validation and delivery

1. Build the snapshot adapter and structured inspect command. Validate batched
   reads, bounds, array globals and actionable entry filtering on SDL and FS-UAE.
2. Add the typed JSON schema, action registry, screen graph, bounded executor and
   report format. Include offline catalog resolution and online hop verification.
   Test with fake adapters/recorded snapshots: ambiguous names, missing catalogs,
   stale paths, disabled actions, same initializer with changed rows, and unsafe
   retries. Include timing and debugger-request counts from the beginning.
3. Port `start_fresh_career()` to graph actions as the first end-to-end acceptance
   flow; retain its existing API as a compatibility wrapper once equivalence is
   validated. Run Arsenal/England and Partizan/Yugoslavia where available on both
   targets, plus an unlisted club supplied only through request data. This is the
   proof that a different team needs no new script or scenario code.
4. Add friendlies and validate `GameLoop_Frame` and match-state checks on both
   targets. Test control cycles and ensure retries cannot double-select or
   deselect. Run Italy-player/England-CPU and an unlisted combination from JSON.
5. Exercise the coding-agent-to-JSON workflow, including ambiguity handling and
   supported compositions beyond the two acceptance examples. No English parser
   is added to the tooling. Keep unsupported operations explicit.

Acceptance requires no game-source changes, no scenario-specific script for an
additional combination of supported actions/entities, no screenshot dependency
for these flows, verified domain outcomes, exclusive debugger ownership and
useful bounded failures. Performance measurements must accompany the live runs.

## Initial limits

The first delivery covers menu-driven setup and inspection, career selection,
load/save operations when registered, and verified match entry. Continuous
football-playing automation is a separate capability. Visual correctness still
requires visual assertions when requested. Unsupported text entry, localization,
picker types or debugger features must be reported as capability gaps; screenshots
must not silently become the default workaround.

The existing transport configuration and symbol layouts vary across checkouts.
Live validation requires a working proxy, matching debug symbols, and write-capable
FS-UAE debugger support. The graph walker must remain independent of those
deployment details.

## Implementation status (2026-10-23)

Everything lives in `scripts/testing_support/` with the CLI
`scripts/swos_test.py`; no game source changed (constraint 1 held — the
match boundary is the existing `GameLoop_Frame`, no new anchor was needed).

| Module | Role |
| --- | --- |
| `snapshot.py` | PURE: the one-round-trip batched observe script, parser, fingerprint. Entry addresses are computed arithmetically (no inferior call), labels read only from understood content forms, everything nonce-framed. |
| `adapter.py` | `SwosStateAdapter` over `swos_plugin.SwosHarness`: observe, act (selection is a MEMORY WRITE — the FS-UAE stub executes writes but not gdb inferior calls), domain reads (`_careerTeam`, `_selectedTeams`, match teams), `enter_match`, request counting/timing. |
| `goals.py` | Versioned goal schema (`career.active`, `match.active`), statuses, recorded defaults, structured `GoalError` questions. |
| `catalog.py` | Offline entity catalog + resolution (exact normalized match, explicit `aliases.json`, candidates on ambiguity). Continent membership from `qualifying.json`, division labels from `leagues.json` names + the `_divisionStringTable` fallback, both resolved through `src/strings_data.c`. |
| `graph.py` / `swos_screens.py` | Declarative screen graph (BFS walker) + the SWOS screens/edges as data. |
| `executor.py` | Bounded observe→act→verify with effect-aware retries: unchanged state retries a dropped pulse; changed-but-incomplete stops as `unknown_outcome` (a press is never blindly replayed). |
| `planner.py` / `friendly.py` | Goal → explicit plan. Careers verify `_careerTeam` identity at the hub; friendlies set controls effect-aware (read `_selectedTeams`, pulse once, re-read) and verify teams/ownership at the `GameLoop_Frame` stop, orientation-agnostic. |
| `runner.py` | Request → plan → run → full JSON report. |
| `fastproxy.py` | `FastGdb`: the daemon's socket protocol spoken in-process — replaces the per-command client subprocess whose Cygwin spawn cost dominated every run (see "Headless operation and performance"). |
| `amiga_session.py` | THIS machine's FS-UAE debug bring-up per docs/AMIGA_DEBUGGING.md: native FS-UAE + win32 m68k gdb, attach-then-`qOffsets`-relocated symbols, write-capability check, intro skip, stop at the anchor. |
| `test_snapshot.py` / `test_flows.py` | 43 offline tests, fake adapters + synthetic catalog: ambiguous names, missing catalogs, stale paths, disabled actions, same-init changed rows, unsafe retries, memo chains, control cycling, wrong-identity assertions. |

### Usage

```sh
# offline (read-only): validate + resolve + plan
python3 scripts/swos_test.py plan --request req.json

# live (SDL): start the daemon once, then run requests
python3 scripts/amiga/amigascope/gdb_proxy.py daemon --mode sdl --bin bin/SWOS.exe &
AMIGASCOPE_DIR=$PWD/scripts/amiga/amigascope \
  python3 scripts/swos_test.py run --request req.json --report run.json
python3 scripts/swos_test.py inspect            # structured snapshot, any time

# live (FS-UAE) — bring the session up first, and tell the adapter the
# NATIVE gdb's spelling of the observe-script path:
python3 scripts/testing_support/amiga_session.py up
export SWOS_TEST_SCRIPT=/cygdrive/c/Users/A7126/AppData/Local/Temp/swos_observe.gdb
export SWOS_TEST_SCRIPT_GDB=C:/Users/A7126/AppData/Local/Temp/swos_observe.gdb
python3 scripts/swos_test.py run --request req.json
```

All of it runs from the Cygwin shell (`docs/LOCAL_WORKFLOW.md`). The target
is left STOPPED at the final verified boundary; `inspect --no-prime --resume`
releases it.

### Validated live (both targets, from pure request JSON)

Times before the `*` are the original subprocess transport; after it, the
direct-socket transport (`fastproxy.py`, below) — same scenarios, same
requests.

| Scenario | SDL | FS-UAE (A1200) |
| --- | --- | --- |
| Arsenal/England new career, hub + `_careerTeam` verified | 16 s | 19 s |
| Partizan Belgrade (alias → `PARTIZAN B'GRADE`), division from fallback names | 11 s | — |
| Juventus/Italy (unlisted club, request data only) | 13 s | — |
| Italy(player) vs England(cpu) national friendly to `GameLoop_Frame`, ownership verified | 23 s | 36 s |
| Liverpool(player) vs Chelsea(cpu) club friendly (unlisted combination) | 27 s | — |
| Red Star Belgrade season (`season.active`, gameType 3, 10-team league)\* | — | 5.7 s |
| Real Madrid / PRIMERA DIVISION preset (`competition.active`, gameType 2, 22 teams)\* | 5.9 s | — |
| Ambiguous "Rangers" → `clarification_needed` with both candidates; unknown club → `entity_not_found` | offline | offline |

One run of each scenario per target is a smoke level, not a soak — rerun
them after touching the executor, the adapter, or the menus they walk.

### Headless operation and performance

**2026-10-29 FS-UAE update:** use [AMIGA_UI_TESTING.md](AMIGA_UI_TESTING.md) for
current commands, the private proxy, session ownership/reuse and measured input
batching results. The older examples and timings below are historical; in
particular, `amiga_session.py up` now reuses a healthy paused session of the same
build instead of always tearing it down.

**Everything above ran headless.** The framework never looks at pixels: it
reads game memory over gdb, so no window, renderer or audio device is
needed. Screenshots remain an opt-in diagnostic (`SwosHarness.screencap`),
never the loop.

- **SDL** headless = dummy SDL drivers on the daemon (the game still runs
  its full render path into the 8bpp frontbuffer — only the window/audio
  sinks are stubbed):

  ```sh
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
      python3 scripts/amiga/amigascope/gdb_proxy.py daemon --mode sdl --bin bin/SWOS.exe
  ```

  `boot()` additionally sets `SWOS_HARNESS_TURBO=1` on the inferior, so the
  game is CPU-bound instead of paced to the 50 Hz wall clock — a cold boot
  to the main menu is ~3 s and the loader intro costs nothing. Start the
  daemon WITHOUT the dummy drivers to watch a run in a real window; nothing
  else changes.
- **FS-UAE cannot run headless** — this build has no dummy video backend;
  the emulator always opens its window. It is also not TURBO-paced: the
  emulated A1200 runs at machine speed. The framework drives it
  identically; the window shows the run as it happens.

**Operating rule: headless is the default (it is faster and steals no
focus), but a run someone wants to WATCH goes windowed** — on SDL start the
daemon without the dummy drivers, and `resume` the target at the end so the
screen is live rather than frozen at the stopped anchor. A stopped target
always looks like a hang to a human watching the window.

**Where the time goes — SDL** (measured, Real Madrid preset run):

| Phase | Cost |
| --- | --- |
| daemon start (once) | ~3 s |
| cold boot to main menu (once per reboot, TURBO) | ~3 s |
| one observation (one sourced gdb script) | ~0.15–0.2 s |
| one plan step (act + settle frame + observe + verify) | ~0.8–1.0 s |
| whole scenario (7 steps, 79 debugger requests) | ~5.9 s |

**Where the time goes — FS-UAE** (measured, timestamped bring-up +
Red Star season):

| Phase | Cost |
| --- | --- |
| verified teardown (kill + confirm old fs-uae/gdb gone) | 0.4 s |
| FS-UAE launch → AmigaDOS boots to the `SYS:SWOS2` trigger | 4.6 s |
| gdb attach + `qOffsets` + 8.6 MB symbol load + write check | 1.1 s |
| trigger → main-menu anchor (SWOS loading its data at emulated speed; intro skipped) | **25 s** |
| **bring-up total (once per emulator session)** | **~31 s** |
| one plan step | ~1.0–1.3 s |
| whole scenario (7 steps, 67 requests) | ~5.7 s |

The 25 s is the game itself booting on an emulated A1200 (RNC decompression,
team/graphics loads) — inherent emulation work, paid ONCE: the session stays
resident (`amiga_session.py up` reuses nothing — the FS-UAE gdb port is
one-shot — but the attached session serves any number of requests until torn
down). Every request after bring-up costs its ~6 s on either target.

**The transport is what made requests fast.** The original client spawned a
`python3 gdb_proxy.py cmd ...` subprocess per gdb command — ~0.10–0.15 s
each under Cygwin's fork emulation, which DOMINATED every run (a friendly =
155 requests ≈ 20 s of pure spawn overhead). `testing_support/fastproxy.py`
(`FastGdb`) speaks the daemon's socket protocol directly from the adapter's
process: per-request overhead drops to ~1–2 ms, leaving only real gdb/game
work (~70–85 ms per request average, settle frames included). Scenario cost
fell ~4×: friendly-shaped runs from 23–36 s to ~6 s. `SWOS_TEST_FAST=0`
restores the subprocess client (what `amiga_gdb.sh` and the historical
helpers still use).

Environment knobs, all optional on SDL: `AMIGASCOPE_DIR` (the AmigaScope
checkout; required), `SWOS_TEST_SCRIPT` / `SWOS_TEST_SCRIPT_GDB` (the
observe-script path as this tool writes it vs as the gdb PROCESS reads it —
they differ only for the native-Windows gdb of the FS-UAE session),
`SWOS_TEST_FAST` (transport selection, above).

### Known gaps (deliberate, reported as structured statuses)

- `career.active` with `source: load/continue`; save/load goals.
- Mixed club/national friendlies and participants on different picker paths
  (needs modeled picker-exit edges).
- Runtime division discovery when the catalog has no division label.
- Pagination of pickers that expose only part of their rows.
- The Amiga target cannot be cold-rebooted by the framework (`reboot()` is
  SDL-only); a fresh scenario needs `amiga_session.py up`.
- `Build_Id()` in the report is null on the Amiga (the stub does not execute
  gdb inferior calls — only reads/writes).

### Notes for maintainers

- `scripts/amiga/amigascope/gdb_proxy.py` (submodule) carries two small local
  changes worth upstreaming: `AMIGASCOPE_GDB` (gdb binary override) and
  `AMIGASCOPE_ELF_NOARG=1` (attach first, `add-symbol-file` at the relocated
  bases afterwards — plain argv symbols are WRONG under AmigaDOS hunk
  relocation, docs/AMIGA_DEBUGGING.md).
- `scan_text()`'s historical `<= numEntries` off-by-one (one entry past the
  end) is fixed; `numEntries` is a count.
