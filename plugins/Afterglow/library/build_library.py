#!/usr/bin/env python3
"""
Build Afterglow's live-instrument sample library from VSCO 2 CE and VCSL (CC0 1.0, Versilian Studios).

  python3 plugins/Afterglow/library/build_library.py --kits <dir> --out <library-dir> [--download]
                                                     [--only id,id] [--discard-kits] [--jobs N]

Same arguments as the other sample-library builders, so CI treats every library alike. sources.json lists
every file (repo, path at a pinned commit, true MIDI note, dynamic layer, round robin, md5); --download
fetches whatever is missing from raw.githubusercontent.com and checks it.

Per instrument the builder writes 16-bit stereo FLAC files and a catalog entry per sample:
  note        the true MIDI note (the file names are mostly an octave low; sources.json is already fixed)
  dyn         dynamic layer (1 = softest)
  loudness    attack loudness after the instrument's gain, for velocity mapping
  cents       measured tuning error; the voice plays it back in tune
  loopStart / loopEnd  for sustaining instruments: a loop in the steady part, a whole number of pitch
              periods long, ending where the waveform best matches its start, with a crossfade baked into
              the samples before loopEnd so the voice can jump back seamlessly and hold the note forever

Below 150 Hz every sample is folded to mono (zero-phase high-pass on the side), so the lows of a stereo
recording never smear. One-shot instruments (piano, harp, mallets, pizzicato) keep their natural decay, trimmed below -64 dB and
capped with a fade. Every instrument gets one gain so its loudest layer lands at the same loudness; layers
keep their recorded dynamics relative to each other.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import urllib.parse
from concurrent.futures import ProcessPoolExecutor

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))


def md5(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def source_path(kits_dir, f):
    return os.path.join(kits_dir, "afterglow-src", f["repo"], f["path"])


def ensure_files(sources, instruments, kits_dir, download):
    missing = []
    for inst in instruments:
        for f in inst["files"]:
            p = source_path(kits_dir, f)
            if not (os.path.exists(p) and md5(p) == f["md5"]):
                missing.append(f)
    if not missing:
        return
    if not download:
        sys.exit("%d source files missing (pass --download to fetch them), e.g. %s" % (len(missing), source_path(kits_dir, missing[0])))
    print("downloading %d files" % len(missing), flush=True)

    def fetch(f):
        repo = sources["repos"][f["repo"]]
        url = "https://raw.githubusercontent.com/%s/%s/%s" % (repo["name"], repo["commit"], urllib.parse.quote(f["path"]))
        p = source_path(kits_dir, f)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        subprocess.run(["curl", "-sSL", "--retry", "5", "-o", p, url], check=True)
        if md5(p) != f["md5"]:
            sys.exit("checksum mismatch for " + p)

    from concurrent.futures import ThreadPoolExecutor
    with ThreadPoolExecutor(8) as ex:
        list(ex.map(fetch, missing))


# ---------------------------------------------------------------------------------------------------------

def resample(x, sr, target):
    if sr == target:
        return x
    from math import gcd
    from scipy.signal import resample_poly
    g = gcd(int(target), int(sr))
    return resample_poly(x, int(target) // g, int(sr) // g, axis=0)


def yin_hz(x, sr, fmin=25.0, fmax=2500.0):
    """Fundamental of a steady stretch (median of a few YIN frames), or None."""
    n = len(x)
    w = min(4096, n // 2)
    if w < 512:
        return None
    tmax = min(int(sr / fmin), w - 1)
    tmin = max(2, int(sr / fmax))
    est = []
    for start in np.linspace(0, n - 2 * w, 5).astype(int):
        seg = x[start:start + 2 * w]
        d = np.array([np.dot(seg[:w] - seg[t:t + w], seg[:w] - seg[t:t + w]) for t in range(tmax + 1)])
        cm = d.copy()
        cm[0] = 1.0
        cm[1:] = d[1:] * np.arange(1, tmax + 1) / (np.cumsum(d[1:]) + 1e-12)
        t = next((k for k in range(tmin, tmax) if cm[k] < 0.15 and cm[k] < cm[k + 1]), None)
        if t is None:
            t = tmin + int(np.argmin(cm[tmin:tmax]))
        if 1 <= t < tmax:
            a, b, c = cm[t - 1], cm[t], cm[t + 1]
            t = t + 0.5 * (a - c) / (a - 2 * b + c + 1e-12)
        est.append(sr / t)
    return float(np.median(est))


def measure_cents(mono, sr, note):
    """Tuning error in cents against the file's note; 0 when the estimate is not trustworthy."""
    pk = int(np.argmax(np.abs(mono)))
    seg = mono[pk + int(0.05 * sr): pk + int(0.6 * sr)]
    if len(seg) < 4096:
        return 0.0
    from scipy.signal import resample_poly
    hz = yin_hz(resample_poly(seg, 1, 2), sr / 2.0)
    if not hz:
        return 0.0
    cents = 1200.0 * np.log2(hz / (440.0 * 2 ** ((note - 69) / 12.0)))
    return float(cents) if abs(cents) < 60.0 else 0.0


