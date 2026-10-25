#!/usr/bin/env python3
"""Print metadata for every plugin in plugins/ (parsed from its dd_add_plugin() call).

  python3 tools/ci/plugin_meta.py            # JSON list
  python3 tools/ci/plugin_meta.py --names    # one target name per line
"""
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MANUFACTURER = "Ddeo"


def parse(path):
    text = open(path).read()
    m = re.search(r"dd_add_plugin\(\s*(\w+)(.*?)\)", text, re.S)
    if not m:
        return None
    body = m.group(2)

    def field(key, default=None):
        f = re.search(r"\b%s\s+(\"[^\"]*\"|\S+)" % key, body)
        return f.group(1).strip('"') if f else default

    kind = field("KIND", "SYNTH")
    return {
        "target": m.group(1),
        "product": field("PRODUCT_NAME", m.group(1)),
        "code": field("PLUGIN_CODE"),
        "kind": kind,
        "version": field("VERSION", "1.0.0"),
        "au_type": "aumu" if kind == "SYNTH" else "aufx",
        "manufacturer": MANUFACTURER,
    }


def main():
    plugins = [p for p in (parse(f) for f in sorted(glob.glob(os.path.join(ROOT, "plugins", "*", "CMakeLists.txt")))) if p]
    if "--names" in sys.argv:
        print("\n".join(p["target"] for p in plugins))
    else:
        print(json.dumps(plugins, indent=1))


if __name__ == "__main__":
    main()
