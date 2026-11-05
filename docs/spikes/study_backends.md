# Render backends: Vulkan, Direct3D, Metal, OpenGL fallback

Status: study, 2026-10-28.

## Verdict

**Do it through SDL3's GPU API (SDL_GPU), starting with gd02, and keep
OpenGL as the fallback backend.** SDL_GPU gives **Vulkan (Linux, Windows,
Android), Direct3D 12 (Windows, Xbox-class) and Metal (macOS, iOS)** from one
API that we already ship (SDL3), with HDR swapchains built in. gd02's
`RenderDevice` (`render/device.h`) is already shaped like it: stateless
`DrawCall`s carrying the full pipeline state, std140 uniform blocks at fixed
bindings, and render targets as handles. A `gpu_device.cpp` next to
`gl_device.cpp` is the right size of job.

Order: **(1) gd02 on SDL_GPU (Metal first, because it fixes the macOS
performance problem; Vulkan next), (2) the launcher's 2D and the module ABI,
(3) gd01 behind a device layer.** D3D12 comes along with SDL_GPU and only
needs testing. Native D3D11 is not worth a separate backend.

Effort: gd02 about 5 to 8 weeks, launcher about 2 weeks, gd01 about 8 to 12
weeks (because it calls GL directly).

## Where we are

| | API today | GL coupling | Shaders |
|---|---|---|---|
| gd02 | GL 3.3 core / GLES 3.0 through `RenderDevice` | **Only `gl_device.cpp` + `gl_api.cpp`** (about 140 GL calls) | 587 GLSL programs: the standard shader's permutations (defines from GH2's key bits), post, 2D, video |
| gd01 | GL 3.3 / GLES 3.0, **direct GL calls**. Vulkan only for the FSR 3.1 / DLSS 4 upscalers (GL↔VK interop). | About 530 GL calls in `renderer.cpp`, `post_fx.cpp`, `vk_upscaler.cpp`. Scene, venue, crowd and track renderers go through them. | Inline GLSL strings |
| gdul | GL 3.3 `gfx.*` 2D batcher (about 100 GL calls). It owns the window. Modules create **their own GL contexts on the launcher's window**, and the shims virtualise `SDL_CreateWindow`/`SwapWindow`. | | One batched 2D shader |

Why change at all:
- **macOS**: Apple's GL is deprecated, frozen at 4.1, and runs on a Metal
  translation layer. gd02 measured about 20 fps on an M4 Pro
  (`renderer.md` "Performance"). Metal (or Vulkan on Metal) removes that
  layer.
- **HDR** (`study_hdr.md`): there is no HDR swapchain for GL anywhere.
  SDL_GPU has `SDL_GPU_SWAPCHAINCOMPOSITION_HDR10_ST2084` and
  `HDR_EXTENDED_LINEAR`, so the separate "GL→Vulkan presenter" becomes
  unnecessary.
- **iOS** (`study_ios.md`): GLES is deprecated there. Metal is the future.
- **Windows**: Vulkan and D3D12 drivers are better maintained than GL on
  Intel/ARM (Snapdragon X) machines.
- **gd01's upscalers**: on a Vulkan backend, FSR 3.1/DLSS run natively, with
  no GL↔VK interop and no semaphores.

## Options

| Option | Backends | Shaders | Fit for us | Cost / risk |
|---|---|---|---|---|
| **A. Our own `RenderDevice` + a raw Vulkan backend**, then Metal and D3D by hand | Whatever we write | GLSL → SPIR-V (glslang), then SPIRV-Cross for MSL/HLSL | Full control. gd02's interface is already there. | 3 backends to write and maintain (Vulkan alone is about 4 to 6 weeks for gd02). Swapchains, memory allocation (VMA), and sync are all ours. |
| **B. `RenderDevice` on SDL_GPU** (recommended) | Vulkan, D3D12, Metal (SDL picks one, `SDL_HINT_GPU_DRIVER` forces one). The D3D11 GPU backend was dropped before SDL 3.2. | Per backend: SPIR-V / DXIL (or DXBC) / MSL. **SDL_shadercross** converts SPIR-V or HLSL to the rest, at run time or offline (CLI + JSON reflection). | One backend for us to write. SDL already owns our window, events and HDR properties. `SDL_CreateGPURenderer` (SDL 3.4) lets a 2D `SDL_Renderer` share the device. | No geometry shaders (we use none). Pipelines are immutable objects, so they need a cache. Uniforms are pushed per slot (fits our std140 blocks). Compute exists. The GL fallback stays our own `gl_device`. |
| **C. bgfx** | D3D11/12, Metal, Vulkan, GL/GLES, WebGL/WebGPU | Its own GLSL-like language + `shaderc` | Mature (MAME uses it), includes GL | It replaces our device layer and shader sources (587 GH2 permutations rewritten). Its build system (GENie) and its own window/thread model fight SDL's. |
| **D. Diligent Engine** | D3D11/12, Vulkan, GL/GLES, Metal | HLSL/GLSL with its own converter | Powerful | Heavy dependency (C++ core, Apache-2.0). The Metal backend's availability under the open licence is unclear (sources disagree), and there is a large learning curve. |
| **E. MoltenVK / KosmicKrisp** (Vulkan on Metal) | Apple only, under a Vulkan backend | SPIR-V | A way to reach Metal if we wrote option A | KosmicKrisp (LunarG, Mesa 26) is Vulkan 1.4 conformant on macOS and reported "not far behind native Metal", but it is **macOS only (no iOS)**. MoltenVK covers iOS. Both add a translation layer that SDL_GPU's native Metal backend avoids. |
| **F. ANGLE** | GLES 3.0 on Metal/Vulkan/D3D11 | Our GLSL ES as is | The cheapest way to stop depending on Apple's GL (also on iOS) | It keeps GL-era limits and does not give HDR. A stopgap, not a backend strategy. |

