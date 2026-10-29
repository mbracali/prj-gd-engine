// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/iso9660.h"

#include <cctype>
#include <vector>

namespace {

constexpr uint64_t kSector = 2048;

uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

bool equalsIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

}  // namespace

std::optional<Iso9660> Iso9660::open(RandomFile& image) {
    uint8_t pvd[kSector];
    if (image.size() < 17 * kSector) return std::nullopt;
    image.read(16 * kSector, pvd, kSector);
    if (pvd[0] != 1 || std::string(reinterpret_cast<char*>(pvd + 1), 5) != "CD001") return std::nullopt;
    std::string vol(reinterpret_cast<const char*>(pvd + 40), 32);
    while (!vol.empty() && (vol.back() == ' ' || vol.back() == '\0')) vol.pop_back();
    const uint8_t* root = pvd + 156;  // root directory record
    return Iso9660(image, Extent{le32(root + 2) * kSector, le32(root + 10)}, vol);
}

std::optional<Iso9660::Extent> Iso9660::findIn(Extent dir, const std::string& name, bool wantDir) const {
    std::vector<uint8_t> buf(dir.size);
    image_->read(dir.offset, buf.data(), buf.size());
    size_t pos = 0;
    while (pos < buf.size()) {
        uint8_t len = buf[pos];
        if (len == 0) {  // records never span sectors; skip to the next one
            pos = (pos / kSector + 1) * kSector;
            continue;
        }
        if (pos + len > buf.size() || len < 34) break;
        const uint8_t* rec = &buf[pos];
        bool isDir = rec[25] & 2;
        std::string id(reinterpret_cast<const char*>(rec + 33), rec[32]);
        if (auto semi = id.find(';'); semi != std::string::npos) id.resize(semi);
        if (!id.empty() && id.back() == '.') id.pop_back();
        if (isDir == wantDir && equalsIgnoreCase(id, name))
            return Extent{le32(rec + 2) * kSector, le32(rec + 10)};
        pos += len;
    }
    return std::nullopt;
}

std::optional<Iso9660::Extent> Iso9660::find(const std::string& path) const {
    Extent cur = root_;
    size_t start = 0;
    while (true) {
        size_t slash = path.find('/', start);
        bool last = slash == std::string::npos;
        std::string part = path.substr(start, last ? std::string::npos : slash - start);
        auto next = findIn(cur, part, !last);
        if (!next) return std::nullopt;
        if (last) return next;
        cur = *next;
        start = slash + 1;
    }
}
