// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "scene/synth_objects.h"

// A SynthSample's audio -> mono float PCM. The PS2 disc's samples are SPU2
// PS-ADPCM (SampleData format kVag; audio/adpcm.h); 16-bit PCM is decoded
// too. Returns false with `error` set when the format is not one of those or
// the data is broken; `frames` gets the decoded count.
bool decodeSampleData(const scene::SynthSample& s, std::vector<float>& pcm, std::string& error, uint32_t* frames = nullptr);
