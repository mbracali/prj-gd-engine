// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// gd24's screen for now: a 960 x 544 picture (twice the PSP's 480 x 272)
// drawn on the CPU (SDL's software renderer: rectangles and SDL's built-in
// 8 x 8 debug font) and shown through gd-engine-core's render device
// (OpenGL 3 by default: the launcher's window), scaled to the window. Only
// our own status / settings / GAMEPADS screens use it until the original's
// highway and UI are decoded (GD24-002).
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <string>

#include "gd/core/render/device.h"

struct Rgba {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

class Canvas {
public:
    static constexpr int kW = 960, kH = 544;

    ~Canvas();
    // The device on `window` (made with gd::core::render::windowFlags(backend)).
    bool init(SDL_Window* window, gd::core::render::Backend backend, std::string& error);
    void shutdown();

    void clear(Rgba c);
    void fill(float x, float y, float w, float h, Rgba c);
    void frame(float x, float y, float w, float h, Rgba c);
    // Text with the 8 x 8 font at `scale` (1: 8 px glyphs); non-ASCII letters
    // are shown without their accents. Returns the width drawn.
    float text(float x, float y, const std::string& s, Rgba c, float scale = 2);
    static float textWidth(const std::string& s, float scale = 2);
    // The picture to the window.
    void present();
    // The current picture as a BMP (development aid: GD24_SCREENSHOT).
    bool save(const std::string& path) const;
    // Window pixels to canvas units.
    void windowToCanvas(float wx, float wy, float& cx, float& cy) const;

private:
    SDL_Window* window_ = nullptr;
    SDL_Surface* surface_ = nullptr;
    SDL_Renderer* soft_ = nullptr;
    std::unique_ptr<gd::core::render::Device> dev_;
    gd::core::render::Texture tex_{};
};
