#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# The Windows download (dist/gd01-windows-x64/: gd01.exe + SDL3.dll), built
# with MinGW-w64. Two ways, same result:
#   - cross-compiled on Linux (Fedora: dnf install mingw64-gcc-c++ mingw64-SDL3
#     cmake ninja-build); this is what the release workflow does;
#   - on Windows in an MSYS2 UCRT64 shell (pacman -S mingw-w64-ucrt-x86_64-gcc
#     mingw-w64-ucrt-x86_64-SDL3 mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja).
set -euo pipefail
cd "$(dirname "$0")"
. ./build_common.sh

say "gd01 (Windows): configuring (CMake, MinGW-w64)..."
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    configure -S . -B build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release
    sdl_dll="${MINGW_PREFIX:-/ucrt64}/bin/SDL3.dll" ;;
  *)
    toolchain="${MINGW_TOOLCHAIN:-/usr/share/mingw/toolchain-mingw64.cmake}"
    configure -S . -B build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$toolchain"
    sdl_dll="${MINGW_SDL3_DLL:-/usr/x86_64-w64-mingw32/sys-root/mingw/bin/SDL3.dll}" ;;
esac
compile build/windows "gd01 (Windows)"

d=dist/gd01-windows-x64
rm -rf "$d" && mkdir -p "$d/licenses"
cp build/windows/gd01.exe "$sdl_dll" README.md ../../LICENSE ../../LICENSE-ADDITIONAL-TERMS.md "$d/"
cp third_party/fidelityfx/LICENSE.txt "$d/licenses/FidelityFX-SDK-LICENSE.txt"
cp third_party/volk/LICENSE.md "$d/licenses/volk-LICENSE.md"
cp third_party/vulkan/LICENSE.md "$d/licenses/Vulkan-Headers-LICENSE.md"
say "Ready: $d"
