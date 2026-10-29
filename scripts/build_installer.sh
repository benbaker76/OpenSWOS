#!/usr/bin/env bash
#
# build_installer.sh — stage the Windows runtime tree and compile the Inno Setup
#                      installer, dist/SWOS-Setup.exe.
#
#   scripts/build_installer.sh                  # stage + compile
#   scripts/build_installer.sh --stage-only     # stage dist/windows, don't run ISCC
#   scripts/build_installer.sh --version 1.0.1  # override the version in the .iss
#   scripts/build_installer.sh --output PATH    # somewhere other than dist/SWOS-Setup.exe
#
# It does NOT build anything. bin/SWOS.exe has to exist already — build it with
#
#     scripts/build_dist.sh --only windows
#
# which is also what calls this script as its last packaging step, so a plain
# `scripts/build_dist.sh` produces the installer along with the zips.
#
# dist/ holds everything a tester is handed; dist/windows is this script's
# staging tree, packaged into dist/SWOS-Setup.exe beside the zips. Both are
# gitignored.
#
# WHY THERE IS A STAGING STEP
#
# bin/ is the SHARED data directory for every target in this tree, and the SDL
# binary chdir's into it — so it holds the Linux ELFs, the m68k SWOS2 and its
# .map, the WASM bundle, the release zips, FS-UAE .uaem sidecars, SWOS.log, and
# 24 MB of test careers under save/. A wildcard sweep of bin/ would ship all of
# it. Copying an explicit list into dist/windows first makes "what ships" one
# reviewable list (below) instead of a wildcard nobody audits, and keeps
# machine-specific paths — the Cygwin DLLs live outside the tree — out of the
# .iss.
#
# WHAT SHIPS
#
# The data list mirrors SWOS-portable.zip in scripts/build_dist.sh, which in turn
# mirrors what `make macos-app` bundles: this project's tested definition of
# "what the game needs at runtime". Keep the three in step — an installer that
# ships a different set from the zip generates bug reports that reproduce on one
# and not the other.
#
# save/ is deliberately NOT staged. Saves go to %USERPROFILE%\Documents\SWOS
# (swos_io.c), so shipping bin/save would drop 24 MB of stale test careers into
# Program Files where the game never looks for them.
#
# THE CYGWIN RUNTIME
#
# bin/SWOS.exe is a CYGWIN binary and will not start without its runtime beside
# the exe. That is FOUR DLLs, only two of which cygcheck can see — see the long
# comment above DLLS below, and keep it in step with FULL_DLLS in
# scripts/build_dist.sh so the installer and SWOS-portable.zip ship the same set.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

# ---- machine-specific paths (override via the environment) ------------------
CYGWIN_BIN="${CYGWIN_BIN:-/mnt/c/cygwin64/bin}"
# Inno Setup 7 installs 64-bit by default (Program Files), 6 was 32-bit
# (Program Files (x86)). Both roots are searched, newest version first.
ISCC="${ISCC:-}"

STAGE_DIR="dist/windows"
ISS="packaging/windows/swos.iss"
OUTPUT="dist/SWOS-Setup.exe"
VERSION=""
STAGE_ONLY=0

