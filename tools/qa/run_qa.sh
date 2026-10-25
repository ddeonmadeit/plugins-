#!/usr/bin/env bash
# Full offline QA for one plugin: preset lint -> render every preset -> objective analysis ->
# randomized stress test -> UI snapshot -> (optional) pluginval.
#
#   tools/qa/run_qa.sh SubGrit                 # uses ./build, writes qa-out/SubGrit/
#   BUILD_DIR=build-ci PLUGINVAL=/path/to/pluginval tools/qa/run_qa.sh SubGrit
#
# Exit code is non-zero if anything fails, so CI can gate on it.
set -euo pipefail

PLUGIN="${1:?usage: run_qa.sh <PluginName>}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
OUT="${QA_OUT:-$ROOT/qa-out/$PLUGIN}"
HARNESS="$BUILD_DIR/plugins/$PLUGIN/${PLUGIN}_Harness_artefacts/Release/${PLUGIN}_Harness"
VST3="$BUILD_DIR/plugins/$PLUGIN/${PLUGIN}_artefacts/Release/VST3/$PLUGIN.vst3"
QA_JSON="$ROOT/plugins/$PLUGIN/qa.json"

[[ -x "$HARNESS" ]] || { echo "harness not built: $HARNESS (cmake --build build --target ${PLUGIN}_Harness)"; exit 1; }
mkdir -p "$OUT"

cfg() { python3 -c "import json,sys; d=json.load(open('$QA_JSON')) if __import__('os').path.exists('$QA_JSON') else {}; v=d.get('$1', '$2'); print(','.join(v) if isinstance(v, list) else v)"; }

echo "== preset lint"
if [[ -d "$ROOT/plugins/$PLUGIN/Presets" ]]; then
    python3 "$ROOT/tools/qa/preset_lint.py" "$HARNESS" "$ROOT/plugins/$PLUGIN/Presets" --categories "$(cfg categories '')"
fi

echo "== render all presets"
"$HARNESS" render-all --outdir "$OUT/renders" --phrase "$(cfg phrase demo)" > "$OUT/render.log"

echo "== analyze"
python3 "$ROOT/tools/qa/analyze.py" "$OUT/renders" --sheet \
    --target-lufs "$(cfg targetLufs -9.5)" --tolerance "$(cfg toleranceLu 1.5)" --similar "$(cfg similarBelow 0.2)" \
    | tee "$OUT/analysis.txt"
# analyze.py exits non-zero on hard failures (NaN, overs, DC, non-mono sub); also fail on loudness/similarity.
if grep -qE "loudness|TOO SIMILAR" "$OUT/analysis.txt"; then
    echo "QA: loudness or similarity check failed (see $OUT/analysis.txt)"; exit 1
fi

echo "== stress"
"$HARNESS" stress --seconds 20 | tee "$OUT/stress.txt"
"$HARNESS" stress --seconds 5 --block 1 | tee -a "$OUT/stress.txt"
"$HARNESS" stress --seconds 5 --sr 96000 --block 1024 | tee -a "$OUT/stress.txt"

echo "== UI snapshot"
if command -v xvfb-run > /dev/null; then
    xvfb-run -a -s "-screen 0 2560x1600x24" "$HARNESS" snapshot --out "$OUT/ui.png"
else
    "$HARNESS" snapshot --out "$OUT/ui.png" || echo "(snapshot skipped: no display)"
fi

if [[ -n "${PLUGINVAL:-}" && -d "$VST3" ]]; then
    echo "== pluginval"
    if command -v xvfb-run > /dev/null; then RUN=(xvfb-run -a -s "-screen 0 1920x1200x24"); else RUN=(); fi
    "${RUN[@]}" "$PLUGINVAL" --strictness-level 8 --validate-in-process --timeout-ms 600000 --validate "$VST3" > "$OUT/pluginval.log" 2>&1 \
        || { tail -40 "$OUT/pluginval.log"; echo "pluginval FAILED"; exit 1; }
    tail -1 "$OUT/pluginval.log"
fi

echo "QA passed: $OUT"
