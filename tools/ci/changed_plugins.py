#!/usr/bin/env python3
"""
Which plugins a push needs to build: the ones whose folder changed, all of them when shared code changed,
none when only CI scripts, the site generator or docs changed (the download site is still rebuilt).

  python3 tools/ci/changed_plugins.py --files changed.txt     # one changed path per line
  python3 tools/ci/changed_plugins.py --all                    # every plugin

Prints the plugin targets, space-separated (empty: nothing to build).
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# Paths that change no plugin binary, installer or QA result.
NO_BUILD = (".github/workflows/", "tools/ci/", "tools/site/", ".claude/", "CLAUDE.md", "README")


def all_plugins():
    out = subprocess.check_output([sys.executable, os.path.join(ROOT, "tools/ci/plugin_meta.py"), "--names"]).decode()
    return [n for n in out.split() if n]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--files")
    ap.add_argument("--all", action="store_true")
    args = ap.parse_args()
    names = all_plugins()
    if args.all or not args.files or not os.path.exists(args.files):
        print(" ".join(names))
        return
    files = [l.strip() for l in open(args.files) if l.strip()]
    if not files:
        print(" ".join(names))   # unknown diff: be safe
        return
    picked = set()
    for f in files:
        if f.startswith("plugins/"):
            parts = f.split("/")
            if len(parts) > 1 and parts[1] in names:
                picked.add(parts[1])
            continue
        if f.startswith(NO_BUILD):
            continue
        print(" ".join(names))   # shared code (modules, cmake, harness, QA tools, packaging): everything
        return
    print(" ".join(n for n in names if n in picked))


if __name__ == "__main__":
    main()
