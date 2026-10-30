#!/usr/bin/env bash
#
# matchmaker_pair_test.sh — two clients meet through the matchmaking server.
#
#   scripts/matchmaker_pair_test.sh
#
# One process hosts a game, the other joins it, the host approves, and BOTH must
# receive START carrying the other's addresses. That handoff is what turns
# matchmaking into a match (docs/NETPLAY.md section 7), and it is the one part of
# the protocol a single client cannot test at all — which is why it gets its own
# script rather than living in --matchmaker-selftest.
#
# The server is started here if it is not already running, and stopped again only
# if we were the ones who started it.
set -uo pipefail
export SWOS_WORLD="${SWOS_WORLD:-9697}"   # (its corpus / careers are 96/97's; World 2026-27 is the game's default)

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

BIN="${BIN:-bin/SWOS.x86_64}"
PORT="${PORT:-19602}"
OUT="${TMPDIR:-/tmp}/swos-mm-pair.$$"
SERVER_PID=""

cleanup() {
    [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null
    rm -f "$OUT.host" "$OUT.join"
}
trap cleanup EXIT INT TERM

[ -x "$BIN" ] || { echo "pair-test: $BIN not built" >&2; exit 1; }
# The server this script starts, by name: the game's own default is the public server,
# and a matchmaker in the player's SWOS.ini would win over a default anyway.
export SWOS_MM_HOST=127.0.0.1 SWOS_MM_PORT="$PORT"

if command -v ss >/dev/null && ss -ulnp 2>/dev/null | grep -q ":$PORT "; then
    echo "-- using the matchmaker already running on $PORT"
else
    echo "-- starting the matchmaker on $PORT"
    scripts/run_matchmaker.sh --port "$PORT" >"$OUT.server" 2>&1 &
    SERVER_PID=$!
    # Wait for the socket rather than sleeping a guessed interval.
    for _ in $(seq 1 50); do
        ss -ulnp 2>/dev/null | grep -q ":$PORT " && break
        sleep 0.1
    done
    ss -ulnp 2>/dev/null | grep -q ":$PORT " || {
        echo "pair-test: server did not come up; log:" >&2
        cat "$OUT.server" >&2
        exit 1
    }
fi

# The host has to be listening before the joiner looks for its game; both sides
# wait up to 60s for the other, so the order only affects how long it takes.
echo "-- host"
"$BIN" --matchmaker-pair=host >"$OUT.host" 2>&1 &
HOST_PID=$!
sleep 1
echo "-- join"
"$BIN" --matchmaker-pair=join >"$OUT.join" 2>&1 &
JOIN_PID=$!

wait "$HOST_PID"; HOST_RC=$?
wait "$JOIN_PID"; JOIN_RC=$?

echo
echo "================ host ================"
grep -vE "^\[settings\]" "$OUT.host"
echo "================ join ================"
grep -vE "^\[settings\]" "$OUT.join"
echo "======================================"

# Each side reports the OTHER's name, so a crossed-wires bug (both told the same
# peer, or told themselves) shows up here rather than in a match that desyncs.
HOST_PEER=$(grep -oE "peer '[^']*'" "$OUT.host" | head -1)
JOIN_PEER=$(grep -oE "peer '[^']*'" "$OUT.join" | head -1)
echo "host was told:  $HOST_PEER"
echo "join was told:  $JOIN_PEER"

if [ "$HOST_RC" = 0 ] && [ "$JOIN_RC" = 0 ]; then
    echo "pair-test: PASSED"
    exit 0
fi
echo "pair-test: FAILED (host rc=$HOST_RC join rc=$JOIN_RC)"
exit 1
