// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <map>
#include <memory>
#include <string>

#include "data/dta.h"
#include "render/scene_renderer.h"
#include "render/text_layout.h"

class GameData;
class ScriptLoader;

// Shared UI data: fonts and text templates (ghui/gen/resources.rnd_ps2),
// component styles (ghui/config.dta "components") and localized strings
// (ghui/eng/locale.dta).
class UiResources {
public:
    UiResources(Renderer& renderer, const GameData& data, ScriptLoader& loader);

    enum State { Normal, Focused, Selected, Disabled };
    struct Style {
        std::string textTemplate;  // a Text object of resources.rnd_ps2
        std::string font;          // overrides the template's font when set
        std::array<std::array<float, 3>, 4> colors{};  // per State
        std::string characters;  // TextEntry: the letters it cycles through
        int length = 0;          // TextEntry: the longest text
        std::string mesh, disabledMesh;  // PictureEx / PictureToggle: resources meshes
        std::string texOn, texOff;
        // Slider: resources parts and the track's per-state colour (mesh_color).
        std::string sliderMesh, podMesh;
        std::array<std::array<float, 3>, 4> meshColors{};       // PictureToggle: ghui/<tex_path>/ pictures for on / off
    };
    // `component` is the config name ("labelex", "buttonex"), `style` the
    // component's style symbol.
    const Style& style(const std::string& component, const std::string& style);

    const TextObj* textTemplate(const std::string& name) const;
    const FontMetrics* font(const std::string& name);
    int fontTexture(const std::string& name);  // the glyph sheet's texture handle
    int fontZMode(const std::string& name) const;
    bool fontUsesEnviron(const std::string& name) const;
    // The font material's colour (the credits' text colours).
    std::array<float, 4> fontMatColor(const std::string& name) const;  // the font material's z-mode (text uses it)
    std::string localize(const std::string& token) const;
    // Draws a resources.rnd_ps2 mesh (PictureEx art) with the component's
    // transform in place of the mesh's own world (its local stays).
    void drawMesh(const std::string& mesh, const Mat4& viewProj, const Mat4& world, int texture = 0,
                  const std::array<float, 3>& ambient = {1, 1, 1});
    // Draws a resources.rnd_ps2 mesh at exactly `world` (none of its stored transform).
    // `ambient` (the panel's transition fade) scales lit materials only.
    void drawMeshAt(const std::string& mesh, const Mat4& viewProj, const Mat4& world,
                    const std::array<float, 3>& tint = {1, 1, 1}, const std::array<float, 3>& ambient = {1, 1, 1});
    const Scene& scene() const { return *scene_; }
    const std::map<std::string, std::string>& strings() const { return strings_; }

private:
    Style parseStyle(const DataNode* component, const DataNode* style) const;
    std::array<std::array<float, 3>, 4> parseColors(const DataNode* colors, const std::array<std::array<float, 3>, 4>& base,
                                                    const char* key = "text_color") const;

    std::unique_ptr<Scene> scene_;
    std::unique_ptr<SceneRenderer> renderer_;
    DataNode config_;
    std::array<std::array<float, 3>, 4> textColors_{};
    std::map<std::string, Style> styles_;
    std::map<std::string, std::unique_ptr<FontMetrics>> fonts_;
    std::map<std::string, std::string> strings_;
};
