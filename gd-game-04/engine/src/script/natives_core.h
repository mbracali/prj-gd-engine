// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include "script/vm.h"

namespace qs {

// Registers the engine-independent C functions (natives_core.cpp).
void addCoreNatives(Vm& vm);

}  // namespace qs
