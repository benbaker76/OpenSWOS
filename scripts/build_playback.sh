#!/bin/bash
# Build the recordings player -- the browser build of SWOS as a .SRP player with a scrub
# bar, which the server publishes at https://<server>/playback/ (docs/PLAYBACK.md).
#
#   scripts/build_playback.sh           # -> dist/playback (index.html, SWOS.html/.js/.wasm/.data)
#
# Needs emsdk: EMSDK_DIR, ~/emsdk, ~/git/emsdk or emsdk beside the checkout, or emcc already on PATH. Without it this
# exits 3 -- "skipped", not "failed" -- so a server without emsdk still builds and
# installs everything else (scripts/run_matchmaker.sh --with-playback,
# scripts/swos-matchmaker-service.sh).
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

if ! command -v emcc >/dev/null 2>&1; then
    # EMSDK_DIR, else ~/emsdk, ~/git/emsdk or beside the checkout -- the first that has
    # emsdk_env.sh. The admin page sets EMSDK_DIR=~/git/emsdk (us.swos.eu's layout) while
    # online.openswos.net's is ~/emsdk, so a given EMSDK_DIR that is not there falls through
    # to the others: "Rebuild" skipped the player there (2026-11-10).
    found=""
    for d in ${EMSDK_DIR:+"$EMSDK_DIR"} "$HOME/emsdk" "$HOME/git/emsdk" "$(dirname "$ROOT")/emsdk"; do
        [ -f "$d/emsdk_env.sh" ] && { found="$d"; break; }
    done
    if [ -z "$found" ]; then
        echo "build_playback: no emsdk_env.sh in ${EMSDK_DIR:+$EMSDK_DIR, }$HOME/emsdk, $HOME/git/emsdk or $(dirname "$ROOT")/emsdk (set EMSDK_DIR) -- skipped" >&2
        exit 3
    fi
    EMSDK_DIR="$found"
    # shellcheck disable=SC1091
    . "$EMSDK_DIR/emsdk_env.sh" >/dev/null 2>&1
    command -v emcc >/dev/null 2>&1 || { echo "build_playback: emsdk_env.sh did not put emcc on PATH" >&2; exit 1; }
fi

# The game's sources need the rules library (a submodule since 02443093). A server checkout
# is updated by `git pull` alone (the admin page's button), which never fetches a submodule:
# without this the player's build stopped at swosrules.h.
git submodule update --init -q third_party/swos-competition-engine ||
    { echo "build_playback: could not fetch the rules library (third_party/swos-competition-engine)" >&2; exit 1; }
# A fork's own submodules its build needs (docs/FORKING.md): fork.submodules, a path a line --
# SWOS United's online API client, third_party/swos-api. (A server's `git pull` fetches none.)
if [ -f fork.submodules ]; then
    while read -r sub; do
        case "$sub" in ''|'#'*) continue ;; esac
        git submodule update --init -q "$sub" ||
            { echo "build_playback: could not fetch $sub (fork.submodules)" >&2; exit 1; }
    done < fork.submodules
fi
# The player plays the GAME's graphics and sounds, which are never in git (the data setup makes
# them from the player's own copy, docs/ASSETS.md): built without them it hangs at its first
# missing file -- a server's whose checkout had none did, asking for sound/sfx.in2 for ever
# (2026-11-07). So no data, no build: put the data setup's assets folder on the server and
# point SWOS_ASSETS at it (or copy its trees into bin/).
# Unset, it is the game's own default, ~/.swos/assets -- the Makefile's too (online.openswos.net
# keeps the data there; the check here used to want SWOS_ASSETS set, 2026-11-10).
ASSETS="${SWOS_ASSETS:-$HOME/.swos/assets}"
has_sound() { [ -f "$1/sound/SFX.SNG" ] || [ -n "$(ls -A "$1/sound_dos/fx" 2>/dev/null)" ]; }
if ! has_sound bin && { [ -z "$ASSETS" ] || ! has_sound "$ASSETS"; }; then
    echo "build_playback: no game data -- neither bin/ nor SWOS_ASSETS ($ASSETS) has the" >&2
    echo "  game's sound (sound/SFX.SNG or sound_dos/). Run the data setup (tools/swosdata) and set" >&2
    echo "  SWOS_ASSETS to its assets folder. A player built without it hangs." >&2
    exit 1
fi

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
