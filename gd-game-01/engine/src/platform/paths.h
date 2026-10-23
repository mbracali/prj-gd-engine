// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

// The save and settings folder (config.txt, career.json, enhanced.json,
// controllers.json, gpu_system, DLSS data), with a trailing separator, or ""
// when there is none. The first call moves an old gh1re folder's files over
// (the game was called gh1re before 0.1.0); the old folder is left in place.
std::string prefDir();

// The developer environment variables were GH1RE_*; they are GD01_* now. Call
// first thing in main: copies every GH1RE_x the user set to GD01_x, unless
// GD01_x is set too. (Accepted for now; to be removed in a later major version.)
void aliasLegacyEnv();
