#!/usr/bin/env bash
#
# matchmaker_interop_test.sh — our client against the REFERENCE client.
#
#   scripts/matchmaker_interop_test.sh
#
# scripts/matchmaker_pair_test.sh runs two copies of src/mm_client.c, so it can
# only prove that our client agrees with ITSELF. Two implementations of one
# protocol can share a misreading and nothing notices. This pairs our client
# with upstream's C++ ServerConnection — the code SWOS 2020 actually ships —
# built as tools/mm_refpeer, BOTH WAYS ROUND:
#
#   round 1   reference hosts, ours joins
#   round 2   ours hosts, the reference joins
#
# so every message each side sends is parsed by the other implementation. Both
# sides assert the same three things (player number, peer name, at least one
# address), and both must pass.
#
# Round 3 is the open-game data blob: the reference client's CreateGame sends
# "<min>,<max>," + a payload, and the server has to accept it. It is run last
# because it is the one thing the two halves of upstream disagree about.
set -uo pipefail
export SWOS_WORLD="${SWOS_WORLD:-9697}"   # (its corpus / careers are 96/97's; World 2026-27 is the game's default)

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

BIN="${BIN:-bin/SWOS.x86_64}"
REF="${REF:-build/matchmaker/mm-refpeer}"
PORT="${PORT:-19604}"          # not 19602: a server left running from another test would be a different build
OUT="${TMPDIR:-/tmp}/swos-mm-interop.$$"
SERVER_PID=""
fail=0

cleanup() {
    [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null
    rm -f "$OUT".*
}
trap cleanup EXIT INT TERM

[ -x "$BIN" ] || { echo "interop: $BIN not built -- make X86_64=1" >&2; exit 1; }
# The server this script starts, by name: the game's own default is the public server.
export SWOS_MM_HOST=127.0.0.1
if [ ! -x "$REF" ]; then
    echo "-- building the reference peer"
    cmake -S third_party/matchmaker -B build/matchmaker -DCMAKE_BUILD_TYPE=Release >/dev/null &&
    cmake --build build/matchmaker -j"$(nproc 2>/dev/null || echo 4)" >/dev/null ||
        { echo "interop: could not build $REF" >&2; exit 1; }
fi

echo "-- starting the matchmaker on $PORT"
scripts/run_matchmaker.sh --port "$PORT" >"$OUT.server" 2>&1 &
SERVER_PID=$!
for _ in $(seq 1 50); do
    ss -ulnp 2>/dev/null | grep -q ":$PORT " && break
    sleep 0.1
done
ss -ulnp 2>/dev/null | grep -q ":$PORT " || {
    echo "interop: the server did not come up; log:" >&2; cat "$OUT.server" >&2; exit 1; }

# round <n> <description> <host-cmd...> -- <join-cmd...>
round() {
    local n=$1 what=$2; shift 2
    local hostcmd=() joincmd=() seen=0 a
    for a in "$@"; do
        if [ "$a" = "--" ]; then seen=1; continue; fi
        if [ "$seen" = 0 ]; then hostcmd+=("$a"); else joincmd+=("$a"); fi
    done

    echo
    echo "======== round $n: $what ========"
    "${hostcmd[@]}" >"$OUT.h$n" 2>&1 & local hp=$!
    sleep 1                                   # the host must be listed before the joiner looks
    "${joincmd[@]}" >"$OUT.j$n" 2>&1 & local jp=$!
    wait "$hp"; local hrc=$?
    wait "$jp"; local jrc=$?

    sed 's/^/  host| /' "$OUT.h$n" | grep -vE "\[settings\]|^\s*host\| \[IP"
    sed 's/^/  join| /' "$OUT.j$n" | grep -vE "\[settings\]|^\s*join\| \[IP"
    if [ "$hrc" = 0 ] && [ "$jrc" = 0 ]; then
        echo "  round $n: PASSED"
        return 0
    fi
    echo "  round $n: FAILED (host rc=$hrc join rc=$jrc)"
    return 1
}

# The reference client says OUR tag: the server lists a game only to players with the
# same one (Connections.cpp, PlayKey), and this test is about the messages, not about
# SWOS 2020 and OpenSWOS meeting. NETPLAY_COMPAT from include/netplay_compat.h.
COMPAT=$(sed -n 's/^#define NETPLAY_COMPAT \([0-9]*\).*/\1/p' include/netplay_compat.h)
[ -n "$COMPAT" ] || { echo "no NETPLAY_COMPAT in include/netplay_compat.h" >&2; exit 1; }

round 1 "the reference hosts, ours joins" \
    "$REF" --game-id "OPENSWOS/$COMPAT" --role host --host 127.0.0.1 --port "$PORT" -- \
    env SWOS_MM_PORT="$PORT" "$BIN" --matchmaker-pair=join || fail=1

round 2 "ours hosts, the reference joins" \
    env SWOS_MM_PORT="$PORT" "$BIN" --matchmaker-pair=host -- \
    "$REF" --game-id "OPENSWOS/$COMPAT" --role join --host 127.0.0.1 --port "$PORT" || fail=1

round 3 "the reference hosts WITH open-game data, ours joins" \
    "$REF" --game-id "OPENSWOS/$COMPAT" --role host --host 127.0.0.1 --port "$PORT" --blob 16 -- \
    env SWOS_MM_PORT="$PORT" "$BIN" --matchmaker-pair=join || fail=1

echo
if [ "$fail" = 0 ]; then echo "interop: PASSED"; exit 0; fi
echo "interop: FAILED"; exit 1
