#!/usr/bin/env bash
#
# build_macos.sh — build the macOS .app on a remote Mac over ssh and bring the
#                  zip back into this checkout's dist/.
#
#   scripts/build_macos.sh                # tester build -> both archives below
#   scripts/build_macos.sh --release      # public build (stock injuries + cards)
#   scripts/build_macos.sh --no-pull      # build the Mac's tree as-is
#
# What comes back:
#   dist/OpenSWOS-macOS.dmg  THE Mac download: SWOS.app with OpenSWOS Setup inside it
#                            (Contents/Helpers), SDL2.framework and the PortMaster port
#                            (this machine's dist/portmaster/swos.zip) -- drag it to
#                            Applications; its first start makes the game data. Signed with a
#                            Developer ID, notarized and stapled when the Mac has the keychain
#                            scripts/macos_signing_setup.sh makes (docs/MACOS_BUILD.md)
#   dist/SWOS-macOS-app.zip  the same app, zipped (itch.io's channel, older links)
#   dist/SWOS-macOS.zip      the universal binary alone — needs SDL2 installed
#                            system-wide, since the framework ships in the .app
#
# and the Mac's part of OpenSWOS-Setup.zip, which scripts/build_setup_zip.sh packs
# with the Windows and Linux setups (not handed out on its own):
#   dist/OpenSWOS-Setup-macOS.zip     macOS/ with OpenSWOS Setup.app and swosdata
#   dist/OpenSWOS-Setup-macOS.commit  the commit it was built from
#
# They land in dist/ beside build_dist.sh's archives, and are packed into the
# MAC'S dist/ too — the Mac is a checkout of this same repo, so it follows the
# same rule: bin/ is the runtime tree, dist/ is what a tester is handed.
#
# macOS is the one target scripts/build_dist.sh cannot produce: `make macos-app`
# hard-fails unless `uname -s` is Darwin and there is no cross-compile path. So
# this script drives a real Mac instead, and defaults to the SAME flags
# build_dist.sh uses so a drop is uniform across platforms:
#
#   SWOS_TEST_RULES=1               (tester build; --release turns it off)
#   SWOS_SAVE_FORMAT_JSON_VERIFY=0  (the Makefile's `?= 0` is inside the WASM
#                                    branch, so this defaults ON otherwise)
#
# It `git pull --ff-only`s first and PRINTS THE COMMIT IT BUILT. On 2026-08-17 a
# tester reported a bug fixed on the HTML build and still present on macOS; the
# cause was that the Mac was two commits behind, not anything Darwin-specific.
# Use --no-pull only when you deliberately want the Mac's working tree.
#
# The bundle is packed with `ditto -c -k --sequesterRsrc --keepParent`, NOT
# `zip -r`. SWOS.app/Contents/Frameworks/SDL2.framework is built out of symlinks
# (Versions/Current, and the top-level SDL2 -> Versions/Current/SDL2); plain zip
# follows them, which both bloats the archive and produces a bundle that fails
# to load. ditto preserves symlinks and resource forks.
#
# Overrides: MAC_HOST, MAC_PATH, MAC_ZIP.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

MAC_HOST="${MAC_HOST:-bbaker@192.168.1.200}"
MAC_PATH="${MAC_PATH:-~/Programming/OpenSWOS}"
MAC_ZIP="${MAC_ZIP:-SWOS-macOS-app.zip}"
MAC_ZIP_BIN="${MAC_ZIP_BIN:-SWOS-macOS.zip}"

JOBS=""
NOINJ=1
DO_PULL=1

usage() {
    awk 'NR >= 3 { if ($0 !~ /^#/) exit; sub(/^# ?/, ""); print }' "${BASH_SOURCE[0]}"
    cat <<EOF

Options:
  -j, --jobs N     parallel make jobs (default: the Mac's core count)
      --release    PUBLIC build: the testers' no-injuries setting compiled out
                   (default is a tester build, SWOS_TEST_RULES=1)
      --no-pull    don't git pull on the Mac first
  -h, --help       this text

Environment:
  MAC_HOST   ssh target      (default: $MAC_HOST)
  MAC_PATH   remote checkout (default: $MAC_PATH)
  MAC_ZIP    .app archive    (default: $MAC_ZIP)
  MAC_ZIP_BIN binary archive  (default: $MAC_ZIP_BIN)
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        -j|--jobs)  JOBS="$2"; shift 2 ;;
        --release)  NOINJ=0; shift ;;
        --no-pull)  DO_PULL=0; shift ;;
        -h|--help)  usage; exit 0 ;;
        *) echo "build_macos: unknown option '$1' (try --help)" >&2; exit 2 ;;
    esac
