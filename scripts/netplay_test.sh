#!/usr/bin/env bash
# Netplay match test, headless (docs/NETPLAY.md, "Playing a match through GGPO").
#
# Plays recorded matches through the netplay frame step (src/netplay_match.c) and
# requires each to be the match the replay rig plays: every frame's state hashes equal
# to a plain run's (--rollback=trace). A recording supplies the setup and both players'
# inputs.
#
#   scripts/netplay_test.sh                          # the shortest recording, both gameplays
#   scripts/netplay_test.sh --all                    # every recording in tests/dos
#   scripts/netplay_test.sh --modes "p2p p2p:2" --stall 8 tests/dos/zkz-11215003.RGD.gz
#   scripts/netplay_test.sh --modes p2p --netem wifi
#   scripts/netplay_test.sh --modes p2p --netem delay=80,jitter=30,loss=5,reorder=5
#
# Modes, each compared with the plain run:
#   direct       the netplay step without GGPO: the input, GameLoop_Frame, the clock, the
#                buffer swap and the VBlank interrupt, all called from outside the loop
#   synctest:N   GGPO's synctest session: every N frames (1..8) it loads the state of N
#                frames back and plays them again. A re-simulated frame whose checksum
#                differs from the first run's fails the run, naming the registry entries
#                that came out different.
#   p2p[:D]      two processes over loopback UDP, each with one player's recorded input and
#                GGPO's prediction of the other's, input delay D (default 0). Each writes
#                every frame as finally played (--netplay-trace); both must equal the plain
#                run. A recording with input in its first D frames cannot -- the delay
#                drops it -- and is reported as skipped.
#
# Sound: every mode also writes the sound effects it played (--netplay-sound-trace).
# With direct among the modes its trace is the reference: synctest must play exactly
# the same effects (a re-simulated frame repeats none), and each p2p peer at least
# them, on the same frames -- a frame played on a wrong prediction may add effects the
# corrected frame does not have, and those are counted, not failed.
#
# Options: --modes "<m> <m> ...", --gameplay amiga|dos|both, --jobs N, --out DIR, --all,
#   --stall MS      p2p: each peer pauses now and then, up to MS, like a busy machine
#                   (--netplay-stall); the other peer plays ahead on predicted input and
#                   rolls back far more than on an idle loopback
#   --netem SPEC    p2p: put a misbehaving network between the peers
#                   (src/netplay_netem.c). SPEC is a profile name --
#                   lan adsl wifi mobile satellite awful -- or a comma list of
#                   profile= delay= jitter= loss= reorder= reorder_ms= dup=
#                   dup_count= throttle= throttle_ms= throttle_drop= tamper=
#                   tamper_bits=, which override a profile named alongside them.
#                   The result must not change: rollback exists to repair exactly
#                   this, so both peers must STILL end identical to the plain run.
#                   Each peer is seeded from its own port, so a run is repeatable
#                   and the two peers never lose the same packet. Conditions hard
#                   enough to disconnect a peer fail the run -- that is a link
#                   nobody could play on, reported honestly, not a netcode bug.
#   --sim-width N   every run plays at simulation draw width N (--replay-sim-width;
#                   default 336, the corpus's own)
#   --peer-widths "A B"
#                   p2p: player 1 renders A wide and player 2 B -- a 4:3 and a 16:9
#                   player at one agreed simulation width, which must still be the
#                   plain run (docs/NETPLAY.md, "One simulation width")
# Exit code: 0 everything passed, 2 something failed, 1 setup failure.
set -uo pipefail
export SWOS_WORLD="${SWOS_WORLD:-9697}"   # (its corpus / careers are 96/97's; World 2026-27 is the game's default)
cd "$(dirname "$0")/.."

MODES="direct synctest:1 synctest:8 p2p"
GAMEPLAY=both
JOBS=$(nproc 2>/dev/null || echo 4)
OUT=tests/netplay
ALL=0
RECS=()
STALL=0
NETEM=""              # what --netem parsed to: "VAR=VAL VAR=VAL", for the p2p peers
BIN=bin/SWOS.x86_64
DEFAULT_REC=tests/dos/zkz-12065730.RGD.gz    # v1.5, 1519 frames -- the shortest
BASE_PORT=23950         # below every ephemeral range; see LIVE_TEST_PORT in src/netplay_live_test.c
P2P_TIMEOUT=1800
SIM_WIDTH=336
PEER_WIDTHS="336 336"

