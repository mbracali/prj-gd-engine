// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/gamepad_panel.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "input/input.h"
#include "render/renderer.h"

namespace {

// [I] Not in the original game. One focus runs down the controllers (left)
// and then the selected controller's bindings, the keyboard's "Frets also
// strum" and Reset (right). On a controller, Left / Right (or Select) changes
// its player (none, P1, P2; one controller per player); on a binding, Select
// waits for the new button, axis or key (Esc cancels, Delete or the row's
// mouse "x" leaves it unbound, 10 s timeout). The touch screen (listed while
// touch controls are on) plays as P1 and has its settings instead of
// bindings: Select / Left / Right change them.
class GamepadPanel : public NativePanel {
public:
    GamepadPanel(SelectHost& host, Input* input) : host_(host), input_(input) {}

    void enter() override {
        focus_ = 0, selected_ = 0;
        captureMs_ = 0;
        if (input_) input_->writeReport();
    }
    void exit() override {
        if (input_) input_->cancelCapture(), input_->writeReport();  // with what was pressed here
    }

    bool buttonDown(int button, int player) override {
        (void)player;
        if (!input_) return false;
        if (input_->capturing()) return true;  // other devices wait while one is being bound
        const auto devs = input_->devices();
        clamp(devs);
        const int rows = rowCount(devs);
        if (button == kUp || button == kDown) {
            focus_ = (focus_ + (button == kDown ? 1 : rows - 1)) % rows;
            if (focus_ < int(devs.size())) selected_ = focus_;
            sound("button_toggle");
            return true;
        }
        if (focus_ < int(devs.size()) && (button == kLeft || button == kRight)) {
            setPlayer(devs[size_t(focus_)], std::clamp(devs[size_t(focus_)].player + (button == kRight ? 1 : -1), -1, 1));
            return true;
        }
        if (isPad(devs) && focus_ - int(devs.size()) == resetRow() + 1 && (button == kLeft || button == kRight)) {
            input_->cycleDeviceType(devs[size_t(selected_)].id, button == kRight ? 1 : -1);
            sound("button_select");
            return true;
        }
        if (isTouch(devs) && focus_ >= int(devs.size()) && (button == kLeft || button == kRight)) {
            const int c = focus_ - int(devs.size());
            if (c < Input::kTouchOptions) input_->cycleTouchOption(c, button == kRight ? 1 : -1), sound("button_select");
            return true;
        }
        if (host_.isSelectButton(button)) return activate(devs), true;
        return false;  // back: the screen's auto-back
    }

    bool mouse(float x, float y, bool click) override {
        if (!input_) return false;
        x -= offset_;
        if (input_->capturing()) {
            if (click) input_->cancelCapture(), sound("button_back");
            return true;
        }
        const auto devs = input_->devices();
        clamp(devs);
        for (int r = 0; r < rowCount(devs); ++r) {
            float x0, y0, x1, y1;
            rowRect(r, int(devs.size()), x0, y0, x1, y1, isTouch(devs));
            if (x < x0 || x > x1 || y < y0 || y > y1) continue;
            const int c = r - int(devs.size());
            if (click && !isTouch(devs) && c >= 0 && c < int(Input::Control::Count) && x >= x1 - kClearW) {
                focus_ = r;
                input_->clearBinding(devs[size_t(selected_)].id, Input::Control(c));
                sound("button_back");
                return true;
            }
            if (r != focus_) {
                focus_ = r;
                if (r < int(devs.size())) selected_ = r;
                sound("button_toggle");
            }
            if (click) activate(devs);
            return true;
        }
        return false;
    }

    void poll(float dtMs) override {
        if (input_ && input_->devices().size() != reportedDevices_) {  // a controller came or went: a fresh report
            reportedDevices_ = input_->devices().size();
            input_->writeReport();
        }
        if (input_ && input_->capturing()) {
            captureMs_ += dtMs;
            if (captureMs_ > 10000) input_->cancelCapture(), sound("button_back");
        } else {
            captureMs_ = 0;
        }
    }

    void drawBefore() override {
        updateTransition();
        host_.renderer().fillRect(offset_, 0, kScreenW, kScreenH, {0, 0, 0, uint8_t(255 * fade_)});
    }

