#!/usr/bin/env python3
"""
Velocity response of a drum instrument from a harness "drumvel" render: every GM kit note hit 8
times at velocities 16, 32 ... 127, 0.45 s apart, 0.6 s between notes (see HarnessMain.cpp).

  $H render --program 0 --phrase drumvel --out /tmp/v.wav
  python3 tools/qa/drum_velocity.py /tmp/v.wav

Prints the attack level (first 60 ms, minus the power of whatever was still ringing just before
the hit) per velocity. Expect roughly 40*log10(vel/127) dB: ~-24 dB at 32, ~-4 dB at 100, rising
monotonically. Flat rows mean velocity barely changes anything; jumps mean a gap in the layers.
"""
import sys

import numpy as np
import soundfile as sf

NOTES = [(36, "kick"), (38, "snare"), (40, "rimshot"), (37, "sidestick"), (42, "hat closed"), (46, "hat open"),
         (44, "hat pedal"), (48, "tom 1"), (45, "tom 2"), (43, "floor tom"), (49, "crash 1"), (57, "crash 2"),
         (51, "ride"), (53, "ride bell"), (52, "china")]
HIT_GAP, NOTE_GAP, HITS = 0.45, 0.6, 8


def main():
    x, sr = sf.read(sys.argv[1], always_2d=True)
    mono = x.mean(axis=1)
    t = 0.0
    print("%-11s %s   range  monotonic" % ("", " ".join("%6d" % min(127, 16 * (i + 1)) for i in range(HITS))))
    for note, name in NOTES:
        levels = []
        for _ in range(HITS):
            a = int(t * sr)
            hit = mono[a:a + int(0.06 * sr)]
            before = mono[max(0, a - int(0.03 * sr)):max(0, a - int(0.002 * sr))]
            power = np.mean(hit ** 2) - (np.mean(before ** 2) if len(before) else 0.0)
            levels.append(10 * np.log10(max(power, 1e-12)))
            t += HIT_GAP
        t += NOTE_GAP
        ok = all(b > a - 0.5 for a, b in zip(levels[1:], levels[2:]))   # first hit can sit on the last note's tail
        print("%-11s %s  %5.1f  %s" % (name, " ".join("%6.1f" % v for v in levels), levels[-1] - levels[1],
                                        "yes" if ok else "NO"))


if __name__ == "__main__":
    main()
