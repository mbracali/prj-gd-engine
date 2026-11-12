// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

class Globals;
namespace qb {
struct Struct;
}

namespace pcstrings {

// GH3's Xbox 360 / Xbox LIVE / storage device / achievement wording, as the
// port's PC wording (as gd-game-01..03; GD10-039). `text` itself when it has
// no PC version. GH3's disc is English only.
const std::string& pcText(const std::string& text);

// gh4's localized strings (qs.pak) in the port's PC wording (GD10-035):
// whole-string rewrites, then rules (pads, headsets, the console, storage
// devices; online-only messages say online play is not in the port). A
// leading \L is kept. `text` itself when nothing names the console.
std::string pcQsText(const std::string& text);

// Applies pcText to every string in a struct (a script's inline structs:
// `textblock = { Text = ... }`).
void applyToStruct(qb::Struct& st);

// Applies pcText to every string in the globals, and swaps the online
// loading tip for the GD-Engine tips (loading_screen_tips).
void applyToGlobals(Globals& globals);

}  // namespace pcstrings