done

MAKE_FLAGS="SWOS_SAVE_FORMAT_JSON_VERIFY=0"
if [ "$NOINJ" = 1 ]; then MAKE_FLAGS="$MAKE_FLAGS SWOS_TEST_RULES=1"; else MAKE_FLAGS="$MAKE_FLAGS SWOS_TEST_RULES=0"; fi

banner() { echo ""; echo "======================================================================"; echo "== $*"; echo "======================================================================"; }

LOG="$(mktemp -t swos-macos-build.XXXXXX)"
trap 'rm -f "$LOG"' EXIT

banner "preflight: $MAC_HOST"
if ! ssh -o BatchMode=yes -o ConnectTimeout=10 "$MAC_HOST" true 2>/dev/null; then
    echo "build_macos: cannot ssh to $MAC_HOST with key auth." >&2
    echo "  Check the Mac is awake and reachable, and that your key is in its" >&2
    echo "  ~/.ssh/authorized_keys (ssh-copy-id $MAC_HOST)." >&2
    exit 1
fi
echo "-- ssh OK"

# The PortMaster port (built HERE: scripts/build_dist.sh's portmaster target, dist/portmaster/swos.zip)
# goes into the app zip's OpenSWOS/portmaster/, where the setup's Handheld screen finds it
# (it looks for portmaster/swos.zip in the folders above its own). Sent as
# dist/portmaster-swos.zip on the Mac; with none here, the Mac's old one is removed, so a
# stale port is never packed.
if [ -f dist/portmaster/swos.zip ]; then
    unzip -Z1 dist/portmaster/swos.zip | grep -xc "Sensible World of Soccer.sh" >/dev/null ||
        { echo "-- FAIL: dist/portmaster/swos.zip is not the PortMaster port (no launcher)"; exit 1; }
    ssh "$MAC_HOST" "mkdir -p $MAC_PATH/dist" && scp -q dist/portmaster/swos.zip "$MAC_HOST:$MAC_PATH/dist/portmaster-swos.zip" ||
        { echo "-- FAIL: sending dist/portmaster/swos.zip to the Mac"; exit 1; }
    echo "-- PortMaster port: dist/portmaster/swos.zip sent"
else
    ssh "$MAC_HOST" "rm -f $MAC_PATH/dist/portmaster-swos.zip"
    echo "build_macos: NOTE no dist/portmaster/swos.zip (scripts/build_dist.sh --only portmaster): the app zip's setup will ask for it" >&2
fi

banner "build on $MAC_HOST:$MAC_PATH"
# ssh does NOT preserve argv boundaries: it concatenates its arguments into a
# single command string that the REMOTE shell re-splits. So "$MAKE_FLAGS" (two
# words) would arrive as two arguments and shift everything after it — which is
# how `-j` once received a make variable ("SWOS_TEST_NO_INJURIES_CARDS=1", as it was). printf %q quotes each
# argument for re-input to the remote bash. The tilde in MAC_PATH survives as a
# literal (%q escapes it, the remote parser strips the backslash) and is expanded
# by the `eval cd` below.
REMOTE_ARGV="$(printf ' %q' "$MAC_PATH" "$MAKE_FLAGS" "$JOBS" "$MAC_ZIP" "$DO_PULL" "$MAC_ZIP_BIN")"
ssh "$MAC_HOST" "bash -s --$REMOTE_ARGV" 2>&1 <<'REMOTE' | tee "$LOG"
set -uo pipefail
REPO="$1"; FLAGS="$2"; JOBS="$3"; ZIP="$4"; DO_PULL="$5"; ZIPBIN="$6"

eval cd "$REPO" || { echo "!! no checkout at $REPO"; exit 1; }
[ -n "$JOBS" ] || JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

if [ "$DO_PULL" = 1 ]; then
    echo "== git pull --ff-only"
    git pull --ff-only || { echo "!! pull failed (diverged? use --no-pull)"; exit 1; }
    # third_party/ggpo-x is a submodule, and a plain pull does NOT move it: the
    # Mac would build the commit it was told to build with the WRONG GGPO, which
    # is the same class of trap as the stale checkout this script already
    # guards against -- and quieter, because it still compiles.
    git submodule update --init --recursive || {
        echo "!! submodule update failed"; exit 1; }
fi
echo "== HEAD: $(git log -1 --format='%h %ad %s' --date=short)"
DIRTY="$(git status --porcelain --untracked-files=no)"
[ -n "$DIRTY" ] && { echo "== WARNING: tracked files modified — this build is NOT $(git rev-parse --short HEAD) exactly:"; echo "$DIRTY"; }

