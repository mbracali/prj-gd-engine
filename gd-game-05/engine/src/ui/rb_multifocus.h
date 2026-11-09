// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "script/script.h"

namespace ui {

class UiManager;
class ScreenObj;
class PanelObj;

// One player's slot on a Rock Band screen with (player_panels ...)
// (ui/rb_multifocus.cpp).
class PlayerSlotObj : public ScriptObject {
public:
    PlayerSlotObj(UiManager& ui, ScreenObj& screen, PanelObj& panel, const DataNode& type, const DataNode* options, int slotNum,
                  const std::string& slot);
    ~PlayerSlotObj() override;
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override;
    // The state's (options) handler, then the type's; unhandled if neither.
    Value dispatch(const std::string& msg, std::vector<Value> args = {});
    bool ready();
    PanelObj& panel() { return panel_; }

private:
    UiManager& ui_;
    ScreenObj& screen_;
    PanelObj& panel_;
    const DataNode* options_;
    bool showing_ = true;
};

// The slots of `screen` (none when it has no player_panels).
std::vector<std::unique_ptr<PlayerSlotObj>> makePlayerSlots(UiManager& ui, ScreenObj& screen);

}  // namespace ui
