#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# Downloads the NVIDIA DLSS SDK files the build uses into engine/third_party/dlss/
# (not in the repo: the DLSS model library alone is 57 MB). Pinned to one SDK
# release and checked by SHA-256. Without them the game builds and runs
# without DLSS (the AI upscaler falls back to FSR 3.1, then FSR 1).
#
#   tools/fetch_dlss.sh            Linux files (headers, NGX static library, DLSS model library)
#   tools/fetch_dlss.sh --windows  also the MSVC files (nvsdk_ngx_s.lib, nvngx_dlss.dll)
#
# To move to a newer SDK: change VERSION and the hashes below (sha256sum of the
# files at https://github.com/NVIDIA/DLSS/tree/v<VERSION>).
set -euo pipefail

VERSION=310.9.16
BASE="https://raw.githubusercontent.com/NVIDIA/DLSS/v$VERSION"
DEST="$(cd "$(dirname "$0")/.." && pwd)/engine/third_party/dlss"

# <sha256> <path in NVIDIA's repo> <path under third_party/dlss>
LINUX="
d4216e39ebef5f9b50a6712ebb37beeb5379862a67733a9999c651f21592aaf0 LICENSE.txt LICENSE.txt
dc38e7467cf415379c9d12ae1b6e4a494c453ed92720fb53e92aecb523e7b848 include/nvsdk_ngx.h include/nvsdk_ngx.h
ea23f33497cd274860d1c25a97644fce807dcb0037c594547203343103fad03e include/nvsdk_ngx_defs.h include/nvsdk_ngx_defs.h
0a24d0861ace7d6b9362a67b7f08bea1b33ea123c5ce730b19133de8a891d031 include/nvsdk_ngx_defs_vk.h include/nvsdk_ngx_defs_vk.h
5bcbadfe7478b802cf6d3aca4dc5ddd7d0889b99726e69c63f9e9bd555f44471 include/nvsdk_ngx_helpers.h include/nvsdk_ngx_helpers.h
f2851a76107bdf4fbcb7b261f3c35573c312d8ecb4bc18fb02d9e2cec7705042 include/nvsdk_ngx_helpers_cuda.h include/nvsdk_ngx_helpers_cuda.h
ec75224f36ed6580baaf250fa83620405ad81a1d98c2c436654a0c9cf6a6b8ba include/nvsdk_ngx_helpers_d3d.h include/nvsdk_ngx_helpers_d3d.h
c192bff72138f12f770db48e12c1d8f712dfc81ea0e8e055ebb27d8dd1b31623 include/nvsdk_ngx_helpers_vk.h include/nvsdk_ngx_helpers_vk.h
abdf5f66a692620538357d29fdf78e3e2a677ee8140b7a638962ecb766585e75 include/nvsdk_ngx_loader.h include/nvsdk_ngx_loader.h
943bc8cc5cdae03b6303016fbad3183636f2335ae27a2d18776798c3b4efabbc include/nvsdk_ngx_params.h include/nvsdk_ngx_params.h
2d364ce7132881eb669e9498fd570d74cba563b1fbebc82c235f8e1ad2dd8b6d include/nvsdk_ngx_vk.h include/nvsdk_ngx_vk.h
d3c50bb0d9624fc35e502a8e929b8fc1e2b505017ebdb84c0d5b627e11a235df lib/Linux_x86_64/libnvsdk_ngx.a lib/Linux_x86_64/libnvsdk_ngx.a
30982c937afed85c96b8d313323de9a650a2eec6de1bb0f714b21750f4ce98f6 lib/Linux_x86_64/rel/libnvidia-ngx-dlss.so.$VERSION lib/Linux_x86_64/libnvidia-ngx-dlss.so.$VERSION
"
WINDOWS="
7b9a5239c1145af5f645ab472aa066f9ea79db36ac819562312aae69d65f3765 lib/Windows_x86_64/x64/nvsdk_ngx_s.lib lib/Windows_x86_64/x64/nvsdk_ngx_s.lib
d00b5b0ab29c3ca101b70c3a5ecb3a01b7ef33766a3383a28a18623083e6123e lib/Windows_x86_64/rel/nvngx_dlss.dll lib/Windows_x86_64/rel/nvngx_dlss.dll
"

list="$LINUX"
[ "${1:-}" = "--windows" ] && list="$LINUX$WINDOWS"

sha() { sha256sum "$1" | cut -d' ' -f1; }

while read -r hash src dst; do
  [ -n "$hash" ] || continue
  out="$DEST/$dst"
  if [ -f "$out" ] && [ "$(sha "$out")" = "$hash" ]; then continue; fi
  mkdir -p "$(dirname "$out")"
  curl -fsSL --retry 3 -o "$out.part" "$BASE/$src"
  if [ "$(sha "$out.part")" != "$hash" ]; then
    rm -f "$out.part"
    echo "fetch_dlss.sh: $src does not match its SHA-256; nothing installed for it" >&2
    exit 1
  fi
  mv "$out.part" "$out"
  echo "DLSS SDK $VERSION: $dst"
done <<< "$list"
echo "DLSS SDK $VERSION ready in $DEST"