echo "== make macos-app $FLAGS -j$JOBS"
# macos-app depends on macos-universal, which runs `make clean` between the
# x86_64 and arm64 slices itself — no manual clean needed. Command-line
# variables reach those recursive sub-makes via MAKEFLAGS.
make macos-app $FLAGS -j"$JOBS" || { echo "!! make macos-app failed"; exit 1; }

[ -d bin/SWOS.app ] || { echo "!! bin/SWOS.app was not produced"; exit 1; }
echo "== lipo: $(lipo -info bin/SWOS.app/Contents/MacOS/SWOS 2>&1)"

# The data setup (tools/swosdata): OpenSWOS ships none of the original game's
# data, so a Mac player needs it to play at all. Both halves are universal, like
# the game: the window, "OpenSWOS Setup.app", links a static Slint built for arm64
# and x86_64 (tools/swosdata/build_slint_macos.sh, once per Mac); the command line,
# swosdata, is plain C, built per slice and joined with lipo.
export PATH="/opt/homebrew/bin:/usr/local/bin:$HOME/.cargo/bin:$PATH"
echo "== data setup: swosdata (universal) + OpenSWOS Setup.app"
for a in arm64 x86_64; do
    make -C tools/swosdata OUT="build-$a" ARCHFLAGS="-arch $a -mmacosx-version-min=11.0" \
        "build-$a/swosdata" >/dev/null || { echo "!! swosdata ($a) failed"; exit 1; }
done
mkdir -p tools/swosdata/build &&
    lipo -create -output tools/swosdata/build/swosdata-universal \
         tools/swosdata/build-arm64/swosdata tools/swosdata/build-x86_64/swosdata ||
    { echo "!! lipo swosdata failed"; exit 1; }
( cd tools/swosdata && ./build.sh static >/dev/null ) || { echo "!! OpenSWOS Setup.app failed"; exit 1; }
SETUP_APP="tools/swosdata/build-app-static/bin/OpenSWOS Setup.app"
[ -d "$SETUP_APP" ] || { echo "!! $SETUP_APP was not produced"; exit 1; }
echo "== setup lipo: $(lipo -archs "$SETUP_APP/Contents/MacOS/OpenSWOS Setup" 2>&1)"
case "$(lipo -archs "$SETUP_APP/Contents/MacOS/OpenSWOS Setup" 2>/dev/null)" in
    *arm64*x86_64*|*x86_64*arm64*) ;;
    *) echo "!! OpenSWOS Setup is not universal"; exit 1 ;;
esac

mkdir -p dist
# ONE app (2026-11-14): SWOS.app with OpenSWOS Setup inside it, in Contents/Helpers (where a signed
# bundle keeps the apps it carries), and the PortMaster port in Contents/Resources/portmaster/ --
# a player drags the one app to Applications, and its first start makes the game data unattended
# (the no-data box's Make Game Data: the setup with --headless; src/assets_check.c).
WORK="$(mktemp -d)"
DMGROOT="$WORK/OpenSWOS"
APP="$DMGROOT/SWOS.app"
mkdir -p "$DMGROOT" &&
    ditto bin/SWOS.app "$APP" &&
    mkdir -p "$APP/Contents/Helpers" &&
    ditto "$SETUP_APP" "$APP/Contents/Helpers/OpenSWOS Setup.app" &&
    { [ ! -f dist/portmaster-swos.zip ] ||
      { mkdir -p "$APP/Contents/Resources/portmaster" &&
        cp dist/portmaster-swos.zip "$APP/Contents/Resources/portmaster/swos.zip"; }; } &&
    xattr -cr "$APP" ||
    { echo "!! assembling the app failed"; exit 1; }

# Signing (docs/MACOS_BUILD.md, "Signing and notarizing"): with the Developer ID keychain that
# scripts/macos_signing_setup.sh made on this Mac, every piece is signed from the inside out with
# the hardened runtime and a timestamp, and the DMG is notarized and stapled. Without it, an
# ad-hoc signature (the app opens with right-click -> Open) and no notarization.
SIGNDIR="$HOME/.openswos-signing"
KC="$HOME/Library/Keychains/openswos-signing.keychain-db"
NOTARY=0
if [ -f "$SIGNDIR/keychain.pass" ] && [ -f "$SIGNDIR/identity" ] && [ -f "$KC" ]; then
    security unlock-keychain -p "$(cat "$SIGNDIR/keychain.pass")" "$KC" || { echo "!! cannot unlock $KC"; exit 1; }
    ID="$(cat "$SIGNDIR/identity")"
    SIGN=(codesign --force --options runtime --timestamp --keychain "$KC" --sign "$ID")
    NOTARY=1
    echo "== signing with $ID"
