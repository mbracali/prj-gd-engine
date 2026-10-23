#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# The Steam Deck download (dist/gd01-steamdeck/): built in Valve's Steam
# Runtime 3 "sniper" SDK (registry.gitlab.steamos.cloud/steamrt/sniper/sdk), the
# environment Steam runs games in, so it runs from Steam (Game Mode, as a
# non-Steam game) and from the desktop alike. SDL3 and the C++ runtime travel
# with it; the handheld settings (60 FPS, 800p, full screen, lighter effects)
# are its defaults (-DGD01_STEAM_DECK=ON).
# Run inside that container (local_build.sh and the release workflow do).
set -euo pipefail
cd "$(dirname "$0")"
. ./build_common.sh

say "gd01 (Steam Deck): configuring (CMake)..."
configure -S . -B build/steamdeck -G Ninja -DCMAKE_BUILD_TYPE=Release -DGD01_STEAM_DECK=ON \
  -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 \
  -DCMAKE_BUILD_RPATH='$ORIGIN' -DCMAKE_INSTALL_RPATH='$ORIGIN' -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
  -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"
compile build/steamdeck "gd01 (Steam Deck)"

say "Assembling dist/gd01-steamdeck (program, SDL3, icon, licences, STEAM-DECK.txt)..."
d=dist/gd01-steamdeck
rm -rf "$d" && mkdir -p "$d/licenses"
cp build/steamdeck/gd01 README.md ../../LICENSE ../../LICENSE-ADDITIONAL-TERMS.md "$d/"
strip "$d/gd01"
cp res/gd01_icon_512.png "$d/gd01.png"
sdl="$(ldd build/steamdeck/gd01 | awk '/libSDL3\.so/ {print $3}')"
cp -L "$sdl" "$d/libSDL3.so.0"
cp /usr/share/doc/libsdl3-0/copyright "$d/licenses/SDL3-LICENSE.txt" 2>/dev/null || true
cp third_party/fidelityfx/LICENSE.txt "$d/licenses/FidelityFX-SDK-LICENSE.txt"
cp third_party/volk/LICENSE.md "$d/licenses/volk-LICENSE.md"
cp third_party/vulkan/LICENSE.md "$d/licenses/Vulkan-Headers-LICENSE.md"
cat > "$d/STEAM-DECK.txt" <<'TXT'
gd01 on the Steam Deck

1. In Desktop Mode, extract this folder somewhere in your home folder (for
   example ~/Games/gd01-steamdeck), and copy your Guitar Hero (PS2) .iso
   INTO it, next to the file "gd01". The game finds it there by itself.
2. Steam > Games > Add a Non-Steam Game to My Library > Browse > choose the
   file "gd01" in this folder > Add Selected Programs.
3. Back in Game Mode, start gd01 from your library and play.
   (Without an .iso in the folder: tap the screen or press A to pick one.)

Icon: in Steam, gd01's Properties > the icon square > choose gd01.png from
this folder. Started once in Desktop Mode, gd01 also adds itself, with its
icon, to the desktop's application menu.

The settings are already tuned for the Deck (60 FPS, 1280 x 800, full
screen). Change them in Options > Enhanced Settings; "Apply Steam Deck
Settings" puts them back.
TXT
say "Ready: $d"
