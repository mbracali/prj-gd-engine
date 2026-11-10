// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/xdvdfs.h"

#include <cctype>
#include <cstring>
#include <vector>

namespace {

constexpr uint64_t kSector = 2048;
// Where the game partition starts, by disc type (the offsets tools/xdvdfs.py
// tries): 0 for an image of the partition alone, then the usual XGD2, XGD3
// and original-Xbox offsets [I]. GH2's Rev1 image: 0xFD90000 (XGD2) [C].
constexpr uint64_t kPartitions[] = {0x0, 0xFD90000, 0x2080000, 0x18300000};
constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";

uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

// The tree is sorted by the upper-cased name [I]; compare the same way.
int compareName(const std::string& a, const std::string& b) {
    size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        int x = std::toupper(static_cast<unsigned char>(a[i])), y = std::toupper(static_cast<unsigned char>(b[i]));
        if (x != y) return x < y ? -1 : 1;
    }
    return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}

}  // namespace

std::optional<Xdvdfs> Xdvdfs::open(RandomFile& image) {
    for (uint64_t base : kPartitions) {
        uint64_t at = base + 32 * kSector;
        uint8_t vd[28];
        if (at + sizeof vd > image.size()) continue;
        image.read(at, vd, sizeof vd);
        if (std::memcmp(vd, kMagic, 20) != 0) continue;
        return Xdvdfs(image, base, Extent{base + le32(vd + 20) * kSector, le32(vd + 24)});
    }
    return std::nullopt;
}

std::optional<Xdvdfs::Extent> Xdvdfs::findIn(Extent dir, const std::string& name, bool wantDir) const {
    if (dir.size == 0 || dir.size > (64u << 20)) return std::nullopt;
    std::vector<uint8_t> buf(dir.size);
    image_->read(dir.offset, buf.data(), buf.size());
    // Walk every entry (as tools/xdvdfs.py does) instead of trusting the tree
    // order: directories are small, and this does not depend on the sort rule.
    std::vector<size_t> stack{0};
    std::vector<bool> seen(buf.size() / 4 + 1, false);
    while (!stack.empty()) {
        size_t off = stack.back();
        stack.pop_back();
        if (off + 14 > buf.size() || seen[off / 4]) continue;
        seen[off / 4] = true;
        const uint8_t* e = &buf[off];
        uint16_t left = le16(e), right = le16(e + 2);
        if (left == 0xFFFF && right == 0xFFFF) continue;
        uint8_t attr = e[12], len = e[13];
        if (off + 14 + len > buf.size()) continue;
        std::string entry(reinterpret_cast<const char*>(e + 14), len);
        bool isDir = attr & 0x10;
        if (isDir == wantDir && compareName(entry, name) == 0)
            return Extent{base_ + uint64_t(le32(e + 4)) * kSector, le32(e + 8)};
        if (left && left != 0xFFFF) stack.push_back(size_t(left) * 4);
        if (right && right != 0xFFFF) stack.push_back(size_t(right) * 4);
    }
    return std::nullopt;
}

std::optional<Xdvdfs::Extent> Xdvdfs::find(const std::string& path) const {
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
