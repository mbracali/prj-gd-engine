// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

// [I] Enhanced > HDR > True HDR: the window's HDR surface (not in the
// original game). The surface is chosen when the window is made, so True HDR
// starts with the game; the frame is then drawn into an RGBA16F target and
// GlDevice::endFrame converts it for the surface (the UI at paper white).
//   Windows: a floating-point (WGL RGBA16F) pixel format, which the desktop
//            composes as scRGB (linear, 1.0 = 80 nits) when HDR is on.
//   Linux:   Wayland only (EGL): an RGBA16F config with
//            EGL_EXT_gl_colorspace_scrgb_linear, else
//            EGL_EXT_gl_colorspace_bt2020_pq (+ SMPTE 2086 metadata when the
//            driver has it). X11 (and so the Steam Deck's game mode) has none.
//   macOS, Android: no OpenGL HDR path.
// Anything missing falls back to Emulated (tone mapped to SDR), never to a
// black window: App retries the window without the request when it fails.
namespace hdr {

enum class Surface { Sdr, ScRgb, Pq };

// Whether this platform / session has a True HDR path at all ("" when it
// may, else why not, for the Enhanced menu).
std::string unsupportedReason();
// Before SDL_CreateWindow: asks for the HDR surface. False (and nothing
// asked) when unsupportedReason() is not empty.
bool request();
// Undoes request(): the window or its context could not be made with it.
void cancel();
// Why the window or context could not be made with the request (`what` and SDL's error, or the
// EGL surface's own reason).
std::string failure(const std::string& what, const char* sdlError);
// After the GL context is current: the surface the window got, or Sdr with
// why not in `reason`.
Surface obtained(std::string& reason);

}  // namespace hdr
