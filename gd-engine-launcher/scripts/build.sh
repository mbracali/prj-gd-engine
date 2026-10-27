#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# Builds the Guitar Dream Universal Launcher and its game modules into
#
#   gd-engine-launcher/local_builds/gdul-<version>-<os>-x86_64/   (git-ignored)
#
# Usage: gd-engine-launcher/scripts/build.sh [linux] [windows] [all] [--force]
#   (no build)  the one for the system you are on
#   linux       Linux x86-64, with this machine's compiler and SDL3 (cmake,
#               ninja, a C++20 compiler, SDL3 development files)
#   windows     Windows x86-64, cross-compiled with MinGW-w64 (Fedora:
#               dnf install mingw64-gcc-c++ mingw64-SDL3), or natively in MSYS2
#   all         both
#   --force     rebuild every game module (normally a module is rebuilt only
#               when its game-info.yaml guitar_dream_version differs from the
#               one in the previous build's gd-engine-games/manifest.json)
#
# The launcher itself is always (re)compiled; CMake/Ninja skip what is up to
# date. Logs: gd-engine-launcher/logs/ (build-<os>.log and one log per step).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
launcher="$(cd "$here/.." && pwd)"
repo="$(cd "$launcher/.." && pwd)"
logs="$launcher/logs"
out_root="$launcher/local_builds"
mkdir -p "$logs" "$out_root"

case "$(uname -s)" in
  Linux) host=linux ;;
  MINGW*|MSYS*|CYGWIN*) host=windows ;;
  Darwin) host=macos ;;
  *) host=unknown ;;
esac
targets=() force=0
for a in "$@"; do
  case "$a" in
    linux|windows) targets+=("$a") ;;
    all) targets+=(linux windows) ;;
    --force) force=1 ;;
    -h|--help) sed -n '4,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "build.sh: unknown build or option $a (see --help)" >&2; exit 2 ;;
  esac
