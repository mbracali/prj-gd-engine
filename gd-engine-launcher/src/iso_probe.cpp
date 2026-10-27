// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "iso_probe.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return s;
}
std::string upper(std::string s) {
    for (char& c : s) c = char(c >= 'a' && c <= 'z' ? c - 32 : c);
    return s;
}
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]); }

class Reader {
public:
    explicit Reader(const std::string& path) : io_(SDL_IOFromFile(path.c_str(), "rb")) {}
    ~Reader() {
        if (io_) SDL_CloseIO(io_);
    }
    bool ok() const { return io_ != nullptr; }
    int64_t size() const { return io_ ? SDL_GetIOSize(io_) : 0; }
    bool read(int64_t offset, void* out, size_t n) {
        if (!io_ || offset < 0 || offset + int64_t(n) > size()) return false;
        if (SDL_SeekIO(io_, offset, SDL_IO_SEEK_SET) < 0) return false;
        return SDL_ReadIO(io_, out, n) == n;
    }
    std::vector<uint8_t> read(int64_t offset, size_t n) {
        std::vector<uint8_t> v(n);
        if (!read(offset, v.data(), n)) v.clear();
        return v;
    }

private:
    SDL_IOStream* io_;
};

constexpr int64_t kSector = 2048;

// ISO 9660: the primary volume descriptor at sector 16, the root folder's
// records, SYSTEM.CNF's BOOT2 line (PlayStation 2).
bool probeIso9660(Reader& r, DiscInfo& d) {
    std::vector<uint8_t> pvd = r.read(16 * kSector, size_t(kSector));
    if (pvd.empty() || pvd[0] != 1 || std::memcmp(&pvd[1], "CD001", 5) != 0) return false;
    d.format = "iso9660";
    d.volumeId = trim(std::string(reinterpret_cast<const char*>(&pvd[40]), 32));
    const uint8_t* root = &pvd[156];
    const uint32_t lba = le32(root + 2), size = std::min<uint32_t>(le32(root + 10), 1u << 20);
    std::vector<uint8_t> dir = r.read(int64_t(lba) * kSector, size);
    uint32_t cnfLba = 0, cnfSize = 0;
    for (size_t i = 0; i < dir.size();) {
        const uint8_t len = dir[i];
        if (len == 0) {  // records do not cross sectors: on to the next one
            i = (i / size_t(kSector) + 1) * size_t(kSector);
            continue;
        }
        if (i + 33 > dir.size() || i + len > dir.size()) break;
        const uint8_t nameLen = dir[i + 32];
        std::string name(reinterpret_cast<const char*>(&dir[i + 33]), std::min<size_t>(nameLen, len - 33u));
        if (!(nameLen == 1 && (name[0] == 0 || name[0] == 1))) {
            if (size_t semi = name.find(';'); semi != std::string::npos) name.erase(semi);
            d.files.push_back(lower(name));
            if (lower(name) == "system.cnf") cnfLba = le32(&dir[i + 2]), cnfSize = le32(&dir[i + 10]);
        }
        i += len;
    }
    if (cnfLba && cnfSize && cnfSize < 4096) {
        std::vector<uint8_t> cnf = r.read(int64_t(cnfLba) * kSector, cnfSize);
        std::string text(cnf.begin(), cnf.end());
        if (size_t at = text.find("BOOT2"); at != std::string::npos) {
            std::string line = text.substr(at, text.find_first_of("\r\n", at) - at);
            size_t start = line.find_last_of(":\\/");
            std::string exe = start == std::string::npos ? std::string() : line.substr(start + 1);
            if (size_t semi = exe.find(';'); semi != std::string::npos) exe.erase(semi);
            d.ps2Boot = upper(trim(exe));
        }
    }
    return true;
}

