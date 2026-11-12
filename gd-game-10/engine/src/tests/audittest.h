// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

// --audittest <disc> [out.md]: the asset / reference audit (kickoff rule 3):
// every name GH3's scripts call, and every file type in the paks, against
// what the engine resolves. Writes the gaps as markdown (default stdout).
int runAuditTest(const std::string& discPath, const std::string& outPath);
