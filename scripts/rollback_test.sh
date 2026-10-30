#!/usr/bin/env bash
# Rollback state test rig, headless (docs/ROLLBACK.md).
#
# Plays recorded matches and rolls them back to test src/sim_state.c -- the
# registry of variables netplay saves and restores. A recording supplies only the
# match setup and the inputs; the game is always compared with itself.
#
#   scripts/rollback_test.sh                      # check the shortest recording, both gameplays
#   scripts/rollback_test.sh --all                # check every recording in tests/dos
#   scripts/rollback_test.sh --mode discover --all
#   scripts/rollback_test.sh --mode oracle tests/dos/zkz-11215003.RGD.gz
#
# Modes (see the header of src/rollback_rig.c):
#   check     registry rollback every --every frames, --window frames each, AND the
#             whole match compared frame by frame with a run that never rolled back
#             (--rollback-trace), so state that matters only much later is caught too.
#             Preceded by a determinism check: two plain runs must trace identically.
#   oracle    whole-data rollback -- validates the method itself.
#   discover  leave-one-out evidence per symbol; merged by scripts/rollback_report.py.
#   scan      every symbol the match writes; merged by scripts/rollback_report.py.
#   resync    save the registry (portable encoding) at 25/50/75% of a plain run, load
#             each into a fresh process at frame 0, and require its trace from there
#             on to equal the plain run's -- a peer joining mid-match. With
#             --restore sim only the SIM tier is loaded: the resync minimum.
#
# Registry options (check):
#   --restore all|sim   restore both tiers (default), or only SIM_TIER_SIM -- the
#                       resync minimum; then the VIEW hash column is not compared
#   --disable a,b,...   leave these registry entries out (try a removal, no rebuild);
#                       applied to the reference runs too, so the hashes cover the
#                       same regions
#   --wire              every rollback restores from a state packet (delta against the
#                       previous window start, deflated, fully validated) -- the resync
#                       format under load; resync mode always uses packets
#
# Exit code: 0 everything passed, 2 something failed, 1 setup failure.
set -uo pipefail
export SWOS_WORLD="${SWOS_WORLD:-9697}"   # (its corpus / careers are 96/97's; World 2026-27 is the game's default)
cd "$(dirname "$0")/.."

MODE=check
GAMEPLAY=both
WINDOW=
EVERY=
JOBS=$(nproc 2>/dev/null || echo 4)
OUT=tests/rollback
ALL=0
RESTORE=all
DISABLE=
WIRE=0
RECS=()
BIN=bin/SWOS.x86_64
DEFAULT_REC=tests/dos/zkz-12065730.RGD.gz    # v1.5, 1519 frames -- the shortest

while [ $# -gt 0 ]; do
    case "$1" in
        --mode)     MODE=$2; shift 2 ;;
        --gameplay) GAMEPLAY=$2; shift 2 ;;
        --window)   WINDOW=$2; shift 2 ;;
        --every)    EVERY=$2; shift 2 ;;
        --jobs)     JOBS=$2; shift 2 ;;
        --out)      OUT=$2; shift 2 ;;
        --all)      ALL=1; shift ;;
        --restore)  RESTORE=$2; shift 2 ;;
        --disable)  DISABLE=$2; shift 2 ;;
        --wire)     WIRE=1; shift ;;
        -h|--help)  sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*)         echo "unknown option $1" >&2; exit 1 ;;
        *)          RECS+=("$1"); shift ;;
    esac
done

case "$MODE" in check|oracle|discover|scan|resync) ;; *) echo "bad --mode $MODE" >&2; exit 1 ;; esac
case "$RESTORE" in all|sim) ;; *) echo "bad --restore $RESTORE (all|sim)" >&2; exit 1 ;; esac
case "$GAMEPLAY" in amiga) GPS=(off) ;; dos) GPS=(on) ;; both) GPS=(off on) ;;
    *) echo "bad --gameplay $GAMEPLAY (amiga|dos|both)" >&2; exit 1 ;; esac
