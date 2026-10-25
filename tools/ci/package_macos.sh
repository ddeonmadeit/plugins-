#!/usr/bin/env bash
# Sign (ad-hoc) and package every built plugin for macOS.
#   tools/ci/package_macos.sh [build-dir] [dist-dir]
# Produces, per plugin:
#   dist/<Plugin>-<version>-macOS.pkg   installer -> /Library/Audio/Plug-Ins/{VST3,Components}
#   dist/<Plugin>-<version>-macOS.zip   bundles + Install.command + README.txt
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${1:-$ROOT/build}"
DIST="${2:-$ROOT/dist}"
mkdir -p "$DIST"

python3 "$ROOT/tools/ci/plugin_meta.py" > "$DIST/plugins.json"
count=$(python3 -c "import json; print(len(json.load(open('$DIST/plugins.json'))))")

for i in $(seq 0 $((count - 1))); do
    meta() { python3 -c "import json; print(json.load(open('$DIST/plugins.json'))[$i]['$1'])"; }
    target=$(meta target); product=$(meta product); version=$(meta version)
    art="$BUILD/plugins/$target/${target}_artefacts/Release"
    vst3="$art/VST3/$product.vst3"
    au="$art/AU/$product.component"
    lower=$(echo "$target" | tr '[:upper:]' '[:lower:]')

    echo "== $target $version"
    for b in "$vst3" "$au"; do
        [[ -d "$b" ]] || { echo "missing $b"; exit 1; }
        codesign --force --deep --timestamp=none --sign - "$b"
        codesign --verify --deep --strict "$b"
        lipo -info "$b/Contents/MacOS/$product"
    done

    # zip: bundles + installer script + instructions (ditto keeps permissions and bundle structure)
    stage="$DIST/stage/$target-$version-macOS"
    rm -rf "$stage"; mkdir -p "$stage"
    ditto "$vst3" "$stage/$product.vst3"
    ditto "$au" "$stage/$product.component"
    cp "$ROOT/packaging/macos/Install.command" "$ROOT/packaging/macos/README.txt" "$stage/"
    chmod +x "$stage/Install.command"
    ditto -c -k --keepParent "$stage" "$DIST/$target-$version-macOS.zip"

    # pkg: one component package per format, bundles marked non-relocatable so the installer
    # never "moves" them to wherever an older copy happens to live.
    pk="$DIST/stage/pkg-$target"
    rm -rf "$pk"; mkdir -p "$pk/vst3" "$pk/au"
    ditto "$vst3" "$pk/vst3/$product.vst3"
    ditto "$au" "$pk/au/$product.component"
    for fmt in vst3 au; do
        pkgbuild --analyze --root "$pk/$fmt" "$pk/$fmt.plist"
        plutil -replace BundleIsRelocatable -bool NO "$pk/$fmt.plist"
    done
    pkgbuild --root "$pk/vst3" --component-plist "$pk/vst3.plist" --identifier "com.ddeonmadeit.$lower.vst3" \
             --version "$version" --install-location "/Library/Audio/Plug-Ins/VST3" "$pk/$target-vst3.pkg"
    pkgbuild --root "$pk/au" --component-plist "$pk/au.plist" --identifier "com.ddeonmadeit.$lower.au" \
             --version "$version" --install-location "/Library/Audio/Plug-Ins/Components" "$pk/$target-au.pkg"
    productbuild --package "$pk/$target-vst3.pkg" --package "$pk/$target-au.pkg" "$DIST/$target-$version-macOS.pkg"
done

rm -rf "$DIST/stage"
ls -la "$DIST"
