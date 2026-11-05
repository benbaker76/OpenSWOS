#!/usr/bin/env bash
# upload_beta.sh -- the drop in dist/ to the beta testers' page, https://openswos.net/beta/.
#
#   scripts/upload_beta.sh [--dry-run]
#
# Writes the page (scripts/gen_beta_page.sh) and copies it and the archives it lists to the server
# (BETA_HOST, default root@74.208.5.140, by ssh key) into /var/www/openswos-beta/new, OUTSIDE the
# site's root (so neither it nor the previous one is ever served unprotected), then swaps it in for
# /var/www/openswos.net/beta/ (the previous one kept as /var/www/openswos-beta/old), so a tester never
# sees half an upload.
# The folder is behind its own login (nginx auth_basic, .htpasswd-openswos-beta:
# packaging/server/nginx/openswos.net.conf); a tester's login is added on the server with
#   htpasswd -B /etc/nginx/.htpasswd-openswos-beta <name>
set -euo pipefail
cd "$(dirname "$0")/.."

HOST=${BETA_HOST:-root@74.208.5.140}
WWW=/var/www/openswos.net
STAGE=/var/www/openswos-beta
DRY=0
[ "${1:-}" = "--dry-run" ] && DRY=1

scripts/gen_beta_page.sh
files=$(grep -o 'href="[^"/]*"' dist/beta/index.html | sed 's/href="//; s/"//' | grep -v '^https\?:' | sort -u)
echo "to $HOST:$WWW/beta/:"
for f in $files; do echo "  $f ($(du -h "dist/$f" | cut -f1))"; done
[ $DRY = 1 ] && { echo "(dry run: nothing sent)"; exit 0; }

ssh "$HOST" "rm -rf $STAGE/new && mkdir -p $STAGE/new"
scp -q dist/beta/index.html $(for f in $files; do echo "dist/$f"; done) "$HOST:$STAGE/new/"
ssh "$HOST" "chown -R www-data:www-data $STAGE/new && chmod -R a+rX,go-w $STAGE/new && \
    rm -rf $STAGE/old && { [ ! -d $WWW/beta ] || mv $WWW/beta $STAGE/old; } && mv $STAGE/new $WWW/beta"
echo "https://openswos.net/beta/ is the drop in dist/ now"
