---
name: audio-qa
description: Objectively check how a plugin sounds without listening - render presets with the headless harness, measure loudness/peaks/spectrum/width/similarity, view spectrograms and UI snapshots, and debug clicks or artifacts. Use whenever DSP or presets change.
---

# Audio QA without ears

Everything goes through `<Name>_Harness` (generic, talks only to `juce::AudioProcessor`).

## Commands

```bash
H=build/plugins/<Name>/<Name>_Harness_artefacts/Release/<Name>_Harness
$H list | param-info | dump-params
$H render --program N --out x.wav [--phrase demo|single|slide|chords] [--sr 48000] [--block 256]
$H render --preset-file p.json --set id=value --set id2=value --out x.wav     # no rebuild needed
$H render-all --outdir qa-out/<Name>/renders                                  # + index.json
$H stress --seconds 20 [--block 1] [--sr 96000]       # random notes + param jumps + program changes
xvfb-run -a -s "-screen 0 2560x1600x24" $H snapshot --out ui.png [--program N]
python3 tools/qa/analyze.py <dir> --sheet --target-lufs -9.5 --tolerance 1.5 --similar 0.2
python3 tools/qa/variety.py <dir> [<older dir>]         # do the presets play like different instruments?
tools/qa/run_qa.sh <Name>                              # all of the above, exits non-zero on failure
$H render --program N --perf --out x.wav               # keep perf* switches (POLY) at their defaults
$H render --preset-file p.json --phrase "notes:50,0,5.8,100 55,0.02,5.8,96 end=9" --bpm 79 --out x.wav
python3 tools/qa/tone.py x.wav 0.2 5.8 [--partial 294] # width, tremolo, warble, octave balance of a window
```

