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
# gamepads gamepads_pad store. song_end and career_win are run by name (each plays a whole song). "band" runs first when another walk needs its save.
# The universal menus (universal_mechanisms.md) are covered by: logo_skip and
# band (1.1.1 skips, 1.1.2 band naming, band_typing), mouse (1.1.3, credits skip,
# song row hover, one Back in Enhanced),
# enhanced (1.2.2 Modes, old enhanced.json, HDR, backends), gamepads / gamepads_pad (1.2.1).
set -uo pipefail
bin="$(realpath "$1")"
iso="$(realpath "$2")"
shift 2
walks=("$@")
[ ${#walks[@]} -eq 0 ] && walks=(selftest logo_skip band quickplay mouse enhanced gamepads gamepads_pad store)

work="$(mktemp -d "${TMPDIR:-/tmp}/gd01-walks.XXXXXX")"
trap '[ -n "${WALK_KEEP:-}" ] && cp "$work"/*.log "$WALK_KEEP"/ ; rm -rf "$work"' EXIT
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
     GD01_SCREENSHOT_WALL="$quit" "$@" timeout "$(( quit / 1000 + 60 ))" "$bin" "$iso" >"$work/$name.log" 2>&1)
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
  walk_band_typing
}

# NAME YOUR BAND with the keyboard (#76): letters typed straight in (H is
# also a player 2 key: here it is a letter), Backspace deletes the last one,
# Enter accepts the name "ROCKH".
walk_band_typing() {
  local keep="$seed"
  seed=""
  run band_typing 31000 "1500:keyReturn,9500:keyReturn,13000:keyReturn,17500:red,20500:keyReturn,23500:keyReturn,26000:keyR,26200:keyO,26400:keyC,26600:keyK,26800:keyBackspace,27000:keyK,27200:keyH,27400:keyReturn"
  seed="$keep"
  result band_typing "$(( $(has band_typing "goto_screen nameprof") && $(json "$(save band_typing)/career.json" "d['profiles'][0]['name'] == 'ROCKH'") ))" \
    "band name typed on the keyboard, Backspace deletes, Enter accepts ROCKH"
}

walk_quickplay() {
  needs_band
  run quickplay 26000 "3000:keyDown,3300:keyReturn,6000:keyReturn,8500:keyReturn,11000:keyReturn,13500:keyReturn" GD01_SKIP_BOOT=1
  result quickplay "$(( $(has quickplay "goto_screen qp_selsong") && $(has quickplay "ui: start game") ))" "main > Quick Play > song > difficulty > the song plays"
}

# Issue #43: the first song (I Love Rock 'n Roll) on autoplay, to its end and
# the results; the game must leave the song by itself.
walk_song_end() {
  needs_band
  local keep="$seed"
  seed="$work/song_end_seed"
  cp -r "$keep" "$seed"
  echo "${SONG_END_SETTINGS:-{\"autoplay\": true\}}" > "$seed/gd-engine/gd01/enhanced.json"
  run song_end "${SONG_END_MS:-235000}" "${SONG_END_KEYS:-3000:keyDown,3300:keyReturn,6000:keyReturn,8500:keyReturn,11000:keyReturn,13500:keyReturn}" GD01_SKIP_BOOT=1
  seed="$keep"
  result song_end "$(has song_end "goto_screen post_show")" "the song ends and the game moves on to the results"
}

