# gd-engine-core: render

The core's graphics layer: one interface, `gd::core::render::Device`
(`render/include/gd/core/render/device.h`), and its backends. The
**OpenGL 3.3 / OpenGL ES 3.0** backend (`opengl3`) exists now (1.1.13,
GDCORE-004 phase 1); **SDL3 GPU** (`sdl3`) and **Vulkan** (`vulkan`) come next,
on the same interface. This file is the design: read it before adding a
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

### 2.3 Conventions (the part that differs most between APIs)

- **Clip space is OpenGL's**: y up, depth -1..1 (`gl_Position` as GL
  computes it). Shaders write `gl_Position = gd_clip(p);`; on 0..1 backends
  `gd_clip` remaps z (`z = (z + w) / 2`) and, on Vulkan, flips y. Games keep
  their matrices. Window depth (depth buffer values, depth range) is 0..1
  everywhere.
- **Images are top-down**: row 0 of an uploaded texture, of a render target,
  of a readback, and y = 0 of a viewport or scissor rectangle are the **top**
  of the image. Texture coordinate (0, 0) is the top-left texel of what was
  uploaded *and* of what was rendered. In fragment shaders `gl_FragCoord`
  counts from the top-left. This is SDL_GPU's and Vulkan's (with the y flip)
  natural convention; the GL backend gets it by rendering every target
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
`#version 450` (Vulkan GLSL), with a few macros where those differ:

| Macro | OpenGL 3.3 / ES 3.0 | Vulkan GLSL 450 (SPIR-V for SDL_GPU and Vulkan) |
|---|---|---|
| `GD_UNIFORMS(slot, Name)` | `layout(std140) uniform Name` (bound to binding `slot` by name after linking) | `layout(std140, set = 1 (vertex) / 3 (fragment), binding = slot) uniform Name` |
| `GD_SAMPLER2D(slot, name)` / `GD_SAMPLERCUBE` | `uniform sampler2D name;` (unit `slot` set after linking) | `layout(set = 0 / 2, binding = slot) uniform sampler2D name;` |
| `GD_IN(loc)` / `GD_OUT(loc)` (varyings) | `in` / `out` (330 has no varying locations) | `layout(location = loc) in` / `out` |
| `GD_ATTR(loc)` (vertex inputs) | `layout(location = loc) in` | the same |
| `GD_FRAG_OUT(loc)` | `layout(location = loc) out` | the same |
| `GD_VERTEX_ID` | `gl_VertexID` | `gl_VertexIndex` |
| `gd_clip(p)` | `p`, y flipped in targets (`gd_FlipY`) | z remapped to 0..1 (+ y flip on Vulkan) |

The set numbers are SDL_GPU's fixed layout (textures set 0 / 2, uniform
buffers set 1 / 3 for vertex / fragment); the Vulkan backend uses the same
layout, so one SPIR-V blob serves both.

- **OpenGL3** compiles the dialect at run time (the driver's compiler; no
  compiler shipped by us): the backend adds the version line, precision
  (ES), the macro definitions, and binds blocks and samplers by the
  reflection names. This is what runs now.
- **SDL3 GPU and Vulkan** (phases 2 and 3) take **SPIR-V compiled at build
  time** with glslang, built from source as a *host* tool (FetchContent at a
  pinned tag) and run on the Linux build machine for every package (SPIR-V
  is platform neutral, so the Windows and Android cross builds use the same
  blobs, embedded as C arrays). Metal (macOS / iOS through SDL_GPU) needs
  MSL: SDL_shadercross (SPIRV-Cross inside) converts the SPIR-V offline on
  the same host; D3D12 takes DXIL from shadercross + DXC, or SDL_GPU's
  SPIR-V path where the driver has it [I]. No shader compiler ships to
  players (rule 8).

**Why not GLSL 450 + SPIRV-Cross to GLSL for GL too?** It would make the GL
path depend on two large host tools now, for no gain: the GL backend
already accepts the dialect, and the drivers' GLSL compilers are the ones
gd01 and gd02 have always used (the risk-free choice for `opengl3`). The
dialect is valid GLSL 450 after the macros, so the build-time SPIR-V step
needs no rewrite of any shader. `tools/` will hold a check that compiles
every shader of the core and of each game to SPIR-V with glslang (it also
catches dialect mistakes before phase 2).

`ShaderDesc` carries the forms a backend may pick: `glsl` (the dialect
bodies + `#define` lines + reflection names) and `spirv` (vertex and
fragment blobs + entry points); a backend takes the first it can use and
fails with a clear error otherwise.

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
| shaders | SPIR-V (Vulkan driver), MSL (Metal), DXIL (D3D12) from the build step |
| depth bias, cull, front face | `SDL_GPURasterizerState` |

