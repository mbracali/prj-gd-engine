// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/canvas.h"

#include <vector>

Canvas::~Canvas() { shutdown(); }

bool Canvas::init(SDL_Window* window, std::string& error) {
    renderer_ = SDL_CreateRenderer(window, nullptr);
    if (!renderer_) {
        error = SDL_GetError();
        return false;
    }
    SDL_SetRenderVSync(renderer_, 1);
    SDL_SetRenderLogicalPresentation(renderer_, int(kScreenW), int(kScreenH), SDL_LOGICAL_PRESENTATION_LETTERBOX);
    return true;
}

void Canvas::shutdown() {
    if (renderer_) SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
}

void Canvas::begin(SDL_Color clear) {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    fillRect(0, 0, kScreenW, kScreenH, clear);
}

void Canvas::end() { SDL_RenderPresent(renderer_); }

void Canvas::draw(const Vertex* v, size_t count, int, Blend blend) {
    if (count == 0) return;
    std::vector<SDL_Vertex> out(count);
    for (size_t i = 0; i < count; ++i)
        out[i] = {{v[i].x, v[i].y}, {v[i].r / 255.0f, v[i].g / 255.0f, v[i].b / 255.0f, v[i].a / 255.0f}, {v[i].u, v[i].v}};
    SDL_SetRenderDrawBlendMode(renderer_, blend == Blend::Opaque ? SDL_BLENDMODE_NONE
                                          : blend == Blend::Add  ? SDL_BLENDMODE_ADD
                                                                 : SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(renderer_, nullptr, out.data(), int(out.size()), nullptr, 0);
}

void Canvas::fillRect(float x, float y, float w, float h, SDL_Color c) {
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
    SDL_FRect r{x, y, w, h};
    SDL_RenderFillRect(renderer_, &r);
}

void Canvas::text(float x, float y, float scale, SDL_Color c, const std::string& s) {
    std::string ascii = s;
    for (char& ch : ascii)
        if (static_cast<unsigned char>(ch) >= 0x80) ch = '?';
    float sx = 1, sy = 1;
    SDL_GetRenderScale(renderer_, &sx, &sy);
    SDL_SetRenderScale(renderer_, scale, scale);
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
    SDL_RenderDebugText(renderer_, x / scale, y / scale, ascii.c_str());
    SDL_SetRenderScale(renderer_, sx, sy);
}

void Canvas::textCentered(float cx, float y, float scale, SDL_Color c, const std::string& s) {
    text(cx - float(s.size()) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * scale * 0.5f, y, scale, c, s);
}

SDL_Surface* Canvas::readPixels() { return SDL_RenderReadPixels(renderer_, nullptr); }
