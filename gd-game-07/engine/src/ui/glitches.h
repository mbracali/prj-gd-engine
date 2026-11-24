// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

namespace qs {
class Vm;
}

#include <cstdint>
#include <unordered_map>

#include "data/qb.h"

// The Glitches (the port's, kickoff rule 11; gd01 / gd02 have them too):
// every save has a band named "The Glitches" in GH3's last band slot (5),
// solo and co-op, with everything unlocked, as GH3's own debug unlock
// (playday_unlockall -> GlobalTags_UnlockAll) leaves a band: every tier
// unlocked and complete with its encore and boss, every song five stars at
// every difficulty, $1,000,000; the quick play songs its career unlocks are
// save-wide, as GH3's are. Run after the save is loaded or created;
// returns whether anything changed.
bool ensureGlitches(qs::Vm& vm);

// Enhanced > CHEATS > Unlock Everything (issue #94): unlock everything the way
// gd07 already does for The Glitches at the main menu -- run gh4's own
// unlock-all scripts (unlock_all_profiles, purchase_all_profiles) and set
// `unlocked = 1` on every save tag that has one (characters, venues and any
// career/song tags present). Returns whether it ran. FLAG: gd07's career /
// song-tier progression is not a working gate yet, so this covers profiles,
// purchases and every `unlocked` tag but does not drive career-tier
// progression; quickplay already lists every set-list song on the disc
// regardless of unlock state (IsSongAvailable is disc-presence only).
bool unlockEverything(qs::Vm& vm);

// The Glitches on the gh4 engine (World Tour / Metallica). The port's house
// band: the original's own unlock-all scripts (unlock_all_profiles /
// purchase_all_profiles -> GlobalTags_UnlockAll) leave the save with every
// song, venue, character, instrument and bonus unlocked; the save's band
// becomes "The Glitches" (current_band_has_band_name reads band_info.name) with
// the house band's $1,000,000, so it is the fully-unlocked band the player
// meets last in the band-selection screen. Singleplayer career also gets a
// premade character seeded (no live Create-A-Rocker). Run once after the first
// main menu (front_end) and from --enhancedtest. Returns whether it changed.
bool ensureGlitchesGh4(qs::Vm& vm);

// The Vault: GH3 keeps its purchases (guitars, finishes, characters,
// outfits, styles, videos, bonus songs) save-wide; the port scopes "all
// bought" to The Glitches' band only (owner 2026-10-29, GD10-026). While the
// band's career is popped (PopGlobalTags of a band-5 section) those tags
// read all bought; popping another band or pushing the Glitches' section
// back restores the save's own state. Installs the VM hook.
void installGlitchesStore(qs::Vm& vm);
// The tags to save: the save's own store state while the Glitches' is in place.
std::unordered_map<uint32_t, qb::Struct> glitchesSaveView(const qs::Vm& vm);
