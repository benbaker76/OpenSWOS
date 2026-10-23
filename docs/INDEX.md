# docs/INDEX.md — a map of the docs, and of every path the project uses

For a fresh session: read `.claude/CLAUDE.md` first (the project's rules and the current state
of every subsystem), then this page to find the doc for the task in front of you. Each row
says what a doc is, whether it is still live, and when to open it.

**Status words used below**

| status | means |
|---|---|
| **current** | describes the code as it is; trust it, and fix it when you change what it describes |
| **historical** | a finished work log, audit or plan — kept as the record of *why*; a dated status line at its top says where the work landed |
| **plan** | not implemented (or only partly); the status line at its top says how far it got |
| **parked** | deliberately paused work (AGA): current as far as it goes, do not resume without the owner |

Every doc with a status line at its top got it in the 2026-09-30 review, which checked the
docs' file paths, symbols, flags, settings and commands against the code. Line numbers in
docs drift: search by function name.

## Start here

| task | read |
|---|---|
| anything at all | `.claude/CLAUDE.md` |
| the player-facing guides (split out of README.md 2026-10-20) | [../INSTALL.md](../INSTALL.md) (installing, for beginners, with the setup's screenshots), [PLAYING.md](PLAYING.md) (keys, pads, arcade), [SETTINGS.md](SETTINGS.md) (`SWOS.ini`, command line, display), [BUILDING.md](BUILDING.md) (from source), [DEVELOPERS.md](DEVELOPERS.md) (layout, conventions) -- current; README.md is now a short front page linking them |
| a tester bug report, a drop, publishing | [TRIAGE.md](TRIAGE.md) |
| changing the match simulation | [GAMEPLAY.md](GAMEPLAY.md), then [TESTS.md](TESTS.md) (the replay rig proves it) |
| online play | [NETPLAY.md](NETPLAY.md) (design), [NETPLAY_SETUP.md](NETPLAY_SETUP.md) (server runbook) |
| updating the server (the admin panel) | [NETPLAY_ADMIN.md](NETPLAY_ADMIN.md) |
| replays / the playback page | [REPLAY_FORMAT_V2.md](REPLAY_FORMAT_V2.md), [PLAYBACK.md](PLAYBACK.md) |
| saves | [SAVE.md](SAVE.md) (where), [SAVE_DATA.md](SAVE_DATA.md) (what, and the read-support policy) |
| saved positions / challenges | [CHALLENGE.md](CHALLENGE.md) (usage, samples, format and limitations), [AMIGA_DEBUGGING.md](AMIGA_DEBUGGING.md) (fixes and debugger workflow) |
| editing game data (teams, competitions, menus, tactics) | [DATA_PIPELINE.md](DATA_PIPELINE.md) |
| the Amiga target under FS-UAE / gdb | [GDB_DEBUGGING.md](GDB_DEBUGGING.md), [GDB_HARNESS.md](GDB_HARNESS.md) |

## Gameplay and the simulation

| doc | what it is | status | read when |
|---|---|---|---|
| [GAMEPLAY.md](GAMEPLAY.md) | The implementation-level spec of Amiga SWOS in-match play (world model, input, player state machine, ball physics, keeper, AI, restarts, clock, constant tables), DOS differences alongside, porting traps. v2.0 folds in the ~15 port bugs the replay rig fixed. | current | changing or debugging `update_players.c`, `ai.c`, `ball_physics.c`, `bench_subs.c`, `goalkeeper`, `game_timers.c`, or reading a replay-rig divergence |
| [DOS_GAMEPLAY.md](DOS_GAMEPLAY.md) | The DOS release's 70 fps gameplay: per-FPS constants, match clock, the 41/64 movement scale — and how `[gameplay] style = dos` applies it. | current | DOS-vs-Amiga physics or timer constants, frame pacing, the style switch (`src/dos_gameplay.c ApplyGameStyle`) |
| [AMIGA_60.md](AMIGA_60.md) | Design for an authentic 60 Hz Amiga mode: fixed 50 Hz logic with interpolated rendering. | plan (its §9a sub-pixel scroll shipped) | high-refresh play, or splitting logic from rendering |
| [TACTICS_POSITION_VALUE.md](TACTICS_POSITION_VALUE.md) | The tactics editor's tick/cross marks, the `GetPlayerPrice` position-value formula and weights, Amiga-vs-DOS formations, the `SWOS_DOS_TACTICS` / `SWOS_ORIGINAL_TACTICS` builds. | current | tick/cross or player-value bugs, formation data |
| [tactics_amiga_vs_dos/](tactics_amiga_vs_dos/README.md) | Data: both releases' 12 formations as JSON, and CSVs of the 145 differing bytes (21 of them live). | historical | comparing Amiga and DOS formation coverage |

## Testing

| doc | what it is | status | read when |
|---|---|---|---|
| [TESTS.md](TESTS.md) | The recorded-game replay rig: DOS `.RGD` recordings replayed frame by frame against the port — format, compare levels, and the story of every defect it found (47), open leads, the Amiga-dumper plan. | current | touching simulation code, running or extending the rig (`scripts/replay_test.sh`, `replay_matrix.sh`, `--replay-test=`) |
| [TESTS_RESULTS.md](TESTS_RESULTS.md) | Generated matrix results (`scripts/replay_matrix.sh > docs/TESTS_RESULTS.md`) plus the index of the rig's defects. | current (data) | checking whether a change moved the simulation |
| [UI_DRIVER.md](UI_DRIVER.md) | Driving the Windows SDL build from a script (`scripts/swos_drive.ps1`): key injection, the game's own F12 screenshots, the `[menu-nav]` log, and getting a gdb backtrace for a crash. | current | verifying a menu/UI change, or debugging one without a human at the keyboard |
| [ROLLBACK.md](ROLLBACK.md) | The match-state registry (`src/sim_state.c`: SIM/VIEW tiers, portable encoding, `state_wire` packets) and the rollback rig that proves it. | current | changing the registry or anything the match writes — run `scripts/rollback_test.sh --all` |
| [VERIFY_STAGE2.md](VERIFY_STAGE2.md) | Protocol for checking league standings survive an SDL save loaded on the big-endian Amiga build (scenario 1 signed off; 2 and 3 have no record of being run). | current | re-verifying standings across SDL and Amiga saves |
| [test_rig.diff](test_rig.diff) | Generated review patch (`scripts/gen_rig_diff.sh`): every game-source change since the rig arrived, one diff per commit, classified by whether it can change Amiga-mode play. | stale — last regenerated 2026-09-06, ~160 source commits behind | reviewing whether rig-driven fixes changed shipped Amiga play |

## Netplay, replays and the server

| doc | what it is | status | read when |
|---|---|---|---|
| [NETPLAY.md](NETPLAY.md) | The design of rollback netplay: GGPO-X, the frame step, netem, the handshake and lobby, matchmaking, the relay and recorder, the browser gate, measured results. | current | the netplay step, `p2p_link`, the lobby, matchmaker/relay/recorder, WASM netplay |
| [NETPLAY_SETUP.md](NETPLAY_SETUP.md) | Operator's runbook: build/run the matchmaker, the systemd unit, ports, certificates and `wss://`, pointing games at a server, troubleshooting, reading logs. | current | deploying or updating the server, a match that will not connect |
| [NETPLAY_ADMIN.md](NETPLAY_ADMIN.md) | Updating the server from the admin panel (`/cc/`): every button, what to press after which kind of change, what still needs ssh, troubleshooting. | current | deploying a server, player or admin-page change |
| [NETPLAY_BALANCE.md](NETPLAY_BALANCE.md) | Why one player guesses on every frame for minutes while the other never does (GGPO's time sync assumes both directions take equally long), a prototype that shares the guessing, its test results, and what to try next. | parked (rolled back; code sketch inside) | picking up netplay pacing, or talking time sync with the GGPO-X side |
| [BALL_CONTEST.md](BALL_CONTEST.md) | Why "ghosting", dead balls and "magnet" tackles are the original game's own rules (the contested-ball roll and its team lock-out, the fast-ball trap, the slide tackle, the over-turn), whose ratings the roll really uses, the proof from both disassemblies and the original PC recordings, and the measurements over 37 online matches. | current | the GGPO-or-SWOS question; a tester reports a walk-through, a dead ball or a ball pulled to an opponent |
| [CONTEST_LOG.md](CONTEST_LOG.md) | `--srp-resim --contest-log` and `scripts/contest_report.py`: every ball contest a recorded match rolled for, every contact it did not and why, slide tackles, over-turns and traps; the first result over 37 matches. | current | a tester reports ghosting, a dead ball or a magnet tackle with a recording and frames |
| [NETPLAY_GHOSTING.md](NETPLAY_GHOSTING.md) | "Players run through each other online": the two candidate causes (not rollback — shown), what every match records, reproducing it without testers. | current | a ghosting or walk-through report |
| [NETPLAY_REPORT.md](NETPLAY_REPORT.md) | Match reports: both players' `SWOS.log` and ratings swapped over the link, zipped, uploaded after each online match. | current | match reports, the rating screen, a "it lagged" report |
| [REPLAY_FORMAT_V2.md](REPLAY_FORMAT_V2.md) | The `.SRP` whole-match replay spec: header, META/FRMS/AUDI/SEEK/CHAP, the resumable SIMV/SETP/STAT/INPT/HASH, PRED/SEEN (what each player saw), `--srp-resim`. | current | `src/replay_v2.c`, `srp_writer.c`, `srp_resim.c`, reading or writing `.SRP` |
| [PLAYBACK.md](PLAYBACK.md) | The recordings player at `/playback/`: using it, who-played-whom cards, the recorded view, the PRED/SEEN overlay, building and deploying it (nginx, the admin button). | current | the player, match cards, `/playback/` deployment |

## Saves, data structures and the untangling (roadmap steps 3–4)

| doc | what it is | status | read when |
|---|---|---|---|
| [SAVE.md](SAVE.md) | Where saves live on each platform, which file types, how the path is chosen. | current | "can't find my saves", changing the save directory |
| [SAVE_DATA.md](SAVE_DATA.md) | Every save file's layout, the pointer-field audit, and the **read-support policy** (which binary formats stay). Layout sections describe the retired raw dumps. | current (policy) / historical (layouts) | before touching `.TAC`/`.HIL`/`.EDT` reads or retiring any save path |
| [SAVE_FORMAT_JSON.md](SAVE_FORMAT_JSON.md) | Roadmap and checklist of the move to JSON saves (schema v3). | historical | why a JSON save field is encoded the way it is |
| [SAVE_FORMAT_TRIM_CANDIDATES.md](SAVE_FORMAT_TRIM_CANDIDATES.md) | Saved fields that are recomputed on load, and the rule for removing them (its gate is now met; nothing trimmed yet). | plan | before removing a field from the JSON save |
| [OVERLAPPED_DATA.md](OVERLAPPED_DATA.md) | Catalogue of Amiga memory that holds different data at different times (unions, overruns, code-as-data) and how the port handles each. | current (some items resolved) | a value "wrong" at an offset another subsystem may own |
| [64BIT_TRANSITION.md](64BIT_TRANSITION.md) | The phased plan to make the source 64-bit clean. | historical (largely done) | a pointer-width or struct-size bug between 32- and 64-bit |
| [PHASE1_AUDIT.md](PHASE1_AUDIT.md) | Audit of raw `base + offset` accesses converted to struct fields (64-bit Phase 1). | historical | rarely |
| [STRUCT_CONSOLIDATE.md](STRUCT_CONSOLIDATE.md) | Merging the per-size competition struct variants into one struct per family. | historical (the §9 AnimTable merge not done) | competition templates, AnimTable variants |
| [STRUCT_FIELDS_UNK.md](STRUCT_FIELDS_UNK.md) | Naming the unknown competition-struct fields; the TeamId migration (§11 TODO). | current | a competition header byte, or starting the TeamId migration |
| [TODO.md](TODO.md) | Four structures still accessed by raw byte offset, each with a proposed struct and its sites (annotated with what has landed). | plan (partly done) | replacing raw-offset accesses with typed fields |

### Competitions and career data

| doc | what it is | status | read when |
|---|---|---|---|
| [COMPETITION_DYNAMIC_REFACTOR.md](COMPETITION_DYNAMIC_REFACTOR.md) | Making competitions id-based and data-addable; how Copa Libertadores was built on it. | historical | adding a data-only competition, `Comp_ById`, `_careerContests` |
| [COPA_LIBERTADORES.md](COPA_LIBERTADORES.md) | The original design of the Copa Libertadores (format, 1996 field, draw, qualification) — built differently in places. | historical | changing Copa rules or CONMEBOL qualification |
| [FEATURES.md](FEATURES.md) | Sketches of three enhancements: Copa (done), career length (done, `num_seasons`), easy data add/remove (partly). | historical / plan | extending competitions, career length, data-driven content |
| [COMPETITION_SLOT_ALIAS_UNTANGLE.md](COMPETITION_SLOT_ALIAS_UNTANGLE.md) | Retiring the 46 comp-slot `.equ` aliases and the careerFileBuffer overlay. | historical | the 5 career competition slot buffers or their save descriptors |
| [SLOT_ALIAS_S6_DISCOVERY.md](SLOT_ALIAS_S6_DISCOVERY.md) | Classification of every slot-0 alias reference before the S6 migration. | historical | archaeology on slot 0's legacy names |
| [PRIMARY_COMP_BLOCK_PROMOTION.md](PRIMARY_COMP_BLOCK_PROMOTION.md) | Retiring the `primary_comp_block` save blob; the `namePtr` symbolic-save constraints. | partly open — symbolic `namePtr` still to do | making `namePtr` save symbolically; a friendly/tour misclassified after a load |
| [CAREERTEAM_STEP4_REWORK.md](CAREERTEAM_STEP4_REWORK.md) | Removing the adjacencies that pinned careerTeam/currentTeam in the overlay. | historical | `teamSelOffset`, `diy_resolve_team`, the careerTeam buffers |
| [DIYCOMPETITIONEX_AUDIT.md](DIYCOMPETITIONEX_AUDIT.md) | Every raw offset in `diy_competition.c` mapped to the SWOS2.s BSS layout. | historical | what a `DIYCompetitionEx` offset means in SWOS2.s |
| [KEYCACHE_CLUSTER_REWORK.md](KEYCACHE_CLUSTER_REWORK.md) | Lifting the team-selection key cache, foreign market and phase bias out of the overlay. | current (§9 per-record writer still deferred) | the key cache |
| [SELECTED_TEAMS_UNTANGLE.md](SELECTED_TEAMS_UNTANGLE.md) | Moving the selTeams cluster into a typed carrier (`_selectedTeams[100]`). | historical | the selected-teams array or its views |
| [SELTEAMS_CONTENT_STABILITY.md](SELTEAMS_CONTENT_STABILITY.md) | A phantom PLAY MATCH in cups — the cup slot copy cut short on 64-bit. | historical | cup slots resolving to the player's team |
| [TEAMOFFSETTABLE_OVERLAY_UNTANGLE.md](TEAMOFFSETTABLE_OVERLAY_UNTANGLE.md) | League standings overlapping the cup scratch; removing the teamOffsetTable rebuild band-aids. | historical | `leagueStandings[64]`, teamOffsetTable rebuilds |
| [TOURNAMENT_BUFFER_UNTANGLE.md](TOURNAMENT_BUFFER_UNTANGLE.md) | Typing the tournament buffer as `DIYTournamentState` (the 64-bit World Cup panic 0x8034). | historical | the type-2 tournament buffer |

## Game data, menus, text

| doc | what it is | status | read when |
|---|---|---|---|
| [DATA_PIPELINE.md](DATA_PIPELINE.md) | **The** doc for `bin/data_json`: layout, run-time JSON vs baked C, the `emit_*`/`export_*` scripts, workflows, verification, `make regen-data`. | current | editing anything under `bin/data_json/` |
| [DATA_JSON.md](DATA_JSON.md) | Byte layouts of `TEAM.NNN`, `*.TMD`, `POOLPLYR.DAT`, the Python codecs, the teams JSON schema. | current | decoding or editing team/pool data |
| [DATA_INTERNAL_JSON.md](DATA_INTERNAL_JSON.md) | Design record for moving the competition graph to JSON (schema, cross-references, `*Copy` tables). | historical | changing the competition JSON schema |
| [DATA_CUSTOMIZE.md](DATA_CUSTOMIZE.md) | Making edited data safe: caps, counts, dangling references, a validator, a data digest for netplay. | plan (backlog open) | data validation, raising a cap |
| [DATA_BSON.md](DATA_BSON.md) | Binary formats and codecs for the Amiga; decided — author JSON, bake C, no parsing on m68k. | historical | someone proposes parsing data at run time on the Amiga |
| [MENUS.md](MENUS.md) | The packed menu byte-stream, the JSON menu schema, the symbol registry, the migration (§0–9, historical), the SWOS 2020 menu import (§10), adding an entry to a classic menu (§11) and adding a whole new one (§12, `_editMenu`). | current | menus, `UnpackMenu`, the SWOS 2020 import, `src/online_menus.c`, `--menu-selftest`, adding a menu or an entry |
| [TRANSLATIONS.md](TRANSLATIONS.md) | UI string localisation: what was built (`tr()`, en/de/fr/it/es/nl, `[general] language`) over the original plan. | current | adding a language or translatable strings |
| [FONTS.md](FONTS.md) | Adding a charset glyph (`swos_font.py`, SwosGfx, `gen_font_ext.py`) and the currency symbol. | current | a new glyph or currency |

## Graphics, display, platforms

| doc | what it is | status | read when |
|---|---|---|---|
| [WIDESCREEN.md](WIDESCREEN.md) | How the render width adapts to the display; menu/HUD centring (`WIDE_MARGIN`, `_menu_draw_x_offset`); `widescreen = stretch`; simulation vs render width. | current | render width, centring, the camera clamp, `[video] widescreen/resolution` |
| [DOS_GRAPHICS.md](DOS_GRAPHICS.md) | The DOS asset formats (pitch BLK/DAT, planar sprites, palette) and how they load into the chunky pipeline. | current | `[graphics] style = dos|2020` |
| [JAGUAR.md](JAGUAR.md) | The Atari Jaguar sprite tier: colour mapping, hair/shadow shading, palette donation, wiring, missing sprites. | current | `--jag-graphics`, `[graphics] sprites`, kit/shadow shading |
| [PALETTE_V2.md](PALETTE_V2.md) | Two palette banks sharing one kit block; truecolour pitches (parked idea). | plan (step 1 of 5 done) | palette entry allocation, kit shading |
| [UNIFORM.md](UNIFORM.md) | The UNIFORM screen (SWOS 2020's uniform selector) on the PLAY MATCH screen: a kit per side for one match, kept beside the team; why SWOS's own kit rule still runs; online; `[graphics] kit_art` (Amiga or Jaguar kit drawings). | current | the UNIFORM button, `src/uniform_menu.c`, `src/kit_art.c`, kit choice at kick-off |
| [CONTROLS.md](CONTROLS.md) | The player's own keys (`[input] key_*`, OPTIONS -> CONTROLS), the press latch (`latch_presses`) and the fire-press evidence a live match's recording keeps. | current | `src/input_sdl.c`, `src/controls_menu.c`, a report of a press the game did not act on |
| [TEAM_IDS.md](TEAM_IDS.md) | Canonical ids for the team database: a `uuid` per team and player, a `checksum` per team (positions, skills, values); how the 96/97 ids are derived; what the game checks; the matchmaking server's team check before an online match; downloadable teams (plan). | current / plan (§6) | team data identity, `src/team_db.c`, `scripts/assign_team_uuids.py`, DLC teams |
| [DOS_SOUND.md](DOS_SOUND.md) | The PC game's sounds as a second sound style (`[audio] style = pc`): its sampled effects, crowd, chants and commentary from `bin/sound_dos/`. Events through the sound gate, so rollbacks and recordings handle them; where each event is sent from; what is and is not the PC game's. | current | PC sound or commentary, `src/dos_sound.c`, a `DOS_SOUND(...)` hook, `scripts/gen_dos_sound_tables.py` |
| [SHOOTING_PRACTICE.md](SHOOTING_PRACTICE.md) | The main menu's SHOOTING PRACTICE entry: the friendly flow, with the computer playing only its goalkeeper, built on the `sentAway` flag a red card sets. Both targets. Includes the bug log from when it was a PLAY MATCH toggle — the menu symbol registry, the latched-vs-live mode, the LLP64 `CUSTOM_BASE` crash, the CRLF Amiga boot, and the toolchain/FS-UAE hurdles. | current | SHOOTING PRACTICE, `src/shooting_practice.c`, adding a menu entry, or hitting any of those traps again |
| [CUSTOM_TEAMS.md](CUSTOM_TEAMS.md) | MY CUSTOM TEAM: three slots, `CUSTOMn.TEAM` (JSON), the editor (EDIT TEAM with IMPORT and LINEUP), and the rule that only names, faces, kits and line-up may differ from the database team — in the file, the editor and on receipt. | current | custom teams, `src/custom_team.c`, a team the lobby refuses |
| [PITCH_MAPS.md](PITCH_MAPS.md) | `[graphics] pitch_maps`: a DOS pitch's picture from a Tile Agent map (`bin/grafs_dos/pitch.tmj`), stage 1 — the ground; why the match is still played on the game's own files; the tile-for-tile check. | current | `pitch_maps`, `src/pitch_map.c`, the Tile Agent runtime |
| [PITCH_FORMAT_V2.md](PITCH_FORMAT_V2.md) | A layered, animated pitch format on Tiled `.tmj` maps. | plan; §2–§5 superseded by Tile Agent's format (PITCH_MAPS.md), the engine-side sections stand | pitch layers or animation |
| [CROWD_ANIMATION.md](CROWD_ANIMATION.md) | How the fans animate: tile toggles, the 8-slot pump, the state machine, `Rand()` use, the widescreen mirror, replays and rollback. | current | `animated_patterns.c`, celebrations, the `Rand()` stream |
| [SHADERS.md](SHADERS.md) | A GL present path (index/palette textures, CRT shader) and an SDL3 assessment. | plan (not started; the CPU CRT effects exist) | GPU, shader or SDL3 work |
| [WEATHER_FX.md](WEATHER_FX.md) | Weather visuals under a strict "never touch the simulation" rule. | plan (nothing implemented) | any weather effect |
| [ARCADE.md](ARCADE.md) | The coin-op arcade mode: attract reels, service menu, golden goal, high scores, `[arcade]` keys. | current | arcade/attract mode (`src/arcade.c`) |
| [WASM.md](WASM.md) | The Emscripten/ASYNCIFY web target's design, and the asm-storage-to-C log. Now live, online, plus the player build. | historical | the web build's pacing, threading, IDBFS saves, preload list |
| [MACOS_BUILD.md](MACOS_BUILD.md) | Building on a Mac: universal binary, `make macos-app`, Gatekeeper; releases go through `scripts/build_macos.sh`. | current | building on the Mac itself |
| [LOCAL_WORKFLOW.md](LOCAL_WORKFLOW.md) | **Start here on this machine.** Copy-paste commands to build (m68k toolchain under `~/tools`, `gnumake` not `make`, the Cygwin shell for SDL, the real python3), to run (fire to leave the intro, `--menu=`, `swos_drive.ps1`, FS-UAE), and to verify a menu/data change (`emit_menus.py --out` diff, `SWOS_MENU_JSON_VERIFY`), plus the menu colour nibbles. CLAUDE.md's and `build.cmd`'s paths are the original author's and fail here. | current | the start of any session that builds, runs or screenshots the game |
| [SWITCH.md](SWITCH.md) | Building, packaging and debugging the Switch flavour; the filesystem/logging differences. No SDK content (NDA). | current — but it says "builds, links and packages" where CLAUDE.md says "not yet linked or run": the owner knows which | `Makefile.switch`, `scripts/build_switch.*` |

## The Amiga target (AGA resumed 2026-10-10: a whole match plays)

| doc | what it is | status | read when |
|---|---|---|---|
| [GDB_DEBUGGING.md](GDB_DEBUGGING.md) | The gdb workflow for both targets: FS-UAE + attach, `gdb_proxy.py`, watchpoints, the one-shot port rule, SDL native mode. | current | attaching gdb to either target |
| [GDB_HARNESS.md](GDB_HARNESS.md) | Driving the game headlessly through gdb: menus by input injection, careers, seasons, capture, TURBO. | current | automating careers/seasons/menus |
| [TESTING_FLOWS_DESIGN.md](TESTING_FLOWS_DESIGN.md) | The flow-testing framework (`scripts/testing_support/`, `scripts/swos_test.py`): typed JSON goals → offline catalog resolution → planned, verified menu flows on SDL and FS-UAE (careers, friendlies to the match boundary), with snapshots, fingerprints and JSON reports. | current | scripting a game scenario from data (a career, a friendly) or extending the flow framework |
| [DEBUG_TOOLKIT.md](DEBUG_TOOLKIT.md) | Terminal tools for a running Amiga: DMA profiler, CPU flame graph, copper disassembler, frame capture. | current | an Amiga-only display/DMA/timing artifact |
| [AGA.md](AGA.md) | AGA 8-bitplane rendering: mixed-depth banks, `grafs_aga`, 256-colour palettes, the FMODE pitch-scroll saga. | parked | the Amiga display, copper, FMODE, `grafs_aga` |
| [AGA_TEST.md](AGA_TEST.md) | Entry point for the AGA scroll/sprite test bed (`PITCH_SCROLL_TEST`). | parked | resuming AGA work |
| [AMIGA_SPRITES.md](AMIGA_SPRITES.md) | Hardware sprites vs blitter bobs in the match renderer; 4bpp sprite storage (P3, mostly landed). | parked | Amiga sprite rendering |
| [AMIGA_DOS_DIFF.md](AMIGA_DOS_DIFF.md) | DOS vs Amiga sprite index table (gameplay sprites: DOS = Amiga + 114). | current | remapping DOS/2020 `.RPL`/`.HIL` sprites |

## Audio

| doc | what it is | status | read when |
|---|---|---|---|
| [SFX.md](SFX.md) | Research log mapping the RJP1 SFX bank's ids to sounds. `include/sfx_ids.h` is the authority now. | historical | sound-effect ids, the SFX trigger path |
| [SFX_REVIEW.md](SFX_REVIEW.md) | Fill-in checklist for naming every SFX id; its Macro column predates the rename. | historical | confirming SFX ids still marked untested |
| [RJP1.txt](RJP1.txt) | Third-party description of the Richard Joseph Player music/SFX format (2012). | reference | the RJP1 decoder (`src/rjp1.c`) |

## Process

| doc | what it is | status | read when |
|---|---|---|---|
| [TRIAGE.md](TRIAGE.md) | From a Discord ReportBot report or feature request to a published drop: queue, feature approval, reproduce, commit, build the drop, changelog, publish, close, announce. | current | `/triage`, `/requests`, cutting or publishing a drop, posting to #swos-changelog |

## Known gaps (found by the 2026-09-30 review, not fixed in docs)

- `docs/DOS_VIEW.md` is cited by `include/display.h` and does not exist; the DOS view is
  described in [DOS_GAMEPLAY.md](DOS_GAMEPLAY.md) and [WIDESCREEN.md](WIDESCREEN.md).
- `.claude/CLAUDE.md` keeps binary read support for a `.CFG` options file that this port does not
  have (the options are JSON fields of the career/preset/DIY saves) — the owner's rule to settle.
- Code the review flagged (not docs): `src/menus.c` treats season 19 as the last one in two places
  where it should read the configured `num_seasons`; `scripts/swos_plugin.py`'s season breakpoint
  `career_init.c:974` now lands in another function; `gdb_proxy.py`'s fallback binary path is
  pre-merge; stale comments in `swos_settings.c` (a `[dos]` section), `include/platform_sdl.h`,
  `include/display.h`, `include/sfx_ids.h`, `src/menus.c` (web has no MULTIPLAYER) and
  `src/ai.c` (87.5% should be 75%); `_goalkeeperSkipFlag` declared `int16_t`, defined `uint16_t[1]`.

---

# Paths — every location the project uses

`C:\...` and `/mnt/c/...` are the same place (Windows / WSL). Paths marked **not here** are
referenced but do not exist on the owner's PC; they are on another machine, or gone.

## Inside this repository

| path | what |
|---|---|
| `.claude/CLAUDE.md` | the project's orientation and rules — read first |
| `src/`, `include/` | the one C source tree for every target |
| `asm/`, `Resload.s`, `support/` | the live m68k assembly (the Amiga target) |
| `data/` | reference only, never compiled: `SWOS2.s` (the disassembly), `Data.c`, `SWOS2_IDA.h`, `SWOS2.h` |
| `bin/` | the runtime tree every target reads and links into: `data grafs grafs_aga grafs_dos grafs_2020 grafs_arcade grafs_jag sound data_json save`, `c`/`s` (AmigaDOS boot files), the binaries, the Cygwin DLLs |
| `bin/data_json/` | editable JSON baked into C / loaded at run time ([DATA_PIPELINE.md](DATA_PIPELINE.md)) |
| `build/<os>/<arch>/` | SDL objects (each with a `.build-flags` stamp); `build/matchmaker/` the server; `build/recordings/`, `build/reports/` fetched from the server; `build/recordings-pred/` sample PRED recordings |
| `obj/` | Amiga objects (`make -f Makefile.amiga`); `obj-switch/`, `build-switch/` the Switch build |
| `dist/` | everything a tester gets (zips, installer, `.nsp`, macOS archives), `dist/web/` the web build, `dist/playback/` the recordings player (gitignored) |
| `tests/` | `dos/` the replay-rig corpus (`.RGD.gz`), `replay/`, `replay-v2/`, `rollback/`, `netplay/`, `hunt/` |
| `scripts/` | build, release, test and tooling scripts; `scripts/amiga/amigascope/` the AmigaScope submodule |
| `third_party/` | `swosgfx/` (submodule: the graphics converters `tools/swosdata` is built on), `ggpo-x/` (submodule, branch `openswos`), `matchmaker/` (the server), `enet/`, `miniz/` |
| `tools/` | `matchmaker-admin/` (the server's admin page), `mm_refpeer/`, address/stub generators |
| `tools/swosdata/` | the data setup: builds the original game's data (not in the repo) from the player's own copy into the assets folder. `lib/` (C: ADF reader, finding the player's copy, the file lists, team JSON, the Jaguar tier; the converters are `third_party/swosgfx`'s), `cli/` (`swosdata`), `app/` (`openswos-setup`, Slint; `build.sh`/`build.bat static`) |
| `bin/setup/` | the setup's own inputs: `glyphs.txt` (the extended font) and the hand-drawn Jaguar goal celebrations |
| `packaging/` | `web/` (`changelog.html`, the record of every drop), `windows/` (Inno Setup), `macos/`, `linux/`, `switch/`, icons |
| `wasm/` | `shell.html` (the game page), `player.html` + `playback-index.html` (the recordings player), `pre.js` |
| `SWOS.svg` | the master icon (`scripts/generate_icons.py`) |
| `docs/images/openswos-logo.png` | the OpenSWOS logo (README, the itch.io cover) |
| `docs/images/screenshots/` | the README's screenshots (the same as the itch.io page's) |
| `docs/images/setup/` | OpenSWOS Setup on Windows (start, found, done) for INSTALL.md: retaken with the setup's `--snapshot` from neutral copies of the sources (never the disk images' own names) and its default assets folder |

## On the owner's PC

| path | what |
|---|---|
| `C:\Projects\AI\SWOS\OpenSWOS` | this repo (`/mnt/c/Projects/AI/SWOS/OpenSWOS`) |
| `C:\Projects\Amiga\SWOS-RE` | SWOS-RE: the reverse-engineering project, the authority on original intent (IDA/Ghidra databases, `Docs/`) |
| `C:\Projects\GitHub\swos-2020` | SWOS 2020, which OpenSWOS continues: its online menus and netplay are the references |
| `C:\Projects\GitHub\swos-port` | the DOS port whose recordings (`tests/data/`) seeded the replay rig |
| `C:\Projects\AI\SWOS\swos-port-c` | partial C++ port with the Amiga game loop working; the verified game-loop reference |
| `C:\Projects\AI\SWOS\swosgfx` | swosgfx (github.com/benbaker76/swosgfx): the C99/C++ rewrite of SwosGfx, byte-identical to it; the converters `tools/swosdata` uses (submodule `third_party/swosgfx`). Converter changes go here |
| `C:\Projects\GitHub\SwosGfx` | the C# SwosGfx (now github.com/benbaker76/SwosGfx-CSharp); `SwosGfx\AmigaTools.cs` is the authority for Amiga SPRITE/PITCH/PALETTE parsing; `bin\Debug\net8.0\Amiga_Images_16` the extracted graphics |
| `C:\msys64\home\headk\rjp1player` | the RJP1 player for the music modules and SFX extraction |
| `C:\Projects\AI\AmigaScope` | a standalone checkout of AmigaScope (the repo uses the submodule) |
| `C:\Projects\Python\SWOS_Font` | the font tooling ([FONTS.md](FONTS.md)) |
| `C:\Users\headk\.vscode\extensions\bartmanabyss.amiga-debug-1.8.2\bin\linux` | the Amiga toolchain — **must be the benbaker76 fork** of vscode-amiga-debug (CLAUDE.md "Building"); `AMIGA_TOOLCHAIN` overrides |
| `C:\Projects\Linux\fs-uae\fs-uae` | the modified FS-UAE the harness uses (`scripts/amiga/amigascope.env`, `FSUAE_BIN`) |
| `C:\Projects\Amiga\Kickstarts\KICK31_A1200.rom` | the Kickstart ROM FS-UAE boots |
| `C:\Projects\Linux\emsdk` | emsdk for the web builds (`EMSDK_DIR`; `build_playback.sh` also looks in `~/emsdk`, **not here**) |
| `C:\cygwin64\bin\bash.exe` | Cygwin, which builds `SWOS.exe` (`CYGWIN_BASH`) |
| `C:\Program Files\Inno Setup 7\ISCC.exe` | Inno Setup, for `SWOS-Setup.exe` (`build_installer.sh` searches `Program Files` for any version; `ISCC=` overrides) |
| `D:\Projects\GitHub\ReportBot` | the Discord ReportBot; `bin/reportctl` works the inbox ([TRIAGE.md](TRIAGE.md)) |
| `~/.swos/` (WSL) | the Linux build's `SWOS.ini`, `SWOS.log`, `save/` |
| `~/.cache/ms-playwright/` | headless Chrome for the WASM tests |
| `~/.claude/projects/-mnt-c-Projects-AI-SWOS-OpenSWOS/memory/` | Claude's memory for this project |
| `C:\Projects\AI\SWOS\SWOS_C`, `SWOS_C_SDL` | the two pre-merge trees — **not here**, gone since 2026-08-12 |
| `D:\Projects\SWOS\OpenSWOS\...` (Arcade, Copa Libertadores notes) | design notes some docs cite — **not here** on this PC |
| the Switch SDK and SDL fork | outside the repo, under NDA — never committed ([SWITCH.md](SWITCH.md)) |

## Other machines and servers

| location | what |
|---|---|
| Mac mini `bbaker@192.168.1.200:~/Programming/OpenSWOS` | a checkout of this repo that builds the macOS archives over ssh (`scripts/build_macos.sh`; `MAC_HOST`, `MAC_PATH`) |
| `online.openswos.net` (the matchmaking server, the game's default since 2026-10-20) | IONOS VPS 74.208.5.140, Debian 12, `ssh root@...`; checkout `/home/swos/OpenSWOS` (user `swos`, pulled with a read-only GitHub deploy key); deployed to `/srv/swos-matchmaker/` like us.swos.eu below; emsdk in `/home/swos/emsdk`, the player's game data in `/home/swos/.swos/assets` (`swos` only); nginx sites in `packaging/server/nginx/`; Let's Encrypt for both names |
| `https://online.openswos.net/playback/`, `/recordings/`, `/reports/`, `/cc/` | the recordings player, the recordings and reports, the admin page -- ALL behind the admin login (internal); `scripts/fetch_recordings.py` / `fetch_reports.py` send it and delete what they fetch |
| `online.openswos.net:19602` (UDP, and TCP for the recorder and the `wss://` gate) | the matchmaker the game logs in to |
| `https://openswos.net` | the public landing page (`packaging/web/site`): the logo, the itch.io link, and `/install/`, the web version of INSTALL.md (change both together) |
| `us.swos.eu` (Redhair's server, the default until 2026-10-20) | repo at `/home/redhair/git/OpenSWOS`; deployed to `/srv/swos-matchmaker/` (`bin/`, `recordings/`, `reports/`, `playback/`, `tls/`, `matches.jsonl`, `admin/`); systemd unit `swos-matchmaker`. Builds before the switch still log in here |
| `https://baker76.itch.io/openswos` | where releases go (`scripts/upload_itch.sh`, butler; [TRIAGE.md](TRIAGE.md) §6), and the one link to give testers. Until 2026-10-20 drops went to baker76.com over FTP; nothing is published there any more |
| GitHub `benbaker76/OpenSWOS`, `benbaker76/SWOS-RE`, `benbaker76/AmigaScope`, `benbaker76/ggpo-x`, `benbaker76/vscode-amiga-debug` | this repo, the RE project, the submodules, the required toolchain fork |

## Where the game keeps its files (per platform)

| platform | `SWOS.ini`, `SWOS.log` | saves (`.CAR`, `.SRP`, …) |
|---|---|---|
| Windows | `%USERPROFILE%\Documents\SWOS` | `...\Documents\SWOS\save` |
| macOS | `~/Library/Application Support/SWOS` | `.../SWOS/save` |
| Linux | `~/.swos` | `~/.swos/save` |
| Web | IndexedDB (`/save`, `wasm/pre.js`) | the same |
| Switch | `save:/` | the same |
| Amiga | `bin/` (FS-UAE's `DH1:`) | `save/`, which must already exist |

The game data itself is read beside the binary (it `chdir`s to its own folder): `bin/` in the
repo, the zip root in a drop. See [SAVE.md](SAVE.md).
