# Building OpenSWOS from source

For developers. To *play*, download a build instead ([INSTALL.md](../INSTALL.md)). Release
builds and drops are `scripts/build_dist.sh` and the procedure in [TRIAGE.md](TRIAGE.md);
the conventions every change follows are in [`.claude/CLAUDE.md`](../.claude/CLAUDE.md).

## Building

The Makefile auto-detects the host and produces a binary named per the
target architecture. Pass an arch flag for cross builds.

| Target                                 | Command                | Output                |
|----------------------------------------|------------------------|-----------------------|
| Linux x86 (default, 32-bit)            | `make`                 | `bin/SWOS`            |
| Linux x86_64 (64-bit)                  | `make X86_64=1`        | `bin/SWOS.x86_64`     |
| Linux ARM64 (aarch64) — Pi 4/5 etc.    | `make ARM64=1`         | `bin/SWOS.aarch64`    |
| Linux ARM32 (armhf, armv7-a + NEON)    | `make ARM=1`           | `bin/SWOS.armhf`      |
| Windows                                | `make` on MSYS2/Cygwin | `bin/SWOS.exe`        |
| macOS (native, single-arch)            | `make`                 | `bin/SWOS.macos`      |
| macOS universal binary (x86_64 + arm64)| `make macos-universal` | `bin/SWOS.macos`      |
| macOS `.app` bundle (data + SDL2 inside)| `make macos-app`       | `bin/SWOS.app`        |
| WebAssembly (browser)                  | `make WASM=1`          | `dist/web/SWOS.html`  |
| **Amiga m68k (A1200/AGA)**             | `make -f Makefile.amiga` | `bin/SWOS2`         |

Common flags:
- `make clean` first if you've switched between archs in the same checkout.
- `make SWOS_DEBUG=1` adds the diagnostic-logging build (autoskip / sfx).
- `make -j4` (or higher) parallelises the compile.

### Linux x86 (32-bit, default)

Default target. Compiles with `-m32` so the Amiga `long`/pointer struct
sizes match the original 68k layout on a 64-bit host. Needs the 32-bit
SDL2 + multilib gcc:

```
# Debian / Ubuntu
sudo apt install gcc-multilib libsdl2-2.0-0:i386 libsdl2-dev:i386

# Arch
sudo pacman -S lib32-sdl2
```

```
make
./bin/SWOS
```

### Linux x86_64 (64-bit)

Same code path, no `-m32`. The 64-bit struct sizes are picked up via
the bitness-conditional `size_check[]` asserts (same as macOS / aarch64).
Needs the 64-bit SDL2 (`libsdl2-dev` on Debian / Ubuntu, not the
`:i386` cross-arch one).

```
sudo apt install libsdl2-dev
make X86_64=1
./bin/SWOS.x86_64
```

### Linux ARM64 (Raspberry Pi 4 / 5, aarch64 server, etc.)

```
# Native on the Pi (Raspberry Pi OS 64-bit)
sudo apt install build-essential libsdl2-dev
make ARM64=1
./bin/SWOS.aarch64
```

Cross-building from an x86_64 host is a bit more involved — enable the
foreign architecture and install both the cross-compiler and arm64 SDL2:

```
sudo dpkg --add-architecture arm64
sudo apt update
sudo apt install gcc-aarch64-linux-gnu libsdl2-dev:arm64

make ARM64=1
```

The Makefile picks `aarch64-linux-gnu-gcc` and reads SDL2 from the
matching pkgconfig directory. ARM64 is LP64 — no `-m32`.

### Linux ARM32 (armhf, Raspberry Pi 2 / 3, or Pi 4/5 in 32-bit mode)

Native on the Pi:

```
sudo apt install build-essential libsdl2-dev
make ARM=1
./bin/SWOS.armhf
```

Cross-build from an x86_64 Debian/Ubuntu host needs the armhf foreign
arch enabled. Ubuntu's default sources don't carry armhf packages, so
you also have to point apt at `ports.ubuntu.com` (Debian users: skip,
`deb.debian.org` has all archs):

```
sudo dpkg --add-architecture armhf

# Restrict your existing amd64 sources so apt doesn't try ports for them.
sudo sed -i -E 's,^(deb )(http),\1[arch=amd64] \2,' /etc/apt/sources.list

# Add armhf sources. You need ALL FOUR pockets — main universe +
# -updates + -security + -backports — otherwise libsdl2-dev:armhf
# pulls in an old libc6:armhf and refuses to install. Replace 'jammy'
# with your Ubuntu codename (`lsb_release -cs`; jammy = 22.04, noble = 24.04).
CODENAME=$(lsb_release -cs)
sudo tee /etc/apt/sources.list.d/ports-armhf.list <<EOF
deb [arch=armhf] http://ports.ubuntu.com/ubuntu-ports/ $CODENAME main universe
deb [arch=armhf] http://ports.ubuntu.com/ubuntu-ports/ $CODENAME-updates main universe
deb [arch=armhf] http://ports.ubuntu.com/ubuntu-ports/ $CODENAME-security main universe
deb [arch=armhf] http://ports.ubuntu.com/ubuntu-ports/ $CODENAME-backports main universe
EOF

sudo apt update
sudo apt install gcc-arm-linux-gnueabihf libsdl2-dev:armhf

make ARM=1
```