# Mouse only: OPTIONS, Enhanced Settings, collapse MODES and CONTROLS, the Resolution
# dropdown (720p), its own "< Back", Enhanced again, "< Back" again,
# then Credits, skipped with green (1.1.1).
walk_mouse() {
  needs_band
  run mouse 24000 "3000:click640_554,5500:click520_376,8000:click200_190,8500:click200_252,9000:click480_455,9300:keyUp,9400:keyUp,9500:keyUp,9700:keyReturn,10500:click490_687,13000:click520_376,15500:click490_687,18000:click600_564,21000:green" GD01_SKIP_BOOT=1
  local backs
  backs=$(grep -c "goto_screen options" "$work/mouse.log")
  result mouse "$(( $(json "$(save mouse)/enhanced.json" "d['internal_res'] == '720'") && backs >= 4 && $(has mouse "goto_screen credits") ))" \
    "clicks through Options and Enhanced, a dropdown, the help bar's BACK twice, credits skipped"
  # The main menu's GAMEPADS pill opens GAMEPADS on a click (#51).
  run mouse_gamepads 6000 "3000:hover560_685,3500:click560_685" GD01_SKIP_BOOT=1
  result mouse_gamepads "$(has mouse_gamepads "goto_screen gamepads")" "a click on the help bar's GAMEPADS opens GAMEPADS"
  walk_mouse_highlight
  walk_mouse_song_hover
  walk_mouse_song_gaps
  walk_mouse_away
  walk_mouse_character
  walk_enhanced_one_back
}

# The character select (#98, GD01-322): the portraits take the pointer
# (before: only the wheel): a hover on Johnny Napalm focuses him, a locked
# portrait (Grim Ripper) takes nothing, a click on Casey Lynch picks her.
walk_mouse_character() {
  run mouse_character 7000 "3000:hover398_115,3500:hover400_572,4000:click278_267" GD01_SKIP_BOOT=1 GD01_SCREEN=sel_character_new
  local seq
  seq="$(grep -oE "ui: (mouse focus [a-z_.]+|goto_screen sel_guitar)" "$work/mouse_character.log" | tr '\n' '|')"
  result mouse_character "$([ "$seq" = "ui: mouse focus sc_char_punk.pic|ui: mouse focus sc_char_classic.pic|ui: goto_screen sel_guitar|" ] && echo 1 || echo 0)" \
    "a portrait hovered and one clicked, a locked one ignored ($seq)"
}

# The song list (#98, GD01-322): the pointer moved slowly down from row 1 to
# row 3 (4 units a step) highlights rows 1, 2, 3 only: the gaps between rows
# belong to a row (before: each gap dropped the highlight back to the selection).
walk_mouse_song_gaps() {
  local k="3000:keyDown,3300:keyReturn" t=4500 y
  for y in $(seq 400 4 560); do k="$k,$t:hover450_$y"; t=$((t + 60)); done
  run mouse_song_gaps 8000 "$k" GD01_SKIP_BOOT=1
  local seq
  seq="$(grep -oE "ui: (hover row -?[0-9]+|highlight [a-z ]+)" "$work/mouse_song_gaps.log" | tr '\n' '|')"
  result mouse_song_gaps "$([ "$seq" = "ui: hover row 1|ui: hover row 2|ui: hover row 3|" ] && echo 1 || echo 0)" \
    "rows 1 to 3 under the pointer with no gap between them ($seq)"
}

# The menus (#98, GD01-322): the highlight follows the pointer only over the
# items: off them (empty space) nothing is highlighted; back on an item, it
# is; a key press gives the focus its highlight back.
walk_mouse_away() {
  run mouse_away 6500 "3000:hover640_340,3500:hover100_100,4000:hover640_340,4500:hover100_100,5000:keyDown" GD01_SKIP_BOOT=1 GD01_SCREEN=options
  local seq
  seq="$(grep -o "ui: highlight [a-z ]*" "$work/mouse_away.log" | tr '\n' '|')"
  result mouse_away "$([ "$seq" = "ui: highlight none|ui: highlight menu focus|ui: highlight none|ui: highlight menu focus|" ] && echo 1 || echo 0)" \
    "no highlight while the pointer is off the items ($seq)"
}

# The song list (#76): the pointer on Smoke on the Water (row 3) highlights
# it while the selection stays on the first song (no scroll); Down gives the
# highlight back to the selection, which moves one row (to row 1).
walk_mouse_song_hover() {
  run mouse_song_hover 6000 "3000:keyDown,3300:keyReturn,4500:hover450_542,5000:keyDown" GD01_SKIP_BOOT=1
  local seq
  seq="$(grep -o "ui: hover row -*[0-9]*" "$work/mouse_song_hover.log" | tr '\n' '|')"
  result mouse_song_hover "$(( $(has mouse_song_hover "goto_screen qp_selsong") && $([ "$seq" = "ui: hover row 3|ui: hover row -1|" ] && echo 1 || echo 0) ))" \
    "the hovered song row takes the highlight, a key gives it back ($seq)"
}

