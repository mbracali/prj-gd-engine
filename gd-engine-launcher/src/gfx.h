// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The launcher's 2D drawing: OpenGL 3.3 core (ES 3.0 later for Android), one
// batched shader for images, text (stb_truetype atlases) and anti-aliased
// rounded rectangles (signed distance), in window pixels.
#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gfx {

struct Color {
    float r = 1, g = 1, b = 1, a = 1;
};
inline Color rgb(uint32_t hex, float a = 1) {
    return {float((hex >> 16) & 255) / 255.f, float((hex >> 8) & 255) / 255.f, float(hex & 255) / 255.f, a};
}
inline Color withAlpha(Color c, float a) { return {c.r, c.g, c.b, c.a * a}; }
inline Color mix(Color a, Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

struct Texture {
    unsigned id = 0;
    int w = 0, h = 0;
};

enum class FontId { Title, Regular, SemiBold, Icons, Count };

// Material Symbols (Outlined) codepoints for the launcher's icons. The subset
// font res/fonts/MaterialSymbols-subset.ttf carries exactly these glyphs.
namespace icon {
constexpr uint32_t kPercent = 0xeb58, kCircle = 0xef4a;
constexpr uint32_t kClock10 = 0xf726, kClock20 = 0xf725, kClock40 = 0xf724, kClock60 = 0xf723, kClock80 = 0xf722,
                   kClock90 = 0xf721, kStars = 0xe8d0, kStrokeFull = 0xf749;
constexpr uint32_t kPauseCircle = 0xe1a2, kStopCircle = 0xef71;
constexpr uint32_t kPlayCircle = 0xe1c4, kAlbum = 0xe019, kDiscFull = 0xe610, kMusicNoteAdd = 0xf391,
                   kAddShoppingCart = 0xe854, kDatabaseSearch = 0xf38e, kHandyman = 0xf10b;
// UTF-8 for one codepoint (text() takes UTF-8 strings).
std::string u8(uint32_t cp);
}  // namespace icon

class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    // After the GL context is current. Fonts and images are the launcher's
    // built-in resources (res/, embedded by CMake).
    bool init(std::string& error);
    void shutdown();

    // Sets the viewport and clears (`clear` false: draws over what is there,
    // e.g. a game module's 3D view).
    void begin(int pixelW, int pixelH, bool clear = true);
    void end();                          // flushes the batch
    void clip(const Rect* r);            // draw only inside r (nullptr: everywhere)

    // Shapes. radius in pixels; `top`/`bottom` make a vertical gradient.
    void rect(const Rect& r, Color top, Color bottom, float radius = 0);
    void rect(const Rect& r, Color c, float radius = 0) { rect(r, c, c, radius); }
    void ring(const Rect& r, Color c, float radius, float width);
    // A soft glow / shadow around a rounded rectangle, `blur` pixels wide.
    void glow(const Rect& r, Color c, float radius, float blur);
    void circle(float cx, float cy, float radius, Color c) { rect({cx - radius, cy - radius, 2 * radius, 2 * radius}, c, radius); }
    void image(const Texture& t, const Rect& r, Color tint = {}, bool flipY = false, float radius = 0);
    // Creates or refills `t` (RGBA rows, bottom row first when drawn with flipY).
    void upload(Texture& t, int w, int h, const uint8_t* rgba);
    void release(Texture& t);

    // Text: `px` is the font size in pixels; (x, y) the top-left of the line box.
    float text(FontId f, float px, float x, float y, const std::string& utf8, Color c, float tracking = 0);
    float measure(FontId f, float px, const std::string& utf8, float tracking = 0);
    float lineHeight(FontId f, float px);
    // Shortens `utf8` with an ellipsis to fit `maxW`.
    std::string elide(FontId f, float px, const std::string& utf8, float maxW);
    // Word-wraps into lines no wider than `maxW`.
    std::vector<std::string> wrap(FontId f, float px, const std::string& utf8, float maxW);

    Texture icon() const { return icon_; }
    Texture backdrop() const { return backdrop_; }

    // Development aid: the frame as RGBA rows, top row first.
    std::vector<uint8_t> readPixels(int w, int h);

private:
    struct Vertex {
        float x, y, u, v;
        uint8_t c[4];
        float lx, ly, hx, hy, radius, mode;
    };
    struct Atlas;
    void flush();
    void useTexture(unsigned tex);
    void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Color top, Color bottom,
              float hx, float hy, float radius, float mode);
    Atlas* atlas(FontId f, float px);
    Texture loadImage(const unsigned char* data, size_t size);

    unsigned program_ = 0, vao_ = 0, vbo_ = 0, white_ = 0, boundTex_ = 0;
    int screenLoc_ = -1;
    int w_ = 0, h_ = 0;
    std::vector<Vertex> verts_;
    std::map<std::pair<int, int>, std::unique_ptr<Atlas>> atlases_;
    Texture icon_, backdrop_;
};

// Reads [x, y, w, h] (GL coordinates, origin bottom-left) of the default
// framebuffer's back buffer of whichever GL context is current, as opaque
// RGBA rows (bottom row first), touching no other state. The launcher reads
// a game module's model explorer frame with it (explorer.cpp).
// `keepAlpha`: leave the read-back alpha as it is (for a transparent capture)
// instead of forcing every pixel opaque.
bool readBackBuffer(int x, int y, int w, int h, std::vector<uint8_t>& out, bool keepAlpha = false);

// Writes an 8-bit RGBA PNG (top row first), no dependency beyond zlib's
// stored deflate (the launcher writes a transparent model shot with it,
// explorer.cpp). false when the file cannot be opened.
bool writePng(const std::string& path, int w, int h, const uint8_t* rgba);

}  // namespace gfx
