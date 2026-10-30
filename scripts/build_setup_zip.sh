#!/usr/bin/env bash
#
# build_setup_zip.sh — dist/OpenSWOS-Setup.zip: OpenSWOS Setup for every desktop.
#
#   scripts/build_setup_zip.sh
#
# The data setup (tools/swosdata) on its own, for a player whose game is not on
# the computer they run the setup on -- the Amiga above all: the setup cannot run
# there, so it fills the SWOS drawer on a PC or Mac. One download for any of them:
#
#   OpenSWOS-Setup/
#     ReadMe.txt              packaging/setup/ReadMe.txt
#     setup/  data/           the setup's own files (bin/setup, credits_icons.bmp),
#                             found ABOVE each program by both of them; setup/whdload
#                             is the WHDLoad package's wrapper (SwosData_MakeWhdload)
#     data_json/worlds/2627/  the default world's Amiga team files and competitions
#     amiga/SWOS/             the Amiga program, SWOS-Amiga.zip's drawer: the setup
#                             makes a ready drawer or WHDLoad package from it, so an
#                             Amiga player needs this one download
#     Windows/                openswos-setup.exe, swosdata.exe (static, no DLLs)
#     Linux/                  openswos-setup + libslint_cpp.so ($ORIGIN), swosdata
#     macOS/                  OpenSWOS Setup.app, swosdata (both universal)
#
# A PACKAGING step like build_installer.sh, run after build_dist.sh and
# build_macos.sh: the Mac's part comes from build_macos.sh
# (dist/OpenSWOS-Setup-macOS.zip) and must be the same commit as this tree. The
# Windows setup is (re)built here through build.bat, the Linux one with build.sh
# (in a Linux build tree, $SETUP_LINUX_BUILD, default ~/build/openswos-setup).
#
# Exit status: 0 done, 1 failed.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

OUT=dist/OpenSWOS-Setup.zip
MAC_PART=dist/OpenSWOS-Setup-macOS.zip
MAC_COMMIT=dist/OpenSWOS-Setup-macOS.commit
SD=tools/swosdata
WIN_BIN="$SD/build_win_static/bin/Release"
LINUX_BUILD="${SETUP_LINUX_BUILD:-$HOME/build/openswos-setup}"

fail() { echo "build_setup_zip: $*" >&2; exit 1; }

# One commit for every part, as for the rest of a drop.
[ -z "$(git status --porcelain --untracked-files=no)" ] ||
    fail "tracked files are modified -- commit first, a drop is one commit"
HEAD_FULL=$(git rev-parse HEAD)
[ -f "$MAC_PART" ] && [ -f "$MAC_COMMIT" ] ||
    fail "no $MAC_PART -- run scripts/build_macos.sh first"
MAC_FULL=$(git rev-parse --verify -q "$(cat "$MAC_COMMIT")^{commit}") ||
    fail "$MAC_PART was built from $(cat "$MAC_COMMIT"), which is not a commit here"
[ "$MAC_FULL" = "$HEAD_FULL" ] ||
    fail "$MAC_PART is $(cat "$MAC_COMMIT"), this tree is $(git rev-parse --short HEAD) -- rerun scripts/build_macos.sh"

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/Windows" "$STAGE/Linux" "$STAGE/setup" "$STAGE/data"

echo "== Windows: build.bat static"
WINSD="$(wslpath -w "$PWD/$SD")" || fail "no Windows side (wslpath)"
( cd /mnt/c && cmd.exe /c "cd /d $WINSD && build.bat static" ) >/dev/null 2>&1 ||
    fail "build.bat static failed -- run it in $SD to see why"
for f in openswos-setup.exe swosdata.exe; do
    cp "$WIN_BIN/$f" "$STAGE/Windows/" || fail "no $WIN_BIN/$f"
done

echo "== Linux: build.sh in $LINUX_BUILD"
BUILD_DIR="$LINUX_BUILD" "$SD/build.sh" >/dev/null || fail "$SD/build.sh failed"
cmake --install "$LINUX_BUILD" --prefix "$STAGE/Linux" --component app >/dev/null ||
    fail "cmake --install failed"
