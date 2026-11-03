# ddeonmadeit plugins: agent guide

Monorepo for audio plugins (VST3 + AU + Standalone) built with JUCE 9 and CMake. The owner
uses **FL Studio on macOS**; every plugin must ship as a universal (arm64 + x86_64) VST3 and AU.
Development happens in a Linux container, and macOS binaries are built by GitHub Actions.

**Download page (permanent): https://ddeonmadeit.github.io/plugins-/**. CI rebuilds it on every push to
`main` and every release. Every plugin in `plugins/` appears there automatically; its download buttons go
live once it has a release. **Releasing = version bump**: when a push to `main` contains a plugin whose
`site.json` has `"publish": true` and whose `VERSION` (in its CMakeLists.txt) has no release yet, CI builds,
validates and releases it as `<Name>-v<version>`. New plugins start with `"publish": false`; flip it when the
owner is happy. To ship an update, bump `VERSION` and push. Details: `.claude/skills/mac-release/SKILL.md`.

Installers aren't notarized yet (that needs the owner's Apple Developer ID; CI is ready for it, see the
mac-release skill), so a double-clicked .pkg gets macOS's "Apple could not verify" warning. The no-warning
install is the Terminal command the page offers: `curl -fsSL https://ddeonmadeit.github.io/plugins-/install.sh | bash`.
When an author name is needed anywhere (signing, credits, metadata), the owner's is **DEON**.

## Skills (read the matching one before starting)

| Task | Skill |
| --- | --- |
| Start a new plugin (synth or effect) end to end | `.claude/skills/new-plugin/SKILL.md` |
| Check how a plugin sounds without ears: render, analyze, fix | `.claude/skills/audio-qa/SKILL.md` |
| Design bass / 808 / artist-style presets | `.claude/skills/preset-design/SKILL.md` |
| Sample-based instrument (real drums etc.): sourcing, library pipeline, streaming, shipping samples | `.claude/skills/sample-plugin/SKILL.md` |
| Build, sign, package and release for macOS / FL Studio | `.claude/skills/mac-release/SKILL.md` |
| JUCE 9 API gotchas hit so far | `.claude/skills/juce-gotchas/SKILL.md` |

## Layout

```
CMakeLists.txt            fetches JUCE (pinned tag), adds modules, adds every plugins/*/CMakeLists.txt
cmake/DDPlugin.cmake      dd_add_plugin(): plugin formats + embedded presets + <Name>_Harness
modules/dd_dsp            header-only DSP: PolyBLEP/unison osc, ZDF ladder, SVF, LR4 crossover, ADSR,
                          LFO, 8 distortion curves + multiband Dirt (4x OS), guitar CabSim, chorus, delay,
                          FDN reverb, glue comp, oversampled clipper, tape stop
modules/dd_core           PresetManager (JSON factory + user presets), param <-> JSON helpers,
                          replaceStateExactly(), ScopeBuffer (lock-free audio -> UI)
modules/dd_gui            house look (see "Visual style"): LookAndFeel/Palette + sunset colours, Knob/Choice/Toggle,
                          grid Panel, PresetBar, CRT Scope, TapeStopControl, materials (brushed metal backdrop/faceplate, inset,
                          bezel, lamp, screw, engraved/embossed/glowing text), embedded Barlow Condensed font (fonts/, SIL OFL)
plugins/<Name>/           one plugin: CMakeLists.txt, Source/, Presets/<CATEGORY>/NN_Name.json, qa.json,
                          site.json (tagline, description, accent, highlights, installTips for the download page),
                          optional library/ (sample library builder) and README-mac.txt (zip README notes)
templates/plugin/         scaffold used by tools/new_plugin.py (//@SYNTH //@EFFECT blocks)
tools/harness/            generic headless CLI compiled per plugin (render any notes at any tempo, snapshot, stress, param-info)
tools/qa/                 analyze.py (objective audio QA), variety.py (presets = different instruments?), spectrum.py (tonal balance),
                          tone.py (match a reference sound: width, tremolo, warble, octave balance),
                          preset_lint/fill/normalize, run_qa.sh, drum_velocity.py
tools/ci/                 plugin_meta.py (reads dd_add_plugin args + site.json), package_macos.sh (+ sample libraries),
                          macos_signing.sh + notarize_macos.sh (Developer ID, active once the owner adds the secrets)
tools/site/               make_previews.py, build_site.py + template.html (download page), install.sh (Terminal
                          installer template), release_notes.py
packaging/macos/          Install.command + README.txt shipped in the zip
.github/workflows/        build.yml: sample libraries (built once, cached) -> Linux QA + previews and macOS
                          universal build/sign/package/validate -> GitHub Release -> download site (gh-pages)
```

