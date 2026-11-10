// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "render/text_layout.h"

class GameData;

namespace gfx {
class MaterialRenderer;
class Renderer;
class SceneCache;
}  // namespace gfx
namespace scene {
class Font;
class MiloLibrary;
}  // namespace scene

namespace ui {

// Text in one of GH2's UI fonts (ui/gen/<font>.milo_xbox, e.g. "impactor",
// "rokk") placed in 1280 x 720 screen units through GH2's ui camera, for the
// port's own screens (the Gamepads screen) and development overlays.
class ScreenText {
public:
    ScreenText(const GameData& data, scene::MiloLibrary& library, gfx::SceneCache& cache);
    ~ScreenText();

    // Sets the ui camera (call once before a run of draws).
    void begin(gfx::Renderer& r);
    // `align`: gfx::TextAlign flags (0x11 top left .. 0x44 bottom right) at (x, y).
    void draw(gfx::MaterialRenderer& r, const std::string& font, const std::string& text, float x, float y, float height,
              const std::array<float, 4>& rgba, uint32_t align);
    float width(const std::string& font, const std::string& text, float height);
    // Frees the layouts not drawn since the last call.
    void collect();

private:
    struct Entry;
    const scene::Font* font(const std::string& name);
    const GameData& data_;
    scene::MiloLibrary& lib_;
    gfx::TextDrawer drawer_;
    std::map<std::string, const scene::Font*> fonts_;
    std::map<std::string, std::unique_ptr<Entry>> entries_;
    uint64_t frame_ = 0;
};

}  // namespace ui
