// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/canvas.h"
#include "render/rmath.h"

#include <cstddef>
#include <cstring>

namespace {

// The atlas: SDL's 8x8 debug font, ASCII 32..127 in a 16 x 6 grid, and an
// 8x8 white block below it that plain-colour draws sample.
constexpr int kGlyph = 8, kCols = 16, kRows = 6, kAtlasW = kCols * kGlyph, kAtlasH = (kRows + 1) * kGlyph;
constexpr float kWhiteU = (kGlyph * 0.5f) / kAtlasW, kWhiteV = (kRows * kGlyph + kGlyph * 0.5f) / kAtlasH;

const gfx::VertexLayout& canvasLayout() {
    static const gfx::VertexLayout layout = [] {
        gfx::VertexLayout l;
        l.stride = sizeof(Vertex);
        l.attribs[0] = {gfx::kAttribPos, 3, gfx::AttribType::Float, uint16_t(offsetof(Vertex, x))};
        l.attribs[1] = {gfx::kAttribUv, 2, gfx::AttribType::Float, uint16_t(offsetof(Vertex, u))};
        l.attribs[2] = {gfx::kAttribColor, 4, gfx::AttribType::UNorm8, uint16_t(offsetof(Vertex, r))};
        return l;
    }();
    return layout;
}

}  // namespace

Canvas::~Canvas() { shutdown(); }

bool Canvas::init(SDL_Window* window, std::string& error) {
    if (!renderer_.init(window, error)) return false;
    if (!makeFont(error)) {
        renderer_.shutdown();
        return false;
    }
    buffer_ = renderer_.device().createBuffer(gfx::BufferKind::Vertex, 256 * 1024, nullptr, true);
    return true;
}

