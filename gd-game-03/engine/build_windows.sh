#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# The Windows download (dist/gd03-windows-x64/: gd03.exe + SDL3.dll + the
# FFmpeg DLLs for the videos), built with MinGW-w64. Two ways, same result:
#   - cross-compiled on Linux (Fedora: dnf install mingw64-gcc-c++ mingw64-SDL3
#     cmake ninja-build make); this is what the release workflow does;
#   - on Windows in an MSYS2 UCRT64 shell (pacman -S mingw-w64-ucrt-x86_64-gcc
#     mingw-w64-ucrt-x86_64-SDL3 mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja make).
# GD03_VIDEO=0 builds without FFmpeg (the videos are then skipped).
set -euo pipefail
cd "$(dirname "$0")"
. ./build_common.sh

native=0
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) native=1 ;; esac
video=(-DGD03_VIDEO=OFF)
ffmpeg=build/ffmpeg/windows/ffmpeg-prefix
if [ "${GD03_VIDEO:-1}" != 0 ]; then
  if [ "$native" = 1 ]; then
    build_ffmpeg build/ffmpeg/windows Windows --disable-x86asm
  else
    build_ffmpeg build/ffmpeg/windows Windows --disable-x86asm --enable-cross-compile --target-os=mingw32 --arch=x86_64 \
      --cross-prefix=x86_64-w64-mingw32- --pkg-config=false
  fi
  video=(-DGD03_VIDEO=ON -DGD03_FFMPEG_DIR="$PWD/$ffmpeg")
fi
say "gd03 (Windows): configuring (CMake, MinGW-w64)..."
if [ "$native" = 1 ]; then
  configure -S . -B build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release "${video[@]}"
  sdl_dll="${MINGW_PREFIX:-/ucrt64}/bin/SDL3.dll"
else
  toolchain="${MINGW_TOOLCHAIN:-/usr/share/mingw/toolchain-mingw64.cmake}"
  configure -S . -B build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$toolchain" "${video[@]}"
  sdl_dll="${MINGW_SDL3_DLL:-/usr/x86_64-w64-mingw32/sys-root/mingw/bin/SDL3.dll}"
fi
compile build/windows "gd03 (Windows)"

d=dist/gd03-windows-x64
rm -rf "$d" && mkdir -p "$d/licenses"
cp build/windows/gd03.exe "$sdl_dll" README.md ../../LICENSE ../../LICENSE-ADDITIONAL-TERMS.md "$d/"
cp third_party/stb_vorbis/LICENSE.txt "$d/licenses/stb_vorbis-LICENSE.txt"
if [ "${GD03_VIDEO:-1}" != 0 ]; then
  cp "$ffmpeg"/bin/av*.dll "$d/"
  ship_winpthread "$d" "$d/licenses"
  ship_ffmpeg_licence "$ffmpeg" "$d"
fi
say "Ready: $d"
