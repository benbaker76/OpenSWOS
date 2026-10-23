# Saved positions and challenges

Implemented for Amiga and SDL. A challenge is a saved position from a match
against the computer. Load it to play on with the same teams, score, clock,
players, ball, bookings and substitutions remaining.

## Saving and loading

1. Start a match with one human side against the computer.
2. Press **C** during play. A successful save briefly displays **STATE SAVED**.
3. Return to the main menu, choose **CHALLENGES**, select a `.CHL` file and press
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

Opening CHALLENGES installs **SAMPLE.CHL** if that filename is absent. It never
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
| `src/game_loop.c` | C-key dispatch, one-shot load and repaint |
| `src/sim_state.c` | Shared registry and target-aware pointer/tile encoding |

The Amiga load fix preserves the pre-load render camera, then uses the existing
replay repaint to advance both buffers to the restored camera. Previously, the
first buffer retained kick-off scroll offsets while the second received the
camera delta. Loaded camera fractions are preserved. On exit, both menu scroll
origins are reset before the normal menu redraw. SDL's top-level Esc shortcut now
also checks that no game, highlight playback or challenge is active.

## Verification and known limitations

Both targets compile. SDL `--challenge-selftest` passes setup and simulation
round trips. Fresh Amiga runs of both original CHAL01 and bundled CHAL02 were
confirmed working, including exiting mid-match and reopening CHALLENGES. The
SDL Esc fix was also confirmed by the user.

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
