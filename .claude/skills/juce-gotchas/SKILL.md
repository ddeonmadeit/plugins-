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
- `juce_add_binary_data` names resources by bare file name: two `01 Init.json` in different folders
  collide. Prefix with a category-unique name or keep names unique (preset_lint checks this).
- Programs: `getNumPrograms()` must never return 0 (return 1 with "Init"). Expose factory presets only;
  the count must not change at runtime.
- The VST3 wrapper is built with LTO in Release: the first link takes ~3-4 min in the container. Iterate
  on `<Name>_Harness` (no LTO link, ~10 s incremental) and build `_VST3` only for pluginval.
- Fetching JUCE: use `-DFETCHCONTENT_SOURCE_DIR_JUCE=<checkout>` to avoid re-cloning; the tag lives in the
  root CMakeLists.txt. Read JUCE's BREAKING_CHANGES.md before bumping it.