[ -x "$BIN" ] || { echo "$BIN not built -- make X86_64=1" >&2; exit 1; }
# The identity lists the portable encoding numbers pointers by must match the source.
python3 scripts/gen_sim_state_ids.py --check >&2 || exit 1
if [ "$ALL" = 1 ]; then
    RECS=(tests/dos/*.RGD.gz)
elif [ ${#RECS[@]} -eq 0 ]; then
    RECS=("$DEFAULT_REC")
fi

mkdir -p "$OUT" tests/replay
OUT=$(cd "$OUT" && pwd)                     # the runs start in bin/: absolute paths only

# The rig needs only each frame's inputs, which every level of .swr carries --
# so the smallest, "scalars". Same reuse rule as scripts/replay_test.sh.
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

# run <name> <swr> <dos-gameplay on|off> <extra args...>: one headless match, log to $OUT.
run() {
    local tag=$1 swr=$2 gp=$3; shift 3
    ( cd bin && ./SWOS.x86_64 --replay-test="../$swr" --dos-gameplay="$gp" "$@" ) \
        > "$OUT/$tag.log" 2>&1
}
export -f run
export OUT

# swr_frames <swr>: the frame count in the .swr header (uint32 at byte 40).
swr_frames() {
    od -An -tu4 -j40 -N4 "$1" | tr -d ' '
}

SWRS=()
MIN_FRAMES=${WINDOW:-32}
for rec in "${RECS[@]}"; do
    [ -f "$rec" ] || { echo "no such recording: $rec" >&2; exit 1; }
    swr=$(swr_for "$rec") || { echo "conversion failed: $rec" >&2; exit 1; }
    frames=$(swr_frames "$swr")
    if [ "${frames:-0}" -le "$MIN_FRAMES" ]; then
        echo "  skip $(basename "$rec"): $frames frame(s), too short for a window"
        continue
    fi
    SWRS+=("$swr")
done
[ ${#SWRS[@]} -gt 0 ] || { echo "no recording long enough to test" >&2; exit 1; }

rargs=()
[ -n "$WINDOW" ] && rargs+=(--rollback-window="$WINDOW")
[ -n "$EVERY" ] && rargs+=(--rollback-every="$EVERY")
rargs+=(--rollback-restore="$RESTORE")
[ "$WIRE" = 1 ] && rargs+=(--rollback-wire)
dargs=()                                    # the registry as tested: every run, reference included
[ -n "$DISABLE" ] && dargs+=(--rollback-disable="$DISABLE")

# Trace lines are "<frame> <sim hash> <view hash> <observable hash>". A sim-only restore
# is not expected to bring the view tier back, so that column is left out.
trace_cols() {
    if [ "$RESTORE" = sim ]; then cut -d' ' -f1,2,4 "$1"; else cat "$1"; fi
}
# first_diff <a> <b>: the first frame whose hashes differ (or "end of <file>" when one
# trace is a prefix of the other).
first_diff() {
    local out
    out=$(cmp <(trace_cols "$1") <(trace_cols "$2") 2>&1)
    case "$out" in
        *"line "*) echo $(( $(echo "$out" | sed -n 's/.*line \([0-9]*\).*/\1/p') - 1 )) ;;
        *EOF*)     echo "end of ${out##*EOF on }" ;;
        *)         echo "?" ;;
    esac
}
traces_equal() {
    cmp -s <(trace_cols "$1") <(trace_cols "$2")
}

fail=0
jobs_file=$(mktemp)
trap 'rm -f "$jobs_file"' EXIT

