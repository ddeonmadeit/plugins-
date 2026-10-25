---
name: new-plugin
description: Create a new audio plugin (synth or effect) in this repo end to end - scaffold, parameters, DSP, UI, presets, QA and macOS build. Use when the user asks for a new plugin, instrument, effect or VST/AU.
---

# New plugin, end to end

Order matters: each step has a check before the next one starts. If the plugin must sound like a real
recorded instrument (acoustic drums, piano, guitar...), read `.claude/skills/sample-plugin/SKILL.md`
too: it covers sourcing recordings, the sample library pipeline and shipping the library.

## 1. Scaffold (2 min)

```bash
python3 tools/new_plugin.py MyThing --kind synth      # or --kind effect [--code Mthg]
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_SOURCE_DIR_JUCE=<juce checkout if you have one>
cmake --build build --target MyThing_Harness -j4
tools/qa/run_qa.sh MyThing                             # template passes QA out of the box
```

The template already has: param table + ParamCache, PresetManager wired to embedded JSON presets,
programs exposed to the host, exact state restore, chunked processing, oversampled clipper, scope,
dd_gui editor with preset bar. Synth = 8-voice poly saw/SVF; effect = dd::Dirt.

Then add a row to the Plugins table in CLAUDE.md.

## 2. Parameters

Edit `Source/Params.h` (enum) and `Source/Params.cpp` (table), keeping the same order. Pick real-unit
ranges, `centre` for log-feel (Hz, ms), and a `Unit` for display. Choice lists: reuse
`dd::waveNames()`, `dd::dirtTypeNames()`, `dd::divisionNames()`, `dd::Lfo::shapeNames()` where possible.
IDs are permanent once shipped.

## 3. DSP

Reuse `modules/dd_dsp` before writing anything new; add genuinely reusable pieces there (header-only,
`namespace dd`, no allocation in process). Patterns that worked in SubGrit:

- Synth voice: a struct of resolved per-chunk params (`VoiceParams`) built once per block from ParamCache;
  voices render into a stereo scratch; global FX run per 64-sample chunk.
- Mono bass voice management with choke + legato glide: copy `plugins/SubGrit/Source/SynthEngine.h`.
- Distortion that stays heavy: `dd::Dirt` (multiband, `subKeep` blends the clean low band back, static
  gain compensation so drive changes tone not level).
- Keep sub mono by splitting the *side* signal only (SubGrit `processChunk`); never run the mid through
  a crossover (it smears transients).
- Use `dd::Clipper` last before output gain; DC-block before and after it.
- Sanitize: if a chunk produces NaN/Inf, zero it and reset all state (see SubGrit).

Check: `$H stress` clean, `$H render` sounds right in `analyze.py`.

## 4. UI

Build the editor from `dd::gui::Panel` grids (`add<Knob>`, `add<Choice>`, `add<Toggle>`, `place()` for spans).
Give the plugin its own `Palette` accent. For big UIs copy SubGrit's scaled `Content` component pattern
(fixed base size, `setTransform`, aspect-locked resizing, zoom stored in state).

Check: `xvfb-run -a -s "-screen 0 2560x1600x24" $H snapshot --out ui.png` and *look at the image*.
Watch for truncated combo text (keep choice names short) and empty cells.

## 5. Presets

Follow `.claude/skills/preset-design/SKILL.md`. Set `qa.json` (categories, loudness target, tolerance).

## 6. QA + ship

```bash
cmake --build build --target MyThing_Harness MyThing_VST3 -j4
PLUGINVAL=<pluginval> tools/qa/run_qa.sh MyThing
```

Fill in `plugins/MyThing/site.json` (tagline, description, accent colour, highlights, optional
`installTips` and `previewNote`): it drives the plugin's card on the download page and its release
notes. Plugin-specific notes for the zip's README go in `plugins/MyThing/README-mac.txt`. Leave `"publish": false` until the owner has
tried it; setting it to true (with an unreleased VERSION) releases it on the next push to main. Then follow `.claude/skills/mac-release/SKILL.md`
to release it. The workflow builds every plugin under `plugins/` automatically and the download page
picks it up; nothing to add to CI.
