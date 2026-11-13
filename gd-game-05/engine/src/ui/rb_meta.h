// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "char/patch.h"
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
// The patches a profile character wears (tattoo location -> layers: its
// tattoos' patches, ui.dta (patch (tattoos ...)) mapping a location to its
// set) (GD05-084); empty for other objects.
std::map<std::string, chr::Patch> rbWornPatches(UiManager& ui, ScriptObject* tourChar);
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
// A category's (outfits ...) names in the catalog's order [C data] (the walks' expectations).
std::vector<std::string> rbCategoryOutfits(UiManager& ui, const std::string& gender, const std::string& category);

// What the profile's tours unlock in quick play (SongMgr 0x82432C60 ->
// 0x82431AC8) [C]: per instrument (guitar, vocals, drum: RB 0x823D4738,
// the most of the profile's characters of the instrument, RB 0x823E0458)
// how many of its ranked songs and of its bonus songs, and the songs a
// band of the profile has played (RB 0x823D43D0).
struct RbTourUnlocks {
    static constexpr const char* kInstruments[3] = {"guitar", "vocals", "drum"};
    int ranked[3] = {0, 0, 0}, bonus[3] = {0, 0, 0};
    std::vector<std::string> bandPlayed;
};
RbTourUnlocks rbTourUnlocks();

}  // namespace ui
