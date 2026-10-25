#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# Builds gd01 the same way the release workflow does, into
# gd-game-01/local_builds/ (git-ignored):
#
#   local_builds/gd01-<version>-<build>/        ready to run
#   local_builds/gd01-<version>-<build>.<ext>   the download a release would publish
#
# Usage: gd-game-01/local_build.sh [build ...] [--native] [--no-dlss] [--force]
#   (no build)  the one for the system you are on
#   linux       Linux. With podman or docker: in an ubuntu:22.04 container
#               (exactly like the release; runs on any distribution from 2022
#               on); without one, or with --native: this machine's compiler and
#               SDL3. DLSS is fetched and included (--no-dlss skips it).
#   steamdeck   Steam Deck (on Linux): in Valve's Steam Runtime "sniper" SDK
#               container (needs podman or docker), with the handheld defaults.
#   windows     Windows: cross-compiled with MinGW on Linux, or natively in an
#               MSYS2 UCRT64 shell on Windows (see engine/build_windows.sh).
#   macos       macOS (on a Mac): brew install cmake sdl3 ninja dylibbundler
#   android     the APK (needs the Android SDK, see engine/build_android.sh);
#               signed with the release key when ~/.config/gd-engine has it.
#   all         every build this system can make.
#   --force     build even when no source changed since the last build
#               (normally such a build is skipped and reported "up to date").
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"
engine="$here/engine"
out="$here/local_builds"

case "$(uname -s)" in
  Linux) host=linux ;;
  Darwin) host=macos ;;
  MINGW*|MSYS*|CYGWIN*) host=windows ;;
  *) echo "local_build.sh: unsupported system $(uname -s)" >&2; exit 2 ;;
esac

builds=() native=0 dlss=1 force=0
for a in "$@"; do
  case "$a" in
    linux|steamdeck|windows|macos|android) builds+=("$a") ;;
    all) case "$host" in
           linux) builds+=(linux steamdeck windows android) ;;
           macos) builds+=(macos android) ;;
           windows) builds+=(windows) ;;
         esac ;;
    --native) native=1 ;;
    --no-dlss) dlss=0 ;;
    --force) force=1 ;;
    -h|--help) sed -n '4,25p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "local_build.sh: unknown build or option $a (see --help)" >&2; exit 2 ;;
  esac
done
[ ${#builds[@]} -eq 0 ] && builds=("$host")

if [ -t 1 ]; then B=$'\e[1m' G=$'\e[32m' Y=$'\e[33m' R=$'\e[31m' C=$'\e[36m' N=$'\e[0m'; else B="" G="" Y="" R="" C="" N=""; fi
say() { printf '   -> %s\n' "$*"; }

version="$(sed -n 's/^project(gd01 VERSION \([0-9.]*\).*/\1/p' "$engine/CMakeLists.txt")"
mkdir -p "$out/logs"

label() {
  case "$1" in
    linux) echo "Linux" ;; steamdeck) echo "Steam Deck" ;; windows) echo "Windows" ;;
    macos) echo "macOS" ;; android) echo "Android" ;;
  esac
}
artifact() {  # the download a build makes
  case "$1" in
    linux) echo "$out/gd01-$version-linux-x86_64.tar.gz" ;;
    steamdeck) echo "$out/gd01-$version-steamdeck.tar.gz" ;;
    windows) echo "$out/gd01-$version-windows-x64.zip" ;;
    macos) echo "$out/gd01-$version-macos.zip" ;;
    android) echo "$out/gd01-$version-android.apk" ;;
  esac
}
inputs() {  # the files a build is made from
  local common=("$engine/src" "$engine/CMakeLists.txt" "$engine/third_party" "$engine/README.md" "$engine/res" "$engine/cmake"
                "$engine/build_common.sh" "$engine/package.sh" "$repo/LICENSE" "$repo/LICENSE-ADDITIONAL-TERMS.md")
  case "$1" in
    linux) echo "${common[@]}" "$engine/build_linux_portable.sh" ;;
    steamdeck) echo "${common[@]}" "$engine/build_linux_steamdeck.sh" ;;
    windows) echo "${common[@]}" "$engine/build_windows.sh" ;;
    macos) echo "${common[@]}" "$engine/build_macos.sh" ;;
    android) echo "${common[@]}" "$engine/build_android.sh" "$engine/android" ;;
  esac
}
changed() {  # <build>: the inputs newer than its last download
  # shellcheck disable=SC2046
  find $(inputs "$1") \( -name .gradle -o -name build \) -prune -o -type f -newer "$(artifact "$1")" -print 2>/dev/null
}

echo "${B}gd01 $version: local build${N}"
names=""
for b in "${builds[@]}"; do names="$names${names:+, }$(label "$b")"; done
echo "Builds: $names  (on $(label "$host"); results in $out)"
echo

