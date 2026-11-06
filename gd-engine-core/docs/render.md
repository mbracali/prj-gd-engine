# gd-engine-core: render

The core's graphics layer: one interface, `gd::core::render::Device`
(`render/include/gd/core/render/device.h`), and its backends. The
**OpenGL 3.3 / OpenGL ES 3.0** backend (`opengl3`) exists since 1.1.13
(GDCORE-004 phase 1), the **SDL3 GPU** backend (`sdl3`, phase 3a, section
4.2) and the **Vulkan** backend (`vulkan`, phase 3b, section 4.3) since then,
on the same interface, on the groundwork of phase 2a (the build-time SPIR-V,
section 3; the launcher's window hand-over, section 5; the state rules, 2.2).
This file is the design: read it before adding a backend or moving a game
onto the core. Facts are marked [C] confirmed or
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
    bindTextures(slot, {texture, sampler}...)           combined texture + sampler slots 0..15
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
- **Textures and samplers bind as pairs** in slots 0..15 (`kMaxTextureSlots`),
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
platform without one (OpenGL3 on macOS later, Vulkan on Apple until MoltenVK
ships) the game does not offer it (rule 7). A compiled backend can still fail
to start (Vulkan without a loader or a device): `createDevice` says why and
the game falls back.

`DeviceCreateInfo::cacheDir` (optional): a folder the backend may keep caches
in across runs, the game's pref folder (Vulkan: `vulkan_pipeline_cache.bin`,
used only when its header names this very GPU and driver). Empty: nothing on
disk.

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
- **Textures are combined samplers** in slots 0..15 (`kMaxTextureSlots`). No
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

### 4.2 SDL3 GPU (`sdl3`, 1.1.13 phase 3a) — how the interface maps

| Interface | SDL_GPU |
|---|---|
| `beginFrame` / `endFrame` | acquire a command buffer; submit with a fence; `SDL_WaitForGPUFences` for frame N-2 |
| `beginPass(PassDesc)` | `SDL_BeginGPURenderPass` with `SDL_GPUColorTargetInfo` (load / store / resolve) and the depth-stencil info |
| transient data, `updateBuffer`, `updateTexture` | transfer buffers (cycled) + a copy pass recorded **before** the render pass (the backend records a pass's transient uploads, then replays) |
| `pushUniforms(slot)` | `SDL_PushGPUVertexUniformData` + `SDL_PushGPUFragmentUniformData` for the stages whose reflection uses the slot |
| `bindTextures` | `SDL_BindGPUFragmentSamplers` / `SDL_BindGPUVertexSamplers` |
| `createPipeline` | `SDL_CreateGPUGraphicsPipeline` (the target formats are in `PipelineDesc`) |
| `copyTexture` / `blit` | `SDL_CopyGPUTextureToTexture` / `SDL_BlitGPUTexture` |
| `generateMipmaps` | `SDL_BlitGPUTexture` level by level (not `SDL_GenerateMipmapsForGPUTexture`: see below) |
| `readTexture` | `SDL_DownloadFromGPUTexture` into a download transfer buffer + fence |
| `present` | `SDL_AcquireGPUSwapchainTexture` + `SDL_BlitGPUTexture` |
| shaders | SPIR-V from the build step (3.3; the Vulkan driver); MSL (Metal), DXIL (D3D12) later from the same SPIR-V |
| shader resource counts | `reflectSpirv` (`src/common/spirv_reflect.h`) |
| depth bias, cull, front face | `SDL_GPURasterizerState` (front face as given: SDL flips the viewport itself) |
| `Wrap::ClampToBorder` | none in SDL_GPU: clamp to edge, `Caps::clampToBorder` false |
| D24 / D24S8 | where `SDL_GPUTextureSupportsFormat` says so, else D32F / D32FS8 behind the same `Format` |

How `render/src/sdlgpu/sdlgpu_device.cpp` does it (the choices that are not
in the table):

- **Passes are recorded, then replayed.** `beginPass` .. `endPass` record
  the pass's commands on the CPU (state, bindings, uniform data, draws);
  `endPass` uploads the transient data allocated so far in one copy pass,
  begins the SDL render pass and replays them. A pass that is never ended
  is replayed by the next `beginPass` / `present` / `endFrame`.
- **One command buffer at a time, in call order.** Everything outside a
  pass (`updateBuffer`, `updateTexture`, `copyTexture`, `blit`,
  `generateMipmaps`, `readTexture`) is recorded into the current command
  buffer when it is called, so the GPU sees the calls' order (a draw
  recorded before an update sees the old contents; uploads to buffers and
  textures never cycle them). `present` submits it (the swapchain texture
  is acquired on it), and `endFrame` submits what is left; each submission
  has a fence, and `beginFrame` waits for the last one of frame N - 2.
