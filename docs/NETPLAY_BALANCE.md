# NETPLAY_BALANCE.md — keeping one player from doing all the predicting

> **Status (2026-10-30): REMOVED from the game** -- the host's BALANCE switch, `[netplay]
> balance` and `SWOS_LIVE_BALANCE` are gone (Playa's tests found it causing netplay problems;
> `NETPLAY_COMPAT` 5). The library code stays in our GGPO-X fork, unused. What follows is the
> history. (2026-10-06: built, OFF by default; `[netplay] balance = true` turned it on, and
> both players had to have it on.) It is in our GGPO-X fork (`third_party/ggpo-x`,
> `lib/ggpo/prediction_balance.h`, `GGPO_PREDICTION_BALANCE`), not in the game: the game
> only waits when the library asks. The same work found that the time sync had been
> steering on noise ("The bug underneath", below); that fix is in every match, balance or
> not, and is why balance goes out switched off -- one change at a time.
> Neither has been played between two real machines yet.

## The problem

Under rollback, a player whose opponent's input arrives late plays on a **prediction**
(GGPO-X repeats the last input it has) and rolls back when the real input comes. How many
frames past the confirmed inputs a frame is first played is its **depth**; the recordings
keep it per frame per player (PRED's lag, docs/REPLAY_FORMAT_V2.md §4).

What the connection fixes is the two players' depths ADDED together. Which of them carries
it is not fixed, and GGPO's time sync does not hold it steady: it estimates the remote
frame as the last input received plus half the round trip
(`UdpProtocol::SetLocalFrameNumber`), which is wrong on a route slower one way, and it
hunts. So a match has spells where one player predicts 2, 3 or 4 frames deep while the
other predicts none.

106 recorded matches (2026-09 to 2026-10, before any of this), frame by frame, the pair of
depths as higher/lower:

| input delay, ping | commonest states | two or more apart |
|---|---|---|
| 1, under 60 ms | 1/0 61%, 1/1 18%, 2/0 7% | 14% of frames |
| 2, 60-130 ms | 1/0 47%, 0/0 18%, 1/1 18%, 2/1 7%, 2/0 6% | 10% |
| 3, 130-220 ms | 3/2 31%, 2/2 21%, 1/1 16%, 2/1 12%, 3/1 10% | 20% |

Every one of those matches has 5 to 10 spells where the two players' 90-frame averages
are more than 1.5 frames apart, 10-25% of its length, and the states then are 2/0, 3/0,
4/0, 3/1, 4/2: the deep rollbacks.

## What can be fixed, and what cannot

**Depth is whole frames.** A link short by less than one frame in all can put that on one
player (1/0) or split it -- but half a frame each rounds up to a whole frame each (1/1).
So 1/0 is the LEAST predicting such a link allows, and evening it out adds prediction.

A gap of two frames or more is different: 2/0 becomes 1/1 and 3/0 becomes 2/1 with the
same total, and the deepest rollbacks -- the ones seen as ghosting -- go.

So the rule is: **close a gap of two frames or more; never touch a gap of one.**

## The design

Fightcade's "rift balancing" rules, on a signal of our own.

Fightcade (`fightcade-snes9x`, `QuarkIncrementFrame` in `src/win32/ggpo/ggpo.cpp`) ignores
GGPO's time sync events, keeps a 90-frame rolling average of the difference between the
two peers' "frames behind" figures, halves it, and acts only when that is over a whole
frame -- the two more than two frames apart -- and at most once a second. The peer ahead
delays its next frame; the peer behind runs frames unshown.

Its signal is the time sync's own estimate, so it shares the half-round-trip blind spot.
Ours is each peer's measured depth, which does not.

In GGPO-X (`lib/ggpo/prediction_balance.{h,cpp}`, `backends/p2p.cpp`, `network/udp_*`):

1. **Measure.** `Peer2PeerBackend::SyncInput` records the depth of every frame's first
   play (frame - last confirmed frame; not during a rollback).
2. **Exchange.** The quality report, sent three times a second, carries the average over
   the last 90 frames and the frame that window ends at (`prediction_frame`,
   `prediction_depth` -- two new fields, so both peers must be built alike).
3. **Compare like with like.** A peer compares the remote average with its OWN average
   over the SAME 90 frames, waiting until it has played them if the other is ahead.
4. **Act only on a real gap.** When the local peer is deeper by 1.5 frames or more
   (`THRESHOLD`: a 2/0 spell averages just under 2), it raises
   `GGPO_EVENTCODE_PREDICTION_BALANCE`: wait a quarter of the gap, spread over 50 frames.
   A wait of w frames takes w off its depth and adds w to the other's, so the gap halves.
   The other peer does nothing.
5. **Then wait for a clean window.** No new recommendation until a window made only of
   frames played after that wait: a correction shows in the average only once the window
   has moved past it.

In the game (`src/netplay_match.c`, `NETPLAY_EVENT_BALANCE`): the wait, by the same
frame-limiter delay the time sync's waits use (`Platform_DelayNextFrame`). Wall time
only -- the simulation, the rollback state and the recordings never see it.

**The time sync is left exactly as it was.** Two other arrangements were tried and
measured worse: holding the time sync's small corrections back after a balance wait
(it also froze the standing slowdown the time sync had learned), and feeding the gap into
the time sync's own figure as a standing offset (that figure is acted on in full every
120 frames and answers slowly: it ran away).

**Switches.** `GGPO_PREDICTION_BALANCE` (default 1; 0 builds the library and its packets as
they were), `ggpo_set_prediction_balance(session, enabled)` at run time, and in the game
`[netplay] balance = true|false` (default false). A peer with it off sends no depth, so it is off
for both: the two players must both set it.

## Measured

Emulated links (`SWOS_NETEM_DELAY_MS` delays a process's OUTGOING packets;
`SWOS_NETEM_LINK=1` agrees the input delay from the emulated ping), 4,000 frames, both
players chasing the ball, depths from the host's `.SRP` from frame 1000. Three runs a
cell. "Total": the two players' average depth, added. "Deep": frames with the two players
two or more apart.

| link (delay on outgoing packets) | balance off: total / deep | balance on: total / deep |
|---|---|---|
| host 40 ms | 1.04/0% 1.04/0% 1.04/0% | 1.05/0% 1.01/0% 1.06/0% |
| host 70 ms | 1.00/0% 1.11/8% 1.00/0% | 1.00/0% 1.04/4% 1.00/0% |
| host 130 ms | 2.00/100% 2.00/100% 2.00/100% | 2.00/3% 2.00/0% 2.00/0% |
| host 160 ms | 3.16/16% 3.03/0% 3.16/15% | 3.00/0% 3.02/0% 3.15/0% |
| both 30 ms | 1.00/0% 1.00/0% 1.00/0% | 1.00/0% 1.00/0% 1.00/0% |
| both 50 ms | 3.00/0% 3.00/0% 3.00/0% | 3.00/0% 3.00/0% 3.00/0% |
| **all 18 runs** | **1.87 / 18.8%**, depth 3+ on 1.9% | **1.85 / 0.4%**, depth 3+ on 0.0% |

It never raises the total, and it leaves a link that is already level alone (0 to 2 waits
a match). What it changes is the 130 ms link, which sits at 2/0 all match without it and
at 1/1 with it, and the short deep spells at 160 ms. The joiner's clock 2% slow
(`SWOS_LIVE_SLOW_PCT=2`): 159 + 312 frames re-simulated without, 136 + 280 with.

A single run means little: with balance off the same link gives a clean match one run and
a one-sided one the next. Judge a change on several runs a link.

```bash
# one link, balance on; SWOS_LIVE_BALANCE=0 in both for off
RECORD=1 RELAY=0 MATCHES=1 FRAMES=4000 \
  HOST_ENV="SWOS_NETEM_DELAY_MS=70 SWOS_NETEM_LINK=1 SWOS_LIVE_CHASE_INPUT=1 SWOS_LIVE_BALANCE=1 SWOS_LIVE_PACING_LOG=1" \
  JOIN_ENV="SWOS_LIVE_CHASE_INPUT=2 SWOS_LIVE_BALANCE=1 SWOS_LIVE_PACING_LOG=1" scripts/relay_test.sh
