#!/usr/bin/env bash
#
# build_portmaster.sh — build the PortMaster port into dist/swos.zip.
#
#   scripts/build_portmaster.sh             # build bin/SWOS.aarch64 and package
#   scripts/build_portmaster.sh --no-build  # package the existing binary
#
# Runs on aarch64 Linux: it bundles this machine's glibc as the port's fallback
# for firmware older than 2.34. See packaging/portmaster/README.md.
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD=1
[ "${1:-}" = "--no-build" ] && BUILD=0

[ "$(uname -m)" = "aarch64" ] || { echo "run this on aarch64 Linux" >&2; exit 1; }
[ "$BUILD" = 1 ] && make ARM64=1 -j"$(nproc)"

STAGE=dist/portmaster
SRC=packaging/portmaster
rm -rf "$STAGE" dist/swos.zip
mkdir -p "$STAGE/swos/libs.aarch64/glibc" "$STAGE/swos/licenses"

cp -r "$SRC/swos/." "$STAGE/swos/"
cp "$SRC/Sensible World of Soccer.sh" "$SRC/port.json" "$SRC/README.md" "$STAGE/"
cp bin/SWOS.aarch64 "$STAGE/swos/"

# Tracked files only: a local bin/ can hold game data, which must never ship.
git ls-files -z bin/data bin/data_json bin/grafs bin/grafs_aga bin/sound bin/sound_dos |
  while IFS= read -r -d '' f; do
    mkdir -p "$STAGE/swos/$(dirname "${f#bin/}")"
    cp "$f" "$STAGE/swos/${f#bin/}"
  done

LIBDIR=/lib/aarch64-linux-gnu
for f in ld-linux-aarch64.so.1 libc.so.6 libm.so.6 libdl.so.2 libpthread.so.0 librt.so.1; do
  cp -L "$LIBDIR/$f" "$STAGE/swos/libs.aarch64/glibc/"
done
cp LICENSE "$STAGE/swos/licenses/OpenSWOS-LICENSE.txt"
cp /usr/share/common-licenses/LGPL-2.1 "$STAGE/swos/licenses/glibc-LGPL-2.1.txt"

(cd "$STAGE" && zip -qry ../swos.zip .)
echo "dist/swos.zip: $(du -h dist/swos.zip | cut -f1)"
