#!/usr/bin/env bash
#
# build_dist.sh — build every OpenSWOS target this machine can produce and
#                 package the results into dist/.
#
#   scripts/build_dist.sh                 # build everything + write dist/SWOS.zip
#   scripts/build_dist.sh --only windows  # just the .exe
#   scripts/build_dist.sh --skip wasm     # everything but the browser build
#   scripts/build_dist.sh --no-zip        # build only, don't package
#   scripts/build_dist.sh --no-full       # skip the big SWOS-portable.zip
#   scripts/build_dist.sh --no-installer  # skip dist/SWOS-Setup.exe
#
# EVERYTHING A TESTER IS GIVEN LANDS IN dist/. bin/ stays what it has always
# been — the shared runtime tree the game reads and every target writes its
# binary into — so the two never have to be told apart by name. dist/ is
# gitignored wholesale, which is also why the artifacts stopped showing up as
# untracked files in `git status`. build_switch.sh already wrote dist/SWOS.nsp;
# now the zips, the installer and the macOS archives join it.
#
# Three archives and a Windows installer are written:
#   dist/SWOS.zip       binaries only (~10 MB) — for someone who already has bin/
#   dist/SWOS-portable.zip  binaries + game data + the Cygwin runtime for SWOS.exe:
#                       unzip and play. Excludes save/ (per-user dir, and 24 MB
#                       of test careers here).
#   dist/SWOS-Amiga.zip the m68k binary + ONLY the trees it actually opens
#                       (data grafs grafs_aga sound classic), as a SWOS drawer with its
#                       icon: drop it onto a hard drive and double-click SWOS.
#                       c/ s/ inside the drawer boot an emulator from it.
#   dist/SWOS-Setup.exe the Inno Setup installer: the same content as
#                       SWOS-portable.zip minus the non-Windows binaries, for a
#                       tester who would rather double-click than unzip.
#                       scripts/build_installer.sh does the work; see it and
#                       packaging/windows/swos.iss.
#
# scripts/build_macos.sh adds dist/SWOS-macOS-app.zip and dist/SWOS-macOS.zip
# from a real Mac — see it for why that one target cannot be built here.
#
# Targets:
#
#   linux-x86      make                 -> bin/SWOS          (32-bit, -m32)
#   linux-x86_64   make X86_64=1        -> bin/SWOS.x86_64
#   linux-arm64    make ARM64=1         -> bin/SWOS.aarch64
#   linux-armhf    make ARM=1           -> bin/SWOS.armhf
#   wasm           make WASM=1          -> dist/web/SWOS.html/.js/.wasm/.data
#   amiga          make -f Makefile.amiga -> bin/SWOS2
#   windows        make (in Cygwin)     -> bin/SWOS.exe
#   switch         scripts/build_switch.sh package
#                                       -> bin/SWOS.nss + dist/SWOS.nsp
#   setup          make -C tools/swosdata  -> tools/swosdata/build/swosdata (the
#                                          data setup's command line, Linux)
#                  tools/swosdata/build.bat static (via cmd.exe)
#                                       -> openswos-setup.exe (the data setup's
#                                          window, one static .exe; needs Visual
#                                          Studio and Rust on the Windows side)
#   installer      scripts/build_installer.sh
#                                       -> dist/SWOS-Setup.exe  (packaging step,
#                                          not a compile: it needs bin/SWOS.exe
#                                          from the `windows` target and Inno
#                                          Setup on the Windows side)
#
# The Switch needs the NintendoSDK and the licensee SDL fork, neither of which
# is in this repository (both NDA). It SKIPs with a reason when they are not
# there, so a drop on another machine still builds everything else. Its output
# is an .nsp package rather than a zip member — an .nsp already IS the
# installable artifact.
#
# macOS is deliberately NOT here: `make macos-app` hard-fails unless
# `uname -s` is Darwin, and there is no cross-compile path. Run this on a Mac:
#
#     make macos-universal      # bin/SWOS.macos (x86_64 + arm64)
#     make macos-app            # bin/SWOS.app
#
# TESTER BUILD BY DEFAULT
#
# SWOS_TEST_RULES=1 is ON unless you pass --release. It compiles in the testers'
# setting `[testing] no_injuries_cards` (SWOS.ini, OFF by default, offline only --
# include/test_rules.h): with it a tester can hit VIEW RESULT through a whole season
# without substitution menus or booking prompts. Injuries and cards themselves are
# no longer a build matter: every build plays the real rules online. Pass --release
# for a public build, where the setting is compiled out. Keep it consistent across
# ALL platforms in a given drop.
#
# THREE THINGS THIS SCRIPT EXISTS TO GET RIGHT
#
# 1. SWOS_SAVE_FORMAT_JSON_VERIFY defaults to ON for every target except WASM.
#    The `?= 0` line in the Makefile sits INSIDE the `ifdef WASM` branch, so a
#    plain `make` builds the save-format round-trip verifier in. That pass makes
#    every SAVE zero the saved fields, reload the file, and NOT restore the
#    snapshot — a bring-up diagnostic, not release behaviour. We pass
#    SWOS_SAVE_FORMAT_JSON_VERIFY=0 unless you ask for --verify.
#
# 2. Makefile.amiga has no SWOS_* knobs, so the m68k target would silently miss
#    SWOS_TEST_RULES. We pass it through its EXTRA_CFLAGS escape
#    hatch, and its objects live in obj/ (not build/) so `rm -rf build` alone
#    leaves it stale and reports "Nothing to be done for 'all'".
#
# 3. Toggling any -D flag needs a clean first: make compares timestamps, not
#    CFLAGS, so a flag change silently relinks stale objects. We wipe the object
#    dirs by default (--no-clean to skip when you know the flags are unchanged).
#
# The Windows binary is a CYGWIN binary (it imports cygwin1.dll) — testers need
# cygwin1.dll + SDL2.dll beside it. The Makefile picks the Windows target from
# the HOST SHELL ($(OS)/uname -s), never from a flag, so it has to be built
# inside Cygwin; from WSL we reach that shell through Windows interop.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

