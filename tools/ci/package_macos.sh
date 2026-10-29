#!/usr/bin/env bash
# Sign and package every built plugin for macOS.
#   tools/ci/package_macos.sh [build-dir] [dist-dir] [libraries-dir]
# Produces, per plugin:
#   dist/<Plugin>-<version>-macOS.pkg   installer -> /Library/Audio/Plug-Ins/{VST3,Components}
#                                       (+ /Library/Application Support/ddeonmadeit/<Plugin>/Library for sample libraries)
#   dist/<Plugin>-<version>-macOS.zip   bundles (+ "<Plugin> Library") + Install.command + README.txt
# README.txt is packaging/macos/README.txt followed by plugins/<Plugin>/README-mac.txt when it exists.
# Signing: Developer ID (hardened runtime, timestamped, pkg signed too) when macos_signing.sh found the
# owner's certificates (DD_SIGN_APP / DD_SIGN_INSTALLER), ad-hoc otherwise (Apple Silicon needs at least that).
# notarize_macos.sh then notarizes the released installers.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${1:-$ROOT/build}"
DIST="${2:-$ROOT/dist}"
LIBS="${3:-$ROOT/libraries}"
mkdir -p "$DIST"

# DD_ONLY=SubGrit restricts packaging to the plugins built for a release tag.
python3 "$ROOT/tools/ci/plugin_meta.py" ${DD_ONLY:+--only "$DD_ONLY"} > "$DIST/plugins.json"
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
        if [[ -n "${DD_SIGN_APP:-}" ]]; then
            codesign --force --deep --options runtime --timestamp --sign "$DD_SIGN_APP" "$b"
        else
            codesign --force --deep --timestamp=none --sign - "$b"
        fi
        codesign --verify --deep --strict "$b"
        lipo -info "$b/Contents/MacOS/$product"
    done

    # zip: bundles + installer script + instructions (ditto keeps permissions and bundle structure)
    stage="$DIST/stage/$target-$version-macOS"
    rm -rf "$stage"; mkdir -p "$stage"
    ditto "$vst3" "$stage/$product.vst3"
    ditto "$au" "$stage/$product.component"
    lib="$LIBS/$target"
    if [[ -f "$ROOT/plugins/$target/library/build_library.py" ]]; then
        [[ -f "$lib/catalog.json" ]] || { echo "missing sample library $lib"; exit 1; }
        ditto "$lib" "$stage/$product Library"
    fi
    cp "$ROOT/packaging/macos/Install.command" "$stage/"
    cat "$ROOT/packaging/macos/README.txt" > "$stage/README.txt"
    if [[ -f "$ROOT/plugins/$target/README-mac.txt" ]]; then
        { echo; cat "$ROOT/plugins/$target/README-mac.txt"; } >> "$stage/README.txt"
    fi
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
    parts=(--package "$pk/$target-vst3.pkg" --package "$pk/$target-au.pkg")
    if [[ -d "$stage/$product Library" ]]; then
        pkgbuild --root "$stage/$product Library" --identifier "com.ddeonmadeit.$lower.library" --version "$version" \
                 --install-location "/Library/Application Support/ddeonmadeit/$product/Library" "$pk/$target-library.pkg"
        parts+=(--package "$pk/$target-library.pkg")
    fi
    sign=()
    [[ -z "${DD_SIGN_INSTALLER:-}" ]] || sign=(--sign "$DD_SIGN_INSTALLER" --timestamp)
    productbuild ${sign[@]+"${sign[@]}"} "${parts[@]}" "$DIST/$target-$version-macOS.pkg"
done

rm -rf "$DIST/stage"
ls -la "$DIST"
