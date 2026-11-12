// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "script/script.h"

namespace ui {

class UiManager;

// Rock Band's meta game (wave 02, GD05-009): the profile's characters and
// bands, the Solo Tour and the Band World Tour (the `tour` object, its
// performers, events, hints and list providers), the character creator's
// and the Rock Shop's host objects, saved as versioned JSON (lesson L12;
// DECISIONS D4). ui/rb_meta.cpp.
void installRbMeta(UiManager& ui, Interp& in, std::vector<std::unique_ptr<ScriptObject>>& owned);
// The meta game's panel classes (CharCreatorPanel ...), or null.
std::unique_ptr<ScriptObject> makeRbMetaClass(UiManager& ui, const std::string& cls, const std::string& name);
// The outfit catalog's price of a piece (char/outfitloader.dta (cost n);
// an instrument model's, char/instruments.dta (groups (<model> (cost n)))),
// -1 when the catalog has no such piece [C data] (the walks' expectations).
int rbOutfitCost(UiManager& ui, const std::string& gender, const std::string& category, const std::string& outfit);
// An instrument category's first model (char/instruments.dta (groups ...)):
// its name, default finish and price [C data] (the walks' expectations).
struct RbModel {
    std::string model, finish;
    int cost = 0;
};
RbModel rbFirstModel(UiManager& ui, const std::string& gender, const std::string& category);

}  // namespace ui
