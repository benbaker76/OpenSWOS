#!/usr/bin/env bash
# Whole-match replays (.SRP, docs/REPLAY_FORMAT_V2.md): what is recorded plays back as it was.
#
# For each recorded match and gameplay, the replay rig plays the match at every width in
# --widths, recording it with --record-replay and tracing (SWOS_REPLAY_TRACE) what the
# live match drew and the sounds it started. Then --view-replay plays
#   - the recording made at the FIRST width at every width, and
#   - the recording made at every other width at the first width,
# through the highlights viewer, tracing the same. Every frame's sprite hash (image, and
# on-screen position of every world sprite drawn) and every frame's sounds must equal the
# live run's AT THE PLAYBACK WIDTH, and so must the pitch's draw origin and the presenter's
# sub-pixel scroll: a recording is resolution independent, so a 4:3 match
# played on a 16:9 screen must look exactly like that match played live at 16:9.
#
#   scripts/replay_v2_test.sh                          # the shortest recording
#   scripts/replay_v2_test.sh --all                    # every recording, both gameplays
#   scripts/replay_v2_test.sh --widths "336 400 484 672" tests/dos/x.RGD.gz
#
# Options: --all, --widths "<w> <w> ..." (default "336 484 672"), --gameplay amiga|dos|both,
#   --jobs N, --out DIR
# Exit code: 0 all identical, 2 a difference, 1 setup failure.
set -uo pipefail
export SWOS_WORLD="${SWOS_WORLD:-9697}"   # (its corpus / careers are 96/97's; World 2026-27 is the game's default)
cd "$(dirname "$0")/.."

WIDTHS="336 484 672"
GAMEPLAY=both
JOBS=6
OUT=tests/replay-v2
ALL=0
RECS=()
BIN=bin/SWOS.x86_64
DEFAULT_REC=tests/dos/zkz-12065730.RGD.gz

while [ $# -gt 0 ]; do
    case "$1" in
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
read -r -a WS <<< "$WIDTHS"
REFW=${WS[0]}

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

live() {   # tag swr gameplay width
    rm -f "$OUT/$1.srp" "$OUT/$1.trace"
    ( cd bin && SWOS_NO_LOG=1 SWOS_REPLAY_TRACE="$OUT/$1.trace" ./SWOS.x86_64 --replay-test="../$2" \
        --dos-gameplay="$3" --rollback=trace --replay-width="$4" --record-replay="$OUT/$1.srp" ) \
        > "$OUT/$1.log" 2>&1
    echo "[replay-v2-test] exit $?" >> "$OUT/$1.log"
}
play() {   # tag srp width
    rm -f "$OUT/$1.trace"
    ( cd bin && SWOS_NO_LOG=1 SWOS_REPLAY_TRACE="$OUT/$1.trace" ./SWOS.x86_64 --view-replay="$2" \
        --replay-width="$3" ) > "$OUT/$1.log" 2>&1
    echo "[replay-v2-test] exit $?" >> "$OUT/$1.log"
}
export -f live play
export OUT

SWRS=()
for rec in "${RECS[@]}"; do
    swr=$(swr_for "$rec") || { echo "conversion failed: $rec" >&2; exit 1; }
    SWRS+=("$swr")
done

# Recordings first: the playbacks read their files.
for swr in "${SWRS[@]}"; do
    name=$(basename "$swr" -scalars.swr)
    for gp in "${GPS[@]}"; do
        for w in "${WS[@]}"; do echo "live $name-$gp-live$w $swr $gp $w"; done
    done
done | xargs -P "$JOBS" -I{} bash -c '{}'

for swr in "${SWRS[@]}"; do
    name=$(basename "$swr" -scalars.swr)
    for gp in "${GPS[@]}"; do
        for w in "${WS[@]}"; do
            echo "play $name-$gp-rec${REFW}-play$w $OUT/$name-$gp-live$REFW.srp $w"
            [ "$w" = "$REFW" ] || echo "play $name-$gp-rec$w-play$REFW $OUT/$name-$gp-live$w.srp $REFW"
        done
    done
done | xargs -P "$JOBS" -I{} bash -c '{}'

# "frame F sprites N hash H view X Y FX FY" and "frame F sound K C I" lines; the first
# difference, or "".
#
# Frames the PLAYBACK marks "clamped" are not compared for sprites: the rig simulates in
# the corpus's 200-line view while an Amiga-view run draws 272 lines, so its camera goes
# down to 664 and the live run drew past the bottom of the stadium; playback holds the
# camera where the picture stays on the pitch (src/replay_v2.c shown_camera_y).
compare() {   # live-trace play-trace
    python3 - "$1" "$2" <<'EOF'
import sys
skip = set()
def load(p):
    spr, snd = {}, {}
    for l in open(p):
        f = l.split()
        if not f or f[0] != "frame":
            continue
        fr = int(f[1])
        if f[2] == "sprites":
            if f[-1] == "clamped":
                skip.add(fr)        # held on the pitch in playback: see above
            spr[fr] = (f[3], f[5], f[7:11])
        elif f[2] == "sound":
            snd.setdefault(fr, []).append(tuple(f[3:]))
    return spr, snd
ls, la = load(sys.argv[1])
ps, pa = load(sys.argv[2])
if not ls:
    print("the live run traced no frames"); sys.exit()
d = [k for k in sorted(set(ls) | set(ps)) if k not in skip and ls.get(k) != ps.get(k)]
s = [k for k in sorted(set(la) | set(pa)) if la.get(k) != pa.get(k)]
if d:
    print("sprites differ on %d of %d frames, first %d" % (len(d), len(ls), d[0]))
elif s:
    print("sounds differ on %d frames, first %d" % (len(s), s[0]))
EOF
}

fail=0
for swr in "${SWRS[@]}"; do
    name=$(basename "$swr" -scalars.swr)
    for gp in "${GPS[@]}"; do
        verdict=identical
        for w in "${WS[@]}"; do
            if ! grep -q '^\[replay-v2-test\] exit 0$' "$OUT/$name-$gp-live$w.log" || [ ! -s "$OUT/$name-$gp-live$w.srp" ]; then
                verdict="recording FAILED at width $w (see $OUT/$name-$gp-live$w.log)"; break
            fi
        done
        if [ "$verdict" = identical ]; then
            for w in "${WS[@]}"; do
                pairs="$REFW:$w"
                [ "$w" = "$REFW" ] || pairs="$pairs $w:$REFW"
                for p in $pairs; do
                    rw=${p%:*}; pw=${p#*:}
                    tag=$name-$gp-rec$rw-play$pw
                    if ! grep -q '^\[replay-v2-test\] exit 0$' "$OUT/$tag.log"; then
                        verdict="playback FAILED: recorded at $rw, played at $pw (see $OUT/$tag.log)"; break 2
                    fi
                    diff=$(compare "$OUT/$name-$gp-live$pw.trace" "$OUT/$tag.trace")
                    if [ -n "$diff" ]; then
                        verdict="recorded at $rw, played at $pw: $diff"; break 2
                    fi
                done
            done
        fi
        frames=$(grep -c ' sprites ' "$OUT/$name-$gp-live$REFW.trace" 2>/dev/null || echo 0)
        size=$(stat -c %s "$OUT/$name-$gp-live$REFW.srp" 2>/dev/null || echo 0)
        echo "  $name-$gp: $verdict ($frames frames, $size bytes; widths ${WS[*]})"
        [ "$verdict" = identical ] || fail=1
    done
done
if [ "$fail" = 0 ]; then echo "PASS"; exit 0; else echo "FAIL"; exit 2; fi
