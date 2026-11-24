// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

// --castest <disc>: World Tour's Create-A-Rocker character creation, headless.
// Builds the real editable CAS slot set from the disc, creates a character from
// the default profile, names it, sets slots to catalog options (respecting
// shop ownership), proves an unowned/locked option is rejected, round-trips the
// created characters through a save write/read, and feeds a created profile to
// the in-game band builder (memberGh4) to confirm it is a playable member.
// Objective (data-derived, no eyeballing). Returns 0 on success.
int runCasTest(const std::string& discPath);