while [ $# -gt 0 ]; do
    case "$1" in
        --modes)    MODES=$2; shift 2 ;;
        --gameplay) GAMEPLAY=$2; shift 2 ;;
        --jobs)     JOBS=$2; shift 2 ;;
        --out)      OUT=$2; shift 2 ;;
        --all)      ALL=1; shift ;;
        --stall)    STALL=$2; shift 2 ;;
        --netem)    NETEM_SPEC=$2; shift 2 ;;
        --sim-width) SIM_WIDTH=$2; shift 2 ;;
        --peer-widths) PEER_WIDTHS=$2; shift 2 ;;
        -h|--help)  sed -n '2,50p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*)         echo "unknown option $1" >&2; exit 1 ;;
        *)          RECS+=("$1"); shift ;;
    esac
done

# --netem: a profile name, or key=value pairs, or both. Keys are the short names
# of the SWOS_NETEM_* knobs so the command line does not read like an env dump.
for kv in ${NETEM_SPEC:+${NETEM_SPEC//,/ }}; do
    case "$kv" in
        *=*) k=${kv%%=*}; v=${kv#*=} ;;
        *)   k=profile;   v=$kv ;;
    esac
    case "$k" in
        profile)       NETEM="$NETEM SWOS_NETEM_PROFILE=$v" ;;
        delay)         NETEM="$NETEM SWOS_NETEM_DELAY_MS=$v" ;;
        jitter)        NETEM="$NETEM SWOS_NETEM_JITTER_MS=$v" ;;
        loss)          NETEM="$NETEM SWOS_NETEM_LOSS_PCT=$v" ;;
        reorder)       NETEM="$NETEM SWOS_NETEM_REORDER_PCT=$v" ;;
        reorder_ms)    NETEM="$NETEM SWOS_NETEM_REORDER_MS=$v" ;;
        dup)           NETEM="$NETEM SWOS_NETEM_DUP_PCT=$v" ;;
        dup_count)     NETEM="$NETEM SWOS_NETEM_DUP_COUNT=$v" ;;
        throttle)      NETEM="$NETEM SWOS_NETEM_THROTTLE_PCT=$v" ;;
        throttle_ms)   NETEM="$NETEM SWOS_NETEM_THROTTLE_MS=$v" ;;
        throttle_drop) NETEM="$NETEM SWOS_NETEM_THROTTLE_DROP=$v" ;;
        tamper)        NETEM="$NETEM SWOS_NETEM_TAMPER_PCT=$v" ;;
        tamper_bits)   NETEM="$NETEM SWOS_NETEM_TAMPER_BITS=$v" ;;
        *) echo "bad --netem key '$k' (see --help)" >&2; exit 1 ;;
    esac
done
NETEM=${NETEM# }
# It only reaches the p2p peers: the other modes have no socket to misbehave on.
case "$NETEM:$MODES" in
    ?*:*p2p*) ;;
    ?*:*) echo "--netem needs a p2p mode (--modes p2p); nothing else uses the network" >&2; exit 1 ;;
esac

for m in $MODES; do
    case "$m" in direct|synctest|synctest:[1-8]|p2p|p2p:[0-7]) ;;
        *) echo "bad mode $m (direct|synctest:N|p2p[:D])" >&2; exit 1 ;; esac
done
case "$GAMEPLAY" in amiga) GPS=(off) ;; dos) GPS=(on) ;; both) GPS=(off on) ;;
    *) echo "bad --gameplay $GAMEPLAY (amiga|dos|both)" >&2; exit 1 ;; esac
