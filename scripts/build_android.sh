#!/usr/bin/env bash
# build_android.sh -- OpenSWOS for Android (docs/MOBILE.md): packaging/android's Gradle project,
# which builds the game with packaging/mobile/CMakeLists.txt.
#
#   scripts/build_android.sh              release APK, signed with OpenSWOS's key -> dist/OpenSWOS-Android.apk
#   scripts/build_android.sh debug        debug APK (the SDK's debug key)        -> dist/OpenSWOS-Android-debug.apk
#   scripts/build_android.sh install      the debug APK onto the connected phone or emulator, and start it
#   scripts/build_android.sh --release    a public build (the testers' rule compiled out), as build_dist.sh --release
#
# Needs a JDK 17 (JAVA_HOME, default ~/opt/jdk-17*) and the Android SDK (ANDROID_HOME, default
# ~/Android/Sdk) with platform 36, NDK 30.0.16248370 and CMake 4.1.2:
#   sdkmanager "platforms;android-36" "build-tools;36.0.0" "ndk;30.0.16248370" "cmake;4.1.2" "platform-tools"
#
# The release key: ~/.openswos-keys (OPENSWOS_KEYS), its passwords in android.env there -- outside the
# repository, and kept for good: an APK signed with another key will not install over one signed
# with it. Back that folder up.
set -euo pipefail
cd "$(dirname "$0")/.."

MODE=release
TEST_RULES=ON
for a in "$@"; do
    case "$a" in
        debug|install|release) MODE=$a ;;
        --release) TEST_RULES=OFF ;;
        -h|--help) sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "build_android: unknown argument $a" >&2; exit 2 ;;
    esac
done

if [ -z "${JAVA_HOME:-}" ]; then
    JAVA_HOME=$(ls -d "$HOME"/opt/jdk-17* 2>/dev/null | head -1 || true)
fi
: "${ANDROID_HOME:=$HOME/Android/Sdk}"
if [ -z "${JAVA_HOME:-}" ] || [ ! -x "$JAVA_HOME/bin/java" ]; then
    echo "build_android: no JDK 17 (set JAVA_HOME)" >&2; exit 3
fi
if [ ! -d "$ANDROID_HOME/ndk/30.0.16248370" ]; then
    echo "build_android: no Android SDK with NDK 30.0.16248370 at $ANDROID_HOME (set ANDROID_HOME)" >&2; exit 3
fi
export JAVA_HOME ANDROID_HOME PATH="$JAVA_HOME/bin:$PATH" OPENSWOS_TEST_RULES=$TEST_RULES

KEYS="${OPENSWOS_KEYS:-$HOME/.openswos-keys}"
mkdir -p dist
cd packaging/android
case "$MODE" in
    release)
        if [ ! -f "$KEYS/android.env" ]; then
            echo "build_android: no release key in $KEYS (android.env + its keystore)" >&2; exit 3
        fi
        set -a; . "$KEYS/android.env"; set +a
        ./gradlew --console=plain assembleRelease
        cp app/build/outputs/apk/release/app-release.apk ../../dist/OpenSWOS-Android.apk
        "$ANDROID_HOME/build-tools/36.0.0/apksigner" verify --print-certs ../../dist/OpenSWOS-Android.apk \
            | grep -m1 "SHA-256" || { echo "build_android: the APK is not signed" >&2; exit 1; }
        # (the commit it is, for scripts/upload_itch.sh: -dirty never goes out)
        C=$(git -C ../.. rev-parse --short=8 HEAD)
        git -C ../.. diff --quiet HEAD -- . ':!dist' || C="$C-dirty"
        echo "$C" > ../../dist/OpenSWOS-Android.commit
        echo "dist/OpenSWOS-Android.apk ($C)"
        ;;
    debug|install)
        ./gradlew --console=plain assembleDebug
        cp app/build/outputs/apk/debug/app-debug.apk ../../dist/OpenSWOS-Android-debug.apk
        echo "dist/OpenSWOS-Android-debug.apk"
        if [ "$MODE" = install ]; then
            ADB=${ADB:-$ANDROID_HOME/platform-tools/adb}
            ID=$(sed -n 's/^#define PROJECT_APP_ID *"\(.*\)"/\1/p' ../../include/project_identity.h)
            "$ADB" install -r ../../dist/OpenSWOS-Android-debug.apk
            "$ADB" shell am start -n "$ID/net.openswos.game.OpenSWOSActivity"
        fi
        ;;
esac
