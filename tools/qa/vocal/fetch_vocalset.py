#!/usr/bin/env python3
"""
Fetch the real-vocal test takes: a few dozen recordings from VocalSet (Wilkins et al. 2018, CC BY 4.0,
https://zenodo.org/records/1442513): male and female singers, straight / vibrato / breathy / belt / vocal fry,
sung and spoken. Only the needed members are read out of the 2 GB archive (HTTP range requests). They become
48 kHz stereo at a tracked-vocal level (-20 dBFS RMS while singing) in $VOCAL_TAKES (default qa-out/vocals),
plus medley.wav (a bit of each). For analysis only: never ship them.

    python3 tools/qa/vocal/fetch_vocalset.py
"""
import os
import struct
import urllib.request
import zlib

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

from common import FS, TAKES

URL = "https://zenodo.org/api/records/1442513/files/VocalSet11.zip/content"
WANT = {
    "FULL/male2/excerpts/straight/m2_row_straight.wav", "FULL/male2/excerpts/vibrato/m2_row_vibrato.wav",
    "FULL/male2/excerpts/straight/m2_caro_straight.wav", "FULL/male2/excerpts/vibrato/m2_caro_vibrato.wav",
    "FULL/male2/excerpts/spoken/m2_row_spoken.wav", "FULL/male4/excerpts/straight/m4_caro_straight.wav",
    "FULL/male4/excerpts/vibrato/m4_caro_vibrato.wav", "FULL/male4/excerpts/straight/m4_row_straight.wav",
    "FULL/male4/excerpts/vibrato/m4_row_vibrato.wav", "FULL/female1/excerpts/spoken/f1_row_spoken.wav",
    "FULL/female1/excerpts/vibrato/f1_caro_vibrato.wav", "FULL/female1/excerpts/straight/f1_caro_straight.wav",
    "FULL/female1/excerpts/straight/f1_row_straight.wav", "FULL/female1/excerpts/vibrato/f1_row_vibrato.wav",
    "FULL/female7/excerpts/vibrato/f7_row_vibrato.wav", "FULL/female7/excerpts/straight/f7_row_straight.wav",
    "FULL/female7/excerpts/straight/f7_caro_straight.wav", "FULL/female7/excerpts/vibrato/f7_caro_vibrato.wav",
    "FULL/female7/excerpts/spoken/f7_row_spoken.wav",
    "FULL/female1/arpeggios/breathy/f1_arpeggios_breathy_i.wav", "FULL/female1/arpeggios/breathy/f1_arpeggios_breathy_o.wav",
    "FULL/male2/arpeggios/vocal_fry/m2_arpeggios_vocal_fry_a.wav", "FULL/male2/arpeggios/vocal_fry/m2_arpeggios_vocal_fry_o.wav",
    "FULL/male2/arpeggios/belt/m2_arpeggios_belt_a.wav", "FULL/male2/arpeggios/belt/m2_arpeggios_belt_o.wav",
}
MEDLEY = [("m2_row_straight", 7), ("f7_row_vibrato", 7), ("m4_caro_vibrato", 8), ("f1_arpeggios_breathy_i", 5),
          ("m2_arpeggios_vocal_fry_a", 5), ("m2_arpeggios_belt_a", 5), ("f1_row_spoken", 5), ("m2_row_spoken", 5)]


def rng(a, b):
    with urllib.request.urlopen(urllib.request.Request(URL, headers={"Range": f"bytes={a}-{b}"}), timeout=120) as r:
        return r.read()


def members():
    with urllib.request.urlopen(urllib.request.Request(URL, method="HEAD"), timeout=60) as r:
        n = int(r.headers["Content-Length"])
    tail = rng(max(0, n - 70000), n - 1)
    i = tail.rfind(b"PK\x05\x06")
    cd_size, cd_off = struct.unpack("<II", tail[i + 12:i + 20])
    cd, p, out = rng(cd_off, cd_off + cd_size - 1), 0, {}
    while p < len(cd) and cd[p:p + 4] == b"PK\x01\x02":
        meth, = struct.unpack("<H", cd[p + 10:p + 12])
        csize, = struct.unpack("<I", cd[p + 20:p + 24])
        nl, el, cl = struct.unpack("<HHH", cd[p + 28:p + 34])
        off, = struct.unpack("<I", cd[p + 42:p + 46])
        out[cd[p + 46:p + 46 + nl].decode("utf-8", "replace")] = (meth, csize, off)
        p += 46 + nl + el + cl
    return out


def active_rms(x):
    w = int(0.05 * FS)
    r = np.sqrt(np.mean(x[:len(x) // w * w].reshape(-1, w) ** 2, axis=1))
    return np.sqrt(np.mean(r[r > r.max() * 10 ** (-30 / 20)] ** 2))


def main():
    os.makedirs(TAKES, exist_ok=True)
    for name, (meth, csize, off) in members().items():
        if name not in WANT:
            continue
        dst = os.path.join(TAKES, os.path.basename(name))
        if os.path.exists(dst):
            continue
        h = rng(off, off + 29)
        nl, el = struct.unpack("<HH", h[26:30])
        data = rng(off + 30 + nl + el, off + 30 + nl + el + csize - 1)
        tmp = dst + ".src.wav"
        open(tmp, "wb").write(zlib.decompress(data, -15) if meth == 8 else data)
        x, sr = sf.read(tmp)
        os.remove(tmp)
        x = x.mean(axis=1) if x.ndim > 1 else x
        x = resample_poly(x, FS // 100, sr // 100) if sr != FS else x
        x = (x - x.mean()) * 10 ** (-20 / 20) / active_rms(x)
        x *= min(1.0, 0.7 / np.max(np.abs(x)))
        sf.write(dst, np.stack([x, x], 1).astype(np.float32), FS, subtype="FLOAT")
        print(dst)
    parts = []
    for name, sec in MEDLEY:
        x, _ = sf.read(os.path.join(TAKES, name + ".wav"))
        parts += [x[:int(sec * FS)], np.zeros((int(0.4 * FS), 2))]
    sf.write(os.path.join(TAKES, "medley.wav"), np.concatenate(parts).astype(np.float32), FS, subtype="FLOAT")
    print("medley.wav")


if __name__ == "__main__":
    main()
