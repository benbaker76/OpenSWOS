# Saved positions and challenges

Implemented for Amiga and SDL. A challenge is a saved position from a match
against the computer. Load it to play on with the same teams, score, clock,
players, ball, bookings and substitutions remaining.

## Challenge collection and progress

For the full feature guide, save format and maintenance notes, see
[CHALLENGE_COLLECTION.md](CHALLENGE_COLLECTION.md).

CHALLENGES now opens **CHALLENGE COLLECTION** and **SAVED POSITIONS**.
The collection is available on SDL and Amiga, with two challenges in each group:

| Difficulty | Challenge | Human team first | Start |
| --- | --- | --- | --- |
| Easy | Finish the Job | AC Milan vs Juventus | 2-0 at 75, Juventus kickoff |
| Easy | Hold Your Nerve | Italy vs Malta | 1-0 at 80, Malta corner |
| Medium | Find the Winner | Liverpool vs Everton | 0-0 at 75, Liverpool corner |
| Medium | Turn It Around | Barcelona vs Real Madrid | 0-1 at 65, Barcelona kickoff |
| Hard | Against the Clock | Juventus vs AC Milan | 0-2 at 80, Juventus kickoff |
| Hard | Derby Miracle | Partizan vs Red Star | 0-3 at 67, Partizan corner |

All six are available immediately. Select a row for its details, then PLAY.
**Win at full time** to complete it; a draw or abandoning the match fails.
There is no extra time or penalty shootout. The existing REPLAY / EXIT screen
still appears after the match: choose EXIT to see the challenge result, then
RETRY, NEXT CHALLENGE (after success), or CHALLENGE LIST. A failed replay never
removes an existing completion. Finishing all six shows ALL CHALLENGES COMPLETED.
REPLAY on the existing match-end screen reloads the original challenge position,
including its minute, score and players, and starts a fresh completion attempt.
This also applies to personal saved positions; ordinary friendly replays still
start a new match.

Progress saves automatically at the final whistle and loads when entering the
collection after a restart. `CHPROG1.PRG` and `CHPROG2.PRG` live in the normal
save folder. These are alternating, versioned, checksummed records of stable
challenge IDs; the previous valid write survives an interrupted save. The UI
reports a failed save and offers RETRY SAVE. On FS-UAE, use the same persistent
mounted game drawer (`bin/` here); emulator snapshots are unnecessary. Keep the
save folder writable and preserve both progress files when moving installations.

The starting positions remain bundled assets, separate from progress and personal
saves: `data/CCS01.DAT` through `CCS06.DAT` for SDL, `data/CCA01.DAT` through
`CCA06.DAT` for Amiga. They are native `.CHL` payloads with an asset extension,
loaded directly without copying over user files. Collection play uses standard
Amiga gameplay/view and cards rules, restoring SDL preferences afterwards.

Regeneration specs are retained in `docs/challenge_specs/collection/`.
On this Windows machine, build the appropriate executable, then run
`scripts/challenge_collection_sdl.ps1` or `scripts/challenge_collection_amiga.ps1`.
These run the existing target-specific generator in isolated sessions. Each
session prepares and saves the position, then closes. To play, launch the normal
game and select the finished challenge; preparation is not replayed.

`python3 scripts/verify_challenge_collection.py` uses `chl_dump.py` to check both
collections' teams, scores, minutes, human sides, rules and setup sizes against
the specs. This is file verification, not play testing. The portable progress
regression test is `python3 scripts/test_challenge_progress.py` (hosted GCC).

Verification on 2026-10-28: both targets build, all twelve bundled files pass the
spec checks, and the SDL menu JSON matches all 52 compiled menus. The user played
and completed the first SDL challenge. In native FS-UAE testing, the first Amiga
challenge reached a winning final whistle, wrote `save/CHPROG1.PRG`, and still
showed DONE / 1 OF 6 COMPLETED after closing and cold-starting the emulator.
The remaining scenarios' difficulty has not been playtested.

## Saving and loading

1. Start a match with one human side against the computer.
2. Press **C** during play. A successful save briefly displays **STATE SAVED**.
3. Return to the main menu, choose **CHALLENGES > SAVED POSITIONS**, select a `.CHL` file and press
   fire/Enter. The saved position replaces kick-off before gameplay resumes.

Files are named `CHAL01.CHL` through `CHAL99.CHL`, using the next free slot. If
all slots are occupied, the next save replaces slot 99. There is a short hold-off
between saves. Saving is disabled during substitutions, replays, halftime/fulltime
result screens, live netplay, two-human matches and computer-only matches.

**Esc** leaves a challenge through the normal match exit flow. On SDL, it should
not quit the application while the challenge is running.

