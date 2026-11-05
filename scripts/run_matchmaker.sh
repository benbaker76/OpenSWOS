#!/usr/bin/env bash
#
# run_matchmaker.sh — build and run the local matchmaking server.
#
#   scripts/run_matchmaker.sh              # build if needed, run on 19602
#   scripts/run_matchmaker.sh --port 20000 # a different port
#   scripts/run_matchmaker.sh --rebuild    # force a reconfigure + rebuild
#   scripts/run_matchmaker.sh --build-only # build, don't run
#   scripts/run_matchmaker.sh --build-only --with-playback   # and the recordings player
#   scripts/run_matchmaker.sh -- --relay --websocket   # anything after -- goes to the server
#
# A public server runs under systemd with its full argument list, including wss://'s
# certificate: docs/NETPLAY_SETUP.md §4 and §10. This script is for building it and
# for local testing.
#
# This is the server SWOS 2020's online play talks to — SimpleMatchmaker,
# vendored at third_party/matchmaker (see its OPENSWOS.md). Running it here
# means the whole online flow can be exercised without any public server:
#
#   MULTIPLAYER -> log in -> lobby -> create/join a game -> START: -> P2P match
#
# WHAT THE SERVER DOES AND DOES NOT DO
#
# It is a MATCHMAKER, not a game server. It introduces two players and then gets
# out of the way: its START: message hands each side the other's candidate
# addresses, and the match itself runs peer-to-peer over GGPO (docs/NETPLAY.md).
# So a match keeps running if the server dies mid-game; you just cannot start a
# new one.
#
# PORT 19602, NOT 19601
#
# Upstream's README says 19601 and its main.cpp hardcoded 19602. We kept 19602
# as the default (so a stock swos-2020 client still finds us) and made the
# argument work as the README always claimed. The game defaults to the same.
#
# It is UDP. `ss -ulnp | grep 19602` shows it listening. TCP on the same number
# exists only with --websocket or --record, so without them "connection refused"
# from a TCP tool means nothing here.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

SRC_DIR="third_party/matchmaker"
BUILD_DIR="build/matchmaker"
BIN="$BUILD_DIR/swos-matchmaker"
PORT=19602
REBUILD=0
BUILD_ONLY=0
WITH_PLAYBACK=0
SERVER_ARGS=()

usage() {
    awk 'NR >= 3 { if ($0 !~ /^#/) exit; sub(/^# ?/, ""); print }' "${BASH_SOURCE[0]}"
    cat <<EOF

Options:
      --port N      listen on port N (default $PORT)
      --rebuild     wipe $BUILD_DIR and reconfigure first
      --build-only  build and exit without running
      --with-playback  also build the recordings player (scripts/build_playback.sh:
                    needs emsdk; skipped with a warning without it)
      -- ARGS...    pass ARGS to the server (--relay, --websocket, --tls-cert ...;
                    see: $BIN --help)
  -h, --help        this text
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --port)       PORT="$2"; shift 2 ;;
        --rebuild)    REBUILD=1; shift ;;
        --build-only) BUILD_ONLY=1; shift ;;
        --with-playback) WITH_PLAYBACK=1; shift ;;
        -h|--help)    usage; exit 0 ;;
        --)           shift; SERVER_ARGS=("$@"); break ;;
        *) echo "run_matchmaker: unknown option '$1' (try --help)" >&2; exit 2 ;;
    esac
done

command -v cmake >/dev/null || { echo "run_matchmaker: cmake not found" >&2; exit 1; }

[ "$REBUILD" = 1 ] && rm -rf "$BUILD_DIR"

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ] || [ "$REBUILD" = 1 ]; then
    cmake -S "$SRC_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release >/dev/null ||
        { echo "run_matchmaker: cmake configure failed" >&2; exit 1; }
fi
# Every time, incrementally (nothing to do: a moment): a server built before its sources
# changed -- the team list (TeamRegistry.inc) above all -- answered with the old ones.
before=$(stat -c %Y "$BIN" 2>/dev/null || echo 0)
cmake --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || echo 4)" >/dev/null ||
    { echo "run_matchmaker: build failed" >&2; exit 1; }
[ "$(stat -c %Y "$BIN" 2>/dev/null || echo 0)" != "$before" ] && echo "-- built $BIN ($(ls -lh "$BIN" | awk '{print $5}'))"

if [ "$WITH_PLAYBACK" = 1 ]; then
    scripts/build_playback.sh
    rc=$?
    [ "$rc" = 3 ] && echo "-- the recordings player was not built (no emsdk)"
    [ "$rc" = 1 ] && exit 1
fi

[ "$BUILD_ONLY" = 1 ] && { echo "-- --build-only: not running"; exit 0; }

# A second instance binds the same UDP port without an error on some systems and
# then silently receives none of the traffic, which looks like "the server is
# up but ignores me". Say so instead.
if command -v ss >/dev/null && ss -ulnp 2>/dev/null | grep -q ":$PORT "; then
    echo "run_matchmaker: something is already listening on UDP $PORT:" >&2
    ss -ulnp 2>/dev/null | grep ":$PORT " >&2
    echo "run_matchmaker: stop it first, or use --port" >&2
    exit 1
fi

echo "== running on UDP $PORT${SERVER_ARGS[*]:+ with ${SERVER_ARGS[*]}} (ctrl-c to stop)"
exec "$BIN" "$PORT" "${SERVER_ARGS[@]}"
