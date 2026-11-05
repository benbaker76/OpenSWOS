#!/usr/bin/env bash
#
# netplay_live_test.sh — two processes play a LIVE match against each other.
#
#   scripts/netplay_live_test.sh
#   PROFILE=wifi scripts/netplay_live_test.sh          # over an emulated wifi link
#   JOIN_PROFILE=awful scripts/netplay_live_test.sh    # one player on a bad line
#   JOIN_ENV="SWOS_LIVE_DESYNC_FRAME=200" scripts/netplay_live_test.sh   # desync on purpose
#
# The end-to-end check for what the multiplayer menus drive: p2p_link connects,
# the input delay is AGREED, the lobby sets the match up (each player picks a team
# and a tactic, the host the rules, both ready, the host starts and the joiner
# confirms), and both open a GGPO session and play.
#
# The network between them is src/netplay_netem.c. PROFILE names a link
# (lan adsl wifi mobile satellite awful); HOST_PROFILE / JOIN_PROFILE give one
# peer its own, which is how one player on a bad line is reproduced. Any
# SWOS_NETEM_* set in the environment is passed through and overrides the
# profile — except the SEED, which is per peer on purpose: two peers on one
# machine sharing a seed would lose the same packets at the same moment, which
# no real pair of machines does.
#
# SWOS_NO_LOG=1 is not optional. Without it both processes redirect stderr into
# the SAME ~/.swos/SWOS.log and clobber each other, and the interesting output
# is the part that disappears.
set -uo pipefail
export SWOS_WORLD="${SWOS_WORLD:-9697}"   # (its corpus / careers are 96/97's, whatever SWOS.ini says)

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

BIN="${BIN:-SWOS.x86_64}"
OUT="${TMPDIR:-/tmp}/swos-live.$$"
WAIT="${WAIT:-420}"

[ -x "bin/$BIN" ] || { echo "live-test: bin/$BIN not built" >&2; exit 1; }
# Keep the per-side logs on failure -- they are the only record of where a
# stalled run stopped, and deleting them was how the first stall got away.
cleanup() { kill -9 $(jobs -p) 2>/dev/null; }
trap cleanup EXIT INT TERM

# FRAMES bounds the run: a full match is thousands of frames of wall clock, and
# what this test needs is "did the two peers stay identical", which a few
# thousand frames answers. Both peers get the same limit, so both stop together.
FRAMES="${FRAMES:-3000}"

# The conditions both peers get. DELAY_MS/JITTER_MS/LOSS_PCT are kept as
# shorthands for the three knobs that came first; every other SWOS_NETEM_* is
# inherited from the caller's environment untouched.
#
# Only a knob the caller actually gave is exported: setting them all to a
# default of 0 here would silently beat a PROFILE, since the environment is what
# overrides a profile.
[ -n "${DELAY_MS:-}" ]  && export SWOS_NETEM_DELAY_MS="$DELAY_MS"
[ -n "${JITTER_MS:-}" ] && export SWOS_NETEM_JITTER_MS="$JITTER_MS"
[ -n "${LOSS_PCT:-}" ]  && export SWOS_NETEM_LOSS_PCT="$LOSS_PCT"
[ -n "${PROFILE:-}" ]   && export SWOS_NETEM_PROFILE="$PROFILE"

# Called as `run ... &`, so it already runs in a subshell of its own; the cd and
# export stay in it. It EXECs the game, so the job's pid IS the game's: the
# watchdog and the kill -9 below reach the peer itself. With a subshell around
# the binary they killed only the subshell, and a hung peer outlived the script.
run() {   # role, netem-seed-salt, logfile
    local prof extra
    if [ "$1" = host ]; then prof=${HOST_PROFILE:-}; extra=${HOST_ENV:-};
    else                    prof=${JOIN_PROFILE:-}; extra=${JOIN_ENV:-}; fi
    exec > "$3" 2>&1          # before the cd, so a relative TMPDIR still lands where it says
    cd bin || exit 1
    [ -n "$prof" ] && export SWOS_NETEM_PROFILE="$prof"
    # HOST_ENV / JOIN_ENV put variables on ONE side, which is the only way to ask for
    # something asymmetric -- a slow clock, or a peer told to go out of step on purpose.
    exec env SWOS_NO_LOG=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        SWOS_LIVE_FRAMES="$FRAMES" SWOS_NETEM_SEED="$2$$" $extra \
        ./"$BIN" --netplay-live-pair="$1"
}

