// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>

namespace qs {
class Vm;
}

// GH3's progression atoms (RegisterAtoms / UpdateAtoms / DeRegisterAtoms):
// the career's dependency graph (GH3_Career_Progression,
// GH3_CoopCareer_Progression) that completes tiers, unlocks the encores and
// the bosses and the next tier (docs/modes.md "Career progression atoms").
// `difficulty`: SetProgressionDifficulty's index (the `required` arrays').
void addAtomNatives(qs::Vm& vm, std::shared_ptr<int> difficulty);
