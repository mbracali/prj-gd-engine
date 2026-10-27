#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# The Linux launcher download: scripts/build.sh linux on Ubuntu 22.04
# (glibc 2.35), so it runs on any distribution from 2022 on, with SDL3 built
# from source and shipped next to the program (RPATH $ORIGIN), and the C++
# runtime linked in. Run as root inside an ubuntu:22.04 container (the gdul
# release workflow does), like gd-game-02/engine/build_linux_portable.sh.
# Arguments go to build.sh (e.g. --previous <folder>).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
launcher="$(cd "$here/.." && pwd)"
SDL3_VERSION="$(sed -n 's/^SDL3_VERSION=//p' "$launcher/../gd-game-02/engine/build_linux_portable.sh")"
say() { printf '   -> %s\n' "$*"; }

export DEBIAN_FRONTEND=noninteractive
say "Installing the build tools in the container (apt)..."
apt-get update -qq >/dev/null
apt-get install -y -qq --no-install-recommends gcc g++-12 make ninja-build python3 python3-pip curl ca-certificates \
  pkg-config file xz-utils git \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev \
  libwayland-dev wayland-protocols libegl1-mesa-dev libgl1-mesa-dev libgles2-mesa-dev libdrm-dev libgbm-dev \
  libasound2-dev libpulse-dev libpipewire-0.3-dev libudev-dev libdbus-1-dev libibus-1.0-dev libdecor-0-dev >/dev/null 2>&1 ||
  { say "apt-get install failed"; exit 1; }
pip3 install -q "cmake>=3.24,<4" 2>&1 | grep -v "Running pip as the .root. user" || true
export CC=gcc-12 CXX=g++-12

prefix="$launcher/build/portable/sdl3-prefix"
if [ -f "$prefix/lib/libSDL3.so.0" ]; then
  say "SDL3 $SDL3_VERSION: already built (reused)"
else
  say "SDL3 $SDL3_VERSION: building from source (a few minutes)..."
  mkdir -p "$launcher/build/portable"
  curl -fsSL "https://github.com/libsdl-org/SDL/releases/download/release-$SDL3_VERSION/SDL3-$SDL3_VERSION.tar.gz" |
    tar xz -C "$launcher/build/portable"
  src="$launcher/build/portable/SDL3-$SDL3_VERSION"
  cmake -S "$src" -B "$launcher/build/portable/sdl3" -G Ninja -DCMAKE_BUILD_TYPE=Release -Wno-dev \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
    -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_INSTALL_LIBDIR=lib >/dev/null
  cmake --build "$launcher/build/portable/sdl3" >/dev/null
  cmake --install "$launcher/build/portable/sdl3" >/dev/null
  cp "$src/LICENSE.txt" "$prefix/LICENSE.txt"
fi

# build.sh finds this SDL3 (CMake), ships it (GDUL_SDL3_LIB) and its licence.
export CMAKE_PREFIX_PATH="$prefix" GDUL_SDL3_LIB="$prefix/lib/libSDL3.so.0" GDUL_SDL3_LICENSE="$prefix/LICENSE.txt"
export LDFLAGS="-static-libstdc++ -static-libgcc"
git config --global --add safe.directory "$(cd "$launcher/.." && pwd)"
exec "$here/build.sh" linux "$@"
