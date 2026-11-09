// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>

#include "data/bytes.h"

// The header of a GH2 .mogg (docs/audio_and_charts.md, tools/mogg.py) [C]:
// a small Harmonix header, then an ordinary Ogg Vorbis stream. All
// little-endian u32:
//   0x00 version         10 = plain Ogg (every GH2 file; 11 and up, used by
//                        later Harmonix games, are encrypted and refused)
//   0x04 Ogg offset      where "OggS" starts, always 20 + 8 * map entries
//   0x08 map version     14
//   0x0C map interval    20000 PCM samples per map entry
//   0x10 map entries     then that many (u32 byte offset, u32 sample) pairs:
//                        a seek table for the console's 32 KiB block
//                        streamer. A PC decoder seeks by itself, so the map
//                        is only checked, not used.
struct MoggHeader {
    uint32_t version = 0;
    uint32_t oggOffset = 0;
    uint32_t mapEntries = 0;
};

// Parses and checks the header of a .mogg's first bytes (at least 20, and up
// to the Ogg data for the "OggS" check). Throws std::runtime_error with the
// reason when it is not a plain GH2 mogg.
MoggHeader parseMoggHeader(const uint8_t* data, size_t size);
