// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/str_file.h"

namespace gd24 {
namespace {
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
}  // namespace

bool StrFile::parse(const uint8_t* head, size_t headSize, uint64_t fileSize, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    if (headSize < 0x40) return fail("too short");
    version = le32(head), headerData = le32(head + 4), count = le32(head + 8);
    if (version != 1) return fail("version " + std::to_string(version) + ", not 1");
    if (count == 0 || count > 1024) return fail("entry count " + std::to_string(count));
    const size_t tableEnd = 0x40 + size_t(count + 1) * 0x40;
    if (tableEnd > headSize) return fail("entry table past the bytes read");
    chunks.clear();
    for (uint32_t i = 0; i <= count; ++i) {
        const uint8_t* e = head + 0x40 + size_t(i) * 0x40;
        const uint32_t off = le32(e), size = le32(e + 4), end = le32(e + 8);
        if (uint64_t(off) + size != end) return fail("entry " + std::to_string(i) + ": offset + size != end");
        if (end > fileSize) return fail("entry " + std::to_string(i) + " runs past the end of the file");
        if (size) chunks.push_back({i, off, size});
    }
    return true;
}

}  // namespace gd24
