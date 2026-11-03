# Replay Format v2 — `.SRP`

Status: **implemented** (`src/replay_v2.c`, `include/replay_v2.h`, SDL builds).
A whole match is recorded to `LATEST.SRP` in the save folder and played back
through the highlights viewer at any screen width, with the sounds the match
made. Trick play (§6) is specified but not wired in the game's viewer; the recordings
player (docs/PLAYBACK.md) seeks, pauses and steps by frame.

Test: `scripts/replay_v2_test.sh --all` — every recording in the corpus, both
gameplays, recorded and played back at 336/484/672, requiring the sprites, the
pitch origin, the sub-pixel scroll and the sounds to be identical to the live
match **at the playback width**.

---

## 1. Why

The current replay formats were reverse-engineered from the Amiga (`.HIL`) and
the DOS/SWOS-2020 community build (`.RPL` + `.IDX`). They work, but they were
never designed for what we now want:

- **No recorded audio.** A replay only ever plays the *crowd* (and even that
  only when the crowd-chant state machine is enabled — see §3). The ball
  bounces, kicks, tackles, whistle and goal cheers you hear in a live match are
  produced by the **live game engine** as physics/events happen. A replay does
  **not** re-run the engine — it plays back recorded sprite positions — so none
  of those SFX exist in the stream. There is nowhere in `.HIL`/`.RPL` to put
  them.
