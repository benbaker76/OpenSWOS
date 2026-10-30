#!/usr/bin/env bash
# The same recordings, frame-hashed on macOS arm64 and on Windows x86_64, with no network
# between them. A live match desyncs once in dozens; a replay is deterministic, so if the
# two platforms simulate differently at all, this says so on the first recording that hits
# the code in question -- and names the frame.
#
# Both binaries carry the same -DSWOS_TEST_NO_INJURIES_CARDS, so each deviates from the
# RECORDING identically; what is compared here is mac against win, not either against the
# corpus, and SWOS_REPLAY_ALLOW_FLAGS=1 is what lets the rig run at all.
set -u
export SWOS_WORLD="${SWOS_WORLD:-9697}"   # (its corpus / careers are 96/97's; World 2026-27 is the game's default)
SCR="${SWOS_HUNT_OUT:-$(cd "$(dirname "$0")/.." && pwd)/tests/hunt}"; mkdir -p "$SCR"
ROOT=/mnt/c/Projects/AI/SWOS/OpenSWOS
GAMEPLAY="${GAMEPLAY:-on}"; GP="$GAMEPLAY"
mkdir -p "$SCR/trace"
recs="${1:-$(ls "$ROOT"/tests/replay/*-scalars.swr | xargs -n1 basename)}"

for r in $recs; do
    base="${r%.swr}"
    ssh bbaker@192.168.1.200 "cd ~/Programming/OpenSWOS/bin && SWOS_NO_LOG=1 SWOS_REPLAY_ALLOW_FLAGS=1 \
        ./SWOS.macos --replay-test=../tests/replay/$r --dos-gameplay=${GAMEPLAY:-on} --rollback=trace \
        --rollback-trace=/tmp/$base.$GP.mac.trace > /tmp/$base.mac.log 2>&1" </dev/null >/dev/null 2>&1
    cmd.exe /c "cd /d C:\\Projects\\AI\\SWOS\\OpenSWOS\\bin && set SWOS_NO_LOG=1&& set SWOS_REPLAY_ALLOW_FLAGS=1&& SWOS.exe --replay-test=C:/Projects/AI/SWOS/OpenSWOS/tests/replay/$r --dos-gameplay=${GAMEPLAY:-on} --rollback=trace --rollback-trace=C:\\Temp\\$base.$GP.win.trace > C:\\Temp\\$base.win.log 2>&1" </dev/null >/dev/null 2>&1
    scp -q "bbaker@192.168.1.200:/tmp/$base.$GP.mac.trace" "$SCR/trace/" 2>/dev/null
    cp "/mnt/c/Temp/$base.$GP.win.trace" "$SCR/trace/" 2>/dev/null
    m="$SCR/trace/$base.$GP.mac.trace"; w="$SCR/trace/$base.$GP.win.trace"
    if [ -s "$m" ] && [ -s "$w" ]; then
        if cmp -s "$m" "$w"; then
            echo "$base: identical, $(wc -l < "$m") frames"
        else
            echo "$base: DIVERGES"
            diff "$m" "$w" | head -4
        fi
    else
        echo "$base: a trace is missing (mac $(wc -c < "$m" 2>/dev/null || echo 0) B, win $(wc -c < "$w" 2>/dev/null || echo 0) B)"
    fi
done
