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
  The player runs the Setup on a PC or Mac and copies its `assets` folder over -- over Wi-Fi (below)
  or by hand; without it the game says how (`Mobile_CheckGameData`), and closes only when told to.
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

## Copying over Wi-Fi

The wizard's phone screen serves the setup's assets folder on the local network while it is open,
and shows a QR code (`include/phone_copy.h` is the whole protocol; the setup's end
`tools/swosdata/app/phone_server.cpp`, the phone's `src/phone_fetch.c`):

- **The QR code is `https://openswos.net/get/#h=<ip>:<port>&k=<key>`** (`PROJECT_SITE`,
  `PhoneServerLink`). Two dead ends before it: a code of the setup's own `http://<ip>:<port>/` page
  fails on **Android 17**, which lets an app reach the local network only with a permission that
  Chrome does not have by default (`ACCESS_LOCAL_NETWORK: ignore` -- Chrome showed
  ERR_TOO_MANY_RETRIES, nothing reached the setup); a code of `openswos://get?...` the Pixel's
  camera shows but does not open. So: on Android an **App Link** -- the manifest's
  `autoVerify` filter for `https://<site>/get`, proved by `packaging/web/site/.well-known/
  assetlinks.json` (the release key's SHA-256: a new key means a new file) -- and the camera opens
  the app directly (`pm get-app-links net.openswos.game` says `verified`). Anywhere else (iOS, an
  app not installed) the site's `/get/` page opens; it reaches nothing on the local network, and
  its button opens the app (`intent://` on Android, `openswos://` elsewhere). Everything after
  `#` stays on the phone: the server never sees the player's address or key. The setup's own
  `/<key>/` page is still served, for a browser that may reach it.
- **The app takes the link** at the start or while it waits on its "the game's data" message:
  Android's `OpenSWOSActivity` keeps the intent's (`takeLink`, over JNI; the activity is
  `singleInstance`), iOS's SDL hands `openURL` over as an `SDL_DROPFILE`. The manifest's VIEW
  filter and the plist's `CFBundleURLTypes` take the scheme from `project_identity.h`.
- **A link while the game plays** cannot be taken in place (its data is loaded): the activity asks
  ("starts again to do it: anything not saved is lost") and restarts through `RestartActivity`, in a
  process of its own (`:restart`), with `&ok=1` on the link so the game does not ask twice. Until
  the game has its data (`gamePlaying`, over JNI) a link is kept for it as it is.
- **Without the code**, COPY FROM MY COMPUTER broadcasts `PHONE_COPY_ASK` on UDP 38630 and the setup
  answers with its port and key. Android only in practice: iOS sends a broadcast only with an
  entitlement Apple grants (`com.apple.developer.networking.multicast`).
- **The copy** goes into `<assets>.part`, and takes the assets folder's place only when every file
  came, at its size. The list's paths are checked (relative, no `..`); the setup serves only files
  of its own list, by their name in it, under a key it makes up each run. The Amiga drawer's
  `amiga/` is left out.
- **A world the phone has** (`data_json/worlds/<n>`) and the setup did not send is kept: a player
  who copied World 2026-27 over by hand does not lose it to a computer that never had it. What a
  copy cut off leaves (`assets.part`, `assets.old`) the app clears at its next start
  (`PhoneFetch_Tidy`) -- the adb shell cannot delete what the app wrote.
- **The messages are short on purpose**: SDL's Android dialog does not scroll, and in landscape a
  long one pushes its buttons off the screen (a Pixel 8 showed no buttons at all).
- **SWOS.ini**: the setup's GAME OPTIONS for the phone (`<user dir>/phone/SWOS.ini`) come too, and
  only their keys (`include/game_options.h`, phone ones, known values) are set in the phone's file,
  which is then read again.
- **The port: 38630**, else the next nine, else any (the code and the answer carry it). Not
  44620-48715: WSL2 in mirrored mode holds those on Windows, and `netsh` does not list them.
- **Testing from a desktop**: `--phone-fetch=<link>` or `--phone-fetch=find` copies into
  `swos_assets_dir()` (set `HOME`/`SWOS_ASSETS` to a scratch folder). From WSL in mirrored mode use
  `h=127.0.0.1`: the LAN address is WSL's own too, and WSL's broadcast never reaches the Windows
  host -- a phone, another device, has neither problem.

## Not done yet

- Importing the game's data in the app (the Setup's library on the phone) instead of copying it.
- The Wi-Fi copy from a running game (OPTIONS), not only at the start. Tried on a Pixel 8
  (2026-11-03): COPY FROM MY COMPUTER found the PC, 451 files, the options taken, a link with data
  there asks first, one while the game plays restarts it and copies, and the QR code through the
  camera opens the app straight (App Link). iOS not yet.
- Haptics, the controls' size and placement as a setting, a controls layout for a second player.
