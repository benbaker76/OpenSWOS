#!/usr/bin/env bash
# make_world_package.sh [TAG] -- dist/OpenSWOS-World-<TAG>.zip: a world's data, for OpenSWOS
# Setup to import (docs/WORLD_2026_27.md, phase 3).
#
# World 2026-27's squads are their authors' (the SWOS 96/97 Mod's data), so OpenSWOS does not
# ship them: this package is hosted apart, and the player's OpenSWOS Setup installs it like
# the original game's data (its "World 2026-27" source: tools/swosdata SdSources_OpenWorld,
# step_world). It is scripts/world_import.py's output, unchanged:
#
#   OpenSWOS-World-2627/
#     WORLD.ini        tag, name, the fingerprint of the files below
#     teams/           the PC game's team files (JSON)
#     competitions/    leagues.json
#     data/            the Amiga's team files (TEAM.nnn)
#
# The fingerprint is baked into the game too (src/world_data.c World_DataFingerprint): a
# package belongs with the builds made from the same import run. Make a new one, and upload
# it, whenever world_import.py's output changes (the fingerprint in WORLD.ini says so). The
# matchmaking server's team list is refreshed here too (assign_team_uuids.py --worlds).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TAG="${1:-2627}"
SRC="$ROOT/bin/data_json/worlds/$TAG"
OUT="$ROOT/dist/OpenSWOS-World-$TAG.zip"

for f in WORLD.ini competitions/leagues.json; do
    [ -f "$SRC/$f" ] || { echo "make_world_package: no $SRC/$f -- run scripts/world_import.py first" >&2; exit 1; }
done
FP=$(sed -n 's/^fingerprint *= *//p' "$SRC/WORLD.ini")
grep -q "\"$FP\"" "$ROOT/src/world_data.c" ||
    { echo "make_world_package: WORLD.ini's data ($FP) is not src/world_data.c's -- rerun world_import.py" >&2; exit 1; }

mkdir -p "$ROOT/dist"
python3 -I - "$SRC" "$OUT.tmp" "OpenSWOS-World-$TAG" <<'PY'
import os, sys, zipfile
src, out, top = sys.argv[1:]
n = 0
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    z.write(os.path.join(src, "WORLD.ini"), top + "/WORLD.ini")
    for sub in ("teams", "competitions", "data"):
        for f in sorted(os.listdir(os.path.join(src, sub))):
            path = os.path.join(src, sub, f)
            if os.path.isfile(path):
                z.write(path, "%s/%s/%s" % (top, sub, f))
                n += 1
print("%d files" % n)
PY
mv "$OUT.tmp" "$OUT"
echo "$OUT: $(du -h "$OUT" | cut -f1), data $FP"

# The matchmaking server's team list carries the world's teams (their ids and checksums: an
# online match's teams are checked against it): it goes with the same data.
python3 -I "$ROOT/scripts/assign_team_uuids.py" --registry --worlds | grep "^registry:"
if ! git -C "$ROOT" diff --quiet -- third_party/matchmaker/Server/include/TeamRegistry.inc; then
    echo "make_world_package: the server's team list changed -- commit TeamRegistry.inc, then rebuild"
    echo "and deploy the server (its admin page) when the package goes up"
fi
