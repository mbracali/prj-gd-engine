// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

// `gd03 --scenetest <iso | game folder>`: parses every .milo_ps2 in the
// archive and prints, per class, how many objects decoded exactly to their
// 0xADDEADDE end marker. Returns 0 only if every file parses and every object
// of a typed class decodes. With GD03_SCENE_DUMP=<ark path> it also lists
// that milo's objects with their decoded fields.
int runSceneTest(const std::string& dataPath);