    void drawAfter() override {
        if (!input_) return;
        const float o = offset_;
        const auto devs = input_->devices();
        clamp(devs);
        text("rokk.font", "GAMEPADS", 90 + o, 40, 62, kTitle, 0x11);
        text("impactor.font", "Controllers, their players and their buttons", 92 + o, 110, 20, kDim, 0x11);
        text("impactor.font", "Report for bug reports: " + fit(input_->reportPath(), 15, 760), 92 + o, 136, 15, kDim, 0x11);
        // Left: the controllers.
        quad(kLeftX0 + o, kTop - 16, kLeftX1 + o, kBottom, {1, 1, 1, 0.05f});
        text("impactor.font", "CONTROLLERS", kLeftX0 + 16 + o, kTop - 2, 22, kAccent, 0x11);
        for (size_t d = 0; d < devs.size(); ++d) {
            float x0, y0, x1, y1;
            rowRect(int(d), int(devs.size()), x0, y0, x1, y1);
            const bool f = int(d) == focus_, sel = int(d) == selected_;
            if (f) quad(x0 + o, y0, x1 + o, y1, {1, 0.86f, 0.2f, 0.14f});
            else if (sel) quad(x0 + o, y0, x1 + o, y1, {1, 1, 1, 0.06f});
            const float h = y1 - y0;  // rows shrink when many controllers are connected
            if (h >= 50) {
                text("impactor.font", fit(devs[d].name, 21, x1 - x0 - 120), x0 + 14 + o, y0 + 18, 21, f ? kFocused : kNormal, 0x21);
                text("impactor.font", fit(devs[d].profile, 16, x1 - x0 - 120), x0 + 14 + o, y0 + 42, 16, kDim, 0x21);
            } else {
                text("impactor.font", fit(devs[d].name, 19, x1 - x0 - 120), x0 + 14 + o, (y0 + y1) * 0.5f + 1, 19, f ? kFocused : kNormal, 0x21);
            }
            const int pn = devs[d].player;
            const std::string pl = pn < 0 ? "--" : pn == 0 ? "P1" : "P2";
            const bool fixed = devs[d].id == "touch";
            text("impactor.font", f && !fixed ? "< " + pl + " >" : pl, x1 - 44 + o, (y0 + y1) * 0.5f, 22, pn < 0 ? kDim : pn == 0 ? kP1 : kP2, 0x22);
        }
        // The selected controller's raw input, live.
        const std::string& id = devs[size_t(selected_)].id;
        float ry = kBottom - 150;
        text("impactor.font", "LIVE INPUT", kLeftX0 + 16 + o, ry, 18, kAccent, 0x11);
        std::string pressed;
        for (const std::string& n : input_->rawPressed(id)) pressed += (pressed.empty() ? "" : "  ") + n;
        text("impactor.font", id == "touch" ? "(touch the screen)" : pressed.empty() ? "(press something)" : fit(pressed, 18, 420),
             kLeftX0 + 16 + o, ry + 26, 18, pressed.empty() ? kDim : kOn, 0x11);
        const auto axes = input_->rawAxes(id);
        for (size_t a = 0; a < axes.size(); ++a) {
            const float ax = kLeftX0 + 16 + float(a % 2) * 215 + o, ay = ry + 56 + float(a / 2) * 26;
            text("impactor.font", axes[a].first, ax, ay + 9, 14, kDim, 0x21);
            const float bx0 = ax + 96, bx1 = ax + 196, mid = (bx0 + bx1) * 0.5f, v = std::clamp(axes[a].second, -1.0f, 1.0f);
            quad(bx0, ay + 4, bx1, ay + 14, {1, 1, 1, 0.12f});
            quad(std::min(mid, mid + v * 50), ay + 4, std::max(mid, mid + v * 50), ay + 14, kOn);
        }
        // Right: the bindings.
        quad(kRightX0 + o, kTop - 16, kRightX1 + o, kBottom, {1, 1, 1, 0.05f});
        text("impactor.font", "BUTTONS: " + fit(devs[size_t(selected_)].name, 22, 420), kRightX0 + 16 + o, kTop - 2, 22, kAccent, 0x11);
        const Input::Bindings& b = input_->bindings(id);
        const bool capturing = input_->capturing();
        if (isTouch(devs)) {  // the touch screen's settings, then Reset
            for (int c = 0; c <= Input::kTouchOptions; ++c) {
                const int r = int(devs.size()) + c;
                float x0, y0, x1, y1;
                rowRect(r, int(devs.size()), x0, y0, x1, y1, true);
                const bool f = r == focus_;
                if (f) quad(x0 + o, y0, x1 + o, y1, {1, 0.86f, 0.2f, 0.14f});
                const float cy = (y0 + y1) * 0.5f + 1;
                if (c == Input::kTouchOptions) {
                    text("impactor.font", "Reset to Defaults", (x0 + x1) * 0.5f + o, cy, 20, f ? kFocused : kNormal, 0x22);
                    continue;
                }
                text("impactor.font", Input::touchOptionName(c), x0 + 16 + o, cy, 19, f ? kFocused : kNormal, 0x21);
                const std::string v = input_->touchOptionValue(c);
                text("impactor.font", f ? "< " + v + " >" : v, x1 - 110 + o, cy, 19, f ? kFocused : kOn, 0x22);
            }
            text("impactor.font", "Taps click; a finger on a lane plays it; two fingers swipe; buttons show at the right edge.",
                 kRightX0 + 16 + o, kTop + 24 + float(Input::kTouchOptions + 1) * kTouchRow + 20, 16, kDim, 0x11);
            return;
        }
        for (int c = 0; c < extraRow(devs) + 1; ++c) {
            const int r = int(devs.size()) + c;
            float x0, y0, x1, y1;
            rowRect(r, int(devs.size()), x0, y0, x1, y1);
            const bool f = r == focus_;
            if (c == resetRow()) {  // Reset to defaults
                if (f) quad(x0 + o, y0, x1 + o, y1, {1, 0.86f, 0.2f, 0.14f});
                text("impactor.font", "Reset to Defaults", (x0 + x1) * 0.5f + o, (y0 + y1) * 0.5f + 1, 20, f ? kFocused : kNormal, 0x22);
                continue;
            }
            if (c > resetRow() && isPad(devs)) {  // the controller's type
                if (f) quad(x0 + o, y0, x1 + o, y1, {1, 0.86f, 0.2f, 0.14f});
                const std::string t = input_->deviceType(id);
                text("impactor.font", "Type: " + (f ? "< " + t + " >" : t), x0 + 12 + o, (y0 + y1) * 0.5f + 1, 18,
                     f ? kFocused : kNormal, 0x21);
                continue;
            }
            if (c > resetRow()) {  // the keyboard's Frets also strum
                const bool on = input_->keyboardFretsStrum();
                if (f) quad(x0 + o, y0, x1 + o, y1, {1, 0.86f, 0.2f, 0.14f});
                const float cy = (y0 + y1) * 0.5f;
                quad(x0 + 10 + o, cy - 9, x0 + 28 + o, cy + 9, {1, 1, 1, 0.25f});
                if (on) quad(x0 + 13 + o, cy - 6, x0 + 25 + o, cy + 6, kOn);
                text("impactor.font", "Frets also strum (like a PlayStation pad)", x0 + 38 + o, cy + 1, 18, f ? kFocused : kNormal, 0x21);
                continue;
            }
            const auto ctrl = Input::Control(c);
            const bool live = input_->controlDown(id, ctrl);
            if (live) quad(x0 + o, y0, x1 + o, y1, {0.3f, 1, 0.35f, 0.22f});
            else if (f) quad(x0 + o, y0, x1 + o, y1, {1, 0.86f, 0.2f, 0.14f});
            if (c < 5) quad(x0 + 8 + o, y0 + 7, x0 + 16 + o, y1 - 7, kFretColors[size_t(c)]);
            text("impactor.font", Input::controlName(ctrl), x0 + 26 + o, (y0 + y1) * 0.5f + 1, 18, f ? kFocused : kNormal, 0x21);
            std::string what;
            if (capturing && f) {
                what = "Press a button or move an axis  (Esc: cancel, Del: none)";
            } else {
                for (const Input::Source& s : b[size_t(c)]) what += (what.empty() ? "" : ", ") + input_->sourceName(id, s);
                if (what.empty()) what = "-";
            }
            text("impactor.font", fit(what, 18, x1 - x0 - 250 - kClearW), x0 + 236 + o, (y0 + y1) * 0.5f + 1, 18,
                 capturing && f ? kAccent : live ? kOn : kDim, 0x21);
            if (!b[size_t(c)].empty() && !(capturing && f))  // the mouse's clear button
                text("impactor.font", "x", x1 - kClearW * 0.5f + o, (y0 + y1) * 0.5f + 1, 18, f ? kFocused : kDim, 0x22);
        }
    }

private:
    static constexpr int kUp = 12, kDown = 14, kLeft = 15, kRight = 13;
    static constexpr float kTop = 168, kBottom = 640, kLeftX0 = 56, kLeftX1 = 520, kRightX0 = 548, kRightX1 = 1224;
    static constexpr float kDevRow = 60, kBindRow = 25.5f, kClearW = 28, kTouchRow = 46;
    static constexpr std::array<float, 4> kTitle{0.95f, 0.95f, 0.95f, 1}, kFocused{1, 0.86f, 0.2f, 1},
        kNormal{0.85f, 0.85f, 0.88f, 1}, kDim{0.55f, 0.55f, 0.6f, 1}, kAccent{1, 0.55f, 0.15f, 1}, kOn{0.3f, 1, 0.35f, 1},
        kP1{0.45f, 0.75f, 1, 1}, kP2{1, 0.5f, 0.45f, 1};
    static constexpr std::array<std::array<float, 4>, 5> kFretColors = {
        std::array<float, 4>{0.2f, 0.85f, 0.25f, 1}, {0.9f, 0.2f, 0.2f, 1}, {0.95f, 0.85f, 0.2f, 1}, {0.25f, 0.45f, 0.95f, 1},
        {0.95f, 0.55f, 0.15f, 1}};