## Shader pipeline (option B)

1. Keep one source of truth: our GLSL, made **Vulkan-flavoured GLSL 450**
   (`layout(set=, binding=)` following SDL_GPU's fixed set layout: samplers
   and textures in set 0/2, uniform buffers in set 1/3 for vertex/fragment).
   A small preprocessor keeps the GL 330/ES 300 output for the fallback.
2. Offline, at build time (`tools/shaders`): glslang → **SPIR-V** for every
   permutation that gd02 builds (`--shadertest` already lists all 585, so
   the list is known), then `shadercross` → **MSL** (Apple) and **DXIL**
   (D3D12, using `dxcompiler`; built on the Windows/MinGW job or prebuilt).
   Pack them into a shader archive next to the binary (a few MB).
3. Run time: load the blob for the device's format
   (`SDL_GetGPUShaderFormats`). Fall back to a simpler permutation exactly
   as `standard_shader.*` does today. Runtime shadercross is optional (for
   mods or debugging).
4. Validation: compare `--shadertest` and the selftest screenshots between
   GL and each GPU backend (the same pixels within a tolerance).

## Device-layer work for gd02 (`gpu_device.cpp`)

- **Pipelines**: hash (program, PipelineState, VertexLayout, target formats)
  → `SDL_GPUGraphicsPipeline`, cached and pre-warmed for the venue's
  materials at load time to avoid hitches.
- **Passes**: SDL_GPU wants explicit render passes. `Renderer::post()`'s
  target switches map to begin and end pass. Texture uploads go through
  copy passes and transfer buffers (rings).
- **Uniforms**: `SDL_PushGPUVertexUniformData` / `...FragmentUniformData`
  per block (this replaces the GL uniform ring and the macOS map/subdata
  workaround).
- **Formats**: RGBA8, BC1/3/5 (desktop; check
  `SDL_GPUTextureSupportsFormat`, with the existing CPU decode as fallback
  on mobile), RGB10A2, RGBA16F, D24/D32 depth as a sampled texture. Cube maps.
- **Readback** (`readPixels`, selftest screenshots, the flare test):
  download through transfer buffers plus fences.
- **Clip space and Y**: Vulkan/Metal/D3D use 0..1 depth (`DeviceInfo::clipDepthZeroToOne`
  exists already) and top-left texture origin. GH2's D3D conventions
  actually get *simpler* (the GL cull flip in `renderer.md` goes away).
- **Pacing**: frames in flight via `SDL_WaitForGPUFences`. Present mode
  (`SDL_GPU_PRESENTMODE_VSYNC/MAILBOX/IMMEDIATE`) replaces the GL swap interval.

## The launcher's shared window

A window can be used by **either** a GL context **or** an SDL_GPU swapchain
(`SDL_ClaimWindowForGPUDevice`), not both at once. On macOS a CAMetalLayer
and NSOpenGLContext also conflict. So:

- **Module ABI v2** (`gd_host.h`): the host says which API it owns (`gl` or
  `gpu`) and passes the `SDL_GPUDevice*` and claimed window. A module
  declares what it supports. The launcher draws its own 2D with an
  `SDL_Renderer` from `SDL_CreateGPURenderer(device, window)`, or ports
  `gfx.*` to the same device.
- **Mixed period** (gd01 still GL, gd02 on GPU): the launcher switches the
  window's API when it starts a module whose API differs. It releases the
  GPU claim or destroys the GL context, recreates the window if the platform
  needs it (SDL_WINDOW_OPENGL vs SDL_WINDOW_METAL/VULKAN), and restores size,
  position and fullscreen as it already does after a game. Expect a frame
  of black. Acceptable.
