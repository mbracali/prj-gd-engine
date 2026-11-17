// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

// The save folder (SDL_GetPrefPath(gd-engine, gd24)), with a trailing
// separator; "" when SDL cannot give one.
std::string prefDir();
