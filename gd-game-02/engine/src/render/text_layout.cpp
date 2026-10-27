// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/text_layout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "render/scene_gpu.h"
#include "scene/milo.h"

namespace gfx {

// ---- Font -------------------------------------------------------------------

FontMetrics::FontMetrics(const scene::Font& font) : font_(font), owner_(&font) {
    // texture_owner chain (RndFont +0x34 ObjOwnerPtr; 0x821D2E40 walks it
    // to the font that owns itself).
    for (int depth = 0; depth < 8; ++depth) {
        const std::string& o = owner_->textureOwner;
        if (o.empty() || o == owner_->name || !owner_->dir) break;
        const scene::Font* next = owner_->dir->resolve<scene::Font>(o);
        if (!next || next == owner_) break;
        owner_ = next;
    }
}

float FontMetrics::lineHeight(float size) const {
    const auto& c = owner_->cellSize;
    return c[0] > 0 ? size * c[1] / c[0] : size;
}

float FontMetrics::kerning() const { return owner_->spacing; }

float FontMetrics::kern(unsigned char prev, unsigned char c, float kerning) const {
    if (prev == 0 || c == 0 || owner_->monospace) return 0;
    float k = kerning;
    const uint32_t key = uint32_t(prev) | uint32_t(c) << 8;
    for (const auto& [pair, amount] : owner_->kerning)
        if (pair == key) {
            k += amount;
            break;
        }
    return k;
}

float FontMetrics::advance(unsigned char c) const { return font_.monospace ? 1.0f : owner_->glyphs[c].advance; }

void FontMetrics::uv(unsigned char c, float& u0, float& v0, float& u1, float& v1) const {
    const scene::Font::Glyph& g = owner_->glyphs[c];
    u0 = g.x, v0 = g.y;
    u1 = g.x + owner_->cellUv[0] * g.width;
    v1 = g.y + owner_->cellUv[1];
}

// ---- Layout -----------------------------------------------------------------

std::string applyCaps(const std::string& s, uint32_t capsMode) {
    std::string r = s;
    for (char& ch : r) {
        const int c = static_cast<signed char>(ch);
        if (capsMode == 2 && ((c > '`' && c < '{') || (c > -0x21 && c < -2))) ch = char(c - 0x20);
        else if (capsMode == 1 && ((c > '@' && c < '[') || (c > -0x41 && c < -0x22))) ch = char(c + 0x20);
    }
    return r;
}

namespace {

// The style state a line starts with and markup changes (size, italics,
// colour; 0x18 bytes from Text +0x11C).
struct RunStyle {
    float size = 1, italics = 0;
    std::array<float, 4> color{1, 1, 1, 1};
};

// A markup tag at text[p] ('<'), as 0x821CF108: <sup> / </sup> (size x 0.7
// [I: GH1's factor; GH2's constant was not read]), <it> / </it> (italics
// 0.1 / 0 [I]), <color r g b a> (0..255) / </color>. Returns the position
// after the tag.
size_t parseTag(const std::string& text, size_t p, RunStyle& s, const TextStyle& base) {
    size_t q = p + 1;
    const bool open = q < text.size() && text[q] != '/';
    if (!open) ++q;
    auto starts = [&](const char* w) { return text.compare(q, std::strlen(w), w) == 0; };
    if (starts("sup")) {
        s.size = open ? base.size * 0.7f : base.size;
        q += 3;
    } else if (starts("it")) {
        s.italics = open ? 0.1f : 0.0f;
    } else if (starts("color")) {
        q += 5;
        if (open) {
            ++q;
            int r = 0, g = 0, b = 0, a = int(s.color[3] * 255);
            std::sscanf(text.c_str() + std::min(q, text.size()), "%d %d %d %d", &r, &g, &b, &a);
            s.color = {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
        } else {
            s.color = base.color;
        }
    }
    while (q < text.size() && text[q] != '>') ++q;
    if (q < text.size()) ++q;
    return q;
}

}  // namespace

float TextLayout::maxWidth() const {
    float w = 0;
    for (const auto& l : lines) w = std::max(w, l.width);
    return w;
}

void TextLayout::extent(const TextStyle& style, float& top, float& bottom) const {
    top = bottom = 0;
    if (lines.empty()) return;
    top = lines.front().y;
    bottom = lines.back().y - style.size * style.leading;
}

TextLayout layoutText(const FontMetrics* font, const std::string& input, const TextStyle& style) {
    TextLayout out;
    out.text = applyCaps(input, style.capsMode);
    const std::string& s = out.text;
    if (!font) {
        out.lines.push_back({0, s.size(), 0, 0, 0});
        return out;
    }
    const float kerning = style.overrideKerning ? style.kerning : font->kerning();
    const float wrap = style.wrapWidth > 0 ? style.wrapWidth : 1e30f;
    RunStyle base;
    base.size = style.size, base.italics = style.italics, base.color = style.color;
    RunStyle cur = base;
    std::vector<RunStyle> lineStyles;

    // Line breaking (0x821D0CE8).
    size_t p = 0;
    do {
        const size_t lineStart = p;
        const RunStyle lineStyle = cur;
        RunStyle atBreak = cur;
        bool tagSinceBreak = false, hyphenBreak = false;
        size_t breakPos = std::string::npos;
        float widthAtBreak = 0, x = 0, lineWidth = 0;
        unsigned char prev = 0;
        size_t lineEnd = 0;
        bool wrapped = false;
        while (true) {
            lineWidth = x;
            if (p >= s.size()) {
                lineEnd = p;
                break;
            }
            const unsigned char c = static_cast<unsigned char>(s[p]);
            if (c == ' ' || c == '\t' || prev == '-') {
                hyphenBreak = prev == '-';
                widthAtBreak = x;
                breakPos = p;
                if (tagSinceBreak) {
                    atBreak = cur;
                    tagSinceBreak = false;
                }
            } else if (c == '\n') {
                lineEnd = p;
                ++p;
                break;
            } else if (c == '<' && style.markup) {
                p = parseTag(s, p, cur, style);
                tagSinceBreak = true;
                continue;
            }
            if (x != 0 && wrap < font->glyph(c).width * cur.size + x) {
                wrapped = true;
                break;
            }
            x += (font->kern(prev, c, kerning) + font->advance(c)) * cur.size;
            ++p;
            prev = c;
        }
        if (wrapped) {
            lineEnd = p;
            if (breakPos != std::string::npos) {
                lineWidth = widthAtBreak;
                cur = atBreak;
                lineEnd = breakPos;
                p = hyphenBreak ? breakPos : breakPos + 1;
            }
        }
        out.lines.push_back({lineStart, lineEnd, 0, 0, lineWidth});
        lineStyles.push_back(lineStyle);
        if (p == lineStart && !wrapped) break;  // guard (an empty string)
    } while (p < s.size());

    // Alignment.
    const float lineH = font->lineHeight(style.size);
    out.lineHeight = lineH;
    const float n = float(out.lines.size());
    float y = 0;
    if (style.align & kAlignMiddle) y = ((n - 1) * style.leading + 1) * lineH * 0.5f;
    else if (style.align & kAlignBottom) y = ((n - 1) * style.leading + 1) * lineH;
    for (auto& l : out.lines) {
        l.x = (style.align & kAlignCenter) ? -l.width * 0.5f : (style.align & kAlignRight) ? -l.width : 0;
        l.y = y;
        y -= style.leading * lineH;
    }

    // Quads (0x821CFB28).
    for (size_t li = 0; li < out.lines.size(); ++li) {
        const TextLine& l = out.lines[li];
        RunStyle rs = lineStyles[li];
        float x = l.x;
        const float top = l.y;
        unsigned char prev = 0;
        for (size_t q = l.begin; q < l.end && q < s.size(); ++q) {
            const unsigned char c = static_cast<unsigned char>(s[q]);
            if (c == '<' && style.markup) {
                q = parseTag(s, q, rs, style) - 1;
                continue;
            }
            const float gw = font->glyph(c).width * rs.size;
            x += font->kern(prev, c, kerning) * rs.size;
            if (gw > 0 && out.verts.size() + 4 <= 65536) {
                const float sl = rs.size * rs.italics, lh = font->lineHeight(rs.size);
                float u0, v0, u1, v1;
                font->uv(c, u0, v0, u1, v1);
                const uint16_t b = uint16_t(out.verts.size());
                auto vert = [&](float px, float pz, float u, float v) {
                    scene::MeshVertex mv{};
                    mv.pos[0] = px, mv.pos[1] = 0, mv.pos[2] = pz;
                    mv.normal[0] = 0, mv.normal[1] = -1, mv.normal[2] = 0;
                    for (int k = 0; k < 4; ++k) mv.color[k] = rs.color[size_t(k)];
                    mv.uv[0] = u, mv.uv[1] = v;
                    out.verts.push_back(mv);
                };
                vert(x + sl, top, u0, v0);
                vert(x - sl, top - lh, u0, v1);
                vert(x - sl + gw, top - lh, u1, v1);
                vert(x + gw + sl, top, u1, v0);
                out.faces.push_back({b, uint16_t(b + 1), uint16_t(b + 2)});
                out.faces.push_back({b, uint16_t(b + 2), uint16_t(b + 3)});
            }
            x += font->advance(c) * rs.size;
            prev = c;
        }
    }
    return out;
}

// ---- Jitter and fit ---------------------------------------------------------

HmxRand::HmxRand(uint32_t seed) {
    for (uint32_t& t : table_) {
        const uint32_t a = seed * 0x41c64e6du + 0x3039u;
        seed = a * 0x41c64e6du + 0x3039u;
        t = (seed & 0x7fff0000u) | (a >> 16);
    }
}

uint32_t HmxRand::next() {
    table_[a_] ^= table_[b_];
    const uint32_t v = table_[a_];
    if (++a_ > 0xf8) a_ = 0;
    if (++b_ > 0xf8) b_ = 0;
    return v;
}

float HmxRand::range(float lo, float hi) { return float(next() & 0xffff) * 1.5258789e-05f * (hi - lo) + lo; }

void jitterText(TextLayout& layout, uint32_t seed, float deltaX, float deltaZ, float deltaRot) {
    if (deltaX == 0 && deltaZ == 0 && deltaRot == 0) return;
    HmxRand rng(seed);
    for (size_t q = 0; q + 4 <= layout.verts.size(); q += 4) {
        const float dx = rng.range(-deltaX, deltaX);
        const float dz = rng.range(-deltaZ, deltaZ);
        const float a = rng.range(-deltaRot, deltaRot) * 0.017453292f;
        float cx = 0, cz = 0;
        for (size_t k = 0; k < 4; ++k) cx += layout.verts[q + k].pos[0], cz += layout.verts[q + k].pos[2];
        cx *= 0.25f, cz *= 0.25f;
        // Rotation about y (0x8214C240: rows (cos 0 -sin) (0 1 0) (sin 0 cos)).
        const float co = std::cos(a), si = std::sin(a);
        for (size_t k = 0; k < 4; ++k) {
            float& px = layout.verts[q + k].pos[0];
            float& pz = layout.verts[q + k].pos[2];
            const float x = px - cx, z = pz - cz;
            px = x * co + z * si + cx + dx;
            pz = -x * si + z * co + cz + dz;
        }
    }
}

std::array<float, 2> fitScale(const TextLayout& layout, const TextStyle& style, uint32_t mode, float width, float height) {
    std::array<float, 2> s{1, 1};
    if (mode == 0 || layout.text.empty()) return s;
    const float w = layout.maxWidth();
    if (!(w > 0)) return s;
    float sx = width / w, sz = 1;
    float top, bottom;
    layout.extent(style, top, bottom);
    const float h = std::fabs(bottom - top);
    if (h > 0 && height > 0) sz = height / h;
    if (mode == 2) {
        const float k = std::min({sx, sz, 1.0f});
        sx = sz = k;
    }
    return {sx, sz};
}

// ---- Drawing ----------------------------------------------------------------

struct TextDrawer::Entry {
    const void* key = nullptr;
    std::vector<scene::MeshVertex> verts;
    std::string text;
    GpuMesh gpu;
    bool used = false;
};

TextDrawer::TextDrawer(SceneCache& cache) : cache_(cache) {}

TextDrawer::~TextDrawer() {
    for (auto& e : entries_) {
        if (e->gpu.vertices) cache_.device().destroyBuffer(e->gpu.vertices);
        if (e->gpu.indices) cache_.device().destroyBuffer(e->gpu.indices);
    }
}

void TextDrawer::collect() {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (!(*it)->used) {
            if ((*it)->gpu.vertices) cache_.device().destroyBuffer((*it)->gpu.vertices);
            if ((*it)->gpu.indices) cache_.device().destroyBuffer((*it)->gpu.indices);
            it = entries_.erase(it);
        } else {
            (*it)->used = false;
            ++it;
        }
    }
}

namespace {
bool sameVerts(const std::vector<scene::MeshVertex>& a, const std::vector<scene::MeshVertex>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(scene::MeshVertex)) == 0);
}
}  // namespace

