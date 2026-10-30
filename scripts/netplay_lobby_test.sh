#!/usr/bin/env bash
#
# netplay_lobby_test.sh — two players meet in the lobby MENU and play matches in a row.
#
#   scripts/netplay_lobby_test.sh                 # 2 matches, 300 frames each
#   MATCHES=3 FRAMES=500 scripts/netplay_lobby_test.sh
#   PROFILE=wifi scripts/netplay_lobby_test.sh    # over an emulated wifi link
#   CAPTURE=25 scripts/netplay_lobby_test.sh      # the host's screen every 25 frames
#   CAPTURE=10 CAPTURE_FROM=2000 ...              # ... from its 2000th frame on
#   JOIN_ENV="SWOS_LIVE_SLOW_PCT=2" ...           # extra environment for one side:
#                                                 # here a joiner whose clock runs 2% slow
#   HOST_WIDTH=336 JOIN_WIDTH=484 ...             # a 4:3 and a 16:9 player: each side gets
#                                                 # a HOME of its own whose SWOS.ini sets
#                                                 # [video] resolution=<W>x272
#
# netplay_live_test.sh drives the lobby through its API and skips the front end. This
# one goes through what a player goes through: --netplay-lobby opens the real lobby
# menu on each side, the test knobs press what a player would (SWOS_LOBBY_TEAM, _READY,
# _START), the match is played, the players come back to the lobby, reconnect, and play
# again (docs/NETPLAY.md, "Rematch"). Kick-off desyncs that only a trip through the
# menus causes are the ones this finds (docs/NETPLAY.md, "The lobby").
#
# Each side's input comes from its own generator (SWOS_LIVE_RANDOM_INPUT), so both
# predict wrong and roll back, as against a real player.
#
# Passes when both peers played MATCHES matches, every one ended normally, and each
# match ended on the same SIM-tier state hash on both sides.
set -uo pipefail
export SWOS_WORLD="${SWOS_WORLD:-9697}"   # (its corpus / careers are 96/97's; World 2026-27 is the game's default)

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

BIN="${BIN:-SWOS.x86_64}"
MATCHES="${MATCHES:-2}"
FRAMES="${FRAMES:-300}"
WAIT="${WAIT:-$((120 + MATCHES * 60))}"
OUT="${OUT:-${TMPDIR:-/tmp}/swos-lobby.$$}"
mkdir -p "$OUT"

[ -x "bin/$BIN" ] || { echo "lobby-test: bin/$BIN not built" >&2; exit 1; }

# SWOS_NO_LOG=1: both processes would otherwise share one SWOS.log (netplay_live_test.sh).
# The host captures into $OUT so the two processes, which share bin/ as their working
# directory, never write the same files.
run() {   # role, team, extra env...
    local role=$1 team=$2; shift 2
    cd bin || exit 1
    env SWOS_NO_LOG=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        SWOS_LOBBY_TEAM="$team" SWOS_LOBBY_READY=1 SWOS_LOBBY_MATCHES="$MATCHES" \
        SWOS_LIVE_FRAMES="$FRAMES" ${PROFILE:+SWOS_NETEM_PROFILE=$PROFILE} "$@" \
        timeout -s KILL "$WAIT" ./"$BIN" --netplay-lobby="$role"
}

# A screen width per side: a copy of this user's SWOS.ini (or none) with the resolution
# replaced, in a HOME of the side's own. Saves and settings then land there too.
side_home() {   # role width -> prints "HOME=<dir>", or nothing for the user's own
    local dir=$OUT/home-$1
    [ -n "$2" ] || return 0
    mkdir -p "$dir/.swos"
    if [ -f "$HOME/.swos/SWOS.ini" ]; then
        sed "s/^resolution=.*/resolution=$2x272/" "$HOME/.swos/SWOS.ini" > "$dir/.swos/SWOS.ini"
    else
        printf '[video]\nresolution=%sx272\n' "$2" > "$dir/.swos/SWOS.ini"
    fi
    grep -q "^resolution=$2x272" "$dir/.swos/SWOS.ini" ||
        printf '[video]\nresolution=%sx272\n' "$2" >> "$dir/.swos/SWOS.ini"
    echo "HOME=$dir"
}
HOST_HOME=$(side_home host "${HOST_WIDTH:-}")
JOIN_HOME=$(side_home join "${JOIN_WIDTH:-}")

rm -f bin/swos_present*.png
( run host 50:0B SWOS_LOBBY_START=1 SWOS_NETEM_SEED=11 SWOS_LIVE_RANDOM_INPUT=0xA5A5F00D \
    ${CAPTURE:+SWOS_PRESENT_CAPTURE=$CAPTURE} ${CAPTURE_FROM:+SWOS_PRESENT_CAPTURE_FROM=$CAPTURE_FROM} \
    ${HOST_HOME} ${HOST_ENV:-} > "$OUT/host.log" 2>&1; echo "[lobby-test] exit $?" >> "$OUT/host.log" ) &
sleep 1
( run join 52:02 SWOS_NETEM_SEED=22 SWOS_LIVE_RANDOM_INPUT=0x5A5A0FF0 ${JOIN_HOME} ${JOIN_ENV:-} > "$OUT/join.log" 2>&1; echo "[lobby-test] exit $?" >> "$OUT/join.log" ) &
wait
[ -n "${CAPTURE:-}" ] && mv bin/swos_present*.png "$OUT"/ 2>/dev/null

fail=0
for side in host join; do
    echo "================ $side ================"
    grep -E "^\[lobby\]|^\[lobby-test\]|^\[netplay\] (live match|desync|the peer|network|nothing|simulation draw width)|^\[live\] final sim|^\[video\] render" \
        "$OUT/$side.log"
    grep -q '^\[lobby-test\] exit 0$' "$OUT/$side.log" || fail=1
done

# The SIM tier: what peers must agree on. (The whole state differs between players on
# different screen widths -- the highlight ring holds drawn coordinates.)
hh=$(grep -oE "final sim hash [0-9a-f]+" "$OUT/host.log" | awk '{print $4}' | paste -sd' ')
jh=$(grep -oE "final sim hash [0-9a-f]+" "$OUT/join.log" | awk '{print $4}' | paste -sd' ')
hn=$(grep -c "live match finished" "$OUT/host.log")
jn=$(grep -c "live match finished" "$OUT/join.log")
echo "host: $hn finished, hashes $hh"
echo "join: $jn finished, hashes $jh"

if [ "$fail" = 0 ] && [ "$hn" = "$MATCHES" ] && [ "$jn" = "$MATCHES" ] && [ -n "$hh" ] && [ "$hh" = "$jh" ]; then
    echo "lobby-test: PASSED — $MATCHES matches through the lobby, both peers identical (logs in $OUT)"
    exit 0
fi
echo "lobby-test: FAILED (logs in $OUT)"
exit 1
