// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

namespace gd24 {
class Umd;
}

// gd24 --audiotest <disc> [<song folder> | all]: decodes the song previews
// (every song by default) and checks each: no decode errors, the fact
// chunk's length (within one frame), PCM that is not silent. One line per
// song; exit code 1 on any failure. --play also plays the first one.
int audioTest(const gd24::Umd& umd, const std::string& which, bool play);
