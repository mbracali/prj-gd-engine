// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>

// The debug drawing surface for the first milestones: SDL's 2D renderer
// with a fixed 1280x720 logical screen (GH2's native 16:9 720p [I]),
// coloured triangles and SDL's built-in 8x8 debug font. It offers the part
// of gd01's Renderer API that render/highway.cpp uses, so the placeholder
// highway works unchanged; gd01's GL renderer (fence ring, frame limiter,
// post-processing) replaces it when the game's own art arrives.
constexpr float kScreenW = 1280, kScreenH = 720;

struct Vertex {
    float x, y, z;
    float u, v;
    uint8_t r, g, b, a;
};

enum class Blend { Opaque, Alpha, Add };

class Canvas {
public:
    ~Canvas();
    // Creates the renderer for `window`; returns false and sets `error` on failure.
    bool init(SDL_Window* window, std::string& error);
    void shutdown();

    void begin(SDL_Color clear);
    void end();

    // Triangles (three vertices each); `texture` is unused (no textures yet).
    void draw(const Vertex* v, size_t count, int texture, Blend blend);
    void fillRect(float x, float y, float w, float h, SDL_Color c);
    // Text in the 8x8 debug font, `scale` times its size (Latin-1 shown as '?').
    void text(float x, float y, float scale, SDL_Color c, const std::string& s);
    void textCentered(float cx, float y, float scale, SDL_Color c, const std::string& s);

    // The last frame drawn, before end() presents it; for screenshots.
    SDL_Surface* readPixels();

private:
    SDL_Renderer* renderer_ = nullptr;
};