# ---- progress state ---------------------------------------------------------
# One atomically-rewritten line saying
# what stage this is on, a terminal state on EVERY exit path (including a
# signal), and a final "== DONE rc=N" line to grep for. A drop takes 40+ minutes
# and a cross-compile can sit silent for several; without this the only way to
# tell "still linking" from "died" is to keep tailing the log, which is how you
# end up with a pile of polling timers firing after the build has finished.
#
#   until grep -qv '^running' dist/.build-status 2>/dev/null; do sleep 30; done
BUILD_STATUS_FILE="${BUILD_STATUS_FILE:-dist/.build-status}"
BUILD_START_EPOCH="$(date +%s)"
# The commit the drop is built from, in the terminal state: scripts/upload_itch.sh
# refuses to publish a drop that is not one commit (or was built from a -dirty tree).
BUILD_COMMIT="$(git rev-parse --short=8 HEAD)"
[ -z "$(git status --porcelain --untracked-files=no)" ] || BUILD_COMMIT="$BUILD_COMMIT-dirty"
build_elapsed() { echo $(( $(date +%s) - BUILD_START_EPOCH )); }

build_status() {
    [ -n "$BUILD_STATUS_FILE" ] || return 0
    mkdir -p "$(dirname "$BUILD_STATUS_FILE")" 2>/dev/null || return 0
    printf '%s\n' "$*" > "$BUILD_STATUS_FILE.tmp" 2>/dev/null &&
        mv -f "$BUILD_STATUS_FILE.tmp" "$BUILD_STATUS_FILE" 2>/dev/null
    return 0
}

build_finish() {
    local rc=$?
    local secs; secs="$(build_elapsed)"
    if [ "$rc" = 0 ]; then
        build_status "done rc=0 commit=$BUILD_COMMIT elapsed=$secs"
    else
        build_status "failed rc=$rc elapsed=$secs"
    fi
    echo ""
    echo "== DONE rc=$rc  (${secs}s)"
    return $rc
}
trap build_finish EXIT
build_status "running phase=start elapsed=0"

# ---- machine-specific paths (override via the environment) ------------------
EMSDK_DIR="${EMSDK_DIR:-/mnt/c/Projects/Linux/emsdk}"
AMIGA_TOOLCHAIN="${AMIGA_TOOLCHAIN:-/mnt/c/Users/headk/.vscode/extensions/bartmanabyss.amiga-debug-1.8.2/bin/linux}"
CYGWIN_BASH="${CYGWIN_BASH:-/mnt/c/cygwin64/bin/bash.exe}"
# Switch: both trees are NDA and live on the Windows side. Defaults match
# scripts/build_switch.sh, which is what actually consumes them; they are
# repeated here only so the target can be SKIPPED with a reason instead of
# failing on a machine that has no SDK. See docs/SWITCH.md.
NINTENDO_SDK_ROOT="${NINTENDO_SDK_ROOT:-D:\\Nintendo\\NativeSDK22.2.10\\NintendoSDK}"
SDL_SWITCH_ROOT="${SDL_SWITCH_ROOT:-D:\\Projects\\GitHub\\SDL-switch}"

# `installer` is a packaging step rather than a compile, but it lives in this
# list so --only/--skip and the summary treat it like everything else.
ALL_TARGETS="linux-x86 linux-x86_64 linux-arm64 linux-armhf wasm amiga windows setup switch installer"

# Binaries that go into the zip. The WASM outputs are deliberately excluded:
# SWOS.html/.js/.wasm/.data are a web deployment, not something a tester runs
# from a zip, and .data alone is ~21 MB. They are not even in bin/ — `make
# WASM=1` links them straight into dist/web/ (Makefile, OUTDIR).
ZIP_MEMBERS="SWOS SWOS.x86_64 SWOS.aarch64 SWOS.armhf SWOS.exe SWOS2"

# SWOS-portable.zip — a RUNNABLE drop: the same binaries plus the project's own
# data and the data setup. The ORIGINAL GAME'S data is not in it: the player's
# own copy is turned into it by the setup (tools/swosdata, openswos-setup.exe /
# swosdata) into their assets folder. Everything sits at the zip root because the
# SDL binary chdir's to its own directory. Staged by scripts/stage_runtime.sh,
# which takes only git-tracked files, so data a developer keeps in bin/ never
# ships.
#
# The data list mirrors what `make macos-app` bundles (Makefile, macos-app rule)
# — that is this project's tested definition of "what the game needs at runtime".
# save/ is deliberately EXCLUDED: it is 24 MB of test careers here, and saves go
# to a per-user directory anyway (swos_io.c), so shipping it would both bloat the
# zip and drop stale saves on testers.
FULL_DATA_DIRS="data grafs grafs_aga grafs_dos grafs_2020 grafs_arcade grafs_jag sound sound_dos data_json setup classic"
# The data setup, built by the `setup` target.
SETUP_CLI="tools/swosdata/build/swosdata"
SETUP_EXE="tools/swosdata/build_win_static/bin/Release/openswos-setup.exe"
# Cygwin runtime for SWOS.exe. FOUR files, and only two of them are visible to
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
FULL_DLLS="cygwin1.dll cygSDL2-2-0-0.dll cygSDL3-0.dll cygiconv-2.dll"

