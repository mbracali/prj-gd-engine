// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

// `gd02 --selftest <disc>`: the gameplay gate (gd01's runSelfTest, L4).
// Without a window or audio device: decodes every .dtb in the archive,
// checks every song's .mogg header against songs.dtb, builds every chart,
// and auto-plays every part on every difficulty twice through the real
// judgement code (strummed only, then with every legal hammer-on and
// pull-off). Prints "all charts fully hit" and returns 0 when every gem of
// every chart was hit; 1 when not; 2 when the data cannot be read.
int runSelfTest(const std::string& dataPath);