    // The right column after the controls: Reset, then (keyboard) Frets also
    // strum. The touch screen: its settings, then Reset.
    static int resetRow() { return int(Input::Control::Count); }
    bool isTouch(const std::vector<Input::Device>& devs) const { return devs[size_t(selected_)].id == "touch"; }
    bool isPad(const std::vector<Input::Device>& devs) const { return devs[size_t(selected_)].id.rfind("pad:", 0) == 0; }
    // After Reset: the keyboard's Frets also strum, or a controller's type.
    int extraRow(const std::vector<Input::Device>& devs) const {
        if (isTouch(devs)) return Input::kTouchOptions;
        return resetRow() + (devs[size_t(selected_)].id == "keyboard" || isPad(devs) ? 1 : 0);
    }
    int rowCount(const std::vector<Input::Device>& devs) const { return int(devs.size()) + extraRow(devs) + 1; }
    void clamp(const std::vector<Input::Device>& devs) {  // devices come and go
        selected_ = std::clamp(selected_, 0, int(devs.size()) - 1);
        focus_ = std::clamp(focus_, 0, rowCount(devs) - 1);
    }
    // Row r: a controller (left column) or a binding / Reset (right column).
    static void rowRect(int r, int devs, float& x0, float& y0, float& x1, float& y1, bool touch = false) {
        if (r < devs) {
            // Up to the live input box (kBottom - 150): 60 units a row, less with many controllers.
            const float row = std::min(kDevRow, (kBottom - 160 - (kTop + 22)) / float(std::max(devs, 1)));
            x0 = kLeftX0 + 8, x1 = kLeftX1 - 8, y0 = kTop + 22 + float(r) * row, y1 = y0 + row - 4;
        } else if (touch) {  // the touch screen's settings, then Reset
            r -= devs;
            x0 = kRightX0 + 8, x1 = kRightX1 - 8;
            if (r == Input::kTouchOptions) y0 = kTop + 24 + float(r) * kTouchRow + 6, y1 = y0 + 30, x0 = kRightX1 - 260;
            else y0 = kTop + 24 + float(r) * kTouchRow, y1 = y0 + kTouchRow - 4;
        } else {
            r -= devs;
            x0 = kRightX0 + 8, x1 = kRightX1 - 8, y0 = kTop + 24 + float(r) * kBindRow, y1 = y0 + kBindRow - 1;
            if (r == resetRow()) y0 += 6, y1 = y0 + 30, x0 = kRightX1 - 260;
            if (r > resetRow()) y0 = kTop + 24 + float(resetRow()) * kBindRow + 6, y1 = y0 + 30, x1 = kRightX1 - 272;
        }
    }
    void setPlayer(const Input::Device& d, int player) {
        if (d.player == player || d.id == "touch") return sound("button_error");  // at the end of none, P1, P2; touch is P1
        input_->setPlayer(d.id, player);
        sound("button_select");
    }
    void activate(const std::vector<Input::Device>& devs) {
        const int n = int(devs.size());
        if (focus_ < n) return setPlayer(devs[size_t(focus_)], devs[size_t(focus_)].player == 1 ? -1 : devs[size_t(focus_)].player + 1);
        const int c = focus_ - n;
        const std::string& id = devs[size_t(selected_)].id;
        if (isTouch(devs)) {
            if (c < Input::kTouchOptions) input_->cycleTouchOption(c, 1);
            else input_->resetTouchOptions();
            sound("button_select");
            return;
        }
        if (c == resetRow()) {
            input_->resetBindings(id);
            sound("button_select");
            return;
        }
        if (c > resetRow() && isPad(devs)) {
            input_->cycleDeviceType(id, 1);
            sound("button_select");
            return;
        }
        if (c > resetRow()) {
            input_->setKeyboardFretsStrum(!input_->keyboardFretsStrum());
            sound("button_select");
            return;
        }
        input_->beginCapture(id, Input::Control(c));
        captureMs_ = 0;
        sound("button_select");
    }
    void sound(const char* seq) { host_.sendTo("synth", "play_sequence", {Value::symbol(seq)}); }
    // Shortened with "..." to `width` at line height `h`.
    std::string fit(std::string s, float h, float width) {
        if (host_.textWidth("impactor.font", s, h) <= width) return s;
        while (s.size() > 1 && host_.textWidth("impactor.font", s + "...", h) > width) s.pop_back();
        return s + "...";
    }

