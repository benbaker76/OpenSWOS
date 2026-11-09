#!/usr/bin/env bash
# upload_web.sh -- the web version (dist/web: make WASM=1) to https://openswos.net/game/.
#
#   scripts/upload_web.sh [--dry-run]
#
# The game in the browser, its data built in (the owner's call, 2026-10-21; nginx:
# packaging/server/nginx/openswos.net.conf, location /game/). Build it first, from a CLEAN tree --
# the page shows the build stamp, and a -dirty one is refused here:
#   . <emsdk>/emsdk_env.sh && rm -f dist/web/SWOS.html && make WASM=1 -j"$(nproc)"
# The four files go to a stage OUTSIDE the site (WEB_HOST, default root@74.208.5.140, by ssh key),
# then it is swapped in for /var/www/openswos.net/game in one move -- nobody loads half an update;
# the previous one is kept as /var/www/openswos-game-old.
set -euo pipefail
cd "$(dirname "$0")/.."

HOST=${WEB_HOST:-root@74.208.5.140}
WWW=/var/www/openswos.net/game
STAGE=/var/www/openswos-game-new
OLD=/var/www/openswos-game-old
FILES="SWOS.html SWOS.js SWOS.wasm SWOS.data"

for f in $FILES; do [ -f "dist/web/$f" ] || { echo "upload_web: no dist/web/$f (make WASM=1)" >&2; exit 1; }; done
stamp=$(grep -ao "OpenSWOS [0-9a-f]\{8\}[-a-z]*" dist/web/SWOS.wasm | head -1 || true)
case "$stamp" in
    *-dirty*|"") echo "upload_web: the build is '${stamp:-unstamped}': build it from a clean tree" >&2; exit 1 ;;
esac
echo "to $HOST:$WWW: $stamp"
for f in $FILES; do echo "  $f ($(du -h "dist/web/$f" | cut -f1))"; done
[ "${1:-}" = "--dry-run" ] && { echo "(dry run: nothing sent)"; exit 0; }

ssh "$HOST" "rm -rf $STAGE && mkdir -p $STAGE"
scp -q $(for f in $FILES; do echo "dist/web/$f"; done) "$HOST:$STAGE/"
ssh "$HOST" "chmod -R a+rX,go-w $STAGE && rm -rf $OLD && { [ ! -d $WWW ] || mv $WWW $OLD; } && mv $STAGE $WWW"
echo "https://openswos.net/game/ is $stamp now"
