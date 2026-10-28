# OpenSWOS Android port: process, build and testing

**Status: current test build `0.7-test` (version code 7); handheld confirmation through `0.4-test`.**
This document records the port developed on 2026-10-22, its build and testing
process, and the fixes made during handheld testing. The user confirmed that
the game, icon, keyboard entry and final keyboard display fix work on their
handheld.

The initial Android target is ARM64, Android 8.0 or later, landscape, with a
physical controller (for example a Retroid Pocket 5). It uses the existing SDL2
controller mappings; the touch KEYS panel only supplies keys the pad lacks. This build is offline;
GGPO/netplay is not compiled in.

## Porting approach and implementation

Initial inspection found an existing portable C/SDL2 backend and ARM64 support.
The Android port therefore reuses the game engine, renderer, audio and controller
mapping. The new work is the Android wrapper/build, storage paths, lifecycle
handling, launcher resources and text input.

The target is a handheld with attached controls; no touch gamepad was needed.
The touchscreen is used for Android's keyboard. The user's prepared assets were
bundled to get a working test APK before implementing an Android data importer.

### Application and native build

The `android/` Gradle project builds SDL2 and the game as shared libraries:
`libSDL2.so` and `libmain.so`. SDL's Java Activity loads the exported `SDL_main`
entry point. SDL Java and native sources come from the same pinned download.

`android/app/jni/Android.mk` compiles `src/*.c` and `src/generated/*.c`,
force-includes `include/swos_platform.h`, and preserves the desktop build's
data-module defines, `-fno-strict-aliasing` and `-fno-builtin`. JSON saves and
tester rules are enabled; the C++ GGPO path is not built.

| Setting | Value used for this port |
|---|---|
| Gradle | 8.9 |
| Android Gradle plugin | 8.7.3 |
| NDK | `28.2.13676358` |
| SDL | 2.32.10 |
| ABI | `arm64-v8a` |
| Minimum SDK | 26 / Android 8.0 |
| Compile / target SDK | 35 |
| Java language level | 17; the local build ran on JDK 21 |
| Application ID | `net.openswos.test` |

These are the versions used, rather than a claim about the latest available
tools. The manifest declares landscape orientation, optional touchscreen/gamepad
features, keyboard resizing, and the launcher icon.

### Build failures resolved

| Failure | Cause | Fix |
|---|---|---|
| Windows linker `CreateProcess` / error 87 | The object-file list exceeded Windows command-length limits | Enable `LOCAL_SHORT_COMMANDS` and `APP_SHORT_COMMANDS` to use response files |
| Undefined `__data_start` | Android defines `__linux__`, so the desktop rollback harness tried to use executable ELF symbols in a shared-library build | Exclude the desktop fork/ELF harness on `__ANDROID__` in `src/rollback_rig.c` |

`APP_SUPPORT_FLEXIBLE_PAGE_SIZES` is enabled. Native LOAD segments and APK library
alignment were checked for 16 KB compatibility.

### Startup and storage

`OpenSWOSActivity` extracts the bundled runtime before starting SDL. ZIP paths
outside the runtime directory are rejected. A marker based on the installed
APK's modification time avoids extraction on every launch and triggers it after
updates. Extraction failures use SDL's startup-error screen and prevent the
native game from starting against incomplete data.

`src/main.c` changes the working directory to the extracted runtime instead of
the binary directory. `src/swos_io.c` uses
`SDL_AndroidGetInternalStoragePath()` for settings and saves. This keeps the
existing ordinary file I/O working without changing every asset loader.

```text
App-private files/
  runtime/       Extracted game data
  SWOS.ini       Settings
  SWOS.log       Diagnostics
  save/          Saves and replays
```

### Controller and lifecycle changes

The first emulator run exposed SDL assigning `Android Accelerometer`, a raw
joystick, to player one. `src/main.c` now disables
`SDL_HINT_ACCELEROMETER_AS_JOYSTICK` before SDL initialization and explicitly
requests `LandscapeLeft LandscapeRight`.

An SDL event watcher sets an atomic background flag for
`SDL_APP_WILLENTERBACKGROUND` and `SDL_APP_DIDENTERFOREGROUND`. The interrupt
clock driver does not advance while backgrounded, and presentation waits for
the foreground. SDL manages the Android surface/audio lifecycle. Timer-worker
rendering is disabled on Android so rendering stays on SDL's main/game thread.
Delivered Android Back key events are translated to Escape; the IME can consume
Back first to dismiss the keyboard.

