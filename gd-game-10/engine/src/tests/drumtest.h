// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// --drumtest: data-free checks of the gh4 drum judge (DrumPlayer) and the
// input layer's drum-kit guard. Plays a synthetic drum chart through the judge
// and asserts every gem is hit on its lane with the score, combo and per-pad
// velocity advancing; also checks wrong-lane strikes miss, and that the
// drum-kit action guard drops exactly the guitar play actions. Needs no disc.
// Returns 0 on success, non-zero on the first failed check.
int runDrumTest();
