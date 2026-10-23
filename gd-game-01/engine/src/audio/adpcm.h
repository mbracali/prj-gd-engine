// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// A mono SPU2 PS-ADPCM sample (16-byte blocks of 28 samples), as stored in
// the sound banks' .nse data.
struct AdpcmSample {
    std::vector<int16_t> pcm;
    int loopStart = -1;  // sample index of the block flagged "loop start" (flag bit 2)
    bool loops = false;  // the end block also has the "repeat" flag (flags 3)
};

// Decodes blocks from `data` until a block with the end flag (bit 0), or
// `maxBytes`. Decoder state starts at zero.
AdpcmSample decodeAdpcm(const uint8_t* data, size_t maxBytes);