Compiles for `armv7-a` with NEON (covers Pi 2 / 3 / 4 / 5 in 32-bit
mode). armhf is already ILP32 so no `-m32`. Override the toolchain
prefix with `make ARM=1 CC=...` if your distro uses a different one.

### Windows

Build with MSYS2 / MinGW32 (gcc 32-bit), Cygwin 32-bit, or cross-compile
from Linux with `i686-w64-mingw32-gcc`. The Makefile auto-detects the
toolchain and emits `bin/SWOS.exe`.

For end users we ship the SDL2 DLL alongside the executable so no
external SDL2 install is needed — just unzip and run `SWOS.exe`.

### WSL2

The default `make` builds a Linux x86 binary that runs under WSL2 with
WSLg (or an X server). Audio uses queue mode (not callback) because
WSL2 + PulseAudio has uneven scheduling on the SDL audio callback
thread; the queue path is paced from the main game loop and is stable.

### macOS

Builds a Mach-O binary using clang / `cc`. Supports both Intel
(`x86_64`) and Apple Silicon (`arm64`) natively, and can produce a
universal binary that runs on both. Default deployment target is
macOS 11 (Big Sur).

```
# Single-arch native build
brew install sdl2 pkg-config
make
./bin/SWOS.macos

# Universal binary (x86_64 + arm64)
# Requires SDL2.framework from libsdl.org (universal, drop into /Library/Frameworks/)
make macos-universal
```

Full details — SDL2 framework setup, deployment-target overrides,
.app bundle layout, troubleshooting — are in
[`docs/MACOS_BUILD.md`](MACOS_BUILD.md).

### Amiga (m68k)

The Amiga target uses a different Makefile and a different toolchain, but the
same `src/` and `include/`:

```
make -f Makefile.amiga -j$(nproc)
```

The m68k cross-toolchain (`m68k-amiga-elf-gcc`, `vasmm68k_mot`, `elf2hunk`)
ships inside the **Amiga C/C++ Compile, Debug & Profile** VS Code extension
rather than being installed system-wide, so put it on `PATH` first.