## Build on Windows

Requires Python 3, Git, JDK 17 or 21, and an Android SDK containing platform 35,
build-tools 34.0.0 (the Gradle plugin default), platform-tools, and NDK
28.2.13676358. Install those using Android Studio's SDK Manager or sdkmanager.
The repository's Gradle wrapper downloads Gradle 8.9. SDL2 2.32.10 is downloaded
into the ignored `build/android-deps/` folder by the build script.

For an existing command-line SDK installation, install the pinned packages with
its SDK Manager (adjust the executable path to the installed tools):

```powershell
$sdk = 'C:/path/to/android-sdk'
& "$sdk/cmdline-tools/latest/bin/sdkmanager.bat" "--sdk_root=$sdk" `
    'platform-tools' 'platforms;android-35' `
    'build-tools;34.0.0' 'build-tools;35.0.0' 'ndk;28.2.13676358'
```

```powershell
./scripts/build_android.ps1 -Sdk C:/path/to/android-sdk -Assets C:/path/to/prepared/assets
```

The SDK argument defaults to `ANDROID_HOME`, then `build/android-sdk` when that
variable is unset. The assets argument defaults to
`$env:USERPROFILE/Documents/SWOS/assets`. With that local setup, both arguments
can be omitted.

The build script checks the NDK, obtains SDL if needed, runs
`scripts/stage_android.py`, writes ignored `android/local.properties`, runs
`assembleDebug`, and copies the resulting APK into `dist/android/`.

For an incremental build after dependencies and runtime data are staged:

```powershell
./android/gradlew.bat -p android assembleDebug
Copy-Item android/app/build/outputs/apk/debug/app-debug.apk `
    dist/android/OpenSWOS-test-arm64.apk