`notes:` plays any notes (`note,start s,length s,velocity`, optional `end=` seconds): one-shots, a chord
from a reference, a loop. `--bpm` gives the plugin a playing host transport from beat 0, so synced LFOs,
delays and pumps (and Afterglow's Trem) run at the reference tempo; without it plugins free-run at 120.

Renders are 32-bit float WAV at 48 kHz with the plugin latency removed. Effects get a built-in test
programme (kick drops, hats, saw chord) on their input automatically.
The `demo` phrase: long note, two bouncy repeats, an overlapping pair (tests glide), a fast double,
a final note with tail. `single` is one 55 Hz note (use for harmonic analysis). `pads` is four held
chords; `mixed` (Afterglow) is a held chord, an eighth-note arpeggio, stabs and a legato melody, so pads,
keys and plucks are all measured fairly. `guitar` is three bars of strummed open chords (down / up
strokes, 80 BPM) and a riff, for Electric presets. `drums` / `drumvel` are Kitroom grooves.
Download-page previews use qa.json `previewPhrase`; `previewPhraseByCategory` overrides it per category or
per `"CATEGORY - Preset Name"` (Afterglow: GUITAR presets get `guitar`, the volume-swell preset `mixed`);
`"guitar@80"` (here or in `previewPhrase`) also renders at that host tempo, for presets with a synced tremolo / pump / delay.

## Reading analyze.py

| Column / flag | Meaning | Pass |
| --- | --- | --- |
| TP | true peak (4x upsampled) | <= 0 dBTP (SubGrit presets sit at -0.8) |
| LUFS | BS.1770 integrated, gated | inside the plugin's qa.json window |
| crest | peak - RMS | ~3 dB = slammed, ~10 dB = clean/percussive |
| sub/low/mid/hi % | power share 20-90 / 90-250 / 250-2.5k / 2.5k+ | bass presets: sub+low >= 25% |
| width | L/R correlation above 300 Hz | 1.0 mono; 0.5 = very wide |
| sub not mono | L/R correlation < 0.95 below 120 Hz | never allowed |
| DC | mean > 0.005 | never allowed |
| TOO SIMILAR | fingerprint RMS distance < threshold | see calibration |

Power shares are dominated by the fundamental: a heavily distorted 808 still reads ~85-90% sub.
Compare harmonics instead (below) when judging distortion character.

Fingerprint calibration (SubGrit, change vs the same preset): drive +0.05 -> 0.06, 808 warmth +0.08
-> 0.14, decay -12% -> 0.36, drive 0.25->0.5 -> 0.38, Tape->Clip -> 0.71, decay /2.7 -> 2.0.
Delay/chorus barely move it (they only act above 200 Hz). So < 0.2 = near-duplicate; 0.2-0.4 =
audibly related; > 0.4 = clearly different. It is a guide, not a verdict: also reason about the design
(dirt type, source, envelope, FX, pitch behaviour).

## Harmonic signature (distortion character)

Render `--phrase single --set pitchAmt=0 --set clickLevel=0 --set aSustain=1 --set subKeep=0 --set clip=0`
and FFT 0.8-1.8 s; read H1..H12 relative to the fundamental at 55 Hz. Reference (drive 0.6, bias 0.4):
Tape odd -11/-17/-21 with H2 -36 (soft); Tube H2 -21, H4 -27 (even, warm); Fuzz/Clip odd with slow rolloff;
Fold H2/H3 above the fundamental (hollow, metallic); Rectify even and odd both strong (raspy); Amp odd,
slowest rolloff (brightest); Crush odd only plus decimation images.

## Debugging clicks and artifacts (what worked)

1. Measure HF energy (>3 kHz highpass) in 5 ms windows, list the times above -50 dB.
2. Toggle features with `--set` (knock off, dirt mix 0, clip 0, punch 0) to isolate the source.
3. To isolate a stage inside the processor, add a temporary `std::getenv` bypass, rebuild only the
   harness, compare, then remove it.
4. Plot the waveform around the event (matplotlib, a few ms window) and *look at it* with the Read tool.
5. Remember the harness shifts output by the reported latency; bypassing stages but keeping
   `setLatencySamples` makes the start look wrong.

Findings so far: a crossover (allpass) on the mid smears 808 onsets; a DC blocker after a clipper pushes
peaks over the ceiling; downsampling after hard clipping overshoots 1-3 dB (catch it with a base-rate
safety clip); rectifier curves need a tracked DC offset or every note thumps.

## Consistency checks before shipping DSP changes

Render 3-5 presets at 44.1/48/96 kHz and block sizes 1, 256, 997; LUFS and centroid should match within
0.1 LU / a few Hz. Differences between block sizes should be < -60 dB (noise generators are the only
exception: their random sequence legitimately depends on block boundaries).

## Listening previews for the user

The user can't hear anything until the Mac build is installed, so publish previews:
`$H render-all --phrase groove --outdir qa-out/<Name>/preview` (two bars, 140 BPM, one slide), mix light
synthesized hats/claps under each render in Python (bass x0.9, gentle tanh safety, 50 ms fade), encode
with `ffmpeg -b:a 128k`, and publish an Artifact page whose audio files go in the `files` map
(`audio/p000.mp3` ...), not as data URIs. Group rows by category, show each preset's description and
real settings as tags, draw waveform peaks precomputed in Python on a canvas with play progress.
SubGrit's page: https://claude.ai/artifact/DTxARTUxVBPbuiynMGdjh3

## Drums and other sample players

- Phrases: `--phrase drums` (5 bars of real-drummer groove on the GM map) and `--phrase drumvel`
  (every kit note at 8 velocities) -> `python3 tools/qa/drum_velocity.py out.wav` prints the attack level
  per velocity (should follow ~40*log10(vel/127) dB and rise monotonically).
- Sample plugins load in the background: the harness marks render/snapshot as non-realtime and the
  processor waits for loads in that mode. Export `KITROOM_LIBRARY` (etc.) or renders are silent.
- When measuring per-hit levels, subtract the power of what is still ringing just before the hit
  (cymbal tails otherwise dominate soft hits).
- After `normalize_presets.py` always rebuild, re-render **and re-analyze** before normalizing again:
  it reads `report.json`, and a stale report applies the same correction twice.

## Pads and textures (Afterglow)

- `--phrase pads`: Dm9 / Bbmaj7 / Fmaj7 / Am7, two bars each at 120 BPM (= the plugins' no-host tempo, so
  pump / synced delay line up); the top note is added last so mono and legato patches play a melody.
- Calibrate every sound source to the same loudness before designing presets (render each alone at a low
  level so nothing hits the clipper; compare RMS) and store the result as per-source gains.
- Sub-mono check: the analyzer only judges it when the band below 120 Hz carries > -30 dB of the signal and
  uses an 8th-order low-pass; synths with wide unison need the side high-passed at ~200 Hz, not 140.
- Plucks, bells and keys read 3-6 LU quieter than pads at the same peak (high crest): give them bus glue
  0.55-0.75 and a little sustain, and give the plugin a wider loudness tolerance (Afterglow: -14 +-3).


## Matching a reference track (owner sends a beat)

Never ship audio from the reference: measure it, rebuild the sounds in our engines, ship presets (and
one-shots rendered from them). Workflow that worked for "The Wall" (Afterglow/SubGrit/Kitroom TRIP HOP):
- Decode with ffmpeg into its own folder; read it with `python3 -I`. Tempo from onset autocorrelation,
  key from a chroma profile, notes from FFT peaks of held sections.
- Split stems for analysis only: Demucs `htdemucs_6s` in a scratch venv (CPU torch from
  download.pytorch.org/whl/cpu, `demucs==4.0.1`; save with soundfile, its torchaudio save needs torchcodec).
  Drums, bass, guitar, vocals (sung-like leads land here), other (keys, strings, pads).
- Per stem: zoomed spectrograms, `tone.py` on a held window, harmonic levels of one note (odd vs even,
  how fast they fall), per-hit pitch / band decays for drums. Then render the same notes through the new
  preset (`notes:` + `--bpm`) and compare the same numbers until they line up.
- What mattered: drums mono and dark with the kick pitch matched (drs_kick sits at 48 Hz), a guitar
  tremolo that is global and synced (a per-voice LFO cancels across strummed notes; use Pump = Trem),
  chorus + tape for the warble, and the stereo width (L/R correlation) of each part.
