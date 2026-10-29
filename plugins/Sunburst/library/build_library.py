#!/usr/bin/env python3
"""
Build the Sunburst guitar sample library from CC0 multisample libraries (see sources.json).

  python3 plugins/Sunburst/library/build_library.py --kits <dir> --out <library-dir> [--download]
                                                    [--only id,id] [--discard-kits] [--jobs N]

--kits holds the extracted sources (Shinyguitar/, Emilyguitar/, BlackAndGreen/, SpanishClassicalGuitar/);
--download fetches and verifies any that are missing first (same arguments as the Kitroom builder, so CI
treats every sample library alike).

Per instrument the builder writes mono 16-bit FLAC files and catalog.json entries:
  sus   sustained notes: MIDI note (from the file name, checked against a pitch estimate), velocity
        layer, round robin, attack loudness for velocity mapping and the tuning error in cents
  rel   string-damping noises played on note-off (the "String Noise" knob scales them)
  ham   hammer-on notes, where the source has them
Noise pools per family (fret squeaks, muted strums, pick noise, body knocks) go to noises/<family>/.

Processing: the silent lead-in is trimmed to a few ms before the pick, tails below -66 dB are cut and
long notes are capped at 6 s with a fade (memory: a guitar stays under ~120 MB in RAM), round robins are
capped at 3 per layer, and every instrument gets one gain so its loudest layer lands at the same
loudness. Layers keep their recorded dynamics relative to each other.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
SR = 44100
NOTE_INDEX = {"c": 0, "db": 1, "d": 2, "eb": 3, "e": 4, "f": 5, "gb": 6, "g": 7, "ab": 8, "a": 9, "bb": 10, "b": 11}
SHARP_INDEX = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}


def md5(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def note_number(name, octave_shift=0):
    """'gb2' -> 42 (C4 = 60); octave_shift corrects libraries that name notes an octave up."""
    m = re.fullmatch(r"([a-g]b?)(-?\d)", name)
    return 12 * (int(m.group(2)) + 1 + octave_shift) + NOTE_INDEX[m.group(1)]


def ensure_source(src, kits_dir, download):
    if os.path.exists(os.path.join(kits_dir, src["check"])):
        return
    if not download:
        sys.exit("missing %s (pass --download to fetch it)" % os.path.join(kits_dir, src["check"]))
    os.makedirs(kits_dir, exist_ok=True)
    archive = os.path.join(kits_dir, os.path.basename(src["url"]))
    print("downloading", src["url"], flush=True)
    subprocess.run(["curl", "-sSL", "--retry", "5", "-o", archive, src["url"]], check=True)
    if md5(archive) != src["md5"]:
        sys.exit("checksum mismatch for " + archive)
    dest = os.path.join(kits_dir, src.get("subdir", ""))
    os.makedirs(dest, exist_ok=True)
    if src["kind"] == "zip":
        subprocess.run(["unzip", "-q", "-o", archive, "-d", dest], check=True)
    else:
        subprocess.run(["7z", "x", "-y", "-o" + dest, archive], check=True, stdout=subprocess.DEVNULL)
    os.remove(archive)


# ---------------------------------------------------------------------------------------------------------
# Parsers: list every usable file of a source as {art, path, note, layer, rr, kind}.

def parse_shiny(root, inst):
    ch = inst["channel"]
    folder = os.path.join(root, "Samples", "acoustic" if ch == 1 else "electric")
    out = []
    for f in sorted(os.listdir(folder)):
        m = re.fullmatch(r"([a-g]b?\d)_vl(\d)_rr(\d)_%d\.wav" % ch, f)
        if m:
            out.append(dict(art="sus", path=os.path.join(folder, f), note=note_number(m.group(1)),
                            layer=int(m.group(2)) - 1, rr=int(m.group(3)) - 1))
            continue
        m = re.fullmatch(r"([a-g]b?\d)_release_rr(\d)_%d\.wav" % ch, f)
        if m:
            out.append(dict(art="rel", path=os.path.join(folder, f), note=note_number(m.group(1)), layer=0, rr=int(m.group(2)) - 1))
            continue
        m = re.fullmatch(r"mute_string(\d)_rr(\d)_%d\.wav" % ch, f)
        if m:   # one string damped by the fretting hand: string 1 = low E
            out.append(dict(art="noise", kind="mute", path=os.path.join(folder, f), note=int(m.group(1)) - 1, layer=0, rr=int(m.group(2)) - 1))
            continue
        m = re.fullmatch(r"mute_strings(\d)_rr(\d)_%d\.wav" % ch, f)
        if m:
            out.append(dict(art="noise", kind="strum", path=os.path.join(folder, f), note=None, layer=0, rr=int(m.group(2)) - 1))
            continue
        m = re.fullmatch(r"perc_(\d)_rr(\d)_%d\.wav" % ch, f)
        if m:
            out.append(dict(art="noise", kind="knock", path=os.path.join(folder, f), note=None, layer=0, rr=int(m.group(2)) - 1))
    return out


def parse_emily(root, inst):
    out = []
    layers = {"p": 0, "mp": 1, "mf": 2, "f": 3}
    for f in sorted(os.listdir(os.path.join(root, "notes"))):
        m = re.fullmatch(r"([a-g]b?\d)_(p|mp|mf|f)_rr(\d)\.wav", f)
        if m:
            out.append(dict(art="sus", path=os.path.join(root, "notes", f), note=note_number(m.group(1)),
                            layer=layers[m.group(2)], rr=int(m.group(3)) - 1))
    for f in sorted(os.listdir(os.path.join(root, "release"))):
        m = re.fullmatch(r"([a-g]b?\d)_release_rr(\d)\.wav", f)
        if m:
            out.append(dict(art="rel", path=os.path.join(root, "release", f), note=note_number(m.group(1)), layer=0, rr=int(m.group(2)) - 1))
    kinds = {"fingering": "squeak", "muted": "strum", "pickrest": "pick"}
    for f in sorted(os.listdir(os.path.join(root, "noises"))):
        m = re.fullmatch(r"(fingering|muted|pickrest)(\d)_rr(\d)\.wav", f)
        if m:
            out.append(dict(art="noise", kind=kinds[m.group(1)], path=os.path.join(root, "noises", f), note=None,
                            layer=int(m.group(2)) - 1, rr=int(m.group(3)) - 1))
    return out


def parse_bg(root, inst):
    base = os.path.join(root, "Samples", inst["color"])
    out = []
    layers = {"p": 0, "mf": 1, "f": 2}
    for f in sorted(os.listdir(os.path.join(base, "ord"))):
        m = re.fullmatch(r"twang_([a-g]b?\d)_(p|mf|f)_rr(\d)\.wav", f)
        if m:   # Black And Green names notes an octave up (its "e3" is the low E string, MIDI 40)
            out.append(dict(art="sus", path=os.path.join(base, "ord", f), note=note_number(m.group(1), -1),
                            layer=layers[m.group(2)], rr=int(m.group(3)) - 1))
    for art, folder, prefix in (("rel", "rel", "release"), ("ham", "hammer", "hammer")):
        if os.path.isdir(os.path.join(base, folder)):
            for f in sorted(os.listdir(os.path.join(base, folder))):
                m = re.fullmatch(prefix + r"_([a-g]b?\d)_rr(\d)\.wav", f)
                if m:
                    out.append(dict(art=art, path=os.path.join(base, folder, f), note=note_number(m.group(1), -1),
                                    layer=0, rr=int(m.group(2)) - 1))
    return out


def parse_nylon(root, inst):
    # File names are unreliable here (G1.wav plays G2); the pitch estimate decides the note.
    folder = os.path.join(root, "samples")
    return [dict(art="sus", path=os.path.join(folder, f), note=None, layer=0, rr=0)
            for f in sorted(os.listdir(folder)) if f.endswith(".wav")]


PARSERS = {"shiny": parse_shiny, "emily": parse_emily, "bg": parse_bg, "nylon": parse_nylon}


# ---------------------------------------------------------------------------------------------------------
def estimate_pitch(x):
    """MIDI pitch (float) from a YIN-style difference function on 0.15-0.55 s."""
    seg = x[int(0.15 * SR):int(0.55 * SR)]
    if len(seg) < int(0.2 * SR):
        seg = x[:int(0.4 * SR)]
    seg = seg - seg.mean()
    minlag, maxlag = int(SR / 1500), int(SR / 60)
    if len(seg) <= 2 * maxlag:
        return None
    w = len(seg) - maxlag
    d = np.array([np.sum((seg[:w] - seg[lag:lag + w]) ** 2) for lag in range(minlag, maxlag)])
    cmnd = d * np.arange(1, len(d) + 1) / (np.cumsum(d) + 1e-12)
    cand = np.where(cmnd < 0.15)[0]
    i = int(cand[0]) if len(cand) else int(np.argmin(cmnd))
    while i + 1 < len(cmnd) and cmnd[i + 1] < cmnd[i]:
        i += 1
    frac = 0.0
    if 0 < i < len(cmnd) - 1:
        y0, y1, y2 = cmnd[i - 1], cmnd[i], cmnd[i + 1]
        frac = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2 + 1e-12)
    return 69 + 12 * np.log2(SR / (i + frac + minlag) / 440.0)


def analyse(job):
    """Load, trim and measure one file. Returns (job, samples, info)."""
    entry, proc = job
    x, sr = sf.read(entry["path"], always_2d=True, dtype="float64")
    if sr != SR:
        raise SystemExit("%s: expected %d Hz, got %d" % (entry["path"], SR, sr))
    x = x.mean(1)
    x = x - np.mean(x[: min(len(x), 2048)])            # recorded DC offset
    peak = np.abs(x).max() + 1e-12
    onset = int(np.argmax(np.abs(x) > peak * 0.01))
    start = max(0, onset - int(proc["preRollMs"] * 1e-3 * SR))
    x = x[start:]

    # Tail: last point above the floor (relative to the peak), plus 30 ms.
    env = np.sqrt(np.convolve(x * x, np.ones(441) / 441, "same"))
    above = np.where(env > peak * 10 ** (proc["tailFloorDb"] / 20))[0]
    end = min(len(x), (above[-1] if len(above) else len(x) - 1) + int(0.03 * SR))
    if entry["art"] == "sus":
        end = min(end, int(proc["maxSeconds"] * SR))
    elif entry["art"] == "rel":
        end = min(end, int(0.8 * SR))
    else:
        end = min(end, int(1.5 * SR))
    x = x[:end].copy()
    fade = min(len(x) // 3, int((proc["fadeSeconds"] if entry["art"] == "sus" and end >= int(proc["maxSeconds"] * SR) else 0.03) * SR))
    if fade > 0:
        x[-fade:] *= np.linspace(1, 0, fade) ** 2

    att = x[: int(proc["loudnessMs"] * 1e-3 * SR)]
    info = dict(loudness=float(20 * np.log10(np.sqrt(np.mean(att ** 2)) + 1e-12)), peak=float(np.abs(x).max()))
    if entry["art"] in ("sus", "ham"):
        info["pitch"] = estimate_pitch(x)
    return entry, x.astype(np.float32), info


def build_instrument(inst, source, kits_dir, out_dir, proc, jobs):
    root = os.path.join(kits_dir, source["root"])
    entries = PARSERS[inst["parser"]](root, inst)
    maxrr = proc["maxRoundRobins"]
    entries = [e for e in entries if e["rr"] < (maxrr if e["art"] == "sus" else 4)]
    with ProcessPoolExecutor(max_workers=jobs) as ex:
        results = list(ex.map(analyse, [(e, proc) for e in entries], chunksize=4))

    # Notes: trust the name, but check it against the pitch; nylon has no reliable names at all.
    tuned = []
    for e, x, info in results:
        if e["art"] in ("sus", "ham") and info.get("pitch") is not None:
            p = info["pitch"]
            if e["note"] is None:
                e["note"] = int(round(p))
            if abs(p - e["note"]) <= 0.6:
                info["cents"] = float((p - e["note"]) * 100.0)
            else:   # very high or very quiet notes fool the estimate; keep the name, skip the correction
                print("  note: %s named %d, pitch estimate %.2f ignored" % (os.path.basename(e["path"]), e["note"], p))
        tuned.append((e, x, info))

    # One gain per instrument: the loudest layer of the middle notes lands on the target loudness.
    sus = [(e, info) for e, _, info in tuned if e["art"] == "sus"]
    top = max(e["layer"] for e, _ in sus)
    mids = [info["loudness"] for e, info in sus if e["layer"] == top and 48 <= e["note"] <= 72] or \
           [info["loudness"] for e, info in sus if e["layer"] == top]
    gain_db = proc["targetLoudnessDb"] - float(np.median(mids))
    max_peak = max(info["peak"] for _, _, info in tuned)
    gain_db = min(gain_db, proc["peakCeilingDb"] - 20 * np.log10(max_peak))
    gain = 10 ** (gain_db / 20)

    entry = dict(id=inst["id"], name=inst["name"], family=inst["family"], finish=inst["finish"],
                 credit=source["credit"], license="CC0 1.0", url=source["url_info"], sampleRate=SR,
                 noiseFamily=inst["noiseFamily"], sus=[], rel=[], ham=[], noises=[])
    counters = {}
    for e, x, info in sorted(tuned, key=lambda t: (t[0]["art"], t[0]["note"] if t[0]["note"] is not None else -1, t[0]["layer"], t[0]["rr"])):
        key = (e["art"], e.get("kind"), e["note"], e["layer"])
        idx = counters.get(key, 0)
        counters[key] = idx + 1
        if e["art"] == "noise":
            rel = "noises/%s/%s_%s_%d.flac" % (inst["id"], e["kind"], "x" if e["note"] is None else e["note"], idx + 10 * e["layer"])
        else:
            rel = "%s/%s/%d_%d_%d.flac" % (inst["id"], e["art"], e["note"], e["layer"], idx)
        path = os.path.join(out_dir, "sounds", rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        sf.write(path, np.clip(x * gain, -1, 1), SR, subtype="PCM_16", format="FLAC")
        item = dict(file="sounds/" + rel, loudness=round(info["loudness"] + gain_db, 2))
        if e["art"] == "noise":
            item["kind"] = e["kind"]
            if e["note"] is not None:
                item["string"] = e["note"]
            entry["noises"].append(item)
        else:
            item.update(note=e["note"], layer=e["layer"])
            if "cents" in info:
                item["cents"] = round(info["cents"], 1)
            entry[e["art"]].append(item)

    layers = sorted({i["layer"] for i in entry["sus"]})
    notes = sorted({i["note"] for i in entry["sus"]})
    print("  %s: %d notes (%d-%d), %d layers, %d sustain / %d release / %d hammer / %d noise files, gain %+.1f dB"
          % (inst["id"], len(notes), notes[0], notes[-1], len(layers), len(entry["sus"]), len(entry["rel"]),
             len(entry["ham"]), len(entry["noises"]), gain_db), flush=True)
    return entry


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kits", required=True, help="folder with the extracted sources")
    ap.add_argument("--out", required=True)
    ap.add_argument("--download", action="store_true")
    ap.add_argument("--discard-kits", action="store_true", help="delete each source once its instruments are built")
    ap.add_argument("--only", default="", help="comma-separated instrument ids (merged into an existing catalog)")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    args = ap.parse_args()

    cfg = json.load(open(os.path.join(HERE, "sources.json")))
    only = set(filter(None, args.only.split(",")))
    os.makedirs(args.out, exist_ok=True)
    catalog_path = os.path.join(args.out, "catalog.json")
    catalog = json.load(open(catalog_path)) if only and os.path.exists(catalog_path) else {"version": 1, "instruments": []}
    built = {i["id"]: i for i in catalog["instruments"]}

    instruments = [i for i in cfg["instruments"] if not only or i["id"] in only]
    for src_id, src in cfg["sources"].items():
        todo = [i for i in instruments if i["source"] == src_id]
        if not todo:
            continue
        ensure_source(src, args.kits, args.download)
        for inst in todo:
            print("building", inst["id"], flush=True)
            built[inst["id"]] = build_instrument(inst, src, args.kits, args.out, cfg["processing"], args.jobs)
        if args.discard_kits:
            shutil.rmtree(os.path.join(args.kits, src["root"]), ignore_errors=True)

    # Instruments without their own release noises borrow another's (string damping sounds alike).
    for inst in cfg["instruments"]:
        if inst.get("releaseFrom") and inst["id"] in built and inst["releaseFrom"] in built:
            built[inst["id"]]["rel"] = built[inst["releaseFrom"]]["rel"]

    # Fret squeaks, muted strums, pick and body noises are shared per family: every electric guitar
    # gets the pool recorded on the electrics, every acoustic the pool from the acoustic recordings.
    catalog["instruments"] = [built[i["id"]] for i in cfg["instruments"] if i["id"] in built]
    catalog["noises"] = {}
    for inst in catalog["instruments"]:
        catalog["noises"].setdefault(inst["family"], []).extend(inst["noises"])
    with open(catalog_path, "w") as f:
        json.dump(catalog, f, indent=1)
    with open(os.path.join(args.out, "ATTRIBUTION.txt"), "w") as f:
        f.write("Sunburst sample library. Every source below is CC0 1.0 (public domain); credited with thanks.\n\n")
        for src in cfg["sources"].values():
            f.write("- %s\n  %s\n" % (src["credit"], src["url_info"]))
    print("catalog:", catalog_path)


if __name__ == "__main__":
    main()
