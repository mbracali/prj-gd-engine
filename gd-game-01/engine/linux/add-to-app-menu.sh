#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# Adds gd01 to your desktop's application menu, with its icon, pointing at
# this folder (run it again if you move the folder). Nothing else is installed.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
apps="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
mkdir -p "$apps"
cat > "$apps/io.github.mbracali.gd01.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=GD Engine 01
GenericName=Guitar Hero (PS2) on Guitar Dream Engine
Comment=Play Guitar Hero (PS2) from your own disc image
Exec="$here/gd01"
Path=$here
Icon=$here/gd01.png
Terminal=false
Categories=Game;
StartupWMClass=io.github.mbracali.gd01
DESKTOP
command -v update-desktop-database >/dev/null && update-desktop-database "$apps" 2>/dev/null || true
echo "Added \"GD Engine 01\" to your application menu ($apps/io.github.mbracali.gd01.desktop)."