- **Uploads** go through one upload transfer buffer used as an arena: it is
  cycled (`SDL_MapGPUTransferBuffer(..., true)`) at its first use in each
  command buffer and filled behind the copies already recorded after that,
  and made bigger when it is full. Transient vertex / index data lives in a
  per-frame ring of GPU buffers (three slots, as the GL backend's).
- **Uniforms**: SDL_GPU keeps pushed data per command buffer and stage. The
  device keeps each slot's last data (2.2: pushed uniforms stay) and pushes
  it, at the draw, to every stage whose reflection uses the slot, once per
  command buffer and again when it changes. SDL's Vulkan driver binds 4 KiB
  per slot [C: SDL 3.4.18 `MAX_UBO_SECTION_SIZE`]: a larger push is cut to
  4096 bytes with a log line (no core or gd01 block is near it).
- **Readback**: a download transfer buffer per `readTexture`, recorded in
  the current command buffer; `readbackReady` submits that command buffer
  if it still holds the copy (a poll must make progress), then queries the
  fence; `readbackData` waits for it.
- **Mipmaps** are generated level by level with linear `SDL_BlitGPUTexture`
  blits (each level half the one above, at least 1 x 1), not with
  `SDL_GenerateMipmapsForGPUTexture`: SDL 3.4.18's Vulkan driver sizes
  level n as `width >> n` without clamping to 1, so a non-square texture's
  last levels get zero-sized blits and stay undefined [C:
  `SDL_gpu_vulkan.c` `VULKAN_GenerateMipmaps`; found by the goldens: gd01's
  drum lugs, small and far, sampled those levels as transparent]. The
  self-test checks a 32 x 4 chain on every backend.
- **Textures**: every renderable colour texture also gets `COLOR_TARGET`
  (a blit's destination, the mipmap blits).
  Unused sampler slots below a stage's highest get a 1 x 1 opaque black
  texture (what GL samples from an empty unit). `MipFilter::None` is GL's
  non-mipmapped minification: nearest mip mode with `max_lod` 0.25 (level 0
  only, minification and magnification still told apart).
- **Front face as given, no y flip** (3.2): SDL_GPU's images are top-down
  natively, so `present(..., flipVertical)` is a blit with or without
  `SDL_FLIP_VERTICAL`.
- **Window**: `SDL_ClaimWindowForGPUDevice` at creation (it fails on the
  `offscreen` video driver, "This surface does not support presenting!",
  so the game falls back), `SDL_ReleaseWindowFromGPUDevice` in the
  destructor (the launcher can then make the window again for another API).
  Present modes: Mailbox, else Immediate, else Vsync, as the window allows.
- **True HDR** is not on this backend yet (the SDR swapchain only): gd01
  runs True HDR as Emulated on it and says so (section 6). SDL_GPU's
  `HDR_EXTENDED_LINEAR` / `HDR10_ST2084` swapchain compositions are the way
  to add it (an interface call to choose the surface; untestable here
  without an HDR display).

### 4.3 Vulkan (`vulkan`, phase 3b, done)

`render/src/vulkan/` (`GDCORE_RENDER_VULKAN`, on everywhere but Apple and the
web). Vulkan 1.1 or newer (1.3 used where the loader and the device have it).

| Interface | Vulkan |
|---|---|
| loading | volk on the loader SDL opens (`SDL_Vulkan_LoadLibrary`, `SDL_Vulkan_GetVkGetInstanceProcAddr`): no Vulkan library at link time; Android: the NDK's `libvulkan.so` the same way. volk is compiled in the C++ namespace `volk` (`VOLK_NAMESPACE`), so gd01's own volk (extern "C", for its upscalers) never clashes with it |
| device | the instance with SDL's surface extensions (+ `VK_EXT_debug_utils`, + portability enumeration where present); the GPU: discrete > integrated > virtual > CPU, one that presents to the window's surface, Vulkan 1.1+ (`GDCORE_VK_DEVICE=<index or part of the name>` prefers one: a development aid); one graphics queue; features `samplerAnisotropy`, `depthClamp`, `textureCompressionBC`, `independentBlend` where supported; memory: VMA 3.3.0 |
| `beginPass` / `endPass` | dynamic rendering (core 1.3, or `VK_KHR_dynamic_rendering` on 1.1 / 1.2); else render passes made per (formats, samples, load / store ops, resolves) and framebuffers per views, both cached (`GDCORE_VK_RENDER_PASSES=1` forces this path for testing). Pipelines are made against a render pass of the same formats and samples: a single-subpass pass ignores resolves for compatibility [C: Vulkan spec, render pass compatibility] |
| image layouts | every image **rests** in one layout: sampled ones `SHADER_READ_ONLY_OPTIMAL`, others their attachment layout. A pass, copy, blit, upload, mip generation, readback or present moves the subresources it touches out and back with a full barrier (ALL_COMMANDS, memory read / write). Clear / DontCare loads come from `UNDEFINED`. Simple and always valid; gd01 breaks passes only a few times per frame |
| frames | one primary command buffer records in call order; each submission has a serial and a fence; a frame slot (`kFramesInFlight` = 2) waits for its previous use before its command pool, upload ring and descriptor pools are reset (so transient data lives until the frame after next starts, 2.1). New images get their first layout in a command buffer submitted just before the current one (a texture may be created inside a pass) |
| transient data, `pushUniforms`, staging | one persistently mapped, host-coherent ring per frame slot (4 MiB chunks, bigger one-off chunks for big uploads, dropped after use) |
| `updateBuffer` / `updateTexture` | staging in the ring + `vkCmdCopyBuffer` / `vkCmdCopyBufferToImage` in call order (draws recorded before see the old contents). Refused inside a pass (2.2) |
| `pushUniforms(slot)` | the data in the ring; sets 1 and 3 are one descriptor set of 4 `UNIFORM_BUFFER_DYNAMIC` bindings (range 16 KiB, the ring keeps that much room after each block) and the slot's dynamic offset moves. Pushed slots are reset at `beginFrame` (an empty block is bound) |
| `bindTextures` | sets 0 and 2: one descriptor set of 16 combined image samplers (both stages: the interface's slots are shared), made on change and cached per frame slot by (texture uid, sampler); unbound slots get a 1 x 1 texture. The set is visible to both stages and bound at sets 0 and 2, so a pipeline layout counts 32 samplers per stage: within every desktop driver's `maxPerStageDescriptorSamplers`, above the spec's minimum of 16 (some older mobile drivers: a vertex-only and a fragment-only layout would bring it back to 16) |
| shaders | SPIR-V only (`ShaderDesc::spirv*`), checked with `reflectSpirv`; one pipeline layout (sets 0..3 as 3.2) for every pipeline |
| pipelines | immutable, dynamic viewport / scissor / stencil reference / blend constants; depth written with the test off = test `Always` (as GL); without `independentBlend` every target uses target 0's blend (as GLES); the pipeline cache on disk (`cacheDir`) |
| y and the front face | the viewport's height is negative (`y = top + h`, core 1.1); the front face goes through unchanged (3.2); the self-test's cull check passes |
| depth | D24 = `X8_D24_UNORM_PACK32` where it can be sampled and rendered, else D32F; D24S8 = `D24_UNORM_S8_UINT` else D32FS8 (AMD); depth targets are always sampleable; a sampled depth + stencil view sees depth |
| samplers | GL's non-mipmapped minification = mip mode nearest with LOD 0..0.25; `ClampToBorder` with the three standard border colours (`Caps::clampToBorder` true); anisotropy where the feature is on |
| MSAA | images with the sample count (clamped to what colour and depth targets both support); `StoreOp::Resolve` = an average resolve into the resolve texture |
| `copyTexture` / `blit` / `generateMipmaps` | `vkCmdCopyImage` / `vkCmdBlitImage` (linear only where the format filters) / a chain of blits |
| `readTexture` | `vkCmdCopyImageToBuffer` into a host buffer, **submitted at once** (GL's flush after its pixel-pack read): `readbackReady` polls the fence; rows top-down already |
| `present` | the swapchain (B8G8R8A8 / R8G8B8A8 UNORM, sRGB non-linear space: values go to the screen as on GL's default framebuffer) is acquired with a 1 s timeout (a window that is not shown skips the frame instead of hanging), the texture blitted into it (`flipVertical` = reversed y offsets), submitted and presented at once; resized / out of date: made again (waiting for the GPU). `PresentMode` Vsync = FIFO, Immediate = IMMEDIATE else MAILBOX, Mailbox = MAILBOX else IMMEDIATE |
| debugging | `pushDebugGroup` = debug utils labels; object names from `label`s |

**Validation.** `VK_LAYER_KHRONOS_validation` is enabled in debug builds,
with `DeviceCreateInfo::debug`, or with `GDCORE_VK_VALIDATION=1` (`=0` turns
it off in a debug build), when the layer is installed; its messages go to the
log, and the device logs how many there were when it is destroyed.

**Without a window to present to.** The surface is made only on a window
made with `SDL_WINDOW_VULKAN` (SDL would convert an OpenGL window in place,
section 5). Without a surface, or with one no GPU presents to (the
`offscreen` video driver's headless surface on NVIDIA), the device draws off
screen and `present` does nothing, **only** under `SDL_VIDEO_DRIVER=offscreen`
or with `GDCORE_VK_HEADLESS=1` (no surface at all); otherwise `createDevice`
fails, so a game never gets a device that shows nothing (rule 2: it falls
back). This is what lets the self-test and the goldens run headless (CI).

**Next (not done):** gd01's FSR 3.1 / DLSS natively on this device (section
9), an HDR swapchain for True HDR (HDR10 / scRGB colour space; today the
swapchain is 8-bit SDR and True HDR shows as its SDR blit), MoltenVK /
KosmicKrisp on Apple.

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
development aid `GD01_GRAPHICS_BACKEND=<id>` overrides it). Every core
backend runs gd01 (`opengl3`, `sdl3`, `vulkan`); a backend this build does
not have (Vulkan on Apple) logs `graphics backend: vulkan: not available
yet; using local` and runs local. `CoreRenderer::init` passes gd01's pref
folder as `DeviceCreateInfo::cacheDir` (the Vulkan pipeline cache).

**The window (sdl3, vulkan).** `App::run` resolves the backend *before*
making its window: for `sdl3` / `vulkan` it calls `prepareWindow(b, gles)`
and makes the window with `windowFlags(b)` instead of `SDL_WINDOW_OPENGL`
(in the launcher the module shim hands the window over, section 5), and
skips the True HDR probe (an OpenGL context on the window). If the device
then does not start (no SDL_GPU / Vulkan driver, the `offscreen` video
driver, a launcher that cannot hand the window over), `Renderer::init`
logs why and returns false on that non-OpenGL window; `App` logs
`graphics backend: sdl3: making the window again for OpenGL (local)`,
destroys the window, calls `prepareWindow(OpenGL3)`, makes it again with
`SDL_WINDOW_OPENGL` and starts local on it. `local` and `opengl3` make the
window exactly as before.

**Shaders.** `CoreRenderer::init` and `CorePostFx::init` give every
program its build-time SPIR-V next to its dialect text: the four renderer
programs and the post-processing's (vertex `kPostVs`, 15 fragment shaders)
from `core_renderer_spirv.h` (`DEFINES GD_HDR=0`), the three HDR variants
(composite, finish, FSR RCAS with `GD_HDR 1`) from `core_post_fx_hdr_spirv.h`
(a second `gdcore_spirv_shaders` call: `NAMESPACE gd01_hdr DEFINES
GD_HDR=1`), in gd01's and in the launcher module's CMakeLists.txt.

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

**Enhanced post-processing and True HDR (1.1.13, after phase 1).**
`render/core_post_fx.{h,cpp}` (`CorePostFx`) is local's `PostFx` and
`outputHdr` on the Device, pass for pass: the same 17 programs plus the
True HDR output pass, in the dialect (the loose uniforms become one std140
block `Post` whose members keep their names, so every function body is the
same text as local's: `gd-game-01/tools/check_core_shaders.py` compares
them), the same targets and formats (RGBA8 / RGBA16F, the luminance map's
mip chain), the same samplers (local's per-texture parameters as sampler
objects, including the HDR frame's never-set REPEAT wrap), the same uniform
values and order. How it fits CoreRenderer:

- `beginScene` makes the scene target (RGBA8, or RGBA16F with HDR, + a
  sampled D24 depth) and makes it "target 0" until `endScene` (every draw,
  copy, depth probe and DOF pass goes to it, as local's bound scene FBO);
  the clear is the pass's load op; pipelines carry the target's format
  (RGBA16F scenes and frames get their own pipelines);
- TAA's jitter is the main shader's `uUvOffsetJitter.zw`, with y negated as
  the MVP's y (exact; impostor targets get 0, as local);
- `endScene` ends the pass, runs the chain (each step a pass with
  `DontCare` load over the whole target, fullscreen triangle whose y is
  negated like `setMvp`'s, so on OpenGL3 it rasterises as local's), then
  draws the result into the frame's viewport (RGB only, as local's mask);
- copies from an RGBA16F scene or frame into RGBA8 textures (DOF, track
  fade) are 1:1 `blit`s (`copyTexture` needs equal formats), the same
  float-to-unorm conversion as local's `glCopyTexSubImage2D`;
- True HDR: the frame is RGBA16F when the window got an HDR surface;
  `endFrame` converts it into an RGBA32F texture at the window's size
  (local writes the shader's floats straight to the surface; a 16-bit
  intermediate would round twice) and presents it 1:1. `readPixels` blits
  the frame to RGBA8 first (local's `glReadPixels` conversion).

**Not on the core path yet:** the FSR 3.1 / DLSS upscalers (`vk_upscaler.*`:
they share GL textures with a Vulkan device). With one selected the chain
does what local does when it cannot run them: FSR 1 in their place, no
jitter, and `ai upscaling: FSR 3.1 is not on the opengl3 graphics backend
yet - using FSR 1` (`sdl3` on SDL3 GPU) in the log and in the Enhanced
menu's status. They become native on the Vulkan backend (phase 3). **True
HDR on sdl3** runs as Emulated (no HDR swapchain yet, 4.2): `hdr:
unavailable (True HDR is not on the sdl3 graphics backend yet), using
Emulated`.

## 6b. gd-game-02 on the core (GDCORE-008)

gd02 already drew through its own device interface, `gfx::RenderDevice`
(gd-game-02/engine/src/render/device.h: std140 blocks at fixed bindings,
fixed texture slots, a `DrawCall` carrying its whole state). So the core
comes in **under** that interface, not beside the renderer as in gd01: a
second implementation, `CoreDevice` (render/core_device.cpp), on
`gd::core::render::Device`; `local` keeps `gl_device.cpp` untouched, and no
caller changes. The backend is read once at start
(render/core_backend.cpp: `GD02_GRAPHICS_BACKEND`, else enhanced.json's
`graphics_backend`, else `local`; `Renderer::init` makes the device for the
backend `App` made the window for).

**The window**, as gd01 (section 6): sdl3 / vulkan call the core's
`prepareWindow` and make the window with `windowFlags(b)`; if the device
does not start, `App` logs `graphics backend: vulkan did not start (...);
using local`, makes the window again for OpenGL (the launcher's shim gets
its OpenGL window back) and starts local. opengl3 keeps the OpenGL window
(on failure local runs on the same window). In a launcher module the
OpenGL3 device takes the module's (shimmed) context calls (`GlHooks`).

How gd02's GL-shaped calls map:

- **Pictures stored as local's** (GL style, row 0 at the bottom): every
  vertex shader is wrapped (`#define main gd02_vertex_main` + a `main` that
  calls it) and ends with `gl_Position = gd_clip(vec4(x, -y, z, w))`. On
  opengl3 the core's own flip cancels the negation, so the rasteriser sees
  exactly local's coordinates; gd02's top-left viewports become `y = height
  - y - h`, the culled winding is mirrored (`Cull::Clockwise` = cull back
  with front face counter-clockwise), the frame is presented with
  `flipVertical` and `readPixels` flips its rows: local's own conversions.
- **Pipelines** are made on first use and cached per (program, state,
  vertex layout, primitive, target format, depth). Local's blend is
  `glBlendFuncSeparate(src, dst, ONE, ONE_MINUS_SRC_ALPHA)` with one
  equation; GL writes no depth while its test is off (so neither does the
  pipeline).
- **Passes**: `setTarget` only notes the target; the pass begins at the
  first draw. A clear of the whole target before any draw is the pass's
  load op (made even when the target changes before a draw), any other
  clear a quad inside the viewport (local's `glClear` is scissored to it).
- **The frame**: always an RGBA8 + D24 target (the window's size or the
  internal resolution's), presented scaled with linear filtering, as local's
  blit.
- **Resource updates** inside a pass end it (the next draw loads the
  target). gd02's dynamic buffers (the canvas, particles, lights, the
  video quad) live in the frame's transient memory: each `updateBuffer` is
  a new slice, a draw binds the newest slice holding its offset (GL's
  rename: earlier draws keep the old data).
- **Textures**: unbound slots a program declares get a 1 x 1 white texture
  (local's), BCn sampled natively where `caps()` says so, else decoded on
  the CPU as local does. GH2's standard shader binds 13 slots: the core's
  `kMaxTextureSlots` went from 8 to 16 for it (every backend allows 16).
- **Shaders: SPIR-V at run time.** GH2 builds its standard shader per
  material key (31 switch bits; the keys a disc uses are known once it is
  loaded), so the build-time step (section 3.3) cannot list them. For sdl3
  and vulkan gd02 compiles each program when it is made
  (render/spirv_compile.cpp) with glslang, the release this file pins for
  gdcore_shaderc (GLSL front end and SPIR-V back end only; with the core's
  backends the Linux program grows from 7.3 to 11.3 MB): each stage is preprocessed with the program's `#define`s,
  then given SDL_GPU's layout from the names gd02 already passes (blocks
  by `uniformBlocks` slot in sets 1 / 3, samplers by `samplers` slot in sets
  0 / 2, varyings located by name in the vertex stage's order) and compiled
  as gdcore_shaderc compiles the dialect (SPIR-V 1.0, Vulkan 1.0, not
  optimised). This is the "unless a backend truly needs it" of rule 8;
  `GD02_RUNTIME_SPIRV=OFF` builds without it (sdl3 / vulkan then fall back).
- **Not on the core path**: True HDR (the HDR surface probe is local's
  OpenGL one; on any core backend True HDR runs as Emulated and says so),
  the frame pacing is local's (vsync where the rate matches, else software).

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
  `--backend vulkan` runs the same checks on the Vulkan backend (plus,
  for every backend, a cull check: a counter-clockwise triangle is the front
  face). Headless under `SDL_VIDEO_DRIVER=offscreen` (no swapchain where the
  GPU cannot present to the headless surface); exit 77 when Vulkan cannot
  start (ctest `gdcore_render_selftest_vulkan` counts it as skipped).
  Development aids: `GDCORE_VK_DEVICE=<index or name part>` (e.g.
  `llvmpipe`), `GDCORE_VK_RENDER_PASSES=1` (the render-pass path),
  `GDCORE_VK_HEADLESS=1` (no surface: any GPU off screen),
  `GDCORE_VK_VALIDATION=1`.
  `--backend sdl3` runs them on SDL3 GPU; it needs a window SDL_GPU can
  present to, so it runs on `x11` / `wayland` and exits 77 (skipped) on
  `offscreen`, where SDL_GPU cannot claim the window (ctest
  `gdcore_render_selftest_sdl3` takes the environment's video driver).
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
  (`GD01_GRAPHICS_BACKEND=opengl3`); on `sdl3` under a display server (the
  walks are written for `offscreen`, where sdl3 falls back to local: run a
  copy of `tools/walks.sh` with the video driver of the display server).
- **A display server that shows nothing**: `kwin_wayland --virtual
  --no-lockscreen --socket <name> --width 1920 --height 1080
  --exit-with-session <script>` is a real Wayland compositor rendering
  into memory; SDL_GPU and the Vulkan backend present to it on the NVIDIA
  GPU, so sdl3's goldens, walks and the launcher run there without a
  window on the desktop (`WAYLAND_DISPLAY=<name> SDL_VIDEO_DRIVER=wayland`
  in the script).
- gd01 goldens: screenshots of the main menu, songs in three venues, the
  highway with Star Power, results, Enhanced menu; before / after on
  `local` must be identical, `opengl3` against `local` within a tiny
  tolerance. Reproducible headless frames need a fixed clock: the harness
  preloads a test-only library that makes `SDL_GetTicks*` advance 1/60 s per
  presented frame and `std::random_device` return a fixed sequence (it is
  not part of any build). A presented frame is a `SDL_GL_SwapWindow`, a
  `SDL_WaitAndAcquireGPUSwapchainTexture` (sdl3) or a `vkQueuePresentKHR`
  (vulkan: the library wraps the `vkGetInstanceProcAddr` that
  `SDL_Vulkan_GetVkGetInstanceProcAddr` hands out); a backend whose present
  it does not count freezes the clock (the Enhanced menu's wall-clock shot
  never comes, the post-processing's time-based effects stand still).
  Results: section 8.

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

**Post-processing on the core (CorePostFx), same machine:** 12 Enhanced
settings files (the Modes Original Feel, Steam Deck, Clean Image, Max Out
(Full HD), Max Out (4K), Rocking on a Budget, Good Old Days, and five mixes
for what no Mode sets: SSAA, FSR 1 balanced / performance, every colour
grade, HDR None / Emulated / True, SSAO, sharpening, grain, chromatic
aberration, Star Power blur at every strength), 3 shots each (Enhanced
menu with its venue backdrop, small_club at 30 s, Star Power in big_club at
183 s): **before (4304464) vs after on local: 0 differing pixels in all 36;
local vs opengl3: 0 differing pixels in all 36** (no tolerance was needed:
the dialect shaders compile to the same code, and the double y flip keeps
the rasteriser, `gl_FragCoord` and texel rows those of local). Not covered:
True HDR with a real HDR surface (offscreen has none: True HDR runs as
Emulated there, which is covered; the RGBA16F frame, the output pass and
the HDR readback are untested on a display), FSR 3.1 / DLSS (by design
FSR 1 on the core; checked that it logs and draws), OpenGL ES.

### Phase 2a (the groundwork for SDL3 GPU and Vulkan)

2026-11-06, same machine:

- `gdcore_shaderc` (glslang + SPIRV-Tools, Vulkan SDK 1.4.321.0) built for
  the host in under a minute; every shader of the core's self-test (6) and
  of gd01's CoreRenderer (8: main, fill, depth of field, depth probe)
  compiles to SPIR-V that passes `spirv-val` (Vulkan 1.0) and the dialect's
  binding rules, with **no change to any shader**: the dialect was already
  valid Vulkan GLSL 450 (the [I] of phase 1 is now [C]). A broken shader, a
  loose uniform or a sampler in slot 16 fail the build at their .cpp line.
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

### Phase 3b (Vulkan)

2026-11-06, Linux (Fedora, kernel 7.2), NVIDIA RTX 3060 Laptop (driver
615.71, Vulkan 1.4.351) + AMD Renoir iGPU (RADV, Mesa) + llvmpipe:

- `gdcore_render_selftest --backend vulkan`: **22 / 22** (the 21 checks + the
  new cull check) on NVIDIA, RADV and llvmpipe, each with dynamic rendering
  and with the render-pass path (`GDCORE_VK_RENDER_PASSES=1`), on `wayland`,
  `x11` and headless `offscreen` (there RADV presents to the headless
  surface; NVIDIA cannot, so it runs with `GDCORE_VK_HEADLESS=1`). opengl3
  still 22 / 22. **The validation layer is not installed on this machine**
  (`/usr/share/vulkan/explicit_layer.d` is empty; building it locally was
  not allowed): nothing has run under `VK_LAYER_KHRONOS_validation` yet.
- gd01 on Vulkan, before the shared wiring landed: a throwaway copy of gd01
  with its ShaderDescs given their SPIR-V (and, for the windowed run only,
  its window made with `SDL_WINDOW_VULKAN`), never committed. Windowed on
  wayland and on x11: the song plays, the frame is presented the right way
  up through the swapchain (checked on screen captures). Goldens headless
  (`offscreen`, the device draws off screen and the shots are readbacks),
  with a clock that advances once per drained event loop (the harness's
  clock counted `SDL_GL_SwapWindow`, which Vulkan never calls), vulkan vs
  opengl3 of the same binary:

  | shot | differing px | max delta |
  |---|---|---|
  | main menu | 0 | 0 |
  | results | 0 | 0 |
  | Enhanced menu | 18 (0.001 %) | 109 |
  | small_club 30 s | 301 (0.015 %) | 226 |
  | theatre 45 s | 284 (0.014 %) | 216 |
  | arena 60 s | 283 (0.014 %) | 228 |
  | Star Power big_club 183 s | 262 (0.013 %) | 231 |
  | basement 4:3 50 s | 528 (0.045 %) | 208 |

  The differences are the fret sparks' sliver triangles and single pixels
  at mesh seams: the rasteriser's tie rules see the picture mirrored (the
  viewport flip), as 3.2 expected; the 2D UI is bit-exact.
  Post-processing (6 of the 12 settings files, 3 shots each): Steam Deck
  0.02-0.24 %, True HDR neon (Emulated headless) 0.5-0.9 %, SSAA warm with
  high sharpening 2.6-4.4 % (max 56 off the sparks); with **film grain** on
  (Max Out HD, Vintage TAA, FSR 1 noir) 18-33 %: the grain hash is
  `fract(sin(x) * 43758.5)`, whose `sin` of large arguments differs between
  NVIDIA's GLSL and SPIR-V compilers (random noise either way). The same
  files without grain: FSR 1 noir 0.01-0.02 %, Max Out HD (TAA, SSAO,
  bloom) 3-5 % at most 8 levels off. No backend bug found.
- Packages (`build.sh linux / windows / android --force`, and Steam Deck in
  the sniper container): all four complete with their 4 game modules; the
  Linux, Windows and Android ones carry the volk, VMA and Vulkan-Headers
  licences (`licenses/gd-game-01-*`).

### gd-game-02 (GDCORE-008)

2026-11-06, Linux, NVIDIA RTX 3060 (driver 615.71). Goldens at the default
1920 x 1080 window: the main menu, the Enhanced menu, Yyz in the theatre
(30 s), in the stone venue (45 s), Star Power in the arena (deployed at
58 s, shot at 61 s). Reproducible frames need the harness's fixed clock and,
for gd02, no audio callback (the song clock otherwise follows the audio
thread's real-time pace): two runs of local give 0 differing pixels in
every shot but the Enhanced menu (its backdrop is loaded in the background:
two states, 13.7 % apart), which is left out below.

- **Before (ef483d0) vs after, local: 0 differing pixels** in the four.
- **local vs opengl3: 0 differing pixels** in the four, headless
  (offscreen) and under the virtual KWin.
- local vs **sdl3** and local vs **vulkan** (KWin, both on the NVIDIA GPU):
  main menu 0; theatre 3.4 % of the pixels (mean |delta| 0.07, max 57),
  stone 0.8 % (0.016, max 208), Star Power arena 1.2 % (0.027, max 176):
  single pixels at mesh edges and in the post-processing's blurs, nothing
  visible side by side (the double y flip does not hold there: the
  rasteriser's tie rules see the picture mirrored, as gd01's 3b results).
  **sdl3 vs vulkan: 0 differing pixels** in every shot (the same SPIR-V on
  the same driver). Headless, vulkan also draws on the AMD iGPU (RADV).

## 9. What phases 2 and 3 need

Ready now (phase 2a): the SPIR-V of every core and gd01 shader at build
time in every package (section 3), `ShaderDesc::spirv`, the reflection
helper, the window hand-over (section 5), the interface's state rules (2.2),
`Caps::clampToBorder`. No other interface change was needed: everything
below is inside the backends.

- **SDL3 GPU** (`render/src/sdlgpu/`, `GDCORE_RENDER_SDLGPU`): **done
  (phase 3a, section 4.2)**. Follow-ups:
  - **True HDR**: SDL_GPU's `HDR_EXTENDED_LINEAR` (RGBA16F scRGB) /
    `HDR10_ST2084` swapchain compositions, chosen through a Device call
    from the game's request (`SDL_WindowSupportsGPUSwapchainComposition`),
    and gd01's True HDR probe for it; today True HDR runs as Emulated on
    sdl3 and says so. Needs an HDR display to test.
  - **Metal / D3D12**: MSL and DXIL from the same SPIR-V at build time
    (3.3); until then SDL picks its Vulkan driver (none on macOS without
    MoltenVK: the game falls back to OpenGL).
  - The FSR 3.1 / DLSS upscalers stay FSR 1 on sdl3 (SDL_GPU exposes no
    native Vulkan objects for them; the vulkan backend is their home).
  - Not run under `VK_LAYER_KHRONOS_validation` (not installed here) nor
    with `SDL_GPU`'s debug mode: once where it is.
- **Vulkan** (`render/src/vulkan/`): **done (phase 3b, section 4.3)**. gd01
  needs only the shared wiring above (SPIR-V in its ShaderDescs, the window
  made with `windowFlags(Backend::Vulkan)`, and `DeviceCreateInfo::cacheDir`
  = its pref folder for the pipeline cache). Follow-ups:
  - **gd01's FSR 3.1 / DLSS natively** (today FSR 1 in their place on every
    core backend). What it needs: (1) an interop call on the Device that
    hands out the native objects (`VkInstance`, `VkPhysicalDevice`,
    `VkDevice`, the queue and its family, `vkGetDeviceProcAddr`, the
    command buffer being recorded outside a pass, and per texture its
    `VkImage` / `VkImageView` / format / resting layout), with the rule that
    the caller leaves every image in its resting layout; (2) extra instance
    and device extensions and features chosen *before* creation
    (`DeviceCreateInfo` lists: DLSS asks NGX for them,
    `NVSDK_NGX_VULKAN_RequiredExtensions`; FidelityFX's Vulkan backend wants
    its own feature list, e.g. 16-bit storage / subgroup ops where it uses
    them), so the core must create its instance and device with them; (3)
    gd01's `vk_upscaler` rewritten from its GL-interop device (exported
    memory, semaphores) to that interop: its FidelityFX backend already
    takes `vkGetDeviceProcAddr`, so the two volk copies stay apart; (4) the
    upscalers' inputs as core textures: colour, depth (D24 is
    `X8_D24_UNORM_PACK32` or D32F here), motion vectors (gd01 has none:
    FSR 3.1 runs with zero motion as now) and the jitter; (5) an output
    texture with storage usage (`TextureUsage` gains a storage bit for
    this backend only, or the interop makes it).
  - **True HDR**: an HDR swapchain (`VK_EXT_swapchain_colorspace`:
    HDR10 ST.2084 or extended sRGB linear with an RGBA16F / RGB10A2 image)
    chosen from the game's request, and gd01's True HDR probe for Vulkan.
  - **Apple**: MoltenVK / KosmicKrisp in the packages, then
    `GDCORE_RENDER_VULKAN` on there (portability enumeration and
    `VK_KHR_portability_subset` are already handled).
  - The validation layer was not installed on the development machine
    (section 8): run the self-test and gd01 once with
    `GDCORE_VK_VALIDATION=1` where it is.
- **gd01**: **done** (the shared wiring, section 6): the backend is
  resolved before the window, `prepareWindow` + `windowFlags` for sdl3 /
  vulkan, the OpenGL fallback that makes the window again, every program's
  SPIR-V (the post-processing's HDR variants from a second
  `gdcore_spirv_shaders` call), `cacheDir` = the pref folder. The goldens
  per backend: section 8.

## 10. Adding a backend

1. `render/src/<backend>/`: a class implementing `Device`; register it in
   `src/device.cpp`'s factory under its id; `backendCompiled` true where it
   builds.
2. Pass `gdcore_render_selftest --backend <id>` (same pixels as opengl3).
3. gd01: nothing changes except removing the id from the "not available
   yet" list; run the goldens against `local`.