# Linux runtime for the four ELFs, the counterpart of FULL_DLLS above. Each
# binary links libSDL2-2.0.so.0 for its own architecture, so a tester who has
# never installed libsdl2 gets
#
#     error while loading shared libraries: libSDL2-2.0.so.0
#
# The binaries carry -Wl,-rpath,$ORIGIN/lib/<arch> (Makefile), so dropping the
# libs at lib/<arch>/ in the zip is all that is needed. A dev tree has no lib/
# dir and falls through to the system SDL2 exactly as before.
#
# WHY libdecor IS HERE TOO, and why bundling libSDL2 alone is not enough:
# Ubuntu builds SDL2 with its backends LINKED, not dlopen'd — 20 DT_NEEDED
# entries (X11, Wayland, Mesa, ALSA, PulseAudio, libdecor). Eighteen of those
# are core desktop libraries present on any machine with a display. libdecor is
# the exception: `apt-cache rdepends libdecor-0-0` shows libsdl2-2.0-0 is
# essentially its ONLY consumer, so a machine without SDL2 almost certainly has
# no libdecor either — and shipping libSDL2 by itself would just trade
# "libSDL2-2.0.so.0 not found" for the more baffling "libdecor-0.so.0 not
# found". It is 17-34 KB and needs only libwayland-client + libc.
#
# The remaining 18 are NOT bundled on purpose: they are the X11/Mesa/audio stack,
# they drag in ~50 transitive libraries including libsystemd and libgcrypt, and
# a machine with a display already has them. Our binaries also need glibc 2.34
# (Ubuntu 21.10+ / Debian 12 / Fedora 35+), so a tester old enough to be missing
# them cannot run SWOS at all regardless.
#
# If a tester ever DOES hit one of the other 18, the real fix is not to bundle
# more of the distro: it is to build SDL2 ourselves with --enable-x11-shared /
# --enable-wayland-shared / --enable-alsa-shared / --enable-pulseaudio-shared,
# the way upstream builds its own release binaries, which leaves libSDL2 with
# almost no DT_NEEDED and probes for backends at run time. All four cross dev
# stacks are installed here, so that is a script away if it becomes necessary.
LINUX_SDL_LIBS="libSDL2-2.0.so.0 libdecor-0.so.0"
# <binary>:<rpath dir, = BUILD_ARCH in the Makefile>:<Debian multiarch triplet>
LINUX_SDL_ARCHES="SWOS:x86:i386-linux-gnu
SWOS.x86_64:x86_64:x86_64-linux-gnu
SWOS.aarch64:arm64:aarch64-linux-gnu
SWOS.armhf:armhf:arm-linux-gnueabihf"
LINUX_LIBDIR="${LINUX_LIBDIR:-/usr/lib}"

# SWOS-Amiga.zip — the runnable Amiga drop: the m68k binary plus only the trees
# the m68k binary actually opens. Verified against the built binary itself
# (`strings bin/SWOS2 | grep -oE '^(data|grafs|...)/'`): it references data/,
# grafs/, grafs_aga/, sound/ and save/ — and NOTHING from grafs_dos, grafs_2020,
# grafs_arcade or data_json, which are SDL-only asset tiers. Shipping those would
# add ~50 MB the Amiga can never load. An EMPTY save/ is created because the
# binary writes there and will not create the directory itself.
# Copy the contents onto a volume FS-UAE/WinUAE mounts (the harness mounts bin/
# as DH1: and cd's in, so the same relative paths resolve).
#
# The zip is laid out to be dropped as it is onto a hard drive (2026-10-13): a SWOS
# drawer and its icon at the top, everything inside the drawer --
#
#   SWOS.info                  the drawer's icon
#   SWOS/SWOS2                 the binary
#   SWOS/SWOS + SWOS.info      what Workbench starts: double-click the icon
#   SWOS/data grafs grafs_aga sound classic save
#   SWOS/c SWOS/s              to boot an emulator straight from the drawer
#
# packaging/amiga mirrors that layout and holds what the strings rule above cannot
# find, because AmigaDOS opens it, not the binary:
#   - SWOS/SWOS is a one-line AmigaDOS SCRIPT, "SWOS2". Its icon is a Project whose
#     default tool is C:IconX, and IconX EXECUTEs the file the icon belongs to -- so
#     that file must be a script, never the binary (renaming SWOS2 to SWOS gives
#     "EXECUTE: Invalid Directive"). IconX starts in the icon's drawer: no CD.
#   - the two icons, and FS-UAE/WinUAE .uaem sidecars carrying the Amiga protection
#     bits (the script flag) a zip cannot.
# c/ and s/ go INSIDE the drawer, never at the top: dropped onto a hard drive, a top
# level s/ would replace that drive's own S:startup-sequence. Inside the drawer they
# do nothing until the drawer itself is mounted as the boot volume, when
# s/startup-sequence runs SYS:SWOS2 and then c/UAEquit.
AMIGA_DATA_DIRS="data grafs grafs_aga sound classic"
AMIGA_BOOT_DIRS="c s"
AMIGA_PKG_DIR="packaging/amiga"
CYGWIN_BIN="${CYGWIN_BIN:-/mnt/c/cygwin64/bin}"

JOBS="$(nproc 2>/dev/null || echo 4)"
DO_CLEAN=1
DO_ZIP=1
VERIFY=0
NOINJ=1          # tester builds: suppress injuries/cards. --release turns it off.
ZIP_PATH="dist/SWOS.zip"
FULL_ZIP_PATH="dist/SWOS-portable.zip"
AMIGA_ZIP_PATH="dist/SWOS-Amiga.zip"
INSTALLER_PATH="dist/SWOS-Setup.exe"
DO_FULL=1
DO_INSTALLER=1
ONLY=""
SKIP=""

