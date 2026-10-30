#!/usr/bin/env bash
#
# career_golden_test.sh -- does today's career still play out exactly as it did?
#
#   scripts/career_golden_test.sh                  # check every career in tests/career
#   scripts/career_golden_test.sh --update         # record them (after a deliberate change)
#   scripts/career_golden_test.sh --twice          # also play each twice: the run is deterministic
#   scripts/career_golden_test.sh [--seasons N] [NAME ...]
#
# Phase 0 of docs/WORLD_2026_27.md. The careers are the NEW ones listed in
# tests/career_golden_careers.txt (a club per line, picked by the career picker's labels),
# plus any career save in tests/career (the original game's data, so local and untracked,
# like tests/dos). Each is played by the game's career autopilot
# (src/career_autopilot.c: --career-golden): every screen answered as a player would, every
# match of the career club simulated, the random numbers pinned. At each season-end hub it
# writes the game's own JSON career save -- tables, competitions, squads, finances -- and
# this compares their hashes with tests/career_golden.txt:
#
#   <career> <save> <sha1>          SEASON00 = the first hub, SEASON01 = one season on, ...
#
# A difference names the first season that parted. Look at what changed with
#   python3 -I scripts/career_golden_diff.py A.CAR B.CAR
# (the two saves), then fix the change -- or, if the change is meant, --update.
#
# The run uses default settings (HOME is a fresh folder, so no SWOS.ini) and reads the game
# data from bin/. The record is for bin/SWOS.x86_64: a 32-bit build lays the saves' raw
# competition blobs out differently.
#
# Exit status: 0 all equal (or recorded), 1 a career parted, 2 could not run.
set -uo pipefail
export SWOS_WORLD="${SWOS_WORLD:-9697}"   # (its corpus / careers are 96/97's, whatever SWOS.ini says)
cd "$(dirname "$0")/.." || exit 2

RECORD=tests/career_golden.txt
LIST=tests/career_golden_careers.txt
FIXTURES=tests/career
OUT=tests/career-golden
BIN=bin/SWOS.x86_64
SEASONS=5
UPDATE=0
TWICE=0
CAREERS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --update)  UPDATE=1; shift ;;
        --twice)   TWICE=1; shift ;;
        --seasons) SEASONS=$2; shift 2 ;;
        -h|--help) sed -n '3,26p' "$0"; exit 0 ;;
        *)         CAREERS+=("$1"); shift ;;
    esac
done

