#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# gd01's headless walks (offscreen video, dummy audio), one at a time, each
# in its own empty save folder (XDG_DATA_HOME), so your own settings are
# never read or written. Keys are injected with GD01_KEYS (app.cpp
# injectDebugKeys: "keyReturn" goes through Input like the real keyboard,
# "clickX_Y" is the mouse at menu units, "vpadN" a button of
# GD01_VIRTUAL_PAD). Each walk prints PASS / FAIL; the exit status is the
# number of failures.
#
#   tools/walks.sh <gd01 binary> <Guitar Hero (PS2) .iso> [walk ...]
#
# Walks (all by default): selftest logo_skip band quickplay mouse enhanced
# gamepads gamepads_pad. "band" runs first when another walk needs its save.
# The universal menus (universal_mechanisms.md) are covered by: logo_skip and
# band (1.1.1 skips, 1.1.2 band naming), mouse (1.1.3, credits skip),
# enhanced (1.2.2 Modes, old enhanced.json, HDR), gamepads / gamepads_pad (1.2.1).
set -uo pipefail
bin="$(realpath "$1")"
iso="$(realpath "$2")"
shift 2
walks=("$@")
[ ${#walks[@]} -eq 0 ] && walks=(selftest logo_skip band quickplay mouse enhanced gamepads gamepads_pad)

work="$(mktemp -d "${TMPDIR:-/tmp}/gd01-walks.XXXXXX")"
trap 'rm -rf "$work"' EXIT
fails=0
seed=""

# run <name> <seconds to quit after> <GD01_KEYS> [VAR=value ...]: the log is $work/<name>.log.
run() {
  local name="$1" quit="$2" keys="$3"
  shift 3
  rm -rf "$work/$name" && mkdir -p "$work/$name/xdg"
  [ -n "$seed" ] && cp -r "$seed/." "$work/$name/xdg/"
  (cd "$work/$name" && env XDG_DATA_HOME="$work/$name/xdg" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy \
     GD01_UILOG=1 GD01_NO_DESKTOP_ENTRY=1 GD01_KEYS="$keys" GD01_SCREENSHOT="$work/$name/shot.bmp" \
     GD01_SCREENSHOT_WALL="$quit" "$@" timeout 120 "$bin" "$iso" >"$work/$name.log" 2>&1)
}
save() { echo "$work/$1/xdg/gd-engine/gd01"; }
result() {  # <name> <ok 0/1> <what>
  if [ "$2" = 1 ]; then echo "PASS $1: $3"; else echo "FAIL $1: $3"; fails=$((fails + 1)); fi
}
has() { grep -q "$2" "$work/$1.log" && echo 1 || echo 0; }
json() {  # <file> <python expression on d>: prints 1 when true
  python3 -I -c "import json,sys; d=json.load(open(sys.argv[1])); print(1 if ($2) else 0)" "$1" 2>/dev/null || echo 0
}
needs_band() {
  if [ -z "$seed" ]; then walk_band >/dev/null; fi
}

walk_selftest() {
  local out
  out="$("$bin" --selftest "$iso" 2>/dev/null)"
  echo "$out" | grep -E "input self-test|all charts|not fully"
  result selftest "$(echo "$out" | grep -q "all charts fully hit" && echo "$out" | grep -q "input self-test: all" && echo 1 || echo 0)" \
    "every chart fully hit, input rules"
}

# 1.1.1: Enter skips the boot logos (they last about 10 s); a click too.
walk_logo_skip() {
  run logo_skip 5000 "1500:keyReturn"
  result logo_skip "$(has logo_skip "goto_screen bootup_load")" "Enter skips the logos"
  run logo_click 5000 "1500:click640_360"
  result logo_click "$(has logo_click "goto_screen bootup_load")" "a click skips the logos"
}

# First boot: logos (Enter), create the save (Enter), the intro movie (red),
# press start, the guitar help, then NAME YOUR BAND: Down, Down (C), green
# takes it, Down (the d-pad / strum), Enter accepts "CD" (1.1.2).
walk_band() {
  seed=""
  run band 33000 "1500:keyReturn,9500:keyReturn,13000:keyReturn,17500:red,20500:keyReturn,23500:keyReturn,26000:keyDown,26300:keyDown,26600:green,27000:down,27400:keyReturn"
  local ok
  ok=$(( $(has band "goto_screen nameprof") && $(has band "goto_screen main") && $(json "$(save band)/career.json" "d['profiles'][0]['name'] == 'CD'") ))
  result band "$ok" "logos, movie (red) skipped; band named CD with up / down, green and Enter"
  [ "$ok" = 1 ] && seed="$work/band/xdg"
}

walk_quickplay() {
  needs_band
  run quickplay 26000 "3000:keyDown,3300:keyReturn,6000:keyReturn,8500:keyReturn,11000:keyReturn,13500:keyReturn" GD01_SKIP_BOOT=1
  result quickplay "$(( $(has quickplay "goto_screen qp_selsong") && $(has quickplay "ui: start game") ))" "main > Quick Play > song > difficulty > the song plays"
}

# Mouse only: OPTIONS, Enhanced Settings, collapse MODES, the Resolution
# dropdown (720p), BACK in the help bar, Enhanced again, its own "< Back",
# then Credits, skipped with green (1.1.1).
walk_mouse() {
  needs_band
  run mouse 24000 "3000:click640_554,5500:click520_376,8000:click200_190,9000:click480_486,9300:keyUp,9400:keyUp,9500:keyUp,9700:keyReturn,10500:click460_684,13000:click520_376,15500:click150_606,18000:click600_564,21000:green" GD01_SKIP_BOOT=1
  local backs
  backs=$(grep -c "goto_screen options" "$work/mouse.log")
  result mouse "$(( $(json "$(save mouse)/enhanced.json" "d['internal_res'] == '720'") && backs >= 4 && $(has mouse "goto_screen credits") ))" \
    "clicks through Options and Enhanced, a dropdown, help-bar BACK, < Back, credits skipped"
}

# Enhanced: a 1.1.6 enhanced.json (old values, XInput / Rock Band toggles)
# loads; MODES > Good Old Days sets its preset.
walk_enhanced() {
  needs_band
  local s
  s="$work/enhanced_seed" && rm -rf "$s" && cp -r "$seed" "$s"
  printf '{"bloom":"high","chromatic":true,"rockband":true,"xinput":false,"fps_counter":true}\n' > "$s/gd-engine/gd01/enhanced.json"
  local keep="$seed"
  seed="$s"
  run enhanced 6000 "3000:keyDown,3150:keyDown,3300:keyDown,3450:keyDown,3600:keyDown,3750:keyDown,3900:keyDown,4200:keyReturn" GD01_SKIP_BOOT=1 GD01_SCREEN=enhanced_settings
  seed="$keep"
  result enhanced "$(json "$(save enhanced)/enhanced.json" "d['color_grade'] == 'sepia' and d['internal_res'] == '448' and d['bloom'] == '3x' and d['fps_counter'] is True and 'rockband' not in d and 'xinput' not in d and d['hdr'] == 'none'")" \
    "Good Old Days preset applied over a migrated 1.1.6 file"
  walk_enhanced_hdr
}

# Enhanced > HDR (universal_mechanisms.md 1.2.2): from a file without the key
# (None), Right steps the row through Emulated, True HDR (offscreen has no HDR
# surface: the logged fallback to Emulated), None and Emulated again, each
# applied and saved. Then the Enhanced screen's backdrop at a fixed time:
# HDR None draws the same pixels as a file without the key, Emulated does not.
walk_enhanced_hdr() {
  local s keys keep="$seed" i
  s="$work/enhanced_hdr_seed" && rm -rf "$s" && cp -r "$seed" "$s"
  printf '{"bloom":"original"}\n' > "$s/gd-engine/gd01/enhanced.json"
  seed="$s"
  keys=""
  for i in $(seq 0 19); do keys+="$((2500 + i * 100)):keyDown,"; done  # down to the HDR row
  run enhanced_hdr 9000 "${keys}5000:keyRight,5800:keyRight,6600:keyRight,7400:keyRight" GD01_SKIP_BOOT=1 GD01_SCREEN=enhanced_settings
  result enhanced_hdr "$(( $(json "$(save enhanced_hdr)/enhanced.json" "d['hdr'] == 'emulated'") && $(has enhanced_hdr "hdr: mode none") && \
    $(has enhanced_hdr "hdr: mode emulated - Emulated") && $(has enhanced_hdr "hdr: mode true - True HDR unavailable.*Using Emulated") ))" \
    "HDR row: None > Emulated > True HDR (fallback logged) > None > Emulated, saved"
  local hashes=() j
  for j in '{"bloom":"original"}' '{"bloom":"original","hdr":"none"}' '{"bloom":"original","hdr":"emulated"}'; do
    printf '%s\n' "$j" > "$s/gd-engine/gd01/enhanced.json"
    run enhanced_frame 5000 "" GD01_SKIP_BOOT=1 GD01_SCREEN=enhanced_settings GD01_FIXED_CLOCK=16.6667
    hashes+=("$(md5sum < "$work/enhanced_frame/shot.bmp" | cut -c1-32)")
  done
  seed="$keep"
  result enhanced_hdr_frame "$([ "${hashes[0]}" = "${hashes[1]}" ] && [ "${hashes[0]}" != "${hashes[2]}" ] && echo 1 || echo 0)" \
    "fixed frame: HDR None identical to no HDR key, Emulated differs"
}

# GAMEPADS (blue / F4): Player 1 (keyboard) > Customize inputs, Green Fret
# KEY 2 = G, Save; Player 2 off; Export.
walk_gamepads() {
  needs_band
  mkdir -p "$work/export"
  run gamepads 9500 "1500:keyF3,3500:keyDown,3700:keyDown,3900:keyDown,4100:keyReturn,4500:keyDown,4600:keyDown,4700:keyDown,4800:keyDown,4900:keyDown,5000:keyDown,5100:keyDown,5200:keyDown,5400:keyRight,5600:keyReturn,5900:keyG,6800:click1005_614,7500:click120_238,8300:click280_616" \
    GD01_SKIP_BOOT=1 GD01_DIALOG="$work/export"
  local f
  f="$(save gamepads)/controllers.json"
  result gamepads "$(( $(json "$f" "d['devices']['keyboard']['bindings']['green'] == [{'kind': 'key', 'index': 49}, {'kind': 'key', 'index': 103}] and d['player2'] is False") && $(json "$work/export/GD01-controllers.json" "d['version'] == 2") ))" \
    "bind G to Green KEY 2, Save, Player 2 off, Export"
}

# A virtual PS3 pad (player 1): Player 2 > Bind to > Detect, then the pad's
# button: the pad becomes player 2 and the keyboard takes player 1.
walk_gamepads_pad() {
  needs_band
  run gamepads_pad 7500 "3000:keyF3,5000:click400_256,5600:click400_398,6200:vpad0" GD01_SKIP_BOOT=1 GD01_VIRTUAL_PAD=054c:0268
  result gamepads_pad "$(json "$(save gamepads_pad)/controllers.json" "d['devices']['keyboard']['player'] == 0 and [v['player'] for k, v in d['devices'].items() if k != 'keyboard'] == [1]")" \
    "Detect binds the pad to Player 2"
}

for w in "${walks[@]}"; do
  if declare -F "walk_$w" >/dev/null; then "walk_$w"; else echo "unknown walk: $w"; fails=$((fails + 1)); fi
done
echo "walks: $fails failure(s)"
exit "$fails"
