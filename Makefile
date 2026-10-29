# ---- Toolchain ----
# Cross-compile to ARM 32-bit (armv7-a / armhf) with `make ARM=1`. Picks
# the standard Debian/Ubuntu cross-toolchain prefix; override with
# `make ARM=1 CC=...` if your distro uses a different name. The build
# expects libsdl2-dev:armhf installed (or a sysroot containing SDL2),
# discoverable via `arm-linux-gnueabihf-pkg-config`. The Amiga struct
# size_check[] asserts assume 32-bit pointers, which armhf already
# provides — no -m32 needed.
#
# Cross-compile to ARM 64-bit (aarch64) with `make ARM64=1`. Picks
# `aarch64-linux-gnu-gcc`; needs libsdl2-dev:arm64 (Debian/Ubuntu
# foreign-arch) or a sysroot. 64-bit pointers mean the static
# size_check[] asserts that hard-code Amiga (32-bit) struct sizes
# will fail to compile until Phase 5 / 6 of the 64-bit transition.
# Useful right now as a "what still hard-codes 32-bit" checklist.
#
# macOS: auto-detected via `uname -s == Darwin`. Native clang; expects
# SDL2 from Homebrew (`brew install sdl2`). Like ARM64, modern macOS
# is 64-bit, so the same Phase 5 / 6 constraints apply.
ifdef WASM
# WebAssembly via Emscripten (`make WASM=1`). `source <emsdk>/emsdk_env.sh`
# first so emcc is on PATH. SDL2 comes from emcc's bundled port (-sUSE_SDL=2),
# NOT pkg-config. Single-threaded + ASYNCIFY — see docs/WASM.md.
CC := emcc
CXX := em++
PKGCONF := true
# Verify-on reloads the career into live globals on every save (corrupts live
# state) — pointless in the browser; default off (command line still overrides).
SWOS_SAVE_FORMAT_JSON_VERIFY ?= 0
else ifdef ARM64
ARM64_HOST_MACHINE := $(shell uname -m)
ifneq (,$(filter aarch64 arm64,$(ARM64_HOST_MACHINE)))
CC := gcc
PKGCONF := pkg-config
else
CC := aarch64-linux-gnu-gcc
PKGCONF := PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig pkg-config
endif
else ifdef ARM
# Native ARM builds (e.g. on a Raspberry Pi) use the system gcc.
# Cross-builds from x86_64 → armhf use the standard Debian/Ubuntu
# arm-linux-gnueabihf- toolchain. Detect by host architecture; user
# can override with `make ARM=1 CC=...` if the distro's prefix is
# different.
ARM_HOST_MACHINE := $(shell uname -m)
ifneq (,$(filter arm% aarch64,$(ARM_HOST_MACHINE)))
CC := gcc
PKGCONF := pkg-config
else
CC := arm-linux-gnueabihf-gcc
# Ubuntu/Debian don't ship an arm-linux-gnueabihf-pkg-config wrapper —
# use the regular pkg-config but force it to look at the armhf
# pkgconfig directory only (PKG_CONFIG_LIBDIR overrides the default
# /usr/lib/pkgconfig:/usr/share/pkgconfig search path that would
# otherwise return amd64 SDL2 paths). libsdl2-dev:armhf installs its
# .pc into /usr/lib/arm-linux-gnueabihf/pkgconfig.
PKGCONF := PKG_CONFIG_LIBDIR=/usr/lib/arm-linux-gnueabihf/pkgconfig pkg-config
endif
else
# Default: native compiler (gcc on Linux/MinGW/Cygwin, clang on macOS).
ifeq ($(shell uname -s),Darwin)
CC := cc
# SDL2 search order on macOS:
#   1. SDL2.framework in /Library/Frameworks, ~/Library/Frameworks,
#      /opt/homebrew/Frameworks, or /usr/local/Frameworks. Preferred
#      because the official libsdl.org build ships as a universal
#      binary — needed for `make macos-universal`.
#   2. Homebrew's SDL2 via pkg-config (single-arch only).
SDL2_MACOS_FW :=
ifneq (,$(wildcard /Library/Frameworks/SDL2.framework))
SDL2_MACOS_FW := /Library/Frameworks
else ifneq (,$(wildcard $(HOME)/Library/Frameworks/SDL2.framework))
SDL2_MACOS_FW := $(HOME)/Library/Frameworks
else ifneq (,$(wildcard /opt/homebrew/Frameworks/SDL2.framework))
SDL2_MACOS_FW := /opt/homebrew/Frameworks
else ifneq (,$(wildcard /usr/local/Frameworks/SDL2.framework))
SDL2_MACOS_FW := /usr/local/Frameworks
endif
ifndef SDL2_MACOS_FW
# Homebrew SDL2 lives under /opt/homebrew (arm64) or /usr/local (intel).
PKGCONF := PKG_CONFIG_PATH=$(shell brew --prefix sdl2 2>/dev/null)/lib/pkgconfig pkg-config
endif
else
CC := gcc
endif
endif
SHELL := /bin/bash

# ---- Platform ----
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)
MACOS_ARCH ?= $(shell uname -m)

# Normalize an arch string for the build/<os>/<arch> object dir so the same
# 64-bit ARM target is "arm64" on every OS (Linux uname says aarch64, macOS/
# Windows say arm64) and 32-bit x86 is "x86": $(call norm_arch,aarch64) -> arm64.
norm_arch = $(patsubst aarch64,arm64,$(patsubst amd64,x86_64,$(patsubst i686,x86,$(patsubst i386,x86,$(1)))))

