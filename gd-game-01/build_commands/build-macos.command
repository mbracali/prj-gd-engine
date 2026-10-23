#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# Double-click in Finder to build the macOS download (and nothing else) into
# gd-game-01/local_builds/. Needs Homebrew: brew install cmake sdl3 ninja dylibbundler
. "$(dirname "$0")/_run.sh"
run_build "$0" macos