# Mirrors FULL_DATA_DIRS in scripts/build_dist.sh. See "WHAT SHIPS" above.
DATA_DIRS="data grafs grafs_aga grafs_dos grafs_2020 grafs_arcade grafs_jag sound sound_dos data_json setup classic"
# The data setup's window (tools/swosdata/build.bat static): the installer runs
# it when it finishes, because the game cannot start without the player's data.
SETUP_EXE="${SETUP_EXE:-tools/swosdata/build_win_static/bin/Release/openswos-setup.exe}"
# Cygwin runtime for SWOS.exe — mirrors FULL_DLLS in build_dist.sh.
# FOUR files, and only two of them are visible to
# cygcheck:
#
#   cygwin1.dll         the Cygwin runtime
#   cygSDL2-2-0-0.dll   NOT SDL2 -- it is sdl2-compat, a ~350 KB SHIM
#   cygSDL3-0.dll       the actual SDL, dlopen'd BY that shim at runtime
#   cygiconv-2.dll      a link-time import of cygSDL3-0.dll
#
# Cygwin now ships SDL2 as sdl2-compat, which implements the SDL2 API on top of
# SDL3 and loads cygSDL3-0.dll with dlopen. A dlopen is invisible to cygcheck
# and to the PE import table, so `cygcheck bin/SWOS.exe` lists only the first
# two -- and a drop built from that list fails on any machine without Cygwin
# with the shim's own message:
#
#     Failed loading SDL3 library.
#
# That is a real tester report (2026-09-07), reproduced here by running the exe
# with just those two DLLs and a PATH of C:\Windows\system32 only. Dropping
# cygiconv-2.dll gives the SAME message, because SDL3 then fails to load and the
# shim reports its own failure rather than the cause.
#
# So: cygcheck is still the right tool, but run it on cygSDL3-0.dll TOO, not
# only on the exe -- it cannot see a library that is loaded at runtime. It is
# still wrong to derive this list with `strings`, which picks up every DLL name
# mentioned anywhere, including backends SDL only ever dlopens, and lands you at
# ~14 MB of X11/pulse/codec libraries that are never loaded.
#
# NOT the SDL2.dll from a normal SDL release: that is a native MSVC/MinGW build
# with no cygwin1.dll import, and a Cygwin binary cannot load it.
DLLS="cygwin1.dll cygSDL2-2-0-0.dll cygSDL3-0.dll cygiconv-2.dll"

usage() {
    awk 'NR >= 3 { if ($0 !~ /^#/) exit; sub(/^# ?/, ""); print }' "${BASH_SOURCE[0]}"
    cat <<EOF

Options:
      --version V     installer version (default: the AppVersion in $ISS)
      --output PATH   installer path (default: $OUTPUT)
      --stage-only    stage $STAGE_DIR and stop
  -h, --help          this text

Environment:
  ISCC          full path to ISCC.exe (default: search both Program Files roots)
  CYGWIN_BIN    where the Cygwin DLLs live (default: $CYGWIN_BIN)

Exit codes:
  0  installer written        2  bad arguments
  1  something failed         3  Inno Setup is not installed (a skip, not a fail)
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --version)    VERSION="$2"; shift 2 ;;
        --output)     OUTPUT="$2"; shift 2 ;;
        --stage-only) STAGE_ONLY=1; shift ;;
        -h|--help)    usage; exit 0 ;;
        *) echo "build_installer: unknown option '$1' (try --help)" >&2; exit 2 ;;
    esac
done

die() { echo "build_installer: $*" >&2; exit 1; }

# ---- prerequisites ----------------------------------------------------------
[ -f bin/SWOS.exe ] ||
    die "bin/SWOS.exe not found — build it first:
                            scripts/build_dist.sh --only windows"
[ -f "$ISS" ] || die "$ISS not found"

# ---- stage ------------------------------------------------------------------
echo "== staging $STAGE_DIR"
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR" || die "cannot create $STAGE_DIR"

cp bin/SWOS.exe "$STAGE_DIR/" || die "cannot stage bin/SWOS.exe"

DLL_OK=""
DLL_MISS=""
for d in $DLLS; do
    if [ -f "$CYGWIN_BIN/$d" ]; then
        cp "$CYGWIN_BIN/$d" "$STAGE_DIR/" || die "cannot stage $CYGWIN_BIN/$d"
        DLL_OK="$DLL_OK $d"
    else
        DLL_MISS="$DLL_MISS $d"
    fi
done
if [ -n "$DLL_MISS" ]; then
    # Not fatal — an installer without them is still useful for checking the
    # wizard — but it will produce an unlaunchable install, so say so loudly.
    echo "!! Cygwin DLLs not found in $CYGWIN_BIN:$DLL_MISS" >&2
    echo "!! the installed SWOS.exe WILL NOT START. Set CYGWIN_BIN." >&2
fi

# Only git-tracked files (scripts/stage_runtime.sh): the project's own data. The
# original game's is made from the player's copy by the setup below, and a copy
# of it in a developer's bin/ must never be packaged.
STAGED_DIRS="$(scripts/stage_runtime.sh "$STAGE_DIR" $DATA_DIRS)" || die "cannot stage bin/"
if [ -f "$SETUP_EXE" ]; then
    cp "$SETUP_EXE" "$STAGE_DIR/" || die "cannot stage $SETUP_EXE"