# Same first step as the release: nothing that must not ship (game data,
# decompiler output, keys, personal paths) is in the repo.
if command -v python3 >/dev/null; then
  printf '%s\n' "${B}=== Repo guard ===${N}"
  if (cd "$repo" && python3 .github/scripts/repo_guard.py) > "$out/logs/repo-guard.log" 2>&1; then
    say "$(tail -1 "$out/logs/repo-guard.log")"
  else
    cat "$out/logs/repo-guard.log"
    echo "${R}The repo guard found files that must not be in the repo (above). Nothing was built.${N}"
    exit 1
  fi
  echo
fi

ship() {  # <platform> <dist folder>: copy the runnable folder and make the download
  local name="gd01-$version-${2#gd01-}"  # gd01-1.0.0-linux-x86_64, like the download
  say "Copying to local_builds/$name/ and packaging..."
  rm -rf "${out:?}/$name"
  cp -R "$engine/dist/$2" "$out/$name"
  "$engine/package.sh" "$1" "$version" "$out" >/dev/null
}

container_runner() {
  if command -v podman >/dev/null; then echo podman
  elif command -v docker >/dev/null; then echo docker
  fi
}

in_container() {  # <runner> <image> <script under gd-game-01/engine>
  local fix_owner=""
  # Docker's root writes root-owned files into the checkout; podman's root is you.
  [ "$1" = docker ] && fix_owner="; chown -R $(id -u):$(id -g) gd-game-01/engine/build gd-game-01/engine/dist"
  if ! "$1" image exists "$2" 2>/dev/null && ! "$1" image inspect "$2" >/dev/null 2>&1; then
    say "Downloading the container image $2 (first time only)..."
  fi
  "$1" run --rm -v "$repo":/src:Z -w /src "$2" bash -c "gd-game-01/engine/$3$fix_owner"
}

build_linux() {
  if [ "$dlss" = 1 ]; then
    say "NVIDIA DLSS: $("$here/tools/fetch_dlss.sh" | tail -1)"
  fi
  local runner=""
  [ "$native" = 0 ] && runner="$(container_runner)"
  if [ -n "$runner" ]; then
    say "Building in an ubuntu:22.04 container ($runner), exactly like the release"
    in_container "$runner" docker.io/library/ubuntu:22.04 build_linux_portable.sh
  else
    say "Building natively (no container; needs cmake, ninja, a C++20 compiler and SDL3 development files)"
    . "$engine/build_common.sh"
    cmake -S "$engine" -B "$engine/build/native" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_BUILD_RPATH='$ORIGIN' -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON -DCMAKE_INSTALL_RPATH='$ORIGIN' >/dev/null
    compile "$engine/build/native" gd01
    local d="$engine/dist/gd01-linux-x86_64"
    rm -rf "$d" && mkdir -p "$d/licenses"
    cp "$engine/build/native/gd01" "$engine/README.md" "$repo/LICENSE" "$repo/LICENSE-ADDITIONAL-TERMS.md" "$d/"
    cp "$engine/res/gd01_icon_512.png" "$d/gd01.png"
    local sdl
    sdl="$(ldd "$d/gd01" | awk '/libSDL3\.so/ {print $3}')"
    [ -n "$sdl" ] && cp -L "$sdl" "$d/"
    cp "$engine/third_party/fidelityfx/LICENSE.txt" "$d/licenses/FidelityFX-SDK-LICENSE.txt"
    cp "$engine/third_party/volk/LICENSE.md" "$d/licenses/volk-LICENSE.md"
    cp "$engine/third_party/vulkan/LICENSE.md" "$d/licenses/Vulkan-Headers-LICENSE.md"
    if ls "$engine"/third_party/dlss/lib/Linux_x86_64/libnvidia-ngx-dlss.so.* >/dev/null 2>&1; then
      cp "$engine"/third_party/dlss/lib/Linux_x86_64/libnvidia-ngx-dlss.so.* "$d/"
      cp "$engine/third_party/dlss/LICENSE.txt" "$d/licenses/NVIDIA-RTX-SDK-LICENSE.txt"
    fi
  fi
  ship linux gd01-linux-x86_64
}

build_steamdeck() {
  local runner
  runner="$(container_runner)"
  if [ -z "$runner" ]; then
    echo "The Steam Deck build needs podman or docker (it runs in Valve's Steam Runtime SDK)." >&2
    return 2
  fi
  say "Building in Valve's Steam Runtime 3 'sniper' SDK container ($runner), exactly like the release"
  in_container "$runner" registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest build_linux_steamdeck.sh
  ship steamdeck gd01-steamdeck
}

build_windows() {
  if [ "$host" = linux ]; then say "Cross-compiling with MinGW-w64, exactly like the release"
  else say "Building with MSYS2 (MinGW-w64)"; fi
  "$engine/build_windows.sh"
  ship windows gd01-windows-x64
}

