// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/screen_text.h"

#include <cmath>

#include "data/game_data.h"
#include "render/renderer.h"
#include "render/scene_gpu.h"
#include "render/ui_view.h"
#include "scene/milo.h"

namespace ui {

namespace {

// `height` is the capital letters' height (gd01's drawText): a font's line
// is about 1.45 times it [I].
constexpr float kLineToCap = 1.45f;
// GH2's ui camera (config/default.dta): y -768, vertical fov 34.516 degrees.
// World units per screen unit at the y = 0 plane.
float unitsPerPixel() {
    static const float k = 2.0f * 768.0f * std::tan(34.516f * 3.14159265f / 360.0f) / 720.0f;
    return k;
}

}  // namespace

struct ScreenText::Entry {
    gfx::TextLayout layout;
    float size = 1;
    uint64_t used = 0;
};

ScreenText::ScreenText(const GameData& data, scene::MiloLibrary& library, gfx::SceneCache& cache)
    : data_(data), lib_(library), drawer_(cache) {}

ScreenText::~ScreenText() = default;

const scene::Font* ScreenText::font(const std::string& name) {
    std::string base = name;
    if (const size_t dot = base.find('.'); dot != std::string::npos) base = base.substr(0, dot);
    if (auto it = fonts_.find(base); it != fonts_.end()) return it->second;
    const scene::Font* f = nullptr;
    const std::string path = "ui/gen/" + base + ".milo_xbox";
    try {
        if (data_.exists(path)) f = lib_.load(path).find<scene::Font>(base + ".font");
    } catch (const std::exception&) {
        f = nullptr;
    }
    fonts_[base] = f;
    return f;
}

void ScreenText::begin(gfx::Renderer& r) {
    int vx, vy, vw, vh;
    r.pictureRect(vx, vy, vw, vh);
    r.materials().setFrame(gfx::UiView::defaultCamera(), gfx::LightEnv{}, vw, vh);
}

float ScreenText::width(const std::string& fontName, const std::string& text, float height) {
    const scene::Font* f = font(fontName);
    if (!f || text.empty()) return 0;
    gfx::FontMetrics fm(*f);
    gfx::TextStyle st;
    st.size = kLineToCap * height * unitsPerPixel() / std::max(1e-4f, fm.lineHeight(1));
    return gfx::layoutText(&fm, text, st).maxWidth() / unitsPerPixel();
}

void ScreenText::draw(gfx::MaterialRenderer& r, const std::string& fontName, const std::string& text, float x, float y, float height,
                      const std::array<float, 4>& rgba, uint32_t align) {
    const scene::Font* f = font(fontName);
    if (!f || text.empty() || rgba[3] <= 0) return;
    const std::string key = fontName + "|" + std::to_string(int(height * 10)) + "|" + std::to_string(align) + "|" + text;
    auto& e = entries_[key];
    if (!e) {
        e = std::make_unique<Entry>();
        gfx::FontMetrics fm(*f);
        gfx::TextStyle st;
        st.size = kLineToCap * height * unitsPerPixel() / std::max(1e-4f, fm.lineHeight(1));
        st.align = align;
        e->layout = gfx::layoutText(&fm, text, st);
        e->size = st.size;
    }
    e->used = frame_;
    scene::Xfm at;
    at.pos = {(x - 640.0f) * unitsPerPixel(), 0, (360.0f - y) * unitsPerPixel()};
    const float rgb[3] = {rgba[0], rgba[1], rgba[2]};
    const float tint[4] = {1, 1, 1, rgba[3]};
    drawer_.draw(r, e.get(), *f, e->layout, gfx::toMat4(at), rgb, tint);
}

void ScreenText::collect() {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (frame_ - it->second->used > 120) it = entries_.erase(it);
        else ++it;
    }
    drawer_.collect();
    ++frame_;
}

}  // namespace ui
