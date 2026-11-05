#!/usr/bin/env python3
"""
Builds Orbit's head-related impulse responses (Resources/hrtf.bin) from the Viking HRTF dataset v2
(Spagnol, Miccini, Unnthorsson 2020, CC BY 4.0, https://doi.org/10.5281/zenodo.4160401): a KEMAR
mannequin measured with 21 pairs of silicone pinnae at 1513 directions, 48 kHz.

For each chosen pinna set ("head"):
  1. equalise: divide every response by a blend of the power average over all directions (diffuse
     field, solid-angle weighted) and the response straight ahead, third-octave smoothed, so the
     timbre is neutral on headphones and a sound in front keeps its own tone; then soften needle
     notches (1/8-octave smoothing, -32 dB floor) and let the bottom end settle to the level at
     500 Hz with the ears converging below 150 Hz, where the head is acoustically small (the 128-tap
     measurements cannot hold the low end, and a short filter should not try);
  2. resample the measurement grid (5 deg up to 45 deg elevation, sparser above) onto a uniform
     5 x 5 deg grid by interpolating dB magnitudes along each elevation ring;
  3. make left and right mirror images (average each ear with the other ear at the mirrored azimuth);
  4. rebuild each response as a 64-tap minimum-phase filter (the interaural time difference is
     added back in the plugin as a smooth fractional delay, so filters can be interpolated freely).

Output (little endian): "OHRT", u32 version, u32 heads, u32 elevations, u32 azimuths, u32 taps,
f32 elMin, f32 step, f32 sampleRate, f32 scale, then int16 [head][el][az][ear][tap].
Azimuth runs clockwise from straight ahead (90 = right), elevation from -45 (below) to +90 (above).

usage: make_hrtf.py --sofa-dir DIR [--download] [--out Resources/hrtf.bin]
"""
import argparse, hashlib, io, os, struct, sys, urllib.request, zipfile

import numpy as np

URL = "https://zenodo.org/api/records/4160401/files/HRTFs.zip/content"
MD5 = "9a69431bbfc01f156d368709402f776c"
HEADS = ["L", "G", "A", "E"]   # medoid of the 21 pinnae, then the clearest front/back + height cues, then two distinct ones
TAPS, NFFT, SR = 64, 512, 48000.0
EL_MIN, STEP, N_EL, N_AZ = -45.0, 5.0, 28, 72


def fetch(sofa_dir):
    os.makedirs(sofa_dir, exist_ok=True)
    if all(os.path.exists(os.path.join(sofa_dir, f"subj_{h}.sofa")) for h in HEADS):
        return
    print("downloading Viking HRTF v2 (58 MB)")
    data = urllib.request.urlopen(URL, timeout=600).read()
    if hashlib.md5(data).hexdigest() != MD5:
        sys.exit("HRTFs.zip checksum mismatch")
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        for h in HEADS:
            name = f"subj_{h}.sofa"
            with open(os.path.join(sofa_dir, name), "wb") as f:
                f.write(z.read(name))


