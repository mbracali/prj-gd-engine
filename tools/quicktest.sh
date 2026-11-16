#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# The QUICK test tier (GDALL-049, docs/guidelines/general_guidelines.md "Tests"):
# builds only the games touched and runs, per game, its --selftest plus at
# most 3 short walks picked from the source folders changed. Target: under
# 5 minutes a game. The full gates (every walk, venuetest, chartest...) are
# for releases only.
#
#   tools/quicktest.sh [options] [game ...]
#     game            gd01 gd02 gd03 gd04 gd05 gd10 (or 01, 1 ...); none: the
#                     games touched by the changes against --base
#     --base <ref>    the base of the changes (default origin/develop, else main)
#     --jobs <n>      games tested at once (default 1: one game binary at a time)
#     --build-jobs <n> cmake --build -j (default 4)
#     --no-build      test the binaries as they are
#     --walks "a b"   these walks instead of the picked ones (every game)
#     --list          only print the games and walks it would run
#
# Builds run one game at a time, each after a check of free memory (stops
# under 6 GB available). Discs come from original_isos/ (QUICKTEST_ISOS=dir
# to change). Logs and the walks' empty save folders go under
# ~/.cache/gd-quicktest/ (not /tmp: it can be RAM), kept until the next run.
# Exit status: the number of games with a FAIL.
set -uo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
isos="${QUICKTEST_ISOS:-$root/original_isos}"
base="" jobs=1 bjobs=4 build=1 forced_walks="" list=0
games=()
while [ $# -gt 0 ]; do
  case "$1" in
    --base) base="$2"; shift 2 ;;
    --jobs) jobs="$2"; shift 2 ;;
    --build-jobs) bjobs="$2"; shift 2 ;;
    --no-build) build=0; shift ;;
    --walks) forced_walks="$2"; shift 2 ;;
    --list) list=1; shift ;;
    -h|--help) sed -n '5,26p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) n="${1#gd}"; n="${n#game-}"; games+=("$(printf '%02d' "$((10#$n))")"); shift ;;
  esac
done

# ---- per game: build dir, binary, disc, how its selftest and walks run, its walk map
dir_of() { echo "$root/gd-game-$1/engine"; }
build_of() {
  case "$1" in
    01) echo build/linux ;; 02) echo build/bl02 ;; 03) echo build/bl03 ;; 04) echo build/bl04 ;; 05) echo build/bl05 ;;
    10) echo "${GD10_BUILD:-build/dev}" ;;
  esac
}
iso_of() {
  case "$1" in
    01) echo "$isos/PS2 - Guitar Hero (USA) (PS2).iso" ;;
    02) echo "$isos/X360 - Guitar Hero II (USA) (Rev1).iso" ;;
    03) echo "$isos/PS2 - Guitar Hero Encore - Rocks the 80s (USA).iso" ;;
    04) echo "$isos/X360 - Guitar Hero III - Legends of Rock (USA) (Rev 1).iso" ;;
    05) echo "$isos/X360 - Rock Band (USA).iso" ;;
    10) echo "$isos/X360 - Guitar Hero - Metallica (USA, Europe) (En,Fr,De,Es,It).iso" ;;
  esac
}
# The walks for the changed source folders (src/<folder>/...), in order of
# priority; the first 3 different ones are run. "*" is any other change.
walk_map() {
  case "$1" in
    01) echo "ui:enhanced ui:quickplay input:gamepads audio:quickplay game:quickplay render:quickplay *:quickplay" ;;
    02|03|05) echo "ui:enhanced ui:quickplay input:options audio:quickplay game:quickplay render:quickplay char:quickplay *:quickplay" ;;
    04|10) echo "ui:options ui:quickplay input:gamepads audio:quickplay game:quickplay render:quickplay *:quickplay" ;;
  esac
}

if [ -z "$base" ]; then
  if git -C "$root" rev-parse -q --verify origin/develop >/dev/null; then base=origin/develop; else base=main; fi
fi
changed="$( (git -C "$root" diff --name-only "$base"...HEAD; git -C "$root" diff --name-only HEAD; \
             git -C "$root" ls-files --others --exclude-standard) 2>/dev/null | sort -u)"