bool Canvas::makeFont(std::string& error) {
    SDL_Surface* s = SDL_CreateSurface(kAtlasW, kAtlasH, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* soft = s ? SDL_CreateSoftwareRenderer(s) : nullptr;
    if (!soft) {
        error = std::string("debug font: ") + SDL_GetError();
        if (s) SDL_DestroySurface(s);
        return false;
    }
    SDL_SetRenderDrawColor(soft, 0, 0, 0, 0);
    SDL_RenderClear(soft);
    SDL_SetRenderDrawColor(soft, 255, 255, 255, 255);
    for (int i = 0; i < kCols * kRows; ++i) {
        const char glyph[2] = {char(32 + i), 0};
        SDL_RenderDebugText(soft, float(i % kCols * kGlyph), float(i / kCols * kGlyph), glyph);
    }
    const SDL_FRect white{0, float(kRows * kGlyph), float(kGlyph), float(kGlyph)};
    SDL_RenderFillRect(soft, &white);
    SDL_RenderPresent(soft);
    SDL_DestroyRenderer(soft);

    std::vector<uint8_t> pixels(size_t(kAtlasW) * kAtlasH * 4);
    for (int y = 0; y < kAtlasH; ++y)
        std::memcpy(&pixels[size_t(y) * kAtlasW * 4], static_cast<uint8_t*>(s->pixels) + size_t(y) * size_t(s->pitch), size_t(kAtlasW) * 4);
    SDL_DestroySurface(s);
    const gfx::TexLevel level{pixels.data(), pixels.size()};
    atlas_ = renderer_.device().createTexture({kAtlasW, kAtlasH, 1, gfx::TexFormat::RGBA8, gfx::TexKind::Tex2D}, &level, error);
    return bool(atlas_);
}

void Canvas::shutdown() {
    if (!renderer_.ready()) return;
    renderer_.device().destroyBuffer(buffer_);
    renderer_.device().destroyTexture(atlas_);
    buffer_ = {}, atlas_ = {};
    renderer_.shutdown();
}

void Canvas::begin(SDL_Color clear) {
    beginNs_ = SDL_GetTicksNS();
    const float c[4] = {clear.r / 255.0f, clear.g / 255.0f, clear.b / 255.0f, 1};
    renderer_.beginFrame(c);
    batch_.clear();
    bufferUsed_ = 0;
}

void Canvas::end() {
    flush();
    drawNs += SDL_GetTicksNS() - beginNs_;
    ++drawnFrames;
    renderer_.endFrame();
}

void Canvas::flush() {
    if (batch_.empty()) return;
    gfx::RenderDevice& d = renderer_.device();
    const size_t bytes = batch_.size() * sizeof(Vertex);
    // Each flush appends; the first of a frame (offset 0) renames the buffer.
    d.updateBuffer(buffer_, bufferUsed_, batch_.data(), bytes);

    gfx::DrawCall c;
    c.program = renderer_.canvasProgram();
    c.layout = &canvasLayout();
    c.vertices = buffer_;
    c.vertexOffset = bufferUsed_;
    c.count = int(batch_.size());
    c.state.blend = batchBlend_ != Blend::Opaque;
    c.state.src = gfx::BlendFactor::SrcAlpha;
    c.state.dst = batchBlend_ == Blend::Add ? gfx::BlendFactor::One : gfx::BlendFactor::InvSrcAlpha;
    c.textures[0] = gfx::TextureId{batchTexture_ ? batchTexture_ : atlas_.id};
    c.samplers[0] = {batchTexture_ ? gfx::Filter::Linear : gfx::Filter::Nearest, batchRepeat_ ? gfx::Wrap::Repeat : gfx::Wrap::Clamp, 0};
    const gfx::Mat4 proj = gfx::Mat4::ortho2D(0, kScreenW, 0, kScreenH);
    c.uniforms[0] = {proj.m, sizeof proj.m};
    d.draw(c);

    bufferUsed_ += bytes;
    batch_.clear();
}

void Canvas::draw(const Vertex* v, size_t count, int texture, Blend blend, bool repeat) {
    if (count == 0) return;
    const uint32_t tex = uint32_t(texture);
    if (!batch_.empty() && (tex != batchTexture_ || blend != batchBlend_ || repeat != batchRepeat_)) flush();
    batchTexture_ = tex, batchBlend_ = blend, batchRepeat_ = repeat;
    const size_t at = batch_.size();
    batch_.insert(batch_.end(), v, v + count);
    if (tex == 0)
        for (size_t i = at; i < batch_.size(); ++i) batch_[i].u = kWhiteU, batch_[i].v = kWhiteV;
}

void Canvas::fillRect(float x, float y, float w, float h, SDL_Color c) {
    const Vertex a{x, y, 0, 0, 0, c.r, c.g, c.b, c.a}, b{x + w, y, 0, 0, 0, c.r, c.g, c.b, c.a};
    const Vertex d{x + w, y + h, 0, 0, 0, c.r, c.g, c.b, c.a}, e{x, y + h, 0, 0, 0, c.r, c.g, c.b, c.a};
    const Vertex quad[6] = {a, b, d, a, d, e};
    draw(quad, 6, 0, Blend::Alpha);
}

void Canvas::text(float x, float y, float scale, SDL_Color c, const std::string& s) {
    std::vector<Vertex> quads;
    quads.reserve(s.size() * 6);
    const float g = kGlyph * scale;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned ch = static_cast<unsigned char>(s[i]);
        if (ch == ' ') continue;
        if (ch < 32 || ch > 127) ch = '?';
        const int idx = int(ch) - 32;
        const float u0 = float(idx % kCols * kGlyph) / kAtlasW, v0 = float(idx / kCols * kGlyph) / kAtlasH;
        const float u1 = u0 + float(kGlyph) / kAtlasW, v1 = v0 + float(kGlyph) / kAtlasH;
        const float x0 = x + float(i) * g, x1 = x0 + g, y1 = y + g;
        const Vertex a{x0, y, 0, u0, v0, c.r, c.g, c.b, c.a}, b{x1, y, 0, u1, v0, c.r, c.g, c.b, c.a};
        const Vertex d{x1, y1, 0, u1, v1, c.r, c.g, c.b, c.a}, e{x0, y1, 0, u0, v1, c.r, c.g, c.b, c.a};
        quads.insert(quads.end(), {a, b, d, a, d, e});
    }
    if (quads.empty()) return;
    // The font lives in the atlas, which is also what plain colour uses.
    if (!batch_.empty() && (batchTexture_ != 0 || batchBlend_ != Blend::Alpha || batchRepeat_)) flush();
    batchTexture_ = 0, batchBlend_ = Blend::Alpha, batchRepeat_ = false;
    batch_.insert(batch_.end(), quads.begin(), quads.end());
}

void Canvas::textCentered(float cx, float y, float scale, SDL_Color c, const std::string& s) {
    text(cx - float(s.size()) * kGlyph * scale * 0.5f, y, scale, c, s);
}

SDL_Surface* Canvas::readPixels() {
    flush();
    return renderer_.readPixels();
}
