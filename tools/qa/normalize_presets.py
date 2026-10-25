#!/usr/bin/env python3
"""
Loudness-match factory presets by adjusting their output-gain parameter, using the report.json
written by analyze.py. Gains never push a preset's true peak above --max-tp.

  python3 tools/qa/normalize_presets.py plugins/<Plugin>/Presets qa-out/<Plugin>/renders/report.json \
      --target -9.5 [--max-tp -0.8] [--param outGain] [--min -14] [--max 3]

Then rebuild (presets are embedded), re-render and re-analyze. Two passes usually converge;
presets that stay quiet are peak-limited: give them more density (clip/saturation) instead.
"""
import argparse
import glob
import json
import os


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("presets_dir")
    ap.add_argument("report")
    ap.add_argument("--target", type=float, required=True)
    ap.add_argument("--max-tp", type=float, default=-0.8)
    ap.add_argument("--param", default="outGain")
    ap.add_argument("--min", type=float, default=-14.0)
    ap.add_argument("--max", type=float, default=3.0)
    args = ap.parse_args()

    report = {r["program"].split(" - ", 1)[-1]: r for r in json.load(open(args.report))}
    changed = 0
    for f in sorted(glob.glob(os.path.join(args.presets_dir, "**", "*.json"), recursive=True)):
        doc = json.load(open(f))
        r = report.get(doc["name"])
        if r is None:
            print("no render for %s" % doc["name"])
            continue
        delta = min(args.target - r["lufs"], args.max_tp - r["true_peak_db"])
        old = float(doc["params"].get(args.param, 0.0))
        new = round(max(args.min, min(args.max, old + delta)), 1)
        if abs(new - old) >= 0.1:
            doc["params"][args.param] = new
            with open(f, "w") as out:
                json.dump(doc, out, indent=2)
                out.write("\n")
            changed += 1
            print("%-28s %+5.1f dB -> %s = %.1f" % (doc["name"], new - old, args.param, new))
    print("%d presets changed" % changed)


if __name__ == "__main__":
    main()