[ -x "$BIN" ] || { echo "$BIN not built -- make X86_64=1" >&2; exit 1; }
if [ "$ALL" = 1 ]; then
    RECS=(tests/dos/*.RGD.gz)
elif [ ${#RECS[@]} -eq 0 ]; then
    RECS=("$DEFAULT_REC")
fi

mkdir -p "$OUT" tests/replay
OUT=$(cd "$OUT" && pwd)                     # the runs start in bin/: absolute paths only

# Inputs are all a run needs from a recording: the smallest .swr, "scalars", reused on
# the same rule as scripts/replay_test.sh and scripts/rollback_test.sh.
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

# run <tag> <swr> <dos-gameplay on|off> <extra args...>: one headless match, logged to
# $OUT/<tag>.log with its exit code on the last line.
run() {
    local tag=$1 swr=$2 gp=$3; shift 3
    ( cd bin && ./SWOS.x86_64 --replay-test="../$swr" --dos-gameplay="$gp" \
        --replay-sim-width="$SIM_WIDTH" "$@" ) \
        > "$OUT/$tag.log" 2>&1
    echo "[netplay-test] exit $?" >> "$OUT/$tag.log"
}

# run_p2p <tag> <swr> <gameplay> <delay> <port>: the two peers of one match, on ports
# <port> and <port>+1, logged and traced as <tag>-1 and <tag>-2.
run_p2p() {
    local tag=$1 swr=$2 gp=$3 delay=$4 port=$5 p
    for p in 1 2; do
        (
            # Each peer gets its own netem seed, derived from its port: the run
            # repeats exactly, and the two peers never drop the same packet at
            # the same moment -- which two real machines never do either.
            cd bin && timeout "$P2P_TIMEOUT" \
                env $NETEM SWOS_NETEM_SEED=$((port + p)) \
                ./SWOS.x86_64 --replay-test="../$swr" --dos-gameplay="$gp" --rollback=trace \
                --replay-sim-width="$SIM_WIDTH" --replay-width="$(echo $PEER_WIDTHS | cut -d' ' -f$p)" \
                --netplay-match=p2p --netplay-delay="$delay" --netplay-stall="$STALL" --netplay-player=$p \
                --netplay-port=$((port + p - 1)) --netplay-peer=127.0.0.1:$((port + 2 - p)) \
                --netplay-trace="$OUT/$tag-$p.trace" --netplay-sound-trace="$OUT/$tag-$p.snd" \
                > "$OUT/$tag-$p.log" 2>&1
            echo "[netplay-test] exit $?" >> "$OUT/$tag-$p.log"
        ) &
    done
    wait
}
export -f run run_p2p
export OUT STALL P2P_TIMEOUT NETEM SIM_WIDTH PEER_WIDTHS

SWRS=()
for rec in "${RECS[@]}"; do
    [ -f "$rec" ] || { echo "no such recording: $rec" >&2; exit 1; }
    swr=$(swr_for "$rec") || { echo "conversion failed: $rec" >&2; exit 1; }
    SWRS+=("$swr")
done

mode_tag() { local m=${1/:/}; echo "${m:-$1}"; }   # synctest:8 -> synctest8, p2p:2 -> p2p2

jobs_file=$(mktemp)
trap 'rm -f "$jobs_file"' EXIT
port=$BASE_PORT
for swr in "${SWRS[@]}"; do
    name=$(basename "$swr" -scalars.swr)
    for gp in "${GPS[@]}"; do
        echo "run plain-$name-$gp $swr $gp --rollback=trace --rollback-trace=$OUT/plain-$name-$gp.trace"
        for m in $MODES; do
            mt=$(mode_tag "$m")
            case "$m" in
            p2p*)
                delay=${m#p2p}; delay=${delay#:}
                echo "run_p2p $mt-$name-$gp $swr $gp ${delay:-0} $port"
                port=$((port + 2)) ;;
            *)
                echo "run $mt-$name-$gp $swr $gp --rollback=trace --rollback-trace=$OUT/$mt-$name-$gp.trace --netplay-match=$m --netplay-sound-trace=$OUT/$mt-$name-$gp.snd" ;;
            esac
        done
    done
done > "$jobs_file"
xargs -P "$JOBS" -I{} bash -c '{}' < "$jobs_file"

exited_ok() { grep -q '^\[netplay-test\] exit 0$' "$1"; }

# trace_diff <ref> <trace>: nothing if they agree, else the first frame that does not.
# Peers on different render widths (--peer-widths) agree on the SIM column only: the VIEW
# tier (the highlight ring) and the rig's observable fields hold drawn coordinates.
trace_diff() {
    set -- "$1" "$2"
    if [ "${PEER_WIDTHS% *}" = "${PEER_WIDTHS#* }" ]; then
        cmp -s "$1" "$2" && return
        line=$(cmp "$1" "$2" 2>&1 | sed -n 's/.*line \([0-9]*\).*/\1/p')
        echo $(( ${line:-1} - 1 ))
    else
        paste -d' ' <(awk '{print $1, $2}' "$1") <(awk '{print $2}' "$2") |
            awk '$2 != $3 { print $1; found = 1; exit } END { if (!found && NR == 0) print 0 }'
        [ "$(wc -l < "$1")" = "$(wc -l < "$2")" ] || echo "(length)"
    fi
}

# sound_check <reference.snd> <run.snd> exact|superset: prints "; sound ..." and fails the
# run on a missing (or, for exact, an extra) effect. Nothing without a direct reference.
sound_ok=1
sound_check() {
    local out
    [ -s "$1" ] || { sound_note=""; return; }
    out=$(python3 scripts/netplay_sound_compare.py "$1" "$2" "$3") || sound_ok=0
    sound_note="; $out"
}

