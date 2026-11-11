#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# Packs a Linux launcher build folder (gdul-<v>-linux-x86_64, from build.sh
# linux or build_linux_portable.sh) into gdul-<v>-x86_64.AppImage and its
# .zsync, the same files as the .tar.gz in one program (GitHub issue #83).
# The AppImage carries its update information (the GitHub releases' newest
# gdul-*-x86_64.AppImage.zsync), so AppImageUpdate and AppManager update it.
# Inside it the launcher finds discs next to the .AppImage file
# ($APPIMAGE), saves in the usual XDG folder and loads the game modules
# from the mount. verify_bundle.sh checks the result.
#
# Usage: gd-engine-launcher/scripts/build_appimage.sh <linux build folder> <output folder>
#
# appimagetool and the AppImage runtime: pinned versions, downloaded once
# (SHA-256 checked) into $GDUL_TOOLS_CACHE (default ~/.cache/gd-engine/appimage).
# They run with APPIMAGE_EXTRACT_AND_RUN=1: no FUSE needed (containers, CI).
set -euo pipefail
shopt -s inherit_errexit
here="$(cd "$(dirname "$0")" && pwd)"
launcher="$(cd "$here/.." && pwd)"
[ $# -eq 2 ] || { sed -n '4,17p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
build="$(cd "$1" && pwd)"
mkdir -p "$2"
out="$(cd "$2" && pwd)"
say() { printf '   -> %s\n' "$*"; }

APPIMAGETOOL_VERSION=1.9.1
APPIMAGETOOL_SHA256=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
RUNTIME_VERSION=20251108
RUNTIME_SHA256=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d
UPDATE_INFO="gh-releases-zsync|mbracali|prj-gd-engine|latest|gdul-*-x86_64.AppImage.zsync"

manifest="$build/gd-engine-games/manifest.json"
[ -f "$manifest" ] || { echo "build_appimage.sh: $build: no gd-engine-games/manifest.json" >&2; exit 2; }
read -r version os arch < <(python3 -I -c 'import json,sys; m=json.load(open(sys.argv[1])); print(m["launcher_version"], m["os"], m["arch"])' "$manifest")
[ "$os $arch" = "linux x86_64" ] || { echo "build_appimage.sh: $build is a $os $arch build, not linux x86_64" >&2; exit 2; }
exe="Guitar Dream Universal Launcher"
[ -x "$build/$exe" ] || { echo "build_appimage.sh: $build: no $exe" >&2; exit 2; }

# The tools, pinned and checked.
cache="${GDUL_TOOLS_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/gd-engine/appimage}"
mkdir -p "$cache"
fetch() {  # <url> <file> <sha256>
  local file="$cache/$2"
  if [ ! -f "$file" ] || [ "$(sha256sum "$file" | cut -d' ' -f1)" != "$3" ]; then
    say "Downloading $2"
    curl -fsSL --retry 3 -o "$file.part" "$1"
    [ "$(sha256sum "$file.part" | cut -d' ' -f1)" = "$3" ] ||
      { rm -f "$file.part"; echo "build_appimage.sh: $2: SHA-256 mismatch" >&2; exit 1; }
    mv "$file.part" "$file"
  fi
  chmod +x "$file"
}
fetch "https://github.com/AppImage/appimagetool/releases/download/$APPIMAGETOOL_VERSION/appimagetool-x86_64.AppImage" \
  "appimagetool-$APPIMAGETOOL_VERSION-x86_64.AppImage" "$APPIMAGETOOL_SHA256"
fetch "https://github.com/AppImage/type2-runtime/releases/download/$RUNTIME_VERSION/runtime-x86_64" \
  "runtime-$RUNTIME_VERSION-x86_64" "$RUNTIME_SHA256"

# The AppDir: the build folder as it is, the program as AppRun, a desktop
# file and the launcher's icon.
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
appdir="$work/gdul.AppDir"
cp -a "$build" "$appdir"
ln -s "$exe" "$appdir/AppRun"
cp "$launcher/res/gdul_icon_512.png" "$appdir/gdul.png"
ln -s gdul.png "$appdir/.DirIcon"
cat > "$appdir/gdul.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Guitar Dream Universal Launcher
Comment=Every Guitar Dream game in one launcher (bring your own discs)
Exec=gdul
Icon=gdul
Terminal=false
Categories=Game;
X-AppImage-Version=$version
EOF

name="gdul-$version-x86_64.AppImage"
rm -f "$out/$name" "$out/$name.zsync"
say "Packing $name (appimagetool $APPIMAGETOOL_VERSION, runtime $RUNTIME_VERSION)"
(cd "$out" && ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 NO_APPSTREAM=1 \
  "$cache/appimagetool-$APPIMAGETOOL_VERSION-x86_64.AppImage" --no-appstream \
  --runtime-file "$cache/runtime-$RUNTIME_VERSION-x86_64" -u "$UPDATE_INFO" "$appdir" "$out/$name")
[ -s "$out/$name" ] || { echo "build_appimage.sh: $name was not made" >&2; exit 1; }
[ -s "$out/$name.zsync" ] || { echo "build_appimage.sh: $name.zsync was not made" >&2; exit 1; }
say "Ready: $out/$name (+ .zsync)"
