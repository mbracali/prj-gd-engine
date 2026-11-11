// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// GH2's two-player choice screens (ui/multi_select.h). Addresses are
// default.xex's; [C] confirmed there, [I] inferred.
#include "ui/multi_select.h"

#include <algorithm>

#include "ui/campaign.h"
#include "ui/char_select.h"
#include "ui/natives.h"
#include "ui/ui_manager.h"
#include "ui/ui_objects.h"

namespace ui {

namespace {

std::string nameOf(const Value& v) { return v.type == Value::Type::Object && v.object ? v.object->name() : v.str(); }
int intArg(const std::vector<Value>& a, size_t i, int def = 0) { return i < a.size() && a[i].isNumber() ? a[i].integerValue() : def; }

// A component's name without its extension (0x822ADBC8: up to the last '.').
std::string stripExt(const std::string& s) {
    const size_t dot = s.rfind('.');
    return dot == std::string::npos ? s : s.substr(0, dot);
}

// The list values of a definition key ((sel_buttons a.btn b.btn), (char_navigator (a b c))).
std::vector<std::string> names(Interp& in, ScriptObject& o, const std::string& key) {
    std::vector<std::string> out;
    const Value v = o.getProp(in, key);
    if (v.type == Value::Type::Array && v.array) {
        for (const Value& x : *v.array) {
            if (x.type == Value::Type::Array && x.array)
                for (const Value& y : *x.array) out.push_back(nameOf(y));
            else
                out.push_back(nameOf(x));
        }
    } else if (!nameOf(v).empty() && nameOf(v) != "0") {
        out.push_back(nameOf(v));
    }
    return out;
}

// MultiSelectPanel (constructor 0x822960A0, handler 0x822959B8): one
// player's column of choices (sel_buttons). +0x40 active (the screen's
// Enter sets player_num < num_players, set_active), +0x41 required ((required
// ...) at the screen's Enter, default TRUE), +0x42 the screen's exclusive,
// +0x38 the selection (a button's name without .btn; none until chosen),
// +0x3c ready_label, +0x44 sel_buttons (0x82296568) [C].
class MultiSelectPanelObj : public PanelObj {
public:
    MultiSelectPanelObj(UiManager& ui, const std::string& name) : PanelObj(ui, name, "MultiSelectPanel") {}

    bool active = false, required = true, exclusive = false;
    std::string selection;

    int playerNum() { return getProp(ui.interp(), "player_num").integerValue(); }

    // 0x82294D48 (MultiSelectScreen Enter): active for the players of the
    // mode, (required ...) read, the selection cleared.
    void screenEnter(int numPlayers) {
        active = playerNum() < numPlayers;
        if (handler("required")) required = getProp(ui.interp(), "required").truthy();
        selection.clear();
        refresh();
    }

    // 0x82295070 (the panel's draw): ready_label shows with a selection;
    // then only the chosen sel_button shows [C].
    void refresh() {
        if (!view) return;
        const std::string ready = getProp(ui.interp(), "ready_label").str();
        if (!ready.empty() && view->find(ready)) view->setShowing(ready, !selection.empty());
        for (const std::string& b : names(ui.interp(), *this, "sel_buttons"))
            if (view->find(b)) view->setShowing(b, selection.empty() || stripExt(b) == selection);
    }

    void enter() override {
        PanelObj::enter();
        refresh();
    }

    // `active` is the native flag as a property ({set [active] FALSE}).
    Value getProp(Interp& in, const std::string& key) override {
        if (key == "active") return Value::integer(active ? 1 : 0);
        return PanelObj::getProp(in, key);
    }
    void setProp(Interp& in, const std::string& key, const Value& v) override {
        if (key == "active") active = showing = v.truthy();
        else PanelObj::setProp(in, key, v);
    }

    // 0x82294F00: {multi_select $part $panel $selected} to the UI (UIManager Handle).
    void sendMultiSelect(const Value& part, bool selected) {
        ui.route("multi_select", {part, Value::obj(this), Value::integer(selected ? 1 : 0)});
    }

