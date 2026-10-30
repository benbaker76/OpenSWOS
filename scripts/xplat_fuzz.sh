#!/usr/bin/env bash
# Cross-platform determinism fuzzer: the same recording played with DIFFERENT RNG seeds.
#
# The corpus is 15 fixed matches, so it drives 15 sets of situations. --rollback-seed
# reseeds the real generators (rollback_rig.c pin_rng), so the same recorded inputs play a
# DIFFERENT match each time -- different tackles, saves, injuries, cards, corners. That is
# how you get coverage the corpus alone cannot give, and it stays deterministic per seed,
# which is what makes the two machines comparable at all.
#
# macOS arm64 and Windows x86_64 run the same (recording, seed, gameplay) AT THE SAME TIME
# -- they are different machines, so there is no reason to wait for one before the other.
set -u
export SWOS_WORLD="${SWOS_WORLD:-9697}"   # (its corpus / careers are 96/97's; World 2026-27 is the game's default)
SCR="${SWOS_HUNT_OUT:-$(cd "$(dirname "$0")/.." && pwd)/tests/hunt}"; mkdir -p "$SCR"
ROOT=/mnt/c/Projects/AI/SWOS/OpenSWOS
GP="${GAMEPLAY:-off}"
mkdir -p "$SCR/fuzz"
RECS="${RECS:-2xcpu-08030320-scalars shootout-08200245-scalars zkz-11215003-scalars playa-10090613-scalars}"
SEEDS="${SEEDS:-0x5EED5EED 0x00000001 0xA1B2C3D4 0xDEADBEEF 0x13579BDF 0xFFFFFFFF}"

for rec in $RECS; do
  for seed in $SEEDS; do
    tag="$rec.$GP.$seed"
    ssh bbaker@192.168.1.200 "cd ~/Programming/OpenSWOS/bin && SWOS_NO_LOG=1 ./SWOS.macos \
        --replay-test=../tests/replay/$rec.swr --dos-gameplay=$GP --rollback=trace \
        --rollback-seed=$seed --rollback-trace=/tmp/$tag.mac.trace > /dev/null 2>&1" </dev/null >/dev/null 2>&1 &
    macpid=$!
    cmd.exe /c "cd /d C:\\Projects\\AI\\SWOS\\OpenSWOS\\bin && set SWOS_NO_LOG=1&& SWOS.exe --replay-test=C:/Projects/AI/SWOS/OpenSWOS/tests/replay/$rec.swr --dos-gameplay=$GP --rollback=trace --rollback-seed=$seed --rollback-trace=C:\\Temp\\$tag.win.trace > NUL 2>&1" </dev/null >/dev/null 2>&1 &
    winpid=$!
    wait $macpid $winpid
    scp -q "bbaker@192.168.1.200:/tmp/$tag.mac.trace" "$SCR/fuzz/" 2>/dev/null
    cp "/mnt/c/Temp/$tag.win.trace" "$SCR/fuzz/" 2>/dev/null
    m="$SCR/fuzz/$tag.mac.trace"; w="$SCR/fuzz/$tag.win.trace"
    if [ -s "$m" ] && [ -s "$w" ]; then
        if cmp -s "$m" "$w"; then
            echo "$tag: identical, $(wc -l < "$m") frames"
            rm -f "$m" "$w"          # keep only what differs; these are big
        else
            echo "$tag: ***DIVERGES*** at $(diff "$m" "$w" | grep -m1 '^<' | awk '{print $1}' | tr -d '<' ) -- traces kept"
        fi
    else
        echo "$tag: a trace is missing (mac $(wc -c < "$m" 2>/dev/null || echo 0) B, win $(wc -c < "$w" 2>/dev/null || echo 0) B)"
    fi
  done
done
