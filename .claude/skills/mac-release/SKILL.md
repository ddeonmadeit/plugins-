---
name: mac-release
description: Build, sign, validate, package and release plugins for macOS (FL Studio, Logic, Ableton) through GitHub Actions, and explain installation to the user. Use when shipping a plugin, debugging the macOS CI job, or answering "how do I install it".
---

# macOS build and release

There is no Mac in the dev container. `.github/workflows/build.yml` does it on `macos-15` for every
plugin listed by `tools/ci/plugin_meta.py` (parsed from each `dd_add_plugin()` call):

1. Configure with `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 -DDD_BUILD_HARNESS=OFF`.
2. Build `<Name>_VST3` and `<Name>_AU`.
3. `tools/ci/package_macos.sh build dist`: ad-hoc `codesign --deep -s -` (required on Apple Silicon),
   `lipo -info` (must list both archs), `<Name>-<ver>-macOS.zip` (bundles + Install.command + README.txt,
   made with `ditto` so permissions survive) and `<Name>-<ver>-macOS.pkg` (pkgbuild per format with
   `BundleIsRelocatable=false`, then productbuild).
4. `auval -v aumu|aufx <code> Ddeo` after copying the component to `~/Library/Audio/Plug-Ins/Components`
   and killing `AudioComponentRegistrar`.
5. pluginval strictness 8 on the VST3.
6. Upload artifact `macOS-installers`; in a release run, `gh release create <Name>-v<version>` (the tag is
   created at the built commit) named "<Name> <version>" with those two files and notes from
   `tools/site/release_notes.py`.

The Linux job runs the full audio QA (`tools/qa/run_qa.sh`) plus pluginval, uploads `qa-report`, and
renders preset previews (`tools/site/make_previews.py`) as the `site-previews` artifact.

The `site` job (push to main or a release tag, after both jobs) queries the releases API, runs
`tools/site/build_site.py` and force-pushes a single-commit `gh-pages` branch. GitHub Pages serves it at
**https://ddeonmadeit.github.io/plugins-/** (Pages was enabled automatically by the first gh-pages push;
nothing to configure). Each plugin's card uses `plugins/<Name>/site.json` and its newest release.

## Checking CI from the container

No `gh` CLI: use the GitHub MCP tools (`mcp__github__actions_list` / `actions_get` / `get_job_logs`)
on `ddeonmadeit/plugins-`. After a push, find the run for the branch, wait for it, read failing job logs.
Fix and push again until both jobs are green.

## Releasing

1. Fill in `plugins/<Name>/site.json` and set `"publish": true` (first release only).
2. Set `VERSION` in `plugins/<Name>/CMakeLists.txt` to a version that has no release yet (semver; bump it
   for every update). Commit and push to `main`. That's the release: the macOS job notices the unreleased
   version, builds and validates it, and creates the Release; the site job then updates the page.
   Nothing is released if any QA, auval or pluginval step fails.

Other triggers (same result): the owner can tap Actions > Build plugins > Run workflow with
release = <Name>, or push a `<Name>-v<version>` tag from their own machine. From this container neither
works: the GitHub integration gets 403 on workflow dispatch and the git proxy drops tag pushes. Plain
branch pushes are fine, hence the version-bump model.
3. When the run finishes, verify: `mcp__github__actions_list` shows the run green; the asset URL
   `https://github.com/ddeonmadeit/plugins-/releases/download/<Name>-v<ver>/<Name>-<ver>-macOS.pkg`
   answers `curl -sI` with 302; https://ddeonmadeit.github.io/plugins-/ shows the version.

The repo is public, so release assets download without a GitHub login. Never delete a release tag that
users may have linked to; ship a new version instead. Releases made by CI do not trigger other workflows
(GITHUB_TOKEN events never do), which is why the site job lives in the same workflow.

## What to tell the user about installing

- Easiest: double-click the `.pkg`. It is not notarized, so macOS blocks it the first time:
  System Settings > Privacy & Security > "Open Anyway", then run it again. Installs to
  `/Library/Audio/Plug-Ins/VST3` and `/Library/Audio/Plug-Ins/Components`.
- No-admin route: unzip, then in Terminal `bash ~/Downloads/<folder>/Install.command`. It copies to
  `~/Library/Audio/Plug-Ins/...` and strips the quarantine flag (unsigned plug-ins with that flag are blocked).
- Sample-based plugins (Kitroom) also ship their sound library: the `.pkg` installs it to
  `/Library/Application Support/ddeonmadeit/<Name>/Library`, `Install.command` copies the zip's
  `<Name> Library` folder to `~/Library/Application Support/...`. CI builds it in the `libraries` job
  (cached; see `.claude/skills/sample-plugin/SKILL.md`), the macOS job restores it and passes it to
  `package_macos.sh build dist libraries`.
- FL Studio: Options > Manage plugins > Find more plugins / Start scan; instruments show under Generators.
  If missing, add the folders above to "Plugin search paths". FL's piano roll names MIDI 60 "C5", so a
  real-pitch 808 lives around FL "C2-C3".
- Real notarization needs a paid Apple Developer ID; add it later as repository secrets plus
  `codesign --options runtime` and `xcrun notarytool` steps if the user wants frictionless installs.
