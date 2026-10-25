#!/usr/bin/env python3
"""
How different do the presets sound from each other? A musical counterpart to analyze.py's spectral
fingerprint: it compares how each preset *plays*, not just its average spectrum.

  python3 tools/qa/variety.py qa-out/Afterglow/renders                 # one bank
  python3 tools/qa/variety.py old/renders qa-out/Afterglow/renders     # before / after, on one scale

Renders should use a phrase with a held chord, an arpeggio and a tail (Afterglow: --phrase mixed).
Features per render: attack time, sustain, articulation, tail, brightness, noisiness, low end, width,
level movement and spectral flux, plus the spectrum's shape in octave bands (half weight). They are
z-scored over every set passed, and the report lists each bank's median nearest-neighbour distance
and its closest pairs. Afterglow 1.0 measured 1.29; the 1.1 bank 1.9. A pair under ~0.8 usually
sounds like the same patch with different effects: change its source, envelope or register.
"""
import glob
import os
import sys

import numpy as np
import soundfile as sf
from scipy.signal import stft

NAMES = ["attack", "sustain", "artic", "tail", "bright", "noisy", "low", "width", "move", "flux"]


def features(path):
    x, sr = sf.read(path)
    if x.ndim == 1:
        x = np.stack([x, x], 1)
    mid, side = (x[:, 0] + x[:, 1]) / 2, (x[:, 0] - x[:, 1]) / 2
    hop = int(sr * 0.01)
    frames = len(mid) // hop
    e = np.sqrt(np.array([np.mean(mid[i * hop:(i + 1) * hop] ** 2) for i in range(frames)]) + 1e-12)
    t = np.arange(frames) * 0.01

    def sec(a, b):
        return e[(t >= a) & (t < b)]

    first = sec(0, 3.85)
    pk = first.max() + 1e-9
    attack = np.log10(0.005 + t[np.argmax(first >= 0.9 * pk)])
    sustain = sec(3.0, 3.8).mean() / pk
    artic = np.std(np.diff(20 * np.log10(sec(4.0, 8.0) + 1e-6)))
    tail = 20 * np.log10(sec(16.5, 19.5).mean() / (e.max() + 1e-9) + 1e-6)

    f, _, Z = stft(mid, sr, nperseg=2048, noverlap=1024)
    S = np.abs(Z) ** 2 + 1e-12
    ps = S.sum(1)
    centroid = np.log2((f * ps).sum() / ps.sum() + 1)
    active = S[:, S.sum(0) > S.sum(0).max() * 1e-3]
    flat = np.mean(np.exp(np.mean(np.log(active), 0)) / np.mean(active, 0))
    low = ps[f < 200].sum() / ps.sum()
    width = np.sum(side ** 2) / (np.sum(mid ** 2) + 1e-12)
    move = np.std(20 * np.log10(sec(0.6, 3.6) + 1e-6))
    held = S[:, int(0.6 * sr / 1024):int(3.6 * sr / 1024)]
    flux = np.mean(np.abs(np.diff(np.log(held + 1e-9), axis=1)))

    edges = [0, 125, 250, 500, 1000, 2000, 4000, 8000, 24000]
    bands = np.log10(np.array([ps[(f >= a) & (f < b)].sum() for a, b in zip(edges, edges[1:])]) + 1e-12)
    return np.concatenate([[attack, sustain, artic, tail, centroid, flat, low, width, move, flux], bands - bands.mean()])


def report(label, names, Z):
    D = np.sqrt(((Z[:, None, :] - Z[None, :, :]) ** 2).sum(-1))
    np.fill_diagonal(D, np.inf)
    nn = D.min(1)
    print("%s: median nearest-neighbour distance %.2f, min %.2f (%d presets)" % (label, np.median(nn), nn.min(), len(names)))
    shown = set()
    for i in np.argsort(nn)[:10]:
        j = int(np.argmin(D[i]))
        key = tuple(sorted((i, j)))
        if key not in shown:
            shown.add(key)
            print("  %.2f  %s  <->  %s" % (D[i, j], names[i], names[j]))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    sets = []
    for folder in sys.argv[1:]:
        files = sorted(glob.glob(os.path.join(folder, "*.wav")))
        sets.append(([os.path.splitext(os.path.basename(f))[0] for f in files], np.array([features(f) for f in files])))
    allF = np.concatenate([F for _, F in sets])
    mu, sd = allF.mean(0), allF.std(0) + 1e-9   # one scale for every set, so banks can be compared
    for folder, (names, F) in zip(sys.argv[1:], sets):
        w = np.array([1.0] * len(NAMES) + [0.5] * (F.shape[1] - len(NAMES)))
        report(folder.rstrip("/"), names, w * (F - mu) / sd)


if __name__ == "__main__":
    main()
