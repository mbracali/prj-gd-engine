#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# Development aid: starts gd24 inside a development launcher while the game
# is still ready_to_play: false (the launcher refuses such a game). Nothing
# tracked changes: a games.yaml listing only gd-game-24, with a copy of its
# game-info.yaml marked ready, is written to gd-game-24/local_builds/launcher-try/
# (git-ignored) and passed as GDUL_GAMES_YAML; the module and its FFmpeg
# libraries are copied into the launcher's gd-engine-games/gd-game-24/.
#
#   gd-game-24/tools/try_in_launcher.sh <launcher program> [<disc>] [-- launcher options]
#   e.g. gd-game-24/tools/try_in_launcher.sh "gd-engine-launcher/build/gd24/Guitar Dream Universal Launcher"
#
# With a disc the game starts at once (--play gd-game-24 --iso <disc>);
# without, pick it in the launcher (Load ISO, then Play).
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
launcher="${1:?usage: try_in_launcher.sh <launcher program> [<disc>]}"; shift
disc=""
if [ $# -gt 0 ] && [ "$1" != "--" ]; then disc="$1"; shift; fi
[ "${1:-}" = "--" ] && shift
build="$repo/gd-game-24/engine/build/dev"
ffmpeg="$repo/gd-game-24/engine/build/ffmpeg/linux/ffmpeg-prefix/lib"
try="$repo/gd-game-24/local_builds/launcher-try"
games="$(cd "$(dirname "$launcher")" && pwd)/gd-engine-games/gd-game-24"
[ -f "$build/gd-game-24.so" ] || { echo "no $build/gd-game-24.so: build gd24_module first (agents/CONTEXT.md)" >&2; exit 1; }
mkdir -p "$try/gd-game-24" "$games"
printf 'games:\n  - gd-game-24/game-info.yaml\n' > "$try/games.yaml"
sed 's/^ready_to_play: false/ready_to_play: true/' "$repo/gd-game-24/game-info.yaml" > "$try/gd-game-24/game-info.yaml"
cp "$build/gd-game-24.so" "$games/"
[ -d "$ffmpeg" ] && cp -P "$ffmpeg"/lib*.so* "$games/"
args=()
[ -n "$disc" ] && args=(--play gd-game-24 --iso "$disc")
GDUL_GAMES_YAML="$try/games.yaml" exec "$launcher" "${args[@]}" "$@"
