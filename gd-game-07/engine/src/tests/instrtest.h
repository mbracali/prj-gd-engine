// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

// --instrtest: the Create-A-Rockstar instrument editor on the real disc CAS
// catalog, headless. Builds/edits/names/saves an instrument and proves a
// save->reload round-trip, option-gating and ownership-gating.
int runInstrTest(const std::string& discPath);