done
[ ${#targets[@]} -eq 0 ] && targets=("$host")

if [ -t 1 ]; then B=$'\e[1m' G=$'\e[32m' Y=$'\e[33m' R=$'\e[31m' C=$'\e[36m' N=$'\e[0m'; else B="" G="" Y="" R="" C="" N=""; fi
say() { printf '   -> %s\n' "$*"; }
yaml_value() {  # <file> <key>: a top-level scalar, quotes removed
  sed -n "s/^$2:[[:space:]]*//p" "$1" | sed 's/[[:space:]]*#.*$//; s/^"\(.*\)"$/\1/; s/^'"'"'\(.*\)'"'"'$/\1/' | head -1
}
version="$(yaml_value "$launcher/launcher-info.yaml" launcher_version)"
mapfile -t game_infos < <(sed -n 's/^[[:space:]]*-[[:space:]]*\([^#[:space:]]*game-info\.yaml\).*/\1/p' "$launcher/games.yaml")

# step <log name> <description> <command...>: runs a step, its output in logs/<log name>.log.
step() {
  local name="$1" what="$2"; shift 2
  say "$what (log: gd-engine-launcher/logs/$name.log)"
  # In the background so that `set -e` holds inside the step (it is ignored
  # in a condition), then waited for.
  local rc=0
  "$@" > "$logs/$name.log" 2>&1 &
  wait $! || rc=$?
  if [ "$rc" != 0 ]; then
    grep -E 'error|Error|FAILED' "$logs/$name.log" | head -30 || tail -30 "$logs/$name.log"
    say "${R}FAILED: $what${N}"
    return 1
  fi
}
warnings_in() { grep -c -E 'warning:' "$1" || true; }

# ---- the launcher ----
cmake_args() {  # <os>: cross-compile settings
  if [ "$1" = windows ] && [ "$host" != windows ]; then
    echo "-DCMAKE_TOOLCHAIN_FILE=${MINGW_TOOLCHAIN:-/usr/share/mingw/toolchain-mingw64.cmake}"
  fi
}
build_launcher() {  # <os> <build dir>
  # shellcheck disable=SC2046
  cmake -S "$launcher" -B "$2" -G Ninja -DCMAKE_BUILD_TYPE=Release $(cmake_args "$1") &&
    cmake --build "$2"
}

# ---- the game modules: one recipe per game (build_module_<NN> <os> <work dir> <game folder out>) ----
build_module_02() {
  local os="$1" work="$2" dest="$3" engine="$repo/gd-game-02/engine" ffmpeg
  ffmpeg="$engine/build/ffmpeg/$os/ffmpeg-prefix"
  (
    cd "$engine"
    # shellcheck source=/dev/null
    . ./build_common.sh
    if [ "$os" = windows ] && [ "$host" != windows ]; then
      build_ffmpeg build/ffmpeg/windows Windows --disable-x86asm --enable-cross-compile --target-os=mingw32 --arch=x86_64 \
        --cross-prefix=x86_64-w64-mingw32- --pkg-config=false
    elif [ "$os" = windows ]; then
      build_ffmpeg build/ffmpeg/windows Windows --disable-x86asm
    else
      build_ffmpeg build/ffmpeg/linux Linux --disable-x86asm
    fi
  )
  # shellcheck disable=SC2046
  cmake -S "$engine" -B "$work" -G Ninja -DCMAKE_BUILD_TYPE=Release -DGD02_MODULE=ON -DGD02_VIDEO=ON \
    -DGD02_FFMPEG_DIR="$ffmpeg" $(cmake_args "$os")
  cmake --build "$work" --target gd02_module
  mkdir -p "$dest/licenses"
  if [ "$os" = windows ]; then
    cp "$work/gd-game-02.dll" "$ffmpeg"/bin/av*.dll "$dest/"
  else
    cp "$work/gd-game-02.so" "$dest/"
    for lib in avformat avcodec avutil; do
      cp -L "$(ls "$ffmpeg/lib/lib$lib.so."[0-9]* | grep -E '\.so\.[0-9]+$' | head -1)" "$dest/"
    done
  fi
  cp "$engine/third_party/stb_vorbis/LICENSE.txt" "$dest/licenses/stb_vorbis-LICENSE.txt"
  (. "$engine/build_common.sh"; ship_ffmpeg_licence "$ffmpeg" "$dest")
  cp "$engine/README.md" "$dest/README.md"
}

build_module_01() {
  local os="$1" work="$2" dest="$3" engine="$repo/gd-game-01/engine"
  # shellcheck disable=SC2046
  cmake -S "$launcher/modules/gd01" -B "$work" -G Ninja -DCMAKE_BUILD_TYPE=Release $(cmake_args "$os")
  cmake --build "$work"
  mkdir -p "$dest/licenses"
  if [ "$os" = windows ]; then cp "$work/gd-game-01.dll" "$dest/"; else cp "$work/gd-game-01.so" "$dest/"; fi
  cp "$engine/third_party/fidelityfx/LICENSE.txt" "$dest/licenses/FidelityFX-SDK-LICENSE.txt"
  cp "$engine/third_party/volk/LICENSE.md" "$dest/licenses/volk-LICENSE.md"
  cp "$engine/third_party/vulkan/LICENSE.md" "$dest/licenses/Vulkan-Headers-LICENSE.md"
  if [ "$os" = linux ] && ls "$engine"/third_party/dlss/lib/Linux_x86_64/libnvidia-ngx-dlss.so.* >/dev/null 2>&1; then
    # NGX loads the DLSS model from the program's folder: the launcher's root
    # (copied there when the build is assembled).
    mkdir -p "$dest/root"
    cp "$engine"/third_party/dlss/lib/Linux_x86_64/libnvidia-ngx-dlss.so.* "$dest/root/"
    cp "$engine/third_party/dlss/LICENSE.txt" "$dest/licenses/NVIDIA-RTX-SDK-LICENSE.txt"
  fi
  cp "$repo/gd-game-01/README.md" "$dest/README.md"
}

previous_manifest() {  # <os> <this build's folder>: the manifest of the newest earlier build for that system
  local os="$1" this="$2" best="" f
  [ -f "$this/gd-engine-games/manifest.json" ] && { echo "$this/gd-engine-games/manifest.json"; return; }
  for f in "$out_root"/gdul-*-"$os"-x86_64/gd-engine-games/manifest.json; do
    [ -f "$f" ] || continue
    if [ -z "$best" ] || [ "$f" -nt "$best" ]; then best="$f"; fi
  done
  echo "$best"
}
manifest_get() {  # <manifest> <game id> <field>
  [ -f "$1" ] || return 0
  python3 -I -c 'import json,sys
m=json.load(open(sys.argv[1])); g=m.get("games",{}).get(sys.argv[2],{}); print(g.get(sys.argv[3],""))' "$1" "$2" "$3"
}

build_os() {
  local os="$1"
  local name="gdul-$version-$os-x86_64"
  local out="$out_root/$name" work="$launcher/build/$os"
  local exe="Guitar Dream Universal Launcher" ext="" modext=".so"
  [ "$os" = windows ] && ext=".exe" modext=".dll"
  echo "${B}${C}=== $name ===${N}"
  local prev
  prev="$(previous_manifest "$os" "$out")"
  [ -n "$prev" ] && say "Previous build: ${prev#"$launcher/"}"
  local stage="$out_root/.$name.new"
  rm -rf "$stage" && mkdir -p "$stage/licenses" "$stage/manual" "$stage/gd-engine-games" "$stage/original_isos"

  step "$os-launcher" "Launcher: compiling" build_launcher "$os" "$work/launcher"
  local w
  w="$(warnings_in "$logs/$os-launcher.log")"
  if [ "$w" != 0 ]; then say "${R}Launcher: $w compiler warning(s) (see logs/$os-launcher.log)${N}"; return 1; fi
  say "${G}Launcher: compiled, 0 warnings${N}"
  cp "$work/launcher/$exe$ext" "$stage/"

  # Games: every game-info.yaml (the launcher lists them all), and a module
  # for each playable one, reused from the previous build when its version
  # is the same.
  cp "$launcher/games.yaml" "$launcher/launcher-info.yaml" "$stage/gd-engine-games/"
  local entries=() gi id ver ready nn mod
  for gi in "${game_infos[@]}"; do
    id="$(yaml_value "$repo/$gi" id)"
    ver="$(yaml_value "$repo/$gi" guitar_dream_version)"
    ready="$(yaml_value "$repo/$gi" ready_to_play)"
    mkdir -p "$stage/gd-engine-games/$(dirname "$gi")"
    cp "$repo/$gi" "$stage/gd-engine-games/$gi"
    case "$ready" in true|True|yes) ;; *) continue ;; esac
    nn="${id##*-}"
    mod="$id$modext"
    if ! declare -F "build_module_$nn" >/dev/null; then
      say "${Y}$id: ready_to_play but the launcher has no module recipe (build_module_$nn); listed only${N}"
      continue
    fi
    local dest="$stage/gd-engine-games/$id" prev_dir="" prev_ver="" built_at reused=""
    if [ -n "$prev" ]; then
      prev_dir="$(dirname "$prev")/$id"
      prev_ver="$(manifest_get "$prev" "$id" guitar_dream_version)"
    fi
    if [ "$force" = 0 ] && [ -n "$prev_ver" ] && [ "$prev_ver" = "$ver" ] && [ -f "$prev_dir/$mod" ]; then
      say "${G}$id $ver: same version as the previous build, module reused${N}"
      cp -R "$prev_dir/." "$dest/"
      cp "$repo/$gi" "$dest/game-info.yaml"
      if [ -f "$dest/root-files.txt" ]; then  # its files at the launcher's root, from that build's root
        mkdir -p "$dest/root"
        while IFS= read -r f; do cp "$(dirname "$(dirname "$prev")")/$f" "$dest/root/"; done < "$dest/root-files.txt"
      fi
      built_at="$(manifest_get "$prev" "$id" built_at)"
      reused="true"
    else
      [ -n "$prev_ver" ] && [ "$prev_ver" != "$ver" ] && say "$id: version $prev_ver -> $ver"
      step "$os-$id" "$id $ver: building the module" "build_module_$nn" "$os" "$work/$id" "$dest" || return 1
      built_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
      reused="false"
      say "${G}$id $ver: module built ($(warnings_in "$logs/$os-$id.log") compiler warnings)${N}"
    fi
    entries+=("$id|$ver|$mod|$built_at|$reused")
  done

  # Root: platform libraries, licences, manual, README.
  if [ "$os" = windows ]; then
    cp "${MINGW_SDL3_DLL:-/usr/x86_64-w64-mingw32/sys-root/mingw/bin/SDL3.dll}" "$stage/"
    cp "$launcher/res/gdul.ico" "$stage/$exe.ico"
    cp /usr/share/licenses/mingw64-SDL3/LICENSE.txt "$stage/licenses/SDL3-LICENSE.txt" 2>/dev/null ||
      cp /usr/share/licenses/SDL3/LICENSE.txt "$stage/licenses/SDL3-LICENSE.txt"
  else
    local sdl
    sdl="$(ldd "$stage/$exe" | awk '/libSDL3\.so/ {print $3}')"
    cp -L "$sdl" "$stage/libSDL3.so.0"
    cp "$launcher/res/gdul_icon_512.png" "$stage/$exe.png"
    cp /usr/share/licenses/SDL3/LICENSE.txt "$stage/licenses/SDL3-LICENSE.txt"
  fi
  for d in "$stage"/gd-engine-games/gd-game-*/; do
    [ -d "$d/licenses" ] && for f in "$d"licenses/*; do cp "$f" "$stage/licenses/$(basename "$d")-$(basename "$f")"; done
    if [ -d "$d/root" ]; then  # files the game needs at the launcher's root
      (cd "$d/root" && ls) > "$d/root-files.txt"
      mv "$d"root/* "$stage/" && rmdir "$d/root"
    fi
  done
  cp "$launcher/third_party/stb/LICENSE" "$stage/licenses/stb-LICENSE.txt"
  cp "$launcher/res/fonts/MetalMania-OFL.txt" "$stage/licenses/MetalMania-OFL.txt"
  cp "$launcher/res/fonts/BarlowCondensed-OFL.txt" "$stage/licenses/BarlowCondensed-OFL.txt"
  cp "$repo/LICENSE" "$repo/LICENSE-ADDITIONAL-TERMS.md" "$stage/"
  cp "$launcher/package/README.md" "$stage/README.md"
  cp "$launcher/package/original_isos-README.md" "$stage/original_isos/README.md"
  cp "$launcher/manual/"*.md "$stage/manual/"

  # The manifest: what each game module was built from.
  local commit
  commit="$(git -C "$repo" rev-parse --short HEAD 2>/dev/null || echo unknown)"
  python3 -I - "$stage/gd-engine-games/manifest.json" "$version" "$os" "$commit" "${entries[@]}" <<'PY'
import json, sys, datetime
path, version, os_name, commit, *entries = sys.argv[1:]
games = {}
for e in entries:
    gid, ver, mod, built, reused = e.split("|")
    games[gid] = {"guitar_dream_version": ver, "module": mod, "built_at": built, "reused": reused == "true"}
json.dump({"launcher_version": version, "os": os_name, "arch": "x86_64",
           "assembled_at": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
           "source_commit": commit, "games": games}, open(path, "w"), indent=2)
open(path, "a").write("\n")
PY
  rm -rf "$out"
  mv "$stage" "$out"
  say "${G}Ready: gd-engine-launcher/local_builds/$name/${N}"
}

echo "${B}Guitar Dream Universal Launcher $version: build${N}"
if command -v python3 >/dev/null; then
  if (cd "$repo" && python3 .github/scripts/repo_guard.py) > "$logs/repo-guard.log" 2>&1; then
    say "$(tail -1 "$logs/repo-guard.log")"
  else
    cat "$logs/repo-guard.log"
    echo "${R}The repo guard found files that must not be in the repo. Nothing was built.${N}"
    exit 1
  fi
fi
failed=0
for t in "${targets[@]}"; do
  if [ "$t" = windows ] || [ "$host" = "$t" ]; then
    { set +e; ( set -e; build_os "$t" ) 2>&1 | tee "$logs/build-$t.log"; rc=${PIPESTATUS[0]}; set -e; }
    [ "$rc" = 0 ] || failed=$((failed + 1))
  else
    say "${Y}Skipped $t: it cannot be built on $host${N}"
  fi
  echo
done
[ "$failed" = 0 ]
