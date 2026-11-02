#!/usr/bin/env bash
# build_ios.sh -- OpenSWOS for iOS (docs/MOBILE.md), built on the Mac mini over ssh (as
# scripts/build_macos.sh), with packaging/mobile/CMakeLists.txt.
#
#   scripts/build_ios.sh               -> dist/OpenSWOS-iOS.ipa: for an iPhone or iPad, UNSIGNED for
#                                         sideloading (AltStore, Sideloadly: they sign it with the
#                                         player's own Apple ID)
#   scripts/build_ios.sh sim           the simulator's build, on the Mac only (a test: xcrun simctl)
#   scripts/build_ios.sh --release     a public build (the testers' rule compiled out)
#   scripts/build_ios.sh --no-pull     build the Mac's tree as it is
#
# The Mac needs Xcode and Homebrew's cmake and ninja. Its checkout (MAC_PATH) is pulled first and
# the commit it built is printed, as build_macos.sh does: a stale Mac is invisible otherwise.
# Overrides: MAC_HOST (default bbaker@192.168.1.200), MAC_PATH (~/Programming/OpenSWOS).
set -uo pipefail
cd "$(dirname "$0")/.."

MAC_HOST="${MAC_HOST:-bbaker@192.168.1.200}"
MAC_PATH="${MAC_PATH:-~/Programming/OpenSWOS}"
WHAT=device
TEST_RULES=ON
DO_PULL=1
for a in "$@"; do
    case "$a" in
        sim) WHAT=sim ;;
        --release) TEST_RULES=OFF ;;
        --no-pull) DO_PULL=0 ;;
        -h|--help) sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "build_ios: unknown argument $a" >&2; exit 2 ;;
    esac
done

if [ "$WHAT" = sim ]; then SDK=iphonesimulator; DIR=build-ios-sim; else SDK=iphoneos; DIR=build-ios-device; fi

# (one remote script: ssh joins its arguments into one string, so the values go in quoted -- printf %q)
REMOTE=$(cat <<EOF
set -e
export PATH=/opt/homebrew/bin:/usr/local/bin:\$PATH
cd $MAC_PATH
if [ $DO_PULL = 1 ]; then
    git pull -q --ff-only
    git submodule update --init -q third_party/ggpo-x third_party/swos-competition-engine
fi
echo "built from commit  : \$(git rev-parse --short=8 HEAD)\$(git diff --quiet HEAD || echo ' (dirty)')"
rm -rf $DIR
cmake -S packaging/mobile -B $DIR -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_SYSTEM_NAME=iOS \\
    -DCMAKE_OSX_SYSROOT=$SDK -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \\
    -DSWOS_TEST_RULES=$TEST_RULES > $DIR.configure.log 2>&1 || { tail -20 $DIR.configure.log; exit 1; }
cmake --build $DIR -j8 > $DIR.build.log 2>&1 || { grep -E "error|^ld: " $DIR.build.log | grep -v warning | head -20; exit 1; }
if [ $WHAT = sim ]; then
    codesign --force --sign - $DIR/OpenSWOS.app
    echo "simulator app: \$PWD/$DIR/OpenSWOS.app"
else
    rm -rf $DIR/Payload $DIR/OpenSWOS-iOS.ipa
    mkdir $DIR/Payload
    cp -R $DIR/OpenSWOS.app $DIR/Payload/
    (cd $DIR && /usr/bin/ditto -c -k --sequesterRsrc --keepParent Payload OpenSWOS-iOS.ipa)
    echo "ipa: \$PWD/$DIR/OpenSWOS-iOS.ipa (\$(du -h $DIR/OpenSWOS-iOS.ipa | cut -f1))"
fi
EOF
)
echo "== iOS build on $MAC_HOST ($WHAT, test rules $TEST_RULES)"
ssh "$MAC_HOST" "bash -c $(printf %q "$REMOTE")" || { echo "build_ios: FAILED" >&2; exit 1; }
if [ "$WHAT" = device ]; then
    mkdir -p dist
    scp -q "$MAC_HOST:$MAC_PATH/$DIR/OpenSWOS-iOS.ipa" dist/OpenSWOS-iOS.ipa || exit 1
    # (the commit it is, for scripts/upload_itch.sh: -dirty never goes out)
    ssh "$MAC_HOST" "cd $MAC_PATH && echo \$(git rev-parse --short=8 HEAD)\$(git diff --quiet HEAD || echo -dirty)" \
        > dist/OpenSWOS-iOS.commit
    echo "dist/OpenSWOS-iOS.ipa ($(cat dist/OpenSWOS-iOS.commit))"
fi
