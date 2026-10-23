// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/text_layout.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

FontMetrics::FontMetrics(const FontObj& font, const Image& sheet) : font_(font) {
    cellW_ = std::max(1.0f, font.cellWidth);
    cellH_ = std::max(1.0f, font.cellHeight);
    texW_ = std::max(1, sheet.width);
    texH_ = std::max(1, sheet.height);
    int cw = static_cast<int>(cellW_), ch = static_cast<int>(cellH_);
    auto alpha = [&](int x, int y) {
        return sheet.width > 0 ? sheet.rgba[(size_t(y) * size_t(sheet.width) + size_t(x)) * 4 + 3] : 255;
    };
    int x = 0, y = 0;
    std::string chars = font.chars;
    if (!chars.empty() && static_cast<unsigned char>(chars[0]) == 0xA0) chars[0] = ' ';
    for (unsigned char c : chars) {
        if (texW_ < x + cw) {
            x = 0;
            y += ch;
        }
        if (texH_ < y + ch) break;
        // FUN_001b6e20 / FUN_001b6908: first and last columns with alpha in the cell.
        int left = -1, right = -1;
        for (int px = x; px < x + cw && left < 0; ++px)
            for (int py = y; py < y + ch; ++py)
                if (alpha(px, py)) {
                    left = px;
                    break;
                }
        for (int px = x + cw - 1; px >= x && right < 0; --px)
            for (int py = y; py < y + ch; ++py)
                if (alpha(px, py)) {
                    right = px;
                    break;
                }
        Glyph g;
        g.row = y / ch;
        if (left < 0) {  // empty cell (space)
            g.x = x;
            g.width = static_cast<int>(cellW_ * 0.25f);
        } else {
            g.x = left;
            g.width = right - left + 1;
        }
        glyphs_[c] = g;
        x += cw;
    }
    glyphs_['\t'] = glyphs_[' '];
    glyphs_['\t'].width = static_cast<uint8_t>(glyphs_[' '].width * 3);
}

float FontMetrics::advance(unsigned char prev, unsigned char c) const {
    float pair = 0;
    if (prev && c) {
        pair = font_.space;
        if (auto it = font_.kerning.find({char(prev), char(c)}); it != font_.kerning.end()) pair += it->second;
    }
    return pair + float(glyphs_[c].width) / cellW_;
}

namespace {

struct Run {  // a glyph with its style, after markup
    unsigned char c;
    float size, italics;
    std::array<float, 4> color;
};

uint8_t unit(float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); }

}  // namespace