```

`SWOS_LIVE_BALANCE=1` (or `=0`) on both sides sets it for a test whatever `SWOS.ini` says.
`SWOS_LIVE_PACING_LOG=1` logs every time sync figure and every balance wait. At the end of
every match `SWOS.log` has a `[balance]` line: how many times this player was the deeper
one and how long it waited in all.

## The bug underneath: the time sync was steering on noise

GGPO's time sync recommends a wait from two figures: this peer's own estimate of how far
ahead the other is, and the other peer's estimate, sent in the quality report
(`frame_advantage`). In every OpenSWOS build up to 1340705 the second was garbage.

Our portability work on the fork replaced `<windows.h>`'s `min` with a template in
`platform_linux.h` whose return type was a bare `decltype(a < b ? a : b)` -- a REFERENCE,
when both arguments have the same type, to one of the function's own parameters. Its one
caller is the line that fills in `frame_advantage`. Compiled, that line read four bytes
of an unrelated pointer as a float: nearly always 0, sometimes not, and different on
every compiler and platform. (Upstream on Windows uses the real macro and is right.)

So the time sync was acting on its own estimate plus noise, and the pacing in
`src/netplay_match.c` (how hard a figure is acted on, the learned slowdown) was tuned
against exactly that. Two consequences:

- **The noise tipped matches over.** With the garbage, the same emulated link gave a level
  match one run and a one-sided one the next: over 15 runs, the two players were two or
  more frames apart on 27% of frames. With a plain 0 sent instead: 12% over 10 runs, and
  19% over the 18 above -- nearly all of it the 130 ms link's steady 2/0.
- **The true figure cannot simply be switched on.** With `frame_advantage` sent as GGPO
  means it, the same pacing over-corrects: the peers swing five frames apart and stay
  there (total 4.5 to 5.4 on every link, balance on or off).

What is built: the template is fixed (`std::decay`), and whether the figure is sent is
now a named option of the fork, `GGPO_SEND_FRAME_ADVANTAGE` -- 1, GGPO's own behaviour, by
default; **OpenSWOS builds with 0** (`GGPO_CXXFLAGS` in the Makefile), which is what
it always did in effect, without the noise. Sending the true figure and retuning the
pacing for it is the proper end of this, and its own piece of work.

## The first attempt (2026-10-05, build 1340705), and what it taught

It evened the two players out at any gap over 0.6 of a frame. On real matches that turned
the commonest state, 1/0, into 1/1: at input delay 2 the total went from 1.32 to 1.94 and
the pair's rollbacks from 93 to 163 per 1000 frames -- "it worsened everything that
worked fine before". It was tested on one emulated link, short by two frames, the one
case where evening out is free. Hence the rule above, and the matrix.

## Still to do

- A match between two real machines, and the testers' word.
- The time sync's own hunting is what makes the deep spells; the balance only shortens
  them. Fixing the estimate (the half-round-trip assumption, how hard the game acts on
  its figure) is the larger job, and one for the GGPO-X side.
- Send the true `frame_advantage` (`GGPO_SEND_FRAME_ADVANTAGE=1`) and retune the pacing
  for it: the time sync would then have both of its inputs. With it sent, the fork also
  converts it through a signed int -- upstream casts a negative float straight to
  `uint8`, which is undefined (x86 wraps and the `int8` field reads back right; not
  checked elsewhere).

Before shipping any change here: the matrix above, `scripts/netplay_test.sh --all`,
`netplay_lobby_test.sh`, `RECORD=1 scripts/relay_test.sh` (both peers' `.SRP`
byte-identical, resims pass).
