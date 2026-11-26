"""Shared bits of the real-vocal QA tools (Aircheck): paths, the neutral preset, rendering, sibilance."""
import json
import os
import subprocess

import numpy as np
from scipy.signal import butter, sosfilt

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
TAKES = os.environ.get("VOCAL_TAKES", os.path.join(ROOT, "qa-out", "vocals"))
OUT = os.path.join(ROOT, "qa-out", "vocal-eval")
FS = 48000


def harness(plugin="Aircheck"):
    return os.environ.get("HARNESS", os.path.join(ROOT, "build", "plugins", plugin, f"{plugin}_Harness_artefacts", "Release", f"{plugin}_Harness"))


def take(name):
    p = os.path.join(TAKES, name + ".wav")
    if not os.path.exists(p):
        raise SystemExit(f"missing {p}: run tools/qa/vocal/fetch_vocalset.py first")
    return p


def neutral_preset():
    """Aircheck with every stage off (Transparent Tune with tuning off too): the chain is a straight wire."""
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, "neutral.json")
    p = json.load(open(os.path.join(ROOT, "plugins", "Aircheck", "Presets", "STUDIO", "21_Transparent_Tune.json")))
    q = p["params"]
    q.update(tuneOn=False, tuneHumanize=0.0, tuneSpeed=0.0, limit=0.0, revMix=0.0, dlyMix=0.0, compAmount=0.0, deess=0.0, preDrive=0.0)
    for i in range(1, 9):
        q[f"b{i}On"] = False
    p["name"] = "Neutral"
    json.dump(p, open(path, "w"), indent=1)
    return path


def render(inp, out, sets=(), preset=None, program=None, h=None):
    cmd = [h or harness(), "render", "--input", inp, "--out", out]
    cmd += ["--program", str(program)] if program is not None else ["--preset-file", preset or neutral_preset()]
    for s in sets:
        cmd += ["--set", s]
    subprocess.run(cmd, check=True, capture_output=True)


_hp = butter(4, 5000, "high", fs=FS, output="sos")
_lp = butter(4, 3000, "low", fs=FS, output="sos")


def frames(x, ms=5.0):
    w = int(ms * 0.001 * FS)
    n = len(x) // w
    return np.mean(x[:n * w].reshape(n, w) ** 2, axis=1)


def sibilance_db(y):
    """Power of the S band (> 5 kHz) during sibilants against the power of the vowels, dB (dry VocalSet ~ -14)."""
    m = y.mean(axis=1) if y.ndim > 1 else y
    h, l, tot = frames(sosfilt(_hp, m)), frames(sosfilt(_lp, m)), frames(m)
    lv = 10 * np.log10(tot + 1e-12)
    ref = np.percentile(lv, 95)
    s = (h > 2 * l) & (lv > ref - 30)
    v = (l > 10 * h) & (lv > ref - 20)
    if not s.any() or not v.any():
        return float("nan")
    return float(10 * np.log10(np.mean(h[s]) / np.mean(l[v] + h[v])))
