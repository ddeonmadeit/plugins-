---
name: juce-gotchas
description: JUCE 9 / CMake API details and pitfalls already hit in this repo (compile errors, pluginval failures, editor sizing, warnings). Read before writing JUCE code here or when a JUCE build or validation fails.
---

# JUCE 9 gotchas (pinned 9.0.4)

- Linux needs **libxi-dev** (new in JUCE 9) on top of the usual X11/ALSA/freetype packages.
- `juce_recommended_warning_flags` is strict: `-Wfloat-equal` (use `juce::exactlyEqual` /
  `juce::approximatelyEqual`), `-Wswitch-enum` (list every enum value, no `default:`),
  `-Wsign-conversion` (cast indices `(size_t)`), `-Wshadow` (lambda locals vs members).
- `createPluginFilter()` is already declared by juce_audio_processors; don't redeclare it.
- `AudioProcessor::createEditorIfNeeded()` is deprecated: use `createEditorAndMakeActive()`.
- `MessageManager::runDispatchLoopUntil()` doesn't exist without modal loops; headless snapshots just
  call `createComponentSnapshot()` after `setSize()` (timers won't have run, so scopes look empty).
- WAV writing: `WavAudioFormat::createWriterFor (std::unique_ptr<OutputStream>&, AudioFormatWriterOptions{}
  .withSampleRate().withNumChannels().withBitsPerSample (32).withSampleFormat (floatingPoint))`.
- `apvts.replaceState()` skips parameters whose snapped value already matches (a bool at 0.18 reads
  "off"), so pluginval's state-restore test fails. Use `dd::replaceStateExactly()`.
- `AudioProcessorEditor::setResizeLimits()` calls `resized()` immediately, before your saved zoom is
  applied. Read saved state first and only store the zoom from `resized()` after construction.
- `juce::dsp::Oversampling<float> (2 ch, factor 2 = 4x, filterHalfBandPolyphaseIIR, maxQuality, integerLatency=true)`:
  integer latency lets you delay dry paths exactly. `initProcessing (maxBlock)` must cover the largest
  chunk you pass, so process in fixed chunks.
- `juce_add_binary_data` names resources by bare file name: two `01_Init.json` in different folders
  collide, so keep file names unique (preset_lint checks this). Use only letters, digits, `_` and `-`:
  Makefile generators choke on `:` (a `4:20 ...json` preset broke the macOS build).
- Programs: `getNumPrograms()` must never return 0 (return 1 with "Init"). Expose factory presets only;
  the count must not change at runtime.
- The VST3 wrapper is built with LTO in Release: the first link takes ~3-4 min in the container. Iterate
  on `<Name>_Harness` (no LTO link, ~10 s incremental) and build `_VST3` only for pluginval.
- Fetching JUCE: use `-DFETCHCONTENT_SOURCE_DIR_JUCE=<checkout>` to avoid re-cloning; the tag lives in the
  root CMakeLists.txt. Read JUCE's BREAKING_CHANGES.md before bumping it.
- `-Wshadow` (part of the recommended warnings) fires on locals named like members, e.g. a FIFO
  `const auto scope = fifo.write (1)` inside a processor that has a `scope` member. Name them `w` / `rd`.
- An `AudioProcessor` that streams samples should honour `isNonRealtime()` (wait for loads): FL's export
  and offline bounces render faster than real time and would otherwise skip notes.
- Embedded fonts: `juce_add_binary_data (dd_fonts ...)` in the root CMakeLists, linked into every plugin and
  harness by `dd_add_plugin`; dd_gui includes `<DDFonts.h>` behind `__has_include`. Load once with
  `Typeface::createSystemTypefaceFor (data, size)` and build fonts as `juce::Font (juce::FontOptions (tf).withHeight (h))`.
  `getTypefaceForFont` only affects the *default* LookAndFeel, so route every font through `LookAndFeel::font()`.
- Static lighting / texture masks (film grain, Kitroom's spotlight darkness) are computed once into a
  `juce::Image` with `BitmapData` and drawn each paint; per-pixel work in `paint()` is too slow at 30 fps.

- `juce::SmoothedValue` starts from its constructor value, and `reset (sr, seconds)` keeps the current value.
  `dd::Dirt` declared `mixSm { 1.0f }`, so after every prepare the first 20 ms ramped from full drive to the
  preset's mix: the first note of every offline render (QA, previews) was distorted even with Dirt off.
  Snap smoothers to their targets (`setCurrentAndTargetValue`) on the first process after a reset; look for a
  loud, bright first 10-20 ms in renders as the symptom.
