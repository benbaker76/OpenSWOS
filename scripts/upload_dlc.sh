#!/usr/bin/env bash
# upload_dlc.sh -- World 2026-27's package and its credits to https://openswos.net/dlc/.
#
#   scripts/upload_dlc.sh [TAG] [--dry-run]
#
# OpenSWOS hosts the world's package itself since 2026-11-12 (until then the SWOS 96/97 Mod's
# Davide did): OpenSWOS Setup downloads it from PROJECT_WORLD_PACKAGE_URL and links its credits
# (include/project_identity.h). Make it first: scripts/make_world_package.sh writes
# dist/OpenSWOS-World-<TAG>.zip and its .txt together. A fork hosts its own (SWOS United: its DLC
# server) and points its project_identity.h there.
#
# Each file goes up beside the live one under a temporary name and is renamed over it: a download
# under way keeps the file it opened, and nobody gets half a package. The previous package is kept
# OUTSIDE the site, in /var/www/openswos-dlc-old/. The site serves /var/www/openswos.net as it is (packaging/server/nginx/
# openswos.net.conf): no nginx change. WEB_HOST as upload_web.sh (default root@74.208.5.140).
set -euo pipefail
cd "$(dirname "$0")/.."

TAG=2627
DRY=0
for a in "$@"; do
    case "$a" in
        --dry-run) DRY=1 ;;
        [0-9]*)    TAG=$a ;;
        *) echo "upload_dlc: unknown argument '$a'" >&2; exit 1 ;;
    esac
done
HOST=${WEB_HOST:-root@74.208.5.140}
WWW=/var/www/openswos.net/dlc
ZIP=dist/OpenSWOS-World-$TAG.zip
TXT=dist/OpenSWOS-World-$TAG.txt

for f in "$ZIP" "$TXT"; do
    [ -f "$f" ] || { echo "upload_dlc: no $f (scripts/make_world_package.sh $TAG)" >&2; exit 1; }
done
# The two must be the same package: the credits name the zip's fingerprint.
FP=$(unzip -p "$ZIP" "OpenSWOS-World-$TAG/WORLD.ini" | sed -n 's/^fingerprint *= *//p' | tr -d '\r')
grep -q "Fingerprint: $FP\." "$TXT" ||
    { echo "upload_dlc: $TXT is not $ZIP's (data $FP): make them again together" >&2; exit 1; }
grep -q "\"$FP\"" src/world_data.c ||
    echo "upload_dlc: (note) the package's data $FP is not this tree's src/world_data.c's" >&2

echo "to $HOST:$WWW: World $TAG, data $FP"
for f in "$ZIP" "$TXT"; do echo "  $(basename "$f") ($(du -h "$f" | cut -f1))"; done
[ "$DRY" = 1 ] && { echo "(dry run: nothing sent)"; exit 0; }

Z=$(basename "$ZIP"); T=$(basename "$TXT")
OLD=/var/www/openswos-dlc-old
ssh "$HOST" "mkdir -p $WWW $OLD"
scp -q "$ZIP" "$HOST:$WWW/.$Z.new"
scp -q "$TXT" "$HOST:$WWW/.$T.new"
ssh "$HOST" "cd $WWW && chmod a+r,go-w .$Z.new .$T.new &&
    { [ ! -f $Z ] || cp -p $Z $T $OLD/ 2>/dev/null || true; } && mv .$Z.new $Z && mv .$T.new $T"
echo "https://openswos.net/dlc/$Z is data $FP now"
