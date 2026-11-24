// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

// --careertest: drive World Tour's Career (the main mode) progression on the
// real disc data, headless. Returns 0 on success, 1 on a failed check, 2 on a
// setup error.
int runCareerTest(const std::string& discPath);