if [ ${#games[@]} -eq 0 ]; then
  for g in 01 02 03 04 05 10; do
    if echo "$changed" | grep -q "^gd-game-$g/engine/" || echo "$changed" | grep -q "^gd-engine-core/"; then games+=("$g"); fi
  done
fi
if [ ${#games[@]} -eq 0 ]; then echo "quicktest: no game touched since $base (docs only?); name games to test them"; exit 0; fi

walks_of() {  # <game>: at most 3 walks
  if [ -n "$forced_walks" ]; then echo "$forced_walks" | tr ' ' '\n' | head -3 | xargs; return; fi
  local g=$1 picked=() dirs
  dirs="$(echo "$changed" | sed -n "s|^gd-game-$g/engine/src/\([^/]*\)/.*|\1|p" | sort -u)"
  [ -z "$dirs" ] && dirs="*"
  for pair in $(walk_map "$g"); do
    local d="${pair%%:*}" w="${pair#*:}"
    if [ "$d" = "*" ] || echo "$dirs" | grep -qx "$d"; then
      [[ " ${picked[*]-} " == *" $w "* ]] || picked+=("$w")
    fi
    [ ${#picked[@]} -ge 3 ] && break
  done
  [ ${#picked[@]} -eq 0 ] && picked=(quickplay)
  echo "${picked[*]}"
}

if [ "$list" = 1 ]; then
  for g in "${games[@]}"; do echo "gd$g: build $(build_of "$g"), selftest + $(walks_of "$g")"; done
  exit 0
fi

out="$HOME/.cache/gd-quicktest"
rm -rf "$out" && mkdir -p "$out"
declare -A buildres
mem_ok() { [ "$(awk '/MemAvailable/ {print int($2 / 1048576)}' /proc/meminfo)" -ge 6 ]; }

# ---- builds: one game at a time
for g in "${games[@]}"; do
  d="$(dir_of "$g")" b="$(build_of "$g")"
  if [ "$build" = 0 ]; then buildres[$g]=skip; continue; fi
  if ! mem_ok; then echo "quicktest: under 6 GB of memory available, stopping before building gd$g"; buildres[$g]=NOMEM; continue; fi
  echo "building gd$g ($b, -j$bjobs)..."
  if (cd "$d" && cmake --build "$b" -j"$bjobs") >"$out/gd$g-build.log" 2>&1 ||
     (cd "$d" && cmake "$b" && cmake --build "$b" -j"$bjobs") >>"$out/gd$g-build.log" 2>&1; then
    buildres[$g]=PASS
  else
    buildres[$g]=FAIL
  fi
done

# ---- tests: a game's selftest then its walks; up to --jobs games at once
test_game() {  # <game>: writes $out/gd<g>.result (one line per test: name status seconds)
  local g=$1 d b bin iso res="$out/gd$1.result"
  d="$(dir_of "$g")" b="$(build_of "$g")" bin="$d/$b/gd$g" iso="$(iso_of "$g")"
  : >"$res"
  if [ ! -x "$bin" ] || [ ! -f "$iso" ]; then echo "setup FAIL 0 (no $( [ -x "$bin" ] && echo disc || echo binary))" >>"$res"; return; fi
  local t0=$SECONDS rc
  if [ "$g" = 01 ]; then timeout 300 "$bin" --selftest "$iso" >"$out/gd$g-selftest.log" 2>&1; rc=$?
  else (cd "$d" && SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy timeout 300 "$bin" "$iso" --selftest) >"$out/gd$g-selftest.log" 2>&1; rc=$?; fi
  echo "selftest $([ $rc = 0 ] && echo PASS || echo FAIL) $((SECONDS - t0))" >>"$res"
  for w in $(walks_of "$g"); do
    t0=$SECONDS
    local save="$out/gd$g-$w-save"
    mkdir -p "$save"
    if [ "$g" = 01 ]; then
      # gd01's walks live in its tools/walks.sh (their own empty save folders).
      TMPDIR="$out" timeout 300 "$root/gd-game-01/tools/walks.sh" "$bin" "$iso" "$w" >"$out/gd$g-$w.log" 2>&1; rc=$?
    else
      (cd "$d" && XDG_DATA_HOME="$save" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy timeout 300 "$bin" "$iso" --walktest "$w") \
        >"$out/gd$g-$w.log" 2>&1; rc=$?
    fi
    echo "$w $([ $rc = 0 ] && echo PASS || echo FAIL) $((SECONDS - t0))" >>"$res"
  done
}
running=0
for g in "${games[@]}"; do
  if [ "${buildres[$g]}" = FAIL ] || [ "${buildres[$g]}" = NOMEM ]; then echo "build FAIL 0" >"$out/gd$g.result"; continue; fi
  test_game "$g" &
  running=$((running + 1))
  if [ "$running" -ge "$jobs" ]; then wait -n; running=$((running - 1)); fi
done
wait

# ---- the table
fails=0
printf '\n%-6s %-6s %-30s %-6s %s\n' game build test result seconds
for g in "${games[@]}"; do
  bad=0
  while read -r name status secs rest; do
    printf '%-6s %-6s %-30s %-6s %s\n' "gd$g" "${buildres[$g]}" "$name" "$status" "$secs${rest:+ $rest}"
    [ "$status" = PASS ] || bad=1
  done <"$out/gd$g.result"
  [ "${buildres[$g]}" = FAIL ] || [ "${buildres[$g]}" = NOMEM ] && bad=1
  fails=$((fails + bad))
done
echo "logs: $out"
exit "$fails"
