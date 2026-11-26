#!/usr/bin/env python3
"""
Aircheck's pitch engine on real voices (fetch_vocalset.py first). For each condition (pitch shifts, hard and
natural tuning, tuning a take with Amount 0) it renders eight takes and compares them with the dry take using
WORLD (pyworld):

  cents_med / p95   pitch accuracy on steady sung frames (shifts: input x ratio; hard tune: on a semitone at all)
  octave%           steady frames an octave or more off (a grain cut a wrong period long: buzz)
  flips/s           hard tune: changes of note while the singer holds one (warble)
  lost%             frames the output stops being voiced where the input sings
  env_lsd           spectral envelope change, dB (formants kept = small)
  aper              added aperiodicity, dB (roughness, buzz)
  clicks/s          added high-frequency bursts

  python3 tools/qa/vocal/pitch_eval.py [--only shift+7,hard] [--rubberband] [--harness other/Aircheck_Harness]

--rubberband adds Rubber Band R3 (`rubberband --fine -F`, apt rubberband-cli) shifting the same takes: the
reference. Last measured (Nov 2026, Aircheck 1.8): shifts 0.6-1.3 c median, octave errors <= 1.7 %; Rubber
Band 0.5-1.4 c, <= 1.9 %; Aircheck keeps formants closer (env 3.3-5.4 dB vs 4.7-7.0).
"""
import argparse
import os
import subprocess
from concurrent.futures import ProcessPoolExecutor

import numpy as np
import pyworld as pw
import soundfile as sf
from scipy.signal import butter, sosfilt

from common import FS, OUT, harness, neutral_preset, render, take

FILES = ["m2_row_straight", "f7_row_vibrato", "m4_caro_straight", "f1_arpeggios_breathy_i", "m2_arpeggios_vocal_fry_a",
         "f1_row_spoken", "m2_arpeggios_belt_a", "f7_caro_straight"]
CONDS = {
    "shift+3": (["pitchShift=3"], 3), "shift-3": (["pitchShift=-3"], -3), "shift+7": (["pitchShift=7"], 7),
    "shift-7": (["pitchShift=-7"], -7), "shift+12": (["pitchShift=12"], 12), "shift-12": (["pitchShift=-12"], -12),
    "hard": (["tuneOn=1", "tuneSpeed=0", "tuneHumanize=0", "tuneVibrato=0"], "hard"),
    "natural": (["tuneOn=1", "tuneSpeed=45", "tuneHumanize=0.3"], None),
    "tune0": (["tuneOn=1", "tuneAmount=0"], 0),
}
RB = {f"rb{s:+d}": ([str(s)], s) for s in (3, -3, 7, -7, 12, -12)}


def analyse(x):
    f0, t = pw.harvest(x.astype(np.float64), FS, frame_period=5.0, f0_floor=60, f0_ceil=1100)
    return f0, pw.cheaptrick(x.astype(np.float64), f0, t, FS), pw.d4c(x.astype(np.float64), f0, t, FS)


def hf_clicks(x):
    h = sosfilt(butter(4, 5000, "high", fs=FS, output="sos"), x)
    w = int(0.0025 * FS)
    n = len(h) // w
    e = 10 * np.log10(np.mean(h[:n * w].reshape(n, w) ** 2, axis=1) + 1e-14)
    c = sum(1 for i in range(10, n - 10) if e[i] > np.median(np.r_[e[i - 10:i - 1], e[i + 2:i + 10]]) + 15 and e[i] > -70)
    return c / (len(x) / FS)