# Enhanced Settings has one Back, the help bar's original BACK pill (#98,
# GD01-322; #76 had a "< Back" box and no BACK in the bar): a click where the
# box was does nothing, a click on the pill goes back to OPTIONS, and Esc too.
walk_enhanced_one_back() {
  run enhanced_one_back 10000 "3000:click520_376,5000:click150_606,6000:click490_687,7500:click520_376,9000:keyEscape" GD01_SKIP_BOOT=1 GD01_SCREEN=options
  local n
  n=$(grep -c "goto_screen options" "$work/enhanced_one_back.log")
  result enhanced_one_back "$(( $(grep -c "goto_screen enhanced_settings" "$work/enhanced_one_back.log") >= 2 && n >= 2 ))" \
    "the help bar's BACK and Esc go back, the old < Back spot does not (options entered $n times)"
}

# One highlight at a time (#76): the mouse on the help bar's BACK takes the
# highlight from the menu's focus; a key press gives it back; the mouse on a
# menu entry keeps it there.
walk_mouse_highlight() {
  run mouse_highlight 6000 "3000:hover490_687,3600:keyDown,4200:hover490_687,4800:hover640_340" GD01_SKIP_BOOT=1 GD01_SCREEN=options
  local seq
  seq="$(grep -o "ui: highlight [a-z ]*" "$work/mouse_highlight.log" | tr '\n' '|')"
  result mouse_highlight "$([ "$seq" = "ui: highlight help bar|ui: highlight menu focus|ui: highlight help bar|ui: highlight menu focus|" ] && echo 1 || echo 0)" \
    "help bar hover mutes the menu focus; a key or a menu hover gives it back ($seq)"
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
  walk_no_transitions
  walk_easy_chords
  walk_enhanced_hdr
  walk_calibration_reset
  walk_enhanced_backends
}

# Enhanced > Disable Menu Transitions (#76): off by default (OPTIONS opens after
# the original's out and in anims, about 1 s); Enhanced turns it on (the last
# row of PERFORMANCE & GRAPHICS) and saves it; the next start loads it and
# OPTIONS opens at once.
walk_no_transitions() {
  local keys="" keep="$seed" i ms_on ms_off
  run nt_default 6000 "3000:click640_554" GD01_SKIP_BOOT=1
  for i in $(seq 0 38); do keys+="$((2500 + i * 60)):keyDown,"; done
  run nt_set 7000 "${keys}5500:keyReturn" GD01_SKIP_BOOT=1 GD01_SCREEN=enhanced_settings
  seed="$work/nt_set/xdg"
  run nt_on 6000 "3000:click640_554" GD01_SKIP_BOOT=1
  seed="$keep"
  ms_off="$(sed -n 's/.*ui: screen options up after \([0-9]*\) ms.*/\1/p' "$work/nt_default.log" | head -1)"
  ms_on="$(sed -n 's/.*ui: screen options up after \([0-9]*\) ms.*/\1/p' "$work/nt_on.log" | head -1)"
  result no_transitions "$(( $(json "$(save nt_set)/enhanced.json" "d['no_menu_transitions'] is True and not d.get('no_shaky_cam', False)") && ${ms_off:-0} >= 900 && ${ms_on:-9999} < 100 ))" \
    "off by default (${ms_off:-?} ms), Enhanced saves it, the next start loads it (${ms_on:-?} ms)"
}

