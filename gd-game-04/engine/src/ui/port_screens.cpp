// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/port_screens.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "data/qbkey.h"
#include "render/canvas.h"
#include "ui/front_end.h"
#include "ui/ui_system.h"

namespace {

constexpr double kTransitionMs = 300;  // gd01's screen transition [I: its length]

// GH3's help texts (add_user_control_helper: SELECT, BACK, UP/DOWN) [C data].
const char* const kHelp[3] = {"SELECT", "BACK", "UP/DOWN"};

constexpr PortDraw::Color kPillText{0.08f, 0.08f, 0.08f, 1};

void pill(PortDraw& d, float x0, float y0, float x1, float y1) {
    const float r = (y1 - y0) * 0.5f, cy = (y0 + y1) * 0.5f;
    const PortDraw::Color c{0.8f, 0.8f, 0.8f, 0.95f};
    d.quad(x0 + r, y0, x1 - r, y1, c);
    for (int k = 0; k < 8; ++k) {  // the round ends
        const float a0 = 3.14159265f * (0.5f + float(k) / 8), a1 = 3.14159265f * (0.5f + float(k + 1) / 8);
        d.triangle(x0 + r, cy, x0 + r + r * std::cos(a0), cy + r * std::sin(a0), x0 + r + r * std::cos(a1), cy + r * std::sin(a1), c);
        d.triangle(x1 - r, cy, x1 - r - r * std::cos(a0), cy + r * std::sin(a0), x1 - r - r * std::cos(a1), cy + r * std::sin(a1), c);
    }
}

const char* itemLabel(const std::string& flowState) { return flowState == "main_menu_fs" ? "GAMEPADS" : "ENHANCED"; }

}  // namespace

void PortContext::sound(const char* what) const {
    // GH3's own menu sounds [C data, gamemenu_helpers.qb]: a move is
    // generic_menu_up_or_down_sound, a choice ui_sfx_select, back has none
    // (generic_menu_pad_back_sound is empty), a refusal Menu_Warning_SFX.
    if (!std::strcmp(what, "toggle")) fe.vm().run(qbkey::key("generic_menu_up_or_down_sound"), qb::Struct{});
    else if (!std::strcmp(what, "select")) fe.soundEvent("ui_sfx_select");
    else if (!std::strcmp(what, "error")) fe.soundEvent("Menu_Warning_SFX");
}

PortScreens::PortScreens(Input& input, FrontEnd& fe, EnhancedStore& store, std::function<void(const std::string&)> changed)
    : ctx_{input, fe, store, *this, std::move(changed)} {
    gamepads_ = makeGamepadScreen(ctx_);
    enhanced_ = makeEnhancedScreen(ctx_);
}

PortScreens::~PortScreens() = default;

PortScreen* PortScreens::screen(Id id) const { return id == Id::Gamepads ? gamepads_.get() : enhanced_.get(); }

std::string PortScreens::current() const {
    if (stack_.empty()) return "";
    return stack_.back() == Id::Gamepads ? "port_gamepads" : "port_enhanced";
}

void PortScreens::open(Id id) {
    if (!stack_.empty()) screen(stack_.back())->exit();
    stack_.push_back(id);
    screen(id)->enter();
    transMs_ = 0, transBack_ = false;
}

void PortScreens::back() {
    if (stack_.empty()) return;
    screen(stack_.back())->exit();
    stack_.pop_back();
    if (!stack_.empty()) screen(stack_.back())->enter();
    transMs_ = 0, transBack_ = true;
}

void PortScreens::handle(const InputEvent& e) {
    if (stack_.empty() || !e.down) return;
    PortButton b;
    switch (e.action) {
    case Action::Up: b = PortButton::Up; break;
    case Action::Down: b = PortButton::Down; break;
    case Action::Left: b = PortButton::Left; break;
    case Action::Right: b = PortButton::Right; break;
    case Action::Confirm:
    case Action::Fret0:
    case Action::Start: b = PortButton::Select; break;  // Enter is Start, and Start selects here
    case Action::Back:
    case Action::Fret1: b = PortButton::Back; break;
    default: return;
    }
    if (screen(stack_.back())->button(b)) return;
    if (b == PortButton::Back) {
        ctx_.sound("back");
        back();
    }
}

void PortScreens::mouse(float x, float y, int button) {
    if (stack_.empty()) return;
    if (button == 3) {  // a right click is Back
        handle(InputEvent{Action::Back, true, 0, ControllerMode::Guitar, 0});
        return;
    }
    if (transMs_ < kTransitionMs) return;  // the page is still sliding in
    for (int k = 0; k < 2; ++k) {  // the help bar's SELECT and BACK
        float x0, y0, x1, y1;
        helpRect(k, x0, y0, x1, y1);
        if (x < x0 || x > x1 || y < y0 || y > y1) continue;
        if (button == 1) handle(InputEvent{k == 0 ? Action::Confirm : Action::Back, true, 0, ControllerMode::Guitar, 0});
        return;
    }
    screen(stack_.back())->mouse(x, y, button == 1);
}

