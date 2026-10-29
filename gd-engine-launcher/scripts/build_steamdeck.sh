#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# The Steam Deck launcher download (gdul-<version>-steamdeck-x86_64): build.sh
# steamdeck inside Valve's Steam Runtime 3 "sniper" SDK
# (registry.gitlab.steamos.cloud/steamrt/sniper/sdk), the environment Steam
# runs games in, as gd01's and gd02's Steam Deck downloads were built: gcc-14,
# the C++ runtime linked in, the runtime's SDL3 shipped next to the program.
# build.sh steamdeck runs this in that container (podman or docker); the gdul
# release workflow runs it as its steamdeck job's container.
# Arguments go to build.sh (e.g. --previous <folder>, --force).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
grep -qs 'Steam Runtime' /etc/os-release || { echo "build_steamdeck.sh: run it in the sniper SDK container (build.sh steamdeck does)" >&2; exit 2; }
export CC=gcc-14 CXX=g++-14
export LDFLAGS="-static-libstdc++ -static-libgcc"
export GDUL_SDL3_LICENSE=/usr/share/doc/libsdl3-0/copyright
git config --global --add safe.directory "$(cd "$here/../.." && pwd)" 2>/dev/null || true
exec "$here/build.sh" steamdeck "$@"
