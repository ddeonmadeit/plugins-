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
A push builds only the plugins it changed plus releases due (`tools/ci/pick_plugins.sh`; shared code = all, CI / site / docs = none),
and the site keeps the published previews of plugins not rebuilt, so a one-plugin change is live in ~15 min.

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
modules/dd_take           capture-then-edit plugins (Contour, Hush): TakeEngine (transfer state machine, worker, take files,
                          state, sample-rate changes, crossfaded playback, bounce; subclasses add their model), TakeStore,
                          CaptureFifo, Published<T> (hazard-pointer hand-over), SpanSet, offline pitch tracker, FLAC takes
modules/dd_gui            house look (see "Visual style"): DARK/LITE theme tokens, LookAndFeel, Knob (eases to new values)/Choice/Toggle/LampSwitch,
                          HoverHelp (controls carry their param id; DescriptionLine::setHelpTable explains whatever is under the mouse),
                          grid Panel, TopBar + PresetBar keys + DescriptionLine, Atmosphere, console Scope, TapeStopControl,
                          materials (keycaps, lit keys, trays, switches, console, wells), embedded Instrument Sans (fonts/, SIL OFL)
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
xvfb-run -a -s "-screen 0 2560x1600x24" $H snapshot --out ui.png   # look at the editor (--hover x,y: as if pointing there)
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
- Presets: users can save over any preset (a factory one gets a user override that survives updates) or save as new, and
  star favourites (Music/ddeonmadeit/<Plugin>/Favourites.json, keyed by category + name, a FAVOURITES browsing mode in the
  PresetBar); see `dd_PresetManager.h`, tested by `tools/tests` (run in CI).
- Presets: JSON in real units, **every parameter listed** (run `tools/qa/preset_fill.py`), loudness-matched
  through the output gain param (`tools/qa/normalize_presets.py`). File names must be unique across folders
  (binary data uses the bare file name). Category order comes from the processor's `categoryOrder()`.
- Every control explains itself: give the editor's DescriptionLine (or a scene's HoverHelp) a help table with one
  plain-words line per parameter id, and `dd::gui::setHelp` / `tagParam` for custom controls. The owner does not
  read manuals; if a knob needs explaining, its help line is where it goes.
- Code style follows JUCE: 4 spaces, `func (args)` with a space, braces on new lines, sparse comments that
  explain why. Build must stay warning-free under `juce_recommended_warning_flags`.
- Don't commit `build/`, `qa-out/`, `dist/`.

## Visual style (owner's brief: every plugin)