usage() {
    # Print the header comment block (from line 3) up to the first code line.
    awk 'NR >= 3 { if ($0 !~ /^#/) exit; sub(/^# ?/, ""); print }' "${BASH_SOURCE[0]}"
    cat <<EOF

Options:
  -j, --jobs N        parallel make jobs (default: $(nproc 2>/dev/null || echo 4))
      --only  LIST    comma-separated targets to build (default: all)
      --skip  LIST    comma-separated targets to skip
      --verify        build WITH SWOS_SAVE_FORMAT_JSON_VERIFY=1 (debug only)
      --release       PUBLIC build: the testers' no-injuries setting compiled out
                      (default is a tester build, SWOS_TEST_RULES=1)
      --no-clean      don't wipe object dirs first (only safe if flags unchanged)
      --no-zip        build but don't package (no zips, no installer)
      --no-full       skip SWOS-portable.zip (binaries-only SWOS.zip still written)
      --no-installer  skip dist/SWOS-Setup.exe
      --full-zip PATH full-dist output path (default: $FULL_ZIP_PATH)
      --amiga-zip PATH Amiga-dist output path (default: $AMIGA_ZIP_PATH)
      --installer PATH installer output path (default: $INSTALLER_PATH)
      --zip PATH      zip output path (default: $ZIP_PATH)
  -h, --help          this text

Targets: $ALL_TARGETS
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        -j|--jobs)   JOBS="$2"; shift 2 ;;
        --only)      ONLY="$2"; shift 2 ;;
        --skip)      SKIP="$2"; shift 2 ;;
        --verify)    VERIFY=1; shift ;;
        --release)   NOINJ=0; shift ;;
        --no-clean)  DO_CLEAN=0; shift ;;
        --no-zip)    DO_ZIP=0; DO_FULL=0; DO_INSTALLER=0; shift ;;
        --no-full)   DO_FULL=0; shift ;;
        --no-installer) DO_INSTALLER=0; shift ;;
        --installer) INSTALLER_PATH="$2"; shift 2 ;;
        --full-zip)  FULL_ZIP_PATH="$2"; shift 2 ;;
        --amiga-zip) AMIGA_ZIP_PATH="$2"; shift 2 ;;
        --zip)       ZIP_PATH="$2"; shift 2 ;;
        -h|--help)   trap - EXIT; usage; exit 0 ;;   # help is not a run: no DONE line
        *) echo "build_dist: unknown option '$1' (try --help)" >&2; exit 2 ;;
    esac
done

# A target name that matches nothing is a TYPO, and silently ignoring it is the
# expensive failure here: `--only windwos` cleans, builds NOTHING, repackages the
# zips from whatever stale binaries are already in bin/, and exits 0. The publish
# step then ships those, so a drop goes out claiming a commit it was not built
# from. Measured, not hypothetical: `--only nosuchtarget` did exactly that in
# 23 seconds and reported success.
for want_t in ${ONLY//,/ } ${SKIP//,/ }; do
    case " $ALL_TARGETS " in
        *" $want_t "*) ;;
        *) echo "build_dist: no such target '$want_t'" >&2
           echo "            targets: $ALL_TARGETS" >&2
           exit 2 ;;
    esac
done

MAKE_FLAGS="SWOS_SAVE_FORMAT_JSON_VERIFY=$VERIFY"
EXTRA_DEFS=""
# A TESTER drop is built with SWOS_TEST_RULES: the `[testing] no_injuries_cards` setting
# exists (off by default, offline only -- include/test_rules.h). --release compiles the
# rule out. Injuries and cards themselves are no longer a build matter.
if [ "$NOINJ" = 1 ]; then
    MAKE_FLAGS="$MAKE_FLAGS SWOS_TEST_RULES=1"
    # Neither Makefile.amiga nor Makefile.switch has SWOS_* knobs of its own;
    # EXTRA_CFLAGS is the documented escape hatch on both, so the m68k and NX
    # binaries are tester builds like the SDL ones (.claude/CLAUDE.md).
    EXTRA_DEFS="-DSWOS_TEST_RULES=1"
else
    MAKE_FLAGS="$MAKE_FLAGS SWOS_TEST_RULES=0"
fi

wanted() {   # wanted <target> -> 0 if it should be built
    local t="$1"
    [ -n "$ONLY" ] && { case ",$ONLY," in *",$t,"*) : ;; *) return 1 ;; esac; }
    [ -n "$SKIP" ] && { case ",$SKIP," in *",$t,"*) return 1 ;; esac; }
    return 0
}

declare -A STATUS   # target -> OK / FAIL / SKIP(reason) / not-requested
ORDER=""

record() { STATUS["$1"]="$2"; ORDER="$ORDER $1"; }

banner() {
    build_status "running phase=$* elapsed=$(build_elapsed)"
    echo ""
    echo "======================================================================"
    echo "== $*"
    echo "======================================================================"
}

# outdir <path> — make sure an artifact's parent directory exists. Per-artifact
# rather than one `mkdir -p dist` because --zip / --full-zip / --amiga-zip /
# --installer can each point somewhere else entirely.
outdir() { mkdir -p "$(dirname "$1")" 2>/dev/null; }

# run <target> <label> <cmd...>
run() {
    local t="$1" label="$2"; shift 2
    banner "$label"
    if "$@"; then record "$t" "OK"; echo "-- OK: $label"
    else       record "$t" "FAIL"; echo "-- FAIL: $label"; fi
}