def mono_lows(x, sr, hz=150.0):
    """Lows stay mono (the house rule for every plugin): high-pass the side signal, zero-phase, below `hz`."""
    from scipy.signal import butter, sosfiltfilt
    mid, side = 0.5 * (x[:, 0] + x[:, 1]), 0.5 * (x[:, 0] - x[:, 1])
    side = sosfiltfilt(butter(2, hz, "high", fs=sr, output="sos"), side)
    return np.stack([mid + side, mid - side], axis=1)


def trim_start(x, pre_roll):
    mono = np.abs(x).max(axis=1)
    onset = int(np.argmax(mono > mono.max() * 0.01))
    return x[max(0, onset - pre_roll):]


def one_shot(x, sr, floor_db, max_seconds):
    """Natural decay: cut below the floor (+30 ms), cap with a fade."""
    env = np.abs(x).max(axis=1)
    floor = env.max() * 10 ** (floor_db / 20.0)
    loud = np.nonzero(env > floor)[0]
    end = min(len(x), (loud[-1] if len(loud) else len(x)) + int(0.03 * sr))
    cap = int(max_seconds * sr)
    capped = end > cap
    x = x[:min(end, cap)].copy()
    fade = min(len(x) // 3, int((0.6 if capped else 0.03) * sr))
    if fade > 0:
        x[-fade:] *= (0.5 + 0.5 * np.cos(np.linspace(0, np.pi, fade)))[:, None]
    return x, None


def make_loop(x, sr, note, cents, proc):
    """A seamless loop in the steady part: whole pitch periods long, ending where the waveform best matches
    the loop start, the crossfade baked in before loopEnd. Returns (samples up to loopEnd + guard, (s, e))."""
    mono = x.mean(axis=1)
    env = np.convolve(np.abs(mono), np.ones(256) / 256, mode="same")
    first = env[:int(1.0 * sr)]
    attack_end = int(np.argmax(first > 0.9 * first.max())) if len(first) else 0
    xfade = int(proc["crossfadeMs"] * 0.001 * sr)
    s_min = max(attack_end + int(0.15 * sr), int(0.3 * sr), xfade + 16)
    period = sr / (440.0 * 2 ** ((note - 69 + cents / 100.0) / 12.0))
    want = proc["loopSeconds"] * sr
    avail_end = min(len(x) - 64, int(proc["loopedSeconds"] * sr))
    length = int(round(min(want, max(0.25 * sr, avail_end - s_min - 64)) / period) * period)
    if length < period * 8 or s_min + length >= avail_end:
        return one_shot(x, sr, proc["tailFloorDb"], proc["loopedSeconds"])

    # Best end point: windows before e and before s = e - length look most alike.
    w = 2048
    best, best_e = -2.0, None
    hop = max(16, int(period / 4))
    for e in range(s_min + length, avail_end, hop):
        s = e - length
        a, b = mono[e - w:e], mono[s - w:s]
        if s - w < 0:
            continue
        ncc = float(np.dot(a, b) / (np.sqrt(np.dot(a, a) * np.dot(b, b)) + 1e-12))
        if ncc > best:
            best, best_e = ncc, e
    if best_e is None:
        return one_shot(x, sr, proc["tailFloorDb"], proc["loopedSeconds"])
    e = best_e
    s = e - length
    y = x[:e + 64].copy()
    # Equal-gain raised-cosine crossfade: the last xfade samples before e turn into the ones before s, so
    # playing on from s after e continues the waveform without a seam.
    ramp = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, xfade))[:, None]
    y[e - xfade:e] = x[e - xfade:e] * (1.0 - ramp) + x[s - xfade:s] * ramp
    y[e:e + 64] = x[s:s + 64]   # interpolation guard: what follows the loop end is the loop start
    return y, (s, e)