- The model explorer (`ExploreSession`) gets a GPU variant: the module
  renders into a texture of the shared device, and the launcher composites it.

## HDR

With SDL_GPU, HDR is a swapchain composition plus an output transform
shader. `study_hdr.md`'s steps 3 to 4 (the Vulkan presenter and interop)
disappear, and HDR also works on macOS (EDR through Metal) and Windows
(D3D12/Vulkan). The FP16 scene, the output pass and the UI paper-white work
remain (1 to 2 weeks).

## Effort

| Step | Effort |
|---|---|
| gd02 shader pipeline (GLSL 450 variants, offline SPIR-V/MSL/DXIL, archive, CI step) | 1.5 to 2 weeks |
| gd02 `gpu_device.cpp` (pipelines, passes, uniforms, formats, readback, pacing) | 3 to 4 weeks |
| gd02 validation on Metal (Mac), Vulkan (Linux, Windows, Android), D3D12 (Windows) | 1 to 2 weeks |
| Launcher: ABI v2, 2D on SDL_GPU, API switching, explorer | about 2 weeks |
| gd01: put a device layer under its renderers (about 530 GL calls, inline GLSL), then reuse `gpu_device`. The upscalers become native Vulkan. | 8 to 12 weeks |
| HDR on top (FP16, output transform, UI) | 1 to 2 weeks per game |

The OpenGL backend stays and remains the default until each GPU backend
passes the screenshot comparisons. It also stays for old GPUs, VMs, and
the Mesa software rasteriser.

## Risks

- Pipeline count: 585 programs × states × target formats. It needs a lazy
  cache with pre-warming per venue (Vulkan pipeline cache on disk).
- shadercross/DXC on MinGW: build the DXIL offline on Linux (DXC has Linux
  builds) and ship the blobs. Runtime DXIL compilation would need
  `dxcompiler.dll`.
- Android Vulkan drivers vary in quality. Keep GLES as the default there and
  offer Vulkan as an Enhanced option.

## Sources

- SDL_GPU category and API: https://wiki.libsdl.org/SDL3/CategoryGPU
- GPU driver hint: https://wiki.libsdl.org/SDL3/SDL_HINT_GPU_DRIVER
- SDL_CreateGPURenderer (SDL 3.4): https://wiki.libsdl.org/SDL3/SDL_CreateGPURenderer
- SDL_GPU swapchain compositions (HDR): https://wiki.libsdl.org/SDL3/SDL_GPUSwapchainComposition
- SDL_shadercross: https://github.com/libsdl-org/SDL_shadercross , https://moonside.games/posts/introducing-sdl-shadercross/
- bgfx: https://github.com/bkaradzic/bgfx
- Diligent Engine: https://github.com/DiligentGraphics/DiligentEngine
- KosmicKrisp (Vulkan 1.4 on Metal): https://www.phoronix.com/news/KosmicKrisp-Vulkan-1.4-2026 , https://www.lunarg.com/the-state-of-vulkan-on-apple-new-lunarg-white-paper/
- MoltenVK: https://github.com/KhronosGroup/MoltenVK
- ANGLE: https://chromium.googlesource.com/angle/angle
- glslang / SPIRV-Cross: https://github.com/KhronosGroup/glslang , https://github.com/KhronosGroup/SPIRV-Cross

## Backlog candidates

- **(GD02, Enhancement) Offline shader pipeline**: GLSL 450 variants of all
  585 programs → SPIR-V/MSL/DXIL archive, with a check in `--shadertest`.
  1.5 to 2 weeks.
- **(GD02, Enhancement) SDL_GPU render device** (`gpu_device.cpp`) with the
  backend chosen in Enhanced Options (Auto / OpenGL / Vulkan / Metal /
  Direct3D 12) and OpenGL as fallback. 3 to 4 weeks plus 1 to 2 weeks of
  validation. **The first target is Metal on macOS** (the 20 fps problem).
- **(GDUL, Enhancement) Module ABI v2 + launcher 2D on SDL_GPU +
  window API switching** between GL and GPU modules. About 2 weeks.
- **(GD01, Enhancement) Device layer under gd01's renderers**, then the
  SDL_GPU backend and native-Vulkan upscalers. 8 to 12 weeks.
- **(GDALL, Enhancement) HDR via SDL_GPU swapchains**: this replaces the GL→VK
  presenter plan in `study_hdr.md`. 1 to 2 weeks per game after the backends.
- **(GDUL/GD02, Enhancement, stopgap) ANGLE on macOS/iOS** if the SDL_GPU work
  is delayed. About 1 week.
