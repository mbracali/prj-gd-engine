// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ui {

class UiManager;

// Drawing for the port's own screens (GAMEPADS and Enhanced Options), the
// widgets of gd01's (gd-game-01/engine/src/ui/select_panels.cpp and
// gamepad_panel.cpp) in GH2's menu font and colours, in menu units (1280 x
// 720). The shared look of every gd-game's two screens is
// guidelines/universal_launcher.md. A frame draws in two
// passes (begin(0): the quads, which the canvas batches; begin(1): the
// texts); each call draws only in its pass. Text heights are line heights,
// as gd01's drawText takes them. The page slides and fades with the screen
// transition, as gd01's do.
class PortDraw {
public:
    using Color = std::array<float, 4>;
    explicit PortDraw(UiManager& ui) : ui_(ui) {}

    // The port's screens are laid out on the whole 16:9 frame, whatever
    // the picture's aspect (4:3 pillarboxed or 16:9): while one of these
    // lives, the canvas and the menu font draw on the frame.
    class FullFrame {
    public:
        explicit FullFrame(UiManager& ui);
        ~FullFrame();
        FullFrame(const FullFrame&) = delete;
        FullFrame& operator=(const FullFrame&) = delete;

    private:
        UiManager& ui_;
        bool was_;
    };

    // The transition's slide (offset) and fade, from the UI manager.
    void updateTransition();
    float offset() const { return offset_; }
    void begin(int pass);
    int pass() const { return pass_; }

    void quad(float x0, float y0, float x1, float y1, const Color& c);
    void frame(float x0, float y0, float x1, float y1, float t, const Color& c);
    void line(float x0, float y0, float x1, float y1, float w, const Color& c);
    void triangle(float ax, float ay, float bx, float by, float cx, float cy, const Color& c);
    void arrow(float x, float y, bool up, const Color& c);
    // gd01's yellow warning sign: not built yet.
    void warning(float x, float cy);
    // A check box (frame and tick), right-aligned at x1, centred on cy.
    void checkBox(float x1, float cy, bool on, const Color& frameColor, const Color& tick);
    void text(const std::string& s, float x, float y, float h, const Color& c, uint32_t align, const char* font = kFont);
    float width(const std::string& s, float h, const char* font = kFont);
    std::string fit(std::string s, float h, float width, const char* font = kFont);
    std::vector<std::string> wrap(const std::string& s, float width, float h, const char* font = kFont);

    // GH2's menu font and the palette (the options posters' red-orange
    // titles, the buttons' gold, white focus).
    static constexpr const char* kFont = "helveticablack.font";
    static constexpr Color kTitle{0.93f, 0.33f, 0.16f, 1}, kAccent{0.93f, 0.33f, 0.16f, 1}, kGold{0.96f, 0.76f, 0.26f, 1},
        kFocused{1, 1, 1, 1}, kNormal{0.86f, 0.84f, 0.8f, 1}, kDim{0.5f, 0.5f, 0.52f, 1}, kOn{0.3f, 1, 0.35f, 1},
        kWarn{1, 0.78f, 0.1f, 1}, kFocusBar{0.96f, 0.76f, 0.26f, 0.16f}, kBox{0, 0, 0, 0.66f}, kEdge{1, 1, 1, 0.12f},
        kP1{0.45f, 0.75f, 1, 1}, kP2{1, 0.5f, 0.45f, 1};

private:
    Color faded(Color c) const { return c[3] *= fade_, c; }
    UiManager& ui_;
    int pass_ = 0;
    float offset_ = 0, fade_ = 1;
};

}  // namespace ui
