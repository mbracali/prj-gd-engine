#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# Builds gd24's FFmpeg (build_common.sh) for this Linux machine into
# gd-game-24/engine/build/ffmpeg/linux/ffmpeg-prefix, for a development build:
#   gd-game-24/engine/build_ffmpeg.sh            (FFMPEG_JOBS=2 to limit make's jobs)
#   cmake ... -DGD24_FFMPEG_DIR=$PWD/gd-game-24/engine/build/ffmpeg/linux/ffmpeg-prefix
set -euo pipefail
cd "$(dirname "$0")"
. ./build_common.sh
build_ffmpeg build/ffmpeg/linux Linux --disable-x86asm