> ⚠️ **Use the [`benbaker76/vscode-amiga-debug`](https://github.com/benbaker76/vscode-amiga-debug)
> fork, not the marketplace build** of Bartman^Abyss's extension. The fork's
> FS-UAE carries the gdb **write**-packet support (`M`/`P`/`G`) that lets the
> AmigaScope harness *drive* the Amiga — inject input, set variables, call
> functions — rather than only read it, plus a log-spam fix. Both install to the
> same `bartmanabyss.amiga-debug-<ver>` directory and report the same publisher
> and upstream repo in `package.json`, so **you cannot tell them apart after the
> fact**. Symptom of being on the stock build: gdb reads memory fine, but writes
> and input injection silently do nothing.

Linux/WSL and macOS builds of the toolchain are both bundled. When building from
WSL, note the extension lives in the **Windows-side** `.vscode` — there is no
extension directory in the WSL home, and `~/.vscode` fails as `Error 127`
(which an already-up-to-date tree hides behind `Nothing to be done for 'all'`):

```
EXT=/mnt/c/Users/<your-user>/.vscode/extensions/bartmanabyss.amiga-debug-1.8.2/bin/linux
PATH="$EXT/opt/bin:$EXT:$PATH" make -f Makefile.amiga -j$(nproc)
```

**Building the Amiga target from native Windows** (no WSL): use the `gnumake.exe`
bundled with the toolchain and pass `WINDOWS=1`, from PowerShell/cmd — *not* from a
POSIX shell:

```powershell
$EXT = "<toolchain>\bin\win32"      # e.g. a checkout of the fork, or the extension dir
$env:PATH = "$EXT\opt\bin;$EXT;" + $env:PATH
& "$EXT\gnumake.exe" -f Makefile.amiga WINDOWS=1 -j4
```

`WINDOWS=1` selects the makefile's cmd-shell branches (`where`, `if not exist obj mkdir`).
Building from MSYS2 or Cygwin instead fails in a confusing way: the recipes pass
`$(CURDIR)/$<`, which becomes `/c/...` or `/cygdrive/c/...`, and the native Windows
compiler mis-parses that as a switch — the error looks like
`'amera.o' is not recognized as an internal or external command` (the leading `/c`
has been eaten). Only the toolchain binaries are needed for a build; installing the
fork as a VS Code extension is required only for the in-editor tasks and FS-UAE.

From VS Code, use the **Build (Amiga)** task, then one of the *FS-UAE* /
*WinUAE* launch configurations.

Output is `bin/SWOS2` (an Amiga hunk executable) plus `bin/SWOS2.elf` and
`bin/SWOS2.map`. It links into `bin/` deliberately: the debugger mounts the
binary's directory as **DH1:** and `cd`s into it, so the game finds `data/`,
`grafs/`, `grafs_aga/`, `sound/` and `save/` there — the very same files the
SDL build reads. There is no separate Amiga copy of the game data.

Useful knobs:
- `make -f Makefile.amiga DBG=1` — `-O0` for reliable gdb line breakpoints
  (same struct/ABI layout, so save-format tests stay valid).
- `make -f Makefile.amiga EXTRA_CFLAGS=-DPITCH_SCROLL_TEST` — boots the
  standalone AGA pitch-scroll rig instead of the menu (see `docs/AGA_TEST.md`).
- Always check `bin/SWOS2.map` after a build that moves globals around —
  CHIP RAM placement is load-bearing and silently correct-looking on SDL.

**Adding a source file:** shared code just goes in `src/` and both targets
compile it. A file that needs SDL2 or hosted libc must also be listed in
`SDL_ONLY_SRCS` in `Makefile.amiga`, or the m68k build will try to compile it.

### Nintendo Switch

**For registered Nintendo developers only.** The NintendoSDK and the Switch
fork of SDL are both under NDA; neither is in this repository and neither may
be committed to it. Nothing here reproduces SDK source.

```bat
scripts\build_switch.bat            :: -> bin\SWOS.nss
scripts\build_switch.bat package    :: -> dist\SWOS.nsp, installable on a devkit
```

```bash
scripts/build_switch.sh [package|clean]   # the same, driven from WSL
```

The compile must run on the Windows side — NintendoClang is a Windows
executable — so the shell script wraps the batch file through Windows interop,
the same way the Cygwin target is reached. Point it at the two trees:

```
NINTENDO_SDK_ROOT   the NintendoSDK        (built against 22.2.10)
SDL_SWITCH_ROOT     libsdl-org/SDL-switch  (licensee-only)
```

The Switch is a flavour of the SDL build, so the same `src/` compiles — SWOS
renders 8bpp chunky into its own frontbuffer and only meets SDL at the texture
upload. What differs is filesystem and logging: content is a read-only `rom:/`
mount, saves are a per-account `save:/` mount that needs an explicit commit,
and `printf` reaches nobody so output goes to the target log. All of it, plus
the packaging and the things that cost an afternoon each, is in
**[docs/SWITCH.md](SWITCH.md)**.

## The data setup from the command line

The game needs the original game's data, which OpenSWOS Setup builds from the player's
own copy ([INSTALL.md](../INSTALL.md)). A developer tree can build it with the setup's
command-line half, `swosdata`:

```sh
# The setup's command-line half, built from tools/swosdata:
make -C tools/swosdata
tools/swosdata/build/swosdata --scan ~/Games/SWOS --scan ~/Amiga/ADF \
                              [--swos2020 "$HOME/Games/SWOS 2020"] \
                              --res bin --out ~/.swos/assets
```

Every file it writes is byte-for-byte the one the original game (or SWOS 2020) shipped (or,
for the converted AGA and Jaguar graphics, the one this project's own
converters always made). `tools/swosdata` is built on
[swosgfx](https://github.com/benbaker76/swosgfx) (the converters; a submodule at
`third_party/swosgfx`, so `git submodule update --init` first) plus an ADF reader.

## Tools

- **[swosgfx](https://github.com/benbaker76/swosgfx)** — converts SWOS graphics
  (Amiga, PC and Jaguar) to and from editable formats: pitches to BMP or Tiled maps
  and back, raw bitplane screens to BMP and back, sprites and palettes. A
  command-line tool for Windows, macOS and Linux and a C library; OpenSWOS's data
  setup is built on the library (`third_party/swosgfx`). It is the C/C++ rewrite
  of the C# [SwosGfx](https://github.com/benbaker76/SwosGfx-CSharp), which
  OpenSWOS's older asset scripts were written against, e.g.
  `scripts/make_credits_aga.py` (the AGA credits backdrop) and
  `scripts/make_jag_graphics.py` (the Jaguar sprites).