# Enhanced > Accessibility > Easy Chords (GDALL-039): off on a fresh save (an
# export says false); Enter turns it on (saved) and Export writes it; from a
# fresh save again, Load User Settings with that file turns it on. Rows: 39
# downs reach Disable Menu Transitions, then ACCESSIBILITY, Easy Chords,
# CHEATS & ENHANCERS, Export User Files, Load User Settings.
walk_easy_chords() {
  local s keys="" keep="$seed" i
  s="$work/easy_chords_seed" && rm -rf "$s" && cp -r "$seed" "$s"
  printf '{}\n' > "$s/gd-engine/gd01/enhanced.json"
  seed="$s"
  for i in $(seq 0 40); do keys+="$((2500 + i * 60)):keyDown,"; done  # down to Easy Chords
  mkdir -p "$work/ec_default" "$work/ec_on"
  run ec_default 8000 "${keys}5200:keyDown,5400:keyDown,5600:keyReturn" GD01_SKIP_BOOT=1 GD01_SCREEN=enhanced_settings GD01_DIALOG="$work/ec_default"
  run ec_on 8000 "${keys}5200:keyReturn,5600:keyDown,5800:keyDown,6000:keyReturn" GD01_SKIP_BOOT=1 GD01_SCREEN=enhanced_settings GD01_DIALOG="$work/ec_on"
  run ec_load 8000 "${keys}5200:keyDown,5400:keyDown,5600:keyDown,5800:keyReturn" GD01_SKIP_BOOT=1 GD01_SCREEN=enhanced_settings \
    GD01_DIALOG="$work/ec_on/GD01-user-settings.json"
  seed="$keep"
  result easy_chords "$(( $(json "$work/ec_default/GD01-user-settings.json" "d['easy_chords'] is False") && \
    $(json "$(save ec_on)/enhanced.json" "d['easy_chords'] is True") && \
    $(json "$work/ec_on/GD01-user-settings.json" "d['easy_chords'] is True") && \
    $(json "$(save ec_load)/enhanced.json" "d['easy_chords'] is True") ))" \
    "off by default, Enter turns it on and saves it, Export writes it, Load User Settings brings it back"
}

# Enhanced > Graphics Backend / Audio Backend (GDCORE-006): a value the build
# does not offer is dropped at start (local); the two rows are the first of
# PERFORMANCE & GRAPHICS; Right steps Graphics to SDL3, then Vulkan (each
# change asks Restart Now / Later: Esc is Later), the Audio dropdown picks
# OpenAL (Later again); MODES > Steam Deck keeps both; the next start reads
# them (the "backends:" log line). Then Restart Now restarts at once.
walk_enhanced_backends() {
  local s keys="" keep="$seed" i
  s="$work/enhanced_backends_seed" && rm -rf "$s" && cp -r "$seed" "$s"
  printf '{"graphics_backend":"metal","audio_backend":"nope"}\n' > "$s/gd-engine/gd01/enhanced.json"
  seed="$s"
  for i in $(seq 0 13); do keys+="$((2500 + i * 100)):keyDown,"; done  # down to Graphics Backend
  keys+="4500:keyRight,4800:keyEscape,5100:keyRight,5400:keyEscape,"  # SDL3, Later; Vulkan, Later
  keys+="5700:keyDown,6000:keyReturn,6200:keyDown,6400:keyDown,6700:keyReturn,7000:keyEscape,"  # dropdown: OpenAL, Later
  for i in $(seq 0 13); do keys+="$((7300 + i * 100)):keyUp,"; done  # up to MODES > Steam Deck
  run enhanced_backends 11000 "${keys}9800:keyReturn" GD01_SKIP_BOOT=1 GD01_SCREEN=enhanced_settings
  result enhanced_backends "$(( $(has enhanced_backends "backends: graphics local, audio local") && \
    ! $(has enhanced_backends "restart:") && \
    $(json "$(save enhanced_backends)/enhanced.json" "d['graphics_backend'] == 'vulkan' and d['audio_backend'] == 'openal' and d['max_fps'] == '60'") ))" \
    "unknown backends dropped; Graphics Vulkan, Audio OpenAL saved; Steam Deck mode keeps them"
  cp "$(save enhanced_backends)/enhanced.json" "$s/gd-engine/gd01/enhanced.json"
  run enhanced_backends_start 4000 "" GD01_SKIP_BOOT=1
  seed="$keep"
  result enhanced_backends_start "$(has enhanced_backends_start "backends: graphics vulkan, audio openal")" "the next start reads the saved backends"
  # Restart Now: Graphics to SDL3, Restart Now; the game starts again in the
  # same process on Enhanced Settings (offscreen: SDL3 falls back to local).
  s="$work/enhanced_restart_seed" && rm -rf "$s" && cp -r "$keep" "$s"
  printf '{}\n' > "$s/gd-engine/gd01/enhanced.json"
  seed="$s"
  keys=""
  for i in $(seq 0 13); do keys+="$((2500 + i * 100)):keyDown,"; done
  run enhanced_restart 12000 "${keys}4500:keyRight,5000:keyReturn" GD01_SKIP_BOOT=1 GD01_SCREEN=enhanced_settings
  seed="$keep"
  result enhanced_restart "$(( $(has enhanced_restart "restart: Enhanced Settings asked") && $(has enhanced_restart "backends: graphics sdl3, audio local") && \
    $(grep -c "goto_screen enhanced_settings" "$work/enhanced_restart.log") >= 2 ))" "Restart Now starts the game again on the new backend, back on Enhanced Settings"
}

