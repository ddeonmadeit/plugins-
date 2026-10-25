#!/bin/bash
# Installs the plug-ins next to this script into your user plug-in folders.
# No admin password needed. Run it by double-clicking, or from Terminal:
#   bash ~/Downloads/<folder>/Install.command
set -e
cd "$(dirname "$0")"

VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST3_DIR" "$AU_DIR"

install_bundle () {
    local src="$1" dest_dir="$2"
    local name; name="$(basename "$src")"
    rm -rf "$dest_dir/$name"
    ditto "$src" "$dest_dir/$name"
    # Downloaded files are quarantined by macOS; plug-ins won't load until that flag is removed.
    xattr -dr com.apple.quarantine "$dest_dir/$name" 2>/dev/null || true
    echo "  installed $dest_dir/$name"
}

shopt -s nullglob
for b in *.vst3; do install_bundle "$b" "$VST3_DIR"; done
for b in *.component; do install_bundle "$b" "$AU_DIR"; done

# Make macOS re-scan Audio Units.
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo
echo "Done. In FL Studio: Options > Manage plugins > Find more plugins (or Start scan),"
echo "then find the plug-in under Generators."