else
    ID="-"
    SIGN=(codesign --force --sign -)
    echo "== WARNING: no signing keychain on this Mac (scripts/macos_signing_setup.sh): ad-hoc signed, NOT notarized"
fi
sign() { "${SIGN[@]}" "$@" >/dev/null 2>"$WORK/sign.err" || { echo "!! codesign $*:"; cat "$WORK/sign.err"; exit 1; }; }
# Deepest first: loose libraries, then frameworks, then the setup's app, then the game's.
while IFS= read -r -d '' f; do sign "$f"; done < <(find "$APP" -name '*.dylib' -type f -print0)
while IFS= read -r -d '' f; do sign "$f"; done < <(find "$APP" -name '*.framework' -type d -prune -print0)
sign "$APP/Contents/Helpers/OpenSWOS Setup.app"
sign "$APP"
codesign --verify --deep --strict "$APP" || { echo "!! the signed app does not verify"; exit 1; }
echo "== SIGNED_AS=$ID"

# The DMG: the app and a link to /Applications, to drag it onto.
DMG="OpenSWOS-macOS.dmg"
ln -s /Applications "$DMGROOT/Applications"
rm -f "dist/$DMG"
hdiutil create -quiet -volname OpenSWOS -srcfolder "$DMGROOT" -ov -format UDZO "dist/$DMG" ||
    { echo "!! hdiutil failed"; exit 1; }
[ "$ID" = "-" ] || sign "dist/$DMG"
rm "$DMGROOT/Applications"

if [ "$NOTARY" = 1 ]; then
    echo "== notarizing dist/$DMG (a few minutes)"
    OUT="$(xcrun notarytool submit "dist/$DMG" --keychain-profile openswos-notary --keychain "$KC" --wait 2>&1)"
    echo "$OUT" | sed 's/^/   /'
    if ! echo "$OUT" | grep -q "status: Accepted"; then
        SUB="$(echo "$OUT" | sed -n 's/^ *id: //p' | head -1)"
        [ -n "$SUB" ] && xcrun notarytool log "$SUB" --keychain-profile openswos-notary --keychain "$KC"
        echo "!! notarization was not accepted"; exit 1
    fi
    # The ticket covers every signature the DMG holds: the DMG, the app and the setup inside it.
    xcrun stapler staple "dist/$DMG" >/dev/null && xcrun stapler staple "$APP" >/dev/null &&
        xcrun stapler staple "$APP/Contents/Helpers/OpenSWOS Setup.app" >/dev/null ||
        { echo "!! stapling failed"; exit 1; }
    spctl -a -t open --context context:primary-signature -v "dist/$DMG" 2>&1 | sed 's/^/   /'
    spctl -a -t exec -v "$APP" 2>&1 | sed 's/^/   /'
    echo "== NOTARIZED=yes"
else
    echo "== NOTARIZED=no"
fi

echo "== packing dist/$ZIP (the same app; ditto — preserves the frameworks' symlinks)"
rm -f "dist/$ZIP"
ditto -c -k --sequesterRsrc --keepParent "$APP" "dist/$ZIP" || { echo "!! ditto failed"; exit 1; }
# zipinfo shows a leading 'l' in the mode column for a stored symlink. A plain
# `zip -r` would have followed them and this count would be 0.
echo "== SYMLINKS_IN_ZIP=$(zipinfo "dist/$ZIP" 2>/dev/null | grep -c '^l' || true)"
# Binaries-only archive, the macOS counterpart of SWOS.zip: just the universal
# binary, for someone who already has a bin/ tree. NOTE this one needs SDL2
# installed system-wide — the bundled SDL2.framework only ships inside the .app.
echo "== packing dist/$ZIPBIN (universal binary only)"
rm -f "dist/$ZIPBIN"
[ -f bin/SWOS.macos ] || { echo "!! bin/SWOS.macos missing"; exit 1; }
( cd bin && ditto -c -k --sequesterRsrc SWOS.macos "../dist/$ZIPBIN" ) \
    || { echo "!! ditto (binary) failed"; exit 1; }

