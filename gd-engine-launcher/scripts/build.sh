#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# Builds the Guitar Dream Universal Launcher and its game modules into
#
#   gd-engine-launcher/local_builds/gdul-<version>-<os>-<arch>/   (git-ignored)
#
# Usage: gd-engine-launcher/scripts/build.sh [linux] [steamdeck] [windows] [macos] [android] [all]
#                                            [--force] [--previous <folder>]
#   (no build)  the one for the system you are on
#   linux       Linux x86-64, with this machine's compiler and SDL3 (cmake,
#               ninja, a C++20 compiler, SDL3 development files)
#   steamdeck   Steam Deck (on Linux): gdul-<version>-steamdeck-x86_64, built in
#               Valve's Steam Runtime 3 "sniper" SDK container (podman or docker;
#               registry.gitlab.steamos.cloud/steamrt/sniper/sdk), with every
#               game module's Steam Deck defaults (GD01_STEAM_DECK, GD02_STEAM_DECK)
#               and the launcher's (GDUL_STEAM_DECK); STEAM-DECK.txt inside
#   windows     Windows x86-64, cross-compiled with MinGW-w64 (Fedora:
#               dnf install mingw64-gcc-c++ mingw64-SDL3), or natively in MSYS2
#   macos       macOS arm64 (Apple silicon), on a Mac with Homebrew (brew install
#               bash cmake ninja sdl3 dylibbundler): "Guitar Dream Universal
#               Launcher.app", ad-hoc signed, not notarised
#   android     Android arm64: gdul-<version>-android-arm64.apk, with a JDK 17/21
#               and the Android SDK at $ANDROID_HOME (default ~/Android/Sdk) with
#               the packages gd-game-02/engine/build_android.sh lists. Signed with
#               the Android debug key, or with GDUL_RELEASE_KEYSTORE (+
#               GDUL_RELEASE_KEYSTORE_PASSWORD, GDUL_RELEASE_KEY_ALIAS,
#               GDUL_RELEASE_KEY_PASSWORD). GDUL_ABI=x86_64 builds
#               gdul-<version>-android-x86_64.apk instead (for the x86-64
#               emulator; not released)
#   all         every one this system can build
#   --force     rebuild every game module (normally a module is reused when its
#               game-info.yaml guitar_dream_version and the launcher's module
#               SDK (sdk/, modules/gdNN/) are the same as in the previous build's
#               gd-engine-games/manifest.json)
#   --previous <folder>  where the previous build is (also GDUL_PREVIOUS_BUILD):
#               a build folder, or unpacked release downloads (an .apk is
#               unpacked too); default: the newest build in local_builds/
#
# Every build carries the module of EVERY game with ready_to_play true in
# games.yaml (a build_module_NN recipe for each, for every system): a game
# that cannot be built for a system fails the build, it is never left out.
#
# The launcher itself is always (re)compiled; CMake/Ninja skip what is up to
# date. Logs: gd-engine-launcher/logs/ (build-<os>.log and one log per step).
set -euo pipefail
shopt -s inherit_errexit  # `set -e` inside $(...) too
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
targets=() force=0 previous="${GDUL_PREVIOUS_BUILD:-}"
while [ $# -gt 0 ]; do
  case "$1" in
    linux|steamdeck|windows|macos|android) targets+=("$1") ;;
    all) targets+=(linux steamdeck windows macos android) ;;
    --force) force=1 ;;
    --previous) [ $# -ge 2 ] || { echo "build.sh: --previous needs a folder" >&2; exit 2; }; previous="$2"; shift ;;
    --previous=*) previous="${1#--previous=}" ;;
    -h|--help) sed -n '4,46p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "build.sh: unknown build or option $1 (see --help)" >&2; exit 2 ;;
  esac
  shift
done
[ ${#targets[@]} -eq 0 ] && targets=("$host")
if [ -n "$previous" ]; then
  [ -d "$previous" ] || { echo "build.sh: --previous $previous: no such folder" >&2; exit 2; }
  previous="$(cd "$previous" && pwd)"
fi

if [ -t 1 ]; then B=$'\e[1m' G=$'\e[32m' Y=$'\e[33m' R=$'\e[31m' C=$'\e[36m' N=$'\e[0m'; else B="" G="" Y="" R="" C="" N=""; fi
say() { printf '   -> %s\n' "$*"; }
yaml_value() {  # <file> <key>: a top-level scalar, quotes removed
  sed -n "s/^$2:[[:space:]]*//p" "$1" | sed 's/[[:space:]]*#.*$//; s/^"\(.*\)"$/\1/; s/^'"'"'\(.*\)'"'"'$/\1/' | head -1
}
version="$(yaml_value "$launcher/launcher-info.yaml" launcher_version)"
android_abi="${GDUL_ABI:-arm64-v8a}"
case "$android_abi" in arm64-v8a|x86_64) ;; *) echo "build.sh: GDUL_ABI=$android_abi: arm64-v8a or x86_64" >&2; exit 2 ;; esac
mapfile -t game_infos < <(sed -n 's/^[[:space:]]*-[[:space:]]*\([^#[:space:]]*game-info\.yaml\).*/\1/p' "$launcher/games.yaml")

