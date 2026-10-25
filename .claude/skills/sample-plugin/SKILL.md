---
name: sample-plugin
description: Build a sample-based instrument (acoustic drums, real instruments) in this repo - find licensable multi-sampled recordings, turn them into a compact library, stream them in a JUCE plugin with velocity layers and round robin, ship the library in the macOS installer. Use for any plugin that must sound like a real recorded instrument rather than a synth.
---

# Sample-based instruments (worked example: plugins/Kitroom)

"Authentic / real sounding" means real recordings. Synthesis or a handful of one-shots sounds fake in
seconds. Kitroom plays multi-mic drum kits with ~10-60 velocity layers per articulation.

## 1. Source recordings you can redistribute

- **DrumGizmo kits** (https://drumgizmo.org/wiki/doku.php?id=kits:drskit and `id=kits:muldjordkit`), CC BY 4.0: DRSKit 2.1 (studio kit incl.
  brushes) and MuldjordKit 3 are used. 13-16 mic channels, 44.1 kHz float WAV, many hits per
  instrument, XML maps instrument -> hits -> channel files. Credit is required: keep ATTRIBUTION.txt
  in the library and the credit line in the UI.
- Check every license for redistribution in a commercial-or-free plugin. Avoid NC licenses.
- Record the md5 of each download in `library/sources.json`; the builder verifies it.

## 2. Library pipeline (`plugins/<Name>/library/`)

`build_library.py --kits DIR --out DIR [--download] [--discard-kits] [--only ids]` and `sources.json`
(kit list, mic channel names, which instruments make up each sound and articulation, processing
constants). What it does per sound, and why:

- Mix down to **5 stems**: close (paired close mics summed after polarity + lag alignment on the
  loudest hit, lag search limited per drum: snare 40, kick 150 samples, otherwise periodic tones alias),
  overheads L/R, room L/R. Users get Close / Overheads / Room faders, which is what makes it sound
  like a recorded kit instead of samples.
- Keep the close-vs-overhead arrival time as recorded (that delay *is* the realism).
- Balance close vs overheads per drum type, trim lead-in (-2 ms before 1% of peak) and tail
  (-72 dB + 30 ms; cymbals capped with a 1 s fade), normalize the sound to a per-category peak target,
  cap the boost (+20 dB) so quiet brush recordings don't bring their noise floor up.
- Measure **loudness** of each hit (RMS of the first 60 ms of the default mix) for velocity mapping.
- Measure **closePan**: which overhead hears the piece first (spaced pair) -> pan position for the
  close mic so both images agree. Kick/snare forced to 0. Per-kit `overheadTrimDb` /
  `overheadCentreLag` correct a lopsided overhead pair.
- Write 16-bit FLAC per hit (`sounds/<id>/<art>/NN.flac`) + `catalog.json`. 31 sounds = 225 MB.

Iterate with `--only id,id` (merges into the existing catalog). Full build: ~2 min locally, raw kits
~8 GB unpacked, so CI uses `--download --discard-kits` (one kit on disk at a time).

## 3. Plugin side (copy from Kitroom)

- `Library.h/.cpp`: finds the library via `$<NAME>_LIBRARY`, then
  `~/Library/Application Support/ddeonmadeit/<Name>/Library`, then `/Library/...`; parses the catalog;
  `load()` decodes one sound to int16 in memory (FLAC via AudioFormatManager).
- `SoundLoader` (a juce::Thread): per-slot `request(id)`, the audio thread reads an atomic raw pointer;
  swapped-out sounds sit in a retire pool for 30 s before being freed, and all refcount changes happen on
  the loader thread, so the audio thread never frees memory.
- **Offline renders**: in `processBlock`, `if (isNonRealtime()) loader->waitUntilIdle();` FL's export and
  the harness then never render silence while samples load. The harness sets non-realtime for
  render/snapshot and runs one offline warm-up block before `stress`.
- `DrumEngine`: 96 voices, at most 6 per articulation (oldest fades 20 ms), Hermite interpolation per
  stem (tune = playback rate), choke groups (closed/pedal hat fade open hats over 45 ms), articulation
  fallback chain (half-open -> open, rimshot -> center).
- **Velocity**: target loudness = loudest layer + 40*log10(v) (GM/DLS curve, bent by a Velocity knob);
  pick among the 3-4 layers nearest that loudness, never the one just played (round robin); make up
  the rest with gain (-30..+1.5 dB). Picking by *rank* instead squashes dynamics, because recordings
  have many loud hits and few soft ones.
- Kit choice (which sound in each slot) is not a parameter: it lives in the preset JSON under
  `"extra": {"kit": {...}}` via `PresetManager::onApplyExtra / onCaptureExtra`, and in the state XML.
- Key map: per-articulation primary keys + GM aliases in a `std::array<std::atomic<int>,128>`; MIDI learn
  by polling from a UI timer. Show keys in **FL Studio note names** (MIDI 60 = C5).
- Master: width on M/S with an LR4 high-pass on the side (lows stay mono whatever the rooms do), glue,
  clipper, output gain.

## 4. UI

A drawn instrument beats a grid of knobs here: Kitroom's `KitView` paints every piece with Graphics
paths (cymbal gradients, lacquered shells in the kit's finish colour, chrome hoops), flashes pieces on
hits, shows NAME + key pills, click = select + audition. The selected piece's editor (`ElementPanel`)
has the sound swap combo, articulation rows with key learn, and per-piece knobs.

## 5. QA specifics

- Harness phrases: `drums` (5 bars at 96 BPM: groove with ghost notes, ride bar, tom fill, crashes)
  for presets/previews; `drumvel` for `tools/qa/drum_velocity.py` (velocity response per note).
- `qa.json`: `"phrase": "drums"`, loudness target -18 LUFS +-2 (acoustic drums have ~20 dB crest),
  `"previewPhrase": "drums"`, `"previewBacking": false` (no synthetic hats under real drums).
- Run QA with `KITROOM_LIBRARY=<built library>` or every render is silent.
- Normalize loop: normalize -> build -> render-all -> **analyze** -> normalize again (normalize reads
  report.json; skipping analyze applies the same correction twice).

## 6. CI and installers (already generic)

- `libraries` job: builds every `plugins/*/library/build_library.py` once, caches `libraries/` keyed on
  the hash of `plugins/*/library/**` (`enableCrossOsArchive`), so both Linux QA and the macOS job restore
  it. Bump `sample-libraries-vN` in the key to force a rebuild.
- Both jobs export `<NAME>_LIBRARY=$PWD/libraries/<Name>`.
- `package_macos.sh build dist libraries`: the .pkg gets a third component installing the library to
  `/Library/Application Support/ddeonmadeit/<Name>/Library`; the .zip carries `<Name> Library/`, which
  `Install.command` copies to `~/Library/Application Support/...`.
- Plugin-specific install text: `plugins/<Name>/README-mac.txt` (appended to the zip README) and
  `site.json` `installTips` / `previewNote` (download page + release notes).
