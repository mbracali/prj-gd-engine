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
// Wave 01: one slot per local player (player 0 for the solo modes) [I: the
// original's MultiFocusScreen natives are not read yet] (GD05-005).
#include "ui/rb_multifocus.h"

#include <SDL3/SDL.h>

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
}

PlayerSlotObj::~PlayerSlotObj() { ui_.interp().unregisterObject(this); }

bool PlayerSlotObj::handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) {
    // The panel's component natives, as the slot's own.
    if (msg == "find" || msg == "set_focus" || msg == "iterate" || msg == "disable" || msg == "enable" || msg == "focus_name" ||
        msg == "is_up")
        return r = in.send(&panel_, msg, args), true;
    if (msg == "set_showing") return showing_ = !args.empty() && args[0].truthy(), r = Value::integer(0), true;
    if (msg == "showing") return r = Value::integer(showing_ ? 1 : 0), true;
    return false;
}

Value PlayerSlotObj::dispatch(const std::string& msg, std::vector<Value> args) {
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

bool PlayerSlotObj::ready() {
    const Value r = ui_.callScript(this, "is_ready");
    return r.isUnhandled() || r.truthy();
}

std::vector<std::unique_ptr<PlayerSlotObj>> makePlayerSlots(UiManager& ui, ScreenObj& screen) {
    std::vector<std::unique_ptr<PlayerSlotObj>> out;
    const DataNode* pp = screen.handler("player_panels");
    if (!pp) return out;
    const DataNode* panelNode = pp->find("panel");
    const DataNode* type = pp->find("type");
    if (!panelNode || panelNode->children.size() < 2 || !type) return out;
    PanelObj* panel = panelNode->children[1].isText() ? ui.panel(panelNode->children[1].s) : nullptr;
    if (!panel) return out;
    // (slots COOP_SLOTS_ORDER): the slot names in order; one local player: slot 0.
    std::string slot = "guitar";
    if (const DataNode* slots = pp->find("slots"); slots && slots->children.size() > 1) {
        const DataNode& first = slots->children[1].isList() && !slots->children[1].children.empty() ? slots->children[1].children[0] : slots->children[1];
        if (first.isText()) slot = first.s;
    }
    auto s = std::make_unique<PlayerSlotObj>(ui, screen, *panel, *type, pp->find("options"), 0, slot);
    ui.interp().registerObject(s.get());
    out.push_back(std::move(s));
    return out;
}

}  // namespace ui
