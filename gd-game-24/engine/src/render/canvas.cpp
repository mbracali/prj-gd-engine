// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/canvas.h"

namespace gr = gd::core::render;

namespace {

// UTF-8 to the 8 x 8 font's ASCII: Latin-1 letters lose their accents
// ("Motörhead" -> "Motorhead"), anything else is '?'.
std::string toAscii(const std::string& s) {
    static const char kLatin1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out.push_back(char(c));
        } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            const unsigned cp = (c & 0x1Fu) << 6 | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
            ++i;
            out.push_back(cp >= 0xC0 && cp <= 0xFF ? kLatin1[cp - 0xC0] : cp == 0xA0 ? ' ' : '?');
        } else {
            // Longer sequences: ™ (E2 84 A2) and the like.
            const int extra = (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 0;
            i += size_t(extra);
            if (c == 0xE2 && extra == 2 && i < s.size() && static_cast<unsigned char>(s[i]) == 0xA2) out += "(TM)";
            else if (c == 0xE2) out.push_back('\'');  // ' ’ “ ” and dashes
            else out.push_back('?');
        }
    }
    return out;
}

}  // namespace

Canvas::~Canvas() { shutdown(); }

bool Canvas::init(SDL_Window* window, gr::Backend backend, std::string& error) {
    window_ = window;
    surface_ = SDL_CreateSurface(kW, kH, SDL_PIXELFORMAT_RGBA32);
    soft_ = surface_ ? SDL_CreateSoftwareRenderer(surface_) : nullptr;
    if (!soft_) {
        error = std::string("software canvas: ") + SDL_GetError();
        return false;
    }
    SDL_SetRenderDrawBlendMode(soft_, SDL_BLENDMODE_BLEND);
    gr::DeviceCreateInfo info;
    info.backend = backend;
    info.window = window;
    info.gl.createContext = [](SDL_Window* w) -> void* { return SDL_GL_CreateContext(w); };
    info.gl.destroyContext = [](void* c) { SDL_GL_DestroyContext(static_cast<SDL_GLContext>(c)); };
    info.gl.swapWindow = [](SDL_Window* w) { return SDL_GL_SwapWindow(w); };
    dev_ = gr::createDevice(info, error);
    if (!dev_) return false;
    gr::TextureDesc td;
    td.width = kW, td.height = kH, td.format = gr::Format::RGBA8, td.usage = gr::kSampledTexture;
    td.label = "gd24 canvas";
    tex_ = dev_->createTexture(td);
    if (!tex_.id) {
        error = "canvas texture: " + dev_->lastError();
        dev_.reset();
        return false;
    }
    return true;
}

void Canvas::shutdown() {
    if (dev_) {
        if (tex_.id) dev_->destroyTexture(tex_);
        tex_ = {};
        dev_.reset();
    }
    if (soft_) SDL_DestroyRenderer(soft_);
    soft_ = nullptr;
    if (surface_) SDL_DestroySurface(surface_);
    surface_ = nullptr;
}

void Canvas::clear(Rgba c) {
    SDL_SetRenderDrawColor(soft_, c.r, c.g, c.b, 255);
    SDL_RenderClear(soft_);
}

void Canvas::clearTransparent() {
    SDL_SetRenderDrawColor(soft_, 0, 0, 0, 0);
    SDL_RenderClear(soft_);
}

void Canvas::fill(float x, float y, float w, float h, Rgba c) {
    SDL_SetRenderDrawColor(soft_, c.r, c.g, c.b, c.a);
    const SDL_FRect r{x, y, w, h};
    SDL_RenderFillRect(soft_, &r);
}

void Canvas::frame(float x, float y, float w, float h, Rgba c) {
    SDL_SetRenderDrawColor(soft_, c.r, c.g, c.b, c.a);
    const SDL_FRect r{x, y, w, h};
    SDL_RenderRect(soft_, &r);
}

float Canvas::textWidth(const std::string& s, float scale) {
    return float(toAscii(s).size()) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * scale;
}

float Canvas::text(float x, float y, const std::string& s, Rgba c, float scale) {
    const std::string a = toAscii(s);
    SDL_SetRenderDrawColor(soft_, c.r, c.g, c.b, c.a);
    SDL_SetRenderScale(soft_, scale, scale);
    SDL_RenderDebugText(soft_, x / scale, y / scale, a.c_str());
    SDL_SetRenderScale(soft_, 1, 1);
    return float(a.size()) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * scale;
}

void Canvas::present() {
    SDL_FlushRenderer(soft_);
    dev_->beginFrame();
    dev_->updateTexture(gr::TextureRegion{tex_}, kW, kH, surface_->pixels, size_t(surface_->pitch));
    dev_->present(tex_, gr::Rect{0, 0, kW, kH}, gr::Filter::Linear);
    dev_->endFrame();
    last3d_ = false;
}

void Canvas::present3d(const Scene& scene) {
    SDL_FlushRenderer(soft_);
    dev_->beginFrame();
    dev_->updateTexture(gr::TextureRegion{tex_}, kW, kH, surface_->pixels, size_t(surface_->pitch));
    shown3d_ = scene(*dev_, tex_);
    const gr::TextureDesc d = dev_->textureDesc(shown3d_);
    dev_->present(shown3d_, gr::Rect{0, 0, d.width, d.height}, gr::Filter::Linear);
    dev_->endFrame();
    last3d_ = true;
}

bool Canvas::save(const std::string& path) const {
    SDL_FlushRenderer(soft_);
    if (last3d_ && shown3d_) {
        // The 3D picture as shown (rows come back top-down).
        const gr::TextureDesc d = dev_->textureDesc(shown3d_);
        const gr::Readback rb = dev_->readTexture(shown3d_, gr::Rect{0, 0, d.width, d.height});
        SDL_Surface* s = rb ? SDL_CreateSurface(d.width, d.height, SDL_PIXELFORMAT_RGBA32) : nullptr;
        bool ok = s && dev_->readbackData(rb, s->pixels, size_t(d.width) * size_t(d.height) * 4);
        if (s) {
            for (int y = 0; ok && y < d.height; ++y) {  // opaque for the BMP
                uint8_t* row = static_cast<uint8_t*>(s->pixels) + size_t(y) * size_t(s->pitch);
                for (int x = 0; x < d.width; ++x) row[x * 4 + 3] = 255;
            }
            ok = ok && SDL_SaveBMP(s, path.c_str());
            SDL_DestroySurface(s);
        }
        return ok;
    }
    return SDL_SaveBMP(surface_, path.c_str());
}

void Canvas::windowToCanvas(float wx, float wy, float& cx, float& cy) const {
    int w = 1, h = 1;
    SDL_GetWindowSize(window_, &w, &h);
    cx = wx * float(kW) / float(w > 0 ? w : 1);
    cy = wy * float(kH) / float(h > 0 ? h : 1);
}