def job(item):
    name, cond, sets, kind, h, outdir = item
    src = take(name)
    out = os.path.join(outdir, f"{name}_{cond}.wav")
    if cond.startswith("rb"):
        subprocess.run(["rubberband", "--fine", "-F", "-p", sets[0], src, out], check=True, capture_output=True)
    else:
        render(src, out, sets, h=h)
    x = sf.read(src)[0][:, 0]
    y = sf.read(out)[0]
    y = (y[:, 0] if y.ndim > 1 else y)[:len(x)]
    y = np.pad(y, (0, len(x) - len(y)))
    (f0i, spi, api), (f0o, spo, apo) = analyse(x), analyse(y)
    n = min(len(f0i), len(f0o))
    f0i, f0o, spi, spo, api, apo = f0i[:n], f0o[:n], spi[:n], spo[:n], api[:n], apo[:n]
    stable = f0i > 0
    for d in range(1, 7):
        stable[d:] &= f0i[:-d] > 0
        stable[:-d] &= f0i[d:] > 0
    both = stable & (f0o > 0)
    midi = 69 + 12 * np.log2(np.maximum(f0i, 1) / 440)
    steady = both.copy()
    for d in range(1, 6):
        steady[d:] &= np.abs(midi[d:] - midi[:-d]) < 0.4
        steady[:-d] &= np.abs(midi[:-d] - midi[d:]) < 0.4
    res = {"lost%": 100 * (1 - both.sum() / max(1, stable.sum()))}
    want = None
    if isinstance(kind, (int, float)):
        want = f0i * 2 ** (kind / 12)
    elif kind == "hard":
        mo = 69 + 12 * np.log2(np.maximum(f0o, 1) / 440)
        want = 440 * 2 ** ((np.round(mo) - 69) / 12)
        notes = np.round(mo[steady])
        res["flips/s"] = float(np.sum(np.diff(notes) != 0) / (steady.sum() * 0.005 + 1e-9))
    if want is not None and steady.any():
        c = 1200 * np.log2(f0o[steady] / want[steady])
        res["octave%"] = float(100 * np.mean(np.abs(c) > 600))
        c = np.abs(c[np.abs(c) < 600])
        res["cents_med"], res["cents_p95"] = float(np.median(c)), float(np.percentile(c, 95))
    fr = np.fft.rfftfreq((spi.shape[1] - 1) * 2, 1 / FS)
    band = (fr > 100) & (fr < 5000)
    d = 10 * np.log10(spo[both][:, band] + 1e-20) - 10 * np.log10(spi[both][:, band] + 1e-20)
    d -= d.mean(axis=1, keepdims=True)
    res["env_lsd"] = float(np.mean(np.sqrt(np.mean(d ** 2, axis=1))))
    ab = (fr > 100) & (fr < 4000)
    res["aper"] = float(np.mean(20 * np.log10(apo[both][:, ab] + 1e-6)) - np.mean(20 * np.log10(api[both][:, ab] + 1e-6)))
    res["clicks/s"] = hf_clicks(y) - hf_clicks(x)
    return cond, res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="")
    ap.add_argument("--rubberband", action="store_true")
    ap.add_argument("--harness", default=harness())
    a = ap.parse_args()
    conds = dict(CONDS, **(RB if a.rubberband else {}))
    if a.only:
        conds = {k: v for k, v in conds.items() if k in a.only.split(",")}
    outdir = os.path.join(OUT, "pitch")
    os.makedirs(outdir, exist_ok=True)
    neutral_preset()
    items = [(f, c, s, k, a.harness, outdir) for c, (s, k) in conds.items() for f in FILES]
    with ProcessPoolExecutor(4) as ex:
        rows = list(ex.map(job, items))
    keys = ["flips/s", "cents_med", "cents_p95", "octave%", "lost%", "env_lsd", "aper", "clicks/s"]
    print(f"{'condition':10s} " + " ".join(f"{k:>10s}" for k in keys))
    for c in conds:
        rs = [r for cc, r in rows if cc == c]
        print(f"{c:10s} " + " ".join(f"{np.mean([r[k] for r in rs if k in r]):10.2f}" if any(k in r for r in rs) else f"{'-':>10s}" for k in keys))


if __name__ == "__main__":
    main()
