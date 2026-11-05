#!/usr/bin/env bash
#
# upload_itch.sh — publish a finished drop to itch.io (https://baker76.itch.io/openswos).
#
#   scripts/upload_itch.sh              # push every archive to its channel
#   scripts/upload_itch.sh --dry-run    # check the drop and print what would go where
#   scripts/upload_itch.sh --status     # what the page holds now (butler status)
#   ITCH_DEV_DOWNLOADS=1 scripts/upload_itch.sh   # ... and the two developer downloads too
#
# Run after the drop (docs/TRIAGE.md §5): build_dist.sh, build_macos.sh, build_setup_zip.sh.
# Each archive goes to a channel of its own with butler, versioned by the build the
# binaries carry, so a new drop REPLACES the previous one's file instead of adding a
# copy. butler sends only what changed. (Not --if-changed: it compares a zip with the
# last build as if the zip were a folder, and fails -- "lstat X.zip/grafs_aga: not a
# directory".)
#
# butler must be logged in once (`butler login`, or BUTLER_API_KEY in the
# environment); its key lives in butler's own config, never here.
#
# NOT published, on purpose:
#   SWOS.nsp   the Switch package -- built with the NDA SDK
#   dist/web   the web build's SWOS.data preloads the assets folder, i.e. the
#              original game's data, which is never distributed
#
# The devlog post (the drop's changelog.html entry) has no API: post it by hand.
#
# Exit status: 0 done, 1 failed, 2 bad usage.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

TARGET="${ITCH_TARGET:-baker76/openswos}"
BUTLER="${BUTLER:-$(command -v butler || echo "$HOME/.local/bin/butler")}"

# archive -> channel. A channel name with windows/linux/mac in it is tagged with
# those platforms by itch.io.
CHANNELS=(
    "SWOS-Setup.exe:windows-installer"
    "SWOS-portable.zip:windows-linux-portable"
    "SWOS-macOS-app.zip:mac-app"
    "SWOS-Amiga.zip:amiga"
    "OpenSWOS-Setup.zip:setup-windows-mac-linux"
    "OpenSWOS-Android.apk:android"
    "OpenSWOS-iOS.ipa:ios"
    "portmaster/swos.zip:portmaster"
)
# The developer downloads -- the game programs alone, and the Mac program without its app
# (it needs SDL2 installed) -- are no use to a player, so the page does not offer them
# (taken off 2026-10-21). ITCH_DEV_DOWNLOADS=1 pushes them again, to the same channels.
if [ "${ITCH_DEV_DOWNLOADS:-0}" = 1 ]; then
    CHANNELS+=(
        "SWOS-macOS.zip:mac-binary"
        "SWOS.zip:binaries-windows-linux-mac-amiga"
    )
fi

DRY=0
case "${1:-}" in
    "") ;;
    --dry-run) DRY=1 ;;
    --status) exec "$BUTLER" status "$TARGET" ;;
    -h|--help) sed -n '2,25p' "$0"; exit 0 ;;
    *) echo "upload_itch: unknown option '$1' (try --help)" >&2; exit 2 ;;
esac

fail() { echo "upload_itch: $*" >&2; exit 1; }

[ -x "$BUTLER" ] || fail "no butler (https://itch.io/docs/butler/installing.html; or BUTLER=)"

# ---- the drop is ONE commit, and it is this tree ------------------------------
STATUS=$(cat dist/.build-status 2>/dev/null) || fail "no dist/.build-status -- build the drop first"
case "$STATUS" in done\ rc=0*) ;; *) fail "the drop did not finish: $STATUS" ;; esac
DROP=$(sed -n 's/.* commit=\([^ ]*\).*/\1/p' <<<"$STATUS")
[ -n "$DROP" ] || fail "dist/.build-status names no commit (a drop from before 2026-10-20) -- rebuild it"
case "$DROP" in *-dirty) fail "the drop was built from a -dirty tree ($DROP)" ;; esac
git rev-parse -q --verify "$DROP^{commit}" >/dev/null || fail "$DROP is not a commit here"
# Only what no archive holds may have moved on since: the changelog (written after
# the drop), the server's nginx sites and landing page, the docs (README and INSTALL too),
# this script and the
# fetch scripts (tools for us, in no archive).
NOT_SHIPPED=(':!packaging/web/changelog.html' ':!packaging/server' ':!packaging/web/site' ':!docs' ':!.claude' ':!scripts/upload_itch.sh'
             ':!scripts/fetch_reports.py' ':!scripts/fetch_recordings.py' ':!README.md' ':!INSTALL.md'
             ':!scripts/prune_recordings.sh')