void TextDrawer::draw(MaterialRenderer& r, const void* key, const scene::Font& font, const TextLayout& layout, const Mat4& world,
                      const float* matColor, const float* tint) {
    if (layout.faces.empty()) return;
    Entry* e = nullptr;
    for (auto& x : entries_)
        if (x->key == key) e = x.get();
    if (!e) {
        entries_.push_back(std::make_unique<Entry>());
        e = entries_.back().get();
        e->key = key;
    }
    e->used = true;
    if (!e->gpu.vertices || !sameVerts(e->verts, layout.verts)) {
        if (e->gpu.vertices) cache_.device().destroyBuffer(e->gpu.vertices);
        if (e->gpu.indices) cache_.device().destroyBuffer(e->gpu.indices);
        scene::Mesh m;
        m.verts = layout.verts;
        m.faces = layout.faces;
        const std::vector<float> v = packVertices(m);
        e->gpu = GpuMesh{};
        e->gpu.vertices = cache_.device().createBuffer(BufferKind::Vertex, v.size() * sizeof(float), v.data(), false);
        e->gpu.indices = cache_.device().createBuffer(BufferKind::Index, layout.faces.size() * 6, layout.faces.data(), false);
        e->gpu.indexCount = int(layout.faces.size() * 3);
        e->gpu.tangents = true;
        e->verts = layout.verts;
    }
    const scene::Mat* mat = font.dir ? font.dir->resolve<scene::Mat>(font.mat) : nullptr;
    const Mat4 bones[1] = {world};
    if (!mat) {
        Material plain;
        r.draw(e->gpu, bones, plain, {1});
        return;
    }
    for (int pass = 0; mat && pass < 4; ++pass) {
        Material m = cache_.material(*mat);
        if (matColor && pass == 0)
            for (int k = 0; k < 3; ++k) m.color[k] = matColor[k];
        if (tint) {
            for (int k = 0; k < 3; ++k) m.color[k] *= tint[k];
            m.alpha *= tint[3];
        }
        r.draw(e->gpu, bones, m, {1});
        if (mat->nextPass.empty() || !mat->dir) break;
        const scene::Mat* next = mat->dir->resolve<scene::Mat>(mat->nextPass);
        mat = next != mat ? next : nullptr;
    }
}

}  // namespace gfx