```

Direct Gradle builds do not regenerate the runtime bundle or
`include/swos_build.h`; use the full build script when changing runtime data.

Output: `dist/android/OpenSWOS-test-arm64.apk`, signed with the local Android debug
key. It includes the specified user's prepared data for private testing. The data
bundle is generated under ignored `build/`, never added to the source tree.
This is separate from the public release staging process.

`stage_android.py` takes only game data/audio/graphics directories from the supplied
assets folder, then overlays the current project's tracked runtime files. It does
not copy desktop settings, logs, saves, executables, or account information.
It validates `data/TEAM.000`, collects the data/graphics/audio trees, and overlays
current tracked runtime files from `bin/`. The tested runtime ZIP was about
9.3 MiB and the APK about 14 MB; sizes vary with the prepared assets.

## Install and test

Copy the APK to the handheld and open it to install, or enable USB debugging and use:

```powershell
adb install -r dist/android/OpenSWOS-test-arm64.apk
adb shell am start -n net.openswos.test/net.openswos.OpenSWOSActivity
```

On first launch and APK updates, the Activity unpacks the bundled runtime to
internal app storage before starting SDL. `SWOS.ini`, `SWOS.log`, and `save/` are
stored separately from that runtime and persist across APK updates. Clearing app
storage or uninstalling removes them. The test application ID is
`net.openswos.test`.

The built-in pad should navigate menus and steer players (controls below). Android
Back key events are mapped to Escape. Check an offline friendly, sound, save/load, sleep/wake,
Home/resume, and controller mapping on the actual handheld. The game clock is
gated during background lifecycle events; SDL manages its Android surface/audio
lifecycle. Timer-worker rendering is disabled on Android.

### Controls (from `0.7-test`)

The target is a handheld with built-in controls (Retroid), so the physical pad is
the control scheme and the touchscreen is only a fallback for keys the pad lacks.

| Button | Menus | Match / training |
|---|---|---|
| A / B | Select | Fire (B also skips) |
| Start, tap | — | Pause / resume |
| **Start, hold 0.7 s** | — | **Escape: leave the match or training** (REPLAY / EXIT screen) |
| R3 (right-stick click), hold 0.7 s | — | Escape, as above; a tap does nothing |
| Select | — | Statistics |
| Y / X | — | Instant replay / replay after the match |
| L1 / R1 | unused until a CONTROLS menu exists | unused |
| Android Back | Escape | Escape |

The SWOS menus do not act on Escape: leave a menu with its EXIT button.

The hold is in the shared `k_padKeys` table (`src/input_sdl.c`), so desktop and
Switch pads behave the same. A button with a hold sends its tap key on release,
and only if the hold did not fire, so pause arrives one tap's length later than
before. R3 became a hold because a handheld's grip can click the stick, and
Escape ends the match. `--input-selftest` checks tap, hold and release timing.

**KEYS** (top right, `GameKeys.java`) opens a touch panel with Esc, F1-F10,
digits, letters, Tab, Space and cursor keys, sent through SDL as keyboard events.
It hides itself, and closes the panel, as soon as a gamepad button or stick is
used, and comes back when the screen is touched. Android's own keyboard still
opens automatically for name fields (below).

To retrieve diagnostics from this debuggable APK:

```powershell
adb shell run-as net.openswos.test cat files/SWOS.log
adb logcat -s SDL OpenSWOS AndroidRuntime
```

APK updates must keep the application ID and signing key to preserve the normal
update path. Retain the local debug key when building on another machine;
otherwise Android may reject the update because its signature differs.

Build and packaging checks alone do not establish runtime compatibility. This
port was subsequently exercised in the emulator and confirmed working by the
user on their handheld; no formal device matrix or performance benchmark was
recorded.

## Local emulator validation

The existing `itb` AVD uses Android 15/API 35 Google APIs x86_64, with
`libndk_translation` and ARM64 support. Its SDK/emulator is under
`~/Downloads/ITB/sdk`. The actual ARM64 APK was installed and run, rather than
substituting an x86 build.

The emulator was initially missed because it was not running: an empty
`adb devices` list did not mean no emulator was installed. The existing AVD's
`emu-launch-params.txt` and `hardware-qemu.ini` revealed its SDK location.

To launch that existing local AVD from the repository root:

```powershell
$emu = "$env:USERPROFILE/Downloads/ITB/sdk/emulator/emulator.exe"
New-Item -ItemType Directory -Force build/android-tools | Out-Null
Start-Process -FilePath $emu `
    -ArgumentList '-avd itb -no-snapshot -no-boot-anim -gpu swiftshader_indirect -no-window' `
    -WindowStyle Hidden `
    -RedirectStandardOutput "$PWD/build/android-tools/emulator.log" `
    -RedirectStandardError "$PWD/build/android-tools/emulator-error.log"
```

Wait for `adb shell getprop sys.boot_completed` to return `1` before installing.
Installing before the package service was ready produced
`Can't find service: package` during bring-up.

Useful inspection commands, with the SDK's `platform-tools` directory on PATH:

```powershell
adb devices
adb shell getprop ro.product.cpu.abilist
adb shell getprop ro.dalvik.vm.native.bridge
adb shell pidof net.openswos.test
adb logcat -b crash -d
adb shell screencap -p /sdcard/openswos.png
adb pull /sdcard/openswos.png build/android-tools/openswos.png
```

Controller testing used a temporary native x86_64 Linux `uinput` helper running
with root access in the emulator. It advertised an Xbox-compatible USB device
with buttons, sticks and D-pad axes. Android and SDL recognised an Xbox 360
controller, and event commands were sent through
`/data/local/tmp/swos-pad.fifo`. This tested controller discovery and the SDL pad
path, beyond merely injecting keyboard events.

The helper source/binary and driving scripts are local artifacts under ignored
`build/android-tools/`; they are not portable tools shipped with the project or
required build inputs.

Verified in the emulator:

- Installation, first-run extraction, intro, main menu, team selection and a live
  friendly (Fortuna Dusseldorf versus MSV Duisburg), with players/pitch rendering.
- A Linux uinput virtual Xbox 360 pad hot-plugged as player one; A started the
  match, direction/fire events were sent, and right-stick click ended it.
- Home/resume and screen sleep/wake retained the same running process and match.
- Android's audio mixer had an active 44.1 kHz stereo game track and reported no
  mixer underruns in the captured diagnostics. Audible quality is not established.
- Ending the match wrote `files/save/LATEST.SRP`. Reinstalling the APK with
  `adb install -r` preserved that replay with the identical SHA-256 hash.
