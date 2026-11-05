// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <map>
#include <string>

namespace scene {

// Replaces Encore's memory card / console / controller port wording with the
// port's PC wording in `language` (eng fre deu ita esl), and adds the
// GD-Engine loading tips (GD03-101).
void applyPcStrings(std::map<std::string, std::string>& strings, const std::string& language);

extern const char* const kPcLoadingTips[];  // join every loading tip pool
extern const int kPcLoadingTipCount;

}  // namespace scene
