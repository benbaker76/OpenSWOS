# FORK_MODS.md — where petarku's work went

Status (2026-11-05): **current.** This fork carries no code of its own: everything from its two
branches is now in OpenSWOS itself, as canonical mods or as base engine features, credited to the5ar.
So the fork can track OpenSWOS (`git merge upstream/main`, never rebase; docs/FORKING.md) with
nothing to conflict. How mods work: docs/MODS.md.

Until 2026-11-04 the fork's work was on two branches, `classic-competitions` (OpenSWOS PR #11)
and `game-modes-and-android` (PR #3). Both stay as they are for their history. Each feature went
one of three ways: into the engine, into a mod OpenSWOS ships, or out, because OpenSWOS now does
the same.

## As mods (OPTIONS -> MODS turns each on or off)

| feature | mod | on by default | how to reach it |
|---|---|---|---|
| HISTORIC TOURNAMENTS (PR #11) | `classics` (`src/mod_classics.c`) | yes | PRESET COMPETITION, the row above WORLD CUP. It shows when SWOS 2020's cups are in `<assets>/classic/{uefa,worldcup,club}/` (`scripts/copy_classics.ps1`) |
| SHOOTING PRACTICE | `practice` (`src/mod_practice.c`, "shooting") | yes | MODES -> PRACTICE -> SHOOTING PRACTICE |
| PENALTY SHOOTOUT, its SHOOTING ORDER screen and the manual keeper | `practice` | yes | MODES -> PRACTICE -> PENALTY SHOOTOUT |
| the six collection challenges (FINISH THE JOB .. DERBY MIRACLE) | `challenge-collection` (data for `challenges`) | yes | MODES -> CHALLENGES |
| CHAMPIONS / EUROPA / CONFERENCE LEAGUE, 36 clubs, in 96/97 | `euro36` (`src/mod_euro36.c`, on `league_phase.c`) | **no** | turn it on; then PRESET -> EUROPE, SEASON and CAREER play the league phases. Its UEFA coefficients are derived from the clubs' strength, not real ones (MODS.md §22) |

None runs on the Amiga yet. Mod code and menus are SDL only until MODS.md §15.5 is done, so the
Amiga builds of HISTORIC TOURNAMENTS, shooting practice and the challenges are not in OpenSWOS.

## Into the engine

| feature (fork commit) | OpenSWOS |
|---|---|
| PC / SWOS 2020 `.DIY` reader, `--convert-diy` (PR #11) | f35ec523 |
| competition save/load speedup (630e30e8) | 5f30c51e |
| ESC quitting from a match started from a menu callback (af534dca) | dd254a18 |
| keycache stride 298; the Amiga save-writer's memory (b901477f, 723acf73) | 18064bcf |
| Android: hold Start / R3 for ESC (25843bdb) | 6154b4d6 |
| what the modes needed: every kick-off one side's, a hidden clock, the PLAY MATCH screen before a mod's match, the shooting order, the manual penalty keeper, a starting corner / kick-off, the pitch, goals already scored | `ModMatchSetup` (c6c88d05) |
| 36-club league phases outside World 2026-27 | `ModApi_SetLeaguePhases` (ed2cc072) |

## Not taken

| feature | why |
|---|---|
| CPU cards and its OPTIONS row (f3c98c81) | OpenSWOS has `[tweaks] cpu_cards` |
| the manual keeper at every penalty of a normal match (6f423e3b) | in mods' matches now; as a tweak for every match only if SWOS2.s agrees (MODS.md §4) |
| EDIT grouping on the main menu (d41ee5e8) | it freed a row for SHOOTING PRACTICE, which the MODES button now gives |
| `.CHL` save-state challenges, the in-match save key, `--challenge-gen` | a `.CHL` is bound to the build's `sim_state` layout; the six collection challenges live on as `challenge-collection` |
| `android/` gradle project, the KEYS panel, the IME pump | OpenSWOS's `packaging/android`, its on-screen controls and the system keyboard |
| the testing framework (`scripts/testing_support`, the drivers, AGENTS.md) | not taken; still in the old branches |
