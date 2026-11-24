// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

// --exploretest <disc>: the launcher's model explorer (gd_game_explore,
// platform/module_explore.cpp) headless. Reproduces the explorer's item list
// (Venues / Guitars / Bass / Drums / Mics / Chars) with the same enumerators
// and loads + render-adds every instrument and character model through the
// band renderer exactly as the explorer's select() does, so the Explore
// categories are proven to show and use real models without the launcher.
// Prints a per-group count line and returns non-zero on any failure.
int runExploreTest(const std::string& discPath);
