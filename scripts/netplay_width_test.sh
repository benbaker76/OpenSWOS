#!/usr/bin/env bash
# One simulation width, whatever the display (docs/NETPLAY.md, "One simulation width").
#
# Plays each recorded match at several RENDER widths with the same SIMULATION draw width
# (--replay-sim-width) through the rollback rig, and requires the SIM hash of every frame
# to be identical across them. Two netplay peers on a 4:3 and a 16:9 screen play at one
# agreed width; if anything the match decides still read the display's own width, their
# games would part here -- as they did at frame 19 before (the crowd tiles' Rand calls).
#
# The rig does not pin Rand() in this mode, and the margin animation is live at a simulation
# width above 336, so this sees what scripts/replay_widecheck.sh cannot.
#
#   scripts/netplay_width_test.sh                         # the shortest recording
#   scripts/netplay_width_test.sh --all                   # every recording, both gameplays
#   scripts/netplay_width_test.sh --sim-width 672 --widths "336 400 484 672" tests/dos/x.RGD.gz
#
# Options: --all, --sim-width N (default 484), --widths "<w> <w> ..." (default "336 484 672"),
#   --gameplay amiga|dos|both, --jobs N, --out DIR
# Exit code: 0 all identical, 2 a difference, 1 setup failure.
set -uo pipefail
export SWOS_WORLD="${SWOS_WORLD:-9697}"   # (its corpus / careers are 96/97's; World 2026-27 is the game's default)
cd "$(dirname "$0")/.."

SIM_WIDTH=484
WIDTHS="336 484 672"
GAMEPLAY=both
JOBS=6
OUT=tests/netplay-width
ALL=0
RECS=()
BIN=bin/SWOS.x86_64
DEFAULT_REC=tests/dos/zkz-12065730.RGD.gz

while [ $# -gt 0 ]; do
    case "$1" in
        --sim-width) SIM_WIDTH=$2; shift 2 ;;
        --widths)    WIDTHS=$2; shift 2 ;;
        --gameplay)  GAMEPLAY=$2; shift 2 ;;
        --jobs)      JOBS=$2; shift 2 ;;
        --out)       OUT=$2; shift 2 ;;
        --all)       ALL=1; shift ;;
        -h|--help)   sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*)          echo "unknown option $1" >&2; exit 1 ;;
        *)           RECS+=("$1"); shift ;;
    esac
done

case "$GAMEPLAY" in amiga) GPS=(off) ;; dos) GPS=(on) ;; both) GPS=(off on) ;;
    *) echo "bad --gameplay $GAMEPLAY" >&2; exit 1 ;; esac
[ -x "$BIN" ] || { echo "$BIN not built -- make X86_64=1" >&2; exit 1; }
if [ "$ALL" = 1 ]; then RECS=(tests/dos/*.RGD.gz); elif [ ${#RECS[@]} -eq 0 ]; then RECS=("$DEFAULT_REC"); fi
mkdir -p "$OUT" tests/replay
OUT=$(cd "$OUT" && pwd)

# The same .swr conversion scripts/netplay_test.sh uses.
swr_for() {
    local rec=$1 name out
    name=$(basename "$rec" .RGD.gz)
    out=tests/replay/$name-scalars.swr
    if [ ! -f "$out" ] || [ "$rec" -nt "$out" ] || [ scripts/rgd_convert.py -nt "$out" ] ||
       [ scripts/rgd_schema.py -nt "$out" ]; then
        python3 scripts/rgd_convert.py --emit "$out" --level scalars "$rec" >&2 || return 1
    fi
    echo "$out"
}

run() {   # tag swr gameplay render-width
    ( cd bin && SWOS_NO_LOG=1 ./SWOS.x86_64 --replay-test="../$2" --dos-gameplay="$3" --rollback=trace \
        --replay-width="$4" --replay-sim-width="$SIM_WIDTH" --rollback-trace="$OUT/$1.trace" ) \
        > "$OUT/$1.log" 2>&1
    echo "[width-test] exit $?" >> "$OUT/$1.log"
}
export -f run
export OUT SIM_WIDTH

jobs_file=$(mktemp)
trap 'rm -f "$jobs_file"' EXIT
SWRS=()
for rec in "${RECS[@]}"; do
    swr=$(swr_for "$rec") || { echo "conversion failed: $rec" >&2; exit 1; }
    SWRS+=("$swr")
    name=$(basename "$swr" -scalars.swr)
    for gp in "${GPS[@]}"; do
        for w in $WIDTHS; do
            echo "run $name-$gp-w$w $swr $gp $w"
        done
    done
done > "$jobs_file"
xargs -P "$JOBS" -I{} bash -c '{}' < "$jobs_file"

fail=0
for swr in "${SWRS[@]}"; do
    name=$(basename "$swr" -scalars.swr)
    for gp in "${GPS[@]}"; do
        ref=""
        verdict=identical
        for w in $WIDTHS; do
            t=$OUT/$name-$gp-w$w.trace
            if ! grep -q '^\[width-test\] exit 0$' "$OUT/$name-$gp-w$w.log" || [ ! -s "$t" ]; then
                verdict="FAILED at render width $w (see $OUT/$name-$gp-w$w.log)"; break
            fi
            if [ -z "$ref" ]; then ref=$t; refw=$w; continue; fi
            # Column 2 is the SIM hash; VIEW (the highlight ring, drawn coordinates) may differ.
            first=$(paste -d' ' <(awk '{print $1, $2}' "$ref") <(awk '{print $2}' "$t") |
                    awk '$2 != $3 { print $1; exit }')
            if [ -n "$first" ] || [ "$(wc -l < "$ref")" != "$(wc -l < "$t")" ]; then
                verdict="DIFFERS: render $refw vs $w from frame ${first:-(length)}"; break
            fi
        done
        frames=$([ -n "$ref" ] && wc -l < "$ref" || echo 0)
        echo "  $name-$gp: $verdict (simulation width $SIM_WIDTH; render widths $WIDTHS; $frames frames)"
        [ "$verdict" = identical ] || fail=1
    done
done
if [ "$fail" = 0 ]; then echo "PASS"; exit 0; else echo "FAIL"; exit 2; fi