# The Mac's part of OpenSWOS-Setup.zip (scripts/build_setup_zip.sh): a macOS/
# folder with the setup window and its command line. ditto, for the Slint dylib.
SETUP_PART=OpenSWOS-Setup-macOS.zip
echo "== packing dist/$SETUP_PART (the setup, for OpenSWOS-Setup.zip)"
rm -f "dist/$SETUP_PART"
# The setup's app is the one signed (and stapled) inside the game's; the command line is signed
# here and, signed, notarized with the folder (a bare program cannot be stapled: Gatekeeper asks
# Apple online the first time it runs).
STAGE="$WORK/macOS"
mkdir -p "$STAGE" &&
    ditto "$APP/Contents/Helpers/OpenSWOS Setup.app" "$STAGE/OpenSWOS Setup.app" &&
    cp tools/swosdata/build/swosdata-universal "$STAGE/swosdata" &&
    sign "$STAGE/swosdata" &&
    ditto -c -k --keepParent "$STAGE" "dist/$SETUP_PART" ||
    { echo "!! ditto (setup) failed"; exit 1; }
if [ "$NOTARY" = 1 ]; then
    echo "== notarizing dist/$SETUP_PART (swosdata)"
    # (the output taken whole first: `| grep -q` under pipefail failed an Accepted submission --
    # grep left at its first match and notarytool died of the broken pipe)
    OUT="$(xcrun notarytool submit "dist/$SETUP_PART" --keychain-profile openswos-notary --keychain "$KC" --wait 2>&1)"
    echo "$OUT" | grep "status:" | tail -1 | sed 's/^/   /'
    case "$OUT" in
        *"status: Accepted"*) ;;
        *) echo "!! notarization of the setup was not accepted"; exit 1 ;;
    esac
fi
rm -rf "$WORK"

echo "== BUILT_COMMIT=$(git rev-parse --short HEAD)"
echo "== ZIP_BYTES=$(stat -f%z "dist/$ZIP")"
echo "== ZIPBIN_BYTES=$(stat -f%z "dist/$ZIPBIN")"
echo "== REMOTE_OK"
REMOTE

grep -q "== REMOTE_OK" "$LOG" || { echo ""; echo "-- FAIL: remote build did not complete"; exit 1; }

banner "fetch -> dist/$MAC_ZIP, dist/$MAC_ZIP_BIN"
mkdir -p dist
scp -q "$MAC_HOST:$MAC_PATH/dist/OpenSWOS-macOS.dmg" dist/OpenSWOS-macOS.dmg || { echo "-- FAIL: scp (.dmg)"; exit 1; }
scp -q "$MAC_HOST:$MAC_PATH/dist/$MAC_ZIP"     "dist/$MAC_ZIP"     || { echo "-- FAIL: scp (.app)"; exit 1; }
scp -q "$MAC_HOST:$MAC_PATH/dist/$MAC_ZIP_BIN" "dist/$MAC_ZIP_BIN" || { echo "-- FAIL: scp (binary)"; exit 1; }
scp -q "$MAC_HOST:$MAC_PATH/dist/OpenSWOS-Setup-macOS.zip" dist/OpenSWOS-Setup-macOS.zip ||
    { echo "-- FAIL: scp (setup)"; exit 1; }
sed -n 's/.*BUILT_COMMIT=//p' "$LOG" | head -1 > dist/OpenSWOS-Setup-macOS.commit

banner "SUMMARY"
printf '  built from commit  : %s\n' "$(sed -n 's/.*BUILT_COMMIT=//p' "$LOG" | head -1)"
printf '  injuries + cards   : %s\n' \
    "$( [ "$NOINJ" = 1 ] && echo 'on; testers can switch them off OFFLINE in SWOS.ini (tester build — SWOS_TEST_RULES=1)' || echo 'on  (release build: the setting is compiled out)' )"
printf '  save-format verify : off\n'
printf '  flag in build      : %s compile lines with -DSWOS_TEST_RULES=1\n' \
    "$(grep -c -- '-DSWOS_TEST_RULES=1' "$LOG")"
printf '  slices             : %s\n' "$(grep -m1 '^== lipo:' "$LOG" | sed 's/^== lipo: //')"
printf '  setup slices       : %s\n' "$(grep -m1 '^== setup lipo:' "$LOG" | sed 's/^== setup lipo: //')"
printf '  symlinks preserved : %s (0 would mean SDL2.framework is broken)\n' \
    "$(sed -n 's/.*SYMLINKS_IN_ZIP=//p' "$LOG" | head -1)"
printf '  signed as          : %s\n' "$(sed -n 's/.*SIGNED_AS=//p' "$LOG" | head -1)"
printf '  notarized          : %s\n' "$(sed -n 's/.*NOTARIZED=//p' "$LOG" | head -1)"
echo ""
ls -l dist/OpenSWOS-macOS.dmg "dist/$MAC_ZIP" "dist/$MAC_ZIP_BIN" dist/OpenSWOS-Setup-macOS.zip
