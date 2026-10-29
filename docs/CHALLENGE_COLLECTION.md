# Challenge collection

The challenge collection provides six predefined matches on **SDL and Amiga**,
ordered by difficulty, 1 to 5 stars. The goal is to complete all six. Completed
challenges show a tick, and the collection displays the total completed. The stars
and the tick are the squad screen's own sprites (value stars 113-122, small tick 208).
Under the list, the highlighted challenge shows two story lines (`story` in
`src/challenge_catalog.c`, placeholders for now) and a yellow summary built from its
score, minute and restart.

## Playing

1. Open **CHALLENGES > CHALLENGE COLLECTION**.
2. Select a challenge to see the teams, starting score and minute, controlled
   team, restart situation and objective.
3. Choose **PLAY**. After the lineup screen, play resumes at the saved position.
4. **Win at full time**. Draws fail; there is no extra time or penalty shootout.
   Leaving early does not count, even when leading.
5. On the existing **REPLAY / EXIT** screen, choose **EXIT** to see the challenge
   result. It offers **RETRY**, **CHALLENGE LIST**, and **NEXT CHALLENGE** after
   success when another challenge follows.

**REPLAY reloads the original challenge**, including its score, minute, players
and ball. It starts a fresh completion attempt rather than a normal match from
minute zero. RETRY on the challenge result screen also restarts the challenge.
Existing completion is permanent: losing a later attempt never removes it.

All six challenges are unlocked from the start. There are no medals or attempt
counters. Completing the collection displays **ALL CHALLENGES COMPLETED**.

| Stars | Challenge | You control | Opponent | Starting position |
| --- | --- | --- | --- | --- |
| 1 | Finish the Job | AC Milan | Juventus | Lead 2-0 at 75; Juventus kickoff |
| 2 | Hold Your Nerve | Italy | Malta | Lead 1-0 at 80; Malta corner |
| 3 | Find the Winner | Liverpool | Everton | 0-0 at 75; Liverpool corner |
| 3 | Turn It Around | Barcelona | Real Madrid | Trail 0-1 at 65; Barcelona kickoff |
| 4 | Against the Clock | Juventus | AC Milan | Trail 0-2 at 80; Juventus kickoff |
| 5 | Derby Miracle | Partizan | Red Star | Trail 0-3 at 67; Partizan corner |

**CHALLENGES > SAVED POSITIONS** retains the personal `.CHL` file picker and the
bundled sample. Pressing **C** during an eligible match saves a position there.
Personal saved positions do not award collection completion.

## Saving and restarting

Successful completion saves automatically at the final whistle. Progress loads
automatically on entering CHALLENGES in a new game session; no manual load or
emulator snapshot is needed.

| Platform | Progress folder |
| --- | --- |
| Amiga / FS-UAE | `save/` inside the game drawer; `bin/save/` in this checkout |
| Windows SDL | `%USERPROFILE%/Documents/SWOS/save/` |
| macOS SDL | `~/Library/Application Support/SWOS/save/` |
| Linux SDL | `~/.swos/save/` |

Keep **CHPROG1.PRG** and **CHPROG2.PRG** when copying a game's saves. FS-UAE must
mount the same persistent, writable game drawer after restarting. SDL and Amiga
use separate save locations here; progress is not automatically synchronized.

The game alternates between these files, checksums them and reads a new write
back before reporting success. Loading picks the newest valid record, allowing
recovery from an interrupted write using the previous valid file. A failed save
keeps completion in memory and shows **PROGRESS NOT SAVED** with **RETRY SAVE**.
Retry successfully before closing the game to retain that new completion.

## Assets and regeneration

The starting positions are bundled separately from personal saves and progress:

- SDL: `bin/data/CCS01.DAT` through `CCS06.DAT`.
- Amiga: `bin/data/CCA01.DAT` through `CCA06.DAT`.
- Reproducible requests: [challenge_specs/collection/](challenge_specs/collection/).

These `.DAT` assets contain native `.CHL` payloads. SDL and Amiga require separate
captures because their simulation registry layouts differ. Target-specific
packages include only their own collection. Collection attempts use standard
Amiga gameplay/view and normal cards rules; SDL preferences are restored on exit.

After building the target on this Windows machine:

```powershell
scripts/challenge_collection_sdl.ps1
scripts/challenge_collection_amiga.ps1
```

The wrappers use the existing generators in isolated sessions, leaving player
games and saves untouched. The machine prepares and saves each position, then
closes its generation session. Subsequently load the finished collection in the
normal game to play directly at the requested minute.

See [LOCAL_WORKFLOW.md](LOCAL_WORKFLOW.md) for build requirements and
[CHALLENGE.md](CHALLENGE.md) for the underlying saved-position format.

## Implementation

| File | Responsibility |
| --- | --- |
| `src/challenge_catalog.c` | Stable IDs, display descriptions, target-specific assets |
| `src/challenge_collection.c` | Hub, list (tick + stars), details, results and retry actions |
| `bin/data_json/menus/challengeMenu.json` | Packed menu definition, also baked into C for Amiga |
| `src/challenge_progress.c` | Attempt lifecycle, completion and durable progress |
| `src/challenge.c` | Bundled loading and re-arming the original state for Replay |
| `src/match_setup.c` | Reports the final-whistle score from `EndOfGame` |
| `src/game_loop.c` | Restarts challenge attempts without recursing into another match |
| `src/resload.c` | Routes Amiga `.PRG` files into `save/` |

An attempt becomes eligible only after the saved simulation state loads. The
final-whistle callback compares the score using the human's team number, awards
completion once, and saves it. Replay retains the challenge identity while
resetting the result and re-arming state loading. Leaving the challenge clears
the attempt identity, so later ordinary matches cannot award collection credit.

Progress version 1 is a 40-byte big-endian record: `CHPR` magic, a 16-bit version,
16-bit capacity (six), 32-bit sequence, six 32-bit completed challenge IDs, and
a 32-bit FNV-1a checksum over the preceding bytes. Zero denotes an unused ID
slot. IDs must not be reused for changed starting positions/objectives; changing
the record capacity requires a format/migration decision.

## Verification

Run from Cygwin or another hosted Python/GCC environment:

```sh
python3 scripts/verify_challenge_collection.py
python3 scripts/test_challenge_progress.py
```

The first uses `chl_dump.py` to verify all twelve assets against their retained
specs: teams, score, minute, human side, controls, rules and payload size. This
checks files, not difficulty or gameplay balance.

The second runs the production progress code in disposable folders and fresh
processes. It covers failure followed by a successful Replay, draws, losses,
abandoning, both human sides, permanent completion, cold-start loading, corrupt
save recovery, failed-write retry and completing all six challenges.

Verified during implementation: SDL and Amiga builds, 52/52 menu JSON/compiled
matches, all twelve assets, native Amiga completion saving and cold-start loading,
and returning from an abandoned attempt. The user confirmed SDL/Amiga play and
subsequently confirmed the Replay correction. Difficulty across all six scenarios
has not been systematically playtested.
