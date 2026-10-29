#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# The Windows download (dist/gd03-windows-x64/: gd03.exe + SDL3.dll and its
# MinGW runtime DLL), built with MinGW-w64. Two ways, same result:
#   - cross-compiled on Linux (Fedora: dnf install mingw64-gcc-c++ mingw64-SDL3
#     cmake ninja-build make); this is what the release workflow does;
#   - on Windows in an MSYS2 UCRT64 shell (pacman -S mingw-w64-ucrt-x86_64-gcc
#     mingw-w64-ucrt-x86_64-SDL3 mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja make).
set -euo pipefail
cd "$(dirname "$0")"
. ./build_common.sh

native=0
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) native=1 ;; esac
say "gd03 (Windows): configuring (CMake, MinGW-w64)..."
if [ "$native" = 1 ]; then
  configure -S . -B build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release
  sdl_dll="${MINGW_PREFIX:-/ucrt64}/bin/SDL3.dll"
else
  toolchain="${MINGW_TOOLCHAIN:-/usr/share/mingw/toolchain-mingw64.cmake}"
  configure -S . -B build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$toolchain"
  sdl_dll="${MINGW_SDL3_DLL:-/usr/x86_64-w64-mingw32/sys-root/mingw/bin/SDL3.dll}"
fi
compile build/windows "gd03 (Windows)"

d=dist/gd03-windows-x64
rm -rf "$d" && mkdir -p "$d/licenses"
cp build/windows/gd03.exe "$sdl_dll" README.md ../../LICENSE ../../LICENSE-ADDITIONAL-TERMS.md "$d/"
ship_winpthread "$d" "$d/licenses"
say "Ready: $d"
