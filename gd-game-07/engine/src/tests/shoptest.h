// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

// --shoptest <disc>: the World Tour store, headless. Builds the real CAS
// purchase catalog from the disc, proves a purchase deducts cash, sets the
// save's `purchased` flag and unlocks the item, and that it persists through a
// save write/read round-trip. Objective (data-derived, no eyeballing).
// Returns 0 on success, non-zero on any failed check.
int runShopTest(const std::string& discPath);