- No entries in Android's crash log during the test. APK signature, 16 KB
  ZIP/native library alignment, exported `SDL_main`, and APK/runtime ZIP integrity
  checks passed. The runtime contains 540 files.

Testing exposed SDL treating Android's accelerometer as a raw player-one joystick.
The Android startup now disables that sensor joystick before SDL initialization
and explicitly requests landscape orientations.

## Typing names

Select a name field with the gamepad and press A/fire. Android's installed
on-screen keyboard opens automatically. Type using the touchscreen and press
the keyboard's Enter key to accept; the keyboard closes and fullscreen returns.
This covers save filenames, manager names and the other standard text fields.
The original character restrictions and field lengths still apply (eight
characters for save filenames). Back can dismiss the keyboard without accepting;
the field remains editable until Enter/fire accepts or Escape cancels.

On Android 11 and later the SDL surface fits above the keyboard using IME
insets, including Android 15's edge-to-edge layout. Earlier Android versions use
the window's resize behavior. Text input has its own queue so a keyboard's word
commit does not overflow the Amiga key FIFO or insert duplicate characters.
Emulator validation covered opening a manager-name field using the virtual pad,
touching the actual IME keys, backspace, uppercase conversion, field-length
limiting, Enter acceptance and return to fullscreen. Earlier Android versions
have not been exercised locally.

The integration is in the shared `InputText()` wrapper and editor, with these
details preserved for future maintenance:

- Start/stop SDL text input at field entry/exit, and suppress the opening button
  until released so it cannot immediately accept the field.
- Use `SDL_TEXTINPUT` for printable text. SDL's Android input connection can also
  synthesize key events for those characters; ignore the printable key events
  during editing to avoid duplicate letters.
- Use a separate 256-byte ring buffer, drained one character per iteration,
  instead of the original nine-key FIFO. Convert ASCII lowercase to uppercase
  and let `InputTextFilterKey()` enforce the original character restrictions.
- Route Backspace, Delete, cursor movement, Enter and Escape to existing editor
  control codes. Text fields own punctuation before video hotkeys, so `+`/`-`
  do not resize the window while typing.
- Pump `WaitForVSync()` inside the blocking Android editor loop. Without that,
  the keyboard could appear while the game failed to pump IME input or present
  updated text on the main thread.
- Temporarily leave fullscreen while typing and restore it afterward. On
  API 30+, use `SOFT_INPUT_ADJUST_NOTHING` and IME-inset bottom padding on the
  SDL parent. `adjustResize` alone did not keep the field visible on API 35.

Version 0.4 disables Android's default focus highlight on the SDL SurfaceView.
After the keyboard closed and SDL restored focus, that foreground tinted the
entire game white. Reproduced in the emulator: a background pixel changed from
RGB (0, 0, 34) to (31, 31, 61). With the fix, it stays (0, 0, 34) before and after
typing/accepting a name. Keyboard input and the return to fullscreen still work.
The fix is `mSurface.setDefaultFocusHighlightEnabled(false)` after SDL creates
the surface in `OpenSWOSActivity`. The user confirmed the corrected display on
their handheld.

Evidence is kept locally in `build/android-tools/` (screenshots and build logs);
`dist/android/emulator-match.png` shows the live match. ARM translation, a virtual
pad, and the software-rendered emulator do not establish Retroid-specific button
mapping, native-device performance, or audible sound quality.

## Launcher icon

The first APK had a generic icon. The port now uses the existing SWOS ball art
from `SWOS.svg` as an Android adaptive icon. The manifest sets both `icon` and
`roundIcon`. The foreground PNG is centred in the adaptive safe area over a
dark blue background.

Resources are under `android/app/src/main/res/`. The existing
`scripts/generate_icons.py` also regenerates
`drawable-nodpi/openswos_mark.png`. Normal builds use the stored image and need
no rasterisation tools; changing the artwork uses the icon script's existing
CairoSVG/Pillow dependencies.

## APK checks and regression procedure

The built APK was checked with these commands:

```powershell
$sdk = "$PWD/build/android-sdk"
& "$sdk/build-tools/34.0.0/apksigner.bat" verify --verbose `
    dist/android/OpenSWOS-test-arm64.apk