# Enhanced > Reset Calibration (#47): a saved calibration goes back to 0 ms.
walk_calibration_reset() {
  local s keys="" keep="$seed" i
  s="$work/calibration_seed" && rm -rf "$s" && cp -r "$seed" "$s"
  printf '{"audio_offset":"40","video_offset":"-20"}\n' > "$s/gd-engine/gd01/enhanced.json"
  seed="$s"
  for i in $(seq 0 11); do keys+="$((2500 + i * 100)):keyDown,"; done  # down to Reset Calibration
  run calibration_reset 6000 "${keys}4500:keyReturn" GD01_SKIP_BOOT=1 GD01_SCREEN=enhanced_settings
  seed="$keep"
  result calibration_reset "$(json "$(save calibration_reset)/enhanced.json" "d['audio_offset'] == '0' and d['video_offset'] == '0'")" \
    "Reset Calibration sets the audio and video delays back to 0 ms"
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
  for i in $(seq 0 22); do keys+="$((2500 + i * 100)):keyDown,"; done  # down to the HDR row
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
  # #80: True HDR in Steam Deck Gaming Mode (gamescope) on OpenGL says why and what to change.
  printf '%s\n' '{"bloom":"original","hdr":"true"}' > "$s/gd-engine/gd01/enhanced.json"
  run enhanced_hdr_gamescope 3000 "" GD01_SKIP_BOOT=1 GD01_SCREEN=enhanced_settings GAMESCOPE_WAYLAND_DISPLAY=gamescope-0
  result enhanced_hdr_gamescope "$(has enhanced_hdr_gamescope "hdr: mode true - True HDR unavailable: in Steam Deck Gaming Mode (gamescope) OpenGL has no HDR; set Graphics Backend to Vulkan")" \
    "True HDR under gamescope on OpenGL: fallback reason names the Vulkan backend"
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
  run gamepads_pad 7500 "3000:keyF3,5000:click400_256,5600:click400_372,6200:vpad0" GD01_SKIP_BOOT=1 GD01_VIRTUAL_PAD=054c:0268
  result gamepads_pad "$(json "$(save gamepads_pad)/controllers.json" "d['devices']['keyboard']['player'] == 0 and [v['player'] for k, v in d['devices'].items() if k != 'keyboard'] == [1]")" \
    "Detect binds the pad to Player 2"
}