SLINT_SO=$(ldd "$LINUX_BUILD/bin/openswos-setup" | awk '/libslint_cpp/ { print $3 }')
[ -f "$SLINT_SO" ] || fail "cannot find the libslint_cpp.so openswos-setup links"
cp "$SLINT_SO" "$STAGE/Linux/" &&
    strip --strip-unneeded "$STAGE/Linux/libslint_cpp.so" "$STAGE/Linux/openswos-setup" \
                           "$STAGE/Linux/swosdata" || fail "strip failed"
readelf -d "$STAGE/Linux/openswos-setup" | grep -q 'path: \[\$ORIGIN\]' ||
    fail "the Linux openswos-setup does not look for its Slint beside itself (\$ORIGIN)"

echo "== the setup's own files"
# Tracked files only, keeping their folders (setup/whdload/...).
scripts/stage_runtime.sh "$STAGE" setup >/dev/null || fail "copying bin/setup failed"
# The default world's Amiga files (the setup installs them, SwosData_Run, and puts them in the
# drawer and the WHDLoad package): its team files and its competitions; not its teams' JSON,
# which only the PC game reads, beside itself.
scripts/stage_runtime.sh "$STAGE" data_json/worlds/2627/data data_json/worlds/2627/competitions >/dev/null ||
    fail "copying the world's files failed"

echo "== the Amiga program (dist/SWOS-Amiga.zip)"
AMIGA_ZIP=dist/SWOS-Amiga.zip
[ -f "$AMIGA_ZIP" ] || fail "no $AMIGA_ZIP -- run scripts/build_dist.sh first"
grep -q "commit=$(git rev-parse --short=8 HEAD)" dist/.build-status 2>/dev/null ||
    fail "$AMIGA_ZIP is not from this commit -- rerun scripts/build_dist.sh"
mkdir -p "$STAGE/amiga" && python3 -c "import zipfile,sys; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" \
    "$AMIGA_ZIP" "$STAGE/amiga" || fail "unpacking $AMIGA_ZIP failed"
cp bin/data/credits_icons.bmp "$STAGE/data/" &&
    cp packaging/setup/ReadMe.txt "$STAGE/ReadMe.txt" || fail "copying the ReadMe failed"

echo "== packing $OUT"
mkdir -p dist
rm -f "$OUT"
python3 - "$STAGE" "$MAC_PART" "$OUT.tmp" <<'PY' || { rm -f "$OUT.tmp"; fail "packing failed"; }
import os, stat, sys, zipfile

stage, mac_part, out = sys.argv[1:]
TOP = "OpenSWOS-Setup"
EXEC = {"openswos-setup", "swosdata", "libslint_cpp.so"}

with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for root, dirs, files in os.walk(stage):
        dirs.sort()
        for f in sorted(files):
            path = os.path.join(root, f)
            zi = zipfile.ZipInfo.from_file(path, TOP + "/" + os.path.relpath(path, stage))
            zi.create_system = 3                     # Unix: the modes below count
            zi.external_attr = (0o100755 if f in EXEC else 0o100644) << 16
            zi.compress_type = zipfile.ZIP_DEFLATED
            with open(path, "rb") as fh:
                z.writestr(zi, fh.read())
    # The Mac's part entry by entry, modes and symlinks as ditto stored them.
    links = 0
    with zipfile.ZipFile(mac_part) as m:
        for zi in m.infolist():
            if zi.filename.startswith("__MACOSX/"):
                continue
            if not zi.filename.startswith("macOS/"):
                sys.exit(f"{mac_part}: unexpected entry {zi.filename}")
            n = zipfile.ZipInfo(TOP + "/" + zi.filename, zi.date_time)
            n.create_system = 3
            n.external_attr = zi.external_attr
            mode = zi.external_attr >> 16
            links += stat.S_ISLNK(mode)
            n.compress_type = zipfile.ZIP_STORED if zi.is_dir() or stat.S_ISLNK(mode) \
                else zipfile.ZIP_DEFLATED
            z.writestr(n, m.read(zi))
    print(f"   macOS part: {links} symlink(s) kept")
PY
mv "$OUT.tmp" "$OUT" || fail "could not write $OUT"

echo ""
echo "== built from commit $(git rev-parse --short HEAD)"
zipinfo -1 "$OUT" | grep -v -E '/$|\.app/Contents/' | sed 's/^/   /'
zipinfo -1 "$OUT" | grep -o '^.*\.app/' | sort -u | sed 's/^/   /'

ls -l "$OUT"
