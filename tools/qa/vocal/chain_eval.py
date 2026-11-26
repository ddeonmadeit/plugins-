#!/usr/bin/env python3
"""
Aircheck's dynamics and repair stages on real voices (fetch_vocalset.py first):

  deess    S-band reduction on sibilant frames vs the change on vowels (a lisp), also with the take 14 dB quieter
           (the de-esser judges esses against the voice, so both should match)
  smooth   how much of two planted resonances (3.15 kHz +10 dB, 6.3 kHz +8 dB) Smooth removes vs what it does elsewhere
  comp     per type: static gain at three levels, THD of a 1 kHz and a 100 Hz tone under ~8 dB of gain reduction

    python3 tools/qa/vocal/chain_eval.py [deess|smooth|comp ...] [--harness path]
"""
import argparse
import os

import numpy as np
import soundfile as sf
from scipy.signal import lfilter, welch

from common import FS, OUT, frames, harness, render, take, _hp, _lp
from scipy.signal import sosfilt


def deess(h, out):
    files = ["f1_row_spoken", "m2_row_spoken", "f7_row_spoken", "f7_caro_straight", "m4_caro_straight", "f1_caro_vibrato", "m2_caro_vibrato"]
    for trim in (0, -14):
        for amt in (0.3, 0.6, 0.9):
            sib, voi = [], []
            for f in files:
                o = os.path.join(out, f"ds_{f}_{amt}_{trim}.wav")
                render(take(f), o, [f"deess={amt}", f"inGain={trim}"], h=h)
                x = sf.read(take(f))[0][:, 0] * 10 ** (trim / 20)
                y = sf.read(o)[0][:len(x), 0]
                hx, hy, lx = frames(sosfilt(_hp, x)), frames(sosfilt(_hp, y)), frames(sosfilt(_lp, x))
                lvl = 10 * np.log10(frames(x) + 1e-12)
                red = 10 * np.log10((hy + 1e-14) / (hx + 1e-14))
                sib += list(red[(hx > 2 * lx) & (lvl > np.percentile(lvl, 95) - 30)])
                voi += list(red[(lx > 10 * hx) & (lvl > np.percentile(lvl, 95) - 20)])
            print(f"deess {amt} (take {trim:+d} dB): S band {np.median(sib):5.1f} dB (loudest 10 % {np.percentile(sib, 10):5.1f}), vowels {np.median(voi):+5.2f} dB")


def smooth(h, out):
    x = sf.read(take("medley"))[0]
    def peak(s, f0, g, q):
        A, w = 10 ** (g / 40), 2 * np.pi * f0 / FS
        al = np.sin(w) / (2 * q)
        return lfilter([1 + al * A, -2 * np.cos(w), 1 - al * A], [1 + al / A, -2 * np.cos(w), 1 - al / A], s, axis=0)
    srcs = {"clean": x, "res": peak(peak(x, 3150, 10, 8), 6300, 8, 10)}
    for k, s in srcs.items():
        sf.write(os.path.join(out, f"smooth_{k}.wav"), (s * 0.5 / np.max(np.abs(s))).astype(np.float32), FS, subtype="FLOAT")
    for depth in (0.3, 0.6, 1.0):
        d = {}
        for k in srcs:
            src, o = os.path.join(out, f"smooth_{k}.wav"), os.path.join(out, f"smooth_{k}_{depth}.wav")
            render(src, o, [f"smoothDepth={depth}", "smoothLow=1000", "smoothHigh=12000"], h=h)
            a, b = sf.read(src)[0], sf.read(o)[0]
            f, pa = welch(a[:, 0], FS, nperseg=8192)
            _, pb = welch(b[:len(a), 0], FS, nperseg=8192)
            d[k] = 10 * np.log10(pb + 1e-20) - 10 * np.log10(pa + 1e-20)
        at = lambda fc: d["res"][(f > fc * 2 ** (-1 / 12)) & (f < fc * 2 ** (1 / 12))].mean()
        band = (f > 1200) & (f < 11000)
        away = band & ~((f > 3150 / 1.23) & (f < 3150 * 1.23)) & ~((f > 6300 / 1.23) & (f < 6300 * 1.23))
        print(f"smooth {depth}: resonances {at(3150):5.1f} / {at(6300):5.1f} dB, elsewhere {d['res'][away].mean():5.1f} dB, clean voice {d['clean'][band].mean():5.1f} dB")


def comp(h, out):
    t = np.arange(int(2 * FS)) / FS
    for nm, typ in (("FET", 0), ("Opto", 1), ("Old Limiter", 2), ("VCA", 3)):
        res = []
        for f0 in (1000, 100):
            src = os.path.join(out, f"tone{f0}.wav")
            s = 10 ** (-14 / 20) * np.sqrt(2) * np.sin(2 * np.pi * f0 * t)
            sf.write(src, np.stack([s, s], 1).astype(np.float32), FS, subtype="FLOAT")
            o = os.path.join(out, f"comp_{typ}_{f0}.wav")
            render(src, o, [f"compType={typ}", "compAmount=0.55", "compRatio=4", "compAttack=3", "compRelease=120", "compScHpf=20"], h=h)
            y = sf.read(o)[0][int(FS):int(1.9 * FS), 0] * np.blackman(int(0.9 * FS))
            sp = np.abs(np.fft.rfft(y))
            amp = lambda f: np.sqrt(np.sum(sp[int(round(f / (FS / len(y)))) - 3:int(round(f / (FS / len(y)))) + 4] ** 2))
            res.append(20 * np.log10(np.sqrt(sum(amp(f0 * k) ** 2 for k in range(2, 10) if f0 * k < 20000)) / amp(f0)))
        print(f"comp {nm:12s} THD 1 kHz {res[0]:7.1f} dB, 100 Hz {res[1]:7.1f} dB")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("what", nargs="*", default=["deess", "smooth", "comp"])
    ap.add_argument("--harness", default=harness())
    a = ap.parse_args()
    out = os.path.join(OUT, "chain")
    os.makedirs(out, exist_ok=True)
    for w in a.what:
        {"deess": deess, "smooth": smooth, "comp": comp}[w](a.harness, out)


if __name__ == "__main__":
    main()
