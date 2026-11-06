# gd-engine-core: render

The core's graphics layer: one interface, `gd::core::render::Device`
(`render/include/gd/core/render/device.h`), and its backends. The
**OpenGL 3.3 / OpenGL ES 3.0** backend (`opengl3`) exists now (1.1.13,
GDCORE-004 phase 1); **SDL3 GPU** (`sdl3`) and **Vulkan** (`vulkan`) come next,
on the same interface, on the groundwork of phase 2a (the build-time SPIR-V,
section 3; the launcher's window hand-over, section 5; the state rules, 2.2). This file is the design: read it before adding a
backend or moving a game onto the core. Facts are marked [C] confirmed or
[I] inferred where it matters.

## 1. What shaped it

- Three very different APIs behind one interface: OpenGL (implicit state, a
  context per window, bottom-left origin, depth -1..1), SDL_GPU (explicit
  passes, immutable pipelines, per-slot pushed uniforms, copy passes, fixed
  descriptor set layout) and Vulkan (everything explicit). The interface is
  the **intersection that maps cleanly onto all three** without emulation
  costs: if SDL_GPU cannot do something cheaply, it is not in the interface.
- gd02's `RenderDevice` (gd-game-02/engine/src/render/device.h) was the
  starting point: handles, std140 blocks at fixed bindings, fixed texture
  slots, target formats. Kept from it: the handle style, std140 blocks at
  fixed slots, BCn formats with a CPU fallback left to the game, top-left
  viewport coordinates. Changed from it: gd02's `DrawCall` carries the whole
  pipeline state per draw, which a GL device can apply cheaply but SDL_GPU and
  Vulkan cannot (pipelines are compiled objects); here **pipelines are
  created up front** (callers cache them), and **render passes are explicit**
  (gd02's `setTarget` is a GL-ism: SDL_GPU and Vulkan need begin / end with
  load and store operations).
- The engine guidelines (docs/guidelines/engine-backend-guidelines.md):
  plain C++17, no exceptions across the interface, no game types, C APIs
  behind the backends, a factory keyed by the setting ids, `local` always the
  default and the fallback.

## 2. The interface

Everything is in `namespace gd::core::render`. All calls happen on one
thread (the one that created the device). Errors never throw: creation
returns an empty handle and the reason is in `Device::lastError()`.

### 2.1 Objects

| Object | Created with | Notes |
|---|---|---|
| `Buffer` | `createBuffer(BufferDesc, data)` | usage flags `Vertex`, `Index`, `Uniform`; static contents updated with `updateBuffer` (outside a pass) |
| transient data | `allocTransient(usage, data, size)` | per-frame ring (vertex / index / uniform data that lives one frame: immediate-mode draws). Valid until the frame after next starts |
| `Texture` | `createTexture(TextureDesc)` | 2D or cube, mip count (0 = full chain), `samples` (MSAA targets), usage flags `Sampled`, `ColorTarget`, `DepthTarget`. Formats below |
| `Sampler` | `createSampler(SamplerDesc)` | filters (min / mag / mip), wrap per axis, anisotropy, LOD bias, border, compare. Deduplicated by the device |
| `Shader` | `createShader(ShaderDesc)` | a vertex + fragment program in every form the build has (section 3) plus its reflection: uniform block names by slot, sampler names by slot |
| `Pipeline` | `createPipeline(PipelineDesc)` | shader + vertex layout + primitive + raster (cull, front face, depth bias) + depth / stencil + blend and write mask per colour target + **the target formats and sample count** it draws into. Immutable |

Formats (`Format`): `RGBA8`, `RGBA8_SRGB`, `BGRA8`, `R8`, `RG8`,
`RGB10A2`, `RG11B10F`, `R16F`, `RG16F`, `RGBA16F`, `R32F`, `RGBA32F`,
`BC1`, `BC2`, `BC3`, `BC4`, `BC5`, `BC7` (with `_SRGB` for 1/2/3/7), depth
`D16`, `D24`, `D24S8`, `D32F`, `D32FS8`. `Device::caps()` says per format
whether it can be sampled, rendered to, filtered, blended (e.g. BCn on GLES
without S3TC: not sampled; the game decodes on the CPU, `gd::core::image`).
Depth formats are always sampleable (shadow maps, depth probes).

Handles are 32-bit ids in typed structs (`Texture{}` is "none"); the device
owns the API objects and `destroy*` releases them (a destroyed object still
used by a frame in flight is released when that frame is done).

### 2.2 Frames, passes, draws

```
beginFrame()                    waits until at most kFramesInFlight (2) frames are on the GPU, recycles transient rings
  updateTexture / updateBuffer / copyTexture / blit / generateMipmaps / readTexture   (outside passes only)
  beginPass(PassDesc)           colour attachments (load Clear / Load / DontCare, store Store / DontCare / Resolve
                                into a resolve texture), depth-stencil attachment (load / clear / store)
    setViewport / setScissor / setStencilRef / setBlendConstants
    bindPipeline / bindVertexBuffers / bindIndexBuffer
    bindTextures(slot, {texture, sampler}...)           combined texture + sampler slots 0..7
    pushUniforms(slot, data, size)                      std140 block contents for slot 0..3, used by later draws
    draw / drawIndexed (+ instances)
  endPass()
present(source texture, rect, filter)   blits into the window's swapchain (scaled) and presents
endFrame()
```

Rules that keep the three backends honest:

- **No resource updates inside a pass** (SDL_GPU forbids copy passes inside a
  render pass; Vulkan forbids transfers inside a render pass instance).
  Only `allocTransient` and `pushUniforms` may be used inside one; they are
  recorded so a backend can upload them before the pass. A caller that wants
  to change a texture mid-frame ends the pass, updates, and begins another
  pass with `Load`.
- **Clears happen at pass begin** (load op `Clear`), never in the middle of a
  pass. A scissored clear is a draw.
- **Pipelines carry the target formats**: a pipeline drawn into a target of
  other formats is an error (the GL backend checks it in debug builds).
- **Uniforms**: `pushUniforms(slot, ...)` copies at most `kMaxUniformBytes`
  (16 KiB) per slot; the data is std140. It is visible to both shader
  stages (SDL_GPU: the backend pushes it to the vertex and fragment slot the
  shader declares; Vulkan: a dynamic-offset uniform buffer per slot; GL: a
  ring buffer bound with `glBindBufferRange` at binding = slot).
- **Textures and samplers bind as pairs** in slots 0..7 (`kMaxTextureSlots`),
  which is what SDL_GPU (`SDL_BindGPUFragmentSamplers`), Vulkan (combined
  image samplers) and GL (texture unit + sampler object) all do natively.
- **What a pass keeps** (SDL_GPU's and Vulkan's state lives in the pass /
  command buffer, GL's forever; the interface promises the intersection):
  `beginPass` resets the viewport and the scissor to the whole attachment
  (depth range 0..1) and unbinds the pipeline and the vertex / index
  buffers. **Texture bindings, the stencil reference and the blend
  constants must be set again after `beginPass`** (the GL backend happens to
  keep them; the others do not). **Pushed uniforms stay until the frame
  ends**, also across passes (SDL_GPU keeps them for the command buffer):
  push every slot a draw reads in each frame before its first draw.
  gd01's CoreRenderer binds its texture and pushes its blocks before every
  draw [C: core_renderer.cpp], so it is portable as it is.

### 2.3 Conventions (the part that differs most between APIs)

- **Clip space is OpenGL's**: y up, depth -1..1 (`gl_Position` as GL
  computes it). Shaders write `gl_Position = gd_clip(p);`; on 0..1 backends
  `gd_clip` remaps z (`z = (z + w) / 2`); y needs no change for SDL_GPU (its
  NDC is y up) and the Vulkan backend uses a negative-height viewport, so
  one SPIR-V serves both (3.2). Games keep their matrices. Window depth
  (depth buffer values, depth range) is 0..1 everywhere.
- **Images are top-down**: row 0 of an uploaded texture, of a render target,
  of a readback, and y = 0 of a viewport or scissor rectangle are the **top**
  of the image. Texture coordinate (0, 0) is the top-left texel of what was
  uploaded *and* of what was rendered. In fragment shaders `gl_FragCoord`
  counts from the top-left. This is SDL_GPU's and Vulkan's (with the
  viewport flip) natural convention; the GL backend gets it by rendering every target
  upside down (it flips y in `gd_clip` and swaps the front face), and by
  flipping once when it presents.
- **Front face** is stated as in GL (counter-clockwise in clip space with y
  up); backends adapt.
- MSAA targets cannot be sampled: resolve them with the pass's `Resolve`
  store op.

### 2.4 Readback and present

- `readTexture(texture, rect)` starts an asynchronous copy of a colour
  texture's rectangle (RGBA8, R32F, RGBA16F...) and returns a `Readback`;
  `readbackReady(r)` polls; `readbackData(r, dst)` copies it (waiting if it
  is not ready: development aids only). Maps to a PBO + fence (GL), a
  download transfer buffer + fence (SDL_GPU), a buffer copy + fence (Vulkan).
  Depth is not read back directly (SDL_GPU cannot download depth on every
  driver [I]); a game that needs depth values samples the depth texture in a
  shader into a colour target and reads that (gd01's lens flares do).
- `present(texture, srcRect, filter)` scales a texture to the whole window
  and presents. `setPresentMode(Vsync / Immediate / Mailbox)` and
  `swapInterval` replace the GL swap interval. The window's size in pixels
  is `drawableSize()`.

### 2.5 Creation and the backends

```cpp
DeviceCreateInfo info;
info.backend = Backend::OpenGL3;   // parseBackend("opengl3")
info.window = window;              // SDL_Window*, made with windowFlags(backend)
info.gl = {...};                   // optional: the module's own context hooks (section 5)
std::string error;
std::unique_ptr<Device> dev = createDevice(info, error);   // nullptr + error: the game falls back to local
```

`backendId()` / `parseBackend()` use the setting ids `opengl3`, `sdl3`,
`vulkan`. `backendCompiled(b)` says whether this build has a backend; on a
platform without one (OpenGL3 on macOS later, Vulkan without a loader) the
game does not offer it (rule 7).

## 3. Shaders: one source, compiled at build time where needed

**Decision.** Shaders are written once, in the **gd shader dialect**:
GLSL in the subset shared by `#version 330 core`, `#version 300 es` and
`#version 450` (Vulkan GLSL), with a few macros where those differ. The
OpenGL3 backend compiles it at run time with the driver's compiler; SDL3 GPU
and Vulkan take **SPIR-V compiled from the same text at build time** by
`gdcore_shaderc` (section 3.3). No shader compiler ships to players (rule 8).

### 3.1 The dialect

| Macro | OpenGL 3.3 / ES 3.0 (run time) | SPIR-V: Vulkan GLSL 450 (build time) |
|---|---|---|
| `GD_UNIFORMS(slot, Name) { ... };` | `layout(std140) uniform Name` (bound to binding `slot` by name after linking) | `layout(std140, set = 1 (vertex) / 3 (fragment), binding = slot) uniform Name` |
| `GD_SAMPLER2D(slot, name);` / `GD_SAMPLER2DSHADOW` / `GD_SAMPLERCUBE` | `uniform sampler2D name;` (unit `slot` set after linking) | `layout(set = 0 (vertex) / 2 (fragment), binding = slot) uniform sampler2D name;` |
| `GD_ATTR(loc)` (vertex inputs) | `layout(location = loc) in` | the same |
| `GD_IN(loc)` / `GD_OUT(loc)` (varyings) | `in` / `out` (330 has no varying locations: matched by name) | `layout(location = loc) in` / `out` (matched by location) |
| `GD_FRAG_OUT(loc)` | `layout(location = loc) out` | the same |
| `GD_VERTEX_ID` / `GD_INSTANCE_ID` | `gl_VertexID` / `gl_InstanceID` | `gl_VertexIndex` / `gl_InstanceIndex` |
| `gd_clip(p)` (vertex) | `vec4(p.x, -p.y, p.z, p.w)`: every target drawn upside down (2.3) | `vec4(p.x, p.y, 0.5 * (p.z + p.w), p.w)`: z to 0..1 |
| defined | `GD_GL`, `GD_VERTEX` / `GD_FRAGMENT` | `GD_SPIRV`, `GD_VERTEX` / `GD_FRAGMENT`, `GD_TEXTURE_SET`, `GD_UNIFORM_SET` |

Rules of the dialect (the build step enforces the binding ones):

- **No loose uniforms** (`uniform float x;`): Vulkan GLSL has none. Every
  value goes in a `GD_UNIFORMS` block (std140), at most
  `kMaxUniformSlots` (4) blocks per stage, slots 0..3.
- **Textures are combined samplers** in slots 0..7 (`kMaxTextureSlots`). No
  separate samplers, storage buffers, storage images or push constants
  (SDL_GPU's graphics pipelines bind only what is listed here).
- **Varyings match by location** in SPIR-V and by name in GL 3.3: give a
  varying the same name *and* the same `GD_IN` / `GD_OUT` location in both
  stages.
- **The same block in both stages** (gd01's `DrawState`) is two bindings
  in SPIR-V (vertex set 1, fragment set 3, same slot); the backends push the
  slot's data to every stage that declares it (2.2).
- **`gl_FragCoord`** counts from the **top-left** on every backend (GL: the
  targets are upside down; SDL_GPU and Vulkan: natively). Do not redeclare
  it (`origin_upper_left` is not ES).
- **`GD_INSTANCE_ID`** includes `firstInstance` in SPIR-V
  (`gl_InstanceIndex`) and not in GL (`gl_InstanceID`; GL 3.3 has no base
  instance): portable code draws instanced with `firstInstance = 0`.
- **Points**: a shader drawn with `Primitive::Points` writes `gl_PointSize`
  (Vulkan requires it).
- **Precision**: the GL ES prelude sets `highp` everywhere; nothing in the
  dialect names a precision.

### 3.2 The SPIR-V form

SPIR-V 1.0 for Vulkan 1.0 (what SDL_GPU's Vulkan driver and our Vulkan
backend both accept), entry point `main`, no debug info, **not optimised**
(the drivers optimise; the blob stays the source's maths, as the GL path
compiles it). The resource sets are **SDL_GPU's fixed SPIR-V layout**
[C: SDL 3.4.18 `SDL_gpu.h`, `SDL_CreateGPUShader`]:

| Set | Vertex stage | Fragment stage |
|---|---|---|
| 0 | sampled textures, binding = slot | - |
| 1 | uniform buffers, binding = slot | - |
| 2 | - | sampled textures, binding = slot |
| 3 | - | uniform buffers, binding = slot |

The Vulkan backend uses the same four descriptor set layouts, so **one blob
serves both** (no second variant). SDL_GPU numbers each stage's bindings
from 0 and wants their counts (`num_samplers`, `num_uniform_buffers`):
`render/src/common/spirv_reflect.h` (`reflectSpirv`) reads them from the
blob's decorations (highest binding + 1 per set, plus masks of what is
really used); a slot below the highest that a shader does not use still
counts, and the backend binds a dummy (a 1 x 1 texture, an empty block) to
it.

**Clip space and y.** SDL_GPU's normalized device coordinates are y up,
z 0..1, and its viewport / texture coordinates are top-left
[C: `SDL_gpu.h` "Coordinate System"; its Vulkan driver flips the viewport
itself]. So `gd_clip` only remaps z for SPIR-V, and NDC y = +1 lands on row 0
(the top), the interface's convention. The **Vulkan backend** gets the same
picture from the same blob with a **negative-height viewport** (core in
Vulkan 1.1, `VK_KHR_maintenance1`): `y = top + height, height = -height`,
exactly as SDL_GPU's own Vulkan driver does, and with the front face passed
through unchanged (counter-clockwise stays counter-clockwise) [C: SDL 3.4.18
`src/gpu/vulkan/SDL_gpu_vulkan.c`: `currentViewport.y = viewport->y +
viewport->h; height = -viewport->h`, and `SDLToVK_FrontFace` maps CCW to
`VK_FRONT_FACE_COUNTER_CLOCKWISE`]. So the interface's GL-style front face
needs no swap on either (the self-test's cull check will say it on each).

### 3.3 The build step: gdcore_shaderc

`render/tools/shaderc/gdcore_shaderc.cpp`, a program for the **build
machine**, linked with glslang (GLSL front end and SPIR-V back end only:
no HLSL, no optimiser) and SPIRV-Tools (the validator), both pinned in
`cmake/gdcore_deps.cmake` at Vulkan SDK 1.4.321.0. A game's CMakeLists.txt
calls (the function comes with gdcore_render, `render/cmake/gdcore_shaders.cmake`):

```cmake
gdcore_spirv_shaders(<target> OUTPUT <header.h> SOURCES <file.cpp>...
                     [NAMESPACE <ns>] [DEFINES NAME[=VALUE]...])
```

It reads the dialect **where the games keep it**: C++ raw string literals
named `k<Name>Vs` (vertex) / `k<Name>Fs` (fragment), e.g.
`const char* kMainVs = R"( ... )";` (any raw-string delimiter; one literal,
no concatenation). Each one is compiled with the SPIR-V prelude (3.1) and the
`DEFINES`, validated with `spirv-val` (Vulkan 1.0) and with the dialect's
binding rules (`reflectSpirv`), and written to
`<build>/gdcore_shaders/<target>/<header.h>`, which is on the target's
include path:

```cpp
// kMainVs (vertex, core_renderer.cpp:21): samplers -, uniform blocks 0,1, inputs 0,1,2,3,4, outputs 0,1,2
inline constexpr uint32_t kMainVsSpirv[] = {0x07230203u, ...};
```

and the game sets `desc.spirv(kMainVsSpirv, kMainFsSpirv)` next to its
`glslVertex` / `glslFragment` (`ShaderDesc::spirv`, a template that also
stores the sizes). A shader that does not compile, is invalid SPIR-V or
breaks the layout **fails the build** with the error at the .cpp line
(`core_renderer.cpp:23: 'x' : undeclared identifier`). A program
permutation (`ShaderDesc::defines`) is a second call with other `DEFINES`
and another `OUTPUT`.

**Host tool, every package.** SPIR-V is the same for every platform, so the
Windows (MinGW) and Android (NDK) cross builds use blobs made on the Linux
build machine. `gdcore_spirv_shaders` builds `gdcore_shaderc` at configure
time, **for the host** (a separate CMake project, configured with the
environment's compilers and without the game's toolchain file; `CC` / `CXX`
are kept, so the Steam Deck build uses the sniper SDK's gcc-14), **once per
environment** in `GDCORE_DEPS_DIR/host-tools/gdcore_shaderc-<system>-<key>/`
(the key hashes the system and its release, the compilers, the pins and the
folder: the Fedora host, the sniper container and the ubuntu:22.04 container
each get their own; about 30 MB and under a minute on 16 cores), under a
file lock. Before every build a no-op step (`gdcore_shaderc_tool`) rebuilds
it if its sources changed. Options: `GDCORE_SHADERC=<path>` (a prebuilt one;
nothing is built), `GDCORE_HOST_CC` / `GDCORE_HOST_CXX` (when `CC` / `CXX`
are a cross compiler), `GDCORE_SPIRV_VALIDATE=OFF` (no SPIRV-Tools; also
automatic, with a warning, where there is no Python 3, which SPIRV-Tools'
build needs).

**Why raw strings and not .glsl files.** gd01's shaders already live in its
.cpp files (the GL path compiles those strings); reading them where they are
keeps one copy, needs no file layout change in any game, and the post-FX
port adds its programs the same way. A game that prefers files can embed
them as `k<Name>Vs` strings (`#include` of a generated header) and call the
same function on that file.

**Metal and D3D12 (later).** SDL_GPU on macOS / iOS needs MSL and on
Windows prefers DXIL. Both come from the same SPIR-V offline: SPIRV-Cross
(MSL; SDL_shadercross wraps it) and shadercross + DXC (DXIL), as further
host tools of `gdcore_shaderc` filling `ShaderDesc` fields added then
(`msl*`, `dxil*`). Until then the SDL3 GPU backend asks SDL for a device
with `SDL_GPU_SHADERFORMAT_SPIRV` only, which picks the Vulkan driver (on
Windows too, where a Vulkan driver is installed; else it fails and the game
falls back to OpenGL).

**Why not GLSL 450 + SPIRV-Cross to GLSL for GL too?** It would make the GL
path depend on two large host tools for no gain: the GL backend accepts the
dialect, and the drivers' GLSL compilers are the ones gd01 and gd02 have
always used (the risk-free choice for `opengl3`).

`ShaderDesc` carries every form the build has: `glsl*` (the dialect bodies +
`#define` lines + reflection names) and `spirv*` (vertex and fragment
blobs). A backend takes the first it can use and fails with a clear error
otherwise (OpenGL3: `glsl`; SDL3 GPU and Vulkan: `spirv`).

## 4. Backends

### 4.1 OpenGL3 (`opengl3`, now)

`render/src/gl/`. GL 3.3 core on desktop, GLES 3.0 on Android (and with
`GD_GLES=1` [I: gd01's GD01_GLES does the same for local]). Entry points are
loaded through `SDL_GL_GetProcAddress` (no GL library at link time).

- Pipelines are state blocks applied with a shadow-state cache (only
  changed state is set). Vertex layouts become VAOs cached per (pipeline
  layout, buffers).
- Passes bind an FBO made for the attachments (cached by attachments); load
  `Clear` clears, `Resolve` blits to the resolve texture at `endPass`.
- Targets are rendered upside down (`gd_FlipY` = -1 and `glFrontFace(CW)`),
  so that GL's rows match the interface's top-down rows; `present` flips.
- Uniform data goes into a ring buffer (`glBindBufferRange`); transient
  vertex data into another ring (orphaned per frame, at most two frames in
  flight with fences).
- Readback: PBO + `glFenceSync`.
- Copies: `glBlitFramebuffer` between FBOs (GL 3.3 has no
  `glCopyImageSubData`).
- On GLES without `EXT_color_buffer_float` the float formats are reported
  not renderable in `caps()`.

### 4.2 SDL3 GPU (`sdl3`, phase 2) — how the interface maps

| Interface | SDL_GPU |
|---|---|
| `beginFrame` / `endFrame` | acquire a command buffer; submit with a fence; `SDL_WaitForGPUFences` for frame N-2 |
| `beginPass(PassDesc)` | `SDL_BeginGPURenderPass` with `SDL_GPUColorTargetInfo` (load / store / resolve) and the depth-stencil info |
| transient data, `updateBuffer`, `updateTexture` | transfer buffers (cycled) + a copy pass recorded **before** the render pass (the backend records a pass's transient uploads, then replays) |
| `pushUniforms(slot)` | `SDL_PushGPUVertexUniformData` + `SDL_PushGPUFragmentUniformData` for the stages whose reflection uses the slot |
| `bindTextures` | `SDL_BindGPUFragmentSamplers` / `SDL_BindGPUVertexSamplers` |
| `createPipeline` | `SDL_CreateGPUGraphicsPipeline` (the target formats are in `PipelineDesc`) |
| `copyTexture` / `blit` | `SDL_CopyGPUTextureToTexture` / `SDL_BlitGPUTexture` |
| `generateMipmaps` | `SDL_GenerateMipmapsForGPUTexture` |
| `readTexture` | `SDL_DownloadFromGPUTexture` into a download transfer buffer + fence |
| `present` | `SDL_AcquireGPUSwapchainTexture` + `SDL_BlitGPUTexture` |
| shaders | SPIR-V from the build step (3.3; the Vulkan driver); MSL (Metal), DXIL (D3D12) later from the same SPIR-V |
| shader resource counts | `reflectSpirv` (`src/common/spirv_reflect.h`) |
| depth bias, cull, front face | `SDL_GPURasterizerState` (front face as given: SDL flips the viewport itself) |
| `Wrap::ClampToBorder` | none in SDL_GPU: clamp to edge, `Caps::clampToBorder` false |
| D24 / D24S8 | where `SDL_GPUTextureSupportsFormat` says so, else D32F / D32FS8 behind the same `Format` |

Section 9 lists the details the phase-2 backend needs.

### 4.3 Vulkan (`vulkan`, phase 3)

Vulkan 1.1+ through volk (gd01 already vendors volk and the Vulkan headers),
VMA for memory (single header, MIT). Dynamic rendering
(`VK_KHR_dynamic_rendering`, core 1.3) for passes, a pipeline cache on
disk, descriptor sets in the SDL_GPU layout (one SPIR-V for both; a
negative-height viewport for y, 3.2),
uniform slots as dynamic uniform buffers on a per-frame ring, timeline or
per-frame fences for `kFramesInFlight`. MoltenVK / KosmicKrisp on Apple.
gd01's FSR 3.1 / DLSS then run natively (no GL interop).

## 5. The window

The launcher (`gd-engine-launcher`) owns the window: it is created with
`SDL_WINDOW_OPENGL` and the launcher keeps a GL context on it. A module runs
on that window: the module shim (`sdk/gd_module_shim.h`, force-included in
every file of a module) turns the module's `SDL_CreateWindow` into "the
launcher's window" and tracks the module's GL contexts so none outlives it.

**OpenGL3.** The core is a separate library, *not* compiled with the shim,
so the GL backend never calls `SDL_GL_CreateContext`,
`SDL_GL_DestroyContext` or `SDL_GL_SwapWindow` itself when the game passes
`DeviceCreateInfo::gl` hooks: gd01 passes its own (shimmed) calls, so the
context the core makes is a module context the shim tracks and destroys,
and the swap is the shim's (which does nothing while the launcher's model
explorer presents). Without hooks (a standalone program, the self-test) the
backend calls SDL directly.

**SDL3 GPU, Vulkan (Metal): the hand-over (1.1.13 phase 2a, done).** A window
carries one graphics API: a GL context and a GPU swapchain cannot share it
(and on macOS a CAMetalLayer and an NSOpenGLContext conflict), Vulkan and
Metal surfaces want the window made with `SDL_WINDOW_VULKAN` /
`SDL_WINDOW_METAL`, and SDL cannot turn a window back into an OpenGL one
[C: SDL 3.4.18: `SDL_Vulkan_CreateSurface` converts an OpenGL window in
place by unloading the GL library, which would pull it from under the
launcher's live context; the reverse needs SDL's internal
`SDL_RecreateWindow`]. So the window is **made again** for the API:

1. **The game** decides its backend *before* making its window and calls
   `prepareWindow(backend, gles)` then `SDL_CreateWindow(...,
   windowFlags(backend) | ...)` (what it does standalone too).
   `prepareWindow` sets the SDL global property
   `gd.render.window_api_request` (`kWindowApiRequestProperty`) to the
   backend's id.
2. **The shim's `SDL_CreateWindow`** reads it (or the `SDL_WINDOW_VULKAN` /
   `METAL` flag of a game not on the core; nothing set: OpenGL, every module
   before the core) and, when it differs from what the launcher's window is
   made for, calls the **module ABI's `request_window(user, api)`**
   (`sdk/gd_host.h`, appended to `GdHost` in gdul 1.1.13, read only when
   `size` covers it: `GD_HOST_HAS`; `GD_WINDOW_API_OPENGL / SDL_GPU /
   VULKAN / METAL`). It then publishes the window it returns
   (`gd.render.window`, `kWindowProperty`, a pointer) and what it is made
   for (`gd.render.window_api`, `kWindowApiProperty`).
3. **The launcher** (`Launcher::switchWindowApi`) releases its GL context
   and 2D renderer, makes a new window for that API (SDL_GPU: no graphics
   flag, `SDL_ClaimWindowForGPUDevice` sets it up; Vulkan:
   `SDL_WINDOW_VULKAN`; Metal: `SDL_WINDOW_METAL`) at the same size, place,
   full screen state (and exclusive mode) and title, destroys the old one
   (Android: first, one window at a time), and returns it; `host->window`
   follows, `host->gl_context` is NULL. NULL when it cannot (no Vulkan
   loader: `SDL_CreateWindow` fails): the window stays as it was.
4. **`createDevice`** refuses a backend the launcher's window is not made
   for (only that window: a game's own hidden window gets any device; the
   properties are absent outside a launcher): `the launcher's window is made
   for opengl3, not sdl3 (this launcher cannot hand it over ...)`. The game
   then falls back (rule 2): `SDL_DestroyWindow` (nothing, for the
   launcher's), `prepareWindow(Backend::OpenGL3, ...)`, `SDL_CreateWindow(...
   SDL_WINDOW_OPENGL)`: the shim asks for an OpenGL window again, and the
   launcher's context comes back with it. The shim's `SDL_GL_CreateContext`
   on a window not made for OpenGL fails with that reason.
5. **The module releases the window** before asking for another API and
   before returning (destroys its device: `SDL_ReleaseWindowFromGPUDevice`,
   the Vulkan surface). After `gd_game_run`, the launcher makes its OpenGL
   window, context and renderer again by itself and draws its list (one
   black frame at most). If that failed it says so in a message box and
   stops, never leaving a black window.

Older launchers (no `request_window`) and older modules (never ask) keep
working: the ABI version stays 1, the new fields are appended, and a module
that asks an older launcher gets the OpenGL window and the core's refusal
(so its OpenGL fallback). The game's window pointer changes with each
hand-over: a game must ask before it stores the pointer anywhere (gd01:
before `renderer_.init`), or take the one `SDL_CreateWindow` returns again.
Window events of the old window (its id) may still be queued: the launcher
pushes a `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` for the new one.

The model explorer (the launcher draws a module's scene) stays OpenGL-only
(its host has no `request_window`) until the launcher's own 2D moves onto
the core.

## 6. gd-game-01 on the core

gd01 selects the backend once at start (`graphics_backend` in
enhanced.json: `local`, `sdl3`, `vulkan`, `opengl3`; default `local`; the
development aid `GD01_GRAPHICS_BACKEND=<id>` overrides it). `sdl3` and
`vulkan` are not built yet: gd01 logs
`graphics backend: <id> is not available yet; using local` and runs local.

**How (and why this way).** All of gd01's drawing goes through one class,
`Renderer` (render/renderer.h); only `renderer.cpp`, `post_fx.cpp` and
`vk_upscaler.cpp` call GL. Renderer keeps its interface, and gets a second
implementation, `CoreRenderer` (render/core_renderer.cpp), written on
`gd::core::render::Device`. With a core backend, every public Renderer
method forwards to it (`if (core_) return core_->...;` as the first line);
with `local`, `core_` is null and every method runs exactly the code it ran
before. That choice is what makes requirement (a) checkable: the local
path's code is the same text with one null check in front, and the
screenshots prove the output (section 7). A device layer *under* the local
GL code (every GL call through an interface) would have rewritten the local
path; a parallel renderer keeps local untouched and lets the core path be
rewritten freely for SDL_GPU and Vulkan later.

CoreRenderer reproduces the local renderer's pixels:

- **GL-style storage**: gd01 is a GL game (it samples its render targets
  with row 0 at the bottom and counts viewport, scissor, copy and depth-read
  rows from the bottom). CoreRenderer negates clip y in every MVP (exact)
  and swaps the culled face, so every target holds its picture as local's
  does and the interface's rows are local's rows; `present(...,
  flipVertical = true)` shows the frame the right way up. On OpenGL3 this
  flip and the backend's own cancel out, so the rasteriser sees exactly
  local's coordinates: edges through pixel centres (the 2D UI at 1.5x
  scale) break ties the same way. A first version without it shifted thin
  2D lines by one row and drew the crowd's impostor cards upside down;
- the same main shader, in the dialect, with its uniforms in two std140
  blocks (draw state; lights); the same maths in the same order; and the
  uniforms keep their last value between draws as GL program uniforms do
  (a `mix()` with a stale fog colour is not bit-exact otherwise);
- one pipeline per (blend mode, depth test / write, cull face, decal bias,
  alpha write, target), cached;
- the frame (Enhanced > Internal resolution, 1080 by default) is a colour
  + D24 target like local's; `present` scales it to the window;
  without an internal resolution the frame target is the window's size;
- scissored clears become draws (`clearDepth` is a depth-only draw at
  z = 1 with the compare Always, inside the scissor as local's glClear);
  mid-frame resource updates (text meshes,
  movie frames, textures made on first use) end the pass and begin a new one
  that loads the frame;
- the depth of field and the track fade copy the frame's rectangle into a
  texture as local's `glCopyTexSubImage2D` does (pass break + copy);
- lens-flare occlusion: the frame's depth texture is sampled at the flare
  points into a small colour target that is read back asynchronously,
  `kFlareLag` frames later, as local's pixel-pack buffers.

**Not on the core path yet (phase 1):** the Enhanced post-processing chain
(`post_fx.cpp`: anti-aliasing, FSR 1, bloom, SSAO, grain, grade...), True
HDR output, and the FSR 3.1 / DLSS upscalers (`vk_upscaler.*`: GL<->Vulkan
interop). With a core backend they are off, and gd01 logs
`graphics backend opengl3: post-processing / True HDR / AI upscaling are not
on this backend yet; drawing without them`. Everything they do stays
available on `local`. The original look (the PS2 depth of field, particles,
lights) is fully on the core path. Porting `post_fx.cpp` onto the Device is
the next step (its 17 programs are fullscreen passes; nothing in them needs
more than the interface has). The upscalers become native on the Vulkan
backend (phase 3).

## 7. Tests

- `gdcore_render_selftest` (render/tests/): creates the OpenGL3 device on a
  hidden window (works with `SDL_VIDEO_DRIVER=offscreen`), draws triangles
  with each blend mode, depth test, scissor, a textured quad with mipmaps,
  a render target sampled in a second pass, an MSAA resolve, a cube map,
  uniforms in two slots, a copy, a blit, a top-down readback, and checks
  pixels. `ctest` runs it; exit status 0 = pass. Its shaders also carry
  their build-time SPIR-V (`render_selftest_spirv.h`), and two checks read
  it back: every blob has SDL_GPU's sets and the dialect's slots, and the
  reflection refuses what is not SPIR-V.
- The build itself: `gdcore_shaderc` fails the build on any shader of the
  core's self-test or of gd01's CoreRenderer that does not compile, is not
  valid SPIR-V (`spirv-val`) or breaks the binding layout, on every package
  build.
- The window hand-over: `gd-engine-launcher/tests/handover/`
  (`-DGDUL_BUILD_TESTS=ON`, `ctest -R gdul_handover`): a test module asks
  for its window for SDL_GPU, Vulkan, OpenGL and SDL_GPU again the way a
  game on the core does, uses each (an SDL_GPU device claims and presents,
  a Vulkan surface, the core's OpenGL3 device draws), checks that the
  core's `createDevice` refuses the wrong backend, and returns with the
  window made for SDL_GPU; the test checks that the launcher made its
  OpenGL window again and draws its list. Under `offscreen` SDL_GPU cannot
  present (the headless surface), so that one check is skipped there.
- gd01: the selftest and the walks on `local` and on `opengl3`
  (`GD01_GRAPHICS_BACKEND=opengl3`).
- gd01 goldens: screenshots of the main menu, songs in three venues, the
  highway with Star Power, results, Enhanced menu; before / after on
  `local` must be identical, `opengl3` against `local` within a tiny
  tolerance. Reproducible headless frames need a fixed clock: the harness
  preloads a test-only library that makes `SDL_GetTicks*` advance 1/60 s per
  presented frame and `std::random_device` return a fixed sequence (it is
  not part of any build). Results: section 8.

## 8. Results (1.1.13)

### Phase 1

2026-11-06, Linux, NVIDIA RTX 3060 (driver 615.71), headless (offscreen):

- `gdcore_render_selftest`: 19 / 19 on OpenGL 3.3 core, on OpenGL ES 3.2
  (`GD_GLES=1`) and on Mesa llvmpipe.
- gd01 goldens, 1280 x 720 window with the default 1080-line internal
  resolution (and one 1000 x 750 window): main menu, Enhanced menu, Smoke on
  the Water in small_club (30 s), theatre (45 s), arena (60 s), basement
  (4:3 window, 50 s), Star Power deployed in big_club (183 s), the results
  screen. The harness is reproducible: two runs of the same binary give 0
  differing pixels. **Before (95e6dbd) vs after, local: 0 differing pixels
  in every shot. Local vs opengl3: 0 differing pixels in every shot.**
- What the goldens do not cover: the Enhanced post-processing (off on the
  core path), True HDR, the upscalers, OpenGL ES devices (Android).

### Phase 2a (the groundwork for SDL3 GPU and Vulkan)

2026-11-06, same machine:

- `gdcore_shaderc` (glslang + SPIRV-Tools, Vulkan SDK 1.4.321.0) built for
  the host in under a minute; every shader of the core's self-test (6) and
  of gd01's CoreRenderer (8: main, fill, depth of field, depth probe)
  compiles to SPIR-V that passes `spirv-val` (Vulkan 1.0) and the dialect's
  binding rules, with **no change to any shader**: the dialect was already
  valid Vulkan GLSL 450 (the [I] of phase 1 is now [C]). A broken shader, a
  loose uniform or a sampler in slot 9 fail the build at their .cpp line.
- `gdcore_render_selftest`: 21 / 21 (19 + the two SPIR-V checks) on OpenGL
  3.3 core, OpenGL ES 3.2 (`GD_GLES=1`) and Mesa llvmpipe.
- The hand-over test (`ctest -R gdul_handover`): passes headless
  (offscreen: SDL_GPU's claim is skipped, "This surface does not support
  presenting!", the headless surface); all 17 checks pass on wayland and on
  x11 (SDL_GPU on its Vulkan driver claims the handed-over window and
  presents three frames; a Vulkan surface; the core's OpenGL3 device; the
  launcher's list drawn again afterwards).
- gd01 goldens (the eight shots of phase 1), the 2a build vs the phase-1
  build (4304464): **local 0 differing pixels, opengl3 0 differing pixels,
  in every shot.** gd01's walks: all pass on local; on opengl3 all but
  `enhanced_hdr_frame` (the post-processing is not on the core path yet).
- Packages (`build.sh linux / windows / android / steamdeck --force`): all
  four complete with their 4 game modules; `gdcore_shaderc` was built for
  the Fedora host (also serving the MinGW and NDK cross builds) and inside
  the sniper container, and made `core_renderer_spirv.h` in each.

## 9. What phases 2 and 3 need

Ready now (phase 2a): the SPIR-V of every core and gd01 shader at build
time in every package (section 3), `ShaderDesc::spirv`, the reflection
helper, the window hand-over (section 5), the interface's state rules (2.2),
`Caps::clampToBorder`. No other interface change was needed: everything
below is inside the backends.

- **SDL3 GPU** (`render/src/sdlgpu/`, `GDCORE_RENDER_SDLGPU`):
  - Device: `SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, info.debug,
    nullptr)` then `SDL_ClaimWindowForGPUDevice(dev, info.window)`; the
    destructor waits idle, releases the window, destroys the device.
    `windowFlags(SdlGpu)` stays 0.
  - Shaders: `SDL_CreateGPUShader` from `spirvVertex` / `spirvFragment`
    (`SDL_GPU_SHADERFORMAT_SPIRV`, entry `main`), `num_samplers` /
    `num_uniform_buffers` from `reflectSpirv(...).numSamplers()` /
    `numUniformBuffers()` per stage (`src/common/spirv_reflect.h`).
    `pushUniforms(slot)` goes to `SDL_PushGPUVertexUniformData` when the
    vertex info's `uniformMask` has the slot and to
    `SDL_PushGPUFragmentUniformData` when the fragment's has it.
    `bindTextures` to `SDL_BindGPUFragmentSamplers` / vertex samplers by the
    `samplerMask`s; unused slots below a stage's count get a 1 x 1 dummy.
  - Passes: record a pass's commands on the CPU until `endPass`, then
    upload its transient data in a copy pass and replay the render pass
    (SDL_GPU forbids a copy pass inside a render pass); outside a pass
    `updateBuffer` / `updateTexture` / `copyTexture` / `generateMipmaps` /
    `readTexture` go in copy passes (or `SDL_BlitGPUTexture` /
    `SDL_GenerateMipmapsForGPUTexture`) of the frame's command buffer, in
    call order.
  - Readback: `SDL_DownloadFromGPUTexture` into a download transfer buffer,
    a fence per frame (`SDL_SubmitGPUCommandBufferAndAcquireFence`),
    `readbackReady` = `SDL_QueryGPUFence`. Rows come top-down already.
  - Formats: D24 where `SDL_GPUTextureSupportsFormat(D24_UNORM,
    SAMPLER | DEPTH_STENCIL_TARGET)`, else store D32_FLOAT (the game still
    says D24; pipelines and textures map the same way); BGRA8 native;
    textures with mips and a renderable format get `COLOR_TARGET` too
    (`SDL_GenerateMipmapsForGPUTexture` needs it). `Caps::clampToBorder`
    false (SDL_GPU has no border colour: ClampToBorder clamps to the edge).
  - Present: acquire the swapchain texture at `present`, `SDL_BlitGPUTexture`
    with `SDL_FLIP_VERTICAL` for `flipVertical`; a null swapchain texture
    (minimised) skips it.
  - Tests: `gdcore_render_selftest --backend sdl3` (same checks; the
    SPIR-V is already in its ShaderDescs) on x11 / wayland (offscreen
    cannot present, section 7), then gd01 with `GD01_GRAPHICS_BACKEND=sdl3`.
- **Vulkan** (`render/src/vulkan/`): the same SPIR-V and reflection;
  descriptor set layouts 0..3 as in 3.2 (combined image samplers in 0 / 2,
  dynamic uniform buffers in 1 / 3, one per slot on a per-frame ring);
  **negative-height viewport, front face unchanged** (3.2); volk + VMA;
  dynamic rendering; a pipeline cache; `SDL_Vulkan_CreateSurface` on the
  window (made with `SDL_WINDOW_VULKAN`); then gd01's FSR 3.1 / DLSS
  natively.
- **gd01** (the backend agents and the post-FX port; core_renderer.cpp is
  the post-FX port's file now):
  - `core_renderer_spirv.h` is already generated for gd01 (engine and
    launcher module builds, `gdcore_spirv_shaders` in both CMakeLists.txt):
    in `CoreRenderer::init`, `#include "core_renderer_spirv.h"` and set
    `d.spirv(kMainVsSpirv, kMainFsSpirv)` etc. for the four programs. The
    post-FX programs: add their file to `SOURCES` (or a second call).
  - `app.cpp`: resolve the backend (`startupGraphicsBackend()` /
    `GD01_GRAPHICS_BACKEND`) *before* `SDL_CreateWindow`; for `sdl3` /
    `vulkan` call `gd::core::render::prepareWindow(b, gles)` and make the
    window with `windowFlags(b)` instead of `SDL_WINDOW_OPENGL` (the True
    HDR probe is GL-only: skip it there); if `renderer_.init` then falls
    back to local, destroy the window, `prepareWindow(Backend::OpenGL3,
    gles)`, make it again with `SDL_WINDOW_OPENGL`, and init local on it.
    Nothing else changes for the launcher: the shim does the hand-over.
  - Port `post_fx.cpp` and the True HDR output pass onto the Device; run the
    same goldens per backend (expect small differences off OpenGL: other
    rasteriser tie rules and precision).

## 10. Adding a backend

1. `render/src/<backend>/`: a class implementing `Device`; register it in
   `src/device.cpp`'s factory under its id; `backendCompiled` true where it
   builds.
2. Pass `gdcore_render_selftest --backend <id>` (same pixels as opengl3).
3. gd01: nothing changes except removing the id from the "not available
   yet" list; run the goldens against `local`.