# step <log name> <description> <command...>: runs a step, its output in logs/<log name>.log.
# The step runs in a fresh bash (-euo pipefail) with this script's functions
# and settings: bash ignores `set -e` in a function called in a condition
# (`step ... || return 1`, `if step`) and in everything that function runs,
# subshells and background jobs included, so a step run here could fail half
# way and still "succeed" with the status of its last command (gdul 0.1.1
# shipped a Windows download without gd02's module that way).
step() {
  local name="$1" what="$2"; shift 2
  say "$what (log: gd-engine-launcher/logs/$name.log)"
  local rc=0 env="$logs/.$name.env"
  { declare -f; declare -p repo launcher here host logs out_root version android_abi force previous B G Y R C N; } > "$env"
  bash -c 'set -euo pipefail; shopt -s inherit_errexit; . "$1"; shift; "$@"' step "$env" "$@" > "$logs/$name.log" 2>&1 || rc=$?
  rm -f "$env"
  if [ "$rc" != 0 ]; then
    grep -E 'error|Error|FAILED' "$logs/$name.log" | head -30 || true
    echo "   ... the end of logs/$name.log:"
    tail -15 "$logs/$name.log"
    say "${R}FAILED: $what${N}"
    return 1
  fi
}
warnings_in() { grep -E 'warning:' "$1" | grep -c -v '^ninja: ' || true; }  # compiler warnings (not Ninja's own)
sha256() { if command -v sha256sum >/dev/null; then sha256sum; else shasum -a 256; fi | cut -d' ' -f1; }
arch_of() {
  case "$1" in macos) echo arm64 ;; android) [ "$android_abi" = x86_64 ] && echo x86_64 || echo arm64 ;; *) echo x86_64 ;; esac
}

# The systems each game's module is built for: every one. A ready game whose
# module cannot be built for a system fails that system's build (gdul 0.1.5's
# APK had no gd01 module: "This launcher build has no gd-game-01 module").
# gd01 renders with OpenGL (OpenGL ES 3.0 on Android, as its own APK does;
# Vulkan only for its AI upscalers, which Android does not have).
module_systems_01="linux steamdeck windows macos android"
module_systems_02="linux steamdeck windows macos android"
module_systems_03="linux steamdeck windows macos android"
module_systems_04="linux steamdeck windows macos android"

# module_inputs_hash <NN>: what a module takes from the launcher besides its
# game's sources (the module SDK, modules/gdNN/); a module is reused only when
# it is the same.
module_inputs_hash() {
  # The packaging recipes too (this script, the game's build_common.sh): a
  # module packaged by an older recipe is rebuilt, not reused (gdul 0.1.2's
  # gd-game-02 folder, without libwinpthread-1.dll, must not come back).
  { find "$launcher/sdk" "$launcher/modules/gd$1" "$launcher/scripts" "$repo/gd-game-$1/engine/build_common.sh" -type f 2>/dev/null || true; } |
    LC_ALL=C sort | while IFS= read -r f; do cat "$f"; done | sha256
}

# ---- Android: the SDK, NDK and JDK (as gd-game-02/engine/build_android.sh), SDL3's source ----
android_ready() { [ -d "${ANDROID_HOME:-$HOME/Android/Sdk}/ndk" ]; }
android_env() {
  export ANDROID_HOME="${ANDROID_HOME:-$HOME/Android/Sdk}"
  if [ -z "${JAVA_HOME:-}" ]; then
    for j in "$HOME"/.local/opt/jdk-21* "$HOME"/.local/opt/jdk-17* /usr/lib/jvm/java-21-openjdk /usr/lib/jvm/java-17-openjdk; do
      [ -x "$j/bin/javac" ] && export JAVA_HOME="$j" && break
    done
  fi
  [ -n "${JAVA_HOME:-}" ] && export PATH="$JAVA_HOME/bin:$PATH"
  if ! android_ready || [ -z "${JAVA_HOME:-}" ]; then
    say "${R}Android SDK/NDK ($ANDROID_HOME) or a JDK not found (see --help)${N}"
    return 1
  fi
  local pinned
  pinned="$(sed -n 's/.*ndkVersion = "\(.*\)".*/\1/p' "$launcher/android/app/build.gradle")"
  ANDROID_NDK="$ANDROID_HOME/ndk/$pinned"
  [ -d "$ANDROID_NDK" ] || ANDROID_NDK="$(ls -d "$ANDROID_HOME"/ndk/* | sort -V | tail -1)"
  case "$(uname -s)" in Darwin) NDK_HOST=darwin-x86_64 ;; *) NDK_HOST=linux-x86_64 ;; esac
  # SDL3: the same release as gd02's Android build (its copy when there is one).
  SDL3_VERSION="$(sed -n 's/^SDL3_VERSION=//p' "$repo/gd-game-02/engine/build_android.sh")"
  SDL3_SRC="$repo/gd-game-02/engine/build/android/SDL3-$SDL3_VERSION"
  if [ ! -f "$SDL3_SRC/CMakeLists.txt" ]; then
    SDL3_SRC="$launcher/build/android/SDL3-$SDL3_VERSION"
    if [ ! -f "$SDL3_SRC/CMakeLists.txt" ]; then
      say "Downloading the SDL3 $SDL3_VERSION source (first time only)..."
      mkdir -p "$launcher/build/android"
      # (Explicit checks: android_env runs in a condition, without `set -e`.)
      curl -fsSL "https://github.com/libsdl-org/SDL/releases/download/release-$SDL3_VERSION/SDL3-$SDL3_VERSION.tar.gz" |
        tar xz -C "$launcher/build/android" || { say "${R}SDL3 $SDL3_VERSION: download FAILED${N}"; return 1; }
    fi
    [ -f "$SDL3_SRC/CMakeLists.txt" ] || { say "${R}SDL3 $SDL3_VERSION: no source in $SDL3_SRC${N}"; return 1; }
  fi
  export ANDROID_NDK NDK_HOST SDL3_SRC
}
android_cmake_args() {
  echo "-DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=$android_abi" \
       "-DANDROID_PLATFORM=android-24 -DANDROID_STL=c++_static"
}