& "$sdk/build-tools/35.0.0/zipalign.exe" -c -P 16 4 `
    dist/android/OpenSWOS-test-arm64.apk
& "$sdk/build-tools/34.0.0/aapt.exe" dump badging `
    dist/android/OpenSWOS-test-arm64.apk
```

The 34.0.0 `zipalign` rejected `-P`; the 35.0.0 tool was used for the 16 KB check.
Additional checks covered APK/nested ZIP integrity, both ARM64 shared libraries,
the exported `SDL_main`, native LOAD alignment, and the absence of desktop
saves/logs/executables in the bundle.

For future changes, build and run the APK locally before handing it over.
Repeat text open/type/delete/accept/close when changing surface or fullscreen
code, and compare colours after focus returns. Preserve the separate save
directory and repeat update-persistence checks when changing extraction or
application identity. Existing screenshots/logs are ignored local artifacts
and will not be present in a fresh checkout.

## Source map

| File / directory | Responsibility |
|---|---|
| `android/app/build.gradle`, root Gradle files and wrapper | App version, SDK/NDK/ABI and build tooling |
| `android/app/jni/Android.mk`, `Application.mk` | Native sources, SDL, defines, response files and page-size support |
| `android/app/src/main/AndroidManifest.xml` | Activity, orientation, resize mode and icon |
| `android/app/src/main/java/net/openswos/OpenSWOSActivity.java` | Extraction, startup errors, IME insets, focus-highlight fix, pad/touch detection for KEYS |
| `android/app/src/main/java/net/openswos/GameKeys.java` | Touch KEYS panel |
| `android/app/src/main/res/` | Adaptive icon resources |
| `scripts/build_android.ps1` | Windows build and APK staging |
| `scripts/stage_android.py` | Build stamp and private runtime ZIP |
| `src/main.c` | Android CWD, orientation and sensor joystick hint |
| `src/swos_io.c` | Internal settings/save paths |
| `src/platform_sdl.c` | Lifecycle clock gate, rendering thread restrictions, Back translation and text-window transitions |
| `src/input_sdl.c`, `src/input_text.c`, `include/input_text.h` | Android text queue and shared editor integration |
| `src/build_info.c` | Android platform identification in diagnostics |
| `src/rollback_rig.c` | Exclude the desktop process/ELF harness on Android |
| `scripts/generate_icons.py` | Regenerate Android foreground from shared artwork |

## Test-build history and remaining work

| Version | Change | Confirmation |
|---|---|---|
| `0.1-test` | Initial ARM64 port and bundled prepared data; sensor joystick fix during emulator testing | Emulator live match and handheld working |
| `0.2-test` | SWOS ball launcher icon | Emulator launcher and handheld confirmed |
| `0.3-test` | Automatic Android keyboard, text queue and keyboard layout handling | Emulator typing and handheld confirmed |
| `0.4-test` | Disable SurfaceView focus highlight left after IME dismissal | Emulator pixel comparison and handheld confirmed |
| `0.5-test` | Refresh from current source and prepared runtime data (2026-10-28, base commit `d41c64c`) | Build, signature, 16 KB APK alignment and ZIP integrity passed; emulator update preserved the saved replay; startup, virtual gamepad menu navigation and Home/resume passed with no crash-log entries. Handheld testing pending. |
| `0.6-test` | Touch KEYS panel (`GameKeys.java`): Esc, F-keys, letters, digits, cursor keys | Emulator: Esc from the panel left shooting practice via REPLAY/EXIT. Handheld testing pending. |
| `0.7-test` | Hold Start (or R3) 0.7 s = Escape; Start tap pauses on release; R3 tap does nothing; KEYS hides while the pad is used and returns on touch | `--input-selftest`; emulator with a virtual Xbox pad: Start tap paused (identical frames), Start hold and R3 hold each left shooting practice, R3 tap left it running, a touch brought KEYS back. Handheld testing pending. |

Keep `versionCode` increasing for subsequent updates. The APK currently remains
a private debug/test build containing the supplied user's prepared game data.
Public distribution still needs a data-import workflow, release signing and
packaging, a decision about Android multiplayer, and broader Android/device
testing. On-screen gameplay controls (virtual stick and fire, for phones without a
pad) are out of scope while the target is the Retroid; if added, they should be a
virtual SDL controller so the game's pad path handles them unchanged.
