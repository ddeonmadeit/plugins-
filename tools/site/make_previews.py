#!/usr/bin/env python3
"""
Render listening previews of every factory preset of one plugin for the download site.

  python3 tools/site/make_previews.py <Plugin> <out-dir> [--build-dir build] [--kind SYNTH|EFFECT]

Instruments play the harness "groove" phrase (2 bars, 140 BPM) with light hats and claps mixed
under them; effects process the harness's built-in test programme. A plugin's qa.json can override
this with "previewPhrase" (e.g. "drums") and "previewBacking": false. Writes
<out-dir>/<Plugin>/pNNN.mp3 and <out-dir>/<Plugin>/entries.json (name, category, description,
file, waveform peaks). Needs ffmpeg, numpy, scipy, soundfile.
"""
import argparse
import glob
import json
import os
import shutil
import subprocess
import tempfile

import numpy as np
import soundfile as sf
from scipy import signal

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
S16 = 60.0 / 140.0 / 4.0  # one sixteenth at 140 BPM, matches the harness groove phrase


def drums(n, sr):
    out = np.zeros((n, 2))
    rng = np.random.default_rng(7)
    hp = signal.butter(4, 7000, "high", fs=sr, output="sos")
    bp = signal.butter(2, [900, 2500], "band", fs=sr, output="sos")

    def hit(t, length, decay, gain, sos, pan):
        i, length_n = int(t * sr), int(length * sr)
        length_n = min(length_n, n - i)
        if length_n <= 0:
            return
        x = signal.sosfilt(sos, rng.standard_normal(length_n)) * np.exp(-np.arange(length_n) / (decay * sr)) * gain
        out[i:i + length_n, 0] += x * (1 - pan)
        out[i:i + length_n, 1] += x * (1 + pan)

    for k in range(0, 32, 2):
        hit(k * S16, 0.06, 0.012, 0.10, hp, 0.2 if k % 4 else -0.2)
    for k in (29, 30, 31):
        hit(k * S16, 0.04, 0.008, 0.07, hp, 0.0)
    for k in (8, 24):
        for d in (0.0, 0.011, 0.022):
            hit(k * S16 + d, 0.25, 0.05, 0.22, bp, 0.0)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("plugin")
    ap.add_argument("out_dir")
    ap.add_argument("--build-dir", default=os.path.join(ROOT, "build"))
    ap.add_argument("--kind", default=None, help="SYNTH or EFFECT (default: read from plugin_meta)")
    args = ap.parse_args()

    kind = args.kind
    if kind is None:
        meta = json.loads(subprocess.check_output(["python3", os.path.join(ROOT, "tools/ci/plugin_meta.py"), "--only", args.plugin]))
        kind = meta[0]["kind"]

    harness = os.path.join(args.build_dir, "plugins", args.plugin, args.plugin + "_Harness_artefacts", "Release", args.plugin + "_Harness")
    out = os.path.join(args.out_dir, args.plugin)
    os.makedirs(out, exist_ok=True)

    presets = {}
    for f in glob.glob(os.path.join(ROOT, "plugins", args.plugin, "Presets", "**", "*.json"), recursive=True):
        d = json.load(open(f))
        presets[(d.get("category", ""), d["name"])] = d

    qa_path = os.path.join(ROOT, "plugins", args.plugin, "qa.json")
    qa = json.load(open(qa_path)) if os.path.exists(qa_path) else {}
    phrase = qa.get("previewPhrase", "groove" if kind == "SYNTH" else "demo")
    backing = qa.get("previewBacking", kind == "SYNTH")

    tmp = tempfile.mkdtemp()
    try:
        subprocess.run([harness, "render-all", "--phrase", phrase, "--outdir", tmp], check=True, stdout=subprocess.DEVNULL)
        # Some categories or presets are played differently (Afterglow's GUITAR presets get strummed chords
        # and a riff). Keys are a category or "CATEGORY - Preset Name"; the preset wins. "phrase@bpm" also
        # gives the plugin that host tempo, so synced LFOs, delays and pumps match the phrase.
        by_category = qa.get("previewPhraseByCategory", {})
        index = json.load(open(os.path.join(tmp, "index.json")))
        for item in index:
            special = by_category.get(item["program"], by_category.get(item["program"].partition(" - ")[0]))
            if special:
                special, _, bpm = special.partition("@")
                subprocess.run([harness, "render", "--program", str(item["index"]), "--phrase", special,
                                "--out", os.path.join(tmp, item["file"])] + (["--bpm", bpm] if bpm else []),
                               check=True, stdout=subprocess.DEVNULL)
        entries = []
        for item in index:
            x, sr = sf.read(os.path.join(tmp, item["file"]), always_2d=True)
            mix = x
            if backing:
                mix = x * 0.9 + drums(len(x), sr)
                mix = np.tanh(mix * 1.05) / np.tanh(1.05)
            fade = int(0.05 * sr)
            mix[-fade:] *= np.linspace(1, 0, fade)[:, None]
            wav = os.path.join(tmp, "mix.wav")
            sf.write(wav, mix.astype(np.float32), sr, subtype="FLOAT")
            name = "p%03d.mp3" % item["index"]
            subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", wav, "-ar", "44100", "-b:a", "128k",
                            os.path.join(out, name)], check=True)

            category, _, preset_name = item["program"].partition(" - ")
            doc = presets.get((category, preset_name), {})
            peaks = np.array([b.max() for b in np.array_split(np.abs(x).max(axis=1), 90)])
            peaks = peaks / max(peaks.max(), 1e-6)
            entries.append({"category": category, "name": preset_name or item["program"],
                            "description": doc.get("description", ""), "file": name,
                            "wave": [round(float(v), 2) for v in peaks]})
        json.dump(entries, open(os.path.join(out, "entries.json"), "w"))
        print("%s: %d previews -> %s" % (args.plugin, len(entries), out))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
