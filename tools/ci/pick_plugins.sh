#!/usr/bin/env bash
# Which plugins this run builds (space-separated, may be empty). A push to a branch builds the plugins it
# changed (all of them when shared code changed, none for CI / site / docs only; see changed_plugins.py),
# plus, on main, every plugin due a release. Pull requests, tags, manual runs and a branch's first push
# build everything. Needs GH_TOKEN and BEFORE (github.event.before).
set -eo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
meta() { python3 "$ROOT/tools/ci/plugin_meta.py" --only "$1" | python3 -c "import json,sys; p=json.load(sys.stdin)[0]; print($2)"; }
all="$(python3 "$ROOT/tools/ci/plugin_meta.py" --names | tr '\n' ' ')"
if [[ "$GITHUB_EVENT_NAME" != push || "$GITHUB_REF" == refs/tags/* || -z "$BEFORE" || "$BEFORE" =~ ^0+$ ]]; then
    echo $all
    exit 0
fi
files="$(mktemp)"
if ! gh api "repos/$GITHUB_REPOSITORY/compare/$BEFORE...$GITHUB_SHA" --jq '.files[].filename' > "$files" 2> /dev/null \
   || [[ $(wc -l < "$files") -ge 300 ]]; then
    echo $all
    exit 0
fi
picked="$(python3 "$ROOT/tools/ci/changed_plugins.py" --files "$files")"
if [[ "$GITHUB_REF" == refs/heads/main ]]; then
    for p in $all; do
        if [[ "$(meta "$p" "p['site'].get('publish', False)")" == True ]] \
           && ! gh release view "$p-v$(meta "$p" "p['version']")" --repo "$GITHUB_REPOSITORY" > /dev/null 2>&1; then
            picked="$picked $p"
        fi
    done
fi
out=""
for p in $all; do
    [[ " $picked " == *" $p "* ]] && out="$out $p"
done
echo $out
