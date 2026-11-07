#!/usr/bin/env bash
# gen_shaders.sh -- the GPU presenter's shaders: src/shaders/*.glsl -> *.glsl.h (committed).
#
#   scripts/gen_shaders.sh
#
# sokol-shdc translates each annotated GLSL file into every backend's shader language: GLSL 410
# (desktop OpenGL), GLSL 300 es (WebGL2, Android), Metal (macOS, iOS) and HLSL 5 (D3D11). Its
# version must be the one third_party/sokol's headers came with (third_party/sokol/OPENSWOS.md):
# SOKOL_SHDC=<path to sokol-shdc>, else Jungool's copy beside them.
set -euo pipefail
cd "$(dirname "$0")/.."

case "$(uname -s)" in
    Darwin) bin=osx_arm64-sokol-shdc ;;
    CYGWIN*|MINGW*|MSYS*) bin=win32-sokol-shdc.exe ;;
    *) bin=linux-sokol-shdc ;;
esac
SHDC=${SOKOL_SHDC:-/mnt/d/Projects/GitHub/Jungool/tools/sokol-shdc/$bin}
[ -x "$SHDC" ] || { echo "no sokol-shdc at $SHDC (set SOKOL_SHDC)" >&2; exit 1; }

for src in src/shaders/*.glsl; do
    "$SHDC" -i "$src" -o "$src.h" -l glsl410:glsl300es:metal_macos:metal_ios:metal_sim:hlsl5 -f sokol_impl
    echo "$src -> $src.h"
done
