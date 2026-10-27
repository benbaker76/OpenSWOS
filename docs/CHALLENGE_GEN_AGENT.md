# Brief for the next agent — challenge generator, 2026-10-27

You are picking up the CHALLENGE GENERATOR: `SWOS --challenge-gen=spec.json` builds a
`.CHL` save state from a declarative spec (teams, human side, scorers, minute, kickoff-
after-goal or corner, who has the ball, tie rules). v1 is DONE, user-verified and
committed (`2077b85`). Your job is the next-steps list at the bottom.

## Read first, in this order

1. **docs/LOCAL_WORKFLOW.md** — how to build and run ANYTHING on this machine. The
   paths in `.claude/CLAUDE.md` are the original author's and fail here.
2. **docs/CHALLENGE_GEN.md** — the technical handover: how the generator works, the
   loader fixes that came out of it, the traps, the roadmap. Do not re-derive it.
3. **docs/CHALLENGE.md** — the feature the generator feeds ("Generating challenges" is
   the user guide; the rest is the save-state format and loader).
4. `src/challenge_gen.c` — heavily commented; the comments are the design notes.

## State of play

- Generator works end to end: `--challenge-gen` → headless friendly → spec written into
  live state → saved at the restart line-up → exit. Seconds per file.
- Situations: kickoff after a chosen goal (`"ball"` picks who restarts) and corner
  (`"corner": {"wing": ...}`, taker from `"ball"`). Extra time + penalties default ON.
- Loader now rebuilds scorer-banner and clock pixels on every `.CHL` load (shared code,
  fixes the Amiga too; likely closed CHALLENGE.md's old "red cards in panel" issue).
- `scripts/chl_dump.py` decodes/diffs `.CHL` headers + setup blocks.
- `--challenge-selftest` passes; SDL and m68k both build. The module is SDL-only
  (`SDL_ONLY_SRCS` in Makefile.amiga); `.CHL` files are per-target by layout hash.

## The verify loop (what "done" means here)

```sh
# build (Cygwin shell — the Makefile picks the target from the host shell)
/c/cygwin64/bin/bash.exe -lc "cd /cygdrive/c/Users/A7126/orca/OpenSWOS && make -j8"

# generate (PowerShell, from bin/ — nested Cygwin bash dies silently, see memory)
.\SWOS.exe --challenge-gen=<abs path to spec.json>     # prints [chalgen] lines + exits

# inspect the file (real Python, not the Store stub)
C:\...\Python312\python scripts/chl_dump.py %USERPROFILE%\Documents\SWOS\save\X.CHL

# prove it plays: launch windowed and LET THE USER TEST IT — they want to play these
Start-Process .\SWOS.exe -ArgumentList "--challenge=X.CHL"
```

A `[chalgen] FAILED` line names the phase and the full game state; the `waiting`/goal
probes print every 5000 frames of a stuck phase. That is your debugging story — add
probes, don't guess (it found every bug this session).

Working spec examples are in the session scratchpad pattern; a minimal one:

```json
{ "team1": "AC MILAN", "team2": "JUVENTUS", "human": 1,
  "scorers": [ {"side":1,"shirt":9,"minute":12} ], "minute": 30, "out": "T.CHL" }
```

## Facts that cost real time — do not rediscover

- `PrepareForInitialKick` RE-ARMS a restart; calling it on `ST_WAITING_ON_PLAYER`
  frames loops the line-up forever. The capture point IS `ST_WAITING_ON_PLAYER`.
- Input from the frame hook goes into the joystick register (`_ciaa_pra` bit 7,
  pulsed), never `TeamGeneralInfo.firePressed` — the real input read overwrites it.
- Skip a half with `EndFirstHalf()` + `BumpPlayersLastPlayedHalfAtHalfEnd()` directly;
  letting the clock whistle gives the CPU open play at 45 (it scored, once).
- "top"/"bottom" are screen slots, not team 1/2 — map through
  `TeamGeneralInfo.teamNumber` (`side_data()` in challenge_gen.c).
- Pixels are not state: anything drawn from sprite-graphics slots (banners, clock)
  must be redrawn after a state load.
- This machine's SWOS.ini has no-cards ON → generated files carry rules 0x04 and only
  load on a build playing the same rule. Generate with the players' rules.
- The user tests by playing: launch challenges WINDOWED for them, never kill their
  running game without saying so (relinking needs the exe unlocked — check first).

## Next tasks (detail + entry points in docs/CHALLENGE_GEN.md "What should go next")

1. **Reference corpus**: the user offered to press C at corners/fouls/keeper-catches.
   Collect those CHALnn.CHL from `%USERPROFILE%\Documents\SWOS\save`, decode, keep with
   notes; `chl_dump.py --diff` generated-vs-captured is the A/B check.
2. **Free kick / throw-in / keeper-holds-ball / penalty situations**: same pattern as
   the corner — drive the engine's own award path, capture at WAITING. States are in
   game_types.h (4–20), award logic in `CheckIfBallOutOfPlay` + foul handling.
3. **Open play** ("ball with the GK", player positions): `GoalkeeperClaimedTheBall` is
   the engine's claim path, update_players.c:912 the field-set authority.
4. **Bookings/subs in the spec**; **minutes 90+** (extra-time phases).
5. **Editor mode** (`"base": "CHAL02.CHL"` → load, apply deltas, re-save) and an
   in-game UI later; **Amiga generation** last (needs JSON-less spec or the gdb rig).

Keep the plan's discipline: the engine builds every byte the spec does not name; when
a new situation is wanted, find how the engine awards it and force the PRECONDITION,
never hand-assemble the dead-ball state.
