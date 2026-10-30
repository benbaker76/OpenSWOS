# NETPLAY.md — rollback netplay (GGPO-X)

**Status (2026-09-17): two players can play each other, and play again.** MULTIPLAYER
finds an opponent through a matchmaking server (§7), the two connect directly, pick their
teams and the rules in the lobby (§6), and play a live match over GGPO with rollback; when
it ends, or is cut short, both are back in the same lobby with their choices kept (§6,
"Rematch"). Recorded matches played through GGPO's synctest and as two peers come out
identical to plain runs at every frame, sound included (§5). Testers have played it across
real machines since the first multiplayer weekend (2026-09-26/27, docs/NETPLAY_REPORT.md,
docs/NETPLAY_GHOSTING.md), natively and in the browser (§8).

OpenSWOS continues SWOS 2020, whose online play runs on
[GGPO-X](https://github.com/thomashenry79/ggpo-x). It is a submodule of the owner's fork,
on upstream `master` plus our patches (`third_party/ggpo-x/OPENSWOS.md`); SWOS 2020 pins
an older commit, and its netplay does not interoperate with ours. The lobby, login and
matchmaking are SWOS 2020 menus, imported separately (docs/MENUS.md §10).

## 1. What is where

| Path | Role |
|---|---|
| `third_party/ggpo-x/` | the library (C++), upstream layout; `OPENSWOS.md` = provenance + patch list |
| `include/netplay.h`, `src/netplay.c` | OpenSWOS's netplay module — the only code that talks to GGPO |
| `include/netplay_match.h`, `src/netplay_match.c` | a match played through the frame step and GGPO: directly, under synctest, or as one of two peers (§5) |
| `include/match_step.h`, `src/match_step.c` | one frame of the players' input and the frame's clock, shared with the replay rig |
| `include/sim_state.h`, `src/sim_state.c` | the match state GGPO saves and loads (docs/ROLLBACK.md) |
| `scripts/netplay_test.sh` | recorded matches through the step, GGPO's synctest and two peers over loopback, compared frame by frame with plain runs |
| `include/p2p_link.h`, `src/p2p_link.c` | the UDP handshake two players make before a match (§6) |
| `include/p2p_menu.h`, `src/p2p_menu.c` | the direct-IP menu over it, JSON-only (§6, docs/MENUS.md §10); MULTIPLAYER itself opens the matchmaking flow (§7) |
| `include/netplay_lobby.h`, `src/netplay_lobby.c` | the lobby's protocol: each side, the rules, START and its answer, rematch (§6) |
| `src/online_menus.c` | the imported online menus' handlers (docs/MENUS.md §10) |
| `include/audio_sfx_filter.h` | the gate every sound effect passes, so a rollback does not repeat them (§5) |
| `scripts/netplay_lobby_test.sh` | two players through the lobby MENU, several matches in a row (§6) |

## 2. Building

`NETPLAY=1` compiles `third_party/ggpo-x` with `$(CXX)`, defines `SWOS_NETPLAY` for
the C code, and links with `$(CXX)`.

| Target | Default | Notes |
|---|---|---|
| Linux x86_64 (`make X86_64=1`) | **on** | built and self-tested |
| Cygwin64 (`bin/SWOS.exe`) | **on** | built; `cygcheck` shows the same DLLs as before |
| macOS universal (`make macos-app`) | **on** | both slices; self-tested on arm64 (2026-09-17) |
| Linux x86 (`make`, -m32) | off | needs g++-multilib (no 32-bit libstdc++ installed here) |
| ARM Linux | off | `NETPLAY=1` should work — the POSIX platform layer is shared — not yet tried |
| WASM | **on** (since 2026-09-24) | a browser has no UDP sockets: each datagram goes as a WebSocket message to the server's gate, and every browser match is relayed (§8) |
| Switch | off | `Makefile.switch` does not build it; sockets there are `nn::socket` |
| Amiga | — | `netplay.c` is in `SDL_ONLY_SRCS` |

The C++ runtime is linked **statically** (`-static-libstdc++ -static-libgcc`, not on
macOS), so no DLL or `.so` is added: the Windows drop's four-DLL list and the Linux
`lib/<arch>` bundle in CLAUDE.md are unchanged. Check with `cygcheck bin/SWOS.exe` and
`ldd bin/SWOS.x86_64` after touching the link.

Without `SWOS_NETPLAY`, `netplay.c` compiles to stubs: `Netplay_Available()` is 0 and
every call returns `NETPLAY_ERR_UNAVAILABLE`. Toggling `NETPLAY` rebuilds every object by
itself (the makefile's flags stamp, CLAUDE.md); it no longer needs a `clean`.

## 3. The API (`include/netplay.h`)

A `NetplaySession` wraps one GGPO session. The game provides `NetplayCallbacks` — save
state, load state, free state, advance one frame, events — and each frame calls:

```c
Netplay_Idle(s);
if (Netplay_AddLocalInput(s, localPlayer, &input) == NETPLAY_OK &&
    Netplay_SyncInput(s, inputs, &disconnectFlags) == NETPLAY_OK) {
    /* step the game with inputs[0], inputs[1] */
    Netplay_AdvanceFrame(s, checksum);
}
```

On a rollback GGPO calls `loadState`, then `advanceFrame` once per frame to re-simulate;
`advanceFrame` does the same SyncInput / step / AdvanceFrame triple. `checksum` feeds
GGPO-X's desync detection (a `NETPLAY_EVENT_DESYNC` names the frame).

`Netplay_StartP2P` takes local port, local player (1/2), remote IPv4 and port, input
size, frame delay, max prediction (≤ 8) and the disconnect timers.
`Netplay_StartSyncTest` runs both players locally and re-simulates every frame to
check that save/load/advance are deterministic — the tool to point at the real game
first.

### Self-test: `bin/SWOS.x86_64 --netplay-selftest`

Drives the library for real with a toy deterministic game (its state is a frame counter
and a hash of every input consumed):

1. **Two peers over loopback** (two UDP ports the system hands out, one process), 600 frames. Each peer's
   input changes every few frames, so the other's "repeat last input" prediction keeps
   failing. Passes when both reach frame 600 with identical state at **every** frame,
   rollbacks actually happened, and there were no desync or disconnect events.
   A typical run: ~1400 ticks, ~200 rollbacks covering ~1400 re-simulated frames.
2. **Synctest**, 600 frames, check distance 1: 1201 saves, 600 re-simulations.
   A checksum mismatch aborts inside GGPO.

Don't run it from the Cygwin build on a desktop you are not watching: binding a UDP
port makes Windows Firewall ask.

**It used to hang on Windows, for two reasons that compounded** (2026-09-16):

- **The ports.** It bound fixed 47901/47902, and on a machine running WSL2 with
  `networkingMode=mirrored` Windows reserves WSL's Linux ephemeral range
  (`/proc/sys/net/ipv4/ip_local_port_range`, 44620-48715 there) — so the bind failed
  with EACCES, `netsh ... excludedportrange` does not list it, and GGPO **started the
  session anyway**, with no socket, and waited for a peer it could never hear. GGPO-X
  now fails such a session with `GGPO_ERRORCODE_NETWORK_ERROR`, and the self-test asks
  the system for its ports. The live test and `netplay_test.sh` moved below 32768, out
  of every ephemeral range. The link self-test's "nobody listening" check had the same
  fault intermittently: its port was derived from the host's ephemeral one, and a joiner
  binds the port it dials, so some runs landed in one of Windows' reserved 100-port
  blocks. It asks the system too now. The direct-IP menu's default was SWOS 2020's
  47885, in that WSL block as well; it is 27885 now, below every ephemeral range, and a
  bind the system refuses outright (EACCES) says THE SYSTEM RESERVES THAT UDP PORT
  rather than IN USE, which sent a player looking for another program.
- **The sleep.** Every `SDL_Delay(1)` slept 15.6 ms, so a 30000-tick loop took ~8
  minutes to give up. See "Sleep granularity on Windows" below.

### Sleep granularity on Windows

Not a netplay problem, but netplay found it, and it reaches the whole game. Windows
rounds a sleep up to its scheduler tick, 15.6 ms unless a process asks for less.
`platform_sdl.c` asked through `SDL_HINT_TIMER_RESOLUTION`, and on the Cygwin build that
does nothing: Cygwin ships SDL2 as sdl2-compat over SDL3, SDL3 dropped the hint, and
SDL3 built for Cygwin sleeps through Cygwin's `nanosleep`. Measured with the hint set and
SDL initialised:

| | `SDL_Delay(1)` | `SDL_Delay(18)` |
|---|---|---|
| default tick | 13.8–16.6 ms | 30.1–33.0 ms |
| after `timeBeginPeriod(1)` | 0.9–2.5 ms | 18.0–19.3 ms |

18 ms is what the vsync-off frame limiter sleeps at 50 Hz, so every frame overshot by
11 ms and the next caught up. That fits the uneven vsync-off pacing a tester reported on
Windows (2026-09-08), though they have not yet confirmed a build with the fix.
`main()` now calls `timeBeginPeriod(1)` itself before anything sleeps
(`src/platform_timer.c`), and `SWOS.log` reports the measured cost of a 1 ms sleep as
`[render] SDL_Delay(1) sleeps up to N ms` — anything near 15 means the request did not
take.

## 4. Integrating with a match — the design

### What SWOS 2020 does (`swos-2020/src/mpgame.cpp`)

- **State:** `save_game_state` is one `memcpy` of the whole game data segment,
  `data_begin`..`data_end` in its converted asm, plus a copy of the sprite save list
  (`pushSavedSprites`). `load_game_state` copies it back.
- **Input:** `GGPO_Input { byte control; }` per player — the joypad control word.
- **Session:** `ggpo_start_session(..., "swos2020_mp_game", 2, sizeof(INPUT_TYPE),
  localport, max_rollback)`, max rollback `min(8, frameDelay + 3)`, default frame
  delay 2, ports 47885/47886, disconnect timeout 5000 ms / notify 1000 ms; the remote
  address comes from the matchmaking server.
- **Frame:** `ggpo_advance_frame(ggpo, checksum)` after every simulated frame.

### Why that does not carry over as is

SWOS 2020's data segment is one contiguous block, like the Amiga BSS. Ours is not:
every global is its own object at a linker-chosen address (CLAUDE.md, "Adjacent-globals
BSS pattern"), interleaved with state that must **not** roll back — SDL, audio, menus,
the renderer's record of what is on screen, the netplay session itself.

### Decided: the match state registry (`include/sim_state.h`, docs/ROLLBACK.md)

An explicit table of named variables, chosen and proved by the rollback rig rather than
by a linker section. It keeps the snapshot small and names every byte a resync sends.

| call | use |
|---|---|
| `SimState_Save/Load(buf, SIM_TIER_ALL)` | GGPO `save_game_state` / `load_game_state`: raw copies, this process only |
| `SimState_Hash(SIM_TIER_SIM)` | the checksum GGPO compares between peers: computed over the portable encoding, so it is equal on every platform for the same match |
| `StateWire_Encode/Decode` (`include/state_wire.h`) | a resync or a mid-match join: SIM (or SIM+VIEW) in the portable encoding, as a validated, deflated delta against a base both peers hold — about 1.3–1.9 KB against the kick-off state, a few hundred bytes against a recent agreed frame |
| `SimState_Size(tiers)` | buffer size |

What crosses the network during a match is only the inputs (two bytes per player per
frame, §5) and the SIM checksum; GGPO's save and load stay on the machine. State is sent
only to resync, in the binary packet described in docs/ROLLBACK.md, "The wire format". Its
state is pointer-free and big-endian — pointers travel as identities, like saves — so a
32-bit, a 64-bit and a Windows build exchange it (`scripts/rollback_xbuild.sh`).

Two tiers. `SIM_TIER_SIM` (16.8 KB raw, ~2.6 KB compressed) is everything the simulation
reads and everything that decides anything it reads — closed: no SIM byte depends on a
VIEW byte, which the rig proves by restoring SIM alone. `SIM_TIER_VIEW` (19 KB) is the
highlight ring and its pointers: restore it on a local rollback so the instant replay does
not show a rolled-back goal; a resync can skip it, as long as the session does not play
in-match replays (their playback reads the ring).

Synctest is the acceptance test before two machines are tried: `Netplay_StartSyncTest`
on a real match must run without a checksum mismatch. It does (§5).

### Still to do

- **Instant replays and pause** leave the match loop. A live match keeps them out: its
  input carries no keys (R, H, P, ESC, S), and the goal replay does not start in one
  (`game_timers.c`, `NetplayMatch_IsLive`) -- before that, the first goal of a live match
  with replays on in OPTIONS ended it. Driving them from both peers' inputs is not built.
- **Determinism:** the simulation must not read wall-clock state. The rollback rig's
  determinism check runs two processes loaded at different addresses and requires
  identical per-frame hashes.

## 5. Playing a match through GGPO (`src/netplay_match.c`)

GGPO re-simulates from inside its own calls — `Netplay_AdvanceFrame` loads an older state
and calls `advanceFrame` for every frame since — so a netplay match cannot be a loop with
the frame boundary at its top. Each frame is a **step**, called once as the frame is first
played and again for every re-simulation of it:

| part | what | where |
|---|---|---|
| input | both players' `MatchPlayerInput` into the joystick registers, the fire bits, the bench calls and the key buffer | `MatchStep_ApplyInput` (`src/match_step.c`) |
| the frame | the body of `GameLoop`'s loop, **drawing included** | `GameLoop_Frame` (`src/game_loop.c`) |
| the clock | `_currentTick`, and `_currentGameTick` behind the interrupt's own gates | `MatchStep_AdvanceClock` |
| the swap | swap the buffers and set the camera | `GameLoop_EndFrame(0)` |
| the VBlank interrupt | its per-frame work: fade ticks, audio channels, key repeat -- but not the clock, which the step already advanced (`NetplayMatch_OwnsClock`) | `_Int3TimerHandler` |

**One clock tick a frame** (fixed 2026-09-29, ReportBot 0014). Until then the step's call of
the interrupt advanced `_currentTick`/`_currentGameTick` a second time, so every live match
ran both clocks at double speed (frame 5000 was tick 10000) -- and the rules read both: the
ball ran away from the dribbler, and dozens of AI and physics decisions came at the wrong
rate. No test saw it, because the corpus runs under the replay rig's clock. A recording made
since carries `P2P_RULE_ONE_TICK` (0x80) in its rules byte; `--srp-resim` plays one without it
on the old double clock (`NetplayMatch_SetLegacyDoubleClock`), so older recordings still
replay exactly, and `scripts/relay_test.sh` fails a match whose clock does not end on its
frame.

Only the frame GGPO ends on is shown: `Platform_Present`, which is `Platform_VBlank`
without the interrupt. `GameLoop` itself is now the same parts in a loop, so the replay
rig and a plain match run exactly what a netplay step runs.

Three things decide that shape:

- **Drawing is part of the frame.** The render calls write match state: `DrawSprites`
  sets `onScreen`, which the match reads, and `DrawAnimatedPatterns` calls `Rand`. A
  re-simulation that skipped drawing would play a different match.
- **The VBlank interrupt is part of the frame.** On the Amiga it runs once after every
  frame. On SDL, `Platform_VBlank` ran it between frames, outside anything GGPO calls, and
  the first synctest failed re-simulating frame 1; with the interrupt in the step, every
  frame matches. (It steps the fade, and a match starts with a fade-in.) A netplay match
  also quiesces the timer thread that would run it too, as the rollback rig does.
- **An input's edges are match state.** The bench call and a key press act on a button's
  rising edge, so the previous frame's buttons are remembered — in `g_matchInputLatch`, a
  6-byte registry entry, so a rollback puts them back.

### The input

`MATCH_INPUT_SIZE` is 2 bytes per player per frame, the same on every platform:

| byte | bits |
|---|---|
| `controls` | up 0x01, down 0x02, left 0x04, right 0x08, fire 0x20, bench 0x40 |
| `key` | the key held this frame, as its `keycodeLut` value (0: none) |

The stick and fire bits are the recordings' own. A player's bench button calls the bench of
the team that player controls — its coach, or with none its player — which is also the
guard `MainKeysCheck` applies. A recording's bench button is per team, so the replay rig
hands each press to the player controlling that team (docs/TESTS.md §6b).

### The callbacks

| callback | does |
|---|---|
| `saveState` | `SimState_Save(SIM_TIER_ALL)`, 35,654 bytes on x86_64; its checksum is `SimState_Hash` of both tiers — over the portable encoding — folded to 32 bits |
| `loadState` | `SimState_Load(SIM_TIER_ALL)` |
| `advanceFrame` | `Netplay_SyncInput`, the step, `Netplay_AdvanceFrame` |
| a frame's checksum | `SimState_Hash(SIM_TIER_SIM)` folded to 16 bits: what a peer compares |

Under synctest the match also keeps every frame's state as first played. GGPO compares only
checksums, and aborts; before it does, the match prints which registry entries differ.

### Testing it

`--netplay-match=<mode>` with `--replay-test=<file.swr>` plays that recording's match through
the step, with both players' inputs from the recording:

| mode | |
|---|---|
| `direct` | the step, without GGPO |
| `synctest[:N]` | GGPO's synctest: every N frames (1–8, default 1) it loads the state of N frames back, plays them again and compares their checksums with the first run's |
| `p2p` | one of two peers, each a process with one player's input (below) |

It takes the rollback rig's watching options (`--rollback=trace`, `--rollback-trace`,
`--rollback-dump`, `--rollback-save-state`) and refuses the ones that rewind the match.
Without `--rollback` the run is the replay rig's comparison with the recording, played
through GGPO. `scripts/netplay_test.sh` plays recordings plain and in each mode and requires
the traces to be identical at every frame.

Results (2026-09-15), over the recorded corpus — 13 recordings, each with Amiga and with DOS
gameplay: 26 matches, 488,837 frames — every trace compared at every frame with a plain run
of the same build (`scripts/netplay_test.sh --all --modes "direct synctest:1 synctest:8 p2p
p2p:2" --stall 8`, 652 s at 4 jobs):

| mode | result |
|---|---|
| direct | 26 of 26 matches identical to the plain run |
| synctest:1 | 26 of 26 identical; 488,837 frames re-simulated, one load per frame |
| synctest:8 | 26 of 26 identical; 488,768 frames re-simulated from 61,096 loads |
| p2p | 26 of 26, both peers identical; 8,481 rollbacks re-simulating 35,343 frames, up to 590 in one match |
| p2p:2 | 22 of 22, both peers identical; 6,745 rollbacks re-simulating 28,416 frames. 4 skipped: zkz-11122841 and zkz-12065730 have input in the 2 frames the delay drops |

Every re-simulated frame's checksum matched. Compared with the recordings instead, a match
played through synctest gets the same result as the replay rig on its own: zkz-12065730 and
playa-10090613 pass every frame, and zkz-11215003 diverges at the same frame (12,646) on the
same field. A synctest:1 match takes 1.2 to 1.5 times as long as a plain one.

### Two peers (`--netplay-match=p2p`)

Each peer is a process of its own with one player's recorded input. GGPO predicts the other
player's input — the input it last received — and when the real input arrives and differs,
loads the state from before the mispredicted frame and plays forward again.

```
bin/SWOS.x86_64 --replay-test=<file.swr> --rollback=trace --netplay-match=p2p \
    --netplay-player=1 --netplay-port=7001 --netplay-peer=127.0.0.1:7002 --netplay-trace=p1.trace
```

What makes a peer's run comparable with a plain one:

- **Its trace is the match as finally played.** A peer's state at a frame boundary may still
  rest on a prediction, so it is not hashed there. Every save GGPO asks for records that
  frame's hashes instead, and a later save of the same frame overwrites them: once every
  frame is confirmed, each entry is the frame as it stands.
- **It ends on a confirmed frame.** `ggpo_get_confirmed_frame`, added to GGPO-X
  (`third_party/ggpo-x/OPENSWOS.md`), is the last frame whose input from both players is
  known. A frame is final once every input before it is confirmed; the match ends at the
  first final frame where it is over (`_playGame == 0`), or at the recording's end. A frame
  played past the end — predicted, or re-simulated after a correction ended the match sooner
  — changes nothing.
- **It waits for the other peer before leaving**, until that peer has acknowledged every input
  (GGPO's send queue is empty) and so can reach the same end — 3 seconds at most.
- **A loop waiting for input inside a frame makes one pass.** The statistics overlay
  (`KeepStatisticsOnScreen`) spun inside a single frame until a player pressed fire; a peer
  holding only a prediction of the input that closes it spun for ever, and the first two-peer
  run of zkz-12065730 hung there. While input arrives a frame at a time (`_gFrameStepInput`,
  set by the replay rig and by netplay) the loop makes one pass per frame and the overlay
  stays up into the next frame (`GAME_FRAME_HELD`). Both recordings that open it close it
  within that pass, and their plain traces are unchanged.
- **Input delay drops input.** With a delay of D frames, the first D frames play with no input
  from that player. A recording with input there cannot equal the plain run; the peer says
  so, and `scripts/netplay_test.sh` reports the match as skipped.
- **`--netplay-stall=<ms>`** makes a peer pause now and then, as a busy machine does, so the
  other plays ahead on predicted input and rolls back far more than on an idle loopback.
  Its own generator decides when — never the game's.
- GGPO-X compares the peers' checksums of confirmed frames (`frameChecksum`, the SIM hash
  folded to 16 bits); a `DESYNC` event fails the run.

In most recordings one team is human, so the other player's input rarely changes: the peer
predicting it is almost always right and seldom rolls back, while the peer predicting the
human's input rolls back hundreds of times a match (in zkz-12080203 both do). Both peers'
traces are compared.

### The network between them (`src/netplay_netem.c`)

Over loopback nothing goes wrong, so nothing is proved: every prediction is correct, and
the first two-peer run measured 1200 frames with **zero** rollbacks. The emulator puts the
misbehaviour inside the process, on the SEND side, through GGPO's generic transport filter
(`Udp::SetTransportFilter`, `third_party/ggpo-x/OPENSWOS.md`). Send-side because a
one-way delay applied by both peers is the same thing as delay on the path between them,
and because it lets each peer be given a link of its own — which is how one player on a
bad line is reproduced. Its randomness is its own and never the game's: `Rand()` is match
state both peers must agree on, and a harness that perturbed it would desync the match it
is testing.

The rules and the profile table live in `include/netem_core.h` / `src/netem_core.c`, shared
with the matchmaking server's relay, which applies the same conditions in the middle of a
relayed match — to the lobby's traffic as well as GGPO's (§7, "The relay").

The conditions are the set clumsy (jagt.github.io/clumsy) offers, which is the list of
what a real link does to a packet:

| clumsy | here | what it does |
|---|---|---|
| lag | `SWOS_NETEM_DELAY_MS`, `_JITTER_MS` | hold every packet, by a per-packet amount |
| drop | `SWOS_NETEM_LOSS_PCT` | the packet never leaves |
| out of order | `SWOS_NETEM_REORDER_PCT`, `_REORDER_MS` | this packet is held back behind the ones sent after it |
| duplicate | `SWOS_NETEM_DUP_PCT`, `_DUP_COUNT` | the same packet arrives more than once |
| throttle | `SWOS_NETEM_THROTTLE_PCT`, `_THROTTLE_MS`, `_THROTTLE_DROP` | nothing is sent for a window, then the lot goes out at once — or is thrown away, which is the link simply being gone |
| tamper | `SWOS_NETEM_TAMPER_PCT`, `_TAMPER_BITS` | bits flipped in the packet |
| set TCP RST | — | the transport is UDP. What it models, the connection cut from under you, is `_THROTTLE_DROP` |

All of it is one queue of whole packets with a due time, and one rule — send what is due.
The queue is in send order and the pump scans it, which is what lets packets overtake each
other. `SWOS_NETEM_PROFILE` names a whole link (`lan adsl wifi mobile satellite awful`);
any knob set alongside it overrides it. A packet that needs none of this is not touched at
all, so "no delay" really is no delay rather than "one poll later".

```bash
scripts/netplay_test.sh --modes p2p --netem wifi              # the corpus, over a wifi link
scripts/netplay_test.sh --modes p2p --netem delay=80,jitter=30,loss=5,reorder=5
PROFILE=wifi scripts/netplay_live_test.sh                     # the live pair test
JOIN_PROFILE=awful scripts/netplay_live_test.sh               # one peer on a bad line
```

**The result must not change.** Repairing exactly this is what rollback is for, so both
peers must still finish identical to the plain run, however the link behaves; the seed is
each peer's port, so a failure repeats. Measured on zkz-12065730 (DOS gameplay, 1520
frames), every row identical to the plain run:

| conditions | what the link did to player 1's packets (about 1.6k of them) | rollbacks p1 / p2 |
|---|---|---|
| none | — | 6 / 8 |
| `lan` | 1568 delayed by 2 ms | 8 / 11 |
| `adsl` | 19 dropped, 10 reordered | 9 / 16 |
| `wifi` | 23 dropped, 19 reordered, 357 burst in 30 throttle windows | 8 / 12 |
| `mobile` | 37 dropped, 26 reordered, 22 duplicates, 799 burst in 58 windows | 7 / 12 |
| `satellite` | 1969 delayed by 300 ms, 18 dropped | 9 / 17 |
| `awful` | 168 dropped, 62 reordered, 38 duplicates, 919 burst in 75 windows | 7 / 11 |
| `dup=50` | 760 duplicates | 6 / 2 |
| `reorder=20,reorder_ms=40` | 283 reordered, nothing else | 6 / 3 |
| `throttle=3,throttle_ms=400,throttle_drop=1` | 565 packets lost in 35 blackouts of 400 ms — 14 s with no link at all | 6 / 7 |

Over the whole corpus (`--all --modes p2p --gameplay dos --netem wifi`): 13 matches,
248,493 frames, **every peer identical to the plain run** — 6,819 rollbacks re-simulating
33,619 frames, while the link dropped 2,585 of player 1's packets and stalled 5,615 times.

The live pair test takes the same conditions, and this is where a peer gets a link of its
own: `PROFILE=wifi JOIN_PROFILE=awful scripts/netplay_live_test.sh` (1200 frames) ends both
peers on the same state hash, having played very different matches — the wifi peer dropped
13 packets and rolled back 76 times, the awful one dropped 122 and rolled back 141,
re-simulating 765 frames.

Each run prints what the link actually **did**, not what it was asked for
(`[netplay] network emulation did: ...`), because a condition set at 2% may never have
fired and a run that tested nothing should say so.

**Tamper is a fuzz knob, not a link.** UDP carries a checksum and the hardware drops what
fails it, so a flipped bit reaching the game is rare — what tamper tests is whether a
corrupt packet is refused rather than acted on. It used to be acted on: `--netem tamper=2`
killed the session within a second of kick-off. Traced: a tampered input packet arrived with
`start_frame` 4194339 (bit 22 of frame 35) against a last-received frame of 42, GGPO took
it, and that peer processed no further packet. GGPO trusted every field of an
unauthenticated UDP packet — `start_frame`, each player's `last_frame`, the
`disconnect_requested` bit that ends a match on its own — and checked only a 16-bit magic
number, which a flipped bit elsewhere leaves intact. So one forged packet could end anyone's
match.

**Every GGPO packet is now checked before it is read** (GGPO-X fork, `openswos`):

- it must be exactly the size its header describes (`UdpMsg::SizeIsValid` — an unknown
  type, or an input's `num_bits` claiming more than the datagram holds, fails), and
- it carries **SipHash-2-4 of itself under a per-session key** (`Udp::PacketMac`, checked
  against the reference vectors). What fails is dropped as if lost; GGPO resends anything
  unacknowledged.

The key is 16 random bytes the host draws (`/dev/urandom`) and sends in the lobby's START;
both peers hand it to GGPO at kick-off (`ggpo_set_packet_key`, before the sync starts). A
party off the path cannot make a packet the session accepts. A party on the path can read
the key — the link is not encrypted — but can also simply drop packets, which no checksum
prevents. Recorded matches played as two peers have no lobby and use the all-zero key:
damaged packets are still refused, forged ones would not be.

The lobby link has the same need at smaller stakes: a flipped bit in a START would kick two
peers off from different setups. Its packets end in a 32-bit FNV-1a checksum (unkeyed —
there is no shared secret yet), except a packet of another protocol version, which is let
through so it can be refused with the reason. Link protocol 4 (`kProtocol`, `p2p_link.c`;
4 added the build to the setup, 2026-09-27).

**It found a GGPO bug on the way.** `Platform::GetCurrentTimeMS` on Linux, Cygwin and macOS
returned 0 on its first call, so the first sync request of a process was stamped 0 — and GGPO
re-sends a sync request only `if (_last_send_time && ...)`. If that one packet was lost, both
peers waited for each other forever: "never synchronized", 30 s, both sides. Loss makes it
possible, tampering made it common (about half of the runs of one recording at 10%).
Windows' `timeGetTime` is never 0 at startup, which is why upstream never saw it. The clock
now starts at 1.

`--netplay-selftest` checks that peers with DIFFERENT keys never connect (and that the same
test with the same keys does); the corpus plays every recording as two peers with 10% of
packets tampered by up to 8 bits and requires both to match the plain run.

### When the two DO go out of step

Everything above is recovery from the network: a misprediction is rolled back and the peers
stay identical. This is the other case -- the simulations themselves disagree -- and it is
worth being able to watch rather than wait for:

```
JOIN_ENV="SWOS_LIVE_DESYNC_FRAME=200" scripts/netplay_live_test.sh     # one peer, one bit
PROFILE=satellite JOIN_ENV="SWOS_LIVE_DESYNC_FRAME=200" MATCHES=2 scripts/netplay_lobby_test.sh
```

`SWOS_LIVE_DESYNC_FRAME=N` makes this peer simulate frame N differently on purpose: one bit
of `_seed`, which is a COUNTER, so the two `Rand()` streams stay a step apart for the rest of
the match. `HOST_ENV` / `JOIN_ENV` put it on ONE side (`netplay_live_test.sh` has them too
now -- the lobby test already did).

**It is applied inside the frame, and re-applied every time GGPO simulates that frame.** That
is not a detail: a change made to match state AFTER the step and before the frame is
committed is **undone by the first rollback**, because the state GGPO saved for that frame
predates it. Injected there, the peers quietly re-converged and `satellite` "detected" 0 of 5
injected desyncs -- the rig lying, not the netcode working. Anything that writes simulation
state outside the step has the same problem.

Measured (2026-09-18, 5 runs a profile, injected at frame 200, 600-frame matches):

| link | detected | noticed after |
|---|---|---|
| `lan` | 5/5 | 16-17 frames |
| `mobile` | 5/5 | 16-18 frames |
| `satellite` | 5/5 | 21-23 frames |
| `awful` | 5/5 | 16-21 frames |

**20 of 20, on every link.** The lag is GGPO's confirmation window, not the network: a
frame's checksums are only compared once both peers have confirmed it, so a slow link is
found out later, not less reliably. At 70 fps that is about a quarter of a second.

What recovery means today: **the match ends, on both peers, and they go back to the lobby**
(`liveEnd`, "THE TWO GAMES WENT OUT OF STEP"), where a rematch connects and plays normally --
verified through the real menus with `netplay_lobby_test.sh`. There is no in-match resync:
`src/state_wire.c` can encode the whole state as a packet and the rollback rig proves it
round-trips, but nothing in a live match sends one. That is the piece to build if a desync
should ever be survivable rather than fatal.

### Sound during a rollback

A frame GGPO plays again has already been played once, and the sound effects it started were
heard then. Replaying them would repeat every kick, whistle and crowd chant of the rolled-back
frames, up to eight frames late, once per rollback. Effects are now gated
(`include/audio_sfx_filter.h`, installed by `netplay_match.c`):

- Every effect a frame starts -- a preset (`AudioApplyPresetToChannels`) or a wave
  (`AudioChannelQueueNextWaveIfEnabled`), the only two ways the match starts one on SDL -- is
  written down against that frame, 32 frames back.
- A frame played again starts only what is not written down yet, counting repeats: a frame
  that started the same effect twice starts it twice. An effect that only exists once the
  corrected input is known plays then, a few frames late, rather than never.
- What mispredicted frames started and the corrected ones do not cannot be taken back. The
  effects are short.
- Volume changes pass untouched: a crowd fade played again ends on the right value.

The match reads nothing back from the mixer (the one gate that looks,
`GateKickSampleAndCrowd`'s `_audioDbgFlagA` bit 7, is never set on SDL), so the state peers
compare is the same with sound on or off.

`--netplay-sound-trace=<file>` writes every effect played -- frame, kind, channel, id, and
`late` for one a rollback started -- and `scripts/netplay_test.sh` compares them with the
`direct` run's: synctest must play exactly the same effects, each peer at least them. Over the
corpus (2026-09-17: 26 matches, direct / synctest:8 / p2p with `--stall 8`): 15,537 effects,
none missing, synctest exact; the peers played 84 extra effects from mispredicted frames and
115 late. One match over an emulated wifi link, 423 rollbacks on one peer: 8 repeats kept
silent that would have been heard.

(The statistics overlay's one pass per frame still calls `Platform_VBlank`, which presents a
frame during a re-simulation. A live match cannot open the overlay -- its input has no keys --
so only recorded matches reach it.)

### Frame pacing

Two machines never run at exactly the same rate, and GGPO does not make the faster one wait.
It plays further and further ahead of the input it has, until it reaches the prediction limit
(8 frames); from then on it stalls a frame at a time and rolls back up to 8 frames whenever
the other player's input changes, and players see the opponent jump and slide -- a cause of
the reported ghosting in its own right. `NETPLAY_EVENT_TIMESYNC` reports every 120 frames
how far ahead this peer is, and OpenSWOS ignored it.

A live match now acts on it (`netplay_match.c`): the peer that is ahead lengthens its frames
a little, never holds one. Two parts: one closes the reported lead over the next 120 frames;
the other learns the clock difference -- a standing slowdown nudged up while ahead and down
while behind, between zero and a tenth of a frame -- because a correction that only acts on a
measured lead leaves a steady lead behind. The frame is lengthened by moving the frame
limiter's deadline (`Platform_DelayNextFrame`): a sleep between frames is taken back out of
the next one and does nothing, which is how the first version was found not to work. The
simulation sees none of it.

Measured with `scripts/netplay_lobby_test.sh`, 2500 frames over a `lan` link, the joiner
running 2% slow (`SWOS_LIVE_SLOW_PCT=2`, which shows a frame without playing one every 50):

| | host re-simulated | host rollbacks | held at the prediction limit | host's lead at the end |
|---|---|---|---|---|
| no pacing (`SWOS_LIVE_NO_PACING=1`) | 1,495 frames | 228 | 42 times | 3.5 frames (GGPO's half of a 7-frame gap: at the limit) |
| closing the lead only | 1,110 | 228 | 0 | 2.5 frames, steady |
| with the learned slowdown | 231 | 115 | 0 | 0.08 frames; learned 430 µs a frame (the true difference is 400) |

`SWOS_LIVE_PACING_LOG=1` prints each time sync.

GGPO-X's time sync was itself corrected upstream after the commit SWOS 2020 pins (it
subtracted our own input delay where it meant the remote's; OpenSWOS always agrees one
delay, so that never bit), and `ggpo_start_session` now takes the frame rate -- 50 or 70 here --
instead of assuming 60. Both came with catching the submodule up to upstream `master`.

**Compared with Fightcade** (2026-09-28, `fightcade-snes9x/src/win32/ggpo/ggpo.cpp`, its
snes9x glue; the engine itself is a closed `ggponet.dll`). Fightcade reaches the same
conclusion -- "GGPO timesync is bad", so it ignores `GGPO_EVENTCODE_TIMESYNC` and never
stalls -- and paces from its own measurement instead: a 90-frame average of
`local_frames_behind - remote_frames_behind` from `ggpo_get_stats`, acted on at most once a
second with a bounded frame delay (`ProcessFrameDelay`) once it passes about two frames.

The one difference worth taking: **both sides meet in the middle.** Fightcade's delay is
signed -- the peer that is ahead runs slightly slower AND the peer that is behind slightly
faster -- where ours only ever slows the one ahead (the learned slowdown never goes below
zero, and the peer behind does nothing). Meeting in the middle closes a gap in about half
the time, and keeps the match at its nominal 50 (or 70) frames a second on average instead
of settling slightly slow, at the speed of the slower machine. It is a small change here: let
the behind peer shorten its frames by the mirror of the ahead peer's lengthening, with the
same bounds, and let the learned bias go negative. The lobby test's slow-joiner run above
measures it. Not done yet -- the pacing we have already holds a 2% clock difference to 0.08
of a frame, so it is a refinement, not a fix.

### One simulation width

Two players on a 4:3 and a 16:9 screen used to be refused by the link, because the match
itself read the render width. It no longer does: they play one match at a width both agreed.

**What read it** (found with the rollback rig, which unlike the replay rig does not pin
`Rand()`): at 336 and 484 wide, playa-10090613's random seed parted at frame 19 and the whole
match by frame 1000.

| where | what the width decided | now |
|---|---|---|
| `DrawAnimatedPatterns` | which column of crowd tiles toggles, and the widescreen mirror -- whether a slot fills, so the `Rand` calls of every frame after | the simulation draw width |
| `BuildSpriteBandAndGeneralRenderStacks` | which sprites in the margin animate (f1d348c), a list the match reads a frame later | the simulation draw width, world sprites only |
| `UpdateCornerFlagSprite` | which corner the flag stands at, and the second flag | the simulation draw width |
| `DrawBenchAndSubsMenu` | the frame the bench is populated, which writes the subs state | the simulation draw width |
| the score panel's extra tile | whether it is in the draw list, whose contents are match state | the simulation draw width |
| HUD sprites (score, clock, names, S) | their `x`, and `onScreen` measured from it | this display's; out of the checksum |

**The simulation draw width** (`_sim_draw_width`, `display.h`; `Sim_DrawCameraX()`, where a
view that wide draws from) is what those decisions read. Locally it IS the render width, so a
widescreen player keeps f1d348c's animated margins; the replay rig pins it to 336 unless
`--replay-sim-width` says otherwise, which keeps the corpus exact; and a live session takes one
both peers agreed -- the wider of the two screens (`P2PLink_AgreedSimWidth`, from the widths
each side's handshake already carried) -- and puts the display's own back when the match ends.
The narrower player's margin animates off-screen, and its crowd tiles and bench are decided
for a slightly wider view than it shows; nothing it can see changes.

**The HUD stays where each display puts it.** A HUD sprite's `x` is placed for the local
width (`WIDE_MARGIN`, `HUD_MARGIN`, a wider score box); nothing the match decides reads it. So
a new registry field kind, `SIM_FIELD_LOCAL16/32`, covers `x` and `onScreen` of those sprites
(`k_hudSprite`, `src/sim_state.c`): saved, loaded and sent like any other field, but counted as
zero in `SimState_Hash`, the checksum peers compare. Their `visible` and `y` stay in it: they
decide the draw list. (The VIEW tier -- the highlight ring, recorded in drawn coordinates --
also differs between such peers, and was never compared.)

A first version left those fields out of a sent state as well, and the resync test caught it:
not every HUD sprite is placed every frame -- the result panel moves only while it shows -- so a
loaded state kept stale positions, and the decode into a scratch buffer made them garbage. A
resync between two peers of DIFFERENT widths is refused instead (`state_wire.c` checks the
delta's base by the hash of its encoded bytes, HUD positions included); live play does not
resync.

The check: `scripts/netplay_width_test.sh` plays each recording through the rollback rig at
render widths 336, 484 and 672 with one simulation width, and requires every frame's SIM hash
to be identical; `scripts/netplay_test.sh --sim-width 484 --peer-widths "336 484"` plays them
as two peers rendering at different widths, compared on the SIM tier.

**It found a 64-bit bug on the way.** At simulation width 484 one DOS match (zkz-11215003)
crashed at frame 24269 in `UpdateReferee`. `InitRefereeAnimationTable` read the direction's
frame table at `animTable + 2 + dir * 4` -- the asm's `lsl.w #2` is the size of an Amiga
pointer, and `AnimTable` holds native pointers. On every 64-bit build an odd direction read
half of one pointer and half of the next, and an even one read the table for `dir / 2`: the
referee animated the wrong way round, and a referee arriving on a diagonal crashed the game.
The rig at 336 never happened to reach the crash; a different course of the same match did.
It reads `dir * sizeof(int16_t *)` now, which is the same m68k code.

### Next

1. ~~**Two humans on two machines**, through MULTIPLAYER, the lobby and a rematch.~~ Done:
   testers have played since 2026-09-26/27 (docs/NETPLAY_REPORT.md).
2. **Instant replays in a session**, driven by both peers.

----

## 6. The direct-IP menu and the link (`src/p2p_menu.c`, `src/p2p_link.c`)

The direct-IP menu is OpenSWOS's own, `p2p/p2pMenu` (JSON-only, docs/MENUS.md §10): one
player hosts on a UDP port, the other joins by the host's IPv4 address. No server —
SWOS 2020's matchmaking menus are a separate import (`src/online_menus.c`) and need a
matchmaking server (§7). The main menu's MULTIPLAYER button opened this menu at first; it
now opens the matchmaking flow (§7, "The menus"), and this one is reached with
`--menu=p2p/p2pMenu`.

| Row | What it is |
|---|---|
| GAME | host a game, or join one |
| HOST ADDRESS | the host's IPv4 address, typed in; the host itself needs none |
| UDP PORT | 27885 by default (§3: SWOS 2020's 47885 is inside a block Windows reserves for WSL2). The host binds exactly it, so it is the port a player forwards; a joiner takes the next free one above it, so two peers can share a machine |
| INPUT DELAY | frames of local input delay for the match to come, 0..6 |

The status panel follows the link every frame — the menu is flagged `dos_redraw`, so its
`onDraw` polls — and shows the round trip once measured, which is the number to choose an
input delay from.

**The link is the handshake, not the match.** Both sides must agree on everything the
simulation depends on before a session starts, because a disagreement is a desync on the
first frame rather than an error:

- the protocol version,
- a COMPATIBLE build: the same `NETPLAY_COMPAT` (`include/netplay_compat.h`,
  `P2P_FAIL_VERSION`, "THE OTHER PLAYER HAS ANOTHER VERSION"). From 2026-09-27 to
  2026-10-25 it had to be the same commit, which turned away most players who had not
  updated that day. The number is raised by whoever changes how a match plays, and
  `scripts/check_netplay_compat.sh` fails when the test recordings' SIM hashes change
  and it was not (record: `tests/netplay_compat.txt`). A build with uncommitted changes
  (a flag in the setup) plays only its own commit. Two different commits that DO agree
  are logged (`[p2p] the other player runs build ...: both netplay N`), so a desync
  between them is a missed raise. The matchmaking server lists a game only to players
  with the same number (`VERSION:OPENSWOS/<n>`, `PlayKey` in `Connections.cpp`), so
  nobody is shown a game the handshake would refuse,
- `SimState_LayoutHash` — the shape of the state a rollback saves, so two builds whose
  match state differs refuse each other,
- the rules: arcade mode and the original fire-hold lockout, which must match,
- the gameplay STYLE (DOS or Amiga), which does not have to: **an online game plays its
  host's style.** The host picks it when they OPEN a game (AMIGA (50FPS) or PC (70FPS));
  the player who joins takes it when the link connects (`link_connected`,
  `GameplayStyle_Borrow`) and gets their own back when they leave the lobby
  (`GameplayStyle_GiveBack`), never written to their SWOS.ini. Before 2026-09-29 a style
  mismatch refused the join and sent the player to OPTIONS.
- The VIEW goes with the style. It reads like a display setting and is not one: the
  simulation camera is placed for the screen the match is seen on (`MatchViewEnter` pins
  it to 320x200 for the DOS view), and the camera moves players -- two peers on different
  views went out of step at frame 0. So there is no separate `view` setting any more
  (`[gameplay] style` sets both); only `--view` on a command line can pin one, and a
  pinned view that differs on the same style is refused.

The screen width travels too, but does not have to match: both take the wider of the two as
the match's simulation draw width (§5, "One simulation width").

A joiner that differs is refused with the reason, and the host is told who bounced off it.

Six packets for the link itself, all UDP, each either repeated until it is answered or
harmless to lose: `HELLO`, `WELCOME` and `REFUSE` for the handshake, `PING`/`PONG` for the
round trip, `BYE` on leaving. Three more came later: `DELAY` (each side's proposed input
delay) and `BLOB`/`BLOB_ACK` (the chunks of a message and their ack), both below. Every one starts with `"SWUN"`, the protocol version and the type, so a peer
of another version is recognised and told so rather than ignored; fields are big-endian,
like the save format. The socket is non-blocking and polled from the menu's draw, and it
closes before GGPO opens its own on the same local port — which is also what keeps a NAT
mapping open for the session that follows.

```
bin/SWOS.x86_64 --netplay-selftest     # GGPO's tests, then the link over loopback
bin/SWOS.x86_64 --menu=p2p/p2pMenu     # open the menu straight away
bin/SWOS.x86_64 --menu-selftest        # it unpacks the way its JSON says
```

The self-test runs the whole protocol over loopback: two links connect and measure the
round trip, a second joiner is turned away, a peer leaving is noticed, a build that would
play a different game is refused with the reason, and a join with nobody listening gives
up instead of hanging.

### The handshake refuses a peer that would play a different match

Every rule a match is played by is one bit of the handshake's `gameplay` byte
(`P2PLink_GameplayFlags`, `P2P_RULE_*`): what the player chose — DOS gameplay and its view (followed, not refused),
arcade mode, the fire-hold lockout (P2P_FAIL_GAMEPLAY, "YOU PLAY BY DIFFERENT RULES"), a
view pinned with --view (P2P_FAIL_VIEW, "ONE OF YOU PINNED THE VIEW"). Two more bits
said what the BUILD decided: until 2026-10-11 a tester build was compiled without bookings
and injuries (`SWOS_TEST_NO_INJURIES_CARDS`). That is a setting now and never applies
online (`include/test_rules.h`), so every build sends 0 there — and an old tester build,
which sends 1, is still refused (P2P_FAIL_BUILD, "YOUR BUILDS PLAY BY DIFFERENT RULES /
ONE OF YOU HAS A TEST BUILD").

The build's bits are reported by the objects that implement the rules —
`TackleFoul_BookingsEnabled` (tackle_foul.c), `CareerStatus_InjuriesEnabled`
(career_status.c) — not by a `-D` in p2p_link.c, because the build that desynced was
MIXED: its tackle_foul.o was compiled without the flag and everything around it with, so
it booked players while claiming it could not. Asked this way, a mixed binary reports
what it will do. (The makefiles no longer produce one: a flag change rebuilds every
object.)

### The link also settles two things the match needs

**The input delay, agreed rather than chosen twice.** Each peer proposes one from the
round trip it measured (`propose_delay`), they exchange proposals, and both take the
**maximum** — the same two numbers on both sides, so the same answer — fixed for the
match. `P2PLink_AgreedDelay()`.

**The proposal is Playa's table** (2026-10-30), from the measured ping, never 0, capped:

| Delay | DOS gameplay (70 fps) | Amiga gameplay (50 fps) |
|---|---|---|
| 1 | up to 35 ms | up to 50 ms |
| 2 | 36–70 ms | 51–99 ms |
| 3 | 71–140 ms | 100–175 ms |
| 4 | 141–210 ms | over 175 ms (the cap) |
| 5 | over 210 ms (the cap) | — |

It replaced SWOS 2020's AUTO formula (33 ms off the ping, 0 on a clean link,
ceil(rtt / 3 frames), capped at 3 / 4); jitter no longer counts.

This is deliberate, and it is the bug that makes one player feel they are **chasing** the
other. SWOS 2020 derives a delay on each machine from that machine's own ping, jitter and
`1000/getMPFps()`, and applies it to its own player only; two machines do not measure the
same jitter, and if one runs slower they do not even agree on the frame time, so a match
that began "AUTO 3" can end up 1 against 5. It announces the number to the other player
(`sendFrameDelayInfo`) — as English prose, into the chat, which nothing parses. Ours takes
its frame time from the **gameplay mode**, which both sides share (the joiner borrows the
host's style as the link connects, before the delay is proposed), rather than from anything
either machine measures.

It is very probably most of the reported **ghosting** too: in a shoulder-to-shoulder
contest for the ball, four extra frames decides it before the game's own skill roll is
reached, and the rollback that follows drags the losing player visibly back *through* the
opponent. The other half is frame pacing (§5): SWOS 2020 does not act on GGPO's time sync
either, so the faster machine sits at the prediction limit and every input change rolls
back up to 8 frames.

**Not a cause: the order the teams are updated in** (checked 2026-09-17). It was suspected
that `CalculateIfPlayerWinsBall`, gating on the opponent's `wonTheBallTimer`, let whichever
team is evaluated first in a frame contest the ball and locked the other out. It does not:
only ONE team is updated per frame, alternately (`UpdateAndApplyTeamControls`, SWOS2.s
25592), and the 12 goes to the side that LOSES the skill roll, as a lock-out
(docs/GAMEPLAY.md §12). Who starts a contest follows who reached the ball when, not which
team is which, and at equal skill the roll is exactly 50/50 (`Rand() & 31` is uniform: the
random table is a permutation of 0..255). The model predicts all 186 contests in the DOS
recordings. It is identical in the Amiga binary, the DOS binary and SWOS 2020. What the
contest does do is make a misprediction expensive: it reads the REMOTE team's stick and ball
claim, so a wrong prediction can roll a contest one peer never had, spend a `Rand`, stop the
ball and lock a side out for ~24 frames -- all undone when the input arrives. That is a
reason to keep the input delay and the pacing right, not a bug in the contest.

**Messages**, for anything that must arrive whole: `P2PLink_Send(channel, …)` /
`P2PLink_Received(channel, …)`, as numbered chunks with a bitmap ack. Each kind of message
has a **channel** (`P2P_NUM_CHANNELS` is 5: 0-2 the lobby, 3 the match report, 4 what each
player saw, `PRED`), and sending on a channel again **replaces** what was there: it gets the
next generation, the receiver keeps only the newest complete one, and chunks of an older
transfer still in flight cannot be mixed into a newer one. Before generations, a re-send of
the same length was indistinguishable from the first and would have been assembled from
both — fine for a one-shot setup, fatal for a lobby where a player changes their mind. Up to
128 KB a message; payloads are the caller's to encode portably.

> The read buffer is sized by the largest packet, not the typical one. It was 64 bytes —
> ample for the control packets and 16 short of a setup chunk's header. `recvfrom`
> truncates silently, so every chunk arrived with all but its first 48 bytes missing while
> the transfer still completed and acked: right length, full bitmap, wrong contents. The
> self-test sends 40 KB and compares every byte, which is how that was found. It now also
> replaces messages before and during sending, both ways at once, and repeats it all on a
> wire that drops every third datagram.

### The lobby (`src/netplay_lobby.c`, `mpGameSetupMenu`)

Once the link is up and the delay agreed, both players land in SWOS 2020's lobby screen.
SWOS 2020's model: **each player publishes their own side** — a team, a tactic, ready or
not — and **the host owns the rules** (cup, game length, pitch, substitutes). Player 1 is
the host and the top team.

| Row / button | Who | What it does |
|---|---|---|
| TEAMS | your row | the game's own team list — clubs, national sides, custom teams (`ChooseTeamsDialog`, as IMPORT TEAM uses it) — and then straight on to the team screen (TACTICS, below), as in SWOS 2020 |
| TACTICS | your row | opens the game's own team screen, the one a friendly shows before kick-off: the line-up (FIRE on two players swaps them, a starter for a substitute too), the formation, VIEW OPPO and EDIT TACTICS. Its PLAY button reads **CONFIRM** here: CONFIRM sends the arranged team to the other player and makes you ready (green); EXIT sends it and leaves you not ready. The other player sees YOUR OPPONENT IS ARRANGING THEIR TEAM meanwhile. The confirmed side is kept for the rematch -- the lobby holds it, not the match, so substitutions made during a match do not carry over. It matters: SWOS scales each human player's skills by how well he suits his slot, down to 75% (ReportBot 0014), and a team played in its file's order can have a quarter of its players misplaced |
| GO | your row | ready, or not |
| CUP | host | how a draw is settled, SWOS 2020's row: **OFF** (a friendly: a draw stands), **1 LEG** or **2 LEGS**, then **ET + PEN** or **PEN ONLY**. Details below |
| GAME LENGTH, PITCH TYPE, SUBSTITUTES | host | the friendly menu's own cycles; the joiner sees them change |
| START | host | lit when both are ready on the current rules |
| FRAME DELAY | — | the delay the link agreed; shown, not chosen |
| SHORT PRESSES, KICK-OFF SYNC, CARDS/INJURIES | host | the host's switches, ON/OFF, on the left; below |

**The host's switches** (`NETPLAY_SW_*`, `include/netplay_match.h`). Report 0042 said online
play changed after 7d9d6d9; each netplay change since then that can be turned off is a
switch, so a host can A/B them in real matches. **Only the host's count**: they travel in
the host's rules like the game length (a change takes the joiner's GO down), the joiner sees
them greyed, and at kick-off both machines put them in force (`NetplayLobby_ApplyMatch`) and
take them back after (`NetplayLobby_RestoreTactics`). ON is how this build plays.

| Switch | Default | OFF means |
|---|---|---|
| SHORT PRESSES | the host's own `[input] latch_presses` | a fire tap shorter than a frame is lost, as before 6976feaf (`input_sdl.c`, the press latch) |
| KICK-OFF SYNC | ON | GGPO-X predicts the whole window from frame 0, as before 4e7553f3 (`ggpo_set_kickoff_prediction`) |
| CARDS/INJURIES | ON | nobody booked or injured: the old tester builds' online game (`TestRules_Force`); the recording's rules say 0xD8 and `--srp-resim` replays it by that rule. Hidden in a `--release` build, which has the rule compiled out: as host it always plays with cards, and as joiner it will not ready up for a host with them off ("HOST PLAYS WITHOUT CARDS: NOT IN YOUR BUILD") |

CATCH UP (a stall's backlog played back to back, fd911618) and BALANCE (GGPO-X's prediction
balance, docs/NETPLAY_BALANCE.md) were switches too until 2026-10-30, when they were removed:
Playa's tests found them causing netplay problems. Their bits (0x02, 0x08) are never set, and
an older host's are dropped (`clamp_rules`); `NETPLAY_COMPAT` 5.

Not a switch: the time sync's frame advantage (29009dbd). The old builds sent garbage there
that was nearly always 0, and 0 is what is sent now (`GGPO_SEND_FRAME_ADVANTAGE=0`).
`[lobby]` and `[netplay] the host's switches: ...` lines in both logs (so in the match report)
say what a match was played by. `SWOS_LOBBY_SWITCHES=<hex>` sets them in the lobby and relay
tests: `HOST_ENV=SWOS_LOBBY_SWITCHES=0 scripts/netplay_lobby_test.sh`.

The protocol is three messages, each on a link channel of its own:

- **STATE** — one player's side (team record, tactic, the USER tactic itself if it is one,
  ready, name). The host's also carries the rules and their generation; the joiner's echoes
  the rules generation it has seen. Re-sent on every change.
- **START** — host → joiner: the whole match setup (`netplay_setup.c`) built from both
  sides, and **which of the joiner's STATE messages it was built from**.
- **ANSWER** — joiner → host: yes, or no. The joiner says no when the START names a STATE
  older than its newest — it changed something while the host was pressing START — and the
  host goes back to the lobby and says so. Without the answer the host would kick off a
  match the joiner never agreed to.

**Ready goes down** when what you agreed to changes: your own team or tactic, the other
player's team or tactic, or (for the joiner) the host's rules. A match starts only from two
ready flags set after the last change either player could see.

**The USER tactics.** Both teams share six slots. Player 1's USER tactic goes in USER A for
the match and player 2's in USER B (SWOS 2020's convention), so two players who both use
their own "USER A" do not collide; both machines put their own six slots back afterwards.

**Kick-off re-applies the setup** (`NetplayLobby_ApplyMatch`) immediately before
`InitAndPlayGame`; `NetplayMatch_StartLive` releases every input; and the first frame step
zeroes the joystick history and the match clock. Each was found the hard way — two peers
that came through the MENUS desynced on frame 1 while the live test, which skips the front
end, never did:

- **Seeds that are not generator state.** `Randomize2` XORs its argument with
  `_randomSeed`, and `ApplyTeamTactics` reseeds per player from `GetPlayerRandomNum`,
  which mixes in `_newRandomSeedCopy` and `_currentSeasonShowing`. All are taken from the
  tick counter when the front end starts, so every player's skill roll differed by a point
  here and there. They travel in the setup now (version 2).
- **Menu key presses.** `Player1StatusProc` keeps a fire counter and edge detectors from
  call to call, and runs in the menus: the host who pressed START came out with
  `_pl1FireCounter` 2 against 0. And `InputSDL_PostFrame` stops updating the joystick
  registers the moment a netplay match is enabled, so the stadium screen's wait for fire to
  be released waited forever on the held START press.
- **The clock.** `StartLive` zeroed the tick counters, but the stadium line-up and its waits
  run after that while the SDL timer thread keeps ticking by the wall clock — so two peers
  reached frame 0 at 0x1B and 0x1D. Only intermittent on one machine, it reproduced every
  time once `SWOS_PRESENT_CAPTURE` slowed one peer, and two real machines will always differ.
  The first frame step now resets the clock where the replay rig does
  (`applyFirstFrameSetup`), with its values.

`SWOS_LIVE_DUMP=<dir>` writes each peer's state at the first frame step and
`--netplay-state-diff=A,B` names the regions that differ; that is how both were found.

**A desync that happens mid-match needs a different tool**, because it names no frame:

```
SWOS_LIVE_HASH_EVERY=1            a [live] hash line every frame, not every 100
SWOS_LIVE_DUMP=<dir> SWOS_LIVE_DUMP_FRAME=399,400    those frames' states
SWOS_LIVE_DUMP_STREAM=<path>      EVERY frame, to <path>.m<match>-p<peer>.bin
bin/SWOS.x86_64 --netplay-state-stream-diff=A,B      where they part, and what differs
```

`SWOS_LIVE_DUMP_FRAME` needs the frame number in advance, which is fine for a desync
that repeats and useless for one between two machines: the frame two peers part on is
only known once both logs are side by side, and by then the match is over. A live match
does not repeat either — each run starts from its own clock and plays a different match.
`SWOS_LIVE_DUMP_STREAM` writes every frame instead (~20 MB per 600-frame match, one
open and one buffered write a frame) and the stream diff finds the first differing
frame afterwards, printing the frame BEFORE it as the control: that pair is the whole
point, since it separates a field that went wrong here from one that was already wrong
and has only now reached the hash.

The stream is the **portable** encoding, not `SimState_Save`: two peers are two
processes, and their raw saves differ in every pointer region from frame 0 while ending
on the same hash (a passing local pair test measures 34 regions apart). Records carry
GGPO's frame number, so a re-simulated frame replaces its own earlier record rather than
shifting the rest, and the header carries `SimState_LayoutHash` so the diff can refuse
two peers whose registries are not the same registry.

One caveat when reading a diff: each peer's own pad state is in the registry
(`_pl1Direction`, `_pl1Down`, …), and in a test where both sides generate their own
input — `scripts/netplay_live_test.sh` — those regions genuinely differ from the second
frame on without the match having desynced. A matchmaker-driven AI-vs-AI run has no
local input at all, which is why it is the clean way to chase a platform divergence.

**The team list is a modal menu**, and nothing polls the link while it is up — the other
player would see this one go quiet and drop the connection after six seconds. The lobby
installs `Platform_SetFrameHook(P2PLink_Poll)`, which `Platform_VBlank` runs on every frame
of every menu. **Returning from it restores the lobby menu from its template**, every hidden
entry visible again, so the lobby re-applies its layout (and the navigation links SWOS 2020
wires in code) on every draw.

Not ported: the location flags, ranked and tournament modes, team database choice, chat and
the live cast — hidden. (Cup legs are there now: the CUP row, below. So is editing a tactic:
TACTICS opens the team screen, whose EDIT TACTICS is the game's own editor.)

**Both peers kick off from the host's state.** The init (`InitAndPlayGame`) does not write
everything a match reads: the game state, the stoppage timers, where the ball and players
stood, the bench and crowd are whatever the machine last had. After a normal match both
machines had the same, so rematches agreed — but after one that ended out of step, or at
different frames on the two machines, every rematch started out of step at frame 0. So
START carries the host's SIM-tier state as it stood when it pressed START (a `state_wire`
packet after the setup; `LOBBY_VERSION` 3; 4 added the CUP rules to STATE), and at kick-off BOTH peers — the host too —
load it, then apply the setup, then run the init (`NetplayLobby_ApplyMatch`). Each keeps
its own LOCAL fields (`SimState_RestoreLocal`): the HUD is placed for its own display.
Checked with `SWOS_LIVE_DESYNC_FRAME=150` on the joiner for three matches in a row: every
one now agrees up to 151; before, matches 2 and 3 parted at frame 0.

**The CUP row** is the game's own tie-break, not an imitation: the setup carries
`_extraTimeState`, `_penaltiesState`, `_secondLeg`, `_playing2ndGame` and the first leg's
score (`SETUP_VERSION` 3), and `game_state.c` settles the draw as it does in a DIY cup. The
friendly init clears all six, so `NetplaySetup_TakeTieRules` puts them back from inside it,
armed only for the match about to start (`--srp-resim` arms them too, so a cup tie replays
as it was played). A 1-LEG match goes to extra time and penalties, or straight to penalties.
Of 2 LEGS, the first may end in a draw; when it is played to the end (`NetplayLobby_MatchPlayed`,
on both machines) the next match is the second leg — the lobby says `FIRST LEG 1-0 - GO FOR
THE SECOND LEG`, the CUP button `2ND LEG` — which starts from that score (the HUD shows the
aggregate) and is settled by the tie-break if the aggregate is level; then the next is a new
tie. A first leg cut short is played again. Changing the CUP row starts a new tie.

As in SWOS 2020 the two players keep their sides for both legs — nobody is ever the away
team — so there are **no away goals** (`_playing2ndGame` 0). The joiner advances its own
copy of the tie from the same match, and only rules that DIFFER from its copy take its ready
down, so a GO pressed before the host's new rules arrive stands. Test knob:
`SWOS_LOBBY_CUP=<legs>:<1 ET+PEN|2 PEN ONLY>` on the host; `MATCHES=3 FRAMES=400
HOST_ENV=SWOS_LOBBY_CUP=2:2 RELAY=0 REPORTS=1 scripts/relay_test.sh` plays a tie and the
first leg of the next.

### When a match ends, and rematch

**A live match hands the machine back the moment it is over** (`liveEnd`,
`netplay_match.c`): the GGPO session closed, the sound filter off, the timer thread and the
pad back, one frame of input at a time no more. The mode goes off there rather than after
`InitAndPlayGame` returns, because the game plays on after the last frame -- the fade, and
the screens on the way back -- and reads input and the clock the ordinary way. Before this
the first live match that reached its end through the menus stopped on the friendly REPLAY /
EXIT screen and waited for ever: the session still owned the pad. That screen is skipped
after an online match too; its REPLAY would have kicked off a second match on one machine,
with no session and nobody at the other end.

**It can also end early, and says why**, where it used to exit the game: the other player
left (GGPO's disconnect, now 10 seconds of silence in a live match rather than 30 --
`LIVE_TIMEOUT_MS`), the link failed, or the two games went out of step. Corpus runs still
fail hard on all of these; that is what a test wants. `NetplayMatch_LiveOutcome` tells the
menus which.

**Then both players are back in the lobby with the same opponent**, which is SWOS 2020's
model too. `NetplayLobby_Rematch` reopens the link -- both punching to the address the match
was played with, from the port it was played on, which is also the NAT mapping the match kept
open -- and the lobby is RECONNECTING until the two are linked and have agreed an input delay
again, measured afresh. Both sides, their tactics and the host's rules are kept; both ready
flags go down, so a rematch is two GOs and a START. The line under the players says FULL TIME
- PRESS GO FOR A REMATCH, or why the match ended; if the other player does not come back
within the link's 15 seconds, YOUR OPPONENT DID NOT COME BACK. The lobby menu is shown afresh
after every match rather than carried on with, since the match has had the screen.

One trap, found by the first two-match test: a message generation starts again at 1 with
every connection, and the lobby remembered the generations it had handled -- so the new
link's first STATE, START and ANSWER looked already seen. `Rematch` forgets them.

**A live match is recorded like any other** (`docs/REPLAY_FORMAT_V2.md`): `LATEST.SRP` in
the save folder, watchable from HIGHLIGHTS -> LOAD. Frames are keyed by GGPO frame number
(`ReplayRec_SetFrame`), so a rollback re-records the frames it re-simulated and the file
holds the match as it was finally played, not as it was first predicted. The recorder sits
on the audio OBSERVER, upstream of the sound filter, so a replay has the sound the match
made rather than the sound this peer happened to hear. Proved by recording both peers of a
stalled p2p run: the two files are byte-identical, and each plays back exactly what its
peer drew.

```
scripts/netplay_lobby_test.sh                                 # 2 matches, 300 frames each
MATCHES=3 FRAMES=600 PROFILE=wifi scripts/netplay_lobby_test.sh
JOIN_ENV="SWOS_LIVE_SLOW_PCT=2" MATCHES=1 FRAMES=2500 scripts/netplay_lobby_test.sh
CAPTURE=5 CAPTURE_FROM=560 MATCHES=1 FRAMES=100 scripts/netplay_lobby_test.sh   # screenshots
```

It runs two `--netplay-lobby` instances through the real lobby MENU -- the kick-off desyncs
above were only ever found that way -- with `SWOS_LOBBY_MATCHES=N` to leave after N matches
and `SWOS_LIVE_RANDOM_INPUT=<seed>`, each side's input from its own generator so both predict
wrong and roll back. It passes when both played every match to the end and each match ended
on the same state hash on both sides. 2026-09-17: 3 matches of 600 frames over `wifi`,
identical, with 59, 37 and 14 rollbacks on one side; and a joiner killed mid-match brings the
host back to the lobby with THE OTHER PLAYER LEFT.

```
bin/SWOS.x86_64 --netplay-lobby=host     # two instances on one machine, over loopback,
bin/SWOS.x86_64 --netplay-lobby=join     # straight into the lobby
scripts/netplay_live_test.sh             # the lobby driven through its API, then a match
```

Test knobs, for looking without a pad: `SWOS_LOBBY_TEAM=<file>:<ordinal>` (hex) picks a
team as the lobby opens, `SWOS_LOBBY_READY=1` says ready, `SWOS_LOBBY_START=1` has the host
press START as soon as it can, and `SWOS_KEY_SCRIPT="<frame>:<key> …"` presses keys on given
frames (SDL key names). With `SWOS_PRESENT_CAPTURE` that is how the lobby screens were
checked.

The live test drives the lobby through the same API the menu does: each peer picks a
different team, the joiner a USER tactic from slot C, the host changes the rules after the
joiner is ready (which must take the joiner's ready down), the joiner changes its mind as
the first START arrives (which must be refused and retried), and both peers must then print
the same match — England 4-3-3 v Brazil on USER B — and end with the same state hash.

---

## 7. Matchmaking (`src/mm_client.c`, `third_party/matchmaker`)

§6's menu connects two players who already know each other's IP address. That is a
useful test and a bad way to find an opponent, so online play goes the way SWOS 2020's
does instead: a **matchmaking server** introduces two players, and the match itself is
still peer-to-peer.

**The server only introduces you.** Its `START:` message hands each side the other's
candidate addresses and it is then out of the loop. Killing it mid-match does not end
the match; it only stops new ones being made. The exception is a server run with
`--relay`, which plays the match through itself — see "The relay" below.

### Running one locally

```bash
scripts/run_matchmaker.sh                  # build + run on UDP 19602
SWOS_MM_HOST=127.0.0.1 bin/SWOS.x86_64 --matchmaker-selftest   # one client: log in, lobby, create, leave
scripts/matchmaker_pair_test.sh            # two clients: join, approve, START
```

**Name the local server.** The game's default is the public server, `us.swos.eu`, so a
client pointed nowhere in particular reaches THAT one, not yours. Every script here sets
`SWOS_MM_HOST=127.0.0.1` for the server it starts; do the same by hand, or the self-test
above quietly tests the public server instead.

```bash
bin/SWOS.x86_64 --matchmaker-selftest      # the public server: a quick check that it is up
```

The server is SimpleMatchmaker, vendored at `third_party/matchmaker` — the same
project SWOS 2020 ships. Its `OPENSWOS.md` records what was changed and why. Both it
and the client are built on the one vendored ENET (`third_party/enet`).

### Running one for testers

Two players on two machines need a server they can both reach, which means one machine
with an open UDP port. The server is stateless and tiny: it introduces players and drops
out, so restarting it interrupts nobody's match, only new ones being made.

**1. Run it.** It binds `ENET_HOST_ANY`, so it is already listening on every interface:

```bash
scripts/run_matchmaker.sh                  # builds if needed, UDP 19602
```

Keep it alive across a disconnect with `tmux new -s mm 'scripts/run_matchmaker.sh'`, or
`nohup`, or a systemd unit if it is going to live somewhere permanently.

**2. Let it through the machine's own firewall.** On this Windows box WSL2 runs in
**mirrored** networking mode (`networkingMode=mirrored` in `%USERPROFILE%\.wslconfig`),
so a WSL listener shares the Windows host's addresses — no `netsh portproxy`, which is
TCP-only and would not have worked for UDP anyway. Mirrored mode is firewalled by the
Hyper-V firewall, so the rule goes there:

```powershell
# elevated PowerShell, once
New-NetFirewallHyperVRule -Name SWOS-MM -DisplayName "SWOS matchmaker" `
    -Direction Inbound -VMCreatorId '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}' `
    -Protocol UDP -LocalPorts 19602 -Action Allow
```

**3. Forward it on the router**: UDP 19602 → the host machine's LAN address.

**4. Tell the testers the address.** `SWOS_MM_HOST=<public IP or DNS name>`; the port only
if it is not 19602. **`baker76.com` is the wrong name to hand out** — it resolves to a
GoDaddy shared host, not to whatever machine is running this. A home
connection needs its current public IP, or a dynamic-DNS name pointed at it.

**5. Prove it from another machine before handing it out**, rather than trusting the
forward:

```bash
SWOS_MM_HOST=<address> bin/SWOS.x86_64 --matchmaker-selftest   # logs in, lobbies, leaves
```

That is a full protocol handshake, not a ping. There is nothing TCP to telnet at — a TCP
tool reporting "connection refused" against a UDP service means nothing.

**Use the PUBLIC address in that test, even from inside the house.** Measured here
(2026-09-17, server in WSL on the Windows box, rule added, UDP 19602 forwarded): a machine
on the same LAN reached the server through the public address and its own port forward
(`172.249.109.62`) but **not** through the server's LAN address (`192.168.1.43`) — same
host, same port, same firewall rule. So a failure against the LAN address does not mean
the setup is broken, and a success against it would not have proved the forward. The
public address is what a tester uses and the only one worth testing.

With that, a cross-machine run of the pair test is the real rehearsal — one machine hosts,
another joins, both through the public address:

```bash
SWOS_MM_HOST=<public> bin/SWOS.x86_64 --matchmaker-pair=host   # on one machine
SWOS_MM_HOST=<public> bin/SWOS.x86_64 --matchmaker-pair=join   # on the other
```

Each side should end with `START:`, the right player number, the other's name, and a list
of candidate addresses that includes both peers' public address. Two machines behind the
SAME router still reach the server by hairpin, so this proves the forward and the server
but not yet that the internet side accepts traffic — the first real tester does that.

### The protocol

Text over ENET, one message per packet, `HEADER:content`. On connect, in this order:

```
VERSION:<gameID>            a compatibility tag; a different one is refused
INFO:<addr,addr,...>        our candidate addresses
LOGIN:<userName>:<userData>
```

then `CREATE:<min>,<max>`, `JOIN:<owner>`, `APPROVE:<player>`, `EJECT:<player>`,
`LEAVE:`, `START:`. Back come `LOGIN:`/`CREATE:`/`LEAVE:`/`EJECT:` as acks,
`PLAYERSACTIVE:` and `GAMESOPEN:` as list updates, `GAMEINFO:`, `JOIN:<name>` and
`START:<playerNumber>,<peerName>,<addr>,...`.

**Three things about this protocol are not guessable, and each one fails silently:**

- **`CREATE:` takes `"<minPlayers>,<maxPlayers>"` and nothing else.** The server splits
  on `,`, requires exactly two parts and `stoi`s both; anything else throws and the game
  is simply not created, with **no reply at all**. Sending a descriptive name (the
  obvious guess) looks exactly like the server ignoring you.
- **The two list messages are encoded differently**, though both are "a list of names":
  `PLAYERSACTIVE:` is `<count>:<name>:<dataLen>:<data>...` and `GAMESOPEN:` is a bare
  `name,name,` with a trailing comma and no count. Parsing the second like the first
  yields zero games, which reads as "nobody is hosting".
- **Addresses are a decimal uint32, not a dotted quad** — `ToString` is
  `std::to_string(addr.host)`, the four octets in network byte order printed as one
  number (127.0.0.1 is 16777343). Sending dotted text does not fail: the server `atoi`s
  it to `127` and later tells your opponent to connect there. The first pair test
  reported peers at 127, 10, 192 and 169 — the first octets of the addresses it meant.

`GAMEINFO:` only arrives from the server's `SendInfoToAll()`, i.e. once somebody asks to
join — so an empty game info straight after `CREATE:` is correct, not a dropped message.

### The client

`src/mm_client.c`, in C, against the vendored ENET. It keeps the lobby in plain arrays
you read whenever you like (`MmClient_Users`, `MmClient_OpenGames`, `MmClient_GameInfo`)
rather than delivering callbacks, because menus redraw from state every frame; the only
one-shot events are a player asking to join and being thrown out. Call `MmClient_Poll()`
once a frame.

The upstream C++ client (`ServerConnection/`, `P2PConnection/`) is vendored unbuilt, as
the authority on what the reference implementation does.

### The menus

**MULTIPLAYER now opens SWOS 2020's online flow**, not §6's direct-IP menu: log in →
matchmaking → the lobby. The handlers are in `src/online_menus.c`, against the menus
imported from SWOS 2020 (docs/MENUS.md §10). The direct-IP menu is still there and still
works — it is the way to test a link across a real router with no server in the middle —
but it is reached with `--menu=p2p/p2pMenu` now.

The matchmaking menu's `OnDraw` is also where the client is pumped: it runs every frame
(the menu is flagged `dos_redraw`) and there is no other per-frame hook while a menu is
up. The lobby is redrawn from `MmClient`'s state each frame rather than from callbacks.

**The player types the server address, they do not set a variable.** The login screen's
SERVER row takes `host` or `host:port` and saves it to `SWOS.ini` (`[netplay] matchmaker`)
the moment it is entered, so a tester is told an address once and never again. The row is
SWOS 2020's password field: our matchmaker has no accounts -- `LOGIN:` is a name and a
blob, `VerifyVersion` is an empty TODO -- so a password box would ask for something
nothing reads. `SWOS_MM_HOST` / `SWOS_MM_PORT` still override it, for a test that has to
point one process somewhere else.

```bash
bin/SWOS.x86_64 --matchmaker-menu=NAME   # log in and open the lobby at startup
SWOS_MM_HOST=... SWOS_MM_PORT=...        # override, without touching the player's ini
SWOS_MM_AUTO=host|join                   # press OPEN, or join the first game, and
                                         # approve -- both ends of a match, no hands
```

**The gdb harness cannot hold a lobby connection.** A process stopped at a breakpoint
sends no ENET keepalives, so the server times it out — the menu goes on saying ONLINE
while the server has already dropped it. Anything needing a live connection has to run
inside ONE continuously-primed harness session, with the opponent spawned from inside it.
Splitting it across two script runs lets the connection die in the gap.

### Testing it against the reference client

`scripts/matchmaker_pair_test.sh` runs two copies of `src/mm_client.c`, which proves only
that our client agrees with **itself** — two implementations of one protocol can share a
misreading and nothing notices. `tools/mm_refpeer` is the other implementation: upstream's
C++ `ServerConnection`, the code SWOS 2020 ships, driven through the same host/join
sequence and asserting the same three things (player number, peer name, at least one
address).

```bash
scripts/matchmaker_interop_test.sh    # starts a server on 19604, runs 3 rounds, stops it
```

| round | |
|---|---|
| 1 | the reference hosts, ours joins |
| 2 | ours hosts, the reference joins |
| 3 | the reference hosts **with an open-game data blob**, ours joins |

Rounds 1 and 2 passed first time: the whole handshake — `VERSION:`/`INFO:`/`LOGIN:`, the
user and open-game lists, `JOIN:`, `APPROVE:`, and the `START:` handoff with both sides
told the right player number, the right peer name and four usable addresses — is read the
same way by both implementations. It also settles a question that matters if we ever meet
their server: **the two clients send different `VERSION:` tags** (`SWOS_MatchMaker` against
our `OPENSWOS`) **and the server does not care**, because `Connections::VerifyVersion` is
an empty `// TODO` returning true.

**Round 3 failed, and it was a real incompatibility.** Upstream's
`ServerConnection::CreateGame` sends `"<min>,<max>," + payload`; upstream's own server
demands exactly two fields and **disconnects** the user otherwise
(`Server/User.cpp`). The two halves of the same release disagree, and the consequence for
us was that the client SWOS 2020 actually ships could not host a game against the server we
test against — it was dropped the moment it pressed CREATE. Our vendored server now accepts
`>= 2` fields and ignores the payload
(`third_party/matchmaker/OPENSWOS.md`, "Changes from upstream"); storing it and emitting
the newer `<count>:<name>:<len>:<data>` open-games format is a separate feature, and
`src/mm_client.c` only parses the old one today.

Two differences that are not faults: the reference binds a fixed local port (first free of
6000–6009) where ours takes an ephemeral one, and the reference drops its server connection
the instant `START:` arrives.

### The relay (`--relay`, `third_party/matchmaker/Server/Relay.cpp`)

Matches are peer to peer, and stay that way for players. The relay is for the cases
that cannot be, or should not be:

- **testing** — the server can put a misbehaving network in the middle of a real match
  between two real machines (`--netem`), which neither of them knows is there;
- **the browser build** — a browser has no UDP, so no peer to peer (the same sessions
  now also run over WebSocket, through the server's gate: §8);
- **players whose NATs will not punch**, and a match being streamed live.

**How.** At `START:` the server makes a session with a random 64-bit token and sends each
OpenSWOS player `RELAY:<token>,<n>` just before `START:` (never to a SWOS 2020 client,
whose parser throws on a header it does not know). Both then punch to ONE candidate, the
server, and each punch round also registers there: a 16-byte `SWRL` datagram with the
token, from the socket the match will be played on (`include/relay_proto.h`). The server
binds each source address to the session and from then on forwards every datagram from
one registered address to the other, unchanged, from the port it arrived on. `p2p_link`
and GGPO each see one peer at one address and never learn it is the server's — GGPO is
not touched at all. A rematch punches the same address and registers again, so one
session carries every match two players play in a row; it closes after a minute with no
traffic.

**As a fallback** (2026-10-22), with `--relay-fallback` -- OFF by default: every pair
that falls back costs the server its traffic both ways (about 2.5 KB/s in and out a pair,
measured), and the owner keeps the relay for browsers. With it, a server without `--relay`
makes a session for every pair of desktop OpenSWOS players and sends `RELAYFB:<token>,<n>` (not `RELAY:`, so
a game from before it skips it as unknown and plays as it did). The players punch direct
as always; a link that has measured no ROUND TRIP after 5 s (`kRelayFallbackMs`) starts
again from the same socket to the server alone, registering with that token -- from there
a relayed match like any other (`p2p_link.c`, `relay_fallback`). A round trip, not
"connected": a router that gives every destination its own port can let the other side's
packets in while its own never arrive, and then one game thinks it is connected while the
other waits -- neither has a round trip, so both move. A link that works both ways has one
within a second, and the fallback is disarmed. A session nobody registered with closes
with one line ("not needed ... played direct") and no ledger line. Found by two players (one through a VPN, one behind a port-changing router)
stuck on "CONNECTING TO ... (8 ROUTES)". Tested by `RECORD=1 RELAY=0 BLOCK=both|host|join
scripts/relay_test.sh`, where `SWOS_P2P_TEST_BLOCK_DIRECT=1` lets nothing in but the server.

It shares UDP **19602** with the matchmaker through ENet's `intercept` hook, so a server
needs no second port forwarded. A datagram is the relay's only if it is a well-formed
`SWRL` control packet or comes from a registered address; everything else is ENet's. It
never forwards for an address that did not register with a token the server issued.

```bash
build/matchmaker/swos-matchmaker 19602 --relay                   # relay every OpenSWOS pair
build/matchmaker/swos-matchmaker 19602 --relay --netem wifi      # ... through wifi
build/matchmaker/swos-matchmaker 19602 --relay --netem cycle \
    --ledger matches.jsonl                                        # a new profile per session
scripts/relay_test.sh                     # two players, the real menus, two matches through it
NETEM=awful scripts/relay_test.sh         # ... under the server's worst link
```

**The server's emulator is the game's.** `include/netem_core.h` + `src/netem_core.c` hold
the profile table and every rule; the game's `src/netplay_netem.c` is now only its env
vars, its clock and GGPO's socket, and the server compiles the same file in from the game
tree. So `wifi` is the same link in both places. The two are not redundant: the game's
touches only GGPO's packets, on a match played peer to peer; the server's touches
EVERYTHING in between — the link's handshake and the lobby too, which no emulator had
disturbed before. Each direction has its own queue and its own generator.

**The ledger** (`--ledger FILE`) gets one JSON line per session when it closes: the token,
when, the two names and registered addresses, how long it was linked, packets and bytes
each way, the profile, and what the emulator actually did each way. Every `[relay]` line on
the console is timed to the millisecond, because the question a relay log answers is
usually *when*.

**What it found on its first runs:**

- **The first cross-machine desync it caught was two builds, not the network.** Mac v
  Windows through the relay, cycling every netem profile, recording on: 34 matches clean,
  then a desync at frame 739 in round 7 (awful). The recorder named the frame; the dumps'
  SIM-only diff (`--netplay-state-stream-diff=A,B,sim`, which masks the display-local
  fields the checksum skips) showed the machines agreeing on every hashed byte at 738 and
  parting at 739 on a booking — `_refState`, `_whichCard`, `_bookedPlayer` — with identical
  inputs. The Mac's `TryBookingThePlayer` disassembles to `return 1` (a clean tester build);
  Windows' was the full routine, from a `tackle_foul.o` compiled two days earlier without
  the flag and never rebuilt. A flag build against a flag-less one on ONE machine, on a
  clean link, desyncs the same way at the first booking. Ruled out on the way: awful and
  rollback depth (16 local matches, ~2800 rollbacks, clean), display width (362 v 336
  under awful, 16 matches, clean), and state a rollback does not restore (2400
  random-input synctest runs, `SWOS_SYNCTEST_RANDOM_INPUT`, clean). Fixed three ways: the
  makefiles rebuild on a flag change, the handshake compares the build's rules (above),
  and the recorder warns when two players' rules differ.

- **The lobby's kick-off raced the network** (fixed, `src/netplay_lobby.c`). The host kicks
  off the moment it READS the joiner's answer and hangs up at once (`link_close`'s BYE);
  the joiner kicked off only when the ACK of its answer came back. On a link with any
  latency the BYE overtook the ack, the joiner read it as "the other player left", and the
  host sat in its session until "THE OTHER PLAYER NEVER ARRIVED". Invisible before because
  the client emulator never touches the link. Now a joiner that has accepted treats the
  host hanging up (or going quiet) as the kick-off it is.
- **Wall-clock time is not a timer.** The first version timed sessions with
  `enet_time_get()`, which is `gettimeofday`; a step in WSL2's clock made a session look a
  minute idle fifteen seconds into a match, and it was closed under the players. The relay
  uses `steady_clock`.

### The recorder (`--record`, `third_party/matchmaker/Server/Recorder.cpp`)

Any live match — relayed or peer to peer — can be streamed by each of its players to a
recorder, which is the matchmaking server run with `--record` (TCP, on the same port
number as its UDP). It can be the server that relays, the one that introduced the
players without relaying, or one of its own anywhere else:

```bash
build/matchmaker/swos-matchmaker 19602 --record --ledger matches.jsonl
SWOS_RECORD=server ...          # the player: stream to the matchmaker it logged in to
SWOS_RECORD=10.0.0.5:19602 ...  #             ... or to any recorder
# or in SWOS.ini:  [netplay] record = server (the default; empty = server) | host[:port] | off
```

**What is streamed** (`include/record_proto.h`, client `src/match_record.c`): a HELLO
(match id, player, both names, the teams, gameplay flags, delay, simulation width, the
build and platform, the relay token if relayed), then the SIM-tier hash of every frame
as it is confirmed, then an END (finished or not, the end frame, the final hash, why).
Only CONFIRMED frames — the GGPO-X fork sets the confirmed frame after the rollback its
inputs called for has been played, so each hash is the frame as finally played — and
only read from the trace the netplay step already keeps, so the recorder cannot touch
the match. It never waits during play: a recorder that is slow or gone costs the
players nothing; only the end of a match waits, a second at most, for the tail.

**Why hashes: the recorder is the desync detector for real matches.** Both players'
streams are paired by match id — derived from the lobby's GGPO packet key, which both
already share, so nobody hands one out — and compared frame by frame as they arrive.
The first frame whose hashes differ is reported the moment it is seen, on the console
and as a `desync` line in the ledger, with both builds and platforms, the teams, the
relay token and the wall-clock time; the match's `match` line follows when it closes
(frames compared, each side's frames, outcome and final hash, whether the final hashes
agree). That is exactly what the 2026-09-17 macOS/Windows desync did not leave behind:
it was found by comparing two logs by hand, hashed every 100 frames.

A frame is hashed at its START, so a desync injected INTO frame N
(`SWOS_LIVE_DESYNC_FRAME=N`) is reported at frame N+1 — the test requires exactly that.

**The match itself, too.** A recording is FULL unless `SWOS_RECORD_MODE=hashes`: each
player also streams the `.SRP` its game is recording (`src/replay_v2.c`) — the META once,
then every replay frame as it is confirmed with the sounds and chapters it played, then
the final score — and the recorder keeps it (`--recordings DIR`, default `recordings/`),
from player 1's stream when it has one. The file is laid out by the same code the game
writes `LATEST.SRP` with (`src/srp_writer.c`), so it is that file: the test requires the
recorder's copy of a match to be the host's own `LATEST.SRP` BYTE FOR BYTE. It is
written as the match goes (every 3000 frames, via a rename so a reader never sees half a
file) and finished when the match closes; the ledger's `match` line names it.

That is also the live-stream path: the frames reach the server a few frames behind play,
so a viewer served from them (later, and the browser build's) watches the match as it
happens.

**And what each player saw.** Each player streams its own half of `PRED` and `SEEN`
(docs/REPLAY_FORMAT_V2.md §4, "What each player saw") beside each batch of replay frames --
`RECORD_REPLAY_PRED`, which a recorder older than it skips -- and the recorder puts BOTH
players' halves in its file: the lag and rollbacks of each, and where each one's screen
drew players somewhere they never were. Back in the lobby the two games swap their halves
over the link (channel 4, `src/match_prediction.c`) and rewrite their own files with both,
so the host's `LATEST.SRP` is still the recorder's copy byte for byte. `[netplay]
prediction = false` keeps neither.

**And what it takes to play it again.** A live match's `.SRP` also carries its setup, the
state it kicked off in, and every frame's input and SIM hash (docs/REPLAY_FORMAT_V2.md §4,
"The match itself"). The player streams them too: a RESUME message once the kick-off is
known (the layout hash, the rules byte, the simulation width, the setup, the kick-off
packet), and each confirmed frame's input beside its replay frame. So the recorder's copy
can be played again anywhere, by `--srp-resim=<file>` (docs/REPLAY_FORMAT_V2.md §5), which
checks every frame against the recorded hashes and names the first that parts. A desync
between two machines no longer has to be caught while the match is played: any machine
can play the recorder's copy again and say whether it plays the match the same way.

That makes the recorder's copy the one to trust for a result — a league table, a
tournament. Neither player wrote it, both players' hash streams agreed on every frame of
it, and anyone can check that its inputs produce its states. A player's own file proves
only that it is self-consistent: a build that plays the same rules can make up one that
is. One thing the recorder cannot see is a doctored kick-off, because both players load
the HOST's kick-off state (the lobby, above) and so agree on it; checking that state
against what the setup alone produces is the next step there.

```bash
RECORD=1 scripts/relay_test.sh                       # relayed and recorded, .SRP checked
RECORD=1 RELAY=0 scripts/relay_test.sh               # peer to peer and recorded
RECORD=1 RECORD_MODE=hashes scripts/relay_test.sh    # hashes only
RECORD=1 DESYNC_FRAME=150 scripts/relay_test.sh      # the recorder must name frame 151,
                                                     # and the joiner's own .SRP part there
bin/SWOS.x86_64 --srp-resim=recordings/<match>.SRP  # play a recorded match again
```

**A match that does not end properly is still kept** (2026-10-12). 8 of the first 153
recordings had no inputs, four of them full matches cut at the final whistle -- where
players close the game. Three nets, each for a different way of going:

- *The window is closed* (or the game is told to quit): a live match is left through
  `liveEnd`, like ESC -- "YOU CLOSED THE GAME". It used to leave from inside the frame, and
  on a re-simulated frame that was `fail()`: `exit(2)`, nothing saved, nothing sent
  (`game_loop.c`, `netplay_match.c`).
- *The game dies* (crash, kill, power, the connection): the recorder keeps the frames that
  came WHOLE -- drawn, with their input and hash -- and drops the few after them, taking
  whichever player's stream got further (`Recorder.cpp` `Usable`/`Whole`). The game now
  sends each frame's hash ahead of the frame, so that is nearly all of them. A stream that
  ended properly is untouched: its file is still the host's own, byte for byte.
- *The player's own file*: at half time and at the final whistle, once every frame up to
  it is confirmed, the match so far is written to `LATEST.SRP` (`ReplayRec_Checkpoint`), so
  a game that dies after either has the match to that whistle. The final whistle is about
  8 seconds before the match ends by itself. The file is built on a thread from a copy:
  built in the frame it stalled that player for 155 ms, the other ran on past the end, and
  the host's file had three frames the recorder's did not. The real end writes over it.

A match that FINISHED is also cut to its last frame now (`liveEnd`, `recordConfirmed`): a
player whose game ran ahead at the end had saved the frames it played past it ("the match
ended after 25556 of 25559 frames", a recording of 2026-10-08).

Try it: play `RECORD=1 RELAY=0 FRAMES=6000 MATCHES=1 scripts/relay_test.sh` and send the
host's game SIGTERM (a window close) or SIGKILL mid-match: every `.SRP` that results --
each player's and the recorder's -- must `--srp-resim` with every frame as recorded. A
whole match is `FRAMES=0 WAIT=600` (the test then complains about the match clock's five
extra ticks at a natural end, which is older than this and not about recordings).

### Status

Working and tested end to end against a local server:

- connect, log in, the user list, open games, create, join, approve
- `START:` on both sides with the peer's name, the right player number and usable
  addresses (`scripts/matchmaker_pair_test.sh`)
- MULTIPLAYER → login → lobby, with a real opponent appearing in it: `2 ONLINE`, the
  opponent's game in the host rows, both names in the user rows
- joining → the host approving → `START:` → the handoff to `p2p_link`, which puts
  `mpWaitingMenu` up and reports the link's state

Since then (2026-09-16/17) the rest of the path exists: both peers punch to all of the
other's candidate addresses at once rather than meeting on the first one (`P2PLink_Punch`),
the host is asked to accept or refuse a player who wants to join (`hostGameMessageMenu`),
the link hands over to the lobby (§6), the lobby to a live GGPO match, and the match back to
the lobby for a rematch. Two people doing it on two machines has happened too, since the
first multiplayer weekend (2026-09-26/27) -- what they reported is docs/NETPLAY_REPORT.md
and docs/NETPLAY_GHOSTING.md.

## 8. The browser (the web build, `third_party/matchmaker/Server/WebGate.cpp`)

The web build plays online too: `make WASM=1 NETPLAY=1` compiles GGPO-X, ENet and the
link into it. What it cannot do is send a UDP datagram -- a browser has no UDP -- so
Emscripten's socket layer (`libsockfs.js`) carries each datagram as ONE WebSocket message
to `ws://<host>:<port>`, the same port number the datagram was for, over TCP, and hands
each message back as one datagram. The game's code is unchanged: matchmaking (ENet), the
link, GGPO and the recorder all still call `sendto`/`recvfrom`.

**The server's gate** (`--websocket`) is the other end. Every TCP connection on the
server's port number is sniffed (`TcpGate`): one that opens with `GET ` is a WebSocket,
anything else is a player's recorder stream (its magic is `SWRC`), since both arrive on
the same port. Each browser socket gets a UDP socket of its own on the server machine,
connected to the server's own UDP port -- so the matchmaker, the relay and GGPO's packets
through it see an ordinary client at `127.0.0.1:<port>`, and nothing behind the gate knows
a browser is there.

**One browser socket, one address.** Emscripten opens a WebSocket per socket and, for a
BOUND socket, sends its port first (`\xFF\xFF\xFF\xFFport` and two bytes). The link and
then GGPO bind the same local port, one after the other, and the relay knows a player by
the address its packets come from -- so a tunnel is keyed by (the browser's address, that
port) and outlives its WebSocket for a minute: GGPO's socket arrives on the tunnel the
link's left, and reaches the relay from the same address. In a browser a port is only a
name (nothing is bound, and 0 stays 0), so the web build picks a random one for the link
(`open_socket`), or two browsers behind one address would both be `:0`.

**A browser can only ever play through the relay** -- it reaches nothing but the server.
It says so at login (`VERSION:OPENSWOS-WEB/<n>`, still `OPENSWOS` to every prefix test),
and a server with `--websocket` relays any match with a browser in it, `--relay` or not
(`Connections.cpp`). A START the server did not relay is refused on the waiting screen:
"THIS SERVER CANNOT RELAY A BROWSER GAME". There are no local addresses to offer
(`getifaddrs` is compiled out), and none would be any use.

**The browser has to honour the time sync.** It paces with its own deadline
(`emscripten_sleep`, `Platform_Present`), so `Platform_DelayNextFrame` has to move THAT
one: before it did, a browser that ran ahead stayed ahead and its peer rolled back
(a Mac joiner against a browser host: 383 frames re-simulated, 31 holds at the prediction
limit; after: 57 and 0). A browser only reads the network when it yields -- once a frame
-- so its packets land up to a frame later than a native build's, and a link on one
machine still rolls back ~30 times in 600 frames where two native builds roll back none.
The input delay rule is not changed for it: that is what rollback is for, and the delay
is agreed from the ping both sides measure (§6).

**Its clock has to be monotonic.** SDL asks `clock_gettime` for `CLOCK_MONOTONIC_RAW`;
Emscripten's accepts only the WASI clocks 0-3, answers `EINVAL`, and SDL quietly falls
back to `gettimeofday` -- the wall clock, which steps (NTP, sleep and wake; under WSL2 all
the time). One step back wrapped a live match's unsigned "time since the last progress",
and its 10 s watchdog ended a match that had just finished, as "THE CONNECTION WENT QUIET"
(a browser host against a browser on the Mac, intermittently). The web build now defines
`clock_gettime` itself (`platform_sdl.c`, over musl's weak one): every clock but realtime
from `emscripten_get_now`, as WASI does. GGPO reads `CLOCK_MONOTONIC` and was never
affected.

**It plays the same match, bit for bit.** The state hash is taken over the portable
encoding (sim_state.c), so a 32-bit wasm build and a 64-bit native one hash the same state
the same way. Proven two ways: every Linux-recorded match in the resim set plays again in
headless Chrome frame for frame (`--srp-resim`, REPLAY_FORMAT_V2.md), and live matches
between browsers, and between a browser and the Linux and macOS builds, end on the same
hash.

```bash
build/matchmaker/swos-matchmaker 19602 --websocket     # [--relay --netem P] [--record]
make WASM=1 NETPLAY=1                         # every build plays the same rules online
scripts/wasm_netplay_test.sh                  # browser v browser, headless Chrome
PAIR=web-native scripts/wasm_netplay_test.sh  # browser v this machine's native build
PAIR=mac-web scripts/wasm_netplay_test.sh     # the Mac's native build v a browser here
PAIR=macweb-web scripts/wasm_netplay_test.sh  # a browser ON the Mac v a browser here
MATCHES=2 NETEM=awful ...                     # a rematch; a disturbed relay
```

`PAIR` is `<host>-<join>`, each `web` (headless Chrome here), `native` (`bin/SWOS.x86_64`),
`mac` (the Mac's native build, over ssh) or `macweb` (headless Chrome on the Mac). A Mac
player reaches this machine's server over the LAN, so the gate's port has to be open for
UDP (matchmaking, the natives' match) and TCP (a browser's WebSocket). The Mac browser is
Chrome for Testing's `chrome-headless-shell` for mac-arm64, unzipped into
`/tmp/swos-chrome` (`MAC_CHROME=`); the test copies the web build to the Mac and serves it
from the Mac's own localhost, so no port but the gate's is needed. Proven: a browser on
the Mac against a browser here, the Linux build and the Mac's own native build.

The test drives the browser through the web build's query-string hooks (`wasm/pre.js`:
`?arg=` command-line arguments, `?env=` environment, `?file=` fetched into `/in/`), the
way the native tests drive a binary. Headless Chrome is Playwright's
`chrome-headless-shell` (or `CHROME=`); the page's console reaches the test through
`--enable-logging=stderr`, so no browser driver is needed. `SWOS_NO_LOG=1` matters there:
without it the game's stderr goes to a `SWOS.log` in the browser's memory.

**Done since:** **`wss://` on the public server.** A page served over https may only open
`wss://`. The gate speaks it (`--tls-cert`/`--tls-key`, TLS on the same
port, `TLS=1 scripts/wasm_netplay_test.sh`; docs/NETPLAY_SETUP.md §10), and `wasm/pre.js`
picks `wss://` on an https page. us.swos.eu has its certificate, and the web build has
netplay on by default since (2026-09-24).

**Not done yet:**
- **Recording from a browser.** The recorder's TCP stream would arrive as a WebSocket too;
  the gate sees `SWRC` in its first message and could hand it to the recorder. Not wired:
  a browser player's match is recorded only if the other player records it.
- **A browser tab in the background** is throttled by the browser (timers to about once a
  second), which a live match will not survive; the player has to keep the tab in front.