[ -x "$BIN" ] || { echo "build $BIN first (make X86_64=1)" >&2; exit 2; }
# NAME -> what --career-golden gets: new:<path> from the list, or a save in tests/career;
# and its world, for a line that names one (@<tag>).
declare -A WHAT WORLD ENVS
SKIPPED=()
while read -r name path; do
    case "$name" in ''|\#*) continue ;; esac
    while [[ "$path" =~ ^[A-Z_][A-Z0-9_]*=[^\ ]*\  ]]; do   # NAME=VALUE: in its run's environment
        ENVS[$name]+="${path%% *} "; path=${path#* }
    done
    if [[ "$path" == @* ]]; then
        world=${path%% *}; world=${world#@}; path=${path#* }
        if [ ! -d "bin/data_json/worlds/$world" ]; then SKIPPED+=("$name (no world $world here)"); continue; fi
        WORLD[$name]=$world
    fi
    WHAT[$name]="new:$path"
done < "$LIST"
for f in "$FIXTURES"/*.CAR; do
    [ -f "$f" ] && WHAT[$(basename "$f" .CAR)]="$(basename "$f")"
done
if [ ${#CAREERS[@]} -eq 0 ]; then
    mapfile -t CAREERS < <(printf '%s\n' "${!WHAT[@]}" | sort)
fi
for c in "${CAREERS[@]}"; do
    [ -n "${WHAT[$c]:-}" ] || { echo "no career $c (in $LIST or $FIXTURES)" >&2; exit 2; }
done

HOMEDIR=$(mktemp -d)
trap 'rm -rf "$HOMEDIR"' EXIT

# play <name> <dir>: the career's seasons into <dir>; 0 if all SEASONS were played.
play() {
    local what=${WHAT[$1]} dir=$2
    rm -rf "${dir:?}"
    mkdir -p "$dir"
    case "$what" in
        new:*) ;;
        *)     cp "$FIXTURES/$what" "$dir/" || return 2 ;;
    esac
    ( cd bin && env ${ENVS[$1]:-} HOME="$HOMEDIR" SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SWOS_HARNESS_TURBO=1 \
        timeout 900 ./SWOS.x86_64 ${WORLD[$1]:+--world=${WORLD[$1]}} \
        --career-golden="$what" --golden-seasons="$SEASONS" \
        --golden-out="$PWD/../$dir" ) > "$dir/run.log" 2>&1
    grep -q '^\[golden\] done' "$dir/run.log"
}

NOW=$(mktemp)
fail=0
for s in "${SKIPPED[@]}"; do echo "  skipped: $s"; done
for name in "${CAREERS[@]}"; do
    car=$name
    dir="$OUT/$name"
    if ! play "$car" "$dir"; then
        echo "  $car: FAILED to play $SEASONS season(s) -- $dir/run.log:"
        grep '^\[golden\]' "$dir/run.log" | tail -3 | sed 's/^/    /'
        fail=2
        continue
    fi
    for s in "$dir"/SEASON*.CAR; do
        echo "$name $(basename "$s" .CAR) $(sha1sum "$s" | cut -d' ' -f1)"
    done >> "$NOW"
    if [ "$TWICE" = 1 ]; then
        if ! play "$car" "$dir.2"; then
            echo "  $car: the second run FAILED"; fail=2; continue
        fi
        if ! diff -q <(cd "$dir" && sha1sum SEASON*.CAR) <(cd "$dir.2" && sha1sum SEASON*.CAR) >/dev/null; then
            echo "  $car: NOT DETERMINISTIC -- two runs differ ($dir, $dir.2)"; fail=1
        fi
    fi
    echo "  $car: $(ls "$dir"/SEASON*.CAR | wc -l) saves"
done

if [ "$UPDATE" = 1 ]; then
    [ "$fail" = 0 ] || { echo "not recorded: a career failed"; rm -f "$NOW"; exit "$fail"; }
    # Only the careers played are replaced: the rest of the record stays.
    KEEP=$(mktemp)
    if [ -f "$RECORD" ]; then
        grep -v '^#' "$RECORD" | awk -v played="$(printf '%s ' "${CAREERS[@]}")" \
            'BEGIN { n = split(played, p, " "); for (i = 1; i <= n; i++) skip[p[i]] = 1 }
             !($1 in skip)' > "$KEEP"
    fi
    {
        echo "# The golden career test (scripts/career_golden_test.sh): <career> <save> <sha1>,"
        echo "# played by bin/SWOS.x86_64 for $SEASONS season(s). --update rewrites it."
        sort "$NOW" "$KEEP"
    } > "$RECORD"
    rm -f "$KEEP"
    echo "recorded $(wc -l < "$NOW") saves of ${#CAREERS[@]} career(s) -> $RECORD"
    rm -f "$NOW"
    exit 0
fi

[ -f "$RECORD" ] || { echo "no $RECORD: run with --update"; rm -f "$NOW"; exit 2; }
while read -r name save sha; do
    want=$(awk -v n="$name" -v s="$save" '$1 == n && $2 == s { print $3 }' "$RECORD")
    if [ -z "$want" ]; then
        echo "  $name $save: not in the record"; fail=1
    elif [ "$want" != "$sha" ]; then
        echo "  $name $save: DIFFERENT from the record"; fail=1
    fi
done < <(sort "$NOW")
rm -f "$NOW"
[ "$fail" = 0 ] && echo "PASS" || echo "FAIL"
exit "$fail"
