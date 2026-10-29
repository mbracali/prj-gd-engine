// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/crypt.h"

#include <array>
#include <cstdint>

namespace {

class DtbCipher {
public:
    explicit DtbCipher(uint32_t seed) {
        for (auto& word : table_) {
            uint32_t a = seed * 0x41C64E6Du + 0x3039u;
            seed = a * 0x41C64E6Du + 0x3039u;
            word = (a >> 16) | (seed & 0x7FFF0000u);
        }
    }
    uint32_t next() {
        uint32_t v = table_[a_] ^ table_[b_];
        table_[a_] = v;
        a_ = a_ + 1 > 0xF8 ? 0 : a_ + 1;
        b_ = b_ + 1 > 0xF8 ? 0 : b_ + 1;
        return v;
    }

private:
    std::array<uint32_t, 256> table_{};
    uint32_t a_ = 0, b_ = 0x67;
};

}  // namespace

Bytes dtbDecrypt(const Bytes& encrypted) {
    ByteReader r(encrypted);
    DtbCipher c(r.u32le());
    Bytes out(encrypted.begin() + 4, encrypted.end());
    for (uint8_t& b : out) b ^= static_cast<uint8_t>(c.next());
    return out;
}