**The rawleads design** (owner, Nov 2026: "match the latest version of this software i made",
https://github.com/kodiakskode/rawleads, `src/outreach.css`): a "Precision Industrial Instrument", Braun-like.
Every plugin follows it exactly; it replaced the earlier brushed-brown-metal look.

- **Two themes, DARK and LITE**, switched by the `ThemeSwitch` in every top bar. The choice is global (all open
  editors of all plugins follow it, with a 280 ms crossfade) and saved in `ddeonmadeit/ui.settings`. Colours
  come from `dd::gui::tokens()` (room, chassis, rail, metal, keycap, tray, edge light/dark, cream text shades,
  signal); never hard-code a theme colour. `DD_THEME=light|dark` forces one for headless snapshots.
- **One signal colour**: the orange of an incandescent lamp behind smoked plastic (#c5602c / glow #e0793f;
  `dd::gui::sunset::*` are its shades). It appears only where a light glows: lit keys, LEDs, knob arcs and
  pointers, the console trace, the accent half of the wordmark. No other hues.
- **Layout**: the room, a chassis with rounded top corners, then `dd::gui::TopBar` (two-tone brand + subtitle,
  the preset keytray with the lit preset key in the middle, the plugin's own switches via `addRight`, DARK/LITE),
  `dd::gui::DescriptionLine` under it (preset description rising in, version hint on the right), then raised
  `Panel`s (".glass"). Editors paint `paintBackdrop` + `paintPanelShadows` and add a `dd::gui::Atmosphere` last
  (vignette, matte grain, the cursor's shadow, theme crossfade, open fade, filament flicker of lit keys).
  Mark custom raised components with `markRaised()` so their shadow is cast.
- **Parts**: keycaps with a wall that collapses when pressed (`paintKeycap`); lit keys are smoked plastic with
  an amber LED glowing through and a flickering filament (`paintLitKey`, components set `ddLit` /
  `ddFlickerPeriod` properties); recessed trays (`paintTray`, `paintInset`); switches with an LED
  (`paintSwitch`, `Toggle`, `LampSwitch`); console displays of warm black glass with scanlines and breathing
  lamp bloom (`paintConsole`, the Scope); wells for drawn scenes (`paintBezel`). Knobs: a round tray, the value
  arc lit in signal orange, a keycap cap with a lit pointer.
- **Text**: Instrument Sans (embedded, SIL OFL) through `LookAndFeel::font (size, weight)` / `display()`.
  rawleads uses Braun Linear, which is proprietary (Braun GmbH, all rights reserved): never ship it.
  Captions are small, bold, wide-tracked and muted (`drawCaption`); lit legends use `drawGlowing`.
- Drawn scenes (Kitroom's kit, Sunburst's stage, Afterglow's horizon) stay dark in both themes, like rawleads'
  preview pane: dark room, warm light, light grain (`paintGrain` amount ~0.15), crisp labels.
- **No flat, dull or muddy objects** (owner, Afterglow 1.0 feedback): everything has depth (cast shadows,
  inset shading, edge light, glows).
- Check both themes, and at `snapshot --scale 2`: glows, walls and grain only read right up close.

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
| SubGrit | synth | Sgrt | 808 + distorted bass, POLY switch. Simple panel by default (SubView speaker + six bipolar macros mBoom/mPunch/mGrit/mTone/mLength/mGlide that bend the preset, 0 = as designed); ALL KNOBS shows the full engine (`allKnobs`, saved per session). 139 presets: TRAVIS, MIKE DEAN, UNTILJAPAN, YE, YEEZUS, MOSHC0RE, KING, TRIP HOP, NEO SOUL, SOUL, SAFE, 80s, 2000s, BOUNCE 808, SOFT, UNIQUE |
| Kitroom | synth | Ktrm | Live acoustic drum kit from multi-mic DrumGizmo recordings (CC BY 4.0: DRS, Muldjord, Aasimonster) plus real hand claps (VCSL, CC0) placed in the DRS room: drawn kit UI (the clap is a sample pad, slot 11 on GM key 39), GM keys, swappable pieces, close/OH/room mics, per-piece Punch (a per-voice attack/body envelope on close + overheads, rooms untouched), Clap on Snare, Lay Back (`layBack`, 1.14: snare/clap queued up to 60 ms late, hats/ride 40 % of it, kick on the grid; pending hits split the render), bus drive/crush/tone. Log drum (1.15, `LogDrum.h`): a synthesised Amapiano log drum, the piece after the sampled slots (`kr::kLogDrum`, drawn at the front left, its own ElementPanel mode): FM knock (carrier + modulator a ratio or two up whose index blooms for ms), pitch blip, short decay, sine sub, low-pass + tanh; mono, a note started while another log key is held slides (Slide time), others cut with a 6 ms fade. Plays keys C5-C8 (MIDI 60-96) not given to a drum, sounding 36 st lower (FL A5 = 55 Hz); `log*` params appended (Volume, Tune, Length, Knock, Tone, Drive, Sub, Glide, Model: Classic / Woody / Deep / Bounce). QA's drums phrase never plays it, so AMAPIANO / AFROBEATS previews use `previewPhraseByCategory` grooves with a log drum line. Parameters added after 1.11 are appended to the layout. 91 presets: STUDIO, LIVE ROOM, VINTAGE, TIGHT, PUNCH, BIG, YEEZUS, MOSHC0RE, KING, TRIP HOP, 80S, 2000S, SOFT, UNIQUE, AMAPIANO, AFROBEATS, NEO SOUL, SOUL. Local QA needs `KITROOM_LIBRARY` (build it with `plugins/Kitroom/library/build_library.py`) |
| Sunburst | synth | Sbst | Sampled acoustic + electric guitars (CC0: Karoryfer Shinyguitar / Emilyguitar / Black And Green, FreePats nylon), six-string engine (string assignment, strum, hammer-on / slide legato, palm mute, double-track), String Noise knob, pedalboard (comp, drive, fuzz, chorus, synced trem, delay), Clean / Crunch / Lead amp + cab + room. Drawn guitar UI with lit strings. TAPE STOP. 71 presets: ACOUSTIC, CLEAN, CRUNCH, HEAVY, TRAVIS SCOTT, MIKE DEAN, YEEZUS, MOSHC0RE, TRIP HOP, 80S, 2000S, SOFT, UNIQUE, NEO SOUL, SOUL, SAFE. Local QA needs `SUNBURST_LIBRARY` (`plugins/Sunburst/library/build_library.py`) |
| Afterglow | synth | Aglw | Pads, keys, plucks, leads, bells, soundscapes, electric guitar, real instruments. Deep two-layer engine (13 sources incl. pluck, organ, mallet, sync, Electric guitar string, and Live: 19 recorded instruments from VSCO 2 CE + VCSL, CC0, `library/` builds them with auto-found seamless loops; `Live.h` loader is lock-free on the audio side) + Dirt amp + CAB + strum + tempo-synced arpeggiator (`Arp.h`) + FX, but the panel is macro-first: a sunset "orb" display (Motion / Tone), 6 macros, effect lamps + ARP, POLY switch, TAPE STOP. 198 presets (each a different role): TAME IMPALA, MASSIVE ATTACK, BRENT FAIYAZ, MIKE DEAN, TRAVIS SCOTT, YEEZUS, MOSHC0RE, GUITAR, TRIP HOP, 80S, 2000S, SOFT, UNIQUE, LIVE, PADS, ARPS, NEO SOUL, SOUL, SAFE. Local QA needs `AFTERGLOW_LIBRARY` (`plugins/Afterglow/library/build_library.py`) |
| Aircheck | effect | Achk | Vocal chain voiced after records: TD-PSOLA pitch correction (YIN detector with period continuity, so no hops onto overtones; key / scale, retune speed, humanize, flex, vibrato) and a formant-keeping Pitch Shift panel (1.5: `pitchShift` +-12 st with magnetic semitones, `pitchFormant` +-7 st, `ShiftView` pad natural vs chipmunk; the tuner's old `tuneTranspose` / `tuneFormant` are kept for old sessions and moved over by `migrateLegacyPitch` on state and preset load). PSOLA waits until the detector has described a moment (plus a 10 ms look-ahead for note starts) before placing marks there, and keeps the power of up-shifted voices; ~59 ms latency, reported, mic voicing, 4x-oversampled saturation (tone, parallel mix), an eight-band dynamic EQ (magnitude-matched bells and shelves, cuts to 48 dB/oct, M/S, pre or post dynamics; `ProEq.h`), vocal rider, FET / opto / old-limiter / VCA compressor (sidechain HPF; FET and VCA look ahead 1.5 ms), de-esser, gate, an STFT resonance suppressor (`Smooth.h`), a dynamic air exciter (`Air.h`), doubler, chorus + Dimension, a 16-band vocoder that plays the sung note, tape, ducked tape delay, plate / room / hall / chamber / gated reverb, and an always-on true-peak limiter (-1 dBTP, 4x polyphase detection, overshoot-free). The dry path for Mix runs through matching oversamplers: a neutral chain nulls to -134 dB. Old six-band `eq*` params are kept only for 1.0/1.1 sessions (`migrateLegacyEq`). 42 presets: DEON (the owner's BandLab chain), 80S (In The Air Tonight), 2000S, TRAVIS SCOTT, YE, TAME IMPALA, R&B, TRIP HOP, MOSHC0RE, STUDIO, PITCHED (justend: the pitched-up intro vocal of justend & loe4t's "Hennessy", +4 st / formant +2), NEO SOUL, SOUL. QA phrase `vocal`: the harness's synthetic sung phrase (`render --input voice.wav` for a real take) |
| Contour | effect | Cntr | Melodyne-style note editor. Captures the track against the host timeline (TRANSFER, `modules/dd_take`: sparse timeline take, capture FIFO, hazard-pointer hand-over, offline YIN/Viterbi pitch track, FLAC takes in Music/ddeonmadeit/Contour/Takes), finds notes, and plays the edit back with zero latency: a worker builds a TD-PSOLA grain table after every edit, the audio thread overlap-adds it from the take (unedited = sample-exact). Note tools: pitch / time / stretch, vibrato, drift, formant, level, split / join, undo; global Center / Drift / Vibrato / Transition / Formant / Transpose; key detection; bounce + drag out. 21 correction-style presets: NATURAL, TIGHT, CREATIVE, NEO SOUL, SOUL. QA renders `passes: 2` (capture, then play the edit); `contour_tests` checks analysis, rendering and save/restore |
| Hush | effect | Hush | Per-sound de-esser on `dd::TakeEngine`: captures the vocal, finds every S / SH / T (sibilance band vs the voice's body and vs its own local background, `Analysis.cpp`), and gives each its own cut (automatic from Amount, louder ones more, or dragged by hand in the reduction lane). Split mode crossfades to a zero-phase 8th-order low band above Freq (no cut = sample-exact), Wide turns the whole sound down; live split-band de-esser for audio not yet captured; Listen (perf) solos what is removed. 16 presets: VOCAL, RAP, SINGING, SPECIAL, NEO SOUL, SOUL. QA phrase `lyric` (the sung phrase with consonants), `passes: 2`; `hush_tests` |
| Orbit | effect | Orbt | 3D room placement heard from the listener's seat (`Spatializer.h`): two virtual sources (input L / R spread by Width) with travel delay (Doppler), 1/r^0.9 distance, air absorption, near-ear level and Woodworth ITD, through interpolated minimum-phase HRIRs (`Hrtf.h`; Viking HRTF v2, CC BY 4.0, 4 pinna sets as the `perfHead` Ears; `hrtf/make_hrtf.py` builds `Resources/hrtf.bin`, embedded with juce_add_binary_data); 24 shoebox image sources per source and a 16-line FDN (RT60 from the room, Sabine) panned onto 16 fixed directions, each rendered with its own HRIR (`perfOutput` Speakers = amplitude panning); room EQ cuts low build-up in small rooms; loudness normalised per room; side HPF keeps the sub mono. 14 rooms x Size (`Room.h`, shared with the drawn view; 1.1 appended Echo Chamber and Theater: room choices only grow at the end), Motion paths locked to the host beat (the 4th dimension), every position a parameter. `RoomView` draws the room in perspective (behind / above / side, drag to place, Shift-drag or wheel = height, right-drag turns), with width arc, reflection rays, trail and rings. 48 presets: AROUND YOU, ROOMS, MOTION, TRAVIS SCOTT, TAME IMPALA, TRIP HOP, MIX, NEO SOUL, SOUL. QA phrase `demo` |
| Mirage | effect | Mrge | Visualiser (audio untouched) for screen recordings: a wide resizable editor (size kept in the state) with its own palette, not the house look: every pixel goes through one red -> orange -> Tarantino-yellow ramp (`Palette` in `MirageView.h`), turned around the colour wheel by `hue` (luma kept, blue pales to white; the glowing dot by DEON opens `HueWheel` in a `CallOutBox`; menus, bubble and resize grip share one `PanelLook`). `StereoTap` (lock-free, monotonic write counter) feeds `Analysis.h` on the message thread (FFT, 160 log bands with AGC, levels, hit detector, waveform history, older spectra). The window is a scene (`scene`: Dunes, Burst, Terrain, Figure, Tunnel; `Scenes.cpp`, each fills a float brightness buffer; the eclipse / dunes dials are relabelled per scene) through a style (`style`: Film grain + mesh, CRT tube with persistence, bloom, barrel warp and scanlines, VHS smear / tracking tears / dropouts; `Styles.cpp`) into a software image; then the instrument deck (levels, scope, waveform, LED spectrum, goniometer, HudDial controls) and a scrolling thermal strip; static chrome is cached per scale. Header: DEON + hue dot, SCENE / STYLE keys, preset selector in the middle (category menu), transport. Dot-matrix text: `DotFont.h`. 10 params (choices only grow at the end), 25 presets (SCENES, ABSTRACT, CRT, VHS, NEO SOUL, SOUL). Keep render + paint under ~9 ms at 1600x440 for every scene x style (no std::floor in pixel loops; images take the current colour's opacity: setOpacity (1) before drawImage) |
