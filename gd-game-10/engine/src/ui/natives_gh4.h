// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

namespace qs {
class Vm;
}

// gh4 (Metallica) C functions the scripts call that GH3 did not have
// (natives_gh4.cpp): the player info (GetPlayerInfo / SetPlayerInfo).
void addGh4Natives(qs::Vm& vm);