case "$MODE" in
check)
    # Determinism first, on the first recording: two runs that never roll back must
    # produce identical traces, or nothing a rollback comparison says is meaningful.
    swr=${SWRS[0]}; name=$(basename "$swr" -scalars.swr); gp=${GPS[0]}
    run "determinism-a-$name-$gp" "$swr" "$gp" --rollback=trace --rollback-trace="$OUT/determinism-a.trace" "${dargs[@]}" &
    run "determinism-b-$name-$gp" "$swr" "$gp" --rollback=trace --rollback-trace="$OUT/determinism-b.trace" "${dargs[@]}" &
    wait
    if cmp -s "$OUT/determinism-a.trace" "$OUT/determinism-b.trace"; then
        echo "determinism: $name ($gp) -- two plain runs trace identically ($(wc -l < "$OUT/determinism-a.trace") frames)"
    else
        echo "determinism: FAIL -- two plain runs of $name differ from frame $(first_diff "$OUT/determinism-a.trace" "$OUT/determinism-b.trace")"
        exit 2
    fi

    for swr in "${SWRS[@]}"; do
        name=$(basename "$swr" -scalars.swr)
        for gp in "${GPS[@]}"; do
            echo "run ref-$name-$gp $swr $gp --rollback=trace --rollback-trace=$OUT/ref-$name-$gp.trace ${dargs[*]}"
            echo "run check-$name-$gp $swr $gp --rollback=check --rollback-trace=$OUT/check-$name-$gp.trace --rollback-report=$OUT/check-$name-$gp.json ${rargs[*]} ${dargs[*]}"
        done
    done > "$jobs_file"
    xargs -P "$JOBS" -I{} bash -c '{}' < "$jobs_file"

    for swr in "${SWRS[@]}"; do
        name=$(basename "$swr" -scalars.swr)
        for gp in "${GPS[@]}"; do
            tag="$name-$gp"
            verdict=$(grep -o '\[rollback\] check: .*' "$OUT/check-$tag.log" | tail -1)
            ref=$OUT/ref-$tag.trace chk=$OUT/check-$tag.trace
            if [ ! -s "$ref" ] || [ ! -s "$chk" ]; then
                echo "  $tag: NO TRACE (see $OUT/check-$tag.log, $OUT/ref-$tag.log)"
                fail=1
                continue
            fi
            if traces_equal "$ref" "$chk"; then
                long="whole match identical to the plain run ($(wc -l < "$ref") frames)"
            else
                long="DIFFERS from the plain run from frame $(first_diff "$ref" "$chk")"
                fail=1
            fi
            case "$verdict" in *PASS*) ;; *) fail=1 ;; esac
            echo "  $tag: ${verdict#\[rollback\] check: }; $long"
        done
    done
    ;;

