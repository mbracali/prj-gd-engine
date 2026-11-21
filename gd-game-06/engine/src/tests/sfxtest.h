// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

// --sfxtest <disc>: decodes every sample of every FMOD bank (the songs'
// stems, streamall) and checks the decoded lengths.
int runSfxTest(const std::string& discPath);