# ---- macOS: a module's libraries find FFmpeg next to them and SDL3 in the app's Frameworks ----
macos_fix_module() {  # <module folder>
  local f dep
  for f in "$1"/*.dylib; do
    case "$(basename "$f")" in libav*) install_name_tool -id "@loader_path/$(basename "$f")" "$f" ;; esac
    for dep in $(otool -L "$f" | awk 'NR > 1 {print $1}'); do
      case "$(basename "$dep")" in
        libSDL3*.dylib) install_name_tool -change "$dep" "@executable_path/../Frameworks/$(basename "$dep")" "$f" ;;
        libav*.dylib) install_name_tool -change "$dep" "@loader_path/$(basename "$dep")" "$f" ;;
      esac
    done
  done
}

# ---- the launcher ----
cmake_args() {  # <os>: cross-compile settings
  if [ "$1" = windows ] && [ "$host" != windows ]; then
    echo "-DCMAKE_TOOLCHAIN_FILE=${MINGW_TOOLCHAIN:-/usr/share/mingw/toolchain-mingw64.cmake}"
  fi
  if [ "$1" = android ]; then android_cmake_args; fi
}
build_launcher() {  # <os> <build dir>
  local deck=""
  [ "$1" = steamdeck ] && deck="-DGDUL_STEAM_DECK=ON"
  # shellcheck disable=SC2046
  cmake -S "$launcher" -B "$2" -G Ninja -DCMAKE_BUILD_TYPE=Release $deck $(cmake_args "$1") &&
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
    elif [ "$os" = macos ]; then
      build_ffmpeg build/ffmpeg/macos macOS --disable-x86asm
    elif [ "$os" = android ]; then
      # The same options as build_android.sh (the same build, reused).
      local tc="$ANDROID_NDK/toolchains/llvm/prebuilt/$NDK_HOST/bin" arch=aarch64 triple=aarch64-linux-android
      [ "$android_abi" = x86_64 ] && arch=x86_64 triple=x86_64-linux-android
      build_ffmpeg "build/ffmpeg/android/$android_abi" "Android $android_abi, NDK $(basename "$ANDROID_NDK")" --enable-cross-compile \
        --target-os=android --arch="$arch" --cc="$tc/${triple}24-clang" --cxx="$tc/${triple}24-clang++" \
        --nm="$tc/llvm-nm" --ar="$tc/llvm-ar" --ranlib="$tc/llvm-ranlib" --strip="$tc/llvm-strip" --pkg-config=false \
        --disable-x86asm --extra-ldflags=-Wl,-z,max-page-size=16384
    elif [ "$os" = steamdeck ]; then  # in the sniper container (build_steamdeck.sh), as build_linux_steamdeck.sh
      build_ffmpeg build/ffmpeg/steamdeck "Steam Deck" --disable-x86asm --cc="${CC:-gcc}"
    else
      build_ffmpeg build/ffmpeg/linux Linux --disable-x86asm
    fi
  )
  local ffmpeg_arg="$ffmpeg" sdl_arg="" deck=""
  [ "$os" = steamdeck ] && deck="-DGD02_STEAM_DECK=ON"
  if [ "$os" = android ]; then
    ffmpeg="$engine/build/ffmpeg/android/$android_abi/ffmpeg-prefix"
    ffmpeg_arg="$engine/build/ffmpeg/android"  # gd02's CMakeLists.txt adds <ABI>/ffmpeg-prefix
    sdl_arg="-DGD02_SDL3_SOURCE=$SDL3_SRC"
  fi
  # shellcheck disable=SC2046
  cmake -S "$engine" -B "$work" -G Ninja -DCMAKE_BUILD_TYPE=Release -DGD02_MODULE=ON -DGD02_VIDEO=ON \
    -DGD02_FFMPEG_DIR="$ffmpeg_arg" $sdl_arg $deck $(cmake_args "$os")
  cmake --build "$work" --target gd02_module
  mkdir -p "$dest/licenses"
  case "$os" in
    windows)
      cp "$work/gd-game-02.dll" "$ffmpeg"/bin/av*.dll "$dest/"
      (. "$engine/build_common.sh"; ship_winpthread "$dest" "$dest/licenses") ;;
    linux|steamdeck)
      cp "$work/gd-game-02.so" "$dest/"
      for lib in avformat avcodec avutil; do
        cp -L "$(ls "$ffmpeg/lib/lib$lib.so."[0-9]* | grep -E '\.so\.[0-9]+$' | head -1)" "$dest/"
      done ;;
    macos)
      cp "$work/gd-game-02.dylib" "$dest/"
      for lib in avformat avcodec avutil; do
        cp -L "$(ls "$ffmpeg/lib/lib$lib".[0-9]*.dylib | grep -E "lib$lib\.[0-9]+\.dylib\$" | head -1)" "$dest/"
      done
      macos_fix_module "$dest" ;;
    android)
      # In the APK's native library folder (lib*.so only); native-libs.txt
      # lists them, to find them again in a released APK.
      cp "$work/libgd-game-02.so" "$ffmpeg"/lib/libavformat.so "$ffmpeg"/lib/libavcodec.so "$ffmpeg"/lib/libavutil.so "$dest/"
      printf '%s\n' libgd-game-02.so libavformat.so libavcodec.so libavutil.so > "$dest/native-libs.txt" ;;
  esac
  cp "$engine/third_party/stb_vorbis/LICENSE.txt" "$dest/licenses/stb_vorbis-LICENSE.txt"
  (. "$engine/build_common.sh"; ship_ffmpeg_licence "$ffmpeg" "$dest")
  cp "$engine/README.md" "$dest/README.md"
}

# gd-game-03 (Guitar Hero Encore: Rocks the 80s, PS2): gd02's engine with
# the PS2 decoders; no FFmpeg (its own MPEG-2 / ADPCM decoders).
build_module_03() {
  local os="$1" work="$2" dest="$3" engine="$repo/gd-game-03/engine" sdl_arg="" deck=""
  [ "$os" = steamdeck ] && deck="-DGD03_STEAM_DECK=ON"
  [ "$os" = android ] && sdl_arg="-DGD03_SDL3_SOURCE=$SDL3_SRC"
  # shellcheck disable=SC2046
  cmake -S "$engine" -B "$work" -G Ninja -DCMAKE_BUILD_TYPE=Release -DGD03_MODULE=ON $sdl_arg $deck $(cmake_args "$os")
  cmake --build "$work" --target gd03_module
  mkdir -p "$dest/licenses"
  case "$os" in
    windows)
      cp "$work/gd-game-03.dll" "$dest/"
      (. "$engine/build_common.sh"; ship_winpthread "$dest" "$dest/licenses") ;;
    macos) cp "$work/gd-game-03.dylib" "$dest/" && macos_fix_module "$dest" ;;
    android)
      cp "$work/libgd-game-03.so" "$dest/"
      echo libgd-game-03.so > "$dest/native-libs.txt" ;;
    *) cp "$work/gd-game-03.so" "$dest/" ;;
  esac
  cp "$engine/README.md" "$dest/README.md"
}

# gd-game-04 (Guitar Hero III, Xbox 360): GH3's own layers on gd02's
# engine-independent ones; FFmpeg for the XMA audio and the Bink videos,
# built the way gd-game-02's recipe builds it.
build_module_04() {
  local os="$1" work="$2" dest="$3" engine="$repo/gd-game-04/engine" ffmpeg
  ffmpeg="$engine/build/ffmpeg/$os/ffmpeg-prefix"
  (
    cd "$engine"
    # shellcheck source=/dev/null
    . ./build_common.sh
    if [ "$os" = windows ] && [ "$host" != windows ]; then
      build_ffmpeg build/ffmpeg/windows Windows --disable-x86asm --enable-cross-compile --target-os=mingw32 --arch=x86_64 \
        --cross-prefix=x86_64-w64-mingw32- --pkg-config=false
    elif [ "$os" = windows ] || [ "$os" = macos ]; then
      build_ffmpeg "build/ffmpeg/$os" "$os" --disable-x86asm
    elif [ "$os" = android ]; then
      local tc="$ANDROID_NDK/toolchains/llvm/prebuilt/$NDK_HOST/bin" arch=aarch64 triple=aarch64-linux-android
      [ "$android_abi" = x86_64 ] && arch=x86_64 triple=x86_64-linux-android
      build_ffmpeg "build/ffmpeg/android/$android_abi" "Android $android_abi, NDK $(basename "$ANDROID_NDK")" --enable-cross-compile \
        --target-os=android --arch="$arch" --cc="$tc/${triple}24-clang" --cxx="$tc/${triple}24-clang++" \
        --nm="$tc/llvm-nm" --ar="$tc/llvm-ar" --ranlib="$tc/llvm-ranlib" --strip="$tc/llvm-strip" --pkg-config=false \
        --disable-x86asm --extra-ldflags=-Wl,-z,max-page-size=16384
    elif [ "$os" = steamdeck ]; then
      build_ffmpeg build/ffmpeg/steamdeck "Steam Deck" --disable-x86asm --cc="${CC:-gcc}"
    else
      build_ffmpeg build/ffmpeg/linux Linux --disable-x86asm
    fi
  )
  local ffmpeg_arg="$ffmpeg" sdl_arg="" deck=""
  [ "$os" = steamdeck ] && deck="-DGD04_STEAM_DECK=ON"
  if [ "$os" = android ]; then
    ffmpeg="$engine/build/ffmpeg/android/$android_abi/ffmpeg-prefix"
    ffmpeg_arg="$engine/build/ffmpeg/android"
    sdl_arg="-DGD04_SDL3_SOURCE=$SDL3_SRC"
  fi
  # shellcheck disable=SC2046
  cmake -S "$engine" -B "$work" -G Ninja -DCMAKE_BUILD_TYPE=Release -DGD04_MODULE=ON -DGD04_VIDEO=ON \
    -DGD04_FFMPEG_DIR="$ffmpeg_arg" $sdl_arg $deck $(cmake_args "$os")
  cmake --build "$work" --target gd04_module
  mkdir -p "$dest/licenses"
  case "$os" in
    windows)
      cp "$work/gd-game-04.dll" "$ffmpeg"/bin/av*.dll "$dest/"
      (. "$engine/build_common.sh"; ship_winpthread "$dest" "$dest/licenses") ;;
    linux|steamdeck)
      cp "$work/gd-game-04.so" "$dest/"
      for lib in avformat avcodec avutil; do
        cp -L "$(ls "$ffmpeg/lib/lib$lib.so."[0-9]* | grep -E '\.so\.[0-9]+$' | head -1)" "$dest/"
      done ;;
    macos)
      cp "$work/gd-game-04.dylib" "$dest/"
      for lib in avformat avcodec avutil; do
        cp -L "$(ls "$ffmpeg/lib/lib$lib".[0-9]*.dylib | grep -E "lib$lib\.[0-9]+\.dylib\$" | head -1)" "$dest/"
      done
      macos_fix_module "$dest" ;;
    android)
      cp "$work/libgd-game-04.so" "$ffmpeg"/lib/libavformat.so "$ffmpeg"/lib/libavcodec.so "$ffmpeg"/lib/libavutil.so "$dest/"
      printf '%s\n' libgd-game-04.so libavformat.so libavcodec.so libavutil.so > "$dest/native-libs.txt" ;;
  esac
  (. "$engine/build_common.sh"; ship_ffmpeg_licence "$ffmpeg" "$dest")
  cp "$engine/README.md" "$dest/README.md"
}

build_module_01() {
  local os="$1" work="$2" dest="$3" engine="$repo/gd-game-01/engine" sdl_arg=""
  [ "$os" = android ] && sdl_arg="-DGD01_SDL3_SOURCE=$SDL3_SRC"
  [ "$os" = steamdeck ] && sdl_arg="-DGD01_STEAM_DECK=ON"  # gd01's Steam Deck defaults (its README's table)
  # shellcheck disable=SC2046
  cmake -S "$launcher/modules/gd01" -B "$work" -G Ninja -DCMAKE_BUILD_TYPE=Release $sdl_arg $(cmake_args "$os")
  cmake --build "$work" --target gd01_module
  mkdir -p "$dest/licenses"
  case "$os" in
    windows) cp "$work/gd-game-01.dll" "$dest/" ;;
    macos) cp "$work/gd-game-01.dylib" "$dest/" && macos_fix_module "$dest" ;;
    android)
      # In the APK's native library folder; native-libs.txt lists it (as gd02's).
      cp "$work/libgd-game-01.so" "$dest/"
      echo libgd-game-01.so > "$dest/native-libs.txt" ;;
    *) cp "$work/gd-game-01.so" "$dest/" ;;
  esac
  if [ "$os" != android ]; then  # the AI upscalers (none on Android)
    cp "$engine/third_party/fidelityfx/LICENSE.txt" "$dest/licenses/FidelityFX-SDK-LICENSE.txt"
    cp "$engine/third_party/volk/LICENSE.md" "$dest/licenses/volk-LICENSE.md"
    cp "$engine/third_party/rtmidi/LICENSE" "$dest/licenses/RtMidi-LICENSE.txt"  # Generic MIDI (none on Android)
    cp "$engine/third_party/vulkan/LICENSE.md" "$dest/licenses/Vulkan-Headers-LICENSE.md"
  fi
  if { [ "$os" = linux ] || [ "$os" = steamdeck ]; } && ls "$engine"/third_party/dlss/lib/Linux_x86_64/libnvidia-ngx-dlss.so.* >/dev/null 2>&1; then
    # NGX loads the DLSS model from the program's folder: the launcher's root
    # (copied there when the build is assembled).
    mkdir -p "$dest/root"
    cp "$engine"/third_party/dlss/lib/Linux_x86_64/libnvidia-ngx-dlss.so.* "$dest/root/"
    cp "$engine/third_party/dlss/LICENSE.txt" "$dest/licenses/NVIDIA-RTX-SDK-LICENSE.txt"
  fi
  cp "$repo/gd-game-01/README.md" "$dest/README.md"
}

# unpack_apks <folder>: a released APK in the --previous folder, unpacked
# next to it (<name>.unpacked/), its modules' libraries put back into
# assets/gd-engine-games/<id>/ (native-libs.txt), as in a build folder.
unpack_apks() {
  local apk u d f
  for apk in "$1"/*.apk "$1"/*/*.apk; do
    [ -f "$apk" ] || continue
    u="${apk%.apk}.unpacked"
    [ -d "$u" ] && continue
    python3 -I -m zipfile -e "$apk" "$u"
    for d in "$u"/assets/gd-engine-games/gd-game-*/; do
      [ -f "$d/native-libs.txt" ] || continue
      for abi in arm64-v8a x86_64; do
        [ -d "$u/lib/$abi" ] || continue
        while IFS= read -r f; do cp "$u/lib/$abi/$f" "$d"; done < "$d/native-libs.txt"
      done
    done
  done
}
manifest_os() {  # <manifest>: "<os> <arch>"
  python3 -I -c 'import json,sys; m=json.load(open(sys.argv[1])); print(m.get("os",""), m.get("arch",""))' "$1" 2>/dev/null || true
}
# previous_manifest <os> <arch> <this build's folder>: the manifest of the
# previous build for that system: in the --previous folder, else this
# version's own earlier build, else the newest one in local_builds/.
previous_manifest() {
  local os="$1" arch="$2" this="$3" best="" f
  if [ -n "$previous" ]; then
    unpack_apks "$previous"
    while IFS= read -r f; do
      if [ "$(manifest_os "$f")" = "$os $arch" ]; then echo "$f"; return; fi
    done < <(find "$previous" -path '*gd-engine-games/manifest.json' 2>/dev/null | LC_ALL=C sort)
    return 0
  fi
  while IFS= read -r f; do
    case "$f" in "$this"/*) echo "$f"; return ;; esac
    if [ -z "$best" ] || [ "$f" -nt "$best" ]; then best="$f"; fi
  done < <(find "$out_root" -path "$out_root/gdul-*-$os-$arch/*gd-engine-games/manifest.json" 2>/dev/null)
  echo "$best"
}
manifest_get() {  # <manifest> <game id> <field>
  [ -f "$1" ] || return 0
  python3 -I -c 'import json,sys
m=json.load(open(sys.argv[1])); g=m.get("games",{}).get(sys.argv[2],{}); print(g.get(sys.argv[3],""))' "$1" "$2" "$3"
}

# ---- Android: the APK (Gradle: the launcher as libmain.so, SDL3; the modules and assets from the build) ----
build_apk() {  # <work dir with assets/ and jniLibs/>
  local sign=()
  if [ -n "${GDUL_RELEASE_KEYSTORE:-}" ]; then
    sign=(-PreleaseKeystore="$GDUL_RELEASE_KEYSTORE" -PreleaseKeystorePassword="$GDUL_RELEASE_KEYSTORE_PASSWORD"
          -PreleaseKeyAlias="$GDUL_RELEASE_KEY_ALIAS" -PreleaseKeyPassword="${GDUL_RELEASE_KEY_PASSWORD:-$GDUL_RELEASE_KEYSTORE_PASSWORD}")
  elif [ ! -f "$HOME/.android/debug.keystore" ]; then
    # The debug key (what Android Studio would create).
    mkdir -p "$HOME/.android"
    "$JAVA_HOME/bin/keytool" -genkeypair -keystore "$HOME/.android/debug.keystore" -storepass android -keypass android \
      -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US"
  fi
  (cd "$launcher/android" && ./gradlew --no-daemon --console=plain assembleRelease -Psdl3Dir="$SDL3_SRC" -Pabis="$android_abi" \
    -PmodulesDir="$1/jniLibs" -PassetsDir="$1/assets" "${sign[@]}")
  local bt
  bt="$(ls -d "$ANDROID_HOME"/build-tools/* | sort -V | tail -1)"
  "$bt/apksigner" verify "$launcher/build/android/gradle/outputs/apk/release/app-release.apk"
  if [ ${#sign[@]} -gt 0 ]; then echo "APK signed with the release key"; else echo "APK signed with the Android debug key"; fi
}
android_launcher_recompile() {
  local cache dir cmake_bin
  cache="$(ls -t "$launcher"/build/android/cxx/*/*/"$android_abi"/CMakeCache.txt | head -1)"
  dir="$(dirname "$cache")"
  cmake_bin="$(sed -n 's/^CMAKE_COMMAND:INTERNAL=//p' "$cache")"
  find "$dir/CMakeFiles/gdul.dir" -name '*.o' -delete
  "${cmake_bin:-cmake}" --build "$dir" --target gdul
}

