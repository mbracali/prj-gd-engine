// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ui/natives.h"

// GH2's store (docs/career_and_store.md): StorePanel (0x8229D678: price,
// low_cost, high_cost) and the providers career.dta's store_screen uses:
// store_item_provider (0x822C0658), store_guitar_provider (0x822C44D0,
// in_stock 0x822C02A8), store_skin_provider (0x822C6128) and
// store_video_provider (0x822C3D10). Buying is campaign buy_item
// (ui/campaign.h).
namespace ui::store {

// StorePanel; nullptr for other classes.
std::unique_ptr<ScriptObject> makeClass(UiManager& ui, const std::string& cls, const std::string& name);
// The providers (named objects).
void installNatives(UiManager& ui, Interp& in, std::vector<std::unique_ptr<ScriptObject>>& owned);

}  // namespace ui::store
