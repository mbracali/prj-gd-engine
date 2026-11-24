// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

// --jamtest: parse the keyed header of every GHTunes .jam on the disc (19 in
// DATA/COMPRESSED/JAMS/jam1.pak.xen) with 0 errors; report each jam's section
// markers, bpm, instrument tracks and the classified+measured note payload.
int runJamTest(const std::string& discPath);