build_os() {
  local os="$1" arch
  arch="$(arch_of "$os")"
  local name="gdul-$version-$os-$arch"
  local out="$out_root/$name" work="$launcher/build/$os"
  [ "$os" = android ] && [ "$android_abi" != arm64-v8a ] && work="$launcher/build/android-$android_abi"
  local exe="Guitar Dream Universal Launcher" modpre="" modext=".so"
  case "$os" in windows) modext=".dll" ;; macos) modext=".dylib" ;; android) modpre="lib" ;; esac
  echo "${B}${C}=== $name ===${N}"
  if [ "$os" = android ]; then android_env || return 1; fi
  local prev
  prev="$(previous_manifest "$os" "$arch" "$out")"
  [ -n "$prev" ] && say "Previous build: ${prev#"$launcher/"}"
  local stage="$out_root/.$name.new"
  rm -rf "$stage" && mkdir -p "$stage/licenses" "$stage/manual" "$stage/gd-engine-games"
  [ "$os" = android ] || mkdir -p "$stage/original_isos"

  # The launcher (on Android it is compiled with the APK, at the end).
  if [ "$os" != android ]; then
    step "$os-launcher" "Launcher: compiling" build_launcher "$os" "$work/launcher"
    local w
    w="$(warnings_in "$logs/$os-launcher.log")"
    if [ "$w" != 0 ]; then say "${R}Launcher: $w compiler warning(s) (see logs/$os-launcher.log)${N}"; return 1; fi
    say "${G}Launcher: compiled, 0 warnings${N}"
    case "$os" in
      windows) cp "$work/launcher/$exe.exe" "$stage/" ;;
      macos) cp -R "$work/launcher/$exe.app" "$stage/" ;;
      *) cp "$work/launcher/$exe" "$stage/" ;;
    esac
  fi

  # Games: every game-info.yaml (the launcher lists them all), and a module
  # for each playable one, reused from the previous build when its version
  # is the same.
  cp "$launcher/games.yaml" "$launcher/launcher-info.yaml" "$stage/gd-engine-games/"
  local entries=() gi id ver ready nn mod systems ihash
  for gi in "${game_infos[@]}"; do
    id="$(yaml_value "$repo/$gi" id)"
    ver="$(yaml_value "$repo/$gi" guitar_dream_version)"
    ready="$(yaml_value "$repo/$gi" ready_to_play)"
    mkdir -p "$stage/gd-engine-games/$(dirname "$gi")"
    cp "$repo/$gi" "$stage/gd-engine-games/$gi"
    case "$ready" in true|True|yes) ;; *) continue ;; esac
    nn="${id##*-}"
    mod="$modpre$id$modext"
    # Every ready game, on every system: never a download without one.
    if ! declare -F "build_module_$nn" >/dev/null; then
      say "${R}$id: ready_to_play but the launcher has no module recipe (build_module_$nn): every build must carry it${N}"
      return 1
    fi
    systems="module_systems_$nn"
    case " ${!systems:-} " in
      *" $os "*) ;;
      *) say "${R}$id: ready_to_play but no $os module (module_systems_$nn): every build must carry it${N}"; return 1 ;;
    esac
    ihash="$(module_inputs_hash "$nn")"
    local dest="$stage/gd-engine-games/$id" prev_dir="" prev_ver="" prev_hash="" built_at reused=""
    if [ -n "$prev" ]; then
      prev_dir="$(dirname "$prev")/$id"
      prev_ver="$(manifest_get "$prev" "$id" guitar_dream_version)"
      prev_hash="$(manifest_get "$prev" "$id" inputs_hash)"
    fi
    if [ "$force" = 0 ] && [ -n "$prev_ver" ] && [ "$prev_ver" = "$ver" ] && [ "$prev_hash" = "$ihash" ] && [ -f "$prev_dir/$mod" ]; then
      say "${G}$id $ver: same version and module SDK as the previous build, module reused${N}"
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
      [ -n "$prev_ver" ] && [ "$prev_ver" = "$ver" ] && [ "$prev_hash" != "$ihash" ] && say "$id: the launcher's module SDK changed"
      step "$os-$id" "$id $ver: building the module" "build_module_$nn" "$os" "$work/$id" "$dest" || return 1
      built_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
      reused="false"
      say "${G}$id $ver: module built ($(warnings_in "$logs/$os-$id.log") compiler warnings)${N}"
    fi
    entries+=("$id|$ver|$mod|$built_at|$reused|$ihash")
  done

  # Root: platform libraries, licences, manual, README.
  case "$os" in
    windows)
      cp "${MINGW_SDL3_DLL:-/usr/x86_64-w64-mingw32/sys-root/mingw/bin/SDL3.dll}" "$stage/"
      cp "$launcher/res/gdul.ico" "$stage/$exe.ico"
      cp /usr/share/licenses/mingw64-SDL3/LICENSE.txt "$stage/licenses/SDL3-LICENSE.txt" 2>/dev/null ||
        cp /usr/share/licenses/SDL3/LICENSE.txt "$stage/licenses/SDL3-LICENSE.txt" ;;
    linux|steamdeck)
      local sdl
      sdl="${GDUL_SDL3_LIB:-$(ldd "$stage/$exe" | awk '/libSDL3\.so/ {print $3}')}"
      cp -L "$sdl" "$stage/libSDL3.so.0"
      cp "$launcher/res/gdul_icon_512.png" "$stage/$exe.png"
      cp "${GDUL_SDL3_LICENSE:-/usr/share/licenses/SDL3/LICENSE.txt}" "$stage/licenses/SDL3-LICENSE.txt" ;;
    macos)  # SDL3 itself goes into the app below
      cp "${GDUL_SDL3_LICENSE:-$(brew --prefix sdl3)/LICENSE.txt}" "$stage/licenses/SDL3-LICENSE.txt" ;;
    android)
      cp "$SDL3_SRC/LICENSE.txt" "$stage/licenses/SDL3-LICENSE.txt" ;;
  esac
  for d in "$stage"/gd-engine-games/gd-game-*/; do
    [ -d "$d/licenses" ] && for f in "$d"licenses/*; do [ -e "$f" ] || continue; cp "$f" "$stage/licenses/$(basename "$d")-$(basename "$f")"; done
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
  [ "$os" = android ] || cp "$launcher/package/original_isos-README.md" "$stage/original_isos/README.md"
  [ "$os" = steamdeck ] && cp "$launcher/package/STEAM-DECK.txt" "$stage/STEAM-DECK.txt"
  cp "$launcher/manual/"*.md "$stage/manual/"

  # The manifest: what each game module was built from.
  local commit
  commit="$(git -C "$repo" rev-parse --short HEAD 2>/dev/null || echo unknown)"
  python3 -I - "$stage/gd-engine-games/manifest.json" "$version" "$os" "$arch" "$commit" "${entries[@]}" <<'PY'
import json, sys, datetime
path, version, os_name, arch, commit, *entries = sys.argv[1:]
games = {}
for e in entries:
    gid, ver, mod, built, reused, inputs = e.split("|")
    games[gid] = {"guitar_dream_version": ver, "module": mod, "built_at": built, "reused": reused == "true",
                  "inputs_hash": inputs}
json.dump({"launcher_version": version, "os": os_name, "arch": arch,
           "assembled_at": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
           "source_commit": commit, "games": games}, open(path, "w"), indent=2)
open(path, "a").write("\n")
PY

  if [ "$os" = macos ]; then
    # The app: the games inside (SDL_GetBasePath is Contents/Resources/),
    # SDL3 in Contents/Frameworks, everything ad-hoc signed.
    local app="$stage/$exe.app"
    mv "$stage/gd-engine-games" "$app/Contents/Resources/"
    dylibbundler -od -b -x "$app/Contents/MacOS/$exe" -d "$app/Contents/Frameworks/" -p @executable_path/../Frameworks/ >/dev/null
    find "$app/Contents/Resources/gd-engine-games" -name '*.dylib' -exec codesign --force --sign - {} \;
    codesign --force --deep --sign - "$app"
    say "${G}$exe.app: assembled and signed (ad hoc)${N}"
  elif [ "$os" = android ]; then
    # The APK: the assets (gd-engine-games/ without libraries, licences,
    # manual) and the modules' libraries, then Gradle.
    rm -rf "$work/assets" "$work/jniLibs" && mkdir -p "$work/assets" "$work/jniLibs/$android_abi"
    cp -R "$stage/gd-engine-games" "$stage/licenses" "$stage/manual" "$stage/LICENSE" "$stage/LICENSE-ADDITIONAL-TERMS.md" "$work/assets/"
    find "$work/assets/gd-engine-games" -name '*.so' -delete
    for d in "$stage"/gd-engine-games/gd-game-*/; do
      [ -f "$d/native-libs.txt" ] || continue
      while IFS= read -r f; do cp "$d$f" "$work/jniLibs/$android_abi/"; done < "$d/native-libs.txt"
    done
    step android-apk "Launcher: compiling and packing the APK (Gradle)" build_apk "$work" || return 1
    say "$(tail -1 "$logs/android-apk.log")"
    # Gradle shows no compiler output: the launcher's files compiled again
    # in its build folder, for the warnings.
    step android-launcher "Launcher: checking for compiler warnings" android_launcher_recompile || return 1
    local w
    w="$(warnings_in "$logs/android-launcher.log")"
    if [ "$w" != 0 ]; then say "${R}Launcher: $w compiler warning(s) (see logs/android-launcher.log)${N}"; return 1; fi
    say "${G}Launcher: compiled, 0 warnings${N}"
    cp "$launcher/build/android/gradle/outputs/apk/release/app-release.apk" "$stage/$name.apk"
  fi
  # Every playable game's module and its libraries really are in it.
  "$here/verify_bundle.sh" "$stage" "$os"
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
in_steamrt() { grep -qs 'Steam Runtime' /etc/os-release; }
container_runner() { if command -v podman >/dev/null; then echo podman; elif command -v docker >/dev/null; then echo docker; fi; }
# The Steam Deck build, from a Linux host: build_steamdeck.sh in the sniper
# SDK container (the repository mounted at /src, the --previous folder at /previous).
build_steamdeck_in_container() {
  local runner image=registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest args=() vols=(-v "$repo":/src:Z)
  runner="$(container_runner)"
  [ "$force" = 1 ] && args+=(--force)
  if [ -n "$previous" ]; then vols+=(-v "$previous":/previous:ro,Z); args+=(--previous /previous); fi
  echo "${B}${C}=== gdul-$version-steamdeck-x86_64 (in Valve's Steam Runtime 3 'sniper' SDK, $runner) ===${N}"
  "$runner" run --rm "${vols[@]}" -w /src "$image" gd-engine-launcher/scripts/build_steamdeck.sh "${args[@]}"
}
can_build() {
  case "$1" in
    windows) return 0 ;;
    steamdeck) [ "$host" = linux ] && { in_steamrt || [ -n "$(container_runner)" ]; } ;;
    android) { [ "$host" = linux ] || [ "$host" = macos ]; } && android_ready ;;
    *) [ "$host" = "$1" ] ;;
  esac
}
for t in "${targets[@]}"; do
  if [ "$t" = steamdeck ] && [ "$host" = linux ] && ! in_steamrt && can_build "$t"; then
    # (Its log, logs/build-steamdeck.log, is written inside the container.)
    { set +e; build_steamdeck_in_container; rc=$?; set -e; }
    [ "$rc" = 0 ] || failed=$((failed + 1))
  elif can_build "$t"; then
    { set +e; ( set -e; build_os "$t" ) 2>&1 | tee "$logs/build-$t.log"; rc=${PIPESTATUS[0]}; set -e; }
    [ "$rc" = 0 ] || failed=$((failed + 1))
  else
    say "${Y}Skipped $t: it cannot be built on $host$([ "$t" = android ] && echo " without the Android SDK (see --help)")$([ "$t" = steamdeck ] && echo " without podman or docker")${N}"
  fi
  echo
done
[ "$failed" = 0 ]
