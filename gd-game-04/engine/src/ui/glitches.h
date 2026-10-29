// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

namespace qs {
class Vm;
}

// The Glitches (the port's, kickoff rule 11; gd01 / gd02 have them too):
// every save has a band named "The Glitches" in GH3's last band slot (5),
// solo and co-op, with everything unlocked, as GH3's own debug unlock
// (playday_unlockall -> GlobalTags_UnlockAll) leaves a band: every tier
// unlocked and complete with its encore and boss, every song five stars at
// every difficulty, $1,000,000. GH3 keeps guitars, finishes, characters,
// outfits, styles, videos and the quick play / bonus songs save-wide, so
// those are unlocked save-wide (DECISIONS D9). Idempotent: run after the
// save is loaded or created; returns whether anything changed.
bool ensureGlitches(qs::Vm& vm);
