#!/usr/bin/env python3
"""
Objective QA for rendered presets (the "ears" when nobody can listen).

  python3 tools/qa/analyze.py qa-out/renders            # table + flags + report.json
  python3 tools/qa/analyze.py qa-out/renders --sheet    # also spectrogram contact sheet (sheet.png)
  python3 tools/qa/analyze.py qa-out/renders --target-lufs -9 --suggest   # outGain fixes

Measures per render: true-ish peak, integrated loudness (BS.1770 K-weighted, gated), band energy
balance, low-band stereo correlation (sub must be mono), DC offset, tail level, and a spectral
fingerprint used to find presets that sound too alike.
"""
import argparse
import json
import math
import os
import sys

import numpy as np
import soundfile as sf
from scipy import signal

BANDS = [("sub", 20, 90), ("low", 90, 250), ("lowmid", 250, 800), ("mid", 800, 2500),
         ("high", 2500, 8000), ("air", 8000, 20000)]


def k_weight(x, sr):
    # BS.1770 coefficients are specified at 48 kHz; resample other rates first.
    if sr != 48000:
        x = signal.resample_poly(x, 48000, sr, axis=0)
    b1, a1 = [1.53512485958697, -2.69169618940638, 1.19839281085285], [1.0, -1.69065929318241, 0.73248077421585]
    b2, a2 = [1.0, -2.0, 1.0], [1.0, -1.99004745483398, 0.99007225036621]
    return signal.lfilter(b2, a2, signal.lfilter(b1, a1, x, axis=0), axis=0)


def integrated_lufs(x, sr):
    y = k_weight(x, sr)
    sr_k = 48000
    block, hop = int(0.4 * sr_k), int(0.1 * sr_k)
    if len(y) < block:
        return -120.0
    powers = []
    for start in range(0, len(y) - block + 1, hop):
        seg = y[start:start + block]
        powers.append(np.sum(np.mean(seg ** 2, axis=0)))
    powers = np.array(powers)
    loud = -0.691 + 10 * np.log10(np.maximum(powers, 1e-12))
    gated = powers[loud > -70]
    if len(gated) == 0:
        return -120.0
    rel = -0.691 + 10 * np.log10(np.mean(gated)) - 10
    gated = powers[(loud > -70) & (loud > rel)]
    return float(-0.691 + 10 * np.log10(np.mean(gated)))


def db(x):
    return 20 * math.log10(max(float(x), 1e-9))