- **Resolution-coupled camera.** The camera is recorded in the *recording's*
  screen space (336 native, or 480 for SWOS-2020 widescreen). Playing back at a
  different render width needs re-centering and, for tight following, a
  reconstructed ball-tracked camera (we implemented both in `highlight.c`, but
  it's a workaround layered on a format that bakes in a fixed view width).
- **Seek is bolted on.** `.IDX` was added in 2020 purely to enable
  fast-forward / rewind / slow-mo / speed-up. It's a side-car file, easy to lose
  or desync from its `.RPL`.
- **One opaque blob.** No versioning room, no metadata, no clean place to add
  features without breaking older readers.

---

## 2. What the legacy formats contain

(From `highlight.c` `SaveCoordinatesForHighlights` / the playback decode, and
`dos_replay_load.c` `convert_frames`.)

Per **frame**, big-endian 32-bit words:

| Word | Meaning |
|------|---------|
| `0x8000xxxx \| Y` | **frame-start marker** (bit 31 set): camera `X<<16 \| Y` |
| `team1Goals<<16 \| team2Goals` | scoreboard |
| `animPatternsState` (×2) | crowd/fan animation state |
| `idx<<20 \| x<<10 \| y` (bit 31 clear) | **sprite** — one per on-pitch object |
| `0xFFFFFFFF` | end-of-frame |

- `.HIL` — Amiga-native, multi-**scene** (each saved highlight is a scene),
  big-endian, 19000-byte scene slots.
- `.RPL` — DOS/SWOS-2020, little-endian, 3626-byte header, **one** whole-match
  scene (~1–3 MB), streamed from a dedicated buffer.
- `.IDX` — one `{u8 minute, f32 timer}` per frame. **It is not a seek index**
  despite the name: SWOS 2020 uses it to show the clock while scrubbing.

Three details cost time when the `.RPL` reader was written, and are recorded
here so the next reader does not pay again: the DOS word order is `Y<<16 | X`
and `t2<<16 | t1` (the opposite of the Amiga's), `0xFFFFFFFF` is a **moving
sentinel** written one word ahead rather than a per-frame terminator, and byte 5
of the header is the gameStyle, which is where the playback rate comes from.
Off-screen sprites are recorded, with their coordinates wrapped into 10 bits.

**There are no key/event/audio markers in the stream.** The crowd toggle
(`currentKey == 0x89` → `TurnCrowdChantsOn`) is **generated internally at scene
transitions**, not recorded — so a one-scene `.RPL` never toggles the crowd on
at all (hence we force `TurnCrowdChantsOn()` at replay start for RPLs).

---

## 3. Why legacy replays are (mostly) silent — and what's correct

- **Crowd:** `PlayCrowdChants` (random crowd samples on channel 2, gated by
  `_crowdChantsOn`) is ticked every frame during playback, but it's only
  *enabled* by the `0x89` scene-transition key. RPLs have no scene transitions,
  so the crowd stays off unless forced on.
- **Action SFX (kick/bounce/whistle/cheer):** never existed in a replay, in any
  SWOS version, because the replay doesn't re-simulate. **This is by design**,
  not a regression — and it is the main thing `.SRP` fixes, by recording an
  explicit audio-event track. A `.SRP` carries its own sound, so the viewer
  turns the random chant generator OFF for one (`ReplayV2_HasAudio`) instead of
  adding chants the match never made.

---

## 4. The file

All fields **big-endian**, like every other format in this tree. One match per
file. Extension `.SRP`; `LATEST.SRP` is the last match played.

### Header (32 bytes)

| Off | Size | Field |
|----|----|----|
| 0 | 4 | `"SWRP"` |
| 4 | 2 | version (1) |
| 6 | 2 | flags: 1 audio, 2 seek, 4 chapters |
| 8 | 2 | tick rate (50 Amiga gameplay, 70 DOS) |
| 10 | 2 | quirks — see below |
| 12 | 2 | pitch width (0x2A0) |
| 14 | 2 | pitch height (0x370) |
| 16 | 4 | frame count |
| 20 | 2 | chunk count |
| 22 | 2 | 0 |
| 24 | 4 | CRC-32 of everything after the header |
| 28 | 4 | 0 |

Then one 16-byte directory entry per chunk: fourcc, offset, stored size, raw
size. `stored == raw` means the chunk is stored; otherwise it is raw deflate
(the vendored miniz, as `state_wire.c` uses). A load that fails magic, version
or CRC is refused with a reason rather than played.

### `META` — the match (stored)

Version, screen width the recording was made on, gameplay flags (1 DOS
gameplay, 2 DOS graphics, 4 arcade, 8 the DOS view, 16 the view is recorded -- a file
without 16 predates it, and the viewer works the view out: docs/PLAYBACK.md, "A recording
is shown in the view it was played in"; 32 the XBOX gameplay style, since 2026-11-02), then (since 2026-09-30, in what was a reserved word)
the view HEIGHT the match was simulated in -- 200 DOS, 272 Amiga; 0 in older files --
which playback maps the camera from onto its own picture (docs/WIDESCREEN.md "The DOS
view"), the UTC time, the 32-character build
stamp, the final score, `pitchNumberAndType`, `gameMaxSubstitutes`, the
competition name and round, and the two teams as the `.HIL` header holds them
(1018 bytes each, the 11 int16 kit words big-endian).

Everything but the final score is taken at **kick-off**. At the end of a match
`SetPitchTypeAndNumber` has already run for whatever comes next, so a `.SRP`
written with the end-of-match value plays on a different pitch than the match
was played on — green grass for a match played on a soft one.

### `FRMS` — the frames (deflated)

Per frame, a 20-byte head followed by its sprites:

| Off | Size | Field |
|----|----|----|
| 0 | 2 | sprite count (≤ 78, `_sortedSprites`) |
| 2 | 4 | camera X, **16.16** |
| 6 | 4 | camera Y, 16.16 |
| 10 | 2 | team 1 goals |
| 12 | 2 | team 2 goals |
| 14 | 2 | `animPatternsState` (the crowd) |
| 16 | 2 | the minute on the clock |
| 18 | 2 | the animated ad's line + 1 (`_currentAnimatedAdLine`); 0 in a recording from before 2026-09-29 |

then 6 bytes per sprite: image index, world X, world Y — the sprite's position
with its graphic's centre and its z already taken out, so playback only has to
subtract the camera.

The ad behind the goal is the one sprite whose look is not its image and position: the
match cuts its 8 tiles' GRAPHICS to a 6-line window scrolled by the ad's line, so playback
re-cuts them from word 18 before drawing (`Adverts_ApplyStripLine`); an older recording
gets the first strip.

Two things this head gets right that the legacy stream does not:

- **The camera is the full 16.16 simulation camera.** A wide display's draw
  origin *scales* the camera (`Render_CameraX16`, docs/GAMEPLAY.md §25), so the
  fraction changes the origin by a whole pixel on some frames. Recording only
  the integer part put playback one pixel out at 484 wide — and lost the
  presenter's sub-pixel scroll, which reads the same fraction. On playback the
  fractions are restored **before** `MoveView` pans, so its last
  `ScrollToCurrent` draws the pitch at, and publishes the shift from, exactly
  the camera the match drew with.
- **The head is taken with the sprites, not at the start of the frame.** The
  camera can move inside a frame — the end-of-match cut does — and
  `DrawAnimatedPatterns` sets the crowd state for the frame it is about to
  draw. Taken at frame start, both are a frame stale.

Sprites are recorded **on screen or not**: a wider display shows more of them.
The screen-pinned HUD (z ≥ `HUD_LAYER_Z_MIN`) is not recorded — it belongs to
the display it was drawn for, and the viewer draws its own.

### `AUDI` — the sounds (deflated)

8 bytes per event: frame, kind, channel, id. Kind 1 is a preset
(`AudioApplyPresetToChannels`), 2 a wave (`AudioChannelQueueNextWaveIfEnabled`),
3 a channel volume (channel 0xFF = all), 4 something the PC game has a sound for
(`Audio_DosEvent`: `channel` is the `DOS_EV_*` event of `include/dos_sound.h`, `id` its
argument; written whatever sound style the match was played with, docs/DOS_SOUND.md --
a reader that does not know kind 4 skips it). The recorder is an **observer** on
`audio_sfx_filter.h`, called before the netplay filter decides what is heard, so
a match recorded under rollback keeps the sound the match made rather than the
sound this peer happened to play.

### `SEEK` — keyframes every 50 frames

`{interval, count}` then `{frame, FRMS offset, audio index}`. Written and
verified; nothing reads it yet (§6). The recordings player's seek does not need it: the
loader walks `FRMS` once and keeps every frame's offset (docs/PLAYBACK.md, "How it works").

### `CHAP` — chapters

`{count}` then `{frame, kind (1 goal, 2 half), team, value}`: every goal and the
start of each half, for jump-to-goal.

### The match itself: `SIMV` `SETP` `STAT` `INPT` `HASH`

The chunks above are what was **drawn**. These five are what was **played**:
what the match was set up from, the state it kicked off in, every frame's input,
and every frame's state hash. With them a match can be played again exactly
(`--srp-resim`, §5), and eventually be taken over from any frame and played on.

They are written for a **live netplay match** only: the setup is what the lobby
sent, the kick-off state is GGPO's first save of frame 0, and the inputs and
hashes are the frames as finally confirmed, so a rollback re-records them.
All five are written or none: a match whose inputs or hashes are not known for
every frame is written without them, and says so in the log (a reader that
finds some but not all refuses the file, rather than play half of it).

| Chunk | Stored | Content |
|---|---|---|
| `SIMV` | as is, 16 bytes | version u16 (1); the handshake's rules byte (`P2P_RULE_*`: 0x01 DOS gameplay, 0x02 arcade, 0x04 fire-hold lockout, 0x08 no bookings, 0x10 no injuries, 0x20 the DOS view, 0x40 the view is known -- `--srp-resim` plays in the recorded view; a recording without 0x40 plays in the local one and, if it parts at frame 1, says to try the other `--view`; 0x80 one clock tick a frame -- a recording without it was
played on the double clock every live match ran before 2026-09-29, and `--srp-resim` replays
it that way, `NetplayMatch_SetLegacyDoubleClock`, docs/NETPLAY.md §5); the gameplay style (`GAMEPLAY_*`: 1 DOS, 2 XBOX -- since 2026-11-02; 0, in an older file or an AMIGA match, means the rules byte's DOS bit says); the registry's layout hash u64 (`SimState_LayoutHash`); the simulation width u16 (`_sim_draw_width`); 0 |
| `SETP` | deflated | the match setup the host sent (`include/netplay_setup.h`): teams, tactics, the rules they are played by |
| `STAT` | as is (it is deflated already) | a `state_wire` packet of the SIM and VIEW tiers at the start of frame 0, in the portable encoding (`src/state_wire.c`) |
| `INPT` | deflated | 4 bytes a frame: player 1's two bytes (controls, key), then player 2's, as `MatchStep_UnpackInput` reads them |
| `HASH` | as is | 4 bytes a frame: the low 32 bits of the SIM-tier hash at the START of that frame, the state GGPO saved for it |

The layout hash and the rules byte are the same two things the netplay handshake
compares (docs/NETPLAY.md §6), for the same reason: a build whose match state is
laid out differently, or that plays by other rules, is not playing the same match,
and a re-simulation on it proves nothing. The width is there because the match is
simulated at a width (`_sim_draw_width`); it is not the width anyone watches it at.

A 600-frame match gains about 5.7 KB: `STAT` 1.9 KB, `SETP` 1.1 KB, `INPT`
0.2 KB, `HASH` 2.4 KB. `HASH` does not compress, so a full match carries about
90 KB of it — beside a ~1 MB file.

### Who played: `PLYR`

An online match's two players, as the lobby named them: version u16 (1), then player 1's
(the host's) name and player 2's, each a length byte and the bytes. Written from kick-off
(`ReplayRec_SetPlayers`, at the live match's start) by the game, and by the recorder from the
players' HELLOs (a player who did not record is named by the other's), so every copy of a
recording says who played however the match ended -- a server restart mid-match, a report
that never came. It sits right after `CHAP`, before `PRED`/`SEEN`, which
`Srp_WithPrediction` replaces at the end, so the byte-for-byte rule holds. Optional: an
offline match has none. The playback page reads it (`ReplayV2_PlayerName`) when a recording
has no card, and `scripts/recordings_index.py` builds cards from it.

### What each player SAW: `PRED` and `SEEN`

Everything above is the match as finally PLAYED. Under rollback that is not what either
player watched: when the other player's input is late a peer guesses it, draws the frame,
and plays it again when the input arrives (docs/NETPLAY_GHOSTING.md). These two chunks
keep what each player saw. They are **optional** -- a live match writes them unless
`[netplay] prediction = false` (or `SWOS_RECORD_PREDICTION=0`), nothing else does, and a
file plays, resims and seeks exactly the same with or without them.

| Chunk | Stored | Content |
|---|---|---|
| `PRED` | deflated | version u16 (1), 0 u16, frame count u32, then 2 bytes a frame: player 1's, player 2's |
| `SEEN` | deflated | version u16 (1), 0 u16, record count u32, then records sorted by frame, then player |

**A `PRED` byte** is what that player's game knew when it FIRST played the frame:

| bits | meaning |
|---|---|
| 0-3 | the **lag**: how many of the other player's inputs up to this frame were guesses (frames past GGPO's confirmed frame; 0 = drawn from real input, capped at 15) |
| 0x10 | **stalled**: GGPO held this peer at its prediction limit before it played the frame |
| 0x20 | **replayed**: a rollback played the frame again |
| `0xFF` | **unknown**: this player's half never reached the file |

GGPO-X guesses by repeating the last input it has (`input_queue.cpp`, `_prediction =
_inputs[PREVIOUS_FRAME(_head)]`), so the guess a frame was drawn on is the other player's
`INPT` at `frame - lag`, and every frame after that up to this one was played on it. That is
**measured, not assumed**: every live match checks it on every guessed frame and logs
`[pred] N frames first played on a guess ...; 0 of the guesses were not their last known
input` (2026-09-29: 0 of 5,442 over a `mobile` link). So `PRED` + `INPT` + `STAT` rebuild
exactly what each player's game simulated, frame by frame -- enough to try another
prediction rule or another input delay against real matches offline.

**A `SEEN` record** is where one player's screen showed things somewhere other than where
the match finally had them -- what `src/netplay_ghost.c` measures, per frame:

| Off | Size | Field |
|---|---|---|
| 0 | 4 | frame |
| 4 | 1 | player (1, 2) whose screen it was |
| 5 | 1 | object count n |
| 6 | 9n | per object: which (0-10 the top team's players, 11-21 the bottom team's -- `_teamSpritesTable` order -- 22 the ball), then drawn x, drawn y, final x, final y as int16 world pixels. A player's position is where he stands; the ball's y is where it is seen (less its height) |

Only objects drawn at least a pixel off get an entry, only frames with one get a record. A
90-second match over a mobile link: `PRED` 2.8 KB, `SEEN` 15 KB, in a 290 KB file.

**Where both halves come from.** Each game knows only its own. It writes its half into its
`LATEST.SRP`, streams it to the recorder (`RECORD_REPLAY_PRED`, include/record_proto.h) and,
back in the lobby, sends it to the other player over the link (channel 4,
include/match_prediction.h), which rewrites its files with both (`Srp_WithPrediction`,
every other chunk kept byte for byte). The recorder builds its copy from both streams with
the same code, so the host's `LATEST.SRP` is still the recorder's file byte for byte
(`RECORD=1 scripts/relay_test.sh`). A half that never arrives -- the other player left, a
stream was cut -- stays `0xFF` / absent; the file is otherwise whole.

The playback page draws it (docs/PLAYBACK.md, "What each player saw").

### What each player's game READ: `READ`

`INPT` is the input every frame was PLAYED with. `READ` is what each player's own game
read from its controls on each frame, and when -- so the two can be compared (ReportBot
0025: "it feels like the input delay changes"). Optional, written with `PRED` and `SEEN`
and carried the same way: each game keeps its own half, streams it to the recorder
(`RECORD_REPLAY_READ`) and swaps it in the lobby; an older recorder skips the message and
its copy has no `READ`.

| Chunk | Stored | Content |
|---|---|---|
| `READ` | deflated | version u16 (1), the input delay u8, flags u8, frame count u32, then 6 bytes a frame: player 1's three, player 2's three |

The header's flags say who had the **prediction balance** on (docs/NETPLAY_BALANCE.md):
`0x01` player 1, `0x02` player 2. It works only when both did.

A player's three bytes for frame F: the **controls byte as read** when GGPO accepted the
input for F (the byte `INPT` holds, `MatchStep_PackInput`'s first); the **milliseconds
since that player's read for frame F-1** (255 = that or more; 0 on the first); and which
**wait** that game was taking as it played the frame -- `0x01` a prediction balance wait,
`0x02` a time sync wait (each stretches a run of frames a little; none holds one). The
same byte says what the **fire button** did between this read and the one before
(2026-10-13; `src/input_sdl.c`): `0x04` a fire key or button went DOWN, `0x08` it was up
again by the time of the read and the press latch (`[input] latch_presses`) read it as
pressed. The match reads fire as a level once a frame, so without these a tap shorter than
a frame looks exactly like no press: `0x04` without FIRE in the controls byte is a press
that reached the game and was lost (latch off); no `0x04` across a stretch a player says
they were pressing in means the presses never reached the game. Older files have neither
bit. All three `0xFF`: that half never reached the file.

GGPO applies an input `delay` frames after it is read, so `INPT[F + delay]` is `READ[F]`
for every frame, if the delay held; the second byte shows how evenly that game's frames
were paced, which is the other thing "the delay changes" can mean, and the third why a
frame was stretched. `scripts/srp_inputs.py <file.SRP>` checks the first and summarises
the rest (`--frames`, `--csv` for the detail); the playback page marks the waits on its
timeline and says whether the balance was on (docs/PLAYBACK.md). It is gathered in memory
-- three bytes a frame, nothing written or logged during play -- by `Ghost_Read` (`src/netplay_ghost.c`), called where `src/netplay_match.c` hands
the input to GGPO. A frame GGPO would not take input for (the prediction limit) is read
again on the next pass, and its time is the whole wait.

### `quirks`

**Which game this was recorded against.** We deliberately depart from the
original in places (the first is the fire-hold lockout — an original-Amiga bug
where an opponent's tackle wipes your held shot charge; `_gOriginalFireHoldLockout`
in `team_control.c`, docs/GAMEPLAY.md §6.3). A recording made under the fix and
one made under the original are recordings of *different games*, and only the
file can say which. One bit per deliberate departure, room to grow.

**Owner's call, 2026-09-13: new recordings are made with the fixes ACTIVE.** So
the bit is normally 0 and set only when something was deliberately recorded
against original behaviour.

This is the same shape as `tickRate`, and for the same reason. **Decide it per
FILE, never per format.** That shortcut has already been paid for once here: the
authored playback rate turned out to live in a per-file byte (SWOS-2020's
`gameStyle` at offset 5) and the first implementation hardcoded it per format,
which broke every reel that disagreed with its neighbours.

---

## 5. How it is wired

**Recording** taps `GameLoop_Frame` (`src/game_loop.c`): `ReplayRec_MatchStart`
before the first frame, `ReplayRec_FrameBegin` after the camera has moved,
`ReplayRec_CaptureSprites` after `DrawSprites`, `ReplayRec_FrameEnd`, and
`ReplayRec_MatchEnd` both where a match ends and where it is quit. Training
games, the arcade cabinet and the replay rig are not recorded unless a test asks
with `--record-replay=<file>`.

Netplay re-simulates frames, so `ReplayRec_SetFrame` (from `netplay_match.c`)
keys frames by GGPO frame number: a rewind truncates the frames, sounds and
chapters from that frame on, and they are recorded again as finally played. A
step that played no frame leaves a gap, which is filled with a copy of the last
frame so a frame's number stays its index.

**Writing** the file is `src/srp_writer.c` (`Srp_Build`, `include/srp_writer.h`,
which also holds the layout's constants): the parts in, the whole file out, no game
state. It is shared with the matchmaking server's recorder
(`third_party/matchmaker/Server/Recorder.cpp`), which is streamed a live match's
frames as they are confirmed (`ReplayRec_Content`, `src/match_record.c`) and writes
the same file — byte for byte the host's own `LATEST.SRP`, which
`RECORD=1 scripts/relay_test.sh` checks (docs/NETPLAY.md §7, "The recorder").

**Playback** hooks the highlights viewer that already plays `.HIL` and `.RPL`.
`ReplayV2_Load` fills the HIL header the viewer reads (teams, score, pitch,
subs) so the stadium line-up and the score overlay work unchanged; the match is
one scene, and `DrawReplayFrame` draws a `.SRP` frame instead of decoding the
legacy stream. The camera is the recording's own, so `MoveView`'s re-fitting
clamps — which exist to squeeze a foreign 336/200 recording into our view — are
skipped for a `.SRP`.

`--record-replay=<file>` and `--view-replay=<file>` drive both halves headless
for tests; `SWOS_REPLAY_TRACE=<file>` writes what was drawn (and
`SWOS_REPLAY_TRACE_SPRITES=1` every sprite) on both sides, which is what
`scripts/replay_v2_test.sh` compares.

`[replays] auto_save` in `SWOS.ini` additionally keeps a dated `MMDDHHMM.SRP`
per match. `LATEST.SRP` is always written.

**Re-simulation** — `--srp-resim=<file.SRP>` (`src/srp_resim.c`) plays a live
match's recording again, headless, from its own setup, kick-off state and inputs,
through the same netplay step the match was played with (`netplay_match.c`, the
`resim` mode), and checks every frame's state against `HASH`:

```
bin/SWOS.x86_64 --srp-resim=LATEST.SRP
[resim] 600 frames, every one as recorded                               exit 0
[resim] parts at frame 251: this build 498AFE21, the recording 752D25C4  exit 2
```

It takes the gameplay style and the fire-hold lockout from the file, whatever
`SWOS.ini` says -- the view too, when `SIMV` says it (0x40), and the clock (without 0x80 it
plays the old two ticks a frame) -- and refuses (exit 1) a file recorded by a build with a different
layout or rules. On the build and rules it was recorded with, every frame must
agree; the first that does not is where THIS machine plays the match differently
from the one that recorded it. So a desync between two machines no longer has to
be caught live: the recorder keeps a copy of every match streamed to it, and any
machine can play that copy again and name the frame. A match that went out of
step on purpose (`SWOS_LIVE_DESYNC_FRAME=N` on one peer) parts at frame N+1 when
that peer's own file is played again — and matches the other peer's hash there,
which says which of the two was the odd one out.

It also catches a file that was changed after the match: one input changed
anywhere (a fire press, a direction) parts within a frame or two, one byte of
`STAT` fails the packet's own check, and a changed hash claim parts at its frame.
What it cannot catch alone is a file made up from scratch by a build that plays
the same rules — that file is self-consistent. The copy that counts is the
recorder's, which neither player wrote (docs/NETPLAY.md §7).

`RECORD=1 scripts/relay_test.sh` plays every copy of the last match again — the
recorder's, the host's and the joiner's — and with `DESYNC_FRAME=N` requires the
joiner's to part at exactly N+1.

---

## 6. Trick play (replaces `.IDX`) — not wired yet

(In the game's own viewer. The browser recordings player, docs/PLAYBACK.md, already seeks to
any frame, pauses and steps a frame at a time, from the frame offsets it keeps on load.)

- **FF / RW:** jump to nearest keyframe, replay deltas; audio either muted or
  time-compressed.
- **Slow-mo / speed-up:** scale the tick advance; the audio track is resampled
  or event-rate-scaled (design choice per SFX class — crowd loops stretch,
  one-shots fire at their real pitch).
- **Frame-step / jump-to-goal:** the `SEEK` and `CHAP` chunks, both already
  written.

---

## 7. Migration / compatibility

- The loader sniffs the magic: `SWRP` → `.SRP`; legacy 3626-byte LE header →
  `.RPL`; Amiga `.HIL` → as today. `.IDX` is still read alongside a legacy
  `.RPL`. **The legacy readers stay** — the arcade attract reels are `.RPL`.
- **Legacy files have no `quirks` field, so any up-conversion must set it to
  "original", not 0.** Every `.HIL` / `.RPL` / `.RGD` that exists today predates
  the deliberate departures, and a missing field is not the same as a field
  saying "fixed". Same rule anywhere else the flag is read: absent ⇒ original.
  (`--replay-test` hardcodes exactly that for the `.RGD` corpus — see the
  `opts.fire_hold_lockout` line in `main.c`.)
- Re-saving a legacy file as `.SRP` is not implemented (positions only; the
  audio track would be empty, since the source never had one).

---

## 8. Open questions

- Crowd: keep the chant state-machine as the ambient bed for legacy replays, or
  record an explicit crowd-intensity envelope? (A `.SRP` already carries the
  real thing.)
- Audio under slow-mo: pitch-shift vs event-rescale per SFX class.
- Size: ~1 MB for a full match (~22000 frames) after deflate. Delta-compressing
  sprite positions would cut it further — most move a few px per frame.
- Bench and substitute sprites are not recorded (nor were they in `.HIL`), so a
  replay shows an empty bench.
- ~~Do we record inputs too (enabling true re-simulation)?~~ Done for live
  matches, **beside** the positions rather than instead of them (§4, "The match
  itself"): the drawn frames still play on any build, and the re-simulation is
  refused by one whose layout or rules differ. Next: **play on from a frame** —
  load the state a re-simulation reached and hand the pads to players.