# A copy of the band save where every <diff> career song is beaten except
# the last one of the last venue (the campaign's final song); [cash] too;
# [band] = the character and guitar already picked (skips those screens).
career_seed() {  # <out dir> <easy|medium|hard|expert> [cash] [band]
  rm -rf "$1" && cp -r "$seed" "$1"
  echo '{"autoplay": true}' > "$1/gd-engine/gd01/enhanced.json"
  python3 -I - "$1/gd-engine/gd01/career.json" "$2" "${3:-0}" "${4:-}" <<'EOF'
import json, sys
p, diff, cash, band = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
order = [('basement', 'iloverockandroll iwannabesedated thunderkiss65 smokeonthewater infected'),
         ('small_club', 'ironman morethanafeeling yougotanotherthingcomin takemeout sharpdressedman'),
         ('big_club', 'killerqueen heyyou stellar heartfullofblack symphofdestruction'),
         ('theatre', 'ziggystardust fatlip cochise takeitoff unsung'),
         ('fest', 'spanishcastlemagic higherground nooneknows aceofspades crossroads'),
         ('arena', 'godzilla texasflood frankenstein cowboysfromhell barkatthemoon')]
if diff == 'easy':
    order = order[:5]  # Easy has no Arena (campaign.h numVenues)
d = json.load(open(p))
c = d['profiles'][0]['careers'][diff]
items = {}
for vi, (v, songs) in enumerate(order):
    items['venue:' + v] = {'unlocked': True}
    for si, s in enumerate(songs.split()):
        beaten = not (vi == len(order) - 1 and si == 4)
        items['song:' + s] = {'unlocked': True, 'beaten': beaten, 'score': 100000 if beaten else 0, 'stars': 4 if beaten else 0}
c.update(items=items, cash=cash)
if band:
    c.update(character='metal', guitar='gibson_sg_cherry')
d['profiles'][0].update(last_difficulty=diff, tutorials_done=3)
json.dump(d, open(p, 'w'))
EOF
}

# Unlock Shop: Career > UNLOCK SHOP > Guitars > the Gibson ES-335 ($600),
# SOLD, back out twice: the purchase is saved by the shop's autosave_goto.
walk_store() {
  needs_band
  local keep="$seed"
  career_seed "$work/store_seed" medium 50000 band
  seed="$work/store_seed"
  run store 27000 "3000:keyReturn,5500:keyReturn,8000:keyDown,8300:keyDown,8600:keyDown,9000:keyReturn,12000:keyReturn,14500:keyReturn,17000:keyReturn,19500:red,22000:red" GD01_SKIP_BOOT=1
  seed="$keep"
  result store "$(( $(has store "goto_screen store_bought") && $(json "$(save store)/career.json" "d['profiles'][0]['careers']['medium']['cash'] == 49400 and d['profiles'][0]['careers']['medium']['items']['guitar:gibson_es335']['unlocked']") ))" \
    "buy the ES-335 for \$600, saved on leaving the shop"
}

# The campaign's final song (Bark at the Moon, Expert) on autoplay: win_game,
# the Battle Axe (display_battleaxe, Expert only), the results, the cash
# award and back to the career (goto_endgame_complete). About 6 minutes.
walk_career_win() {
  needs_band
  local keep="$seed" keys="3000:keyReturn,5500:keyReturn,8000:keyReturn,10500:keyReturn,13000:keyReturn,18000:keyReturn,21000:keyReturn,24000:keyReturn" t
  for t in $(seq 316000 8000 356000); do keys+=",$t:keyReturn"; done
  career_seed "$work/win_seed" expert
  seed="$work/win_seed"
  run career_win 365000 "$keys" GD01_SKIP_BOOT=1
  seed="$keep"
  local order
  order=$(grep -oE "goto_screen (post_show|win_game|endgame|status|cashaward|career)$" "$work/career_win.log" | awk '{print $2}' | uniq | tr '\n' ' ')
  result career_win "$( [[ "$order" == *"post_show win_game endgame status cashaward career"* ]] && echo 1 || echo 0 )" \
    "final song won: win_game, Battle Axe, results, cash award, career"
  result career_win_axe "$(json "$(save career_win)/career.json" "d['profiles'][0]['careers']['expert']['items']['guitar:battleaxe']['unlocked'] and d['profiles'][0]['careers']['expert']['items']['song:barkatthemoon']['beaten']")" \
    "the Battle Axe is unlocked and the final song saved as beaten"
}

for w in "${walks[@]}"; do
  if declare -F "walk_$w" >/dev/null; then "walk_$w"; else echo "unknown walk: $w"; fails=$((fails + 1)); fi
done
echo "walks: $fails failure(s)"
exit "$fails"
