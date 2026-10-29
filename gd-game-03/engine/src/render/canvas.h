// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include "render/renderer.h"

// The app's drawing surface: it owns the renderer (render/renderer.h) and
// draws 2D on a fixed 1280x720 logical screen (the 16:9 frame, letterboxed
// in the window): coloured or textured triangles, rectangles and text in an
// 8x8 debug font. The debug highway, HUD text and the menus use it; 3D
// scenes draw through renderer() before the 2D on top. The game's picture
// is the frame's middle 960 units (4:3, the PS2's) or all of it (16:9):
// drawing outside the picture is cut off, unless the renderer's full frame
// is on (the port's own screens).
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
    // Creates the renderer for `window` (made with gfx::prepareWindow()'s
    // flags); returns false and sets `error` on failure.
    bool init(SDL_Window* window, std::string& error);
    void shutdown();

    void begin(SDL_Color clear);
    void end();
    // GD03_PERF: the time from begin() to the present (drawing, on the CPU)
    // and the frames drawn, summed until the caller clears them.
    uint64_t drawNs = 0;
    int drawnFrames = 0;

    // Triangles (three vertices each). `texture` is a gfx::TextureId's id,
    // 0 for plain colour (the vertices' uv are then ignored).
    void draw(const Vertex* v, size_t count, int texture, Blend blend);
    void fillRect(float x, float y, float w, float h, SDL_Color c);
    // Text in the 8x8 debug font, `scale` times its size (non-ASCII shows as '?').
    void text(float x, float y, float scale, SDL_Color c, const std::string& s);
    void textCentered(float cx, float y, float scale, SDL_Color c, const std::string& s);

    // The last frame drawn, before end() presents it; for screenshots.
    SDL_Surface* readPixels();

    // The picture's left edge and width in logical units (0 and 1280 when
    // widescreen, 160 and 960 when 4:3).
    float pictureX0() const;
    float pictureW() const;
    // Logical units to clip space over the picture's viewport.
    gfx::Mat4 projection() const;

    gfx::Renderer& renderer() { return renderer_; }
    // Draws what is batched so far (call before drawing 3D in between).
    void flush();

private:
    uint64_t beginNs_ = 0;
    bool makeFont(std::string& error);

    gfx::Renderer renderer_;
    gfx::TextureId atlas_;    // the font, plus a white block for plain colour
    gfx::BufferId buffer_;
    size_t bufferUsed_ = 0;
    std::vector<Vertex> batch_;
    uint32_t batchTexture_ = 0;
    Blend batchBlend_ = Blend::Alpha;
};
