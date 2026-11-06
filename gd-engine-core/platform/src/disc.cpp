// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "gd/core/platform/disc.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace gd::core::platform {

namespace {

constexpr uint64_t kSector = 2048;
// A folder larger than this is a damaged image, not a folder [I] (gd02's limit).
constexpr uint64_t kMaxDir = 64ull << 20;

uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
char upper(char c) { return c >= 'a' && c <= 'z' ? char(c - 32) : c; }
bool sameName(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (upper(a[i]) != upper(b[i])) return false;
    return true;
}
std::string trim(std::string s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

bool readDir(RandomAccessFile& image, const DiscEntry& dir, std::vector<uint8_t>& buf) {
    if (!dir.isDir || dir.size > kMaxDir) return false;
    buf.resize(size_t(dir.size));
    return image.read(dir.offset, buf.data(), buf.size());
}

// ---- ISO 9660 ----
// Primary volume descriptor at sector 16 ("\1CD001"), the root folder's
// record at byte 156 of it. A folder is a list of records (u8 length, u32 LE
// extent at 2, u32 LE size at 10, flags at 25 (2 = folder), u8 name length at
// 32, name at 33) that never cross a sector: a 0 length skips to the next one
// [C: gd01 data/iso9660.cpp, launcher iso_probe.cpp, ECMA-119 9.1].
class Iso9660 final : public DiscFileSystem {
public:
    Iso9660(std::shared_ptr<RandomAccessFile> image, DiscEntry root, std::string volume)
        : DiscFileSystem(std::move(image)), root_(std::move(root)), volume_(std::move(volume)) {}
    std::string_view format() const override { return "iso9660"; }
    std::string volumeId() const override { return volume_; }

protected:
    DiscEntry root() const override { return root_; }
    bool entries(const DiscEntry& dir, std::vector<DiscEntry>& out) const override {
        std::vector<uint8_t> buf;
        if (!readDir(*image_, dir, buf)) return false;
        size_t pos = 0;
        while (pos < buf.size()) {
            const uint8_t len = buf[pos];
            if (len == 0) {
                pos = (pos / kSector + 1) * kSector;
                continue;
            }
            if (len < 34 || pos + len > buf.size()) break;
            const uint8_t* rec = &buf[pos];
            const uint8_t nameLen = rec[32];
            pos += len;
            if (33u + nameLen > len) continue;
            if (nameLen == 1 && (rec[33] == 0 || rec[33] == 1)) continue;  // "." and ".."
            DiscEntry e;
            e.name.assign(reinterpret_cast<const char*>(rec + 33), nameLen);
            if (size_t semi = e.name.find(';'); semi != std::string::npos) e.name.resize(semi);
            if (!e.name.empty() && e.name.back() == '.') e.name.pop_back();
            e.isDir = (rec[25] & 2) != 0;
            e.offset = uint64_t(le32(rec + 2)) * kSector;
            e.size = le32(rec + 10);
            out.push_back(std::move(e));
        }
        return true;
    }

private:
    DiscEntry root_;
    std::string volume_;
};

// ---- XDVDFS ----
// The game partition starts at one of a few offsets (0 for an image of the
// partition alone; then XGD2, XGD3 and original Xbox) [C: GH2's Rev1 image at
// 0xFD90000; the others I, as tools/xdvdfs.py]. Sector 32 of the partition:
// "MICROSOFT*XBOX*MEDIA", u32 root folder sector, u32 root folder size. A
// folder is a binary tree of 4-byte aligned entries (u16 left, u16 right in
// dwords, u32 sector, u32 size, u8 attributes (0x10 folder), u8 name length,
// name); 0xFFFF fills unused space [C: gd02 data/xdvdfs.cpp].
constexpr uint64_t kXboxPartitions[] = {0x0, 0xFD90000, 0x2080000, 0x18300000};

class Xdvdfs final : public DiscFileSystem {
public:
    Xdvdfs(std::shared_ptr<RandomAccessFile> image, uint64_t base, DiscEntry root)
        : DiscFileSystem(std::move(image)), base_(base), root_(std::move(root)) {}
    std::string_view format() const override { return "xdvdfs"; }

protected:
    DiscEntry root() const override { return root_; }
    bool entries(const DiscEntry& dir, std::vector<DiscEntry>& out) const override {
        if (dir.isDir && dir.size == 0) return true;  // an empty folder
        std::vector<uint8_t> buf;
        if (!readDir(*image_, dir, buf)) return false;
        // Every entry of the tree, not a search by its order: folders are
        // small, and this does not depend on the sort rule [C: gd02].
        std::vector<size_t> todo{0};
        std::vector<bool> seen(buf.size() / 4 + 1, false);
        while (!todo.empty()) {
            const size_t off = todo.back();
            todo.pop_back();
            if (off + 14 > buf.size() || seen[off / 4]) continue;
            seen[off / 4] = true;
            const uint8_t* e = &buf[off];
            const uint16_t left = le16(e), right = le16(e + 2);
            if (left == 0xFFFF && right == 0xFFFF) continue;
            const uint8_t nameLen = e[13];
            if (off + 14 + nameLen > buf.size()) continue;
            DiscEntry d;
            d.name.assign(reinterpret_cast<const char*>(e + 14), nameLen);
            d.isDir = (e[12] & 0x10) != 0;
            d.offset = base_ + uint64_t(le32(e + 4)) * kSector;
            d.size = le32(e + 8);
            out.push_back(std::move(d));
            if (left && left != 0xFFFF) todo.push_back(size_t(left) * 4);
            if (right && right != 0xFFFF) todo.push_back(size_t(right) * 4);
        }
        return true;
    }

private:
    uint64_t base_;
    DiscEntry root_;
};

}  // namespace

bool DiscFileSystem::list(std::string_view dir, std::vector<DiscEntry>& out) const {
    DiscEntry d;
    if (!find(dir, d) || !d.isDir) return false;
    out.clear();
    return entries(d, out);
}

bool DiscFileSystem::find(std::string_view path, DiscEntry& out) const {
    DiscEntry cur = root();
    std::vector<DiscEntry> here;
    size_t start = 0;
    while (start <= path.size()) {
        size_t slash = path.find('/', start);
        if (slash == std::string_view::npos) slash = path.size();
        const std::string_view part = path.substr(start, slash - start);
        start = slash + 1;
        if (part.empty()) continue;  // "", "/", "a//b"
        if (!cur.isDir) return false;
        here.clear();
        if (!entries(cur, here)) return false;
        auto it = std::find_if(here.begin(), here.end(), [&](const DiscEntry& e) { return sameName(e.name, part); });
        if (it == here.end()) return false;
        cur = *it;
    }
    out = cur;
    return true;
}

bool DiscFileSystem::read(const DiscEntry& file, uint64_t offset, void* dst, size_t n) const {
    if (offset > file.size || n > file.size - offset) return false;
    return image_->read(file.offset + offset, dst, n);
}

bool DiscFileSystem::readFile(const DiscEntry& file, std::vector<uint8_t>& out) const {
    if (file.isDir || file.size > uint64_t(SIZE_MAX / 2)) return false;
    out.resize(size_t(file.size));
    if (read(file, 0, out.data(), out.size())) return true;
    out.clear();
    return false;
}

std::unique_ptr<DiscFileSystem> openIso9660(std::shared_ptr<RandomAccessFile> image) {
    if (!image || image->size() < 17 * kSector) return nullptr;
    uint8_t pvd[kSector];
    if (!image->read(16 * kSector, pvd, sizeof pvd)) return nullptr;
    if (pvd[0] != 1 || std::memcmp(pvd + 1, "CD001", 5) != 0) return nullptr;
    const uint8_t* rec = pvd + 156;
    DiscEntry root;
    root.isDir = true;
    root.offset = uint64_t(le32(rec + 2)) * kSector;
    root.size = le32(rec + 10);
    std::string volume = trim(std::string(reinterpret_cast<const char*>(pvd + 40), 32));
    return std::make_unique<Iso9660>(std::move(image), std::move(root), std::move(volume));
}

std::unique_ptr<DiscFileSystem> openXdvdfs(std::shared_ptr<RandomAccessFile> image) {
    if (!image) return nullptr;
    for (uint64_t base : kXboxPartitions) {
        uint8_t vd[28];
        const uint64_t at = base + 32 * kSector;
        if (at + sizeof vd > image->size() || !image->read(at, vd, sizeof vd)) continue;
        if (std::memcmp(vd, "MICROSOFT*XBOX*MEDIA", 20) != 0) continue;
        DiscEntry root;
        root.isDir = true;
        root.offset = base + uint64_t(le32(vd + 20)) * kSector;
        root.size = le32(vd + 24);
        return std::make_unique<Xdvdfs>(std::move(image), base, std::move(root));
    }
    return nullptr;
}

std::unique_ptr<DiscFileSystem> openDisc(std::shared_ptr<RandomAccessFile> image, std::string* error) {
    if (!image) {
        if (error) *error = "no image";
        return nullptr;
    }
    if (auto fs = openXdvdfs(image)) return fs;
    if (auto fs = openIso9660(image)) return fs;
    if (error) *error = "not a disc image (no XDVDFS or ISO 9660 file system found)";
    return nullptr;
}

}  // namespace gd::core::platform
