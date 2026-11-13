// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Rock Band's player slots (MultiFocusScreen and the screens with
// (player_panels (panel <p>) (slots ...) (type ...) (options ...)), e.g. the
// difficulty select, ui/seldiff.dta) [C data]: each player gets a slot object
// whose handlers are the (type ...) block, whose state is its current_option
// handler's symbol and whose per-state handlers are (options (<state> ...));
// the slot works on the panel's components (find, set_focus, iterate,
// disable). Buttons go to the slot of the player who pressed, the panel's
// FOCUS_MSG / SELECT_MSG to the slot first; once every active slot
// is_ready the screen gets start_loading_msg.
// One slot per (slots ...) entry, each its own focus in its directory
// (slot0..slot3) and the buttons of the player in it (RB 0x8282EA48:
// the slot whose player_num is the player's, else the first free slot
// whose allow_button_msg takes the player) on the band lobby
// (matchmaking_band_screen, GD05-082). The other screens keep one slot,
// player 0's [I: seldiff's per-player difficulty is GD05-091].
#include "ui/rb_multifocus.h"

#include <SDL3/SDL.h>

#include "render/ui_view.h"
#include "ui/ui_manager.h"
#include "ui/ui_objects.h"

namespace ui {

PlayerSlotObj::PlayerSlotObj(UiManager& ui, ScreenObj& screen, PanelObj& panel, const DataNode& type, const DataNode* options, int slotNum,
                             const std::string& slot)
    : ScriptObject(screen.name() + "_slot" + std::to_string(slotNum), "PlayerSlot"),
      ui_(ui),
      screen_(screen),
      panel_(panel),
      options_(options) {
    setTypeDef(&type, 1);
    props["slot_num"] = Value::integer(slotNum);
    props["slot"] = Value::symbol(slot);
    // seldiff_solo.milo / seldiff.milo: each slot's own inline copy of
    // seldiffpart_player.milo, slot0..slot3 [C data]: the slot's scripts
    // name its components (seldiff.grp, selpart.grp, medium.btn) there, not
    // the panel's own seldiff.grp of the same name.
    const std::string own = "slot" + std::to_string(slotNum);
    if (panel.view && panel.view->hasScope(own)) {
        scope_ = own;
        panel.view->registerScope(own);
    }
}

PlayerSlotObj::~PlayerSlotObj() { ui_.interp().unregisterObject(this); }

bool PlayerSlotObj::handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) {
    // The panel's component natives, as the slot's own.
    if (!scope_.empty() && msg == "find" && !args.empty())
        if (ComponentObj* c = panel_.scopedComponent(scope_, args[0].str())) return r = Value::obj(c), true;
    if (!scope_.empty() && msg == "iterate" && args.size() >= 2 && panel_.view) {
        r = Value::integer(0);
        std::string var = args[1].str();
        if (!var.empty() && var[0] == '$') var = var.substr(1);
        const Value saved = in.var(var);
        for (scene::Object* o : panel_.view->byClassIn(scope_, args[0].str())) {
            ComponentObj* c = panel_.scopedComponent(scope_, o->name);
            if (!c) continue;
            in.var(var) = Value::obj(c);
            for (size_t k = 2; k < args.size(); ++k) r = in.evalValue(args[k]);
        }
        in.var(var) = saved;
        return true;
    }
    // The slot's own focus (its directory's PanelDir) and its components' states.
    if (!scope_.empty()) {
        const auto compName = [&](size_t i) {
            if (i >= args.size()) return std::string();
            if (auto* c = dynamic_cast<ComponentObj*>(args[i].object)) return c->name();
            return args[i].str();
        };
        if (msg == "set_focus") {
            const std::string n = compName(0);
            if (n.empty() || n == "0" || panel_.scopedComponent(scope_, n)) panel_.setScopeFocus(scope_, n == "0" ? std::string() : n, false);
            return r = Value::integer(0), true;
        }
        if (msg == "focus_name") return r = Value::symbol(panel_.scopeFocus[scope_]), true;
        if (msg == "enable" || msg == "disable") {
            const std::string key = gfx::UiView::scopedKey(scope_, compName(0));
            if (msg == "enable") panel_.enable(key);
            else panel_.disable(key, args.size() > 1 && args[1].isNumber() ? args[1].integerValue() : kPad_DDown);
            return r = Value::integer(0), true;
        }
    }
    if (msg == "find" || msg == "set_focus" || msg == "iterate" || msg == "disable" || msg == "enable" || msg == "focus_name" ||
        msg == "is_up")
        return r = in.send(&panel_, msg, args), true;
    if (msg == "set_showing") return showing_ = !args.empty() && args[0].truthy(), r = Value::integer(0), true;
    if (msg == "showing") return r = Value::integer(showing_ ? 1 : 0), true;
    return false;
}

