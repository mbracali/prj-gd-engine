// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/crypt.h"

Bytes harmonixDecrypt(const Bytes& encrypted) {
    ByteReader r(encrypted);
    int32_t key = static_cast<int32_t>(r.u32le());
    Bytes out(encrypted.begin() + 4, encrypted.end());
    for (uint8_t& b : out) {
        // Schrage: 16807 * key mod 2147483647 without overflow (0x1F31D = 127773, 0xB14 = 2836).
        key = (key % 0x1F31D) * 0x41A7 - (key / 0x1F31D) * 0xB14;
        if (key <= 0) key += 0x7FFFFFFF;
        b ^= static_cast<uint8_t>(key & 0xFF);
    }
    return out;
}