# ---- submodules -------------------------------------------------------------
# third_party/ggpo-x is a SUBMODULE (github.com/benbaker76/ggpo-x, branch
# openswos). A fresh clone has the directory and nothing in it, and the
# netplay build then fails on a missing header several hundred lines into make
# output -- so check it here and say what is wrong in one line.
if [ -f .gitmodules ] && ! [ -f third_party/ggpo-x/src/include/ggponet.h ]; then
    banner "submodules"
    if git submodule update --init --recursive >/dev/null 2>&1; then
        echo "-- fetched third_party/ggpo-x"
    else
        echo "!! third_party/ggpo-x is empty and could not be fetched." >&2
        echo "!! Run: git submodule update --init --recursive" >&2
        exit 1
    fi
fi

# ---- clean ------------------------------------------------------------------
if [ "$DO_CLEAN" = 1 ]; then
    banner "clean (object dirs — make compares timestamps, not CFLAGS)"
    # SDL targets keep objects under build/<os>/<arch>; the Amiga makefile uses
    # its own obj/ tree and the Switch its own obj-switch/, so `rm -rf build`
    # alone would leave both stale. This matters MORE for those two than for the
    # SDL targets, because neither has SWOS_* knobs: the tester flag reaches them
    # through EXTRA_CFLAGS, and make compares timestamps, not CFLAGS. Skip the
    # Switch clean and only the files that happened to change get the flag —
    # measured, not theorised: career_status.o survived from a pre-flag build
    # with IsPlayerInjured still 88 bytes while the rest of the tree had it
    # stubbed out.
    # Everything under build/ EXCEPT what was downloaded into it: the testers'
    # recordings and match reports (scripts/fetch_recordings.py, fetch_reports.py)
    # are not build output, and the server may have been cleared of them since --
    # a wholesale `rm -rf build` deleted both on 2026-09-29.
    if [ -d build ]; then
        find build -mindepth 1 -maxdepth 1 ! -name recordings ! -name reports -exec rm -rf {} +
    fi
    make -f Makefile.amiga clean >/dev/null 2>&1
    # Only OUR objects, which sit at the top of obj-switch/. The SDL fork's live
    # in obj-switch/sdl/ with its own prebuilt libSDL2_nx.a, and EXTRA_CFLAGS is
    # deliberately kept out of SDL_CFLAGS (the fork is third-party; none of the
    # game's toggles mean anything in it), so those cannot go stale on a flag
    # change and rebuilding them would cost minutes for nothing.
    rm -f obj-switch/*.o obj-switch/*.opp obj-switch/*.d
    rm -rf build-switch
    echo "-- cleaned build/ (the downloaded recordings and reports kept), the Amiga obj/ tree and obj-switch/*.o (SDL fork kept)"
fi

# ---- SDL targets ------------------------------------------------------------
wanted linux-x86    && run linux-x86    "linux x86 (32-bit) -> bin/SWOS" \
                           make $MAKE_FLAGS -j"$JOBS"
wanted linux-x86_64 && run linux-x86_64 "linux x86_64       -> bin/SWOS.x86_64" \
                           make X86_64=1 $MAKE_FLAGS -j"$JOBS"

if wanted linux-arm64; then
    if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1 || \
       [ "$(uname -m)" = "aarch64" ] || [ "$(uname -m)" = "arm64" ]; then
        run linux-arm64 "linux arm64        -> bin/SWOS.aarch64" \
            make ARM64=1 $MAKE_FLAGS -j"$JOBS"
    else
        record linux-arm64 "SKIP (no aarch64-linux-gnu-gcc)"
    fi
fi

if wanted linux-armhf; then
    if command -v arm-linux-gnueabihf-gcc >/dev/null 2>&1 || \
       [ "$(uname -m)" = "armv7l" ]; then
        run linux-armhf "linux armhf        -> bin/SWOS.armhf" \
            make ARM=1 $MAKE_FLAGS -j"$JOBS"
    else
        record linux-armhf "SKIP (no arm-linux-gnueabihf-gcc)"
    fi
fi

# ---- wasm -------------------------------------------------------------------
if wanted wasm; then
    if command -v emcc >/dev/null 2>&1 || [ -f "$EMSDK_DIR/emsdk_env.sh" ]; then
        banner "wasm -> dist/web/SWOS.html/.js/.wasm/.data"
        # emsdk_env.sh is not -u clean, hence the subshell + set +u.
        if ( set +u
             [ -f "$EMSDK_DIR/emsdk_env.sh" ] && . "$EMSDK_DIR/emsdk_env.sh" >/dev/null 2>&1
             cd "$ROOT" && make WASM=1 $MAKE_FLAGS -j"$JOBS" ); then
            record wasm "OK"; echo "-- OK: wasm"
        else
            record wasm "FAIL"; echo "-- FAIL: wasm"
        fi
    else
        record wasm "SKIP (no emcc; set EMSDK_DIR)"
    fi
fi

# ---- amiga ------------------------------------------------------------------
if wanted amiga; then
    if [ -x "$AMIGA_TOOLCHAIN/opt/bin/m68k-amiga-elf-gcc" ] || \
       command -v m68k-amiga-elf-gcc >/dev/null 2>&1; then
        banner "amiga m68k -> bin/SWOS2"
        # The toolchain ships inside the amiga-debug VSCode extension and MUST be
        # the benbaker76 fork (see .claude/CLAUDE.md). A wrong path fails as
        # 'Error 127' and an up-to-date tree hides it as "Nothing to be done".
        if PATH="$AMIGA_TOOLCHAIN/opt/bin:$AMIGA_TOOLCHAIN:$PATH" \
               make -f Makefile.amiga EXTRA_CFLAGS="$EXTRA_DEFS" -j"$JOBS"; then
            record amiga "OK"; echo "-- OK: amiga"
        else
            record amiga "FAIL"; echo "-- FAIL: amiga"
        fi
    else
        record amiga "SKIP (no m68k-amiga-elf-gcc; set AMIGA_TOOLCHAIN)"
    fi
fi

# ---- windows (Cygwin) -------------------------------------------------------
if wanted windows; then
    case "$(uname -s)" in
        CYGWIN*|MINGW*|MSYS*)
            # Already inside a Windows shell — plain make picks SWOS.exe.
            run windows "windows -> bin/SWOS.exe (native)" \
                make $MAKE_FLAGS -j"$JOBS"
            ;;
        *)
            if [ -x "$CYGWIN_BASH" ]; then
                banner "windows (Cygwin via interop) -> bin/SWOS.exe"
                # Translate this WSL path into a Cygwin one. -lc is a LOGIN
                # shell: the Cygwin profile is sourced, so a stray
                # SWOS_* export there would override our flags.
                WINPATH="$(wslpath -w "$ROOT" 2>/dev/null || echo "")"
                if [ -z "$WINPATH" ]; then
                    record windows "SKIP (can't map $ROOT to a Windows path)"
                elif "$CYGWIN_BASH" -lc \
                        "cd \"\$(cygpath -u '$WINPATH')\" && make $MAKE_FLAGS -j$JOBS"; then
                    record windows "OK"; echo "-- OK: windows"
                else
                    record windows "FAIL"; echo "-- FAIL: windows"
                fi
            else
                record windows "SKIP (no Cygwin at $CYGWIN_BASH)"
            fi
            ;;
    esac
fi

# ---- setup (the data setup: tools/swosdata) ----------------------------------
# OpenSWOS ships none of the original game's data; this is what makes it from
# the player's own copy. The command line builds here; the window is a static
# Slint .exe built by tools/swosdata/build.bat on the Windows side (Visual Studio
# + Rust; docs in tools/swosdata). A missing Windows toolchain is a SKIP.
if wanted setup; then
    banner "setup -> $SETUP_CLI + $SETUP_EXE"
    SETUP_RC=0
    make -C tools/swosdata -j"$JOBS" >/dev/null || SETUP_RC=1
    WINSD="$(wslpath -w "$ROOT/tools/swosdata" 2>/dev/null || echo "")"
    if [ "$SETUP_RC" = 0 ] && [ -n "$WINSD" ] && command -v cmd.exe >/dev/null 2>&1; then
        ( cd /mnt/c && cmd.exe /c "cd /d $WINSD && build.bat static" ) >/dev/null 2>&1 || SETUP_RC=2
    else
        [ "$SETUP_RC" = 0 ] && SETUP_RC=3
    fi
    case $SETUP_RC in
        0) record setup "OK"; echo "-- OK: setup" ;;
        1) record setup "FAIL"; echo "-- FAIL: setup (the command line)" ;;
        2) record setup "FAIL"; echo "-- FAIL: setup (openswos-setup.exe; run tools/swosdata/build.bat static)" ;;
        3) record setup "SKIP (no Windows side for openswos-setup.exe)" ;;
    esac
fi

# ---- switch (NX, via Windows interop) ---------------------------------------
# Produces dist/SWOS.nsp, not a zip: an .nsp IS the installable package, and at
# ~84 MB it is dominated by the SDK's own opengl/vulkan modules, which compress
# to nothing. It is listed in the summary alongside the archives.
#
# Both the SDK and the SDL fork are NDA and live on the Windows side, so this
# SKIPs with a reason on any machine that lacks them rather than failing the
# whole drop. scripts/build_switch.sh owns the interop; all this does is decide
# whether it can run and hand it the shared -D flags.
if wanted switch; then
    NX_MARK=""
    if command -v wslpath >/dev/null 2>&1; then
        NX_MARK="$(wslpath -u "$NINTENDO_SDK_ROOT" 2>/dev/null || echo "")"
    fi
    NX_SDL=""
    if command -v wslpath >/dev/null 2>&1; then
        NX_SDL="$(wslpath -u "$SDL_SWITCH_ROOT" 2>/dev/null || echo "")"
    fi
    if [ -z "$NX_MARK" ] || [ ! -f "$NX_MARK/NintendoSdkRootMark" ]; then
        record switch "SKIP (no NintendoSDK at $NINTENDO_SDK_ROOT; set NINTENDO_SDK_ROOT)"
    elif [ -z "$NX_SDL" ] || [ ! -d "$NX_SDL/src/video/nintendoswitch" ]; then
        # Checked separately from the SDK: the fork is a different NDA tree and
        # a different thing to go and get. Without this the build would reach
        # the compiler and die on a missing SDL_config, failing the whole drop
        # instead of stepping over one unavailable target.
        record switch "SKIP (no SDL fork at $SDL_SWITCH_ROOT; set SDL_SWITCH_ROOT)"
    elif [ ! -x "${COMSPEC:-/mnt/c/Windows/System32/cmd.exe}" ] &&
         [ ! -x /mnt/c/Windows/System32/cmd.exe ]; then
        record switch "SKIP (no Windows interop — NintendoClang is a Windows .exe)"
    else
        banner "switch NX-NXFP2-a64 -> bin/SWOS.nss + dist/SWOS.nsp"
        # EXTRA_CFLAGS rides through WSLENV -> cmd -> Makefile.switch. Without
        # it the console build would be the only one in the drop that is not
        # a tester build (SWOS_TEST_RULES).
        if EXTRA_CFLAGS="$EXTRA_DEFS" scripts/build_switch.sh package; then
            record switch "OK"; echo "-- OK: switch"
        else
            record switch "FAIL"; echo "-- FAIL: switch"
        fi
    fi
fi

# ---- package ----------------------------------------------------------------
ZIPPED=""
MISSING=""
if [ "$DO_ZIP" = 1 ]; then
    banner "package -> $ZIP_PATH"
    if ! command -v zip >/dev/null 2>&1; then
        echo "-- SKIP: 'zip' not installed (apt install zip)"
    else
        for m in $ZIP_MEMBERS; do
            if [ -f "bin/$m" ]; then ZIPPED="$ZIPPED $m"; else MISSING="$MISSING $m"; fi
        done
        if [ -z "$ZIPPED" ]; then
            echo "-- SKIP: nothing to package"
        else
            outdir "$ZIP_PATH"
            rm -f "$ZIP_PATH"
            # -j junks paths so members sit at the zip root, not under bin/.
            ( cd bin && zip -j -q "$ROOT/$ZIP_PATH" $ZIPPED ) && \
                echo "-- OK: $ZIP_PATH <-$ZIPPED"
            [ -n "$MISSING" ] && echo "-- NOTE: not in the zip (not built):$MISSING"
        fi
    fi
fi

# ---- package: full dist (binaries + data + Windows runtime) ------------------
if [ "$DO_FULL" = 1 ] && [ -n "$ZIPPED" ] && command -v zip >/dev/null 2>&1; then
    banner "package -> $FULL_ZIP_PATH  (portable: every PC binary + the data setup, no game data)"
    FULL_TMP="$(mktemp -d)"
    FULL_DIRS="$(SWOS_RUNTIME_TARGET=all scripts/stage_runtime.sh "$FULL_TMP" $FULL_DATA_DIRS)"
    if [ -z "$FULL_DIRS" ]; then
        echo "-- SKIP: no data directories under bin/"
    else
        outdir "$FULL_ZIP_PATH"
        rm -f "$FULL_ZIP_PATH"
        # Binaries junked to the root, then the data trees with their paths kept.
        ( cd bin && zip -j -q "$ROOT/$FULL_ZIP_PATH" $ZIPPED ) &&
        ( cd "$FULL_TMP" && zip -r -q "$ROOT/$FULL_ZIP_PATH" $FULL_DIRS -x '*/.DS_Store' ) &&
            echo "-- data:     $FULL_DIRS"
        # The data setup beside the game: the window on Windows, the command
        # line elsewhere. Without them the drop cannot be played at all.
        SETUP_IN=""
        for f in "$SETUP_EXE" "$SETUP_CLI"; do
            [ -f "$f" ] && SETUP_IN="$SETUP_IN $f"
        done
        if [ -n "$SETUP_IN" ]; then
            zip -j -q "$FULL_ZIP_PATH" $SETUP_IN && echo "-- setup:   $(echo $SETUP_IN | tr ' ' '\n' | sed 's|.*/||' | tr '\n' ' ')"
        else
            echo "-- NOTE: no data setup built (the setup target): the drop needs it to be played"
        fi

        # Windows runtime beside SWOS.exe. Skipped cleanly off-Cygwin hosts.
        DLL_ADDED=""; DLL_MISS=""
        for d in $FULL_DLLS; do
            if [ -f "$CYGWIN_BIN/$d" ]; then DLL_ADDED="$DLL_ADDED $CYGWIN_BIN/$d"
            else DLL_MISS="$DLL_MISS $d"; fi
        done
        if [ -n "$DLL_ADDED" ]; then
            zip -j -q "$FULL_ZIP_PATH" $DLL_ADDED && \
                echo "-- windows runtime:$(echo $DLL_ADDED | tr ' ' '\n' | sed 's|.*/||' | tr '\n' ' ')"
        fi
        [ -n "$DLL_MISS" ] && \
            echo "-- NOTE: Cygwin DLLs not found in $CYGWIN_BIN (SWOS.exe will not run standalone):$DLL_MISS"

        # Linux SDL runtime, one lib/<arch>/ per ELF that actually got built.
        # Staged in a temp dir so nothing lands in the source tree; -L because
        # the multiarch names are symlinks onto the versioned .so and the zip
        # must carry the SONAME with real content behind it.
        SDL_TMP="$(mktemp -d)"
        SDL_ADDED=""; SDL_MISS=""
        echo "$LINUX_SDL_ARCHES" | while read -r spec; do
            [ -n "$spec" ] || continue
            bin="${spec%%:*}"; rest="${spec#*:}"
            arch="${rest%%:*}"; triplet="${rest#*:}"
            [ -f "bin/$bin" ] || continue          # that arch wasn't built
            mkdir -p "$SDL_TMP/lib/$arch"
            for l in $LINUX_SDL_LIBS; do
                if [ -f "$LINUX_LIBDIR/$triplet/$l" ]; then
                    cp -L "$LINUX_LIBDIR/$triplet/$l" "$SDL_TMP/lib/$arch/$l"
                else
                    echo "!! $bin: $l missing for $triplet — it will need a system SDL2" >&2
                fi
            done
        done
        if [ -d "$SDL_TMP/lib" ]; then
            ( cd "$SDL_TMP" && zip -r -q "$ROOT/$FULL_ZIP_PATH" lib ) &&
                echo "-- linux runtime: $(cd "$SDL_TMP" && ls lib | tr '\n' ' ')(libSDL2 + libdecor)"
        fi
        rm -rf "$SDL_TMP"
        echo "-- OK: $FULL_ZIP_PATH"
    fi
    rm -rf "$FULL_TMP"