    // 0x82295130 (button_down): with a selection a button does nothing,
    // but kPad_Tri clears it ({multi_select 0 $panel FALSE}); kPad_Start and
    // a panel without a selection go on to PanelDir (navigation, select) [C].
    bool buttonDown(int button, int player) override {
        if (!active || player != playerNum()) return false;
        if (selecting.empty() && !selection.empty() && button != kPad_Start) {
            if (button == kPad_Tri) {
                selection.clear();
                refresh();
                sendMultiSelect(Value::integer(0), false);
            }
            return true;
        }
        return PanelObj::buttonDown(button, player);
    }

    // 0x822955B8 (force_select <button>): that button is the selection, focused.
    void forceSelect(const std::string& button) {
        if (component(button)) setFocus(button, false);
        selection = stripExt(button);
        refresh();
        sendMultiSelect(Value::symbol(selection), true);
    }

    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        if (msg == "set_active") {
            // +0x40 and the panel's showing (+0x11) [I: the second flag].
            active = showing = !args.empty() && args[0].truthy();
            return true;
        }
        if (msg == "is_active") return r = Value::integer(active ? 1 : 0), true;
        if (active) {
            if (msg == "button_name") return r = Value::string(nameOf(args.empty() ? Value::integer(0) : args[0]) + ".btn"), true;
            if (msg == "force_select") return forceSelect(args.empty() ? std::string() : nameOf(args[0])), true;
            if (msg == "component_select") {
                // 0x82295790: the chosen button is the selection; multi_select TRUE.
                const std::string c = args.empty() ? std::string() : nameOf(args[0]);
                selection = stripExt(c);
                refresh();
                sendMultiSelect(Value::symbol(selection), true);
                return r = Value::integer(1), true;
            }
            if (msg == "multi_select") {
                // 0x822958A0: another panel's choice disables that button
                // here (its release enables "<part>.btn": the part is 0 then,
                // so nothing is enabled) [C].
                const std::string panel = args.size() > 1 ? nameOf(args[1]) : std::string();
                if (panel != name()) {
                    const std::string b = (args.empty() ? std::string("0") : nameOf(args[0])) + ".btn";
                    const bool selected = args.size() > 2 && args[2].truthy();
                    if (component(b)) {
                        if (selected) disable(b, kPad_DDown);
                        else enable(b);
                    }
                }
                return r = Value::integer(1), true;
            }
        }
        return PanelObj::handleNative(in, msg, args, r);
    }
};

// MultiSelectScreen (Enter 0x822981A8 / 0x82296EF8, handler 0x822978C0):
// its MultiSelectPanels are <panel_base_name>0 and 1 [C].
class MultiSelectScreenObj : public ScreenObj {
public:
    MultiSelectScreenObj(UiManager& ui, const std::string& name) : ScreenObj(ui, name, "MultiSelectScreen") {}

    std::vector<MultiSelectPanelObj*> multiPanels() {
        std::vector<MultiSelectPanelObj*> out;
        const std::string base = getProp(ui.interp(), "panel_base_name").str();
        for (int p = 0; p < 2; ++p)
            if (auto* m = dynamic_cast<MultiSelectPanelObj*>(ui.panel(base + std::to_string(p)))) out.push_back(m);
        return out;
    }

    // 0x82294C98: a panel is not ready while (active or required) without a selection.
    static bool ready(const MultiSelectPanelObj* p) { return !((p->active || p->required) && p->selection.empty()); }
    bool allReady() {
        for (MultiSelectPanelObj* p : multiPanels())
            if (!ready(p)) return false;
        return true;
    }

    // 0x82297528: all ready: set_selection for each active panel, then all_ready.
    void checkReady() {
        if (!allReady()) return;
        for (MultiSelectPanelObj* p : multiPanels())
            if (p->active) ui.callScript(this, "set_selection", {Value::integer(p->playerNum()), Value::symbol(p->selection)});
        ui.callScript(this, "all_ready");
    }

