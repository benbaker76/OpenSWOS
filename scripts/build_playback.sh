#!/bin/bash
# Build the recordings player -- the browser build of SWOS as a .SRP player with a scrub
# bar, which the server publishes at https://<server>/playback/ (docs/PLAYBACK.md).
#
#   scripts/build_playback.sh           # -> dist/playback (index.html, SWOS.html/.js/.wasm/.data)
#
# Needs emsdk: EMSDK_DIR (default ~/emsdk), or emcc already on PATH. Without it this
# exits 3 -- "skipped", not "failed" -- so a server without emsdk still builds and
# installs everything else (scripts/run_matchmaker.sh --with-playback,
# scripts/swos-matchmaker-service.sh).
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

if ! command -v emcc >/dev/null 2>&1; then
    EMSDK_DIR="${EMSDK_DIR:-$HOME/emsdk}"
    if [ ! -f "$EMSDK_DIR/emsdk_env.sh" ]; then
        echo "build_playback: no emsdk (looked for $EMSDK_DIR/emsdk_env.sh; set EMSDK_DIR) -- skipped" >&2
        exit 3
    fi
    # shellcheck disable=SC1091
    . "$EMSDK_DIR/emsdk_env.sh" >/dev/null 2>&1
    command -v emcc >/dev/null 2>&1 || { echo "build_playback: emsdk_env.sh did not put emcc on PATH" >&2; exit 1; }
fi

# The game's sources need the rules library (a submodule since 02443093). A server checkout
# is updated by `git pull` alone (the admin page's button), which never fetches a submodule:
# without this the player's build stopped at swosrules.h.
git submodule update --init -q third_party/swos-competition-engine ||
    { echo "build_playback: could not fetch the rules library (third_party/swos-competition-engine)" >&2; exit 1; }

echo "== building the recordings player (make WASM=1 PLAYER=1)"
make WASM=1 PLAYER=1 -j"$(nproc 2>/dev/null || echo 4)" >build/playback-build.log 2>&1 || {
    echo "build_playback: the build failed -- the end of build/playback-build.log:" >&2
    tail -20 build/playback-build.log >&2
    exit 1
}
for f in index.html SWOS.html SWOS.js SWOS.wasm SWOS.data; do
    [ -s "dist/playback/$f" ] || { echo "build_playback: dist/playback/$f is missing" >&2; exit 1; }
done
echo "-- built dist/playback ($(du -sh dist/playback | cut -f1))"