### 4.3 Vulkan (`vulkan`, phase 3)

Vulkan 1.1+ through volk (gd01 already vendors volk and the Vulkan headers),
VMA for memory (single header, MIT). Dynamic rendering
(`VK_KHR_dynamic_rendering`, core 1.3) for passes, a pipeline cache on
disk, descriptor sets in the SDL_GPU layout (one SPIR-V for both),
uniform slots as dynamic uniform buffers on a per-frame ring, timeline or
per-frame fences for `kFramesInFlight`. MoltenVK / KosmicKrisp on Apple.
gd01's FSR 3.1 / DLSS then run natively (no GL interop).

## 5. The window

The launcher (`gd-engine-launcher`) owns the window: it is created with
`SDL_WINDOW_OPENGL` and the launcher keeps a GL context on it. A module runs
on that window: the module shim (`sdk/gd_module_shim.h`, force-included in
every file of a module) turns the module's `SDL_CreateWindow` into "the
launcher's window" and tracks the module's GL contexts so none outlives it.

**Now (OpenGL3).** The core is a separate library, *not* compiled with the
shim, so the GL backend never calls `SDL_GL_CreateContext`,
`SDL_GL_DestroyContext` or `SDL_GL_SwapWindow` itself when the game passes
`DeviceCreateInfo::gl` hooks: gd01 passes its own (shimmed) calls, so the
context the core makes is a module context the shim tracks and destroys,
and the swap is the shim's (which does nothing while the launcher's model
explorer presents). Without hooks (a standalone program, the self-test) the
backend calls SDL directly. Nothing in the launcher changes for phase 1.

**Phase 2 / 3 (SDL_GPU, Vulkan).** A window cannot be used by a GL context
and a GPU swapchain at once (and on macOS a CAMetalLayer and an
NSOpenGLContext conflict), and SDL's Vulkan / Metal surfaces want the
window made with `SDL_WINDOW_VULKAN` / `SDL_WINDOW_METAL` [I: SDL3 recreates
a window to switch its graphics API for `SDL_Renderer`; `SDL_ClaimWindowForGPUDevice`
on a GL window must be tested on each platform]. The plan:

1. **Module ABI**: the module asks the host for the window *for an API*
   (`gd_host.h` gains `request_window(api)`: `gl`, `gpu`); the shim calls it
   from the core's `DeviceCreateInfo::prepareWindow` hook.
2. The **launcher releases its GL context** (makes nothing current,
   destroys its context and its `gfx` objects), recreates the window
   without `SDL_WINDOW_OPENGL` (with `SDL_WINDOW_VULKAN` / `METAL` when the
   platform needs it) at the same size, position and fullscreen state, and
   hands it to the module. The module's device claims it
   (`SDL_ClaimWindowForGPUDevice`, or a `VkSurfaceKHR`).
3. On return the module's device releases the window
   (`SDL_ReleaseWindowFromGPUDevice` / surface destroyed); the launcher
   recreates its GL window and context as it already does after a game
   (size, position, fullscreen restored). One black frame is acceptable.
4. A module that fails to get its backend (no Vulkan driver) creates the GL
   device instead and logs why (rule 2: never a black screen).

The model explorer (the launcher draws a module's scene) stays GL-only
until the launcher's own 2D moves onto the core.

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
  pixels. `ctest` runs it; exit status 0 = pass.
- gd01: the selftest and the walks on `local` and on `opengl3`
  (`GD01_GRAPHICS_BACKEND=opengl3`).
- gd01 goldens: screenshots of the main menu, songs in three venues, the
  highway with Star Power, results, Enhanced menu; before / after on
  `local` must be identical, `opengl3` against `local` within a tiny
  tolerance. Reproducible headless frames need a fixed clock: the harness
  preloads a test-only library that makes `SDL_GetTicks*` advance 1/60 s per
  presented frame and `std::random_device` return a fixed sequence (it is
  not part of any build). Results: section 8.

## 8. Results (1.1.13 phase 1)

Filled in by the work that landed it; see the report in the backlog item
GDCORE-004 / 005.

## 9. Adding a backend

1. `render/src/<backend>/`: a class implementing `Device`; register it in
   `src/device.cpp`'s factory under its id; `backendCompiled` true where it
   builds.
2. Pass `gdcore_render_selftest --backend <id>` (same pixels as opengl3).
3. gd01: nothing changes except removing the id from the "not available
   yet" list; run the goldens against `local`.