SDL also accepts `SWOS.exe --challenge=SAMPLE.CHL` (or the corresponding platform
binary). The filename is resolved in the save folder. This mode skips the front
end and returns from the game when the challenge session ends.

## One bundled sample per build

Opening SAVED POSITIONS installs **SAMPLE.CHL** if that filename is absent. It never
overwrites an existing copy. Deleting that copy lets it be installed again.
User-created CHAL files remain separate and are not packaged.

| Target | Sample | Tracked runtime asset |
| --- | --- | --- |
| Amiga | Liechtenstein vs Italy, frame 8004 | `bin/data/CHALAMI.DAT` |
| SDL | Italy 1–0 Malta, around minute 20, frame 3017 | `bin/data/CHALSDL.DAT` |

Both are unchanged CHAL02 captures. The `.DAT` extension lets the Amiga read a
bundled asset without redirecting it into save/. Single-target packages exclude
the other sample. The combined portable archive contains both executables and
their assets; each executable exposes only its own sample. Original game data
is still required.

## Locations and compatibility

| Platform | Save folder |
| --- | --- |
| Amiga | `save/` in the game drawer (`bin/save/` in this checkout) |
| Windows SDL | `%USERPROFILE%\Documents\SWOS\save` |
| macOS SDL | `~/Library/Application Support/SWOS/save` |
| Linux SDL | `~/.swos/save` |

Copy a compatible `.CHL` there to make it appear in CHALLENGES. Format version,
registry layout and gameplay-rule checks reject incompatible files. **Amiga and
SDL currently need separate captures**: their sprite identity lists produce
different layout hashes. Portable encoding does not guarantee matching registries.
A future registry change can also invalidate older captures; no migration exists.

## Implementation

`src/challenge.c` writes an uncompressed 96-byte header, match setup and
`SimState_SavePortable(SIM_TIER_SIM)`. Current files total 18,752 bytes: 3,662
setup bytes and 14,994 simulation bytes, plus the header. Setup includes both
teams, six user tactics, controls, match length and rules. The simulation encoding
uses typed big-endian fields and pointer identities rather than process addresses.
Framebuffer pixels and circular scroll offsets are deliberately excluded.

| Code | Responsibility |
| --- | --- |
| `Challenge_SaveNow`, `src/challenge.c` | Save gate, encoding, slot names and notice |
| `Challenge_Arm` / `Challenge_LoadState` | Validate and load after match initialization |
| `Challenge_EnsureSample` | Install the matching sample without replacing existing saves |
| `ChallengesMenu`, `src/menus.c` | File picker, playing the position and restoring the main menu |
| `src/challenge_collection.c` / `src/challenge_catalog.c` | Shared collection UI and six stable definitions |
| `src/challenge_progress.c` / `EndOfGame` | Final-whistle completion, portable progress and save recovery |
| `src/game_loop.c` | C-key dispatch, one-shot load and repaint |
| `src/sim_state.c` | Shared registry and target-aware pointer/tile encoding |

The Amiga load fix preserves the pre-load render camera, then uses the existing
replay repaint to advance both buffers to the restored camera. Previously, the
first buffer retained kick-off scroll offsets while the second received the
camera delta. Loaded camera fractions are preserved. On exit, both menu scroll
origins are reset before the normal menu redraw. SDL's top-level Esc shortcut now
also checks that no game, highlight playback or challenge is active.

## Generating challenges

For the plain-language request template, supported choices, examples and JSON
field reference, read [How to describe a challenge](CHALLENGE_REQUESTS.md).
The request order is **teams → minute → situation → ball → score → human team → platform**.

For an **Amiga `.CHL`**, run the Windows helper against the built `bin/SWOS2`:

```powershell
scripts\challenge_gen_amiga.ps1 -Spec docs\challenge_specs\amiga_partizan_67.json
```

It boots FS-UAE in an isolated game drawer, drives a native match, and copies the
finished file to `bin/save/` (`PART67.CHL` for this example). Load it from
**CHALLENGES**, or copy it into another Amiga installation's `save/` folder.
Loading starts at the saved minute. The first-half/halftime sequence visible
during generation is only how the generator constructs the position.

**First comes the machine's automatic recording stage:** it runs the match,
prepares the requested position and saves the `.CHL`; no player input is needed.
Its emulator closes after the save. **Then comes your playing stage:** launch the
normal game and load that file from CHALLENGES. The planned generator closure is
not a crash, and the file is a playable saved position, not a video or replay.

The helper needs staged Amiga assets, FS-UAE and an A1200 Kickstart ROM; see
[LOCAL_WORKFLOW.md](LOCAL_WORKFLOW.md). `-FsUae` and `-Kickstart` override discovery,
`-Output` chooses the destination, and `-Force` permits replacing an existing file.
Without `out` it uses `AMIGAGEN.CHL`. Each run keeps diagnostics under
`build/challenge-amiga/<job>/data/CHALGEN.LOG`. Existing emulator sessions and
save files are left alone.