TextMesh layoutText(const FontMetrics& font, const std::string& text, const TextStyle& st) {
    // Markup (FUN_001d83e8): <sup> </sup> <it> </it> <color r g b a> </color>.
    std::vector<Run> runs;
    float size = st.size, italics = st.italics;
    std::array<float, 4> color = st.color;
    for (size_t i = 0; i < text.size(); ++i) {
        if (st.markup && text[i] == '<') {
            size_t end = text.find('>', i);
            if (end != std::string::npos) {
                std::string tag = text.substr(i + 1, end - i - 1);
                if (tag == "sup") size = st.size * 0.7f;
                else if (tag == "/sup") size = st.size;
                else if (tag == "it") italics = 0.1f;
                else if (tag == "/it") italics = 0;
                else if (tag.rfind("color", 0) == 0) {
                    float v[4] = {255, 255, 255, 255};
                    char* p = tag.data() + 5;
                    for (float& f : v) f = std::strtof(p, &p);
                    for (int k = 0; k < 4; ++k) color[size_t(k)] = v[k] / 255.0f;
                } else if (tag == "/color") color = st.color;
                i = end;
                continue;
            }
        }
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (st.capsMode == 2 && c < 128) c = static_cast<unsigned char>(std::toupper(c));
        else if (st.capsMode == 1 && c < 128) c = static_cast<unsigned char>(std::tolower(c));
        runs.push_back({c, size, italics, color});
    }

    // Lines: wrap at the last space / tab, or mid-word when there is none.
    struct Line {
        size_t start, end;  // runs [start, end)
        float width;
    };
    std::vector<Line> lines;
    float wrap = st.wrapWidth > 0 ? st.wrapWidth : 1e30f;
    size_t start = 0;
    float w = 0;
    unsigned char prev = 0;
    size_t lastSpace = std::string::npos;
    for (size_t i = 0; i < runs.size(); ++i) {
        unsigned char c = runs[i].c;
        if (c == '\n') {
            lines.push_back({start, i, w});
            start = i + 1, w = 0, prev = 0, lastSpace = std::string::npos;
            continue;
        }
        float adv = font.advance(prev, c) * runs[i].size;
        if (i == start || w + adv <= wrap) {
            if (c == ' ' || c == '\t') lastSpace = i;
            w += adv;
            prev = c;
            continue;
        }
        size_t brk = lastSpace != std::string::npos ? lastSpace : i;
        // Width of the line up to the break (the break space itself dropped).
        float lw = 0;
        unsigned char p = 0;
        for (size_t k = start; k < brk; ++k) {
            lw += font.advance(p, runs[k].c) * runs[k].size;
            p = runs[k].c;
        }
        lines.push_back({start, brk, lw});
        start = lastSpace != std::string::npos ? brk + 1 : brk;
        w = 0, prev = 0, lastSpace = std::string::npos;
        for (size_t k = start; k <= i; ++k) {
            w += font.advance(prev, runs[k].c) * runs[k].size;
            prev = runs[k].c;
            if (runs[k].c == ' ' || runs[k].c == '\t') lastSpace = k;
        }
    }
    lines.push_back({start, runs.size(), w});

    // Vertical placement: top / middle / bottom by the high nibble.
    TextMesh m;
    float L = st.size * font.cellHeight() / font.cellWidth();
    float n = static_cast<float>(lines.size());
    float y0 = 0;
    if (st.align & 0x20) y0 = st.leading * (n - 1) * L / 2 + L / 2;
    else if (st.align & 0x40) y0 = st.leading * (n - 1) * L + L;
    m.height = st.leading * (n - 1) * L + L;
    m.lines = int(n);
    m.lineHeight = L;
    float tw = float(font.texWidth()), th = float(font.texHeight());
    for (size_t li = 0; li < lines.size(); ++li) {
        const Line& line = lines[li];
        m.width = std::max(m.width, line.width);
        float zt = y0 - float(li) * st.leading * L;
        float cx = (st.align & 2) ? -line.width / 2 : (st.align & 4) ? -line.width : 0;
        unsigned char p = 0;
        for (size_t k = line.start; k < line.end; ++k) {
            const Run& r = runs[k];
            const FontMetrics::Glyph& g = font.glyph(r.c);
            float s = r.size;
            float pair = p ? font.advance(p, r.c) - float(g.width) / font.cellWidth() : 0;
            float x0 = cx + pair * s;
            float gw = float(g.width) / font.cellWidth() * s;
            cx = x0 + gw;
            p = r.c;
            float sl = r.italics * s;
            float zb = zt - s * font.cellHeight() / font.cellWidth();
            float u0 = float(g.x) / tw, u1 = float(g.x + g.width) / tw;
            float v0 = float(g.row) * font.cellHeight() / th, v1 = v0 + font.cellHeight() / th;
            uint8_t cr = unit(r.color[0]), cg = unit(r.color[1]), cb = unit(r.color[2]), ca = unit(r.color[3]);
            uint32_t base = static_cast<uint32_t>(m.vertices.size());
            m.vertices.push_back({x0 + sl, 0, zt, u0, v0, cr, cg, cb, ca});
            m.vertices.push_back({x0 - sl, 0, zb, u0, v1, cr, cg, cb, ca});
            m.vertices.push_back({x0 - sl + gw, 0, zb, u1, v1, cr, cg, cb, ca});
            m.vertices.push_back({x0 + sl + gw, 0, zt, u1, v0, cr, cg, cb, ca});
            m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        }
    }
    return m;
}
