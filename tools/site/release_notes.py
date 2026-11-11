#!/usr/bin/env python3
"""Markdown body for a plugin's GitHub Release.  Usage: release_notes.py <Plugin> <version>

DD_NOTARIZED=1 (set by the macOS job after notarize_macos.sh) drops the Gatekeeper workaround and adds
a marker that tools/site/build_site.py reads, so the download page stops offering it too.
"""
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SITE_URL = "https://ddeonmadeit.github.io/plugins-/"
NOTARIZED_MARK = "<!-- dd:notarized -->"   # keep in sync with build_site.py
notarized = os.environ.get("DD_NOTARIZED") == "1"

plugin, version = sys.argv[1], sys.argv[2]
meta = json.loads(subprocess.check_output(["python3", os.path.join(ROOT, "tools/ci/plugin_meta.py"), "--only", plugin]))[0]
site = meta.get("site", {})
where = "Generators" if meta["kind"] == "SYNTH" else "Effects"
ableton = ("Ableton Live: Settings (Cmd+,) → Plug-Ins: turn on **Use Audio Units** and **Use VST3 Plug-In System Folders**, then click "
           "**Rescan**. %s appears in the browser under Plug-Ins (ddeonmadeit folder); drag it onto %s."
           % (meta["product"], "a MIDI track" if meta["kind"] == "SYNTH" else "a track"))

lines = ["**%s**: %s" % (meta["product"], site.get("tagline", "")), "", site.get("description", ""), ""]
new = site.get("whatsNew") or {}
if new.get("version") == version and new.get("items"):
    lines += ["### What's new in %s" % version] + ["- " + h for h in new["items"]] + ["", "### Everything in %s" % meta["product"]]
for h in site.get("highlights", []):
    lines.append("- " + h)
lines += ["", "### Install (macOS 11+, Apple Silicon + Intel, VST3 + AU)"]
if notarized:
    lines += [
        "1. Download **%s-%s-macOS.pkg** below and double-click it." % (plugin, version),
        "2. FL Studio: Options → Manage plugins → **Find more plugins**. %s appears under %s." % (meta["product"], where),
        "3. " + ableton,
    ]
else:
    lines += [
        "**No macOS warning:** open Terminal, paste this and press Return (it asks for your Mac password):",
        "",
        "```",
        "curl -fsSL %sinstall.sh | bash -s %s" % (SITE_URL, meta["product"]),
        "```",
        "",
        "Or download **%s-%s-macOS.pkg** below and double-click it. The first time, macOS says \"Apple could not verify\" it: "
        "click Done, then System Settings → Privacy & Security → **Open Anyway**, and run the .pkg again." % (plugin, version),
        "",
        "Then in FL Studio: Options → Manage plugins → **Find more plugins**. %s appears under %s." % (meta["product"], where),
        "",
        "Or in " + ableton,
    ]
lines += [
    "",
    "No admin password? Use the .zip: unzip it and run `bash ~/Downloads/%s-%s-macOS/Install.command` in Terminal." % (plugin, version),
    "",
    "**Updating from an older version:** quit your DAW, run the new .pkg (it replaces the old version), then reopen. "
    "Projects keep all their settings; FL Studio needs no rescan (in Ableton, click Rescan if it still shows the old version).",
    "",
]
for t in site.get("installTips", []):
    lines.append("- " + t)
lines += [
    "",
    "Previews of every preset and all other plugins: %s" % SITE_URL,
]
if notarized:
    lines += ["", NOTARIZED_MARK]
print("\n".join(lines))
