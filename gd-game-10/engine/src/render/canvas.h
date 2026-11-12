// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include "render/renderer.h"

// The app's drawing surface: it owns the renderer (render/renderer.h) and
// draws 2D on a fixed 1280x720 logical screen (GH2's native 16:9 720p),
// letterboxed in the window: coloured or textured triangles, rectangles and
// text in an 8x8 debug font. The debug highway, HUD text and the menus use
// it; 3D scenes draw through renderer() before the 2D on top.
constexpr float kScreenW = 1280, kScreenH = 720;

struct Vertex {
    float x, y, z;
    float u, v;
    uint8_t r, g, b, a;
};

enum class Blend { Opaque, Alpha, Add, Sub };  // Sub: destination - source x alpha

class Canvas {
public:
    ~Canvas();
    // Creates the renderer for `window` (made with gfx::prepareWindow()'s
    // flags); returns false and sets `error` on failure.
    bool init(SDL_Window* window, std::string& error);
    void shutdown();

    void begin(SDL_Color clear);
    // Presents the frame. With `holdIfEmpty`, a frame that drew nothing after
    // its clear (a screen torn down, the next not built yet) is not
    // presented: the window keeps the last picture, as GH3 shows the previous
    // screen until the next draws, rather than a black flash. A hold lasts
    // at most kMaxHoldMs (a screen that is really empty goes black after it).
    // Returns whether the frame was presented.
    bool end(bool holdIfEmpty = false);
    static constexpr double kMaxHoldMs = 250;
    // GD10_PERF: the time from begin() to the present (drawing, on the CPU)
    // and the frames drawn, summed until the caller clears them.
    uint64_t drawNs = 0;
    int drawnFrames = 0;
    int heldFrames = 0;  // frames not presented (end's hold), all told
    // Walks (probe:black / check:no_black): while on, presented frames that
    // are all black after a lit one are counted (a black flash between
    // screens: GD10-089).
    static inline bool countBlack = false;
    static inline bool countBlackLit = false;
    static inline int blackFrames = 0;
    // Walks (shot:<name> with GD10_WALK_SHOTS=<dir>): the next presented frame is saved there.
    static inline std::string shotPath;

    // Triangles (three vertices each). `texture` is a gfx::TextureId's id,
    // 0 for plain colour (the vertices' uv are then ignored).
    // `repeat`: the texture wraps (scrolling surfaces) instead of clamping.
    void draw(const Vertex* v, size_t count, int texture, Blend blend, bool repeat = false);
    void fillRect(float x, float y, float w, float h, SDL_Color c);
    // Triangles drawn while a clip rectangle is set are cut to it (screen
    // units; UVs and colours interpolated) -- GH3's window elements.
    void setClip(float x0, float y0, float x1, float y1) { clip_ = true, cx0_ = x0, cy0_ = y0, cx1_ = x1, cy1_ = y1; }
    void clearClip() { clip_ = false; }
    // Text in the 8x8 debug font, `scale` times its size (non-ASCII shows as '?').
    void text(float x, float y, float scale, SDL_Color c, const std::string& s);
    void textCentered(float cx, float y, float scale, SDL_Color c, const std::string& s);

    // The last frame drawn, before end() presents it; for screenshots.
    SDL_Surface* readPixels();

    gfx::Renderer& renderer() { return renderer_; }
    // Draws what is batched so far (call before drawing 3D in between).
    void flush();

private:
    uint64_t beginDraws_ = 0, heldSinceNs_ = 0;
    bool presentedContent_ = false;
    uint64_t beginNs_ = 0;
    bool makeFont(std::string& error);

    gfx::Renderer renderer_;
    gfx::TextureId atlas_;    // the font, plus a white block for plain colour
    gfx::BufferId buffer_;
    size_t bufferUsed_ = 0;
    std::vector<Vertex> batch_;
    uint32_t batchTexture_ = 0;
    Blend batchBlend_ = Blend::Alpha;
    bool batchRepeat_ = false;
    bool clip_ = false;
    float cx0_ = 0, cy0_ = 0, cx1_ = 0, cy1_ = 0;
    std::vector<Vertex> clipped_;
};