    // 0x82296EF8: each panel active for the players of the mode (before
    // the panels enter: their scripts may set [active] FALSE), then the
    // active ones focus their get_selection's button (0x82295550).
    void nativeEnter() override {
        const bool excl = flag("exclusive", false);
        const int n = std::max(1, gameCfgValue(ui, "num_players").integerValue());
        for (MultiSelectPanelObj* p : multiPanels()) {
            p->exclusive = excl;
            p->screenEnter(n);
            p->showing = true;
        }
    }
    void nativeEntered() override {
        for (MultiSelectPanelObj* p : multiPanels()) {
            // An exclusive screen's buttons taken last time are free again [I].
            for (const std::string& b : names(ui.interp(), *p, "sel_buttons")) p->enable(b);
            p->refresh();
            if (!p->active) continue;
            const Value sel = ui.callScript(this, "get_selection", {Value::integer(p->playerNum())});
            const std::string b = nameOf(sel) + ".btn";
            if (p->component(b)) p->setFocus(b, false);
        }
    }

    // 0x822973C8: a button goes to the player's panel; a selection it
    // clears sends not_ready when all were ready [C].
    bool nativeButtonDown(int button, int player) override {
        const bool was = allReady();
        bool handled = false;
        for (MultiSelectPanelObj* p : multiPanels())
            if (p->up && p->active && p->playerNum() == player) handled = p->buttonDown(button, player) || handled;
        if (handled) {
            if (was && !allReady()) ui.callScript(this, "not_ready");
            return true;
        }
        // GHScreen (0x8229EED0): kPad_Tri goes to the focus panel first
        // (MultiCharSelPanel's deselect), then back.
        if (button == kPad_Tri)
            if (PanelObj* fp = focusPanel(); fp && fp->up) {
                if (!ui.callScript(fp, "button_down", {Value::integer(button), Value::integer(player)}).isUnhandled()) return true;
                if (fp->buttonDown(button, player)) return true;
            }
        return ScreenObj::nativeButtonDown(button, player);
    }

    bool routeNative(const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(1);
        if (msg == "component_select") {
            // 0x82297750: to the component's panel, then the ready check.
            auto* c = !args.empty() && args[0].type == Value::Type::Object ? dynamic_cast<ComponentObj*>(args[0].object) : nullptr;
            bool mine = false;
            for (MultiSelectPanelObj* p : multiPanels())
                if (c && c->panel == p) mine = true, ui.interp().send(p, msg, args, false);
            if (!mine) return false;
            checkReady();
            return true;
        }
        if (msg == "multi_select") {
            // 0x82297810: not exclusive, the ready check (the script then
            // has it); exclusive, every panel gets it.
            if (!flag("exclusive", false)) {
                checkReady();
                return false;
            }
            for (MultiSelectPanelObj* p : multiPanels())
                if (p->active) ui.interp().send(p, msg, args, false);
            return true;
        }
        if (msg == "component_focus") {
            // 0x822970A0: to every active panel (their FOCUS_MSG).
            for (MultiSelectPanelObj* p : multiPanels())
                if (p->active && p->up) ui.callScript(p, msg, args);
            return true;
        }
        return false;
    }
};

// MultiCharSelPanel (Enter 0x8228B460, handler 0x8228A9D8, buttons
// 0x8228A590): the characters of (char_navigator ...) a player can pick
// (the unlocked ones), each player's index (+0x4c) and frozen flag (the
// bits at +0x38: chosen) [C].
class MultiCharSelPanelObj : public PanelObj {
public:
    MultiCharSelPanelObj(UiManager& ui, const std::string& name) : PanelObj(ui, name, "MultiCharSelPanel") {}

