# MOBILE.md — OpenSWOS on Android and iOS

**Status (2026-11-02): both build and run, on real hardware.** The signed release APK plays a match
on a Pixel 8 (touch stick and FIRE, a Bluetooth controller); the iOS build runs on an iPod touch and
the iPhone 16 simulator. OPTIONS -> WORLD was tested on the Pixel (World 2026-27 copied in with the
assets folder; `[world] 2627` after a restart). Touches held when the app loses focus or goes to the
background are released (`TouchControls_ReleaseAll`), so a FIRE held over a message box does not stay down. Both are flavours of the
SDL build, the same sources as every other target (`SWOS_MOBILE_BUILD`, with `SWOS_ANDROID_BUILD` /
`SWOS_IOS_BUILD`: `include/swos_platform.h`).

## Building

| | Build | Output | Where |
|---|---|---|---|
| Android | `scripts/build_android.sh` | `dist/OpenSWOS-Android.apk`, signed with OpenSWOS's key | here (WSL) |
| Android, debug | `scripts/build_android.sh debug` / `install` | `dist/OpenSWOS-Android-debug.apk` | here |
| iOS | `scripts/build_ios.sh` | `dist/OpenSWOS-iOS.ipa`, unsigned (sideloading) | the Mac mini, over ssh |
| iOS simulator | `scripts/build_ios.sh sim` | `build-ios-sim/OpenSWOS.app` on the Mac | the Mac mini |

`--release` on either is a public build (the testers' rule compiled out), as `build_dist.sh --release`.

- **One CMake build for both**: `packaging/mobile/CMakeLists.txt`. It mirrors the Makefile's SDL
  build (its sources, its `-D`s, GGPO-X and ENET for online play): keep the two in step. SDL2 is
  built from its release (2.30.12, pinned by hash), as the macOS app bundles.
- **Android**: `packaging/android`, a Gradle project (AGP 9.4, Gradle 9.7.1 through `gradlew`),
  SDL2's Java activity (`org/libsdl/app`, from the same release -- they must match) and
  `net.openswos.game.OpenSWOSActivity`. arm64-v8a and x86_64 (the emulator). Needs a JDK 17
  (`~/opt/jdk-17*`) and the SDK at `~/Android/Sdk` with platform 36, NDK 30.0.16248370, CMake 4.1.2.
- **iOS**: arm64, iOS 15 and later; the app bundle is the game's working folder. The packed structs
  hold pointers at unaligned offsets, which iOS 15's chained fixups cannot express: the link uses
  `-Wl,-no_fixup_chains` (the classic linker the macOS build uses is gone from current Xcode).
- **The identity** -- the Android application id, the iOS bundle id -- is `PROJECT_APP_ID` in
  `include/project_identity.h` (`net.openswos.game`), read by both builds: a fork's apps install
  beside OpenSWOS's.
- **Icons**: `scripts/generate_icons.py` writes Android's mipmaps and iOS's icons (opaque: the art on
  black) from `SWOS.svg`, with the rest.

### The Android key

Every release APK is signed with **OpenSWOS's own key**, `~/.openswos-keys/openswos-release.jks`
(`CN=OpenSWOS, O=OpenSWOS, C=US`, certificate SHA-256 `59:BE:DC:59:...`), its passwords in
`android.env` beside it -- outside the repository, and kept for good: Android installs an update
only over an APK signed with the same key, so **losing it means every player uninstalling**. Back
that folder up. `OPENSWOS_KEYS=` points the script at another copy.

### iOS: sideloading

An iPhone installs apps only from the App Store, TestFlight, or a sideloading tool, so the drop's
`.ipa` is unsigned: AltStore or Sideloadly sign it with the player's own Apple ID (a free account
re-signs every 7 days). `build_ios.sh sim` and the Mac's development identity are for testing.

## What differs on a phone

- **Folders** (`src/swos_io.c`, `src/mobile.c`). The player's folder -- `SWOS.ini`, `SWOS.log`,
  `save/`, and the `assets/` folder -- is where the player can reach it: Android's
  `Android/data/net.openswos.game/files` (over USB from a PC), iOS's Documents (the Files app, On My
  iPhone > OpenSWOS; Finder from a Mac). The project's own data (the tracked files of `bin/` every drop
  ships: `scripts/stage_runtime.sh`) is the working folder: on iOS the app bundle; on Android it is in
  the APK's assets with a manifest (a stamp, then "size path" per file), copied out to internal
  storage on the first start and again when the stamp changes.
- **The game's data**: OpenSWOS Setup cannot run on a phone, and no drop carries the original data.
  The player runs the Setup on a PC or Mac and copies its `assets` folder over; without it the game
  says where it goes (`Mobile_CheckGameData`) and closes.
- **Touch controls** (`src/touch_controls.c`): the web page's (`wasm/shell.html`) done natively --
  a stick on the left, FIRE on the right, PAUSE and ESC above it, drawn over the picture in the
  present path, player 1's through `InputSDL_TouchControls` / `InputSDL_TouchKey`. They go when a
  controller's button is pressed and come back with the next touch (not on a controller merely
  being there: the iOS simulator reports one nobody holds).
- **Text**: a text field brings up the system keyboard (`input_text.c`); SDL turns its characters
  into the key events the game reads. Android's back button is ESC.
- **The screen**: always the whole screen, landscape, at the screen's own pixels; the widescreen
  picture takes the phone's shape (an iPhone 16: 590x272).
- **Hints** (`Mobile_SetHints`): the accelerometer is not a joystick (it would take player 1's place).
- Online play is in: the same GGPO-X and matchmaking as the desktops (`NETPLAY_COMPAT` alike).

## Not done yet

- Importing the game's data in the app (the Setup's library on the phone) instead of copying it.
- Haptics, the controls' size and placement as a setting, a controls layout for a second player.
