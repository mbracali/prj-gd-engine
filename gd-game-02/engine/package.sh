#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# Turns a built dist/ folder into the download a release publishes:
#   package.sh <windows|linux|steamdeck|macos|android> <version> <out dir>
# writes <out dir>/gd02-<version>-<platform>.<zip|tar.gz|apk>.
set -euo pipefail
cd "$(dirname "$0")"
platform="$1" version="$2" out="$3"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
case "$platform" in
  windows) f="$out/gd02-$version-windows-x64.zip"; rm -f "$f"
           (cd dist && if command -v zip >/dev/null; then zip -qr "$f" gd02-windows-x64; else 7z a -tzip "$f" gd02-windows-x64 >/dev/null; fi) ;;
  linux)   f="$out/gd02-$version-linux-x86_64.tar.gz"; tar czf "$f" -C dist gd02-linux-x86_64 ;;
  steamdeck) f="$out/gd02-$version-steamdeck.tar.gz"; tar czf "$f" -C dist gd02-steamdeck ;;
  macos)   f="$out/gd02-$version-macos.zip"; rm -f "$f"; (cd dist && ditto -c -k --keepParent gd02-macos "$f") ;;
  android) f="$out/gd02-$version-android.apk"; cp dist/android/gd02.apk "$f" ;;
  *) echo "package.sh: unknown platform $platform" >&2; exit 2 ;;
esac
echo "Packaged $f"
