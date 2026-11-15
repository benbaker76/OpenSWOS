# Building on macOS

The SDL port builds on macOS as a native Mach-O binary, either:

- **Single-arch** for your current Mac (Apple Silicon → `arm64`, Intel → `x86_64`), or
- **Universal binary** (`x86_64 + arm64`) that runs on both Apple Silicon and Intel.

Both paths target a **macOS 11 (Big Sur)** minimum by default, so a
universal build produced on a 2025-era Mac will still run on a 2021 M1
or a late-Intel iMac on Monterey.

> **Release builds are not made by hand.** From the Linux/WSL dev machine,
> `scripts/build_macos.sh` (`--release`, `--no-pull`) drives the Mac mini over
> ssh: it `git pull --ff-only`s and prints the commit it built, runs
> `make macos-app`, and brings back `dist/SWOS-macOS-app.zip` (the `.app`,
> packed with `ditto` so `SDL2.framework`'s symlinks survive) and
> `dist/SWOS-macOS.zip` (the universal binary alone). Overrides: `MAC_HOST`,
> `MAC_PATH`, `MAC_ZIP`. See `.claude/CLAUDE.md` "macOS". This doc is for
> building on the Mac itself.

---

## Prerequisites

### 1. Xcode Command Line Tools

```bash
xcode-select --install
```

(Skip if you have full Xcode installed.) This gives you `clang`, `make`,
`lipo`, and the macOS SDK.

### 2. SDL2

You have two choices. **The universal-binary target requires option A.**

#### Option A — SDL2.framework (universal, recommended)

The official build from libsdl.org ships as a universal binary that
links cleanly against both `x86_64` and `arm64` slices.

1. Download the latest **`SDL2-2.x.x.dmg`** from
   <https://github.com/libsdl-org/SDL/releases> (look in the
   "Assets" section of the latest 2.x release).
2. Open the dmg and drag `SDL2.framework` into one of:
   - `/Library/Frameworks/` (system-wide, requires admin password — preferred)
   - `~/Library/Frameworks/` (user-only)
3. Verify:
   ```bash
   lipo -info /Library/Frameworks/SDL2.framework/SDL2
   # → Architectures in the fat file: ... x86_64 arm64
   ```

The Makefile auto-detects `SDL2.framework` in these standard locations
and uses it in preference to Homebrew.

#### Option B — Homebrew SDL2 (single-arch only)

Faster to set up if you only need a native build for your Mac:

```bash
brew install sdl2 pkg-config
```

This installs a single-arch SDL2 matching the host Mac. The plain
`make` target will pick it up via `pkg-config`. `make macos-universal`
will fail because Homebrew's SDL2 isn't a universal binary.

---

## Building

Clone the repo somewhere convenient:

```bash
git clone <repo-url> OpenSWOS
cd OpenSWOS
```

### Native single-arch (for the Mac you're on)

```bash
make
```

Produces `bin/SWOS.macos` for the host architecture. Run it:

```bash
./bin/SWOS.macos
```

### Universal binary (x86_64 + arm64)

```bash
make macos-universal
```

This:
1. `make clean`
2. Builds the `x86_64` slice → `bin/SWOS.macos.x86_64`
3. `make clean`
4. Builds the `arm64` slice → `bin/SWOS.macos.arm64`
5. `lipo -create` → `bin/SWOS.macos` (universal)

Verify:
```bash
lipo -info bin/SWOS.macos
# → Architectures in the fat file: bin/SWOS.macos are: x86_64 arm64
```

Same binary runs on both architectures via the kernel's per-arch dispatch.

### Cross-build a single non-host slice

```bash
make MACOS_ARCH=x86_64        # build x86_64 slice on an M-series Mac
make MACOS_ARCH=arm64         # build arm64 slice on an Intel Mac
```

Requires SDL2.framework (universal). The output is still
`bin/SWOS.macos` — rename or copy if you want to keep both arches
around.

---

## Tweaking the deployment target

The default minimum is **macOS 11 (Big Sur)** because that's the
oldest macOS we currently care about. Override per-invocation:

```bash
make macos-universal MACOS_MIN=13.0      # Ventura+
make macos-universal MACOS_MIN=10.13      # High Sierra+ (latest SDL2 still supports it)
```

Going below ~10.13 is not recommended — newer SDL2 builds may not
load on those versions.

---

## Running

### From command line

```bash
./bin/SWOS.macos
```

The binary expects to find game data (`data/`, `grafs/`, `grafs_aga/`,
`sound/`, `data_json/`, and the optional `grafs_dos/`, `grafs_2020/`,
`grafs_arcade/`, `grafs_jag/` tiers) **relative to itself** in the same `bin/`
directory.

### As an .app bundle (optional, for sharing)

```
make macos-app
```

Builds the universal binary first, then assembles `bin/SWOS.app`:

```
SWOS.app/
└── Contents/
    ├── Info.plist                   # bundle metadata (CFBundleName, version, min-OS, …)
    ├── PkgInfo                      # "APPL????" legacy marker
    ├── MacOS/
    │   └── SWOS                     # the universal binary -- code ONLY here (signing)
    ├── Frameworks/
    │   └── SDL2.framework           # bundled (so user doesn't need to install)
    ├── Helpers/                     # (scripts/build_macos.sh adds it: not make macos-app)
    │   └── OpenSWOS Setup.app       # the data setup, started by the game's no-data box
    └── Resources/
        ├── SWOS.icns                # from the repo root (committed; generated from SWOS.svg by scripts/generate_icons.py)
        ├── data/  grafs/  grafs_aga/  sound/  sound_dos/  data_json/  setup/  mods/ ...
        │                            # the project's own files (scripts/stage_runtime.sh);
        │                            # (no save/ — saves go to a per-user dir; see docs/SAVE.md)
        └── portmaster/swos.zip      # (build_macos.sh: the setup's Handheld screen puts it on a card)
```

