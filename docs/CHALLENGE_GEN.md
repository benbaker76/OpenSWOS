# The challenge generator — handover, 2026-10-27

## Amiga follow-up

Generation now runs on both targets. `scripts/challenge_gen_amiga.ps1` stages an
isolated FS-UAE drawer and the same JSON spec as `data/CHALGEN.DAT`, then copies
the native result into `bin/save/`. `ChallengeGen_Boot` checks this optional job
before the intro. The native parser uses `mjson_get_long`; team lookup probes
existing TEAM files and delegates RNC decompression/global numbering to
`LoadTeamFile`. Names or `TEAM.nnn:ordinal` select teams; UUID lookup remains SDL-only.

The phase machine, score bookkeeping and restart capture are shared. Amiga input
injects fire before `Player1StatusProc`'s edge tracking, rather than writing a
physical CIA register. Success/failure writes `data/CHALGEN.LOG`; completion unwinds
the game loop and restores hardware normally. An ordinary boot without the job
file is unchanged. The helper never changes the regular drawer's startup sequence.

Both targets build; SDL `--challenge-selftest` and a generated SDL corner pass.
The user confirmed both native examples load and play correctly: Milan 2–0 Juventus
at an 88-minute kickoff, and Partizan 0–3 Red Star at a 67-minute Partizan corner.
The files are tracked as `bin/save/AMI88.CHL` and `bin/save/PART67.CHL`; their specs
live in `docs/challenge_specs/`. Other personal saves remain ignored. The original v1 notes
below describe the SDL implementation before this follow-up; Amiga generation is
no longer an outstanding roadmap item.

What was built, how it works, what to do next. The user-facing guide is the
"Generating challenges" section of [CHALLENGE.md](CHALLENGE.md); this page is for
whoever works on it next.

## What is done and user-verified

`SWOS --challenge-gen=spec.json` (SDL only) constructs a `.CHL` nobody had to play
into. All of the following was tested by the user on real generated files:

- **Teams by database name** (or uuid; a duplicated name is refused with the
  candidates' uuids), either side human (`"human": 1|2`).
- **Score via scorers** — each goal a side (1/2), a shirt number from the starting
  eleven and a minute; the scorers ARE the score (`"score"` is only a cross-check).
  Real names show on the banner (Savicevic scored in a test).
- **Minute 1–89**, with a position past 45 reached by the engine playing its own
  half time (see "how" below).
- **Situations**: the kickoff after a goal (default), or a **corner**
  (`"situation": "corner"`, `"corner": {"wing": "left"|"right"}`).
- **`"ball": 1|2`** — who has the ball. For a kickoff that means restarting after
  the OTHER side's latest goal (a restart belongs to the conceder — the game's own
  rule); for a corner it picks the taker.
- **Extra time + penalties on a level score at 90**, ON by default
  (`"extra_time"/"penalties": false` opt out) — the friendly's tie-rule globals,
  set after the init (which clears them) so the setup block carries them.
- `scripts/chl_dump.py` decodes any `.CHL`'s header + setup (and `--diff`s two).

Worked example specs: Milan 2–0 Juventus at 88' saved at the restart kickoff;
Juventus 3–1 Milan at 76' with Milan's ball; Juventus 4–2 Milan at 80' at Milan's
left-wing corner.

## How it works (the design, in one paragraph each)

**The engine builds every byte the spec does not name.** The SIM state blob
(~15 KB, ~355 registry regions) is never fabricated: a real headless friendly
kicks off between the spec's teams (dummy video + `SWOS_HARNESS_TURBO`, seconds
per file), the module writes the spec into the LIVE state, and the ordinary
challenge encoder saves it — so a generated file is exactly what a C-key capture
is. Offline byte-patching of the blob was considered and rejected: offsets are
the build's registry (hence the layout hash), and score/possession/team identity
each live in many fields that must agree.

