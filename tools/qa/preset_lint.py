#!/usr/bin/env python3
"""
Validate a plugin's preset JSON files against its real parameter list.

  python3 tools/qa/preset_lint.py <harness-binary> plugins/<Plugin>/Presets [--categories "A,B,C"]

Checks: safe file names (Make cannot handle ':' etc.), valid JSON, required fields, unique names,
known category, every param id exists, values inside range, ints are ints, choice names valid,
bools are bools. Exit 1 on any error.
"""
import argparse
import glob
import json
import os
import re
import subprocess
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("harness")
    ap.add_argument("presets_dir")
    ap.add_argument("--categories", default=None, help="comma-separated allowed categories")
    args = ap.parse_args()

    info = json.loads(subprocess.check_output([args.harness, "param-info"]).decode())
    params = {p["id"]: p for p in info}
    allowed = set(c.strip() for c in args.categories.split(",")) if args.categories else None

    errors, names = [], {}
    files = sorted(glob.glob(os.path.join(args.presets_dir, "**", "*.json"), recursive=True))
    basenames = {}
    for f in files:
        rel = os.path.relpath(f, args.presets_dir)
        b = os.path.basename(f)
        if not re.fullmatch(r"[0-9]{2}_[A-Za-z0-9_-]+\.json", b):
            errors.append("%s: file name must look like 01_Some_Name.json (letters, digits, _ and - only)" % rel)
        if b in basenames:
            errors.append("%s: file name also used by %s (binary data names must be unique)" % (rel, basenames[b]))
        basenames[b] = rel
        try:
            data = json.load(open(f))
        except Exception as e:  # noqa: BLE001
            errors.append("%s: invalid JSON: %s" % (rel, e))
            continue
        for key in ("name", "category", "params"):
            if key not in data:
                errors.append("%s: missing '%s'" % (rel, key))
        name = data.get("name", "")
        if name in names:
            errors.append("%s: duplicate preset name '%s' (also %s)" % (rel, name, names[name]))
        names[name] = rel
        if allowed and data.get("category") not in allowed:
            errors.append("%s: unknown category '%s'" % (rel, data.get("category")))
        for pid, v in data.get("params", {}).items():
            if pid.startswith("perf"):
                errors.append("%s: '%s' is a performance parameter; presets must not set it" % (rel, pid))
                continue
            p = params.get(pid)
            if p is None:
                errors.append("%s: unknown parameter '%s'" % (rel, pid))
                continue
            kind = p["kind"]
            if kind == "choice":
                if isinstance(v, str):
                    if v not in p["choices"]:
                        errors.append("%s: %s='%s' not in %s" % (rel, pid, v, p["choices"]))
                elif not isinstance(v, int) or not 0 <= v < len(p["choices"]):
                    errors.append("%s: %s must be one of %s" % (rel, pid, p["choices"]))
            elif kind == "bool":
                if not isinstance(v, bool):
                    errors.append("%s: %s must be true/false" % (rel, pid))
            else:
                if isinstance(v, bool) or not isinstance(v, (int, float)):
                    errors.append("%s: %s must be a number" % (rel, pid))
                    continue
                if kind == "int" and int(v) != v:
                    errors.append("%s: %s must be an integer" % (rel, pid))
                if not p["min"] - 1e-6 <= v <= p["max"] + 1e-6:
                    errors.append("%s: %s=%s outside [%s, %s]" % (rel, pid, v, p["min"], p["max"]))

    for e in errors:
        print("ERROR " + e)
    print("%d preset files, %d errors" % (len(files), len(errors)))
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
