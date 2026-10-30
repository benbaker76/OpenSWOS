#!/usr/bin/env bash
#
# check_netplay_compat.sh -- did a change alter how a match plays without raising
# NETPLAY_COMPAT (include/netplay_compat.h)?
#
#   scripts/check_netplay_compat.sh             # check (bin/SWOS.x86_64 must be built)
#   scripts/check_netplay_compat.sh --update    # after raising NETPLAY_COMPAT: record it
#
# Two builds with the same NETPLAY_COMPAT play each other online, whatever their
# commits. This plays every recording in tests/dos through the netplay rig
# (scripts/netplay_test.sh --modes direct, both gameplays) and keeps, per recording
# and gameplay, a digest of every frame's SIM hash -- the state a desync detector
# compares. The record is tests/netplay_compat.txt:
#
#   netplay_compat <n>
#   <recording>-<on|off> <sha1 of the SIM hashes> <frames>
#
# It FAILS when a digest changed and NETPLAY_COMPAT is still the recorded number:
# raise the number, then --update. A raised number with the old digests is fine
# (a change the recordings cannot see, such as the link's messages) -- --update
# to record it. A recording that is not here (the corpus is the maintainer's own,
# not in git) is skipped with a note.
#
# What it cannot see: anything the recordings never reach (a booking, a shoot-out,
# the lobby, GGPO-X, the recorder's stream). Those raises are on whoever makes the
# change -- include/netplay_compat.h lists them.
#
# Exit status: 0 consistent (or recorded), 1 a raise is missing, 2 could not run.
set -uo pipefail
export SWOS_WORLD="${SWOS_WORLD:-9697}"   # (its corpus / careers are 96/97's, whatever SWOS.ini says)
cd "$(dirname "$0")/.." || exit 2

RECORD=tests/netplay_compat.txt
UPDATE=0
case "${1:-}" in
    --update) UPDATE=1 ;;
    "") ;;
    *) sed -n '3,8p' "$0" >&2; exit 2 ;;
esac

COMPAT=$(sed -n 's/^#define NETPLAY_COMPAT \([0-9]*\).*/\1/p' include/netplay_compat.h)
[ -n "$COMPAT" ] || { echo "no NETPLAY_COMPAT in include/netplay_compat.h" >&2; exit 2; }
[ -x bin/SWOS.x86_64 ] || { echo "build bin/SWOS.x86_64 first (make X86_64=1)" >&2; exit 2; }
ls tests/dos/*.RGD.gz >/dev/null 2>&1 || { echo "no recordings in tests/dos" >&2; exit 2; }

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
echo "== playing tests/dos through the netplay rig (netplay_test.sh --all --modes direct)"
if ! scripts/netplay_test.sh --all --modes direct --gameplay both --out "$OUT" > "$OUT/rig.log" 2>&1; then
    tail -20 "$OUT/rig.log" >&2
    echo "the rig failed: fix that first (a netplay step that differs from a plain run)" >&2
    exit 2
fi

# <recording>-<on|off> <digest> <frames>, sorted
NOW="$OUT/now.txt"
for t in "$OUT"/plain-*.trace; do
    name=$(basename "$t" .trace); name=${name#plain-}
    printf '%s %s %s\n' "$name" "$(awk '{ print $2 }' "$t" | sha1sum | cut -d' ' -f1)" "$(wc -l < "$t")"
done | sort > "$NOW"
[ -s "$NOW" ] || { echo "the rig wrote no traces" >&2; exit 2; }

if [ "$UPDATE" = 1 ]; then
    {
        echo "# NETPLAY_COMPAT and the SIM-hash digest of each test recording it was recorded with."
        echo "# Written by scripts/check_netplay_compat.sh --update; read by the same script."
        echo "netplay_compat $COMPAT"
        cat "$NOW"
    } > "$RECORD"
    echo "recorded NETPLAY_COMPAT $COMPAT, $(wc -l < "$NOW") digests -> $RECORD"
    exit 0
fi

[ -f "$RECORD" ] || { echo "no $RECORD: run with --update" >&2; exit 2; }
WAS=$(sed -n 's/^netplay_compat \([0-9]*\)$/\1/p' "$RECORD")

changed=0 same=0 missing=0
while read -r name digest frames; do
    old=$(awk -v n="$name" '$1 == n { print $2 " " $3 }' "$RECORD")
    if [ -z "$old" ]; then
        echo "  new      $name (not in the record)"
        changed=$((changed + 1))
    elif [ "$old" != "$digest $frames" ]; then
        echo "  CHANGED  $name"
        changed=$((changed + 1))
    else
        same=$((same + 1))
    fi
done < "$NOW"
while read -r name _; do
    case "$name" in \#*|netplay_compat) continue ;; esac
    grep -q "^$name " "$NOW" || { echo "  skipped  $name (no such recording here)"; missing=$((missing + 1)); }
done < "$RECORD"

echo "== $same unchanged, $changed changed, $missing not here; NETPLAY_COMPAT $COMPAT (recorded $WAS)"
if [ "$changed" -gt 0 ] && [ "$COMPAT" = "$WAS" ]; then
    echo "FAIL: matches play differently, but NETPLAY_COMPAT is still $COMPAT."
    echo "      Raise it in include/netplay_compat.h, then: scripts/check_netplay_compat.sh --update"
    exit 1
fi
if [ "$COMPAT" != "$WAS" ]; then
    echo "NETPLAY_COMPAT is $COMPAT, the record says $WAS: run --update to record it."
    exit 0
fi
echo "OK"