**Score is written the way the engine writes it.** Past goals call `GoalScored`
(the complete bookkeeping: totals, stats, digit words, scorer tables with the
clock pre-set to each goal's minute, name sprites) with the celebration flags
cleared in the same hook call, so the engine never sees them. The restart goal is
scored FOR REAL: the ball is teleported into the goal window the detection code
scores from (`CheckIfBallOutOfPlay`, src/game_timers.c — a ball over the top line
is a goal for the side `_bottomTeamData` holds) with the spec's scorer as
`_lastPlayerPlayed` (where `handleGoalScored` reads the scorer), and the engine
runs its own celebration and restart. A corner is the same trick: ball just past
the conceding side's end line, `_lastTeamPlayed` = the defenders, and the engine
awards and places it.

**The capture point is `ST_WAITING_ON_PLAYER`.** Every restart — goal kickoff,
corner, free kick, throw-in — funnels through the break state machine
(`_breakCameraMode`, src/game_timers.c), whose final phase sets
`_gameStatePl = ST_WAITING_ON_PLAYER`: line-up complete, game waiting on the
taker. A human taker waits forever and a computer one for `_gsKickoffStoppage`
ticks, so the position is stable for the save. The final clock is set there,
3 settle frames run, then `Challenge_SaveAs`.

**Half time is the engine's own, whistled by hand.** For minute > 45 the module
sets the clock to 45 and calls `EndFirstHalf()` + `BumpPlayersLastPlayedHalfAtHalfEnd()`
(the body of UpdateTime's minute-45 branch) at the first in-play frame — then the
engine plays the whole break (end swap, `_halfNumber`, walk-out) in turbo.
Letting the CLOCK blow the whistle instead gave the computer open play at 45 (the
last-minute possession extension re-arms the countdown) — long enough to score,
which a probe now logs and a 0:0 check turns into a clean failure.

**Restarts nobody can take are taken for them.** A headless run has no player, so
in the non-capture wait phases a fire press is PULSED into the joystick register
(`_ciaa_pra` bit 7 — the write `MatchStep_ApplyInput` does for the rigs; pulsed so
edge detectors see press and release). Setting `TeamGeneralInfo.firePressed` from
the hook does NOT work: the real input read later in the frame overwrites it.
The half-time result screen is dismissed the same way.

## Loader fixes that came out of it (both targets, every .CHL)

Two "pixels are not state" bugs in `Challenge_LoadState`, found because generated
files always carry scorers:

- **Scorer banner**: the tables/widths/sprite structs are registry state and
  restore, but the name PIXELS live in sprite-graphics slots the registry
  deliberately leaves alone — the banner drew garbage (almost certainly
  CHALLENGE.md's old "red cards in the score panel" mystery).
  `RebuildScorerSprites()` (src/goal_scored.c) redraws them from the tables,
  taking the restored layout (numSpritesTaken, imageIndexes) as-is — it never
  re-allocates rows, because GoalScored's live row-stealing is already reflected
  in the restored fields.
- **Clock**: same shape — `_copyTimeSprite = 1` after load, or the first frames
  show whatever minute the digit sprites last held.

## Files

New: `src/challenge_gen.c`, `include/challenge_gen.h`, `scripts/chl_dump.py`,
this page. Changed: `src/challenge.c` (+`Challenge_SaveAs`, the two loader
fixes), `include/challenge.h`, `src/goal_scored.c`/`.h` (`RebuildScorerSprites`),
`src/game_init.c` (SetupPlayers bypass branch), `src/game_loop.c` (per-frame
hook), `src/main.c` (flag + headless env), `src/SWOS.c` (Phase 9 dispatch),
`Makefile.amiga` (`challenge_gen.c` into `SDL_ONLY_SRCS`), `docs/CHALLENGE.md`.

## Traps for whoever touches this next

- `PrepareForInitialKick` does NOT take the kick — it re-arms the restart.
  Calling it on every `ST_WAITING_ON_PLAYER` frame loops the line-up forever.
- The generated file inherits the generating run's RULES byte (`--dos-gameplay`,
  `--view`, the no-cards setting — this machine's SWOS.ini has no-cards ON, so
  files generated here say `rules 0x04` and only load on a build playing that).
  Generate with the players' rules.
- The sim width saved is the headless run's (362 here). Legitimate, but if a
  canonical width is ever wanted, set `_sim_draw_width` before the match.
- "top"/"bottom" are SCREEN positions; map a spec side through
  `TeamGeneralInfo.teamNumber` (`side_data()`), as `handleGoalScored` does.
- The `[chalgen] waiting`/goal-watch probes only print on armed runs; they are
  the debugging story for a wedged generation — keep them.

## What should go next

1. **Reference corpus** (the user offered): play matches and press C at corners,
   fouls, free kicks, keeper-holds-ball — the captures are the ground truth for
   adding those situations, and `chl_dump.py --diff` against generated files is
   the A/B check.
2. **More situations**: free kick (states 6–12 — award path is a FOUL, so
   probably `_gameState`+`writeRestartGlobalsAndStopPlayers` directly or a
   forced foul), throw-in (15–20, ball over a side line), keeper-holds-ball
   (state 3 — `GoalkeeperClaimedTheBall` is the engine's own claim path,
   update_players.c:912 is the field-set authority), penalty (14).
3. **Open-play positions** ("ball with the GK", player x/y) — deferred from v1;
   the keeper claim path above is the starting point.
4. **Bookings / cards / subs-used** in the spec.
5. **Minutes 90+ / extra-time positions** (needs `_extraTimeState` phase
   handling, the 105' end and the 120' whistle).
6. **In-game editor UI** on top of the CLI, and/or a batch mode (one run per
   spec file is seconds, so a folder of specs is already scriptable).
7. **Amiga generation** — a `.CHL` is per-target (layout hash); the m68k build
   would need the module compiled in (it reads no JSON today) or the FS-UAE gdb
   harness driving the same writes.
8. Consider committing a couple of generated samples as additional bundled
   challenges (`Challenge_EnsureSample` pattern).