Value PlayerSlotObj::dispatch(const std::string& msg, std::vector<Value> args) {
    // A slot's update (RB 0x8282E5E0) [C]: when its current_option is no
    // longer its (option ...), the option is set to it and the new state's
    // init runs, then update.
    if (msg == "update" && options_) {
        const Value now = ui_.callScript(this, "current_option");
        const std::string want = now.str();
        if (!now.isUnhandled() && getProp(ui_.interp(), "option").str() != want) {
            props["option"] = Value::symbol(want);
            if (const DataNode* o = options_->find(want))
                if (const DataNode* h = o->find("init")) {
                    std::vector<Value> none;
                    ui_.interp().callHandler(this, *h, none);
                }
        }
    }
    // The state's handler first, then the type's.
    if (options_) {
        const Value state = ui_.callScript(this, "current_option");
        if (const DataNode* o = options_->find(state.str()))
            if (const DataNode* h = o->find(msg)) {
                const Value r = ui_.interp().callHandler(this, *h, args);
                if (!r.isUnhandled()) return r;
            }
    }
    return ui_.callScript(this, msg, std::move(args));
}

bool PlayerSlotObj::buttonDown(int button, int player) {
    if (scope_.empty()) return false;
    if (!panel_.selecting.empty()) return true;  // one selection at a time
    const std::string focus = panel_.scopeFocus[scope_];
    if (ComponentObj* c = focus.empty() ? nullptr : panel_.scopedComponent(scope_, focus))
        if (!panel_.isDisabled(c->key) && c->buttonDown(button, player)) return true;
    if (button == kPad_DUp || button == kPad_DDown || button == kPad_DLeft || button == kPad_DRight) {
        panel_.navigate(button, scope_);
        return panel_.scopeFocus[scope_] != focus;
    }
    return false;
}

int PlayerSlotObj::playerNum() {
    // Its code ({gamecfg get_player_num_from_slot_num [slot_num]}) runs as the slot's.
    const Value saved = ui_.interp().var("this");
    ui_.interp().var("this") = Value::obj(this);
    const Value v = getProp(ui_.interp(), "player_num");
    ui_.interp().var("this") = saved;
    return v.isNumber() ? v.integerValue() : -1;
}

bool PlayerSlotObj::ready() {
    const Value r = ui_.callScript(this, "is_ready");
    return r.isUnhandled() || r.truthy();
}

bool multiSlotScreen(const ScreenObj& screen) { return screen.name() == "matchmaking_band_screen"; }

std::vector<std::unique_ptr<PlayerSlotObj>> makePlayerSlots(UiManager& ui, ScreenObj& screen) {
    std::vector<std::unique_ptr<PlayerSlotObj>> out;
    const DataNode* pp = screen.handler("player_panels");
    if (!pp) return out;
    const DataNode* panelNode = pp->find("panel");
    const DataNode* type = pp->find("type");
    if (!panelNode || panelNode->children.size() < 2 || !type) return out;
    PanelObj* panel = panelNode->children[1].isText() ? ui.panel(panelNode->children[1].s) : nullptr;
    if (!panel) return out;
    // (slots COOP_SLOTS_ORDER): the slot names in order.
    std::vector<std::string> names;
    if (const DataNode* slots = pp->find("slots"); slots && slots->children.size() > 1) {
        if (slots->children[1].isList()) {
            for (const DataNode& n : slots->children[1].children)
                if (n.isText()) names.push_back(n.s);
        } else {
            for (size_t k = 1; k < slots->children.size(); ++k)
                if (slots->children[k].isText()) names.push_back(slots->children[k].s);
        }
    }
    if (names.empty()) names.push_back("guitar");
    // One local player (every screen but the band lobby): slot 0, the
    // others' directories hidden.
    if (!multiSlotScreen(screen)) {
        names.resize(1);
        if (panel->view)
            for (int n = 1; n < 4; ++n) panel->view->setScopeShowing("slot" + std::to_string(n), false);
    }
    for (size_t n = 0; n < names.size(); ++n) {
        auto s = std::make_unique<PlayerSlotObj>(ui, screen, *panel, *type, pp->find("options"), int(n), names[n]);
        ui.interp().registerObject(s.get());
        out.push_back(std::move(s));
    }
    return out;
}

}  // namespace ui
