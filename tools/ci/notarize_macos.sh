#!/usr/bin/env bash
# Have Apple notarize a Developer ID signed installer, staple the ticket to it, and staple the
# plug-in bundles inside the matching zip too (the pkg's ticket covers them; stapling lets macOS
# check them offline). After this, double-clicking the .pkg shows no "Apple could not verify" warning.
#   tools/ci/notarize_macos.sh dist/<Plugin>-<ver>-macOS.pkg [dist/<Plugin>-<ver>-macOS.zip]
# Needs APPLE_ID and APPLE_APP_PASSWORD (an app-specific password from account.apple.com), plus
# DD_TEAM_ID from macos_signing.sh (APPLE_TEAM_ID overrides it).
set -euo pipefail

pkg="$1"
zip="${2:-}"
: "${APPLE_ID:?secret APPLE_ID is not set (needed to notarize)}"
: "${APPLE_APP_PASSWORD:?secret APPLE_APP_PASSWORD is not set (needed to notarize)}"
team="${APPLE_TEAM_ID:-${DD_TEAM_ID:-}}"
[[ -n "$team" ]] || { echo "::error::No team id: set APPLE_TEAM_ID"; exit 1; }
auth=(--apple-id "$APPLE_ID" --password "$APPLE_APP_PASSWORD" --team-id "$team")

echo "== notarizing $(basename "$pkg")"
out="$(xcrun notarytool submit "$pkg" "${auth[@]}" --wait --timeout 45m --output-format json || true)"
echo "$out"
field() { python3 -c 'import json, sys
try: print(json.loads(sys.argv[1]).get(sys.argv[2], ""))
except Exception: print("")' "$out" "$1"; }
id="$(field id)"
status="$(field status)"
if [[ "$status" != Accepted ]]; then
    [[ -n "$id" ]] && xcrun notarytool log "$id" "${auth[@]}" || true
    echo "::error::Notarization of $(basename "$pkg") ended with status '${status:-no answer}'"
    exit 1
fi

xcrun stapler staple "$pkg"
xcrun stapler validate "$pkg"
spctl --assess --type install -vv "$pkg"

if [[ -n "$zip" ]]; then
    tmp="$(mktemp -d)"
    ditto -x -k "$zip" "$tmp"
    dir="$(find "$tmp" -mindepth 1 -maxdepth 1 -type d | head -n 1)"
    for b in "$dir"/*.vst3 "$dir"/*.component; do
        [[ -e "$b" ]] || continue
        xcrun stapler staple "$b" || echo "::warning::Could not staple $(basename "$b"); it is still notarized (checked online)."
    done
    rm -f "$zip"
    ditto -c -k --keepParent "$dir" "$zip"
    rm -rf "$tmp"
fi
