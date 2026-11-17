// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

namespace gd24 {
class Umd;
}

// gd24 --highwaytest <disc> [--sheet <file.bmp>]: decodes STREAMS/COMMON.STR's
// textures and meshes (data/psp_gfx.h), lists the highway's pieces
// (track_plane_A, plate_4a..d, beat_bar*, gem_4a..d / 4od, the crash
// pieces) and checks the four gem textures' colours (red, yellow, green,
// blue: the original's RED / YELLOW / GREEN / BLUE NOTE). --sheet writes
// every texture side by side. Exit code 1 on a failed check.
int highwayTest(const gd24::Umd& umd, const std::string& sheet);