## Everyday commands

```bash
# Configure once. Reuse a local JUCE checkout to skip the 100 MB clone:
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release [-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE]

cmake --build build --target SubGrit_Harness -j4          # fast: no plugin wrappers, no LTO link
tools/qa/run_qa.sh SubGrit                                # lint, render, analyze, stress, UI snapshot
PLUGINVAL=/path/pluginval tools/qa/run_qa.sh SubGrit      # + pluginval (build SubGrit_VST3 first)

H=build/plugins/SubGrit/SubGrit_Harness_artefacts/Release/SubGrit_Harness
$H list                                                    # factory presets (= host programs)
$H render --program 3 --out /tmp/x.wav                     # one preset, demo phrase
$H render --preset-file p.json --set cutoff=300 --out x.wav  # audition JSON without rebuilding
xvfb-run -a -s "-screen 0 2560x1600x24" $H snapshot --out ui.png   # look at the editor
```

Linux build deps (already present in the dev container image): ninja, libasound2-dev, libx11-dev,
libxrandr-dev, libxinerama-dev, libxcursor-dev, libxcomposite-dev, **libxi-dev (JUCE 9)**,
libfreetype-dev, libfontconfig1-dev, xvfb; Python: numpy, scipy, soundfile, matplotlib.

## Conventions

- **Target name == PRODUCT_NAME** (CI scripts assume `<Name>.vst3`). CamelCase, no spaces.
- **PLUGIN_CODE** is 4 chars with at least one uppercase letter, unique in the repo (new_plugin.py checks).
  Manufacturer code is `Ddeo` for everything. Never change either after release: hosts key presets/projects on them.
- **Parameters**: one table in `Source/Params.cpp` (enum order == table order, asserted). IDs are permanent:
  renaming one breaks saved sessions and presets. Ranges in real units, skew via `centre`.
- Audio thread reads params through `ParamCache` (raw atomics). Never lock, allocate or log there.
- Use `DD_PLUGIN_NAME` / `DD_PLUGIN_VERSION` / `DD_PLUGIN_COMPANY`, not `JucePlugin_*`, so sources also
  compile into the harness.
- `setStateInformation` must use `dd::replaceStateExactly()` (pluginval fails with plain `replaceState`).
- Process audio in fixed internal chunks (SubGrit: 64) so oversamplers and smoothers see bounded sizes;
  never assume the host block size.
- Performance switches (`perf*` ids, e.g. `perfPoly`) belong to the instance, never to presets; see
  new-plugin skill. Every synth plays chords (POLY switch) even when its presets are mono.
- Presets must be genuinely different instruments, not one patch with different effects: plan roles per
  category and check `tools/qa/variety.py` (preset-design skill).
- Presets: JSON in real units, **every parameter listed** (run `tools/qa/preset_fill.py`), loudness-matched
  through the output gain param (`tools/qa/normalize_presets.py`). File names must be unique across folders
  (binary data uses the bare file name). Category order comes from the processor's `categoryOrder()`.
- Code style follows JUCE: 4 spaces, `func (args)` with a space, braces on new lines, sparse comments that
  explain why. Build must stay warning-free under `juce_recommended_warning_flags`.
- Don't commit `build/`, `qa-out/`, `dist/`.

## Visual style (owner's brief: every plugin)

**Extremely realistic hardware** (owner's brief, Nov 2026): panels are **brushed brown metal** (the owner likes
the brown; keep `Palette::panel` / `background`, measured hue ~26°, saturation ~0.33), lit and shiny, with
**machined silver** parts on them: knobs, screws, keys, bezels, pedal enclosures. Everything is embossed or
debossed. Warm colour (`dd::gui::sunset::red / ember / orange / amber / yellow`) appears **only where a real
light would glow**: lamps, backlit knob slots, display glass, lit key legends, scope trace, spotlights. No
printed colour, no blue, green, teal, purple or neon.

- Light comes from above: top edges catch it, parts cast soft shadows down onto the plate, anything cut into
  the metal is shaded under its top wall and lit on its lower lip.