git diff --quiet "$DROP" HEAD -- . "${NOT_SHIPPED[@]}" ||
    fail "the tree has changed since the drop ($DROP) -- rebuild it"
[ -z "$(git status --porcelain --untracked-files=no -- . "${NOT_SHIPPED[@]}")" ] ||
    fail "tracked files are modified"
MAC=$(cat dist/OpenSWOS-Setup-macOS.commit 2>/dev/null) || fail "no macOS part -- run scripts/build_macos.sh"
[ "$(git rev-parse "$MAC^{commit}" 2>/dev/null)" = "$(git rev-parse "$DROP^{commit}")" ] ||
    fail "the macOS archives are $MAC, the drop is $DROP -- rerun scripts/build_macos.sh"
# The phones' (scripts/build_android.sh, build_ios.sh), built after the drop: of its commit.
for p in Android iOS; do
    PC=$(cat "dist/OpenSWOS-$p.commit" 2>/dev/null) || fail "no $p build -- run scripts/build_$(tr A-Z a-z <<<"$p").sh"
    [ "$(git rev-parse "$PC^{commit}" 2>/dev/null)" = "$(git rev-parse "$DROP^{commit}")" ] ||
        fail "the $p build is $PC, the drop is $DROP -- rerun scripts/build_$(tr A-Z a-z <<<"$p").sh"
done
grep -q "<code>$DROP</code>" packaging/web/changelog.html ||
    echo "upload_itch: WARNING: no changelog.html entry for $DROP" >&2

# Every archive present and no older than the drop's build.
for c in "${CHANNELS[@]}"; do
    f="dist/${c%%:*}"
    [ -f "$f" ] || fail "missing $f"
    [ "$f" -nt dist/.build-status ] && [ "${c%%:*}" != OpenSWOS-Setup.zip ] && \
        [ "${c%%:*}" != SWOS-macOS-app.zip ] && [ "${c%%:*}" != SWOS-macOS.zip ] && \
        [ "${c%%:*}" != OpenSWOS-Android.apk ] && [ "${c%%:*}" != OpenSWOS-iOS.ipa ] && \
        fail "$f is newer than the drop -- rebuild it"
done
# The port really is the port (until 2026-11-04 dist/swos.zip WAS dist/SWOS.zip on a Windows
# drive, and the binaries went up as the PortMaster download).
unzip -Z1 dist/portmaster/swos.zip | grep -xc "Sensible World of Soccer.sh" >/dev/null ||
    fail "dist/portmaster/swos.zip is not the PortMaster port (no launcher)"
[ "dist/OpenSWOS-Setup.zip" -nt "dist/OpenSWOS-Setup-macOS.zip" ] ||
    fail "dist/OpenSWOS-Setup.zip is older than the macOS part -- rerun scripts/build_setup_zip.sh"

echo "== drop $DROP -> $TARGET"
for c in "${CHANNELS[@]}"; do
    printf '   %-22s -> %s\n' "${c%%:*}" "${c#*:}"
done

if [ "$DRY" = 1 ]; then
    for c in "${CHANNELS[@]}"; do
        "$BUTLER" push --dry-run "dist/${c%%:*}" "$TARGET:${c#*:}" --userversion "$DROP" >/dev/null ||
            fail "butler refused dist/${c%%:*}"
    done
    echo "== dry run: nothing was sent"
    exit 0
fi

"$BUTLER" status "$TARGET" >/dev/null 2>&1 ||
    fail "butler cannot reach $TARGET -- run 'butler login' first"

for c in "${CHANNELS[@]}"; do
    echo "== ${c%%:*}"
    "$BUTLER" push "dist/${c%%:*}" "$TARGET:${c#*:}" --userversion "$DROP" ||
        fail "pushing dist/${c%%:*} failed"
done
echo ""
"$BUTLER" status "$TARGET"
echo "== published $DROP; now post the devlog (changelog.html's entry) on itch.io by hand"
