// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class Canvas;
namespace ui {
class UiSystem;
}

// Drawing for the port's own screens (GAMEPADS and Enhanced Options; not in
// the original game): gd01's widgets (gd-game-01/engine/src/ui/
// select_panels.cpp and gamepad_panel.cpp), as gd02 ported them
// (gd-game-02/engine/src/ui/port_draw.*), in GH3's menu fonts and colours, in
// menu units (1280 x 720). The shared look of every gd-game's two screens is
// guidelines/universal_launcher.md. Text heights are line heights, as gd01's
// drawText takes them; GH3's fonts are scaled so their line height is that
// height. The page slides and fades with the screen transition (offset,
// fade), as gd01's do. Unlike gd02 there is one pass: the canvas batches by
// texture and keeps the order.
class PortDraw {
public:
    using Color = std::array<float, 4>;
    PortDraw(Canvas& c, const ui::UiSystem& ui) : c_(c), ui_(ui) {}

    void setTransition(float offset, float fade) { offset_ = offset, fade_ = fade; }
    float offset() const { return offset_; }

    void quad(float x0, float y0, float x1, float y1, const Color& c);
    void frame(float x0, float y0, float x1, float y1, float t, const Color& c);
    void line(float x0, float y0, float x1, float y1, float w, const Color& c);
    void triangle(float ax, float ay, float bx, float by, float cx, float cy, const Color& c);
    void arrow(float x, float y, bool up, const Color& c);
    // gd01's yellow warning sign: not built yet.
    void warning(float x, float cy);
    // A check box (frame and tick), right-aligned at x1, centred on cy.
    void checkBox(float x1, float cy, bool on, const Color& frameColor, const Color& tick);
    // `title`: GH3's title font (fontgrid_title_gh3, the main menu's), else
    // its body font (text_a5, the options menu's).
    void text(const std::string& s, float x, float y, float h, const Color& c, uint32_t align, bool title = false);
    float width(const std::string& s, float h, bool title = false) const;
    std::string fit(std::string s, float h, float width, bool title = false) const;
    std::vector<std::string> wrap(const std::string& s, float width, float h) const;

    // GH3's palette [C data, guitar_menu.qb]: menu_text_color (the main
    // menu's tan) for titles and headings, menu_unfocus_color (the options'
    // orange) for the accent and the focus bar, menu_focus_color (light grey)
    // for the focused text.
    static constexpr Color kTitle{215 / 255.f, 160 / 255.f, 110 / 255.f, 1}, kAccent{210 / 255.f, 130 / 255.f, 0, 1},
        kGold{240 / 255.f, 180 / 255.f, 60 / 255.f, 1}, kFocused{1, 1, 1, 1}, kNormal{0.86f, 0.84f, 0.8f, 1}, kDim{0.5f, 0.5f, 0.52f, 1},
        kOn{0.3f, 1, 0.35f, 1}, kWarn{1, 0.78f, 0.1f, 1}, kFocusBar{210 / 255.f, 130 / 255.f, 0, 0.22f}, kBox{0, 0, 0, 0.66f},
        kEdge{1, 1, 1, 0.12f}, kP1{0.45f, 0.75f, 1, 1}, kP2{1, 0.5f, 0.45f, 1};

private:
    Color faded(Color c) const { return c[3] *= fade_, c; }
    Canvas& c_;
    const ui::UiSystem& ui_;
    float offset_ = 0, fade_ = 1;
};