# ---- Output ----
# Each branch sets TARGET_NAME (the bin/ binary name) AND the BUILD_OS /
# BUILD_ARCH that select the build/<os>/<arch> object dir.
ifdef WASM
# emcc emits SWOS.html + SWOS.js + SWOS.wasm + SWOS.data (the preloaded data
# package). Objects live under build/emscripten/wasm (wasm32 = ILP32).
TARGET_NAME := SWOS.html
BUILD_OS    := emscripten
BUILD_ARCH  := wasm
# PLAYER=1: the recordings player for the server's /playback/ page -- the same game,
# its own page (wasm/player.html) and objects, and no netplay (docs/PLAYBACK.md).
ifdef PLAYER
BUILD_ARCH  := wasm-player
endif
else ifeq ($(OS),Windows_NT)
TARGET_NAME := SWOS.exe
BUILD_OS    := windows
BUILD_ARCH  := $(call norm_arch,$(UNAME_M))
else ifneq (,$(findstring CYGWIN,$(UNAME_S)))
# uname -s on Cygwin outputs `CYGWIN_NT-10.0-22631` (or similar with
# the Windows build number suffix), not bare `CYGWIN_NT`. Use
# `findstring` so the version suffix doesn't break detection.
TARGET_NAME := SWOS.exe
BUILD_OS    := windows
BUILD_ARCH  := $(call norm_arch,$(UNAME_M))
else ifneq (,$(findstring MINGW,$(UNAME_S)))
# Same suffix issue for MSYS2 MINGW32_NT-X.Y / MINGW64_NT-X.Y.
TARGET_NAME := SWOS.exe
BUILD_OS    := windows
BUILD_ARCH  := $(call norm_arch,$(UNAME_M))
else ifdef ARM64
TARGET_NAME := SWOS.aarch64
BUILD_OS    := linux
BUILD_ARCH  := arm64
else ifdef ARM
# Distinguish the cross-built ARM binary so it doesn't overwrite a
# native x86 bin/SWOS in the same checkout. Native ARM builds (on a
# Pi) also get the .armhf suffix — `file bin/SWOS.armhf` confirms the
# ELF arch either way.
TARGET_NAME := SWOS.armhf
BUILD_OS    := linux
BUILD_ARCH  := armhf
else ifdef X86_64
# Linux x86_64 explicit build (drops the default -m32 so the binary
# is 64-bit LP64 — matches what the macOS / aarch64 / cygwin64
# targets already produce). The size_check[] asserts are gated on
# UINTPTR_MAX so they pick up the 64-bit struct sizes correctly.
TARGET_NAME := SWOS.x86_64
BUILD_OS    := linux
BUILD_ARCH  := x86_64
else ifeq ($(UNAME_S),Darwin)
TARGET_NAME := SWOS.macos
BUILD_OS    := macos
BUILD_ARCH  := $(call norm_arch,$(MACOS_ARCH))
else
# Default native build: 32-bit x86 (-m32).
TARGET_NAME := SWOS
BUILD_OS    := linux
BUILD_ARCH  := x86
endif

BINDIR := bin
# Where the linked target lands. Everything native goes to bin/ — the shared
# runtime tree the game chdir's into and reads its data from.
#
# WASM is the exception: it is not a binary anyone runs from bin/, it is a web
# DEPLOYMENT of four coupled files (SWOS.html + .js + .wasm + the ~21 MB .data
# preload) that are only useful served together. They belong with the rest of
# the release output, so they go to dist/web — beside the zips, the installer
# and the .nsp. scripts/build_dist.sh has always excluded them from every
# archive for the same reason.
ifdef WASM
ifdef PLAYER
OUTDIR := dist/playback
else
OUTDIR := dist/web
endif
else
OUTDIR := $(BINDIR)
endif
TARGET := $(OUTDIR)/$(TARGET_NAME)

# ---- Object output dir (build/<os>/<arch>) ----
# Objects build under build/<os>/<arch> instead of cluttering src/, split by OS
# AND arch so every target in the same checkout keeps its own .o files:
# build/linux/x86_64, build/windows/x86_64, build/windows/arm64, build/linux/
# arm64, build/macos/arm64, … never collide. (The old flat in-tree layout forced
# a `make clean` whenever you switched target; Windows-on-ARM now slots in too.)
OBJDIR := build/$(BUILD_OS)/$(BUILD_ARCH)

# ---- Sources ----
SRCDIR  := src
INCDIR  := include
# The build stamp: regenerate include/swos_build.h before anything compiles.
# Parse-time, so it is always current; the script rewrites the header only when
# the commit changes, so this does not force a rebuild (scripts/gen_build_info.sh).
# Invoked through `sh`, not as an executable: core.filemode is false on the
# development mount and every script here is recorded 100644, so a fresh clone
# cannot exec it -- which broke the first clean-export build of this commit.
BUILD_STAMP_UNUSED := $(shell sh scripts/gen_build_info.sh)

