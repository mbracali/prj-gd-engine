# Third-party code (AI upscaling)

Used by `src/render/vk_upscaler.cpp` (Enhanced Settings > AI Upscaling). None of
it is needed to play; the game falls back to its own FSR 1 when any piece is
missing at run time.

| Folder | What | Version | Licence |
|---|---|---|---|
| `vulkan/` | Khronos Vulkan-Headers (C headers only) | 1.4.350 | Apache-2.0 / MIT (`vulkan/LICENSE.md`) |
| `volk/` | Vulkan meta-loader (Vulkan is loaded at run time; no SDK needed to build) | 1.4.350 | MIT (`volk/LICENSE.md`) |
| `fidelityfx/` | AMD FidelityFX SDK: the FSR 3.1 upscaler and its Vulkan backend only | 1.1.4 | MIT (`fidelityfx/LICENSE.txt`) |
| `dlss/` | NVIDIA DLSS SDK: NGX headers, the Linux NGX static library and the DLSS model library. **Not in the repo:** `../tools/fetch_dlss.sh` downloads it (pinned, SHA-256 checked) | 310.9.16 | NVIDIA RTX SDKs licence (`dlss/LICENSE.txt`, downloaded with it) |

## Changes to the FidelityFX SDK (marked `[I] gd01`)

- `ffx_port.h` (force-included): the MSVC-only CRT helpers for gcc / clang
  (mingw-w64 has its own).
- `ffx_fsr3upscaler.h`: the opaque context is twice the size, since the
  private context holds `wchar_t` names (4 bytes outside Windows).
- `src/backends/vk/ffx_vk.cpp`: the scratch arrays are 16-byte aligned
  (gcc's vectorised stores need it); the device is reported as wave32
  without FP16, the only shader permutations generated.
- `ffx_vk_volk.cpp`: builds the Vulkan backend on volk, with a stub for the
  frame-generation swap chain (not built).
- `generated/`: the FSR 3.1 shader permutation headers, made by
  `tools/ffx_gen_shaders.py` with glslang from the SDK's GLSL passes (the
  SDK's own shader compiler is Windows-only). The build does not need glslang.

## DLSS on Windows

NVIDIA ships the NGX library for MSVC only, so the MinGW build has no DLSS.
An MSVC build gets it after `../tools/fetch_dlss.sh --windows`, which also
downloads `dlss/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib` and
`dlss/lib/Windows_x86_64/rel/nvngx_dlss.dll` from the same SDK release.
