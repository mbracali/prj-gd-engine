// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The "Custom songs" overlay for GH1 (additive-songs mod, owner §7, GDALL-042):
// a semi-transparent menu opened with the yellow fret on the Quick Play song
// screen, listing the shared games' songs by game; selecting one starts it
// through the normal Quick Play flow. GH1's port UI (NativePanel + SelectHost).
#pragma once

#include <memory>

#include "ui/select_panels.h"

std::unique_ptr<NativePanel> makeCustomSongsPanel(SelectHost& host);
