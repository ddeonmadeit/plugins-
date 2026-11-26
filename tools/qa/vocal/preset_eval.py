#!/usr/bin/env python3
"""
Every Aircheck factory preset on the real-vocal medley (fetch_vocalset.py first): loudness, sibilance (S band vs
vowels; the dry takes read about -14 dB, a bright modern lead about -10, smooth styles about -11.5) and the
preset's effective EQ. The EQ is measured frame by frame against the dry take, normalised to 500 Hz - 2 kHz,
so a compressor that changes how loud each note is does not pose as tone.

    python3 tools/qa/vocal/preset_eval.py [--harness path] [--input take.wav]
"""
import argparse
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

import numpy as np
import soundfile as sf

from common import FS, OUT, harness, render, sibilance_db, take

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from analyze import integrated_lufs  # noqa: E402

EDGES = [88, 177, 354, 707, 1414, 2828, 5657, 11314, 20000]
BANDS = [125, 250, 500, 1000, 2000, 4000, 8000, 16000]


def bandframes(y, n=2400):
    k = len(y) // n
    spec = np.abs(np.fft.rfft(y[:k * n].reshape(k, n) * np.hanning(n), axis=1)) ** 2
    f = np.fft.rfftfreq(n, 1 / FS)
    return np.stack([10 * np.log10(spec[:, (f >= EDGES[i]) & (f < EDGES[i + 1])].sum(1) + 1e-14) for i in range(8)], 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--harness", default=harness())
    ap.add_argument("--input", default=None)
    a = ap.parse_args()
    inp = a.input or take("medley")
    out = os.path.join(OUT, "presets")
    os.makedirs(out, exist_ok=True)
    names = [l.split("\t", 1)[1] for l in subprocess.check_output([a.harness, "list"], text=True).strip().split("\n")]
    with ThreadPoolExecutor(4) as ex:
        list(ex.map(lambda i: render(inp, os.path.join(out, f"{i:02d}.wav"), program=i, h=a.harness), range(len(names))))
    x = sf.read(inp)[0]
    X = bandframes(x.mean(axis=1))
    lvl = 10 * np.log10(np.sum(10 ** (X / 10), 1))
    act = lvl > lvl.max() - 30
    print(f"{'preset':40s}  LUFS  sibil |" + " ".join(f"{b:>6d}" for b in BANDS))
    print(f"{'(dry take)':40s} {integrated_lufs(x, FS):5.1f} {sibilance_db(x):6.1f} |")
    for i, nm in enumerate(names):
        y = sf.read(os.path.join(out, f"{i:02d}.wav"))[0][:len(x)]
        Y = bandframes(y.mean(axis=1))
        n = min(len(X), len(Y))
        d = Y[:n] - X[:n]
        eq = np.median((d - d[:, 2:5].mean(1, keepdims=True))[act[:n]], 0)
        print(f"{nm:40s} {integrated_lufs(y, FS):5.1f} {sibilance_db(y):6.1f} |" + " ".join(f"{v:6.1f}" for v in eq))


if __name__ == "__main__":
    main()