resync)
    for swr in "${SWRS[@]}"; do
        name=$(basename "$swr" -scalars.swr); frames=$(swr_frames "$swr")
        for gp in "${GPS[@]}"; do
            echo "run ref-$name-$gp $swr $gp --rollback=trace --rollback-trace=$OUT/ref-$name-$gp.trace --rollback-save-state=$OUT/state-$name-$gp --rollback-save-frames=$((frames / 4)),$((frames / 2)),$((frames * 3 / 4)) ${dargs[*]}"
        done
    done > "$jobs_file"
    xargs -P "$JOBS" -I{} bash -c '{}' < "$jobs_file"
    for swr in "${SWRS[@]}"; do
        name=$(basename "$swr" -scalars.swr); frames=$(swr_frames "$swr")
        for gp in "${GPS[@]}"; do
            for f in $((frames / 4)) $((frames / 2)) $((frames * 3 / 4)); do
                [ -f "$OUT/state-$name-$gp.$f" ] || continue
                echo "run resync-$name-$gp-$f $swr $gp --rollback=trace --rollback-trace=$OUT/resync-$name-$gp-$f.trace --rollback-load-state=$OUT/state-$name-$gp.$f --rollback-restore=$RESTORE ${dargs[*]}"
            done
        done
    done > "$jobs_file"
    xargs -P "$JOBS" -I{} bash -c '{}' < "$jobs_file"
    for swr in "${SWRS[@]}"; do
        name=$(basename "$swr" -scalars.swr); frames=$(swr_frames "$swr")
        for gp in "${GPS[@]}"; do
            for f in $((frames / 4)) $((frames / 2)) $((frames * 3 / 4)); do
                tag="$name-$gp-$f"
                ref=$OUT/ref-$name-$gp.trace res=$OUT/resync-$tag.trace
                if [ ! -f "$OUT/state-$name-$gp.$f" ]; then
                    echo "  $tag: no state saved (the plain run ended first?)"
                    continue
                fi
                if [ ! -s "$res" ]; then
                    echo "  $tag: NO TRACE (see $OUT/resync-$tag.log)"; fail=1; continue
                fi
                if cmp -s <(trace_cols "$ref" | tail -n +$((f + 1))) <(trace_cols "$res" | tail -n +$((f + 1))); then
                    echo "  $tag: loaded at frame $f, identical to the plain run to the end ($(( $(wc -l < "$ref") - f )) frames)"
                else
                    d=$(cmp <(trace_cols "$ref" | tail -n +$((f + 1))) <(trace_cols "$res" | tail -n +$((f + 1))) 2>&1 | sed -n 's/.*line \([0-9]*\).*/\1/p')
                    echo "  $tag: loaded at frame $f, DIFFERS from frame $(( f + ${d:-1} - 1 ))"
                    fail=1
                fi
            done
        done
    done
    grep -h "saved resync state" "$OUT"/ref-*.log 2>/dev/null |
        sed -n 's/.*SIM \([0-9]*\) bytes, SIM+VIEW \([0-9]*\) bytes.*/\1 \2/p' |
        awk '{ s += $1; a += $2
               if (!n || $1 < smin) smin = $1; if ($1 > smax) smax = $1
               if (!n || $2 < amin) amin = $2; if ($2 > amax) amax = $2; n++ }
             END { if (n) printf "  resync packets, delta against kick-off, %d saves: SIM %d..%d bytes (average %d); SIM+VIEW %d..%d (average %d)\n", n, smin, smax, s / n, amin, amax, a / n }'
    ;;

oracle|discover|scan)
    for swr in "${SWRS[@]}"; do
        name=$(basename "$swr" -scalars.swr)
        for gp in "${GPS[@]}"; do
            echo "run $MODE-$name-$gp $swr $gp --rollback=$MODE --rollback-report=$OUT/$MODE-$name-$gp.json ${rargs[*]} ${dargs[*]}"
        done
    done > "$jobs_file"
    xargs -P "$JOBS" -I{} bash -c '{}' < "$jobs_file"

    reports=()
    for swr in "${SWRS[@]}"; do
        name=$(basename "$swr" -scalars.swr)
        for gp in "${GPS[@]}"; do
            tag="$MODE-$name-$gp"
            line=$(grep -o "\[rollback\] $MODE: .*" "$OUT/$tag.log" | tail -1)
            if [ -f "$OUT/$tag.json" ]; then
                reports+=("$OUT/$tag.json")
                echo "  $tag: ${line#\[rollback\] $MODE: }"
                case "$MODE:$line" in oracle:*FAIL*|discover:*" 0 invalid"*) ;; esac
                case "$MODE" in oracle) case "$line" in *PASS*) ;; *) fail=1 ;; esac ;; esac
                case "$MODE" in discover) case "$line" in *"; 0 invalid"*) ;; *) fail=1 ;; esac ;; esac
            else
                echo "  $tag: NO REPORT (see $OUT/$tag.log)"
                fail=1
            fi
        done
    done
    if [ "$MODE" != oracle ] && [ ${#reports[@]} -gt 0 ]; then
        python3 scripts/rollback_report.py "${reports[@]}" > "$OUT/$MODE-summary.txt"
        echo "summary: $OUT/$MODE-summary.txt"
    fi
    ;;
esac

if [ "$fail" = 0 ]; then echo "PASS"; exit 0; else echo "FAIL"; exit 2; fi
