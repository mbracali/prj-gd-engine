#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# The fuzz walk (--walktest fuzz) on several seeds in parallel, offscreen,
# each with its own scratch XDG_DATA_HOME; prints one line per seed and
# exits non-zero when any fails.
#   tools/fuzz_seeds.sh <gd10 binary> <iso> [seeds...]
# Without seeds: the regression seeds (the default seed, and the seeds that
# found menu bugs; docs/frontend.md "Walks").
# JOBS (default 8) runs at once; LOGS (default a temp dir) keeps the logs.
set -uo pipefail
bin="$1" iso="$2"
shift 2
seeds=("$@")
[ ${#seeds[@]} -gt 0 ] || seeds=(default 7 99 1 3 5 11 17 23 27 31 39 73)
logs="${LOGS:-$(mktemp -d)}"
mkdir -p "$logs"
export SDL_GAMECONTROLLER_IGNORE_DEVICES=0x1bad/0x0002 SDL_JOYSTICK_IGNORE_DEVICES=0x1bad/0x0002
export SDL_AUDIO_DRIVER=dummy SDL_VIDEO_DRIVER=offscreen GD10_WALKLOG=1
run() {
  local s="$1" home="$logs/xdg-$1"
  rm -rf "$home" && mkdir -p "$home"
  if [ "$s" = default ]; then
    XDG_DATA_HOME="$home" timeout 600 "$bin" --walktest fuzz "$iso" > "$logs/seed-$s.log" 2>&1
  else
    XDG_DATA_HOME="$home" GD10_SEED="$s" timeout 600 "$bin" --walktest fuzz "$iso" > "$logs/seed-$s.log" 2>&1
  fi
  local rc=$? line
  line="$(grep -m1 '^walktest fuzz:' "$logs/seed-$s.log" | cut -c1-160)"
  rm -rf "$home"
  echo "seed $s: rc=$rc ${line:-no result}"
}
export -f run
export bin iso logs
printf '%s\n' "${seeds[@]}" | xargs -P "${JOBS:-8}" -I{} bash -c 'run {}' | sort -V | tee "$logs/summary.txt"
echo "logs: $logs"
! grep -qv 'rc=0 walktest fuzz: passed' "$logs/summary.txt"
