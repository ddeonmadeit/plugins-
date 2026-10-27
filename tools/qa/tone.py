#!/usr/bin/env python3
"""
Tone fingerprint of a window of audio, for matching a preset to a reference sound (e.g. a stem split
out of a track the owner sent, for analysis only):

  python3 tools/qa/tone.py ref_guitar.wav 0.2 5.8          # one window
  python3 tools/qa/tone.py ours.wav 0.2 5.8 --partial 294  # warble measured on the partial near 294 Hz

Prints: spectral centroid, L/R correlation and side/mid energy (width), octave levels from 80 Hz to
10 kHz relative to the loudest, the strongest level modulation between 1 and 12 Hz (tremolo / pump rate
and how far it stands above the rest) with its depth in dB, and the pitch warble (cents, standard
deviation) of one partial (chorus, vibrato, tape wow). Compare the same window of the reference and of
a render of the same notes (harness: --phrase "notes:..." --bpm <tempo>).
"""
import argparse

import numpy as np
import soundfile as sf
from scipy.signal import butter, hilbert, sosfiltfilt


def level_modulation(x, sr):
    sos = butter(4, [200, 3000], "bandpass", fs=sr, output="sos")
    env = np.abs(hilbert(sosfiltfilt(sos, x)))
    hop = int(sr * 0.01)
    e = np.log(env[::hop] + 1e-9)
    idx = np.arange(len(e))
    e = e - np.polyval(np.polyfit(idx, e, 2), idx)        # remove the note's decay
    spec = np.abs(np.fft.rfft(e * np.hanning(len(e))))
    fr = np.fft.rfftfreq(len(e), hop / sr)
    sel = (fr > 1.0) & (fr < 12.0)
    k = np.argmax(spec[sel])
    return fr[sel][k], 20 * np.log10(spec[sel][k] / (np.median(spec[sel]) + 1e-12)), np.std(e) * 8.686


def warble(x, sr, hz):
    sos = butter(4, [hz * 0.95, hz * 1.05], "bandpass", fs=sr, output="sos")
    inst = np.diff(np.unwrap(np.angle(hilbert(sosfiltfilt(sos, x))))) * sr / (2 * np.pi)
    inst = np.convolve(inst, np.ones(int(sr * 0.01)) / int(sr * 0.01), "same")[int(0.3 * sr):-int(0.3 * sr)]
    return 1200 * np.std(np.log2(np.clip(inst, hz * 0.9, hz * 1.1) / hz))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("wav")
    ap.add_argument("start", type=float, nargs="?", default=0.0)
    ap.add_argument("end", type=float, nargs="?", default=None)
    ap.add_argument("--partial", type=float, default=None, help="partial (Hz) for the warble measurement; default: strongest below 2 kHz")
    a = ap.parse_args()

    x, sr = sf.read(a.wav)
    if x.ndim == 1:
        x = np.stack([x, x], 1)
    x = x[int(a.start * sr):int(a.end * sr) if a.end else len(x)]
    mid, side = x.mean(1), (x[:, 0] - x[:, 1]) / 2

    p = np.abs(np.fft.rfft(mid)) ** 2
    f = np.fft.rfftfreq(len(mid), 1 / sr)
    centroid = (f * p).sum() / p.sum()
    edges = [80, 160, 320, 640, 1280, 2560, 5120, 10240]
    octaves = np.array([10 * np.log10(p[(f >= lo) & (f < hi)].sum() + 1e-12) for lo, hi in zip(edges, edges[1:])])
    corr = np.corrcoef(x[:, 0], x[:, 1])[0, 1] if np.std(side) > 1e-9 else 1.0
    partial = a.partial or f[(f > 60) & (f < 2000)][np.argmax(p[(f > 60) & (f < 2000)])]
    rate, prominence, depth = level_modulation(mid, sr)

    print("centroid %.0f Hz   L/R corr %.2f   side/mid %.2f" % (centroid, corr, np.sum(side ** 2) / np.sum(mid ** 2)))
    print("octaves  " + "  ".join("%d:%.0f" % (lo, v) for lo, v in zip(edges, octaves - octaves.max())))
    print("level modulation %.1f Hz (+%.0f dB over the rest), depth %.1f dB" % (rate, prominence, depth))
    print("warble %.0f cents on the partial at %.0f Hz" % (warble(mid, sr, partial), partial))


if __name__ == "__main__":
    main()