The game changes to `Contents/MacOS` at start (main.c `chdir_to_binary`) and on to `../Resources`
when the data is there. Until 2026-11-14 the data sat beside the binary in `Contents/MacOS`. A
signed bundle may hold only code there: codesign stores the signatures of other files as extended
attributes, which a zip or DMG loses, and notarization refuses the bundle. Older bundles still
run, because the game only moves when `data/` is not beside it.

The binary's `-rpath @executable_path/../Frameworks` means a bundled
`SDL2.framework` at `Contents/Frameworks/` resolves automatically — end
users don't need to install SDL2 themselves.

Run it via `open bin/SWOS.app` (or just double-click in Finder).

`make macos-app` leaves the bundle unsigned. That is fine on the Mac that built it, but on another
Mac Gatekeeper refuses it. Releases come from `scripts/build_macos.sh`, below.

## The release: one app in a DMG, signed and notarized (2026-11-14)

`scripts/build_macos.sh` (run from the Linux/WSL checkout; it drives the Mac over ssh) makes
**`dist/OpenSWOS-macOS.dmg`**, which holds `SWOS.app` and a link to Applications. The player drags
SWOS onto Applications and starts it. OpenSWOS Setup is inside the app, so the first start's
no-data box (**Make Game Data**) runs it with `--headless`: it finds the player's copy of the game
and makes the data with no window, and opens its window only when it needs the player. This is
the Mac's version of the Windows installer's last page. `SWOS-macOS-app.zip` is the same app,
zipped, for itch.io's channel.

The same app is in both. Its parts are signed from the inside out (dylibs, frameworks, the setup's
app, then the game's) with the **hardened runtime** and a secure timestamp. The DMG is signed,
**notarized** (`xcrun notarytool submit --wait`) and **stapled**, as are the app and the setup
inside it, so a player's Mac opens them with no warning, even offline. `OpenSWOS-Setup-macOS.zip`
(for `OpenSWOS-Setup.zip`) carries that same signed setup app, plus `swosdata`, which is signed
and notarized with the zip. A bare program can't be stapled, so Gatekeeper checks it online the
first time it runs.

### Signing and notarizing: the Mac's keychain, once

An ssh session cannot use the login keychain while it's locked: codesign fails with
`errSecInternalComponent`. So the build Mac has a keychain of its own for this, made once by its
owner **in a terminal on the Mac**:

1. In Keychain Access, under My Certificates, select **Developer ID Application: Benjamin Baker
   (9AM8849YMD)** and choose File → Export Items… → `.p12`, with a password of your choosing.
2. Make an **app-specific password** for your Apple ID: account.apple.com → Sign-In and Security →
   App-Specific Passwords.
3. `scripts/macos_signing_setup.sh ~/Desktop/DeveloperID.p12 --apple-id you@example.com`
   - It makes `~/Library/Keychains/openswos-signing.keychain-db`. Its password is random and is
     kept in `~/.openswos-signing/keychain.pass` (mode 600), which the build uses to unlock it.
   - It imports the certificate: you type the `.p12`'s password. It also records the certificate's
     name in `~/.openswos-signing/identity` and makes a test signature.
   - It stores the notary credentials as the profile `openswos-notary` in that keychain:
     notarytool asks for the app-specific password.
   - Then delete the `.p12`.

Nothing typed there is kept anywhere but the keychain. Without that keychain, `build_macos.sh`
still makes the DMG, ad-hoc signed and not notarized, and says so ("signed as: -", "notarized:
no"). Players then open it with right-click → Open, or on Sequoia with System Settings → Privacy
& Security → Open Anyway.

A rejected notarization prints notarytool's log: an unsigned piece, a missing timestamp, or code
in `Resources` that is not signed. The summary also prints `spctl`'s verdict on the DMG and the
app ("accepted, source=Notarized Developer ID").

---

## Troubleshooting

- **`SDL2.framework not found in any standard location`** — install
  per *Option A* above. The Makefile checks `/Library/Frameworks`,
  `~/Library/Frameworks`, `/opt/homebrew/Frameworks`, and
  `/usr/local/Frameworks` (in that order).
- **`ld: warning: ignoring file ... building for macOS-arm64 but ... is x86_64`** —
  you've got Homebrew SDL2 on the search path AND tried a non-host
  arch. Switch to SDL2.framework (it's universal and beats this) or
  build only for the host arch.
- **`/usr/bin/make: Bad CPU type in executable`** when running an
  arm64-only binary on Intel (or vice versa) — the universal target
  fixes this. Use `make macos-universal`.
- **`ld: library 'SDL2' not found`** even though Homebrew installed
  it — `make clean` first; stale `.o` files from a prior build may
  reference an old SDL2 search path.
- **The app crashes immediately on Monterey but runs on Tahoe** — you
  probably built without the deployment-target flag (the default is
  `11.0`, so if you've overridden `MACOS_MIN` to a higher value the
  binary won't load on older systems).

---

## Quick sanity check on a built binary

```bash
file bin/SWOS.macos
# Universal: "Mach-O universal binary with 2 architectures: ..."
# Single-arch arm64: "Mach-O 64-bit executable arm64"
# Single-arch x86_64: "Mach-O 64-bit executable x86_64"

otool -L bin/SWOS.macos | head -5
# Should show @rpath/SDL2.framework/Versions/A/SDL2 (or similar)
# and the system libs (libSystem, libc++, etc.)

vtool -show-build bin/SWOS.macos | grep minos
# minos 11.0  ← the deployment target you built with
```
