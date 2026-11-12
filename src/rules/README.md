# src/rules -- the season-end and cup rules

The Italian play-offs, every pack's knockouts, the career club's play-off contests, B teams and the
season's moves, the cups' clubs, the Coppa D's later seasons, the parallel groups: freestanding
C99, no libc, one caller-owned state (`sr_state`, `swosrules.h`), built on every target through
`src/swos_rules_*.c`.

**Where it came from.** Until 2026-11-11 this was a submodule, `third_party/swos-competition-engine`
(github.com/aminta/swos-competition-engine), a library shared with Davide Lorigliola's SWOS 96/97
Mod, at its tag v1 (87b6527). The owner moved it into OpenSWOS: the files here are that tag's
`rules/`, unchanged. They came under the MIT licence, Copyright (c) 2026 Davide Lorigliola
(`LICENSE-MIT`, kept with them as that licence requires); what OpenSWOS changes from here on is
OpenSWOS's, GPL-3.0-or-later like the rest of the game.

**Keeping up with the Mod.** A rule change in a new Mod release is ported here from its public
repo (`tools/dplay.c`, `gplay.c`, `conti.c`, ...: the release notes and the diff between tags);
`scripts/league_playoffs_check.sh` builds the Mod's own C beside ours and compares them
(docs/RULES_LIBRARY.md).
