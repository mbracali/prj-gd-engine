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

// The help texts' button glyphs: \m0 green, \m1 red, \bb the strum bar [C script add_user_control_helper].
const char* const kGlyph[3] = {"\\m0", "\\m1", "\\bb"};

// GH3's help bar as add_user_control_helper and user_control_build_pills
// lay it out (user_control_auto_center on, no super pill; GD04-110) [C
// scripts; the globals' defaults: user_control_pill_scale 0.4,
// user_control_pill_y_position 650, user_control_pill_gap 150,
// user_control_pill_end_width 50, action_safe_width_for_helpers 925]: the
// glyph (gh3_button_font = buttonsxenon at 1.2 x the pill scale) at
// (-10, 8) x scale, the text (user_control_text_font = fontgrid_title_gh3
// at 1.1 x) at (50, 20) x scale, the strum bar's text 50 x scale further
// (offset_for_strumbar); every pill as wide as the widest glyph + text, the
// row centred on 640 with pill_gap x scale between the bodies (narrowed to
// the action-safe width); the body at y + 0.8 x scale, 1.2 x the glyph-top
// to text-bottom height, raised by a tenth of it; the ends (Control_pill_end,
// the left one flipped) end_width x scale wide outside it; glyph and text
// centred on their pill (align_user_control_with_pill). The same numbers as
// the Options screen's helpers (GD04_UIDUMP: bodies 125 wide, 60 apart, at
// y 647.1).
struct HelpPill {
    float x0, x1, y0, y1, end;  // the body; the ends outside it
    float glyphX, glyphY, glyphH, textX, textY, textH;
};

void helpLayout(FrontEnd& fe, HelpPill out[3]) {
    const ui::UiSystem& ui = fe.ui();
    auto g = [&](const char* name, float def) { return qs::Vm::number(fe.vm().global(qbkey::key(name)), def); };
    const float s = g("user_control_pill_scale", 0.4f), y = g("user_control_pill_y_position", 650);
    const float gap = g("user_control_pill_gap", 150) * s, end = g("user_control_pill_end_width", 50) * s;
    const float safeW = g("action_safe_width_for_helpers", 925);
    const uint32_t tf = qbkey::key("fontgrid_title_gh3");
    const float glyphH = ui.fontLineHeight(qbkey::key("buttonsxenon")) * 1.2f * s, textH = ui.fontLineHeight(tf) * 1.1f * s;
    constexpr int n = 3;
    float groupW[n], textOff[n], widest = 0;
    for (int k = 0; k < n; ++k) {
        textOff[k] = (50 + 10) * s + (k == 2 ? 50 * s : 0);  // the text's x past the glyph's
        groupW[k] = textOff[k] + ui.textWidth(tf, kHelp[k], textH);
        widest = std::max(widest, groupW[k]);
    }
    if ((widest + 2 * end) * n + gap * (n - 1) > safeW) widest = (safeW - gap * (n - 1)) / n - 2 * end;
    const float glyphTop = y + 8 * s, textTop = y + 20 * s, rawH = textTop + textH - glyphTop, pad = rawH * 0.2f;
    float x = 640 - n / 2.0f * widest - 0.5f * gap * (n - 1);
    for (int k = 0; k < n; ++k) {
        HelpPill& p = out[k];
        p.x0 = x, p.x1 = x + widest, p.y0 = y + 0.8f * s - 0.5f * pad, p.y1 = p.y0 + rawH + pad, p.end = end;
        p.glyphX = x + 0.5f * (widest - groupW[k]), p.glyphY = glyphTop, p.glyphH = glyphH;
        p.textX = p.glyphX + textOff[k], p.textY = textTop, p.textH = textH;
        x += widest + gap;
    }
}

// A colour global ([r g b a]) as the screen below left it.
std::array<uint8_t, 4> globalColor(FrontEnd& fe, const char* name, std::array<uint8_t, 4> c) {
    const qb::Value* v = fe.vm().global(qbkey::key(name));
    if (!v || !v->arr || v->arr->values.size() < 4) return c;
    for (size_t i = 0; i < 4; ++i) c[i] = uint8_t(std::clamp(qs::Vm::number(&v->arr->values[i]), 0.0f, 255.0f));
    return c;
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
    // The help bar's SELECT, BACK and UP/DOWN (the strum bar: up above its
    // middle, down below it, as GH3's own pills in App::menuMouse; GD04-111).
    for (int k = 0; k < 3; ++k) {
        float x0, y0, x1, y1;
        helpRect(k, x0, y0, x1, y1);
        if (x < x0 || x > x1 || y < y0 || y > y1) continue;
        const Action a = k == 0 ? Action::Confirm : k == 1 ? Action::Back : y < 0.5f * (y0 + y1) ? Action::Up : Action::Down;
        if (button == 1) handle(InputEvent{a, true, 0, ControllerMode::Guitar, 0});
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
    // GH3's help bar on the port's screens: select, back, up / down, laid
    // out and drawn as GH3's own (helpLayout), in the colours the screen
    // below (Options, the main menu) left [C globals user_control_pill_color,
    // user_control_pill_text_color; their defaults 20 20 20 155 / 180 180 180].
    ui::UiSystem& ui = ctx_.fe.ui();
    HelpPill pills[3];
    helpLayout(ctx_.fe, pills);
    const auto pillColor = globalColor(ctx_.fe, "user_control_pill_color", {20, 20, 20, 155});
    const auto textColor = globalColor(ctx_.fe, "user_control_pill_text_color", {180, 180, 180, 255});
    const uint32_t body = qbkey::key("Control_pill_body"), endTex = qbkey::key("Control_pill_end");
    for (int k = 0; k < 3; ++k) {
        const HelpPill& p = pills[k];
        ui.drawSprite(c, body, p.x0, p.y0, p.x1, p.y1, pillColor);
        ui.drawSprite(c, endTex, p.x0 - p.end, p.y0, p.x0, p.y1, pillColor, true);
        ui.drawSprite(c, endTex, p.x1, p.y0, p.x1 + p.end, p.y1, pillColor);
        ui.drawText(c, qbkey::key("buttonsxenon"), ui.setText(kGlyph[k]), p.glyphX, p.glyphY, p.glyphH, {255, 255, 255, 255}, 0x11);
        ui.drawText(c, qbkey::key("fontgrid_title_gh3"), kHelp[k], p.textX, p.textY, p.textH, textColor, 0x11);
    }
}

void PortScreens::helpRect(int k, float& x0, float& y0, float& x1, float& y1) const {
    HelpPill pills[3];
    helpLayout(ctx_.fe, pills);
    const HelpPill& p = pills[k];
    x0 = p.x0 - p.end, x1 = p.x1 + p.end, y0 = p.y0, y1 = p.y1;
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
