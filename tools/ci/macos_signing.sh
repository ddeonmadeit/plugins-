#!/usr/bin/env bash
# Set up Developer ID signing on the macOS runner, when the owner has added the certificates.
#   MACOS_CERTS_P12       base64 of one .p12 holding "Developer ID Application" and "Developer ID Installer"
#   MACOS_CERTS_PASSWORD  the password the .p12 was exported with
# Writes DD_SIGN_APP, DD_SIGN_INSTALLER and DD_TEAM_ID to $GITHUB_ENV for package_macos.sh and
# notarize_macos.sh. Without the secret it does nothing and the build stays ad-hoc signed.
set -euo pipefail

if [[ -z "${MACOS_CERTS_P12:-}" ]]; then
    echo "No Developer ID certificates (secret MACOS_CERTS_P12): ad-hoc signing, installers are not notarized."
    exit 0
fi
: "${MACOS_CERTS_PASSWORD:?secret MACOS_CERTS_PASSWORD is not set}"

keychain="$RUNNER_TEMP/dd-signing.keychain-db"
kpass="$(openssl rand -hex 24)"
security create-keychain -p "$kpass" "$keychain"
security set-keychain-settings -lut 21600 "$keychain"
security unlock-keychain -p "$kpass" "$keychain"

p12="$RUNNER_TEMP/dd-certs.p12"
printf '%s' "$MACOS_CERTS_P12" | base64 --decode > "$p12"
security import "$p12" -k "$keychain" -P "$MACOS_CERTS_PASSWORD" -f pkcs12 \
         -T /usr/bin/codesign -T /usr/bin/productbuild -T /usr/bin/productsign -T /usr/bin/pkgbuild
rm -f "$p12"
security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$kpass" "$keychain" > /dev/null
# Search the new keychain first, keep the runner's own ones after it (split on purpose: one path each).
# shellcheck disable=SC2046
security list-keychains -d user -s "$keychain" $(security list-keychains -d user | tr -d '"')

identities="$(security find-identity -v -p basic "$keychain")"
app="$(sed -n 's/.*"\(Developer ID Application: [^"]*\)".*/\1/p' <<< "$identities" | head -n 1)"
installer="$(sed -n 's/.*"\(Developer ID Installer: [^"]*\)".*/\1/p' <<< "$identities" | head -n 1)"
if [[ -z "$app" || -z "$installer" ]]; then
    echo "$identities"
    echo "::error::MACOS_CERTS_P12 must contain both a Developer ID Application and a Developer ID Installer certificate with their private keys."
    exit 1
fi
team="$(sed -n 's/.*(\([A-Z0-9]\{10\}\))$/\1/p' <<< "$app")"

echo "Signing as: $app"
echo "Installer:  $installer"
{
    echo "DD_SIGN_APP=$app"
    echo "DD_SIGN_INSTALLER=$installer"
    echo "DD_TEAM_ID=$team"
} >> "$GITHUB_ENV"
