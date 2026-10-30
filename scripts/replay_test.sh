#!/usr/bin/env bash
# Convert a recorded match and replay it against this build (docs/TESTS.md).
#
#   scripts/replay_test.sh                       # the default recording, level info
#   scripts/replay_test.sh --level sprites
#   scripts/replay_test.sh --level teams tests/dos/zkz-11021807.RGD.gz
#   scripts/replay_test.sh --all                 # every level, shortest first
#
# Exit code is the rig's: 0 every frame matched, 2 a divergence, 1 setup failed.
set -uo pipefail
export SWOS_WORLD="${SWOS_WORLD:-9697}"   # (its corpus / careers are 96/97's; World 2026-27 is the game's default)
cd "$(dirname "$0")/.."

REC=tests/dos/zkz-12065730.RGD.gz     # v1.5, 1519 frames — the shortest
LEVEL=info
FRAMES=
ALL=0
EXTRA=()

# Five sprite fields the corpus cannot agree on at kick-off. swos-port builds
# its sprites in C++ rather than from SWOS's static sprite table: Sprite::init()
# memsets from offsetof(frameOffset) and then applies its own initial values, so
# the table's values never reach frame 0 there. On ALL FIVE this port holds the
# value data/SWOS2.s gives (save_sprite included — SWOS2.s:16857 writes the 1 we
# have); the recording holds the constructor's. Muted by default at the sprites
# level so the run reaches real gameplay state; --strict compares them anyway.
# See docs/TESTS.md, "Can we claim 100% accurate?" — this is a statement about
# the harness, not about the port, and nothing past frame 0 depends on it.
INIT_FIELDS='.starting_direction,.frame_switch_counter,.field_3e,.player_direction,.save_sprite'
STRICT=0

while [ $# -gt 0 ]; do
    case "$1" in
        --level)  LEVEL=$2; shift 2 ;;
        --frames) FRAMES=$2; shift 2 ;;
        --all)    ALL=1; shift ;;
        --strict) STRICT=1; shift ;;
        -h|--help) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*)       EXTRA+=("$1"); shift ;;
        *)        REC=$1; shift ;;
    esac
done

[ -f "$REC" ] || { echo "no such recording: $REC (see docs/TESTS.md to restore tests/dos)" >&2; exit 1; }
[ -x bin/SWOS ] || { echo "bin/SWOS not built — run make" >&2; exit 1; }

mkdir -p tests/replay
name=$(basename "$REC" .RGD.gz)

# Converting a long recording at "sprites" writes >100 MB of .swr, which on a
# /mnt/c working copy costs more than the replay it feeds. The output is a pure
# function of (recording, level, converter, schema), so a .swr newer than all
# four is reused. Any edit to either script bumps its mtime and invalidates the
# cache; --frames never reuses, since a windowed emit is not the whole file.
swr_is_fresh() {
    local out=$1
    [ -n "$FRAMES" ] && return 1
    [ -f "$out" ] || return 1
    local dep
    for dep in "$REC" scripts/rgd_convert.py scripts/rgd_schema.py; do
        [ "$dep" -nt "$out" ] && return 1
    done
    return 0
}

run_level() {
    local lvl=$1 out="tests/replay/$name-$1.swr"
    if swr_is_fresh "$out"; then
        echo "  reusing $out (newer than the recording, the converter and the schema)" >&2
    else
        python3 scripts/rgd_convert.py --emit "$out" --level "$lvl" \
            ${FRAMES:+--frames "$FRAMES"} "$REC" || return 1
    fi
    local skip=()
    [ "$lvl" = sprites ] && [ "$STRICT" = 0 ] && skip=(--replay-skip="$INIT_FIELDS")
    ( cd bin && ./SWOS --replay-test="../$out" "${skip[@]}" "${EXTRA[@]+"${EXTRA[@]}"}" )
}

if [ "$ALL" = 1 ]; then
    rc=0
    for lvl in scalars teams info sprites; do
        echo "=== level $lvl"
        run_level "$lvl" || rc=$?
    done
    exit $rc
fi

run_level "$LEVEL"
