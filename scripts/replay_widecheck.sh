#!/usr/bin/env bash
# Width-invariance check — run the corpus at a NON-native render width.
#
#   scripts/replay_widecheck.sh              # 484 (16:9) against 336
#   scripts/replay_widecheck.sh 672          # ultrawide
#   scripts/replay_widecheck.sh 484 zkz-1014 # one recording (substring match)
#
# WHAT THIS PROVES, and why it is a different question from the corpus itself.
#
# The simulation is SUPPOSED to be width-independent: b4775f3 pinned
# _sim_view_width to SCREEN_WIDTH so every display plays the same game and only
# the renderer draws wider (main.c, GAMEPLAY.md 25.4/25.5). If that holds, every
# field the rig compares must be IDENTICAL at any render width, so a divergence
# here is a render -> simulation leak and can be nothing else. That makes this a
# far sharper instrument than the ordinary corpus, which only says "something
# changed".
#
# It exists because scripts/replay_matrix.sh pins the render to 336, where
# WIDE_MARGIN is 0 and every width-dependent path folds to identity. Three
# tester-reported bugs in one day (2026-09-06) were widescreen-only and
# invisible to the corpus for exactly that reason.
#
# WHAT IT DOES NOT CATCH: draw-time bugs that never touch a compared field —
# the empty-bench gate (25111bb) and the Switch asset redirect (7c80c73) were
# both invisible to any field comparison. Those need a rendered-frame check
# (widening the view must only ADD content at the edges, never move or drop it).
# Do not read a pass here as "widescreen is correct".
#
# NOR leaks that change how often Rand() is called: the replay rig pins Rand, so the crowd
# tiles' column (DrawAnimatedPatterns), which parted two widths' matches at frame 19, passed
# here. The rollback rig does not pin it: scripts/netplay_width_test.sh plays the corpus at
# several render widths with one simulation draw width and compares the SIM hash of every
# frame. That is the check for "two displays play the same match" (docs/NETPLAY.md).
set -uo pipefail
export SWOS_WORLD="${SWOS_WORLD:-9697}"   # (its corpus / careers are 96/97's; World 2026-27 is the game's default)
cd "$(dirname "$0")/.."

WIDTH="${1:-484}"
FILTER="${2:-}"

[ -x bin/SWOS ] || { echo "bin/SWOS not built — run make" >&2; exit 1; }

pass=0; fail=0; failed=""
# Drive from tests/dos/*.RGD.gz, exactly as scripts/replay_matrix.sh does — that
# is the corpus. The .swr files are GENERATED (scripts/rgd_convert.py --emit) and
# untracked, so globbing them sweeps up whatever else is lying in the working
# tree: a stray t-scalars.swr did exactly that here and was reported alongside
# the real recordings as though it were one of them.
for r in tests/dos/*.RGD.gz; do
    n=$(basename "$r" .RGD.gz)
    [ -n "$FILTER" ] && case "$n" in *"$FILTER"*) : ;; *) continue ;; esac
    # The matrix's own exclusion: the one v1.2 file has no resolved
    # (header, frame) pair, so it cannot pass at ANY width and would sit in the
    # failing list forever pretending to be a leak.
    [ "$n" = zkz-11122841 ] && continue
    f="tests/replay/$n-scalars.swr"
    [ -f "$f" ] || { printf '  %-16s SKIP (no %s — run scripts/replay_matrix.sh once to emit it)\n' "$n" "$f"; continue; }
    printf '  %-16s ' "$n"
    out=$( (cd bin && ./SWOS --replay-test="../$f" --replay-width="$WIDTH" 2>&1) | \
           grep -E '^\[replay\] (PASS|FAIL)' | head -1 )
    case "$out" in
        *PASS*) pass=$((pass+1)); echo "OK" ;;
        *)      fail=$((fail+1)); failed="$failed $n"
                echo "${out#\[replay\] }" ;;
    esac
done

echo
echo "width $WIDTH:  $pass passed, $fail failed"
[ -n "$failed" ] && {
    echo "failing:$failed"
    echo
    echo "A failure here is a render->simulation leak. To find it, run the same"
    echo "recording at 336 and at $WIDTH with a probe on the diverging field —"
    echo "the two runs are identical until the exact frame the leak fires."
}
exit $(( fail > 0 ))
