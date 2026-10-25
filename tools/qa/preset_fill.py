#!/usr/bin/env python3
"""
Make every preset JSON list every parameter (missing ones get the plugin's default), in the
plugin's parameter order. Factory presets then keep sounding the same even if a default changes.

  python3 tools/qa/preset_fill.py <harness-binary> plugins/<Plugin>/Presets
"""
import glob
import json
import os
import subprocess
import sys


def default_value(p):
    v = p["default"]
    if p["kind"] == "choice":
        return p["choices"][int(round(v))]
    if p["kind"] == "bool":
        return bool(v >= 0.5)
    if p["kind"] == "int":
        return int(round(v))
    return round(float(v), 4)


def main():
    harness, presets_dir = sys.argv[1], sys.argv[2]
    info = json.loads(subprocess.check_output([harness, "param-info"]).decode())
    files = sorted(glob.glob(os.path.join(presets_dir, "**", "*.json"), recursive=True))
    for f in files:
        doc = json.load(open(f))
        old = doc.get("params", {})
        doc["params"] = {p["id"]: old.get(p["id"], default_value(p)) for p in info}
        extra = set(old) - set(doc["params"])
        if extra:
            print("warning: %s has unknown params %s (dropped)" % (f, sorted(extra)))
        with open(f, "w") as out:
            json.dump(doc, out, indent=2)
            out.write("\n")
    print("filled %d presets" % len(files))


if __name__ == "__main__":
    main()
