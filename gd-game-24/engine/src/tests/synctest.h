// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

namespace gd24 {
class Umd;
}

// gd24 --synctest <disc> [<song>]: where the preview sits in the chart. For
// each song: the DTA's (preview start end) against the AT3's length, and the
// lag that best lines the preview's onsets (spectral flux, 10 ms frames) up
// with the Expert drum gems placed from the preview's start; 0 ms = the
// preview starts exactly at (preview <start>).
namespace gd24 {
int syncTest(const Umd& umd, const std::string& song);
}