fi

# ---- package: Amiga dist (m68k binary + the trees it actually reads) ---------
if [ "$DO_FULL" = 1 ] && [ -f bin/SWOS2 ] && command -v zip >/dev/null 2>&1; then
    banner "package -> $AMIGA_ZIP_PATH  (Amiga: a SWOS drawer to drop onto a hard drive)"
    [ -f "$AMIGA_PKG_DIR/SWOS/SWOS" ] && [ -f "$AMIGA_PKG_DIR/SWOS/SWOS.info" ] ||
        echo "!! $AMIGA_PKG_DIR/SWOS/SWOS or its icon missing — the drop will not start from Workbench" >&2
    outdir "$AMIGA_ZIP_PATH"
    rm -f "$AMIGA_ZIP_PATH"
    # Staged, so the zip holds exactly the layout above. save/ is created EMPTY --
    # the binary writes there but won't mkdir it -- and never copied from bin/save,
    # whose 24 MB of test careers must not leak in.
    #
    # Only the project's own files in the trees go in (scripts/stage_runtime.sh
    # stages tracked files): the original game's data is added by the player with
    # the data setup ("Amiga version" -> Add the game data), from their own copy.
    AMIGA_TMP="$(mktemp -d)"
    if cp -R "$AMIGA_PKG_DIR/." "$AMIGA_TMP/" &&
       cp bin/SWOS2 "$AMIGA_TMP/SWOS/" &&
       AMIGA_DIRS="$(SWOS_RUNTIME_TARGET=amiga scripts/stage_runtime.sh "$AMIGA_TMP/SWOS" $AMIGA_DATA_DIRS $AMIGA_BOOT_DIRS)" &&
       mkdir -p "$AMIGA_TMP/SWOS/save" &&
       ( cd "$AMIGA_TMP" && zip -r -q "$ROOT/$AMIGA_ZIP_PATH" . -x '*/.DS_Store' ); then
        echo "-- drawer:  SWOS/ + SWOS.info; inside it SWOS2, the Workbench script and icon, $AMIGA_DIRS, empty save/"
        echo "-- (no game data: the player adds it with the data setup)"
        echo "-- OK: $AMIGA_ZIP_PATH"
    else
        echo "-- FAIL: $AMIGA_ZIP_PATH" >&2
    fi
    rm -rf "$AMIGA_TMP"