SOURCES := $(wildcard $(SRCDIR)/*.c) $(wildcard $(SRCDIR)/generated/*.c)
OBJECTS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SOURCES))
DEPS    := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.d,$(SOURCES))

# The generated-C rules below define `src/*.c` TARGETS, which would otherwise become the
# Makefile's first (default) goal. Pin the default goal so a bare `make` still builds the
# binary (the `all` target), not competition_data.c.
.DEFAULT_GOAL := all

# ---- Data-driven C (JSON -> C bake) ----
# competition_data.c and menu_data.c are GENERATED from the editable JSON in
# bin/data_json/ by the Python emitters (the Amiga build bakes JSON->C the same way).
# Editing the JSON regenerates the C at build; `make regen-data` forces it. To verify the
# regen is faithful, run the bare emitter (no --out): python3 scripts/emit_competitions.py
COMP_JSON := $(wildcard bin/data_json/competitions/*.json)
MENU_JSON := $(wildcard bin/data_json/menus/*.json)
TACTICS_JSON := $(wildcard bin/data_json/tactics/*.json)

# python3 for the JSON->C regen. If it's not on PATH (e.g. a minimal Cygwin), the committed
# .c is compiled as-is and a JSON edit needs python3 to take effect (the touch suppresses a
# pointless rebuild loop). The committed .c always exists, so the build never hard-fails.
PYTHON := $(shell command -v python3 2>/dev/null || command -v python 2>/dev/null)

$(SRCDIR)/competition_data.c: $(COMP_JSON) scripts/emit_competitions.py scripts/export_competitions.py scripts/swos_team_file.py
ifeq ($(strip $(PYTHON)),)
	@echo "  WARN  python3 not found; compiling the committed $@ (JSON edits won't apply)"; touch $@
else
	@echo "  REGEN $@"; $(PYTHON) scripts/emit_competitions.py --out $@ >/dev/null
endif

$(SRCDIR)/menu_data.c: $(MENU_JSON) scripts/emit_menus.py scripts/export_menus.py
ifeq ($(strip $(PYTHON)),)
	@echo "  WARN  python3 not found; compiling the committed $@ (JSON edits won't apply)"; touch $@
else
	@echo "  REGEN $@"; $(PYTHON) scripts/emit_menus.py --out $@ >/dev/null
endif

$(SRCDIR)/tactics_data.c: $(TACTICS_JSON) scripts/emit_tactics.py scripts/export_tactics.py
ifeq ($(strip $(PYTHON)),)
	@echo "  WARN  python3 not found; compiling the committed $@ (JSON edits won't apply)"; touch $@
else
	@echo "  REGEN $@"; $(PYTHON) scripts/emit_tactics.py --out $@ >/dev/null
endif

.PHONY: regen-data
regen-data:
	@test -n "$(strip $(PYTHON))" || { echo "regen-data needs python3 on PATH"; exit 1; }
	$(PYTHON) scripts/emit_competitions.py --out $(SRCDIR)/competition_data.c
	$(PYTHON) scripts/emit_menus.py --out $(SRCDIR)/menu_data.c
	$(PYTHON) scripts/emit_tactics.py --out $(SRCDIR)/tactics_data.c

# ---- Windows resource (icon) ----
# On MinGW and Cygwin we compile SWOS.rc → SWOS.res.o via windres and
# link it in, which gives the .exe the SWOS.ico for both the file icon
# (Explorer) and the SDL window/taskbar icon (SDL2 picks up resource ID
# 1 when no SDL_SetWindowIcon is called).
#
# `gcc -dumpmachine` returns e.g. `x86_64-w64-mingw32` on MinGW and
# `x86_64-pc-cygwin` on Cygwin64 — match either via findstring.
GCC_MACHINE := $(shell $(CC) -dumpmachine 2>/dev/null)
WINDRES ?= windres
ifneq (,$(findstring mingw,$(GCC_MACHINE)))
WIN_RES_OBJ := $(OBJDIR)/SWOS.res.o
OBJECTS     += $(WIN_RES_OBJ)
else ifneq (,$(findstring cygwin,$(GCC_MACHINE)))
WIN_RES_OBJ := $(OBJDIR)/SWOS.res.o
OBJECTS     += $(WIN_RES_OBJ)
endif

# ---- Helpers ----
PKGCONF ?= pkg-config

ifdef WASM
# Emscripten ships SDL2 as a port — downloaded + cached on first use. Same flag
# for compile and link; no pkg-config.
SDL2_CFLAGS := -sUSE_SDL=2
SDL2_LIBS   := -sUSE_SDL=2
else ifdef SDL2_MACOS_FW
# macOS framework: header search via -F + Headers/, link via -framework SDL2.
# The -rpath entries let the binary find SDL2.framework at runtime when
# either bundled inside the .app (Contents/Frameworks/) or installed
# system-wide under /Library/Frameworks/ or ~/Library/Frameworks/.
SDL2_CFLAGS := -F $(SDL2_MACOS_FW) -I $(SDL2_MACOS_FW)/SDL2.framework/Headers
SDL2_LIBS   := -F $(SDL2_MACOS_FW) -framework SDL2 \
               -Wl,-rpath,@executable_path/../Frameworks \
               -Wl,-rpath,/Library/Frameworks \
               -Wl,-rpath,$(HOME)/Library/Frameworks
else
SDL2_CFLAGS := $(shell $(PKGCONF) --cflags sdl2 2>/dev/null || sdl2-config --cflags 2>/dev/null)
SDL2_LIBS   := $(shell $(PKGCONF) --libs sdl2 2>/dev/null || sdl2-config --libs 2>/dev/null)
endif

# ---- Build flags ----
# -m32 keeps pointers and `long` at 32 bits so the Amiga struct layouts
#   (and the static size_check[] asserts) survive on a 64-bit host.
# -include forces swos_platform.h into every TU before any other header.
# -fno-strict-aliasing matches the original Amiga build (heavy type punning).
# Warnings dialed back: the imported Amiga code triggers many style warnings
# that are not bugs on x86/x64 but would otherwise drown the output.
#
# MSYS2/MINGW64 ships 64-bit-only libs and has no multilib, so -m32 fails
# at link time ("skipping incompatible libmingw32.a"). Detect the
# 64-bit-only mingw target via `gcc -dumpmachine` and drop -m32 there.
# Testers wanting an actual 32-bit Windows binary should use MSYS2
# MINGW32 (gcc already 32-bit; -m32 is a no-op) or Cygwin's 32-bit
# toolchain.
ifdef WASM
# wasm32 is ILP32 — no -m flags; emcc targets wasm by default. (Struct
# size_check[] asserts pass on ILP32, same as armhf.)
ARCHFLAGS :=
else ifdef ARM64
# aarch64 is 64-bit (LP64) — no -m32 / -m64 needed. The static
# size_check[] asserts that hard-code Amiga 32-bit struct sizes
# will fail until Phase 5 / 6 lands.
ARCHFLAGS :=
else ifdef ARM
# armhf is already 32-bit (ILP32) — no -m32 needed; passing it would
# fail with "unrecognized command-line option '-m32'" on most ARM gcc
# builds. Force armv7-a to keep things uniform across Pi 2/3/4 etc.
ARCHFLAGS := -march=armv7-a -mfpu=neon -mfloat-abi=hard
else ifeq ($(UNAME_S),Darwin)
# Modern macOS is 64-bit (arm64 or x86_64). No -m32. The static
# size_check[] asserts are gated on UINTPTR_MAX (see game_types.h) and
# already pick the 64-bit struct sizes.
#
# MACOS_ARCH: target architecture. Defaults to the host arch (so a plain
# `make` on an M-series Mac produces arm64, on Intel produces x86_64).
# Set explicitly to build a single non-host slice (used by macos-universal
# below). Accepted values match clang's -arch flag: arm64 or x86_64.
#
# MACOS_MIN: minimum deployment target. 11.0 = Big Sur — the oldest macOS
# that supports Apple Silicon natively and that libsdl.org's prebuilt
# SDL2.framework still loads on. Override with MACOS_MIN=12.0 etc. if
# you only need to target newer systems.
MACOS_ARCH ?= $(shell uname -m)
MACOS_MIN  ?= 11.0
ARCHFLAGS := -arch $(MACOS_ARCH) -mmacosx-version-min=$(MACOS_MIN)
else ifeq ($(GCC_MACHINE),x86_64-w64-mingw32)
ARCHFLAGS :=
else ifneq (,$(findstring CYGWIN,$(UNAME_S)))
# Cygwin64's gcc is 64-bit-only; -m32 fails with "unimplemented for
# this target" in <signal.h>. Drop -m32 and let it produce a native
# 64-bit Windows binary (struct sizes go through the Phase 5
# bitness-conditional asserts the same way aarch64 does). Testers
# wanting a 32-bit Windows binary should use MSYS2 MINGW32 instead.
ARCHFLAGS :=
else ifdef X86_64
# Explicit 64-bit Linux x86 build. Drop -m32 and pass -m64 so the
# binary is LP64. Same code path as cygwin64 / aarch64 / macOS for
# the bitness-conditional size_check asserts.
ARCHFLAGS := -m64
else
ARCHFLAGS := -m32
endif
# All Amiga "data module" toggles set to 1 so the C versions of
# the leagues, palettes, sprites, fixtures, etc. are compiled in
# (replacing the SWOS2.s data section). This eliminates a large
# fraction of the zero-filled stubs.
DATA_FLAGS := -DCOMPETITION_DATA_C=1 -DTACTICS_DATA_C=1 -DFIXTURE_DATA_C=1 \
              -DSIMULATION_DATA_C=1 -DPLAYER_DATA_C=1 -DRENDER_DATA_C=1 \
              -DPALETTE_DATA_C=1 -DSTRINGS_DATA_C=1 -DGENERAL_DATA_C=1

# Diagnostic logging defaults to OFF. The bring-up loggers (autoskip
# menu trace, SFX trigger trace) are still in the source — gated on
# SWOS_DEBUG_AUTOSKIP / SWOS_DEBUG_SFX — so a tester chasing a
# regression can re-enable with `make SWOS_DEBUG=1`.
ifdef SWOS_DEBUG
DEBUG_FLAGS := -DSWOS_DEBUG_AUTOSKIP=1 -DSWOS_DEBUG_SFX=1
endif

# Toggling a -D flag below rebuilds every object that the flag reaches -- see
# BUILD_FLAGS_STAMP further down. It used not to: make compares TIMESTAMPS, so
# `make X86_64=1` after `make X86_64=1 SWOS_TEST_NO_INJURIES_CARDS=1` (as it was) relinked
# the old objects, and a binary could carry the flag in some files and not in
# others -- which desynced a live match (2026-09-19, see there).
#
# The testers' rule -- no cards, no injuries, OFFLINE only -- is a SWOS.ini setting,
# `[testing] no_injuries_cards = true`, off by default (include/test_rules.h). It lets a
# tester hit VIEW RESULT uninterruptedly through a season: no substitution menus, no
# booking prompts. SWOS_TEST_RULES=1 (the default) compiles the setting in;
# `make SWOS_TEST_RULES=0` compiles the rule OUT, for a public release nobody should be
# able to switch it on in (scripts/build_dist.sh --release). It also marks a tester
# build for the defaults that differ (match reports on, the menu navigation log).
#
# Until 2026-10-11 this was SWOS_TEST_NO_INJURIES_CARDS, compile-time: a tester's binary
# was a different game from a release, took the rule into online matches, and a binary
# built partly with it desynced one.
#
# KNOWN DISPLAY SIDE-EFFECTS with the rule ON (do NOT re-investigate as port
# bugs — they vanish with the setting off). IsPlayerInjured() returns 0 for
# everyone, so any player carrying a PRE-EXISTING injury/suspension in a loaded
# save is "healthy" to every gameplay query while the row indicator — which
# reads pf->cardsInjuries directly — still shows the "3":
#   * the squad screens' red name needs the honest status; all three call sites
#     now use IsPlayerInjuredForDisplay() (edit_tactics.c, play_match_menu.c
#     DrawPlayerEntries, menus.c SquadAfterDrawCommon), so this one is handled.
#   * the tick/cross marks still read low: UpdatePlayerPositionAndReturnRole
#     zeroes an injured player's position value via IsPlayerInjured, so under
#     the flag he keeps full value and substituting him off scores as a
#     DOWNGRADE (big red cross instead of a green tick). Left as-is — that
#     value also feeds the match sim, where the flag's "he can play" is the
#     intended behaviour.
#   * squad LEGALITY (isPlayerDisabled -> GetNumberOfAvailablePlayers/
#     Substitutes) read cardsInjuries directly, so ShowOrHidePlayMatchButton
#     still hid the PLAY MATCH / VIEW RESULT button and forced a substitution —
#     the exact friction the flag exists to remove. isPlayerDisabled now honours
#     the override too, so this one is handled. (Stock builds are unaffected:
#     an injured starter still hides the button, per SWOS2.s loc_14872A.)
SWOS_TEST_RULES ?= 1
ifeq ($(SWOS_TEST_RULES),1)
DEBUG_FLAGS += -DSWOS_TEST_RULES=1
endif

# The football worlds beyond 96/97 (World 2026-27 and all it brought: include/swos_types.h,
# docs/WORLD_2026_27.md). `make SWOS_WORLDS=0` compiles them out: 96/97 alone, as before them.
SWOS_WORLDS ?= 1
DEBUG_FLAGS += -DSWOS_WORLDS=$(SWOS_WORLDS)

# Build with the PRISTINE Amiga data for the four formations this port
# deliberately corrected — 4-4-2, 4-3-3, 3-4-3, Sweep (commit 1828528, whose
# tester-supplied fix un-swaps central defenders that crossed sides instead of
# holding station). It is the port's one intentional gameplay-data divergence,
# so anything measuring us against ORIGINAL behaviour — the recorded-game
# replay rig above all (docs/TESTS.md) — needs to be able to turn it off.
# `make clean` first: this changes baked data and make compares timestamps,
# not flags. See the header comment in src/tactics_data.c.
ifdef SWOS_ORIGINAL_TACTICS
DEBUG_FLAGS += -DSWOS_ORIGINAL_TACTICS=1
endif

# The DOS release's formation coverage instead of the Amiga's. Nine of the
# twelve are byte-identical; 4-4-2 (68 bytes), Sweep (64) and 4-3-3 (10) are
# not, and 60/61 of the first two are the two forward slots, rewritten. Since
# GetPlayerPrice walks the tactics table for every player, this changes player
# values and in-game skill nibbles as well as where men stand.
# `make clean` first (same reason as above), and it is mutually exclusive with
# SWOS_ORIGINAL_TACTICS. See src/tactics_data.c and docs/TACTICS_POSITION_VALUE.md
# section 9 — including why the replay corpus disagrees with the forward slots.
ifdef SWOS_DOS_TACTICS
DEBUG_FLAGS += -DSWOS_DOS_TACTICS=1
endif

# Debug: force a World Cup for a national-team career, bypassing the natural
# 4-year competition cycle, so the national-coach WC path can be reproduced on
# demand (e.g. the season-transition 0x8888 backup-clobber in
# _VerifyTeamLoaded). Off by default; enable with `make SWOS_INJECT_WC=1`.
# Only affects national-team career selection (country code 80..85), and is
# compile-time only so production builds never ship with it on. See the inject
# site in src/career_init.c (_InitializeNewSeason).
ifdef SWOS_INJECT_WC
DEBUG_FLAGS += -DSWOS_INJECT_WC=1
endif

# Save-file format toggle. Default ON — new saves are written as JSON
# via mjson (see src/save_format_json.c) and load auto-detects format
# from the file header.
#
# The big overlay-dump saves (.CAR/.SEA/.PRE/.DIY) are now JSON-only on
# BOTH read and write regardless of this flag — their legacy raw-memory-
# dump readers AND writers have been deleted (roadmap step 4 complete).
# The flag now only governs the WRITE format of the small fixed-layout
# records that KEEP binary support (.HIL/.TAC): ON (default) writes them
# JSON-first; =0 writes them in the original binary layout. Their binary
# READ path is compiled either way (load auto-detects), so .HIL/.TAC/.EDT/
# .CFG interop is preserved. See docs/SAVE_DATA.md "Format migration /
# read-support policy".
ifneq ($(SWOS_SAVE_FORMAT_JSON),0)
DEBUG_FLAGS += -DSWOS_SAVE_FORMAT_JSON=1
endif

# Coverage verification for the JSON save format. Default ON during
# bring-up — load-time diff compares the byte image produced by the
# named-blob + semantic field loaders against the bytes from opaque_data.
# A zero-mismatch result means every meaningful byte is covered by typed
# fields/blobs and opaque_data can be removed. Opt out with
# `make SWOS_SAVE_FORMAT_JSON_VERIFY=0` to skip the diff (small load-time
# perf win once we trust the format).
ifneq ($(SWOS_SAVE_FORMAT_JSON_VERIFY),0)
DEBUG_FLAGS += -DSWOS_SAVE_FORMAT_JSON_VERIFY=1
endif

# Profile flag: prints per-phase wall-clock timings (`[json-prof-*]`
# lines) so you can see where load/save time is going. Default OFF —
# profiling is largely done; opt in with `make SWOS_SAVE_FORMAT_JSON_PROFILE=1`
# when investigating save/load perf regressions.
ifdef SWOS_SAVE_FORMAT_JSON_PROFILE
DEBUG_FLAGS += -DSWOS_SAVE_FORMAT_JSON_PROFILE=1
endif

# Legacy-dispatch toggle: build with `make SWOS_SAVE_LEGACY_DISPATCH=1` to
# select the per-field path-lookup loop (slow but the established
# baseline). Default OFF — the new walk-once dispatch is faster.
# Keep around for comparison testing when investigating verify deltas.
ifdef SWOS_SAVE_LEGACY_DISPATCH
DEBUG_FLAGS += -DSWOS_SAVE_LEGACY_DISPATCH=1
endif

CFLAGS  := $(ARCHFLAGS) -O2 -g -std=gnu11 -fno-strict-aliasing -fno-builtin \
           -MMD -MP \
           -D_FILE_OFFSET_BITS=64 $(DEBUG_FLAGS) \
           -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable \
           -Wno-attributes -Wno-multichar -Wno-builtin-declaration-mismatch \
           -Wno-pointer-sign -Wno-int-conversion -Wno-incompatible-pointer-types \
           -Wno-braces -Wno-address-of-packed-member \
           -Wno-strict-aliasing -Wno-array-bounds -Wno-stringop-overflow \
           $(DATA_FLAGS) \
           -include $(INCDIR)/swos_platform.h \
           -I$(SRCDIR) -I$(INCDIR) $(SDL2_CFLAGS)
# -Wno-braces:                 tactics_data.c auto-extractor wraps scalar
#                              initialisers in `{ }`; intentional, ~4200 hits.
# -Wno-address-of-packed-member: packed structs mirror Amiga byte layout
#                              everywhere; we take &field deliberately.
# -Wno-strict-aliasing:        we already pass -fno-strict-aliasing for the
#                              same reason; this just silences the diag.
# -Wno-array-bounds:           Amiga BSS adjacency reads (e.g. extern
#                              uint8_t selTeamsPtr; then read [4]) intentionally
#                              walk past the declared end of 1-byte stubs.
#                              Real adjacency bugs surface as runtime panics —
#                              this warning is too noisy to be useful.
# -Wno-stringop-overflow:      same root cause as -Wno-array-bounds but for
#                              the WRITE side (`((uint8_t *)&selTeamsPtr)[4] = x`).
#                              The compiler sees a 1-byte stub and flags any
#                              wider write; we know the real backing is much
#                              larger.
LDFLAGS := $(ARCHFLAGS)

# Linux: look for a bundled SDL beside the binary before falling back to the
# system one. SWOS-portable.zip ships lib/<arch>/libSDL2-2.0.so.0 (see
# LINUX_SDL_LIBS in scripts/build_dist.sh) so a tester who has never installed
# libsdl2 can still run it; a dev tree has no lib/ dir, so the loader falls
# straight through to the system copy and nothing changes here.
#
# --disable-new-dtags emits DT_RPATH, NOT the more modern DT_RUNPATH, and that
# is deliberate. DT_RUNPATH applies only to the executable's DIRECT
# dependencies; DT_RPATH is inherited down the whole chain. We need the
# inherited kind, because the distro libSDL2 links its backends rather than
# dlopening them -- with RUNPATH the bundled libSDL2 loads but its own
# dependency on libdecor-0.so.0 is resolved from ld.so.cache only, so on a
# machine without libdecor the bundle fails anyway and shipping it was pointless.
# Measured, not assumed: under RUNPATH ldd showed libSDL2 coming from the bundle
# and libdecor from /lib/x86_64-linux-gnu.
#
# The cost is that DT_RPATH is searched BEFORE LD_LIBRARY_PATH, so that is no
# longer an override. To use a system SDL2 instead, delete the lib/ directory --
# the loader then falls straight through to ld.so.cache. (patchelf on the
# bundled .so would let us keep RUNPATH semantics, but it is not installed here
# and a build machine without it would silently produce a broken drop.)
#
# $$ORIGIN survives make -> shell -> linker as a literal $ORIGIN, resolved by
# the loader at run time. Do not "fix" it to an absolute path.
ifeq ($(BUILD_OS),linux)
LDFLAGS += -Wl,--disable-new-dtags -Wl,-rpath,'$$ORIGIN/lib/$(BUILD_ARCH)'
endif

# macOS: switch to Apple's classic linker (-ld_classic). Modern ld64
# (Xcode 15+) is strict about pointer-relocation alignment and rejects
# packed-struct atoms whose internal pointer fields aren't 8-byte
# aligned — emits errors like "pointer not aligned in _euroCup+0x2E".
# The classic linker behaves like every other linker and accepts the
# misaligned relocations, which is fine at runtime: ARM64 (and x86_64)
# scalar loads cope with misaligned pointers correctly. This keeps the
# struct layout identical across all 64-bit targets (Linux aarch64
# already builds + runs with the same packed structs).

ifeq ($(UNAME_S),Darwin)
LDFLAGS += -Wl,-ld_classic
endif

# The original game's data is not in this repo: the data setup (tools/swosdata)
# builds it from the player's own copy into the assets folder, the same folder
# the game reads at run time (swos_assets_dir() in src/swos_io.c). The web build
# bakes both into SWOS.data: the project's own files from bin/, the game's from
# here. Override with SWOS_ASSETS=/path (also what the game reads).
ifeq ($(origin SWOS_ASSETS),undefined)
SWOS_ASSETS := $(HOME)/.swos/assets
endif
WASM_DATA_DIRS := data grafs grafs_aga grafs_jag sound data_json

# Emscripten link flags. ASYNCIFY lets the blocking game loop yield to the
# browser at the one Platform_VBlank choke-point (emscripten_sleep — docs/WASM.md
# §0/§2a). Growable heap; preload the read-only game data into MEMFS; IDBFS for
# persistent saves at /save (mounted in wasm/pre.js). Single-threaded — the
# 20 ms SDL timer + 5 ms audio-pump thread are compiled out (#ifdef SWOS_WASM_BUILD).
#
# BOTH graphics tiers are preloaded: resload.c resolves a grafs load as
# grafs_aga/ first then grafs/ (display.h defines AGA on this build too), so
# without grafs_aga in MEMFS the browser silently ran the 4bpp OCS set while
# native ran 8bpp. grafs_aga is currently a complete superset, so grafs/ is only
# the safety net — droppable later if the ~268K matters for download size.
ifdef WASM
ifdef PLAYER
WASM_SHELL := wasm/player.html
else
WASM_SHELL := wasm/shell.html
endif
# EMULATE_FUNCTION_POINTER_CASTS: SWOS's menu system stores void(void) callbacks
# (e.g. FriendlyMenu) in void(MenuEntry*) fields and invokes them through void*
# casts — fine on every native ABI, but wasm hard-traps an indirect call whose
# table signature differs ("indirect call signature mismatch"). This generates
# adapter thunks so the mismatched calls work, like native. wasm-only flag.
LDFLAGS += -sASYNCIFY -sASYNCIFY_STACK_SIZE=65536 \
           -sEMULATE_FUNCTION_POINTER_CASTS=1 \
           -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=134217728 \
           -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,FS -lidbfs.js \
           --pre-js wasm/pre.js --shell-file $(WASM_SHELL) \
           $(foreach d,$(WASM_DATA_DIRS),$(foreach r,bin $(SWOS_ASSETS),\
               $(if $(wildcard $(r)/$(d)),--preload-file $(r)/$(d)@/$(d))))
# Re-link when the pre-js or shell HTML changes (both embedded at link time).
WASM_LINK_DEPS := wasm/pre.js $(WASM_SHELL)
# The --preload-file dirs are baked into SWOS.data AT LINK TIME, but nothing above
# makes them a prerequisite — so editing/adding a data file and re-running
# `make WASM=1` relinked nothing and silently shipped the OLD SWOS.data. (Hit for
# real when bin/data_json/teams landed: SWOS.data stayed 2.8MB until the target was
# deleted by hand, then jumped to 21MB.) List the preloaded files so a data change
# forces the relink.
WASM_PRELOAD_FILES := $(wildcard $(foreach r,bin $(SWOS_ASSETS),\
                                 $(r)/data/* $(r)/grafs/* $(r)/grafs_aga/* $(r)/grafs_jag/* $(r)/sound/* \
                                 $(r)/data_json/*.json $(r)/data_json/*/*.json \
                                 $(r)/data_json/*/*/*.json))
