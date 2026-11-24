// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

// Onboarding Step 7.10 (gate 95%) — the Enhanced menu + cheats + "The Glitches"
// band, headless and data-derived. Proves, on the real disc:
//   * the Enhanced option catalogue is complete (every universal option the
//     port_enhanced host must carry), its start-up defaults respect the game's
//     ORIGINAL look (per-OS, effects off / GH3's own look, cheats off), and the
//     store honours every option (set/get round-trip, invalid refused, unknown
//     dropped, numeric range);
//   * every cheat takes a real effect: Always Max Multiplier (x4/x8), Hyperspeed
//     (travel time), Star Power Always Full (pinned 100), Easy Chords, Forgiving
//     HOPOs, Unlock Everything (unlock-all scripts + the save-wide unlocked
//     sweep), plus the game's own Options>Cheats table (guitar_hero_cheats);
//   * "The Glitches" is the fully-unlocked house band (band_info named
//     The Glitches, $1,000,000, unlock-all run), the last band in the
//     band-selection screen.
// Returns 0 on success, 1 on a failed check, 2 if the disc cannot be read.
int runEnhancedTest(const std::string& discPath);
