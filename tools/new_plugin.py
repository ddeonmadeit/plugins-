#!/usr/bin/env python3
"""
Scaffold a new plugin from templates/plugin.

  python3 tools/new_plugin.py MyBass --kind synth
  python3 tools/new_plugin.py GritBox --kind effect --code Grbx

Creates plugins/<Name>/ with sources, an INIT preset and qa.json, then you can:
  cmake -B build && cmake --build build --target <Name>_Harness && tools/qa/run_qa.sh <Name>
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEMPLATE = os.path.join(ROOT, "templates", "plugin")


def filter_kind(text, kind):
    """Keep //@SYNTH or //@EFFECT blocks for this kind, drop the others and the markers."""
    out, keep = [], True
    for line in text.splitlines(keepends=True):
        tag = line.strip()
        if tag in ("//@SYNTH", "//@EFFECT"):
            keep = tag == "//@" + kind
            continue
        if tag == "//@END":
            keep = True
            continue
        if keep:
            out.append(line)
    return "".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("name", help="CamelCase target / product name, e.g. GritBox")
    ap.add_argument("--kind", choices=["synth", "effect"], required=True)
    ap.add_argument("--code", help="4-char plugin code with at least one uppercase letter (default: derived)")
    args = ap.parse_args()

    name = args.name
    if not re.fullmatch(r"[A-Z][A-Za-z0-9]{1,30}", name):
        sys.exit("name must be CamelCase letters/digits, starting with an uppercase letter")
    dest = os.path.join(ROOT, "plugins", name)
    if os.path.exists(dest):
        sys.exit("plugins/%s already exists" % name)

    code = args.code or (name[0].upper() + re.sub(r"[^a-z0-9]", "", name[1:].lower()) + "xxx")[:4]
    if not re.fullmatch(r"[A-Za-z0-9]{4}", code) or code.lower() == code:
        sys.exit("plugin code must be 4 letters/digits with at least one uppercase")
    existing = json.loads(subprocess.check_output([sys.executable, os.path.join(ROOT, "tools", "ci", "plugin_meta.py")]))
    if any(p["code"] == code for p in existing):
        sys.exit("plugin code %s is already used; pass --code" % code)

    kind = args.kind.upper()
    subs = {"__NAME__": name, "__NS__": name.lower(), "__CODE__": code, "__KIND__": kind}

    for dirpath, _, files in os.walk(TEMPLATE):
        rel = os.path.relpath(dirpath, TEMPLATE)
        os.makedirs(os.path.join(dest, rel), exist_ok=True)
        for f in files:
            text = open(os.path.join(dirpath, f)).read()
            text = filter_kind(text, kind)
            for k, v in subs.items():
                text = text.replace(k, v)
            open(os.path.join(dest, rel, f), "w").write(text)

    preset_dir = os.path.join(dest, "Presets", "INIT")
    os.makedirs(preset_dir, exist_ok=True)
    json.dump({"name": "Init", "category": "INIT", "author": "ddeonmadeit",
               "description": "Starting point.", "params": {}},
              open(os.path.join(preset_dir, "01 Init.json"), "w"), indent=2)

    print("created plugins/%s (%s, code %s)" % (name, kind.lower(), code))
    print("next: cmake -B build && cmake --build build --target %s_Harness && tools/qa/run_qa.sh %s" % (name, name))


if __name__ == "__main__":
    main()
