#!/usr/bin/env python3
"""
Long-term tonal balance of renders in 2/3-octave bands, relative to the loudest band. For judging a
tone against a reference shape (e.g. an amped guitar: about flat 160 Hz - 2.5 kHz, -20 dB by 6 kHz,
-50 dB by 10 kHz) rather than just loudness.

  python3 tools/qa/spectrum.py qa-out/Afterglow/renders/*GUITAR*.wav
"""
import os
import sys

import numpy as np
import soundfile as sf

CENTRES = [63, 100, 160, 250, 400, 630, 1000, 1600, 2500, 4000, 6300, 10000, 16000]


def balance(path):
    x, sr = sf.read(path)
    m = x.mean(1) if x.ndim > 1 else x
    power = np.abs(np.fft.rfft(m)) ** 2
    freqs = np.fft.rfftfreq(len(m), 1 / sr)
    levels = np.array([10 * np.log10(power[(freqs >= c / 2 ** (1 / 3)) & (freqs < c * 2 ** (1 / 3))].sum() + 1e-12)
                       for c in CENTRES])
    return levels - levels.max()


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    print("%-30s" % "band Hz" + " ".join("%6d" % c for c in CENTRES))
    for path in sys.argv[1:]:
        name = os.path.splitext(os.path.basename(path))[0]
        print("%-30s" % name[:30] + " ".join("%6.1f" % v for v in balance(path)))


if __name__ == "__main__":
    main()
