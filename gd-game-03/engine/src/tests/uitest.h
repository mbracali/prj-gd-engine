// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

// `gd03 --uitest <disc>`: GH2's menus without a window. Runs the UI's boot
// scripts with the real UI objects (every `new` definition), loads every
// panel's milo, and checks each component name a panel's own script uses
// (tip.lbl, ss_song.lst, splash.view...) against the objects of that
// milo and its subdirectories. Exits 1 on any UNKNOWN (a missing class,
// object, screen or component) or UNHANDLED message in the boot run.
int runUiTest(const std::string& dataPath);