fail=0
for swr in "${SWRS[@]}"; do
    name=$(basename "$swr" -scalars.swr)
    for gp in "${GPS[@]}"; do
        ref=$OUT/plain-$name-$gp.trace
        if [ ! -s "$ref" ] || ! exited_ok "$OUT/plain-$name-$gp.log"; then
            echo "  $name-$gp: the plain run failed (see $OUT/plain-$name-$gp.log)"
            fail=1
            continue
        fi
        frames=$(wc -l < "$ref")
        for m in $MODES; do
            mt=$(mode_tag "$m")
            case "$m" in
            p2p*)
                tag=$mt-$name-$gp
                if grep -qs "cannot equal the plain run" "$OUT/$tag-1.log" "$OUT/$tag-2.log"; then
                    echo "  $name-$gp $m: skipped -- the recording has input in the frames the delay drops"
                    continue
                fi
                verdict=identical
                for p in 1 2; do
                    if ! exited_ok "$OUT/$tag-$p.log" || [ ! -s "$OUT/$tag-$p.trace" ]; then
                        verdict=FAILED
                    else
                        first=$(trace_diff "$ref" "$OUT/$tag-$p.trace" | head -1)
                        [ -n "$first" ] && verdict="DIFFERS (player $p from frame $first)"
                    fi
                done
                rolls=$(grep -hoE 'player [12]: [0-9]+ frames played, [0-9]+ re-simulated after [0-9]+ rollbacks' \
                            "$OUT/$tag-1.log" "$OUT/$tag-2.log" |
                        sed -E 's/player ([12]): [0-9]+ frames played, ([0-9]+) re-simulated after ([0-9]+) rollbacks/p\1 \3 (\2 frames)/' |
                        paste -sd' ' -)
                # What the emulated network actually did. A condition that never
                # fired is a run that tested nothing, and only the peer can say.
                net=""
                [ -n "$NETEM" ] && net=$(grep -hoE 'network emulation did: .*' "$OUT/$tag-1.log" | tail -1 |
                                         sed 's/^network emulation did: /; player 1 link: /')
                snd=""
                if [ "$verdict" = identical ]; then
                    for p in 1 2; do
                        sound_ok=1
                        sound_check "$OUT/direct-$name-$gp.snd" "$OUT/$tag-$p.snd" superset
                        [ -n "$sound_note" ] && snd="$snd; player $p ${sound_note#; }"
                        [ "$sound_ok" = 1 ] || verdict="SOUND WRONG"
                    done
                fi
                widths=""
                [ "${PEER_WIDTHS% *}" != "${PEER_WIDTHS#* }" ] &&
                    widths=" (SIM tier; rendering $PEER_WIDTHS wide at simulation width $SIM_WIDTH)"
                if [ "$verdict" = identical ]; then
                    echo "  $name-$gp $m: both peers identical to the plain run$widths ($frames frames); rollbacks $rolls$net$snd"
                else
                    echo "  $name-$gp $m: $verdict; rollbacks $rolls$net$snd"
                    for p in 1 2; do
                        grep -hE '^\[netplay\] FAIL|^\[netplay\]   |terminate called|what\(\)' "$OUT/$tag-$p.log" | head -4 | sed "s/^/    player $p: /"
                    done
                    echo "    (see $OUT/$tag-1.log, $OUT/$tag-2.log)"
                    fail=1
                fi ;;
            *)
                tag=$mt-$name-$gp log=$OUT/$tag.log trace=$OUT/$tag.trace
                session=$(grep -hoE '^\[netplay\] (synctest|direct): .*' "$log" | tail -1)
                session=${session#\[netplay\] }
                if ! exited_ok "$log" || [ ! -s "$trace" ]; then
                    echo "  $name-$gp $m: FAILED -- $(grep -hE '^\[netplay\] FAIL|^\[replay\] FAIL' "$log" | head -1)"
                    grep -hE '^\[netplay\]   ' "$log" | head -12
                    echo "    (see $log)"
                    fail=1
                elif ! cmp -s "$ref" "$trace"; then
                    line=$(cmp "$ref" "$trace" 2>&1 | sed -n 's/.*line \([0-9]*\).*/\1/p')
                    echo "  $name-$gp $m: $session; DIFFERS from the plain run from frame $(( ${line:-1} - 1 ))"
                    fail=1
                else
                    sound_ok=1 sound_note=""
                    [ "$m" != direct ] && sound_check "$OUT/direct-$name-$gp.snd" "$OUT/$tag.snd" exact
                    if [ "$sound_ok" = 1 ]; then
                        echo "  $name-$gp $m: $session; identical to the plain run ($frames frames)$sound_note"
                    else
                        echo "  $name-$gp $m: $session; identical to the plain run, but$sound_note"
                        fail=1
                    fi
                fi ;;
            esac
        done
    done
done

if [ "$fail" = 0 ]; then echo "PASS"; exit 0; else echo "FAIL"; exit 2; fi
