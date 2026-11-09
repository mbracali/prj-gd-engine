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

}  // namespace ui