- Materials are in `dd_gui` (dd_Theme): `paintBackdrop` (darker brushed chassis), `dd::gui::Panel` /
  `paintFaceplate` (raised brushed plate, machined edge, silver Phillips screws), `paintBrushedMetal` (any
  metal surface, cached brushed texture + sheen), `paintInset` (window milled into the plate: chamfer, smoked
  glass), `paintBezel` (silver frame around a scene window), `paintLamp` (chrome bezel, glowing glass).
  Knobs (LookAndFeel) are machined silver: knurled skirt, polished chamfer, spun top (cached image), black
  pointer groove, sitting in a backlit slot that glows in the knob's accent. Keys are silver with engraved
  or backlit legends; combo boxes and menus are dark glass with amber text (`Palette::glass`).
- Text: `drawPrinted()` engraves (light colours: cut through the brown to bare metal; dark colours: engraved
  into silver), `drawEmbossed()` for raised chrome wordmarks (next to `paintSunsetStripes`, which now cuts three
  machined grooves), `drawGlowing()` for light behind glass. Parts that are bare silver (pedals) use a
  `LookAndFeel` built from `dd::gui::onSilver (palette)` so their labels engrave dark.
- Use `LookAndFeel::font()` / `display()` for all text (embedded font, identical on macOS and Linux).
- **No flat, dull or muddy objects** (owner, Afterglow 1.0 feedback): everything has depth (shadows, bevels,
  sockets, inner shadows, glows); metal needs its sheen and brushing, never a flat fill.
- Drawn scenes (Kitroom's kit, Sunburst's stage, Afterglow's horizon) are seen through a silver-bezelled
  window: dark room, warm tungsten spotlights from above, film grain and vignette, crisp labels on top.
- Check a change at `snapshot --scale 2` too: brushing, chamfers and glows only read right up close.

## Quality bar (what "done" means for a plugin)

1. `tools/qa/run_qa.sh <Name>` passes: no NaN/Inf, true peak <= 0 dBTP, no DC, sub mono, presets within
   the plugin's loudness window, no near-duplicate presets, stress test clean at 1 / 128 / 1024 samples and 96 kHz.
2. pluginval strictness 8 passes on Linux (and on macOS in CI), auval passes in CI.
3. You looked at the UI snapshot and the spectrogram sheet (`qa-out/<Name>/renders/sheet.png`).
4. The macOS job is green and its artifact contains `<Name>-<ver>-macOS.pkg` and `.zip`.
5. Released: the Release `<Name>-v<ver>` has both assets, and the download page shows the new version
   with working links (check https://ddeonmadeit.github.io/plugins-/ after the `site` job).

## Plugins

| Plugin | Kind | Code | Notes |
| --- | --- | --- | --- |
| SubGrit | synth | Sgrt | 808 + distorted bass, POLY switch. 94 presets: TRAVIS, MIKE DEAN, UNTILJAPAN, YE, YEEZUS, MOSHC0RE, KING, TRIP HOP, SAFE, BOUNCE 808 |
| Kitroom | synth | Ktrm | Live acoustic drum kit from multi-mic DrumGizmo recordings (CC BY 4.0): drawn kit UI, GM keys, swappable pieces, close/OH/room mics, bus drive/crush/tone. 40 presets: STUDIO, LIVE ROOM, VINTAGE, TIGHT, BIG, YEEZUS, MOSHC0RE, KING, TRIP HOP. Local QA needs `KITROOM_LIBRARY` (build it with `plugins/Kitroom/library/build_library.py`) |
| Sunburst | synth | Sbst | Sampled acoustic + electric guitars (CC0: Karoryfer Shinyguitar / Emilyguitar / Black And Green, FreePats nylon), six-string engine (string assignment, strum, hammer-on / slide legato, palm mute, double-track), String Noise knob, pedalboard (comp, drive, fuzz, chorus, synced trem, delay), Clean / Crunch / Lead amp + cab + room. Drawn guitar UI with lit strings. TAPE STOP. 35 presets: ACOUSTIC, CLEAN, CRUNCH, HEAVY, TRAVIS SCOTT, MIKE DEAN, YEEZUS, MOSHC0RE, TRIP HOP, SAFE. Local QA needs `SUNBURST_LIBRARY` (`plugins/Sunburst/library/build_library.py`) |
| Afterglow | synth | Aglw | Pads, keys, plucks, leads, bells, soundscapes, electric guitar. Deep two-layer engine (12 sources incl. pluck, organ, mallet, sync, Electric guitar string) + Dirt amp + CAB + strum + FX, but the panel is macro-first: a sunset "orb" display (Motion / Tone), 6 macros, POLY switch, TAPE STOP. 89 presets (each a different role): TAME IMPALA, MASSIVE ATTACK, BRENT FAIYAZ, MIKE DEAN, TRAVIS SCOTT, YEEZUS, MOSHC0RE, GUITAR, TRIP HOP, SAFE |
