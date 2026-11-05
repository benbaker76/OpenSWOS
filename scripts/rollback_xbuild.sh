#!/usr/bin/env bash
# Cross-build test of the portable match state (docs/ROLLBACK.md, "Across builds").
#
# The portable encoding (src/sim_state.c) must be the same bytes in every build: 32- or
# 64-bit, Linux or Windows. This plays the same recordings on every build it finds and
# proves two things:
#
#   traces   a plain run's per-frame hashes (SIM, VIEW, observable) are identical on
#            every build -- the same match, and the same checksum for it;
#   resync   state saved by one build at 25/50/75% of the match, loaded by ANOTHER
#            build into a fresh process at frame 0, plays on identically to the end
#            -- a peer on one platform resyncing a peer on another.
#
#   scripts/rollback_xbuild.sh                     # the default recordings
#   scripts/rollback_xbuild.sh zkz-12080203:off    # recording[:gameplay], gameplay on|off
#
# Builds used, when present: bin/SWOS.x86_64 (make X86_64=1), bin/SWOS (make, 32-bit
# Linux), bin/SWOS.exe (Cygwin, run through cmd.exe from WSL). Build them first; at least
# two are needed. Exit code: 0 everything identical, 2 a difference, 1 setup failure.
set -uo pipefail
export SWOS_WORLD="${SWOS_WORLD:-9697}"   # (its corpus / careers are 96/97's, whatever SWOS.ini says)
cd "$(dirname "$0")/.."

OUT=tests/rollback/xbuild
mkdir -p "$OUT"
RECS=("$@")
[ ${#RECS[@]} -gt 0 ] || RECS=(zkz-12065730:off zkz-12065730:on zkz-10140228:on)

BUILDS=()
[ -x bin/SWOS.x86_64 ] && BUILDS+=(x86_64)
[ -x bin/SWOS ] && file bin/SWOS 2>/dev/null | grep -q "ELF 32-bit" && BUILDS+=(m32)
[ -f bin/SWOS.exe ] && command -v cmd.exe >/dev/null && BUILDS+=(cygwin)
[ ${#BUILDS[@]} -ge 2 ] || { echo "need at least two builds, found: ${BUILDS[*]:-none}" >&2; exit 1; }
echo "builds: ${BUILDS[*]}"

WIN_REPO=
if [[ " ${BUILDS[*]} " == *" cygwin "* ]]; then
    WIN_REPO=$(wslpath -w "$(pwd)")
fi

# run_build <build> <log> <args...>: one headless match. Paths in args are relative to
# the repository root, written with forward slashes.
run_build() {
    local build=$1 log=$2; shift 2
    case "$build" in
        x86_64) (cd bin && ./SWOS.x86_64 "${@//@R@/..}") > "$log" 2>&1 ;;
        m32)    (cd bin && ./SWOS "${@//@R@/..}") > "$log" 2>&1 ;;
        cygwin)
            local args=() a
            for a in "$@"; do a=${a//@R@/..}; args+=("${a//\//\\}"); done
            cmd.exe /c "cd /d $WIN_REPO\\bin && SWOS.exe ${args[*]} > ..\\${log//\//\\} 2>&1" < /dev/null ;;
    esac
}

swr_frames() { od -An -tu4 -j40 -N4 "$1" | tr -d ' '; }

# same_from <a> <b> <first frame>: compare two traces from that frame to the end.
same_from() {
    cmp -s <(tail -n +$(( $3 + 1 )) "$1") <(tail -n +$(( $3 + 1 )) "$2")
}

fail=0
for spec in "${RECS[@]}"; do
    rec=${spec%%:*}; gp=${spec#*:}; [ "$gp" = "$spec" ] && gp=off
    swr=tests/replay/$rec-scalars.swr
    if [ ! -f "$swr" ]; then
        python3 scripts/rgd_convert.py --emit "$swr" --level scalars "tests/dos/$rec.RGD.gz" >&2 ||
            { echo "no recording $rec" >&2; exit 1; }
    fi
    frames=$(swr_frames "$swr")
    saves="$((frames / 4)),$((frames / 2)),$((frames * 3 / 4))"
    tag=$rec-$gp

    # Plain runs on every build, each saving resync files as it goes.
    for b in "${BUILDS[@]}"; do
        run_build "$b" "$OUT/$tag-$b.log" --replay-test=@R@/"$swr" --dos-gameplay="$gp" --rollback=trace \
            --rollback-trace=@R@/"$OUT/$tag-$b.trace" --rollback-save-state=@R@/"$OUT/state-$tag-$b" \
            --rollback-save-frames="$saves" &
    done
    wait
    ref=${BUILDS[0]}
    for b in "${BUILDS[@]:1}"; do
        if [ -s "$OUT/$tag-$b.trace" ] && cmp -s "$OUT/$tag-$ref.trace" "$OUT/$tag-$b.trace"; then
            echo "  $tag traces: $b identical to $ref ($(wc -l < "$OUT/$tag-$ref.trace") frames)"
        else
            echo "  $tag traces: $b DIFFERS from $ref (see $OUT/$tag-$b.log)"; fail=1
        fi
    done

    # Every build loads every OTHER build's state.
    for saver in "${BUILDS[@]}"; do
        for loader in "${BUILDS[@]}"; do
            [ "$saver" = "$loader" ] && continue
            for f in $((frames / 4)) $((frames / 2)) $((frames * 3 / 4)); do
                state=$OUT/state-$tag-$saver.$f
                [ -f "$state" ] || continue
                run_build "$loader" "$OUT/load-$tag-$saver-$loader-$f.log" --replay-test=@R@/"$swr" \
                    --dos-gameplay="$gp" --rollback=trace --rollback-restore=sim \
                    --rollback-trace=@R@/"$OUT/load-$tag-$saver-$loader-$f.trace" \
                    --rollback-load-state=@R@/"$state" &
            done
        done
    done
    wait
    for saver in "${BUILDS[@]}"; do
        for loader in "${BUILDS[@]}"; do
            [ "$saver" = "$loader" ] && continue
            for f in $((frames / 4)) $((frames / 2)) $((frames * 3 / 4)); do
                state=$OUT/state-$tag-$saver.$f
                if [ ! -f "$state" ]; then
                    echo "  $tag resync: no state saved by $saver at frame $f (the match ended first) -- skipped"
                    continue
                fi
                t=$OUT/load-$tag-$saver-$loader-$f.trace
                # SIM and observable columns: a resync loads the SIM tier
                if [ -s "$t" ] && same_from <(cut -d' ' -f1,2,4 "$OUT/$tag-$saver.trace") <(cut -d' ' -f1,2,4 "$t") "$f"; then
                    echo "  $tag resync: saved by $saver at frame $f, loaded by $loader -- identical to the end"
                else
                    echo "  $tag resync: saved by $saver at frame $f, loaded by $loader -- DIFFERS"
                    grep -hE "refused|not a resync" "$OUT/load-$tag-$saver-$loader-$f.log" | head -1
                    fail=1
                fi
            done
        done
    done
done
if grep -qlE "no (portable )?identity" "$OUT"/*.log 2>/dev/null; then
    echo "  a run held a pointer with no identity:"; grep -lE "no (portable )?identity" "$OUT"/*.log; fail=1
fi
if [ "$fail" = 0 ]; then echo PASS; exit 0; else echo FAIL; exit 2; fi
