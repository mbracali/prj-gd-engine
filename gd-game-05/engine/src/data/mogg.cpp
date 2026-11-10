// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/mogg.h"

#include <cstring>
#include <stdexcept>
#include <string>

#include "data/aes128.h"

namespace {

// The version 11 key: 16 bytes in the executable's .data at RB 0x828B9AB4,
// handed to the cipher set-up by the mogg header reader (RB 0x827E02A8) [C].
// DECISIONS.md D3: kept here as a constant (the executable on the disc is
// itself encrypted); OPEN_QUESTIONS.md asks the owner.
constexpr uint8_t kMoggKey[16] = {0x37, 0xB2, 0xE2, 0xB9, 0x1C, 0x74, 0xFA, 0x9E,
                                  0x38, 0x81, 0x08, 0xEA, 0x36, 0x23, 0xDB, 0xE4};

}  // namespace

MoggHeader parseMoggHeader(const uint8_t* data, size_t size, bool checkOgg) {
    ByteReader r(data, size);
    if (size < 20) throw std::runtime_error("too short for a mogg header");
    MoggHeader h;
    h.version = r.u32le();
    h.oggOffset = r.u32le();
    r.u32le();  // map version (14)
    r.u32le();  // map interval (20000)
    h.mapEntries = r.u32le();
    if (h.version != 10 && h.version != 11)
        throw std::runtime_error("mogg version " + std::to_string(h.version) + ": not a mogg this game reads");
    const uint64_t extra = h.version == 11 ? 16 : 0;
    if (uint64_t(20) + 8 * uint64_t(h.mapEntries) + extra != h.oggOffset)
        throw std::runtime_error("mogg map size does not match its Ogg offset");
    if (checkOgg && h.version == 10 && size >= uint64_t(h.oggOffset) + 4 && std::memcmp(data + h.oggOffset, "OggS", 4) != 0)
        throw std::runtime_error("no Ogg stream at the mogg's Ogg offset");
    return h;
}

// Counter mode as the game runs it [C data: every song decodes to its last
// Ogg page]: keystream block i = AES(nonce + i), the 128-bit counter added
// to little-endian (its first byte is the lowest).
void decryptMogg(uint8_t* data, size_t size) {
    MoggHeader h = parseMoggHeader(data, size, false);
    if (h.version != 11) return;
    if (size < h.oggOffset) throw std::runtime_error("mogg header longer than the bytes read");
    uint8_t counter[16];
    std::memcpy(counter, data + h.oggOffset - 16, 16);
    const Aes128 aes(kMoggKey);
    uint8_t ks[16];
    for (size_t pos = h.oggOffset; pos < size; pos += 16) {
        aes.encryptBlock(counter, ks);
        const size_t n = size - pos < 16 ? size - pos : 16;
        for (size_t k = 0; k < n; ++k) data[pos + k] ^= ks[k];
        for (int k = 0; k < 16; ++k)
            if (++counter[k] != 0) break;
    }
    if (size >= uint64_t(h.oggOffset) + 4 && std::memcmp(data + h.oggOffset, "OggS", 4) != 0)
        throw std::runtime_error("the mogg did not decrypt to an Ogg stream");
}