build_macos() {
  "$engine/build_macos.sh"
  # Apple's GLSL compiler is stricter than the Linux and Windows drivers: compile
  # every shader on this Mac before shipping (exit 2: no GL context, e.g. over ssh).
  local rc=0
  "$engine/dist/gd01-macos/gd01.app/Contents/MacOS/gd01" --shadertest > "$out/logs/macos-shadertest.log" 2>&1 || rc=$?
  case "$rc" in
    0) say "Shaders: $(tail -1 "$out/logs/macos-shadertest.log")" ;;
    2) say "${Y}Shaders: not checked (no OpenGL context here; see local_builds/logs/macos-shadertest.log)${N}" ;;
    *) grep -E 'ERROR|FAILED|failed' "$out/logs/macos-shadertest.log" | head -20
       say "${R}Shaders: a shader does not compile on this Mac (see local_builds/logs/macos-shadertest.log)${N}"
       return 1 ;;
  esac
  ship macos gd01-macos
}

build_android() {
  local key="$HOME/.config/gd-engine/gd01-release-key.txt"
  if [ -z "${GD01_RELEASE_KEYSTORE:-}" ] && [ -f "$key" ]; then
    local pw
    pw="$(sed -n 's/^keystore password (also the key password): //p' "$key")"
    export GD01_RELEASE_KEYSTORE="$HOME/.config/gd-engine/gd01-release.jks" GD01_RELEASE_KEY_ALIAS=gd01
    export GD01_RELEASE_KEYSTORE_PASSWORD="$pw" GD01_RELEASE_KEY_PASSWORD="$pw"
  fi
  ABIS="${ABIS:-arm64-v8a}" "$engine/build_android.sh"
  say "Packaging..."
  "$engine/package.sh" android "$version" "$out" >/dev/null
}

can_build() {  # <build>: whether this system can make it
  case "$1:$host" in
    linux:linux|steamdeck:linux|windows:linux|windows:windows|macos:macos|android:*) return 0 ;;
    *) return 1 ;;
  esac
}

elapsed() { local s=$1; if [ "$s" -ge 60 ]; then echo "$((s / 60))m$((s % 60))s"; else echo "${s}s"; fi; }

statuses=() times=() failed=0 i=0
for b in "${builds[@]}"; do
  i=$((i + 1))
  name="$(label "$b")"
  art="$(artifact "$b")"
  echo "${B}${C}=== Build $i of ${#builds[@]}: $name ===${N}"
  if ! can_build "$b"; then
    say "${Y}Skipped: the $name build cannot be made on $(label "$host")${N}"
    statuses+=("${Y}skipped (needs $( [ "$b" = macos ] && echo a Mac || echo Linux ))${N}") times+=("-")
    echo; continue
  fi
  if [ "$force" = 0 ] && [ -f "$art" ]; then
    n="$(changed "$b" | wc -l)"
    if [ "$n" = 0 ]; then
      say "${G}Nothing to build: no source file changed since $(basename "$art") was made ($(date -r "$art" '+%Y-%m-%d %H:%M')).${N}"
      say "It is ready in local_builds/. To build it again anyway: local_build.sh $b --force"
      statuses+=("${G}up to date (nothing to build)${N}") times+=("-")
      echo; continue
    fi
    say "Building: $n file(s) changed since the last build, e.g.:"
    changed "$b" | head -3 | sed "s|^$repo/|      |"
  elif [ "$force" = 1 ]; then
    say "Building (--force)"
  else
    say "Building: no previous $name build of $version in local_builds/"
  fi
  t0=$(date +%s)
  set +e
  ( set -e; "build_$b" ) 2>&1 | tee "$out/logs/$b.log"
  rc=${PIPESTATUS[0]}
  set -e
  t=$(elapsed $(( $(date +%s) - t0 )))
  if [ "$rc" = 0 ] && grep -q "nothing to compile, already up to date" "$out/logs/$b.log"; then
    say "${G}$name: repackaged in $t (the code was already compiled) -> local_builds/$(basename "$art")${N}"
    statuses+=("${G}repackaged (code already compiled)${N}") times+=("$t")
  elif [ "$rc" = 0 ]; then
    say "${G}$name: built in $t -> local_builds/$(basename "$art")${N}"
    statuses+=("${G}built${N}") times+=("$t")
  else
    say "${R}$name: FAILED after $t (full log: local_builds/logs/$b.log)${N}"
    statuses+=("${R}FAILED (see local_builds/logs/$b.log)${N}") times+=("$t")
    failed=$((failed + 1))
  fi
  echo
done

echo "${B}=== Summary: gd01 $version ===${N}"
printf '  %-11s %-8s %s\n' "Build" "Time" "Status"
for k in "${!builds[@]}"; do
  printf '  %-11s %-8s %s\n' "$(label "${builds[$k]}")" "${times[$k]}" "${statuses[$k]}"
done
echo "  Files: $out"
[ "$failed" = 0 ]
