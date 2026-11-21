// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/param_sfo.h"

#include <cstring>

namespace gd24 {

namespace {
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
}  // namespace

bool ParamSfo::parse(const std::vector<uint8_t>& b, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    if (b.size() < 20 || std::memcmp(b.data(), "\0PSF", 4) != 0) return fail("no \\0PSF header");
    const uint32_t keys = le32(&b[8]), data = le32(&b[12]), count = le32(&b[16]);
    if (20 + uint64_t(count) * 16 > b.size() || keys > b.size() || data > b.size()) return fail("table past the end");
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = &b[20 + i * 16];
        const uint32_t k = keys + le16(e), fmt = le16(e + 2), len = le32(e + 4), at = data + le32(e + 12);
        if (k >= b.size() || uint64_t(at) + len > b.size()) return fail("entry past the end");
        const char* kp = reinterpret_cast<const char*>(&b[k]);
        const std::string key(kp, strnlen(kp, b.size() - k));
        if (fmt == 0x0404 && len >= 4) numbers[key] = le32(&b[at]);
        else {
            std::string v(reinterpret_cast<const char*>(&b[at]), len);
            if (const size_t z = v.find('\0'); z != std::string::npos) v.resize(z);
            strings[key] = v;
        }
    }
    return true;
}

}  // namespace gd24
