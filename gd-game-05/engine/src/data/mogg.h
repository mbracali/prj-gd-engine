// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>

#include "data/bytes.h"

// The header of a .mogg (docs/formats.md "Song audio", tools/mogg.py). All
// little-endian u32:
//   0x00 version         10 = plain Ogg (GH2); 11 = the Ogg encrypted with
//                        AES-128 in counter mode (every Rock Band song) [C
//                        RB 0x827E02A8: a version above 10 reads the nonce
//                        and starts the cipher]
//   0x04 Ogg offset      where the Ogg data starts: 20 + 8 * map entries,
//                        plus 16 for version 11's nonce
//   0x08 map version     14
//   0x0C map interval    20000 PCM samples per map entry
//   0x10 map entries     then that many (u32 byte offset, u32 sample) pairs:
//                        the console streamer's seek table (checked only)
//   version 11: then the 16-byte initial counter (nonce)
struct MoggHeader {
    uint32_t version = 0;
    uint32_t oggOffset = 0;
    uint32_t mapEntries = 0;
};

// Parses and checks the header of a .mogg's first bytes (at least 20).
// Throws std::runtime_error with the reason when it is not a mogg this game
// reads. The "OggS" check is made only on plain (or decrypted) data.
MoggHeader parseMoggHeader(const uint8_t* data, size_t size, bool checkOgg = true);

// Decrypts a version 11 mogg's Ogg data in place (the whole file, or its
// first `size` bytes); the header is left as it is, so call it once per
// buffer. A version 10 mogg is left as is. Throws when the result does not
// start with an Ogg page (wrong key or data).
void decryptMogg(uint8_t* data, size_t size);
inline void decryptMogg(Bytes& mogg) { decryptMogg(mogg.data(), mogg.size()); }
