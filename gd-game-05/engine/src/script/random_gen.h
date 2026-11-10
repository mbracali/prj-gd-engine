// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>

// GH2's RandomGen [C]: one global instance at 0x82783D08, seeded with 666
// (0x29A) at start-up (0x826E1738). Seed (0x8226DB90): 256 words from a
// linear congruential generator (x * 0x41C64E6D + 0x3039, two steps per
// word: (second & 0x7FFF0000) | (first >> 16)), i = 0, j = 103. Next
// (0x8226DBE0): t[i] ^= t[j]; the result is t[i]; i and j advance and wrap
// past 248 (a 249-word lagged XOR generator). RandomInt(lo, hi)
// (0x8226DCB8): lo + int(r) % (hi - lo); RandomFloat() (0x8226DD10): the low
// 16 bits / 65536; RandomFloat(lo, hi) (0x8226DDA0): lo + that x (hi - lo).
// Script random_int / random_float (0x8232AB20 / 0x8232AB80) use the same
// generator.
class RandomGen {
public:
    explicit RandomGen(uint32_t seed = 0x29Au) { reseed(seed); }

    void reseed(uint32_t s) {
        for (uint32_t& w : t_) {
            const uint32_t a = s * 0x41C64E6Du + 0x3039u;
            s = a * 0x41C64E6Du + 0x3039u;
            w = (s & 0x7FFF0000u) | (a >> 16);
        }
        i_ = 0, j_ = 103;
    }
    uint32_t next() {
        t_[i_] ^= t_[j_];
        const uint32_t r = t_[i_];
        if (++i_ > 248) i_ = 0;
        if (++j_ > 248) j_ = 0;
        return r;
    }
    // [lo, hi); hi <= lo gives lo (GH2 traps on a zero range).
    int integer(int lo, int hi) { return hi <= lo ? lo : lo + int(next()) % (hi - lo); }
    float real01() { return float(next() & 0xFFFFu) * 1.5258789e-05f; }
    float real(float lo, float hi) { return real01() * (hi - lo) + lo; }

private:
    uint32_t t_[256] = {};
    int i_ = 0, j_ = 103;
};
