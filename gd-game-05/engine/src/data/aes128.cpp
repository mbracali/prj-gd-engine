// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/aes128.h"

#include <cstring>

namespace {

uint8_t xtime(uint8_t a) { return uint8_t((a << 1) ^ ((a & 0x80) ? 0x1B : 0)); }

uint8_t gmul(uint8_t a, uint8_t b) {
    uint8_t p = 0;
    while (b) {
        if (b & 1) p ^= a;
        a = xtime(a);
        b >>= 1;
    }
    return p;
}

struct SBox {
    uint8_t s[256];
    SBox() {
        for (int x = 0; x < 256; ++x) {
            // Inverse by search (256 x 256 once at start-up); 0 maps to 0.
            uint8_t inv = 0;
            for (int y = 1; y < 256 && x; ++y)
                if (gmul(uint8_t(x), uint8_t(y)) == 1) {
                    inv = uint8_t(y);
                    break;
                }
            uint8_t b = inv, r = inv;
            for (int i = 0; i < 4; ++i) {
                b = uint8_t((b << 1) | (b >> 7));
                r ^= b;
            }
            s[x] = uint8_t(r ^ 0x63);
        }
    }
};

const SBox& sbox() {
    static const SBox box;
    return box;
}

}  // namespace

Aes128::Aes128(const uint8_t key[16]) {
    const uint8_t* s = sbox().s;
    std::memcpy(roundKeys_.data(), key, 16);
    uint8_t rcon = 1;
    for (int i = 16; i < 176; i += 4) {
        uint8_t t[4];
        std::memcpy(t, &roundKeys_[size_t(i) - 4], 4);
        if (i % 16 == 0) {
            const uint8_t first = t[0];
            t[0] = uint8_t(s[t[1]] ^ rcon);
            t[1] = s[t[2]];
            t[2] = s[t[3]];
            t[3] = s[first];
            rcon = xtime(rcon);
        }
        for (int k = 0; k < 4; ++k) roundKeys_[size_t(i + k)] = uint8_t(roundKeys_[size_t(i + k - 16)] ^ t[k]);
    }
}

void Aes128::encryptBlock(const uint8_t in[16], uint8_t out[16]) const {
    const uint8_t* s = sbox().s;
    uint8_t st[16];
    for (int i = 0; i < 16; ++i) st[i] = uint8_t(in[i] ^ roundKeys_[size_t(i)]);
    for (int round = 1; round <= 10; ++round) {
        uint8_t t[16];
        // SubBytes + ShiftRows (column-major state: byte r + 4c).
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) t[r + 4 * c] = s[st[r + 4 * ((c + r) % 4)]];
        if (round < 10)
            for (int c = 0; c < 4; ++c) {  // MixColumns
                uint8_t* col = t + 4 * c;
                const uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
                col[0] = uint8_t(xtime(a0) ^ (xtime(a1) ^ a1) ^ a2 ^ a3);
                col[1] = uint8_t(a0 ^ xtime(a1) ^ (xtime(a2) ^ a2) ^ a3);
                col[2] = uint8_t(a0 ^ a1 ^ xtime(a2) ^ (xtime(a3) ^ a3));
                col[3] = uint8_t((xtime(a0) ^ a0) ^ a1 ^ a2 ^ xtime(a3));
            }
        for (int i = 0; i < 16; ++i) st[i] = uint8_t(t[i] ^ roundKeys_[size_t(16 * round + i)]);
    }
    std::memcpy(out, st, 16);
}
