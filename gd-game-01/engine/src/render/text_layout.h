// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <string>
#include <vector>

#include "data/bitmap.h"
#include "render/renderer.h"
#include "scene/scene.h"

// RndFont glyph metrics (FUN_001b7040): the sheet is walked row-major in
// cellWidth x cellHeight cells, one per character of `chars`; each glyph's
// horizontal extent is measured from the texture alpha (first and last
// columns with any alpha). See FINDINGS.md "Fonts and text".
class FontMetrics {
public:
    FontMetrics(const FontObj& font, const Image& sheet);

    struct Glyph {
        int x = 0, width = 0, row = 0;  // pixels; row of cells
    };
    const Glyph& glyph(unsigned char c) const { return glyphs_[c]; }
    // Advance of `c` after `prev` in cell widths (FUN_001b6fd0); prev 0 = line start.
    float advance(unsigned char prev, unsigned char c) const;
    float cellWidth() const { return cellW_; }
    float cellHeight() const { return cellH_; }
    int texWidth() const { return texW_; }
    int texHeight() const { return texH_; }

private:
    std::array<Glyph, 256> glyphs_{};
    const FontObj& font_;
    float cellW_ = 1, cellH_ = 1;
    int texW_ = 1, texH_ = 1;
};

// RndText layout and mesh (FUN_001d85e0 / FUN_001d8fc0): quads in the XZ
// plane, `size` = the world width of one cell.
struct TextStyle {
    float size = 1, leading = 1, wrapWidth = 0, italics = 0;
    uint32_t align = 0x11;  // high nibble 0x10 top / 0x20 middle / 0x40 bottom; low 1 left / 2 centre / 4 right
    int capsMode = 0;       // 2 upper case, 1 lower case
    bool markup = false;
    std::array<float, 4> color{1, 1, 1, 1};
};

struct TextMesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    float width = 0, height = 0;  // widest line, total height
    int lines = 0;
    float lineHeight = 0;         // size * cellH / cellW
};

TextMesh layoutText(const FontMetrics& font, const std::string& text, const TextStyle& style);
