#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# The Steam Deck download (dist/gd04-steamdeck/): built in Valve's Steam
# Runtime 3 "sniper" SDK (registry.gitlab.steamos.cloud/steamrt/sniper/sdk), the
# environment Steam runs games in, so it runs from Steam (Game Mode, as a
# non-Steam game) and from the desktop alike. SDL3 and the C++ runtime travel
# with it, and FFmpeg's libraries for the videos (build_common.sh's
# build_ffmpeg); the handheld settings are its defaults (-DGD04_STEAM_DECK=ON,
# as gd01's; gd04 has no settings of its own yet).
# Run inside that container (local_build.sh and the release workflow do).
set -euo pipefail
cd "$(dirname "$0")"
. ./build_common.sh

build_ffmpeg build/ffmpeg/steamdeck "Steam Deck" --disable-x86asm --cc=gcc-14

say "gd04 (Steam Deck): configuring (CMake)..."
configure -S . -B build/steamdeck -G Ninja -DCMAKE_BUILD_TYPE=Release -DGD04_STEAM_DECK=ON \
  -DGD04_FFMPEG_DIR="$PWD/build/ffmpeg/steamdeck/ffmpeg-prefix" \
  -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 \
  -DCMAKE_BUILD_RPATH='$ORIGIN' -DCMAKE_INSTALL_RPATH='$ORIGIN' -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
  -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"
compile build/steamdeck "gd04 (Steam Deck)"

say "Assembling dist/gd04-steamdeck (program, SDL3, FFmpeg, icon, licences, STEAM-DECK.txt)..."
d=dist/gd04-steamdeck
rm -rf "$d" && mkdir -p "$d/licenses"
cp build/steamdeck/gd04 README.md ../../LICENSE ../../LICENSE-ADDITIONAL-TERMS.md "$d/"
strip "$d/gd04"
cp res/gd04_icon_512.png "$d/gd04.png"
sdl="$(ldd build/steamdeck/gd04 | awk '/libSDL3\.so/ {print $3}')"
cp -L "$sdl" "$d/libSDL3.so.0"
cp /usr/share/doc/libsdl3-0/copyright "$d/licenses/SDL3-LICENSE.txt" 2>/dev/null || true
cp build/steamdeck/libav*.so.[0-9]* "$d/"
ship_ffmpeg_licence build/ffmpeg/steamdeck/ffmpeg-prefix "$d"
cat > "$d/STEAM-DECK.txt" <<'TXT'
gd04 on the Steam Deck

1. In Desktop Mode, extract this folder somewhere in your home folder (for
   example ~/Games/gd04-steamdeck), and copy your Guitar Hero III: Legends of Rock (Xbox 360)
   .iso INTO it, next to the file "gd04". The game finds it there by itself.
2. Steam > Games > Add a Non-Steam Game to My Library > Browse > choose the
   file "gd04" in this folder > Add Selected Programs.
3. Back in Game Mode, start gd04 from your library and play.
   (Without an .iso in the folder: tap the screen to pick one.)

Icon: in Steam, gd04's Properties > the icon square > choose gd04.png from
this folder. Started once in Desktop Mode, gd04 also adds itself, with its
icon, to the desktop's application menu.
TXT
say "Ready: $d"
