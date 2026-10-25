#!/usr/bin/env python3
"""Markdown body for a plugin's GitHub Release.  Usage: release_notes.py <Plugin> <version>"""
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SITE_URL = "https://ddeonmadeit.github.io/plugins-/"

plugin, version = sys.argv[1], sys.argv[2]
meta = json.loads(subprocess.check_output(["python3", os.path.join(ROOT, "tools/ci/plugin_meta.py"), "--only", plugin]))[0]
site = meta.get("site", {})
where = "Generators" if meta["kind"] == "SYNTH" else "Effects"

lines = ["**%s**: %s" % (meta["product"], site.get("tagline", "")), "", site.get("description", ""), ""]
for h in site.get("highlights", []):
    lines.append("- " + h)
lines += [
    "",
    "### Install (macOS 11+, Apple Silicon + Intel, VST3 + AU)",
    "1. Download **%s-%s-macOS.pkg** below and double-click it." % (plugin, version),
    "2. The first time, macOS says it can't verify the developer: System Settings → Privacy & Security → **Open Anyway**, then run the .pkg again.",
    "3. FL Studio: Options → Manage plugins → **Find more plugins**. %s appears under %s." % (meta["product"], where),
    "",
    "No admin password? Use the .zip: unzip it and run `bash ~/Downloads/%s-%s-macOS/Install.command` in Terminal." % (plugin, version),
    "",
]
for t in site.get("installTips", []):
    lines.append("- " + t)
lines += [
    "",
    "Previews of every preset and all other plugins: %s" % SITE_URL,
]
print("\n".join(lines))