    void updateTransition() {
        int phase = 0;
        float t = 1;
        bool back = false;
        host_.transition(phase, t, back);
        float e = t * t * (3 - 2 * t);
        offset_ = 0, fade_ = 1;
        if (phase == 2) offset_ = (back ? -1.0f : 1.0f) * (1 - e) * kScreenW * 0.6f, fade_ = e;
        if (phase == 1) offset_ = (back ? 1.0f : -1.0f) * e * kScreenW * 0.6f, fade_ = 1 - e;
    }
    std::array<float, 4> faded(std::array<float, 4> c) const { return c[3] *= fade_, c; }
    void text(const char* font, const std::string& s, float x, float y, float h, const std::array<float, 4>& c, uint32_t align) {
        host_.drawText(font, s, x, y, h, faded(c), align);
    }
    void quad(float x0, float y0, float x1, float y1, const std::array<float, 4>& c) {
        auto f = faded(c);
        const uint8_t r = uint8_t(f[0] * 255), g = uint8_t(f[1] * 255), bl = uint8_t(f[2] * 255), a = uint8_t(f[3] * 255);
        const Vertex v[6] = {{x0, y0, 0, 0, 0, r, g, bl, a}, {x1, y0, 0, 0, 0, r, g, bl, a}, {x1, y1, 0, 0, 0, r, g, bl, a},
                             {x0, y0, 0, 0, 0, r, g, bl, a}, {x1, y1, 0, 0, 0, r, g, bl, a}, {x0, y1, 0, 0, 0, r, g, bl, a}};
        host_.renderer().draw(v, 6, 0, Blend::Alpha);
    }

    SelectHost& host_;
    Input* input_;
    int focus_ = 0, selected_ = 0;
    float captureMs_ = 0, offset_ = 0, fade_ = 1;
    size_t reportedDevices_ = 0;
};

}  // namespace

std::unique_ptr<NativePanel> makeGamepadPanel(SelectHost& host, Input* input) { return std::make_unique<GamepadPanel>(host, input); }
