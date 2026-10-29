---
name: preset-design
description: Design factory presets that sound finished and fit a named style (808s, distorted bass, artist-inspired categories like Travis Scott, Mike Dean, untiljapan, Kanye), then lint, fill, loudness-match and de-duplicate them. Use when creating or changing presets for any plugin here.
---

# Preset design

## Workflow

1. **Define each category's signature first** (what every preset in it shares, and what no other
   category does). Write it down in the generator or a comment; it stops presets converging.
2. Author presets as JSON (`plugins/<Name>/Presets/<CATEGORY>/NN_Name.json`). Only the values that
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
9. Variety: `python3 tools/qa/variety.py qa-out/<Name>/renders` compares how presets *play* (attack,
   sustain, articulation, tail, brightness, noise, width, movement). Fix pairs under ~0.8.
10. Look at `qa-out/<Name>/renders/sheet.png`: every tile should look different.

**Variety rule (owner feedback on Afterglow 1.0: "most of them sounded like the same sound with
different effects").** A bank must not be one patch with different FX. Plan each category as a set of
ROLES before writing values: pad, keys, pluck, lead, bell/mallet, texture/soundscape, bass/drone,
strings/brass/choir, one EXTREME (pushed hard: distortion, gates, wow, crush) and one SUBTLE (dry, soft,
close, almost no FX). No two presets may share source + envelope class + register. Prefix extreme and
subtle descriptions with "EXTREME:" / "SUBTLE:" so the owner can find them.

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
- **YEEZUS** (its own category in every plugin): industrial, abrasive and minimal, produced by Daft Punk,
  Gesaffelstein, Arca, Hudson Mohawke, TNGHT and Mike Dean, reduced by Rick Rubin. Track references:
  On Sight (squelching acid, fuzz), Black Skinhead (stomping tribal floor toms, heavy breathing),
  I Am A God (crushed hits, screams), New Slaves (one bare buzzing synth), Hold My Liquor (dark
  distorted pads), Send It Up (alarm-like synth), Blood On The Leaves (TNGHT horn blasts), Bound 2
  (dusty soul break). Mix the extremes: some presets brutal (amp, rectify, crush, gates), some bare and
  dry (the Rubin reduction).
- **MOSHC0RE** (category in every plugin): moshc0re is an underground rapper who self-produces in the
  Playboi Carti / Ken Carson (Opium) rage lane and tags #techno; titles are sci-fi and brutalist
  (INTERGALACTIC, ALIEN, SYNTHETIC, BRUTALIST, C0RPROGANDA, EGO DEATH, LIGHTS LIGHTS LIGHTS, LONESOME;
  INTERGALACTIC ~119 BPM, SEMPER FI ~84 / 168). Researched from tags, titles and BPMs, not by ear: ask the
  owner for reference tracks before reworking it. Signature: blown-out clipped rage 808s with long slides,
  bright detuned supersaw rage leads, techno (kick-808s, EBM 16th pulses, S&H sequences, dub-techno chords,
  hoovers, strobes), alien textures (theremin, wobble, 8-bit), gothic vamp choir and organ, one bare SUBTLE preset.
- **KING** (SubGrit + Kitroom): Ye's KING, the opener of BULLY (2026), industrial hip-hop compared to
  Yeezus, TLOP and a darker MBDTF. Reviews describe a heavy bass line loud in the mix "made to be blasted
  throughout a stadium", heavy drums, a repeating alarm/ringing-phone sound and a looped gospel-preacher
  sample (Duke Edwards). Researched from reviews, not by ear: recreations, never the record's audio.
  Signature: a mid-forward stadium bass line (sub + saw/square, tube), a long taped 808, alarm-rhythm
  gated grit, industrial stomp, kick-808 boom; kits huge and glued or hammered and dry, one soft church room.
- **TRIP HOP** (SubGrit + Kitroom + Afterglow): measured from a reference beat the owner sent ("The Wall",
  Massive Attack / Sneaker Pimps type, 79 BPM, D / G minor). Pure sine sub with a soft attack; mono dusty
  break (47 Hz kick, fat 195 Hz snare, dark hats, tape + light crush); warm clean guitar with a 1/16 Trem pump,
  wide chorus warble and a dark room; scooping "aah" choir lead; hollow square keys with dub echo; slow wide
  high strings. Matching method: audio-qa skill, "Matching a reference track".
- **SAFE** (SubGrit + Afterglow; owner said no drums): SAFE (Saif Musaad), Toronto R&B / trap, sing-rap;
  reviews: moody R&B with a street edge, "evocative sub-bass and vaporous atmospherics"; STAY (2019) adds
  dancehall and afrobeat (DJ Dahi, T-Minus, Doc McKinney, Pluss on Paid In Full ft. Carti). Researched from
  reviews and credits, not by ear. Signature: underwater low-passed 808s and keys, dark 80s synth bass and
  lead (Doc McKinney), afro / dancehall plucks (marimba, muted guitar, log bass), reverse-swell haze,
  vocal chops. Keep it apart from BRENT FAIYAZ (bright Rhodes, airy choir) and MASSIVE ATTACK (kalimba).
- **Bouncy 808s**: dry and simple. Punch + knock + short decay for bounce; clean sustained subs;
  drill slides; pluggnb soft and low-passed; Detroit kick-808; phonk fold+clip.

## Guitar presets (Sunburst)

Each preset is a guitar part: pick the instrument first (acoustic, nylon, jazz box, solidbody, orange / club
hollow), then the playing (pick, palm mute, strum, ring, legato, double), then the rig. Two presets that share
instrument + amp channel + legato style read as duplicates on the `guitar` phrase (analyzer < 0.15); change
the guitar or the register (transpose +12 for a high lead). Amp channels are level-matched across Gain, so
`outGain` stays within a few dB. Target -14 LUFS +-3 on `guitar`; clean plucked presets are peak-limited:
give them glue rather than gain.

## Loudness reference (SubGrit)

Demo phrase target -9.5 LUFS, window -11.0 to -8.0, true peak <= -0.8 dBTP. Slammed presets end up
with outGain around -5 to -8 dB; clean percussive ones need clip 0.4-0.6 to reach the window.

## Kit presets (Kitroom)

A drum preset = mic balance + tuning + bus processing + which recorded sound sits in each slot. The
sound choice lives in `"extra": {"kit": {"kick": "drs_kick_dead", ...}}` (PresetManager extra hooks), so
`preset_fill.py` keeps it. Character comes from: tuning (jazz high, ballad low), Decay (dead 70s toms),
Close vs Overheads vs Room, glue/clip for density, width. Target -18 LUFS +-2 on the `drums` phrase;
clean presets are peak-limited (crest ~21 dB), so give quiet ones a little glue rather than gain.

## Pad / texture presets (Afterglow)

Signatures used (keep them apart): **Tame Impala** = Juno PWM saws + heavy phaser + tape wow + Currents-style
pump, glide leads with vibrato. **Massive Attack** = dark (cutoff < 1.5 kHz, high cut 6-9 kHz), dub delay with
dark feedback, vinyl, slow LFO 2 filter breathing, eerie choir, drones. **Brent Faiyaz** = Tines Rhodes with
tremolo, airy choir, soft attacks, gentle tape and lo-fi, nothing harsh. **Mike Dean** = big saws, resonant
ladder sweeps from the mod envelope, legato glide solos with delayed vibrato, brass swells, huge halls.
**Travis Scott** = eerie detuned FM bells, vocal textures, flute leads (sine + pitched air), 16th gates,
bitcrushed drones, very long dark reverb with shimmer. **Yeezus** = see above: Sync screams, square alarms,
amp-driven horn stabs, rectified buzz, gated breath. Every preset sets all FX switches explicitly; macros
stay at 0 in factory presets.

Afterglow sources (shape / char): Analog (saw->pulse / PWM), Strings (brightness / 4'), Choir (vowel
A-E-I-O-U / breath), Glass (FM index / ratio), Tines (bark / tine), Air (breath / pitchedness), Sine
(->triangle / octave), Pluck (brightness / ring time 0.35-16 s: harp, harpsichord, nylon), Organ (drawbars
flute 0, jazz .25, gospel .5, reed .75, full 1 / percussion+click), Mallet (hardness / bar: marimba <.2,
vibes <.4, kalimba <.6, glock <.8, steel), Sync (slave ratio 1-8x, sweep it with Shape Env / master saw
blend), Electric (pickup 0 neck .. 1 bridge / sustain 0 palm-muted .. 1 singing). Pluck, Mallet and
Electric ring by themselves: give them amp sustain 1 and let the source decay.

Electric guitar presets (GUITAR): Electric source, voice filter open (it is the guitar's tone knob), Dirt as
the amp (Tube = clean/edge, Amp = crunch/high gain, Fuzz = pedal) with CAB on (1x12 Combo or 4x12 Stack),
`strum` 4-14 ms for chords, amp release 70-260 ms (fingers muting the strings). With CAB on the processor
tightens the lows before the drive, distorts the full range and adds a tone stack. Judge the tone with
`tools/qa/spectrum.py` on the renders: crunch ~flat 160 Hz-2.5 kHz, -20 dB by 6 kHz, -50 dB by 10 kHz.
Double-track rhythm parts with a second Electric layer panned opposite and a few cents apart. QA and
previews use the `mixed` phrase (held chord, arpeggio, stabs, legato melody) so plucks are heard fairly.
The generator lives outside the repo; the JSON files are the source of truth.