fi

# ---- package: Windows installer (Inno Setup) --------------------------------
# A packaging step, not a compile: scripts/build_installer.sh stages the runtime
# tree into dist/windows and runs ISCC over packaging/windows/swos.iss. It is
# last because it is the only artifact that depends on a Windows-side tool being
# installed — everything above still lands if Inno Setup is missing.
if [ "$DO_INSTALLER" = 1 ] && wanted installer; then
    if [ ! -f bin/SWOS.exe ]; then
        # Distinguish the two ways there is no .exe: a target that was never
        # asked for reads very differently from one that failed.
        case "${STATUS[windows]:-}" in
            FAIL)  record installer "SKIP (the windows target FAILED)" ;;
            SKIP*) record installer "SKIP (windows was skipped: ${STATUS[windows]})" ;;
            *)     record installer "SKIP (no bin/SWOS.exe — build the windows target)" ;;
        esac
    else
        banner "package -> $INSTALLER_PATH  (Windows installer)"
        scripts/build_installer.sh --output "$INSTALLER_PATH"
        case $? in
            0) record installer "OK"; echo "-- OK: installer" ;;
            # 3 means Inno Setup is not installed here — a missing tool, the same
            # as no emcc or no m68k gcc, so it SKIPs with a reason rather than
            # failing the whole drop. build_installer.sh printed where to get it.
            3) record installer "SKIP (no Inno Setup; set ISCC)" ;;
            *) record installer "FAIL"; echo "-- FAIL: installer" ;;
        esac
    fi
