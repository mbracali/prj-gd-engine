#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# The Linux download: built on Ubuntu 22.04 (glibc 2.35) so it runs on any
# distribution from 2022 on, with SDL3 built from source and shipped next to
# the program (found through RPATH $ORIGIN), so nothing has to be installed.
# Run as root inside an ubuntu:22.04 container (the release workflow does);
# the result is dist/gd01-linux-x86_64/.
set -euo pipefail
cd "$(dirname "$0")"
. ./build_common.sh
SDL3_VERSION=3.4.18

export DEBIAN_FRONTEND=noninteractive
say "Installing the build tools in the container (apt; one to a few minutes, depending on the mirror)..."
apt-get update -qq >/dev/null
apt-get install -y -qq --no-install-recommends g++-12 ninja-build python3-pip curl ca-certificates pkg-config file \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev \
  libwayland-dev wayland-protocols libegl1-mesa-dev libgl1-mesa-dev libgles2-mesa-dev libdrm-dev libgbm-dev \
  libasound2-dev libpulse-dev libpipewire-0.3-dev libudev-dev libdbus-1-dev libibus-1.0-dev libdecor-0-dev >/dev/null 2>&1 || { say "apt-get install failed"; exit 1; }
pip3 install -q "cmake>=3.24,<4" 2>&1 | grep -v "Running pip as the .root. user" || true
export CC=gcc-12 CXX=g++-12

mkdir -p build/portable
if [ -f build/portable/sdl3-prefix/lib/libSDL3.so.0 ]; then
  say "SDL3 $SDL3_VERSION: already built (reused)"
else
  say "SDL3 $SDL3_VERSION: building from source (first time only, a few minutes)..."
  curl -fsSL "https://github.com/libsdl-org/SDL/releases/download/release-$SDL3_VERSION/SDL3-$SDL3_VERSION.tar.gz" | tar xz -C build/portable
  configure -S "build/portable/SDL3-$SDL3_VERSION" -B build/portable/sdl3 -G Ninja -DCMAKE_BUILD_TYPE=Release -Wno-dev \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
    -DCMAKE_INSTALL_PREFIX="$PWD/build/portable/sdl3-prefix" -DCMAKE_INSTALL_LIBDIR=lib
  cmake --build build/portable/sdl3 >/dev/null
  cmake --install build/portable/sdl3 >/dev/null
  cp "build/portable/SDL3-$SDL3_VERSION/LICENSE.txt" build/portable/sdl3-prefix/LICENSE.txt
fi

say "gd01: configuring (CMake)..."
configure -S . -B build/portable/gd01 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSDL3_DIR="$PWD/build/portable/sdl3-prefix/lib/cmake/SDL3" \
  -DCMAKE_BUILD_RPATH='$ORIGIN' -DCMAKE_INSTALL_RPATH='$ORIGIN' -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
  -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"
compile build/portable/gd01 gd01

say "Assembling dist/gd01-linux-x86_64 (program, SDL3, icon, licences)..."
d=dist/gd01-linux-x86_64
rm -rf "$d" && mkdir -p "$d/licenses"
cp build/portable/gd01/gd01 README.md ../../LICENSE ../../LICENSE-ADDITIONAL-TERMS.md "$d/"
strip "$d/gd01"
cp res/gd01_icon_512.png "$d/gd01.png"
cp -L build/portable/sdl3-prefix/lib/libSDL3.so.0 "$d/"
cp build/portable/sdl3-prefix/LICENSE.txt "$d/licenses/SDL3-LICENSE.txt"
cp third_party/fidelityfx/LICENSE.txt "$d/licenses/FidelityFX-SDK-LICENSE.txt"
cp third_party/volk/LICENSE.md "$d/licenses/volk-LICENSE.md"
cp third_party/vulkan/LICENSE.md "$d/licenses/Vulkan-Headers-LICENSE.md"
if ls third_party/dlss/lib/Linux_x86_64/libnvidia-ngx-dlss.so.* >/dev/null 2>&1; then
  cp third_party/dlss/lib/Linux_x86_64/libnvidia-ngx-dlss.so.* "$d/"
  cp third_party/dlss/LICENSE.txt "$d/licenses/NVIDIA-RTX-SDK-LICENSE.txt"
fi
say "Ready: $d"