// XDVDFS: "MICROSOFT*XBOX*MEDIA" at sector 32 of the game partition (plain
// images start with it; Xbox 360 rips have it at their XGD offset); the root
// folder is a binary tree of entries; default.xex's execution info header
// (key 0x00040006) holds the title ID.
bool probeXdvdfs(Reader& r, DiscInfo& d) {
    static const int64_t kPartitions[] = {0, 0xFD90000, 0x2080000, 0x18300000};
    const char kMagic[] = "MICROSOFT*XBOX*MEDIA";
    for (int64_t base : kPartitions) {
        std::vector<uint8_t> vd = r.read(base + 32 * kSector, 28);
        if (vd.empty() || std::memcmp(vd.data(), kMagic, 20) != 0) continue;
        d.format = "xdvdfs";
        const uint32_t rootSector = le32(&vd[20]), rootSize = std::min<uint32_t>(le32(&vd[24]), 1u << 20);
        std::vector<uint8_t> tree = r.read(base + int64_t(rootSector) * kSector, rootSize);
        uint32_t xexSector = 0, xexSize = 0;
        std::vector<size_t> todo{0};
        std::vector<bool> seen(tree.size() / 4 + 1, false);
        while (!todo.empty()) {
            size_t off = todo.back() * 4;
            todo.pop_back();
            if (off + 14 > tree.size() || seen[off / 4]) continue;
            seen[off / 4] = true;
            const uint16_t left = le16(&tree[off]), right = le16(&tree[off + 2]);
            if (left == 0xFFFF) continue;  // padding
            const uint8_t nameLen = tree[off + 13];
            if (off + 14 + nameLen > tree.size()) continue;
            std::string name = lower(std::string(reinterpret_cast<const char*>(&tree[off + 14]), nameLen));
            d.files.push_back(name);
            if (name == "default.xex") xexSector = le32(&tree[off + 4]), xexSize = le32(&tree[off + 8]);
            if (left) todo.push_back(left);
            if (right) todo.push_back(right);
        }
        if (xexSector && xexSize >= 24) {
            std::vector<uint8_t> x = r.read(base + int64_t(xexSector) * kSector, std::min<size_t>(xexSize, 64 * 1024));
            if (x.size() >= 24 && std::memcmp(x.data(), "XEX2", 4) == 0) {
                const uint32_t count = be32(&x[0x14]);
                for (uint32_t k = 0; k < count && 0x18 + size_t(k) * 8 + 8 <= x.size(); ++k) {
                    const uint32_t key = be32(&x[0x18 + k * 8]), value = be32(&x[0x18 + k * 8 + 4]);
                    if (key == 0x00040006 && size_t(value) + 16 <= x.size()) {
                        char id[9];
                        SDL_snprintf(id, sizeof id, "%08X", be32(&x[value + 12]));
                        d.xexTitleId = id;
                    }
                }
            }
        }
        return true;
    }
    return false;
}

}  // namespace

std::string DiscInfo::describe() const {
    if (format == "xdvdfs") return xexTitleId.empty() ? "Xbox disc" : "Xbox 360 disc, title ID " + xexTitleId;
    if (format == "iso9660") {
        if (!ps2Boot.empty()) return "PlayStation 2 disc, " + ps2Boot;
        return volumeId.empty() ? "ISO 9660 disc" : "ISO 9660 disc \"" + volumeId + "\"";
    }
    return error.empty() ? "not a disc image" : error;
}

bool IsoSignature::matches(const DiscInfo& d) const {
    if (format.empty() || d.format != format) return false;
    if (!ps2Boot.empty() && upper(ps2Boot) != d.ps2Boot) return false;
    if (!volumeId.empty() && volumeId != d.volumeId) return false;
    if (!xexTitleId.empty() && upper(xexTitleId) != d.xexTitleId) return false;
    for (const std::string& f : files)
        if (std::find(d.files.begin(), d.files.end(), lower(f)) == d.files.end()) return false;
    // A signature must check something beyond the file system.
    return !ps2Boot.empty() || !volumeId.empty() || !xexTitleId.empty() || !files.empty();
}

DiscInfo probeDisc(const std::string& path) {
    DiscInfo d;
    Reader r(path);
    if (!r.ok()) {
        d.error = SDL_GetError();
        return d;
    }
    if (r.size() < 64 * kSector) return d;
    if (probeXdvdfs(r, d)) return d;
    probeIso9660(r, d);
    return d;
}