void PortScreens::update(double dtMs) {
    transMs_ += dtMs;
    if (!stack_.empty()) screen(stack_.back())->update(dtMs);
}

void PortScreens::draw(Canvas& c) {
    if (stack_.empty()) return;
    PortDraw d(c, ctx_.fe.ui());
    // gd01's transition: the page slides 0.6 screen widths and fades in,
    // from the right going forwards, from the left going back.
    const float t = float(std::min(1.0, transMs_ / kTransitionMs)), e = t * t * (3 - 2 * t);
    d.setTransition((transBack_ ? -1.0f : 1.0f) * (1 - e) * kScreenW * 0.6f, e);
    screen(stack_.back())->draw(d);
    // GH3's help bar on the port's screens: select, back, up / down.
    d.setTransition(0, 1);
    for (int k = 0; k < 3; ++k) {
        float x0, y0, x1, y1;
        helpRect(k, x0, y0, x1, y1);
        static const PortDraw::Color kFret[3] = {{0.2f, 0.85f, 0.25f, 1}, {0.9f, 0.2f, 0.2f, 1}, {0.25f, 0.25f, 0.25f, 1}};
        const float h = y1 - y0 - 8, cy = (y0 + y1) * 0.5f;
        pill(d, x0, y0, x1, y1);
        if (k < 2) d.quad(x0 + 8, cy - h * 0.5f, x0 + 8 + h, cy + h * 0.5f, kFret[k]);
        else d.quad(x0 + 8, cy - 3, x0 + 8 + h, cy + 3, kFret[k]);  // the strum bar
        d.text(kHelp[k], x0 + h + 20, cy, h * 0.8f, kPillText, 0x21, true);
    }
}

void PortScreens::helpRect(int k, float& x0, float& y0, float& x1, float& y1) const {
    const float h = 26;
    x0 = 56;
    for (int i = 0; i <= k; ++i) {
        x1 = x0 + h + 34 + ctx_.fe.ui().textWidth(qbkey::key("fontgrid_title_gh3"), kHelp[i], h * 0.8f);
        if (i < k) x0 = x1 + 16;
    }
    y0 = 660 - h * 0.5f - 4, y1 = 660 + h * 0.5f + 4;
}

bool PortScreens::itemRect(const std::string& flowState, float& x0, float& y0, float& x1, float& y1) const {
    if (flowState != "main_menu_fs" && flowState != "options_select_option_fs") return false;
    // After GH3's own help items' texts (user_control_container), on their
    // line; GH3 puts its pills at y 650 (user_control_pill_y_position) [C data].
    const ui::UiSystem& ui = ctx_.fe.ui();
    float bx1 = 120, by0 = 650, by1 = 680;
    bool any = false;
    if (const ui::Element* box = ui.find(qbkey::key("user_control_container")))
        for (const auto& c : box->children)
            if (c->drawnIn == ui.drawCount() && !c->text.empty() && c->text[0] != '\\') {
                if (!any) bx1 = c->bx1, by0 = c->by0, by1 = c->by1;
                else bx1 = std::max(bx1, c->bx1), by0 = std::min(by0, c->by0), by1 = std::max(by1, c->by1);
                any = true;
            }
    const float h = std::clamp(by1 - by0, 22.0f, 34.0f), cy = (by0 + by1) * 0.5f;
    x0 = bx1 + 40, x1 = x0 + h + 14 + ui.textWidth(qbkey::key("fontgrid_title_gh3"), itemLabel(flowState), h * 0.8f) + 18;
    y0 = cy - h * 0.5f - 4, y1 = cy + h * 0.5f + 4;
    return true;
}

void PortScreens::drawItems(Canvas& c, const std::string& flowState) {
    float x0, y0, x1, y1;
    if (isOpen() || !itemRect(flowState, x0, y0, x1, y1)) return;
    PortDraw d(c, ctx_.fe.ui());
    // A pill as GH3 draws its help items (light, dark text, as seen on its
    // Control_pill textures) [I: drawn as quads], with the fret's colour
    // (yellow: GAMEPADS, blue: ENHANCED) as a square where GH3 draws its
    // button glyph.
    const bool main = flowState == "main_menu_fs";
    const PortDraw::Color fret = main ? PortDraw::Color{0.95f, 0.85f, 0.2f, 1} : PortDraw::Color{0.25f, 0.45f, 0.95f, 1};
    const float h = y1 - y0 - 8, cy = (y0 + y1) * 0.5f;
    pill(d, x0, y0, x1, y1);
    d.quad(x0 + 8, cy - h * 0.5f, x0 + 8 + h, cy + h * 0.5f, fret);
    d.text(itemLabel(flowState), x0 + h + 20, cy, h * 0.8f, kPillText, 0x21, true);
}

std::string PortScreens::itemAt(float x, float y, const std::string& flowState) const {
    float x0, y0, x1, y1;
    if (isOpen() || !itemRect(flowState, x0, y0, x1, y1) || x < x0 || x > x1 || y < y0 || y > y1) return "";
    return flowState == "main_menu_fs" ? "gamepads" : "enhanced";
}
