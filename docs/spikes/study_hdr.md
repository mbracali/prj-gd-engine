# HDR output

Status: study, 2026-10-28.

## Verdict

**Possible, but not cheap. Treat it as an optional "Enhanced" setting, never a
default.** The original games are SDR, and we match them exactly. The blocker is
our **presentation API: everything presents through an OpenGL default
framebuffer**, and neither SDL3 nor most OSes offer HDR for OpenGL windows.
The workable path is **render as now (GL) and present through a Vulkan HDR
swapchain**, reusing gd01's existing GL↔Vulkan interop, on Windows, Linux
(Wayland/gamescope, Steam Deck OLED) and later Android. macOS EDR would need
Metal or MoltenVK. Effort: about **3 to 5 weeks** for gd02 + the launcher on
Windows/Linux, plus about 1 week per extra OS.

## Correction to the brief

gd01 is **not** a Vulkan renderer. It creates an OpenGL context
(`render/renderer.cpp:208`, `SDL_WINDOW_OPENGL`) and uses Vulkan only for the AI
upscalers, with shared images (`vk_upscaler.cpp`, `GL_RGBA16F` ↔
`VK_FORMAT_R16G16B16A16_SFLOAT`). gd02 and the launcher are GL 3.3 / GLES 3.0.

## What our renderers already have

- **gd02:** `post_fx.cpp` renders into a **10-bit `RGB10A2` "HDR" buffer**. It
  copies GH2: colours divided by 4 by the HDR key, then 8-bit bloom/DOF clamped
  at 1. That is the 360's fake-HDR scheme, not linear scene-referred light. The
  output is gamma-encoded 8-bit SDR. A `TargetFormat::RGBA16F` exists in `gl_device`.
- **gd01:** optional `GL_RGBA16F` post-FX targets (`post_fx.cpp:750`), bloom, and
  AO. The output is 8-bit SDR.
- There is no tone mapper or paper-white concept. UI and highway are drawn in
  SDR sRGB.

So we would need: an FP16 scene buffer, a linear pipeline up to the final pass,
then **two output transforms**. SDR keeps today's exact result. HDR maps the
"original look" to paper white (for example 203 nits, or the OS SDR white level)
and only lets *highlights* (bloom, spotlights, star power, fireworks) use the
headroom. UI and the highway are composited at paper white.

## Platform support

| OS | HDR presentation | With our GL renderer |
|---|---|---|
| Windows 10/11 | DXGI flip-model: `R16G16B16A16_FLOAT` + scRGB, or `R10G10B10A2` + HDR10/PQ. Vulkan via `VK_EXT_swapchain_colorspace` (NVIDIA/AMD/Intel). | GL has no HDR swapchain (there are vendor-specific float pixel formats, but they are unreliable). **Present through Vulkan or D3D.** |
| macOS | EDR: `CAMetalLayer` with `wantsExtendedDynamicRangeContent`, FP16, extended linear sRGB. MoltenVK maps `EXTENDED_SRGB_LINEAR` to EDR. | GL is deprecated with no EDR. Needs Metal/MoltenVK presentation and GL→IOSurface interop, or a port. |
| Linux desktop | Wayland `color-management-v1` (KDE Plasma 6, GNOME 48+, wlroots in progress). Mesa/NVIDIA Vulkan WSI expose HDR10 when the compositor supports it. X11: no. | Vulkan swapchain only (EGL `EXT_gl_colorspace_bt2020_pq` exists but is rare) |
| Steam Deck OLED | gamescope passes HDR10 through for Vulkan HDR swapchains (its WSI layer), with "Enable HDR" in Steam settings | Same Vulkan path. It is the easiest HDR target to test. |
| Android | Vulkan HDR10 or EGL `bt2020_pq` on some devices (Pixel, Samsung). Behaviour varies by device. | Later, if at all. |

**SDL3:** it reports `SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN`,
`SDL_PROP_WINDOW_SDR_WHITE_LEVEL_FLOAT` and `SDL_PROP_WINDOW_HDR_HEADROOM_FLOAT`,
plus `SDL_EVENT_WINDOW_HDR_STATE_CHANGED`. HDR output in SDL itself exists for
the 2D renderer (D3D11/12, Vulkan, and now Metal/GPU, with `SDL_COLORSPACE_HDR10`
and `SRGB_LINEAR`) and for **SDL_GPU** swapchains
(`SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR` / `HDR10_ST2084`). **Not for
OpenGL.** We can still use the properties to read headroom and SDR white for our
own Vulkan swapchain.

## Plan (if we do it)

1. **Linear FP16 scene (both games).** Use `RGBA16F` targets. Keep the current
   SDR output pixel-identical (selftest screenshots compare).
2. **Output pass.** Add an `outputTransform(mode, paperWhite, headroom)` shader:
   SDR (today), scRGB (linear, 1.0 = 80 nits, scaled by the SDR white level), or
   HDR10 (BT.2020 + PQ). Add a gentle highlight shoulder above paper white.
3. **Presenter.** A small Vulkan presenter in the launcher (gd01's
   `vk_upscaler.cpp` interop shows how). GL renders into a shared FP16 image,
   and Vulkan copies it to an HDR swapchain (`A2B10G10R10` + `HDR10_ST2084_EXT`,
   or `R16G16B16A16_SFLOAT` + `EXTENDED_SRGB_LINEAR_EXT`). Sync with semaphores
   (`GL_EXT_semaphore`). Fall back to plain GL presentation when anything is
   missing.
4. **Module ABI.** Add an HDR flag in `gd_host.h` so modules get paper white and
   headroom, and the launcher owns the swapchain.
5. **UI.** An Enhanced setting "HDR (Off/On)" with a paper-white slider and a
   peak-brightness slider. Handle `HDR_STATE_CHANGED`.
6. Test on Windows (an HDR monitor), KDE Wayland, and the Steam Deck OLED.

## Effort and costs

- Steps 1–2: about 1 to 2 weeks (two renderers, with the screenshot-identical SDR check).
- Step 3–4: about 1 to 2 weeks (interop, sync, fallbacks, the module ABI).
- Step 5–6: about 1 week. macOS EDR: about 1 extra week. Android: undecided.
- No money cost except an HDR display (or a Deck OLED). There is no CI cost
  because there is no visual test in CI.

## Risks

- GL↔Vulkan interop is missing on some Intel/older drivers (fall back to SDR).
- Bloom and DOF are 8-bit in the original, clamped at 1. A "faithful" look has
  little real HDR content, so the benefit is modest: brighter highlights
  and wider-gamut venue lights.

## Sources

- SDL window HDR properties: https://wiki.libsdl.org/SDL3/SDL_GetWindowProperties
- SDL_GPU swapchain compositions: https://wiki.libsdl.org/SDL3/SDL_GPUSwapchainComposition
- SDL colourspaces: https://wiki.libsdl.org/SDL3/SDL_Colorspace
- SDL HDR10 rendering (D3D/Vulkan/Metal): https://www.phoronix.com/news/SDL-HDR10-Rendering-Support
- Vulkan `VK_EXT_swapchain_colorspace`: https://registry.khronos.org/vulkan/specs/latest/man/html/VK_EXT_swapchain_colorspace.html
- Windows advanced colour: https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range
- Apple EDR: https://developer.apple.com/documentation/metal/hdr_content/displaying_hdr_content_in_a_metal_layer
- Wayland color management: https://wayland.app/protocols/color-management-v1
- Steam Deck HDR / gamescope: https://www.phoronix.com/news/XDC-2023-AMD-Colors-HDR , https://github.com/ValveSoftware/gamescope