else
    die "no $SETUP_EXE: build it with tools/swosdata/build.bat static (the installer runs it)"
fi
find "$STAGE_DIR" -name .DS_Store -delete 2>/dev/null
find "$STAGE_DIR" -name '*.uaem' -delete 2>/dev/null

echo "-- runtime: SWOS.exe$DLL_OK"
echo "-- data: $STAGED_DIRS"
echo "-- setup: $(basename "$SETUP_EXE")"
echo "-- staged $(du -sh "$STAGE_DIR" 2>/dev/null | cut -f1) in $STAGE_DIR"

[ "$STAGE_ONLY" = 1 ] && { echo "-- --stage-only: not compiling"; exit 0; }

# ---- find ISCC --------------------------------------------------------------
# ISCC.exe is a Windows program; from WSL it is reached through interop, which
# means every path handed to it must be a WINDOWS path (wslpath -w). Inside
# Cygwin/MSYS it is just an exe on a normal path.
if [ -z "$ISCC" ]; then
    for pf in "/mnt/c/Program Files" "/mnt/c/Program Files (x86)" \
              "/c/Program Files" "/c/Program Files (x86)"; do
        for v in 6 7; do   # newest last so 7 wins
            [ -x "$pf/Inno Setup $v/ISCC.exe" ] && ISCC="$pf/Inno Setup $v/ISCC.exe"
        done
    done
fi

# Exit 3, not 1: "Inno Setup isn't installed on this machine" is a SKIP, not a
# failure. scripts/build_dist.sh tells them apart by this code, so a drop built
# somewhere without Inno Setup still reports success for everything else.
if [ -z "$ISCC" ] || [ ! -x "$ISCC" ]; then
    echo ""
    echo "-- staged $STAGE_DIR, but Inno Setup was not found, so no installer"
    echo "   was compiled. Get it from https://jrsoftware.org/isdl.php, or point"
    echo "   ISCC at it:"
    echo ""
    echo "     ISCC='/mnt/c/Program Files/Inno Setup 7/ISCC.exe' $0"
    echo ""
    exit 3
fi

# ---- compile ----------------------------------------------------------------
echo ""
echo "== compiling $OUTPUT"
echo "-- ISCC: $ISCC"

ISS_ARG="$ISS"
if command -v wslpath >/dev/null 2>&1; then
    ISS_ARG="$(wslpath -w "$ROOT/$ISS" 2>/dev/null || echo "$ISS")"
fi

ISCC_ARGS=""
[ -n "$VERSION" ] && ISCC_ARGS="/DAppVersion=$VERSION"

rm -f "$OUTPUT"
# /Q is quiet-but-not-silent: errors still print. Anything ISCC writes to stdout
# on a good run is noise we don't want in the build_dist summary.
if [ -n "$ISCC_ARGS" ]; then
    "$ISCC" /Q "$ISCC_ARGS" "$ISS_ARG"
else
    "$ISCC" /Q "$ISS_ARG"
fi
RC=$?

# ISCC writes dist\SWOS-Setup.exe (OutputDir + OutputBaseFilename in the .iss).
# --output only has to move it if the caller asked for somewhere else.
BUILT="dist/SWOS-Setup.exe"
if [ "$RC" != 0 ]; then
    echo "-- FAIL: ISCC exited $RC" >&2
    exit "$RC"
fi
if [ ! -f "$BUILT" ]; then
    echo "-- FAIL: ISCC reported success but $BUILT is not there" >&2
    exit 1
fi
if [ "$OUTPUT" != "$BUILT" ]; then
    mkdir -p "$(dirname "$OUTPUT")"
    mv "$BUILT" "$OUTPUT" || die "cannot move $BUILT to $OUTPUT"
fi

echo "-- OK: $OUTPUT"
ls -l --time-style='+%Y-%m-%d %H:%M' "$OUTPUT" 2>/dev/null |
    awk '{ printf "   %s bytes  %s %s\n", $5, $6, $7 }'
exit 0
