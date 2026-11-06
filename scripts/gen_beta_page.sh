#!/usr/bin/env bash
# gen_beta_page.sh -- the beta testers' page (packaging/web/beta/index.html -> dist/beta/index.html).
#
#   scripts/gen_beta_page.sh [--build HASH]
#
# Fills the template's @BUILD@, @STAMP@/@STAMP_ISO@ (now, UTC) and @ROWS@: one row per archive of
# the list below that is in dist/, with its size -- so the page never offers a file that is not
# being uploaded. The build is the commit's short hash (git), unless --build says which commit the
# archives are (the page then says that one, and an archive older than IT is left out); a
# -dirty tree is refused (a drop must be the commit it says it is: CLAUDE.md, "Build stamp").
# scripts/upload_beta.sh runs this.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

BUILD=""
while [ $# -gt 0 ]; do
    case "$1" in
        --build) BUILD="$2"; shift 2 ;;
        -h|--help) sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option $1" >&2; exit 1 ;;
    esac
done
if [ -z "$BUILD" ]; then
    BUILD=$(git rev-parse --short=8 HEAD)
    if ! git diff --quiet HEAD -- src include Makefile Makefile.amiga; then
        echo "gen_beta_page: the tree is -dirty: commit first" >&2; exit 1
    fi
fi

# file | platform | what it is
ROWS_SPEC='SWOS-Setup.exe|Windows|Installer; runs OpenSWOS Setup when it finishes
SWOS-portable.zip|Windows / Linux|Unzip, run <code>openswos-setup.exe</code> (Linux: <code>swosdata</code>) once, then play
SWOS-macOS-app.zip|macOS|SWOS.app and OpenSWOS Setup.app: run the setup once
SWOS-Amiga.zip|Amiga|A SWOS drawer: OpenSWOS Setup adds the game data and the mods, then copy it to your Amiga
OpenSWOS-Setup.zip|Windows / macOS / Linux|OpenSWOS Setup on its own: for the Amiga drawer and the WHDLoad package
OpenSWOS-Android.apk|Android|Install on the phone (allow installs from this source)
SWOS.zip|All|Binaries only: to update an install that already has its data'

human() {
    local b; b=$(stat -c %s "$1")
    awk -v b="$b" 'BEGIN { if (b >= 1048576) printf "%.1f MB", b/1048576; else printf "%d KB", (b+1023)/1024 }'
}

# An archive older than the commit is an earlier drop's (dist/ keeps them): left out, and said.
COMMIT_TIME=$(git log -1 --format=%ct "$BUILD")
rows=""
while IFS='|' read -r f plat what; do
    [ -f "dist/$f" ] || continue
    if [ "$(stat -c %Y "dist/$f")" -lt "$COMMIT_TIME" ]; then
        echo "gen_beta_page: dist/$f is older than $BUILD: left out (rebuild it to offer it)" >&2
        continue
    fi
    rows+="    <tr>\n      <td class=\"file\"><a href=\"$f\">$f</a></td>\n      <td class=\"plat\">$plat</td>\n      <td>$what</td>\n      <td class=\"size\">$(human "dist/$f")</td>\n    </tr>\n"
done <<< "$ROWS_SPEC"
[ -n "$rows" ] || { echo "gen_beta_page: no archives in dist/" >&2; exit 1; }

STAMP=$(date -u '+%Y-%m-%d %H:%M UTC')
STAMP_ISO=$(date -u '+%Y-%m-%dT%H:%M:%SZ')
mkdir -p dist/beta
python3 - "$BUILD" "$STAMP" "$STAMP_ISO" "$rows" <<'PY'
import sys
build, stamp, iso, rows = sys.argv[1:5]
t = open('packaging/web/beta/index.html', encoding='utf-8').read()
t = t.replace('@BUILD@', build).replace('@STAMP_ISO@', iso).replace('@STAMP@', stamp)
t = t.replace('@ROWS@', rows.replace('\\n', '\n').rstrip('\n'))
if '@' in t.replace('&#64;', ''):
    import re
    left = sorted(set(re.findall(r'@[A-Z_]+@', t)))
    if left:
        sys.exit('gen_beta_page: placeholders left: ' + ' '.join(left))
open('dist/beta/index.html', 'w', encoding='utf-8').write(t)
PY
echo "dist/beta/index.html: build $BUILD"
