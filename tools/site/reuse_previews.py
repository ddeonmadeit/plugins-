#!/usr/bin/env python3
"""
Keep the preview audio of plugins that were not rebuilt: copies it from the published site (a checkout of
the gh-pages branch) into the previews folder build_site.py reads, for every plugin that has no fresh
previews there.

  python3 tools/site/reuse_previews.py <gh-pages checkout> <previews dir>
"""
import json
import os
import re
import shutil
import sys


def main():
    old, previews = sys.argv[1], sys.argv[2]
    page = os.path.join(old, "index.html")
    if not os.path.exists(page):
        print("no published site to reuse")
        return
    m = re.search(r"const SITE = (\{.*?\});\n", open(page, encoding="utf-8").read(), re.S)
    if not m:
        print("published site has no data block")
        return
    site = json.loads(m.group(1).replace("<\\/", "</"))
    for p in site.get("plugins", []):
        target, entries = p["target"], p.get("previews", [])
        dest = os.path.join(previews, target)
        if os.path.exists(os.path.join(dest, "entries.json")) or not entries:
            continue
        os.makedirs(dest, exist_ok=True)
        kept = []
        for e in entries:
            src = os.path.join(old, e["src"])
            if not os.path.exists(src):
                continue
            e = dict(e)
            e["file"] = os.path.basename(e.pop("src"))
            shutil.copy(src, os.path.join(dest, e["file"]))
            kept.append(e)
        json.dump(kept, open(os.path.join(dest, "entries.json"), "w"))
        print("%-12s reused %d previews" % (target, len(kept)))


if __name__ == "__main__":
    main()
