// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "render/material.h"
#include "scene/objects.h"

// GH2's RndText layout and mesh, from the Xbox 360 executable:
//
// RndFont v15 (Load @ 0x821D4CD8) stores its glyph table (GH1 measured the
// widths from the texture): per character the u, v of its cell's top-left
// corner, its width and its advance, both in cell widths. cell_uv is one
// cell's size in UV, so a glyph covers u .. u + cell_uv.x * width and
// v .. v + cell_uv.y (@ 0x821D2E40). A font may take its glyph table (and
// kerning) from another font, its texture_owner (followed to the font that
// owns itself). Kerning between two characters (@ 0x821D2E98) is 0 at a line
// start or for a monospace font, else the font's `kerning` plus its pair
// table's amount (key first | second << 8, @ 0x821D2930).
//
// RndText v17 (Load @ 0x821D1A68): size is the world width of one cell, so
// a line is size * cell_h / cell_w high. SetText (@ 0x821D1710) applies the
// caps mode, then the layout (@ 0x821D0CE8) breaks lines at spaces / tabs /
// after '-' or inside a word when nothing else fits the wrap width, and the
// mesh (@ 0x821CFB28) places one quad per visible glyph in the XZ plane
// (y = 0), x to the right and z up, from the alignment's origin.
namespace gfx {

// RndText alignment flags: horizontal 1 left / 2 centre / 4 right, vertical
// 0x10 top / 0x20 middle / 0x40 bottom (the origin is that point of the
// text's box).
enum TextAlign : uint32_t {
    kAlignLeft = 1, kAlignCenter = 2, kAlignRight = 4,
    kAlignTop = 0x10, kAlignMiddle = 0x20, kAlignBottom = 0x40,
};

// A font with its glyph-table owner resolved.
class FontMetrics {
public:
    // `font`'s texture_owner is looked up in its directory (nullptr: itself).
    explicit FontMetrics(const scene::Font& font);

    const scene::Font& font() const { return font_; }    // material owner
    const scene::Font& glyphs() const { return *owner_; }  // glyph table owner
    const scene::Font::Glyph& glyph(unsigned char c) const { return owner_->glyphs[c]; }
    // size * cell_h / cell_w.
    float lineHeight(float size) const;
    // Kerning before `c` after `prev` in cell widths (`kerning`: the global
    // value, normally the font's own; BandLabel / BandButton replace it).
    float kern(unsigned char prev, unsigned char c, float kerning) const;
    float kerning() const;  // the font's global kerning
    // Advance of `c` in cell widths (1 for a monospace font).
    float advance(unsigned char c) const;
    // The glyph's UV rectangle.
    void uv(unsigned char c, float& u0, float& v0, float& u1, float& v1) const;

private:
    const scene::Font& font_;
    const scene::Font* owner_;
};

struct TextStyle {
    float size = 1, leading = 1, wrapWidth = 0, italics = 0;
    uint32_t align = kAlignLeft | kAlignTop;
    uint32_t capsMode = 0;  // 2 upper case, 1 lower case
    bool markup = false;    // <sup> <it> <color r g b a> tags (@ 0x821CF108)
    std::array<float, 4> color{1, 1, 1, 1};  // vertex colour
    bool overrideKerning = false;
    float kerning = 0;      // replaces the font's kerning when overrideKerning
};

struct TextLine {
    size_t begin = 0, end = 0;  // bytes of the (caps-applied) string
    float x = 0, y = 0;         // origin: left x, top y (z in the mesh)
    float width = 0;
};

struct TextLayout {
    std::string text;           // after the caps mode
    std::vector<TextLine> lines;
    std::vector<scene::MeshVertex> verts;  // 4 per quad
    std::vector<std::array<uint16_t, 3>> faces;
    float lineHeight = 0;       // at the text's size
    // The widest line (@ 0x821CFF50) and the vertical extent's top and
    // bottom (@ 0x821CFF08: the first line's y and the last one's y minus
    // size * leading).
    float maxWidth() const;
    void extent(const TextStyle& style, float& top, float& bottom) const;
};

// SetText's caps mode (@ 0x821D1710): 2 upper-cases a-z and 0xE0..0xFD,
// 1 lower-cases A-Z and 0xC0..0xDD.
std::string applyCaps(const std::string& s, uint32_t capsMode);

// Lays out `text` and builds its quads. Without a font: one line, no quads.
TextLayout layoutText(const FontMetrics* font, const std::string& text, const TextStyle& style);

// BandLabel / BandButton per-glyph jitter (@ 0x8214CD88): each quad is
// rotated about its centre by a random angle in +-deltaRot degrees (around
// y) and moved by random x / z offsets in +-deltaX / +-deltaZ, from GH2's
// table random generator (@ 0x8226DB90 / 0x8226DBE0) seeded with `seed` on
// every SetText.
void jitterText(TextLayout& layout, uint32_t seed, float deltaX, float deltaZ, float deltaRot);

// BandLabel / BandButton fit_text (@ 0x8214C7B0): the scale (x, z) the
// component's Text gets: mode 1 scales x to `width` and z to `height`
// (each when the text and the box have a size), mode 2 shrinks both by the
// smaller factor (never grows), 0 none.
std::array<float, 2> fitScale(const TextLayout& layout, const TextStyle& style, uint32_t mode, float width, float height);

// GH2's table random generator (Rand2-like: 256 words from an LCG, xor of
// two taps 103 apart over 249 entries). float(lo, hi) as 0x8226DD58.
class HmxRand {
public:
    explicit HmxRand(uint32_t seed);
    uint32_t next();
    float range(float lo, float hi);

private:
    uint32_t a_ = 0, b_ = 0x67;
    uint32_t table_[256] = {};
};

class SceneCache;
class MaterialRenderer;

// Draws laid-out texts through the renderer: the quads go to a vertex
// buffer per text (rebuilt when the text changes) and draw with the font's
// material (and its next passes), like any milo mesh.
class TextDrawer {
public:
    explicit TextDrawer(SceneCache& cache);
    ~TextDrawer();
    TextDrawer(const TextDrawer&) = delete;
    TextDrawer& operator=(const TextDrawer&) = delete;

    // Draws `layout` with `font`'s material at `world`. `key` identifies the
    // text (its buffers are kept until the layout changes); `matColor`,
    // when set, replaces the material's rgb (BandLabel's colour, @
    // 0x8214B0A0); `tint` multiplies colour and alpha.
    void draw(MaterialRenderer& r, const void* key, const scene::Font& font, const TextLayout& layout, const Mat4& world,
              const float* matColor = nullptr, const float* tint = nullptr);
    // Forgets the buffers of texts not drawn since the last call.
    void collect();

private:
    struct Entry;
    SceneCache& cache_;
    std::vector<std::unique_ptr<Entry>> entries_;
};

}  // namespace gfx
