#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# The Steam Deck download (dist/gd03-steamdeck/): built in Valve's Steam
# Runtime 3 "sniper" SDK (registry.gitlab.steamos.cloud/steamrt/sniper/sdk), the
# environment Steam runs games in, so it runs from Steam (Game Mode, as a
# non-Steam game) and from the desktop alike. SDL3 and the C++ runtime travel
# with it; the handheld settings are its defaults (-DGD03_STEAM_DECK=ON,
# as gd01's; gd03 has no settings of its own yet).
# Run inside that container (local_build.sh and the release workflow do).
set -euo pipefail
cd "$(dirname "$0")"
. ./build_common.sh

say "gd03 (Steam Deck): configuring (CMake)..."
configure -S . -B build/steamdeck -G Ninja -DCMAKE_BUILD_TYPE=Release -DGD03_STEAM_DECK=ON \
  -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 \
  -DCMAKE_BUILD_RPATH='$ORIGIN' -DCMAKE_INSTALL_RPATH='$ORIGIN' -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
  -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"
compile build/steamdeck "gd03 (Steam Deck)"

say "Assembling dist/gd03-steamdeck (program, SDL3, icon, licences, STEAM-DECK.txt)..."
d=dist/gd03-steamdeck
rm -rf "$d" && mkdir -p "$d/licenses"
cp build/steamdeck/gd03 README.md ../../LICENSE ../../LICENSE-ADDITIONAL-TERMS.md "$d/"
strip "$d/gd03"
cp res/gd03_icon_512.png "$d/gd03.png"
sdl="$(ldd build/steamdeck/gd03 | awk '/libSDL3\.so/ {print $3}')"
cp -L "$sdl" "$d/libSDL3.so.0"
cp /usr/share/doc/libsdl3-0/copyright "$d/licenses/SDL3-LICENSE.txt" 2>/dev/null || true
cp third_party/rtmidi/LICENSE "$d/licenses/RtMidi-LICENSE.txt"
cat > "$d/STEAM-DECK.txt" <<'TXT'
gd03 on the Steam Deck

1. In Desktop Mode, extract this folder somewhere in your home folder (for
   example ~/Games/gd03-steamdeck), and copy your Guitar Hero II (Xbox 360)
   .iso INTO it, next to the file "gd03". The game finds it there by itself.
2. Steam > Games > Add a Non-Steam Game to My Library > Browse > choose the
   file "gd03" in this folder > Add Selected Programs.
3. Back in Game Mode, start gd03 from your library and play.
   (Without an .iso in the folder: tap the screen to pick one.)

Icon: in Steam, gd03's Properties > the icon square > choose gd03.png from
this folder. Started once in Desktop Mode, gd03 also adds itself, with its
icon, to the desktop's application menu.
TXT
say "Ready: $d"