fi

# ---- summary ----------------------------------------------------------------
banner "SUMMARY"
printf '  save-format verify : %s\n' \
    "$( [ "$VERIFY" = 1 ] && echo 'ON  (debug — every save round-trips)' || echo 'off' )"
printf '  injuries + cards   : %s\n' \
    "$( [ "$NOINJ" = 1 ] && echo 'on; testers can switch them off OFFLINE in SWOS.ini (tester build — SWOS_TEST_RULES=1)' || echo 'on  (release build: the setting is compiled out)' )"
echo ""
RC=0
for t in $ALL_TARGETS; do
    s="${STATUS[$t]:-}"
    [ -z "$s" ] && s="not requested"
    printf '  %-14s %s\n' "$t" "$s"
    [ "$s" = "FAIL" ] && RC=1
done
echo ""
echo "  macos          not buildable here — run 'make macos-app' on a Mac"
echo ""
# The Switch artifact is a package, not a zip member — say where it landed and
# when, so "I installed the file from dist" can be checked against the commit
# that was built rather than assumed to be current.
if [ "${STATUS[switch]:-}" = "OK" ] && [ -f dist/SWOS.nsp ]; then
    ls -l --time-style='+%Y-%m-%d %H:%M' dist/SWOS.nsp 2>/dev/null |
        awk '{ printf "  switch package : %s bytes  %s %s\n", $5, $6, $7 }'
    echo ""
fi
[ "$DO_ZIP" = 1 ] && [ -f "$ZIP_PATH" ] && ls -l "$ZIP_PATH"
[ -f "$FULL_ZIP_PATH" ] && ls -l "$FULL_ZIP_PATH"
[ -f "$AMIGA_ZIP_PATH" ] && ls -l "$AMIGA_ZIP_PATH"
[ -f "$INSTALLER_PATH" ] && ls -l "$INSTALLER_PATH"
exit $RC
