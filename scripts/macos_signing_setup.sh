#!/usr/bin/env bash
# macos_signing_setup.sh -- ONCE, on the Mac that builds OpenSWOS, by its owner in a terminal there:
# a keychain of its own for signing and notarizing, so scripts/build_macos.sh can do both over ssh
# (the login keychain is locked in an ssh session: codesign fails with errSecInternalComponent).
#
#   scripts/macos_signing_setup.sh <DeveloperID.p12> [--apple-id you@example.com]
#
# What it makes (docs/MACOS_BUILD.md, "Signing and notarizing"):
#   ~/Library/Keychains/openswos-signing.keychain-db   the Developer ID Application certificate and
#                                                      its key, and the notary credentials
#   ~/.openswos-signing/keychain.pass                  that keychain's password (random, made here,
#                                                      readable by you alone); build_macos.sh
#                                                      unlocks the keychain with it
#   ~/.openswos-signing/identity                       the certificate's name, for codesign
#
# The .p12: in Keychain Access, My Certificates, "Developer ID Application: <you> (<team>)", File ->
# Export Items..., as .p12 with a password of your choosing (asked for below, then the file can go).
# The notary credentials: an app-specific password from account.apple.com -> Sign-In and Security ->
# App-Specific Passwords; notarytool asks for it. Nothing typed here is stored anywhere but the
# keychain.
set -euo pipefail

P12=${1:-}
if [ -z "$P12" ]; then
    sed -n '2,20p' "$0"
    exit 2
fi
if [ ! -f "$P12" ]; then
    echo "No file at $P12 -- the certificate exported from Keychain Access (File -> Export Items...;"
    echo "it is named Certificates.p12 unless you named it). Found here:"
    ls -1 "$HOME"/Desktop/*.p12 "$HOME"/Documents/*.p12 "$HOME"/Downloads/*.p12 2>/dev/null | sed 's/^/  /' ||
        echo "  (no .p12 on the Desktop, in Documents or in Downloads)"
    exit 2
fi
shift
APPLE_ID=""
[ "${1:-}" = "--apple-id" ] && APPLE_ID=${2:-}

[ "$(uname -s)" = Darwin ] || { echo "run this on the Mac"; exit 1; }
DIR="$HOME/.openswos-signing"
KC="$HOME/Library/Keychains/openswos-signing.keychain-db"
PROFILE=openswos-notary

mkdir -p "$DIR" && chmod 700 "$DIR"
if [ ! -f "$DIR/keychain.pass" ]; then
    ( umask 077; openssl rand -base64 32 > "$DIR/keychain.pass" )
fi
KCPASS=$(cat "$DIR/keychain.pass")

if [ ! -f "$KC" ]; then
    echo "== creating $KC"
    security create-keychain -p "$KCPASS" "$KC"
fi
security unlock-keychain -p "$KCPASS" "$KC"
security set-keychain-settings "$KC"                      # (no auto-lock: a build can be long)

# On the user's search list, after the ones already there, so codesign and notarytool find it.
EXISTING=$(security list-keychains -d user | sed 's/^ *"//; s/"$//')
if ! printf '%s\n' "$EXISTING" | grep -qx "$KC"; then
    # shellcheck disable=SC2086
    security list-keychains -d user -s $(printf '%s\n' "$EXISTING" | tr '\n' ' ') "$KC"
fi

echo "== importing $P12 (its export password, please)"
security import "$P12" -k "$KC" -f pkcs12 -T /usr/bin/codesign -T /usr/bin/security -T /usr/bin/productbuild
security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$KCPASS" "$KC" >/dev/null

IDENTITY=$(security find-identity -v -p codesigning "$KC" | sed -n 's/.*"\(Developer ID Application: .*\)"/\1/p' | head -1)
[ -n "$IDENTITY" ] || { echo "!! no Developer ID Application certificate in $P12"; exit 1; }
printf '%s\n' "$IDENTITY" > "$DIR/identity"
TEAM=$(printf '%s' "$IDENTITY" | sed -n 's/.*(\([A-Z0-9]*\))$/\1/p')
echo "== identity: $IDENTITY (team $TEAM)"

echo "== a test signature"
T=$(mktemp -d); cp /bin/echo "$T/t"
codesign --force --options runtime --timestamp --sign "$IDENTITY" --keychain "$KC" "$T/t"
codesign --verify --strict "$T/t" && echo "   signed and verified"
rm -rf "$T"

echo "== the notary credentials, as the profile '$PROFILE' (notarytool asks for the app-specific password)"
[ -n "$APPLE_ID" ] || read -r -p "Apple ID (e-mail): " APPLE_ID
xcrun notarytool store-credentials "$PROFILE" --apple-id "$APPLE_ID" --team-id "$TEAM" --keychain "$KC"
xcrun notarytool history --keychain-profile "$PROFILE" --keychain "$KC" >/dev/null && echo "   the profile works"

echo ""
echo "Done. scripts/build_macos.sh now signs with '$IDENTITY' and notarizes as '$PROFILE'."
echo "You can delete $P12."