echo "-- host"; run host 11 "$OUT.host" & HOST=$!
sleep 2
echo "-- join"; run join 22 "$OUT.join" & JOIN=$!

# Bounded wait: a hung peer must not hang the suite.
#
# The sleep runs as its own job so the watchdog can take it down when it is
# cancelled. `( sleep; kill ) &` cannot: killing that subshell orphans the sleep,
# which lives on for the full $WAIT holding the script's stdout -- so
# `netplay_live_test.sh | tail` took 6m45 for a 22-second run.
( sleep "$WAIT" & nap=$!
  trap 'kill $nap 2>/dev/null; exit 0' TERM
  wait $nap && kill "$HOST" "$JOIN" 2>/dev/null ) & WATCH=$!
wait "$HOST"; HRC=$?
wait "$JOIN"; JRC=$?
kill "$WATCH" 2>/dev/null
kill -9 "$HOST" "$JOIN" 2>/dev/null   # a peer that ignored SIGTERM must not keep the port

for side in host join; do
    echo "================ $side ================"
    grep -E "^\[live\] (host|join|stopping|final)|^  (ok|FAIL|\.\.)|^\[netplay\]" "$OUT.$side" \
        || tail -5 "$OUT.$side"
done

# The per-frame hashes are INFORMATIONAL ONLY and are expected to differ once
# rollbacks start.
#
# Each is sampled on a frame's FIRST play, which under rollback may be a
# predicted state that is re-simulated moments later -- so the two peers are
# sampled at different points in their own rollback cycles and have no reason to
# match. Treating a difference here as a desync would be crying wolf.
#
# The real checks are GGPO's confirmed-frame checksums (it aborts the match on a
# mismatch, which is how the frame-0 desync was caught) and the FINAL state
# below, taken when every frame is confirmed.
echo "---- per-frame hashes (informational: first-play, not confirmed) ----"
join -j1 \
    <(grep -oE "^\[live\] frame [0-9]+ +hash [0-9a-f]+" "$OUT.host" | awk '{print $3, $5}' | sort -k1,1) \
    <(grep -oE "^\[live\] frame [0-9]+ +hash [0-9a-f]+" "$OUT.join" | awk '{print $3, $5}' | sort -k1,1) \
    2>/dev/null | awk '{ if ($2 != $3) bad++; else same++ }
        END { printf "  %d sampled, %d matched, %d differed (differences here are expected under rollback)\n", same+bad, same, bad }'
echo "======================================"

# What the lobby set up, as each peer is about to play it: the two teams and their
# tactics, the USER tactic that had to move to slot B, the host's rules. Both peers
# must print the same line -- the final hash would catch a difference eventually, but
# this names it. (Each peer also checks its own side is the team it picked.)
hm=$(grep -m1 "^\[live\] match:" "$OUT.host")
jm=$(grep -m1 "^\[live\] match:" "$OUT.join")
echo "host ${hm:-[live] match: none}"
echo "join ${jm:-[live] match: none}"

hh=$(grep -oE "final state hash [0-9a-f]+" "$OUT.host" | awk '{print $4}')
jh=$(grep -oE "final state hash [0-9a-f]+" "$OUT.join" | awk '{print $4}')
echo "host hash: ${hh:-none}"
echo "join hash: ${jh:-none}"

if [ "$HRC" = 0 ] && [ "$JRC" = 0 ] && [ -n "$hm" ] && [ "$hm" = "$jm" ] &&
   [ -n "$hh" ] && [ "$hh" = "$jh" ]; then
    echo "live-test: PASSED — both peers played the same match"
    exit 0
fi
echo "live-test: FAILED (host rc=$HRC join rc=$JRC)"
exit 1