Amiga specs use native team names or `TEAM.nnn:ordinal` (zero-based ordinal),
instead of JSON database UUIDs. The Partizan example explicitly selects Yugoslav
league teams `TEAM.041:3` and `TEAM.041:7`. Both targets share the generation logic
and the fields below; each target writes its own compatible layout and rules.
The Amiga job trigger is JSON in `data/CHALGEN.DAT`; the helper places it only in
the isolated drawer and removes it afterwards.

`SWOS --challenge-gen=spec.json` constructs a challenge nobody had to play into
(`src/challenge_gen.c`, `scripts/chl_dump.py` decodes the result). The run is headless
and takes seconds: a real friendly between the spec's teams kicks off under the dummy
video driver, the module writes the spec into the live match state, and the position is
saved through `Challenge_SaveAs` — so a generated file is the same thing a C-key capture
is, and the engine is the authority for every byte the spec does not mention.

```json
{
  "team1": "AC MILAN", "team2": "JUVENTUS",
  "human": 1,
  "score": [2, 0],
  "scorers": [ {"side": 1, "shirt": 9,  "minute": 12},
               {"side": 1, "shirt": 10, "minute": 55} ],
  "minute": 88,
  "out": "MILANJUV.CHL"
}
```

Teams are named as the database spells them (or by uuid, which also settles a duplicated
name). The scorers ARE the score — `score` is only a cross-check — and each one is a
shirt number from that side's starting eleven plus the goal's minute, which is what the
in-match scorer list shows. No goal is simulated: the score is ordinary match state,
written the way the engine itself writes it (`GoalScored`).

The POSITION is never fabricated, because an arbitrary open-play arrangement cannot be
kept consistent by hand across the state registry. It is a dead-ball line-up the engine
builds on its own:

- **the default** is the kickoff straight after the spec's last goal — that goal is
  scored for real (the ball put over the line with the spec's scorer as the last player
  to touch it) and the engine runs its own bookkeeping, celebration and restart.
  **`"ball": 1|2`** picks who kicks off: a restart belongs to the side that conceded, so
  the position restarts after the OTHER side's latest goal (which must exist), every
  later goal being history by then;
- **`"situation": "corner"`** (with `"corner": {"for": 1, "wing": "left"}`) puts the
  ball just past the end line instead, last touched by the defenders, and the engine's
  own out-of-play code awards and places the corner. A goalless position must be a
  corner for now.

Every restart funnels into `ST_WAITING_ON_PLAYER` — line-up complete, game waiting on
the taker — and that is where the position is saved, after the clock is set to the
spec's `minute` (1–89). For a minute past 45, the generator triggers the engine's
half-time routine directly and lets it complete the break and end swap (in turbo on
SDL). An unexpected goal during this transition fails the run.

The match is a friendly, but **a level score at 90 plays extra time and then penalties
by default** — the friendly's own tie options, carried in the file's setup block —
because a challenge wants deciding; `"extra_time": false` / `"penalties": false` opt
out.

Caveats: generate with the same rules the players will use (`--dos-gameplay`, `--view`,
the no-cards setting) — `Challenge_Arm` refuses a rules mismatch, and a file is per
target like every `.CHL`. `pitch` (optional) picks the pitch condition (0–6), otherwise the
friendly's random roll decides. `out` must end in `.CHL`; without it the next free
`CHALnn.CHL` is used.

## Verification and known limitations

Both targets compile. SDL `--challenge-selftest` passes setup and simulation
round trips. Fresh Amiga runs of both original CHAL01 and bundled CHAL02 were
confirmed working, including exiting mid-match and reopening CHALLENGES. The
SDL Esc fix was also confirmed by the user.

Generated Amiga kickoff (`AMI88.CHL`) and corner (`PART67.CHL`) positions were both
confirmed working by the user. The latter is Partizan 0–3 Red Star at 67 minutes,
with the human controlling Partizan and taking the corner.

An **SDL score-panel graphics issue on challenge startup** remains reported:
the team-name/results area can show incorrect graphics resembling red cards.
Its cause has not been established or fixed.

The header checksum is calculated from live state after serialization, with
separate calls for its two halves. Interrupt-driven timers can change between
operations, so a checksum warning alone does not establish a corrupt capture.
Diagnostic instrumentation also remains enabled; its output files are not tracked.

Read [AMIGA_DEBUGGING.md](AMIGA_DEBUGGING.md) for the fixes, debugger setup and
buffer measurements. [CHALLENGE_WIP.md](CHALLENGE_WIP.md) retains the original
handover and experiments; this page is the feature guide.
