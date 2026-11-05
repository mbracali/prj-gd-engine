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

// GH3's help pill: Control_pill_end, Control_pill_body, the end mirrored,
// in user_control_pill_color [C script add_user_control_helper; I: the
// ends' widths at this height]; quads if the textures are missing.
void pill(PortDraw& d, float x0, float y0, float x1, float y1) {
    const float h = y1 - y0, end = h * 0.5f;
    const PortDraw::Color c{1, 1, 1, 1};
    if (d.sprite(qbkey::key("Control_pill_end"), x0, y0, x0 + end, y1, c, true) &&
        d.sprite(qbkey::key("Control_pill_body"), x0 + end, y0, x1 - end, y1, c) && d.sprite(qbkey::key("Control_pill_end"), x1 - end, y0, x1, y1, c))
        return;
    const float r = h * 0.5f, cy = (y0 + y1) * 0.5f;
    const PortDraw::Color q{0.8f, 0.8f, 0.8f, 0.95f};
    d.quad(x0 + r, y0, x1 - r, y1, q);
    for (int k = 0; k < 8; ++k) {  // the round ends
        const float a0 = 3.14159265f * (0.5f + float(k) / 8), a1 = 3.14159265f * (0.5f + float(k + 1) / 8);
        d.triangle(x0 + r, cy, x0 + r + r * std::cos(a0), cy + r * std::sin(a0), x0 + r + r * std::cos(a1), cy + r * std::sin(a1), q);
        d.triangle(x1 - r, cy, x1 - r - r * std::cos(a0), cy + r * std::sin(a0), x1 - r - r * std::cos(a1), cy + r * std::sin(a1), q);
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
    : ctx_{input, fe, store, *this, std::move(changed), {}, {}} {
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
        // GH3's glyphs from its button font: \m0 green, \m1 red, \bb the strum bar [C script add_user_control_helper].
        static const char* const kGlyph[3] = {"\\m0", "\\m1", "\\bb"};
        const float h = y1 - y0 - 8, cy = (y0 + y1) * 0.5f;
        pill(d, x0, y0, x1, y1);
        d.text(kGlyph[k], x0 + 4, cy, h * 1.1f, PortDraw::Color{1, 1, 1, 1}, 0x21, true);
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

// The port's item on GH3's help bar (GAMEPADS on the main menu, ENHANCED on
// Options) is one of GH3's own pills: added through its
// add_user_control_helper (yellow / blue fret glyph, its pill texture and
// layout) when the screen's helpers lack it (ensureItem), so it looks and
// lines up as GH3's do [C script add_user_control_helper].
void PortScreens::ensureItem(const std::string& flowState) {
    if (isOpen() || (flowState != "main_menu_fs" && flowState != "options_select_option_fs")) return;
    ui::UiSystem& ui = ctx_.fe.ui();
    const ui::Element* box = ui.find(qbkey::key("user_control_container"));
    if (!box || box->children.empty()) return;  // the screen's own helpers first
    const std::string label = itemLabel(flowState);
    for (const auto& c : box->children)
        if (c->text == label) return;
    qb::Struct p;
    qs::Vm::set(p, qbkey::key("Text"), qs::Vm::makeString(label));
    qs::Vm::set(p, qbkey::key("button"), qs::Vm::makeKey(qbkey::key(flowState == "main_menu_fs" ? "yellow" : "blue")));
    qs::Vm::set(p, qbkey::key("z"), qs::Vm::makeInt(100));
    ctx_.fe.vm().run(qbkey::key("add_user_control_helper"), p);
}

bool PortScreens::itemRect(const std::string& flowState, float& x0, float& y0, float& x1, float& y1) const {
    if (flowState != "main_menu_fs" && flowState != "options_select_option_fs") return false;
    // GH3's pill holding the item's text (ensureItem), its glyph to the left.
    const ui::UiSystem& ui = ctx_.fe.ui();
    const ui::Element* box = ui.find(qbkey::key("user_control_container"));
    if (!box) return false;
    const std::string label = itemLabel(flowState);
    for (const auto& c : box->children)
        if (c->text == label && c->drawnIn == ui.drawCount()) {
            x0 = c->bx0 - 40, x1 = c->bx1 + 8, y0 = c->by0 - 8, y1 = c->by1 + 8;
            return true;
        }
    return false;
}

void PortScreens::drawItems(Canvas&, const std::string& flowState) { ensureItem(flowState); }

std::string PortScreens::itemAt(float x, float y, const std::string& flowState) const {
    float x0, y0, x1, y1;
    if (isOpen() || !itemRect(flowState, x0, y0, x1, y1) || x < x0 || x > x1 || y < y0 || y > y1) return "";
    return flowState == "main_menu_fs" ? "gamepads" : "enhanced";
}
