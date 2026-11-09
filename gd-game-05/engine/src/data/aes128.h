// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>

// AES-128 (FIPS-197), the forward cipher only: enough for counter mode, as
// Rock Band's version 11 moggs use (data/mogg.h). Written for this project;
// the S-box is computed at start-up from its definition (the multiplicative
// inverse in GF(2^8) followed by the affine map).
class Aes128 {
public:
    explicit Aes128(const uint8_t key[16]);
    void encryptBlock(const uint8_t in[16], uint8_t out[16]) const;

private:
    std::array<uint8_t, 176> roundKeys_{};
};