def analyse(path):
    x, sr = sf.read(path, always_2d=True, dtype="float64")
    mono = x.mean(axis=1)
    out = {"file": os.path.basename(path), "sr": sr}
    out["nonfinite"] = int(np.sum(~np.isfinite(x)))
    x = np.nan_to_num(x)
    out["peak_db"] = db(np.max(np.abs(x)))
    up = signal.resample_poly(x, 4, 1, axis=0)
    out["true_peak_db"] = db(np.max(np.abs(up)))
    out["lufs"] = integrated_lufs(x, sr)
    active = np.abs(mono) > 1e-4
    out["rms_db"] = db(np.sqrt(np.mean(mono[active] ** 2))) if active.any() else -120.0
    out["crest_db"] = out["peak_db"] - out["rms_db"]
    out["dc"] = float(np.mean(mono))

    f, pxx = signal.welch(mono, sr, nperseg=8192)
    total = np.sum(pxx[(f >= 20) & (f <= 20000)]) + 1e-20
    for name, lo, hi in BANDS:
        out["band_" + name] = float(np.sum(pxx[(f >= lo) & (f < hi)]) / total)
    out["centroid_hz"] = float(np.sum(f * pxx) / (np.sum(pxx) + 1e-20))

    # Sub must be mono: correlation of L/R below 120 Hz. Only judged when that band carries real energy
    # (> -30 dB of the whole signal); the correlation of a near-silent residual says nothing about mono.
    sos = signal.butter(8, 120, "low", fs=sr, output="sos")   # steep, so stereo mids above 200 Hz do not leak in
    lo = signal.sosfilt(sos, x, axis=0)
    lo_share = np.sqrt(np.mean(lo ** 2)) / (np.sqrt(np.mean(x ** 2)) + 1e-12)
    if lo_share > 0.0316 and np.std(lo[:, 0]) > 1e-6 and np.std(lo[:, 1]) > 1e-6:
        out["sub_corr"] = float(np.corrcoef(lo[:, 0], lo[:, 1])[0, 1])
    else:
        out["sub_corr"] = 1.0
    sos_h = signal.butter(4, 300, "high", fs=sr, output="sos")
    hi = signal.sosfilt(sos_h, x, axis=0)
    if np.std(hi[:, 0]) > 1e-6 and np.std(hi[:, 1]) > 1e-6:
        out["width_corr"] = float(np.corrcoef(hi[:, 0], hi[:, 1])[0, 1])
    else:
        out["width_corr"] = 1.0

    tail = mono[-int(0.25 * sr):]
    out["tail_db"] = db(np.sqrt(np.mean(tail ** 2)) + 1e-12)

    # Fingerprint for "do these two sound alike?". The sub dominates any raw spectrum, so the
    # character part is the 1/3-octave shape from 100 Hz up, relative to total power; plus the
    # envelope of the first note, stereo width and how much tail (delay/space/release) there is.
    edges = 100 * 2 ** (np.arange(0, 23) / 3.0)
    fp = []
    for a, b in zip(edges[:-1], edges[1:]):
        fp.append(10 * np.log10(np.sum(pxx[(f >= a) & (f < b)]) / total + 1e-14))
    fp = np.maximum(np.array(fp), -70.0) / 8.0
    env = np.abs(signal.hilbert(mono[: int(1.7 * sr)]))
    env = signal.resample(env, 40)
    env = 20 * np.log10(np.maximum(env / (env.max() + 1e-12), 1e-4)) / 12.0
    tail_region = mono[int(6.35 * sr): int(7.4 * sr)] if len(mono) > int(7.4 * sr) else mono[-int(0.5 * sr):]
    tail_rel = db(np.sqrt(np.mean(tail_region ** 2)) + 1e-12) - out["rms_db"]
    extras = [(1.0 - out["width_corr"]) * 6.0] * 3 + [max(tail_rel, -80.0) / 10.0] * 3
    out["_fingerprint"] = np.concatenate([fp, env, extras]).tolist()
    return out


def flags_for(r, args):
    flags = []
    if r["nonfinite"]:
        flags.append("NaN/Inf")
    if r["true_peak_db"] > 0.0:
        flags.append("overs %.1f dBTP" % r["true_peak_db"])
    if abs(r["dc"]) > 0.005:
        flags.append("DC %.3f" % r["dc"])
    if r["sub_corr"] < 0.95:
        flags.append("sub not mono (%.2f)" % r["sub_corr"])
    if r["band_sub"] + r["band_low"] < 0.25:
        flags.append("thin low end")
    if args.target_lufs is not None and abs(r["lufs"] - args.target_lufs) > args.tolerance:
        flags.append("loudness %.1f LUFS" % r["lufs"])
    return flags


