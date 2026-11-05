#!/usr/bin/env bash
#
# build_portmaster.sh — build the PortMaster port into dist/portmaster/swos.zip.
# (A folder of its own: on a Windows drive dist/swos.zip IS dist/SWOS.zip, the binaries-only
#  zip build_dist.sh writes after this -- it replaced the port in every drop until 2026-11-04.)
#
#   scripts/build_portmaster.sh             # build bin/SWOS.aarch64 and package
#   scripts/build_portmaster.sh --no-build  # package the existing binary
#
# Runs on aarch64 Linux, which bundles this machine's glibc as the port's fallback for
# firmware older than 2.34 -- or cross-compiles on another Linux with the aarch64 toolchain
# (make ARM64=1, as build_dist.sh), bundling that toolchain's glibc, the one the binary was
# linked against. See packaging/portmaster/README.md.
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD=1
[ "${1:-}" = "--no-build" ] && BUILD=0

if [ "$(uname -m)" = "aarch64" ]; then
    LIBDIR=/lib/aarch64-linux-gnu
elif command -v aarch64-linux-gnu-gcc >/dev/null 2>&1 && [ -d /usr/aarch64-linux-gnu/lib ]; then
    LIBDIR=/usr/aarch64-linux-gnu/lib        # (the cross toolchain's)
else
    echo "run this on aarch64 Linux, or install the aarch64-linux-gnu cross toolchain" >&2
    exit 1
fi
# The save verifier is on in a plain make and damages the running game: see build_dist.sh.
# Online play is C++ (GGPO-X): cross-compiled, it needs the aarch64 g++ as well.
NETPLAY=1
if [ "$(uname -m)" != "aarch64" ] && ! command -v aarch64-linux-gnu-g++ >/dev/null 2>&1; then
    echo "build_portmaster: no aarch64-linux-gnu-g++ -- building WITHOUT online play" \
         "(sudo apt install g++-aarch64-linux-gnu)" >&2
    NETPLAY=0
fi
[ "$BUILD" = 1 ] && make ARM64=1 NETPLAY=$NETPLAY SWOS_SAVE_FORMAT_JSON_VERIFY=0 -j"$(nproc)"

STAGE=dist/.portmaster-stage
SRC=packaging/portmaster
OUT=dist/portmaster/swos.zip
rm -rf "$STAGE" dist/portmaster
mkdir -p dist/portmaster
mkdir -p "$STAGE/swos/libs.aarch64/glibc" "$STAGE/swos/licenses"

cp -r "$SRC/swos/." "$STAGE/swos/"
cp "$SRC/Sensible World of Soccer.sh" "$SRC/port.json" "$SRC/README.md" "$STAGE/"
cp bin/SWOS.aarch64 "$STAGE/swos/"

# Tracked files only: a local bin/ can hold game data, which must never ship.
git ls-files -z bin/data bin/data_json bin/grafs bin/grafs_aga bin/sound bin/sound_dos bin/mods |
  while IFS= read -r -d '' f; do
    mkdir -p "$STAGE/swos/$(dirname "${f#bin/}")"
    cp "$f" "$STAGE/swos/${f#bin/}"
  done

for f in ld-linux-aarch64.so.1 libc.so.6 libm.so.6 libdl.so.2 libpthread.so.0 librt.so.1; do
  cp -L "$LIBDIR/$f" "$STAGE/swos/libs.aarch64/glibc/"
done
cp LICENSE "$STAGE/swos/licenses/OpenSWOS-LICENSE.txt"
cp /usr/share/common-licenses/LGPL-2.1 "$STAGE/swos/licenses/glibc-LGPL-2.1.txt"

(cd "$STAGE" && zip -qry "$(cd .. && pwd)/portmaster/swos.zip" .)
rm -rf "$STAGE"
unzip -Z1 "$OUT" | grep -xc "Sensible World of Soccer.sh" >/dev/null || { echo "build_portmaster: $OUT has no launcher" >&2; exit 1; }
echo "$OUT: $(du -h "$OUT" | cut -f1)"
