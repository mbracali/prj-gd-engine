#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# Character movement golden log (docs/character_animation.md §5.6).
# Runs <song> headless on a fixed 60 fps song clock with autoplay up to
# <ms> (default 90000) and logs every clip each stage character queues (the
# crowd is left out: which members are shown follows the camera's random shots).
# The first run of a song stores the log as the golden one; later runs are
# compared with it (an exact match is expected from the same build; after a
# deliberate change, delete the golden file to take the new log).
# Logs live in agents/scratch/anim_golden/ (git-ignored: they name game data).
#
# Usage: tools/anim_golden.sh <iso> <song id> [ms]
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
iso="${1:?usage: anim_golden.sh <iso> <song id> [ms]}"
song="${2:?usage: anim_golden.sh <iso> <song id> [ms]}"
until="${3:-90000}"
bin="${GD01_BIN:-$here/engine/build/linux/gd01}"
out="$here/agents/scratch/anim_golden"
mkdir -p "$out"
log="$out/$song.log"
shot="$out/$song.bmp"
SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy GD01_FIXED_CLOCK=16.6667 GD01_ANIMLOG=1 GD01_WINDOW=640x360 \
  GD01_SCREENSHOT="$shot" GD01_SCREENSHOT_AT="$until" \
  "$bin" --song "$song" --autoplay "$iso" 2>&1 | grep -E "anim [a-z_0-9]+ +beat" | sed 's/^.*anim /anim /' | grep -v "^anim crowd" > "$log.new" || true
rm -f "$shot"
lines=$(wc -l < "$log.new")
if [ ! -s "$out/$song.golden" ]; then
  mv "$log.new" "$out/$song.golden"
  echo "anim_golden: $song: stored $lines lines as the golden log"
elif cmp -s "$log.new" "$out/$song.golden"; then
  rm -f "$log.new"
  echo "anim_golden: $song: $lines lines, matches the golden log"
else
  echo "anim_golden: $song: differs from the golden log:"
  diff "$out/$song.golden" "$log.new" | head -20 || true
  exit 1
fi
