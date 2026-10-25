#!/usr/bin/env python3
"""
Build the download site (one page listing every plugin in plugins/).

  gh api "repos/OWNER/REPO/releases?per_page=100" > releases.json
  python3 tools/site/build_site.py --releases releases.json --previews qa-out/site-previews --out _site

Each plugin's download block points at its newest published release tagged "<Plugin>-v<version>"
with assets "<Plugin>-<version>-macOS.pkg/.zip". A plugin with "publish": true but no release yet is
listed as "not released yet"; unpublished plugins without a release are left off the page.
Text comes from plugins/<Plugin>/site.json; previews from tools/site/make_previews.py.
"""
import argparse
import datetime
import json
import os
import re
import shutil
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def version_key(v):
    return [int(x) if x.isdigit() else x for x in re.split(r"[.\-]", v)]


def latest_release(releases, target):
    best = None
    for r in releases:
        tag = r.get("tag_name", "")
        if r.get("draft") or r.get("prerelease") or not tag.startswith(target + "-v"):
            continue
        version = tag[len(target) + 2:]
        if best is None or version_key(version) > version_key(best["version"]):
            assets = {a["name"]: a for a in r.get("assets", [])}

            def asset(suffix):
                for name, a in assets.items():
                    if name.endswith(suffix):
                        return {"url": a["browser_download_url"], "size": a["size"]}
                return None

            best = {"version": version, "tag": tag, "url": r["html_url"],
                    "date": (r.get("published_at") or "")[:10],
                    "pkg": asset("-macOS.pkg"), "zip": asset("-macOS.zip")}
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--releases", required=True)
    ap.add_argument("--previews", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--repo", default="ddeonmadeit/plugins-")
    args = ap.parse_args()

    releases = json.load(open(args.releases))
    plugins = json.loads(subprocess.check_output(["python3", os.path.join(ROOT, "tools/ci/plugin_meta.py")]))

    os.makedirs(args.out, exist_ok=True)
    site = {"repo": args.repo, "generated": datetime.date.today().isoformat(), "plugins": []}
    for p in plugins:
        s = p.get("site", {})
        release = latest_release(releases, p["target"])
        if release is None and not s.get("publish", False):
            print("%-12s skipped (not published yet)" % p["target"])
            continue  # work in progress stays off the public page
        entry = {"target": p["target"], "product": p["product"], "kind": p["kind"],
                 "tagline": s.get("tagline", ""), "description": s.get("description", ""),
                 "accent": s.get("accent", "#ff5a1f"), "highlights": s.get("highlights", []),
                 "installTips": s.get("installTips", []), "previewNote": s.get("previewNote", ""),
                 "release": release, "previews": []}

        src = os.path.join(args.previews, p["target"])
        if os.path.exists(os.path.join(src, "entries.json")):
            dest = os.path.join(args.out, "audio", p["target"])
            os.makedirs(dest, exist_ok=True)
            for e in json.load(open(os.path.join(src, "entries.json"))):
                shutil.copy(os.path.join(src, e["file"]), os.path.join(dest, e["file"]))
                e["src"] = "audio/%s/%s" % (p["target"], e.pop("file"))
                entry["previews"].append(e)

        site["plugins"].append(entry)
        print("%-12s release: %-8s previews: %d" % (p["target"], entry["release"]["version"] if entry["release"] else "none",
                                                 len(entry["previews"])))

    # Released plugins first, newest release first; unreleased ones at the end.
    site["plugins"].sort(key=lambda e: e["release"]["date"] if e["release"] else "", reverse=True)

    html = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "template.html")).read()
    data = json.dumps(site, separators=(",", ":")).replace("</", "<\\/")
    open(os.path.join(args.out, "index.html"), "w").write(html.replace("__SITE__", data))
    open(os.path.join(args.out, ".nojekyll"), "w").close()
    print("wrote %s/index.html" % args.out)


if __name__ == "__main__":
    main()
