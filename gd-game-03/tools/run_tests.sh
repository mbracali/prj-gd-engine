#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# Every headless gd03 test, one result line each.
# Usage: gd-game-03/tools/run_tests.sh <disc image> [walk ...]
#   GD03_BIN: the program (default engine/build/linux/gd03);
#   GD03_AUDIT=<report.md>: run the walks as --audittest, accumulating one report.
# Logs: engine/build/test-logs/<test>.txt (git-ignored).
set -u
here="$(cd "$(dirname "$0")/.." && pwd)"
ISO="${1:?usage: run_tests.sh <disc image> [walk ...]}"; shift
B="${GD03_BIN:-$here/engine/build/linux/gd03}"
L="$here/engine/build/test-logs"; mkdir -p "$L"
export SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy
run() { local name=$1 pat=$2; shift 2; timeout 1500 "$B" "$@" > "$L/$name.txt" 2>&1; echo "$name rc=$? :: $(grep -E "$pat" "$L/$name.txt" | tail -1)"; }
run scenetest 'scene test (passed|FAILED)' "$ISO" --scenetest
run selftest 'rule check' "$ISO" --selftest
run sfxtest 'sfxtest: (OK|FAIL)' "$ISO" --sfxtest
run scripttest 'scripttest: ' "$ISO" --scripttest
run uitest 'uitest: (OK|FAILED)' "$ISO" --uitest
run venuetest 'venue test' "$ISO" --venuetest
run chartest 'chartest: [0-9]+ failures' "$ISO" --chartest
run facefxtest 'facefxtest: (OK|FAIL)' "$ISO" --facefxtest
run shadertest 'shader' --shadertest
GD03_GLES=1 run shadertest_gles 'shader' --shadertest
for w in "$@"; do
  if [ -n "${GD03_AUDIT:-}" ]; then run "walk_$w" "walktest $w: " "$ISO" --audittest "$w"
  else run "walk_$w" "walktest $w: " "$ISO" --walktest "$w"; fi
done
