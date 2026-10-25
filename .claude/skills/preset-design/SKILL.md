---
name: preset-design
description: Design factory presets that sound finished and fit a named style (808s, distorted bass, artist-inspired categories like Travis Scott, Mike Dean, untiljapan, Kanye), then lint, fill, loudness-match and de-duplicate them. Use when creating or changing presets for any plugin here.
---

# Preset design

## Workflow

1. **Define each category's signature first** (what every preset in it shares, and what no other
   category does). Write it down in the generator or a comment; it stops presets converging.
2. Author presets as JSON (`plugins/<Name>/Presets/<CATEGORY>/NN Name.json`). Only the values that
   matter need writing at first; a Python dict list run through a small writer is the fastest way
   to author dozens.
3. Audition any JSON without rebuilding: `$H render --preset-file p.json --out x.wav`, then analyze.
4. `python3 tools/qa/preset_fill.py $H plugins/<Name>/Presets` -> every parameter listed.
5. `python3 tools/qa/preset_lint.py $H plugins/<Name>/Presets --categories "A,B"`.
6. Rebuild the harness (presets are embedded), `tools/qa/run_qa.sh <Name>`.
7. Loudness: `python3 tools/qa/normalize_presets.py plugins/<Name>/Presets qa-out/<Name>/renders/report.json --target -9.5`
   then rebuild + re-run QA. Repeat once. Presets that stay quiet are peak-limited (high crest):
   give them density (more clip / saturation / glue), never more output gain past the peak cap.
8. Similarity: fix every pair under 0.2 by changing something structural (dirt type, source,
   envelope class, FX, modulation), not by nudging a knob.
9. Look at `qa-out/<Name>/renders/sheet.png`: every tile should look different.

Names: evocative, no artist names inside preset names (categories carry the style). Descriptions:
one sentence of what it is plus how to play it ("overlap notes to slide").

## SubGrit engine semantics (reuse for similar synths)

- `pitchAmt`/`pitchTime`: 808 punch, semitones above the note falling to ~5% in that many ms.
  Classic 5-9 st / 40-90 ms; kick-like 19-36 st / 18-30 ms; "boing" 12 st / 110 ms.
- `bodyDrive` (808 Warmth): rounds the sine toward a square *before* dirt. 0.5+ = squared, Vultures/rage.
- `aDecay` is time-to-silence; -20 dB arrives at ~25% of it. Sustain holds a floor; release cuts at note-off.
  Short bounce: 550-1200 ms, sustain 0. Held trap 808: 2-5 s, sustain 0.4-0.8, release 100-300 ms.
- `glide` (ms to ~95%) with `legato` = slides only when notes overlap; `glideMode Always` = every note.
- `diveAmt/diveTime`: pitch falls after note-off (Travis-style tail drop). Needs release >= diveTime/2.
- `dirtDrive` max gain: Tape 28 dB, Fold 20, Crush 18, Fuzz 30, others 32. `subKeep` 0.6-0.85 keeps
  distorted 808s full; `xover` 90-140 Hz.
- Delay/chorus/space only process content above 200 Hz: on a clean sine 808 they are nearly inaudible,
  so give echo-centric presets enough harmonics (drive or an osc layer).
- `clip` adds density at a -1 dBFS ceiling; `outGain` is for loudness matching only.

## Style signatures used for SubGrit (researched, keep consistent)

- **Travis Scott**: dark and psychedelic. Tape/Tube saturation with high Sub Keep, long legato glides,
  release dives, darker high cut (3-6.5 kHz), chorus/space haze on the harmonics only. Utopia/FE!N rage:
  fuzz or clip with an octave-up square layer, squared bodies. "The Plan"-type pulse: resonant saw,
  tape, 1/16 tremolo, chorus width.
- **Mike Dean**: Moog (Minimoog Model D, Grandmother, Matriarch) synth bass: saw/square oscillators
  carry the sound and the 808 body is only a sub, driven resonant ladder filter with an envelope blip,
  portamento, analog drift, delayed vibrato for lead-bass, delay throws, plus his Yeezus-style
  distortion on 808s (amp-style with a 1 kHz honk).
- **untiljapan**: cinematic, polished, heavy and *echoed* bass. Every preset uses delay and/or space,
  lo-fi textures (crush, fold, rectify), slow filter breathing, vibrato ghosts, glue for polish.
- **Ye (Kanye)**, by era: 808s & Heartbreak = long, nearly clean TR-808 boom; Yeezus = industrial
  (amp/crush, square + saw, noise, stuttering drive, acid fold); TLOP/Donda = stadium distortion with
  huge space, organ-like sustained subs; Vultures = flat-topped, massively clipped 808s.
- **Bouncy 808s**: dry and simple. Punch + knock + short decay for bounce; clean sustained subs;
  drill slides; pluggnb soft and low-passed; Detroit kick-808; phonk fold+clip.

## Loudness reference (SubGrit)

Demo phrase target -9.5 LUFS, window -11.0 to -8.0, true peak <= -0.8 dBTP. Slammed presets end up
with outGain around -5 to -8 dB; clean percussive ones need clip 0.4-0.6 to reach the window.
