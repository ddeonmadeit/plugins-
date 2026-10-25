#!/usr/bin/env python3
"""
Build the Kitroom sample library from DrumGizmo kits (CC BY 4.0).

  python3 plugins/Kitroom/library/build_library.py --kits <dir> --out <library-dir> [--download] [--only id,id]
                                                   [--discard-kits] [--jobs N]

--kits holds the extracted kits (DRSKit/, MuldjordKit3/ ...); --download fetches and verifies any that
are missing first. Every hit becomes one 5-channel 16-bit FLAC: [close, overheadL, overheadR, roomL, roomR].
The mapping from DrumGizmo instruments to Kitroom sounds lives in sources.json next to this script.

Per hit: paired close mics are summed after polarity/time alignment, the silent lead-in and the
tail below -72 dB are trimmed (cymbals are capped and faded), and loudness of the attack is measured
for velocity mapping. Per sound: close mics are balanced against the overheads, the whole sound
is normalized to a peak target for its drum type, and its position in the overheads is stored as
closePan so the plugin can pan the close mic to match.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from concurrent.futures import ProcessPoolExecutor

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
SR = 44100
DEFAULT_MIX = (1.0, 0.8, 0.6)   # close, overheads, room: matches the plugin's default mixer


def md5(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def ensure_kit(kit, kits_dir, download):
    target = os.path.join(kits_dir, kit["dir"])
    if os.path.exists(os.path.join(target, kit["kitxml"])):
        return
    if not download:
        sys.exit("missing %s (pass --download to fetch it)" % target)
    os.makedirs(kits_dir, exist_ok=True)
    zip_path = os.path.join(kits_dir, os.path.basename(kit["url"]))
    print("downloading", kit["url"], flush=True)
    subprocess.run(["curl", "-sSL", "--retry", "5", "-o", zip_path, kit["url"]], check=True)
    if md5(zip_path) != kit["md5"]:
        sys.exit("checksum mismatch for " + zip_path)
    subprocess.run(["unzip", "-q", "-o", zip_path, "-d", kits_dir], check=True)
    os.remove(zip_path)


def load_instruments(kit, kits_dir):
    """instrument name -> list of (power, {channel: (wav path, column)})"""
    base = os.path.join(kits_dir, kit["dir"])
    root = ET.parse(os.path.join(base, kit["kitxml"])).getroot()
    out = {}
    for ins in root.find("instruments"):
        path = os.path.join(base, ins.get("file"))
        samples = []
        for s in ET.parse(path).getroot().find("samples").findall("sample"):
            chans = {}
            for af in s.findall("audiofile"):
                chans[af.get("channel")] = (os.path.join(os.path.dirname(path), af.get("file")), int(af.get("filechannel")) - 1)
            samples.append((float(s.get("power")), chans))
        out[ins.get("name")] = samples
    return out


def read_hit(chans, names):
    """Columns for the given channel names from one multichannel hit file -> (frames, len(names))."""
    cache = {}
    cols = []
    for n in names:
        path, col = chans[n]
        if path not in cache:
            cache[path] = sf.read(path, dtype="float32", always_2d=True)[0]
        cols.append(cache[path][:, col])
    length = min(len(c) for c in cols) if cols else 0
    return np.stack([c[:length] for c in cols], axis=1) if cols else np.zeros((0, 0), np.float32)


def best_lag(ref, sig, max_lag=150):
    """Lag (samples) and polarity that best align sig to ref over the attack."""
    n = min(len(ref), len(sig), 2048)
    a, b = ref[:n].astype(np.float64), sig[:n].astype(np.float64)
    best, best_val = 0, 0.0
    for lag in range(-max_lag, max_lag + 1):
        if lag >= 0:
            v = np.dot(a[lag:], b[:n - lag])
        else:
            v = np.dot(a[:n + lag], b[-lag:])
        if abs(v) > abs(best_val):
            best, best_val = lag, v
    return best, (1.0 if best_val >= 0 else -1.0)


def shift(x, lag):
    if lag == 0:
        return x
    out = np.zeros_like(x)
    if lag > 0:
        out[lag:] = x[:-lag]
    else:
        out[:lag] = x[-lag:]
    return out


def mono_mix(stems):
    c, o, r = DEFAULT_MIX
    return stems[:, 0] * c + 0.5 * o * (stems[:, 1] + stems[:, 2]) + 0.5 * r * (stems[:, 3] + stems[:, 4])


def stereo_peak(stems):
    c, o, r = DEFAULT_MIX
    left = stems[:, 0] * c + o * stems[:, 1] + r * stems[:, 3]
    right = stems[:, 0] * c + o * stems[:, 2] + r * stems[:, 4]
    return max(np.abs(left).max(), np.abs(right).max())


def close_pan(kit, cat, hits):
    """Where the piece sits in the overheads, from which overhead hears it first (spaced pair), as a
    pan position for its close mic so the two images agree. Kick and snare stay centred."""
    if cat in ("kick", "snare"):
        return 0.0
    st = max((h[1] for h in hits), key=lambda x: np.abs(x[:, 1:3]).max())
    first = [int(np.argmax(np.abs(st[:, ch]) > 0.2 * np.abs(st[:, ch]).max())) for ch in (1, 2)]
    lag = first[0] - first[1] - kit.get("overheadCentreLag", 0)   # < 0: left overhead first
    return round(float(np.clip(lag / 150.0, -0.7, 0.7)), 2)


def build_sound(job):
    kit, sound, kits_dir, out_dir, proc = job
    instruments = load_instruments(kit, kits_dir)
    cat = sound["category"]
    close_names = sound["close"]
    oh, room = kit["overheads"], kit["room"]

    raw = []  # (articulation, stems)
    for art, instr_list in sound["articulations"].items():
        for instr in instr_list:
            for _, chans in instruments[instr]:
                cols = read_hit(chans, close_names + oh + room)
                close = cols[:, :len(close_names)] if close_names else None
                amb = cols[:, len(close_names):]
                if amb.shape[0] < 256 or np.abs(amb).max() < 1e-5:
                    continue  # empty / broken recording
                raw.append((art, close, amb))

    # Align secondary close mics to the primary once, using the loudest hit.
    lags = []
    if close_names and len(close_names) > 1:
        loudest = max(raw, key=lambda r: np.abs(r[1][:, 0]).max())
        ref = loudest[1][:, 0]
        # Snare heads are ~15 cm apart, kick mics sit on opposite heads: limit the search accordingly,
        # or periodic drum tones produce lags that smear the attack.
        max_lag = proc.get("alignMaxLagSamples", {}).get(cat, 40)
        for k in range(1, len(close_names)):
            lags.append(best_lag(ref, loudest[1][:, k], max_lag))

    hits = []
    oh_trim = [10 ** (db / 20.0) for db in kit.get("overheadTrimDb", [0.0, 0.0])]
    for art, close, amb in raw:
        n = amb.shape[0]
        stems = np.zeros((n, 5), np.float32)
        stems[:, 1:5] = amb[:n]
        stems[:, 1] *= oh_trim[0]
        stems[:, 2] *= oh_trim[1]
        if close is not None:
            c = close[:n, 0].copy()
            prim_peak = np.abs(c).max() + 1e-9
            for k, (lag, pol) in enumerate(lags, start=1):
                sec = shift(close[:n, k], -lag) * pol
                # Secondary mic (snare bottom, kick front) sits under the primary.
                sec_gain = 0.55 * prim_peak / (np.abs(sec).max() + 1e-9)
                c += sec * min(sec_gain, 1.0)
            stems[:, 0] = c
        hits.append([art, stems])

    # Balance close mics against the overheads for this drum type.
    if close_names:
        peak_close = max(np.abs(h[1][:, 0]).max() for h in hits) + 1e-9
        peak_oh = max(np.abs(h[1][:, 1:3]).max() for h in hits) + 1e-9
        ratio = proc["closeToOverheadRatio"].get(cat, 1.0)
        for h in hits:
            h[1][:, 0] *= ratio * peak_oh / peak_close

    # Trim each hit: start just before the attack, end when everything is below the floor.
    max_len = int(proc["maxSeconds"][cat] * SR)
    floor = 10 ** (proc["tailFloorDb"] / 20.0)
    for h in hits:
        st = h[1]
        mix = mono_mix(st)
        peak = np.abs(mix).max()
        onset = int(np.argmax(np.abs(mix) > peak * 0.01))
        start = max(0, onset - int(0.002 * SR))
        env = np.abs(st).max(axis=1)
        block = 1024
        nb = len(env) // block
        loud = [i for i in range(nb) if env[i * block:(i + 1) * block].max() > np.abs(st).max() * floor]
        end = min(len(env), ((loud[-1] + 1) * block if loud else len(env)) + int(0.03 * SR))
        capped = end - start > max_len
        end = min(end, start + max_len)
        st = st[start:end].copy()
        fade = int((1.0 if capped else 0.03) * SR)
        fade = min(fade, len(st) // 3)
        if fade > 0:
            st[-fade:] *= (0.5 + 0.5 * np.cos(np.linspace(0, np.pi, fade)))[:, None]
        st[:16] *= np.linspace(0, 1, 16)[:, None]
        h[1] = st

    # Normalize the whole sound to its peak target, never letting any stem clip.
    peak = max(stereo_peak(h[1]) for h in hits) + 1e-9
    gain = 10 ** (proc["peakTargetDb"][cat] / 20.0) / peak
    # Very quiet recordings (brushes) would otherwise come up with their room noise; let presets add the rest.
    gain = min(gain, 10 ** (proc.get("maxGainDb", 99.0) / 20.0))
    stem_peak = max(np.abs(h[1]).max() for h in hits) * gain
    if stem_peak > 0.98:
        gain *= 0.98 / stem_peak

    entry = {"id": sound["id"], "name": sound["name"], "kit": kit["id"], "category": cat,
             "sampleRate": SR, "closePan": close_pan(kit, cat, hits) if close_names else 0.0, "articulations": {}}
    counters = {}
    for art, st in sorted(hits, key=lambda h: (h[0], np.sqrt(np.mean(mono_mix(h[1])[:int(0.06 * SR)] ** 2)))):
        st = st * gain
        attack = mono_mix(st)[:int(0.06 * SR)]
        loudness = 20 * np.log10(np.sqrt(np.mean(attack ** 2)) + 1e-9)
        counters[art] = counters.get(art, 0) + 1
        rel = "sounds/%s/%s/%02d.flac" % (sound["id"], art, counters[art])
        path = os.path.join(out_dir, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        sf.write(path, np.clip(st, -1.0, 1.0), SR, subtype="PCM_16", format="FLAC")
        entry["articulations"].setdefault(art, []).append({"file": rel, "loudness": round(float(loudness), 2),
                                                           "frames": int(st.shape[0])})
    total = sum(len(v) for v in entry["articulations"].values())
    print("%-24s %3d hits  gain %+.1f dB" % (sound["id"], total, 20 * np.log10(gain)), flush=True)
    return entry


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kits", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--download", action="store_true")
    ap.add_argument("--only", default="")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--discard-kits", action="store_true",
                    help="delete each raw kit once its sounds are built (CI: the kits are ~8 GB unpacked)")
    args = ap.parse_args()

    sources = json.load(open(os.path.join(HERE, "sources.json")))
    only = set(filter(None, args.only.split(",")))

    if os.path.exists(args.out) and not only:
        shutil.rmtree(os.path.join(args.out, "sounds"), ignore_errors=True)
    os.makedirs(args.out, exist_ok=True)

    # One kit at a time, so only one raw kit has to be on disk.
    entries = []
    for kit in sources["kits"]:
        jobs = [(kit, s, args.kits, args.out, sources["processing"]) for s in kit["sounds"] if not only or s["id"] in only]
        if not jobs:
            continue
        ensure_kit(kit, args.kits, args.download)
        with ProcessPoolExecutor(max_workers=args.jobs) as pool:
            entries += list(pool.map(build_sound, jobs))
        if args.discard_kits:
            shutil.rmtree(os.path.join(args.kits, kit["dir"]), ignore_errors=True)

    order = [s["id"] for kit in sources["kits"] for s in kit["sounds"]]
    catalog_path = os.path.join(args.out, "catalog.json")
    if only and os.path.exists(catalog_path):
        old = {s["id"]: s for s in json.load(open(catalog_path))["sounds"]}
        old.update({e["id"]: e for e in entries})
        entries = list(old.values())
    entries.sort(key=lambda e: order.index(e["id"]) if e["id"] in order else len(order))

    kits = [{k: kit[k] for k in ("id", "name", "credit", "license", "finish")} | {"url": kit["page"]} for kit in sources["kits"]]
    json.dump({"version": 1, "stems": ["close", "overheadL", "overheadR", "roomL", "roomR"],
               "kits": kits, "sounds": entries}, open(catalog_path, "w"), indent=1)

    with open(os.path.join(args.out, "ATTRIBUTION.txt"), "w") as f:
        f.write("Kitroom sample library\n======================\n\n")
        f.write("The drum samples in this folder were processed (mixed to close/overhead/room stems, trimmed,\n"
                "level-matched and converted to FLAC) from the following kits:\n\n")
        for kit in sources["kits"]:
            f.write("- %s\n  %s\n  License: %s\n  Source: %s\n\n" % (kit["name"], kit["credit"], kit["license"], kit["page"]))
        f.write("When the samples are used in a composition, credit them where other credits appear,\n"
                "e.g. \"Drum samples provided by DrumGizmo.org\".\n")

    size = sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(args.out) for f in fs)
    print("library: %d sounds, %.0f MB -> %s" % (len(entries), size / 1e6, args.out))


if __name__ == "__main__":
    main()
