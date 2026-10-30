#!/usr/bin/env bash
# stage_runtime.sh DEST DIR... -- copy the git-TRACKED files of bin/DIR... into DEST.
#
#   scripts/stage_runtime.sh dist/windows data grafs_aga sound_dos setup
#
# Every drop (SWOS-portable.zip, the Windows installer, the Amiga drawer, the macOS
# .app) is staged through this, and only tracked files go in. bin/ is also the
# working folder every target runs from, so it can hold anything a developer put
# there -- above all the original game's data, which OpenSWOS does not ship: the
# player's own copy is turned into it by tools/swosdata, into the assets folder.
# Staging from `git ls-files` rather than the folder means a local copy of that
# data, a save, a log or a test zip can never ride along into a release.
#
# Paths keep their place under bin/ (bin/grafs_aga/X -> DEST/grafs_aga/X). A DIR
# with nothing tracked is skipped and named on stderr. Prints the DIRs staged.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[ $# -ge 2 ] || { echo "usage: $0 DEST DIR..." >&2; exit 2; }
DEST="$1"; shift
case "$DEST" in /*) ;; *) DEST="$PWD/$DEST" ;; esac
mkdir -p "$DEST"

cd "$ROOT/bin"
STAGED=""
for d in "$@"; do
    n=0
    while IFS= read -r -d '' f; do
        mkdir -p "$DEST/$(dirname "$f")"
        cp "$f" "$DEST/$f"
        n=$((n + 1))
    done < <(git ls-files -z -- "$d" ':!:*/worlds/*/mod/*')   # (a world's sources: the importer's, not the game's)
    if [ "$n" -gt 0 ]; then STAGED="$STAGED $d"
    else echo "stage_runtime: nothing tracked in bin/$d" >&2; fi
done
echo "${STAGED# }"