def minphase(mag):
    logm = np.log(np.maximum(mag, 1e-5))
    cep = np.fft.irfft(logm, NFFT)
    w = np.zeros(NFFT); w[0] = 1.0; w[1:NFFT // 2] = 2.0; w[NFFT // 2] = 1.0
    return np.fft.ifft(np.exp(np.fft.fft(cep * w))).real


def third_octave_smooth(freqs, x):
    out = np.empty_like(x)
    for i, f in enumerate(freqs):
        lo, hi = f / 2 ** (1 / 6), f * 2 ** (1 / 6)
        sel = (freqs >= lo) & (freqs <= hi)
        out[i] = x[sel].mean() if sel.any() else x[i]
    return out


def smooth_octave(freqs, x, fraction):
    out = x.copy()
    half = 2 ** (1 / (2 * fraction))
    for i, f in enumerate(freqs):
        if f < 1000.0:
            continue
        sel = (freqs >= f / half) & (freqs <= f * half)
        out[i] = 10 * np.log10(np.mean(10 ** (x[sel] / 10)))
    return out


def build_head(path):
    import h5py
    with h5py.File(path, "r") as f:
        pos = f["SourcePosition"][:]
        ir = f["Data.IR"][:]
        sr = float(np.array(f["Data.SamplingRate"]).ravel()[0])
    assert sr == SR, sr
    freqs = np.fft.rfftfreq(NFFT, 1 / SR)
    power = np.abs(np.fft.rfft(ir, NFFT, axis=-1)) ** 2        # (positions, ears, bins)
    az_ccw, el = pos[:, 0], pos[:, 1]

    # Diffuse-field average, each measurement weighted by the solid angle it stands for.
    weights = np.zeros(len(pos))
    for e in np.unique(el):
        ring = el == e
        band = np.radians(STEP)
        if e >= 90:
            weights[ring] = 2 * np.pi * (1 - np.cos(band / 2))
        else:
            weights[ring] = np.cos(np.radians(e)) * band * (2 * np.pi / ring.sum())
    df = (power * weights[:, None, None]).sum(0).mean(0) / weights.sum()
    front = power[np.where((az_ccw == 0) & (el == 0))[0][0]].mean(0)
    df_db = third_octave_smooth(freqs, 0.5 * 10 * np.log10(df + 1e-12) + 0.5 * 10 * np.log10(front + 1e-12))
    eq_db = -df_db
    eq_db -= np.interp(1000.0, freqs, eq_db)                     # 0 dB at 1 kHz
    fade = np.clip((freqs - 150.0) / 150.0, 0.0, 1.0)            # flat at the bottom: the head is transparent there
    eq_db = np.clip(eq_db * fade, -15.0, 12.0)
    mag_db = 10 * np.log10(power + 1e-12) + eq_db[None, None, :]

    # Soften needle notches, then settle the bottom end.
    for i in range(mag_db.shape[0]):
        for ear in range(2):
            mag_db[i, ear] = smooth_octave(freqs, mag_db[i, ear], 8)
    mag_db = np.maximum(mag_db, -32.0)
    k500 = np.searchsorted(freqs, 500.0)
    at500 = mag_db[:, :, k500]
    both = at500.mean(1, keepdims=True)
    for k in range(k500):
        f = max(freqs[k], 1.0)
        t = np.clip(np.log2(f / 150.0) / np.log2(500.0 / 150.0), 0.0, 1.0)   # 0 at 150 Hz and below, 1 at 500 Hz
        mag_db[:, :, k] = both + t * (at500 - both)

    # Uniform grid: interpolate dB magnitudes around each measured elevation ring.
    grid = np.zeros((N_EL, N_AZ, 2, TAPS))
    for ei in range(N_EL):
        e = EL_MIN + ei * STEP
        ring = np.where(np.isclose(el, e))[0]
        cw = (360.0 - az_ccw[ring]) % 360.0
        order = np.argsort(cw)
        ring, cw = ring[order], cw[order]
        ring_mags = [None] * N_AZ
        for ai in range(N_AZ):
            a = ai * STEP
            if len(ring) == 1:
                m = mag_db[ring[0]]
            else:
                ext = np.concatenate([cw, [cw[0] + 360.0]])
                k = np.searchsorted(ext, a, side="right") - 1
                k = min(k, len(ring) - 1)
                t = (a - ext[k]) / (ext[k + 1] - ext[k])
                m = (1 - t) * mag_db[ring[k]] + t * mag_db[ring[(k + 1) % len(ring)]]
            ring_mags[ai] = m
        # Mirror symmetry: the left ear at azimuth a is the right ear at -a (one pinna is never quite
        # like the other, and a sound straight ahead must not lean).
        for ai in range(N_AZ):
            mirror = (N_AZ - ai) % N_AZ
            for ear in range(2):
                m = 0.5 * (ring_mags[ai][ear] + ring_mags[mirror][1 - ear])
                h = minphase(10 ** (m / 20))[:TAPS].copy()
                h[-8:] *= 0.5 + 0.5 * np.cos(np.linspace(0, np.pi, 8))
                grid[ei, ai, ear] = h
    return grid


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sofa-dir", required=True)
    ap.add_argument("--download", action="store_true")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "Resources", "hrtf.bin"))
    args = ap.parse_args()
    if args.download:
        fetch(args.sofa_dir)
    heads = [build_head(os.path.join(args.sofa_dir, f"subj_{h}.sofa")) for h in HEADS]
    data = np.stack(heads)
    # One gain for every head: a source straight ahead sounds at the level of the dry signal (both ears, 1 kHz).
    f = np.fft.rfftfreq(NFFT, 1 / SR)
    front = np.abs(np.fft.rfft(data[0, int(-EL_MIN / STEP), 0], NFFT, axis=-1))
    data *= 1.0 / np.interp(1000.0, f, front.mean(0))
    scale = float(np.abs(data).max()) / 32767.0
    q = np.round(data / scale).astype("<i2")
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "wb") as fo:
        fo.write(b"OHRT")
        fo.write(struct.pack("<5I4f", 1, len(HEADS), N_EL, N_AZ, TAPS, EL_MIN, STEP, SR, scale))
        fo.write(q.tobytes())
    print(f"wrote {args.out}: {len(HEADS)} heads, {N_EL}x{N_AZ} directions, {TAPS} taps, {os.path.getsize(args.out)} bytes")


if __name__ == "__main__":
    main()
