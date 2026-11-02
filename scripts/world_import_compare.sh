#!/usr/bin/env bash
#
# world_import_compare.sh -- the same World 2026-27 import from two sets of the Mod's packs,
# compared byte for byte (the contract between the Mod's pack.json and schema v1: whichever the
# packs came from, OpenSWOS imports the same world -- docs/RULES_LIBRARY.md §10).
#
#   scripts/world_import_compare.sh PACKS_A PACKS_B [MOD]
#
# PACKS_A, PACKS_B: folders of the Mod's packs as its world5tm has them -- <code>/pack.json,
#   clubs.csv, players.csv, ties.csv, cupd.json ... -- e.g. A the 4.0.5 release's, B the same
#   packs round-tripped through schema v1 (pack.json -> v1 -> pack.json).
# MOD: the rest of the Mod's data the import reads (default bin/data_json/worlds/2627/mod); its
#   packs/ is what A and B replace.
#
# Each import (scripts/world_import.py --mod --out --data-c) goes to a temporary folder; every
# file it writes must be the same in both: competitions/leagues.json, teams/*.json, data/TEAM.nnn,
# WORLD.ini (its fingerprint) and the baked tables (world_data.c). The packs are the Mod's,
# private: nothing is written outside the temporary folder, which is removed (KEEP=1 keeps it).
#
# Exit status: 0 the same, 1 different, 2 bad use or an import that failed.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 2

if [ $# -lt 2 ] || [ $# -gt 3 ]; then
    sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi
A=$(cd "$1" 2>/dev/null && pwd) || { echo "world_import_compare: no folder $1" >&2; exit 2; }
B=$(cd "$2" 2>/dev/null && pwd) || { echo "world_import_compare: no folder $2" >&2; exit 2; }
MOD=$(cd "${3:-bin/data_json/worlds/2627/mod}" 2>/dev/null && pwd) ||
    { echo "world_import_compare: no Mod data at ${3:-bin/data_json/worlds/2627/mod}" >&2; exit 2; }
for p in "$A" "$B"; do
    ls "$p"/*/pack.json > /dev/null 2>&1 || { echo "world_import_compare: no <code>/pack.json in $p" >&2; exit 2; }
done

TMP=$(mktemp -d)
[ -n "${KEEP:-}" ] || trap 'rm -rf "$TMP"' EXIT

for side in A B; do
    packs=$A; [ $side = B ] && packs=$B
    mkdir -p "$TMP/$side/mod"
    for f in "$MOD"/*; do                       # (the Mod's other data as it is; its packs replaced)
        [ "$(basename "$f")" = packs ] || ln -s "$f" "$TMP/$side/mod/$(basename "$f")"
    done
    ln -s "$packs" "$TMP/$side/mod/packs"
    if ! python3 -I scripts/world_import.py --mod "$TMP/$side/mod" --out "$TMP/$side/world" \
            --data-c "$TMP/$side/world_data.c" > "$TMP/$side.log" 2>&1; then
        echo "world_import_compare: the import from $packs failed:" >&2
        tail -5 "$TMP/$side.log" >&2
        exit 2
    fi
done

diffs=$( { (cd "$TMP/A/world" && find . -type f); (cd "$TMP/B/world" && find . -type f); } | sort -u |
         while read -r f; do cmp -s "$TMP/A/world/$f" "$TMP/B/world/$f" || echo "${f#./}"; done )
cbad=0
cmp -s "$TMP/A/world_data.c" "$TMP/B/world_data.c" || cbad=1
nd=$(printf '%s' "$diffs" | grep -c . || true)
if [ "$nd" -eq 0 ] && [ $cbad -eq 0 ]; then
    echo "SAME: $(cd "$TMP/A/world" && find . -type f | wc -l) files and world_data.c, byte for byte" \
         "(fingerprint $(sed -n 's/^fingerprint = //p' "$TMP/A/world/WORLD.ini"))"
    exit 0
fi
[ -n "$diffs" ] && printf 'DIFFERENT: %s\n' $diffs | head -20
if [ $cbad -eq 1 ]; then
    echo "DIFFERENT: world_data.c (the baked tables)"
    diff "$TMP/A/world_data.c" "$TMP/B/world_data.c" | head -6
fi
echo "DIFFERENT: $nd file(s)$( [ $cbad -eq 1 ] && echo ', and world_data.c' )"
[ -n "${KEEP:-}" ] && echo "(kept: $TMP)"
exit 1
