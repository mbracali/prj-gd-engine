// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

// --songaudiotest <song>: decodes the song's banks and prints, every 5 s,
// the level of each stereo pair (gh4: 4 + 3 + 2 pairs), to tell which pair
// holds which instrument against the charts (docs/audio.md).
int runSongAudioTest(const std::string& discPath, const std::string& song);