    void enter() override {
        PanelObj::enter();
        chars_ = names(ui.interp(), *this, "char_navigator");
        frozen_[0] = frozen_[1] = false;
        idx_[0] = idx_[1] = 0;
        Session& s = ui.session();
        for (int i = 0; i < int(chars_.size()); ++i) {
            if (chars_[size_t(i)] == s.players[0].character) idx_[0] = i;
            else if (chars_[size_t(i)] == s.players[1].character) idx_[1] = i;
        }
        // The same character: the second player's index one on, at most
        // the count [C]; the count itself wraps to the first [I].
        if (idx_[0] == idx_[1]) idx_[1] = std::min(idx_[0] + 1, int(chars_.size()));
        if (idx_[1] >= int(chars_.size())) idx_[1] = 0;
    }

    bool available(const std::string& c) { return chars::unlocked(ui, c); }

    // 0x8228A408: up / down step through the characters, wrapping, past the
    // locked ones and the other player's; a new one is the player's (its
    // first outfit, set_character 0x822BD8B8).
    bool scroll(int player, bool down) {
        const int n = int(chars_.size());
        if (n == 0) return false;
        const std::string other = ui.session().players[size_t(1 - player)].character;
        int i = idx_[player];
        for (int guard = 0; guard <= n; ++guard) {
            i = ((i + (down ? 1 : -1)) % n + n) % n;
            if (available(chars_[size_t(i)]) && chars_[size_t(i)] != other) break;
        }
        if (i == idx_[player]) return false;
        idx_[player] = i;
        chars::setCharacter(ui, ui.session().players[size_t(player)], chars::outfitAt(ui, chars_[size_t(i)], 0), false);
        return true;
    }

    // 0x8228A590 [C]: X picks (multi_char_selected), up / down scroll
    // (multi_char_scroll), Tri on a picked one releases it (deselect_char).
    bool buttonDown(int button, int player) override {
        const int p = std::clamp(player, 0, 1);
        if (button == kPad_X) {
            if (!frozen_[p]) {
                ui.route("multi_char_selected", {Value::integer(p)});
                frozen_[p] = true;
            }
            return true;
        }
        if (button == kPad_DUp || button == kPad_DDown) {
            if (!frozen_[p] && scroll(p, button == kPad_DDown)) ui.route("multi_char_scroll", {Value::integer(p)});
            return true;
        }
        if (button == kPad_Tri && frozen_[p]) {
            frozen_[p] = false;
            ui.route("deselect_char", {Value::integer(p)});
            return true;
        }
        return false;
    }

    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        if (msg == "get_char_idx") return r = Value::integer(idx_[std::clamp(intArg(args, 0), 0, 1)]), true;
        if (msg == "num_outfits") {
            // 0x8228A248: the character's unlocked outfits; 1 for one not in gh2.dta.
            const std::string c = args.empty() ? std::string() : nameOf(args[0]);
            bool known = false;
            for (const auto& [ch, outfits] : chars::data(ui).characters) known = known || ch == c;
            return r = Value::integer(known ? chars::numOutfits(ui, c) : 1), true;
        }
        if (msg == "get_outfit")
            return r = Value::symbol(chars::outfitAt(ui, args.empty() ? std::string() : nameOf(args[0]), intArg(args, 1))), true;
        if (msg == "set_frozen") return frozen_[std::clamp(intArg(args, 0), 0, 1)] = args.size() > 1 && args[1].truthy(), true;
        return PanelObj::handleNative(in, msg, args, r);
    }

private:
    std::vector<std::string> chars_;
    int idx_[2] = {0, 0};
    bool frozen_[2] = {false, false};
};

}  // namespace

std::unique_ptr<ScriptObject> makeMultiClass(UiManager& ui, const std::string& cls, const std::string& name) {
    if (cls == "MultiSelectScreen") return std::make_unique<MultiSelectScreenObj>(ui, name);
    if (cls == "MultiSelectPanel") return std::make_unique<MultiSelectPanelObj>(ui, name);
    if (cls == "MultiCharSelPanel") return std::make_unique<MultiCharSelPanelObj>(ui, name);
    return nullptr;
}

}  // namespace ui
