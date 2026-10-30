#!/usr/bin/env bash
# Every recording, both gameplays, several RNG seeds, under AddressSanitizer.
#
# Calibrated first: with the goalkeeper fix reverted, ASan names that exact read
# (global-buffer-overflow, GetPlayerPrice src/match_sim.c:2012), so a clean sweep here is
# evidence rather than an absence of effort. Seeds vary the match the same way the
# cross-platform fuzzer does -- a read that only a particular situation performs needs that
# situation to happen.
set -u
export SWOS_WORLD="${SWOS_WORLD:-9697}"   # (its corpus / careers are 96/97's, whatever SWOS.ini says)
SCR="${SWOS_HUNT_OUT:-$(cd "$(dirname "$0")/.." && pwd)/tests/hunt}"; mkdir -p "$SCR"
cd /mnt/c/Projects/AI/SWOS/OpenSWOS/bin || exit 1
OUT="$SCR/asan_reports"
rm -rf "$OUT"; mkdir -p "$OUT"

run_one() {   # rec, gameplay, seed
    local rec=$1 gp=$2 seed=$3 tag
    tag="$(basename "$rec" .swr).$gp.$seed"
    ASAN_OPTIONS="halt_on_error=0:detect_leaks=0:log_path=$OUT/$tag" \
    SWOS_NO_LOG=1 timeout 3600 ./SWOS.x86_64 --replay-test="../tests/replay/$rec" \
        --dos-gameplay=$gp --rollback=trace --rollback-seed=$seed \
        --rollback-trace=/dev/null > "$OUT/$tag.log" 2>&1
    local n
    n=$(cat "$OUT/$tag".[0-9]* 2>/dev/null | grep -c "ERROR: AddressSanitizer" || true)
    printf "%-52s %s\n" "$tag" "${n:-0} report(s)"
}
export -f run_one; export OUT SCR

for rec in $(ls ../tests/replay/*-scalars.swr | xargs -n1 basename); do
    for gp in on off; do
        for seed in 0x5EED5EED 0xA1B2C3D4 0xDEADBEEF 0x00000001 0x13579BDF 0xFFFFFFFF 0x2468ACE0 0x7F3A91C5 0xC0FFEE11 0x5A5A5A5A; do
            echo "$rec $gp $seed"
        done
    done
done | xargs -P 6 -n 3 bash -c 'run_one "$0" "$1" "$2"'

echo "---- distinct ASan reports ----"
cat "$OUT"/*.[0-9]* 2>/dev/null | grep -A3 "ERROR: AddressSanitizer" | grep -E "^\s+#0|ERROR:" | sort | uniq -c | sort -rn | head -30
echo "(no output above = clean)"