def contact_sheet(results, folder, out_png):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    n = len(results)
    cols = 6
    rows = int(math.ceil(n / cols))
    fig, axes = plt.subplots(rows, cols, figsize=(cols * 3.2, rows * 2.0), squeeze=False)
    for ax in axes.flat:
        ax.axis("off")
    for ax, r in zip(axes.flat, results):
        x, sr = sf.read(os.path.join(folder, r["file"]), always_2d=True)
        mono = x.mean(axis=1)
        f, t, s = signal.spectrogram(mono, sr, nperseg=4096, noverlap=3072)
        keep = (f >= 20) & (f <= 12000)
        ax.pcolormesh(t, f[keep], 10 * np.log10(s[keep] + 1e-12), shading="auto", vmin=-110, vmax=-20, cmap="magma")
        ax.set_yscale("log")
        ax.set_ylim(25, 12000)
        ax.set_title(r.get("program", r["file"])[:34], fontsize=7)
        ax.axis("on")
        ax.tick_params(labelsize=5)
    fig.tight_layout()
    fig.savefig(out_png, dpi=90)
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("folder")
    ap.add_argument("--target-lufs", type=float, default=None)
    ap.add_argument("--tolerance", type=float, default=1.5)
    ap.add_argument("--sheet", action="store_true")
    ap.add_argument("--suggest", action="store_true", help="print outGain corrections to hit --target-lufs")
    ap.add_argument("--similar", type=float, default=0.9, help="flag pairs whose fingerprint distance is below this")
    args = ap.parse_args()

    index_path = os.path.join(args.folder, "index.json")
    if os.path.exists(index_path):
        index = json.load(open(index_path))
    else:
        index = [{"file": f, "program": f} for f in sorted(os.listdir(args.folder)) if f.endswith(".wav")]

    results = []
    for item in index:
        r = analyse(os.path.join(args.folder, item["file"]))
        r["program"] = item.get("program", item["file"])
        r["flags"] = flags_for(r, args)
        results.append(r)

    hdr = "%-42s %6s %6s %6s %5s %5s %5s %5s %6s  %s" % ("preset", "TP", "LUFS", "crest", "sub%", "low%", "mid%", "hi%", "width", "flags")
    print(hdr)
    print("-" * len(hdr))
    for r in results:
        mid = r["band_lowmid"] + r["band_mid"]
        hi = r["band_high"] + r["band_air"]
        print("%-42s %6.1f %6.1f %6.1f %5.0f %5.0f %5.0f %5.1f %6.2f  %s" % (
            r["program"][:42], r["true_peak_db"], r["lufs"], r["crest_db"], 100 * r["band_sub"], 100 * r["band_low"],
            100 * mid, 100 * hi, r["width_corr"], ", ".join(r["flags"])))

    # Similarity: nearest neighbour per preset
    fps = np.array([r["_fingerprint"] for r in results])
    if len(fps) > 1:
        d = np.sqrt(((fps[:, None, :] - fps[None, :, :]) ** 2).mean(axis=2))
        np.fill_diagonal(d, np.inf)
        print("\nnearest neighbours (fingerprint RMS distance; < %.2f flagged):" % args.similar)
        pairs = set()
        for i, r in enumerate(results):
            j = int(np.argmin(d[i]))
            r["nearest"] = results[j]["program"]
            r["nearest_dist"] = float(d[i, j])
            if d[i, j] < args.similar:
                pairs.add(tuple(sorted((i, j))))
        for i, j in sorted(pairs):
            print("  TOO SIMILAR: %s  <->  %s  (%.2f)" % (results[i]["program"], results[j]["program"], d[i, j]))
        print("  median nearest distance: %.2f" % float(np.median([r["nearest_dist"] for r in results])))

    if args.suggest and args.target_lufs is not None:
        print("\noutGain corrections (dB) to reach %.1f LUFS:" % args.target_lufs)
        for r in results:
            delta = args.target_lufs - r["lufs"]
            if abs(delta) > 0.3:
                print("  %-42s %+5.1f" % (r["program"][:42], delta))

    for r in results:
        r.pop("_fingerprint", None)
    json.dump(results, open(os.path.join(args.folder, "report.json"), "w"), indent=1)

    if args.sheet:
        out_png = os.path.join(args.folder, "sheet.png")
        contact_sheet(results, args.folder, out_png)
        print("\nspectrogram sheet: " + out_png)

    bad = [r for r in results if any(f.startswith(("NaN", "overs", "DC", "sub not mono")) for f in r["flags"])]
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