def build_instrument(job):
    inst, kits_dir, out_dir, proc = job
    sr = proc["sampleRate"]
    pre_roll = int(proc["preRollMs"] * 0.001 * sr)
    items = []
    for f in inst["files"]:
        x, fsr = sf.read(source_path(kits_dir, f), dtype="float64", always_2d=True)
        if x.shape[1] == 1:
            x = np.repeat(x, 2, axis=1)
        x = mono_lows(resample(x[:, :2], fsr, sr), sr)
        x = trim_start(x, pre_roll)
        cents = measure_cents(x.mean(axis=1), sr, f["note"])
        if inst["loop"]:
            y, loop = make_loop(x, sr, f["note"], cents, proc)
        else:
            y, loop = one_shot(x, sr, proc["tailFloorDb"], inst.get("maxSeconds", 5.0))
        y[:16] *= np.linspace(0, 1, 16)[:, None]
        if loop:
            # A sustain is heard as its steady part (a slow organ or bowed attack would otherwise read quiet).
            loudness = 20 * np.log10(np.sqrt(np.mean(y[loop[0]:loop[1]] ** 2)) + 1e-12)
        else:
            n_loud = int(proc["loudnessMs"] * 0.001 * sr)
            loudness = 20 * np.log10(np.sqrt(np.mean(y[:n_loud] ** 2)) + 1e-12)
        items.append(dict(f=f, y=y, loop=loop, cents=cents, loudness=loudness))

    # One gain per instrument: its loudest layer (median over notes) lands on the target loudness.
    top = max(it["f"]["dyn"] for it in items)
    ref = float(np.median([it["loudness"] for it in items if it["f"]["dyn"] == top]))
    gain = 10 ** ((proc["targetLoudnessDb"] - ref) / 20.0)
    peak = max(np.abs(it["y"]).max() for it in items) * gain
    ceiling = 10 ** (proc["peakCeilingDb"] / 20.0)
    if peak > ceiling:
        gain *= ceiling / peak

    zones = []
    for it in items:
        f = it["f"]
        rel = "sounds/%s/%03d_%d_%d.flac" % (inst["id"], f["note"], f["dyn"], f["rr"])
        path = os.path.join(out_dir, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        sf.write(path, np.clip(it["y"] * gain, -1.0, 1.0), sr, subtype="PCM_16", format="FLAC")
        z = {"file": rel, "note": f["note"], "dyn": f["dyn"], "rr": f["rr"], "frames": int(len(it["y"])),
             "loudness": round(float(it["loudness"] + 20 * np.log10(gain)), 2), "cents": round(it["cents"], 1)}
        if it["loop"]:
            z["loopStart"], z["loopEnd"] = int(it["loop"][0]), int(it["loop"][1])
        zones.append(z)
    looped = sum(1 for z in zones if "loopStart" in z)
    print("%-10s %3d samples (%d looped)  gain %+.1f dB" % (inst["id"], len(zones), looped, 20 * np.log10(gain)), flush=True)
    return {"id": inst["id"], "name": inst["name"], "family": inst["family"], "loop": inst["loop"], "sampleRate": sr,
            "zones": sorted(zones, key=lambda z: (z["note"], z["dyn"], z["rr"]))}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kits", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--download", action="store_true")
    ap.add_argument("--only", default="")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--discard-kits", action="store_true", help="delete the downloaded sources once built")
    args = ap.parse_args()

    sources = json.load(open(os.path.join(HERE, "sources.json")))
    only = set(filter(None, args.only.split(",")))
    instruments = [i for i in sources["instruments"] if not only or i["id"] in only]
    if os.path.exists(args.out) and not only:
        shutil.rmtree(os.path.join(args.out, "sounds"), ignore_errors=True)
    os.makedirs(args.out, exist_ok=True)

    ensure_files(sources, instruments, args.kits, args.download)
    with ProcessPoolExecutor(max_workers=args.jobs) as pool:
        entries = list(pool.map(build_instrument, [(i, args.kits, args.out, sources["processing"]) for i in instruments]))
    if args.discard_kits:
        shutil.rmtree(os.path.join(args.kits, "afterglow-src"), ignore_errors=True)

    catalog_path = os.path.join(args.out, "catalog.json")
    if only and os.path.exists(catalog_path):
        old = {e["id"]: e for e in json.load(open(catalog_path))["instruments"]}
        old.update({e["id"]: e for e in entries})
        entries = list(old.values())
    order = [i["id"] for i in sources["instruments"]]
    entries.sort(key=lambda e: order.index(e["id"]) if e["id"] in order else len(order))
    json.dump({"version": 1, "credit": "VSCO 2 CE and VCSL by Versilian Studios LLC, CC0 1.0",
               "instruments": entries}, open(catalog_path, "w"), indent=1)

    with open(os.path.join(args.out, "ATTRIBUTION.txt"), "w") as f:
        f.write("Afterglow live-instrument library\n=================================\n\n"
                "Processed (trimmed, looped, level-matched, converted to FLAC) from:\n\n"
                "- VSCO 2: Community Edition, Versilian Studios LLC. CC0 1.0. https://github.com/sgossner/VSCO-2-CE\n"
                "- Versilian Community Sample Library (VCSL), Versilian Studios LLC. CC0 1.0. https://github.com/sgossner/VCSL\n\n"
                "Both are public domain: no credit is required, but it is kindly given.\n")
    size = sum(os.path.getsize(os.path.join(d, x)) for d, _, xs in os.walk(args.out) for x in xs)
    print("library: %d instruments, %.0f MB -> %s" % (len(entries), size / 1e6, args.out))


if __name__ == "__main__":
    main()
