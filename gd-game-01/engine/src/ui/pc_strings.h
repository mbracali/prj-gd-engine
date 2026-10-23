// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <map>
#include <string>

// [I] The port's own wording over the original locale (docs/texts_loading_and_save.md):
// the memory-card texts become PC save texts, plus the GD01 loading tips.
// Each token replaces both `tok` and `tok_ps2` (memcard localize looks up the
// _ps2 form first).
void applyPcStrings(std::map<std::string, std::string>& strings);

// The extra loading tips (loading_tip30 ...), added to config/tips.dta's pool.
extern const char* const kPcLoadingTips[];
extern const int kPcLoadingTipCount;