WASM_LINK_DEPS += $(WASM_PRELOAD_FILES)
endif

LDLIBS  := $(SDL2_LIBS) -lm
# timeBeginPeriod (src/platform_timer.c). winmm.dll is part of Windows, so the
# shipped DLL list does not change.
ifeq ($(BUILD_OS),windows)
LDLIBS  += -lwinmm
endif

# ---- Netplay: GGPO-X rollback networking (docs/NETPLAY.md) ----
# third_party/ggpo-x is C++; src/netplay.c reaches it through ggponet.h's C API.
# Default ON where it is built and tested: 64-bit Linux (X86_64=1), macOS (both
# slices) and Cygwin64. Anywhere else opt in with NETPLAY=1 -- it needs a C++
# compiler for the target, and the default -m32 Linux build also needs
# g++-multilib. Without it src/netplay.c compiles to stubs.
#
# WASM: a browser has no UDP sockets, so Emscripten's socket layer carries each
# datagram as one WebSocket message to ws://host:port -- wss:// from an https page --
# the matchmaker's WebSocket gate (--websocket, docs/NETPLAY.md §8), which hands it
# to the server's UDP port as if it came from an ordinary client. A browser can
# only ever talk to that server, so it always plays through the relay. Default ON
# since 2026-09-24, when the public server (us.swos.eu) got its certificate: the
# public page is https, and until the gate spoke wss:// its MULTIPLAYER button
# would have led nowhere. `make WASM=1 NETPLAY=0` builds one without.
#
# macOS was off until 2026-09-17 for no reason but never having been tried: the
# arch flags were already threaded through the C++ and ENET rules ($(ARCHFLAGS)),
# and the link already skips -static-libstdc++ on Darwin (clang ships libc++).
# A Mac player could not see the MULTIPLAYER button at all, since the main menu
# hides it without SWOS_NETPLAY.
#
# The C++ runtime is linked STATICALLY, so the binary gains no DLL/.so: the
# Windows drop's exact four-DLL list (CLAUDE.md) and the Linux lib/<arch> bundle
# stay as they are. Check with `cygcheck bin/SWOS.exe` / `ldd bin/SWOS.x86_64`.
#
# Toggling NETPLAY rebuilds every object by itself: the flags stamp (CLAUDE.md).
GGPO_DIR := third_party/ggpo-x/src
ENET_DIR := third_party/enet
ifndef NETPLAY
ifdef PLAYER
NETPLAY := 0
else ifdef WASM
NETPLAY := 1
else ifeq ($(BUILD_OS)/$(BUILD_ARCH),linux/x86_64)
NETPLAY := 1
else ifeq ($(BUILD_OS),macos)
NETPLAY := 1
else ifneq (,$(findstring cygwin,$(GCC_MACHINE)))
NETPLAY := 1
else
NETPLAY := 0
endif
endif
LINK := $(CC)
ifeq ($(NETPLAY),1)
GGPO_SRCS := $(filter-out %/platform_windows.cpp, \
                 $(wildcard $(GGPO_DIR)/lib/ggpo/*.cpp \
                            $(GGPO_DIR)/lib/ggpo/network/*.cpp \
                            $(GGPO_DIR)/lib/ggpo/backends/*.cpp))
GGPO_OBJS := $(patsubst $(GGPO_DIR)/lib/ggpo/%.cpp,$(OBJDIR)/ggpo/%.o,$(GGPO_SRCS))
OBJECTS   += $(GGPO_OBJS)
# The one C++ file of ours: it bridges the C emulator to GGPO's C++ hook.
OBJECTS   += $(OBJDIR)/netplay_netem_install.o
DEPS      += $(OBJDIR)/netplay_netem_install.d
DEPS      += $(GGPO_OBJS:.o=.d)
CFLAGS    += -DSWOS_NETPLAY=1 -I$(GGPO_DIR)/include
# -w: third-party code, compiled as upstream ships it (plus OPENSWOS.md's patches).
# GGPO_SEND_FRAME_ADVANTAGE=0: the time sync works from each peer's own estimate alone.
# That is how every build before 2026-10-06 ran without meaning to -- the fork's min()
# returned a dangling reference, so the advantage a peer sent was noise, nearly always
# 0 -- and it is what the pacing in src/netplay_match.c was tuned against: with the real
# figure the same pacing over-corrects and the two peers swing five frames apart
# (docs/NETPLAY_BALANCE.md). Sending a plain 0 keeps that behaviour without the noise.
GGPO_CXXFLAGS := $(ARCHFLAGS) -O2 -g -std=gnu++17 -MMD -MP -w \
                 -DGGPO_SEND_FRAME_ADVANTAGE=0 \
                 -I$(GGPO_DIR)/include -I$(GGPO_DIR)/lib/ggpo \
                 -I$(GGPO_DIR)/lib/ggpo/network -I$(GGPO_DIR)/lib/ggpo/backends \
                 -Iinclude

# ENET: the transport for matchmaking (src/mm_client.c) and the same library the
# vendored server is built from -- one copy, see third_party/enet/OPENSWOS.md.
#
# win32.c ONLY for a real Win32 target. Cygwin sets BUILD_OS=windows but is a
# POSIX host and does NOT define _WIN32, and enet.h picks its platform header on
# exactly that (`#if defined(_WIN32)` -> enet/win32.h, else enet/unix.h). Keying
# this off BUILD_OS would hand Cygwin win32.c against unix.h and fail to build.
# So it keys off mingw, the same way the rest of the file distinguishes them.
ENET_SRCS := $(ENET_DIR)/callbacks.c $(ENET_DIR)/compress.c $(ENET_DIR)/host.c \
             $(ENET_DIR)/list.c $(ENET_DIR)/packet.c $(ENET_DIR)/peer.c \
             $(ENET_DIR)/protocol.c
ifneq (,$(findstring mingw,$(GCC_MACHINE)))
ENET_SRCS += $(ENET_DIR)/win32.c
ENET_PLATFORM_DEFS :=
else
ENET_SRCS += $(ENET_DIR)/unix.c
# Normally probed by enet's own autotools/CMake. Getting one wrong does not fail
# the build, it silently changes socket behaviour -- so they are written out.
ENET_PLATFORM_DEFS := -DHAS_SOCKLEN_T=1 -DHAS_POLL=1 -DHAS_FCNTL=1 \
                      -DHAS_INET_PTON=1 -DHAS_INET_NTOP=1 -DHAS_MSGHDR_FLAGS=1
endif
ENET_OBJS := $(patsubst $(ENET_DIR)/%.c,$(OBJDIR)/enet/%.o,$(ENET_SRCS))
OBJECTS   += $(ENET_OBJS)
DEPS      += $(ENET_OBJS:.o=.d)
CFLAGS    += -I$(ENET_DIR)/include
# -w: third-party code, compiled as upstream ships it.
ENET_CFLAGS := $(ARCHFLAGS) -O2 -g -MMD -MP -w -I$(ENET_DIR)/include $(ENET_PLATFORM_DEFS)
ifneq (,$(findstring mingw,$(GCC_MACHINE)))
LDLIBS    += -lws2_32 -lwinmm
endif

LINK      := $(CXX)
# Not Darwin (clang ships libc++) and not WASM (em++ links its own runtime).
ifneq ($(UNAME_S),Darwin)
ifndef WASM
LDFLAGS   += -static-libstdc++ -static-libgcc
endif
endif
endif

# ---- A changed flag rebuilds everything ----
# make compares TIMESTAMPS, not flags. Toggling a -D (SWOS_TEST_NO_INJURIES_CARDS,
# say) used to relink the old objects, and hand back a binary with the old flag in
# every file that had not changed since and the new one in the rest. That is how a
# Windows build that books players (a stale tackle_foul.o, compiled without the flag)
# met a clean Mac build that cannot: a live match desynced at the first booking
# (2026-09-19), with both build stamps reading the same commit.
#
# So the compile flags are written to one file per object tree, rewritten only when
# they change, and every object depends on it. $(shell), not $(file): macOS still
# ships GNU make 3.81.
BUILD_FLAGS_STAMP := $(OBJDIR)/.build-flags
BUILD_FLAGS_NOW   := $(strip $(CC) $(CFLAGS) :: $(CXX) $(GGPO_CXXFLAGS) :: $(ENET_CFLAGS))
ifneq ($(BUILD_FLAGS_NOW),$(strip $(shell cat $(BUILD_FLAGS_STAMP) 2>/dev/null)))
$(shell mkdir -p $(OBJDIR) && printf '%s\n' '$(BUILD_FLAGS_NOW)' > $(BUILD_FLAGS_STAMP))
endif
# Gone after a `clean` in the same run: write it again rather than stop.
$(BUILD_FLAGS_STAMP):
	@mkdir -p $(dir $@)
	@printf '%s\n' '$(BUILD_FLAGS_NOW)' > $@

# ---- Targets ----
.PHONY: all clean distclean debug macos-universal macos-app
all: $(TARGET)
ifdef PLAYER
# The recordings list beside the player (docs/PLAYBACK.md).
all: $(OUTDIR)/index.html
$(OUTDIR)/index.html: wasm/playback-index.html
	@mkdir -p $(OUTDIR)
	cp $< $@
endif

debug:
	$(MAKE) DEBUG=1 all

# Ensure the output dir exists, then link. $(WASM_LINK_DEPS) re-links on a
# pre-js change (empty for non-WASM targets). emcc drops SWOS.js/.wasm/.data
# beside the -o target, so pointing TARGET at dist/web moves all four.
$(TARGET): $(OBJECTS) $(WASM_LINK_DEPS) | $(OUTDIR)
	$(LINK) $(OBJECTS) -o $@ $(LDFLAGS) $(LDLIBS)

# Create output directory if needed ($(OUTDIR) is bin/ for every native target)
$(OUTDIR):
	mkdir -p $(OUTDIR)

# Compile each src/*.c into build/<os>/<arch>/*.o (mirrors src/ subdirs, e.g.
# src/generated/x.c -> $(OBJDIR)/generated/x.o). The mkdir creates the
# per-target object tree on first build.
$(OBJDIR)/%.o: $(SRCDIR)/%.c $(BUILD_FLAGS_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

ifeq ($(NETPLAY),1)
$(OBJDIR)/ggpo/%.o: $(GGPO_DIR)/lib/ggpo/%.cpp $(BUILD_FLAGS_STAMP)
	@mkdir -p $(dir $@)
	$(CXX) $(GGPO_CXXFLAGS) -c $< -o $@

# Our C++ shim needs GGPO's include path AND our own build defines: compiled
# with GGPO_CXXFLAGS alone, SWOS_SDL_BUILD/SWOS_NETPLAY are undefined and the
# whole file preprocesses away to an empty object that links without error and
# resolves nothing.
$(OBJDIR)/netplay_netem_install.o: $(SRCDIR)/netplay_netem_install.cpp $(BUILD_FLAGS_STAMP)
	@mkdir -p $(dir $@)
	$(CXX) $(GGPO_CXXFLAGS) -DSWOS_SDL_BUILD=1 -DSWOS_NETPLAY=1 -Iinclude -c $< -o $@

$(OBJDIR)/enet/%.o: $(ENET_DIR)/%.c $(BUILD_FLAGS_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(ENET_CFLAGS) -c $< -o $@
endif

# Compile the Windows resource file (icon) on MinGW/Cygwin builds. The output
# is COFF-format and gets linked into the final exe.
$(OBJDIR)/SWOS.res.o: SWOS.rc SWOS.ico
	@mkdir -p $(dir $@)
	$(WINDRES) -O coff $< $@

# Pull in the auto-generated header dependencies (-MMD) so editing a header
# rebuilds just the affected objects — no more blanket `make clean`.
-include $(DEPS)

# clean removes THIS target's objects (build/<os>/<arch>/) + its binary. It also
# sweeps any legacy in-tree *.o from before the build/ move. `distclean` nukes
# every target's objects.
clean:
	rm -rf $(OBJDIR)
	rm -f $(TARGET)
ifdef WASM
# emcc emits four files but only SWOS.html is $(TARGET), so `rm -f $(TARGET)`
# used to leave .js/.wasm/.data behind — including a stale multi-MB .data.
# dist/web holds nothing else, so it goes wholesale.
	rm -rf $(OUTDIR)
endif
	rm -f $(SRCDIR)/*.o $(SRCDIR)/generated/*.o SWOS.res.o

distclean: clean
	rm -rf build

# ---- macOS universal binary (x86_64 + arm64) ---------------------- *
# Builds two slices via recursive make, lipos them into a single
# bin/SWOS.macos that runs on both Apple Silicon and Intel Macs.
#
# Requires SDL2.framework (universal) under one of:
#   /Library/Frameworks/  ~/Library/Frameworks/
#   /opt/homebrew/Frameworks/  /usr/local/Frameworks/
# (the official libsdl.org dmg installs into /Library/Frameworks/).
#
# Minimum deployment target defaults to macOS 12 (Monterey); override
# with `make macos-universal MACOS_MIN=11.0` etc. if needed.
#
# The slices now build into per-arch dirs (build/macos/x86_64 and
# build/macos/arm64) so they no longer share objects; the `make clean` between
# them is just a precaution (each slice's tree is already independent).
.PHONY: macos-universal
macos-universal:
	@test "$$(uname -s)" = Darwin || { \
	    echo "macos-universal: must run on macOS (got $$(uname -s))" >&2; \
	    exit 1; }
	@test -n "$(SDL2_MACOS_FW)" || { \
	    echo "SDL2.framework not found in any standard location." >&2; \
	    echo "Install from https://github.com/libsdl-org/SDL/releases" >&2; \
	    echo "(SDL2-2.x.x.dmg) and drop SDL2.framework into" >&2; \
	    echo "/Library/Frameworks/. See docs/MACOS_BUILD.md." >&2; \
	    exit 1; }
	@echo "==> [macos-universal] Building x86_64 slice"
	$(MAKE) clean
	$(MAKE) MACOS_ARCH=x86_64
	mv $(BINDIR)/SWOS.macos $(BINDIR)/SWOS.macos.x86_64
	@echo "==> [macos-universal] Building arm64 slice"
	$(MAKE) clean
	$(MAKE) MACOS_ARCH=arm64
	mv $(BINDIR)/SWOS.macos $(BINDIR)/SWOS.macos.arm64
	@echo "==> [macos-universal] Combining slices via lipo"
	lipo -create -output $(BINDIR)/SWOS.macos \
	     $(BINDIR)/SWOS.macos.x86_64 $(BINDIR)/SWOS.macos.arm64
	@lipo -info $(BINDIR)/SWOS.macos
	@echo "==> Universal binary: $(BINDIR)/SWOS.macos"

# ---- macOS .app bundle ------------------------------------------- *
# Builds (or reuses) the universal binary, then assembles a complete
# bin/SWOS.app drag-installable bundle that includes:
#   - the universal SWOS binary at Contents/MacOS/SWOS
#   - the game data dirs (data/ grafs/ grafs_aga/ grafs_dos/ grafs_2020/
#     grafs_arcade/ sound/ data_json/) beside the binary, where the
#     SWOS chdir-to-own-dir startup finds them. NO save/ — saves go to a
#     per-user dir (~/Library/Application Support/SWOS/save); see docs/SAVE.md.
#   - SDL2.framework bundled in Contents/Frameworks/ so end users
#     don't need to install SDL2 themselves (the binary's -rpath
#     already points at @executable_path/../Frameworks)
#   - a minimal Info.plist + PkgInfo
#   - an optional SWOS.icns icon if one is present in the checkout
#
# The bundle is unsigned by default — local double-click works, but
# Gatekeeper will warn on first launch when copied to another Mac.
# `codesign --force --deep --sign - bin/SWOS.app` adds an ad-hoc
# signature that lets right-click → Open work without warnings.

# Info.plist content as a multi-line variable (exported so the recipe
# can echo it via $$INFO_PLIST_CONTENT without escaping quotes).
define INFO_PLIST_CONTENT
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>               <string>SWOS</string>
    <key>CFBundleDisplayName</key>        <string>SWOS</string>
    <key>CFBundleIdentifier</key>         <string>com.Baker76.SWOS</string>
    <key>CFBundleVersion</key>            <string>1.0</string>
    <key>CFBundleShortVersionString</key> <string>1.0</string>
    <key>CFBundlePackageType</key>        <string>APPL</string>
    <key>CFBundleSignature</key>          <string>????</string>
    <key>CFBundleExecutable</key>         <string>SWOS</string>
    <key>CFBundleIconFile</key>           <string>SWOS</string>
    <key>LSMinimumSystemVersion</key>     <string>$(MACOS_MIN)</string>
    <key>LSApplicationCategoryType</key>  <string>public.app-category.sports-games</string>
    <key>NSHighResolutionCapable</key>    <true/>
</dict>
</plist>
endef
export INFO_PLIST_CONTENT

macos-app: macos-universal
	@test "$$(uname -s)" = Darwin || { \
	    echo "macos-app: must run on macOS (got $$(uname -s))" >&2; \
	    exit 1; }
	@echo "==> [macos-app] Assembling $(BINDIR)/SWOS.app"
	@APP=$(BINDIR)/SWOS.app; \
	  rm -rf $$APP; \
	  mkdir -p $$APP/Contents/MacOS \
	           $$APP/Contents/Frameworks \
	           $$APP/Contents/Resources; \
	  echo "    binary: Contents/MacOS/SWOS"; \
	  cp $(BINDIR)/SWOS.macos $$APP/Contents/MacOS/SWOS; \
	  chmod +x $$APP/Contents/MacOS/SWOS; \
	  if [ -d "$(SDL2_MACOS_FW)/SDL2.framework" ]; then \
	    echo "    bundling SDL2.framework from $(SDL2_MACOS_FW)"; \
	    cp -R "$(SDL2_MACOS_FW)/SDL2.framework" $$APP/Contents/Frameworks/; \
	  else \
	    echo "    WARNING: SDL2.framework not found — user will need to install it system-wide" >&2; \
	  fi; \
	  echo "    copying $$(scripts/stage_runtime.sh $$APP/Contents/MacOS \
	        data grafs grafs_aga grafs_dos grafs_2020 grafs_arcade grafs_jag sound sound_dos data_json setup)"; \
	  if   [ -f SWOS.icns ];           then cp SWOS.icns           $$APP/Contents/Resources/; \
	  elif [ -f $(BINDIR)/SWOS.icns ]; then cp $(BINDIR)/SWOS.icns $$APP/Contents/Resources/; \
	  else echo "    (no SWOS.icns — Finder will use the default app icon)"; fi; \
	  printf 'APPL????' > $$APP/Contents/PkgInfo; \
	  echo "$$INFO_PLIST_CONTENT" > $$APP/Contents/Info.plist; \
	  xattr -cr $$APP 2>/dev/null || true; \
	  echo ""; \
	  echo "==> bin/SWOS.app ready"; \
	  echo "    archs: $$(lipo -info $$APP/Contents/MacOS/SWOS 2>/dev/null | sed 's/^.*: //')"; \
	  echo "    size:  $$(du -sh $$APP | cut -f1)"; \
	  echo ""; \
	  echo "    Run:        open $$APP"; \
	  echo "    Ad-hoc sign: codesign --force --deep --sign - $$APP"; \
	  echo "    Distribute: hdiutil create -fs HFS+ -srcfolder $$APP -volname SWOS $(BINDIR)/SWOS.dmg"