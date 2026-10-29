// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/adpcm.h"

#include <algorithm>

namespace {

constexpr int kFilters[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};

}  // namespace

AdpcmSample decodeAdpcm(const uint8_t* data, size_t maxBytes) {
    AdpcmSample out;
    int s1 = 0, s2 = 0;
    for (size_t off = 0; off + 16 <= maxBytes; off += 16) {
        const uint8_t* b = data + off;
        int shift = b[0] & 0x0F;
        int filter = std::min(b[0] >> 4, 4);
        int flags = b[1];
        if (flags & 4) out.loopStart = static_cast<int>(out.pcm.size());
        for (int i = 0; i < 28; ++i) {
            int nibble = (b[2 + i / 2] >> ((i & 1) * 4)) & 0x0F;
            int s = static_cast<int16_t>(nibble << 12) >> shift;
            s += (s1 * kFilters[filter][0] + s2 * kFilters[filter][1] + 32) >> 6;
            s = std::clamp(s, -32768, 32767);
            out.pcm.push_back(static_cast<int16_t>(s));
            s2 = s1;
            s1 = s;
        }
        if (flags & 1) {
            out.loops = (flags & 2) != 0;
            break;
        }
    }
    if (out.loops && out.loopStart < 0) out.loopStart = 0;
    return out;
}
